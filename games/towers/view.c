/* towers view: draws the simulation. Render only: nothing here feeds the rules.
 *
 *   the map     mapbuild.c builds its still scenery (the baked ground and path, the props); the camera is fixed for
 *               the map, the whole of it on one screen.
 *   things      creeps, towers, pads, the core, the gates, shells, beams and lightning: instanced meshes (a creep kind
 *               is one mesh per animation frame: thousands of creeps are a handful of draws and no skinning),
 *               rewritten every frame.
 *   light       the ground is baked and drawn unlit; everything that moves takes the bake's light where it stands (the
 *               probe grid), the towers' flashes and the beams' hits are clustered omni lights, and every moving light
 *               shows on the baked ground as a pool of added light (splats); blob shadows under everything. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "cartwheel/rim_light.h"
#include "cartwheel/splats.h"
#include "cartwheel/vfs.h"
#include "game.h"
#include "mapbuild.h"

t3_color lin(float r, float g, float b) { return mb_lin(r, g, b); }
const float PCOL[MAXP][3] = { { 0.25f, 0.65f, 1.0f }, { 1.0f, 0.55f, 0.12f }, { 0.4f, 0.95f, 0.35f }, { 0.95f, 0.3f, 0.8f } };
const float TCOL[TK_KINDS][3] = { { 0.55f, 0.8f, 1.0f }, { 0.6f, 0.95f, 1.0f }, { 1.0f, 0.3f, 0.75f }, { 1.0f, 0.6f, 0.2f } };

static const t3_mat4 ZERO;
#define PAD_H 0.12f /* a pad's top: the towers stand on it */
#define TSC 1.15f /* the towers drawn a little over their modelled size: a tile across, they read from above */
#define FAR_SPHERE 400.0f

typedef struct kindv { mb_chr c; int nwalk, natk; t3_instanced_mesh *im[MB_MAXF], *edge[MB_MAXF]; float r; t3_material *edge_mat; } kindv;
typedef struct partv { t3_instanced_mesh *surf[3], *glow[3], *trim[3]; int n; } partv; /* a model by level (frame) */

static struct {
  cw_node root;
  bool have_map, lit;
  mb_built b;
  int map_built, theme;
  tdmap m;
  char *text;
  kindv k[CK_KINDS];
  partv base[TK_KINDS], head[TK_KINDS];
  float muzzle[TK_KINDS][3][3];
  t3_instanced_mesh *pad, *pad_glow, *pad_trim;
  t3_instanced_mesh *core[4], *core_glow[4];
  int core_frames;
  t3_instanced_mesh *portal, *portal_glow, *shell, *shell_glow;
  t3_instanced_mesh *beam_core, *beam_glow, *bolt_core, *bolt_glow, *column;
  t3_instanced_mesh *ring;      /* flat rings on the ground: the cursors, the ranges */
  t3_instanced_mesh *marker;    /* the cursors' markers: a bobbing diamond over the slot */
  cw_splats *shadows, *pools, *glows;
  int nbeam_col, nring;
  view_stats st;
  t3_vec3 eye, at;
  float aspect, dist;
  cw_node core_light, portal_light[MP_MAXSPAWN], beam_light[8];
} V;

float g_cam_k = 1.0f, g_cam_x, g_cam_z; /* development and key art: a closer camera over another point */
float g_tilt = 15.0f, g_lean = 15.0f;
bool g_hide_cursors;

/* ── helpers ────────────────────────────────────────────────────────── */

