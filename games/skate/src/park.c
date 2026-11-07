/* skate park geometry: the triangles the board rides and hits, the edges it grinds, and the kit that builds them.
 *
 * Every piece of a park (a floor, a quarter pipe, a bowl, a box, a rail, stairs) writes its triangles here, each with
 * smooth corner normals (a ramp's curve is exact between its rows: the board takes the normal at its contact point from
 * them, so riding a transition never feels the facets), what it is to the board (ride, wall, or drawn only) and what
 * it is drawn with. The same triangles are drawn (view.c) and ridden (skater.c): nothing is ridden that is not seen.
 *
 * Queries go through a grid of 2 m cells over the ground plan. park_cast is a segment against the triangles' front
 * faces; park_ground is the highest ride surface under a point. Pure C, deterministic. */
#include <stddef.h>

#include "game.h"

/* ── triangles and edges ───────────────────────────────────────────── */

static void push_tri(park *k, const ptri *t) {
  if (k->nt == k->cap_t) {
    k->cap_t = k->cap_t ? k->cap_t * 2 : 4096;
    k->t = realloc(k->t, sizeof *k->t * (size_t)k->cap_t);
  }
  k->t[k->nt++] = *t;
}

void park_free(park *k) {
  free(k->t), free(k->e), free(k->cell_start), free(k->cell_items), free(k->rest), free(k->rest_piece);
  memset(k, 0, sizeof *k);
}

void kit_begin(kit *b, park *k) {
  park_free(k);
  b->k = k;
  b->piece = 0;
  b->lift = 0;
  k->goal_run = -1;
  k->goal_gap = -1;
  k->ctl_block_line = k->ctl_short_line = -1;
}
int kit_piece(kit *b) {
  b->piece = ++b->k->npieces;
  return b->piece;
}
int kit_run(kit *b) { return b->k->nruns++; }

static void uv_for(ptri *t) {
  /* planar, in metres, on the face's main axis */
  v3 n = t->n;
  float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);
  const v3 *p[3] = { &t->a, &t->b, &t->c };
  for (int i = 0; i < 3; i++) {
    float u, v;
    if (ay >= ax && ay >= az) u = p[i]->x, v = p[i]->z;
    else if (ax >= az) u = p[i]->z, v = p[i]->y;
    else u = p[i]->x, v = p[i]->y;
    t->uvs[i * 2] = u, t->uvs[i * 2 + 1] = v;
  }
}

void kit_tri(kit *b, v3 a, v3 c, v3 d, v3 na, v3 nc, v3 nd, int surf, int mat, const float col[3]) {
  ptri t;
  memset(&t, 0, sizeof t);
  v3 n = vcross(vsub(c, a), vsub(d, a));
  float l = vlen(n);
  if (l < 1e-9f) return; /* degenerate */
  n = vmul(n, 1.0f / l);
  v3 want = vadd(vadd(na, nc), nd);
  if (vdot(n, want) < 0) { /* wound the other way: turn it to face its normals */
    v3 s = c; c = d; d = s;
    s = nc; nc = nd; nd = s;
    n = vmul(n, -1);
  }
  t.a = V(a.x, a.y + b->lift, a.z), t.b = V(c.x, c.y + b->lift, c.z), t.c = V(d.x, d.y + b->lift, d.z);
  t.na = vnorm(na), t.nb = vnorm(nc), t.nc = vnorm(nd);
  t.n = n;
  t.col[0] = col[0], t.col[1] = col[1], t.col[2] = col[2];
  t.surf = (uint8_t)surf, t.mat = (uint8_t)mat;
  t.piece = (uint16_t)b->piece;
  uv_for(&t);
  push_tri(b->k, &t);
}

/* a flat quad (a c d e in order round it); faces the side its winding gives, unless want is set */
static void quad_n(kit *b, v3 a, v3 c, v3 d, v3 e, v3 n, int surf, int mat, const float col[3]) {
  kit_tri(b, a, c, d, n, n, n, surf, mat, col);
  kit_tri(b, a, d, e, n, n, n, surf, mat, col);
}
void kit_quad(kit *b, v3 a, v3 c, v3 d, v3 e, int surf, int mat, const float col[3]) {
  v3 n = vnorm(vcross(vsub(c, a), vsub(d, a)));
  quad_n(b, a, c, d, e, n, surf, mat, col);
}

void kit_rest(kit *b, v3 p) {
  park *k = b->k;
  if (k->nrest == k->cap_rest) {
    k->cap_rest = k->cap_rest ? k->cap_rest * 2 : 256;
    k->rest = realloc(k->rest, sizeof *k->rest * (size_t)k->cap_rest);
    k->rest_piece = realloc(k->rest_piece, sizeof *k->rest_piece * (size_t)k->cap_rest);
  }
  k->rest[k->nrest] = V(p.x, p.y + b->lift, p.z);
  k->rest_piece[k->nrest++] = (uint16_t)b->piece;
}

