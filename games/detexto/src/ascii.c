/* The ASCII renderer (ascii.h). */
#include "ascii.h"
#include "opts.h"
#include <cartwheel/debug.h>
#include <cartwheel/gpu.h>
#include <cartwheel/pipeline.h>
#include <cartwheel/quality.h>
#include <cartwheel/vfs.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define FIRST_CP 32
#define NGLYPH 95 /* printable ASCII, space first */
#define FONT_PATH "fonts/FiraMono-Regular.ttf"
/* flat cells (no edge inside) take a character by brightness alone, from this ramp */
static const char RAMP[] = " .:-=+*#%@";
#define RAMP_N ((int)sizeof RAMP - 1)
#define MAXMASK 16
/* the characters shape matching may pick: lines, corners, brackets and marks that draw the edge of a thing (letters
 * with tails and signs like $ read as noise). Flat cells use the ramp. */
static const char SHAPES[] = ".,:;'`\"-_=+*^~|/\\()[]{}<>!?lLJT7YVvo";

static struct {
  cw_tree *tree;
  bool on, ready;
  unsigned char *ttf;
  stbtt_fontinfo font;
  float aspect;          /* advance / line height */
  unsigned gv_tex;       /* NGLYPH x 3 RGBA8: each glyph's six region coverages, then the ramp */
  unsigned atlas;        /* R8, NGLYPH cells across, one cell high */
  int atlas_cw, atlas_ch;
  unsigned prog_cells, prog_text, prog_glow;
  /* this frame's grid (the POST pass makes it, the OVERLAY pass draws it) */
  const cw_rt *cells;
  uint64_t cells_frame;
  bool have_cells, scaled;
  int cols, rows, cw, ch;
  int frame;
  /* debug fields */
  int dbg_cols, dbg_rows, dbg_cw, dbg_ch, dbg_on, dbg_boxes;
  /* the HUD boxes the pass keeps black (fractions of the frame, top left origin: x0, y0, x1, y1) */
  float mask[MAXMASK][4];
  int nmask;
} A;

/* ── the font ────────────────────────────────────────────────────────── */

/* glyph i rasterized into a cw x ch cell (row 0 the TOP), anti-aliased coverage 0..255 */
static void raster(int i, int cw, int ch, unsigned char *out) {
  memset(out, 0, (size_t)cw * ch);
  int asc, desc, gap;
  stbtt_GetFontVMetrics(&A.font, &asc, &desc, &gap);
  float s = (float)ch / (float)(asc - desc);
  int cp = FIRST_CP + i;
  if (cp == ' ') return;
  int adv, lsb;
  stbtt_GetCodepointHMetrics(&A.font, cp, &adv, &lsb);
  int x0, y0, x1, y1;
  stbtt_GetCodepointBitmapBox(&A.font, cp, s, s, &x0, &y0, &x1, &y1);
  int gw = x1 - x0, gh = y1 - y0;
  if (gw <= 0 || gh <= 0) return;
  unsigned char *g = calloc((size_t)gw * gh, 1);
  stbtt_MakeCodepointBitmap(&A.font, g, gw, gh, gw, s, s, cp);
  float base = (float)asc * s;
  /* x0 already holds the left side bearing: centre the advance box in the cell */
  int oy = (int)lrintf(base) + y0, ox = (int)lrintf(((float)cw - (float)adv * s) * 0.5f) + x0;
  for (int y = 0; y < gh; y++)
    for (int x = 0; x < gw; x++) {
      int cx = ox + x, cy = oy + y;
      if (cx < 0 || cy < 0 || cx >= cw || cy >= ch) continue;
      int v = out[cy * cw + cx] + g[y * gw + x];
      out[cy * cw + cx] = (unsigned char)(v > 255 ? 255 : v);
    }
  free(g);
}

/* the glyph shape vectors: six regions (3 rows x 2 columns, top row first, left column first) measured on a large
 * raster, normalized so the inkiest region of any glyph is 1 */
static void make_vectors(void) {
  const int ch = 96, cw = (int)lrintf(96 * A.aspect);
  unsigned char *b = malloc((size_t)cw * ch);
  float v[NGLYPH][6], mx = 0;
  for (int i = 0; i < NGLYPH; i++) {
    raster(i, cw, ch, b);
    for (int r = 0; r < 6; r++) {
      int col = r & 1, row = r >> 1;
      int xa = col * cw / 2, xb = (col + 1) * cw / 2, ya = row * ch / 3, yb = (row + 1) * ch / 3;
      double s = 0;
      for (int y = ya; y < yb; y++)
        for (int x = xa; x < xb; x++) s += b[y * cw + x];
      v[i][r] = (float)(s / (255.0 * (xb - xa) * (yb - ya)));
      if (v[i][r] > mx) mx = v[i][r];
    }
  }
  free(b);
  unsigned char tex[3][NGLYPH][4] = { 0 };
  for (int i = 0; i < NGLYPH; i++)
    for (int r = 0; r < 6; r++) {
      float n = mx > 0 ? v[i][r] / mx : 0;
      tex[r / 4][i][r % 4] = (unsigned char)lrintf(n * 255.0f);
    }
  /* row 2: the density ramp for flat cells, darkest first (glyph indices) */
  for (int i = 0; RAMP[i]; i++) tex[2][i][0] = (unsigned char)(RAMP[i] - FIRST_CP);
  for (int i = 0; i < NGLYPH; i++) tex[2][i][1] = strchr(SHAPES, FIRST_CP + i) && i > 0 ? 255 : 0;
  A.gv_tex = cw_pass_texture_new(NGLYPH, 3, CW_RT_RGBA8, tex);
}

