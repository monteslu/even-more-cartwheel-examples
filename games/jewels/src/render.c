/* The picture (render.h). Shader code is written twice, GLSL ES 3.00 and WGSL, line for line the same math; the
 * parts that depend on data (the studio's lights, each cut's facet planes) are generated from the same C tables
 * for both. */
#include "render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/gpu.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/quality.h"
#include "cartwheel/scene.h"
#include "cartwheel/scene_file.h"
#include "cuts.h"
#include "game.h"
#include "shaders.h"

#define CAM_X (-288.0f) /* above the board's centre */
#define CAM_Y 0.0f
#define CAM_D 4200.0f

static struct {
  jw_frame_fn fn;
  void *user;
  jw_frame f;
  bool have_frame;
  uint64_t frame_no;
  bool wg;
  bool tray_dirty;
  unsigned present, tray, gem[JW_NCUTS], under, spark, ring, mark, glint[5], light, grab, shock;
  bool built, failed;
  jw_render_stats st;
  unsigned caustic_tex, dummy_tex, studio_tex;
  float *caustic; /* the bake, RGBA float, 6 kinds side by side */
  int bake_kind, bake_row, studio_row;
  float *studio; /* the stones' studio map (shaders.c), baked in rows */
} R;

#define CAUSTIC_N 128
#define CAUSTIC_SPAN 2.6f /* the bake covers +-2.6 of a stone's local units around its centre */

const jw_render_stats *jw_render_get_stats(void) { return &R.st; }

void jw_world_to_canvas(float x, float y, float z, float *px, float *py) {
  float k = CAM_D / (CAM_D - z);
  *px = (CAM_X + (x - CAM_X) * k) + 960.0f;
  *py = 540.0f - (CAM_Y + (y - CAM_Y) * k);
}

static bool fx_off(const char *part);

/* ── quality: how the stones are traced ─────────────────────────────── */

typedef struct gem_q { int sub, bounces, waves; bool glint, caustics; } gem_q;
static gem_q quality_now(void) {
  int q = cw_gfx_effective(CW_GFX_EFFECTS);
  static const gem_q table[] = {
    { 1, 3, 1, false, false }, /* Very Low: no fire, three bounces, no glints */
    { 1, 4, 3, true, true },   /* Low */
    { 1, 4, 4, true, true },   /* Medium */
    { 1, 5, 6, true, true },   /* High (with the pipeline's temporal resolve) */
    { 4, 8, 8, true, true },   /* Ultra */
  };
  if (q < 0) q = 0;
  if (q > 4) q = 4;
  R.st.quality = q;
  gem_q r = table[q];
  if (getenv("JEWELS_SUB")) r.sub = atoi(getenv("JEWELS_SUB")); /* development: look and cost experiments */
  if (getenv("JEWELS_BOUNCES")) r.bounces = atoi(getenv("JEWELS_BOUNCES"));
  return r;
}

/* ── matrices ────────────────────────────────────────────────────────── */

typedef struct { float m[16]; } mat4; /* column-major */
static mat4 m_ident(void) { mat4 r = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } }; return r; }
static mat4 m_mul(mat4 a, mat4 b) {
  mat4 r;
  for (int c = 0; c < 4; c++)
    for (int rr = 0; rr < 4; rr++) {
      float s = 0;
      for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
      r.m[c * 4 + rr] = s;
    }
  return r;
}
static mat4 m_rot(float ax, float ay, float az) { /* Rz * Ry * Rx */
  float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
  mat4 X = m_ident(), Y = m_ident(), Z = m_ident();
  X.m[5] = cx, X.m[6] = sx, X.m[9] = -sx, X.m[10] = cx;
  Y.m[0] = cy, Y.m[2] = -sy, Y.m[8] = sy, Y.m[10] = cy;
  Z.m[0] = cz, Z.m[1] = sz, Z.m[4] = -sz, Z.m[5] = cz;
  return m_mul(Z, m_mul(Y, X));
}
static mat4 m_transpose3(mat4 a) {
  mat4 r = m_ident();
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) r.m[j * 4 + i] = a.m[i * 4 + j];
  return r;
}
static void set_mat(unsigned p, const char *name, mat4 m) { cw_pass_uniform_mat4(p, name, (const t3_mat4 *)&m); }

/* ── the common uniforms ─────────────────────────────────────────────── */

static float room_drift(void) { return 0.05f * sinf((float)R.f.time * 0.11f) + 0.025f * sinf((float)R.f.time * 0.047f + 1.3f); }

static void common_uniforms(unsigned p, const cw_pass_ctx *ctx) {
  cw_pass_uniform4f(p, "u_view", (float)ctx->width, (float)ctx->height, 1.0f / (float)ctx->width, 1.0f / (float)ctx->height);
  cw_pass_uniform4f(p, "u_cam", CAM_X, CAM_Y, CAM_D, room_drift());
  /* the room warms with a cascade: the key light goes from neutral to amber */
  float w = R.f.warmth, k = R.f.room > 0 ? R.f.room : 1; /* and it brightens a little with each step */
  cw_pass_uniform4f(p, "u_tint", (1.0f + 0.10f * w) * k, (1.0f - 0.04f * w) * k, (1.0f - 0.22f * w) * k, R.f.sweep);
}

/* ── the tray ────────────────────────────────────────────────────────── */

