/* skate parks: five original parks built from the kit (park.c). Each one lists its start points, its lines (what the
 * park check skates and the bots pick), its gaps and its five tokens.
 *
 *   Sunnyside     a concrete park: a deep bowl with a little vert, two long quarter pipes facing across, a funbox,
 *                 ledges, a flat bar, a kicker over a picnic table.
 *   Civic Plaza   a street plaza: a raised terrace with stairs and handrails, banks up to it, granite ledges, a fountain
 *                 with a grindable rim, manual pads.
 *   Big Sky       the mega ramp: a 14 m roll-in, a kicker over a 10 m gap onto a landing slope, a giant quarter pipe,
 *                 with a small practice area beside it.
 *   Rooftops      four roofs stepping down over the street: gaps to ollie, a kicker, parapets and rails to grind, AC
 *                 units, a bank up from the street.
 *   Timber Yard   wooden ramps: a vert halfpipe, a mini ramp, a spine, a pyramid with rails.
 *
 * Variants for the park check's controls (PARKV_*): a piece lifted off what it stands on, a wall across a rail's
 * approach, a kicker too short for its gap. Each must fail the check. */
#include "game.h"

const char *const PARK_NAMES[PARK_COUNT] = { "Sunnyside", "Civic Plaza", "Big Sky", "Rooftops", "Timber Yard" };

/* colours (sRGB) */
static const float C_CONC[3] = { 0.80f, 0.78f, 0.74f }, C_CONC2[3] = { 0.72f, 0.72f, 0.70f }, C_DECK[3] = { 0.86f, 0.84f, 0.80f };
static const float C_TEAL[3] = { 0.10f, 0.62f, 0.68f }, C_ORANGE[3] = { 1.0f, 0.52f, 0.14f }, C_YELLOW[3] = { 1.0f, 0.82f, 0.18f };
static const float C_MAGENTA[3] = { 0.90f, 0.24f, 0.52f }, C_BLUE[3] = { 0.22f, 0.45f, 0.92f }, C_GREEN[3] = { 0.36f, 0.74f, 0.30f };
static const float C_STEEL[3] = { 0.80f, 0.82f, 0.86f }, C_RED[3] = { 0.86f, 0.22f, 0.18f }, C_WOOD[3] = { 0.92f, 0.74f, 0.50f };
static const float C_GRASS[3] = { 0.42f, 0.72f, 0.28f }, C_TILE[3] = { 0.84f, 0.76f, 0.66f }, C_GRANITE[3] = { 0.62f, 0.60f, 0.62f };
static const float C_ASPH[3] = { 0.40f, 0.40f, 0.42f }, C_BRICK1[3] = { 0.78f, 0.42f, 0.30f }, C_BRICK2[3] = { 0.86f, 0.70f, 0.50f };
static const float C_BRICK3[3] = { 0.52f, 0.62f, 0.78f }, C_BRICK4[3] = { 0.90f, 0.86f, 0.74f }, C_ROOF[3] = { 0.58f, 0.56f, 0.55f };
static const float C_WATER[3] = { 0.30f, 0.62f, 0.86f }, C_TRUNK[3] = { 0.45f, 0.32f, 0.22f }, C_LEAF[3] = { 0.30f, 0.62f, 0.22f };

static float utop_of(const profile *p) { return p->u[p->n - 1]; }
static float ulip(float r, float h) {
  profile p;
  prof_kicker(&p, r, h, 10);
  return utop_of(&p);
}
static ramp_opts ropts(int mat_face, const float face[3], const float side[3], float deck, bool coping) {
  ramp_opts o;
  memset(&o, 0, sizeof o);
  o.deck = deck, o.y0 = 0, o.mat_face = mat_face, o.mat_side = MAT_PAINT, o.coping = coping, o.run = -1;
  memcpy(o.face, face, sizeof o.face), memcpy(o.side, side, sizeof o.side), memcpy(o.deck_col, C_DECK, sizeof o.deck_col);
  return o;
}

static int spawn_add(park *k, v3 p, float yaw, const char *name) {
  if (k->nspawn >= PARK_MAXSPAWN) return k->nspawn - 1;
  pspawn *s = &k->spawn[k->nspawn];
  s->p = p, s->yaw = yaw;
  snprintf(s->name, sizeof s->name, "%s", name);
  return k->nspawn++;
}
static int gap_add(park *k, v3 flo, v3 fhi, v3 tlo, v3 thi, const char *name, int pts) {
  if (k->ngap >= PARK_MAXGAP) return -1;
  pgap *g = &k->gap[k->ngap];
  g->from_lo = flo, g->from_hi = fhi, g->to_lo = tlo, g->to_hi = thi, g->points = pts;
  snprintf(g->name, sizeof g->name, "%s", name);
  return k->ngap++;
}
/* a line: its start becomes a start point (the reset button reaches it) */
static void line_add(park *k, v3 start, float yaw, float speed, int kind, v3 at, int run, int gap, float pop_dist, const char *name) {
  if (k->nline >= PARK_MAXLINE) return;
  pline *l = &k->line[k->nline++];
  l->start = start, l->yaw = yaw, l->speed = speed, l->kind = kind, l->at = at, l->edge_run = run, l->gap = gap, l->pop_dist = pop_dist;
  snprintf(l->name, sizeof l->name, "%s", name);
  l->spawn = spawn_add(k, start, yaw, name);
}
static int find_line(const park *k, const char *name) {
  for (int i = 0; i < k->nline; i++)
    if (!strcmp(k->line[i].name, name)) return i;
  return -1;
}
static void token(park *k, v3 p) {
  if (k->ntoken < PARK_MAXTOKEN) k->token[k->ntoken++] = p;
}

