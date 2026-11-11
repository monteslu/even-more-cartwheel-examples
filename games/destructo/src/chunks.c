/* destructo's chunks and support graph.
 *
 * What stands. A structure is laid as blocks (sim.c, landmarks.c, levels.c) and then gathered into CHUNKS: a chunk is one
 * rigid body of several blocks (a compound of box shapes). A steel lattice's welded members are one chunk, a course of
 * masonry bonded by its mortar a chunk (bricks that touch inside one cell of space), a loose brick or a piece of rubble
 * a chunk of one. The chunks start STATIC: a static body is rigid whatever is above it, costs the physics nothing, and
 * nothing is held to its neighbours by a joint (a joint is a soft constraint: a chain of them sags, hums and flops).
 *
 * What holds it up is the SUPPORT GRAPH. Every placed block has links to the blocks it rests on (a block under it, or
 * the ground) with the footprint they touch. A static chunk stands while the centre of its mass is inside the hull of
 * the footprints of the standing chunks (and the ground) it rests on, by the margin it had as built (so a structure
 * that stands as built stands). When blocks are lost (broken by a blow) the pieces that fall apart are cut apart (the
 * bonds are walked), and any chunk whose centre is no longer over its support is let go: it goes DYNAMIC and falls as one
 * solid piece (nothing inside it moves against anything else), and the chunks resting on it lose their support in
 * turn: a collapse runs up the structure a layer a tick.
 *
 * What breaks. A dynamic chunk that lands hard (a hit event) breaks in two along its long side, each half on its way
 * with the speed that part of it had, and breaks again where it lands, down to single blocks, which crush on a hard
 * enough blow (sim.c harm). A chunk too heavy to be moved by a blow (impulse over mass under V_MOVE) is not moved: the
 * blow breaks the blocks it lands on and what rests over the hole falls.
 *
 * Everything here is deterministic: integer hashes, sorted queries, no clocks. */
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"
#include "opts.h"
#include "box3d/box3d.h"

extern double (*g_sim_clock)(void);
double g_ch_ms[6]; /* profiling: set type, carve, resolve, support pass (ms, summed) */
/* the scratch arrays below are shared by every game in the process, and a level is built on a background job (loader.c) while another
 * game steps: one lock holds the whole of a build, a step, a push or a break */
static atomic_flag CH_LOCK = ATOMIC_FLAG_INIT;
static void ch_lock(void) { while (atomic_flag_test_and_set_explicit(&CH_LOCK, memory_order_acquire)) {} }
static void ch_unlock(void) { atomic_flag_clear_explicit(&CH_LOCK, memory_order_release); }
#define CH_MAX (MAXBLK + 4096)
#define LAYER_BLOCK_BIT 2u
#define V_MOVE 0.25f      /* m/s: a standing chunk a blow moves less than this stays where it is (the blow still breaks what it lands on) */
#define SECTION 150       /* members: a lattice let go breaks into sections of at most this many at once */
#define TOUCH 0.012f      /* m: blocks this close are touching */