int kit_edge(kit *b, v3 a, v3 c, v3 up, int kind, int run) {
  park *k = b->k;
  if (k->ne == k->cap_e) {
    k->cap_e = k->cap_e ? k->cap_e * 2 : 128;
    k->e = realloc(k->e, sizeof *k->e * (size_t)k->cap_e);
  }
  pedge *e = &k->e[k->ne];
  memset(e, 0, sizeof *e);
  e->a = V(a.x, a.y + b->lift, a.z), e->b = V(c.x, c.y + b->lift, c.z);
  e->len = vlen(vsub(c, a));
  e->t = vnorm(vsub(c, a));
  e->up = up;
  e->kind = (uint8_t)kind;
  e->piece = (uint16_t)b->piece;
  e->run = (uint16_t)run;
  e->prev = e->next = -1;
  return k->ne++;
}

/* ── the frame of a piece: yaw 0 rides toward -Z ─────────────────────── */
typedef struct frame { v3 o, F, R; } frame;
static frame frame_of(float x, float y, float z, float yaw) {
  float s, c;
  cw_sincos(yaw, &s, &c);
  return (frame){ V(x, y, z), V(-s, 0, -c), V(c, 0, -s) };
}
/* local (r across, u along, y up) to world */
static v3 at(const frame *f, float r, float u, float y) { return vadd(vadd(vadd(f->o, vmul(f->R, r)), vmul(f->F, u)), V(0, y, 0)); }
static v3 dir(const frame *f, float r, float u, float y) { return vnorm(vadd(vadd(vmul(f->R, r), vmul(f->F, u)), V(0, y, 0))); }

/* ── simple pieces ───────────────────────────────────────────────────── */

void kit_box(kit *b, float cx, float cz, float yaw, float w, float d, float y0, float y1, int mat_top, int mat_side, const float top[3], const float side[3]) {
  frame f = frame_of(cx, 0, cz, yaw);
  float hw = w * 0.5f, hd = d * 0.5f;
  v3 c0[4] = { at(&f, -hw, -hd, y0), at(&f, hw, -hd, y0), at(&f, hw, hd, y0), at(&f, -hw, hd, y0) };
  v3 c1[4] = { at(&f, -hw, -hd, y1), at(&f, hw, -hd, y1), at(&f, hw, hd, y1), at(&f, -hw, hd, y1) };
  quad_n(b, c1[0], c1[1], c1[2], c1[3], V(0, 1, 0), SURF_RIDE, mat_top, top);
  v3 out[4] = { vmul(f.F, -1), f.R, f.F, vmul(f.R, -1) };
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    /* side i runs from corner i to corner j; its outward normal */
    v3 mid = vmul(vadd(c0[i], c0[j]), 0.5f), cen = V(cx, mid.y, cz);
    v3 n = vnorm(vsub(mid, cen));
    (void)out;
    quad_n(b, c0[i], c0[j], c1[j], c1[i], n, SURF_WALL, mat_side, side);
  }
  for (int i = 0; i < 4; i++) kit_rest(b, c0[i]);
  kit_rest(b, V(cx, y0, cz));
}

void kit_floor(kit *b, float cx, float cz, float yaw, float w, float d, float y, int mat, const float col[3], float tile_m) {
  frame f = frame_of(cx, 0, cz, yaw);
  int nx = CW_MAX(1, (int)(w / tile_m + 0.5f)), nz = CW_MAX(1, (int)(d / tile_m + 0.5f));
  for (int i = 0; i < nx; i++)
    for (int j = 0; j < nz; j++) {
      float r0 = -w * 0.5f + w * (float)i / (float)nx, r1 = -w * 0.5f + w * (float)(i + 1) / (float)nx;
      float u0 = -d * 0.5f + d * (float)j / (float)nz, u1 = -d * 0.5f + d * (float)(j + 1) / (float)nz;
      quad_n(b, at(&f, r0, u0, y), at(&f, r1, u0, y), at(&f, r1, u1, y), at(&f, r0, u1, y), V(0, 1, 0), SURF_RIDE, mat, col);
    }
  (void)0;
}

/* ── profiles ────────────────────────────────────────────────────────── */

static void prof_add(profile *p, float u, float y, float nu, float ny) {
  if (p->n >= PROF_MAX) return;
  p->u[p->n] = u, p->y[p->n] = y, p->nu[p->n] = nu, p->ny[p->n] = ny;
  p->n++;
}
void prof_quarter(profile *p, float r, float h, int segs) {
  p->n = 0;
  float top = h < r ? h : r;
  float amax = cw_acos(1 - top / r);
  for (int i = 0; i <= segs; i++) {
    float a = amax * (float)i / (float)segs, s, c;
    cw_sincos(a, &s, &c);
    prof_add(p, r * s, r * (1 - c), -s, c);
  }
  if (h > r + 0.01f) { /* vert: straight up past the radius */
    int nv = CW_MAX(1, (int)((h - r) / 0.5f + 0.5f));
    for (int i = 1; i <= nv; i++) prof_add(p, r, r + (h - r) * (float)i / (float)nv, -1, 0);
  }
}
void prof_kicker(profile *p, float r, float h, int segs) { prof_quarter(p, r, h, segs); }
void prof_bank(profile *p, float len, float h, float fr) {
  p->n = 0;
  float al = cw_atan2(h, len), s, c;
  cw_sincos(al, &s, &c);
  int segs = 6;
  for (int i = 0; i <= segs; i++) {
    float a = al * (float)i / (float)segs, sa, ca;
    cw_sincos(a, &sa, &ca);
    prof_add(p, fr * sa, fr * (1 - ca), -sa, ca);
  }
  float u0 = fr * s, y0 = fr * (1 - c);
  float rest = (h - y0) / (s / c); /* run left at the slope */
  int n = CW_MAX(1, (int)(rest / 1.5f + 0.5f));
  for (int i = 1; i <= n; i++) {
    float k = (float)i / (float)n;
    prof_add(p, u0 + rest * k, y0 + (h - y0) * k, -s, c);
  }
}