static bool build(void) {
  if (R.built) return !R.failed;
  R.built = true;
  R.wg = cw_gpu_backend() == CW_GPU_WEBGPU;
  char *src;
  src = jw_shader_tray(R.wg);
  R.tray = R.wg ? cw_pass_program_wgsl("jw_tray", src) : cw_pass_program("jw_tray", src);
  free(src);
  src = jw_shader_present(R.wg);
  R.present = R.wg ? cw_pass_program_wgsl("jw_present", src) : cw_pass_program("jw_present", src);
  free(src);
  for (int k = 0; k < JW_NCUTS; k++) {
    char name[32];
    snprintf(name, sizeof name, "jw_gem%d", k);
    char *vs = NULL;
    src = jw_shader_gem(R.wg, &jw_cuts[k], &vs);
    R.gem[k] = R.wg ? cw_pass_program_wgsl(name, src) : cw_pass_program_vs(name, vs, src);
    free(src), free(vs);
  }
  char *vs = NULL;
  src = jw_shader_under(R.wg, &vs);
  R.under = R.wg ? cw_pass_program_wgsl("jw_under", src) : cw_pass_program_vs("jw_under", vs, src);
  free(src), free(vs), vs = NULL;
  src = jw_shader_spark(R.wg, &vs);
  R.spark = R.wg ? cw_pass_program_wgsl("jw_spark", src) : cw_pass_program_vs("jw_spark", vs, src);
  free(src), free(vs), vs = NULL;
  src = jw_shader_mark(R.wg, &vs);
  R.mark = R.wg ? cw_pass_program_wgsl("jw_mark", src) : cw_pass_program_vs("jw_mark", vs, src);
  free(src), free(vs), vs = NULL;
  for (int i = 0; i < 5; i++) {
    vs = NULL;
    src = jw_shader_glint(R.wg, i, &vs);
    static const char *names[] = { "jw_glint_down", "jw_glint_block", "jw_glint_tile", "jw_glint_apart", "jw_glint_star" };
    R.glint[i] = R.wg ? cw_pass_program_wgsl(names[i], src) : vs ? cw_pass_program_vs(names[i], vs, src) : cw_pass_program(names[i], src);
    free(vs), vs = NULL;
    free(src);
  }
  vs = NULL;
  src = jw_shader_light(R.wg, &vs);
  R.light = R.wg ? cw_pass_program_wgsl("jw_light", src) : cw_pass_program_vs("jw_light", vs, src);
  free(src), free(vs), vs = NULL;
  src = jw_shader_grab(R.wg);
  R.grab = R.wg ? cw_pass_program_wgsl("jw_grab", src) : cw_pass_program("jw_grab", src);
  free(src);
  src = jw_shader_shock(R.wg, &vs);
  R.shock = R.wg ? cw_pass_program_wgsl("jw_shock", src) : cw_pass_program_vs("jw_shock", vs, src);
  free(src), free(vs), vs = NULL;
  R.failed = !R.light || !R.grab || !R.shock || !R.present || !R.tray || !R.under || !R.spark || !R.mark || !R.glint[0] || !R.glint[1] || !R.glint[2] || !R.glint[3] || !R.glint[4];
  for (int k = 0; k < JW_NCUTS; k++) R.failed |= !R.gem[k];
  if (R.failed) cw_error("jewels: a shader failed to build (see above)");
  return !R.failed;
}

static void need_frame(const cw_pass_ctx *ctx) {
  if (R.frame_no == ctx->frame + 1 && R.have_frame) return;
  memset(&R.f, 0, sizeof R.f);
  R.f.sweep = -1;
  if (R.fn) R.fn(&R.f, ctx->alpha, R.user);
  R.have_frame = true;
  R.frame_no = ctx->frame + 1;
}

static void tray_exec(cw_pass_ctx *ctx, void *u) {
  (void)u;
  need_frame(ctx);
  if (!build()) return;
  /* the tray itself never changes: drawn once into a target of its own (again when the size changes), then
   * copied each frame with its light (the room's warmth, the flashes, the button's glow) laid over it */
  bool fresh = false;
  const cw_rt *rt = cw_pipeline_rt_persistent(ctx->pipeline, "jw.tray", ctx->width, ctx->height, CW_RT_RGBA16F, &fresh);
  if (!rt) return;
  if (fresh || R.tray_dirty) {
    cw_pass_bind_target(ctx, rt);
    cw_pass_use(R.tray);
    common_uniforms(R.tray, ctx);
    cw_pass_uniform4f(R.tray, "u_cam", CAM_X, CAM_Y, CAM_D, 0);
    cw_pass_uniform4f(R.tray, "u_tint", 1, 1, 1, 0);
    for (int i = 0; i < 4; i++) {
      static const char *pos[4] = { "u_fl0", "u_fl1", "u_fl2", "u_fl3" }, *col[4] = { "u_fc0", "u_fc1", "u_fc2", "u_fc3" };
      cw_pass_uniform4f(R.tray, pos[i], 0, 0, 100, 0);
      cw_pass_uniform4f(R.tray, col[i], 0, 0, 0, 0);
    }
    cw_pass_uniform4f(R.tray, "u_misc", 0, 0, 0, 0);
    cw_pass_fullscreen();
    cw_pass_bind_target(ctx, ctx->scene);
    R.tray_dirty = false;
    R.st.tray_draws++;
  }
  /* the lights of the moment, added up at a quarter of the canvas */
  int lw = ctx->width / 4 > 1 ? ctx->width / 4 : 1, lh = ctx->height / 4 > 1 ? ctx->height / 4 : 1;
  const cw_rt *lb = cw_pass_rt(ctx, lw, lh, CW_RT_RGBA16F, false);
  if (!lb) return;
  cw_pass_bind_target(ctx, lb);
  cw_pass_clear(0, 0, 0, 0, false);
  R.st.lights = R.f.nlights;
  if (R.f.nlights) {
    cw_pass_set_blend(CW_PASS_BLEND_ADD);
    cw_pass_use(R.light);
    cw_pass_uniform4f(R.light, "u_view", (float)lw, (float)lh, 1.0f / (float)lw, 1.0f / (float)lh);
    cw_pass_uniform4f(R.light, "u_cam", CAM_X, CAM_Y, CAM_D, 0);
    cw_pass_uniform4f(R.light, "u_tint", 1, 1, 1, 0);
    for (int i = 0; i < R.f.nlights; i++) {
      const jw_light *l = &R.f.lights[i];
      if (l->stones > 1.5f) continue; /* a light for the stones only */
      cw_pass_uniform4f(R.light, "u_lp", l->x, l->y, l->z, fminf(l->reach * 3.2f, 1400.0f));
      cw_pass_uniform4f(R.light, "u_lc", l->r, l->g, l->b, l->reach);
      cw_pass_draw(6);
    }
    cw_pass_set_blend(CW_PASS_BLEND_NONE);
  }
  cw_pass_bind_target(ctx, ctx->scene);
  cw_pass_use(R.present);
  common_uniforms(R.present, ctx);
  cw_pass_uniform4f(R.present, "u_misc", (float)R.f.time, R.f.button_glow, R.f.button_press, R.f.reshuffle);
  cw_pass_bind_texture(0, rt->tex);
  cw_pass_bind_texture(1, lb->tex);
  if (!R.wg) cw_pass_uniform1i(R.present, "u_t0", 0), cw_pass_uniform1i(R.present, "u_t1", 1);
  cw_pass_fullscreen();
  cw_pass_rt_release(ctx, lb);
}

/* ── the stones ──────────────────────────────────────────────────────── */

