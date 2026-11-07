/* skate view: draws the run. Presentation only: nothing here feeds the simulation.
 *
 *   the world  a sunny sky (ProceduralSky driving the sun), the sun's shadow map over the whole park (one map, not one
 *              per view: the park holds still), a light haze, filmic tone mapping.
 *   the park   park.c's own triangles, one mesh per surface kind (concrete, plywood, paint, steel, ...), tinted by their
 *              vertex colours; the tokens are spinning gold rings, hidden in each player's view once that player has
 *              taken them (the pipeline's view hook).
 *   skaters    a rig per seat: the rider (a skinned glb, posed by pose.c from the board's state) standing on a board
 *              built here (deck, grip, trucks, wheels in the seat's colour). Both follow the simulation between ticks;
 *              a flip turns the board under the rider, a grab lifts it to the hand.
 *   cameras    one per player: behind and above the skater along its run, held still through vert air (the rider
 *              comes back down into view), pulled in when a wall is between it and the rider. Never shaken. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/environment.h"
#include "cartwheel/lightmap.h"
#include "cartwheel/vfs.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/resource.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "view.h"

const char *const SKIN_NAMES[NSKINS] = { "Kai", "Nova", "Dex", "Marlo" };
const char *const SKIN_FILES[NSKINS] = { "models/kai.glb", "models/nova.glb", "models/dex.glb", "models/marlo.glb" };
const float PLAYER_COL[MAXP][3] = { { 1.0f, 0.45f, 0.12f }, { 0.15f, 0.7f, 1.0f }, { 0.55f, 0.95f, 0.25f }, { 1.0f, 0.3f, 0.75f } };

int g_view_shadow_size = 4096;
view_metrics g_view_metrics;
float g_cam_k = 1, g_cam_side = 0; /* development and key art: a closer camera, off to one side */

typedef struct seat {
  cw_node root, board, rider, sparks;
  bool sparking;
  t3_object *model;
  rig rig;
  int skin;
  /* the camera */
  v3 cam_p, cam_h, cam_at;
  bool cam_init;
  v3 cam_fwd, last_p;
  bool cam_fwd_ok;
  float fov, air_y0, low_y; /* the lens (wider with speed); where vert air took off; the camera's height held through it */
  int last_st;
  bool blocked;
  float look_k;
  float swing_side; /* which way the camera last swung round the rider (+1 or -1) */
  t3_object *wheel[4];
} seat;

static struct {
  cw_node root, world, park, sun, sky, env, fog, post, light, dome;
  cw_node baked;          /* the park's own meshes: lightmapped (its bake, lightmaps/park<n>.cwlm) */
  cw_lightmap_data *lm;
  cw_node dust[MAXP];     /* a landing's puff */
  int landed[MAXP];
  float wheel_a[MAXP];    /* how far each board's wheels have turned */
  cw_node cam[MAXP];
  t3_material *mat[MAT_KINDS];
  cw_res skin_res[NSKINS];
  seat seat[MAXP];
  int players;
  cw_node token[PARK_MAXTOKEN];
  int ntoken;
  uint32_t token_seen[MAXP]; /* each player's taken tokens, for the view hook */
  const park *k;
  v3 park_c;
  float park_r;
  int built_park;
} VW;

cw_node view_camera(int i) { return VW.cam[i]; }

/* ── materials ────────────────────────────────────────────────────────── */

static t3_material *park_material(int kind) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->vertex_colors = true;
  float ns = 0.8f, ao = 0.9f;
  m->roughness = 1.0f;
  m->metalness = 0;
  if (kind == MAT_METAL) m->metalness = 0.85f, ns = 0.4f;
  if (kind == MAT_GLASS) m->roughness = 0.35f, ns = 0.6f;
  if (kind == MAT_PAINT) ns = 0.5f;
  if (kind == MAT_LEAF) m->side = T3_DOUBLE_SIDE;
  tex_apply(m, kind, ns, ao);
  return m;
}

/* ── the board ────────────────────────────────────────────────────────── */

/* a box into a builder (centre, half sizes along x y z), its own colour */
static void bx(mesh_build *b, v3 c, v3 h, const float col[3]) {
  static const float N[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
  for (int f = 0; f < 6; f++) {
    v3 n = V(N[f][0], N[f][1], N[f][2]);
    v3 a = fabsf(n.y) > 0.5f ? V(1, 0, 0) : V(0, 1, 0), bb = vcross(n, a);
    float ha = fabsf(a.x) * h.x + fabsf(a.y) * h.y + fabsf(a.z) * h.z, hb = fabsf(bb.x) * h.x + fabsf(bb.y) * h.y + fabsf(bb.z) * h.z;
    float hn = fabsf(n.x) * h.x + fabsf(n.y) * h.y + fabsf(n.z) * h.z;
    v3 o = vadd(c, vmul(n, hn));
    int v[4];
    const float S[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int i = 0; i < 4; i++) {
      v[i] = mb_vert(b, vadd(o, vadd(vmul(a, S[i][0] * ha), vmul(bb, S[i][1] * hb))), n, S[i][0] * 0.5f + 0.5f, S[i][1] * 0.5f + 0.5f);
      t3_color l = lin(col[0], col[1], col[2]);
      mb_color(b, v[i], l.r, l.g, l.b);
    }
    mb_quad(b, v[0], v[1], v[2], v[3]);
  }
}
/* a wheel: a short cylinder across z at c */
static void wheel(mesh_build *b, v3 c, float r, float w, const float col[3]) {
  const int S = 12;
  t3_color l = lin(col[0], col[1], col[2]);
  for (int i = 0; i < S; i++) {
    float a0 = 2 * PI_F * (float)i / S, a1 = 2 * PI_F * (float)(i + 1) / S, s0, c0, s1, c1;
    cw_sincos(a0, &s0, &c0), cw_sincos(a1, &s1, &c1);
    v3 n0 = V(c0, s0, 0), n1 = V(c1, s1, 0);
    int v[4] = { mb_vert(b, vadd(c, V(c0 * r, s0 * r, -w)), n0, 0, 0), mb_vert(b, vadd(c, V(c1 * r, s1 * r, -w)), n1, 1, 0),
                 mb_vert(b, vadd(c, V(c1 * r, s1 * r, w)), n1, 1, 1), mb_vert(b, vadd(c, V(c0 * r, s0 * r, w)), n0, 0, 1) };
    /* a printed band on two of the segments: the spin shows */
    float kk = (i % 6 == 0) ? 0.45f : 1.0f;
    for (int k = 0; k < 4; k++) mb_color(b, v[k], l.r * kk, l.g * kk, l.b * kk);
    mb_quad(b, v[0], v[3], v[2], v[1]);
    for (int sd = -1; sd <= 1; sd += 2) {
      int m = mb_vert(b, vadd(c, V(0, 0, w * (float)sd)), V(0, 0, (float)sd), 0.5f, 0.5f);
      int p = mb_vert(b, vadd(c, V(c0 * r, s0 * r, w * (float)sd)), V(0, 0, (float)sd), 0, 0);
      int q = mb_vert(b, vadd(c, V(c1 * r, s1 * r, w * (float)sd)), V(0, 0, (float)sd), 1, 0);
      float hub = (i % 4 == 0) ? 0.3f : 0.85f; /* the hub's spokes of shade */
      mb_color(b, m, l.r * 0.5f, l.g * 0.5f, l.b * 0.5f), mb_color(b, p, l.r * hub, l.g * hub, l.b * hub), mb_color(b, q, l.r * hub, l.g * hub, l.b * hub);
      if (sd > 0) mb_tri(b, m, p, q);
      else mb_tri(b, m, q, p);
    }
  }
}
/* the deck: a curved plank (kicktails at both ends), nose along +x, its top at y = DECK_Y */

static t3_object *board_model(const float col[3], t3_object *wheels[4]) {
  mesh_build deck = { 0 }, grip = { 0 }, hw = { 0 }, wb = { 0 };
  const int NS = 16;
  const float L = 0.40f, W = 0.105f, T = 0.012f;
  /* along the length: the deck's height (kicktails rise over the last 13 cm) */
  for (int i = 0; i < NS; i++) {
    float x0 = -L + 2 * L * (float)i / NS, x1 = -L + 2 * L * (float)(i + 1) / NS;
    float y0 = DECK_Y - T + 0.06f * cw_pow(clampf((fabsf(x0) - 0.27f) / 0.13f, 0, 1), 1.6f), y1 = DECK_Y - T + 0.06f * cw_pow(clampf((fabsf(x1) - 0.27f) / 0.13f, 0, 1), 1.6f);
    /* round the ends: the width narrows over the last 6 cm */
    float w0 = W * (fabsf(x0) > 0.34f ? cw_pow(1 - (fabsf(x0) - 0.34f) / 0.07f, 0.5f) : 1), w1 = W * (fabsf(x1) > 0.34f ? cw_pow(1 - clampf((fabsf(x1) - 0.34f) / 0.07f, 0, 0.98f), 0.5f) : 1);
    v3 n = vnorm(V(-(y1 - y0), x1 - x0, 0));
    /* the top (grip) and the bottom (the graphic in the seat's colour) */
    int a = mb_vert(&grip, V(x0, y0 + T, -w0), n, x0, -w0), b2 = mb_vert(&grip, V(x1, y1 + T, -w1), n, x1, -w1), c = mb_vert(&grip, V(x1, y1 + T, w1), n, x1, w1), d = mb_vert(&grip, V(x0, y0 + T, w0), n, x0, w0);
    mb_quad(&grip, a, d, c, b2);
    t3_color gc = lin(col[0], col[1], col[2]), wc = lin(0.95f, 0.92f, 0.85f);
    bool stripe = (i == 4 || i == 11);
    int e = mb_vert(&deck, V(x0, y0, -w0), vmul(n, -1), 0, 0), f = mb_vert(&deck, V(x1, y1, -w1), vmul(n, -1), 1, 0), g = mb_vert(&deck, V(x1, y1, w1), vmul(n, -1), 1, 1),
        h = mb_vert(&deck, V(x0, y0, w0), vmul(n, -1), 0, 1);
    t3_color cc = stripe ? wc : gc;
    mb_color(&deck, e, cc.r, cc.g, cc.b), mb_color(&deck, f, cc.r, cc.g, cc.b), mb_color(&deck, g, cc.r, cc.g, cc.b), mb_color(&deck, h, cc.r, cc.g, cc.b);
    mb_quad(&deck, e, f, g, h);
    /* the edges: plywood */
    t3_color ec = lin(0.85f, 0.7f, 0.5f);
    for (int sd = -1; sd <= 1; sd += 2) {
      float ww0 = w0 * (float)sd, ww1 = w1 * (float)sd;
      int p = mb_vert(&deck, V(x0, y0, ww0), V(0, 0, (float)sd), 0, 0), q = mb_vert(&deck, V(x1, y1, ww1), V(0, 0, (float)sd), 1, 0), r = mb_vert(&deck, V(x1, y1 + T, ww1), V(0, 0, (float)sd), 1, 1),
          s = mb_vert(&deck, V(x0, y0 + T, ww0), V(0, 0, (float)sd), 0, 1);
      mb_color(&deck, p, ec.r, ec.g, ec.b), mb_color(&deck, q, ec.r, ec.g, ec.b), mb_color(&deck, r, ec.r, ec.g, ec.b), mb_color(&deck, s, ec.r, ec.g, ec.b);
      if (sd > 0) mb_quad(&deck, p, q, r, s);
      else mb_quad(&deck, p, s, r, q);
    }
  }
  /* trucks and wheels */
  static const float steel[3] = { 0.78f, 0.8f, 0.84f }, urethane[3] = { 1.0f, 0.95f, 0.75f };
  for (int t = -1; t <= 1; t += 2) {
    float x = 0.21f * (float)t;
    bx(&hw, V(x, 0.075f, 0), V(0.03f, 0.016f, 0.03f), steel);    /* the baseplate and kingpin */
    bx(&hw, V(x, 0.045f, 0), V(0.014f, 0.014f, 0.09f), steel);   /* the hanger and axle */
  }
  wheel(&wb, V(0, 0, 0), 0.028f, 0.018f, urethane); /* one wheel, four times: each turns on its axle */
  t3_object *g = t3_group_new();
  t3_material *md = t3_mesh_standard_material_new(0xffffff);
  md->vertex_colors = true, md->roughness = 0.45f;
  t3_material *mg = t3_mesh_standard_material_new(0x222326);
  mg->roughness = 0.95f;
  tex_apply(mg, MAT_ASPHALT, 1.2f, 0.6f);
  mg->color = lin(0.16f, 0.16f, 0.17f);
  t3_material *mh = t3_mesh_standard_material_new(0xffffff);
  mh->vertex_colors = true, mh->roughness = 0.35f, mh->metalness = 0.3f;
  t3_mesh *m1 = t3_mesh_new(mb_finish(&deck), md), *m2 = t3_mesh_new(mb_finish(&grip), mg), *m3 = t3_mesh_new(mb_finish(&hw), mh);
  t3_mesh *ms[3] = { m1, m2, m3 };
  for (int i = 0; i < 3; i++) {
    ms[i]->base.cast_shadow = true, ms[i]->base.receive_shadow = true;
    t3_release(ms[i]->geometry);
    t3_object_add(g, ms[i]);
    t3_release(ms[i]);
  }
  t3_geometry *wg = mb_finish(&wb);
  for (int t = 0; t < 4; t++) {
    t3_mesh *w = t3_mesh_new(wg, mh);
    w->base.cast_shadow = true, w->base.receive_shadow = true;
    w->base.position = V(0.21f * ((t & 1) ? 1.0f : -1.0f), 0.028f, 0.085f * ((t & 2) ? 1.0f : -1.0f));
    t3_object_add(g, w);
    wheels[t] = &w->base;
    t3_release(w);
  }
  t3_release(wg);
  t3_release(md), t3_release(mg), t3_release(mh);
  return g;
}

/* ── init ─────────────────────────────────────────────────────────────── */

static void set_shadows(t3_object *o) {
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) o->cast_shadow = !(o->name && !strncmp(o->name, "Hair", 4)), o->receive_shadow = true;
  for (int i = 0; i < o->child_count; i++) set_shadows(o->children[i]);
}

