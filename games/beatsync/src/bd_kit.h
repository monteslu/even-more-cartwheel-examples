/* The backdrops' building blocks (src/backdrop.c, src/worlds.c): textures made from a function, additive light
 * quads and instanced sprites, placing and aiming, a hash for scattering things. Each world builds its parts from
 * these in its own Node3D root (cw_node3d_object of the Backdrop's world). */
#ifndef BEATSYNC_BD_KIT_H
#define BEATSYNC_BD_KIT_H
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "cartwheel/core.h"
#include "cartwheel/scene.h"

#define PI 3.14159265f

/* ── building blocks ─────────────────────────────────────────────────── */

static inline void setf(cw_node n, const char *p, double v) { cw_node_set(n, p, cw_val_float(v)); }
static inline void setc(cw_node n, const char *p, float r, float g, float b) { cw_node_set(n, p, cw_val_color(r, g, b, 1)); }


/* a texture from a function of (u, v), v 0 at the top: white light whose brightness is the function, alpha 1 */
typedef float (*tex_fn)(float u, float v);
static inline t3_texture *texture(int w, int h, tex_fn f) {
  uint8_t *px = malloc((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float k = f(((float)x + 0.5f) / (float)w, ((float)y + 0.5f) / (float)h);
      uint8_t c = (uint8_t)lrintf(fminf(1, fmaxf(0, k)) * 255.0f);
      uint8_t *p = px + ((size_t)y * w + x) * 4;
      p[0] = p[1] = p[2] = c, p[3] = 255;
    }
  t3_texture *t = t3_texture_new(w, h, px);
  free(px);
  return t;
}
typedef void (*tex_rgb_fn)(float u, float v, float out[3]);
static inline t3_texture *texture_rgb(int w, int h, tex_rgb_fn f) {
  uint8_t *px = malloc((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float c[3];
      f(((float)x + 0.5f) / (float)w, ((float)y + 0.5f) / (float)h, c);
      uint8_t *p = px + ((size_t)y * w + x) * 4;
      for (int k = 0; k < 3; k++) p[k] = (uint8_t)lrintf(fminf(1, fmaxf(0, c[k])) * 255.0f);
      p[3] = 255;
    }
  t3_texture *t = t3_texture_new(w, h, px);
  free(px);
  return t;
}

/* a texture with its own alpha: the function writes r, g, b, a */
typedef void (*tex_rgba_fn)(float u, float v, float out[4]);
static inline t3_texture *texture_rgba(int w, int h, tex_rgba_fn f) {
  uint8_t *px = malloc((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float c[4];
      f(((float)x + 0.5f) / (float)w, ((float)y + 0.5f) / (float)h, c);
      uint8_t *p = px + ((size_t)y * w + x) * 4;
      for (int q = 0; q < 4; q++) p[q] = (uint8_t)lrintf(fminf(1, fmaxf(0, c[q])) * 255.0f);
    }
  t3_texture *t = t3_texture_new(w, h, px);
  free(px);
  return t;
}

/* additive light, its colour set every frame */
static inline t3_material *light_mat(t3_texture *tex) {
  t3_material *m = t3_mesh_basic_material_new(0x000000);
  m->blending = T3_ADDITIVE_BLENDING, m->transparent = true, m->depth_write = false;
  if (tex) t3_material_set_map(m, tex), t3_release(tex);
  return m;
}
static inline t3_mesh *add_mesh(t3_object *at, t3_geometry *g, t3_material *m, t3_vec3 pos) {
  t3_mesh *q = t3_mesh_new(g, m);
  t3_release(g), t3_release(m);
  T3_OBJ(q)->position = pos;
  T3_OBJ(q)->frustum_culled = false;
  t3_object_add(at, q);
  t3_release(q);
  return q;
}
static inline t3_mesh *quad(t3_object *at, float w, float h, t3_texture *tex, t3_vec3 pos) {
  return add_mesh(at, t3_plane_geometry_new(w, h, 1, 1), light_mat(tex), pos);
}
static inline void tint(t3_mesh *q, float r, float g, float b, float k) { q->materials[0]->color = (t3_color){ r * k, g * k, b * k }; }
static inline t3_instanced_mesh *many_m(t3_object *at, t3_geometry *g, t3_material *m, int n) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  t3_release(g), t3_release(m);
  T3_OBJ(im)->frustum_culled = false;
  t3_object_add(at, im);
  t3_release(im);
  return im;
}
static inline t3_instanced_mesh *many(t3_object *at, t3_geometry *g, t3_texture *tex, int n) {
  t3_material *m = light_mat(tex);
  m->color = (t3_color){ 1, 1, 1 };
  return many_m(at, g, m, n);
}
static inline void place(t3_instanced_mesh *im, int i, t3_vec3 p, t3_quat q, t3_vec3 s, t3_color c) {
  t3_mat4 m;
  t3_mat4_compose(&m, p, q, s);
  t3_instanced_mesh_set_matrix_at(im, i, &m);
  t3_instanced_mesh_set_color_at(im, i, c);
}
/* a beam: a quad from 'from' along unit 'dir' for len, turned about its own axis to face the camera at 'eye' */
static inline void place_beam(t3_instanced_mesh *im, int i, t3_vec3 from, t3_vec3 dir, float len, float width, t3_vec3 eye, t3_color c) {
  t3_vec3 mid = { from.x + dir.x * len * 0.5f, from.y + dir.y * len * 0.5f, from.z + dir.z * len * 0.5f };
  t3_vec3 tc = { eye.x - mid.x, eye.y - mid.y, eye.z - mid.z };
  /* x = tc x dir: with y along -dir that leaves +z (the quad's front) toward the camera */
  t3_vec3 x = { tc.y * dir.z - tc.z * dir.y, tc.z * dir.x - tc.x * dir.z, tc.x * dir.y - tc.y * dir.x };
  float xl = sqrtf(x.x * x.x + x.y * x.y + x.z * x.z);
  if (xl < 1e-5f) x = (t3_vec3){ 1, 0, 0 }, xl = 1;
  x = (t3_vec3){ x.x / xl, x.y / xl, x.z / xl };
  t3_vec3 z = { dir.y * x.z - dir.z * x.y, dir.z * x.x - dir.x * x.z, dir.x * x.y - dir.y * x.x }; /* x * (-dir) */
  /* the plane's +y (v 0, the head) at 'from': y runs from the far end to the head */
  t3_mat4 m = { { x.x * width, x.y * width, x.z * width, 0, -dir.x * len, -dir.y * len, -dir.z * len, 0, z.x, z.y, z.z, 0, mid.x, mid.y, mid.z, 1 } };
  t3_instanced_mesh_set_matrix_at(im, i, &m);
  t3_instanced_mesh_set_color_at(im, i, c);
}
static inline void done(t3_instanced_mesh *im) {
  im->instance_matrix->version++;
  if (im->instance_color) im->instance_color->version++;
}
static inline t3_quat about(float x, float y, float z, float a) { return t3_quat_from_axis_angle((t3_vec3){ x, y, z }, a); }
static inline t3_quat qmul(t3_quat a, t3_quat b) {
  return (t3_quat){ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
static inline uint32_t hsh(uint32_t a, uint32_t b) {
  uint32_t h = a * 374761393u + b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static inline float rnd(uint32_t a, uint32_t b) { return (float)(hsh(a, b) & 0xffffff) / 16777216.0f; }
static inline float smooth(float e0, float e1, float x) {
  float t = fminf(1, fmaxf(0, (x - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
}
/* a node's -Z along a direction (yaw then pitch) */
static inline void aim(cw_node n, t3_vec3 d) {
  float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
  float yaw = atan2f(-d.x, -d.z), pitch = asinf(d.y / fmaxf(len, 1e-6f));
  cw_node3d_set_rotation(n, (t3_vec3){ pitch, yaw, 0 });
}

/* a lamp's head on a light node (a spot): a dark housing along its -Z with a lens at its mouth that glows in the light's
 * colour (an opaque emissive surface: a world's environment capture draws surfaces, not the volumetric beams, so the
 * lens is what a reflection of the world shows of its lamp). Returns the lens's material (its colour set as the light's
 * energy changes) */
static inline t3_material *lamp_head(cw_node light, float radius, float depth) {
  t3_object *at = cw_node3d_object(light);
  if (!at) return NULL;
  t3_material *hm = t3_mesh_standard_material_new(0x000000);
  hm->color = (t3_color){ 0.03f, 0.03f, 0.035f }, hm->roughness = 0.5f, hm->metalness = 0.6f;
  t3_mesh *h = add_mesh(at, t3_cylinder_geometry_new(radius * 1.15f, radius * 0.9f, depth, 20, 1, true), hm, (t3_vec3){ 0, 0, depth * 0.5f });
  T3_OBJ(h)->quaternion = about(1, 0, 0, PI / 2);
  t3_material *lm = t3_mesh_basic_material_new(0x000000);
  lm->color = (t3_color){ 1, 1, 1 };
  t3_retain(lm);
  t3_mesh *lens = add_mesh(at, t3_circle_geometry_new(radius, 24), lm, (t3_vec3){ 0, 0, -0.01f });
  T3_OBJ(lens)->quaternion = about(0, 1, 0, PI); /* facing -Z, down the beam */
  t3_release(lm); /* (the mesh holds it; the caller's pointer stays good while the world does) */
  return lm;
}

/* the textures */
static inline float tx_horizon(float u, float v) { (void)u; return expf(-fabsf(v - 0.5f) * 9.0f); }
static inline float tx_beam(float u, float v) { /* a laser: a hard bright core in a soft sheath, fading along its length */
  float a = expf(-powf((u - 0.5f) * 6.0f, 2)), core = expf(-powf((u - 0.5f) * 30.0f, 2));
  return (0.35f * a + 1.0f * core) * powf(1 - v, 0.7f) * smooth(0, 0.02f, v);
}
static inline float tx_dot(float u, float v) {
  float d = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2;
  return expf(-d * d * 5.0f);
}
static inline float tx_disc(float u, float v) {
  float x = (u - 0.5f) * 2, y = (v - 0.5f) * 2, d = sqrtf(x * x + y * y);
  float edge = 1 - smooth(0.92f, 1.0f, d);
  float maria = 0.86f + 0.14f * sinf(x * 7.1f + 1.3f) * sinf(y * 5.3f - 0.4f) - 0.1f * expf(-((x + 0.3f) * (x + 0.3f) + (y + 0.2f) * (y + 0.2f)) * 9);
  return edge * maria * (0.8f + 0.2f * sqrtf(fmaxf(0, 1 - d * d)));
}
static inline float tx_halo(float u, float v) {
  float d = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2;
  return d >= 1 ? 0 : powf(1 - d, 3.0f);
}
#endif