/* the atlas at the cell size in use: NGLYPH cells across; row 0 is the glyph's BOTTOM row (GL's v = 0), so a
 * pixel's height in its cell counted from the bottom (gl_FragCoord) is its atlas row */
static void make_atlas(int cw, int ch) {
  if (A.atlas && A.atlas_cw == cw && A.atlas_ch == ch) return;
  cw_pass_texture_free(A.atlas);
  unsigned char *cell = malloc((size_t)cw * ch), *tex = calloc((size_t)cw * NGLYPH * ch, 1);
  for (int i = 0; i < NGLYPH; i++) {
    raster(i, cw, ch, cell);
    for (int y = 0; y < ch; y++) {
      unsigned char *row = tex + (size_t)(ch - 1 - y) * cw * NGLYPH + (size_t)i * cw;
      for (int x = 0; x < cw; x++) { /* a little bolder: thin strokes glow too faintly */
        int v = cell[y * cw + x], l = x > 0 ? cell[y * cw + x - 1] * 4 / 5 : 0;
        row[x] = (unsigned char)(v > l ? v : l);
      }
    }
  }
  A.atlas = cw_pass_texture_new(cw * NGLYPH, ch, CW_RT_R8, tex);
  A.atlas_cw = cw, A.atlas_ch = ch;
  free(cell), free(tex);
}

/* ── the shaders ─────────────────────────────────────────────────────── */

/* Uniforms of the cells program:
 *   u_cell: cell w, cell h (pixels), cols, rows
 *   u_lin:  near, far, ortho, have the previous frame's cells
 *   u_par:  blank floor, contrast exponent, depth edge gain, keep (how much better a new glyph must fit)
 *   u_col:  exposure, saturation, glyphs, brightness floor
 *   u_ext:  HUD boxes, flatness (a cell whose darkest part is within this share of its brightest is flat), ramp
 *           length, ramp gain
 *   u_m0..u_m15: the HUD boxes (fractions of the frame, top left origin: x0, y0, x1, y1)
 * The body is the same text in GLSL and WGSL apart from the spelling the macros below cover. */