/* the sky behind everything: a dome of colour from a deep blue overhead to a pale blue at the horizon, and a few
 * flat-bottomed clouds (the ProceduralSky still lights the park and fills the reflections; its own background is off:
 * a physical sky at this exposure reads grey, and this game's sky is a postcard's) */
static void sky_dome(cw_node parent, const scenery_mood *md) {
  if (cw_node_valid(VW.dome)) cw_node_free(VW.dome);
  mesh_build b = { 0 };
  /* the dome is unlit: it holds its brightness whatever the park's exposure, a bright sky over a darker ground */
  float sk = 2.5f * cw_pow(2.0f, -md->exposure), ck = sk * 1.1f;
  sk /= 1.0f - DITHER_K; /* (the dither's mean, given back) */
  const int AZ = 48, EL = 40;
  const float R = 380;
  for (int j = 0; j <= EL; j++) {
    float e = -0.12f + (PI_F * 0.5f + 0.12f) * (float)j / EL, se, ce;
    cw_sincos(e, &se, &ce);
    float k = clampf(se, 0, 1);
    /* horizon pale, zenith deep; a warm whisper at the very bottom */
    /* the mood's horizon up to its zenith, quickly: most of the sky is near the zenith's colour */
    float kk = cw_pow(k, 0.35f); /* a thin band of the horizon's colour, the rest the zenith's */
    float r = md->horizon[0] + (md->zenith[0] - md->horizon[0]) * kk, g = md->horizon[1] + (md->zenith[1] - md->horizon[1]) * kk, bl = md->horizon[2] + (md->zenith[2] - md->horizon[2]) * kk;
    if (se < 0) r = md->fog[0], g = md->fog[1], bl = md->fog[2];
    t3_color c = lin(r, g, bl);
    for (int i = 0; i <= AZ; i++) {
      float a = 2 * PI_F * (float)i / AZ, sa, ca;
      cw_sincos(a, &sa, &ca);
      v3 p = V(ca * ce * R, se * R, sa * ce * R);
      int v = mb_vert(&b, p, vmul(vnorm(p), -1), (float)i / AZ * 72, (e + 0.12f) / (PI_F * 0.5f + 0.12f) * 20); /* (the dither's texels: finer than a pixel) */
      mb_color(&b, v, c.r * sk, c.g * sk, c.b * sk);
    }
  }
  for (int j = 0; j < EL; j++)
    for (int i = 0; i < AZ; i++) {
      int a = j * (AZ + 1) + i;
      mb_quad(&b, a, a + 1, a + AZ + 2, a + AZ + 1);
    }
  /* clouds: cumulus cards (the cloud texture, soft edged, sunlit tops) at 300 m, a few overlapping in each bank */
  mesh_build cb = { 0 };
  uint32_t h = 977;
  t3_color tint = lin(md->cloud[0], md->cloud[1], md->cloud[2]);
  for (int c = 0; c < 10; c++) {
    h = h * 1664525u + 1013904223u;
    float az = (float)(h >> 8) / 16777216.0f * 2 * PI_F;
    h = h * 1664525u + 1013904223u;
    float el = 0.07f + 0.3f * (float)(h >> 8) / 16777216.0f;
    for (int p = 0; p < 2; p++) {
      h = h * 1664525u + 1013904223u;
      float da = ((float)(h >> 8) / 16777216.0f - 0.5f) * 0.22f, de = ((float)((h >> 4) & 255) / 255.0f) * 0.03f;
      float sa, ca, se, ce;
      cw_sincos(az + da, &sa, &ca), cw_sincos(el + de, &se, &ce);
      float R = 300 - (float)p * 4;
      v3 cen = V(ca * ce * R, se * R, sa * ce * R);
      v3 fw = vnorm(vmul(cen, -1)), right = vnorm(vcross(fw, V(0, 1, 0))), up = vcross(right, fw);
      float wx = 34 + 26 * (float)((h >> 12) & 255) / 255.0f, wy = wx * 0.42f;
      v3 P[4] = { vsub(cen, vmul(right, wx)), vadd(cen, vmul(right, wx)), vadd(vadd(cen, vmul(right, wx)), vmul(up, wy * 2)), vadd(vsub(cen, vmul(right, wx)), vmul(up, wy * 2)) };
      static const float UV[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
      int v[4];
      float k = ck * (0.9f + 0.1f * (float)p);
      for (int i = 0; i < 4; i++) v[i] = mb_vert(&cb, P[i], fw, (p & 1) ? 1 - UV[i][0] : UV[i][0], UV[i][1]), mb_color(&cb, v[i], tint.r * k, tint.g * k, tint.b * k);
      mb_quad(&cb, v[0], v[1], v[2], v[3]);
    }
  }
  /* dithered: a smooth gradient over a few levels of the screen's 8 bits bands (a sunset's most); noise finer than a
     pixel, a level and a half deep, breaks the bands up. (Drawn with the transparent pass's program, the clouds' own:
     first of the transparent things, behind everything the depth test keeps in front of it.) */
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  m->side = T3_DOUBLE_SIDE;
  m->depth_write = false;
  m->fog = false;
  if (!getenv("SKATE_NO_DITHER")) m->transparent = true, t3_material_set_texture(m, T3_MAP, tex_dither());
  t3_geometry *g = mb_finish(&b);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false, me->base.receive_shadow = false;
  me->base.frustum_culled = false;
  me->base.render_order = -100;
  cw_node n = cw_node_new("Node3D", "SkyDome");
  cw_node_add_child(parent, n);
  {
    t3_material *cm = t3_mesh_basic_material_new(0xffffff);
    cm->vertex_colors = true, cm->side = T3_DOUBLE_SIDE, cm->transparent = true, cm->depth_write = false, cm->fog = false;
    t3_material_set_texture(cm, T3_MAP, tex_get(TEX_CLOUD)->map);
    t3_geometry *cg = mb_finish(&cb);
    t3_mesh *cme = t3_mesh_new(cg, cm);
    cme->base.cast_shadow = false, cme->base.receive_shadow = false, cme->base.frustum_culled = false, cme->base.render_order = -99;
    t3_object_add(&me->base, &cme->base);
    t3_release(cme), t3_release(cg), t3_release(cm);
  }
  cw_node3d_set_object(n, &me->base);
  t3_release(me), t3_release(g), t3_release(m);
  VW.dome = n;
}
static void view_hook(int view, void *u) {
  /* the sky dome around each view's own camera */
  if (cw_node_valid(VW.dome) && view >= 0 && view < MAXP) {
    /* (the scene's matrices are made once, with the first view: a later view's move updates its own) */
    v3 c = cw_node3d_global_position(VW.cam[view]);
    cw_node3d_set_position(VW.dome, V(c.x, 0, c.z));
    t3_object *o = cw_node3d_object(VW.dome);
    if (o) o->position = V(c.x, 0, c.z), t3_object_update_world_matrix(o, true, true);
  }
  (void)u;
  for (int t = 0; t < VW.ntoken; t++)
    if (cw_node_valid(VW.token[t])) cw_node_set(VW.token[t], "visible", cw_val_bool(view >= MAXP || !(VW.token_seen[view] & (1u << t))));
}

bool view_init_step(cw_node root, int *done, int *total) {
  enum { S_ROOT, S_TEX, S_MATS = S_TEX + MAT_KINDS * 8, S_SKINS, S_DONE = S_SKINS + NSKINS };
  static int step, tex_k;
  *total = S_DONE;
  *done = step;
  if (step == S_ROOT) {
    memset(&VW, 0, sizeof VW);
    VW.root = root;
    VW.world = cw_node_new("Node3D", "World");
    cw_node_add_child(root, VW.world);
    VW.env = cw_node_new("WorldEnvironment", "Env");
    cw_node_add_child(root, VW.env);
    cw_node_set(VW.env, "use_background", cw_val_bool(false));
    cw_node_set(VW.env, "hemi_energy", cw_val_float(0.35f));
    cw_node_set(VW.env, "sky_color", cw_val_color(0.55f, 0.7f, 1.0f, 1));
    cw_node_set(VW.env, "ground_color", cw_val_color(0.5f, 0.45f, 0.38f, 1));
    VW.sun = cw_node_new("DirectionalLight3D", "Sun");
    cw_node_add_child(root, VW.sun);
    cw_node_set(VW.sun, "shadow", cw_val_bool(true));
    cw_node_set(VW.sun, "shadow_size", cw_val_int(g_view_shadow_size));
    cw_node_set(VW.sun, "shadow_extent", cw_val_float(60));
    cw_node_set(VW.sun, "shadow_far", cw_val_float(260));
    cw_node_set(VW.sun, "shadow_bias", cw_val_float(-0.0004f));
    cw_node_set(VW.sun, "shadow_normal_bias", cw_val_float(0.03f));
    VW.sky = cw_node_new("ProceduralSky", "Sky");
    cw_node_add_child(root, VW.sky);
    cw_node_set(VW.sky, "sun", cw_val_string("../Sun"));
    cw_node_set(VW.sky, "time_of_day", cw_val_float(14.5f));
    cw_node_set(VW.sky, "latitude", cw_val_float(34));
    cw_node_set(VW.sky, "declination", cw_val_float(18));
    cw_node_set(VW.sky, "north", cw_val_float(30));
    cw_node_set(VW.sky, "mie", cw_val_float(0.3f));
    cw_node_set(VW.sky, "rayleigh", cw_val_float(1.25f));
    cw_node_set(VW.sky, "sun_energy", cw_val_float(4.2f));
    cw_node_set(VW.sky, "background", cw_val_bool(false));
    VW.fog = cw_node_new("HeightFog", "Haze");
    cw_node_add_child(root, VW.fog);
    cw_node_set(VW.fog, "density", cw_val_float(0.0009f));
    cw_node_set(VW.fog, "height_falloff", cw_val_float(0.06f));
    cw_node_set(VW.fog, "start_distance", cw_val_float(90));
    cw_node_set(VW.fog, "sun_scatter", cw_val_float(0.2f));
    cw_node_set(VW.fog, "use_sky_color", cw_val_bool(false));
    cw_node_set(VW.fog, "sky_distance", cw_val_float(90)); /* the sky stays blue: the haze is on the ground's distance, not on it */
    cw_node_set(VW.fog, "color", cw_val_color(0.55f, 0.7f, 0.95f, 1));
    VW.post = cw_node_new("PostProcess", "Post");
    cw_node_add_child(root, VW.post);
    cw_node_set(VW.post, "hdr", cw_val_bool(true));
    cw_node_set(VW.post, "msaa", cw_val_int(4));
    cw_node_set(VW.post, "bloom", cw_val_bool(false)); /* a sunny afternoon has little to bloom: the 0.8 ms goes to the four views */
    cw_node_set(VW.post, "bloom_intensity", cw_val_float(0.05f));
    cw_node_set(VW.post, "bloom_threshold", cw_val_float(2.5f));
    cw_node_set(VW.post, "exposure_ev", cw_val_float(0.0f));
    cw_node_set(VW.post, "tonemap", cw_val_enum("ACES_FILMIC")); /* contrast and colour: the sky stays blue, the shadows dark */
    cw_node_set(VW.post, "vignette", cw_val_bool(true)); /* a light frame on each view: the eye goes to the middle */
    cw_node_set(VW.post, "vignette_intensity", cw_val_float(0.42f));
    cw_node_set(VW.post, "vignette_smoothness", cw_val_float(0.55f));
    /* the grade (tools/make-lut.mjs): a gentle S-curve and a little saturation after the tone curve */
    if (!getenv("SKATE_NO_GRADE")) {
      cw_node_set(VW.post, "lut", cw_val_string("grade.cube"));
      cw_node_set(VW.post, "grade", cw_val_bool(true));
      cw_node_set(VW.post, "grade_intensity", cw_val_float(1.0f));
    }
    cw_node_set(VW.sky, "sky_energy", cw_val_float(1.9f));
    if (getenv("SKATE_SKY")) { /* development: SKATE_SKY=sky_energy,mie,rayleigh,exposure_ev,time */
      float se = 1.9f, mi = 0.3f, ra = 1.25f, ev = 0, tod = 14.5f;
      sscanf(getenv("SKATE_SKY"), "%f,%f,%f,%f,%f", &se, &mi, &ra, &ev, &tod);
      cw_node_set(VW.sky, "sky_energy", cw_val_float(se)), cw_node_set(VW.sky, "mie", cw_val_float(mi)), cw_node_set(VW.sky, "rayleigh", cw_val_float(ra));
      cw_node_set(VW.post, "exposure_ev", cw_val_float(ev)), cw_node_set(VW.sky, "time_of_day", cw_val_float(tod));
    }
    VW.light = cw_node_new("LightingSettings", "Lighting");
    cw_node_add_child(root, VW.light);
    cw_node_set(VW.light, "mode", cw_val_enum("CLUSTERED"));
    for (int i = 0; i < MAXP; i++) {
      char nm[16];
      snprintf(nm, sizeof nm, "Cam%d", i + 1);
      VW.cam[i] = cw_node_new("Camera3D", nm);
      cw_node_set(VW.cam[i], "fov", cw_val_float(68));
      cw_node_set(VW.cam[i], "near", cw_val_float(0.1f));
      cw_node_set(VW.cam[i], "far", cw_val_float(420));
      cw_node_set(VW.cam[i], "current", cw_val_bool(i == 0));
      cw_node_add_child(root, VW.cam[i]);
    }
    cw_pipeline_set_view_hook(cw_pipeline_of(cw_scene_app_tree()), view_hook, NULL);
    VW.built_park = -1;
    tex_k = 0;
  } else if (step >= S_TEX && step < S_MATS) {
    /* the textures, a band of rows at a time */
    while (tex_k < TEX_KINDS && tex_warm(tex_k)) tex_k++;
    if (tex_k < TEX_KINDS) tex_warm(tex_k);
    if (step == S_MATS - 1) while (tex_k < TEX_KINDS) { while (!tex_warm(tex_k)) {} tex_k++; }
  } else if (step == S_MATS) {
    for (int k = 0; k < MAT_KINDS; k++) VW.mat[k] = park_material(k);
  } else if (step >= S_SKINS && step < S_DONE) {
    int s = step - S_SKINS;
    VW.skin_res[s] = cw_res_load(SKIN_FILES[s], CW_RES_GLTF);
    if (!cw_res_gltf(VW.skin_res[s])) cw_error("skate: %s did not load", SKIN_FILES[s]);
  }
  step++;
  *done = step;
  return step >= S_DONE;
}

/* ── the park ─────────────────────────────────────────────────────────── */

static t3_geometry *token_geo(void) {
  /* a gold gem standing on its point */
  t3_geometry *g = t3_octahedron_geometry_new(0.42f, 0);
  t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 1.5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } }); /* a tall gem: it reads from every side */
  return g;
}

