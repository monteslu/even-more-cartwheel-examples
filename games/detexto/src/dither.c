/* The 1-bit looks (dither.h). */
#include "dither.h"
#include "game.h"
#include "opts.h"
#include <cartwheel/debug.h>
#include <cartwheel/gpu.h>
#include <cartwheel/pipeline.h>
#include <cartwheel/quality.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BN 64 /* the blue noise matrix's side */

static struct {
  cw_tree *tree;
  bool ready;
  int look, level;
  unsigned frames;
  unsigned bayer_tex, blue_tex; /* RGBA8: the threshold's high byte in r, low byte in g */
  unsigned prog_tone, prog_quant;
  /* debug fields */
  int dbg_look, dbg_w, dbg_h;
  float dbg_ms, dbg_world, dbg_block;
} D = { .look = LOOK_DITHER, .level = -1 };

static float optf(const char *name, float def) {
  const char *s = opt_get(name);
  return s ? (float)atof(s) : def;
}

/* ── the threshold matrices ──────────────────────────────────────────── */

static void put16(unsigned char *t, int i, unsigned v) { t[i * 4] = (unsigned char)(v >> 8), t[i * 4 + 1] = (unsigned char)v, t[i * 4 + 2] = 0, t[i * 4 + 3] = 255; }

/* 8x8 ordered matrix: M = bit reverse of the interleave of (x ^ y) and y; thresholds (rank + 0.5) / 64 */
static void make_bayer(void) {
  unsigned char t[64 * 4];
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      int a = x ^ y, r = 0;
      r = ((a & 1) << 5) | ((y & 1) << 4) | ((a & 2) << 2) | ((y & 2) << 1) | ((a & 4) >> 1) | ((y & 4) >> 2);
      put16(t, y * 8 + x, (unsigned)((r * 2 + 1) * 65536 / 128));
    }
  D.bayer_tex = cw_pass_texture_new(8, 8, CW_RT_RGBA8, t);
}

/* BN x BN blue noise by void and cluster (Ulichney): the pixels ranked by putting each next one in the largest
 * void (the empty pixel with the least Gaussian energy, sigma 1.5, the matrix wrapping round), from an empty matrix.
 * Past half full this is the same as removing the tightest cluster of the empty pixels (their energy is the total
 * minus the filled ones'), so one loop ranks all of them. About 4096 x 4096 steps: a few ms at start. */
static void make_blue(void) {
  enum { N = BN * BN };
  float *lut = malloc(sizeof(float) * N), *e = calloc(N, sizeof(float));
  unsigned char *on = calloc(N, 1);
  unsigned char *t = malloc((size_t)N * 4);
  for (int y = 0; y < BN; y++)
    for (int x = 0; x < BN; x++) {
      int dx = x > BN / 2 ? BN - x : x, dy = y > BN / 2 ? BN - y : y;
      lut[y * BN + x] = expf(-(float)(dx * dx + dy * dy) / (2.0f * 1.5f * 1.5f));
    }
  /* a fixed, spread start: the energy ties are broken by a small hash so the first points do not line up */
  for (int i = 0; i < N; i++) {
    uint32_t h = (uint32_t)i * 2654435761u;
    h ^= h >> 15, h *= 2246822519u, h ^= h >> 13;
    e[i] = (float)(h & 0xffff) * 1e-9f;
  }
  for (int rank = 0; rank < N; rank++) {
    int best = -1;
    float be = 1e30f;
    for (int i = 0; i < N; i++)
      if (!on[i] && e[i] < be) be = e[i], best = i;
    on[best] = 1;
    put16(t, best, (unsigned)(((uint64_t)rank * 2 + 1) * 65536 / (2 * N)));
    int bx = best % BN, by = best / BN;
    for (int y = 0; y < BN; y++) {
      int dy = (y - by + BN) % BN;
      for (int x = 0; x < BN; x++) e[y * BN + x] += lut[dy * BN + (x - bx + BN) % BN];
    }
  }
  D.blue_tex = cw_pass_texture_new(BN, BN, CW_RT_RGBA8, t);
  free(lut), free(e), free(on), free(t);
}

/* ── the shaders ─────────────────────────────────────────────────────── */