/* a granite ledge: a box with grindable lips on its long sides */
static int ledge(kit *b, float cx, float cz, float yaw, float len, float depth, float h, const float col[3]) {
  kit_piece(b);
  kit_box(b, cx, cz, yaw, depth, len, 0, h, MAT_CONCRETE, MAT_CONCRETE, col, col);
  float s, c;
  cw_sincos(yaw, &s, &c);
  v3 F = V(-s, 0, -c), R = V(c, 0, -s);
  int run = kit_run(b);
  for (int sd = -1; sd <= 1; sd += 2) {
    v3 off = vmul(R, (float)sd * (depth * 0.5f - 0.03f));
    v3 a = vadd(V(cx, h + 0.01f, cz), vadd(off, vmul(F, -len * 0.5f + 0.05f))), e = vadd(V(cx, h + 0.01f, cz), vadd(off, vmul(F, len * 0.5f - 0.05f)));
    kit_edge(b, a, e, V(0, 1, 0), EDGE_LEDGE, sd < 0 ? run : kit_run(b));
  }
  return run;
}
static void quarter(kit *b, float x, float z, float yaw, float w, float r, float h, float deck, int mat, const float face[3], const float side[3]) {
  kit_piece(b);
  profile p;
  prof_quarter(&p, r, h, 14);
  ramp_opts o = ropts(mat, face, side, deck, true);
  kit_ramp(b, x, z, yaw, w, &p, &o);
}
/* a bank whose top edge is at (x, z): its foot out in front of it */
static void bank(kit *b, float x, float z, float yaw, float w, float len, float h, int mat, const float face[3], const float side[3], float deck, bool back) {
  kit_piece(b);
  profile p;
  prof_bank(&p, len, h, 1.6f);
  ramp_opts o = ropts(mat, face, side, deck, false);
  o.no_back = !back;
  float ut = utop_of(&p), s, c;
  cw_sincos(yaw, &s, &c);
  kit_ramp(b, x + s * ut, z + c * ut, yaw, w, &p, &o);
}
static float kicker(kit *b, float x, float z, float yaw, float w, float r, float h, int mat, const float face[3], const float side[3]) {
  kit_piece(b);
  profile p;
  prof_kicker(&p, r, h, 10);
  ramp_opts o = ropts(mat, face, side, 0, false);
  kit_ramp(b, x, z, yaw, w, &p, &o);
  return utop_of(&p);
}
static void tree(kit *b, float x, float z, float s) {
  kit_piece(b);
  /* a trunk (a box, it blocks) and a crown of leaves (drawn only) */
  kit_box(b, x, z, 0.4f, 0.4f * s, 0.4f * s, 0, 2.6f * s, MAT_WOOD, MAT_WOOD, C_TRUNK, C_TRUNK);
  for (int i = 0; i < 3; i++) {
    float y = (2.6f + 0.9f * (float)i) * s, r = (1.8f - 0.45f * (float)i) * s;
    v3 c = V(x, y, z);
    for (int k = 0; k < 8; k++) {
      float a0 = (float)k / 8.0f * 2 * PI_F + (float)i * 0.4f, a1 = (float)(k + 1) / 8.0f * 2 * PI_F + (float)i * 0.4f, s0, c0, s1, c1;
      cw_sincos(a0, &s0, &c0);
      cw_sincos(a1, &s1, &c1);
      v3 p0 = V(x + c0 * r, y, z + s0 * r), p1 = V(x + c1 * r, y, z + s1 * r), top = V(x, y + 1.4f * s, z), bot = V(x, y - 0.5f * s, z);
      v3 n0 = vnorm(vsub(p0, c)), n1 = vnorm(vsub(p1, c));
      kit_tri(b, p0, p1, top, n0, n1, V(0, 1, 0), SURF_NONE, MAT_LEAF, C_LEAF);
      kit_tri(b, p0, bot, p1, n0, V(0, -1, 0), n1, SURF_NONE, MAT_LEAF, C_LEAF);
    }
  }
}
/* a building (scenery past the fence, or a roof to ride): walls block, the top rides */
static void building(kit *b, float x0, float z0, float x1, float z1, float h, const float wall[3]) {
  kit_piece(b);
  kit_box(b, (x0 + x1) * 0.5f, (z0 + z1) * 0.5f, 0, x1 - x0, z1 - z0, 0, h, MAT_ROOF, MAT_BRICK, C_ROOF, wall);
}