/* ── a ramp swept along a straight run ───────────────────────────────── */

static void tube(kit *b, v3 a, v3 c, float r, int sides, const float col[3]) {
  v3 ax = vnorm(vsub(c, a));
  v3 up = fabsf(ax.y) > 0.9f ? V(1, 0, 0) : V(0, 1, 0);
  v3 x = vnorm(vcross(up, ax)), y = vcross(ax, x);
  for (int i = 0; i < sides; i++) {
    float a0 = 2 * PI_F * (float)i / (float)sides, a1 = 2 * PI_F * (float)(i + 1) / (float)sides, s0, c0, s1, c1;
    cw_sincos(a0, &s0, &c0);
    cw_sincos(a1, &s1, &c1);
    v3 n0 = vadd(vmul(x, c0), vmul(y, s0)), n1 = vadd(vmul(x, c1), vmul(y, s1));
    v3 p0 = vmad(a, n0, r), p1 = vmad(a, n1, r), q0 = vmad(c, n0, r), q1 = vmad(c, n1, r);
    kit_tri(b, p0, p1, q1, n0, n1, n1, SURF_NONE, MAT_METAL, col);
    kit_tri(b, p0, q1, q0, n0, n1, n0, SURF_NONE, MAT_METAL, col);
  }
}

void kit_ramp(kit *b, float x, float z, float yaw, float w, const profile *pr, const ramp_opts *o) {
  frame f = frame_of(x, 0, z, yaw);
  float hw = w * 0.5f, y0 = o->y0;
  int n = pr->n;
  float utop = pr->u[n - 1], ytop = pr->y[n - 1];
  /* the face: rows across, cut every 2 m so the lighting has vertices to vary across */
  int cuts = CW_MAX(1, (int)(w / 2.0f + 0.5f));
  for (int i = 0; i + 1 < n; i++)
    for (int c = 0; c < cuts; c++) {
      float r0 = -hw + w * (float)c / (float)cuts, r1 = -hw + w * (float)(c + 1) / (float)cuts;
      v3 a = at(&f, r0, pr->u[i], y0 + pr->y[i]), bb = at(&f, r1, pr->u[i], y0 + pr->y[i]);
      v3 cc = at(&f, r1, pr->u[i + 1], y0 + pr->y[i + 1]), d = at(&f, r0, pr->u[i + 1], y0 + pr->y[i + 1]);
      v3 n0 = dir(&f, 0, pr->nu[i], pr->ny[i]), n1 = dir(&f, 0, pr->nu[i + 1], pr->ny[i + 1]);
      kit_tri(b, a, bb, cc, n0, n0, n1, SURF_RIDE, o->mat_face, o->face);
      kit_tri(b, a, cc, d, n0, n1, n1, SURF_RIDE, o->mat_face, o->face);
    }
  float uend = utop + o->deck;
  if (o->deck > 0.01f) { /* the deck behind the top */
    for (int c = 0; c < cuts; c++) {
      float r0 = -hw + w * (float)c / (float)cuts, r1 = -hw + w * (float)(c + 1) / (float)cuts;
      quad_n(b, at(&f, r0, utop, y0 + ytop), at(&f, r1, utop, y0 + ytop), at(&f, r1, uend, y0 + ytop), at(&f, r0, uend, y0 + ytop), V(0, 1, 0), SURF_RIDE, MAT_CONCRETE, o->deck_col);
    }
  }
  if (!o->no_back) /* the back, down to the ground */
    quad_n(b, at(&f, -hw, uend, y0), at(&f, hw, uend, y0), at(&f, hw, uend, y0 + ytop), at(&f, -hw, uend, y0 + ytop), f.F, SURF_WALL, o->mat_side, o->side);
  if (!o->no_sides)
    for (int sd = -1; sd <= 1; sd += 2) { /* the sides: the profile's outline down to the ground */
      v3 nn = vmul(f.R, (float)sd);
      float r = hw * (float)sd;
      for (int i = 0; i + 1 < n; i++)
        quad_n(b, at(&f, r, pr->u[i], y0), at(&f, r, pr->u[i + 1], y0), at(&f, r, pr->u[i + 1], y0 + pr->y[i + 1]), at(&f, r, pr->u[i], y0 + pr->y[i]), nn, SURF_WALL,
               o->mat_side, o->side);
      if (o->deck > 0.01f)
        quad_n(b, at(&f, r, utop, y0), at(&f, r, uend, y0), at(&f, r, uend, y0 + ytop), at(&f, r, utop, y0 + ytop), nn, SURF_WALL, o->mat_side, o->side);
    }
  if (o->coping) { /* steel coping on the lip: grindable */
    static const float steel[3] = { 0.78f, 0.8f, 0.84f };
    v3 a = at(&f, -hw, utop + 0.0f, y0 + ytop + 0.02f), c = at(&f, hw, utop, y0 + ytop + 0.02f);
    tube(b, at(&f, -hw, utop - 0.01f, y0 + ytop - 0.015f), at(&f, hw, utop - 0.01f, y0 + ytop - 0.015f), 0.035f, 8, steel);
    kit_edge(b, vmul(vadd(a, V(0, 0, 0)), 1), c, V(0, 1, 0), EDGE_COPING, o->run >= 0 ? o->run : kit_run(b));
  }
  kit_rest(b, at(&f, -hw, 0.02f, y0));
  kit_rest(b, at(&f, hw, 0.02f, y0));
  kit_rest(b, at(&f, -hw, uend - 0.02f, y0));
  kit_rest(b, at(&f, hw, uend - 0.02f, y0));
}