/* tone: u_tone gamma, contrast, bias, sharpen; u_edge outline gain, from, to (rel. depth curvature); u_lin near, far,
 * ortho (GL). Writes the tone value (0..1, before the threshold) in r. */
static const char *TONE_GLSL =
    "uniform sampler2D u_src; uniform sampler2D u_depth; uniform vec4 u_tone; uniform vec4 u_edge; uniform vec4 u_lin; uniform vec4 u_key;\n"
    "out vec4 o;\n"
    "float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
    "/* the tone a pixel starts from: the grey world by its luma (x world gain), the landmark's neon hues by their value\n"
    " * (x block gain), so a saturated block reads bright however dark its hue's luma is */\n"
    "float L(ivec2 q) {\n"
    "  q = clamp(q, ivec2(0), textureSize(u_src, 0) - 1); vec3 c = clamp(texelFetch(u_src, q, 0).rgb, 0.0, 1.0);\n"
    "  float mx = max(max(c.r, c.g), c.b), mn = min(min(c.r, c.g), c.b);\n"
    "  float key = smoothstep(0.22, 0.5, (mx - mn) / max(mx, 1e-4));\n"
    "  float l = lum(c);\n"
    "  return clamp(mix(mix(l * u_key.x, l, smoothstep(0.6, 0.95, l)), mx * u_key.y, key), 0.0, 1.0); /* the world's highlights (a blast's white core) kept */\n"
    "}\n"
    "float W(ivec2 q) {\n"
    "  q = clamp(q, ivec2(0), textureSize(u_depth, 0) - 1);\n"
    "  float d = texelFetch(u_depth, q, 0).r;\n"
    "  if (d >= 0.999999) return 0.0;\n"
    "  float z = u_lin.z > 0.5 ? u_lin.x + d * (u_lin.y - u_lin.x) : u_lin.x * u_lin.y / (u_lin.y - d * (u_lin.y - u_lin.x));\n"
    "  return 1.0 / max(z, 1e-4);\n"
    "}\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "  float c = L(p);\n"
    "  float b = (L(p + ivec2(2, 0)) + L(p - ivec2(2, 0)) + L(p + ivec2(0, 2)) + L(p - ivec2(0, 2))) * 0.15\n"
    "          + (L(p + ivec2(1, 1)) + L(p - ivec2(1, 1)) + L(p + ivec2(1, -1)) + L(p - ivec2(1, -1))) * 0.1;\n"
    "  float v = clamp(c + u_tone.w * (c - b), 0.0, 1.0);\n"
    "  v = pow(v, u_tone.x);\n"
    "  v = clamp((v - 0.5) * u_tone.y + 0.5 + u_tone.z, 0.0, 1.0);\n"
    "  float w = W(p);\n"
    "  if (u_edge.x > 0.0 && w > 0.0) {\n"
    "    float lap = 4.0 * w - W(p + ivec2(1, 0)) - W(p - ivec2(1, 0)) - W(p + ivec2(0, 1)) - W(p - ivec2(0, 1));\n"
    "    float e = smoothstep(u_edge.y, u_edge.z, lap / w);\n"
    "    v = max(v, e * u_edge.x);\n"
    "  }\n"
    "  o = vec4(v, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char *TONE_WGSL =
    "struct U { u_tone: vec4f, u_edge: vec4f, u_lin: vec4f, u_key: vec4f };\n"
    "@group(0) @binding(0) var<uniform> u: U;\n"
    "@group(0) @binding(1) var u_src: texture_2d<f32>;\n"
    "@group(0) @binding(2) var u_depth: texture_2d<f32>;\n"
    "fn lum(c: vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }\n"
    "fn L(q0: vec2i) -> f32 {\n"
    "  let q = clamp(q0, vec2i(0), vec2i(textureDimensions(u_src)) - 1); let c = clamp(textureLoad(u_src, q, 0).rgb, vec3f(0.0), vec3f(1.0));\n"
    "  let mx = max(max(c.r, c.g), c.b); let mn = min(min(c.r, c.g), c.b);\n"
    "  let key = smoothstep(0.22, 0.5, (mx - mn) / max(mx, 1e-4));\n"
    "  let l = lum(c);\n"
    "  return clamp(mix(mix(l * u.u_key.x, l, smoothstep(0.6, 0.95, l)), mx * u.u_key.y, key), 0.0, 1.0);\n"
    "}\n"
    "fn W(q0: vec2i) -> f32 {\n"
    "  let q = clamp(q0, vec2i(0), vec2i(textureDimensions(u_depth)) - 1);\n"
    "  let z = textureLoad(u_depth, q, 0).r;\n"
    "  if (z > 1e30) { return 0.0; }\n"
    "  return 1.0 / max(z, 1e-4);\n"
    "}\n"
    "@fragment fn fs(@builtin(position) pos: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {\n"
    "  let p = vec2i(cw_frag_coord(pos));\n"
    "  let c = L(p);\n"
    "  let b = (L(p + vec2i(2, 0)) + L(p - vec2i(2, 0)) + L(p + vec2i(0, 2)) + L(p - vec2i(0, 2))) * 0.15\n"
    "        + (L(p + vec2i(1, 1)) + L(p - vec2i(1, 1)) + L(p + vec2i(1, -1)) + L(p - vec2i(1, -1))) * 0.1;\n"
    "  var v = clamp(c + u.u_tone.w * (c - b), 0.0, 1.0);\n"
    "  v = pow(v, u.u_tone.x);\n"
    "  v = clamp((v - 0.5) * u.u_tone.y + 0.5 + u.u_tone.z, 0.0, 1.0);\n"
    "  let w = W(p);\n"
    "  if (u.u_edge.x > 0.0 && w > 0.0) {\n"
    "    let lap = 4.0 * w - W(p + vec2i(1, 0)) - W(p - vec2i(1, 0)) - W(p + vec2i(0, 1)) - W(p - vec2i(0, 1));\n"
    "    let e = smoothstep(u.u_edge.y, u.u_edge.z, lap / w);\n"
    "    v = max(v, e * u.u_edge.x);\n"
    "  }\n"
    "  return vec4f(v, 0.0, 0.0, 1.0);\n"
    "}\n";