static t3_instanced_mesh *imesh(const char *name, t3_geometry *g, t3_material *m, int n) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  im->mesh.base.cast_shadow = false;
  im->mesh.base.receive_shadow = false;
  for (int i = 0; i < n; i++) {
    t3_instanced_mesh_set_matrix_at(im, i, &ZERO);
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
  }
  im->instance_matrix->usage = T3_DYNAMIC_DRAW;
  im->instance_color->usage = T3_DYNAMIC_DRAW;
  t3_sphere sph = { { 0, 0, 0 }, FAR_SPHERE };
  t3_instanced_mesh_set_bounding_sphere(im, &sph);
  cw_node node = cw_node_new("Node3D", name);
  cw_node_add_child(V.root, node);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, &im->mesh.base);
  cw_node3d_set_object(node, wrap);
  t3_release(wrap), t3_release(g), t3_release(m);
  im->count = 0;
  return im;
}
static t3_material *mat_std(float rough, float metal) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->vertex_colors = true;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}
static t3_material *mat_glow(void) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  return m;
}
static t3_material *mat_add(void) { /* additive light: beams, bolts, columns */
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true, m->transparent = true, m->depth_write = false, m->blending = T3_ADDITIVE_BLENDING, m->side = T3_DOUBLE_SIDE;
  return m;
}
static t3_quat lean_q(float yaw) {
  float l = g_lean * 3.14159265f / 180.0f;
  t3_quat qx = { cw_sin(l * 0.5f), 0, 0, cw_cos(l * 0.5f) }, qy = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
  return t3_quat_multiply(qx, qy);
}
static void put_up(t3_instanced_mesh *im, int i, float x, float y, float z, float yaw, float s) {
  t3_mat4 m;
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, lean_q(yaw), (t3_vec3){ s, s, s });
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void put(t3_instanced_mesh *im, int i, float x, float y, float z, float yaw, float s) {
  t3_mat4 m;
  t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, q, (t3_vec3){ s, s, s });
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void put_s(t3_instanced_mesh *im, int i, float x, float y, float z, float yaw, float sx, float sy, float sz) {
  t3_mat4 m;
  t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, q, (t3_vec3){ sx, sy, sz });
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
/* a unit segment mesh (along +Z from 0 to 1) stretched from a to b, w wide */
static void put_seg(t3_instanced_mesh *im, int i, t3_vec3 a, t3_vec3 b, float w) {
  t3_vec3 d = { b.x - a.x, b.y - a.y, b.z - a.z };
  float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
  if (l < 1e-4f) l = 1e-4f, d = (t3_vec3){ 0, 0, 1e-4f };
  t3_vec3 z = { d.x / l, d.y / l, d.z / l };
  t3_vec3 up = fabsf(z.y) > 0.95f ? (t3_vec3){ 1, 0, 0 } : (t3_vec3){ 0, 1, 0 };
  t3_vec3 x = { up.y * z.z - up.z * z.y, up.z * z.x - up.x * z.z, up.x * z.y - up.y * z.x };
  float xl = sqrtf(x.x * x.x + x.y * x.y + x.z * x.z);
  x = (t3_vec3){ x.x / xl, x.y / xl, x.z / xl };
  t3_vec3 y = { z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x };
  t3_mat4 m = { { x.x * w, x.y * w, x.z * w, 0, y.x * w, y.y * w, y.z * w, 0, d.x, d.y, d.z, 0, a.x, a.y, a.z, 1 } };
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void done(t3_instanced_mesh *im, int n) {
  if (!im) return;
  im->count = n;
  im->instance_matrix->version++;
  im->instance_color->version++;
}
static uint32_t hash2(uint32_t a, uint32_t b) {
  uint32_t h = a * 374761393u + b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(uint32_t a, uint32_t b) { return (float)(hash2(a, b) & 0xffffff) / 16777216.0f; }

static t3_geometry *geo_colored(t3_geometry *g, float r, float gg, float b) {
  int n = g->attributes[T3_ATTR_POSITION]->count;
  float *c = malloc(sizeof(float) * 3 * (size_t)n);
  t3_color l = lin(r, gg, b);
  for (int i = 0; i < n; i++) c[i * 3] = l.r, c[i * 3 + 1] = l.g, c[i * 3 + 2] = l.b;
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, c, n, 3));
  free(c);
  return g;
}
/* a model's frames as instanced meshes (surface, glow, trim), or a stand-in shape when the file is missing */
static int model_parts(const char *file, const char *name, t3_instanced_mesh **surf, t3_instanced_mesh **glow, t3_instanced_mesh **trim, int maxf, int cap,
                       t3_geometry *(*fallback)(void), float rough) {
  mb_chr c;
  int n = 0;
  if (cw_vfs_exists(file) && mb_chr_load(&c, file)) {
    n = CW_MIN(c.total, maxf);
    for (int f = 0; f < n; f++) {
      surf[f] = c.geo[0][f] ? imesh(name, t3_retain(c.geo[0][f]), mat_std(rough, 0.15f), cap) : NULL;
      if (glow) glow[f] = c.ngroup > 1 && c.geo[1][f] ? imesh(name, t3_retain(c.geo[1][f]), mat_glow(), cap) : NULL;
      if (trim) trim[f] = c.ngroup > 2 && c.geo[2][f] ? imesh(name, t3_retain(c.geo[2][f]), mat_std(0.5f, 0.2f), cap) : NULL;
    }
    mb_chr_free(&c);
  } else if (fallback) {
    surf[0] = imesh(name, fallback(), mat_std(rough, 0.1f), cap);
    if (glow) glow[0] = NULL;
    if (trim) trim[0] = NULL;
    n = 1;
  }
  return n;
}
static t3_geometry *fb_tower(void) {
  t3_geometry *g = t3_cylinder_geometry_new(0.55f, 0.75f, 1.2f, 12, 1, false);
  t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0.6f, 0, 1 } });
  return geo_colored(g, 0.6f, 0.58f, 0.55f);
}
static t3_geometry *fb_head(void) { return geo_colored(t3_sphere_geometry_new(0.45f, 12, 8), 0.75f, 0.75f, 0.8f); }
static t3_geometry *fb_pad(void) { return geo_colored(t3_cylinder_geometry_new(0.9f, 0.9f, 0.12f, 20, 1, false), 0.6f, 0.6f, 0.62f); }
static t3_geometry *fb_core(void) { return geo_colored(t3_octahedron_geometry_new(1.4f, 0), 0.5f, 0.9f, 1.0f); }
static t3_geometry *fb_portal(void) {
  t3_geometry *g = t3_torus_geometry_new(1.4f, 0.25f, 10, 24, 6.2831853f);
  t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1.6f, 0, 1 } });
  return geo_colored(g, 0.45f, 0.35f, 0.5f);
}
static t3_geometry *fb_shell(void) { return geo_colored(t3_sphere_geometry_new(0.2f, 10, 8), 0.3f, 0.3f, 0.32f); }

/* ── init ───────────────────────────────────────────────────────────── */

static const char *const MON_FILE[CK_KINDS] = { "models/skitter.cwchar", "models/ghoul.cwchar", "models/hound.cwchar", "models/imp.cwchar",
                                                "models/sporeling.cwchar", "models/bloater.cwchar", "models/shade.cwchar", "models/ogre.cwchar" };
static const char *const MON_NAME[CK_KINDS] = { "Skitters", "Ghouls", "Hounds", "Imps", "Sporelings", "Bloaters", "Shades", "Ogres" };
static const int MON_CAP[CK_KINDS] = { 3400, 2400, 2000, 1600, 1200, 900, 1600, 600 };
/* each kind's identity colour: its edge (DESIGN.md, the look) */
static const float KIND_RIM[CK_KINDS][3] = { { 0.2f, 1.0f, 1.0f }, { 1.0f, 0.95f, 0.65f }, { 1.0f, 0.55f, 0.1f }, { 1.0f, 0.85f, 0.15f },
                                             { 1.0f, 0.35f, 1.0f }, { 0.7f, 1.0f, 0.1f }, { 0.75f, 0.9f, 1.0f }, { 1.0f, 0.15f, 0.25f } };
/* each kind's body: the modelled colours (a dark body, one accent) lifted and tinted in the kind's colour, so at 20 to 40
 * pixels a kind reads by its colour and its size, not its edge alone: cyan skitters, bone ghouls, orange hounds, gold imps,
 * violet sporelings, lime bloaters, pale shades, crimson ogres */
static const float KIND_BODY[CK_KINDS][3] = { { 0.8f, 2.3f, 2.5f }, { 2.5f, 2.3f, 1.7f }, { 2.8f, 1.4f, 0.45f }, { 2.9f, 2.1f, 0.4f },
                                             { 2.7f, 1.3f, 3.1f }, { 1.7f, 2.7f, 0.5f }, { 1.9f, 2.3f, 2.9f }, { 2.7f, 0.55f, 0.7f } };