/* ── a bowl ──────────────────────────────────────────────────────────── */

/* a point round a rounded rectangle (half sizes hx, hz, corner radius rc) at parameter t in 0..1, its outward normal */
static void rrect(float hx, float hz, float rc, int i, int per, v3 *p, v3 *n) {
  /* four corners, each per points; corner k's centre and start angle */
  int k = i / per, j = i % per;
  float cx[4] = { hx - rc, -(hx - rc), -(hx - rc), hx - rc }, cz[4] = { -(hz - rc), -(hz - rc), hz - rc, hz - rc };
  float a0[4] = { 0, 0.5f * PI_F, PI_F, 1.5f * PI_F };
  float a = a0[k] + 0.5f * PI_F * (float)j / (float)(per - 1);
  float s, c;
  cw_sincos(a, &s, &c);
  /* angle 0 points +x, 90 points -z */
  *n = V(c, 0, -s);
  *p = V(cx[k] + rc * c, 0, cz[k] - rc * s);
}

void kit_bowl(kit *b, float cx, float cz, float hx, float hz, float rc, float depth, float r, int mat, const float col[3], const float coping[3]) {
  profile pr;
  prof_quarter(&pr, r, depth, 10);
  int n = pr.n;
  float utop = pr.u[n - 1];
  const int per = 9, nring = per * 4;
  v3 rim[64], out[64];
  for (int i = 0; i < nring; i++) {
    rrect(hx, hz, rc, i, per, &rim[i], &out[i]);
    rim[i] = vadd(rim[i], V(cx, 0, cz));
  }
  /* the walls */
  for (int i = 0; i < nring; i++) {
    int j = (i + 1) % nring;
    if (vlen(vsub(rim[i], rim[j])) < 1e-4f) continue; /* (the corners' shared points) */
    for (int q = 0; q + 1 < n; q++) {
      float in0 = utop - pr.u[q], in1 = utop - pr.u[q + 1];
      v3 a = vadd(vmad(rim[i], out[i], -in0), V(0, pr.y[q] - depth, 0)), bb = vadd(vmad(rim[j], out[j], -in0), V(0, pr.y[q] - depth, 0));
      v3 c = vadd(vmad(rim[j], out[j], -in1), V(0, pr.y[q + 1] - depth, 0)), d = vadd(vmad(rim[i], out[i], -in1), V(0, pr.y[q + 1] - depth, 0));
      /* the normal: the profile's (nu toward the rim, ny up): nu runs along out */
      v3 ni0 = vnorm(vadd(vmul(out[i], pr.nu[q]), V(0, pr.ny[q], 0))), nj0 = vnorm(vadd(vmul(out[j], pr.nu[q]), V(0, pr.ny[q], 0)));
      v3 ni1 = vnorm(vadd(vmul(out[i], pr.nu[q + 1]), V(0, pr.ny[q + 1], 0))), nj1 = vnorm(vadd(vmul(out[j], pr.nu[q + 1]), V(0, pr.ny[q + 1], 0)));
      kit_tri(b, a, bb, c, ni0, nj0, nj1, SURF_RIDE, mat, col);
      kit_tri(b, a, c, d, ni0, nj1, ni1, SURF_RIDE, mat, col);
    }
  }
  /* the floor: a fan from the middle */
  v3 mid = V(cx, -depth, cz);
  for (int i = 0; i < nring; i++) {
    int j = (i + 1) % nring;
    if (vlen(vsub(rim[i], rim[j])) < 1e-4f) continue;
    v3 a = vadd(vmad(rim[i], out[i], -utop), V(0, -depth, 0)), c = vadd(vmad(rim[j], out[j], -utop), V(0, -depth, 0));
    kit_tri(b, mid, a, c, V(0, 1, 0), V(0, 1, 0), V(0, 1, 0), SURF_RIDE, mat, col);
  }
  /* the coping: one loop of linked edges */
  int run = kit_run(b), first = -1, prev = -1;
  for (int i = 0; i < nring; i++) {
    int j = (i + 1) % nring;
    if (vlen(vsub(rim[i], rim[j])) < 1e-4f) continue;
    v3 a = vadd(vmad(rim[i], out[i], -0.01f), V(0, 0.02f, 0)), c = vadd(vmad(rim[j], out[j], -0.01f), V(0, 0.02f, 0));
    tube(b, vadd(vmad(rim[i], out[i], -0.02f), V(0, -0.015f, 0)), vadd(vmad(rim[j], out[j], -0.02f), V(0, -0.015f, 0)), 0.035f, 8, coping);
    int e = kit_edge(b, a, c, V(0, 1, 0), EDGE_COPING, run);
    if (prev >= 0) b->k->e[prev].next = e, b->k->e[e].prev = prev;
    else first = e;
    prev = e;
  }
  if (first >= 0 && prev >= 0) b->k->e[prev].next = first, b->k->e[first].prev = prev;
}