/* ── Sunnyside ─────────────────────────────────────────────────────────── */
static void build_sunny(kit *b, int variant) {
  park *k = b->k;
  k->theme = 0;
  snprintf(k->blurb, sizeof k->blurb, "a sunny concrete park: bowl, quarters, funbox");
  /* the bowl, sunk in the deck */
  const float bx = -18, bz = -14, bhx = 9, bhz = 6.5f, brc = 4, bd = 2.7f, br = 2.3f, box_ = 11.5f, boz = 9;
  float holes[4] = { bx - box_, bz - boz, bx + box_, bz + boz };
  kit_piece(b);
  kit_ground(b, -42, -42, 42, 42, holes, 1, MAT_CONCRETE, C_CONC);
  kit_piece(b);
  kit_bowl(b, bx, bz, bhx, bhz, brc, bd, br, MAT_CONCRETE, C_BLUE, C_STEEL);
  kit_bowl_deck(b, bx, bz, bhx, bhz, brc, box_, boz, MAT_CONCRETE, C_DECK);
  kit_piece(b);
  kit_fence(b, -42, -42, 42, 42, 1.1f, MAT_CONCRETE, C_CONC2);
  /* two long quarter pipes facing each other across the park's east half */
  quarter(b, 18, -30, 0, 34, 3.0f, 3.5f, 2.2f, MAT_CONCRETE, C_TEAL, C_TEAL);
  quarter(b, 18, 30, PI_F, 34, 3.0f, 3.5f, 2.2f, MAT_CONCRETE, C_ORANGE, C_ORANGE);
  /* the funbox in the middle: a table with banks east and west and grindable lips */
  {
    const float fx = 18, fz = 0, fw = 6, fd = 4.5f, fh = 0.9f;
    kit_piece(b);
    kit_box(b, fx, fz, 0, fw, fd, 0, fh, MAT_CONCRETE, MAT_PAINT, C_DECK, C_YELLOW);
    profile p;
    prof_bank(&p, 2.6f, fh, 1.2f);
    float ut = utop_of(&p);
    ramp_opts o = ropts(MAT_CONCRETE, C_CONC, C_YELLOW, 0, false);
    o.no_back = true;
    kit_piece(b);
    kit_ramp(b, fx + fw * 0.5f + ut, fz, PI_F * 0.5f, fd, &p, &o);
    kit_piece(b);
    kit_ramp(b, fx - fw * 0.5f - ut, fz, -PI_F * 0.5f, fd, &p, &o);
    int run = kit_run(b);
    kit_edge(b, V(fx - fw * 0.5f + 0.05f, fh + 0.01f, fz - fd * 0.5f + 0.03f), V(fx + fw * 0.5f - 0.05f, fh + 0.01f, fz - fd * 0.5f + 0.03f), V(0, 1, 0), EDGE_LEDGE, run);
    int run2 = kit_run(b);
    kit_edge(b, V(fx - fw * 0.5f + 0.05f, fh + 0.01f, fz + fd * 0.5f - 0.03f), V(fx + fw * 0.5f - 0.05f, fh + 0.01f, fz + fd * 0.5f - 0.03f), V(0, 1, 0), EDGE_LEDGE, run2);
    line_add(k, V(fx - 16, 0, fz - fd * 0.5f + 0.4f), -PI_F * 0.5f, 7.5f, LINE_LEDGE, V(fx, fh, fz - fd * 0.5f), run, -1, 2.2f, "Funbox Lip");
    line_add(k, V(fx + 16, 0, fz + 1.0f), PI_F * 0.5f, 7.0f, LINE_KICKER, V(fx - fw * 0.5f, fh, fz), -1, -1, 0, "Funbox Hop");
  }
  /* ledges and a flat bar on the west side */
  int lr = ledge(b, -26, 14, PI_F * 0.5f, 14, 0.6f, 0.5f, C_GRANITE);
  ledge(b, -26, 22, PI_F * 0.5f, 10, 0.6f, 0.5f, C_GRANITE);
  kit_piece(b);
  kit_rail_on(b, V(-24, 0.45f, 31), V(-10, 0.45f, 31), 0, 0, C_RED);
  int rail_run = k->e[k->ne - 1].run;
  /* a manual pad */
  kit_piece(b);
  kit_box(b, -4, 14, 0, 2.2f, 7, 0, 0.22f, MAT_CONCRETE, MAT_CONCRETE, C_DECK, C_GRANITE);
  /* the kicker over the picnic table */
  float kx = -2, kz = 24;
  float ku = ulip(3.6f, 1.15f); /* laid out for the full kicker, so the short control keeps the same gap */
  kicker(b, kx, kz, -PI_F * 0.5f, 3.2f, 3.6f, variant == PARKV_SHORT ? 0.35f : 1.15f, MAT_WOOD, C_WOOD, C_MAGENTA);
  kit_piece(b);
  kit_box(b, kx + ku + 3.0f, kz, 0, 2.0f, 2.6f, 0, 0.75f, MAT_WOOD, MAT_WOOD, C_WOOD, C_TRUNK);
  int tg = gap_add(k, V(kx + ku - 1.5f, 0.3f, kz - 2.5f), V(kx + ku + 0.4f, 2.0f, kz + 2.5f), V(kx + ku + 4.1f, -0.1f, kz - 4), V(kx + ku + 16, 0.2f, kz + 4), "Table Hop", 400);
  /* the variant controls: a wall across the rail's approach, the ledge lifted off the ground */
  if (variant == PARKV_BLOCK) {
    kit_piece(b);
    kit_box(b, -27, 31, 0, 0.4f, 3, 0, 1.4f, MAT_CONCRETE, MAT_CONCRETE, C_RED, C_RED);
  }
  if (variant == PARKV_FLOAT) {
    kit_piece(b);
    kit_box(b, 4, 24, 0, 1.2f, 4, 0.3f, 0.8f, MAT_CONCRETE, MAT_CONCRETE, C_RED, C_RED);
  }
  /* trees and buildings past the fence */
  for (int i = 0; i < 10; i++) tree(b, -46 + (float)(i % 5) * 23 + (i & 1) * 4, i < 5 ? -48 : 48, 1.0f + 0.15f * (float)(i % 3));
  building(b, -70, -60, -52, -20, 14, C_BRICK1);
  building(b, -70, -10, -50, 30, 9, C_BRICK2);
  building(b, 52, -50, 72, -10, 18, C_BRICK3);
  building(b, 52, 0, 76, 40, 11, C_BRICK4);
  /* start points and lines */
  spawn_add(k, V(4, 0, 4), PI_F * 0.5f, "Middle");
  line_add(k, V(30, 0, 18), 0, 8.5f, LINE_QUARTER, V(30, 3.5f, -30 - 3.0f), -1, -1, 0, "North Quarter");
  line_add(k, V(10, 0, -16), PI_F, 8.5f, LINE_QUARTER, V(10, 3.5f, 33), -1, -1, 0, "South Quarter");
  line_add(k, V(-18, 0, 6.5f), 0, 6.5f, LINE_BOWL, V(bx, -bd, bz), -1, -1, 0, "Deep End");
  line_add(k, V(-38, 0, 14.4f), -PI_F * 0.5f, 7.0f, LINE_LEDGE, V(-26, 0.5f, 14), lr, -1, 2.0f, "Long Ledge");
  line_add(k, V(-38, 0, 31.1f), -PI_F * 0.5f, 6.5f, LINE_RAIL, V(-17, 0.45f, 31), rail_run, -1, 1.9f, "Flat Bar");
  line_add(k, V(-16, 0, kz), -PI_F * 0.5f, 8.5f, LINE_GAP, V(kx + ku + 3, 1, kz), -1, tg, 0, "Table Hop");
  line_add(k, V(-4, 0, 26), 0, 5.0f, LINE_MANUAL, V(-4, 0.22f, 17.5f), -1, -1, 1.0f, "Pad");
  k->goal_run = lr;
  k->goal_grind_s = 2.0f;
  snprintf(k->goal_run_name, sizeof k->goal_run_name, "the long ledge");
  k->goal_gap = tg;
  token(k, V(bx, 1.2f, bz - bhz - 0.2f));
  token(k, V(30, 5.2f, -32.6f));
  token(k, V(18, 2.2f, 0));
  token(k, V(-10.5f, 1.4f, 31));
  token(k, V(kx + ku + 3.0f, 2.6f, kz));
  k->ctl_block_line = find_line(k, "Flat Bar");
  k->ctl_short_line = find_line(k, "Table Hop");
}