/* quantize: u_q.x the threshold matrix's side, u_q.yz its offset in pixels (0: anchored to the screen; the gate's
 * crawl control moves it every frame); u_ink, u_paper */
static const char *QUANT_GLSL =
    "uniform sampler2D u_tonetex; uniform sampler2D u_thr; uniform vec4 u_q; uniform vec4 u_ink; uniform vec4 u_paper;\n"
    "out vec4 o;\n"
    "float T(ivec2 q) { int n = int(u_q.x); vec2 t = texelFetch(u_thr, ivec2(q.x - (q.x / n) * n, q.y - (q.y / n) * n), 0).rg; return (t.r * 65280.0 + t.g * 255.0) / 65536.0; }\n"
    "float V(ivec2 q) { q = clamp(q, ivec2(0), textureSize(u_tonetex, 0) - 1); return texelFetch(u_tonetex, q, 0).r; }\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "  float v = V(p);\n"
    "  float b = v > T(p + ivec2(u_q.yz)) ? 1.0 : 0.0;\n"
    "  o = vec4(mix(u_paper.rgb, u_ink.rgb, b), 1.0);\n"
    "}\n";

static const char *QUANT_WGSL =
    "struct U { u_q: vec4f, u_ink: vec4f, u_paper: vec4f };\n"
    "@group(0) @binding(0) var<uniform> u: U;\n"
    "@group(0) @binding(1) var u_tonetex: texture_2d<f32>;\n"
    "@group(0) @binding(2) var u_thr: texture_2d<f32>;\n"
    "fn T(q: vec2i) -> f32 { let n = i32(u.u_q.x); let t = textureLoad(u_thr, vec2i(q.x - (q.x / n) * n, q.y - (q.y / n) * n), 0).rg; return (t.r * 65280.0 + t.g * 255.0) / 65536.0; }\n"
    "fn V(q0: vec2i) -> f32 { let q = clamp(q0, vec2i(0), vec2i(textureDimensions(u_tonetex)) - 1); return textureLoad(u_tonetex, q, 0).r; }\n"
    "@fragment fn fs(@builtin(position) pos: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {\n"
    "  let p = vec2i(cw_frag_coord(pos));\n"
    "  let v = V(p);\n"
    "  let b = select(0.0, 1.0, v > T(p + vec2i(u.u_q.yz)));\n"
    "  return vec4f(mix(u.u_paper.rgb, u.u_ink.rgb, b), 1.0);\n"
    "}\n";