static const char *CELLS_GLSL_HEAD =
    "uniform sampler2D u_src; uniform sampler2D u_depth; uniform sampler2D u_gv; uniform sampler2D u_prev;\n"
    "uniform vec4 u_cell; uniform vec4 u_lin; uniform vec4 u_par; uniform vec4 u_col; uniform vec4 u_ext; uniform vec4 u_ex2;\n"
    "uniform vec4 u_m0; uniform vec4 u_m1; uniform vec4 u_m2; uniform vec4 u_m3; uniform vec4 u_m4; uniform vec4 u_m5;\n"
    "uniform vec4 u_m6; uniform vec4 u_m7; uniform vec4 u_m8; uniform vec4 u_m9; uniform vec4 u_m10; uniform vec4 u_m11;\n"
    "uniform vec4 u_m12; uniform vec4 u_m13; uniform vec4 u_m14; uniform vec4 u_m15;\n"
    "out vec4 o;\n"
    "float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
    "float ldepth(vec2 px) {\n"
    "  ivec2 q = clamp(ivec2(px), ivec2(0), textureSize(u_depth, 0) - 1);\n"
    "  float d = texelFetch(u_depth, q, 0).r;\n"
    "  if (d >= 0.999999) return 1e6;\n"
    "  if (u_lin.z > 0.5) return u_lin.x + d * (u_lin.y - u_lin.x);\n"
    "  return u_lin.x * u_lin.y / (u_lin.y - d * (u_lin.y - u_lin.x));\n"
    "}\n"
    "vec3 tap(vec2 px, vec2 isz) { return texture(u_src, px * isz).rgb; }\n"
    "float gdist(int i, float v[6]) {\n"
    "  vec4 a = texelFetch(u_gv, ivec2(i, 0), 0); vec2 b = texelFetch(u_gv, ivec2(i, 1), 0).rg;\n"
    "  vec4 da = vec4(v[0], v[1], v[2], v[3]) - a; vec2 db = vec2(v[4], v[5]) - b;\n"
    "  return dot(da, da) + dot(db, db);\n"
    "}\n"
    "bool boxed(vec4 m, vec2 lo, vec2 hi, vec2 fs) {\n"
    "  return lo.x < m.z * fs.x && hi.x > m.x * fs.x && lo.y < (1.0 - m.y) * fs.y && hi.y > (1.0 - m.w) * fs.y;\n"
    "}\n"
    "void main() {\n"
    "  vec2 cell = floor(gl_FragCoord.xy);\n"
    "  vec2 cs = u_cell.xy; vec2 base = cell * cs;\n"
    "  vec2 fsz = vec2(textureSize(u_src, 0)); vec2 isz = 1.0 / fsz;\n"
    "  int nm = int(u_ext.x); bool hidden = false;\n"
    "  vec4 ms[16] = vec4[16](u_m0, u_m1, u_m2, u_m3, u_m4, u_m5, u_m6, u_m7, u_m8, u_m9, u_m10, u_m11, u_m12, u_m13, u_m14, u_m15);\n"
    "  for (int k = 0; k < 16; k++) { if (k < nm && boxed(ms[k], base, base + cs, fsz)) hidden = true; }\n"
    "  if (hidden) { o = vec4(0.0); return; }\n"
    "  float v[6]; float z[6]; vec3 csum = vec3(0.0); float far = 0.0; float zmax = 0.0;\n"
    "  for (int r = 0; r < 6; r++) {\n"
    "    float col = float(r - (r / 2) * 2), row = float(r / 2);\n"
    "    vec2 c = base + vec2((col + 0.5) * cs.x * 0.5, cs.y - (row + 0.5) * cs.y / 3.0);\n"
    "    vec2 e = vec2(cs.x * 0.125, cs.y / 12.0);\n"
    "    vec3 t0 = tap(c + vec2(-e.x, -e.y), isz), t1 = tap(c + vec2(e.x, -e.y), isz), t2 = tap(c + vec2(-e.x, e.y), isz), t3 = tap(c + e, isz);\n"
    "    vec3 s = (t0 + t1 + t2 + t3) * 0.25;\n"
    "    vec3 mx = t0; if (lum(t1) > lum(mx)) mx = t1; if (lum(t2) > lum(mx)) mx = t2; if (lum(t3) > lum(mx)) mx = t3;\n"
    "    s = mix(s, mx, u_ex2.z) * u_col.x; /* thin bright things (a lattice girder) keep their light */\n"
    "    float zz = ldepth(c); z[r] = zz;\n"
    "    if (zz > 1e5) { s = vec3(0.0); far += 1.0; } else { zmax = max(zmax, zz); }\n"
    "    csum += s;\n"
    "    float l = lum(s); v[r] = l / (1.0 + l);\n"
    "  }\n"
    "  float edged = 0.0;\n"
    "  if (far < 6.0) {\n"
    "    float far_side = (far > 0.0) ? 1e6 : zmax;\n"
    "    for (int r = 0; r < 6; r++) {\n"
    "      if (z[r] > 1e5) continue;\n"
    "      float edge = clamp((log(far_side) - log(z[r])) * u_par.z, 0.0, 1.0);\n"
    "      edged = max(edged, edge);\n"
    "      v[r] = max(v[r], edge * 0.8);\n"
    "    }\n"
    "  }\n"
    "  float m = 0.0; float mn = 1.0; float mean = 0.0;\n"
    "  for (int r = 0; r < 6; r++) { m = max(m, v[r]); mn = min(mn, v[r]); mean += v[r] / 6.0; }\n"
    "  float g = 0.0; int n = int(u_col.z);\n"
    "  bool isflat = (m - mn) <= u_ext.y * m && edged < 0.2;\n"
    "  if (m > u_par.x && isflat) {\n"
    "    int ri = clamp(int(mean * u_ext.w * u_ext.z), 0, int(u_ext.z) - 1);\n"
    "    g = floor(texelFetch(u_gv, ivec2(ri, 2), 0).r * 255.0 + 0.5);\n"
    "  } else if (m > u_par.x) {\n"
    "    for (int r = 0; r < 6; r++) v[r] = pow(v[r] / m, u_par.y) * m;\n"
    "    float best = 1e9;\n"
    "    for (int i = 1; i < 95; i++) {\n"
    "      if (i >= n) break;\n"
    "      if (texelFetch(u_gv, ivec2(i, 2), 0).g < 0.5) continue;\n"
    "      float d = gdist(i, v);\n"
    "      if (d < best) { best = d; g = float(i); }\n"
    "    }\n"
    "    if (u_lin.w > 0.5) {\n"
    "      int p = int(texelFetch(u_prev, ivec2(cell), 0).a + 0.5);\n"
    "      if (p > 0 && p < n && float(p) != g && gdist(p, v) <= best * u_par.w + 0.004) g = float(p);\n"
    "    }\n"
    "  }\n"
    "  vec3 c = csum / max(6.0 - far, 1.0);\n"
    "  float mc = max(max(c.r, c.g), max(c.b, 1e-4));\n"
    "  float mnc = min(min(c.r, c.g), c.b); float key = smoothstep(0.22, 0.5, (mc - mnc) / mc);\n"
    "  vec3 hue = clamp(mix(vec3(lum(c / mc)), c / mc, u_col.y), 0.0, 1.0);\n"
    "  hue /= max(max(hue.r, hue.g), max(hue.b, 1e-4));\n"
    "  hue = mix(vec3(0.2, 1.0, 0.45), hue, key);\n"
    "  float bright = (u_col.w + (1.0 - u_col.w) * clamp(m * 1.4, 0.0, 1.0)) * (isflat ? u_ex2.x : 1.0) * mix(u_ex2.y, 1.0, key);\n"
    "  o = vec4(g > 0.5 ? hue * bright : vec3(0.0), g);\n"
    "}\n";