/* the deck round a bowl: from its rim out to a rectangle (half sizes ox, oz) at y 0 */
void kit_bowl_deck(kit *b, float cx, float cz, float hx, float hz, float rc, float ox, float oz, int mat, const float col[3]);
void kit_bowl_deck(kit *b, float cx, float cz, float hx, float hz, float rc, float ox, float oz, int mat, const float col[3]) {
  const int per = 9, nring = per * 4;
  for (int i = 0; i < nring; i++) {
    int j = (i + 1) % nring;
    v3 pi, ni, pj, nj;
    rrect(hx, hz, rc, i, per, &pi, &ni);
    rrect(hx, hz, rc, j, per, &pj, &nj);
    if (vlen(vsub(pi, pj)) < 1e-4f) continue;
    /* each rim point out along its normal to the rectangle */
    v3 qi, qj;
    int si, sj;
    for (int w = 0; w < 2; w++) {
      v3 p = w ? pj : pi, n = w ? nj : ni;
      float tx = fabsf(n.x) > 1e-6f ? ((n.x > 0 ? ox : -ox) - p.x) / n.x : 1e9f;
      float tz = fabsf(n.z) > 1e-6f ? ((n.z > 0 ? oz : -oz) - p.z) / n.z : 1e9f;
      float t = tx < tz ? tx : tz;
      int side = tx < tz ? (n.x > 0 ? 0 : 2) : (n.z > 0 ? 1 : 3);
      v3 q = vmad(p, n, t);
      if (w) qj = q, sj = side; else qi = q, si = side;
    }
    v3 A = V(cx + pi.x, 0, cz + pi.z), B = V(cx + pj.x, 0, cz + pj.z), C = V(cx + qj.x, 0, cz + qj.z), D = V(cx + qi.x, 0, cz + qi.z);
    v3 up = V(0, 1, 0);
    kit_tri(b, A, B, C, up, up, up, SURF_RIDE, mat, col);
    kit_tri(b, A, C, D, up, up, up, SURF_RIDE, mat, col);
    if (si != sj) { /* the rays turned a corner of the rectangle: fill it */
      float kx = (si == 0 || sj == 0) ? ox : -ox, kz = (si == 1 || sj == 1) ? oz : -oz;
      kit_tri(b, D, C, V(cx + kx, 0, cz + kz), up, up, up, SURF_RIDE, mat, col);
    }
  }
}

/* ── rails and stairs ────────────────────────────────────────────────── */

/* a rail from a to c (the bar's top), its feet at ya and yc */
void kit_rail_on(kit *b, v3 a, v3 c, float ya, float yc, const float col[3]);
void kit_rail_on(kit *b, v3 a, v3 c, float ya, float yc, const float col[3]) {
  tube(b, vadd(a, V(0, -0.03f, 0)), vadd(c, V(0, -0.03f, 0)), 0.03f, 8, col);
  float len = vlen(vsub(c, a));
  int posts = CW_MAX(2, (int)(len / 2.5f) + 2);
  for (int i = 0; i < posts; i++) {
    float t = 0.04f + 0.92f * (float)i / (float)(posts - 1);
    v3 top = vlerp(a, c, t);
    float fy = ya + (yc - ya) * t;
    tube(b, vadd(top, V(0, -0.05f, 0)), V(top.x, fy, top.z), 0.025f, 6, col);
    kit_rest(b, V(top.x, fy, top.z));
  }
  kit_edge(b, a, c, V(0, 1, 0), EDGE_RAIL, kit_run(b));
}