/* the park's light and air: its sky, sun, haze and exposure (scenery.c's moods) */
static void apply_mood(const scenery_mood *md) {
  cw_node_set(VW.sky, "time_of_day", cw_val_float(md->time));
  cw_node_set(VW.sky, "north", cw_val_float(md->north));
  cw_node_set(VW.sky, "sun_energy", cw_val_float(md->sun_energy));
  cw_node_set(VW.sky, "sky_energy", cw_val_float(md->sky_energy));
  cw_node_set(VW.sky, "mie", cw_val_float(md->mie));
  cw_node_set(VW.sky, "rayleigh", cw_val_float(md->rayleigh));
  /* (SKATE_EXPOSURE: stops more, the look gate's control) */
  cw_node_set(VW.post, "exposure_ev", cw_val_float(md->exposure + (getenv("SKATE_EXPOSURE") ? (float)atof(getenv("SKATE_EXPOSURE")) : 0)));
  cw_node_set(VW.env, "hemi_energy", cw_val_float(md->hemi * (getenv("SKATE_HEMIK") ? (float)atof(getenv("SKATE_HEMIK")) : 1)));
  if (getenv("SKATE_TONE")) cw_node_set(VW.post, "tonemap", cw_val_enum(getenv("SKATE_TONE"))); /* development: the look gate's matrix */
  cw_node_set(VW.env, "sky_color", cw_val_color(md->zenith[0] * 0.6f + 0.3f, md->zenith[1] * 0.6f + 0.3f, md->zenith[2] * 0.6f + 0.35f, 1));
  cw_node_set(VW.fog, "color", cw_val_color(md->fog[0], md->fog[1], md->fog[2], 1));
  cw_node_set(VW.fog, "density", cw_val_float(md->fog_density));
  sky_dome(VW.root, md);
}