static const char *CELLS_WGSL =
    "struct U { u_cell: vec4f, u_lin: vec4f, u_par: vec4f, u_col: vec4f, u_ext: vec4f, u_ex2: vec4f,\n"
    "  u_m0: vec4f, u_m1: vec4f, u_m2: vec4f, u_m3: vec4f, u_m4: vec4f, u_m5: vec4f, u_m6: vec4f, u_m7: vec4f,\n"
    "  u_m8: vec4f, u_m9: vec4f, u_m10: vec4f, u_m11: vec4f, u_m12: vec4f, u_m13: vec4f, u_m14: vec4f, u_m15: vec4f };\n"
    "@group(0) @binding(0) var<uniform> u: U;\n"
    "@group(0) @binding(1) var u_src: texture_2d<f32>;\n"
    "@group(0) @binding(2) var u_depth: texture_2d<f32>;\n"
    "@group(0) @binding(3) var u_gv: texture_2d<f32>;\n"
    "@group(0) @binding(4) var u_prev: texture_2d<f32>;\n"
    "fn lum(c: vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }\n"
    "fn ldepth(px: vec2f) -> f32 {\n"
    "  let q = clamp(vec2i(px), vec2i(0), vec2i(textureDimensions(u_depth)) - 1);\n"
    "  let zl = textureLoad(u_depth, q, 0).r;\n"
    "  if (zl > 1e30) { return 1e6; }\n"
    "  return zl;\n"
    "}\n"
    "fn tap(px: vec2f, isz: vec2f) -> vec3f { return textureSampleLevel(u_src, cw_linear, px * isz, 0.0).rgb; }\n"
    "fn gdist(i: i32, v: array<f32, 6>) -> f32 {\n"
    "  let a = textureLoad(u_gv, vec2i(i, 0), 0); let b = textureLoad(u_gv, vec2i(i, 1), 0).rg;\n"
    "  let da = vec4f(v[0], v[1], v[2], v[3]) - a; let db = vec2f(v[4], v[5]) - b;\n"
    "  return dot(da, da) + dot(db, db);\n"
    "}\n"
    "fn boxed(m: vec4f, lo: vec2f, hi: vec2f, fs: vec2f) -> bool {\n"
    "  return lo.x < m.z * fs.x && hi.x > m.x * fs.x && lo.y < (1.0 - m.y) * fs.y && hi.y > (1.0 - m.w) * fs.y;\n"
    "}\n"
    "@fragment fn fs(@builtin(position) pos: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {\n"
    "  let cell = floor(cw_frag_coord(pos));\n"
    "  let cs = u.u_cell.xy; let base = cell * cs;\n"
    "  let fsz = vec2f(textureDimensions(u_src)); let isz = 1.0 / fsz;\n"
    "  let nm = i32(u.u_ext.x); var hidden = false;\n"
    "  let ms = array<vec4f, 16>(u.u_m0, u.u_m1, u.u_m2, u.u_m3, u.u_m4, u.u_m5, u.u_m6, u.u_m7, u.u_m8, u.u_m9, u.u_m10, u.u_m11, u.u_m12, u.u_m13, u.u_m14, u.u_m15);\n"
    "  for (var k = 0; k < 16; k++) { if (k < nm && boxed(ms[k], base, base + cs, fsz)) { hidden = true; } }\n"
    "  if (hidden) { return vec4f(0.0); }\n"
    "  var v: array<f32, 6>; var z: array<f32, 6>; var csum = vec3f(0.0); var far = 0.0; var zmax = 0.0;\n"
    "  for (var r = 0; r < 6; r++) {\n"
    "    let col = f32(r - (r / 2) * 2); let row = f32(r / 2);\n"
    "    let c = base + vec2f((col + 0.5) * cs.x * 0.5, cs.y - (row + 0.5) * cs.y / 3.0);\n"
    "    let e = vec2f(cs.x * 0.125, cs.y / 12.0);\n"
    "    let t0 = tap(c + vec2f(-e.x, -e.y), isz); let t1 = tap(c + vec2f(e.x, -e.y), isz); let t2 = tap(c + vec2f(-e.x, e.y), isz); let t3 = tap(c + e, isz);\n"
    "    var mx = t0; if (lum(t1) > lum(mx)) { mx = t1; } if (lum(t2) > lum(mx)) { mx = t2; } if (lum(t3) > lum(mx)) { mx = t3; }\n"
    "    var s = mix((t0 + t1 + t2 + t3) * 0.25, mx, u.u_ex2.z) * u.u_col.x;\n"
    "    let zz = ldepth(c); z[r] = zz;\n"
    "    if (zz > 1e5) { s = vec3f(0.0); far += 1.0; } else { zmax = max(zmax, zz); }\n"
    "    csum += s;\n"
    "    let l = lum(s); v[r] = l / (1.0 + l);\n"
    "  }\n"
    "  var edged = 0.0;\n"
    "  if (far < 6.0) {\n"
    "    let far_side = select(zmax, 1e6, far > 0.0);\n"
    "    for (var r = 0; r < 6; r++) {\n"
    "      if (z[r] > 1e5) { continue; }\n"
    "      let edge = clamp((log(far_side) - log(z[r])) * u.u_par.z, 0.0, 1.0);\n"
    "      edged = max(edged, edge);\n"
    "      v[r] = max(v[r], edge * 0.8);\n"
    "    }\n"
    "  }\n"
    "  var m = 0.0; var mn = 1.0; var mean = 0.0;\n"
    "  for (var r = 0; r < 6; r++) { m = max(m, v[r]); mn = min(mn, v[r]); mean += v[r] / 6.0; }\n"
    "  var g = 0; let n = i32(u.u_col.z);\n"
    "  let isflat = (m - mn) <= u.u_ext.y * m && edged < 0.2;\n"
    "  if (m > u.u_par.x && isflat) {\n"
    "    let ri = clamp(i32(mean * u.u_ext.w * u.u_ext.z), 0, i32(u.u_ext.z) - 1);\n"
    "    g = i32(floor(textureLoad(u_gv, vec2i(ri, 2), 0).r * 255.0 + 0.5));\n"
    "  } else if (m > u.u_par.x) {\n"
    "    for (var r = 0; r < 6; r++) { v[r] = pow(v[r] / m, u.u_par.y) * m; }\n"
    "    var best = 1e9;\n"
    "    for (var i = 1; i < 95; i++) {\n"
    "      if (i >= n) { break; }\n"
    "      if (textureLoad(u_gv, vec2i(i, 2), 0).g < 0.5) { continue; }\n"
    "      let d = gdist(i, v);\n"
    "      if (d < best) { best = d; g = i; }\n"
    "    }\n"
    "    if (u.u_lin.w > 0.5) {\n"
    "      let p = i32(textureLoad(u_prev, vec2i(cell), 0).a + 0.5);\n"
    "      if (p > 0 && p < n && p != g) {\n"
    "        if (gdist(p, v) <= best * u.u_par.w + 0.004) { g = p; }\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  let c = csum / max(6.0 - far, 1.0);\n"
    "  let mc = max(max(c.r, c.g), max(c.b, 1e-4));\n"
    "  let mnc = min(min(c.r, c.g), c.b); let key = smoothstep(0.22, 0.5, (mc - mnc) / mc);\n"
    "  var hue = clamp(mix(vec3f(lum(c / mc)), c / mc, u.u_col.y), vec3f(0.0), vec3f(1.0));\n"
    "  hue = hue / max(max(hue.r, hue.g), max(hue.b, 1e-4));\n"
    "  hue = mix(vec3f(0.2, 1.0, 0.45), hue, key);\n"
    "  let bright = (u.u_col.w + (1.0 - u.u_col.w) * clamp(m * 1.4, 0.0, 1.0)) * select(1.0, u.u_ex2.x, isflat) * mix(u.u_ex2.y, 1.0, key);\n"
    "  return vec4f(select(vec3f(0.0), hue * bright, g > 0), f32(g));\n"
    "}\n";