void kit_stairs(kit *b, float x, float z, float yaw, float w, float h, int n, float y0, int mat, const float col[3], int rail_side) {
  frame f = frame_of(x, 0, z, yaw);
  float rise = h / (float)n, run = 0.34f, len = run * (float)n, hw = w * 0.5f;
  /* the ride: one slope over the nosings, the steps drawn under it */
  v3 up = vnorm(vadd(vmul(f.F, rise), V(0, run, 0)));
  quad_n(b, at(&f, -hw, 0, y0 + h), at(&f, hw, 0, y0 + h), at(&f, hw, len, y0), at(&f, -hw, len, y0), up, SURF_RIDE, mat, col);
  for (int i = 0; i < n; i++) {
    float yt = y0 + h - rise * (float)i, u0 = run * (float)i, u1 = run * (float)(i + 1);
    float ys = yt - rise;
    quad_n(b, at(&f, -hw, u0, ys - 0.004f), at(&f, hw, u0, ys - 0.004f), at(&f, hw, u0, yt - 0.004f), at(&f, -hw, u0, yt - 0.004f), vmul(f.F, 1), SURF_NONE, mat, col);
    quad_n(b, at(&f, -hw, u0, ys - 0.004f), at(&f, hw, u0, ys - 0.004f), at(&f, hw, u1, ys - 0.004f), at(&f, -hw, u1, ys - 0.004f), V(0, 1, 0), SURF_NONE, mat, col);
  }
  for (int sd = -1; sd <= 1; sd += 2) { /* cheeks: wedges down to the ground */
    v3 nn = vmul(f.R, (float)sd);
    float r = hw * (float)sd;
    kit_tri(b, at(&f, r, 0, y0), at(&f, r, len, y0), at(&f, r, 0, y0 + h), nn, nn, nn, SURF_WALL, mat, col);
  }
  kit_rest(b, at(&f, -hw, len - 0.02f, y0));
  kit_rest(b, at(&f, hw, len - 0.02f, y0));
  if (rail_side) {
    static const float rc[3] = { 0.85f, 0.25f, 0.2f };
    float r = (hw - 0.15f) * (float)rail_side;
    v3 a = at(&f, r, -0.6f, y0 + h + 0.82f), c = at(&f, r, len + 0.5f, y0 + 0.82f - 0.5f * rise / run * 0 /* level past the foot */);
    /* the rail follows the slope over the steps, then runs level a little past the top and the foot */
    v3 a2 = at(&f, r, 0, y0 + h + 0.82f), c2 = at(&f, r, len, y0 + 0.82f);
    kit_piece(b); /* the handrail is its own piece: it stands on the steps */
    kit_rail_on(b, a2, c2, y0 + h, y0, rc);
    int e0 = b->k->ne - 1;
    kit_rail_on(b, a, a2, y0 + h, y0 + h, rc);
    int e1 = b->k->ne - 1;
    kit_rail_on(b, c2, c, y0, y0, rc);
    int e2 = b->k->ne - 1;
    park *k = b->k;
    int run_id = k->e[e0].run;
    k->e[e1].run = k->e[e2].run = (uint16_t)run_id;
    k->e[e1].next = e0, k->e[e0].prev = e1;
    k->e[e0].next = e2, k->e[e2].prev = e0;
    k->nruns -= 2;
  }
}

/* ── the ground and the park's edge ──────────────────────────────────── */

static int cmpf(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return x < y ? -1 : x > y;
}
void kit_ground(kit *b, float x0, float z0, float x1, float z1, const float *holes, int nholes, int mat, const float col[3]) {
  float zs[64];
  int nz = 0;
  zs[nz++] = z0, zs[nz++] = z1;
  for (int h = 0; h < nholes && nz < 62; h++) {
    const float *r = holes + h * 4;
    if (r[1] > z0 && r[1] < z1) zs[nz++] = r[1];
    if (r[3] > z0 && r[3] < z1) zs[nz++] = r[3];
  }
  qsort(zs, (size_t)nz, sizeof *zs, cmpf);
  v3 up = V(0, 1, 0);
  for (int i = 0; i + 1 < nz; i++) {
    float za = zs[i], zb = zs[i + 1];
    if (zb - za < 1e-4f) continue;
    float zm = (za + zb) * 0.5f;
    /* the holes across this band, as x intervals, sorted */
    float hx[64];
    int nh = 0;
    for (int h = 0; h < nholes && nh < 62; h++) {
      const float *r = holes + h * 4;
      if (zm > r[1] && zm < r[3]) hx[nh++] = r[0], hx[nh++] = r[2];
    }
    /* sort the pairs by their start */
    for (int a = 0; a < nh; a += 2)
      for (int c = a + 2; c < nh; c += 2)
        if (hx[c] < hx[a]) { float t0 = hx[a], t1 = hx[a + 1]; hx[a] = hx[c], hx[a + 1] = hx[c + 1], hx[c] = t0, hx[c + 1] = t1; }
    float x = x0;
    for (int a = 0; a <= nh; a += 2) {
      float xe = a < nh ? hx[a] : x1;
      /* in 8 m pieces, so the light has corners to vary across */
      while (xe - x > 1e-4f) {
        float xn = fminf(xe, x + 8.0f);
        for (float zz = za; zz < zb - 1e-4f; zz += 8.0f) {
          float zn = fminf(zb, zz + 8.0f);
          /* each slab a shade apart (a poured park is a patchwork), from its corner: the same on every host */
          uint32_t hh = (uint32_t)(int)(x * 7.0f) * 73856093u ^ (uint32_t)(int)(zz * 7.0f) * 19349663u;
          hh = (hh ^ (hh >> 13)) * 1274126177u;
          float k = 0.93f + 0.1f * (float)((hh >> 8) & 255) / 255.0f;
          const float tint[3] = { col[0] * k, col[1] * k, col[2] * (k * 0.98f + 0.02f) };
          quad_n(b, V(x, 0, zz), V(xn, 0, zz), V(xn, 0, zn), V(x, 0, zn), up, SURF_RIDE, mat, tint);
        }
        x = xn;
      }
      if (a < nh) x = fmaxf(x, hx[a + 1]);
    }
  }
}

