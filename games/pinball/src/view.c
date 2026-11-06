/* pinball's view: the table in 3D from the game's own data (tables.c). Render only.
 *
 *   playfield  one plane with the table's painted art (art.c), a waxed finish (low roughness): it shows the lights
 *   walls      merged per material: wood for the cabinet, chrome guides, coloured plastics, black and white rubber
 *   parts      pop bumpers (a flashing cap and a light), slingshots, drop targets that sink, standups, spinners that
 *              turn, holes, flippers, the plunger, ramps (clear tinted plastic troughs with chrome rails)
 *   inserts    the lamps: one instanced mesh per shape, their brightness the instance colour (lit ones run past white
 *              into the bloom); the brightest lit inserts also get a small coloured light each
 *   lights     a key light from above with a shadow (the ball's shadow), warm side lights, a light in every pop,
 *              flashers on the big events, the clustered lighting carrying them all
 *   ball       a mirror ball: the scene's reflection probe gives it the table and its lights */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"
#include "geo.h"
#include "view.h"

#define NLAMPLIGHT 40
#define NGI 6
#define NFLASH 4
#define NSHAPES 6

static struct {
  cw_node root, table;
  int built;
  /* moving parts */
  cw_node ball[PB_MAXB];
  cw_node flip[PB_MAXFL];
  cw_node part[MAXEL];       /* drop targets, spinners, pop caps, standups: by element */
  t3_material *part_mat[MAXEL];
  cw_node plunger;
  t3_instanced_mesh *lamp_im[NSHAPES];
  int lamp_slot[MAXLAMP];
  cw_node lamp_light[NLAMPLIGHT];
  cw_node gi[NGI];
  cw_node pop_light[4];
  cw_node flash_light[NFLASH];
  t3_material *flash_mat[NFLASH];
  int nflash;
  float flash[NFLASH];
  float pop_flash[4];
  float drop_z[MAXEL];
  float lamp_level[MAXLAMP]; /* each insert drawn like a bulb: up fast, down slower */
  float t;
  t3_vec3 eye, at;
  float fov, aspect;
  float cam_follow;
  view_stats st;
} V;

float g_cam_k = 1.0f;

static t3_color lin(const float *c, float k) { return (t3_color){ c[0] * k, c[1] * k, c[2] * k }; }

static t3_material *std_mat(float r, float g, float b, float rough, float metal) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = (t3_color){ r, g, b };
  m->roughness = rough, m->metalness = metal;
  return m;
}

/* a node holding one mesh (the node owns it); shadow: it casts and takes shadows */
static cw_node piece(cw_node parent, const char *name, t3_geometry *g, t3_material *m, bool shadow) {
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  t3_object *grp = t3_group_new();
  t3_mesh *mesh = t3_mesh_new(g, m);
  mesh->base.cast_shadow = shadow, mesh->base.receive_shadow = true;
  t3_object_add(grp, mesh);
  cw_node3d_set_object(n, grp);
  t3_release(mesh), t3_release(grp), t3_release(g), t3_release(m);
  return n;
}

static cw_node omni(cw_node parent, const char *name, t3_color c, float energy, float range) {
  cw_node l = cw_node_new("OmniLight3D", name);
  cw_node_add_child(parent, l);
  cw_node_set(l, "color", cw_val_color(c.r, c.g, c.b, 1));
  cw_node_set(l, "energy", cw_val_float(energy));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node_set(l, "decay", cw_val_float(2));
  return l;
}

void view_init(cw_node root) {
  memset(&V, 0, sizeof V);
  V.root = root;
  V.built = -1;
  V.fov = 36;
}

int view_built_table(void) { return V.built; }

/* ── the inserts' shapes (flat, local x along the shape's pointing, unit size) ── */

static t3_geometry *lamp_shape(int shape) {
  gb b = { 0 };
  float p[16][2];
  int n = 0;
  switch (shape) {
  case LS_ARROW: { /* a chevron pointing +x */
    static const float A[][2] = { { 1.2f, 0 }, { -0.2f, 1.0f }, { -0.8f, 1.0f }, { 0.35f, 0 }, { -0.8f, -1.0f }, { -0.2f, -1.0f } };
    /* two convex halves */
    float h1[4][2] = { { A[0][0], -A[0][1] }, { A[1][0], -A[1][1] }, { A[2][0], -A[2][1] }, { A[3][0], -A[3][1] } };
    float h2[4][2] = { { A[0][0], -A[0][1] }, { A[3][0], -A[3][1] }, { A[4][0], -A[4][1] }, { A[5][0], -A[5][1] } };
    gb_prism(&b, (const float (*)[2])h1, 4, 0, 0.0012f);
    gb_prism(&b, (const float (*)[2])h2, 4, 0, 0.0012f);
    return gb_geometry(&b);
  }
  case LS_TRI: n = 3; p[0][0] = 1.2f, p[0][1] = 0, p[1][0] = -0.8f, p[1][1] = -1, p[2][0] = -0.8f, p[2][1] = 1; break;
  case LS_RECT: n = 4; p[0][0] = -1, p[0][1] = -0.6f, p[1][0] = 1, p[1][1] = -0.6f, p[2][0] = 1, p[2][1] = 0.6f, p[3][0] = -1, p[3][1] = 0.6f; break;
  case LS_STAR:
    n = 10;
    for (int i = 0; i < 10; i++) {
      float a = (float)i * 0.6283185f + 1.5707963f, r = i & 1 ? 0.45f : 1.15f;
      p[i][0] = cosf(a) * r, p[i][1] = -sinf(a) * r;
    }
    break;
  case LS_SHIELD:
    n = 7;
    { static const float S[][2] = { { -1, 0.8f }, { 1, 0.8f }, { 1, 0 }, { 0.6f, -0.7f }, { 0, -1.1f }, { -0.6f, -0.7f }, { -1, 0 } };
      for (int i = 0; i < 7; i++) p[i][0] = S[i][0], p[i][1] = -S[i][1]; }
    break;
  default:
    n = 16;
    for (int i = 0; i < 16; i++) { float a = (float)i * 0.3926991f; p[i][0] = cosf(a), p[i][1] = -sinf(a); }
    break;
  }
  gb_prism(&b, (const float (*)[2])p, n, 0, 0.0012f);
  return gb_geometry(&b);
}