static t3_geometry *hull(t3_geometry *g, float d) {
  t3_attribute *pa = g->attributes[T3_ATTR_POSITION], *na = g->attributes[T3_ATTR_NORMAL];
  if (!pa || !na || !g->index) return NULL;
  int n = pa->count;
  const float *p = t3_attribute_f32(pa), *nr = t3_attribute_f32(na);
  float *q = malloc(sizeof(float) * 3 * (size_t)n);
  for (int i = 0; i < n * 3; i++) q[i] = p[i] + nr[i] * d;
  t3_geometry *h = t3_geometry_new();
  t3_geometry_set_attribute(h, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, q, n, 3));
  t3_geometry_set_attribute(h, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nr, n, 3));
  t3_geometry_set_index(h, t3_attribute_new(g->index->type, g->index->array, g->index->count, 1));
  free(q);
  return h;
}
static void kind_load(int k) {
  kindv *kv = &V.k[k];
  if (!mb_chr_load(&kv->c, MON_FILE[k])) return;
  mb_chr_simplify(&kv->c, 0, 0.14f); /* a creep is 20 to 40 pixels across: a third of its triangles do */
  kv->nwalk = kv->c.nf[0];
  kv->natk = 0;
  kv->r = kv->c.rad;
  t3_material *m = t3_mesh_lambert_material_new(0xffffff);
  m->vertex_colors = true;
  t3_material *em = t3_mesh_basic_material_new(0xffffff);
  em->side = T3_BACK_SIDE;
  t3_color kc = lin(KIND_RIM[k][0], KIND_RIM[k][1], KIND_RIM[k][2]);
  em->color = (t3_color){ kc.r * 0.05f + 0.012f, kc.g * 0.05f + 0.012f, kc.b * 0.05f + 0.016f }; /* an ink outline: the tinted body pops on the dark ground, the outline on the pale path */
  (void)kc;
  for (int f = 0; f < kv->nwalk && f < MB_MAXF; f++) {
    int cap = MON_CAP[k] / CW_MAX(kv->nwalk, 1) * 3 / 2 + 48;
    kv->im[f] = imesh(MON_NAME[k], t3_retain(kv->c.geo[0][f]), t3_retain(m), cap);
    if (k == CK_SKITTER) continue; /* the swarm is cyan specks: an edge on each costs a third of the crowd's edges and reads as nothing */
    t3_geometry *hg = hull(kv->c.geo[0][f], 0.045f);
    if (!hg) continue;
    t3_instanced_mesh *e = imesh(MON_NAME[k], hg, t3_retain(em), 1);
    t3_release(e->instance_matrix), t3_release(e->instance_color);
    e->instance_matrix = t3_retain(kv->im[f]->instance_matrix), e->instance_color = t3_retain(kv->im[f]->instance_color);
    e->mesh.base.render_order = 1;
    kv->edge[f] = e;
  }
  kv->edge_mat = em;
  t3_release(em), t3_release(m);
}

static void read_muzzles(void) {
  for (int k = 0; k < TK_KINDS; k++)
    for (int l = 0; l < 3; l++) V.muzzle[k][l][0] = 0, V.muzzle[k][l][1] = 0.6f + 0.1f * (float)l, V.muzzle[k][l][2] = k == TK_BOLT || k == TK_FROST ? 0 : -0.5f;
  size_t len = 0;
  char *b = cw_vfs_exists("models/towers.json") ? (char *)cw_vfs_read("models/towers.json", &len) : NULL;
  if (!b) return;
  char *t = malloc(len + 1);
  memcpy(t, b, len), t[len] = 0, free(b);
  static const char *const KEY[TK_KINDS] = { "\"bolt\"", "\"frost\"", "\"beam\"", "\"mortar\"" };
  for (int k = 0; k < TK_KINDS; k++) {
    const char *p = strstr(t, KEY[k]);
    if (!p) continue;
    for (int l = 0; l < 3; l++) {
      while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
      float v[3];
      for (int c = 0; c < 3; c++) {
        while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
        v[c] = (float)strtod(p, (char **)&p);
      }
      memcpy(V.muzzle[k][l], v, sizeof v);
    }
  }
  free(t);
}

static cw_node omni(const char *name, float range) {
  cw_node l = cw_node_new("OmniLight3D", name);
  cw_node_add_child(V.root, l);
  cw_node3d_set_position(l, (t3_vec3){ 0, -60, 0 });
  cw_node_set(l, "energy", cw_val_float(0));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node_set(l, "decay", cw_val_float(2));
  cw_node_set(l, "fog_energy", cw_val_float(0));
  return l;
}