/* ── the pass ────────────────────────────────────────────────────────── */

const char *const LOOK_NAMES[LOOK_COUNT] = { "Dither", "Dither warm", "Dither grid", "ASCII" };

cw_color dither_ink(void) { return D.look == LOOK_WARM ? (cw_color){ 1.0f, 0.9f, 0.74f, 1 } : (cw_color){ 1, 1, 1, 1 }; }
cw_color dither_paper(void) { return D.look == LOOK_WARM ? (cw_color){ 0.035f, 0.03f, 0.025f, 1 } : (cw_color){ 0, 0, 0, 1 }; }

static bool dither_active(const cw_pass_ctx *ctx, void *u) {
  (void)ctx, (void)u;
  return D.ready && D.look != LOOK_ASCII && !opt_on("DITHER_OFF"); /* (DITHER_OFF: the plain picture, a gate's control) */
}

static void programs(void) {
  if (D.prog_tone) return;
  bool wg = cw_gpu_backend() == CW_GPU_WEBGPU;
  D.prog_tone = wg ? cw_pass_program_wgsl("dt_dither_tone", TONE_WGSL) : cw_pass_program("dt_dither_tone", TONE_GLSL);
  D.prog_quant = wg ? cw_pass_program_wgsl("dt_dither_quant", QUANT_WGSL) : cw_pass_program("dt_dither_quant", QUANT_GLSL);
}

/* a level's tone: the world's gain, the landmark's gain and the bias, times / plus the run options' (1, 1, 0 for most) */
static void level_tone(float *world, float *block, float *bias) {
  *world = 1, *block = 1, *bias = 0;
  if (D.level < 0 || D.level >= NLEVELS) return;
  if (level_setting(&LEVELS[D.level]) == ST_DESERT) /* pale sand round a landmark the sun lights from behind */
    *world = optf("DITHER_DESERT_WORLD", 0.62f), *block = optf("DITHER_DESERT_BLOCK", 1.45f);
}

static void dither_exec(cw_pass_ctx *ctx, void *u) {
  (void)u;
  bool wg = cw_gpu_backend() == CW_GPU_WEBGPU;
  programs();
  if (!D.prog_tone || !D.prog_quant) return;
  D.dbg_w = ctx->width, D.dbg_h = ctx->height;
  const cw_rt *tone = cw_pass_rt(ctx, ctx->width, ctx->height, CW_RT_R16F, false);
  float lin[3], wk, bk, bb;
  level_tone(&wk, &bk, &bb);
  D.dbg_world = optf("DITHER_WORLD", 0.8f) * wk, D.dbg_block = optf("DITHER_BLOCK", 1.15f) * bk;
  cw_pass_linear_depth_params(ctx, lin);
  cw_pass_bind_target(ctx, tone);
  cw_pass_use(D.prog_tone);
  cw_pass_bind_texture(0, ctx->src->tex);
  cw_pass_bind_texture(1, ctx->depth_tex);
  if (!wg) cw_pass_uniform1i(D.prog_tone, "u_src", 0), cw_pass_uniform1i(D.prog_tone, "u_depth", 1);
  cw_pass_uniform4f(D.prog_tone, "u_tone", optf("DITHER_GAMMA", 1.3f), optf("DITHER_CONTRAST", 1.3f), optf("DITHER_BIAS", 0.0f) + bb,
                    optf("DITHER_SHARPEN", 0.9f));
  cw_pass_uniform4f(D.prog_tone, "u_edge", optf("DITHER_OUTLINE", 1.0f), optf("DITHER_OUTLINE_FROM", 0.02f), optf("DITHER_OUTLINE_TO", 0.08f), 0);
  cw_pass_uniform4f(D.prog_tone, "u_lin", lin[0], lin[1], lin[2], 0);
  cw_pass_uniform4f(D.prog_tone, "u_key", D.dbg_world, D.dbg_block, 0, 0);
  cw_pass_fullscreen();

  cw_pass_bind_target(ctx, ctx->dst);
  cw_pass_use(D.prog_quant);
  cw_pass_bind_texture(0, tone->tex);
  bool grid = D.look == LOOK_GRID;
  cw_pass_bind_texture(1, grid ? D.bayer_tex : D.blue_tex);
  if (!wg) cw_pass_uniform1i(D.prog_quant, "u_tonetex", 0), cw_pass_uniform1i(D.prog_quant, "u_thr", 1);
  /* DITHER_CRAWL 1, the no-crawl gate's control: the threshold moved every frame, as a dither tied to time would */
  float off = opt_on("DITHER_CRAWL") ? (float)((D.frames * 37) % 64) : 0;
  D.frames++;
  cw_pass_uniform4f(D.prog_quant, "u_q", grid ? 8.0f : (float)BN, off, off * 0.5f, 0);
  cw_color ink = dither_ink(), paper = dither_paper();
  cw_pass_uniform4f(D.prog_quant, "u_ink", ink.r, ink.g, ink.b, 1);
  cw_pass_uniform4f(D.prog_quant, "u_paper", paper.r, paper.g, paper.b, 1);
  cw_pass_fullscreen();
  cw_pass_rt_release(ctx, tone);
  /* this pass's GPU time, as the pipeline last measured it (dt.dither.gpu_ms; -1: no timers on this host) */
  const cw_pass_stat *st;
  int n = cw_pipeline_pass_stats(ctx->pipeline, &st);
  D.dbg_ms = -1;
  for (int i = 0; i < n; i++)
    if (st[i].name && !strcmp(st[i].name, "dither")) D.dbg_ms = st[i].gpu_ms_avg;
}