/* ── building a table ────────────────────────────────────────────────── */

static void wall_geo(gb *b, const wall *w) {
  int segs = w->closed ? w->n : w->n - 1;
  for (int i = 0; i < segs; i++) {
    int j = (i + 1) % w->n;
    float ax = w->p[i][0], ay = w->p[i][1], bx = w->p[j][0], by = w->p[j][1];
    float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-5f) continue;
    t3_vec3 c = tw((ax + bx) * 0.5f, (ay + by) * 0.5f, w->h * 0.5f);
    gb_box(b, c.x, c.y, c.z, l * 0.5f, w->h * 0.5f, w->r, atan2f(dy, dx));
  }
  for (int i = 0; i < w->n; i++) {
    t3_vec3 c = tw(w->p[i][0], w->p[i][1], 0);
    gb_cyl(b, c.x, 0, c.z, w->h, w->r, 10, true);
  }
}

static void flipper_outline(const pb_flipper *f, float grow, float (*p)[2], int *n) {
  /* the uneven capsule's hull, local: the pivot at 0, the tip along +x; as (x, z) with z = -y */
  int k = 0;
  float rb = f->rb + grow, rt = f->rt + grow, b = (f->rb - f->rt) / f->len, a0 = asinf(b);
  for (int i = 0; i <= 8; i++) { /* the base's far side: from +90+a0 through 180 to -90-a0 */
    float a = (1.5707963f + a0) + (3.1415926f - 2 * a0) * (float)i / 8.0f;
    p[k][0] = cosf(a) * rb, p[k][1] = -sinf(a) * rb, k++;
  }
  for (int i = 0; i <= 8; i++) { /* the tip: from -90-a0 round through 0 to 90+a0 */
    float a = -(1.5707963f + a0) + (3.1415926f + 2 * a0) * (float)i / 8.0f;
    p[k][0] = f->len + cosf(a) * rt, p[k][1] = -sinf(a) * rt, k++;
  }
  *n = k;
}

