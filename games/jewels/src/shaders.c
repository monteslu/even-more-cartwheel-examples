/* Shader sources (shaders.h). The GLSL and the WGSL of each program are kept side by side and say the same thing;
 * the studio's lights and each cut's planes are generated into both from the same tables. */
#include "shaders.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── a growing string ─────────────────────────────────────────────────── */

typedef struct sb { char *s; size_t n, cap; } sb;
static void put(sb *b, const char *fmt, ...) {
  va_list ap;
  for (;;) {
    va_start(ap, fmt);
    size_t room = b->cap - b->n;
    int k = vsnprintf(b->s ? b->s + b->n : NULL, b->s ? room : 0, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    if (b->s && (size_t)k < room) {
      b->n += (size_t)k;
      return;
    }
    b->cap = (b->cap + (size_t)k + 1) * 2;
    b->s = realloc(b->s, b->cap);
    if (!b->n) b->s[0] = 0;
  }
}
static char *done(sb *b) { return b->s; }

/* ── the studio ───────────────────────────────────────────────────────── *
 * A dark room with soft boxes for brilliance, thin strips for the long flashes of the step cuts, and small hot
 * points for fire; straight overhead (where the viewer stands) it is dark, which is what draws the dark half of a
 * cut stone's contrast pattern. World directions: x right, y up the screen, z toward the viewer. */
typedef struct box_light { float a[3], hx, hy, i[3]; } box_light;
typedef struct pin_light { float a[3], r, i[3]; } pin_light;
static const box_light boxes[] = {
  { { -0.45f, 0.54f, 0.72f }, 0.30f, 0.22f, { 5.0f, 4.7f, 4.2f } },   /* the key, upper left, warm */
  { { 0.70f, 0.10f, 0.70f }, 0.16f, 0.36f, { 1.6f, 1.8f, 2.2f } },    /* the fill, right, cool */
  { { 0.00f, -0.80f, 0.60f }, 0.60f, 0.025f, { 2.0f, 1.8f, 1.5f } },  /* a strip below */
  { { -0.88f, -0.15f, 0.45f }, 0.025f, 0.55f, { 2.0f, 1.8f, 1.5f } }, /* a strip to the left */
  { { 0.25f, 0.88f, 0.40f }, 0.50f, 0.025f, { 1.8f, 1.8f, 1.9f } },    /* a strip above */
  /* the ceiling: six panels in a ring round the viewer, with dark gaps between, where the light a well cut stone
   * sends back up comes from (brilliance, and the contrast of the gaps) */
  { { 0.4532f, 0.1214f, 0.8829f }, 0.18f, 0.19f, { 3.4f, 3.2f, 2.9f } },
  { { 0.1214f, 0.4532f, 0.8829f }, 0.18f, 0.19f, { 2.5f, 2.5f, 2.6f } },
  { { -0.3318f, 0.3318f, 0.8829f }, 0.18f, 0.19f, { 3.6f, 3.4f, 3.0f } },
  { { -0.4532f, -0.1214f, 0.8829f }, 0.18f, 0.19f, { 2.4f, 2.4f, 2.5f } },
  { { -0.1214f, -0.4532f, 0.8829f }, 0.18f, 0.19f, { 3.3f, 3.1f, 2.8f } },
  { { 0.3318f, -0.3318f, 0.8829f }, 0.18f, 0.19f, { 2.5f, 2.5f, 2.6f } },
  /* and an inner ring of small lamps close round the viewer: the bright arrows under a brilliant's table */
  { { 0.2078f, 0.0861f, 0.9744f }, 0.06f, 0.05f, { 3.0f, 2.9f, 2.7f } }, { { 0.0861f, 0.2078f, 0.9744f }, 0.06f, 0.05f, { 2.6f, 2.6f, 2.7f } },
  { { -0.0861f, 0.2078f, 0.9744f }, 0.06f, 0.05f, { 3.0f, 2.9f, 2.7f } }, { { -0.2078f, 0.0861f, 0.9744f }, 0.06f, 0.05f, { 2.6f, 2.6f, 2.7f } },
  { { -0.2078f, -0.0861f, 0.9744f }, 0.06f, 0.05f, { 3.0f, 2.9f, 2.7f } }, { { -0.0861f, -0.2078f, 0.9744f }, 0.06f, 0.05f, { 2.6f, 2.6f, 2.7f } },
  { { 0.0861f, -0.2078f, 0.9744f }, 0.06f, 0.05f, { 3.0f, 2.9f, 2.7f } }, { { 0.2078f, -0.0861f, 0.9744f }, 0.06f, 0.05f, { 2.6f, 2.6f, 2.7f } },
};
#define NPIN 320
static pin_light pins[NPIN];
/* small hot lamps on a golden-angle spiral from 6 to 52 degrees off the zenith: as a stone rocks, its facets sweep
 * across them, and each one split by dispersion is a fleck of fire */
static void make_pins(void) {
  for (int i = 0; i < NPIN; i++) {
    float th = (6.0f + 46.0f * sqrtf(((float)i + 0.5f) / (float)NPIN)) * 3.14159265f / 180.0f, ph = (float)i * 2.39996323f + 0.4f;
    pins[i].a[0] = sinf(th) * cosf(ph), pins[i].a[1] = sinf(th) * sinf(ph), pins[i].a[2] = cosf(th);
    pins[i].r = 0.0035f + 0.0025f * (float)((i * 7) % 5) / 4.0f;
    float k = 100.0f + 100.0f * (float)((i * 3) % 4) / 3.0f, warm = (float)(i % 3) * 0.06f;
    pins[i].i[0] = k, pins[i].i[1] = k * (0.97f - warm * 0.5f), pins[i].i[2] = k * (0.93f - warm);
  }
}
#define NBOX ((int)(sizeof boxes / sizeof boxes[0]))

static void norm3(const float *a, float *o) {
  float l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  o[0] = a[0] / l, o[1] = a[1] / l, o[2] = a[2] / l;
}

static void studio_fn(sb *b, bool w) {
  make_pins();
  if (w)
    put(b, "fn box(d: vec3f, a: vec3f, t1: vec3f, t2: vec3f, h: vec2f, soft: f32) -> f32 {\n"
           "  let ca = dot(d, a);\n"
           "  if (ca <= 0.0) { return 0.0; }\n"
           "  let q = vec2f(dot(d, t1), dot(d, t2)) / ca;\n"
           "  let e = abs(q) - h;\n"
           "  let m = 1.0 - smoothstep(-soft, soft, max(e.x, e.y));\n"
           "  let r = q / h;\n"
           "  return m * (1.0 - 0.15 * min(dot(r, r), 2.0));\n"
           "}\n"
           "fn pin(d: vec3f, a: vec3f, r: f32, soft: f32) -> f32 { return smoothstep(cos(r + soft), cos(r * 0.4), dot(d, a)); }\n"
           "fn studio(d0: vec3f, rough: f32) -> vec3f {\n"
           "  let ca = cos(u.u_cam.w); let sa = sin(u.u_cam.w);\n"
           "  let d = vec3f(ca * d0.x - sa * d0.y, sa * d0.x + ca * d0.y, d0.z);\n"
           "  let up = d.z;\n"
           "  var c = mix(vec3f(0.010, 0.0085, 0.0075), vec3f(0.022, 0.019, 0.017), smoothstep(-0.05, 0.6, up));\n"
           "  c += vec3f(0.045, 0.036, 0.028) * exp(-pow((up - 0.16) / 0.10, 2.0));\n"
           "  if (up < 0.0) { c = vec3f(0.004, 0.0042, 0.006); }\n"
           "  c *= 1.0 - smoothstep(0.984, 0.9935, up);\n"
           "  let s = 0.012 + rough;\n");
  else
    put(b, "float box(vec3 d, vec3 a, vec3 t1, vec3 t2, vec2 h, float soft) {\n"
           "  float ca = dot(d, a);\n"
           "  if (ca <= 0.0) return 0.0;\n"
           "  vec2 q = vec2(dot(d, t1), dot(d, t2)) / ca;\n"
           "  vec2 e = abs(q) - h;\n"
           "  float m = 1.0 - smoothstep(-soft, soft, max(e.x, e.y));\n"
           "  vec2 r = q / h;\n"
           "  return m * (1.0 - 0.15 * min(dot(r, r), 2.0));\n"
           "}\n"
           "float pin(vec3 d, vec3 a, float r, float soft) { return smoothstep(cos(r + soft), cos(r * 0.4), dot(d, a)); }\n"
           "vec3 studio(vec3 d0, float rough) {\n"
           "  float ca = cos(u_cam.w), sa = sin(u_cam.w);\n"
           "  vec3 d = vec3(ca * d0.x - sa * d0.y, sa * d0.x + ca * d0.y, d0.z);\n"
           "  float up = d.z;\n"
           "  vec3 c = mix(vec3(0.010, 0.0085, 0.0075), vec3(0.022, 0.019, 0.017), smoothstep(-0.05, 0.6, up));\n"
           "  c += vec3(0.045, 0.036, 0.028) * exp(-pow((up - 0.16) / 0.10, 2.0));\n"
           "  if (up < 0.0) c = vec3(0.004, 0.0042, 0.006);\n"
           "  c *= 1.0 - smoothstep(0.984, 0.9935, up);\n"
           "  float s = 0.012 + rough;\n");
  const char *v3 = w ? "vec3f" : "vec3", *v2 = w ? "vec2f" : "vec2";
  for (int i = 0; i < NBOX; i++) {
    float a[3], t1[3], t2[3];
    norm3(boxes[i].a, a);
    /* the box's frame: t1 level (in the xy plane), t2 across */
    float z[3] = { 0, 0, 1 }, c1[3] = { a[1] * z[2] - a[2] * z[1], a[2] * z[0] - a[0] * z[2], a[0] * z[1] - a[1] * z[0] };
    norm3(c1, t1);
    float c2[3] = { t1[1] * a[2] - t1[2] * a[1], t1[2] * a[0] - t1[0] * a[2], t1[0] * a[1] - t1[1] * a[0] };
    norm3(c2, t2);
    put(b, "  c += %s(%.4f, %.4f, %.4f) * box(d, %s(%.6f, %.6f, %.6f), %s(%.6f, %.6f, %.6f), %s(%.6f, %.6f, %.6f), %s(%.4f, %.4f), s);\n", v3,
        boxes[i].i[0], boxes[i].i[1], boxes[i].i[2], v3, a[0], a[1], a[2], v3, t1[0], t1[1], t1[2], v3, t2[0], t2[1], t2[2], v2, boxes[i].hx,
        boxes[i].hy);
  }
  /* the tray's metal sees the boxes only: the hot lamps would put glints on the lattice that compete with the
   * stones' */
  put(b, w ? "  return c * u.u_tint.xyz;\n}\n" : "  return c * u_tint.xyz;\n}\n");
}

/* ── the studio as the stones see it: a texture ───────────────────────── *
 * The room and its boxes are baked once into a hemi-octahedral map of the upper sky (JW_STUDIO_N squared texels,
 * about 0.15 degrees each, bilinear), its alpha the index of the nearest hot lamp; two rows under it hold the lamps
 * (direction and radius, then colour). A stone's exit then costs one filtered fetch and one lamp, worked
 * analytically: undispersed (a disc), or along the dispersed exit (the exit's directions at 400 and 700 nm bound an
 * arc across the sky; where the arc passes the lamp, the wavelength that lands on it is found and the lamp is seen
 * in that colour, with the share of the spectrum that lands on it: spectral fire without sampling, so without
 * noise). The lamps are far apart against the arcs (0.29 rad against a few hundredths), so the nearest one is the
 * only one an exit can see. */
static float sstep(float a, float b, float x) {
  float t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}
static void box_frame(const box_light *bx, float a[3], float t1[3], float t2[3]) {
  norm3(bx->a, a);
  float z[3] = { 0, 0, 1 }, c1[3] = { a[1] * z[2] - a[2] * z[1], a[2] * z[0] - a[0] * z[2], a[0] * z[1] - a[1] * z[0] };
  norm3(c1, t1);
  float c2[3] = { t1[1] * a[2] - t1[2] * a[1], t1[2] * a[0] - t1[0] * a[2], t1[0] * a[1] - t1[1] * a[0] };
  norm3(c2, t2);
}
static void studio_c(const float d[3], float out[3]) {
  float up = d[2];
  if (up < 0) {
    out[0] = 0.004f, out[1] = 0.0042f, out[2] = 0.006f;
    return;
  }
  float m = sstep(-0.05f, 0.6f, up), g = expf(-powf((up - 0.16f) / 0.10f, 2.0f)), z = 1 - sstep(0.984f, 0.9935f, up);
  out[0] = (0.010f + (0.022f - 0.010f) * m + 0.045f * g) * z;
  out[1] = (0.0085f + (0.019f - 0.0085f) * m + 0.036f * g) * z;
  out[2] = (0.0075f + (0.017f - 0.0075f) * m + 0.028f * g) * z;
  for (int i = 0; i < NBOX; i++) {
    float a[3], t1[3], t2[3];
    box_frame(&boxes[i], a, t1, t2);
    float ca = d[0] * a[0] + d[1] * a[1] + d[2] * a[2];
    if (ca <= 0) continue;
    float q0 = (d[0] * t1[0] + d[1] * t1[1] + d[2] * t1[2]) / ca, q1 = (d[0] * t2[0] + d[1] * t2[1] + d[2] * t2[2]) / ca;
    float e = fmaxf(fabsf(q0) - boxes[i].hx, fabsf(q1) - boxes[i].hy);
    float k = 1 - sstep(-0.012f, 0.012f, e);
    if (k <= 0) continue;
    float r0 = q0 / boxes[i].hx, r1 = q1 / boxes[i].hy;
    k *= 1 - 0.15f * fminf(r0 * r0 + r1 * r1, 2.0f);
    for (int j = 0; j < 3; j++) out[j] += boxes[i].i[j] * k * 1.3f; /* the stones see the boxes a little hotter than the tray does */
  }
}
void jw_studio_rows(float *img, int row0, int nrows) {
  const int N = JW_STUDIO_N;
  make_pins();
  for (int row = row0; row < row0 + nrows && row < N + 2; row++) {
    float *px = img + (size_t)row * N * 4;
    if (row >= N) { /* the lamps */
      for (int i = 0; i < N * 4; i++) px[i] = 0;
      for (int i = 0; i < NPIN; i++) {
        float a[3];
        norm3(pins[i].a, a);
        if (row == N) px[i * 4] = a[0], px[i * 4 + 1] = a[1], px[i * 4 + 2] = a[2], px[i * 4 + 3] = pins[i].r;
        else px[i * 4] = pins[i].i[0], px[i * 4 + 1] = pins[i].i[1], px[i * 4 + 2] = pins[i].i[2];
      }
      continue;
    }
    for (int col = 0; col < N; col++) {
      float sx = (float)col / (float)(N - 1) * 2 - 1, sy = (float)row / (float)(N - 1) * 2 - 1;
      float p0 = (sx + sy) * 0.5f, p1 = (sx - sy) * 0.5f, z = fmaxf(1 - fabsf(p0) - fabsf(p1), 0);
      float d[3] = { p0, p1, z };
      norm3(d, d);
      studio_c(d, px + col * 4);
      float best = -2;
      int bi = -1;
      if (d[2] > 0.55f)
        for (int i = 0; i < NPIN; i++) {
          float a[3];
          norm3(pins[i].a, a);
          float c = d[0] * a[0] + d[1] * a[1] + d[2] * a[2];
          if (c > best) best = c, bi = i;
        }
      px[col * 4 + 3] = (float)bi;
    }
  }
}

static void gem_env_fns(sb *b, bool w) {
  if (w)
    put(b, "fn drift(d0: vec3f) -> vec3f { let ca = cos(u.u_cam.w); let sa = sin(u.u_cam.w); return vec3f(ca * d0.x - sa * d0.y, sa * d0.x + ca * d0.y, d0.z); }\n"
           "const SN = %d.0;\nconst SH = %d.0;\n"
           "fn studioT(d: vec3f) -> vec4f {\n"
           "  if (d.z < 0.0) { return vec4f(0.004, 0.0042, 0.006, -1.0); }\n"
           "  let p = d.xy / (abs(d.x) + abs(d.y) + d.z);\n"
           "  let s = (vec2f(p.x + p.y, p.x - p.y) * 0.5 + 0.5) * (SN - 1.0);\n"
           "  let c = textureSampleLevel(u_t0, cw_linear, (s + 0.5) / vec2f(SN, SH), 0.0).rgb;\n"
           "  let id = textureLoad(u_t0, vec2i(s + 0.5), 0).a;\n"
           "  return vec4f(c, id);\n"
           "}\n"
           "fn pinMono(d: vec3f, id: f32) -> vec3f {\n"
           "  if (id < -0.5) { return vec3f(0.0); }\n"
           "  let pa = textureLoad(u_t0, vec2i(i32(id + 0.5), %d), 0);\n"
           "  let pc = textureLoad(u_t0, vec2i(i32(id + 0.5), %d), 0).rgb;\n"
           "  return pc * smoothstep(cos(pa.w + 0.006), cos(pa.w * 0.4), dot(d, pa.xyz));\n"
           "}\n"
           "fn lamOf(t: f32) -> f32 { return 1.0 / sqrt(1.0 / (400.0 * 400.0) + t * (1.0 / (700.0 * 700.0) - 1.0 / (400.0 * 400.0))); }\n"
           "fn pinDisp(e0: vec3f, e1: vec3f, id: f32) -> vec3f {\n"
           "  if (id < -0.5) { return vec3f(0.0); }\n"
           "  let pa = textureLoad(u_t0, vec2i(i32(id + 0.5), %d), 0);\n"
           "  let R = pa.w + 0.006;\n"
           "  let seg = e1 - e0; let sl2 = max(dot(seg, seg), 1e-14);\n"
           "  let t = clamp(dot(pa.xyz - e0, seg) / sl2, 0.0, 1.0);\n"
           "  let d = length(e0 + seg * t - pa.xyz);\n"
           "  if (d >= R) { return vec3f(0.0); }\n"
           "  let pc = textureLoad(u_t0, vec2i(i32(id + 0.5), %d), 0).rgb;\n"
           "  let fr = min(1.0, 2.0 * pa.w / sqrt(sl2));\n"
           "  return pc * (1.0 - smoothstep(pa.w * 0.4, R, d)) * fr * mix(spec(lamOf(t)), vec3f(1.0), fr);\n"
           "}\n",
        JW_STUDIO_N, JW_STUDIO_N + 2, JW_STUDIO_N, JW_STUDIO_N + 1, JW_STUDIO_N, JW_STUDIO_N + 1);
  else
    put(b, "vec3 drift(vec3 d0) { float ca = cos(u_cam.w), sa = sin(u_cam.w); return vec3(ca * d0.x - sa * d0.y, sa * d0.x + ca * d0.y, d0.z); }\n"
           "uniform sampler2D u_t0;\n"
           "const float SN = %d.0, SH = %d.0;\n"
           "vec4 studioT(vec3 d) {\n"
           "  if (d.z < 0.0) return vec4(0.004, 0.0042, 0.006, -1.0);\n"
           "  vec2 p = d.xy / (abs(d.x) + abs(d.y) + d.z);\n"
           "  vec2 s = (vec2(p.x + p.y, p.x - p.y) * 0.5 + 0.5) * (SN - 1.0);\n"
           "  vec3 c = texture(u_t0, (s + 0.5) / vec2(SN, SH)).rgb;\n"
           "  float id = texelFetch(u_t0, ivec2(s + 0.5), 0).a;\n"
           "  return vec4(c, id);\n"
           "}\n"
           "vec3 pinMono(vec3 d, float id) {\n"
           "  if (id < -0.5) return vec3(0.0);\n"
           "  vec4 pa = texelFetch(u_t0, ivec2(int(id + 0.5), %d), 0);\n"
           "  vec3 pc = texelFetch(u_t0, ivec2(int(id + 0.5), %d), 0).rgb;\n"
           "  return pc * smoothstep(cos(pa.w + 0.006), cos(pa.w * 0.4), dot(d, pa.xyz));\n"
           "}\n"
           "float lamOf(float t) { return 1.0 / sqrt(1.0 / (400.0 * 400.0) + t * (1.0 / (700.0 * 700.0) - 1.0 / (400.0 * 400.0))); }\n"
           "vec3 pinDisp(vec3 e0, vec3 e1, float id) {\n"
           "  if (id < -0.5) return vec3(0.0);\n"
           "  vec4 pa = texelFetch(u_t0, ivec2(int(id + 0.5), %d), 0);\n"
           "  float R = pa.w + 0.006;\n"
           "  vec3 seg = e1 - e0; float sl2 = max(dot(seg, seg), 1e-14);\n"
           "  float t = clamp(dot(pa.xyz - e0, seg) / sl2, 0.0, 1.0);\n"
           "  float d = length(e0 + seg * t - pa.xyz);\n"
           "  if (d >= R) return vec3(0.0);\n"
           "  vec3 pc = texelFetch(u_t0, ivec2(int(id + 0.5), %d), 0).rgb;\n"
           "  float fr = min(1.0, 2.0 * pa.w / sqrt(sl2));\n"
           "  return pc * (1.0 - smoothstep(pa.w * 0.4, R, d)) * fr * mix(spec(lamOf(t)), vec3(1.0), fr);\n"
           "}\n",
        JW_STUDIO_N, JW_STUDIO_N + 2, JW_STUDIO_N, JW_STUDIO_N + 1, JW_STUDIO_N, JW_STUDIO_N + 1);
}

/* the CIE 1931 observer (an analytic multi-lobe fit) to linear sRGB, scaled so 400-700 nm averages to white */
static void spectrum_fn(sb *b, bool w) {
  float k[3] = { 0 };
  {
    float s[3];
    jw_spectrum_rgb(550, s); /* fills the normalisation */
    float raw[3];
    (void)raw;
  }
  /* the scale: the same as cuts.c (recomputed here so the shader stands alone) */
  {
    double sum[3] = { 0 };
    int n = 0;
    for (float l = 400; l <= 700; l += 0.5f, n++) {
      float s[3];
      jw_spectrum_rgb(l, s);
      for (int i = 0; i < 3; i++) sum[i] += s[i];
    }
    /* jw_spectrum_rgb is already scaled; the shader's raw fit needs the scale itself */
    float raw_sum[3] = { 0 };
    for (float l = 400; l <= 700; l += 0.5f) {
      float lob[8];
      float mu[] = { 599.8f, 442.0f, 501.1f, 568.8f, 530.9f, 437.0f, 459.0f };
      float s1[] = { 37.9f, 16.0f, 20.4f, 46.9f, 16.3f, 11.8f, 26.0f }, s2[] = { 31.0f, 26.7f, 26.2f, 40.5f, 31.1f, 36.0f, 13.8f };
      for (int i = 0; i < 7; i++) {
        float t = (l - mu[i]) / (l < mu[i] ? s1[i] : s2[i]);
        lob[i] = expf(-0.5f * t * t);
      }
      float X = 1.056f * lob[0] + 0.362f * lob[1] - 0.065f * lob[2], Y = 0.821f * lob[3] + 0.286f * lob[4], Z = 1.217f * lob[5] + 0.681f * lob[6];
      raw_sum[0] += fmaxf(0, 3.2406f * X - 1.5372f * Y - 0.4986f * Z);
      raw_sum[1] += fmaxf(0, -0.9689f * X + 1.8758f * Y + 0.0415f * Z);
      raw_sum[2] += fmaxf(0, 0.0557f * X - 0.2040f * Y + 1.0570f * Z);
    }
    for (int i = 0; i < 3; i++) k[i] = (float)n / raw_sum[i];
  }
  if (w)
    put(b, "fn lobe(x: f32, m: f32, s1: f32, s2: f32) -> f32 { let t = (x - m) / select(s2, s1, x < m); return exp(-0.5 * t * t); }\n"
           "fn spec(l: f32) -> vec3f {\n"
           "  let X = 1.056 * lobe(l, 599.8, 37.9, 31.0) + 0.362 * lobe(l, 442.0, 16.0, 26.7) - 0.065 * lobe(l, 501.1, 20.4, 26.2);\n"
           "  let Y = 0.821 * lobe(l, 568.8, 46.9, 40.5) + 0.286 * lobe(l, 530.9, 16.3, 31.1);\n"
           "  let Z = 1.217 * lobe(l, 437.0, 11.8, 36.0) + 0.681 * lobe(l, 459.0, 26.0, 13.8);\n"
           "  let c = max(vec3f(3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z, 0.0557 * X - 0.2040 * Y + 1.0570 * Z), vec3f(0.0));\n"
           "  return c * vec3f(%.6f, %.6f, %.6f);\n}\n",
        k[0], k[1], k[2]);
  else
    put(b, "float lobe(float x, float m, float s1, float s2) { float t = (x - m) / (x < m ? s1 : s2); return exp(-0.5 * t * t); }\n"
           "vec3 spec(float l) {\n"
           "  float X = 1.056 * lobe(l, 599.8, 37.9, 31.0) + 0.362 * lobe(l, 442.0, 16.0, 26.7) - 0.065 * lobe(l, 501.1, 20.4, 26.2);\n"
           "  float Y = 0.821 * lobe(l, 568.8, 46.9, 40.5) + 0.286 * lobe(l, 530.9, 16.3, 31.1);\n"
           "  float Z = 1.217 * lobe(l, 437.0, 11.8, 36.0) + 0.681 * lobe(l, 459.0, 26.0, 13.8);\n"
           "  vec3 c = max(vec3(3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z, 0.0557 * X - 0.2040 * Y + 1.0570 * Z), 0.0);\n"
           "  return c * vec3(%.6f, %.6f, %.6f);\n}\n",
        k[0], k[1], k[2]);
}

/* the canvas and the tray: shared by every program */
static const char *GL_COMMON =
    "uniform vec4 u_view;\n"
    "uniform vec4 u_cam;\n"
    "uniform vec4 u_tint;\n"
    "vec2 canvasOf(vec2 fc) { return vec2(fc.x * 1920.0 * u_view.z, 1080.0 - fc.y * 1080.0 * u_view.w); }\n"
    "vec3 trayPoint(vec2 c) { return vec3(c.x - 960.0, 540.0 - c.y, 0.0); }\n"
    "vec4 canvasClip(vec2 p) { return vec4(p.x / 960.0 - 1.0, 1.0 - p.y / 540.0, 0.0, 1.0); }\n"
    "vec2 corner(int c) { return c == 1 ? vec2(1.0, 0.0) : (c == 2 || c == 4) ? vec2(1.0) : c == 5 ? vec2(0.0, 1.0) : vec2(0.0); }\n";
/* WGSL: the uniform block's head (each program appends its own members and closes it) */
static const char *WG_COMMON_FNS =
    "fn canvasOf(fc: vec2f) -> vec2f { return vec2f(fc.x * 1920.0 * u.u_view.z, 1080.0 - fc.y * 1080.0 * u.u_view.w); }\n"
    "fn trayPoint(c: vec2f) -> vec3f { return vec3f(c.x - 960.0, 540.0 - c.y, 0.0); }\n"
    "fn canvasClip(p: vec2f) -> vec4f { return vec4f(p.x / 960.0 - 1.0, (1.0 - p.y / 540.0) * cw_target.flip, 0.5, 1.0); }\n"
    "fn corner(c: i32) -> vec2f { if (c == 1) { return vec2f(1.0, 0.0); } if (c == 2 || c == 4) { return vec2f(1.0); } if (c == 5) { return vec2f(0.0, 1.0); } return vec2f(0.0); }\n";

#include "shaders_wgsl.h"

static void key_consts(sb *b, bool w) {
  if (w) put(b, "const KEY = vec3f(%.7f, %.7f, %.7f);\n", jw_key_dir[0], jw_key_dir[1], jw_key_dir[2]);
  else put(b, "const vec3 KEY = vec3(%.7f, %.7f, %.7f);\n", jw_key_dir[0], jw_key_dir[1], jw_key_dir[2]);
}

/* ── the tray ─────────────────────────────────────────────────────────── */

static const char *TRAY_GL =
    "uniform vec4 u_misc;\n"
    "uniform vec4 u_fl0; uniform vec4 u_fl1; uniform vec4 u_fl2; uniform vec4 u_fl3;\n"
    "uniform vec4 u_fc0; uniform vec4 u_fc1; uniform vec4 u_fc2; uniform vec4 u_fc3;\n"
    "out vec4 o;\n"
    "float h21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
    "float vnoise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x), mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y); }\n"
    "float fbm(vec2 p) { return 0.5 * vnoise(p) + 0.25 * vnoise(p * 2.03 + 7.1) + 0.125 * vnoise(p * 4.01 + 3.3) + 0.0625 * vnoise(p * 8.05 + 1.7); }\n"
    "float sdBox(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\n"
    "vec3 keyCol() { return vec3(3.0, 2.85, 2.6) * u_tint.xyz; }\n"
    /* the flashes of the bursts: point lights a little over the tray */
    "vec3 flashAt(vec3 P, vec3 N, vec4 fl, vec4 fc, float shin, out vec3 spec) {\n"
    "  vec3 L = fl.xyz - P; float d2 = dot(L, L); L = normalize(L);\n"
    "  float k = fl.w / (1.0 + d2 / (220.0 * 220.0));\n"
    "  vec3 V = normalize(u_cam.xyz - P), H = normalize(L + V);\n"
    "  spec = fc.xyz * k * pow(max(dot(N, H), 0.0), shin);\n"
    "  return fc.xyz * k * max(dot(N, L), 0.0);\n"
    "}\n"
    "vec3 flashes(vec3 P, vec3 N, float shin, out vec3 spec) {\n"
    "  vec3 s0, s1, s2, s3;\n"
    "  vec3 d = flashAt(P, N, u_fl0, u_fc0, shin, s0) + flashAt(P, N, u_fl1, u_fc1, shin, s1) + flashAt(P, N, u_fl2, u_fc2, shin, s2) + flashAt(P, N, u_fl3, u_fc3, shin, s3);\n"
    "  spec = s0 + s1 + s2 + s3;\n"
    "  return d;\n"
    "}\n"
    /* the tufting: a button at each corner of the cells pulls the velvet down into a dimple, and each stone sits in
     * a soft round dish of its own, the velvet it pushed aside a little raised round it */
    "float tuftH(vec2 c) {\n"
    "  vec2 o2 = c - vec2(72.0, 60.0);\n"
    "  vec2 lp = o2 - floor(o2 / 120.0) * 120.0 - 60.0;\n"
    "  vec2 a = abs(lp);\n"
    "  float r = length(lp);\n"
    "  float dish = -8.0 * (1.0 - smoothstep(34.0, 58.0, r)) + 1.6 * exp(-(r - 63.0) * (r - 63.0) / 50.0);\n"
    "  float dc = length(vec2(60.0) - a);\n"
    "  float pull = -8.0 * exp(-dc * dc / (2.0 * 12.0 * 12.0));\n"
    "  return dish + pull;\n"
    "}\n"
    /* velvet: a deep navy pile with an inverted sheen (Charlie's distribution: brightest where the fibres are seen
     * side on, so the slopes that face away from the light glow and the flats stay dark), a rim of the room's light
     * at grazing view, and patches where the pile lies another way (crush) */
    "vec3 velvet(vec3 P, vec3 N, float tone, float crush) {\n"
    "  vec3 V = normalize(u_cam.xyz - P);\n"
    "  float nl = max(dot(N, KEY), 0.0), nv = max(dot(N, V), 1e-3);\n"
    "  vec3 H = normalize(KEY + V);\n"
    "  float nh = dot(N, H), sh = sqrt(max(1.0 - nh * nh, 0.0));\n"
    "  float D = (2.0 + 1.0 / 0.4) * pow(sh, 1.0 / 0.4) / 6.2831853;\n"
    "  float vis = 1.0 / (4.0 * (nl + nv - nl * nv) + 1e-3);\n"
    "  vec3 alb = vec3(0.0034, 0.0044, 0.0112) * tone;\n"
    "  vec3 shc = vec3(0.22, 0.28, 0.62) * crush * tone;\n"
    "  float rim = pow(1.0 - nv, 2.0);\n"
    "  vec3 fs; vec3 fd = flashes(P, N, 8.0, fs);\n"
    "  return alb * (nl * keyCol() + 0.10 + fd) + shc * (D * vis * nl * keyCol() * 0.55 + rim * (0.5 + fd * 0.25)) + alb * fs * 0.4;\n"
    "}\n"
    /* polished gold: the studio in a mirror of gold */
    "vec3 gold(vec3 P, vec3 N, float rough) {\n"
    "  vec3 V = normalize(u_cam.xyz - P), R = reflect(-V, N);\n"
    "  vec3 F0 = vec3(1.0, 0.71, 0.29);\n"
    "  float nv = max(dot(N, V), 0.0);\n"
    "  vec3 F = F0 + (1.0 - F0) * pow(1.0 - nv, 5.0);\n"
    "  vec3 fs; vec3 fd = flashes(P, N, 90.0, fs);\n"
    "  return F * (studio(R, rough) * 1.1 + fs * 2.0) + F0 * 0.03 * (max(dot(N, KEY), 0.0) * keyCol() + fd);\n"
    "}\n"
    /* walnut under a clear lacquer: growth rings that wander with the figure, darker latewood and mineral streaks,
     * open pores drawn out along the grain, a chatoyant shimmer across it; the lacquer a clear coat over all (a
     * hair of orange peel), the room in it where it turns toward the light */
    "vec3 walnut(vec3 P, vec3 N, float along, float across, float seed) {\n"
    "  float w = across + 7.0 * fbm(vec2(along * 0.003 + seed, across * 0.012)) + 2.0 * sin(along * 0.011 + seed * 3.0);\n"
    "  float ring = fract(w * 0.085 + 0.4 * fbm(vec2(along * 0.006, seed * 2.0)));\n"
    "  float late = smoothstep(0.45, 0.92, ring) * (1.0 - smoothstep(0.92, 1.0, ring));\n"
    "  float streak = smoothstep(0.35, 0.8, fbm(vec2(along * 0.0015 + seed, w * 0.05)));\n"
    "  float pore = smoothstep(0.72, 0.95, vnoise(vec2(along * 0.45, w * 1.6)));\n"
    "  vec3 alb = mix(vec3(0.078, 0.037, 0.017), vec3(0.028, 0.012, 0.0055), clamp(0.6 * late + 0.45 * streak, 0.0, 1.0));\n"
    "  alb *= 1.0 - 0.4 * pore;\n"
    "  vec3 V = normalize(u_cam.xyz - P);\n"
    "  vec3 Nc = normalize(N + vec3(vnoise(vec2(along * 0.05, across * 0.05)) - 0.5, vnoise(vec2(across * 0.05 + 3.0, along * 0.05)) - 0.5, 0.0) * 0.012);\n"
    "  vec3 R = reflect(-V, Nc);\n"
    "  float nv = max(dot(Nc, V), 0.0), Fc = 0.04 + 0.96 * pow(1.0 - nv, 5.0);\n"
    "  float chat = 0.5 + 0.5 * sin(w * 0.6 + along * 0.002);\n"
    "  vec3 fs; vec3 fd = flashes(P, N, 40.0, fs);\n"
    "  vec3 Hk = normalize(KEY + V);\n"
    "  float spec = pow(max(dot(Nc, Hk), 0.0), 90.0);\n"
    "  return alb * (max(dot(N, KEY), 0.0) * keyCol() * (0.85 + 0.3 * chat * (1.0 - late)) + 0.10 + fd) + Fc * (studio(R, 0.01) + fs * 0.5) + spec * keyCol() * 0.03;\n"
    "}\n"
    /* a frame round a rectangle: lacquered walnut with a rounded outer edge, a hairline of gold inlaid in its flat, a
     * gold bead inside, a small step down to the velvet */
    "vec3 frame(vec2 c, vec3 P, vec2 ctr, vec2 hb, float seed) {\n"
    "  float d = sdBox(c - ctr, hb + 30.0, 22.0);\n"
    "  vec2 e = vec2(1.0, 0.0);\n"
    "  vec2 gr = vec2(sdBox(c + e.xy - ctr, hb + 30.0, 22.0) - sdBox(c - e.xy - ctr, hb + 30.0, 22.0),\n"
    "                 sdBox(c + e.yx - ctr, hb + 30.0, 22.0) - sdBox(c - e.yx - ctr, hb + 30.0, 22.0)) * 0.5;\n"
    "  vec2 o2 = normalize(gr + 1e-6);\n"
    "  vec2 ow = vec2(o2.x, -o2.y);\n" /* outward, in world xy */
    "  float s = clamp(-d / 30.0, 0.0, 1.0);\n"
    "  bool horiz = abs(o2.y) > abs(o2.x);\n"
    "  float along = horiz ? c.x : c.y, across = horiz ? c.y : c.x;\n"
    "  vec3 col;\n"
    "  if (s < 0.38) {\n"
    "    float k = s / 0.38;\n"
    "    float slope = 1.6 * (1.0 - k) / sqrt(max(1.0 - (1.0 - k) * (1.0 - k), 0.02));\n"
    "    col = walnut(P, normalize(vec3(ow * slope * 0.9, 1.0)), along, across, seed);\n"
    "  } else if (s < 0.72) {\n"
    "    float il = (s - 0.555) / 0.022;\n" /* the inlay: a rounded hairline */
    "    if (abs(il) < 1.0) col = gold(P, normalize(vec3(ow * il * 0.9, sqrt(max(1.0 - il * il * 0.8, 0.05)))), 0.03) * 0.8;\n"
    "    else col = walnut(P, normalize(vec3(-ow * 0.03, 1.0)), along, across, seed) * (0.8 + 0.2 * smoothstep(1.0, 2.2, abs(il)));\n"
    "  } else if (s < 0.90) {\n"
    "    float k = (s - 0.81) / 0.09;\n"
    "    col = gold(P, normalize(vec3(ow * -k * 1.4, sqrt(max(1.0 - k * k, 0.05)))), 0.03) * (0.85 + 0.15 * vnoise(vec2(along * 0.4, 3.0)));\n"
    "  } else {\n"
    "    col = walnut(P, normalize(vec3(ow * -0.8, 1.0)), along, across, seed) * 0.35;\n"
    "  }\n"
    "  col *= 0.55 + 0.45 * smoothstep(0.0, 4.0, -d);\n" /* the outer edge rolls into shadow */
    "  return col;\n"
    "}\n"
    "vec3 backdrop(vec2 c) {\n"
    "  vec3 col = vec3(0.0052, 0.0044, 0.0040);\n"
    "  vec2 a = (c - vec2(560.0, 180.0)) / vec2(1100.0, 700.0);\n"
    "  col += vec3(0.050, 0.036, 0.024) * exp(-dot(a, a) * 2.0);\n"
    "  vec2 b = (c - vec2(1700.0, 980.0)) / vec2(700.0, 500.0);\n"
    "  col += vec3(0.014, 0.016, 0.024) * exp(-dot(b, b) * 2.0);\n"
    /* lamps far behind, out of focus */
    "  vec4 bk[7] = vec4[7](vec4(400.0, 12.0, 26.0, 0.030), vec4(1905.0, 300.0, 22.0, 0.026), vec4(1316.0, 520.0, 14.0, 0.020),\n"
    "                       vec4(980.0, 1068.0, 20.0, 0.020), vec4(1316.0, 14.0, 18.0, 0.030), vec4(14.0, 640.0, 18.0, 0.018), vec4(1600.0, 1070.0, 16.0, 0.022));\n"
    "  for (int i = 0; i < 7; i++) {\n"
    "    float d = length(c - bk[i].xy), r = bk[i].z;\n"
    "    float disc = smoothstep(r, r - 6.0, d) * (0.75 + 0.35 * smoothstep(r * 0.55, r, d));\n"
    "    col += vec3(1.0, 0.72, 0.42) * bk[i].w * disc;\n"
    "  }\n"
    /* the frames' shadows on the wall behind */
    "  float s1 = sdBox(c - vec2(672.0, 540.0) - vec2(18.0, 24.0), vec2(630.0, 510.0), 22.0);\n"
    "  float s2 = sdBox(c - vec2(1600.0, 540.0) - vec2(18.0, 24.0), vec2(280.0, 510.0), 22.0);\n"
    "  col *= 0.35 + 0.65 * smoothstep(-6.0, 46.0, min(s1, s2));\n"
    "  return col;\n"
    "}\n"
    "void main() {\n"
    "  vec2 c = canvasOf(gl_FragCoord.xy);\n"
    "  vec3 P = trayPoint(c);\n"
    "  vec3 col;\n"
    "  vec2 bc = vec2(672.0, 540.0), bh = vec2(600.0, 480.0);\n"
    "  vec2 pc = vec2(1600.0, 540.0), ph = vec2(250.0, 480.0);\n"
    "  float dB = sdBox(c - bc, bh + 30.0, 22.0), dP = sdBox(c - pc, ph + 30.0, 22.0);\n"
    "  vec2 qb = abs(c - bc) - bh, qp = abs(c - pc) - ph;\n"
    "  float crush = 0.8 + 0.45 * fbm(c * 0.006 + vec2(3.1, 7.7));\n" /* the pile's lie, in broad soft patches */
    "  if (qb.x < 0.0 && qb.y < 0.0) {\n"
    /* the board: tufted velvet, a covered button where four cells meet, a dish for each stone */
    "    vec2 o2 = c - (bc - bh);\n"
    "    vec2 lp = o2 - floor(o2 / 120.0) * 120.0 - 60.0;\n"
    "    vec2 cv = lp - sign(lp) * 60.0;\n" /* from the nearest button */
    "    float dc = length(cv);\n"
    "    float tone = (0.86 + 0.28 * fbm(c * 0.035)) * (0.84 + 0.32 * h21(floor(c * 1.4)));\n"
    "    if (dc < 5.5) {\n"
    "      vec2 q = cv / 5.5;\n"
    "      vec3 N = normalize(vec3(q.x, -q.y, sqrt(max(1.0 - dot(q, q), 0.04))));\n"
    "      col = velvet(P, N, tone * 1.1, crush) * (0.7 + 0.3 * smoothstep(1.0, 0.55, length(q)));\n"
    "    } else {\n"
    "      float h0 = tuftH(c);\n"
    "      vec2 g = vec2(tuftH(c + vec2(1.0, 0.0)) - tuftH(c - vec2(1.0, 0.0)), tuftH(c + vec2(0.0, 1.0)) - tuftH(c - vec2(0.0, 1.0))) * 0.5;\n"
    "      vec3 N = normalize(vec3(-g.x, g.y, 1.0));\n"
    "      col = velvet(P, N, tone, crush);\n"
    "      col *= 0.6 + 0.4 * smoothstep(0.0, 13.0, dc - 5.5);\n" /* the folds the button draws in */
    "      col *= 0.82 + 0.18 * smoothstep(-6.0, -1.5, h0 + 8.0 * exp(-dc * dc / 288.0));\n" /* a dish's floor */
    "    }\n"
    "  } else if (dB < 0.0) {\n"
    "    col = frame(c, P, bc, bh, 1.7);\n"
    "  } else if (qp.x < 0.0 && qp.y < 0.0) {\n"
    /* the panel: the same velvet, and the hint button raised on it in a gold rim */
    "    vec3 N = normalize(vec3((c - pc) / vec2(250.0, 480.0) * vec2(-0.05, 0.05), 1.0));\n"
    "    float tone = 0.9 * (0.86 + 0.28 * fbm(c * 0.035)) * (0.84 + 0.32 * h21(floor(c * 1.4)));\n"
    "    col = velvet(P, N, tone, crush);\n"
    "    float edge = min(-qp.x, -qp.y);\n"
    "    col *= 0.35 + 0.65 * smoothstep(0.0, 10.0, edge);\n"
    /* two plaques pressed into it, each in a fine gold rim: the score, then the best and the moves */
    "    for (int i = 0; i < 2; i++) {\n"
    "      vec2 pcn = i == 0 ? vec2(1600.0, 192.0) : vec2(1600.0, 430.0), phs = i == 0 ? vec2(212.0, 112.0) : vec2(212.0, 104.0);\n"
    "      float dq = sdBox(c - pcn, phs, 20.0);\n"
    "      vec2 ge = normalize(vec2(sdBox(c + vec2(1.0, 0.0) - pcn, phs, 20.0) - sdBox(c - vec2(1.0, 0.0) - pcn, phs, 20.0), sdBox(c + vec2(0.0, 1.0) - pcn, phs, 20.0) - sdBox(c - vec2(0.0, 1.0) - pcn, phs, 20.0)) + 1e-6);\n"
    "      if (dq < 0.0 && dq > -2.4) { float k = (dq + 1.2) / 1.2; col = gold(P, normalize(vec3(vec2(ge.x, -ge.y) * k * 0.9, sqrt(max(1.0 - k * k * 0.8, 0.05)))), 0.03) * 0.8; }\n"
    "      else if (dq <= -2.4) col = velvet(P, vec3(0.0, 0.0, 1.0), tone * 0.8, crush) * (0.55 + 0.45 * smoothstep(-2.4, -16.0, dq));\n"
    "      else col *= 0.6 + 0.4 * smoothstep(0.0, 8.0, dq);\n"
    "    }\n"
    "    vec2 btc = vec2(1600.0, 932.0), bth = vec2(220.0, 64.0);\n"
    "    float db = sdBox(c - btc, bth, 26.0);\n"
    "    vec2 ee = vec2(1.0, 0.0);\n"
    "    vec2 gb = normalize(vec2(sdBox(c + ee - btc, bth, 26.0) - sdBox(c - ee - btc, bth, 26.0), sdBox(c + ee.yx - btc, bth, 26.0) - sdBox(c - ee.yx - btc, bth, 26.0)) + 1e-6);\n"
    "    if (db < 0.0 && db > -6.0) {\n"
    "      float k = (db + 3.0) / 3.0;\n"
    "      vec3 Nb = normalize(vec3(vec2(gb.x, -gb.y) * k * 1.3, sqrt(max(1.0 - k * k, 0.04))));\n"
    "      col = gold(P, Nb, 0.04) * (1.0 + 1.5 * u_misc.y);\n"
    "    } else if (db <= -6.0) {\n"
    "      float rise = (1.0 - u_misc.z) * smoothstep(-6.0, -30.0, db);\n"
    "      vec3 Nb = normalize(vec3(vec2(gb.x, -gb.y) * (1.0 - rise) * 0.5, 1.0));\n"
    "      col = velvet(P, Nb, 1.25, crush) * (1.0 - 0.4 * u_misc.z);\n"
    "      col += vec3(1.0, 0.72, 0.35) * 0.05 * u_misc.y * smoothstep(-6.0, -60.0, db);\n"
    "    } else {\n"
    "      col *= 0.4 + 0.6 * smoothstep(0.0, 14.0, db);\n" /* the button's shadow on the panel */
    "    }\n"
    "  } else if (dP < 0.0) {\n"
    "    col = frame(c, P, pc, ph, 4.2);\n"
    "  } else {\n"
    "    col = backdrop(c);\n"
    "  }\n"
    "  o = vec4(col, 1.0);\n"
    "}\n";

char *jw_shader_tray(bool w) {
  sb b = { 0 };
  if (w) {
    put(&b, "%s%s", WG_TRAY_U, WG_COMMON_FNS);
    key_consts(&b, true);
    studio_fn(&b, true);
    put(&b, "%s", WG_TRAY);
    return done(&b);
  }
  put(&b, "%s", GL_COMMON);
  key_consts(&b, false);
  studio_fn(&b, false);
  put(&b, "%s", TRAY_GL);
  return done(&b);
}

/* ── the stones ───────────────────────────────────────────────────────── */

static void hull_fns(sb *b, const jw_cut *c, bool w) {
  if (w)
    put(b, "struct HitIn { ok: bool, tn: f32, n: vec3f };\n"
           "fn hullIn(o: vec3f, d: vec3f) -> HitIn {\n"
           "  var tn = -1e30; var tf = 1e30; var nn = vec3f(0.0, 0.0, 1.0); var den: f32; var t: f32;\n");
  else
    put(b, "bool hullIn(vec3 o, vec3 d, out float tn, out vec3 nn) {\n"
           "  tn = -1e30; float tf = 1e30; nn = vec3(0.0, 0.0, 1.0); float den, t;\n");
  const char *v3 = w ? "vec3f" : "vec3";
  for (int i = 0; i < c->nplanes; i++) {
    const jw_plane *p = &c->p[i];
    put(b, "  den = dot(d, %s(%.7f, %.7f, %.7f)); t = (%.7f - dot(o, %s(%.7f, %.7f, %.7f))) / den;\n", v3, p->n[0], p->n[1], p->n[2], p->d, v3, p->n[0],
        p->n[1], p->n[2]);
    if (w) put(b, "  if (den < 0.0) { if (t > tn) { tn = t; nn = %s(%.7f, %.7f, %.7f); } } else { tf = min(tf, t); }\n", v3, p->n[0], p->n[1], p->n[2]);
    else put(b, "  if (den < 0.0) { if (t > tn) { tn = t; nn = %s(%.7f, %.7f, %.7f); } } else tf = min(tf, t);\n", v3, p->n[0], p->n[1], p->n[2]);
  }
  if (w) put(b, "  var r: HitIn; r.ok = tn <= tf && tf > 0.0 && tn > -1e29; r.tn = tn; r.n = nn; return r;\n}\n");
  else put(b, "  return tn <= tf && tf > 0.0 && tn > -1e29;\n}\n");
  if (w)
    put(b, "struct HitOut { t: f32, n: vec3f };\n"
           "fn hullOut(p: vec3f, d: vec3f) -> HitOut {\n"
           "  var te = 1e30; var n = vec3f(0.0, 0.0, 1.0); var den: f32; var t: f32;\n");
  else
    put(b, "float hullOut(vec3 p, vec3 d, out vec3 n) {\n"
           "  float te = 1e30; n = vec3(0.0, 0.0, 1.0); float den, t;\n");
  for (int i = 0; i < c->nplanes; i++) {
    const jw_plane *p = &c->p[i];
    if (w)
      put(b, "  den = dot(d, %s(%.7f, %.7f, %.7f)); t = select(1e30, (%.7f - dot(p, %s(%.7f, %.7f, %.7f))) / den, den > 1e-7); if (t < te) { te = t; n = %s(%.7f, %.7f, %.7f); }\n",
          v3, p->n[0], p->n[1], p->n[2], p->d, v3, p->n[0], p->n[1], p->n[2], v3, p->n[0], p->n[1], p->n[2]);
    else
      put(b, "  den = dot(d, %s(%.7f, %.7f, %.7f)); t = den > 1e-7 ? (%.7f - dot(p, %s(%.7f, %.7f, %.7f))) / den : 1e30; if (t < te) { te = t; n = %s(%.7f, %.7f, %.7f); }\n",
          v3, p->n[0], p->n[1], p->n[2], p->d, v3, p->n[0], p->n[1], p->n[2], v3, p->n[0], p->n[1], p->n[2]);
  }
  if (w) put(b, "  var r: HitOut; r.t = te; r.n = n; return r;\n}\n");
  else put(b, "  return te;\n}\n");
}

static const char *GEM_VS_GL =
    "uniform vec4 u_rect;\n"
    "vec4 canvasClipV(vec2 p) { return vec4(p.x / 960.0 - 1.0, 1.0 - p.y / 540.0, 0.0, 1.0); }\n"
    "vec2 cornerV(int c) { return c == 1 ? vec2(1.0, 0.0) : (c == 2 || c == 4) ? vec2(1.0) : c == 5 ? vec2(0.0, 1.0) : vec2(0.0); }\n"
    "void main() { gl_Position = canvasClipV(mix(u_rect.xy, u_rect.zw, cornerV(gl_VertexID % 6))); }\n";

static const char *GEM_FS_GL =
    "uniform mat4 u_inv;\n"
    "uniform mat4 u_rot;\n"
    "uniform vec4 u_rect;\n"
    "uniform vec4 u_abs;\n"  /* absorption rgb, opacity */
    "uniform vec4 u_ior;\n"  /* Cauchy A, B; glow */
    "uniform vec4 u_q;\n"    /* subsamples, bounces, fire on, show leaks */
    "uniform vec4 u_body;\n"
    "uniform vec4 u_jit;\n"
    "uniform vec4 u_gf0; uniform vec4 u_gf1; uniform vec4 u_gc0; uniform vec4 u_gc1;\n"
    "out vec4 o;\n"
    "vec3 flashL(vec3 dw) {\n"
    "  return u_gc0.xyz * (u_gf0.w * 9.0 * pow(max(dot(dw, u_gf0.xyz), 0.0), 24.0)) + u_gc1.xyz * (u_gf1.w * 9.0 * pow(max(dot(dw, u_gf1.xyz), 0.0), 24.0));\n"
    "}\n"
    "vec3 toW(vec3 dl) { return drift(normalize(mat3(u_rot) * dl)); }\n"
    "vec3 envMono(vec3 dl) { vec3 dw = toW(dl); vec4 s = studioT(dw); return (s.rgb + pinMono(dw, s.a)) * u_tint.xyz + flashL(dw); }\n"
    "vec3 envDisp(vec3 e0, vec3 e1, vec3 em) { vec3 dm = toW(em); vec4 s = studioT(dm); return (s.rgb + pinDisp(toW(e0), toW(e1), s.a)) * u_tint.xyz + flashL(dm); }\n"
    "float fres(float ci, float n1, float n2) {\n"
    "  float st = n1 / n2 * sqrt(max(0.0, 1.0 - ci * ci));\n"
    "  if (st >= 1.0) return 1.0;\n"
    "  float ct = sqrt(1.0 - st * st);\n"
    "  float rs = (n1 * ci - n2 * ct) / (n1 * ci + n2 * ct), rp = (n2 * ci - n1 * ct) / (n2 * ci + n1 * ct);\n"
    "  return 0.5 * (rs * rs + rp * rp);\n"
    "}\n"
    /* one path through the stone: Fresnel at every face, total internal reflection past the critical angle,
     * Beer-Lambert absorption along the way, and at each exit the spectrum fanned out between its 400 and 700 nm
     * directions (pinsDisp) */
    "vec3 shade(vec3 p, vec3 d, vec3 nn, int NB, bool fire) {\n"
    "  float n = u_ior.x + u_ior.y / (560.0 * 560.0);\n"
    "  float n0 = u_ior.x + u_ior.y / (400.0 * 400.0), n1 = u_ior.x + u_ior.y / (700.0 * 700.0);\n"
    "  float ci = -dot(nn, d);\n"
    "  float F = fres(ci, 1.0, n);\n"
    "  vec3 col = F * envMono(reflect(d, nn));\n"
    "  vec3 t = refract(d, nn, 1.0 / n);\n"
    "  float thr = 1.0 - F, path = 0.0;\n"
    "  for (int b = 0; b < 12; b++) {\n"
    "    if (b >= NB || thr < 0.03) break;\n"
    "    vec3 ne;\n"
    "    float te = max(hullOut(p, t, ne), 0.0);\n"
    "    p += t * te; path += te;\n"
    "    float ce = dot(t, ne);\n"
    "    if (n * n * (1.0 - ce * ce) < 1.0) {\n"
    "      float Fe = fres(ce, n, 1.0);\n"
    "      vec3 T = exp(-u_abs.xyz * path) * (thr * (1.0 - Fe));\n"
    "      vec3 em = refract(t, -ne, n);\n"
    "      if (fire) {\n"
    "        vec3 e0 = refract(t, -ne, n0), e1 = refract(t, -ne, n1);\n"
    "        if (dot(e0, e0) < 0.5) e0 = em;\n"
    "        if (dot(e1, e1) < 0.5) e1 = em;\n"
    "        col += T * envDisp(e0, e1, em);\n"
    "      } else col += T * envMono(em);\n"
    "      thr *= Fe;\n"
    "    }\n"
    "    t = reflect(t, ne);\n"
    "  }\n"
    "  if (u_q.w > 0.5) col += vec3(thr * 3.0, 0.0, thr * 3.0);\n" /* the light the bounce budget left inside */
    "  else col += thr * exp(-u_abs.xyz * path) * envMono(t) * 0.5;\n"
    "  return col;\n"
    "}\n"
    "void main() {\n"
    "  vec2 fc = gl_FragCoord.xy + u_jit.xy;\n"
    "  int S = int(u_q.x), NB = int(u_q.y);\n"
    "  bool fire = u_q.z > 0.5;\n"
    "  vec3 acc = vec3(0.0); float cover = 0.0;\n"
    "  vec3 ro = u_cam.xyz;\n"
    "  vec3 ol = (u_inv * vec4(ro, 1.0)).xyz;\n"
    "  for (int k = 0; k < 4; k++) {\n"
    "    if (k >= S) break;\n"
    "    vec2 off = S == 1 ? vec2(0.0) : S == 2 ? (k == 0 ? vec2(-0.25, 0.25) : vec2(0.25, -0.25))\n"
    "             : k == 0 ? vec2(-0.375, 0.125) : k == 1 ? vec2(0.125, 0.375) : k == 2 ? vec2(0.375, -0.125) : vec2(-0.125, -0.375);\n"
    "    vec3 rd = normalize(trayPoint(canvasOf(fc + off)) - ro);\n"
    "    vec3 dl = normalize(mat3(u_inv) * rd);\n"
    "    float tn; vec3 nn;\n"
    "    if (!hullIn(ol, dl, tn, nn)) continue;\n"
    "    cover += 1.0;\n"
    "    acc += shade(ol + dl * tn, dl, nn, NB, fire);\n"
    "  }\n"
    "  if (cover <= 0.0) discard;\n"
    "  vec3 col = acc / cover;\n"
    "  col += u_body.xyz * u_body.w;\n"
    "  float g = u_ior.z;\n"
    "  col = col * (1.0 + 1.6 * g) + u_body.xyz * g * 0.3;\n"
    "  float a = cover / float(S) * u_abs.w;\n"
    "  o = vec4(col * a, a);\n"
    "}\n";

char *jw_shader_gem(bool w, const jw_cut *c, char **vs) {
  sb b = { 0 };
  if (w) {
    put(&b, "%s%s", WG_GEM_U, WG_COMMON_FNS);
    spectrum_fn(&b, true);
    gem_env_fns(&b, true);
    hull_fns(&b, c, true);
    put(&b, "%s", WG_GEM);
    return done(&b);
  }
  *vs = strdup(GEM_VS_GL);
  put(&b, "%s", GL_COMMON);
  spectrum_fn(&b, false);
  gem_env_fns(&b, false);
  hull_fns(&b, c, false);
  put(&b, "%s", GEM_FS_GL);
  return done(&b);
}

/* ── under each stone: shadow, caustic, the held one's glow ──────────── */

static const char *UNDER_VS_GL =
    "uniform vec4 u_rect;\n"
    "void main() {\n"
    "  int c = gl_VertexID % 6;\n"
    "  vec2 q = c == 1 ? vec2(1.0, 0.0) : (c == 2 || c == 4) ? vec2(1.0) : c == 5 ? vec2(0.0, 1.0) : vec2(0.0);\n"
    "  vec2 p = mix(u_rect.xy, u_rect.zw, q);\n"
    "  gl_Position = vec4(p.x / 960.0 - 1.0, 1.0 - p.y / 540.0, 0.0, 1.0);\n"
    "}\n";
static const char *UNDER_FS_GL =
    "uniform vec4 u_rect;\n"
    "uniform vec4 u_gem;\n"   /* kind, opacity, held, time */
    "uniform vec4 u_shape;\n" /* half-extents x, y (px), centre height, px per local unit */
    "uniform vec4 u_body;\n"
    "uniform vec4 u_misc;\n"  /* caustics baked */
    "uniform sampler2D u_t0;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec2 c = canvasOf(gl_FragCoord.xy);\n"
    "  vec2 ctr = 0.5 * (u_rect.xy + u_rect.zw);\n"
    "  vec2 rel = (c - ctr) * vec2(1.0, -1.0) / u_shape.w;\n"
    "  vec2 ext = u_shape.xy / u_shape.w;\n"
    "  float A; vec3 L = vec3(0.0);\n"
    "  if (u_misc.x > 0.5) {\n"
    "    vec2 uv = vec2((u_gem.x + 0.5 + rel.x / (2.0 * SPAN)) / 6.0, 0.5 + rel.y / (2.0 * SPAN));\n"
    "    vec4 t = texture(u_t0, uv);\n"
    "    L = t.xyz; A = clamp(t.w, 0.0, 1.0);\n"
    "  } else {\n"
    "    vec2 off = -KEY.xy / KEY.z * (u_shape.z / u_shape.w);\n"
    "    A = 1.0 - smoothstep(0.55, 1.2, length((rel - off) / ext));\n"
    "  }\n"
    "  float r = length(rel / ext);\n"
    "  float ao = (1.0 - smoothstep(0.1, 1.1, r)) * 0.6;\n"
    "  float sh = max(A * 0.8, ao);\n"
    /* the light the stone lets through, pooled in its own shadow (stray rays beyond it would read as scratches) */
    "  vec3 add = L * smoothstep(0.15, 0.9, max(L.r, max(L.g, L.b))) * smoothstep(0.08, 0.45, A) * vec3(3.0, 2.85, 2.6) * u_tint.xyz * 0.09;\n"
    "  if (u_gem.z > 0.5) {\n" /* held: a warm light under it */
    "    float ring = exp(-pow((r - 1.15) / 0.28, 2.0));\n"
    "    add += vec3(1.0, 0.72, 0.32) * (0.35 + 0.15 * sin(u_gem.w * 6.0)) * ring;\n"
    "    sh *= 0.5;\n"
    "  }\n"
    "  float op = u_gem.y;\n"
    "  o = vec4(add * op, sh * op);\n"
    "}\n";

char *jw_shader_under(bool w, char **vs) {
  sb b = { 0 };
  if (w) {
    put(&b, "%s%s", WG_UNDER_U, WG_COMMON_FNS);
    key_consts(&b, true);
    put(&b, "const SPAN = %.4f;\n", JW_CAUSTIC_SPAN);
    put(&b, "%s", WG_UNDER);
    return done(&b);
  }
  *vs = strdup(UNDER_VS_GL);
  put(&b, "%s", GL_COMMON);
  key_consts(&b, false);
  put(&b, "const float SPAN = %.4f;\n", JW_CAUSTIC_SPAN);
  put(&b, "%s", UNDER_FS_GL);
  return done(&b);
}

/* ── sparks, rings and trails (added light) ───────────────────────────── */

static const char *SPARK_VS_GL =
    "uniform vec4 u_burst;\n" /* x, y, t, power (a ring: its power; a trail: x0, y0, t) */
    "uniform vec4 u_col;\n"   /* rgb, seed */
    "uniform vec4 u_kind;\n"  /* 0 sparks, 1 a ring, 2 a trail; count; the trail's end */
    "out vec2 v_q;\n"
    "out vec4 v_c;\n"
    "float hs(float n) { return fract(sin(n * 12.9898 + u_col.w * 0.137) * 43758.5453); }\n"
    "void main() {\n"
    "  int i = gl_VertexID / 6, c = gl_VertexID % 6;\n"
    "  vec2 q = c == 1 ? vec2(1.0, 0.0) : (c == 2 || c == 4) ? vec2(1.0) : c == 5 ? vec2(0.0, 1.0) : vec2(0.0);\n"
    "  q = q * 2.0 - 1.0;\n"
    "  vec2 p; float sz; vec4 col;\n"
    "  if (u_kind.x < 0.5) {\n"
    "    float fi = float(i), r1 = hs(fi), r2 = hs(fi + 17.3), r3 = hs(fi + 41.9), r4 = hs(fi + 77.1);\n"
    "    float life = 0.45 + 0.75 * r3 + 0.15 * u_burst.w;\n"
    "    float t = u_burst.z, k = t / life;\n"
    "    float a = 6.2831853 * r1, sp = (160.0 + 420.0 * r2) * (0.7 + 0.3 * u_burst.w);\n"
    "    float drag = (1.0 - exp(-3.2 * t)) / 3.2;\n"
    "    p = u_burst.xy + vec2(cos(a), sin(a)) * (sp * drag + 30.0 * r4) + vec2(0.0, 90.0 * t * t);\n"
    "    sz = (5.0 + 11.0 * r4) * (0.7 + 0.3 * u_burst.w) * (1.0 - k * k);\n"
    "    float tw = 0.65 + 0.35 * sin(t * (18.0 + 20.0 * r4) + r1 * 6.0);\n" /* twinkle */
    "    vec3 hue = mix(u_col.xyz * 1.6 + 0.08, vec3(1.0), 0.25 + 0.5 * r3);\n"
    "    col = vec4(hue * (0.8 + 0.6 * u_burst.w) * tw * (1.0 - k), k < 1.0 ? 1.0 : 0.0);\n"
    "    if (k >= 1.0) sz = 0.0;\n"
    "  } else if (u_kind.x < 1.5) {\n"
    "    float t = u_burst.z;\n"
    "    float R = 30.0 + (260.0 + 120.0 * u_burst.w) * (1.0 - exp(-4.0 * t));\n"
    "    p = u_burst.xy; sz = R + 30.0;\n"
    "    col = vec4(u_col.xyz * (1.0 - smoothstep(0.0, 0.8, t)) * (0.8 + 0.4 * u_burst.w), R);\n"
    "  } else {\n"
    "    vec2 a = u_burst.xy, b = u_kind.zw;\n"
    "    vec2 dv = b - a; float L = length(dv); vec2 dn = dv / max(L, 1.0), nn = vec2(-dn.y, dn.x);\n"
    "    p = mix(a, b, q.x * 0.5 + 0.5) + nn * q.y * 26.0 - dn * q.x * 0.0;\n"
    "    v_q = vec2(q.x * 0.5 + 0.5, q.y);\n"
    "    float t = u_burst.z;\n"
    "    v_c = vec4(u_col.xyz * (1.0 - smoothstep(0.0, 0.7, t)) * 1.6, t);\n"
    "    gl_Position = vec4(p.x / 960.0 - 1.0, 1.0 - p.y / 540.0, 0.0, 1.0);\n"
    "    return;\n"
    "  }\n"
    "  v_q = q; v_c = col;\n"
    "  vec2 pp = p + q * sz;\n"
    "  gl_Position = vec4(pp.x / 960.0 - 1.0, 1.0 - pp.y / 540.0, 0.0, 1.0);\n"
    "}\n";
static const char *SPARK_FS_GL =
    "uniform vec4 u_kind;\n"
    "in vec2 v_q;\n"
    "in vec4 v_c;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec3 c;\n"
    "  if (u_kind.x < 0.5) {\n"
    /* a four-point star with a soft core */
    "    vec2 q = v_q;\n"
    "    float r = length(q);\n"
    "    float core = exp(-r * r * 18.0);\n"
    "    float rays = exp(-abs(q.x) * 30.0) * exp(-q.y * q.y * 3.0) + exp(-abs(q.y) * 30.0) * exp(-q.x * q.x * 3.0);\n"
    "    c = v_c.xyz * (core * 1.4 + rays * 0.8) * v_c.w;\n"
    "  } else if (u_kind.x < 1.5) {\n"
    "    float R = v_c.w;\n"
    "    float d = length(v_q) * (R + 30.0);\n"
    "    float ring = exp(-pow((d - R) / 7.0, 2.0)) + 0.35 * exp(-pow((d - R + 14.0) / 16.0, 2.0));\n"
    "    c = v_c.xyz * ring;\n"
    "  } else {\n"
    "    float k = v_q.x, t = v_c.w;\n"
    "    float head = smoothstep(0.0, 0.35, t);\n"
    "    float along = smoothstep(head - 0.6, head, k) * (1.0 - smoothstep(head, head + 0.05, k));\n"
    "    c = v_c.xyz * exp(-v_q.y * v_q.y * 9.0) * along;\n"
    "  }\n"
    "  o = vec4(c, 0.0);\n"
    "}\n";

char *jw_shader_spark(bool w, char **vs) {
  sb b = { 0 };
  if (w) {
    put(&b, "%s%s%s", WG_SPARK_U, WG_COMMON_FNS, WG_SPARK);
    return done(&b);
  }
  *vs = strdup(SPARK_VS_GL);
  put(&b, "%s", GL_COMMON);
  put(&b, "%s", SPARK_FS_GL);
  return done(&b);
}

/* ── the cursor, the held jewel, the hint ─────────────────────────────── */

static const char *MARK_VS_GL =
    "uniform vec4 u_mark;\n"  /* kind (0 cursor, 1 held, 2 hint), x, y, time */
    "uniform vec4 u_mark2;\n" /* the hint's second cell x, y; seconds shown */
    "out vec2 v_c;\n"
    "void main() {\n"
    "  int c = gl_VertexID % 6;\n"
    "  vec2 q = c == 1 ? vec2(1.0, 0.0) : (c == 2 || c == 4) ? vec2(1.0) : c == 5 ? vec2(0.0, 1.0) : vec2(0.0);\n"
    "  vec2 lo = u_mark.yz - 78.0, hi = u_mark.yz + 78.0;\n"
    "  if (u_mark.x > 1.5) { lo = min(lo, u_mark2.xy - 78.0); hi = max(hi, u_mark2.xy + 78.0); }\n"
    "  vec2 p = mix(lo, hi, q);\n"
    "  v_c = p;\n"
    "  gl_Position = vec4(p.x / 960.0 - 1.0, 1.0 - p.y / 540.0, 0.0, 1.0);\n"
    "}\n";
static const char *MARK_FS_GL =
    "uniform vec4 u_mark;\n"
    "uniform vec4 u_mark2;\n"
    "in vec2 v_c;\n"
    "out vec4 o;\n"
    "float sdBox(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\n"
    "float sdSeg(vec2 p, vec2 a, vec2 b) { vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0); return length(pa - ba * h); }\n"
    /* a frame round a cell: a line, with brackets at the corners; returns (coverage of the line, glow) */
    "vec2 frameAt(vec2 p, float inset, float w, float bracket) {\n"
    "  float d = sdBox(p, vec2(60.0 - inset), 14.0);\n"
    "  float line = 1.0 - smoothstep(w * 0.5 - 0.75, w * 0.5 + 0.75, abs(d));\n"
    "  vec2 a = abs(p);\n"
    "  float corner = step(60.0 - inset - bracket, min(a.x, a.y));\n"
    "  float br = (1.0 - smoothstep(w - 0.75, w + 0.75, abs(d + w * 0.25))) * corner;\n"
    "  return vec2(max(line, br), exp(-abs(d) / 7.0));\n"
    "}\n"
    "void main() {\n"
    "  vec2 c = canvasOf(gl_FragCoord.xy);\n"
    "  float t = u_mark.w;\n"
    "  vec3 col = vec3(0.0); float a = 0.0;\n"
    "  if (u_mark.x < 0.5) {\n"
    /* the cursor: gold, gently breathing, on a dark edge so it reads over any stone */
    "    float pulse = 0.82 + 0.18 * sin(t * 3.4);\n"
    "    vec2 f = frameAt(c - u_mark.yz, 2.0, 5.0, 30.0);\n"
    "    vec2 dk = frameAt(c - u_mark.yz, 2.0, 11.0, 34.0);\n"
    "    vec3 goldc = vec3(2.6, 1.75, 0.62) * pulse;\n"
    "    a = max(f.x, dk.x * 0.7);\n"
    "    col = goldc * f.x + goldc * 0.10 * f.y;\n"
    "  } else if (u_mark.x < 1.5) {\n"
    /* the held jewel: a bright white-gold frame, quicker breath */
    "    float pulse = 0.75 + 0.25 * sin(t * 6.0);\n"
    "    vec2 f = frameAt(c - u_mark.yz, 6.0, 4.0, 60.0);\n"
    "    vec2 dk = frameAt(c - u_mark.yz, 6.0, 9.0, 60.0);\n"
    "    vec3 wc = vec3(3.2, 2.9, 2.2) * pulse;\n"
    "    a = max(f.x, dk.x * 0.6);\n"
    "    col = wc * f.x + wc * 0.12 * f.y;\n"
    "  } else {\n"
    /* the hint: both cells framed in a soft aqua light and an arrow between them, fading in and out */
    "    float life = clamp(u_mark2.z * 3.0, 0.0, 1.0) * (1.0 - smoothstep(3.4, 4.0, u_mark2.z));\n"
    "    float pulse = (0.7 + 0.3 * sin(t * 5.0)) * life;\n"
    "    vec2 f0 = frameAt(c - u_mark.yz, 6.0, 4.0, 60.0), f1 = frameAt(c - u_mark2.xy, 6.0, 4.0, 60.0);\n"
    "    vec2 ab = u_mark2.xy - u_mark.yz; vec2 dn = normalize(ab), nn = vec2(-dn.y, dn.x);\n"
    "    vec2 m = (u_mark.yz + u_mark2.xy) * 0.5;\n"
    "    float shaft = sdSeg(c, m - dn * 26.0, m + dn * 26.0);\n"
    "    float head1 = sdSeg(c, m + dn * 26.0, m + dn * 12.0 + nn * 12.0), head2 = sdSeg(c, m + dn * 26.0, m + dn * 12.0 - nn * 12.0);\n"
    "    float tail1 = sdSeg(c, m - dn * 26.0, m - dn * 12.0 + nn * 12.0), tail2 = sdSeg(c, m - dn * 26.0, m - dn * 12.0 - nn * 12.0);\n"
    "    float arrow = 1.0 - smoothstep(2.6, 4.2, min(min(shaft, min(head1, head2)), min(tail1, tail2)));\n"
    "    float dark = 1.0 - smoothstep(5.0, 8.0, min(min(shaft, min(head1, head2)), min(tail1, tail2)));\n"
    "    vec3 hc = vec3(0.9, 2.4, 2.7) * pulse;\n"
    "    float fl = max(f0.x, f1.x);\n"
    "    a = max(fl, max(arrow, dark * 0.8)) * life;\n"
    "    col = hc * max(fl, arrow) + hc * 0.10 * max(f0.y, f1.y);\n"
    "  }\n"
    "  o = vec4(col, a);\n"
    "}\n";

char *jw_shader_mark(bool w, char **vs) {
  sb b = { 0 };
  if (w) {
    put(&b, "%s%s%s", WG_MARK_U, WG_COMMON_FNS, WG_MARK);
    return done(&b);
  }
  *vs = strdup(MARK_VS_GL);
  put(&b, "%s", GL_COMMON);
  put(&b, "%s", MARK_FS_GL);
  return done(&b);
}

/* ── the star glints ─────────────────────────────────────────────────── *
 * A star only where a facet flashes a hot lamp: the traced picture at half the canvas (stage 0), the brightest point
 * of each 6 x 6 block (1), then of each tile of 60 canvas pixels, kept when it passes the threshold (2), a tile that
 * has a brighter star close by gives way (3), and each star is drawn analytically at full resolution over the board
 * where its highlight is (4): four thin spokes and four short ones, in the highlight's own colour, its length growing
 * with how bright the flash is. Peaks live as the traced highlights do: as a stone rocks, a facet sweeps across a
 * lamp and its star flares and fades with it. */
static const char *GLINT_GL[4] = {
  "uniform sampler2D u_t0;\n"
  "uniform vec4 u_px;\n" /* the source's texel */
  "out vec4 o;\n"
  "void main() {\n"
  "  vec3 c = texture(u_t0, v_uv + u_px.xy * vec2(-0.5, -0.5)).rgb + texture(u_t0, v_uv + u_px.xy * vec2(0.5, -0.5)).rgb\n"
  "         + texture(u_t0, v_uv + u_px.xy * vec2(-0.5, 0.5)).rgb + texture(u_t0, v_uv + u_px.xy * vec2(0.5, 0.5)).rgb;\n"
  "  o = vec4(min(c * 0.25, vec3(400.0)), 1.0);\n"
  "}\n",
  "uniform sampler2D u_t0;\n"
  "out vec4 o;\n"
  "void main() {\n"
  "  ivec2 b = ivec2(gl_FragCoord.xy) * 6;\n"
  "  float best = -1.0, bi = 0.0; vec3 bc = vec3(0.0);\n"
  "  for (int y = 0; y < 6; y++)\n"
  "    for (int x = 0; x < 6; x++) {\n"
  "      vec3 c = texelFetch(u_t0, b + ivec2(x, y), 0).rgb;\n"
  "      float l = max(c.r, max(c.g, c.b));\n"
  "      if (l > best) { best = l; bc = c; bi = float(y * 6 + x); }\n"
  "    }\n"
  "  o = vec4(bc, bi);\n"
  "}\n",
  "uniform sampler2D u_t0;\n"
  "uniform vec4 u_px;\n" /* threshold */
  "out vec4 o;\n"
  "void main() {\n"
  "  ivec2 b = ivec2(gl_FragCoord.xy) * 5;\n"
  "  float best = -1.0, bi = -1.0; vec3 bc = vec3(0.0);\n"
  "  for (int y = 0; y < 5; y++)\n"
  "    for (int x = 0; x < 5; x++) {\n"
  "      vec4 t = texelFetch(u_t0, b + ivec2(x, y), 0);\n"
  "      float l = max(t.r, max(t.g, t.b));\n"
  "      if (l > best) { best = l; bc = t.rgb; bi = (float(y * 6) + floor(t.a / 6.0 + 0.01)) * 30.0 + float(x * 6) + mod(t.a + 0.01, 6.0) - 0.01; }\n"
  "    }\n"
  "  float k = step(u_px.x, best);\n"
  "  o = k > 0.0 ? vec4(bc * k, floor(bi + 0.5)) : vec4(0.0, 0.0, 0.0, -1.0);\n"
  "}\n",
  "uniform sampler2D u_t0;\n"
  "uniform vec4 u_px;\n" /* -, the closest two stars may be (half-canvas texels), the tiles across and down */
  "out vec4 o;\n"
  "vec2 at(ivec2 t, float i) { return vec2(t * 30) + vec2(mod(i + 0.01, 30.0) - 0.01, floor(i / 30.0 + 0.01)); }\n"
  "void main() {\n"
  "  ivec2 t = ivec2(gl_FragCoord.xy);\n"
  "  vec4 me = texelFetch(u_t0, t, 0);\n"
  "  o = me;\n"
  "  if (me.a < 0.0) return;\n"
  "  vec2 mp = at(t, me.a);\n"
  "  float ml = max(me.r, max(me.g, me.b));\n"
  "  for (int dy = -1; dy <= 1; dy++)\n"
  "    for (int dx = -1; dx <= 1; dx++) {\n"
  "      ivec2 n = t + ivec2(dx, dy);\n"
  "      if ((dx == 0 && dy == 0) || n.x < 0 || n.y < 0 || n.x >= int(u_px.z) || n.y >= int(u_px.w)) continue;\n"
  "      vec4 s = texelFetch(u_t0, n, 0);\n"
  "      if (s.a < 0.0) continue;\n"
  "      float nl = max(s.r, max(s.g, s.b));\n"
  "      if (distance(mp, at(n, s.a)) < u_px.y && (nl > ml || (nl == ml && dx + dy * 3 > 0))) o = vec4(0.0, 0.0, 0.0, -1.0);\n"
  "    }\n"
  "}\n",
};

/* the stars over the board, added to the scene before the temporal resolve: a quad over the board and its frame */
static const char *GLINT_OVER_VS =
    "uniform vec4 u_rect;\n"
    "void main() {\n"
    "  int c = gl_VertexID % 6;\n"
    "  vec2 q = c == 1 ? vec2(1.0, 0.0) : (c == 2 || c == 4) ? vec2(1.0) : c == 5 ? vec2(0.0, 1.0) : vec2(0.0);\n"
    "  vec2 p = mix(u_rect.xy, u_rect.zw, q);\n"
    "  gl_Position = vec4(p.x / 960.0 - 1.0, 1.0 - p.y / 540.0, 0.0, 1.0);\n"
    "}\n";
static const char *GLINT_OVER_FS =
    "uniform sampler2D u_t0;\n"
    "uniform vec4 u_view;\n"
    "uniform vec4 u_px;\n" /* strength, length, angle, threshold */
    "out vec4 o;\n"
    "vec3 star(vec2 d, vec3 col, float pw) {\n"
    "  float l = max(col.r, max(col.g, col.b));\n"
    "  float fade = smoothstep(u_px.w, u_px.w * 1.5, l), s = 1.0 + clamp(log2(l / u_px.w), 0.0, 2.5);\n"
    "  float L = (6.0 + 6.0 * s) * u_px.y;\n"
    "  float r2 = dot(d, d);\n"
    "  if (r2 > L * L) return vec3(0.0);\n"
    "  float w = max(0.55, pw * 0.6);\n"
    "  vec2 a0 = vec2(cos(u_px.z), sin(u_px.z)), a1 = vec2(-a0.y, a0.x);\n"
    "  vec3 hue = col / max(l, 1e-4);\n"
    "  vec3 acc = vec3(0.0);\n"
    "  for (int k = 0; k < 4; k++) {\n"
    "    vec2 dir = k == 0 ? a0 : k == 1 ? a1 : k == 2 ? (a0 + a1) * 0.70710678 : (a0 - a1) * 0.70710678;\n"
    "    float len = k < 2 ? L : L * 0.42, amp = k < 2 ? 1.0 : 0.45;\n"
    "    float al = abs(dot(d, dir)) / len, ac = dot(d, vec2(-dir.y, dir.x));\n"
    "    float f = max(1.0 - al, 0.0);\n"
    "    vec3 tip = mix(vec3(1.0), 0.55 + 0.45 * cos(6.2831 * (al * 0.8 + vec3(0.0, 0.33, 0.67))), smoothstep(0.35, 1.0, al) * 0.5);\n"
    "    acc += amp * exp(-ac * ac / (2.0 * w * w)) * f * f * f * tip;\n"
    "  }\n"
    "  float core = exp(-r2 / (2.0 * 1.4 * 1.4)) * 0.7 + exp(-r2 / (2.0 * 4.5 * 4.5)) * 0.12;\n"
    "  return (acc + core) * mix(vec3(1.0), hue, 0.55) * fade * (0.7 + 0.3 * s);\n"
    "}\n"
    "void main() {\n"
    "  vec2 g = gl_FragCoord.xy * vec2(960.0 * u_view.z, 540.0 * u_view.w);\n"
    "  float pw = 1920.0 * u_view.z;\n"
    "  ivec2 t0 = ivec2(floor(g / 30.0));\n"
    "  vec3 acc = vec3(0.0);\n"
    "  for (int dy = -1; dy <= 1; dy++)\n"
    "    for (int dx = -1; dx <= 1; dx++) {\n"
    "      ivec2 t = t0 + ivec2(dx, dy);\n"
    "      if (t.x < 0 || t.y < 0 || t.x >= 32 || t.y >= 18) continue;\n"
    "      vec4 s = texelFetch(u_t0, t, 0);\n"
    "      if (s.a < 0.0) continue;\n"
    "      vec2 p = vec2(t * 30) + vec2(mod(s.a + 0.01, 30.0) - 0.01, floor(s.a / 30.0 + 0.01)) + 0.5;\n"
    "      acc += star((g - p) * 2.0, s.rgb, pw);\n"
    "    }\n"
    "  o = vec4(acc * u_px.x, 0.0);\n"
    "}\n";

char *jw_shader_glint(bool w, int stage, char **vs) {
  if (stage == 4) {
    if (!w) *vs = strdup(GLINT_OVER_VS);
    return strdup(w ? WG_GLINT_OVER : GLINT_OVER_FS);
  }
  return strdup(w ? WG_GLINT[stage] : GLINT_GL[stage]);
}

/* ── the tray, from its cache, with the light of the moment over it ─────── */

static const char *PRESENT_GL =
    "uniform sampler2D u_t0;\n"
    "uniform vec4 u_misc;\n" /* time, button glow, button pressed, reshuffle */
    "uniform vec4 u_fl0; uniform vec4 u_fl1; uniform vec4 u_fl2; uniform vec4 u_fl3;\n"
    "uniform vec4 u_fc0; uniform vec4 u_fc1; uniform vec4 u_fc2; uniform vec4 u_fc3;\n"
    "out vec4 o;\n"
    "float sdBox(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\n"
    "vec3 pool(vec3 P, vec4 fl, vec4 fc) { vec3 L = fl.xyz - P; float d2 = dot(L, L); return fc.xyz * (fl.w / (1.0 + d2 / (220.0 * 220.0))) * (L.z * inversesqrt(d2)); }\n"
    "void main() {\n"
    "  vec2 c = canvasOf(gl_FragCoord.xy);\n"
    "  vec3 P = trayPoint(c);\n"
    "  vec3 base = texture(u_t0, v_uv).rgb;\n"
    "  vec3 fl = pool(P, u_fl0, u_fc0) + pool(P, u_fl1, u_fc1) + pool(P, u_fl2, u_fc2) + pool(P, u_fl3, u_fc3);\n"
    "  vec3 col = base * (u_tint.xyz + fl * 0.45) + fl * 0.0035;\n"
    "  float db = sdBox(c - vec2(1600.0, 932.0), vec2(220.0, 64.0), 26.0);\n"
    "  if (db < 0.0 && db > -6.0) col *= 1.0 + 1.5 * u_misc.y;\n"
    "  else if (db <= -6.0) col = col * (1.0 - 0.4 * u_misc.z) + vec3(1.0, 0.72, 0.35) * 0.05 * u_misc.y * smoothstep(-6.0, -60.0, db);\n"
    "  vec2 qb = abs(c - vec2(672.0, 540.0)) - vec2(600.0, 480.0);\n"
    "  if (qb.x < 0.0 && qb.y < 0.0) col += vec3(0.03, 0.06, 0.09) * u_misc.w * (0.5 + 0.5 * sin(c.x * 0.02 - u_misc.x * 6.0));\n"
    "  o = vec4(col, 1.0);\n"
    "}\n";

static const char *PRESENT_WG =
    "struct U {\n  u_view: vec4f,\n  u_cam: vec4f,\n  u_tint: vec4f,\n  u_misc: vec4f,\n  u_fl0: vec4f, u_fl1: vec4f, u_fl2: vec4f, u_fl3: vec4f,\n"
    "  u_fc0: vec4f, u_fc1: vec4f, u_fc2: vec4f, u_fc3: vec4f,\n};\n@group(0) @binding(0) var<uniform> u: U;\n"
    "@group(0) @binding(1) var u_t0: texture_2d<f32>;\n"
    "fn sdBox(p: vec2f, b: vec2f, r: f32) -> f32 { let q = abs(p) - b + r; return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r; }\n"
    "fn pool(P: vec3f, fl: vec4f, fc: vec4f) -> vec3f { let L = fl.xyz - P; let d2 = dot(L, L); return fc.xyz * (fl.w / (1.0 + d2 / (220.0 * 220.0))) * (L.z * inverseSqrt(d2)); }\n"
    "@fragment fn fs(@builtin(position) pos: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {\n"
    "  let c = canvasOf(cw_frag_coord(pos));\n"
    "  let P = trayPoint(c);\n"
    "  let base = textureSampleLevel(u_t0, cw_linear, uv, 0.0).rgb;\n"
    "  let fl = pool(P, u.u_fl0, u.u_fc0) + pool(P, u.u_fl1, u.u_fc1) + pool(P, u.u_fl2, u.u_fc2) + pool(P, u.u_fl3, u.u_fc3);\n"
    "  var col = base * (u.u_tint.xyz + fl * 0.45) + fl * 0.0035;\n"
    "  let db = sdBox(c - vec2f(1600.0, 932.0), vec2f(220.0, 64.0), 26.0);\n"
    "  if (db < 0.0 && db > -6.0) { col *= 1.0 + 1.5 * u.u_misc.y; }\n"
    "  else if (db <= -6.0) { col = col * (1.0 - 0.4 * u.u_misc.z) + vec3f(1.0, 0.72, 0.35) * 0.05 * u.u_misc.y * smoothstep(-6.0, -60.0, db); }\n"
    "  let qb = abs(c - vec2f(672.0, 540.0)) - vec2f(600.0, 480.0);\n"
    "  if (qb.x < 0.0 && qb.y < 0.0) { col += vec3f(0.03, 0.06, 0.09) * u.u_misc.w * (0.5 + 0.5 * sin(c.x * 0.02 - u.u_misc.x * 6.0)); }\n"
    "  return vec4f(col, 1.0);\n"
    "}\n";

char *jw_shader_present(bool w) {
  sb b = { 0 };
  if (w) put(&b, "%s%s%s", PRESENT_WG, WG_COMMON_FNS, "");
  else put(&b, "%s%s", GL_COMMON, PRESENT_GL);
  return done(&b);
}