/* The glow (POST, at the scene's size): the bright cells' colours blurred over about two cells round them, a soft halo
 * of the character's own colour under it (the dim world gets none). u_cell: the cell's size in scene pixels, cols,
 * rows; u_glow: gain, the brightness a cell starts to glow at. */
static const char *GLOW_GLSL =
    "uniform sampler2D u_cells; uniform vec4 u_cell; uniform vec4 u_glow;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec2 q = gl_FragCoord.xy / u_cell.xy;\n"
    "  vec3 s = vec3(0.0); float wsum = 0.0;\n"
    "  for (int j = -2; j <= 2; j++) for (int i = -2; i <= 2; i++) {\n"
    "    vec2 d = vec2(float(i), float(j)) * vec2(0.7, 0.45);\n"
    "    float w = exp(-dot(d, d) * 0.9);\n"
    "    vec3 c = texture(u_cells, (q + d) / u_cell.zw).rgb;\n"
    "    float k = smoothstep(u_glow.y, 1.0, max(c.r, max(c.g, c.b)));\n"
    "    s += c * k * w; wsum += w;\n"
    "  }\n"
    "  o = vec4(s / wsum * u_glow.x, 1.0);\n"
    "}\n";

static const char *GLOW_WGSL =
    "struct U { u_cell: vec4f, u_glow: vec4f };\n"
    "@group(0) @binding(0) var<uniform> u: U;\n"
    "@group(0) @binding(1) var u_cells: texture_2d<f32>;\n"
    "@fragment fn fs(@builtin(position) pos: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {\n"
    "  let q = cw_frag_coord(pos) / u.u_cell.xy;\n"
    "  var s = vec3f(0.0); var wsum = 0.0;\n"
    "  for (var j = -2; j <= 2; j++) { for (var i = -2; i <= 2; i++) {\n"
    "    let d = vec2f(f32(i), f32(j)) * vec2f(0.7, 0.45);\n"
    "    let w = exp(-dot(d, d) * 0.9);\n"
    "    let c = textureSampleLevel(u_cells, cw_linear, (q + d) / u.u_cell.zw, 0.0).rgb;\n"
    "    let k = smoothstep(u.u_glow.y, 1.0, max(c.r, max(c.g, c.b)));\n"
    "    s += c * k * w; wsum += w;\n"
    "  } }\n"
    "  return vec4f(s / wsum * u.u_glow.x, 1.0);\n"
    "}\n";

/* The characters (OVERLAY, at the output's full size, added over the glowing picture): each pixel draws its cell's
 * glyph from the atlas in the cell's colour, with a soft shoulder toward white where it is brightest.
 * u_cell: the cell's size in output pixels, cols, rows; u_out: gain. */
static const char *TEXT_GLSL =
    "uniform sampler2D u_cells; uniform sampler2D u_atlas; uniform vec4 u_cell; uniform vec4 u_out;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec2 p = gl_FragCoord.xy;\n"
    "  vec2 c = floor(p / u_cell.xy);\n"
    "  if (c.x >= u_cell.z || c.y >= u_cell.w) { o = vec4(0.0); return; }\n"
    "  vec4 cell = texelFetch(u_cells, ivec2(c), 0);\n"
    "  int g = int(cell.a + 0.5);\n"
    "  ivec2 l = ivec2(p - c * u_cell.xy);\n"
    "  float cov = texelFetch(u_atlas, ivec2(g * int(u_cell.x) + l.x, l.y), 0).r;\n"
    "  o = vec4(1.0 - exp(-cell.rgb * cov * u_out.x), 1.0);\n"
    "}\n";

static const char *TEXT_WGSL =
    "struct U { u_cell: vec4f, u_out: vec4f };\n"
    "@group(0) @binding(0) var<uniform> u: U;\n"
    "@group(0) @binding(1) var u_cells: texture_2d<f32>;\n"
    "@group(0) @binding(2) var u_atlas: texture_2d<f32>;\n"
    "@fragment fn fs(@builtin(position) pos: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {\n"
    "  let p = cw_frag_coord(pos);\n"
    "  let c = floor(p / u.u_cell.xy);\n"
    "  if (c.x >= u.u_cell.z || c.y >= u.u_cell.w) { return vec4f(0.0); }\n"
    "  let cell = textureLoad(u_cells, vec2i(c), 0);\n"
    "  let g = i32(cell.a + 0.5);\n"
    "  let l = vec2i(p - c * u.u_cell.xy);\n"
    "  let cov = textureLoad(u_atlas, vec2i(g * i32(u.u_cell.x) + l.x, l.y), 0).r;\n"
    "  return vec4f(1.0 - exp(-cell.rgb * cov * u.u_out.x), 1.0);\n"
    "}\n";