void view_build(const game *g) {
  const table *T = &g->T;
  if (cw_node_valid(V.table)) cw_node_free(V.table);
  memset(V.part, 0, sizeof V.part), memset(V.part_mat, 0, sizeof V.part_mat);
  memset(V.flash, 0, sizeof V.flash), memset(V.pop_flash, 0, sizeof V.pop_flash);
  V.table = cw_node_new("Node3D", "Table");
  cw_node_add_child(V.root, V.table);
  const theme *th = &T->th;
  /* the playfield */
  {
    int w, h;
    uint8_t *px = art_paint(T, &w, &h);
    t3_texture *tex = t3_texture_new(w, h, px);
    free(px);
    tex->color_space = T3_SRGB_COLOR_SPACE;
    tex->anisotropy = 8;
    t3_material *m = std_mat(1, 1, 1, 0.32f, 0);
    t3_material_set_map(m, tex);
    t3_release(tex);
    t3_geometry *pg = t3_plane_geometry_new(0.52f, 1.09f, 1, 1);
    t3_mat4 rx = { { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } }; /* -90 about x: the plane lies flat, its top up the table */
    t3_geometry_apply_matrix4(pg, &rx);
    cw_node n = piece(V.table, "Playfield", pg, m, false);
    cw_node3d_set_position(n, tw(0, 0.545f, 0));
    /* under the board: a dark floor so nothing shows through */
  }
  /* walls by material */
  {
    gb wb[MAT_COUNT] = { 0 };
    for (int i = 0; i < T->nwall; i++) wall_geo(&wb[T->walls[i].mat], &T->walls[i]);
    /* posts: rubber rings on chrome posts */
    for (int i = 0; i < T->nel; i++) {
      const elem *e = &T->el[i];
      if (e->kind != EK_POST) continue;
      t3_vec3 c = tw(e->x, e->y, 0);
      gb_band(&wb[e->col ? MAT_RUBBER_WHITE : MAT_RUBBER], c.x, 0.006f, c.z, 0.018f, e->w * 0.45f, e->w, 14);
      gb_cyl(&wb[MAT_METAL], c.x, 0, c.z, 0.03f, e->w * 0.45f, 10, true);
    }
    /* the slingshots' rubber faces */
    for (int i = 0; i < T->nel; i++) {
      const elem *e = &T->el[i];
      if (e->kind != EK_SLING) continue;
      const pb_shape *s = &T->W.sh[e->shape];
      float dx = s->bx - s->ax, dy = s->by - s->ay;
      t3_vec3 c = tw((s->ax + s->bx) * 0.5f, (s->ay + s->by) * 0.5f, 0.012f);
      gb_box(&wb[MAT_RUBBER_WHITE], c.x, c.y, c.z, sqrtf(dx * dx + dy * dy) * 0.5f, 0.006f, 0.0035f, atan2f(dy, dx));
    }
    t3_material *mats[MAT_COUNT];
    mats[MAT_WOOD] = std_mat(th->wood[0], th->wood[1], th->wood[2], 0.5f, 0);
    mats[MAT_METAL] = std_mat(0.9f, 0.9f, 0.92f, 0.18f, 1);
    mats[MAT_PLASTIC] = std_mat(0.85f, 0.85f, 0.85f, 0.3f, 0);
    mats[MAT_RUBBER] = std_mat(0.03f, 0.03f, 0.03f, 0.75f, 0);
    mats[MAT_RUBBER_WHITE] = std_mat(0.75f, 0.75f, 0.72f, 0.6f, 0);
    mats[MAT_WIRE] = std_mat(0.9f, 0.9f, 0.92f, 0.15f, 1);
    for (int m = 0; m < MAT_COUNT; m++) {
      if (wb[m].nv) piece(V.table, "Walls", gb_geometry(&wb[m]), mats[m], true);
      else gb_free(&wb[m]), t3_release(mats[m]);
    }
  }
  /* the cabinet round the playfield: its side rails and the lockdown bar, and the apron */
  {
    gb b = { 0 };
    t3_vec3 c = tw(-0.28f, 0.53f, 0.03f);
    gb_box(&b, c.x, c.y, c.z, 0.018f, 0.045f, 0.58f, 0);
    c = tw(0.278f, 0.53f, 0.03f);
    gb_box(&b, c.x, c.y, c.z, 0.018f, 0.045f, 0.58f, 0);
    c = tw(0, 1.1f, 0.03f);
    gb_box(&b, c.x, c.y, c.z, 0.3f, 0.045f, 0.015f, 0);
    piece(V.table, "Cabinet", gb_geometry(&b), std_mat(th->wood[0] * 0.8f, th->wood[1] * 0.8f, th->wood[2] * 0.8f, 0.45f, 0), true);
    gb a = { 0 };
    float ap[6][2] = { { -0.258f, -0.0f }, { -0.16f, -0.055f }, { -0.06f, -0.06f }, { 0.03f, -0.06f }, { 0.127f, -0.055f }, { 0.219f, 0.0f } };
    float ap2[8][2];
    for (int i = 0; i < 6; i++) ap2[i][0] = ap[5 - i][0], ap2[i][1] = ap[5 - i][1] - 0.545f + 0.0f;
    (void)ap2;
    /* the apron: a metal plate over the bottom, below the flippers */
    t3_vec3 ac = tw(-0.02f, 0.01f, 0.004f);
    gb_box(&a, ac.x, ac.y, ac.z, 0.24f, 0.004f, 0.03f, 0);
    piece(V.table, "Apron", gb_geometry(&a), std_mat(th->col[3][0] * 0.3f + 0.05f, th->col[3][1] * 0.3f + 0.05f, th->col[3][2] * 0.3f + 0.05f, 0.35f, 0.7f), true);
  }
  /* plastics: coloured translucent sheets on posts */
  for (int i = 0; i < T->ndec; i++) {
    const decor *d = &T->dec[i];
    if (d->kind != DC_PLASTIC) continue;
    gb b = { 0 };
    float p[8][2];
    for (int k = 0; k < d->n; k++) p[k][0] = d->pts[k][0], p[k][1] = TABLE_MID - d->pts[k][1];
    gb_prism(&b, (const float (*)[2])p, d->n, d->z, d->z + 0.003f);
    t3_material *m = std_mat(th->col[d->col][0], th->col[d->col][1], th->col[d->col][2], 0.2f, 0);
    m->transparent = true, m->opacity = 0.78f;
    m->emissive = lin(th->col[d->col], 0.35f);
    piece(V.table, "Plastic", gb_geometry(&b), m, true);
  }
  /* parts */
  int npop = 0;
  for (int i = 0; i < T->nel; i++) {
    const elem *e = &T->el[i];
    t3_vec3 c = tw(e->x, e->y, 0);
    switch (e->kind) {
    case EK_POP: {
      gb b = { 0 };
      gb_cyl(&b, c.x, 0, c.z, 0.004f, 0.03f, 24, true);           /* the base ring */
      gb_cyl(&b, c.x, 0.004f, c.z, 0.028f, 0.021f, 24, false);    /* the body */
      piece(V.table, "PopBody", gb_geometry(&b), std_mat(0.85f, 0.85f, 0.82f, 0.35f, 0), true);
      gb s = { 0 };
      gb_band(&s, c.x, 0.008f, c.z, 0.012f, 0.021f, 0.027f, 24);  /* the skirt */
      piece(V.table, "PopSkirt", gb_geometry(&s), std_mat(0.85f, 0.85f, 0.88f, 0.2f, 1), true);
      gb cap = { 0 };
      gb_cyl(&cap, c.x, 0.028f, c.z, 0.036f, 0.024f, 24, true);
      gb_dome(&cap, c.x, 0.036f, c.z, 0.016f, 16);
      t3_material *m = std_mat(th->col[e->col][0], th->col[e->col][1], th->col[e->col][2], 0.25f, 0);
      m->emissive = lin(th->col[e->col], 0.5f);
      V.part[i] = piece(V.table, "PopCap", gb_geometry(&cap), m, true);
      V.part_mat[i] = m;
      if (npop < 4) V.pop_light[npop] = omni(V.table, "PopLight", lin(th->col[e->col], 1), 0.0f, 0.16f), cw_node3d_set_position(V.pop_light[npop], tw(e->x, e->y, 0.05f)), npop++;
      break;
    }
    case EK_DROP: case EK_TARGET: {
      gb b = { 0 };
      bool drop = e->kind == EK_DROP;
      gb_box(&b, 0, drop ? 0.014f : 0.016f, 0, e->w * 0.5f, drop ? 0.014f : 0.01f, 0.003f, 0);
      t3_material *m = std_mat(th->col[e->col][0], th->col[e->col][1], th->col[e->col][2], 0.35f, 0);
      m->emissive = lin(th->col[e->col], 0.2f);
      cw_node n = piece(V.table, drop ? "Drop" : "Target", gb_geometry(&b), m, true);
      cw_node3d_set_position(n, c);
      cw_node3d_set_rotation(n, (t3_vec3){ 0, (e->ang + 90) * CW_PI / 180.0f, 0 });
      V.part[i] = n, V.part_mat[i] = m, V.drop_z[i] = 0;
      if (!drop) { /* its wire post behind */
        gb p = { 0 };
        float a = e->ang * CW_PI / 180.0f;
        t3_vec3 pc = tw(e->x - cosf(a) * 0.006f, e->y - sinf(a) * 0.006f, 0);
        gb_cyl(&p, pc.x, 0, pc.z, 0.022f, 0.0015f, 6, true);
        piece(V.table, "TargetPost", gb_geometry(&p), std_mat(0.8f, 0.8f, 0.82f, 0.2f, 1), true);
      }
      break;
    }
    case EK_SPINNER: {
      gb b = { 0 };
      gb_box(&b, 0, -0.008f, 0, e->w * 0.45f, 0.008f, 0.0008f, 0);
      t3_material *m = std_mat(0.9f, 0.9f, 0.92f, 0.15f, 1);
      cw_node n = piece(V.table, "Spinner", gb_geometry(&b), m, true);
      cw_node3d_set_position(n, tw(e->x, e->y, 0.024f));
      V.part[i] = n;
      gb w = { 0 };
      float a = e->ang * CW_PI / 180.0f;
      for (int s = -1; s <= 1; s += 2) {
        t3_vec3 pc = tw(e->x + cosf(a) * e->w * 0.5f * (float)s, e->y + sinf(a) * e->w * 0.5f * (float)s, 0);
        gb_cyl(&w, pc.x, 0, pc.z, 0.028f, 0.0015f, 6, true);
      }
      piece(V.table, "SpinnerWire", gb_geometry(&w), std_mat(0.85f, 0.85f, 0.88f, 0.2f, 1), true);
      break;
    }
    case EK_HOLE: {
      gb b = { 0 };
      gb_band(&b, c.x, 0, c.z, 0.004f, 0.016f, 0.019f, 20);
      piece(V.table, "HoleRim", gb_geometry(&b), std_mat(0.75f, 0.75f, 0.78f, 0.2f, 1), false);
      break;
    }
    case EK_ROLLOVER: case EK_LOOP: {
      if (e->kind == EK_LOOP) break;
      gb b = { 0 };
      float a = e->ang * CW_PI / 180.0f;
      t3_vec3 p0 = tw(e->x - cosf(a) * e->w * 0.3f, e->y - sinf(a) * e->w * 0.3f, 0.0015f);
      t3_vec3 p1 = tw(e->x + cosf(a) * e->w * 0.3f, e->y + sinf(a) * e->w * 0.3f, 0.0015f);
      float pts[2][3] = { { p0.x, p0.y, p0.z }, { p1.x, p1.y, p1.z } };
      gb_tube(&b, (const float (*)[3])pts, 2, 0.0009f, 6);
      piece(V.table, "Rollover", gb_geometry(&b), std_mat(0.9f, 0.9f, 0.92f, 0.15f, 1), false);
      break;
    }
    case EK_RAMP: {
      const pb_path *P = &T->W.path[e->path];
      float pts[PB_PATHPTS][3], rl[PB_PATHPTS][3], rr[PB_PATHPTS][3];
      for (int k = 0; k < P->n; k++) {
        t3_vec3 w = tw(P->p[k][0], P->p[k][1], P->p[k][2] - PB_R - 0.001f);
        pts[k][0] = w.x, pts[k][1] = fmaxf(w.y, 0.0005f), pts[k][2] = w.z;
      }
      gb b = { 0 };
      gb_trough(&b, (const float (*)[3])pts, P->n, 0.036f, 0.018f);
      t3_material *m = std_mat(th->col[e->col][0] * 0.6f + 0.3f, th->col[e->col][1] * 0.6f + 0.3f, th->col[e->col][2] * 0.6f + 0.3f, 0.08f, 0);
      m->transparent = true, m->opacity = 0.5f, m->side = T3_DOUBLE_SIDE, m->depth_write = false;
      m->emissive = lin(th->col[e->col], 0.12f);
      cw_node rn = piece(V.table, "Ramp", gb_geometry(&b), m, false);
      (void)rn;
      /* chrome rails along both top edges */
      for (int k = 0; k < P->n; k++) {
        int a = k > 0 ? k - 1 : 0, cc = k < P->n - 1 ? k + 1 : P->n - 1;
        float tx = pts[cc][0] - pts[a][0], tz = pts[cc][2] - pts[a][2], tl = sqrtf(tx * tx + tz * tz);
        if (tl < 1e-6f) tl = 1;
        float sx = -tz / tl * 0.019f, sz = tx / tl * 0.019f;
        rl[k][0] = pts[k][0] - sx, rl[k][1] = pts[k][1] + 0.019f, rl[k][2] = pts[k][2] - sz;
        rr[k][0] = pts[k][0] + sx, rr[k][1] = pts[k][1] + 0.019f, rr[k][2] = pts[k][2] + sz;
      }
      gb r = { 0 };
      gb_tube(&r, (const float (*)[3])rl, P->n, 0.0016f, 6);
      gb_tube(&r, (const float (*)[3])rr, P->n, 0.0016f, 6);
      piece(V.table, "RampRail", gb_geometry(&r), std_mat(0.92f, 0.92f, 0.95f, 0.12f, 1), true);
      /* posts holding it up */
      gb sp = { 0 };
      for (int k = 4; k < P->n - 2; k += 8)
        if (pts[k][1] > 0.02f) gb_cyl(&sp, pts[k][0], 0, pts[k][2], pts[k][1], 0.002f, 6, false);
      if (sp.nv) piece(V.table, "RampPost", gb_geometry(&sp), std_mat(0.85f, 0.85f, 0.88f, 0.2f, 1), true);
      else gb_free(&sp);
      break;
    }
    case EK_PLUNGER: {
      gb b = { 0 };
      float rod[2][3] = { { 0, 0.012f, 0 }, { 0, 0.012f, 0.09f } };
      gb_tube(&b, (const float (*)[3])rod, 2, 0.003f, 10);
      gb_cyl(&b, 0, 0.004f, 0.0f, 0.02f, 0.0085f, 14, true);
      cw_node n = piece(V.table, "Plunger", gb_geometry(&b), std_mat(0.85f, 0.85f, 0.88f, 0.2f, 1), true);
      cw_node3d_set_rotation(n, (t3_vec3){ 0, 0, 0 });
      V.plunger = n;
      break;
    }
    default: break;
    }
  }
  /* flippers */
  for (int f = 0; f < g->W.nfl; f++) {
    const pb_flipper *F = &g->W.fl[f];
    float p[24][2];
    int n;
    gb b = { 0 }, r = { 0 };
    flipper_outline(F, -0.0018f, p, &n);
    gb_prism(&b, (const float (*)[2])p, n, 0.002f, 0.024f);
    flipper_outline(F, 0.0008f, p, &n);
    gb_prism(&r, (const float (*)[2])p, n, 0.008f, 0.018f);
    cw_node fn = piece(V.table, "Flipper", gb_geometry(&b), std_mat(0.92f, 0.92f, 0.9f, 0.3f, 0), true);
    t3_material *rm = std_mat(th->col[0][0] * 0.8f, th->col[0][1] * 0.8f, th->col[0][2] * 0.8f, 0.6f, 0);
    t3_mesh *rubber = t3_mesh_new(gb_geometry(&r), rm);
    t3_release(rm);
    rubber->base.cast_shadow = true;
    t3_object_add(cw_node3d_object(fn), rubber);
    t3_release(rubber->geometry), t3_release(rubber);
    cw_node3d_set_position(fn, tw(F->px, F->py, 0));
    V.flip[f] = fn;
  }
  /* inserts */
  {
    int count[NSHAPES] = { 0 };
    for (int i = 0; i < T->nlamp; i++) V.lamp_slot[i] = count[T->lamps[i].shape]++;
    for (int s = 0; s < NSHAPES; s++) {
      V.lamp_im[s] = NULL;
      if (!count[s]) continue;
      t3_material *m = t3_mesh_basic_material_new(0xffffff);
      m->side = T3_DOUBLE_SIDE;
      t3_instanced_mesh *im = t3_instanced_mesh_new(lamp_shape(s), m, count[s]);
      t3_release(m);
      im->mesh.base.cast_shadow = false, im->mesh.base.receive_shadow = false;
      im->mesh.base.frustum_culled = false; /* small and always on the table */
      for (int i = 0; i < count[s]; i++) t3_instanced_mesh_set_color_at(im, i, (t3_color){ 0, 0, 0 });
      im->instance_color->usage = T3_DYNAMIC_DRAW;
      for (int i = 0; i < T->nlamp; i++) {
        const lamp *l = &T->lamps[i];
        if (l->shape != s) continue;
        t3_mat4 mx;
        float a = l->ang * CW_PI / 180.0f;
        t3_mat4_compose(&mx, tw(l->x, l->y, 0.0006f), (t3_quat){ 0, sinf(a * 0.5f), 0, cosf(a * 0.5f) }, (t3_vec3){ l->size, 1, l->size });
        t3_instanced_mesh_set_matrix_at(im, V.lamp_slot[i], &mx);
      }
      cw_node n = cw_node_new("Node3D", "Inserts");
      cw_node_add_child(V.table, n);
      t3_object *grp = t3_group_new();
      t3_object_add(grp, &im->mesh.base);
      cw_node3d_set_object(n, grp);
      t3_release(grp), t3_release(im->mesh.geometry), t3_release(im);
      V.lamp_im[s] = im;
    }
    for (int i = 0; i < NLAMPLIGHT; i++) V.lamp_light[i] = omni(V.table, "InsertLight", (t3_color){ 1, 1, 1 }, 0, 0.07f), cw_node3d_set_position(V.lamp_light[i], tw(0, -5, 0.01f));
  }
  /* the general illumination: warm lamps down both sides and under the top arc */
  {
    static const float GI[NGI][3] = { { -0.235f, 0.3f, 0.045f }, { -0.235f, 0.65f, 0.045f }, { 0.2f, 0.3f, 0.045f }, { 0.2f, 0.65f, 0.045f }, { -0.1f, 0.98f, 0.05f }, { 0.08f, 0.98f, 0.05f } };
    for (int i = 0; i < NGI; i++) {
      V.gi[i] = omni(V.table, "GI", lin(th->light, 1), 0.035f, 0.45f);
      cw_node3d_set_position(V.gi[i], tw(GI[i][0], GI[i][1], GI[i][2]));
    }
  }
  /* flashers */
  V.nflash = 0;
  for (int i = 0; i < T->ndec && V.nflash < NFLASH; i++) {
    const decor *d = &T->dec[i];
    if (d->kind != DC_FLASHER) continue;
    gb b = { 0 };
    t3_vec3 c = tw(d->x, d->y, d->z);
    gb_dome(&b, c.x, c.y, c.z, d->w, 16);
    gb_cyl(&b, c.x, 0, c.z, c.y, 0.004f, 8, false);
    t3_material *m = std_mat(th->col[d->col][0], th->col[d->col][1], th->col[d->col][2], 0.1f, 0);
    m->transparent = true, m->opacity = 0.8f;
    m->emissive = lin(th->col[d->col], 0.3f);
    piece(V.table, "Flasher", gb_geometry(&b), m, false);
    V.flash_mat[V.nflash] = m;
    V.flash_light[V.nflash] = omni(V.table, "FlashLight", lin(th->col[d->col], 1), 0, 0.6f);
    cw_node3d_set_position(V.flash_light[V.nflash], tw(d->x, d->y, d->z + 0.02f));
    V.nflash++;
  }
  /* what the mirror ball sees above it: the cabinet's lit canopy, bright tubes over a dark board. On a layer only the
   * reflection probe draws (the camera never shows it) */
  {
    gb b = { 0 };
    for (int k = 0; k < 4; k++) {
      t3_vec3 c = tw(-0.2f + (float)k * 0.13f, 0.55f, 0.55f);
      gb_box(&b, c.x, c.y, c.z, 0.012f, 0.004f, 0.62f, 0);
    }
    t3_vec3 c = tw(-0.017f, 1.0f, 0.5f);
    gb_box(&b, c.x, c.y, c.z, 0.3f, 0.004f, 0.02f, 0);
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->color = (t3_color){ th->light[0] * 6, th->light[1] * 6, th->light[2] * 6 };
    m->side = T3_DOUBLE_SIDE;
    cw_node n = piece(V.table, "Canopy", gb_geometry(&b), m, false);
    cw_node_set(n, "layers", cw_val_int(4));
    gb d = { 0 };
    t3_vec3 dc = tw(-0.017f, 0.55f, 0.56f);
    gb_box(&d, dc.x, dc.y, dc.z, 0.4f, 0.002f, 0.75f, 0);
    t3_material *dm = t3_mesh_basic_material_new(0xffffff);
    dm->color = (t3_color){ th->base2[0] * 0.25f + 0.02f, th->base2[1] * 0.25f + 0.02f, th->base2[2] * 0.25f + 0.02f };
    dm->side = T3_DOUBLE_SIDE;
    cw_node dn = piece(V.table, "CanopyBoard", gb_geometry(&d), dm, false);
    cw_node_set(dn, "layers", cw_val_int(4));
  }
  /* the balls */
  for (int i = 0; i < PB_MAXB; i++) {
    t3_material *m = std_mat(0.97f, 0.97f, 0.98f, 0.04f, 1);
    V.ball[i] = piece(V.table, "Ball", t3_sphere_geometry_new(PB_R, 32, 24), m, true);
    cw_node_set(V.ball[i], "layers", cw_val_int(2)); /* not in the probe's capture: the ball never mirrors itself */
    cw_node3d_set_position(V.ball[i], tw(0, -5, 0));
  }
  V.built = g->table_id;
}