/* a stone's world frame: centre, rotation, size */
typedef struct gem_xf { float cx, cy, cz, s; mat4 rot, inv; float rect[4]; } gem_xf;
static gem_xf gem_frame(const jw_gem_draw *g, int cut) {
  const jw_cut *c = &jw_cuts[cut];
  gem_xf x;
  x.s = c->display * g->scale;
  x.cx = g->x - 960.0f, x.cy = 540.0f - g->y;
  /* the culet sinks a little into the velvet */
  x.cz = -c->zmin * c->display * 0.82f + g->lift;
  x.rot = m_rot(g->tilt_x, g->tilt_y, g->spin);
  /* world to local: R^T (p - c) / s */
  mat4 rt = m_transpose3(x.rot), t = m_ident();
  t.m[12] = -x.cx, t.m[13] = -x.cy, t.m[14] = -x.cz;
  x.inv = m_mul(rt, t);
  for (int i = 0; i < 12; i++) x.inv.m[i] /= x.s;
  x.inv.m[12] /= x.s, x.inv.m[13] /= x.s, x.inv.m[14] /= x.s;
  /* the quad: the girdle's extents (turned, when the stone spins far), plus room for the tilt and the perspective */
  float k = CAM_D / (CAM_D - x.cz - c->zmax * x.s);
  float px, py;
  jw_world_to_canvas(x.cx, x.cy, x.cz, &px, &py);
  float ex = c->extent_x, ey = c->extent_y;
  if (fabsf(g->spin) > 0.12f) ex = ey = fmaxf(c->radius, fmaxf(ex, ey));
  float tilt = (fabsf(g->tilt_x) + fabsf(g->tilt_y)) * 0.9f + 0.04f;
  float rx = (ex + tilt) * x.s * k + 2.0f, ry = (ey + tilt) * x.s * k + 2.0f;
  x.rect[0] = px - rx, x.rect[1] = py - ry, x.rect[2] = px + rx, x.rect[3] = py + ry;
  return x;
}

/* the hues of light the bands of wavelength carry, averaged: S bands across 400-700 nm */
static void band_weights(int S, float out[4][3]) {
  for (int k = 0; k < 4; k++) {
    out[k][0] = out[k][1] = out[k][2] = 0;
    if (k >= S) continue;
    int n = 0;
    float lo = 400.0f + 300.0f * (float)k / (float)S, hi = 400.0f + 300.0f * (float)(k + 1) / (float)S;
    for (float l = lo; l <= hi; l += 1.0f, n++) {
      float s[3];
      jw_spectrum_rgb(l, s);
      for (int i = 0; i < 3; i++) out[k][i] += s[i];
    }
    for (int i = 0; i < 3; i++) out[k][i] /= (float)n;
  }
}

static void cauchy(const jw_cut *c, float *A, float *B) {
  float b = c->dispersion * c->fire / (1.0f / (430.8f * 430.8f) - 1.0f / (686.7f * 686.7f));
  *B = b, *A = c->ior - b / (589.3f * 589.3f);
}

/* the lights of the moment a stone sees: the few that reach it most (`most`, up to four), as directions from it with the angular size
 * of their hot cores, and the light of them all on its body (in its own colour) */
static void gem_lights(unsigned p, const gem_xf *x, const float *body, int most) {
  static const char *ln[4] = { "u_gl0", "u_gl1", "u_gl2", "u_gl3" }, *kn[4] = { "u_gk0", "u_gk1", "u_gk2", "u_gk3" };
  int pick[4];
  float score[4], fall[4];
  int n = 0;
  float ex = 0, ey = 0, ez = 0;
  for (int i = 0; i < R.f.nlights; i++) {
    const jw_light *l = &R.f.lights[i];
    if (l->stones <= 0 || fx_off("lamps")) continue;
    float dx = (l->x - 960.0f) - x->cx, dy = (540.0f - l->y) - x->cy, dz = l->z - x->cz;
    float d2 = dx * dx + dy * dy + dz * dz;
    float rr = l->reach * 0.9f, k = 1.0f / (1.0f + d2 / (rr * rr)); /* the stones see the light fall off sooner than the velvet: it stays a local event */
    float s = (l->r + l->g + l->b) * k;
    if (s < 0.3f) continue; /* far and faint: not worth a lamp in the trace */
    ex += l->r * k, ey += l->g * k, ez += l->b * k;
    /* keep the four strongest */
    if (n == most && (most == 0 || s <= score[most - 1])) continue;
    int at = n < most ? n++ : most - 1;
    score[at] = s, pick[at] = i, fall[at] = k;
    for (int j = at; j > 0 && score[j] > score[j - 1]; j--) {
      float ts = score[j]; score[j] = score[j - 1], score[j - 1] = ts;
      int tp = pick[j]; pick[j] = pick[j - 1], pick[j - 1] = tp;
      float tf = fall[j]; fall[j] = fall[j - 1], fall[j - 1] = tf;
    }
  }
  for (int j = 0; j < 4; j++) {
    if (j < n) {
      const jw_light *l = &R.f.lights[pick[j]];
      float dx = (l->x - 960.0f) - x->cx, dy = (540.0f - l->y) - x->cy, dz = l->z - x->cz;
      float d = sqrtf(dx * dx + dy * dy + dz * dz);
      if (d < 1) d = 1, dz = 1;
      float core = fminf(fmaxf(l->core / d, 0.035f), 0.10f);
      cw_pass_uniform4f(p, ln[j], dx / d, dy / d, dz / d, core);
      cw_pass_uniform4f(p, kn[j], l->r * fall[j], l->g * fall[j], l->b * fall[j], 1);
    } else {
      cw_pass_uniform4f(p, ln[j], 0, 0, 1, 0.05f);
      cw_pass_uniform4f(p, kn[j], 0, 0, 0, 0);
    }
  }
  R.st.gem_lights += n;
  float m = fmaxf(body[0], fmaxf(body[1], body[2]));
  if (m <= 0) m = 1;
  const float kb = 0.024f; /* the light the body scatters back */
  cw_pass_uniform4f(p, "u_gx", ex * kb * (0.3f + 0.7f * body[0] / m), ey * kb * (0.3f + 0.7f * body[1] / m), ez * kb * (0.3f + 0.7f * body[2] / m), 0);
}