/* decals on the park: wax along the ledges and the coping, our own tags and stickers on its walls, skid marks on its
 * ground. Each is a quad a hair off its surface, kept only where all four corners lie on that surface. */
static bool on_surface(const park *k, v3 p, v3 n, unsigned mask) {
  phit h;
  if (!park_cast(k, vmad(p, n, 0.06f), vmul(n, -0.12f), mask, &h)) return false;
  return vdot(h.fn, n) > 0.97f && fabsf(h.t - 0.5f) < 0.1f;
}
static void decal_quad(mesh_build *b, v3 c, v3 r, v3 u, v3 n, int cell, float alpha_k) {
  float u0 = (float)(cell & 3) / 4 + 0.004f, u1 = (float)((cell & 3) + 1) / 4 - 0.004f, v0 = (float)(cell >> 2) / 4 + 0.004f, v1 = (float)((cell >> 2) + 1) / 4 - 0.004f;
  int q[4] = { mb_vert(b, vsub(vsub(c, r), u), n, u0, v0), mb_vert(b, vsub(vadd(c, r), u), n, u1, v0), mb_vert(b, vadd(vadd(c, r), u), n, u1, v1), mb_vert(b, vadd(vsub(c, r), u), n, u0, v1) };
  for (int i = 0; i < 4; i++) mb_color(b, q[i], alpha_k, alpha_k, alpha_k);
  mb_quad(b, q[0], q[1], q[2], q[3]);
}
static void park_decals(const park *k) {
  mesh_build b = { 0 };
  uint32_t rng = 9001u + (uint32_t)k->theme * 31u;
#define DRND() (rng = rng * 1664525u + 1013904223u, (float)(rng >> 8) / 16777216.0f)
  /* wax: along each ledge and coping edge, on the top beside the lip */
  for (int i = 0; i < k->ne; i++) {
    const pedge *e = &k->e[i];
    if (e->kind == EDGE_RAIL || e->len < 0.8f) continue;
    v3 side = vnorm(vcross(e->t, e->up));
    float in = 0;
    for (int s = -1; s <= 1; s += 2) {
      v3 p = vadd(vmul(vadd(e->a, e->b), 0.5f), vmul(side, 0.12f * (float)s));
      if (on_surface(k, vsub(p, vmul(e->up, 0.01f)), e->up, 1u << SURF_RIDE)) in = (float)s;
    }
    if (in == 0) continue;
    for (float t = 0.2f; t < e->len - 0.4f; t += 1.0f) {
      float len = fminf(1.0f, e->len - 0.2f - t) * 0.5f;
      v3 c = vadd(vadd(e->a, vmul(e->t, t + len)), vmul(side, in * 0.09f));
      c = vadd(c, vmul(e->up, -0.01f + 0.006f));
      decal_quad(&b, c, vmul(e->t, len), vmul(side, 0.09f), e->up, 12 + (int)(DRND() * 2), 1);
    }
  }
  /* tags and stickers on the walls: a random wall triangle by area, a spot in it */
  float area_sum = 0;
  for (int i = 0; i < k->nt; i++) {
    const ptri *t = &k->t[i];
    if (t->surf != SURF_WALL || fabsf(t->n.y) > 0.2f || (t->mat != MAT_PAINT && t->mat != MAT_CONCRETE && t->mat != MAT_WOOD)) continue;
    area_sum += vlen(vcross(vsub(t->b, t->a), vsub(t->c, t->a))) * 0.5f;
  }
  int placed = 0;
  for (int tries = 0; tries < 900 && placed < 46 && area_sum > 0; tries++) {
    float pick = DRND() * area_sum;
    const ptri *t = NULL;
    for (int i = 0; i < k->nt; i++) {
      const ptri *q = &k->t[i];
      if (q->surf != SURF_WALL || fabsf(q->n.y) > 0.2f || (q->mat != MAT_PAINT && q->mat != MAT_CONCRETE && q->mat != MAT_WOOD)) continue;
      pick -= vlen(vcross(vsub(q->b, q->a), vsub(q->c, q->a))) * 0.5f;
      if (pick <= 0) { t = q; break; }
    }
    if (!t) continue;
    float s = DRND(), r = DRND();
    if (s + r > 1) s = 1 - s, r = 1 - r;
    v3 p = vadd(t->a, vadd(vmul(vsub(t->b, t->a), s), vmul(vsub(t->c, t->a), r)));
    v3 n = t->n, rt = vnorm(vcross(V(0, 1, 0), n)), up = vcross(n, rt);
    bool sticker = DRND() < 0.45f;
    int cell = sticker ? 6 + (int)(DRND() * 6) : (int)(DRND() * 6);
    float w = sticker ? 0.12f + 0.1f * DRND() : 0.5f + 0.6f * DRND(), h = sticker ? w * 0.8f : w * 0.7f;
    if (!sticker && p.y < 0.15f + h) p.y = 0.15f + h + 0.05f;
    v3 c = vmad(p, n, 0.008f);
    v3 R = vmul(rt, w), U = vmul(up, h);
    if (!on_surface(k, vadd(vadd(p, R), U), n, 1u << SURF_WALL) || !on_surface(k, vsub(vadd(p, R), U), n, 1u << SURF_WALL) || !on_surface(k, vadd(vsub(p, R), U), n, 1u << SURF_WALL) ||
        !on_surface(k, vsub(vsub(p, R), U), n, 1u << SURF_WALL))
      continue;
    decal_quad(&b, c, R, U, n, cell, 1);
    placed++;
  }
  /* skids and wax smears on the ground near the obstacles */
  for (int tries = 0; tries < 600 && placed < 110; tries++) {
    v3 p = V(k->lo.x + (k->hi.x - k->lo.x) * DRND(), k->hi.y + 1, k->lo.z + (k->hi.z - k->lo.z) * DRND());
    phit h;
    if (!park_ground(k, p, 0, k->hi.y - k->lo.y + 2, &h) || h.fn.y < 0.98f) continue;
    const ptri *t = &k->t[h.tri];
    if (t->mat != MAT_CONCRETE && t->mat != MAT_ASPHALT && t->mat != MAT_TILE && t->mat != MAT_WOOD) continue;
    float a = DRND() * 6.28f, sa, ca;
    cw_sincos(a, &sa, &ca);
    v3 R = V(ca * 0.9f, 0, sa * 0.9f), U = V(-sa * 0.35f, 0, ca * 0.35f), n = V(0, 1, 0);
    p = h.p;
    if (!on_surface(k, vadd(vadd(p, R), U), n, 1u << SURF_RIDE) || !on_surface(k, vsub(vadd(p, R), U), n, 1u << SURF_RIDE) || !on_surface(k, vadd(vsub(p, R), U), n, 1u << SURF_RIDE) ||
        !on_surface(k, vsub(vsub(p, R), U), n, 1u << SURF_RIDE))
      continue;
    decal_quad(&b, vadd(p, V(0, 0.006f, 0)), R, U, n, DRND() < 0.7f ? 14 + (int)(DRND() * 2) : 12, 1);
    placed++;
  }
#undef DRND
  if (!b.nv) { mb_free(&b); return; }
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->vertex_colors = true;
  m->transparent = true;
  m->depth_write = false;
  m->roughness = 0.6f;
  m->side = T3_DOUBLE_SIDE; /* (the wax and the skids lie either way up) */
  const tex_set *ts = tex_get(TEX_DECALS);
  t3_material_set_texture(m, T3_MAP, ts->map);
  t3_material_set_texture(m, T3_ROUGHNESS_MAP, ts->rough);
  t3_geometry *g = mb_finish(&b);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false, me->base.receive_shadow = true;
  me->base.render_order = 5;
  cw_node n = cw_node_new("Node3D", "Decals");
  cw_node_add_child(VW.park, n);
  cw_node3d_set_object(n, &me->base);
  t3_release(me), t3_release(g), t3_release(m);
}