/* ── every frame ─────────────────────────────────────────────────────── */

void view_sync(const game *g, float alpha, float dt) {
  if (V.built != g->table_id) view_build(g);
  const table *T = &g->T;
  const theme *th = &T->th;
  V.t += dt;
  /* events light things */
  for (int k = 0; k < g->nfx; k++) {
    int kind = g->fx[k].kind;
    if (kind == FX_POP) {
      for (int p = 0; p < T->npop; p++) if (T->pops[p] == g->fx[k].a) V.pop_flash[p] = 1;
    }
    float big = kind == FX_JACKPOT || kind == FX_MULTIBALL || kind == FX_MODE_DONE || kind == FX_EXTRA || kind == FX_SKILL ? 1.0f
              : kind == FX_MODE_START || kind == FX_MODE_SHOT || kind == FX_LOCK || kind == FX_RAMP_DONE || kind == FX_HOLE || kind == FX_KICKBACK || kind == FX_LANES ? 0.7f : 0;
    if (big > 0)
      for (int f = 0; f < V.nflash; f++)
        if (kind != FX_RAMP_DONE || f == (g->tick % V.nflash)) V.flash[f] = fmaxf(V.flash[f], big);
  }
  /* balls */
  for (int i = 0; i < PB_MAXB; i++) {
    const pb_ball *b = &g->W.b[i];
    if (b->state == BS_OFF) { cw_node3d_set_position(V.ball[i], tw(0, -5, 0)); continue; }
    float x = b->pwx + (b->wx - b->pwx) * alpha, y = b->pwy + (b->wy - b->pwy) * alpha, z = b->pwz + (b->wz - b->pwz) * alpha;
    cw_node3d_set_position(V.ball[i], tw(x, y, z + PB_R));
  }
  /* flippers */
  for (int f = 0; f < g->W.nfl; f++) cw_node3d_set_rotation(V.flip[f], (t3_vec3){ 0, g->W.fl[f].ang, 0 });
  /* parts */
  for (int i = 0; i < T->nel; i++) {
    const elem *e = &T->el[i];
    if (!cw_node_valid(V.part[i])) continue;
    if (e->kind == EK_DROP) {
      float target = e->down ? -0.03f : 0;
      V.drop_z[i] += (target - V.drop_z[i]) * fminf(1, dt * (e->down ? 30.0f : 14.0f));
      t3_vec3 c = tw(e->x, e->y, V.drop_z[i]);
      cw_node3d_set_position(V.part[i], c);
      if (V.part_mat[i]) V.part_mat[i]->emissive = lin(th->col[e->col], 0.15f + e->flash * 3);
    } else if (e->kind == EK_TARGET) {
      if (V.part_mat[i]) V.part_mat[i]->emissive = lin(th->col[e->col], 0.15f + e->flash * 3);
      float a = e->ang * CW_PI / 180.0f, k = e->flash * 0.003f;
      cw_node3d_set_position(V.part[i], tw(e->x - cosf(a) * k, e->y - sinf(a) * k, 0));
    } else if (e->kind == EK_SPINNER) {
      float a = e->ang * CW_PI / 180.0f; /* the plate turns about the line across the lane */
      t3_vec3 ax = { cosf(a), 0, -sinf(a) };
      float s = sinf(e->spin * 0.5f), c = cosf(e->spin * 0.5f);
      t3_quat qa = { ax.x * s, ax.y * s, ax.z * s, c }, qy = { 0, sinf(a * 0.5f), 0, cosf(a * 0.5f) };
      cw_node3d_set_quaternion(V.part[i], t3_quat_multiply(qa, qy));
    } else if (e->kind == EK_POP && V.part_mat[i]) {
      V.part_mat[i]->emissive = lin(th->col[e->col], 0.6f + e->flash * 6);
    }
  }
  if (cw_node_valid(V.plunger)) cw_node3d_set_position(V.plunger, tw(0.2375f, -0.065f - g->plunge * 0.035f, 0));
  /* pop lights */
  for (int p = 0; p < T->npop && p < 4; p++) {
    V.pop_flash[p] = fmaxf(0, V.pop_flash[p] - dt * 6);
    if (cw_node_valid(V.pop_light[p])) cw_node_set(V.pop_light[p], "energy", cw_val_float(0.004f + V.pop_flash[p] * 0.05f));
  }
  /* flashers */
  for (int f = 0; f < V.nflash; f++) {
    V.flash[f] = fmaxf(0, V.flash[f] - dt * 3.5f);
    float k = V.flash[f] > 0 ? V.flash[f] * (0.6f + 0.4f * ((int)(V.t * 30) & 1)) : 0;
    cw_node_set(V.flash_light[f], "energy", cw_val_float(k * 0.4f));
    const decor *d = NULL;
    int n = 0;
    for (int i = 0; i < T->ndec; i++) if (T->dec[i].kind == DC_FLASHER && n++ == f) d = &T->dec[i];
    if (d) V.flash_mat[f]->emissive = lin(th->col[d->col], 0.3f + k * 12);
  }
  /* GI: dims on a tilt, flickers when the ball drains */
  float gi = g->tilted ? 0.15f : g->state == GS_BONUS ? 0.6f : 1.0f;
  for (int i = 0; i < NGI; i++) cw_node_set(V.gi[i], "energy", cw_val_float(0.035f * gi));
  /* inserts: brightness follows the rules' lamp states, smoothed like a bulb */
  int nl = 0;
  static int order[MAXLAMP];
  V.st.lamps_lit = 0;
  for (int i = 0; i < T->nlamp; i++) {
    const lamp *l = &T->lamps[i];
    float want = game_flash(g, i), *lv = &V.lamp_level[i];
    *lv += (want - *lv) * fminf(1, dt * (want > *lv ? 40.0f : 14.0f));
    t3_instanced_mesh *im = V.lamp_im[l->shape];
    if (!im) continue;
    float k = 0.25f + *lv * 3.2f; /* lit: the insert colour run past one into the bloom, still coloured */
    t3_instanced_mesh_set_color_at(im, V.lamp_slot[i], lin(th->col[l->col], k));
    if (*lv > 0.3f) order[nl++] = i, V.st.lamps_lit++;
  }
  for (int s = 0; s < NSHAPES; s++) if (V.lamp_im[s]) V.lamp_im[s]->instance_color->version++;
  /* the brightest lit inserts each light the table round them */
  for (int i = 0; i < NLAMPLIGHT; i++) {
    if (i < nl) {
      const lamp *l = &T->lamps[order[i]];
      cw_node3d_set_position(V.lamp_light[i], tw(l->x, l->y, 0.01f));
      cw_node_set(V.lamp_light[i], "color", cw_val_color(th->col[l->col][0], th->col[l->col][1], th->col[l->col][2], 1));
      cw_node_set(V.lamp_light[i], "energy", cw_val_float(V.lamp_level[order[i]] * 0.004f * (l->size / 0.01f)));
    } else cw_node_set(V.lamp_light[i], "energy", cw_val_float(0));
  }
  V.st.lights = NGI + V.nflash + T->npop + (nl < NLAMPLIGHT ? nl : NLAMPLIGHT);
}