static inline t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static inline t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline t3_vec3 vsub(t3_vec3 a, t3_vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline t3_vec3 vscale(t3_vec3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static inline t3_vec3 vcross(t3_vec3 a, t3_vec3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float vlen(t3_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
static t3_vec3 qrot(t3_quat q, t3_vec3 v) {
  t3_vec3 u = v3(q.x, q.y, q.z);
  t3_vec3 t = vscale(vcross(u, v), 2);
  return vadd(vadd(v, vscale(t, q.w)), vcross(u, t));
}
static inline t3_vec3 wcom(cw_body3d b) { b3Pos p = b3Body_GetWorldCenterOfMass(cw_body3d_raw(b)); return v3(p.x, p.y, p.z); }

/* ── chunks ──────────────────────────────────────────────────────────── */

int chunk_new(game *g, cw_body3d body, bool dyn, bool placed) {
  if (g->ncfree == 0 && g->nch == g->ch_cap) { /* grows as the level needs (a game is three at once while a level loads) */
    int cap = g->ch_cap ? g->ch_cap * 2 : 1024;
    if (cap > CH_MAX) cap = CH_MAX;
    if (cap == g->ch_cap) return -1;
    chunk *nc = realloc(g->ch, (size_t)cap * sizeof *nc);
    int *nf = realloc(g->cfree, (size_t)cap * sizeof *nf);
    if (!nc || !nf) return -1;
    memset(nc + g->ch_cap, 0, (size_t)(cap - g->ch_cap) * sizeof *nc);
    g->ch = nc, g->cfree = nf, g->ch_cap = cap;
  }
  int c = g->ncfree > 0 ? g->cfree[--g->ncfree] : g->nch++;
  chunk *k = &g->ch[c];
  memset(k, 0, sizeof *k);
  k->leader = c, k->anext = -1, k->body = body, k->head = -1, k->alive = 1, k->dyn = dyn, k->placed = placed, k->frac_tick = -100;
  k->stamp = ++g->epoch;
  return c;
}

void chunk_add_member(game *g, int c, int i) {
  chunk *k = &g->ch[c];
  block *b = &g->b[i];
  b->chunk = c, b->cprev = -1, b->cnext = k->head;
  if (k->head >= 0) g->b[k->head].cprev = i;
  k->head = i, k->n++;
}

static void unlink_member(game *g, int i) {
  block *b = &g->b[i];
  chunk *k = &g->ch[b->chunk];
  if (b->cprev >= 0) g->b[b->cprev].cnext = b->cnext; else k->head = b->cnext;
  if (b->cnext >= 0) g->b[b->cnext].cprev = b->cprev;
  k->n--;
}

/* an assembly is the chunks bonded as one rigid structure (a lattice's sections): the leader chunk carries its measure, stamp and
 * judgement; they stand or go together */
static void asm_unlink(game *g, int c) {
  int L = g->ch[c].leader;
  if (L == c) {
    int nx = g->ch[c].anext;
    if (nx >= 0) {
      chunk *a = &g->ch[c], *n = &g->ch[nx];
      n->d0 = a->d0, n->seen = a->seen, n->stamp = a->stamp, n->dirty = 1, n->mass = a->mass, n->com = a->com, n->pq = 0;
      for (int cc = nx; cc >= 0; cc = g->ch[cc].anext) g->ch[cc].leader = nx;
    }
  } else {
    int p = L;
    while (p >= 0 && g->ch[p].anext != c) p = g->ch[p].anext;
    if (p >= 0) g->ch[p].anext = g->ch[c].anext;
  }
  g->ch[c].leader = c, g->ch[c].anext = -1;
}

static void chunk_free(game *g, int c) {
  chunk *k = &g->ch[c];
  asm_unlink(g, c);
  if (cw_body3d_valid(k->body)) cw_body3d_destroy(k->body);
  k->alive = 0, k->stamp = ++g->epoch;
  g->cfree[g->ncfree++] = c;
}

void chunk_remove_member(game *g, int i) {
  block *b = &g->b[i];
  int c = b->chunk;
  chunk *k = &g->ch[c];
  if (k->n > 1 && cw_shape3d_valid(b->shape)) cw_shape3d_destroy(b->shape);
  unlink_member(g, i);
  g->sup_dirty = 1;
  int L = k->leader;
  if (k->n == 0) {
    int Ld = L == c ? k->anext : L;
    chunk_free(g, c);
    if (Ld >= 0) g->ch[Ld].stamp = ++g->epoch, g->ch[Ld].dirty = 1;
  } else g->ch[L].stamp = ++g->epoch, g->ch[L].dirty = 1;
}

/* the pieces of a chunk: the members in list `mem` leave for a new chunk of their own at the same pose, each moving as it
 * moved (a point of a turning body moves with the body's speed plus its turn about the middle) */
static int chunk_carve(game *g, int c, const int *mem, int nm) {
  double t0 = g_sim_clock();
  chunk *k = &g->ch[c];
  cw_xform3 X = cw_body3d_transform(k->body);
  bool dyn = k->dyn;
  t3_vec3 v0 = v3(0, 0, 0), w0 = v3(0, 0, 0), com0 = v3(0, 0, 0);
  if (dyn) v0 = cw_body3d_velocity(k->body), w0 = cw_body3d_angular_velocity(k->body), com0 = wcom(k->body);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = dyn ? CW_BODY_DYNAMIC : CW_BODY_STATIC;
  bd.position = X.p, bd.rotation = X.q;
  bd.interpolate = true;
  bd.angular_damping = dyn ? 0.15f : 0, bd.linear_damping = dyn ? 0.02f : 0;
  cw_body3d nb = cw_body3d_create(g->w, &bd);
  int nc = chunk_new(g, nb, dyn, k->placed);
  if (nc < 0) { cw_body3d_destroy(nb); return -1; }
  k = &g->ch[c];
  g->ch[nc].frac_tick = k->frac_tick, g->ch[nc].rel_tick = k->rel_tick;
  for (int m = 0; m < nm; m++) {
    block *b = &g->b[mem[m]];
    cw_shape3d_destroy(b->shape);
    unlink_member(g, mem[m]);
    b->shape = block_shape(g, mem[m], nb, b->local);
    chunk_add_member(g, nc, mem[m]);
  }
  g->ch[k->leader].stamp = ++g->epoch, g->ch[nc].stamp = g->epoch, g->ch[k->leader].dirty = 1, g->ch[nc].dirty = 1;
  g->sup_dirty = 1;
  if (dyn) {
    for (int pass = 0; pass < 2; pass++) {
      cw_body3d b = pass ? k->body : nb;
      b3BodyId rb = cw_body3d_raw(b);
      t3_vec3 com = wcom(b);
      cw_body3d_set_velocity(b, vadd(v0, vcross(w0, vsub(com, com0))));
      cw_body3d_set_angular_velocity(b, w0);
      b3Body_SetSleepThreshold(rb, 0.12f);
      b3Body_SetAwake(rb, true);
    }
  } else {
    /* a static chunk cut in pieces: its measure of what it carries is made again */
    g->ch[nc].mass = 0;
  }
  g_ch_ms[1] += (g_sim_clock() - t0) * 1000;
  return nc;
}

/* what the assembly weighs and where its centre is */
static void chunk_measure(game *g, int c) {
  float m = 0;
  t3_vec3 s = v3(0, 0, 0);
  for (int cc = c; cc >= 0; cc = g->ch[cc].anext)
    for (int i = g->ch[cc].head; i >= 0; i = g->b[i].cnext) {
      float mi = MATS[g->b[i].mat].density * g->b[i].vol;
      t3_vec3 p = game_block_xform(g, i).p;
      m += mi, s = vadd(s, vscale(p, mi));
    }
  g->ch[c].mass = m, g->ch[c].com = m > 0 ? vscale(s, 1.0f / m) : v3(0, 0, 0);
}

/* ── pieces: the bonds, and where a chunk breaks ─────────────────────── */

static int comp_of[MAXBLK], queue_[MAXBLK], mem_[MAXBLK], key_i[MAXBLK];
static float key_f[MAXBLK];

/* an assembly (or a free chunk) that lost blocks may have come apart: its members are walked along the bonds, and every part but the
 * biggest is a piece of its own (a new assembly: what it rests on decides whether it stays) */
static void resolve(game *g, int c) {
  chunk *k = &g->ch[c];
  k->dirty = 0;
  if (k->n <= 0 && k->anext < 0) return;
  if (!k->placed || !g->bnd_off) { if (!k->dyn) chunk_measure(g, c); return; }
  int n = 0;
  for (int cc = c; cc >= 0; cc = g->ch[cc].anext)
    for (int i = g->ch[cc].head; i >= 0; i = g->b[i].cnext) mem_[n++] = i, comp_of[i] = -1;
  if (n == 0) return;
  int ncomp = 0, biggest = 0, bsize = 0;
  for (int s = 0; s < n; s++) {
    int r = mem_[s];
    if (comp_of[r] >= 0) continue;
    int qh = 0, qt = 0, id = ncomp < 63 ? ncomp++ : 63;
    queue_[qt++] = r, comp_of[r] = id;
    while (qh < qt) {
      int u = queue_[qh++];
      for (int e = g->bnd_off[u]; e < g->bnd_off[u + 1]; e++) {
        int w = g->bnd_to[e];
        if (!g->b[w].alive || g->b[w].gen || g->ch[g->b[w].chunk].leader != c || comp_of[w] >= 0) continue;
        comp_of[w] = id, queue_[qt++] = w;
      }
    }
    if (qt > bsize) bsize = qt, biggest = id;
  }
  int bigchunk = -1;
  for (int s = 0; s < n && bigchunk < 0; s++) if (comp_of[mem_[s]] == biggest) bigchunk = g->b[mem_[s]].chunk;
  if (ncomp > 1) {
    static int out_[MAXBLK];
    for (int id = 0; id < ncomp; id++) {
      if (id == biggest) continue;
      int newL = -1, tail = -1;
      for (int s = 0; s < n; s++) {
        int m0 = mem_[s];
        if (!(comp_of[m0] == id || (id == 63 && comp_of[m0] >= 63))) continue;
        int cc = g->b[m0].chunk, nm = 0;
        for (int t = s; t < n; t++) { int m1 = mem_[t]; if ((comp_of[m1] == id || (id == 63 && comp_of[m1] >= 63)) && g->b[m1].chunk == cc) out_[nm++] = m1, comp_of[m1] = -2; }
        int nc = chunk_carve(g, cc, out_, nm);
        if (nc < 0) continue;
        if (g->ch[cc].dyn) { newL = -1; continue; }
        g->ch[nc].d0 = 0;
        if (newL < 0) newL = tail = nc; else g->ch[tail].anext = nc, g->ch[nc].leader = newL, tail = nc;
      }
    }
    /* chunks left empty go */
    for (int cc = g->ch[bigchunk].leader, nx; cc >= 0; cc = nx) { nx = g->ch[cc].anext; if (g->ch[cc].n == 0) chunk_free(g, cc); }
  }
  c = g->ch[bigchunk].leader;
  if (!g->ch[c].dyn) chunk_measure(g, c);
  g->ch[c].dirty = 0;
}

static int cmp_key(const void *a, const void *b) {
  int ia = *(const int *)a, ib = *(const int *)b;
  if (key_f[ia] < key_f[ib]) return -1;
  if (key_f[ia] > key_f[ib]) return 1;
  return ia - ib;
}

/* a dynamic chunk breaks in two along its long side, at a place between a third and two thirds along it (the same on every host) */
static int fracture_i(game *g, int c) {
  chunk *k = &g->ch[c];
  if (!k->alive || k->n < 2) return -1;
  int n = 0;
  t3_vec3 lo = v3(1e9f, 1e9f, 1e9f), hi = v3(-1e9f, -1e9f, -1e9f);
  for (int i = k->head; i >= 0; i = g->b[i].cnext) {
    mem_[n++] = i;
    t3_vec3 p = g->b[i].local.p;
    if (p.x < lo.x) lo.x = p.x;
    if (p.y < lo.y) lo.y = p.y;
    if (p.z < lo.z) lo.z = p.z;
    if (p.x > hi.x) hi.x = p.x;
    if (p.y > hi.y) hi.y = p.y;
    if (p.z > hi.z) hi.z = p.z;
  }
  float ex = hi.x - lo.x, ey = hi.y - lo.y, ez = hi.z - lo.z;
  int axis = ex >= ey && ex >= ez ? 0 : ey >= ez ? 1 : 2;
  for (int s = 0; s < n; s++) {
    t3_vec3 p = g->b[mem_[s]].local.p;
    key_f[mem_[s]] = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
  }
  qsort(mem_, (size_t)n, sizeof mem_[0], cmp_key);
  float q = 0.34f + 0.32f * hashf((uint32_t)c * 7919u + (uint32_t)g->tick);
  int cut = (int)(n * q);
  if (cut < 1) cut = 1;
  if (cut > n - 1) cut = n - 1;
  for (int s = cut; s < n; s++) key_i[s - cut] = mem_[s];
  int nc = chunk_carve(g, c, key_i, n - cut);
  if (nc < 0) return -1;
  k = &g->ch[c];
  k->frac_tick = g->ch[nc].frac_tick = g->tick;
  g->fractured++;
  /* the two halves part a little along the cut (the break pushes them apart) */
  if (k->dyn) {
    t3_vec3 d = axis == 0 ? v3(1, 0, 0) : axis == 1 ? v3(0, 1, 0) : v3(0, 0, 1);
    t3_quat q0 = cw_body3d_transform(k->body).q;
    d = qrot(q0, d);
    cw_body3d_set_velocity(g->ch[nc].body, vadd(cw_body3d_velocity(g->ch[nc].body), vscale(d, 0.35f)));
    cw_body3d_set_velocity(k->body, vadd(cw_body3d_velocity(k->body), vscale(d, -0.35f)));
  }
  return nc;
}

/* a lattice let go breaks into sections: a tower comes down in pieces, not as one tree. The members are cut by their
 * places (always along the long side of what is left, in the middle, down to SECTION members a piece) and each leaves
 * for its piece once (a shape moves between bodies by being made again, which costs: so no member moves twice) */
static int leaf_of[MAXBLK], sec_n;
static void sec_cut(game *g, int lo, int hi) {
  if (hi - lo <= SECTION) { for (int s = lo; s < hi; s++) leaf_of[mem_[s]] = sec_n; sec_n++; return; }
  t3_vec3 mn = v3(1e9f, 1e9f, 1e9f), mx = v3(-1e9f, -1e9f, -1e9f);
  for (int s = lo; s < hi; s++) {
    t3_vec3 p = g->b[mem_[s]].local.p;
    if (p.x < mn.x) mn.x = p.x;
    if (p.y < mn.y) mn.y = p.y;
    if (p.z < mn.z) mn.z = p.z;
    if (p.x > mx.x) mx.x = p.x;
    if (p.y > mx.y) mx.y = p.y;
    if (p.z > mx.z) mx.z = p.z;
  }
  float ex = mx.x - mn.x, ey = mx.y - mn.y, ez = mx.z - mn.z;
  int axis = ex >= ey && ex >= ez ? 0 : ey >= ez ? 1 : 2;
  for (int s = lo; s < hi; s++) { t3_vec3 p = g->b[mem_[s]].local.p; key_f[mem_[s]] = axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
  qsort(mem_ + lo, (size_t)(hi - lo), sizeof mem_[0], cmp_key);
  int mid = (lo + hi) / 2;
  sec_cut(g, lo, mid);
  sec_cut(g, mid, hi);
}
void chunk_fracture(game *g, int c) { ch_lock(); (void)fracture_i(g, c); ch_unlock(); }

/* an assembly let go: every section of it becomes a free chunk of its own, falling together from rest */
void chunk_release(game *g, int c) {
  chunk *k = &g->ch[c];
  if (!k->alive || k->dyn) return;
  static int ids[CH_MAX];
  int ni = 0;
  for (int cc = c; cc >= 0; cc = g->ch[cc].anext) ids[ni++] = cc;
  float mass = k->mass;
  t3_vec3 com = k->com;
  float mw[M_COUNT] = { 0 }, lo = 1e9f, hi = -1e9f;
  for (int s = 0; s < ni; s++) {
    chunk *q = &g->ch[ids[s]];
    double t0 = g_sim_clock();
    cw_body3d_set_type(q->body, CW_BODY_DYNAMIC);
    g_ch_ms[0] += (g_sim_clock() - t0) * 1000;
    b3BodyId rb = cw_body3d_raw(q->body);
    b3Body_SetSleepThreshold(rb, 0.12f);
    b3Body_SetLinearDamping(rb, 0.02f);
    b3Body_SetAngularDamping(rb, 0.15f);
    b3Body_SetAwake(rb, true);
    for (int i = q->head; i >= 0; i = g->b[i].cnext) { mw[g->b[i].mat] += MATS[g->b[i].mat].density * g->b[i].vol; float y = g->b[i].wxf.p.y; if (y < lo) lo = y; if (y > hi) hi = y; }
    q->rel_tick = g->tick;
    q->dyn = 1, q->leader = ids[s], q->anext = -1, q->stamp = ++g->epoch;
  }
  g->sup_dirty = 1;
  g->released++;
  if (k->placed && mass > 400) { /* the crack of what lets go: its material by weight, how heavy, how tall */
    int mat = M_STONE;
    for (int m = 0; m < M_COUNT; m++) if (m != M_TARGET && mw[m] > mw[mat]) mat = m;
    game_ev(g, EV_CRACK, mat, v3(com.x, lo, com.z), v3(0, 1, 0), hi - lo, 0, (int)mass);
  }
}

/* ── pushes ──────────────────────────────────────────────────────────── */

static int push_list[CH_MAX], npush;

static void impulse_i(game *g, int blk, t3_vec3 J, t3_vec3 at) {
  int c = g->b[blk].chunk;
  chunk *k = &g->ch[c];
  if (!k->dyn) c = k->leader, k = &g->ch[c];
  if (k->dyn) { /* a free chunk takes it where it lands (a push off its middle turns it) */
    b3Vec3 imp = { J.x, J.y, J.z };
    b3Pos pt = { at.x, at.y, at.z };
    b3Body_ApplyLinearImpulse(cw_body3d_raw(k->body), imp, pt, true);
    return;
  }
  if (!k->pq && npush < CH_MAX) { k->pq = 1; push_list[npush++] = c; k->pj = v3(0, 0, 0), k->pt = v3(0, 0, 0), k->pm = 0; }
  float m = vlen(J);
  k->pj = vadd(k->pj, J), k->pt = vadd(k->pt, vscale(at, m)), k->pm += m;
}

void chunk_impulse(game *g, int blk, t3_vec3 J, t3_vec3 at) { ch_lock(); impulse_i(g, blk, J, at); ch_unlock(); }

static void release_lean(game *g, int c);
static void flush_i(game *g) {
  for (int s = 0; s < npush; s++) {
    int c = push_list[s];
    chunk *k = &g->ch[c];
    k->pq = 0;
    if (!k->alive || k->dyn || k->mass <= 0) continue;
    float dv = vlen(k->pj) / k->mass, vmove = opt_get("V_MOVE") ? (float)atof(opt_get("V_MOVE")) : V_MOVE;
    if (dv < vmove) continue; /* too heavy for this blow: it stays */
    static int ids[CH_MAX];
    int ni = 0;
    for (int cc = c; cc >= 0; cc = g->ch[cc].anext) ids[ni++] = cc;
    t3_vec3 v = vscale(k->pj, 1.0f / k->mass);
    float vl = vlen(v);
    if (vl > 18.0f) v = vscale(v, 18.0f / vl); /* a blow never throws a chunk faster than this */
    release_lean(g, c);
    for (int s = 0; s < ni; s++) { /* each section moves at the speed the whole was given */
      b3BodyId rb = cw_body3d_raw(g->ch[ids[s]].body);
      float m = b3Body_GetMass(rb);
      b3Body_ApplyLinearImpulseToCenter(rb, (b3Vec3){ v.x * m, v.y * m, v.z * m }, true);
    }
  }
  npush = 0;
}

/* ── the support graph ───────────────────────────────────────────────── */

typedef struct p2 { float x, z; } p2;
static int cmp_p2(const void *a, const void *b) {
  const p2 *p = a, *q = b;
  if (p->x < q->x) return -1;
  if (p->x > q->x) return 1;
  if (p->z < q->z) return -1;
  if (p->z > q->z) return 1;
  return 0;
}
static inline float cross2(p2 o, p2 a, p2 b) { return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x); }

/* how far inside the hull of pts the point (x, z) lies (negative: outside), by the edges; -1e9 for no hull */
static float inside_hull(p2 *pts, int n, float x, float z) {
  static p2 H[2 * 8192 + 4];
  if (n < 3) return -1e9f;
  qsort(pts, (size_t)n, sizeof *pts, cmp_p2);
  int m = 0;
  for (int i = 0; i < n; i++) {
    while (m >= 2 && cross2(H[m - 2], H[m - 1], pts[i]) <= 0) m--;
    H[m++] = pts[i];
  }
  for (int i = n - 2, lo = m + 1; i >= 0; i--) {
    while (m >= lo && cross2(H[m - 2], H[m - 1], pts[i]) <= 0) m--;
    H[m++] = pts[i];
  }
  m--;
  if (m < 3) return -1e9f;
  float d = 1e9f;
  p2 c = { x, z };
  for (int i = 0; i < m; i++) {
    p2 a = H[i], b = H[(i + 1) % m];
    float len = sqrtf((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
    if (len < 1e-6f) continue;
    float e = cross2(a, b, c) / len;
    if (e < d) d = e;
  }
  return d;
}

float g_hull_dbg[6];
float g_tipdbg[8];
static float g_cent[2], g_edge[2]; /* the middle of the feet of the last chunk_inside, and the foot's furthest point toward where the centre leans */
static inline bool standing(const game *g, int j) {
  const block *b = &g->b[j];
  return b->alive && !b->gen && !g->ch[b->chunk].dyn;
}

/* how far inside its support the centre of chunk c stands now (the feet: every footprint of a standing block it rests on,
 * blocks of its own chunk not counted); -1e9 when nothing holds it */
static float chunk_inside(game *g, int c) {
  static p2 pts[8192];
  chunk *k = &g->ch[c];
  int np = 0;
  g_cent[0] = 1e9f;
  for (int cc = c; cc >= 0; cc = g->ch[cc].anext)
    for (int i = g->ch[cc].head; i >= 0; i = g->b[i].cnext) {
      for (int l = g->sl_off[i]; l < g->sl_off[i + 1]; l++) {
        int j = g->sl_to[l];
        if (j >= 0 && (!standing(g, j) || g->ch[g->b[j].chunk].leader == c)) continue;
        if (np + 4 > 8192) continue;
        const float *r = &g->sl_rect[4 * l];
        pts[np++] = (p2){ r[0], r[1] }, pts[np++] = (p2){ r[2], r[1] }, pts[np++] = (p2){ r[2], r[3] }, pts[np++] = (p2){ r[0], r[3] };
      }
    }
  if (np == 0) return -1e9f;
  { float sx = 0, sz = 0; for (int i = 0; i < np; i++) sx += pts[i].x, sz += pts[i].z; g_cent[0] = sx / (float)np, g_cent[1] = sz / (float)np;
    float dx = k->com.x - g_cent[0], dz = k->com.z - g_cent[1], dl = sqrtf(dx * dx + dz * dz), best = -1e9f;
    g_edge[0] = g_cent[0], g_edge[1] = g_cent[1];
    if (dl > 1e-4f) for (int i = 0; i < np; i++) { float pr = (pts[i].x - g_cent[0]) * dx / dl + (pts[i].z - g_cent[1]) * dz / dl; if (pr > best) best = pr, g_edge[0] = pts[i].x, g_edge[1] = pts[i].z; } }
  { float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f; for (int i = 0; i < np; i++) { if (pts[i].x < x0) x0 = pts[i].x; if (pts[i].x > x1) x1 = pts[i].x; if (pts[i].z < z0) z0 = pts[i].z; if (pts[i].z > z1) z1 = pts[i].z; } g_hull_dbg[0] = (float)np, g_hull_dbg[1] = x0, g_hull_dbg[2] = x1, g_hull_dbg[3] = z0, g_hull_dbg[4] = z1; }
  return inside_hull(pts, np, k->com.x, k->com.z);
}

float chunk_inside_now(game *g, int c) { return chunk_inside(g, c); }
/* a chunk stands while its centre keeps this fraction of the margin it had as built (a frame with a leg gone is not what it was) */
static float stand_frac(void) { return opt_get("STAND_FRAC") ? (float)atof(opt_get("STAND_FRAC")) : 0.7f; }

/* one judgement of every standing chunk whose support changed since it was last judged: those out of balance go */
/* an assembly let go, by a lost footing or by a blow: it is an ordinary rigid body from now on; one that was out of balance (its centre not over what is left of its foot,
 * by the margin the structure needs) also goes over: a turn about the edge of the foot's remains and a sideways pull growing with height, for a second and a half, which
 * is what a frame with its foot half gone does and the solver's stack of resting sections would not (a stack standing on a thinner foot is in balance on paper) */
static void release_lean(game *g, int c) {
  static int rel_ids[CH_MAX];
  chunk *k = &g->ch[c];
  float d = chunk_inside(g, c);
  float req = k->d0 >= 0 ? (k->frac > 0 ? k->frac : stand_frac()) * k->d0 : k->d0 - 0.03f;
  float tx = k->com.x - g_cent[0], tz = k->com.z - g_cent[1], ex = g_edge[0], ez = g_edge[1];
  bool unstable = d < req && g_cent[0] < 1e8f;
  int ni = 0;
  float lo = 1e9f, hi = -1e9f;
  for (int cc = c; cc >= 0; cc = g->ch[cc].anext) { rel_ids[ni++] = cc; for (int i = g->ch[cc].head; i >= 0; i = g->b[i].cnext) { float y = g->b[i].wxf.p.y; if (y < lo) lo = y; if (y > hi) hi = y; } }
  chunk_release(g, c);
  float tl = sqrtf(tx * tx + tz * tz);
  if (!unstable || tl < 0.02f) return;
  for (int q = 0; q < ni; q++) {
    cw_body3d bd = g->ch[rel_ids[q]].body;
    t3_vec3 om = v3(tz / tl * 0.32f, 0, -tx / tl * 0.32f), r = vsub(wcom(bd), v3(ex, 0, ez)), vv = cw_body3d_velocity(bd), av = cw_body3d_angular_velocity(bd);
    float f = fmaxf(0.0f, fminf(1.0f, (wcom(bd).y - lo) / fmaxf(0.5f, hi - lo)));
    g->ch[rel_ids[q]].lean[0] = tx / tl * 5.0f * f, g->ch[rel_ids[q]].lean[1] = tz / tl * 5.0f * f, g->ch[rel_ids[q]].lean_until = g->tick + 90;
    cw_body3d_set_velocity(bd, vadd(vv, vcross(om, r))), cw_body3d_set_angular_velocity(bd, vadd(av, om));
  }
}

static void support_pass(game *g) {
  static int out[CH_MAX];
  int nout = 0;
  int E = g->epoch;
  for (int c = 0; c < g->nch; c++) {
    chunk *k = &g->ch[c];
    if (!k->alive || k->dyn || !k->placed || k->dirty || k->d0 < -1e7f || k->leader != c) continue;
    bool need = k->stamp > k->seen;
    for (int cc = c; cc >= 0 && !need; cc = g->ch[cc].anext)
      for (int i = g->ch[cc].head; i >= 0 && !need; i = g->b[i].cnext)
        for (int l = g->sl_off[i]; l < g->sl_off[i + 1]; l++) {
          int j = g->sl_to[l];
          if (j < 0) continue;
          const block *b = &g->b[j];
          if (g->ch[g->ch[b->chunk].leader].stamp > k->seen || !b->alive) { need = true; break; }
        }
    k->seen = E;
    if (!need) continue;
    float d = chunk_inside(g, c);
    float req = k->d0 >= 0 ? (k->frac > 0 ? k->frac : stand_frac()) * k->d0 : k->d0 - 0.03f;
    if (d < req) { out[nout++] = c; }
  }
  for (int s = 0; s < nout; s++) release_lean(g, out[s]);
}

/* what lies in a heap crumbles under its own weight: a let-go chunk of more than 6 blocks that has come nearly to rest breaks again
 * (two a tick at most, looking at a fixed number of chunks a tick round the list), so nothing but what still stands lies in lumps */
/* a lump that crumbles gives way: its pieces spill (a stack of whole bricks would stand for ever), a push sideways and a turn, by a hash */
static void spill(game *g, int c, uint32_t h) {
  chunk *k = &g->ch[c];
  if (!k->alive || !k->dyn) return;
  float a = (float)(h & 1023) / 1024.0f * 6.2831853f, w = 1.4f + 2.0f * hashf(h);
  t3_vec3 v = cw_body3d_velocity(k->body), av = cw_body3d_angular_velocity(k->body);
  cw_body3d_set_velocity(k->body, v3(v.x + cw_cos(a) * w, v.y, v.z + cw_sin(a) * w));
  cw_body3d_set_angular_velocity(k->body, v3(av.x + (hashf(h >> 3) - 0.5f) * 4, av.y, av.z + (hashf(h >> 7) - 0.5f) * 4));
  b3Body_SetAwake(cw_body3d_raw(k->body), true);
}
static void crumble_one(game *g, int c) {
  int nc = fracture_i(g, c);
  uint32_t h = hash32((uint32_t)c * 2654435761u + (uint32_t)g->tick);
  spill(g, c, h);
  if (nc >= 0) spill(g, nc, hash32(h ^ 0x9e3779b9u));
}

static void crumble(game *g) {
  if (opt_on("NO_CRUMBLE")) return;
  int nf = 0;
  for (int look = 0; look < 300 && nf < 3; look++) {
    int c = g->crumble_i++;
    if (g->crumble_i >= g->nch) g->crumble_i = 0;
    chunk *k = &g->ch[c];
    if ((opt_on("OLD_SLEEP") || (g->awake_now < 120 && k->alive && k->dyn && wcom(k->body).y < 0.7f)) && k->alive && k->dyn && k->n <= 6 && g->tick - k->rel_tick > 90 && g->tick - k->frac_tick > 30 && cw_body3d_awake(k->body)) { /* rubble nearly at rest goes to sleep (a heap that hums keeps its whole island stepping) */
      t3_vec3 v = cw_body3d_velocity(k->body), w = cw_body3d_angular_velocity(k->body);
      if (v.x * v.x + v.y * v.y + v.z * v.z < 0.4f * 0.4f && w.x * w.x + w.y * w.y + w.z * w.z < 0.8f * 0.8f) {
        cw_body3d_set_velocity(k->body, v3(0, 0, 0)), cw_body3d_set_angular_velocity(k->body, v3(0, 0, 0));
        b3Body_SetAwake(cw_body3d_raw(k->body), false);
      }
    }
    if (!k->alive || !k->dyn || !k->placed || k->n <= 6 || g->tick - k->rel_tick < 50 || g->tick - k->frac_tick < 10) continue;
    { /* a lump gives way only once it lies (nearly still: a tower going over is not crumbling), however long it takes */
      t3_vec3 v = cw_body3d_velocity(k->body);
      if (v.x * v.x + v.y * v.y + v.z * v.z < 1.0f && g->tick - k->rel_tick > 40) crumble_one(g, c), nf++;
    }
  }
}

void chunks_flush(game *g) { ch_lock(); flush_i(g); ch_unlock(); }

static void step_i(game *g) {
  if (opt_on("NO_SUPPORT")) return;
  for (int c = 0; c < g->nch; c++) { /* the lean of what lost its footing */
    chunk *k = &g->ch[c];
    if (!k->alive || !k->dyn || k->lean_until <= g->tick) continue;
    float m = cw_body3d_mass(k->body);
    b3Body_ApplyLinearImpulseToCenter(cw_body3d_raw(k->body), (b3Vec3){ k->lean[0] * m * TICK, 0, k->lean[1] * m * TICK }, true);
  } /* (the look sheets of a section: nothing judged, nothing falls) */
  crumble(g);
  if (!g->sup_dirty) return;
  g->sup_dirty = 0;
  double t0 = g_sim_clock();
  for (int c = 0; c < g->nch; c++) if (g->ch[c].alive && g->ch[c].dirty) resolve(g, c);
  double t1 = g_sim_clock();
  support_pass(g);
  g_ch_ms[2] += (t1 - t0) * 1000, g_ch_ms[3] += (g_sim_clock() - t1) * 1000;
  for (int c = 0; c < g->nch; c++) if (g->ch[c].alive && g->ch[c].dirty) resolve(g, c);
  /* a pass that let chunks go changed what the ones above stand on: judge them next tick */
  for (int c = 0; c < g->nch; c++) if (g->ch[c].alive && !g->ch[c].dyn && g->ch[c].placed && g->ch[c].leader == c && g->ch[c].stamp > g->ch[c].seen) { g->sup_dirty = 1; break; }
}

/* the invariant: every standing assembly has a load path to the ground: a link to the ground from one of its live blocks, or to a live block of a standing assembly that has one */
int chunks_floating(game *g) {
  static uint8_t ok[CH_MAX];
  ch_lock();
  int n = 0;
  for (int c = 0; c < g->nch; c++) ok[c] = 0;
  for (bool grew = true; grew;) {
    grew = false;
    for (int c = 0; c < g->nch; c++) {
      chunk *k = &g->ch[c];
      if (!k->alive || k->dyn || !k->placed || k->leader != c || ok[c]) continue;
      bool found = false;
      for (int cc = c; cc >= 0 && !found; cc = g->ch[cc].anext)
        for (int i = g->ch[cc].head; i >= 0 && !found; i = g->b[i].cnext)
          for (int l = g->sl_off[i]; l < g->sl_off[i + 1]; l++) {
            int j = g->sl_to[l];
            if (j < 0) { found = true; break; }
            const block *b = &g->b[j];
            if (b->alive && !b->gen && !g->ch[b->chunk].dyn && g->ch[g->ch[b->chunk].leader].leader != c && ok[g->ch[b->chunk].leader]) { found = true; break; }
          }
      if (found) ok[c] = 1, grew = true;
    }
  }
  for (int c = 0; c < g->nch; c++) { chunk *k = &g->ch[c]; if (k->alive && !k->dyn && k->placed && k->leader == c && !ok[c]) n++; }
  ch_unlock();
  return n;
}

/* the same for what fell loose: a free chunk asleep in the air (nothing touching it, its middle higher than 0.4 m) hangs there for ever: a bug */
int chunks_floating_dyn(game *g) {
  int n = 0;
  for (int c = 0; c < g->nch; c++) {
    chunk *k = &g->ch[c];
    if (!k->alive || !k->dyn || cw_body3d_awake(k->body)) continue;
    if (wcom(k->body).y < 0.4f) continue;
    cw_contact3d ct[2];
    if (cw_body3d_contacts(k->body, ct, 2) == 0) n++;
  }
  return n;
}

float g_unsup_dbg[8][8];
int chunks_unsupported(game *g) {
  int n = 0;
  ch_lock();
  for (int c = 0; c < g->nch; c++) {
    chunk *k = &g->ch[c];
    if (!k->alive || k->dyn || !k->placed || k->leader != c || k->d0 < -1e7f) continue;
    float d = chunk_inside(g, c), req = k->d0 >= 0 ? (k->frac > 0 ? k->frac : stand_frac()) * k->d0 : k->d0 - 0.03f;
    if (d < req) { if (n < 8) { float *o = g_unsup_dbg[n]; int cnt = 0; for (int cc = c; cc >= 0; cc = g->ch[cc].anext) cnt += g->ch[cc].n; o[0] = (float)cnt, o[1] = d, o[2] = req, o[3] = k->dirty, o[4] = (float)k->stamp, o[5] = (float)k->seen, o[6] = k->d0, o[7] = (float)c; } n++; }
  }
  ch_unlock();
  return n;
}

int chunks_largest(const game *g, int *over6, int *standing_n) {
  int big = 0, o6 = 0, st = 0;
  for (int c = 0; c < g->nch; c++) {
    const chunk *k = &g->ch[c];
    if (!k->alive) continue;
    if (!k->dyn) { st += k->leader == c; continue; }
    if (k->n > big) big = k->n;
    if (k->n > 6) o6++;
  }
  if (over6) *over6 = o6;
  if (standing_n) *standing_n = st;
  return big;
}

/* ── building: the placed blocks into static chunks, the links and the bonds ───────────────────────── */

typedef struct link_t { int up, lo; float r[4]; } link_t;
typedef struct edge_t { int a, b; float r[4]; } edge_t;
static int uf_[MAXBLK];
static int uf_find(int a) { while (uf_[a] != a) uf_[a] = uf_[uf_[a]], a = uf_[a]; return a; }
static void uf_union(int a, int b) { a = uf_find(a), b = uf_find(b); if (a != b) uf_[b] = a; }

static void xz_extent(const block *b, cw_xform3 x, float *ex, float *ez) {
  t3_vec3 ax = qrot(x.q, v3(1, 0, 0)), az = qrot(x.q, v3(0, 0, 1));
  *ex = b->half.x * fabsf(ax.x) + b->half.z * fabsf(az.x);
  *ez = b->half.x * fabsf(ax.z) + b->half.z * fabsf(az.z);
}

static bool placed_block(const block *k) { return k->alive && k->tag && k->target < 0 && !k->keg; }

static void build_i(game *g);
void chunks_build(game *g) { ch_lock(); build_i(g); ch_unlock(); }
void chunks_step(game *g);
static void build_i(game *g) {
  float cell = opt_get("CELL") ? (float)atof(opt_get("CELL")) : (g->L->kind == K_LANDMARK ? 2.0f : 1.0f);
  int nb = g->nb;
  static cw_xform3 X[MAXBLK];
  static uint8_t welded[MAXBLK];
  static int cx[MAXBLK][3];
  memset(welded, 0, sizeof welded);
  for (int k = 0; k < g->nweld; k++) welded[g->weld[k].a] = welded[g->weld[k].b] = 1;
  int np = 0;
  for (int i = 0; i < nb; i++) {
    uf_[i] = i;
    if (!g->b[i].alive) continue;
    X[i] = game_block_xform(g, i);
    cx[i][0] = (int)floorf(X[i].p.x / cell), cx[i][1] = (int)floorf(X[i].p.y / cell), cx[i][2] = (int)floorf(X[i].p.z / cell);
    np += placed_block(&g->b[i]);
  }
  link_t *LN = malloc(sizeof(link_t) * (size_t)(np * 8 + 16));
  edge_t *ED = malloc(sizeof(edge_t) * (size_t)(np * 14 + 16));
  int nln = 0, ned = 0;
  cw_phys_filter f = cw_phys_filter_layers(LAYER_BLOCK_BIT);
  static cw_shape3d hit[256];
  for (int i = 0; i < nb; i++) {
    const block *a = &g->b[i];
    if (!placed_block(a)) continue;
    float ax, az;
    xz_extent(a, X[i], &ax, &az);
    float a0x = X[i].p.x - ax, a1x = X[i].p.x + ax, a0z = X[i].p.z - az, a1z = X[i].p.z + az;
    if (X[i].p.y - a->half.y <= 0.03f && nln < np * 8) { /* on the ground */
      link_t *l = &LN[nln++];
      l->up = i, l->lo = -1, l->r[0] = a0x, l->r[1] = a0z, l->r[2] = a1x, l->r[3] = a1z;
    }
    cw_qshape3d q = cw_qshape3d_box(X[i].p, v3(a->half.x + TOUCH, a->half.y + TOUCH * 0.5f, a->half.z + TOUCH), X[i].q);
    int nh = cw_world3d_overlap(g->w, &q, &f, hit, 256);
    for (int h = 0; h < nh; h++) {
      int j = game_block_of_shape(hit[h]);
      if (j <= i || j >= nb || !placed_block(&g->b[j])) continue;
      const block *b = &g->b[j];
      float bx, bz;
      xz_extent(b, X[j], &bx, &bz);
      float x0 = fmaxf(a0x, X[j].p.x - bx), x1 = fminf(a1x, X[j].p.x + bx), z0 = fmaxf(a0z, X[j].p.z - bz), z1 = fminf(a1z, X[j].p.z + bz);
      if (ned < np * 14) { edge_t *e = &ED[ned++]; e->a = i, e->b = j, e->r[0] = x0, e->r[1] = z0, e->r[2] = x1, e->r[3] = z1; }
      float topi = X[i].p.y + a->half.y, boti = X[i].p.y - a->half.y, topj = X[j].p.y + b->half.y, botj = X[j].p.y - b->half.y;
      if (x1 - x0 > 0.02f && z1 - z0 > 0.02f && nln < np * 8) {
        if (fabsf(topi - botj) < 0.025f) { link_t *l = &LN[nln++]; l->up = j, l->lo = i, l->r[0] = x0, l->r[1] = z0, l->r[2] = x1, l->r[3] = z1; }
        else if (fabsf(topj - boti) < 0.025f) { link_t *l = &LN[nln++]; l->up = i, l->lo = j, l->r[0] = x0, l->r[1] = z0, l->r[2] = x1, l->r[3] = z1; }
      }
    }
  }
  /* bonds: welds, and mortar (blocks that touch inside one cell of space) */
  for (int k = 0; k < g->nweld; k++) uf_union(g->weld[k].a, g->weld[k].b);
  static uint8_t isbond_[1];
  (void)isbond_;
  int nbond = 0;
  int *bond_a = malloc(sizeof(int) * (size_t)(ned + g->nweld + 1)), *bond_b = malloc(sizeof(int) * (size_t)(ned + g->nweld + 1));
  for (int k = 0; k < g->nweld; k++) bond_a[nbond] = g->weld[k].a, bond_b[nbond++] = g->weld[k].b;
  for (int e = 0; e < ned; e++) {
    int i = ED[e].a, j = ED[e].b;
    if (welded[i] || welded[j]) continue;
    if (cx[i][0] != cx[j][0] || cx[i][1] != cx[j][1] || cx[i][2] != cx[j][2]) continue;
    uf_union(i, j);
    bond_a[nbond] = i, bond_b[nbond++] = j;
  }
  /* lateral pins: a chunk nothing rests under is held by what it touches */
  static int owner[MAXBLK];
  static uint8_t has_sup[MAXBLK];
  memset(has_sup, 0, sizeof has_sup);
  for (int i = 0; i < nb; i++) owner[i] = placed_block(&g->b[i]) ? uf_find(i) : -1;
  for (int l = 0; l < nln; l++) if (LN[l].lo < 0 || owner[LN[l].lo] != owner[LN[l].up]) has_sup[owner[LN[l].up]] = 1;
  for (int e = 0; e < ned; e++) {
    int i = ED[e].a, j = ED[e].b;
    if (owner[i] == owner[j]) continue;
    for (int s = 0; s < 2; s++) {
      int u = s ? j : i, o = s ? i : j;
      if (has_sup[owner[u]] || nln >= np * 8) continue;
      /* (only a chunk that has nothing under it: pinned to every neighbour it touches) */
      link_t *l = &LN[nln++];
      l->up = u, l->lo = o, l->r[0] = ED[e].r[0] - 0.04f, l->r[1] = ED[e].r[1] - 0.04f, l->r[2] = ED[e].r[2] + 0.04f, l->r[3] = ED[e].r[3] + 0.04f;
    }
  }
  /* the CSRs */
  g->sl_off = calloc((size_t)nb + 1, sizeof(int)), g->bnd_off = calloc((size_t)nb + 1, sizeof(int));
  for (int l = 0; l < nln; l++) g->sl_off[LN[l].up + 1]++;
  for (int k = 0; k < nbond; k++) g->bnd_off[bond_a[k] + 1]++, g->bnd_off[bond_b[k] + 1]++;
  for (int i = 0; i < nb; i++) g->sl_off[i + 1] += g->sl_off[i], g->bnd_off[i + 1] += g->bnd_off[i];
  g->nsl = nln;
  g->sl_to = malloc(sizeof(int) * (size_t)(nln + 1)), g->sl_rect = malloc(sizeof(float) * 4 * (size_t)(nln + 1)), g->bnd_to = malloc(sizeof(int) * (size_t)(g->bnd_off[nb] + 1));
  int *fill = calloc((size_t)nb + 1, sizeof(int)), *bfill = calloc((size_t)nb + 1, sizeof(int));
  for (int l = 0; l < nln; l++) {
    int at = g->sl_off[LN[l].up] + fill[LN[l].up]++;
    g->sl_to[at] = LN[l].lo;
    memcpy(&g->sl_rect[4 * at], LN[l].r, sizeof LN[l].r);
  }
  for (int k = 0; k < nbond; k++) {
    g->bnd_to[g->bnd_off[bond_a[k]] + bfill[bond_a[k]]++] = bond_b[k];
    g->bnd_to[g->bnd_off[bond_b[k]] + bfill[bond_b[k]]++] = bond_a[k];
  }
  free(fill), free(bfill), free(LN), free(ED), free(bond_a), free(bond_b);
  /* the chunks: one static body each; a bonded group of more than SECTION members is cut at build time along its long side into
   * sections of at most that many, one static body each, held as one assembly (so a let-go lattice is already in its pieces) */
  static int target_[MAXBLK], cnt_[MAXBLK];
  for (int i = 0; i < nb; i++) target_[i] = -1, cnt_[i] = 0;
  for (int i = 0; i < nb; i++) if (placed_block(&g->b[i])) { cnt_[uf_find(i)]++; g->b[i].local = (cw_xform3){ X[i].p, X[i].q }; }
  static int off_[MAXBLK + 1], byroot_[MAXBLK];
  { int a = 0; for (int r = 0; r < nb; r++) off_[r] = a, a += cnt_[r]; off_[nb] = a; static int fillp[MAXBLK]; memset(fillp, 0, sizeof fillp); for (int i = 0; i < nb; i++) if (placed_block(&g->b[i])) { int r = uf_find(i); byroot_[off_[r] + fillp[r]++] = i; } }
  for (int r = 0; r < nb; r++) {
    if (!cnt_[r]) continue;
    int n = 0;
    for (int q = 0; q < cnt_[r]; q++) mem_[n++] = byroot_[off_[r] + q];
    sec_n = 0;
    if (n > SECTION) sec_cut(g, 0, n); else { for (int s = 0; s < n; s++) leaf_of[mem_[s]] = 0; sec_n = 1; }
    int head = -1, tail = -1;
    for (int leaf = 0; leaf < sec_n; leaf++) {
      int f = -1;
      for (int s = 0; s < n; s++) if (leaf_of[mem_[s]] == leaf) { f = mem_[s]; break; }
      if (f < 0) continue;
      cw_body3d_def bd = cw_body3d_default_def();
      bd.type = CW_BODY_STATIC;
      bd.position = X[f].p;
      bd.interpolate = true;
      int c = chunk_new(g, cw_body3d_create(g->w, &bd), false, true);
      for (int s = 0; s < n; s++) if (leaf_of[mem_[s]] == leaf) target_[mem_[s]] = c;
      if (head < 0) head = tail = c; else g->ch[tail].anext = c, g->ch[c].leader = head, tail = c;
    }
  }
  for (int i = 0; i < nb; i++) {
    if (!placed_block(&g->b[i])) continue;
    int c = target_[i];
    block *b = &g->b[i];
    cw_body3d old = g->ch[b->chunk].body;
    int oc = b->chunk;
    cw_xform3 bp = cw_body3d_transform(g->ch[c].body);
    b->local = (cw_xform3){ vsub(X[i].p, bp.p), X[i].q };
    b->wxf = X[i];
    cw_shape3d sh = block_shape(g, i, g->ch[c].body, b->local);
    unlink_member(g, i);
    cw_body3d_destroy(old); /* its own body and the shape on it */
    g->ch[oc].alive = 0, g->cfree[g->ncfree++] = oc;
    b->shape = sh;
    chunk_add_member(g, c, i);
  }
  /* what each stands on as built: where its centre is inside its feet, and what it weighs */
  for (int c = 0; c < g->nch; c++) {
    chunk *k = &g->ch[c];
    if (!k->alive || k->dyn || !k->placed) continue;
    k->frac_tick = -100;
    if (k->leader != c) continue;
    chunk_measure(g, c);
    float d = chunk_inside(g, c);
    k->d0 = d < -1e8f ? -1e9f : d; /* nothing under it as built: never judged */
    { /* a slender frame (tall over its footing) keeps more of its margin: it is not what it was with a part of its foot gone */
      float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
      for (int cc = c; cc >= 0; cc = g->ch[cc].anext)
        for (int i = g->ch[cc].head; i >= 0; i = g->b[i].cnext) {
          float yb = g->b[i].wxf.p.y - g->b[i].half.y, yt = g->b[i].wxf.p.y + g->b[i].half.y;
          if (yb < y0) y0 = yb;
          if (yt > y1) y1 = yt;
          for (int l = g->sl_off[i]; l < g->sl_off[i + 1]; l++) if (g->sl_to[l] < 0) { const float *r = &g->sl_rect[4 * l]; if (r[0] < x0) x0 = r[0]; if (r[2] > x1) x1 = r[2]; if (r[1] < z0) z0 = r[1]; if (r[3] > z1) z1 = r[3]; }
        }
      float w = fmaxf(0.5f, fminf(x1 - x0, z1 - z0)), sl = x1 > x0 ? (y1 - y0) / w : 0;
      k->frac = sl > 3 ? fminf(0.93f, stand_frac() + 0.05f * (sl - 3)) : 0;
    }
    k->seen = g->epoch;
  }
  g->sup_dirty = 0;
}

void chunks_step(game *g) { ch_lock(); step_i(g); ch_unlock(); }