static int bake_settings(cw_lightmap_settings *s) {
  *s = cw_lightmap_settings_default();
  s->texel_density = 3.0f;
  s->max_size = 1024;
  s->samples = 48;
  s->bounces = 1;
  s->probe_spacing = 0;
  s->denoise = 2;
  s->direct_samples = 1;
  return 0;
}

void view_park(const park *k) {
  if (cw_node_valid(VW.park)) cw_node_free(VW.park);
  if (VW.lm) cw_lightmap_free(VW.lm), VW.lm = NULL;
  VW.park = cw_node_new("Node3D", "Park");
  cw_node_add_child(VW.world, VW.park);
  VW.baked = cw_node_new("Node3D", "Baked");
  cw_node_add_child(VW.park, VW.baked);
  VW.k = k;
  const scenery_mood *md = scenery_mood_of(k->theme);
  apply_mood(md);
  for (int m = 0; m < MAT_KINDS; m++) {
    mesh_build b = { 0 };
    float inv = 1.0f / MAT_TILE_M[m];
    for (int i = 0; i < k->nt; i++) {
      const ptri *t = &k->t[i];
      if (t->mat != m) continue;
      t3_color c = lin(t->col[0], t->col[1], t->col[2]);
      const v3 *P[3] = { &t->a, &t->b, &t->c }, *N[3] = { &t->na, &t->nb, &t->nc };
      int v[3];
      for (int j = 0; j < 3; j++) {
        /* walls and sides keep the face's own normal: only ride surfaces are smooth (their curves) */
        v3 n = t->surf == SURF_RIDE ? *N[j] : t->n;
        if (t->surf == SURF_NONE) n = *N[j];
        v[j] = mb_vert(&b, *P[j], n, t->uvs[j * 2] * inv, t->uvs[j * 2 + 1] * inv);
        mb_color(&b, v[j], c.r, c.g, c.b);
      }
      mb_tri(&b, v[0], v[1], v[2]);
    }
    if (!b.nv) { mb_free(&b); continue; }
    t3_geometry *g = mb_finish(&b);
    t3_mesh *me = t3_mesh_new(g, VW.mat[m]);
    me->base.cast_shadow = m != MAT_GLASS;
    me->base.receive_shadow = true;
    /* the park's own surfaces are baked (sky light, its occlusion, the sun's bounce): one lightmap each */
    char nm[16];
    snprintf(nm, sizeof nm, "S%d", m);
    cw_node n = cw_node_new("MeshInstance3D", nm);
    cw_node_add_child(VW.baked, n);
    bool baked = m != MAT_LEAF && m != MAT_GLASS;
    cw_node_set(n, "gi_mode", cw_val_enum(baked ? "STATIC" : "DYNAMIC"));
    cw_node_set(n, "cast_shadow", cw_val_bool(m != MAT_GLASS));
    cw_node_set(n, "receive_shadow", cw_val_bool(true));
    cw_mesh3d_set_procedural(n, me);
    t3_release(me), t3_release(g);
  }
  if (!getenv("SKATE_NO_DECALS")) park_decals(k); /* (development switches: what each part costs) */
  if (!getenv("SKATE_NO_SCENERY")) scenery_build(VW.park, k, k->theme);
  if (!getenv("SKATE_BAKE")) {
    char path[48];
    snprintf(path, sizeof path, "lightmaps/park%d.cwlm", k->theme);
    VW.lm = cw_vfs_exists(path) ? cw_lightmap_load(path) : NULL;
    if (VW.lm) cw_lightmap_apply_shaded(cw_scene_app_tree(), VW.baked, VW.lm, md->lm_gain * (getenv("SKATE_LMK") ? (float)atof(getenv("SKATE_LMK")) : 1), CW_LIGHTMAP_LIT);
    else cw_error("skate: park %d has no bake (%s): it draws unbaked", k->theme, path);
  }
  /* the tokens */
  t3_material *tm = t3_mesh_standard_material_new(0xffc020);
  tm->emissive = lin(1.0f, 0.62f, 0.1f);
  tm->emissive_intensity = 2.2f * fminf(1.0f, cw_pow(2.0f, -md->exposure)); /* (a sunset's exposure would blow a gold gem to white) */
  tm->metalness = 0.7f, tm->roughness = 0.25f;
  t3_geometry *tg = token_geo();
  VW.ntoken = k->ntoken;
  for (int i = 0; i < k->ntoken; i++) {
    VW.token[i] = cw_node_new("Node3D", "Token");
    cw_node_add_child(VW.park, VW.token[i]);
    t3_mesh *me = t3_mesh_new(tg, tm);
    me->base.cast_shadow = true;
    cw_node3d_set_object(VW.token[i], &me->base);
    t3_release(me);
    cw_node3d_set_position(VW.token[i], k->token[i]);
  }
  t3_release(tg), t3_release(tm);
  /* the sun's shadow map over the whole park */
  VW.park_c = vmul(vadd(k->lo, k->hi), 0.5f);
  VW.park_c.y = 0;
  float ex = fmaxf(k->hi.x - k->lo.x, k->hi.z - k->lo.z) * 0.5f;
  VW.park_r = ex;
  cw_node3d_set_position(VW.sun, VW.park_c);
  cw_node_set(VW.sun, "shadow_extent", cw_val_float(fminf(ex, 70)));
  memset(VW.token_seen, 0, sizeof VW.token_seen);
  for (int i = 0; i < MAXP; i++) VW.seat[i].cam_init = false;
}

/* the park's bake (native development, SKATE_BAKE=<dir>): the sky's light on the park's own surfaces, its occlusion and
 * the sun's bounce, into <dir>/park<n>.cwlm (the cart packs lightmaps/). The sun's direct light stays real time. */
bool view_bake(const char *dir, int park_index) {
  cw_tree *t = cw_scene_app_tree();
  /* the sun, in the bake for its bounce only (the lightmaps hold the indirect light) */
  cw_node sun = cw_node_new("DirectionalLight3D", "BakeSun");
  cw_node_add_child(VW.baked, sun);
  cw_node3d_set_quaternion(sun, cw_node3d_quaternion(VW.sun));
  cw_value e;
  float energy = cw_node_get_prop(VW.sun, "energy", &e) ? e.f : 3.0f;
  cw_node_set(sun, "energy", cw_val_float(energy));
  cw_value c;
  if (cw_node_get_prop(VW.sun, "color", &c)) cw_node_set(sun, "color", c);
  cw_node_set(sun, "bake", cw_val_bool(true));
  cw_node_set(sun, "realtime", cw_val_bool(false));
  cw_tree_pre_render(t, 0);
  cw_lightmap_settings s;
  bake_settings(&s);
  double t0 = cw_time();
  cw_lightmap_data *lm = cw_lightmap_bake(t, VW.baked, &s);
  cw_node_free(sun);
  if (!lm) return false;
  char path[512];
  snprintf(path, sizeof path, "%s/park%d.cwlm", dir, park_index);
  bool ok = cw_lightmap_save(lm, path);
  snprintf(path, sizeof path, "%s/park%d.hash", dir, park_index);
  FILE *hf = fopen(path, "wb");
  if (hf) fprintf(hf, "%08x\n", park_geom_hash(VW.k)), fclose(hf);
  else ok = false;
  snprintf(path, sizeof path, "%s/park%d.cwlm", dir, park_index);
  cw_lightmap_info info;
  cw_lightmap_get_info(lm, &info);
  fprintf(stderr, "skate: baked park %d: %d meshes, %d texels, %.1f s -> %s\n", park_index, info.meshes, info.texels, cw_time() - t0, path);
  cw_lightmap_free(lm);
  return ok;
}