/* a LOOK run option: a name (the old trial's names too) or a number */
static int parse_look(const char *s) {
  if (!strcmp(s, "dither") || !strcmp(s, "blue")) return LOOK_DITHER;
  if (!strcmp(s, "warm")) return LOOK_WARM;
  if (!strcmp(s, "grid") || !strcmp(s, "bayer")) return LOOK_GRID;
  if (!strcmp(s, "ascii")) return LOOK_ASCII;
  int k = atoi(s);
  return k >= 0 && k < LOOK_COUNT ? k : LOOK_DITHER;
}

void dither_init(cw_tree *tree) {
  if (D.tree) return;
  D.tree = tree;
  make_bayer();
  make_blue();
  cw_pipeline *p = cw_pipeline_of(tree);
  cw_pipeline_add_pass(p, &(cw_pass_desc){ .name = "dither", .phase = CW_PHASE_POST, .order = 9000, .reads = CW_RES_COLOR | CW_RES_DEPTH,
                                           .writes = CW_RES_COLOR, .active = dither_active, .execute = dither_exec, .webgpu = true });
  D.ready = true;
  dither_option_changed(NULL);
  cw_debug_i32("dt.dither.look", &D.dbg_look);
  cw_debug_i32("dt.dither.w", &D.dbg_w);
  cw_debug_i32("dt.dither.h", &D.dbg_h);
  cw_debug_f32("dt.dither.gpu_ms", &D.dbg_ms);
  cw_debug_f32("dt.dither.world", &D.dbg_world);
  cw_debug_f32("dt.dither.block", &D.dbg_block);
}

int *dither_look_value(void) { return &D.look; }
int dither_look(void) { return D.look; }
void dither_set_look(int look) { D.look = look >= 0 && look < LOOK_COUNT ? look : LOOK_DITHER, D.dbg_look = D.look; }
bool dither_mono(void) { return D.look != LOOK_ASCII; }
void dither_set_level(int level) { D.level = level; }

void dither_option_changed(const char *name) {
  if ((!name || !strcmp(name, "LOOK")) && opt_get("LOOK")) dither_set_look(parse_look(opt_get("LOOK")));
}

cw_color dither_hud_color(cw_color c) {
  if (!dither_mono()) return c;
  float l = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
  cw_color k = l >= 0.33f ? dither_ink() : dither_paper();
  k.a = c.a;
  return k;
}