bool view_init_step(cw_node root, int *done_, int *total) {
  enum { S_ROOT, S_KINDS, S_TOWERS = S_KINDS + CK_KINDS, S_PARTS = S_TOWERS + TK_KINDS, S_FX, S_DONE };
  static int step;
  *total = S_DONE;
  int s = step++;
  *done_ = step;
  if (s == S_ROOT) {
    memset(&V, 0, sizeof V);
    V.root = cw_node_new("Node3D", "View");
    cw_node_add_child(root, V.root);
    V.map_built = -1;
    read_muzzles();
  } else if (s >= S_KINDS && s < S_TOWERS) {
    kind_load(s - S_KINDS);
  } else if (s >= S_TOWERS && s < S_PARTS) {
    int k = s - S_TOWERS;
    static const char *const ID[TK_KINDS] = { "bolt", "frost", "beam", "mortar" };
    char f[64];
    snprintf(f, sizeof f, "models/tower_%s_base.cwchar", ID[k]);
    V.base[k].n = model_parts(f, "TowerBase", V.base[k].surf, V.base[k].glow, V.base[k].trim, 3, MAXT, fb_tower, 0.4f);
    snprintf(f, sizeof f, "models/tower_%s_head.cwchar", ID[k]);
    V.head[k].n = model_parts(f, "TowerHead", V.head[k].surf, V.head[k].glow, V.head[k].trim, 3, MAXT, fb_head, 0.3f);
    { /* the towers pop: a bright warm rim round every part, lit from the camera's side, so a tower reads at a glance */
      cw_rim_light_desc rd = cw_rim_light_default();
      rd.color = (t3_color){ 1.0f, 0.92f, 0.78f };
      rd.strength = 1.6f, rd.power = 2.4f, rd.near = 0, rd.far = 1, rd.close = 1;
      for (int l = 0; l < 3; l++) {
        if (V.base[k].surf[l]) cw_rim_light_apply(V.base[k].surf[l]->mesh.materials[0], &rd);
        if (V.head[k].surf[l]) cw_rim_light_apply(V.head[k].surf[l]->mesh.materials[0], &rd);
      }
    }
  } else if (s == S_PARTS) {
    model_parts("models/pad.cwchar", "Pads", &V.pad, &V.pad_glow, &V.pad_trim, 1, MP_MAXSLOT, fb_pad, 0.7f);
    V.core_frames = model_parts("models/core.cwchar", "Core", V.core, V.core_glow, NULL, 4, 1, fb_core, 0.3f);
    model_parts("models/portal.cwchar", "Portals", &V.portal, &V.portal_glow, NULL, 1, MP_MAXSPAWN, fb_portal, 0.6f);
    model_parts("models/shell.cwchar", "Shells", &V.shell, &V.shell_glow, NULL, 1, MAXSH, fb_shell, 0.5f);
  } else if (s == S_FX) {
    V.shadows = cw_splats_new(V.root, "Shadows", CW_SPLATS_SHADOW, MAXE + 256);
    V.pools = cw_splats_new(V.root, "LightPools", CW_SPLATS_LIGHT, 128);
    V.glows = cw_splats_new(V.root, "Glows", CW_SPLATS_LIGHT, 512);
    { /* the beams: a unit cylinder along +Z, a white core and a wide coloured glow */
      t3_geometry *g = t3_cylinder_geometry_new(1, 1, 1, 8, 1, true);
      t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0.5f, 1 } });
      geo_colored(g, 1, 1, 1);
      V.beam_core = imesh("BeamCore", t3_retain(g), mat_add(), MAXT);
      V.beam_glow = imesh("BeamGlow", g, mat_add(), MAXT);
      V.beam_core->mesh.base.render_order = 6, V.beam_glow->mesh.base.render_order = 6;
    }
    { /* lightning: thin boxes along each jagged segment */
      t3_geometry *g = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
      t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0.5f, 1 } });
      geo_colored(g, 1, 1, 1);
      V.bolt_core = imesh("BoltCore", t3_retain(g), mat_add(), 1600);
      V.bolt_glow = imesh("BoltGlow", g, mat_add(), 1600);
      V.bolt_core->mesh.base.render_order = 7, V.bolt_glow->mesh.base.render_order = 7;
    }
    { /* columns of light: the gates, the core, the cursors */
      t3_geometry *g = t3_cylinder_geometry_new(1, 0.55f, 1, 16, 4, true);
      t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0.5f, 0, 1 } });
      t3_attribute *pa = g->attributes[T3_ATTR_POSITION];
      const float *pp = t3_attribute_f32(pa);
      float *c = malloc(sizeof(float) * 3 * (size_t)pa->count);
      for (int i = 0; i < pa->count; i++) { float k = 1.0f - pp[i * 3 + 1]; k = k * k; c[i * 3] = c[i * 3 + 1] = c[i * 3 + 2] = k; }
      t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, c, pa->count, 3));
      free(c);
      V.column = imesh("Columns", g, mat_add(), 64);
      V.column->mesh.base.render_order = 5;
    }
    { /* rings on the ground: a thin unit ring, flat, drawn over everything near the ground */
      t3_geometry *g = t3_ring_geometry_new(0.94f, 1.0f, 64, 1);
      t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } });
      t3_material *m = mat_add();
      m->depth_test = false;
      V.ring = imesh("Rings", geo_colored(g, 1, 1, 1), m, 64);
      V.ring->mesh.base.render_order = 10;
    }
    { /* a cursor's marker: a squat diamond (an octahedron) floating over its slot */
      t3_geometry *g = t3_octahedron_geometry_new(0.42f, 0);
      t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 1.4f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } });
      t3_material *m = t3_mesh_basic_material_new(0xffffff);
      m->vertex_colors = true;
      V.marker = imesh("Markers", geo_colored(g, 1, 1, 1), m, MAXP);
      V.marker->mesh.base.render_order = 9;
    }
    V.core_light = omni("CoreLight", 12);
    for (int i = 0; i < MP_MAXSPAWN; i++) V.portal_light[i] = omni("GateLight", 9);
    for (int i = 0; i < 8; i++) V.beam_light[i] = omni("BeamLight", 6);
  }
  return step >= S_DONE;
}

void view_free(void) {
  for (int k = 0; k < CK_KINDS; k++) mb_chr_free(&V.k[k].c);
  if (V.have_map) mapbuild_free(&V.b);
  free(V.text);
  memset(&V, 0, sizeof V);
}

/* ── the map ────────────────────────────────────────────────────────── */

void view_map(const sim *s) {
  if (s->m.num == V.map_built) return;
  if (V.have_map) mapbuild_free(&V.b);
  V.have_map = false, V.lit = false;
  V.m = s->m;
  V.theme = CW_CLAMP(V.m.theme, 0, MP_THEMES - 1);
  free(V.text), V.text = NULL;
  {
    char path[64];
    map_path(V.m.num, path, sizeof path);
    size_t len = 0;
    char *b = cw_vfs_exists(path) ? (char *)cw_vfs_read(path, &len) : NULL;
    if (b) V.text = malloc(len + 1), memcpy(V.text, b, len), V.text[len] = 0, free(b);
  }
  if (!mapbuild_build(V.root, &V.m, &V.b)) return;
  V.have_map = true;
  V.map_built = s->m.num;
  cw_info("towers: map %d (%s, %d x %d tiles) built", V.m.num, MP_THEME_ID[V.theme], V.m.w, V.m.h);
}

bool view_lightmap(cw_tree *t, const char *bake_out) {
  if (!V.have_map || V.lit) return V.lit;
  V.lit = true;
  return mapbuild_lightmap(t, &V.b, bake_out, V.text);
}
t3_color view_light_at(float x, float z) {
  t3_color c = V.have_map ? mapbuild_light_at(&V.b, x, z) : (t3_color){ 1, 1, 1 };
  float m = CW_MAX(c.r, CW_MAX(c.g, c.b));
  const float LO = 0.6f;
  if (m < LO) c = m > 1e-3f ? (t3_color){ c.r * LO / m, c.g * LO / m, c.b * LO / m } : (t3_color){ LO, LO, LO };
  return c;
}

/* ── the camera: fixed over the whole map ─────────────────────────────── */

#define CAM_FOV 14.0f
void view_camera(const sim *s, cw_node camera, float aspect) {
  const tdmap *m = &s->m;
  float th = tanf(CAM_FOV * 3.14159265f / 360.0f);
  /* the map and a little round it on screen: its height with room for the HUD's top line, its width */
  float hh = (float)m->h * MP_TILE * 0.5f * 1.1f, hw = (float)m->w * MP_TILE * 0.5f * 1.02f;
  float d = CW_MAX(hh / th, hw / (th * aspect)) * g_cam_k;
  t3_vec3 at = { g_cam_x, 0, 0.5f + g_cam_z };
  float tl = g_tilt * 3.14159265f / 180.0f;
  t3_vec3 eye = { at.x, cw_cos(tl) * d, at.z + cw_sin(tl) * d };
  cw_node3d_set_position(camera, eye);
  cw_node_set(camera, "fov", cw_val_float(CAM_FOV));
  cw_node_set(camera, "near", cw_val_float(d * 0.75f));
  cw_node_set(camera, "far", cw_val_float(d * 1.3f));
  cw_node3d_look_at(camera, at, (t3_vec3){ 0, 1, 0 });
  V.eye = eye, V.at = at, V.aspect = aspect, V.dist = d;
}

