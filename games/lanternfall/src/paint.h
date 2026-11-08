/* lanternfall: a small painter for the art made in code (paint.c). Shapes are signed distance fields painted into
 * float canvases of colour, coverage, height and material; each shape is shaded as a rounded form (a value ramp from
 * its cool edge to its lit middle, a soft key light from above), carries a material texture (fur, chitin, scales,
 * feathers, cloth, brass, glass, bark, skin...), casts a soft contact shadow on what is under it, and may glow. A
 * finished canvas becomes a colour texture with a normal map (from the height) and a material map (emission,
 * specular, gloss), painted for each facing (the renderer does not turn a mirrored sprite's normals).
 * Render code: libm is fine here. */
#ifndef LANTERNFALL_PAINT_H
#define LANTERNFALL_PAINT_H

#include "cartwheel/render2d.h"

/* material textures */
enum { TX_NONE, TX_FUR, TX_CHITIN, TX_SCALES, TX_WARTS, TX_VEINS, TX_FEATHER, TX_METAL, TX_CLOTH, TX_GLASS, TX_BARK, TX_SKIN, TX_MOSS,
       TX_MEMBRANE, TX_STONE };

typedef struct pstyle {
  cw_color base;   /* the form's colour in light */
  cw_color shade;  /* toward its edge (cooler, more saturated); zero = base darkened */
  cw_color hi;     /* the lit middle; zero = base lightened a little */
  float dome;      /* how round the form is (height at its middle); 0 = flat */
  float lift;      /* height of its edge (stacking: plates over a body) */
  float shadow;    /* contact shadow on what is under (0..1) */
  float shadow_r;  /* its reach, pixels; 0 = 10 */
  int tex;         /* TX_* */
  float tex_scale; /* pixels per texture feature; 0 = 8 */
  float tex_amt;   /* 0..1 */
  float tex_dir;   /* radians: the grain's direction (fur, feathers, brushed metal); 0 = down */
  float em;        /* emission share 0..1 (eyes, glands, cores) */
  float spec, gloss;
  float ink;       /* a dark line round the inside of the edge, pixels */
  float alpha;     /* 0 = 1 */
  float rim;       /* a lit rim on the top edge, 0..1 (cut-out forms against the dark) */
  float soft;      /* edge softness, pixels; 0 = 1.2 */
  uint32_t seed;
} pstyle;

typedef struct pcan {
  int w, h;
  float *c;              /* premultiplied rgba */
  float *ht, *em, *sp, *gl;
} pcan;

pcan *pc_new(int w, int h);
void pc_free(pcan *p);

/* shapes (pixels, y down) */
void pc_ellipse(pcan *p, float cx, float cy, float rx, float ry, float rot, const pstyle *s);
void pc_capsule(pcan *p, float ax, float ay, float bx, float by, float ra, float rb, const pstyle *s); /* tapered */
void pc_poly(pcan *p, const float *xy, int n, float round, const pstyle *s);                           /* round: corner radius */
/* a curved tapered stroke through n points (a Catmull-Rom path), radius from r0 to r1 */
void pc_stroke(pcan *p, const float *xy, int n, float r0, float r1, const pstyle *s);
/* a curved leaf / blade / feather between a and b bulging w at its middle (bend: sideways bow of its spine) */
void pc_leaf(pcan *p, float ax, float ay, float bx, float by, float w, float bend, const pstyle *s);

/* the finished art: a texture for each facing (left mirrored, its normals mirrored too), mipmapped */
typedef struct ptex { cw_texture t[2]; int w, h; } ptex; /* t[0] facing right, t[1] facing left */
ptex pc_finish(pcan *p, float normal_k);

/* noise (also for callers): value noise 0..1, fbm */
float pn_noise(float x, float y, uint32_t s);
float pn_fbm(float x, float y, uint32_t s, int oct);
float pn_hash(int x, int y, uint32_t s);

static inline cw_color pc_hex(uint32_t c) { return (cw_color){ (float)((c >> 16) & 255) / 255.0f, (float)((c >> 8) & 255) / 255.0f, (float)(c & 255) / 255.0f, 1 }; }
static inline cw_color pc_mix(cw_color a, cw_color b, float t) { return (cw_color){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t }; }
static inline cw_color pc_scale(cw_color a, float k) { return (cw_color){ a.r * k, a.g * k, a.b * k, a.a }; }

#endif
