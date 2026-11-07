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
  unsigned present, tray, gem[JW_NCUTS], under, spark, ring, mark, glint_pre, glint_streak, glint_add;
  bool built, failed;
  jw_render_stats st;
  unsigned caustic_tex, dummy_tex;
  float *caustic; /* the bake, RGBA float, 6 kinds side by side */
  int bake_kind, bake_row;
} R;

#define CAUSTIC_N 128
#define CAUSTIC_SPAN 2.6f /* the bake covers +-2.6 of a stone's local units around its centre */

const jw_render_stats *jw_render_get_stats(void) { return &R.st; }

void jw_world_to_canvas(float x, float y, float z, float *px, float *py) {
  float k = CAM_D / (CAM_D - z);
  *px = (CAM_X + (x - CAM_X) * k) + 960.0f;
  *py = 540.0f - (CAM_Y + (y - CAM_Y) * k);
}

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
  float w = R.f.warmth;
  cw_pass_uniform4f(p, "u_tint", 1.0f + 0.10f * w, 1.0f - 0.04f * w, 1.0f - 0.22f * w, R.f.sweep);
}

/* ── the tray ────────────────────────────────────────────────────────── */

static void flashes_uniforms(unsigned p) {
  static const char *pos[4] = { "u_fl0", "u_fl1", "u_fl2", "u_fl3" }, *col[4] = { "u_fc0", "u_fc1", "u_fc2", "u_fc3" };
  for (int i = 0; i < 4; i++) {
    if (i < R.f.nflashes) {
      const jw_flash *f = &R.f.flashes[i];
      cw_pass_uniform4f(p, pos[i], f->x - 960.0f, 540.0f - f->y, f->z, 1);
      cw_pass_uniform4f(p, col[i], f->r, f->g, f->b, 0);
    } else {
      cw_pass_uniform4f(p, pos[i], 0, 0, 100, 0);
      cw_pass_uniform4f(p, col[i], 0, 0, 0, 0);
    }
  }
}

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
  for (int i = 0; i < 3; i++) {
    src = jw_shader_glint(R.wg, i);
    static const char *names[] = { "jw_glint_pre", "jw_glint_streak", "jw_glint_add" };
    unsigned pr = R.wg ? cw_pass_program_wgsl(names[i], src) : cw_pass_program(names[i], src);
    if (i == 0) R.glint_pre = pr;
    else if (i == 1) R.glint_streak = pr;
    else R.glint_add = pr;
    free(src);
  }
  R.failed = !R.present || !R.tray || !R.under || !R.spark || !R.mark || !R.glint_pre || !R.glint_streak || !R.glint_add;
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
  cw_pass_use(R.present);
  common_uniforms(R.present, ctx);
  flashes_uniforms(R.present);
  cw_pass_uniform4f(R.present, "u_misc", (float)R.f.time, R.f.button_glow, R.f.button_press, R.f.reshuffle);
  cw_pass_bind_texture(0, rt->tex);
  if (!R.wg) cw_pass_uniform1i(R.present, "u_t0", 0);
  cw_pass_fullscreen();
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

/* the flashes nearest a stone, as directions from it (its own lights) */
static void gem_flashes(unsigned p, const gem_xf *x) {
  static const char *dn[2] = { "u_gf0", "u_gf1" }, *cn[2] = { "u_gc0", "u_gc1" };
  int used = 0;
  for (int i = 0; i < R.f.nflashes && used < 2; i++) {
    const jw_flash *f = &R.f.flashes[i];
    float dx = (f->x - 960.0f) - x->cx, dy = (540.0f - f->y) - x->cy, dz = f->z - x->cz;
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    float k = 1.0f / (1.0f + d * d / (260.0f * 260.0f));
    if (k < 0.02f) continue;
    cw_pass_uniform4f(p, dn[used], dx / d, dy / d, dz / d, k);
    cw_pass_uniform4f(p, cn[used], f->r, f->g, f->b, 0);
    used++;
  }
  for (; used < 2; used++) {
    cw_pass_uniform4f(p, dn[used], 0, 0, 1, 0);
    cw_pass_uniform4f(p, cn[used], 0, 0, 0, 0);
  }
}

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
  cw_pass_uniform4f(p, "u_ior", A, B, g->glow, 0);
  cw_pass_uniform4f(p, "u_q", (float)q.sub, (float)q.bounces, q.waves > 1 ? 1.0f : 0.0f, getenv("JEWELS_DEBUG_LEAK") ? 1.0f : 0.0f);
  cw_pass_uniform4f(p, "u_body", c->body[0], c->body[1], c->body[2], 0);
  cw_pass_uniform4f(p, "u_jit", ctx->jitter[0], ctx->jitter[1], 0, 0);
  gem_flashes(p, &x);
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
  R.st.gems_drawn = 0;
  for (int i = 0; i < f->ngems; i++) {
    const jw_gem_draw *g = &f->gems[i];
    if (g->opacity <= 0.01f || g->scale <= 0.02f) continue;
    draw_gem(ctx, g, g->kind, q, bw, 0);
    R.st.gems_drawn++;
  }

  /* the bursts: shards of the stone (traced like it, through a broken shape), then sparks and rings */
  R.st.shards_drawn = R.st.sparks_drawn = 0;
  gem_q qs = q;
  qs.bounces = q.bounces > 3 ? 3 : q.bounces, qs.sub = 1;
  /* a budget of shards a frame, shared by the bursts in flight */
  float want = 0;
  for (int b = 0; b < f->nbursts; b++) want += 4 + 3 * f->bursts[b].power;
  float share = want > 130 ? 130 / want : 1;
  float bws[4][3];
  band_weights(1, bws);
  for (int b = 0; b < f->nbursts; b++) {
    const jw_burst_draw *bu = &f->bursts[b];
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
      s.opacity = 1;
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
    }
  }
  cw_pass_set_blend(CW_PASS_BLEND_ADD);
  cw_pass_use(R.spark);
  common_uniforms(R.spark, ctx);
  for (int b = 0; b < f->nbursts; b++) {
    const jw_burst_draw *bu = &f->bursts[b];
    int n = (int)(14 + 16 * bu->power);
    if (n > 90) n = 90;
    const jw_cut *c = &jw_cuts[bu->kind];
    cw_pass_uniform4f(R.spark, "u_burst", bu->x, bu->y, bu->t, bu->power);
    cw_pass_uniform4f(R.spark, "u_col", c->body[0], c->body[1], c->body[2], (float)(bu->seed & 0xffff));
    cw_pass_uniform4f(R.spark, "u_kind", 0, (float)n, 0, 0);
    cw_pass_draw(n * 6);
    R.st.sparks_drawn += n;
  }
  for (int i = 0; i < f->nrings; i++) {
    const jw_ring_draw *r = &f->rings[i];
    cw_pass_uniform4f(R.spark, "u_burst", r->x, r->y, r->t, r->power);
    cw_pass_uniform4f(R.spark, "u_col", r->r, r->g, r->b, 0);
    cw_pass_uniform4f(R.spark, "u_kind", 1, 1, 0, 0);
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
  cw_pass_set_blend(CW_PASS_BLEND_NONE);
}