/* ── rigs ─────────────────────────────────────────────────────────────── */

void view_rigs(const int skin[MAXP], int players) {
  for (int i = 0; i < MAXP; i++) {
    seat *s = &VW.seat[i];
    bool want = i < players;
    if (cw_node_valid(s->root) && (!want || s->skin != skin[i])) {
      cw_node_free(s->root);
      if (cw_node_valid(s->rider)) cw_node_free(s->rider);
      if (cw_node_valid(s->sparks)) cw_node_free(s->sparks);
      cw_pose_free(&s->rig.pose);
      memset(s, 0, sizeof *s);
    }
    if (!want || cw_node_valid(s->root)) continue;
    s->skin = skin[i];
    char nm[16];
    snprintf(nm, sizeof nm, "Skater%d", i + 1);
    s->root = cw_node_new("Node3D", nm);
    cw_node_add_child(VW.world, s->root);
    s->board = cw_node_new("Node3D", "Board");
    cw_node_add_child(s->root, s->board);
    t3_object *bo = board_model(PLAYER_COL[i], s->wheel);
    cw_node3d_set_object(s->board, bo);
    t3_release(bo);
    s->sparks = cw_node_new("GPUParticles3D", "Sparks");
    cw_node_add_child(VW.world, s->sparks);
    cw_node_set(s->sparks, "emitting", cw_val_bool(false));
    cw_node_set(s->sparks, "amount", cw_val_int(48));
    cw_node_set(s->sparks, "lifetime", cw_val_float(0.35f));
    cw_node_set(s->sparks, "lifetime_random", cw_val_float(0.5f));
    cw_node_set(s->sparks, "spread", cw_val_float(35));
    cw_node_set(s->sparks, "speed_min", cw_val_float(1.5f));
    cw_node_set(s->sparks, "speed_max", cw_val_float(4.0f));
    cw_node_set(s->sparks, "draw_mode", cw_val_enum("STRETCHED"));
    cw_node_set(s->sparks, "stretch", cw_val_float(0.04f));
    cw_node_set(s->sparks, "size", cw_val_float(0.025f));
    cw_node_set(s->sparks, "color", cw_val_color(1.0f, 0.75f, 0.35f, 1));
    cw_node_set(s->sparks, "energy", cw_val_float(6));
    cw_node_set(s->sparks, "blend", cw_val_enum("ADD"));
    if (!cw_node_valid(VW.dust[i])) {
      VW.dust[i] = cw_node_new("GPUParticles3D", "Dust");
      cw_node_add_child(VW.world, VW.dust[i]);
      cw_node *d = &VW.dust[i];
      cw_node_set(*d, "emitting", cw_val_bool(false));
      cw_node_set(*d, "one_shot", cw_val_bool(true));
      cw_node_set(*d, "explosiveness", cw_val_float(0.95f));
      cw_node_set(*d, "amount", cw_val_int(24));
      cw_node_set(*d, "lifetime", cw_val_float(0.7f));
      cw_node_set(*d, "lifetime_random", cw_val_float(0.4f));
      cw_node_set(*d, "emission_shape", cw_val_enum("SPHERE"));
      cw_node_set(*d, "emission_radius", cw_val_float(0.25f));
      cw_node_set(*d, "direction", cw_val_vec3(0, 0.4f, 0));
      cw_node_set(*d, "spread", cw_val_float(85));
      cw_node_set(*d, "speed_min", cw_val_float(0.6f));
      cw_node_set(*d, "speed_max", cw_val_float(1.8f));
      cw_node_set(*d, "gravity", cw_val_vec3(0, -0.6f, 0));
      cw_node_set(*d, "damping", cw_val_float(2.5f));
      cw_node_set(*d, "size", cw_val_float(0.16f));
      cw_node_set(*d, "size_random", cw_val_float(0.5f));
      cw_node_set(*d, "color", cw_val_color(0.72f, 0.68f, 0.6f, 0.3f));
      cw_node_set(*d, "blend", cw_val_enum("ALPHA"));
    }
    s->rider = cw_node_new("Node3D", "Rider");
    cw_node_add_child(VW.world, s->rider);
    t3_object *inst = cw_gltf_instantiate(VW.skin_res[s->skin % NSKINS]);
    if (inst) {
      set_shadows(inst);
      cw_node3d_set_object(s->rider, inst);
      s->model = inst;
      t3_release(inst);
      rig_bind(&s->rig, cw_node3d_object(s->rider));
    }
  }
  VW.players = players;
  /* the sun's map: 4096 texels for one or two views, 2048 when the screen is split three or four ways (each view is a
     quarter of the screen: the finer map is not seen, its cost is) */
  int ss = players > 2 ? 2048 : g_view_shadow_size;
  cw_node_set(VW.sun, "shadow_size", cw_val_int(ss));
}

/* ── the frame ────────────────────────────────────────────────────────── */