#define EMERALD_SPREAD 0.022f /* how far a small lamp spreads in the emerald cut's broad flat facets, radians (shaders.c) */
static void draw_gem(const cw_pass_ctx *ctx, const jw_gem_draw *g, int cut, gem_q q, float bw[4][3], float body_override) {
  (void)body_override;
  const jw_cut *c = &jw_cuts[cut];
  unsigned p = R.gem[cut];
  gem_xf x = gem_frame(g, cut);
  cw_pass_use(p);
  common_uniforms(p, ctx);
  set_mat(p, "u_inv", x.inv);
  set_mat(p, "u_rot", x.rot);
  cw_pass_uniform4f(p, "u_rect", x.rect[0], x.rect[1], x.rect[2], x.rect[3]);
  cw_pass_uniform4f(p, "u_abs", c->sigma[0], c->sigma[1], c->sigma[2], g->opacity);
  float A, B;
  cauchy(c, &A, &B);
  cw_pass_uniform4f(p, "u_ior", A, B, g->glow, cut == 2 ? EMERALD_SPREAD : 0.0f);
  cw_pass_uniform4f(p, "u_q", (float)q.sub, (float)q.bounces, q.waves > 1 ? 1.0f : 0.0f, getenv("JEWELS_DEBUG_LEAK") ? 1.0f : 0.0f);
  /* a little light scattered in the body of the coloured stones (none in the diamond): every stone reads as its
   * colour at any tilt, even where its facets look out at the dark */
  cw_pass_uniform4f(p, "u_body", c->body[0], c->body[1], c->body[2], cut == 5 ? 0.0f : cut == JW_CUT_SHARD ? 0.04f : cut == 3 ? 0.13f : cut == 4 ? 0.11f : cut == 0 ? 0.10f : 0.05f);
  cw_pass_uniform4f(p, "u_jit", ctx->jitter[0], ctx->jitter[1], 0, 0);
  cw_pass_bind_texture(0, R.studio_tex ? R.studio_tex : R.dummy_tex);
  if (!R.wg) cw_pass_uniform1i(p, "u_t0", 0);
  gem_lights(p, &x, c->body, cut == JW_CUT_SHARD ? 0 : 2); /* a stone sees the two strongest lights as lamps; a shard (small, fast) takes their light on its body only */
  cw_pass_draw(6);
}

static void under_gem(const cw_pass_ctx *ctx, const jw_gem_draw *g, bool selected) {
  const jw_cut *c = &jw_cuts[g->kind];
  gem_xf x = gem_frame(g, g->kind);
  float span = CAUSTIC_SPAN * c->display * g->scale;
  float px, py;
  jw_world_to_canvas(x.cx, x.cy, 0, &px, &py);
  cw_pass_uniform4f(R.under, "u_rect", px - span, py - span, px + span, py + span);
  cw_pass_uniform4f(R.under, "u_gem", (float)g->kind, g->opacity * (1.0f - fminf(g->glow, 1.0f) * 0.5f), selected ? 1.0f : 0.0f, (float)ctx->time);
  cw_pass_uniform4f(R.under, "u_shape", c->extent_x * c->display * g->scale, c->extent_y * c->display * g->scale, x.cz, c->display * g->scale);
  cw_pass_uniform4f(R.under, "u_body", c->body[0], c->body[1], c->body[2], 0);
  cw_pass_draw(6);
}

/* the shockwaves: rings of the bigger clears bend the picture as they run out. The part of the scene they cover is
 * copied, then each ring draws the copy back through its wave (shaders.c jw_shader_shock) */
#define GLOW_K 0.05f /* how bright the air round a burst light glows, per unit of its strength */
/* a ring bends the picture while it runs out, for the bigger clears (power 2.8 up); one at a time, the newest */
#define SHOCKS 1
static bool shock_on(const jw_ring_draw *r) {
  if (r->power < 2.8f || r->t >= 0.75f) return false;
  int stronger = 0; /* the newest rings first: a ring gives way to newer ones of its kind */
  for (const jw_ring_draw *o = r + 1; o < R.f.rings + R.f.nrings; o++)
    if (o->power >= 2.8f && o->t < 0.75f) stronger++;
  return stronger < SHOCKS;
}
static float ring_r(float t, float p) { return 30.0f + (260.0f + 120.0f * p) * (1 - expf(-4.0f * t)); }
/* development (native): JEWELS_FX_OFF="trails,dust,shock,glow,rays,lamps" leaves those parts out (cost experiments) */
static bool fx_off(const char *part) {
  static const char *off = (const char *)1;
  if (off == (const char *)1) off = getenv("JEWELS_FX_OFF");
  return off && strstr(off, part);
}
static void shockwaves(cw_pass_ctx *ctx) {
  const jw_frame *f = &R.f;
  if (fx_off("shock")) return;
  if (!ctx->scene || !ctx->scene->tex) return;
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  int n = 0;
  for (int i = 0; i < f->nrings; i++) {
    const jw_ring_draw *r = &f->rings[i];
    if (!shock_on(r)) continue;
    float e = ring_r(r->t, r->power) + 70.0f;
    x0 = fminf(x0, r->x - e), y0 = fminf(y0, r->y - e), x1 = fmaxf(x1, r->x + e), y1 = fmaxf(y1, r->y + e);
    n++;
  }
  if (!n) return;
  x0 = fmaxf(floorf(x0), 0), y0 = fmaxf(floorf(y0), 0), x1 = fminf(ceilf(x1), 1920), y1 = fminf(ceilf(y1), 1080);
  if (x1 - x0 < 2 || y1 - y0 < 2) return;
  float sx = (float)ctx->width / 1920.0f, sy = (float)ctx->height / 1080.0f;
  int gw = (int)((x1 - x0) * sx + 0.5f), gh = (int)((y1 - y0) * sy + 0.5f);
  const cw_rt *g = cw_pass_rt(ctx, gw > 1 ? gw : 1, gh > 1 ? gh : 1, CW_RT_RGBA16F, false);
  if (!g) return;
  cw_pass_set_blend(CW_PASS_BLEND_NONE);
  cw_pass_bind_target(ctx, g);
  cw_pass_use(R.grab);
  cw_pass_bind_texture(0, ctx->scene->tex);
  if (!R.wg) cw_pass_uniform1i(R.grab, "u_t0", 0);
  cw_pass_uniform4f(R.grab, "u_px", x0 / 1920.0f, 1.0f - y1 / 1080.0f, (x1 - x0) / 1920.0f, (y1 - y0) / 1080.0f);
  cw_pass_fullscreen();
  cw_pass_bind_target(ctx, ctx->scene);
  cw_pass_use(R.shock);
  common_uniforms(R.shock, ctx);
  cw_pass_bind_texture(0, g->tex);
  if (!R.wg) cw_pass_uniform1i(R.shock, "u_t0", 0);
  cw_pass_uniform4f(R.shock, "u_grab", x0, y1, x1 - x0, y1 - y0);
  for (int i = 0; i < f->nrings; i++) {
    const jw_ring_draw *r = &f->rings[i];
    if (!shock_on(r)) continue;
    cw_pass_uniform4f(R.shock, "u_shock", r->x, r->y, r->t, r->power);
    cw_pass_draw(6);
    R.st.shocks++;
  }
  cw_pass_rt_release(ctx, g);
}

static void glints(cw_pass_ctx *ctx);