/* ── the star glints ─────────────────────────────────────────────────── */

static bool glint_active(const cw_pass_ctx *ctx, void *u) {
  (void)ctx, (void)u;
  return quality_now().glint && !getenv("JEWELS_NO_GLINT");
}

static void glint_exec(cw_pass_ctx *ctx, void *u) {
  (void)u;
  if (!build() || !ctx->src || !ctx->dst) return;
  int w = (ctx->width + 3) / 4, h = (ctx->height + 3) / 4;
  const cw_rt *a = cw_pass_rt(ctx, w, h, CW_RT_RGBA16F, false), *b = cw_pass_rt(ctx, w, h, CW_RT_RGBA16F, false);
  if (!a || !b) return;
  /* the brightest light, at half size */
  cw_pass_bind_target(ctx, a);
  cw_pass_use(R.glint_pre);
  cw_pass_bind_texture(0, ctx->src->tex);
  if (!R.wg) cw_pass_uniform1i(R.glint_pre, "u_t0", 0);
  cw_pass_uniform4f(R.glint_pre, "u_px", 1.0f / (float)ctx->src->w, 1.0f / (float)ctx->src->h, 2.2f, 0);
  cw_pass_fullscreen();
  /* six spokes */
  cw_pass_bind_target(ctx, b);
  cw_pass_use(R.glint_streak);
  cw_pass_bind_texture(0, a->tex);
  if (!R.wg) cw_pass_uniform1i(R.glint_streak, "u_t0", 0);
  cw_pass_uniform4f(R.glint_streak, "u_px", 1.0f / (float)w, 1.0f / (float)h, 7.0f * (float)h / 1080.0f, 0.2f + 0.05f * sinf((float)R.f.time * 0.07f));
  cw_pass_fullscreen();
  /* over the picture */
  cw_pass_bind_target(ctx, ctx->dst);
  cw_pass_use(R.glint_add);
  cw_pass_bind_texture(0, ctx->src->tex);
  cw_pass_bind_texture(1, b->tex);
  if (!R.wg) cw_pass_uniform1i(R.glint_add, "u_t0", 0), cw_pass_uniform1i(R.glint_add, "u_t1", 1);
  cw_pass_uniform4f(R.glint_add, "u_px", 1.0f / (float)ctx->dst->w, 1.0f / (float)ctx->dst->h, 0.55f, 0);
  cw_pass_fullscreen();
  cw_pass_rt_release(ctx, a);
  cw_pass_rt_release(ctx, b);
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
  if (R.bake_kind >= 6) return true;
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
  /* soften: the key is a softbox, not a point */
  int W = CAUSTIC_N * 6, H = CAUSTIC_N;
  float *tmp = malloc((size_t)W * H * 4 * sizeof(float));
  for (int pass = 0; pass < 2; pass++) {
    memcpy(tmp, R.caustic, (size_t)W * H * 4 * sizeof(float));
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        for (int ch = 0; ch < 4; ch++) {
          float s = 0, n = 0;
          for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
              int xx = x + dx, yy = y + dy;
              if (yy < 0 || yy >= H || xx / CAUSTIC_N != x / CAUSTIC_N || xx < 0 || xx >= W) continue;
              float wgt = (3.0f - (float)abs(dx)) * (3.0f - (float)abs(dy));
              s += tmp[(yy * W + xx) * 4 + ch] * wgt, n += wgt;
            }
          R.caustic[(y * W + x) * 4 + ch] = s / n;
        }
  }
  free(tmp);
  return true;
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

static void upload_exec(cw_pass_ctx *ctx, void *u) {
  (void)ctx, (void)u;
  upload_caustics();
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
  cw_pipeline_add_pass(p, &(cw_pass_desc){ .name = "jw_glint", .phase = CW_PHASE_POST, .order = 320, .reads = CW_RES_COLOR, .writes = CW_RES_COLOR,
                                           .active = glint_active, .execute = glint_exec, .webgpu = true });
}