/* ── Civic Plaza ───────────────────────────────────────────────────────── */
static void build_plaza(kit *b, int variant) {
  park *k = b->k;
  k->theme = 1;
  snprintf(k->blurb, sizeof k->blurb, "a street plaza: stairs, rails, ledges, banks");
  const float th = 1.6f, tz0 = -38, tz1 = -20;
  /* the ground, the terrace (its front at z = tz1) */
  kit_piece(b);
  kit_ground(b, -42, -42, 42, 42, NULL, 0, MAT_TILE, C_TILE);
  kit_piece(b);
  kit_fence(b, -42, -42, 42, 42, 1.1f, MAT_CONCRETE, C_CONC2);
  kit_piece(b);
  kit_box(b, 0, (tz0 + tz1) * 0.5f, 0, 70, tz1 - tz0, 0, th, MAT_TILE, MAT_CONCRETE, C_TILE, C_GRANITE);
  int run_lip = kit_run(b);
  kit_edge(b, V(-34.9f, th + 0.01f, tz1 - 0.03f), V(-17.2f, th + 0.01f, tz1 - 0.03f), V(0, 1, 0), EDGE_LEDGE, run_lip);
  /* stairs down from the terrace, six steps, handrails */
  kit_piece(b);
  kit_stairs(b, -12, tz1, PI_F, 6, th, 6, 0, MAT_CONCRETE, C_GRANITE, 1);
  int rail_l = k->e[k->ne - 3].run;
  kit_piece(b);
  kit_stairs(b, 12, tz1, PI_F, 6, th, 6, 0, MAT_CONCRETE, C_GRANITE, -1);
  int rail_r = k->e[k->ne - 3].run;
  /* banks up to the terrace at its ends */
  bank(b, -30, tz1, 0, 8, 3.6f, th, MAT_CONCRETE, C_CONC, C_GRANITE, 0, false);
  bank(b, 30, tz1, 0, 8, 3.6f, th, MAT_CONCRETE, C_CONC, C_GRANITE, 0, false);
  /* granite ledges on the terrace and in the plaza */
  b->lift = th;
  int l1 = ledge(b, 0, -29, PI_F * 0.5f, 12, 0.7f, 0.55f, C_GRANITE);
  b->lift = 0;
  int l2 = ledge(b, -22, 4, PI_F * 0.5f, 12, 0.7f, 0.5f, C_GRANITE);
  ledge(b, 22, 4, PI_F * 0.5f, 12, 0.7f, 0.5f, C_GRANITE);
  ledge(b, -22, 16, 0.3f, 8, 0.7f, 0.45f, C_GRANITE);
  /* the fountain: a pool wall with a grindable rim (its water drawn, not ridden) */
  {
    const float fx = 0, fz = 14, hw = 5, hd = 4, fh = 0.55f;
    kit_piece(b);
    kit_box(b, fx, fz, 0, hw * 2, hd * 2, 0, fh, MAT_CONCRETE, MAT_CONCRETE, C_WATER, C_GRANITE);
    int run = kit_run(b);
    v3 c[4] = { V(fx - hw + 0.08f, fh + 0.01f, fz - hd + 0.08f), V(fx + hw - 0.08f, fh + 0.01f, fz - hd + 0.08f), V(fx + hw - 0.08f, fh + 0.01f, fz + hd - 0.08f),
                V(fx - hw + 0.08f, fh + 0.01f, fz + hd - 0.08f) };
    int first = -1, prev = -1;
    for (int i = 0; i < 4; i++) {
      int e = kit_edge(b, c[i], c[(i + 1) % 4], V(0, 1, 0), EDGE_LEDGE, run);
      if (prev >= 0) k->e[prev].next = e, k->e[e].prev = prev;
      else first = e;
      prev = e;
    }
    k->e[prev].next = first, k->e[first].prev = prev;
    line_add(k, V(-16, 0, fz - hd + 0.3f), -PI_F * 0.5f, 6.5f, LINE_LEDGE, V(fx, fh, fz - hd), run, -1, 1.9f, "Fountain Rim");
  }
  /* manual pads */
  kit_piece(b);
  kit_box(b, 26, 22, 0, 2.4f, 9, 0, 0.24f, MAT_CONCRETE, MAT_CONCRETE, C_GRANITE, C_GRANITE);
  kit_piece(b);
  kit_box(b, -30, 30, PI_F * 0.5f, 2.4f, 9, 0, 0.24f, MAT_CONCRETE, MAT_CONCRETE, C_GRANITE, C_GRANITE);
  /* a flat bar */
  kit_piece(b);
  kit_rail_on(b, V(6, 0.42f, 32), V(20, 0.42f, 32), 0, 0, C_STEEL);
  int bar = k->e[k->ne - 1].run;
  /* planters with trees on the terrace */
  for (int i = 0; i < 4; i++) {
    float x = -27 + (float)i * 18;
    if (i == 1 || i == 2) continue;
    kit_piece(b);
    kit_box(b, x, -33, 0, 3, 3, th, th + 0.6f, MAT_DIRT, MAT_CONCRETE, C_GRASS, C_GRANITE);
    b->lift = th + 0.6f;
    tree(b, x, -33, 0.9f);
    b->lift = 0;
  }
  if (variant == PARKV_BLOCK) {
    kit_piece(b);
    kit_box(b, -14.55f, -24, 0, 3, 0.4f, th, th + 1.4f, MAT_CONCRETE, MAT_CONCRETE, C_RED, C_RED);
  }
  if (variant == PARKV_FLOAT) {
    kit_piece(b);
    kit_box(b, 10, 24, 0, 1.2f, 4, 0.3f, 0.8f, MAT_CONCRETE, MAT_CONCRETE, C_RED, C_RED);
  }
  int sg = gap_add(k, V(-16, th - 0.2f, tz1 - 3), V(-8, th + 2, tz1 + 0.1f), V(-16, -0.2f, tz1 + 2.1f), V(-8, 0.3f, tz1 + 14), "Six Stair", 500);
  gap_add(k, V(8, th - 0.2f, tz1 - 3), V(16, th + 2, tz1 + 0.1f), V(8, -0.2f, tz1 + 2.1f), V(16, 0.3f, tz1 + 14), "Six Stair", 500);
  building(b, -60, -70, -20, -52, 22, C_BRICK4);
  building(b, -10, -72, 30, -52, 30, C_BRICK3);
  building(b, 40, -70, 70, -50, 16, C_BRICK1);
  building(b, 52, -30, 72, 30, 12, C_BRICK2);
  building(b, -72, -20, -52, 40, 18, C_BRICK1);
  for (int i = 0; i < 6; i++) tree(b, -36 + (float)i * 14, 48, 1.1f);
  spawn_add(k, V(0, 0, 0), 0, "Plaza");
  line_add(k, V(-14.55f, th, -31), PI_F, 7.0f, LINE_RAIL, V(-12, 1.2f, tz1 + 1), rail_l, -1, 1.6f, "Left Handrail");
  line_add(k, V(14.55f, th, -31), PI_F, 7.0f, LINE_RAIL, V(12, 1.2f, tz1 + 1), rail_r, -1, 1.6f, "Right Handrail");
  line_add(k, V(-10, th, -31), PI_F, 7.5f, LINE_GAP, V(-10, 0, tz1 + 4), -1, sg, 0.6f, "Six Stair");
  line_add(k, V(-34, 0, 4.4f), -PI_F * 0.5f, 6.5f, LINE_LEDGE, V(-22, 0.5f, 4), l2, -1, 1.9f, "Plaza Ledge");
  line_add(k, V(-12, th, -28.6f), -PI_F * 0.5f, 6.5f, LINE_LEDGE, V(0, th + 0.55f, -29), l1, -1, 1.9f, "Terrace Ledge");
  line_add(k, V(-6, 0, 32.1f), -PI_F * 0.5f, 6.5f, LINE_RAIL, V(13, 0.42f, 32), bar, -1, 1.9f, "Flat Bar");
  line_add(k, V(26, 0, 32), 0, 5.0f, LINE_MANUAL, V(26, 0.24f, 26.5f), -1, -1, 1.0f, "Pad");
  line_add(k, V(-30, 0, -2), 0, 7.0f, LINE_KICKER, V(-30, th, tz1), -1, -1, 0, "Bank Up");
  k->goal_run = rail_l;
  k->goal_grind_s = 1.0f;
  snprintf(k->goal_run_name, sizeof k->goal_run_name, "the left handrail");
  k->goal_gap = sg;
  token(k, V(-12, 3.2f, tz1 + 1.6f));
  token(k, V(0, 1.6f, 10));
  token(k, V(30, 2.8f, tz1 + 2));
  token(k, V(0, th + 1.4f, -29));
  token(k, V(13, 1.4f, 32));
  k->ctl_block_line = find_line(k, "Left Handrail");
}