bool view_screen(t3_vec3 p, float *u, float *v) {
  if (V.aspect <= 0) return false;
  t3_vec3 f = { V.at.x - V.eye.x, V.at.y - V.eye.y, V.at.z - V.eye.z };
  float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
  if (fl < 1e-4f) return false;
  f.x /= fl, f.y /= fl, f.z /= fl;
  const t3_vec3 r = { 1, 0, 0 };
  t3_vec3 up = { r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
  t3_vec3 d = { p.x - V.eye.x, p.y - V.eye.y, p.z - V.eye.z };
  float z = d.x * f.x + d.y * f.y + d.z * f.z;
  if (z <= 0.01f) return false;
  float th = tanf(CAM_FOV * 3.14159265f / 360.0f);
  float x = (d.x * r.x + d.y * r.y + d.z * r.z) / (z * th * V.aspect), y = (d.x * up.x + d.y * up.y + d.z * up.z) / (z * th);
  *u = 0.5f + 0.5f * x, *v = 0.5f - 0.5f * y;
  return true;
}
float view_px_per_m(float canvas_h) {
  float th = tanf(CAM_FOV * 3.14159265f / 360.0f);
  return V.dist > 0 ? canvas_h / (2.0f * V.dist * th) : 20;
}

view_stats view_get_stats(void) { return V.st; }

t3_vec3 view_muzzle(const sim *s, int slot) {
  const tower *t = &s->tw[slot];
  int k = t->kind, l = CW_CLAMP(t->level, 1, 3) - 1;
  static const float TOP[3] = { 1.0f, 1.25f, 1.5f };
  const float *mz = V.muzzle[k][l];
  float yaw = k == TK_BEAM || k == TK_MORTAR ? -t->yaw - 1.5707963f : 0;
  float c = cw_cos(yaw), sn = cw_sin(yaw);
  float gx = (mz[0] * c + mz[2] * sn) * TSC, gz = (-mz[0] * sn + mz[2] * c) * TSC;
  return (t3_vec3){ t->x + gx, MP_GROUND_H + PAD_H + (TOP[l] + mz[1]) * TSC, t->y + gz };
}

static void column(float x, float y, float z, float h, float r, t3_color c) {
  if (!V.column || V.nbeam_col >= V.column->instance_matrix->count) return;
  put_s(V.column, V.nbeam_col, x, y, z, 0, r, h, r);
  t3_instanced_mesh_set_color_at(V.column, V.nbeam_col++, c);
}
static void ring(float x, float y, float z, float r, t3_color c) {
  if (!V.ring || V.nring >= V.ring->instance_matrix->count) return;
  put(V.ring, V.nring, x, y, z, 0, r);
  t3_instanced_mesh_set_color_at(V.ring, V.nring++, c);
}
static void shadow_at(float x, float z, float r, float strength) {
  cw_splats_add(V.shadows, (t3_vec3){ x, 0, z }, 2.3f * r, 2.0f * r, 0, 0, (t3_color){ 1, 1, 1 }, strength);
}

/* ── per frame ──────────────────────────────────────────────────────── */

static void sync_creeps(const sim *s, float time) {
  static int cnt[CK_KINDS][MB_MAXF];
  memset(cnt, 0, sizeof cnt);
  int drawn = 0;
  static const float RATE[CK_KINDS] = { 2.6f, 1.5f, 2.4f, 1.8f, 1.4f, 1.0f, 1.2f, 0.9f };
  static const float SIZE[CK_KINDS] = { 0.9f, 1.05f, 1.1f, 1.0f, 1.15f, 1.45f, 1.15f, 1.65f }; /* small swarms, fat bloaters, big ogres */
  for (int i = 0; i < MAXE; i++) {
    const creep *e = &s->e[i];
    if (!e->alive) continue;
    kindv *kv = &V.k[e->kind];
    if (kv->nwalk <= 0) continue;
    float spd = e->frozen ? 0 : e->slow_t > 0 ? 1 - e->slow_k : 1;
    float ph = time * RATE[e->kind] * spd * (0.85f + 0.3f * h01((uint32_t)i, 3)) + h01((uint32_t)i, 4) * 8.0f;
    int fr = (int)((ph - floorf(ph)) * (float)kv->nwalk);
    if (e->frozen > 0) fr = 0;
    t3_instanced_mesh *im = kv->im[fr];
    if (!im || cnt[e->kind][fr] >= im->instance_matrix->count) continue;
    int n = cnt[e->kind][fr]++;
    float sc = SIZE[e->kind] * (e->boss ? 2.2f : 1.0f) * (e->flash > 0 ? 1.08f : 1.0f);
    float yaw = -e->face + 1.5708f;
    float y = e->kind == CK_SHADE ? 0.12f + 0.08f * cw_sin(time * 2.2f + (float)i) : 0;
    put_up(im, n, e->x, y, e->y, yaw, sc);
    t3_color gl = view_light_at(e->x, e->y);
    float lm = CW_MAX(gl.r, CW_MAX(gl.g, gl.b)), lk = lm > 1.4f ? 1.4f / lm : 1.0f;
    const float *kb = KIND_BODY[e->kind];
    t3_color c = { gl.r * lk * kb[0], gl.g * lk * kb[1], gl.b * lk * kb[2] };
    if (e->flash > 0) c = (t3_color){ 3.5f, 3.0f, 2.6f };
    else if (e->frozen > 0) c = (t3_color){ 1.4f, 1.9f, 2.6f };
    else if (e->slow_t > 0) c.r *= 0.6f, c.g *= 0.9f, c.b *= 1.7f;
    if (e->boss && e->flash <= 0) c.r *= 1.3f;
    t3_instanced_mesh_set_color_at(im, n, c);
    if (e->boss || e->kind == CK_OGRE || e->kind == CK_BLOATER) shadow_at(e->x, e->y, 0.4f * sc, 0.55f);
    else if (e->kind != CK_SKITTER && ((i & 1) == 0 || s->nenemies < 1200)) shadow_at(e->x, e->y, 0.3f * sc, 0.4f); /* (the swarm casts none) */
    drawn++;
  }
  for (int k = 0; k < CK_KINDS; k++)
    for (int f = 0; f < V.k[k].nwalk && f < MB_MAXF; f++) {
      done(V.k[k].im[f], cnt[k][f]);
      if (V.k[k].edge[f]) V.k[k].edge[f]->count = cnt[k][f];
    }
  V.st.creeps_drawn = drawn;
}

static void sync_towers(const sim *s, float time) {
  static const float TOP[3] = { 1.0f, 1.25f, 1.5f };
  int nb[TK_KINDS][3] = { { 0 } }, nh[TK_KINDS][3] = { { 0 } }, npad = 0, nbeam = 0, nlight = 0;
  for (int k = 0; k < s->m.nslot; k++) {
    float x = mp_wx(&s->m, s->m.slot[k].x), z = mp_wz(&s->m, s->m.slot[k].y);
    const tower *t = &s->tw[k];
    int own = t->alive ? t->owner : sim_slot_owner(s, k);
    const float *pc = PCOL[own];
    t3_color gl = view_light_at(x, z);
    if (V.pad && npad < MP_MAXSLOT) {
      put(V.pad, npad, x, MP_GROUND_H, z, (float)(k % 4) * 1.5707963f, 1);
      t3_instanced_mesh_set_color_at(V.pad, npad, gl);
      if (V.pad_trim) put(V.pad_trim, npad, x, MP_GROUND_H, z, (float)(k % 4) * 1.5707963f, 1), t3_instanced_mesh_set_color_at(V.pad_trim, npad, (t3_color){ lin(pc[0], pc[1], pc[2]).r * 1.8f, lin(pc[0], pc[1], pc[2]).g * 1.8f, lin(pc[0], pc[1], pc[2]).b * 1.8f });
      if (V.pad_glow) {
        float p = t->alive ? 0.4f : 0.9f + 0.3f * cw_sin(time * 2.0f + (float)k);
        put(V.pad_glow, npad, x, MP_GROUND_H, z, (float)(k % 4) * 1.5707963f, 1);
        t3_color pl = lin(pc[0], pc[1], pc[2]);
        t3_instanced_mesh_set_color_at(V.pad_glow, npad, (t3_color){ pl.r * 2 * p, pl.g * 2 * p, pl.b * 2 * p });
      }
      npad++;
    }
    if (!t->alive) continue;
    int L = CW_CLAMP(t->level, 1, 3) - 1, kd = t->kind;
    partv *b = &V.base[kd], *h = &V.head[kd];
    int fb = CW_MIN(L, b->n - 1), fh = CW_MIN(L, h->n - 1);
    float grow = t->age < 20 ? 0.3f + 0.7f * (float)t->age / 20.0f : 1.0f; /* rising out of the pad */
    const float *tc = TCOL[kd];
    float pulse = 1.0f;
    if (kd == TK_BOLT) pulse = 1.0f + 2.5f * CW_MAX(0.0f, 1.0f - (float)(TOWER[TK_BOLT].period[L] - t->cd) / 12.0f) + 0.25f * cw_sin(time * 9 + (float)k);
    else if (kd == TK_FROST) pulse = 1.0f + 0.4f * cw_sin(time * 2.5f + (float)k);
    else if (kd == TK_BEAM) pulse = t->tgt >= 0 ? 1.4f + 0.8f * (t->beam_k - 1) : 0.8f;
    else pulse = 1.0f + 2.0f * CW_MAX(0.0f, 1.0f - (float)(TOWER[TK_MORTAR].period[L] - t->cd) / 20.0f);
    t3_color glc = lin(tc[0], tc[1], tc[2]);
    glc = (t3_color){ glc.r * 2.4f * pulse, glc.g * 2.4f * pulse, glc.b * 2.4f * pulse };
    if (fb >= 0 && b->surf[fb] && nb[kd][fb] < MAXT) {
      int n = nb[kd][fb]++;
      put_s(b->surf[fb], n, x, MP_GROUND_H + PAD_H, z, 0, TSC, TSC * grow, TSC);
      t3_instanced_mesh_set_color_at(b->surf[fb], n, gl);
      if (b->glow[fb]) put_s(b->glow[fb], n, x, MP_GROUND_H + PAD_H, z, 0, TSC, TSC * grow, TSC), t3_instanced_mesh_set_color_at(b->glow[fb], n, glc);
      if (b->trim[fb]) put_s(b->trim[fb], n, x, MP_GROUND_H + PAD_H, z, 0, TSC, TSC * grow, TSC), t3_instanced_mesh_set_color_at(b->trim[fb], n, lin(pc[0], pc[1], pc[2]));
    }
    float yaw = kd == TK_BEAM || kd == TK_MORTAR ? -t->yaw - 1.5707963f : kd == TK_BOLT ? time * 0.8f + (float)k : time * 0.4f + (float)k;
    float hy = MP_GROUND_H + PAD_H + TOP[L] * grow * TSC;
    if (kd == TK_MORTAR) hy -= 0.08f * CW_MAX(0.0f, 1.0f - (float)(TOWER[TK_MORTAR].period[L] - t->cd) / 10.0f); /* the recoil */
    if (fh >= 0 && h->surf[fh] && nh[kd][fh] < MAXT) {
      int n = nh[kd][fh]++;
      put(h->surf[fh], n, x, hy, z, yaw, grow * TSC);
      t3_instanced_mesh_set_color_at(h->surf[fh], n, gl);
      if (h->glow[fh]) put(h->glow[fh], n, x, hy, z, yaw, grow * TSC), t3_instanced_mesh_set_color_at(h->glow[fh], n, glc);
      if (h->trim[fh]) put(h->trim[fh], n, x, hy, z, yaw, grow * TSC), t3_instanced_mesh_set_color_at(h->trim[fh], n, lin(pc[0], pc[1], pc[2]));
    }
    /* a shadow cast away from the sun (north-west light: south-east of the tower), and the tower's glow on the ground */
    cw_splats_add(V.shadows, (t3_vec3){ x + 0.5f, 0, z + 0.45f }, 2.4f, 1.8f, 0.6f, 0, (t3_color){ 1, 1, 1 }, 0.75f);
    if (kd == TK_FROST) {
      float r = TOWER[TK_FROST].range[L];
      cw_splats_add(V.glows, (t3_vec3){ x, 0, z }, r * 2, r * 2, time * 0.3f, 0, (t3_color){ 0.25f, 0.65f, 1.0f }, 0.35f + 0.1f * cw_sin(time * 2.5f + (float)k));
    } else
      cw_splats_add(V.glows, (t3_vec3){ x, 0, z }, 2.6f, 2.6f, 0, 0, lin(tc[0], tc[1], tc[2]), 0.35f * pulse);
    cw_splats_add(V.glows, (t3_vec3){ x, 0, z }, 3.2f, 3.2f, 0, 0, lin(pc[0], pc[1], pc[2]), 0.9f); /* a lit disc of the owner's colour under every tower */
    /* the beam: from the emitter to the target, white-hot at its core, wider and hotter as it ramps */
    if (kd == TK_BEAM && t->tgt >= 0 && s->e[t->tgt].alive && nbeam < MAXT) {
      const creep *e = &s->e[t->tgt];
      t3_vec3 a = view_muzzle(s, k), bpt = { e->x, 0.7f, e->y };
      float ramp = (t->beam_k - 1) * 0.5f, fl = 0.85f + 0.15f * cw_sin(time * 47 + (float)k * 3);
      if (TOWER[TK_BEAM].extra[L]) { /* the pierce: on to the edge of its range */
        float dx = bpt.x - a.x, dz = bpt.z - a.z, l = sqrtf(dx * dx + dz * dz) + 1e-4f, r = TOWER[TK_BEAM].range[L];
        float ext = CW_MAX(r - sqrtf((e->x - t->x) * (e->x - t->x) + (e->y - t->y) * (e->y - t->y)), 0.0f);
        bpt = (t3_vec3){ bpt.x + dx / l * ext, 0.5f, bpt.z + dz / l * ext };
      }
      put_seg(V.beam_core, nbeam, a, bpt, (0.05f + 0.05f * ramp) * fl);
      t3_instanced_mesh_set_color_at(V.beam_core, nbeam, (t3_color){ 6 + 4 * ramp, 5 + 3 * ramp, 6 + 4 * ramp });
      put_seg(V.beam_glow, nbeam, a, bpt, (0.16f + 0.14f * ramp) * fl);
      t3_instanced_mesh_set_color_at(V.beam_glow, nbeam, (t3_color){ glc.r * 0.45f, glc.g * 0.45f, glc.b * 0.45f });
      nbeam++;
      cw_splats_add(V.glows, (t3_vec3){ e->x, 0, e->y }, 2.2f + ramp, 2.2f + ramp, time * 3, 0, lin(tc[0], tc[1], tc[2]), 1.2f + ramp);
      if (nlight < 8) {
        cw_node3d_set_position(V.beam_light[nlight], (t3_vec3){ e->x, 1.0f, e->y });
        cw_node_set(V.beam_light[nlight], "color", cw_val_color(tc[0], tc[1], tc[2], 1));
        cw_node_set(V.beam_light[nlight], "energy", cw_val_float((18 + 14 * ramp) * fl));
        nlight++;
      }
    }
  }
  for (int i = nlight; i < 8; i++) cw_node_set(V.beam_light[i], "energy", cw_val_float(0));
  for (int kd = 0; kd < TK_KINDS; kd++)
    for (int l = 0; l < 3; l++) {
      done(V.base[kd].surf[l], nb[kd][l]), done(V.base[kd].glow[l], nb[kd][l]), done(V.base[kd].trim[l], nb[kd][l]);
      done(V.head[kd].surf[l], nh[kd][l]), done(V.head[kd].glow[l], nh[kd][l]), done(V.head[kd].trim[l], nh[kd][l]);
    }
  done(V.pad, npad), done(V.pad_glow, npad), done(V.pad_trim, npad);
  done(V.beam_core, nbeam), done(V.beam_glow, nbeam);
  int tw = 0;
  for (int k = 0; k < s->m.nslot; k++) tw += s->tw[k].alive;
  V.st.towers = tw;
}

static void sync_core_gates(const sim *s, float time) {
  float cx = s->core_x, cz = s->core_y;
  int f = s->core_hp > CORE_HP * 3 / 4 ? 0 : s->core_hp > CORE_HP / 2 ? 1 : s->core_hp > 0 ? 2 : 3;
  f = CW_MIN(f, V.core_frames - 1);
  for (int k = 0; k < V.core_frames; k++) done(V.core[k], 0), done(V.core_glow[k], 0);
  if (f >= 0 && V.core[f]) {
    float bob = 0.05f * cw_sin(time * 1.3f);
    put(V.core[f], 0, cx, bob, cz, time * 0.15f, 1);
    t3_instanced_mesh_set_color_at(V.core[f], 0, view_light_at(cx, cz));
    done(V.core[f], 1);
    if (V.core_glow[f]) {
      float hurt = (float)s->core_hp / (float)CORE_HP, p = 0.8f + 0.3f * cw_sin(time * (2.0f + 4.0f * (1 - hurt)));
      put(V.core_glow[f], 0, cx, bob, cz, time * 0.15f, 1);
      t3_instanced_mesh_set_color_at(V.core_glow[f], 0, (t3_color){ (0.4f + 1.6f * (1 - hurt)) * p, (1.4f * hurt + 0.3f) * p, (2.2f * hurt + 0.3f) * p });
      done(V.core_glow[f], 1);
    }
  }
  float hurt = (float)s->core_hp / (float)CORE_HP;
  t3_color cc = { 0.3f + 0.7f * (1 - hurt), 0.6f * hurt + 0.2f, 1.0f * hurt + 0.2f };
  cw_splats_add(V.glows, (t3_vec3){ cx, 0, cz }, 6, 6, 0, 0, cc, 0.8f + 0.2f * cw_sin(time * 2));
  column(cx, 0.02f, cz, 7, 1.6f, (t3_color){ cc.r * 0.35f, cc.g * 0.35f, cc.b * 0.35f });
  cw_node3d_set_position(V.core_light, (t3_vec3){ cx, 3, cz });
  cw_node_set(V.core_light, "color", cw_val_color(cc.r, cc.g, cc.b, 1));
  cw_node_set(V.core_light, "energy", cw_val_float(22));
  shadow_at(cx, cz, 1.6f, 0.6f);
  for (int g = 0; g < s->m.nspawn && V.portal; g++) {
    float x, z;
    mp_gate_pos(&s->m, g, &x, &z);
    float yaw = mp_gate_side(&s->m, g) ? 1.5707963f : 0; /* the gate faces along its path into the map */
    put(V.portal, g, x, 0, z, yaw, 1);
    t3_instanced_mesh_set_color_at(V.portal, g, view_light_at(x, z));
    bool live = false;
    for (int k = 0; k < MAXGRP; k++) live |= s->grp[k].live && s->grp[k].spawn == g;
    float p = (live ? 2.2f : 1.0f) + 0.4f * cw_sin(time * 3 + (float)g);
    if (V.portal_glow) put(V.portal_glow, g, x, 0, z, yaw, 1), t3_instanced_mesh_set_color_at(V.portal_glow, g, (t3_color){ 2.0f * p, 0.35f * p, 1.6f * p });
    cw_splats_add(V.glows, (t3_vec3){ x, 0, z }, 4.5f, 4.5f, time, 0, (t3_color){ 1.0f, 0.25f, 0.8f }, 0.6f * p);
    cw_node3d_set_position(V.portal_light[g], (t3_vec3){ x, 2.2f, z });
    cw_node_set(V.portal_light[g], "color", cw_val_color(1, 0.3f, 0.85f, 1));
    cw_node_set(V.portal_light[g], "energy", cw_val_float(18 * p));
  }
  done(V.portal, s->m.nspawn), done(V.portal_glow, s->m.nspawn);
  for (int g = s->m.nspawn; g < MP_MAXSPAWN; g++) cw_node_set(V.portal_light[g], "energy", cw_val_float(0));
}

static void sync_shells(const sim *s, float time) {
  int n = 0;
  for (int k = 0; k < MAXSH && V.shell; k++) {
    const shell *sh = &s->sh[k];
    if (!sh->alive || n >= V.shell->instance_matrix->count) continue;
    float t = (float)sh->t / (float)CW_MAX(sh->T, 1), x = sh->x0 + (sh->tx - sh->x0) * t, z = sh->y0 + (sh->ty - sh->y0) * t;
    float dist = sqrtf((sh->tx - sh->x0) * (sh->tx - sh->x0) + (sh->ty - sh->y0) * (sh->ty - sh->y0));
    float y0 = sh->T > 30 ? MP_GROUND_H + 1.8f : 0.6f, peak = sh->T > 30 ? 3.0f + dist * 0.45f : 1.5f;
    float y = y0 * (1 - t) + 4.0f * peak * t * (1 - t) + 0.4f * t;
    put(V.shell, n, x, y, z, time * 6 + (float)k, sh->T > 30 ? 1.0f : 0.7f);
    t3_instanced_mesh_set_color_at(V.shell, n, (t3_color){ 1, 1, 1 });
    if (V.shell_glow) put(V.shell_glow, n, x, y, z, time * 6 + (float)k, sh->T > 30 ? 1.0f : 0.7f), t3_instanced_mesh_set_color_at(V.shell_glow, n, (t3_color){ 4, 1.8f, 0.4f });
    shadow_at(x, z, 0.3f, 0.5f * (0.4f + 0.6f * t));
    n++;
  }
  done(V.shell, n), done(V.shell_glow, n);
  V.st.shells = n;
}

static void sync_bolts(void) {
  const float *seg;
  int ns = fx_bolt_segments(&seg), n = 0;
  for (int i = 0; i < ns && n < V.bolt_core->instance_matrix->count; i++) {
    const float *q = seg + i * 7;
    t3_vec3 a = { q[0], q[1], q[2] }, b = { q[3], q[4], q[5] };
    float w = q[6];
    put_seg(V.bolt_core, n, a, b, 0.06f * w);
    t3_instanced_mesh_set_color_at(V.bolt_core, n, (t3_color){ 7 * w, 8 * w, 12 * w });
    put_seg(V.bolt_glow, n, a, b, 0.24f * w);
    t3_instanced_mesh_set_color_at(V.bolt_glow, n, (t3_color){ 0.35f * w, 0.6f * w, 1.6f * w });
    n++;
  }
  done(V.bolt_core, n), done(V.bolt_glow, n);
  V.st.zaps = n;
}

static void sync_cursors(const sim *s, float time, const bool show[MAXP]) {
  int nm = 0;
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active || p->slot < 0 || !show[i] || g_hide_cursors) continue;
    float x = mp_wx(&s->m, s->m.slot[p->slot].x), z = mp_wz(&s->m, s->m.slot[p->slot].y);
    t3_color pc = lin(PCOL[i][0], PCOL[i][1], PCOL[i][2]);
    float pulse = 1.6f + 0.6f * cw_sin(time * 6 + (float)i);
    ring(x, MP_GROUND_H + 0.06f, z, 1.15f, (t3_color){ pc.r * 3 * pulse, pc.g * 3 * pulse, pc.b * 3 * pulse });
    ring(x, MP_GROUND_H + 0.06f, z, 1.05f, (t3_color){ pc.r * 2 * pulse, pc.g * 2 * pulse, pc.b * 2 * pulse });
    const tower *t = &s->tw[p->slot];
    if (t->alive) { /* its reach */
      float r = sim_tower_range(t->kind, t->level);
      ring(t->x, 0.08f, t->y, r, (t3_color){ pc.r * 0.9f, pc.g * 0.9f, pc.b * 0.9f });
      if (t->level < 3) ring(t->x, 0.08f, t->y, sim_tower_range(t->kind, t->level + 1), (t3_color){ pc.r * 0.3f, pc.g * 0.3f, pc.b * 0.3f });
    }
    if (V.marker && nm < MAXP) {
      float top = t->alive ? MP_GROUND_H + 2.9f + 0.3f * (float)t->level : MP_GROUND_H + 1.6f;
      put(V.marker, nm, x, top + 0.25f * cw_sin(time * 4 + (float)i), z, time * 2, 1);
      t3_instanced_mesh_set_color_at(V.marker, nm++, (t3_color){ pc.r * 2.5f, pc.g * 2.5f, pc.b * 2.5f });
    }
    column(x, MP_GROUND_H, z, 3.5f, 0.9f, (t3_color){ pc.r * 0.4f, pc.g * 0.4f, pc.b * 0.4f });
  }
  done(V.marker, nm);
}