void view_camera(const game *g, cw_node camera, float aspect, float dt) {
  /* a tilted view of the whole table from behind the flippers, as a player stands; it leans up the table a little
   * while a ball is high, and back for the flippers */
  float high = 0;
  int n = 0;
  for (int i = 0; i < PB_MAXB; i++)
    if (g->W.b[i].state != BS_OFF) high += g->W.b[i].wy, n++;
  high = n ? high / (float)n : 0.3f;
  float want = cw_clampf((high - 0.35f) / 0.6f, 0, 1);
  V.cam_follow += (want - V.cam_follow) * fminf(1, dt * 1.5f);
  float f = V.cam_follow * 0.06f;
  /* the table's shove when nudged */
  float sh = g->nudge_t > 0 ? (float)g->nudge_t / 18.0f : 0;
  float nx = g->nudge_x * sh * 0.004f, ny = sh * 0.003f;
  float k = g_cam_k;
  V.eye = tw(-0.017f + nx, -0.56f * k + f * 0.5f + ny, 1.0f * k);
  V.at = tw(-0.017f + nx, 0.45f + f + ny, 0);
  V.aspect = aspect;
  cw_node3d_set_position(camera, V.eye);
  cw_node3d_look_at(camera, V.at, (t3_vec3){ 0, 1, 0 });
  cw_node_set(camera, "fov", cw_val_float(V.fov));
  cw_node_set(camera, "near", cw_val_float(0.05f));
  cw_node_set(camera, "far", cw_val_float(6));
  V.st.cam_y = f;
}

bool view_screen(float x, float y, float z, float *u, float *v) {
  if (V.aspect <= 0) return false;
  t3_vec3 p = tw(x, y, z);
  t3_vec3 f = { V.at.x - V.eye.x, V.at.y - V.eye.y, V.at.z - V.eye.z };
  float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
  f.x /= fl, f.y /= fl, f.z /= fl;
  t3_vec3 r = { -f.z, 0, f.x }; /* right: f x up */
  float rl = sqrtf(r.x * r.x + r.z * r.z);
  r.x /= rl, r.z /= rl;
  t3_vec3 up = { r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
  t3_vec3 d = { p.x - V.eye.x, p.y - V.eye.y, p.z - V.eye.z };
  float zz = d.x * f.x + d.y * f.y + d.z * f.z;
  if (zz <= 0.01f) return false;
  float th = tanf(V.fov * CW_PI / 360.0f);
  float sx = (d.x * r.x + d.y * r.y + d.z * r.z) / (zz * th * V.aspect), sy = (d.x * up.x + d.y * up.y + d.z * up.z) / (zz * th);
  *u = 0.5f + 0.5f * sx, *v = 0.5f - 0.5f * sy;
  return true;
}

view_stats view_get_stats(void) { return V.st; }