/* ── Big Sky: the mega ramp ────────────────────────────────────────────── */
static void build_mega(kit *b, int variant) {
  park *k = b->k;
  k->theme = 2;
  snprintf(k->blurb, sizeof k->blurb, "the mega ramp: a 14 m roll in and a 10 m gap");
  /* the line runs toward +z: the roll in's top deck at z -70, its foot at z -48 */
  const float RH = 14;
  kit_piece(b);
  kit_ground(b, -30, -80, 40, 70, NULL, 0, MAT_ASPHALT, C_ASPH);
  kit_piece(b);
  kit_fence(b, -30, -80, 40, 70, 1.2f, MAT_CONCRETE, C_CONC2);
  /* the roll in: a bank down from the tower (the rider rides toward +z, so the ramp rises toward -z: yaw 0) */
  profile p;
  prof_bank(&p, 20, RH, 9);
  float ut = utop_of(&p);
  float foot = -46;
  {
    kit_piece(b);
    ramp_opts o = ropts(MAT_WOOD, C_WOOD, C_ORANGE, 5.0f, false);
    kit_ramp(b, 0, foot, 0, 8, &p, &o);
  }
  /* the kicker */
  float kz = foot + 12;
  float kr = 12, kh = variant == PARKV_SHORT ? 0.6f : 2.6f;
  float ku = ulip(kr, 2.6f);
  kicker(b, 0, kz, PI_F, 8, kr, kh, MAT_WOOD, C_WOOD, C_ORANGE);
  float lip = kz + ku;
  /* the landing: a knuckle deck at 4 m from lip + 10, then a slope down 16 m */
  float kn = lip + 10, lh = 4;
  {
    kit_piece(b);
    kit_box(b, 0, kn + 1, 0, 8, 2, 0, lh, MAT_WOOD, MAT_PAINT, C_WOOD, C_ORANGE);
    profile q;
    prof_bank(&q, 16, lh, 4);
    float uq = utop_of(&q);
    kit_piece(b);
    ramp_opts o = ropts(MAT_WOOD, C_WOOD, C_ORANGE, 0, false);
    o.no_back = true;
    kit_ramp(b, 0, kn + 2 + uq, 0, 8, &q, &o);
  }
  /* the giant quarter pipe at the end */
  float qz = kn + 2 + 18 + 22;
  quarter(b, 0, qz, PI_F, 12, 6, 8, 2.5f, MAT_WOOD, C_WOOD, C_ORANGE);
  /* the practice area beside it: a mini halfpipe, a rail, a bank */
  quarter(b, 26, -10, 0, 10, 2.0f, 2.0f, 1.5f, MAT_WOOD, C_WOOD, C_TEAL);
  quarter(b, 26, 4, PI_F, 10, 2.0f, 2.0f, 1.5f, MAT_WOOD, C_WOOD, C_TEAL);
  kit_piece(b);
  kit_rail_on(b, V(16, 0.5f, 30), V(32, 0.5f, 30), 0, 0, C_STEEL);
  int bar = k->e[k->ne - 1].run;
  bank(b, -22, 20, -PI_F * 0.5f, 10, 4, 1.8f, MAT_CONCRETE, C_CONC, C_TEAL, 3, true);
  if (variant == PARKV_FLOAT) {
    kit_piece(b);
    kit_box(b, 20, 50, 0, 1.2f, 4, 0.3f, 0.8f, MAT_CONCRETE, MAT_CONCRETE, C_RED, C_RED);
  }
  if (variant == PARKV_BLOCK) {
    kit_piece(b);
    kit_box(b, 13, 30, 0, 0.4f, 3, 0, 1.4f, MAT_CONCRETE, MAT_CONCRETE, C_RED, C_RED);
  }
  int mg = gap_add(k, V(-4, kh - 1.0f, lip - 2.5f), V(4, kh + 1.0f, lip + 0.3f), V(-4, -0.3f, kn + 2.5f), V(4, lh + 0.3f, kn + 22), "Mega Gap", 1500);
  for (int i = 0; i < 8; i++) tree(b, -38 + (float)(i % 2) * 86, -60 + (float)(i / 2) * 32, 1.2f);
  building(b, -60, -40, -42, 0, 10, C_BRICK2);
  building(b, 50, -60, 70, -20, 8, C_BRICK4);
  /* start points: the top of the roll in first */
  line_add(k, V(0, RH, foot - ut - 3.5f), PI_F, 20.0f, LINE_MEGA, V(0, lh, kn + 8), -1, mg, 0, "Mega Line");
  spawn_add(k, V(10, 0, 10), PI_F, "Bottom");
  line_add(k, V(26, 0, -3), 0, 5.0f, LINE_QUARTER, V(26, 2, -12), -1, -1, 0, "Mini Ramp");
  line_add(k, V(4, 0, 30.1f), -PI_F * 0.5f, 6.0f, LINE_RAIL, V(24, 0.5f, 30), bar, -1, 1.9f, "Practice Rail");
  k->goal_run = bar;
  k->goal_grind_s = 1.5f;
  snprintf(k->goal_run_name, sizeof k->goal_run_name, "the practice rail");
  k->goal_gap = mg;
  token(k, V(0, RH - 5, foot - 10));
  token(k, V(0, 7.5f, lip + 5));
  token(k, V(0, 9.2f, qz + 6.1f));
  token(k, V(26, 3.6f, -12.6f));
  token(k, V(30, 1.6f, 30));
  k->ctl_block_line = find_line(k, "Practice Rail");
  k->ctl_short_line = find_line(k, "Mega Line");
}