void view_sync(const sim *s, float time, const bool show_cursor[MAXP]) {
  if (!V.root.id) return;
  cw_splats_begin(V.shadows);
  cw_splats_begin(V.glows);
  V.nbeam_col = 0, V.nring = 0;
  sync_towers(s, time);
  sync_core_gates(s, time);
  sync_creeps(s, time);
  sync_shells(s, time);
  sync_bolts();
  sync_cursors(s, time, show_cursor);
  cw_splats_end(V.shadows);
  cw_splats_end(V.glows);
  done(V.column, V.nbeam_col);
  done(V.ring, V.nring);
  { /* development (A/B timing): TOWERS_HIDE bits hide 1 the crowd, 2 the blob shadows, 4 the glows, 16 the crowd's edges */
    static int hide = -1;
    if (hide < 0) hide = getenv("TOWERS_HIDE") ? atoi(getenv("TOWERS_HIDE")) : 0;
    if (hide & 1) for (int k = 0; k < CK_KINDS; k++) for (int f = 0; f < MB_MAXF; f++) { if (V.k[k].im[f]) V.k[k].im[f]->count = 0; if (V.k[k].edge[f]) V.k[k].edge[f]->count = 0; }
    if (hide & 16) for (int k = 0; k < CK_KINDS; k++) for (int f = 0; f < MB_MAXF; f++) if (V.k[k].edge[f]) V.k[k].edge[f]->count = 0;
    if (hide & 2) cw_splats_mesh(V.shadows)->count = 0;
    if (hide & 4) cw_splats_mesh(V.glows)->count = 0;
  }
  cw_splats_begin(V.pools);
  cw_splats_add_lights(V.pools, cw_node_parent(V.root), 0, 0.12f / 3.14159265f);
  cw_splats_end(V.pools);
}
