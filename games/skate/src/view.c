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
} seat;

static struct {
  cw_node root, world, park, sun, sky, env, fog, post, light, dome;
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
    for (int k = 0; k < 4; k++) mb_color(b, v[k], l.r, l.g, l.b);
    mb_quad(b, v[0], v[3], v[2], v[1]);
    for (int sd = -1; sd <= 1; sd += 2) {
      int m = mb_vert(b, vadd(c, V(0, 0, w * (float)sd)), V(0, 0, (float)sd), 0.5f, 0.5f);
      int p = mb_vert(b, vadd(c, V(c0 * r, s0 * r, w * (float)sd)), V(0, 0, (float)sd), 0, 0);
      int q = mb_vert(b, vadd(c, V(c1 * r, s1 * r, w * (float)sd)), V(0, 0, (float)sd), 1, 0);
      mb_color(b, m, l.r * 0.8f, l.g * 0.8f, l.b * 0.8f), mb_color(b, p, l.r, l.g, l.b), mb_color(b, q, l.r, l.g, l.b);
      if (sd > 0) mb_tri(b, m, p, q);
      else mb_tri(b, m, q, p);
    }
  }
}
/* the deck: a curved plank (kicktails at both ends), nose along +x, its top at y = DECK_Y */

static t3_object *board_model(const float col[3]) {
  mesh_build deck = { 0 }, grip = { 0 }, hw = { 0 };
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
    for (int sd = -1; sd <= 1; sd += 2) wheel(&hw, V(x, 0.028f, 0.085f * (float)sd), 0.028f, 0.018f, urethane);
  }
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
static void sky_dome(cw_node parent) {
  mesh_build b = { 0 };
  const int AZ = 32, EL = 14;
  const float R = 380;
  for (int j = 0; j <= EL; j++) {
    float e = -0.12f + (PI_F * 0.5f + 0.12f) * (float)j / EL, se, ce;
    cw_sincos(e, &se, &ce);
    float k = clampf(se, 0, 1);
    /* horizon pale, zenith deep; a warm whisper at the very bottom */
    float r = 0.72f + (0.13f - 0.72f) * cw_pow(k, 0.55f), g = 0.86f + (0.42f - 0.86f) * cw_pow(k, 0.6f), bl = 1.0f + (0.92f - 1.0f) * k;
    if (se < 0) r = 0.78f, g = 0.86f, bl = 0.9f;
    t3_color c = lin(r, g, bl);
    for (int i = 0; i <= AZ; i++) {
      float a = 2 * PI_F * (float)i / AZ, sa, ca;
      cw_sincos(a, &sa, &ca);
      v3 p = V(ca * ce * R, se * R, sa * ce * R);
      int v = mb_vert(&b, p, vmul(vnorm(p), -1), (float)i / AZ, (float)j / EL);
      mb_color(&b, v, c.r * 1.7f, c.g * 1.7f, c.b * 1.7f);
    }
  }
  for (int j = 0; j < EL; j++)
    for (int i = 0; i < AZ; i++) {
      int a = j * (AZ + 1) + i;
      mb_quad(&b, a, a + 1, a + AZ + 2, a + AZ + 1);
    }
  /* clouds: puffs of flat ellipses at 150 to 250 m, white tops, a grey-blue underside */
  uint32_t h = 977;
  for (int c = 0; c < 14; c++) {
    h = h * 1664525u + 1013904223u;
    float az = (float)(h >> 8) / 16777216.0f * 2 * PI_F;
    h = h * 1664525u + 1013904223u;
    float el = 0.12f + 0.32f * (float)(h >> 8) / 16777216.0f;
    float sa, ca, se, ce;
    cw_sincos(az, &sa, &ca), cw_sincos(el, &se, &ce);
    v3 cen = V(ca * ce * 330, se * 330, sa * ce * 330);
    v3 fw = vnorm(vmul(cen, -1)), right = vnorm(vcross(fw, V(0, 1, 0))), up = vcross(right, fw);
    for (int p = 0; p < 6; p++) {
      h = h * 1664525u + 1013904223u;
      float ox = ((float)(h >> 8) / 16777216.0f - 0.5f) * 70, oy = ((float)((h >> 4) & 255) / 255.0f) * 10;
      float rx = 14 + 10 * (float)((h >> 12) & 255) / 255.0f, ry = rx * 0.55f;
      v3 m = vadd(cen, vadd(vmul(right, ox), vmul(up, oy)));
      t3_color top = lin(1, 1, 1), bot = lin(0.78f, 0.84f, 0.95f);
      int ci = mb_vert(&b, m, fw, 0.5f, 0.5f);
      mb_color(&b, ci, top.r * 2.1f, top.g * 2.1f, top.b * 2.1f);
      const int S = 16;
      int first = -1, prev = -1;
      for (int s = 0; s <= S; s++) {
        float a = PI_F * (float)s / S, sn, cs;
        cw_sincos(a, &sn, &cs);
        float yy = sn * ry; /* the top half: a dome; the flat bottom closes it */
        v3 q = vadd(m, vadd(vmul(right, cs * rx), vmul(up, yy)));
        int v = mb_vert(&b, q, fw, 0, 0);
        float t = clampf(yy / ry, 0, 1);
        mb_color(&b, v, (bot.r + (top.r - bot.r) * t) * 2.0f, (bot.g + (top.g - bot.g) * t) * 2.0f, (bot.b + (top.b - bot.b) * t) * 2.0f);
        if (prev >= 0) mb_tri(&b, ci, v, prev);
        if (first < 0) first = v;
        prev = v;
      }
      (void)first;
    }
  }
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  m->side = T3_DOUBLE_SIDE;
  m->depth_write = false;
  m->fog = false;
  t3_geometry *g = mb_finish(&b);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false, me->base.receive_shadow = false;
  me->base.frustum_culled = false;
  me->base.render_order = -100;
  cw_node n = cw_node_new("Node3D", "SkyDome");
  cw_node_add_child(parent, n);
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
    sky_dome(root);
    VW.fog = cw_node_new("HeightFog", "Haze");
    cw_node_add_child(root, VW.fog);
    cw_node_set(VW.fog, "density", cw_val_float(0.0009f));
    cw_node_set(VW.fog, "height_falloff", cw_val_float(0.06f));
    cw_node_set(VW.fog, "start_distance", cw_val_float(70));
    cw_node_set(VW.fog, "use_sky_color", cw_val_bool(false));
    cw_node_set(VW.fog, "color", cw_val_color(0.55f, 0.7f, 0.95f, 1));
    VW.post = cw_node_new("PostProcess", "Post");
    cw_node_add_child(root, VW.post);
    cw_node_set(VW.post, "hdr", cw_val_bool(true));
    cw_node_set(VW.post, "msaa", cw_val_int(4));
    cw_node_set(VW.post, "bloom", cw_val_bool(false)); /* a sunny afternoon has little to bloom: the 0.8 ms goes to the four views */
    cw_node_set(VW.post, "bloom_intensity", cw_val_float(0.05f));
    cw_node_set(VW.post, "bloom_threshold", cw_val_float(2.5f));
    cw_node_set(VW.post, "exposure_ev", cw_val_float(0.0f));
    cw_node_set(VW.post, "tonemap", cw_val_enum("NEUTRAL"));
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
    while (tex_k < MAT_KINDS && tex_warm(tex_k)) tex_k++;
    if (tex_k < MAT_KINDS) tex_warm(tex_k);
    if (step == S_MATS - 1) while (tex_k < MAT_KINDS) { while (!tex_warm(tex_k)) {} tex_k++; }
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

void view_park(const park *k) {
  if (cw_node_valid(VW.park)) cw_node_free(VW.park);
  VW.park = cw_node_new("Node3D", "Park");
  cw_node_add_child(VW.world, VW.park);
  VW.k = k;
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
    cw_node n = cw_node_new("Node3D", "Surface");
    cw_node_add_child(VW.park, n);
    cw_node3d_set_object(n, &me->base);
    t3_release(me), t3_release(g);
  }
  /* the tokens */
  t3_material *tm = t3_mesh_standard_material_new(0xffc020);
  tm->emissive = lin(1.0f, 0.62f, 0.1f);
  tm->emissive_intensity = 2.2f;
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
    t3_object *bo = board_model(PLAYER_COL[i]);
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
    float rate = 1 - cw_exp(-dt * (k->st == SK_GRIND ? 5.0f : 3.2f));
    s->cam_h = vnorm(vlerp(s->cam_h, want_h, rate));
  }
  v3 at = vadd(p, V(0, 1.25f, 0));
  float back = (4.4f + clampf(sp * 0.08f, 0, 1.4f)) * g_cam_k, up = 1.7f * g_cam_k;
  /* a bail: watch the body tumble from further back (it can fly back past where the board stopped) */
  v3 hips;
  if (k->st == SK_BAIL && rag_hips(i, &hips)) at = vadd(hips, V(0, 0.5f, 0)), back = fmaxf(back, 6.0f * g_cam_k), up = 2.4f * g_cam_k;
  v3 want = vadd(vadd(vsub(at, vmul(s->cam_h, back)), V(0, up, 0)), vmul(vcross(s->cam_h, V(0, 1, 0)), g_cam_side));
  /* keep a wall from coming between: pull in along the line to the rider */
  phit h;
  v3 d = vsub(want, at);
  if (VW.k && park_cast(VW.k, at, d, (1u << SURF_RIDE) | (1u << SURF_WALL), &h)) want = vmad(at, d, fmaxf(0.15f, h.t - 0.08f)), d = vsub(want, at);
  /* and a wall seen from behind (the outside of a building the rider skates the roof of): cast back from the camera */
  if (VW.k && park_cast(VW.k, want, vmul(d, -1), (1u << SURF_RIDE) | (1u << SURF_WALL), &h)) want = vmad(at, d, fmaxf(0.15f, 1 - h.t - 0.08f));
  if (!s->cam_init) s->cam_p = want, s->cam_at = at, s->cam_init = true;
  float kp = 1 - cw_exp(-dt * 7.0f), ka = 1 - cw_exp(-dt * 9.0f);
  s->cam_p = vlerp(s->cam_p, want, kp);
  s->cam_at = vlerp(s->cam_at, at, ka);
  /* the camera's own floor: never under the ground it looks across */
  if (VW.k && park_ground(VW.k, s->cam_p, 0.3f, 40, &h) && s->cam_p.y < h.p.y + 0.4f) s->cam_p.y = h.p.y + 0.4f;
  cw_node3d_set_position(VW.cam[i], s->cam_p);
  v3 look = vadd(s->cam_at, vmul(s->cam_h, 1.2f));
  cw_node3d_look_at(VW.cam[i], look, V(0, 1, 0));
  /* measurements: how fast the view turns (a jerk is a cut the eye cannot follow), how far it sits from the rider */
  v3 fwd = vnorm(vsub(look, s->cam_p));
  if (s->cam_fwd_ok && dt > 0) { float a = cw_acos(clampf(vdot(fwd, s->cam_fwd), -1, 1)) * 57.29578f; if (a > g_view_metrics.cam_turn_max) g_view_metrics.cam_turn_max = a; }
  s->cam_fwd = fwd, s->cam_fwd_ok = true;
  if (i == 0) g_view_metrics.cam_dist = vlen(vsub(s->cam_p, p));
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
    if (k->st == SK_AIR && k->vert) {
      /* vert air, drawn: the board stays square to the wall as it leaves and comes back, and turns level over the
         coping at the top (the rider upright there), about the line of the coping */
      v3 ax = vcross(k->vert_n, V(0, 1, 0));
      if (vlen(ax) > 0.5f) {
        ax = vnorm(ax);
        float w = clampf(1 - fabsf(k->v.y) / 5.5f, 0, 1), th = w * w * (3 - 2 * w) * 1.2f;
        v3 c = vadd(p, vmul(u, 0.6f));
        f = vrot(f, ax, th), u = vrot(u, ax, th);
        p = vsub(c, vmul(u, 0.6f));
      }
    }
    if (k->st == SK_BAIL) place_board(s, k, bp, bf, bu, t);
    else place_board(s, k, p, f, u, t);
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
  v3 p = V(c.x + s * r, 16 + 4 * cw_sin(t * 0.13f), c.z + co * r);
  cw_node3d_set_position(VW.cam[0], p);
  cw_node3d_look_at(VW.cam[0], V(c.x, 0, c.z), V(0, 1, 0));
  /* one view, whatever the players: the menus sit over the whole picture */
  cw_pipeline_split(cw_pipeline_of(cw_scene_app_tree()), &VW.cam[0], 1);
  if (cw_node_valid(VW.dome)) cw_node3d_set_position(VW.dome, V(p.x, 0, p.z));
}