/* ── Rooftops ──────────────────────────────────────────────────────────── */
static int parapet(kit *b, float x0, float x1, float z, float y, int side) {
  /* a low wall along a roof's edge, its top grindable */
  kit_piece(b);
  float zc = z + 0.2f * (float)side;
  kit_box(b, (x0 + x1) * 0.5f, zc, 0, x1 - x0, 0.4f, y, y + 0.45f, MAT_CONCRETE, MAT_CONCRETE, C_CONC2, C_CONC2);
  int run = kit_run(b);
  kit_edge(b, V(x0 + 0.05f, y + 0.46f, zc), V(x1 - 0.05f, y + 0.46f, zc), V(0, 1, 0), EDGE_LEDGE, run);
  return run;
}
static void build_roofs(kit *b, int variant) {
  park *k = b->k;
  k->theme = 3;
  snprintf(k->blurb, sizeof k->blurb, "rooftops stepping down over the street");
  kit_piece(b);
  kit_ground(b, -64, -34, 84, 34, NULL, 0, MAT_ASPHALT, C_ASPH);
  kit_piece(b);
  kit_fence(b, -64, -34, 84, 34, 1.2f, MAT_CONCRETE, C_CONC2);
  /* four roofs, x ranges and heights; their long sides at z -14 and 14 */
  const float X0[4] = { -52, -16, 19.5f, 50 }, X1[4] = { -20, 12, 44, 72 }, H[4] = { 10, 8, 6, 4 };
  const float *WALL[4] = { C_BRICK1, C_BRICK3, C_BRICK2, C_BRICK4 };
  int par3 = -1;
  for (int i = 0; i < 4; i++) {
    building(b, X0[i], -14, X1[i], 14, H[i], WALL[i]);
    int pr = parapet(b, X0[i] + 1, X1[i] - 1, -14, H[i], 1);
    if (i == 2) par3 = pr;
    if (i == 3) { /* an opening where the bank from the street comes up */
      parapet(b, X0[i] + 1, 56.5f, 14, H[i], -1);
      parapet(b, 65.5f, X1[i] - 1, 14, H[i], -1);
    } else parapet(b, X0[i] + 1, X1[i] - 1, 14, H[i], -1);
  }
  /* roof furniture, stood on the roofs: AC units (grindable boxes), a rail, a kicker on the second roof */
  b->lift = H[0];
  ledge(b, -36, 7, PI_F * 0.5f, 6, 1.4f, 0.8f, C_STEEL);
  ledge(b, -26, -8, 0, 4, 1.4f, 0.8f, C_STEEL);
  kit_piece(b);
  kit_rail_on(b, V(-46, 0.45f, -6), V(-30, 0.45f, -6), 0, 0, C_RED);
  int roof_rail = k->e[k->ne - 1].run;
  b->lift = H[1];
  float ku = kicker(b, 8.5f, 0, -PI_F * 0.5f, 5, 3.2f, variant == PARKV_SHORT ? 0.2f : 1.15f, MAT_WOOD, C_WOOD, C_YELLOW);
  ledge(b, -4, 8, PI_F * 0.5f, 8, 0.6f, 0.5f, C_GRANITE);
  b->lift = H[2];
  kit_piece(b);
  kit_rail_on(b, V(22, 0.5f, 6), V(38, 0.5f, 6), 0, 0, C_STEEL);
  b->lift = 0;
  (void)ku;
  /* a bank from the street up to the lowest roof, its deck meeting the roof's edge */
  {
    profile p;
    prof_bank(&p, 7, H[3], 1.6f);
    float ut = utop_of(&p);
    kit_piece(b);
    ramp_opts o = ropts(MAT_CONCRETE, C_CONC, C_BRICK4, 24 - ut - 14.02f, false);
    kit_ramp(b, 61, 24, 0, 8, &p, &o);
  }
  quarter(b, -2, 27, PI_F, 20, 2.4f, 2.6f, 2.0f, MAT_CONCRETE, C_TEAL, C_TEAL);
  if (variant == PARKV_FLOAT) {
    kit_piece(b);
    kit_box(b, 30, 24, 0, 1.2f, 4, 0.3f, 0.8f, MAT_CONCRETE, MAT_CONCRETE, C_RED, C_RED);
  }
  if (variant == PARKV_BLOCK) {
    kit_piece(b);
    kit_box(b, -47.6f, -6, 0, 0.4f, 7, H[0], H[0] + 1.6f, MAT_CONCRETE, MAT_CONCRETE, C_RED, C_RED);
  }
  int g1 = gap_add(k, V(X1[0] - 3, H[0] - 0.2f, -13), V(X1[0] + 0.1f, H[0] + 2, 13), V(X0[1] - 0.1f, H[1] - 0.2f, -13), V(X1[1], H[1] + 0.3f, 13), "Alley Gap", 600);
  int g2 = gap_add(k, V(X1[1] - 4, H[1] - 0.2f, -13), V(X1[1] + 0.1f, H[1] + 2.5f, 13), V(X0[2] - 0.1f, H[2] - 0.2f, -13), V(X1[2], H[2] + 0.3f, 13), "Street Gap", 900);
  gap_add(k, V(X1[2] - 3, H[2] - 0.2f, -13), V(X1[2] + 0.1f, H[2] + 2, 13), V(X0[3] - 0.1f, H[3] - 0.2f, -13), V(X1[3], H[3] + 0.3f, 13), "Low Roof Drop", 500);
  gap_add(k, V(X1[3] - 3, H[3] - 0.2f, -13), V(X1[3] + 0.1f, H[3] + 2, 13), V(X1[3] + 1, -0.2f, -30), V(84, 0.3f, 30), "Down To The Street", 400);
  for (int i = 0; i < 6; i++) building(b, -60 + (float)i * 26, 44, -42 + (float)i * 26, 60, 12 + (float)((i * 7) % 5) * 4, WALL[i % 4]);
  for (int i = 0; i < 6; i++) building(b, -58 + (float)i * 26, -62, -40 + (float)i * 26, -44, 14 + (float)((i * 5) % 4) * 5, WALL[(i + 2) % 4]);
  line_add(k, V(-48, H[0], 0), -PI_F * 0.5f, 8.5f, LINE_GAP, V(-14, H[1], 0), -1, g1, 0.6f, "Alley Gap");
  line_add(k, V(-12, H[1], 0), -PI_F * 0.5f, 9.0f, LINE_GAP, V(20, H[2], 0), -1, g2, 0, "Street Gap");
  line_add(k, V(-50, H[0], -5.6f), -PI_F * 0.5f, 6.5f, LINE_RAIL, V(-37, H[0] + 0.45f, -6), roof_rail, -1, 1.9f, "Roof Rail");
  line_add(k, V(21, H[2], -13.35f), -PI_F * 0.5f, 6.5f, LINE_LEDGE, V(31, H[2] + 0.45f, -13.8f), par3, -1, 1.9f, "Parapet");
  line_add(k, V(-2, 0, 15), PI_F, 9.0f, LINE_QUARTER, V(-2, 3, 30), -1, -1, 0, "Street Quarter");
  spawn_add(k, V(40, 0, 22), -PI_F * 0.5f, "Street");
  k->goal_run = roof_rail;
  k->goal_grind_s = 1.5f;
  snprintf(k->goal_run_name, sizeof k->goal_run_name, "the roof rail");
  k->goal_gap = g2;
  token(k, V(X1[0] + 2, H[0] + 1.5f, 0));
  token(k, V(X1[1] + 3, H[1] + 1.5f, 0));
  token(k, V(-37, H[0] + 1.6f, -6));
  token(k, V(-2, 4.8f, 30));
  token(k, V(64, 2.5f, 26));
  k->ctl_block_line = find_line(k, "Roof Rail");
  k->ctl_short_line = find_line(k, "Street Gap");
}