static void gems_exec(cw_pass_ctx *ctx, void *u) {
  (void)u;
  need_frame(ctx);
  if (!build()) return;
  gem_q q = quality_now();
  R.st.subsamples = q.sub, R.st.bounces = q.bounces, R.st.wavelengths = q.waves;
  float bw[4][3];
  band_weights(1, bw); /* the exits disperse across the whole spectrum: every subsample is white light */
  for (int k = 1; k < 4; k++) memcpy(bw[k], bw[0], sizeof bw[0]);
  const jw_frame *f = &R.f;

  /* under each stone: its shadow, its caustic, the glow of a held one */
  cw_pass_set_blend(CW_PASS_BLEND_PREMUL);
  cw_pass_use(R.under);
  common_uniforms(R.under, ctx);
  if (!R.dummy_tex) { uint16_t z[4] = { 0 }; R.dummy_tex = cw_pass_texture_new(1, 1, CW_RT_RGBA16F, z); }
  cw_pass_bind_texture(0, R.caustic_tex ? R.caustic_tex : R.dummy_tex);
  if (!R.wg) cw_pass_uniform1i(R.under, "u_t0", 0);
  cw_pass_uniform4f(R.under, "u_misc", q.caustics && R.caustic_tex ? 1.0f : 0.0f, 0, 0, 0);
  for (int i = 0; i < f->ngems; i++) {
    const jw_gem_draw *g = &f->gems[i];
    if (g->opacity <= 0.01f) continue;
    bool sel = f->sel_on && fabsf(g->x - f->sel_x) < 1 && fabsf(g->y - f->sel_y) < 1;
    under_gem(ctx, g, sel);
  }

  /* the stones */
  R.st.gems_drawn = 0, R.st.gem_lights = 0;
  for (int i = 0; i < f->ngems; i++) {
    const jw_gem_draw *g = &f->gems[i];
    if (g->opacity <= 0.01f || g->scale <= 0.02f) continue;
    draw_gem(ctx, g, g->kind, q, bw, 0);
    R.st.gems_drawn++;
  }

  /* the bursts: shards of the stone (traced like it, through a broken shape, lit by the bursts' own lights), each
   * with a short trail of light behind it; then a ring that bends the picture as it runs out (a shockwave), then the
   * added light: sparks, sparkle dust, rings, rays and the cascade's links */
  R.st.shards_drawn = R.st.sparks_drawn = R.st.shocks = 0;
  gem_q qs = q;
  qs.bounces = q.bounces > 2 ? 2 : q.bounces, qs.sub = 1; /* a shard is small and quick: two bounces carry its sparkle */
  /* a budget of shards a frame, shared by the bursts in flight */
  float want = 0;
  for (int b = 0; b < f->nbursts; b++)
    if (f->bursts[b].t < 1.4f) want += 4 + 3 * f->bursts[b].power;
  float share = want > 130 ? 130 / want : 1;
  float bws[4][3];
  band_weights(1, bws);
  static struct { float x0, y0, x1, y1, w, r, g, b; } trail[160];
  int ntrail = 0;
  for (int b = 0; b < f->nbursts; b++) {
    const jw_burst_draw *bu = &f->bursts[b];
    if (bu->t > 1.4f) continue;
    int n = (int)((4 + 3 * bu->power) * share + 0.5f);
    if (n > 18) n = 18;
    for (int i = 0; i < n; i++) {
      /* each shard's flight, from the burst's seed */
      uint32_t h = bu->seed * 2654435761u + (uint32_t)i * 40503u;
      h ^= h >> 15, h *= 2246822519u, h ^= h >> 13;
      float r1 = (float)(h & 1023) / 1023.0f, r2 = (float)((h >> 10) & 1023) / 1023.0f, r3 = (float)((h >> 20) & 1023) / 1023.0f;
      float life = 0.55f + 0.35f * r3 + 0.08f * bu->power;
      if (bu->t > life) continue;
      float a = 6.2831853f * ((float)i + r1 * 0.7f) / (float)n, sp = (150.0f + 260.0f * r2) * (0.8f + 0.25f * bu->power);
      float t = bu->t, drag = (1 - expf(-2.6f * t)) / 2.6f;
      jw_gem_draw s = { 0 };
      s.kind = bu->kind;
      s.x = bu->x + cosf(a) * sp * drag;
      s.y = bu->y + sinf(a) * sp * drag + 260.0f * t * t; /* they fall a little, as they fly */
      s.lift = 40.0f + 120.0f * r2 * (1 - t / life);
      float k = t / life;
      s.scale = (0.16f + 0.14f * r3) * (1 - k * k) * (1 + 0.1f * bu->power);
      /* the shards stay over the velvet: one that reaches the frame fades out before it crosses it */
      float edge = fminf(fminf(s.x - JW_BX, JW_BX + 10 * JW_CELL - s.x), fminf(s.y - JW_BY, JW_BY + 8 * JW_CELL - s.y));
      float inside = fminf(fmaxf((edge - 6.0f) / 34.0f, 0.0f), 1.0f);
      if (inside <= 0.0f) continue;
      s.opacity = inside;
      s.glow = 0.5f * (1 - k);
      s.spin = (r1 - 0.5f) * 14.0f * t + r2 * 6.0f;
      s.tilt_x = (r2 - 0.5f) * 10.0f * t + r1 * 3.0f;
      s.tilt_y = (r3 - 0.5f) * 10.0f * t;
      /* a shard is the shard cut, sized against the stone it came from */
      const jw_cut *src = &jw_cuts[bu->kind];
      s.scale *= src->display;
      jw_cut *sh = &jw_cuts[JW_CUT_SHARD];
      memcpy(sh->sigma, src->sigma, sizeof sh->sigma);
      sh->ior = src->ior, sh->dispersion = src->dispersion, sh->fire = src->fire * 1.4f;
      memcpy(sh->body, src->body, sizeof sh->body);
      draw_gem(ctx, &s, JW_CUT_SHARD, qs, bws, 0);
      R.st.shards_drawn++;
      /* its trail: where it was a few hundredths of a second ago, to where it is (fast early, gone as it slows) */
      if (ntrail < 160 && t > 0.01f) {
        float tp = fmaxf(t - 0.05f, 0.0f), dp = (1 - expf(-2.6f * tp)) / 2.6f;
        float px0 = bu->x + cosf(a) * sp * dp, py0 = bu->y + sinf(a) * sp * dp + 260.0f * tp * tp;
        float z = 40.0f + 120.0f * r2 * (1 - tp / life), z1 = s.lift;
        float c0x, c0y, c1x, c1y;
        jw_world_to_canvas(px0 - 960.0f, 540.0f - py0, z, &c0x, &c0y);
        jw_world_to_canvas(s.x - 960.0f, 540.0f - s.y, z1, &c1x, &c1y);
        float m = fmaxf(src->body[0], fmaxf(src->body[1], src->body[2]));
        float e = 0.9f * (1 - k) * (1 - k) * inside;
        if (m <= 0) m = 1;
        trail[ntrail++] = (typeof(trail[0])){ c0x, c0y, c1x, c1y, 2.0f + 3.0f * s.scale / src->display * 10.0f * (1 - k),
                                              e * (0.35f + 0.65f * src->body[0] / m), e * (0.35f + 0.65f * src->body[1] / m), e * (0.35f + 0.65f * src->body[2] / m) };
      }
    }
  }
  shockwaves(ctx);
  cw_pass_set_blend(CW_PASS_BLEND_ADD);
  cw_pass_use(R.spark);
  common_uniforms(R.spark, ctx);
  for (int i = 0; i < ntrail && !fx_off("trails"); i++) {
    cw_pass_uniform4f(R.spark, "u_burst", trail[i].x0, trail[i].y0, 1, trail[i].w);
    cw_pass_uniform4f(R.spark, "u_col", trail[i].r, trail[i].g, trail[i].b, 0);
    cw_pass_uniform4f(R.spark, "u_kind", 5, 1, trail[i].x1, trail[i].y1);
    cw_pass_draw(6);
  }
  int dust_left = 700; /* the motes of dust a frame, the newest bursts' first */
  for (int b = f->nbursts - 1; b >= 0; b--) {
    const jw_burst_draw *bu = &f->bursts[b];
    const jw_cut *c = &jw_cuts[bu->kind];
    cw_pass_uniform4f(R.spark, "u_burst", bu->x, bu->y, bu->t, bu->power);
    cw_pass_uniform4f(R.spark, "u_col", c->body[0], c->body[1], c->body[2], (float)(bu->seed & 0xffff));
    if (bu->t < 1.6f) {
      int n = (int)(10 + 8 * bu->power);
      if (n > 50) n = 50;
      cw_pass_uniform4f(R.spark, "u_kind", 0, (float)n, 0, 0);
      cw_pass_draw(n * 6);
      R.st.sparks_drawn += n;
    }
    /* the dust lingers longer than the sparks */
    int nd = (int)(12 + 6 * bu->power);
    if (nd > 40) nd = 40;
    if (bu->t > 1.5f || dust_left <= 0 || bu->shower || fx_off("dust")) continue;
    if (nd > dust_left) nd = dust_left;
    dust_left -= nd;
    cw_pass_uniform4f(R.spark, "u_kind", 4, (float)nd, 0, 0);
    cw_pass_draw(nd * 6);
  }
  /* the burst lights themselves: the air round each one lit, in its colour (the broad lights, the climax's and the
   * sweep's, light the room and are not seen as a glow) */
  for (int i = 0; i < f->nlights; i++) {
    const jw_light *l = &f->lights[i];
    if (l->stones <= 0 || l->reach > 400.0f || fx_off("glow")) continue;
    float px, py;
    jw_world_to_canvas(l->x - 960.0f, 540.0f - l->y, l->z * 0.5f, &px, &py);
    cw_pass_uniform4f(R.spark, "u_burst", px, py, 0, 0);
    cw_pass_uniform4f(R.spark, "u_col", l->r * GLOW_K, l->g * GLOW_K, l->b * GLOW_K, 0);
    cw_pass_uniform4f(R.spark, "u_kind", 6, 1, 36.0f + 0.2f * l->reach, 0);
    cw_pass_draw(6);
  }
  for (int i = 0; i < f->nrings; i++) {
    const jw_ring_draw *r = &f->rings[i];
    cw_pass_uniform4f(R.spark, "u_burst", r->x, r->y, r->t, r->power);
    cw_pass_uniform4f(R.spark, "u_col", r->r, r->g, r->b, 0);
    cw_pass_uniform4f(R.spark, "u_kind", 1, 1, 0, 0);
    cw_pass_draw(6);
  }
  for (int i = 0; i < f->nrays && !fx_off("rays"); i++) {
    const jw_rays *r = &f->rays[i];
    cw_pass_uniform4f(R.spark, "u_burst", r->x, r->y, r->t, r->power);
    cw_pass_uniform4f(R.spark, "u_col", r->r, r->g, r->b, (float)(r->seed & 0xffff));
    cw_pass_uniform4f(R.spark, "u_kind", 3, 1, 150.0f + 55.0f * r->power, 0);
    cw_pass_draw(6);
  }
  for (int i = 0; i < f->ntrails; i++) {
    const jw_trail *t = &f->trails[i];
    cw_pass_uniform4f(R.spark, "u_burst", t->x0, t->y0, t->t, 0);
    cw_pass_uniform4f(R.spark, "u_col", t->r, t->g, t->b, 0);
    cw_pass_uniform4f(R.spark, "u_kind", 2, 1, t->x1, t->y1);
    cw_pass_draw(6);
  }

  /* the marks: the cursor, the held jewel, the hint */
  cw_pass_set_blend(CW_PASS_BLEND_PREMUL);
  cw_pass_use(R.mark);
  common_uniforms(R.mark, ctx);
  float tm = (float)f->time;
  if (f->hint_on && !f->board_only) {
    float x0 = jw_cell_cx(f->hint_x), y0 = jw_cell_cy(f->hint_y);
    float x1 = jw_cell_cx(f->hint_x + f->hint_dx), y1 = jw_cell_cy(f->hint_y + f->hint_dy);
    cw_pass_uniform4f(R.mark, "u_mark", 2, x0, y0, tm);
    cw_pass_uniform4f(R.mark, "u_mark2", x1, y1, f->hint_t, 0);
    cw_pass_draw(6);
  }
  if (f->sel_on && !f->board_only) {
    cw_pass_uniform4f(R.mark, "u_mark", 1, f->sel_x, f->sel_y, tm);
    cw_pass_uniform4f(R.mark, "u_mark2", 0, 0, 0, 0);
    cw_pass_draw(6);
  }
  if (f->cursor_on && !f->board_only) {
    cw_pass_uniform4f(R.mark, "u_mark", 0, f->cursor_x, f->cursor_y, tm);
    cw_pass_uniform4f(R.mark, "u_mark2", 0, 0, 0, 0);
    cw_pass_draw(6);
  }
  if (q.glint && !getenv("JEWELS_NO_GLINT")) glints(ctx);
  cw_pass_set_blend(CW_PASS_BLEND_NONE);
}