/* ── the passes ──────────────────────────────────────────────────────── */

static float optf(const char *name, float def) {
  const char *s = opt_get(name);
  return s ? (float)atof(s) : def;
}

static bool ascii_active(const cw_pass_ctx *ctx, void *u) {
  (void)ctx, (void)u;
  return A.on && A.ready;
}

static void programs(void) {
  if (A.prog_cells) return;
  bool wg = cw_gpu_backend() == CW_GPU_WEBGPU;
  A.prog_cells = wg ? cw_pass_program_wgsl("dt_ascii_cells", CELLS_WGSL) : cw_pass_program("dt_ascii_cells", CELLS_GLSL_HEAD);
  A.prog_glow = wg ? cw_pass_program_wgsl("dt_ascii_glow", GLOW_WGSL) : cw_pass_program("dt_ascii_glow", GLOW_GLSL);
  A.prog_text = wg ? cw_pass_program_wgsl("dt_ascii_text", TEXT_WGSL) : cw_pass_program("dt_ascii_text", TEXT_GLSL);
}

/* POST: the grid from the scene (at the scene's render size, which may be a share of the output), then the glow */
static void cells_exec(cw_pass_ctx *ctx, void *u) {
  (void)u;
  bool wg = cw_gpu_backend() == CW_GPU_WEBGPU;
  programs();
  if (!A.prog_cells || !A.prog_glow) return;
  /* the grid is laid on the OUTPUT (the characters draw there, crisp): ROWS rows, the font's own aspect */
  int ow = ctx->output && ctx->output->w > 0 ? ctx->output->w : (int)lrintf((float)ctx->width / cw_gfx_render_scale());
  int oh = ctx->output && ctx->output->h > 0 ? ctx->output->h : (int)lrintf((float)ctx->height / cw_gfx_render_scale());
  float rows_want = optf("ASCII_ROWS", 54);
  int ch = (int)lrintf((float)oh / (rows_want < 8 ? 8 : rows_want));
  if (ch < 6) ch = 6;
  int cw = (int)lrintf((float)ch * A.aspect);
  if (cw < 3) cw = 3;
  int cols = (ow + cw - 1) / cw, rows = (oh + ch - 1) / ch;
  A.cols = cols, A.rows = rows, A.cw = cw, A.ch = ch;
  A.dbg_cols = cols, A.dbg_rows = rows, A.dbg_cw = cw, A.dbg_ch = ch;
  /* one cell in scene pixels */
  float sx = (float)cw * (float)ctx->width / (float)ow, sy = (float)ch * (float)ctx->height / (float)oh;

  bool fresh0, fresh1;
  const cw_rt *t0 = cw_pipeline_rt_persistent(ctx->pipeline, "detexto.cells0", cols, rows, CW_RT_RGBA16F, &fresh0);
  const cw_rt *t1 = cw_pipeline_rt_persistent(ctx->pipeline, "detexto.cells1", cols, rows, CW_RT_RGBA16F, &fresh1);
  const cw_rt *cur = (A.frame & 1) ? t1 : t0, *prev = (A.frame & 1) ? t0 : t1;
  bool have_prev = !((A.frame & 1) ? fresh0 : fresh1) && A.frame > 0;
  A.frame++;

  float lin[3];
  cw_pass_linear_depth_params(ctx, lin);
  cw_pass_bind_target(ctx, cur);
  cw_pass_use(A.prog_cells);
  cw_pass_bind_texture(0, ctx->src->tex);
  cw_pass_bind_texture(1, ctx->depth_tex);
  cw_pass_bind_texture(2, A.gv_tex);
  cw_pass_bind_texture(3, prev->tex);
  if (!wg) {
    cw_pass_uniform1i(A.prog_cells, "u_src", 0), cw_pass_uniform1i(A.prog_cells, "u_depth", 1);
    cw_pass_uniform1i(A.prog_cells, "u_gv", 2), cw_pass_uniform1i(A.prog_cells, "u_prev", 3);
  }
  cw_pass_uniform4f(A.prog_cells, "u_cell", sx, sy, (float)cols, (float)rows);
  cw_pass_uniform4f(A.prog_cells, "u_lin", lin[0], lin[1], lin[2], have_prev ? 1.0f : 0.0f);
  cw_pass_uniform4f(A.prog_cells, "u_par", optf("ASCII_FLOOR", 0.05f), optf("ASCII_CONTRAST", 2.0f), optf("ASCII_EDGE", 1.5f),
                    optf("ASCII_KEEP", 1.35f));
  cw_pass_uniform4f(A.prog_cells, "u_col", optf("ASCII_EXPOSURE", 1.0f), optf("ASCII_SAT", 1.8f), (float)NGLYPH,
                    optf("ASCII_BRIGHT_FLOOR", 0.35f));
  int nm = opt_on("ASCII_NO_HUD_BOXES") ? 0 : A.nmask;
  cw_pass_uniform4f(A.prog_cells, "u_ext", (float)nm, optf("ASCII_FLAT", 0.45f), (float)RAMP_N, optf("ASCII_RAMP_GAIN", 1.0f));
  cw_pass_uniform4f(A.prog_cells, "u_ex2", optf("ASCII_FLAT_BRIGHT", 0.75f), optf("ASCII_WORLD_BRIGHT", 0.3f), optf("ASCII_PEAK", 0.5f), 0);
  for (int k = 0; k < MAXMASK; k++) {
    char nmk[8];
    snprintf(nmk, sizeof nmk, "u_m%d", k);
    const float *m = A.mask[k];
    cw_pass_uniform4f(A.prog_cells, nmk, m[0], m[1], m[2], m[3]);
  }
  cw_pass_fullscreen();

  cw_pass_bind_target(ctx, ctx->dst);
  cw_pass_use(A.prog_glow);
  cw_pass_bind_texture(0, cur->tex);
  if (!wg) cw_pass_uniform1i(A.prog_glow, "u_cells", 0);
  cw_pass_uniform4f(A.prog_glow, "u_cell", sx, sy, (float)cols, (float)rows);
  cw_pass_uniform4f(A.prog_glow, "u_glow", optf("ASCII_GLOW", 0.5f), optf("ASCII_GLOW_FROM", 0.5f), 0, 0);
  cw_pass_fullscreen();
  A.cells = cur, A.cells_frame = ctx->frame, A.have_cells = true;
  /* the scene at a share of the output (the Resolution setting): on this engine the OVERLAY passes draw into that
   * share too, so the characters draw here, at the scene's size, and are scaled up with it (still legible) */
  A.scaled = cw_gfx_render_scale() < 0.999f;
  if (A.scaled && A.prog_text) {
    int acw = (int)ceilf(sx), ach = (int)ceilf(sy);
    make_atlas(acw, ach);
    cw_pass_use(A.prog_text);
    cw_pass_set_blend(CW_PASS_BLEND_ADD);
    cw_pass_bind_texture(0, cur->tex);
    cw_pass_bind_texture(1, A.atlas);
    if (!wg) cw_pass_uniform1i(A.prog_text, "u_cells", 0), cw_pass_uniform1i(A.prog_text, "u_atlas", 1);
    cw_pass_uniform4f(A.prog_text, "u_cell", (float)acw, (float)ach, (float)cols, (float)rows);
    cw_pass_uniform4f(A.prog_text, "u_out", optf("ASCII_GAIN", 1.6f), 0, 0, 0);
    cw_pass_fullscreen();
    cw_pass_set_blend(CW_PASS_BLEND_NONE);
    A.have_cells = false;
  }
}