/* ── Timber Yard ───────────────────────────────────────────────────────── */
static void build_barn(kit *b, int variant) {
  park *k = b->k;
  k->theme = 4;
  snprintf(k->blurb, sizeof k->blurb, "wooden ramps: a vert halfpipe, a spine, a mini");
  kit_piece(b);
  kit_ground(b, -40, -40, 40, 40, NULL, 0, MAT_CONCRETE, C_CONC);
  kit_piece(b);
  kit_fence(b, -40, -40, 40, 40, 1.1f, MAT_WOOD, C_WOOD);
  /* the vert halfpipe: two quarters 8 m apart (flat bottom), facing each other along z */
  quarter(b, -14, -4, 0, 14, 3.2f, 4.0f, 2.0f, MAT_WOOD, C_WOOD, C_BLUE);
  quarter(b, -14, 4, PI_F, 14, 3.2f, 4.0f, 2.0f, MAT_WOOD, C_WOOD, C_BLUE);
  /* the spine: two quarters back to back */
  quarter(b, 18, -10.4f, 0, 10, 2.6f, 1.7f, 0.02f, MAT_WOOD, C_WOOD, C_RED);
  quarter(b, 18, -15.32f, PI_F, 10, 2.6f, 1.7f, 0.02f, MAT_WOOD, C_WOOD, C_RED);
  /* the mini ramp */
  quarter(b, 18, 14, 0, 8, 1.8f, 1.8f, 1.2f, MAT_WOOD, C_WOOD, C_GREEN);
  quarter(b, 18, 20, PI_F, 8, 1.8f, 1.8f, 1.2f, MAT_WOOD, C_WOOD, C_GREEN);
  /* the pyramid: a box with banks round it, rails off two sides */
  {
    const float px = -14, pz = 26, pw = 4, ph = 0.9f;
    kit_piece(b);
    kit_box(b, px, pz, 0, pw, pw, 0, ph, MAT_WOOD, MAT_PAINT, C_WOOD, C_YELLOW);
    profile p;
    prof_bank(&p, 2.4f, ph, 1.0f);
    float ut = utop_of(&p);
    ramp_opts o = ropts(MAT_WOOD, C_WOOD, C_YELLOW, 0, false);
    o.no_back = true;
    for (int s = 0; s < 4; s++) {
      float a = (float)s * PI_F * 0.5f, ss, cc;
      cw_sincos(a, &ss, &cc);
      /* the bank on side s rises toward the box: its foot ut + pw / 2 out from the middle */
      v3 F = V(-ss, 0, -cc);
      kit_piece(b);
      kit_ramp(b, px - F.x * (pw * 0.5f + ut), pz - F.z * (pw * 0.5f + ut), a, pw, &p, &o);
    }
  }
  kit_piece(b);
  kit_rail_on(b, V(-30, 0.5f, 10), V(-18, 0.5f, 10), 0, 0, C_STEEL);
  int bar = k->e[k->ne - 1].run;
  int lr = ledge(b, 2, 30, PI_F * 0.5f, 10, 0.6f, 0.5f, C_WOOD);
  if (variant == PARKV_FLOAT) {
    kit_piece(b);
    kit_box(b, 30, 0, 0, 1.2f, 4, 0.3f, 0.8f, MAT_CONCRETE, MAT_CONCRETE, C_RED, C_RED);
  }
  if (variant == PARKV_BLOCK) {
    kit_piece(b);
    kit_box(b, -34, 10, 0, 0.4f, 3, 0, 1.4f, MAT_CONCRETE, MAT_CONCRETE, C_RED, C_RED);
  }
  for (int i = 0; i < 8; i++) tree(b, -44 + (float)(i % 4) * 29, i < 4 ? -46 : 46, 1.1f);
  building(b, -70, -30, -50, 20, 9, C_BRICK1);
  building(b, 50, -40, 66, 30, 7, C_BRICK2);
  spawn_add(k, V(0, 0, 0), 0, "Yard");
  line_add(k, V(-14, 0, 3.5f), 0, 7.0f, LINE_QUARTER, V(-14, 4, -7.2f), -1, -1, 0, "Halfpipe");
  line_add(k, V(18, 0, 4), 0, 7.0f, LINE_KICKER, V(18, 1.7f, -12.86f), -1, -1, 0, "Spine");
  line_add(k, V(18, 1.8f, 22.6f), 0, 3.0f, LINE_QUARTER, V(18, 1.8f, 12.2f), -1, -1, 0, "Mini Ramp");
  line_add(k, V(-38, 0, 10.1f), -PI_F * 0.5f, 6.0f, LINE_RAIL, V(-24, 0.5f, 10), bar, -1, 1.9f, "Yard Rail");
  line_add(k, V(-10, 0, 30.4f), -PI_F * 0.5f, 6.5f, LINE_LEDGE, V(2, 0.5f, 30), lr, -1, 1.9f, "Plank Ledge");
  line_add(k, V(-14, 0, 12), PI_F, 6.5f, LINE_KICKER, V(-14, 0.9f, 26), -1, -1, 0, "Pyramid");
  k->goal_run = bar;
  k->goal_grind_s = 1.5f;
  snprintf(k->goal_run_name, sizeof k->goal_run_name, "the yard rail");
  k->goal_gap = -1;
  token(k, V(-14, 5.6f, -7.4f));
  token(k, V(-20, 5.6f, 7.4f));
  token(k, V(18, 3.6f, -12.8f));
  token(k, V(-14, 2.4f, 26));
  token(k, V(2, 1.6f, 30));
  k->ctl_block_line = find_line(k, "Yard Rail");
}

bool park_build(park *k, int n, int variant) {
  kit b;
  kit_begin(&b, k);
  snprintf(k->name, sizeof k->name, "%s", PARK_NAMES[n % PARK_COUNT]);
  switch (n) {
  case PARK_SUNNY: build_sunny(&b, variant); break;
  case PARK_PLAZA: build_plaza(&b, variant); break;
  case PARK_MEGA: build_mega(&b, variant); break;
  case PARK_ROOFS: build_roofs(&b, variant); break;
  case PARK_BARN: build_barn(&b, variant); break;
  default: return false;
  }
  park_finish(k);
  return true;
}