static t3_quat quat_basis(v3 x, v3 y, v3 z) {
  /* rotation whose columns are x, y, z */
  float m00 = x.x, m11 = y.y, m22 = z.z, tr = m00 + m11 + m22;
  t3_quat q;
  if (tr > 0) {
    float s = sqrtf(tr + 1) * 2;
    q = (t3_quat){ (y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s };
  } else if (m00 > m11 && m00 > m22) {
    float s = sqrtf(1 + m00 - m11 - m22) * 2;
    q = (t3_quat){ 0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s };
  } else if (m11 > m22) {
    float s = sqrtf(1 + m11 - m00 - m22) * 2;
    q = (t3_quat){ (y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s };
  } else {
    float s = sqrtf(1 + m22 - m00 - m11) * 2;
    q = (t3_quat){ (z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s };
  }
  float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return (t3_quat){ q.x / l, q.y / l, q.z / l, q.w / l };
}
static t3_quat qmul(t3_quat a, t3_quat b) {
  return (t3_quat){ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
static t3_quat qaxis(v3 ax, float a) {
  float s, c;
  cw_sincos(a * 0.5f, &s, &c);
  return (t3_quat){ ax.x * s, ax.y * s, ax.z * s, c };
}
static float ease_io(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

/* the skater between ticks */
static void interp(const skater *a, const skater *b, float al, v3 *p, v3 *f, v3 *u) {
  bool jump = vlen(vsub(a->p, b->p)) > 3.0f; /* a reset or a respawn: no slide across the park */
  if (jump) al = 1;
  *p = vlerp(a->p, b->p, al);
  *f = vnorm(vlerp(a->f, b->f, al));
  *u = vnorm(vlerp(a->u, b->u, al));
  if (vlen(*f) < 0.5f) *f = b->f;
  if (vlen(*u) < 0.5f) *u = b->u;
  *f = vnorm(vperp(*f, *u));
}

static void place_board(seat *s, const skater *k, v3 p, v3 f, v3 u, float t) {
  v3 side = vcross(f, u);
  t3_quat q = quat_basis(f, u, side);
  /* tricks on the board itself, in its own frame about its middle */
  t3_quat lq = { 0, 0, 0, 1 };
  v3 lp = V(0, 0, 0);
  if (k->st == SK_AIR && k->flip >= 0 && k->flip_t < FLIPS[k->flip].len) {
    float e = ease_io((float)k->flip_t / (float)FLIPS[k->flip].len);
    const trick_def *d = &FLIPS[k->flip];
    lq = qmul(qmul(qaxis(V(1, 0, 0), d->roll * 2 * PI_F * e), qaxis(V(0, 1, 0), d->yaw * 2 * PI_F * e)), qaxis(V(0, 0, 1), d->pitch * 2 * PI_F * e));
    lp = V(0, 0.18f * cw_sin(e * PI_F) + 0.06f, 0); /* it pops up to the feet and comes back under them */
    /* about the deck's middle, not its wheels' line: lp + c - q c */
    v3 c = V(0, 0.07f, 0), qc = t3_vec3_apply_quat(c, lq);
    lp = vadd(lp, vsub(c, qc));
  }
  if (k->st == SK_AIR) lp.y += rig_board_lift(&s->rig);
  if (k->st == SK_ROLL && !k->manual) /* a carve tips the deck onto its edge, the way the rider leans */
    lq = qmul(lq, qaxis(V(1, 0, 0), clampf(s->rig.s.lean, -0.5f, 0.5f) * 0.35f));
  if (k->st == SK_ROLL && k->manual) { /* on two wheels: about the back (or front) axle */
    float a = (k->manual_kind ? -1.0f : 1.0f) * (0.2f + 0.03f * k->mbal);
    lq = qaxis(V(0, 0, 1), a);
    lp = V(0.21f * (k->manual_kind ? 1.0f : -1.0f) * (1 - cw_cos(a)), 0.21f * fabsf(cw_sin(a)), 0);
  }
  if (k->st == SK_GRIND) {
    /* the trucks on the rail: a nose or a tail run tips it; a glide turns it across (already in f) */
    if (k->grind_kind == 1) lq = qaxis(V(0, 0, 1), -0.25f), lp = V(0, 0.06f, 0);
    if (k->grind_kind == 2) lq = qaxis(V(0, 0, 1), 0.25f), lp = V(0, 0.06f, 0);
    lp.y -= 0.03f;
  }
  (void)t;
  cw_node3d_set_position(s->root, p);
  cw_node3d_set_quaternion(s->root, q);
  cw_node3d_set_position(s->board, lp);
  cw_node3d_set_quaternion(s->board, lq);
}

/* the chase camera */
static void camera(int i, const skater *k, v3 p, float dt) {
  seat *s = &VW.seat[i];
  v3 travel = V(k->v.x, 0, k->v.z);
  float sp = vlen(travel);
  v3 want_h = sp > 1.0f ? vmul(travel, 1.0f / sp) : (vlen(V(k->f.x, 0, k->f.z)) > 0.3f ? vnorm(V(k->f.x, 0, k->f.z)) : s->cam_h);
  if (k->fakie && sp <= 1.0f) want_h = vmul(want_h, -1);
  /* a jump (a start point, back on the board after a bail): a cut, not a slide across the park */
  if (s->cam_init && vlen(vsub(p, s->last_p)) > 3.0f) s->cam_init = false, s->cam_fwd_ok = false;
  s->last_p = p;
  if (!s->cam_init) s->cam_h = want_h;
  bool hold = (k->st == SK_AIR && k->vert) || (k->st == SK_ROLL && k->u.y < 0.5f) || k->st == SK_BAIL;
  if (!hold && vlen(want_h) > 0.5f) {
    /* swing round toward the run about the rider, a share of the angle a frame and never faster than 2.2 radians a
       second: a run that turned right about (down out of vert air, back from a wall) is swung round to, a side
       picked, not lerped through the rider (a lerp between opposite headings never turns) */
    float rate = 1 - cw_exp(-dt * (s->blocked ? 7.0f : k->st == SK_GRIND ? 5.0f : 3.2f));
    float ang = cw_atan2(vcross(s->cam_h, want_h).y, vdot(s->cam_h, want_h));
    if (fabsf(ang) > 3.1f) ang = (s->swing_side < 0 ? -1.0f : 1.0f) * fabsf(ang); /* (a heading straight back: the side it last swung to) */
    else if (fabsf(ang) > 0.05f) s->swing_side = ang < 0 ? -1.0f : 1.0f;
    float step = clampf(ang * rate, -2.2f * dt, 2.2f * dt);
    if (getenv("SKATE_CAM_LERP")) /* the camera check's control: the old lerp toward the run (stuck when the run turns about) */
      s->cam_h = vnorm(vlerp(s->cam_h, want_h, rate)), step = 0;
    s->cam_h = vnorm(vrot(s->cam_h, V(0, 1, 0), step));
  }
  /* low and close at a roll, further back and wider with speed (the ground rushes past under it) */
  float fast = clampf((sp - 3.0f) / 9.0f, 0, 1);
  v3 at = vadd(p, V(0, 1.05f, 0));
  float back = (3.9f + 1.9f * fast) * g_cam_k, up = (1.15f + 0.25f * fast) * g_cam_k;
  float want_fov = 62 + 14 * fast;
  /* vert air: the camera stays down at the coping's height and looks up at the rider, so the height shows */
  if (k->st == SK_AIR && s->last_st != SK_AIR) s->air_y0 = p.y;
  s->last_st = k->st;
  bool vert_air = k->st == SK_AIR && k->vert;
  /* a bail: watch the body tumble from further back (it can fly back past where the board stopped) */
  v3 hips;
  if (k->st == SK_BAIL && rag_hips(i, &hips)) at = vadd(hips, V(0, 0.5f, 0)), back = fmaxf(back, 6.0f * g_cam_k), up = 2.4f * g_cam_k;
  v3 want = vadd(vadd(vsub(at, vmul(s->cam_h, back)), V(0, up, 0)), vmul(vcross(s->cam_h, V(0, 1, 0)), g_cam_side));
  if (vert_air) {
    float rise = p.y - s->air_y0;
    want.y = fminf(want.y, s->air_y0 + 1.05f + up + 0.4f * fmaxf(rise, 0));
    want_fov += 6;
  }
  if (!s->cam_init) s->fov = want_fov;
  s->fov += (want_fov - s->fov) * (1 - cw_exp(-dt * 2.5f));
  cw_node_set(VW.cam[i], "fov", cw_val_float(s->fov));
  /* keep a wall from coming between: pull in along the line to the rider */
  phit h;
  v3 d = vsub(want, at);
  s->blocked = false;
  if (VW.k && park_cast(VW.k, at, d, (1u << SURF_RIDE) | (1u << SURF_WALL), &h) && h.t < 0.6f) {
    /* a ramp behind the rider (it rolled down out of vert air, or back from a wall): over it, not into the rider's back */
    /* (or a wall right behind a start point): above it, else round to one side or the other */
    static const float TRY[5][2] = { { 0.8f, 0.6f }, { -0.8f, 0.6f }, { 1.5f, 0.8f }, { -1.5f, 0.8f }, { 0, 2.2f } };
    for (int c = 0; c < 5; c++) {
      float sa, ca;
      cw_sincos(TRY[c][0], &sa, &ca);
      v3 hh = V(s->cam_h.x * ca - s->cam_h.z * sa, 0, s->cam_h.x * sa + s->cam_h.z * ca);
      v3 cand = vadd(vsub(at, vmul(hh, back * 0.8f)), V(0, up + TRY[c][1], 0));
      phit h2;
      if (!park_cast(VW.k, at, vsub(cand, at), (1u << SURF_RIDE) | (1u << SURF_WALL), &h2)) { want = cand, d = vsub(want, at); break; }
    }
    s->blocked = true;
  }
  if (VW.k && park_cast(VW.k, at, d, (1u << SURF_RIDE) | (1u << SURF_WALL), &h)) want = vmad(at, d, fmaxf(0.15f, h.t - 0.08f)), d = vsub(want, at);
  /* and a wall seen from behind (the outside of a building the rider skates the roof of): cast back from the camera */
  if (VW.k && park_cast(VW.k, want, vmul(d, -1), (1u << SURF_RIDE) | (1u << SURF_WALL), &h)) want = vmad(at, d, fmaxf(0.15f, 1 - h.t - 0.08f));
  if (!s->cam_init) s->cam_p = want, s->cam_at = at, s->cam_init = true;
  /* it catches up faster when it has fallen far behind */
  float lag = vlen(vsub(s->cam_p, want));
  float kp = 1 - cw_exp(-dt * (lag > 3.0f ? 14.0f : 7.0f)), ka = 1 - cw_exp(-dt * 9.0f);
  s->cam_p = vlerp(s->cam_p, want, kp);
  /* and never closer than 2.4 m across the ground (a rider rolling back toward it): it backs off along the run */
  {
    v3 off = vsub(s->cam_p, at);
    float hd = sqrtf(off.x * off.x + off.z * off.z);
    if (hd < 2.4f * g_cam_k && k->st != SK_BAIL) {
      v3 dirh = hd > 0.05f ? V(off.x / hd, 0, off.z / hd) : vmul(s->cam_h, -1);
      v3 np = V(at.x + dirh.x * 2.4f * g_cam_k, s->cam_p.y, at.z + dirh.z * 2.4f * g_cam_k);
      phit hb;
      if (!VW.k || !park_cast(VW.k, at, vsub(np, at), (1u << SURF_RIDE) | (1u << SURF_WALL), &hb)) s->cam_p = np;
    }
  }
  s->cam_at = vlerp(s->cam_at, at, ka);
  /* the camera's own floor: never under the ground it looks across */
  if (VW.k && park_ground(VW.k, s->cam_p, 0.3f, 40, &h) && s->cam_p.y < h.p.y + 0.4f) s->cam_p.y = h.p.y + 0.4f;
  if (getenv("SKATE_CAMLOG") && atoi(getenv("SKATE_CAMLOG")) == i) /* development: where the camera went */
    fprintf(stderr, "bench cam %d st %d vert %d p %.1f %.1f %.1f cam %.1f %.1f %.1f want %.1f %.1f %.1f h %.2f %.2f blocked %d\n", i, k->st, k->vert, p.x, p.y, p.z, s->cam_p.x, s->cam_p.y, s->cam_p.z, want.x, want.y, want.z,
            s->cam_h.x, s->cam_h.z, s->blocked);
  cw_node3d_set_position(VW.cam[i], s->cam_p);
  /* along the run, the horizon in view; down at the rider when the camera has had to go over or round something */
  s->look_k += ((s->blocked ? 1.0f : 0.0f) - s->look_k) * (1 - cw_exp(-dt * 4.0f));
  v3 look = vlerp(vadd(vadd(s->cam_at, vmul(s->cam_h, 4.0f)), V(0, 0.25f, 0)), vadd(s->cam_at, vmul(s->cam_h, 1.0f)), s->look_k);
  cw_node3d_look_at(VW.cam[i], look, V(0, 1, 0));
  /* measurements: how fast the view turns (a jerk is a cut the eye cannot follow), how far it sits from the rider */
  v3 fwd = vnorm(vsub(look, s->cam_p));
  if (s->cam_fwd_ok && dt > 0) { float a = cw_acos(clampf(vdot(fwd, s->cam_fwd), -1, 1)) * 57.29578f; if (a > g_view_metrics.cam_turn_max) g_view_metrics.cam_turn_max = a; }
  s->cam_fwd = fwd, s->cam_fwd_ok = true;
  if (i == 0) g_view_metrics.cam_dist = vlen(vsub(s->cam_p, p));
}

/* how far vert air turns the board out of the wall's plane, about the coping's line (radians toward level); *face is
   the ramp face's own turn where it took off (0 for a vertical wall, more for a lip short of vertical). 0 when not in
   vert air. The board turns from the face toward level with the air's rise: none at the coping, most at the top, and
   the higher the air the further (a 2.5 m air or more: a little over half way, so a big air still reads steep). */
static float vert_turn(const skater *k, float *face) {
  *face = 0;
  if (k->st != SK_AIR || !k->vert || !VW.k) return 0;
  phit h;
  v3 o = vadd(vmad(k->takeoff, k->vert_n, 0.3f), V(0, -0.15f, 0));
  if (park_cast(VW.k, o, vmul(k->vert_n, -0.6f), 1u << SURF_RIDE, &h) && h.n.y > 0) *face = cw_asin(clampf(h.n.y, 0, 1));
  float rise = k->p.y - k->takeoff_y, top = k->v.y > 0 ? rise + k->v.y * k->v.y / (2 * GRAV) : k->peak_y - k->takeoff_y;
  float frac = clampf(rise / fmaxf(top, 0.05f), 0, 1), reach = 0.55f * clampf(top / 2.5f, 0, 1);
  return *face + (PI_F * 0.5f - *face) * reach * frac * frac * (3 - 2 * frac);
}

void view_frame(const sim *S, const skater prev[MAXP], float alpha, int players, float dt) {
  static float t;
  t += dt;
  for (int i = 0; i < MAXP; i++) {
    seat *s = &VW.seat[i];
    if (!cw_node_valid(s->root)) continue;
    const skater *k = &S->s[i];
    v3 p, f, u;
    interp(&prev[i], k, alpha, &p, &f, &u);
    v3 bp = p, bf = f, bu = u; /* the board's own placement (a bail throws it off the rider) */
    if (k->st == SK_BAIL && VW.k) {
      /* a bail: the board gets away from the rider, lying on the ground and spinning out as it slides */
      phit gh;
      v3 n = park_ground(VW.k, p, 0.5f, 3.0f, &gh) ? gh.n : V(0, 1, 0);
      if (park_ground(VW.k, p, 0.5f, 3.0f, &gh)) bp = gh.p;
      v3 ff = vperp(f, n);
      if (vlen(ff) < 0.2f) ff = vperp(V(1, 0, 0), n);
      float spin = 2.5f * (1 - cw_exp(-(float)k->bail_t * 0.04f));
      bf = vrot(vnorm(ff), n, spin), bu = n;
    }
    {
      /* vert air, drawn with its real attitude (the simulation keeps the board square to the wall's plane): it leaves
         at the angle of the ramp's face where it took off and turns toward level about the line of the coping only
         as far as the air's height allows, most at the top, and is back at the face's angle by the time it comes
         down to the coping, so it meets the wall square. Between ticks both ends' turns are blended (a takeoff and a
         landing do not jump), about the rider's middle. */
      const skater *a = &prev[i];
      float al = vlen(vsub(a->p, k->p)) > 3.0f ? 1.0f : alpha, a0, b0, ta = vert_turn(a, &a0), tb = vert_turn(k, &b0);
      const skater *vs = (k->st == SK_AIR && k->vert) ? k : (a->st == SK_AIR && a->vert) ? a : NULL;
      v3 ax = vs ? vcross(vs->vert_n, V(0, 1, 0)) : V(0, 0, 0);
      if (vs && vlen(ax) > 0.5f) {
        ax = vnorm(ax);
        float th = ta + (tb - ta) * al, th0 = a0 + (b0 - a0) * al;
        v3 c = vadd(p, vmul(vrot(u, ax, th0), 0.6f));
        f = vrot(f, ax, th), u = vrot(u, ax, th);
        p = vsub(c, vmul(u, 0.6f));
      }
    }
    if (k->st == SK_BAIL) place_board(s, k, bp, bf, bu, t);
    else place_board(s, k, p, f, u, t);
    /* the wheels turn with the speed (8 cm across); a landing kicks up a puff of dust */
    if (k->st != SK_BAIL) VW.wheel_a[i] += vlen(k->v) * dt / 0.028f;
    VW.wheel_a[i] = fmodf(VW.wheel_a[i], 2 * PI_F);
    for (int w = 0; w < 4; w++)
      if (s->wheel[w]) s->wheel[w]->quaternion = qaxis(V(0, 0, 1), -VW.wheel_a[i]), s->wheel[w]->matrix_world_needs_update = true;
    if (cw_node_valid(VW.dust[i]) && VW.landed[i] == SK_AIR && (k->st == SK_ROLL || k->st == SK_GRIND)) {
      cw_node3d_set_position(VW.dust[i], vadd(p, V(0, 0.05f, 0)));
      cw_node_set(VW.dust[i], "emitting", cw_val_bool(false));
      cw_node_set(VW.dust[i], "emitting", cw_val_bool(true));
    }
    VW.landed[i] = k->st;
    /* the rider stands on the deck, turned with the board (not its flips) */
    v3 side = vcross(f, u);
    t3_quat q = quat_basis(f, u, side);
    v3 rp = vadd(p, vmul(u, DECK_Y));
    cw_node3d_set_position(s->rider, rp);
    cw_node3d_set_quaternion(s->rider, q);
    if (s->rig.bound) {
      rig_pose(&s->rig, k, dt, t);
      if (i == 0 && (k->st == SK_ROLL || k->st == SK_GRIND) && s->rig.foot_err > g_view_metrics.foot_err_max) g_view_metrics.foot_err_max = s->rig.foot_err, g_view_metrics.foot_err_state = k->st * 10 + (k->push_t > 0) + 2 * k->manual;
      t3_mat4 m;
      t3_mat4_compose(&m, rp, q, V(1, 1, 1));
      rag_tick(i, k, &s->rig, s->rider, &m);
      if (!rag_active(i)) cw_pose_flush(&s->rig.pose);
    }
    /* sparks off the trucks while it grinds */
    bool grinding = k->st == SK_GRIND;
    if (grinding != s->sparking) cw_node_set(s->sparks, "emitting", cw_val_bool(grinding)), s->sparking = grinding;
    if (grinding) {
      cw_node3d_set_position(s->sparks, vadd(p, V(0, 0.03f, 0)));
      v3 back = vnorm(vadd(vmul(k->v, -1.0f / fmaxf(vlen(k->v), 0.1f)), V(0, 0.6f, 0)));
      cw_node_set(s->sparks, "direction", cw_val_vec3(back.x, back.y, back.z));
    }
    if (i < players) {
      camera(i, k, p, dt);
      VW.token_seen[i] = (uint32_t)k->tokens;
    }
  }
  if (getenv("SKATE_SKY")) { static int once; cw_value e; if (!once++ && cw_node_get_prop(VW.sky, "sun_elevation", &e)) fprintf(stderr, "sun elevation %.1f\n", e.f); }
  /* the tokens spin and bob */
  for (int i = 0; i < VW.ntoken; i++) {
    if (!cw_node_valid(VW.token[i])) continue;
    v3 b = VW.k->token[i];
    cw_node3d_set_position(VW.token[i], V(b.x, b.y + 0.12f * cw_sin(t * 2.0f + (float)i), b.z));
    cw_node3d_set_rotation(VW.token[i], V(0, t * 2.4f + (float)i, 0));
  }
  /* the sky dome round the first camera (the view hook moves it for the others) */
  if (cw_node_valid(VW.dome)) { v3 c = cw_node3d_global_position(VW.cam[0]); cw_node3d_set_position(VW.dome, V(c.x, 0, c.z)); }
  /* one view per player */
  cw_node cams[MAXP];
  for (int i = 0; i < MAXP; i++) cams[i] = VW.cam[i];
  cw_shell_split(NULL, cams);
  (void)players;
}

void view_title_camera(const park *k, float t) {
  v3 c = vmul(vadd(k->lo, k->hi), 0.5f);
  float r = fmaxf(k->hi.x - k->lo.x, k->hi.z - k->lo.z) * 0.42f, s, co;
  cw_sincos(t * 0.06f, &s, &co);
  v3 p = V(c.x + s * r, 11 + 3 * cw_sin(t * 0.13f), c.z + co * r);
  cw_node3d_set_position(VW.cam[0], p);
  /* across the park to its setting beyond: the horizon in the top third */
  cw_node3d_look_at(VW.cam[0], V(c.x - s * r * 0.4f, 1.5f, c.z - co * r * 0.4f), V(0, 1, 0));
  cw_node_set(VW.cam[0], "fov", cw_val_float(64));
  /* one view, whatever the players: the menus sit over the whole picture */
  cw_pipeline_split(cw_pipeline_of(cw_scene_app_tree()), &VW.cam[0], 1);
  if (cw_node_valid(VW.dome)) cw_node3d_set_position(VW.dome, V(p.x, 0, p.z));
}