/* ── the star glints ─────────────────────────────────────────────────── */

/* the star glints (shaders.c): the picture at half the canvas, its peaks found block by block into tiles of 60 canvas
 * pixels (at most one star a tile, none close to a brighter one), then each star drawn over the board at full
 * resolution in the scene's own target */
#define GLINT_THRESHOLD (getenv("JEWELS_GLINT_T") ? (float)atof(getenv("JEWELS_GLINT_T")) : 105.0f) /* development: JEWELS_GLINT_T */
static void glint_stage(cw_pass_ctx *ctx, int i, const cw_rt *dst, unsigned src) {
  cw_pass_bind_target(ctx, dst);
  cw_pass_use(R.glint[i]);
  cw_pass_bind_texture(0, src);
  if (!R.wg) cw_pass_uniform1i(R.glint[i], "u_t0", 0);
}
static void glints(cw_pass_ctx *ctx) {
  if (!ctx->scene || !ctx->scene->tex) return;
  const cw_rt *a = cw_pass_rt(ctx, 960, 540, CW_RT_RGBA16F, false), *b = cw_pass_rt(ctx, 160, 90, CW_RT_RGBA16F, false);
  const cw_rt *c = cw_pass_rt(ctx, 32, 18, CW_RT_RGBA16F, false), *d = cw_pass_rt(ctx, 32, 18, CW_RT_RGBA16F, false);
  if (!a || !b || !c || !d) return;
  cw_pass_set_blend(CW_PASS_BLEND_NONE);
  glint_stage(ctx, 0, a, ctx->scene->tex);
  cw_pass_uniform4f(R.glint[0], "u_px", 1.0f / (float)ctx->scene->w, 1.0f / (float)ctx->scene->h, 0, 0);
  cw_pass_fullscreen();
  glint_stage(ctx, 1, b, a->tex);
  cw_pass_fullscreen();
  glint_stage(ctx, 2, c, b->tex);
  cw_pass_uniform4f(R.glint[2], "u_px", GLINT_THRESHOLD, 0, 0, 0);
  cw_pass_fullscreen();
  glint_stage(ctx, 3, d, c->tex);
  cw_pass_uniform4f(R.glint[3], "u_px", 0, 16.0f, 32, 18); /* stars closer than 32 canvas px: the brighter one */
  cw_pass_fullscreen();
  glint_stage(ctx, 4, ctx->scene, d->tex);
  cw_pass_set_blend(CW_PASS_BLEND_ADD);
  cw_pass_uniform4f(R.glint[4], "u_view", (float)ctx->width, (float)ctx->height, 1.0f / (float)ctx->width, 1.0f / (float)ctx->height);
  cw_pass_uniform4f(R.glint[4], "u_rect", 30, 20, 1314, 1060);
  cw_pass_uniform4f(R.glint[4], "u_px", 1.6f, 1.0f, 0.21f, GLINT_THRESHOLD);
  cw_pass_draw(6);
  cw_pass_set_blend(CW_PASS_BLEND_NONE);
  cw_pass_rt_release(ctx, a), cw_pass_rt_release(ctx, b), cw_pass_rt_release(ctx, c), cw_pass_rt_release(ctx, d);
}