void kit_fence(kit *b, float x0, float z0, float x1, float z1, float h, int mat, const float col[3]) {
  v3 c[4] = { V(x0, 0, z0), V(x1, 0, z0), V(x1, 0, z1), V(x0, 0, z1) };
  v3 in[4] = { V(0, 0, 1), V(-1, 0, 0), V(0, 0, -1), V(1, 0, 0) };
  const float t = 0.3f;
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    v3 a = c[i], d = c[j];
    float len = vlen(vsub(d, a));
    int segs = CW_MAX(1, (int)(len / 6.0f));
    for (int s = 0; s < segs; s++) {
      v3 p = vlerp(a, d, (float)s / (float)segs), q = vlerp(a, d, (float)(s + 1) / (float)segs);
      v3 po = vmad(p, in[i], -t), qo = vmad(q, in[i], -t);
      quad_n(b, p, q, vadd(q, V(0, h, 0)), vadd(p, V(0, h, 0)), in[i], SURF_WALL, mat, col);
      quad_n(b, po, qo, vadd(qo, V(0, h, 0)), vadd(po, V(0, h, 0)), vmul(in[i], -1), SURF_NONE, mat, col);
      quad_n(b, vadd(p, V(0, h, 0)), vadd(q, V(0, h, 0)), vadd(qo, V(0, h, 0)), vadd(po, V(0, h, 0)), V(0, 1, 0), SURF_WALL, mat, col);
    }
  }
}

/* ── finishing: bounds, the grid, linked edges ───────────────────────── */

void park_finish(park *k) {
  k->lo = V(1e9f, 1e9f, 1e9f), k->hi = V(-1e9f, -1e9f, -1e9f);
  for (int i = 0; i < k->nt; i++) { /* (what is only drawn, the land round the park, is left out of its bounds) */
    if (k->t[i].surf == SURF_NONE) continue;
    const v3 *p[3] = { &k->t[i].a, &k->t[i].b, &k->t[i].c };
    for (int j = 0; j < 3; j++) {
      k->lo.x = fminf(k->lo.x, p[j]->x), k->lo.y = fminf(k->lo.y, p[j]->y), k->lo.z = fminf(k->lo.z, p[j]->z);
      k->hi.x = fmaxf(k->hi.x, p[j]->x), k->hi.y = fmaxf(k->hi.y, p[j]->y), k->hi.z = fmaxf(k->hi.z, p[j]->z);
    }
  }
  k->cell = 2.0f;
  k->ox = k->lo.x - 1, k->oz = k->lo.z - 1;
  k->gx = (int)((k->hi.x - k->ox) / k->cell) + 2;
  k->gz = (int)((k->hi.z - k->oz) / k->cell) + 2;
  int nc = k->gx * k->gz;
  free(k->cell_start), free(k->cell_items);
  k->cell_start = calloc((size_t)nc + 1, sizeof(int));
  /* count, then fill (a triangle in every cell its box touches; drawn-only ones in none) */
  for (int pass = 0; pass < 2; pass++) {
    int *fill = pass ? calloc((size_t)nc, sizeof(int)) : NULL;
    for (int i = 0; i < k->nt; i++) {
      const ptri *t = &k->t[i];
      if (t->surf == SURF_NONE) continue;
      float x0 = fminf(t->a.x, fminf(t->b.x, t->c.x)), x1 = fmaxf(t->a.x, fmaxf(t->b.x, t->c.x));
      float z0 = fminf(t->a.z, fminf(t->b.z, t->c.z)), z1 = fmaxf(t->a.z, fmaxf(t->b.z, t->c.z));
      int cx0 = (int)((x0 - k->ox) / k->cell), cx1 = (int)((x1 - k->ox) / k->cell);
      int cz0 = (int)((z0 - k->oz) / k->cell), cz1 = (int)((z1 - k->oz) / k->cell);
      for (int z = cz0; z <= cz1; z++)
        for (int x = cx0; x <= cx1; x++) {
          int c = z * k->gx + x;
          if (pass == 0) k->cell_start[c + 1]++;
          else k->cell_items[k->cell_start[c] + fill[c]++] = i;
        }
    }
    if (pass == 0) {
      for (int c = 0; c < nc; c++) k->cell_start[c + 1] += k->cell_start[c];
      k->cell_items = malloc(sizeof(int) * (size_t)(k->cell_start[nc] + 1));
    }
    free(fill);
  }
  /* edges that meet end to end join up (a rail's kinks, a coping round its corners): within 2 cm, along a run */
  for (int i = 0; i < k->ne; i++) {
    pedge *a = &k->e[i];
    if (a->next >= 0) continue;
    for (int j = 0; j < k->ne; j++) {
      if (j == i || k->e[j].prev >= 0) continue;
      pedge *c = &k->e[j];
      if (vlen(vsub(a->b, c->a)) < 0.02f && vdot(a->t, c->t) > 0.5f) { a->next = j, c->prev = i; break; }
    }
  }
}

/* ── queries ─────────────────────────────────────────────────────────── */

static uint32_t *stamp;
static int stamp_cap;
static uint32_t stamp_id;

static bool ray_tri(const ptri *t, v3 o, v3 d, float *tt, float *uu, float *vv) {
  v3 e1 = vsub(t->b, t->a), e2 = vsub(t->c, t->a);
  v3 p = vcross(d, e2);
  float det = vdot(e1, p);
  if (det < 1e-12f) return false; /* parallel, or crossing from behind: det = -d.(e1 x e2), so a front face (d against the normal) has det > 0 */
  float inv = 1.0f / det;
  v3 s = vsub(o, t->a);
  float u = vdot(s, p) * inv;
  if (u < -1e-5f || u > 1 + 1e-5f) return false;
  v3 q = vcross(s, e1);
  float v = vdot(d, q) * inv;
  if (v < -1e-5f || u + v > 1 + 1e-5f) return false;
  float t0 = vdot(e2, q) * inv;
  if (t0 < 0 || t0 > 1) return false;
  *tt = t0, *uu = u, *vv = v;
  return true;
}