/* OVERLAY, first (before the 2D canvas and the HUD): the characters at the output's full size, added */
static void text_exec(cw_pass_ctx *ctx, void *u) {
  (void)u;
  bool wg = cw_gpu_backend() == CW_GPU_WEBGPU;
  if (!A.have_cells || !A.prog_text) return; /* no 3D since the last characters drawn: nothing to draw */
  A.have_cells = false;
  make_atlas(A.cw, A.ch);
  cw_pass_bind_target(ctx, NULL);
  cw_pass_use(A.prog_text);
  cw_pass_set_blend(CW_PASS_BLEND_ADD);
  cw_pass_bind_texture(0, A.cells->tex);
  cw_pass_bind_texture(1, A.atlas);
  if (!wg) cw_pass_uniform1i(A.prog_text, "u_cells", 0), cw_pass_uniform1i(A.prog_text, "u_atlas", 1);
  cw_pass_uniform4f(A.prog_text, "u_cell", (float)A.cw, (float)A.ch, (float)A.cols, (float)A.rows);
  cw_pass_uniform4f(A.prog_text, "u_out", optf("ASCII_GAIN", 1.6f), 0, 0, 0);
  cw_pass_fullscreen();
  cw_pass_set_blend(CW_PASS_BLEND_NONE);
}

void ascii_init(cw_tree *tree) {
  if (A.tree) return;
  A.tree = tree;
  A.on = !opt_on("ASCII_OFF");
  size_t len = 0;
  A.ttf = cw_vfs_read(FONT_PATH, &len);
  if (!A.ttf || !stbtt_InitFont(&A.font, A.ttf, stbtt_GetFontOffsetForIndex(A.ttf, 0))) {
    cw_log(CW_LOG_ERROR, "detexto: no font at %s: the ASCII pass is off", FONT_PATH);
    return;
  }
  int asc, desc, gap, adv, lsb;
  stbtt_GetFontVMetrics(&A.font, &asc, &desc, &gap);
  stbtt_GetCodepointHMetrics(&A.font, 'M', &adv, &lsb);
  A.aspect = (float)adv / (float)(asc - desc);
  make_vectors();
  cw_pipeline *p = cw_pipeline_of(tree);
  cw_pipeline_add_pass(p, &(cw_pass_desc){ .name = "ascii", .phase = CW_PHASE_POST, .order = 280, .reads = CW_RES_COLOR | CW_RES_DEPTH,
                                           .writes = CW_RES_COLOR, .active = ascii_active, .execute = cells_exec, .webgpu = true });
  cw_pipeline_add_pass(p, &(cw_pass_desc){ .name = "ascii_text", .phase = CW_PHASE_OVERLAY, .order = -200, .writes = CW_RES_OUTPUT,
                                           .active = ascii_active, .execute = text_exec, .webgpu = true });
  A.ready = true;
  cw_debug_i32("dt.ascii.cols", &A.dbg_cols);
  cw_debug_i32("dt.ascii.rows", &A.dbg_rows);
  cw_debug_i32("dt.ascii.cell_w", &A.dbg_cw);
  cw_debug_i32("dt.ascii.cell_h", &A.dbg_ch);
  cw_debug_i32("dt.ascii.on", &A.dbg_on);
  cw_debug_i32("dt.ascii.hud_boxes", &A.dbg_boxes);
  A.dbg_on = A.on;
}