/* ── the caustic bake ────────────────────────────────────────────────── */

typedef struct bake_ctx { float *img; float gz, s; float light[3]; float sp[3]; } bake_ctx;
static void bake_exit(const float pos[3], const float dir[3], const float w[3], void *user) {
  bake_ctx *b = user;
  if (dir[2] >= -1e-3f) return; /* only light heading down reaches the velvet */
  /* local to world: the stone stands upright, origin at height gz (local units) above the tray */
  float pz = pos[2] + b->gz;
  float t = -pz / dir[2];
  float x = pos[0] + dir[0] * t, y = pos[1] + dir[1] * t;
  int ix = (int)((x / CAUSTIC_SPAN * 0.5f + 0.5f) * CAUSTIC_N), iy = (int)((0.5f - y / CAUSTIC_SPAN * 0.5f) * CAUSTIC_N);
  if (ix < 0 || iy < 0 || ix >= CAUSTIC_N || iy >= CAUSTIC_N) return;
  float *px = &b->img[(iy * CAUSTIC_N * 6 + ix) * 4];
  for (int i = 0; i < 3; i++) px[i] += w[i] * b->sp[i] * b->s;
}

bool jw_render_bake_step(void) {
  if (!R.caustic) {
    R.caustic = calloc((size_t)CAUSTIC_N * 6 * CAUSTIC_N * 4, sizeof(float));
    R.bake_kind = 0, R.bake_row = 0;
  }
  if (R.bake_kind >= 6) {
    /* then the studio the stones see, 64 rows a step */
    const int H = JW_STUDIO_N + 2;
    if (!R.studio) R.studio = malloc((size_t)JW_STUDIO_N * H * 4 * sizeof(float));
    if (R.studio_row >= H) return true;
    jw_studio_rows(R.studio, R.studio_row, 64);
    R.studio_row += 64;
    return R.studio_row >= H;
  }
  const jw_cut *c = &jw_cuts[R.bake_kind];
  float *img = R.caustic + (size_t)R.bake_kind * CAUSTIC_N * 4;
  bake_ctx b = { img, -c->zmin * 0.82f, 0, { -jw_key_dir[0], -jw_key_dir[1], -jw_key_dir[2] } };
  /* photons on a grid over the stone, seen from the key light: each carries the light of its patch of the tray */
  const int G = 220;
  float span = c->radius * 1.05f;
  /* a patch of the beam: its area on the tray, over a texel's */
  float beam = (2 * span / G) * (2 * span / G) / jw_key_dir[2];
  float texel = (2 * CAUSTIC_SPAN / CAUSTIC_N) * (2 * CAUSTIC_SPAN / CAUSTIC_N);
  /* the beam's frame */
  float L[3] = { jw_key_dir[0], jw_key_dir[1], jw_key_dir[2] }, U[3] = { 0, 0, 1 }, T1[3], T2[3];
  T1[0] = U[1] * L[2] - U[2] * L[1], T1[1] = U[2] * L[0] - U[0] * L[2], T1[2] = U[0] * L[1] - U[1] * L[0];
  float l = sqrtf(T1[0] * T1[0] + T1[1] * T1[1] + T1[2] * T1[2]);
  for (int i = 0; i < 3; i++) T1[i] /= l;
  T2[0] = L[1] * T1[2] - L[2] * T1[1], T2[1] = L[2] * T1[0] - L[0] * T1[2], T2[2] = L[0] * T1[1] - L[1] * T1[0];
  int rows = G / 20;
  for (int r = R.bake_row; r < R.bake_row + rows && r < G; r++)
    for (int q = 0; q < G; q++) {
      float a = (2 * ((float)q + 0.5f) / G - 1) * span, bb = (2 * ((float)r + 0.5f) / G - 1) * span;
      float o[3], d[3];
      for (int i = 0; i < 3; i++) o[i] = L[i] * 6 + T1[i] * a + T2[i] * bb, d[i] = -L[i];
      o[2] -= b.gz;
      /* the shadow: where this photon would have landed with no stone, it now does not */
      float nm = 400.0f + 300.0f * (float)((q * 7 + r * 13) % 31) / 31.0f;
      float sp[3];
      jw_spectrum_rgb(nm, sp);
      b.s = beam / texel;
      memcpy(b.sp, sp, sizeof sp);
      if (jw_trace_cpu(c, o, d, nm, 8, bake_exit, &b)) {
        /* blocked: the shadow, in alpha, where the photon would have landed */
        float t = (o[2] + b.gz) / L[2];
        float x = o[0] - L[0] * t, y = o[1] - L[1] * t;
        int ix = (int)((x / CAUSTIC_SPAN * 0.5f + 0.5f) * CAUSTIC_N), iy = (int)((0.5f - y / CAUSTIC_SPAN * 0.5f) * CAUSTIC_N);
        if (ix >= 0 && iy >= 0 && ix < CAUSTIC_N && iy < CAUSTIC_N) img[(iy * CAUSTIC_N * 6 + ix) * 4 + 3] += b.s;
      }
    }
  R.bake_row += rows;
  if (R.bake_row >= G) {
    R.bake_row = 0;
    R.bake_kind++;
  }
  if (R.bake_kind < 6) return false;
  /* soften: the key is a softbox, not a point (the light lightly, keeping its rays; the shadow more) */
  int W = CAUSTIC_N * 6, H = CAUSTIC_N;
  float *tmp = malloc((size_t)W * H * 4 * sizeof(float));
  for (int pass = 0; pass < 2; pass++) {
    memcpy(tmp, R.caustic, (size_t)W * H * 4 * sizeof(float));
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        for (int ch = pass ? 3 : 0; ch < 4; ch++) {
          int rad = pass ? 2 : 1;
          float s = 0, n = 0;
          for (int dy = -rad; dy <= rad; dy++)
            for (int dx = -rad; dx <= rad; dx++) {
              int xx = x + dx, yy = y + dy;
              if (yy < 0 || yy >= H || xx / CAUSTIC_N != x / CAUSTIC_N || xx < 0 || xx >= W) continue;
              float wgt = ((float)rad + 1 - (float)abs(dx)) * ((float)rad + 1 - (float)abs(dy));
              s += tmp[(yy * W + xx) * 4 + ch] * wgt, n += wgt;
            }
          R.caustic[(y * W + x) * 4 + ch] = s / n;
        }
  }
  free(tmp);
  return false; /* the studio next */
}