v3 park_normal_at(const park *k, int i, v3 p) {
  const ptri *t = &k->t[i];
  v3 e1 = vsub(t->b, t->a), e2 = vsub(t->c, t->a), q = vsub(p, t->a);
  float d00 = vdot(e1, e1), d01 = vdot(e1, e2), d11 = vdot(e2, e2), d20 = vdot(q, e1), d21 = vdot(q, e2);
  float den = d00 * d11 - d01 * d01;
  if (fabsf(den) < 1e-12f) return t->n;
  float v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den, u = 1 - v - w;
  if (g_phys.no_smooth) return t->n;
  return vnorm(vadd(vadd(vmul(t->na, u), vmul(t->nb, v)), vmul(t->nc, w)));
}

bool park_cast(const park *k, v3 o, v3 d, unsigned mask, phit *h) {
  if (!k->nt || !k->cell_start) return false;
  if (stamp_cap < k->nt) {
    free(stamp);
    stamp = calloc((size_t)k->nt, sizeof *stamp);
    stamp_cap = k->nt;
    stamp_id = 0;
  }
  if (++stamp_id == 0) { memset(stamp, 0, sizeof *stamp * (size_t)stamp_cap); stamp_id = 1; }
  v3 e = vadd(o, d);
  float x0 = fminf(o.x, e.x), x1 = fmaxf(o.x, e.x), z0 = fminf(o.z, e.z), z1 = fmaxf(o.z, e.z);
  int cx0 = CW_MAX(0, (int)((x0 - k->ox) / k->cell)), cx1 = CW_MIN(k->gx - 1, (int)((x1 - k->ox) / k->cell));
  int cz0 = CW_MAX(0, (int)((z0 - k->oz) / k->cell)), cz1 = CW_MIN(k->gz - 1, (int)((z1 - k->oz) / k->cell));
  if (x1 < k->ox || z1 < k->oz) return false;
  float best = 2;
  int bi = -1;
  float bu = 0, bv = 0;
  for (int z = cz0; z <= cz1; z++)
    for (int x = cx0; x <= cx1; x++) {
      int c = z * k->gx + x;
      for (int s = k->cell_start[c]; s < k->cell_start[c + 1]; s++) {
        int i = k->cell_items[s];
        if (stamp[i] == stamp_id) continue;
        stamp[i] = stamp_id;
        const ptri *t = &k->t[i];
        if (!(mask & (1u << t->surf))) continue;
        float tt, uu, vv;
        if (ray_tri(t, o, d, &tt, &uu, &vv) && tt < best) best = tt, bi = i, bu = uu, bv = vv;
      }
    }
  if (bi < 0) return false;
  const ptri *t = &k->t[bi];
  h->t = best;
  h->p = vmad(o, d, best);
  h->tri = bi;
  h->fn = t->n;
  if (g_phys.no_smooth) h->n = t->n;
  else h->n = vnorm(vadd(vadd(vmul(t->na, 1 - bu - bv), vmul(t->nb, bu)), vmul(t->nc, bv)));
  return true;
}

bool park_ground(const park *k, v3 p, float above, float below, phit *h) {
  return park_cast(k, V(p.x, p.y + above, p.z), V(0, -(above + below), 0), 1u << SURF_RIDE, h);
}

/* ── the float check ─────────────────────────────────────────────────── */

int park_check_floats(const park *k, char *out, size_t cap) {
  int bad = 0;
  size_t used = 0;
  if (out && cap) out[0] = 0;
  for (int i = 0; i < k->nrest; i++) {
    v3 p = k->rest[i];
    if (p.y <= 0.001f && p.y >= -0.001f) continue; /* on the earth */
    /* something of another piece directly under it, within 2 cm */
    bool ok = false;
    for (int dx = -1; dx <= 1 && !ok; dx++)
      for (int dz = -1; dz <= 1 && !ok; dz++) {
        v3 q = V(p.x + 0.01f * (float)dx, p.y + 0.05f, p.z + 0.01f * (float)dz);
        /* any surface but this piece's own; a cast only sees front faces, so walls are found by testing both ways */
        phit h;
        if (park_cast(k, q, V(0, -0.07f, 0), (1u << SURF_RIDE) | (1u << SURF_WALL), &h) && k->t[h.tri].piece != k->rest_piece[i]) ok = true;
      }
    if (!ok) {
      bad++;
      if (out && used + 96 < cap) used += (size_t)snprintf(out + used, cap - used, "piece %d floats at (%.2f, %.2f, %.2f)\n", k->rest_piece[i], p.x, p.y, p.z);
    }
  }
  return bad;
}

/* the drawn geometry's fingerprint (corners, normals, tints, materials, coordinates): a park's bake is for this shape */
uint32_t park_geom_hash(const park *k) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < k->nt; i++) {
    const ptri *t = &k->t[i];
    const unsigned char *p = (const unsigned char *)t;
    for (size_t j = 0; j < offsetof(ptri, uvs) + sizeof t->uvs; j++) h = (h ^ p[j]) * 16777619u;
    h = (h ^ (uint32_t)t->mat) * 16777619u;
    h = (h ^ (uint32_t)t->surf) * 16777619u;
  }
  return h;
}