void ascii_set_enabled(bool on) {
  A.on = on;
  A.dbg_on = on;
}

bool ascii_enabled(void) { return A.on; }

/* ── the HUD boxes ───────────────────────────────────────────────────── */

#define MAXREC 96
static struct {
  cw_vec2 cs;
  float u;
  int n;
  float r[MAXREC][9]; /* x0, y0, x1, y1, alpha, edge r, g, b, edge given */
  int nb;
  float b[MAXREC][9];  /* last frame's merged boxes: drawn first, under this frame's HUD */
} H;

/* the neon border: a thin bright line with two soft passes of the same colour round it */
static void border(cw_rect r, float u, cw_color c) {
  for (int k = 2; k >= 0; k--) {
    float g = (float)k * 3.0f * u;
    cw_color cc = c;
    cc.a *= k == 0 ? 1.0f : k == 1 ? 0.28f : 0.12f;
    cw_r2d_round_rect_lines(cw_rect_make(r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g), 8 * u + g, (k == 0 ? 2.0f : 3.0f) * u, 6, cc);
  }
}

/* the boxes go down before the HUD draws (the last frame's: the HUD barely moves), black inside, a neon edge */
void ascii_hud_begin(cw_vec2 canvas, float u) {
  H.cs = canvas, H.u = u, H.n = 0;
  if (!A.on || opt_on("ASCII_NO_HUD_BOXES")) return;
  for (int i = 0; i < H.nb; i++) {
    const float *r = H.b[i];
    cw_rect rr = cw_rect_make(r[0], r[1], r[2] - r[0], r[3] - r[1]);
    cw_r2d_round_rect(rr, 8 * u, 6, (cw_color){ 0, 0, 0, 0.85f * r[4] });
    border(rr, u, (cw_color){ r[5], r[6], r[7], 0.9f * r[4] });
  }
}

void ascii_hud_rect(float x, float y, float w, float h, float alpha) {
  ascii_hud_rect_edge(x, y, w, h, alpha, (cw_color){ 0, 0, 0, 0 });
}

void ascii_hud_rect_edge(float x, float y, float w, float h, float alpha, cw_color edge) {
  if (alpha < 0.05f || w <= 0 || h <= 0) return;
  if (x + w < 0 || y + h < 0 || x > H.cs.x || y > H.cs.y) return; /* off the screen (the glyph bake) */
  if (H.n >= MAXREC) return;
  float *r = H.r[H.n++];
  r[0] = x, r[1] = y, r[2] = x + w, r[3] = y + h, r[4] = alpha;
  bool given = edge.a > 0;
  r[5] = given ? edge.r : 0.25f, r[6] = given ? edge.g : 0.95f, r[7] = given ? edge.b : 1.0f, r[8] = given;
}

void ascii_hud_end(void) {
  float pad = 14 * H.u, gap = 18 * H.u;
  /* pad every element, then merge boxes that touch (within gap) until none do */
  for (int i = 0; i < H.n; i++) H.r[i][0] -= pad, H.r[i][1] -= pad * 0.6f, H.r[i][2] += pad, H.r[i][3] += pad * 0.6f;
  bool merged = true;
  while (merged) {
    merged = false;
    for (int i = 0; i < H.n && !merged; i++)
      for (int j = i + 1; j < H.n; j++) {
        float *a = H.r[i], *b = H.r[j];
        if (a[0] - gap < b[2] && b[0] - gap < a[2] && a[1] - gap < b[3] && b[1] - gap < a[3]) {
          a[0] = fminf(a[0], b[0]), a[1] = fminf(a[1], b[1]), a[2] = fmaxf(a[2], b[2]), a[3] = fmaxf(a[3], b[3]);
          a[4] = fmaxf(a[4], b[4]);
          if (b[8] > 0 && a[8] <= 0) memcpy(&a[5], &b[5], 4 * sizeof(float)); /* a panel's own edge colour wins */
          memcpy(H.r[j], H.r[--H.n], sizeof H.r[0]);
          merged = true;
          break;
        }
      }
  }
  int nm = 0;
  H.nb = H.n;
  memcpy(H.b, H.r, sizeof H.r[0] * (size_t)H.n);
  for (int i = 0; i < H.n; i++) {
    const float *r = H.r[i];
    if (nm < MAXMASK && r[4] > 0.3f) {
      float *m = A.mask[nm++];
      m[0] = r[0] / H.cs.x, m[1] = r[1] / H.cs.y, m[2] = r[2] / H.cs.x, m[3] = r[3] / H.cs.y;
    }
  }
  A.nmask = nm;
  A.dbg_boxes = H.n;
}

void ascii_hud_none(void) {
  H.n = H.nb = 0;
  A.nmask = 0, A.dbg_boxes = 0;
}

void ascii_hud_reset(cw_vec2 canvas, float u) {
  H.cs = canvas, H.u = u, H.n = 0;
}

void ascii_hud_draw(void) {
  if (!A.on || opt_on("ASCII_NO_HUD_BOXES")) return;
  for (int i = 0; i < H.nb; i++) {
    const float *r = H.b[i];
    cw_rect rr = cw_rect_make(r[0], r[1], r[2] - r[0], r[3] - r[1]);
    cw_r2d_round_rect(rr, 8 * H.u, 6, (cw_color){ 0, 0, 0, 0.85f * r[4] });
    border(rr, H.u, (cw_color){ r[5], r[6], r[7], 0.9f * r[4] });
  }
}