static void upload_caustics(void) {
  if (R.caustic_tex || !R.caustic || R.bake_kind < 6) return;
  int W = CAUSTIC_N * 6, H = CAUSTIC_N;
  uint16_t *half = malloc((size_t)W * H * 4 * sizeof(uint16_t));
  for (int i = 0; i < W * H * 4; i++) {
    float v = fminf(R.caustic[i], 60000.0f);
    /* float to IEEE half (positive, finite) */
    uint32_t b;
    memcpy(&b, &v, 4);
    int e = (int)((b >> 23) & 255) - 127 + 15;
    uint16_t h;
    if (v <= 0) h = 0;
    else if (e <= 0) h = 0;
    else if (e >= 31) h = 0x7bff;
    else h = (uint16_t)((e << 10) | ((b >> 13) & 0x3ff));
    half[i] = h;
  }
  /* row 0 is the bottom (v = 0) for the pass textures: flip */
  uint16_t *flip = malloc((size_t)W * H * 4 * sizeof(uint16_t));
  for (int y = 0; y < H; y++) memcpy(flip + (size_t)y * W * 4, half + (size_t)(H - 1 - y) * W * 4, (size_t)W * 4 * sizeof(uint16_t));
  R.caustic_tex = cw_pass_texture_new(W, H, CW_RT_RGBA16F, flip);
  free(half), free(flip);
}

/* float to IEEE half, rounded to nearest, signed */
static uint16_t to_half(float v) {
  uint32_t b;
  memcpy(&b, &v, 4);
  uint16_t s = (uint16_t)((b >> 16) & 0x8000);
  float a = fabsf(v);
  if (!(a >= 6.1035e-5f)) return s; /* zero, tiny, NaN */
  if (a >= 65504.0f) return (uint16_t)(s | 0x7bff);
  memcpy(&b, &a, 4);
  b += 0x1000; /* round the dropped 13 bits */
  int e = (int)((b >> 23) & 255) - 127 + 15;
  if (e >= 31) return (uint16_t)(s | 0x7bff);
  return (uint16_t)(s | (e << 10) | ((b >> 13) & 0x3ff));
}

static void upload_studio(void) {
  const int W = JW_STUDIO_N, H = JW_STUDIO_N + 2;
  if (R.studio_tex || !R.studio || R.studio_row < H) return;
  uint16_t *half = malloc((size_t)W * H * 4 * sizeof(uint16_t));
  for (size_t i = 0; i < (size_t)W * H * 4; i++) half[i] = to_half(R.studio[i]);
  R.studio_tex = cw_pass_texture_new(W, H, CW_RT_RGBA16F, half);
  free(half);
  free(R.studio), R.studio = NULL;
}

static void upload_exec(cw_pass_ctx *ctx, void *u) {
  (void)ctx, (void)u;
  upload_caustics();
  upload_studio();
}

void jw_render_init(jw_frame_fn fn, void *user) {
  R.fn = fn, R.user = user;
  cw_tree *tree = cw_scene_app_tree();
  cw_pipeline *p = cw_pipeline_of(tree);
  if (cw_pipeline_has_pass(p, "jw_tray")) return;
  cw_pipeline_set_pass_enabled(p, "scene", false);
  cw_pipeline_add_pass(p, &(cw_pass_desc){ .name = "jw_upload", .phase = CW_PHASE_SHADOW, .order = 0, .execute = upload_exec, .webgpu = true });
  cw_pipeline_add_pass(p, &(cw_pass_desc){ .name = "jw_tray", .phase = CW_PHASE_OPAQUE, .order = 0, .writes = CW_RES_COLOR | CW_RES_DEPTH,
                                           .execute = tray_exec, .webgpu = true });
  cw_pipeline_add_pass(p, &(cw_pass_desc){ .name = "jw_gems", .phase = CW_PHASE_OPAQUE, .order = 10, .writes = CW_RES_COLOR,
                                           .execute = gems_exec, .webgpu = true });
}
