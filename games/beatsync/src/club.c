/* The picture (club.h): three rivers of light, a note highway, as flat neon shapes for the ASCII pass.
 *
 * The camera looks straight at the plane z = 0 (main.cwscene: Camera at z 22, 40 degrees): 16 units of height on
 * screen, about 28 of width. Everything is unlit colour (MeshBasicMaterial) on black: a shape's brightness is the
 * character the ASCII pass picks for it, so every effect here is a brightness that rises and falls.
 *
 * The rivers: one per instrument, side by side as the buttons are (kick left, clap middle, bass right), running from
 * the top of the screen down to a gate near the bottom. A river is a current of dim characters streaming down at the
 * notes' speed, between two banks, crossed by a faint line on every beat (brighter on the bar). A note b beats ahead
 * of now sits b beats up its river and rides the current down to the gate, reaching it exactly on its beat. The
 * highway recedes in true perspective, a road going away toward a horizon: a river's far end, high up the screen, is
 * a quarter of its width at the gate, and a note far off is that small and grows all the way to the gate. How many
 * beats a river shows is the view's `ahead`: more on Easy (slower, readable well ahead), fewer on Hard; a note
 * crosses the gate at (TOP_Y - GATE_Y) / (1 - FAR) * (1 / FAR - 1) / ahead units a beat.
 *
 * The mapping: d beats ahead is a scale s = 1 / (1 + c d), 1 at the gate and FAR at the top (c from `ahead`): the
 * depth of a point on a ground plane seen from above it. The height on screen follows (1 - s), so the rivers converge
 * toward a vanishing point above the screen, and x and sizes scale by s. Past the gate (d < 0) the same mapping
 * carries the outflow on down.
 *
 * The land: beside the rivers, out to the screen's edges, a landscape that is the OTHER track (the chords, pads,
 * stabs and synth lines the player does not play), read from the song's look (song.h song_look, analysed when the
 * package is made): a row every half beat riding down with the notes, so what the music will do is seen coming. Each
 * row rises with the track's loudness at its beat and its bass, mid and treble laid outward (bass beside the rivers,
 * treble at the edges); it takes the colour of the harmony sounding then (held from chord change to chord change); a
 * stab puts lights on its ridge; a row where the harmony moves carries a crest of light. A faint sky above the
 * horizon holds the harmony sounding now.
 *
 * The streak: the banks charge with light from the gate up as the streak grows, reaching the far end at the top
 * multiplier; each multiplier step sends a band of light up the rivers; a broken streak's light drains back down the
 * banks and its lit pieces fall away.
 *
 * Grounding: nothing here stands on anything (it is a picture of light): the Field node is in the group airborne. */
#include "club.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"
#include "cartwheel/scene_file.h"

#define VIEW_HALF_H 8.0f /* camera at z 22, 40 degrees */
#define GATE_Y (-4.7f)   /* the gate line */
#define TOP_Y 6.6f       /* where a river starts (its far end, `ahead` beats up), under the horizon */
#define LANE_DX 4.9f     /* the three-lane game's rivers' centres at the gate: -LANE_DX, 0, LANE_DX */
#define PAIR_DX 3.2f     /* one player's two rivers: -PAIR_DX, PAIR_DX (close together; their busy notes are drawn dimmer, so two rivers side by side are no flicker) */
#define DUO_C 6.2f       /* two players: each player's road at the gates, its own highway converging to its own point */
#define DUO_HALF 2.1f    /* and its two rivers either side of that centre */
#define DUO_VP 4.6f      /* the point each road converges to: a little inside its centre, so the two roads lean in together */
#define RIVER_W 2.8f     /* a river's width at the gate (a gap of about 2 between rivers, a character at the far end) */
#define FAR 0.25f        /* a river's scale at its far end: a quarter of its size at the gate */
#define PAST 0.7f        /* beats a river runs on below the gate */
#define MAX_NOTES_SHOWN 64
#define PROBE_MAX 96
#define FLOW 140         /* current streaks a river */
#define BANK_SEGS 22     /* segments in each bank */
#define TICKS 14         /* beat lines a river */
#define SHARDS 192 /* the hit bursts' shards, shared by the three lanes */
#define RINGS 6    /* the hit bursts' rings, two a lane */
#define LAND_ROWS 20 /* half-beat rows of the land (the longest view, 8 beats ahead and PAST below the gate, is 18) */
#define LAND_COLS 14 /* columns a side (two sides: 2 * LAND_COLS a row, shared by the row's stretches of land) */
#define LAND_SPANS 3 /* stretches of land a row at most: left of the roads, between two roads, right of them */
#define DEBRIS 160   /* a broken streak's falling pieces */
/* a note's size at the gate (times its shape), and its brightness there (NB0, times the view's note_light) and what
 * it adds at the far end (NB_FAR), where it is smaller than a character and needs the light to show */
#define NOTE_SIZE 0.85f
#define NB0 1.0f
#define NB_FAR 0.6f

typedef struct pal { t3_color lane[LANES], bg; } pal;
/* a palette per section: intro violet, build warming, drop hot magenta and orange, break cool cyan, outro blue (the
 * synth, a fourth colour apart from the other three) */
static const pal PALS[6] = {
  [SEC_INTRO] = { { { 0.75f, 0.25f, 1 }, { 0.2f, 0.7f, 1 }, { 1, 0.45f, 0.15f }, { 0.35f, 1, 0.45f } }, { 0.35f, 0.18f, 0.9f } },
  [SEC_BUILD] = { { { 1, 0.45f, 0.1f }, { 1, 0.9f, 0.3f }, { 0.95f, 0.2f, 0.75f }, { 0.25f, 0.8f, 1 } }, { 0.85f, 0.4f, 0.08f } },
  [SEC_DROP] = { { { 1, 0.1f, 0.65f }, { 1, 0.55f, 0.05f }, { 0.2f, 0.65f, 1 }, { 0.4f, 1, 0.35f } }, { 0.9f, 0.08f, 0.45f } },
  [SEC_BREAK] = { { { 0.1f, 0.8f, 1 }, { 0.45f, 1, 0.6f }, { 0.6f, 0.3f, 1 }, { 1, 0.6f, 0.2f } }, { 0.05f, 0.5f, 0.75f } },
  [SEC_OUTRO] = { { { 0.35f, 0.45f, 1 }, { 0.75f, 0.35f, 1 }, { 0.3f, 1, 0.6f }, { 1, 0.75f, 0.25f } }, { 0.2f, 0.25f, 0.8f } },
  [SEC_OTHER] = { { { 0.75f, 0.25f, 1 }, { 0.2f, 0.7f, 1 }, { 1, 0.45f, 0.15f }, { 0.35f, 1, 0.45f } }, { 0.35f, 0.18f, 0.9f } },
};

static struct {
  t3_object *root;
  t3_instanced_mesh *notes[LANES], *front, *flow, *banks, *ticks, *gates, *flare, *shards, *rings, *land, *caps, *crests, *sky, *band, *debris;
  t3_instanced_mesh *ridge, *plate, *pocket; /* the styles' (src/styles.c): a line along the land's tops, the dark road under each river */
  t3_material *plate_mat, *land_mat;
  /* the lit picture (club_style.lit): gems over the flat notes, a soft halo under each, glowing banks */
  t3_instanced_mesh *gem[LANES], *halo, *bank_glow, *gate_glow, *pellet;
  t3_texture *env;
  cw_node key, fill_l, fill_r;
  club_style sa, sb; /* the style fading out and the one fading in (club_set_style) */
  float st;          /* how far from sa to sb, 0..1 */
  bool styled;
  pal sec;           /* the section's palette, eased (C.p is what is drawn: the section's under the styles') */
  cw_node camera;
  float zoom, spread, ahead;
  int place[SOLO_LANES]; /* the three-lane game's river each lane is drawn as: 0 left, 1 middle, 2 right (the layout gate swaps it) */
  float x[LANES], on[LANES]; /* each lane's river: its centre at the gate and how far it shows, gliding to the layout */
  float ax[LANES];           /* the point its road converges to (0: the one highway; two players: each player's own) */
  bool laid;
  struct { float x, y, vx, vy, born, size; uint8_t lane, perfect; } shard[SHARDS];
  struct { float x, y, born; uint8_t lane, perfect; } ring[RINGS];
  int shard_next, ring_next, bursts;
  int32_t last_hit[LANES];
  pal p;
  float flow_u[FLOW], flow_ph[FLOW], flow_len[FLOW];
  t3_color sky_col;
  struct { float x, y, vx, vy, born, size; uint8_t lane; } bit[DEBRIS];
  int debris_next;
  int32_t last_break[CLUB_PLAYERS];
  bool hidden_draw; /* the `hidden draw` request: a river not shown is drawn dark, as 0.36.0's first build did (the rivers gate's control) */
  int land_mode; /* 0 the song's look; 1 flat (one height and colour, the land gate's control); 2 off */
  struct land_probe { float beat, height, hue, bands[3]; int stab; bool crest; } land_probe[LAND_ROWS];
  int land_n;
  int notescale; /* 0 the game; 1 flat, 2 reversed: the size gate's controls */
  bool notes_solo; /* the `notes solo 1` request: only the notes drawn (the footprint gate counts their pixels on black) */
  bool old_shapes; /* the `notes old` request: the bass a flat bar and the synth a hexagon again (the footprint gate's control) */
  t3_instanced_mesh *old_bass, *old_synth;
  struct probe { uint8_t lane; float ahead, x, y, px, bright; } probe[PROBE_MAX];
  int probe_n;
  bool built;
} C;
/* each note shape's width in units at scale 1 (the geometries in club_build) */
static const float NOTE_W[LANES] = { 1.46f, 1.84f, 1.975f, 1.3f };
/* each flat shape's area in units at scale 1: every river's note at least the area of the biggest before (the synth's
 * hexagon, 1.66), so none reads as the smaller target (the bass was a bar of 1.32) */

static uint32_t hash(uint32_t a, uint32_t b) {
  uint32_t h = a * 374761393u + b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float hf(uint32_t a, uint32_t b) { return (hash(a, b) & 0xffffff) / 16777216.0f; }
static t3_color scale(t3_color c, float k) { return (t3_color){ c.r * k, c.g * k, c.b * k }; }
static t3_color mix(t3_color a, t3_color b, float t) { return (t3_color){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }

/* the ASCII pass picks a character by luminance, and a blue or a magenta is far darker than a yellow at the same
 * strength: every palette colour is brought to one luminance, so a brightness here is the same character in any hue */
static t3_color even(t3_color c) {
  float l = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
  return scale(c, 0.42f / fmaxf(l, 0.05f));
}

static void to_canvas(t3_vec3 p, float cw, float ch, float *x, float *y);
/* a note's scale at a river scale s: the river's own perspective, so a note far off is small and grows as it comes
 * (the size gate, tests/beatsync-depth.mjs, holds it to that; `notescale flat|reverse` is its control) */
static float note_size(float s) {
  if (C.notescale == 1) return NOTE_SIZE * 0.6f;
  if (C.notescale == 2) return NOTE_SIZE * (1.25f - s);
  return NOTE_SIZE * s;
}

static t3_material *unlit(void) { return t3_mesh_basic_material_new(0xffffff); }
/* light that only adds (a burst's ring, a flare): drawn over what is behind it, never as a dark shape in front of it
 * (a fading ring drawn opaque was a black ring over the river in the plain picture) */
static t3_instanced_mesh *glowing(t3_instanced_mesh *im) {
  t3_material *m = im->mesh.materials[0];
  m->blending = T3_ADDITIVE_BLENDING, m->transparent = true, m->depth_write = false;
  m->version++;
  return im;
}

static void set_inst(t3_instanced_mesh *im, int i, t3_vec3 p, float rot, t3_vec3 s) {
  t3_mat4 m;
  t3_mat4_compose(&m, p, t3_quat_from_axis_angle((t3_vec3){ 0, 0, 1 }, rot), s);
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void hide(t3_instanced_mesh *im, int i) { set_inst(im, i, (t3_vec3){ 0, 0, -50 }, 0, (t3_vec3){ 0, 0, 0 }); }
static void changed(t3_instanced_mesh *im) {
  im->instance_matrix->version++; /* three.js's instanceMatrix.needsUpdate */
  if (im->instance_color) im->instance_color->version++;
}

static t3_instanced_mesh *instanced(t3_geometry *g, int n) {
  t3_material *m = unlit();
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  t3_release(g);
  t3_release(m);
  T3_OBJ(im)->frustum_culled = false;
  for (int i = 0; i < n; i++) {
    hide(im, i);
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ 0, 0, 0 });
  }
  t3_object_add(C.root, im);
  t3_release(im);
  return im;
}

/* the highway: d beats ahead of the gate (negative past it) as a scale, and a point across a river at it (u from -0.5,
 * the left bank, to 0.5, the right) */
static float persp(float d) {
  float c = (1 / FAR - 1) / fmaxf(C.ahead, 0.5f);
  return 1 / fmaxf(0.2f, 1 + c * d);
}
/* the haze: a river's current, banks and beat lines fade toward the far end (the banks keep more of theirs, the outline of the road), so the
 * highway recedes into the dark instead of bunching into a bright knot where it converges */
static float haze(float s, float lo) { return lo + (1 - lo) * fminf(1, fmaxf(0, (s - FAR) / (1 - FAR))); }
static t3_vec3 on_river(int lane, float d, float u, float z) {
  float s = persp(d), p = (1 - s) / (1 - FAR);
  float ax = C.ax[lane] * C.spread, cx = (C.x[lane] - C.ax[lane]) * C.spread;
  return (t3_vec3){ ax + (cx + u * RIVER_W) * s, GATE_Y + (TOP_Y - GATE_Y) * p, z };
}

/* the outermost river shown (its centre at the gate, before the spread): the land starts past it */
static float outer_x(void) {
  float m = 0;
  for (int l = 0; l < LANES; l++) m = fmaxf(m, fabsf(C.x[l]) * fminf(1, C.on[l] * 2));
  return fmaxf(m, PAIR_DX);
}

/* where each lane's river goes in a layout, and whether it shows; a hidden river stays where it was (it only fades) */
static void layout_targets(int layout, float x[LANES], float on[LANES], float ax[LANES]) {
  for (int l = 0; l < LANES; l++) x[l] = C.x[l], on[l] = 0, ax[l] = 0;
  static const float X3[SOLO_LANES] = { -LANE_DX, 0, LANE_DX };
  switch (layout) {
  case CLUB_DRUMS: x[LANE_KICK] = -PAIR_DX, x[LANE_CLAP] = PAIR_DX, on[LANE_KICK] = on[LANE_CLAP] = 1; break;
  case CLUB_GROOVE: x[LANE_BASS] = -PAIR_DX, x[LANE_SYNTH] = PAIR_DX, on[LANE_BASS] = on[LANE_SYNTH] = 1; break;
  case CLUB_DUO_DRUMS_LEFT:
  case CLUB_DUO_GROOVE_LEFT: {
    float s = layout == CLUB_DUO_DRUMS_LEFT ? -1 : 1; /* the drums' road: left (player 1 on the drums) or right */
    ax[LANE_KICK] = ax[LANE_CLAP] = DUO_VP * s, ax[LANE_BASS] = ax[LANE_SYNTH] = -DUO_VP * s;
    x[LANE_KICK] = DUO_C * s - DUO_HALF, x[LANE_CLAP] = DUO_C * s + DUO_HALF; /* each pair left to right on its road */
    x[LANE_BASS] = -DUO_C * s - DUO_HALF, x[LANE_SYNTH] = -DUO_C * s + DUO_HALF;
    for (int l = 0; l < LANES; l++) on[l] = 1;
    break;
  }
  default:
    for (int l = 0; l < SOLO_LANES; l++) x[l] = X3[C.place[l]], on[l] = 1;
    break;
  }
}

#define PI_F 3.14159265f
static int lit_off; /* the `lit` request: parts of the lit picture left out (1 gems, 2 halos, 4 gate glow, 8 bank glow, 16 the low lights): a look at what each adds */
#define PELLETS 40 /* a river's pellets in the arcade maze, one each half beat */
/* ── the lit picture (club_style.lit) ──────────────────────────────────────────────────────────────────────────────
 * The characters read flat shapes; the plain picture lights them. Each note gets a gem in front of its flat shape (the
 * kick a polished orb, the clap a cut diamond, the bass a triangular prism, the synth a square slab) in a clear-coated material lit by the field's lights and reflecting a studio of soft boxes, so it carries
 * highlights and a bright rim; a soft halo of its colour sits under it (the bloom takes it). The banks get a glow along
 * them; the roads under the rivers are dark glass with a clear coat, so the field's low lights slide across them. The
 * field's lights (main.cwscene: Key, FillL, FillR) take the style's colours, the low ones those of its backdrop. */

/* a studio of soft boxes for the reflections: a big one overhead, two tall strips behind the camera, dim walls */
static void studio_face(int f, int x, int y, int n, uint8_t *p) {
  float u = ((float)x + 0.5f) / (float)n * 2 - 1, v = ((float)y + 0.5f) / (float)n * 2 - 1, k = 0.025f;
  if (f == 2 && fabsf(u) < 0.6f && fabsf(v) < 0.6f) k = 1.0f;                       /* +y: the overhead box */
  if (f == 4 && (fabsf(u - 0.55f) < 0.12f || fabsf(u + 0.55f) < 0.12f) && fabsf(v) < 0.75f) k = 0.8f; /* +z: two strips */
  if ((f == 0 || f == 1) && v < -0.2f && v > -0.6f) k = 0.12f;                      /* the side walls: a low band */
  uint8_t c = (uint8_t)lrintf(fminf(1, k) * 255);
  p[0] = p[1] = p[2] = c, p[3] = 255;
}
static t3_texture *studio(void) {
  enum { N = 64 };
  uint8_t *faces[6];
  for (int f = 0; f < 6; f++) {
    faces[f] = malloc(N * N * 4);
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++) studio_face(f, x, y, N, faces[f] + (y * N + x) * 4);
  }
  t3_texture *t = t3_cube_texture_new(N, (const uint8_t *const *)faces);
  for (int f = 0; f < 6; f++) free(faces[f]);
  return t;
}
static float tx_soft(float u, float v) { /* a soft round glow */
  float d = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2;
  return d >= 1 ? 0 : powf(1 - d, 2.2f);
}
static float tx_line(float u, float v) { (void)v; return expf(-powf((u - 0.5f) * 5.0f, 2)); } /* soft across, even along */
static t3_texture *gray_tex(int w, int h, float (*f)(float, float)) {
  uint8_t *px = malloc((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      uint8_t c = (uint8_t)lrintf(fminf(1, fmaxf(0, f(((float)x + 0.5f) / (float)w, ((float)y + 0.5f) / (float)h))) * 255);
      uint8_t *p = px + ((size_t)y * w + x) * 4;
      p[0] = p[1] = p[2] = c, p[3] = 255;
    }
  t3_texture *t = t3_texture_new(w, h, px);
  free(px);
  return t;
}
static t3_instanced_mesh *soft(t3_geometry *g, t3_texture *tex, int n) {
  t3_instanced_mesh *im = glowing(instanced(g, n));
  t3_material_set_map(im->mesh.materials[0], tex), t3_release(tex);
  return im;
}
/* the road's card: a plane facing the camera whose normals lean back toward +y by `tilt` (radians from facing the
 * camera), as a floor's would seen from above it */
#define ROAD_TILT 1.35f
#define ROAD_ROUGH 0.1f
#define ROAD_METAL 0.35f
#define ROAD_ENV 1.0f
static t3_geometry *road_geometry(float tilt) {
  t3_geometry *g = t3_plane_geometry_new(1.0f, 1.0f, 1, 1);
  float n[12];
  for (int i = 0; i < 4; i++) n[i * 3] = 0, n[i * 3 + 1] = sinf(tilt), n[i * 3 + 2] = cosf(tilt);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, n, 4, 3));
  return g;
}
static void lit_build(cw_node level) {
  C.env = studio();
  t3_geometry *g[LANES] = {
    t3_sphere_geometry_new(0.62f, 32, 20),          /* kick: a polished orb */
    t3_octahedron_geometry_new(0.88f, 0),           /* clap: a cut diamond (its points up, down and to the sides) */
    t3_cylinder_geometry_new(0.98f, 0.98f, 0.5f, 3, 1, false), /* bass: a triangular prism, face on, a point away */
    t3_box_geometry_new(1.12f, 1.12f, 0.5f, 1, 1, 1), /* synth: a square slab, face on */
  };
  /* the prism stood face on (its axis toward the camera), one point up the river */
  t3_geometry_rotate_y(g[LANE_BASS], PI_F);
  t3_geometry_rotate_x(g[LANE_BASS], PI_F / 2);
  for (int l = 0; l < LANES; l++) {
    t3_material *m = t3_mesh_physical_material_new(0xffffff);
    m->roughness = l == LANE_CLAP ? 0.06f : 0.14f, m->metalness = 0.05f;
    m->clearcoat = 0.4f, m->clearcoat_roughness = 0.08f;
    m->flat_shading = l == LANE_CLAP || l == LANE_SYNTH;
    t3_material_set_texture(m, T3_ENV_MAP, C.env);
    m->env_map_intensity = 0.3f; /* (the reflections do not dim with a note: a run of bright highlights was a regular flicker) */
    t3_instanced_mesh *im = t3_instanced_mesh_new(g[l], m, MAX_NOTES_SHOWN);
    t3_release(g[l]), t3_release(m);
    T3_OBJ(im)->frustum_culled = false;
    for (int i = 0; i < MAX_NOTES_SHOWN; i++) hide(im, i), t3_instanced_mesh_set_color_at(im, i, (t3_color){ 0, 0, 0 });
    im->count = 0;
    t3_object_add(C.root, im);
    t3_release(im);
    C.gem[l] = im;
  }
  C.pellet = instanced(t3_circle_geometry_new(0.5f, 12), PELLETS * LANES);
  C.halo = soft(t3_plane_geometry_new(1, 1, 1, 1), gray_tex(64, 64, tx_soft), MAX_NOTES_SHOWN * LANES);
  C.halo->count = 0;
  C.bank_glow = soft(t3_plane_geometry_new(1, 1, 1, 1), gray_tex(64, 4, tx_line), BANK_SEGS * 2 * LANES);
  C.gate_glow = soft(t3_plane_geometry_new(1, 1, 1, 1), gray_tex(64, 64, tx_soft), LANES);
  /* the road: glass with a clear coat over a dark body. It reflects the scene's environment (no map of its own): the
   * studio below by default, and the world behind while a backdrop with `environment` lights the scene (the metal
   * hall, the night sea, the stage: cartwheel-engine 0.66.0 blends it in by the backdrop's share, so a fade fades the
   * reflection too). The road is a flat card facing the camera that stands for a floor going away, so its normals lean
   * back like a floor's (road_normals): a face-on mirror would show what is behind the camera, a floor shows the
   * world beyond it, upside down below its horizon. A little metal, so the screen-space fallback (ssr_backdrop) weighs it */
  {
    t3_scene_set_environment(cw_render3d_scene(cw_scene_app_tree()), C.env);
    t3_material *m = t3_mesh_physical_material_new(0x000000);
    m->color = (t3_color){ 0.006f, 0.007f, 0.012f };
    m->roughness = ROAD_ROUGH, m->metalness = ROAD_METAL, m->clearcoat = 1, m->clearcoat_roughness = 0.03f;
    m->env_map_intensity = ROAD_ENV;
    m->transparent = true, m->opacity = 0;
    t3_material *old = C.plate->mesh.materials[0];
    C.plate->mesh.materials[0] = m; /* the mesh holds one reference: hand it the new material and drop the old */
    t3_release(old);
    C.plate_mat = m;
  }
  C.key = cw_node_get(level, "Key");
  C.fill_l = cw_node_get(level, "FillL");
  C.fill_r = cw_node_get(level, "FillR");
}
static void set_inst_q(t3_instanced_mesh *im, int i, t3_vec3 p, t3_quat q, t3_vec3 s) {
  t3_mat4 m;
  t3_mat4_compose(&m, p, q, s);
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void light_set(cw_node n, const float *a, const float *b, float t, float k) {
  if (!n.id) return;
  float c[3], mx = 0;
  for (int i = 0; i < 3; i++) c[i] = (a[i] + (b[i] - a[i]) * t), mx = fmaxf(mx, c[i]);
  cw_node_set(n, "color", cw_val_color(mx > 0 ? c[0] / mx : 0, mx > 0 ? c[1] / mx : 0, mx > 0 ? c[2] / mx : 0, 1));
  cw_node_set(n, "energy", cw_val_float(mx * k));
}

void club_build(void) {
  if (C.built) return;
  memset(&C, 0, sizeof C);
  cw_node level = cw_node_get(cw_tree_root(cw_scene_app_tree()), "Level");
  C.root = cw_node3d_object(cw_node_get(level, "Field"));
  if (!C.root) { cw_error("main.cwscene: Level/Field (Node3D) is missing"); return; }
  C.built = true;
  C.camera = cw_node_get(level, "Camera");
  C.zoom = 1, C.spread = 1, C.ahead = 3;
  for (int l = 0; l < SOLO_LANES; l++) C.place[l] = l;
  /* the notes: kick round, clap a diamond, bass a triangle (its point away from the player), the synth a square; each of
   * about the same area (1.67 to 1.69 units: NOTE_W), so every river's note is as big a target */
  C.notes[LANE_KICK] = instanced(t3_circle_geometry_new(0.73f, 24), MAX_NOTES_SHOWN);
  C.notes[LANE_CLAP] = instanced(t3_circle_geometry_new(0.92f, 4), MAX_NOTES_SHOWN);
  {
    t3_geometry *tri = t3_circle_geometry_new(1.14f, 3);
    t3_geometry_rotate_z(tri, PI_F / 2);
    C.notes[LANE_BASS] = instanced(tri, MAX_NOTES_SHOWN);
  }
  C.notes[LANE_SYNTH] = instanced(t3_plane_geometry_new(1.3f, 1.3f, 1, 1), MAX_NOTES_SHOWN);
  C.old_bass = instanced(t3_plane_geometry_new(2.0f, 0.66f, 1, 1), MAX_NOTES_SHOWN);
  C.old_synth = instanced(t3_circle_geometry_new(0.8f, 6), MAX_NOTES_SHOWN);
  C.old_bass->count = C.old_synth->count = 0;
  C.flow = instanced(t3_plane_geometry_new(0.55f, 0.12f, 1, 1), FLOW * LANES);
  C.banks = instanced(t3_plane_geometry_new(0.14f, 1.0f, 1, 1), BANK_SEGS * 2 * LANES);
  C.ticks = instanced(t3_plane_geometry_new(1.0f, 0.1f, 1, 1), TICKS * LANES);
  C.front = instanced(t3_plane_geometry_new(1.0f, 0.2f, 1, 1), LANES);
  C.gates = instanced(t3_plane_geometry_new(1.0f, 0.34f, 1, 1), LANES);
  C.flare = glowing(instanced(t3_circle_geometry_new(1.0f, 32), LANES));
  C.shards = glowing(instanced(t3_plane_geometry_new(0.42f, 0.42f, 1, 1), SHARDS));
  C.rings = glowing(instanced(t3_ring_geometry_new(0.72f, 1.0f, 64, 1), RINGS));
  C.land = instanced(t3_plane_geometry_new(1.0f, 1.0f, 1, 1), LAND_ROWS * 2 * LAND_COLS);
  C.caps = glowing(instanced(t3_plane_geometry_new(1.0f, 1.0f, 1, 1), LAND_ROWS * 2 * LAND_COLS));
  C.crests = glowing(instanced(t3_plane_geometry_new(1.0f, 1.0f, 1, 1), LAND_ROWS * LAND_SPANS));
  C.sky = glowing(instanced(t3_plane_geometry_new(1.0f, 1.0f, 1, 1), 1));
  C.band = glowing(instanced(t3_plane_geometry_new(1.0f, 1.0f, 1, 1), LANES));
  C.debris = glowing(instanced(t3_plane_geometry_new(0.8f, 0.8f, 1, 1), DEBRIS));
  /* each pending note's pocket: a dark disc a little larger than the note, under it, clearing the current round it so
   * the note reads against black (club_style.pocket) */
  C.pocket = instanced(t3_circle_geometry_new(1.0f, 40), MAX_NOTES_SHOWN * LANES);
  C.ridge = glowing(instanced(t3_plane_geometry_new(1.0f, 1.0f, 1, 1), LAND_ROWS * 2 * LAND_COLS));
  /* the land is see-through for the styles that show a backdrop through it (club_style.land_alpha): drawn without
   * depth, its rows far to near (each row's instances in order, the farthest row first), so a nearer row covers a
   * farther one as it did with depth */
  C.land_mat = C.land->mesh.materials[0];
  C.land_mat->transparent = true, C.land_mat->depth_write = false, C.land_mat->opacity = 1, C.land_mat->version++;
  /* the road: dark glass under each river, in front of a backdrop (it writes depth, so a backdrop's particles stay
   * out of it), drawn only while a style asks for it */
  C.plate = instanced(road_geometry(ROAD_TILT), BANK_SEGS * LANES);
  C.plate_mat = C.plate->mesh.materials[0];
  C.plate_mat->transparent = true, C.plate_mat->opacity = 0, C.plate_mat->version++;
  T3_OBJ(C.plate)->visible = false;
  T3_OBJ(C.plate)->render_order = -1;
  lit_build(level);
  for (int i = 0; i < DEBRIS; i++) C.bit[i].born = -1e9f;
  for (int i = 0; i < FLOW; i++) {
    C.flow_u[i] = -0.42f + 0.84f * hf(i, 21);
    C.flow_ph[i] = hf(i, 22);
    C.flow_len[i] = 0.5f + 0.7f * hf(i, 23);
  }
  for (int i = 0; i < SHARDS; i++) C.shard[i].born = -1e9f;
  for (int i = 0; i < RINGS; i++) C.ring[i].born = -1e9f;
  for (int l = 0; l < LANES; l++) C.p.lane[l] = even(PALS[SEC_INTRO].lane[l]);
  C.p.bg = even(PALS[SEC_INTRO].bg);
  C.sec = C.p;
  C.sky_col = (t3_color){ 0, 0, 0 };
}

/* a colour from hue (degrees), saturation and value */
static t3_color hsv(float h, float sat, float val) {
  h = fmodf(fmodf(h, 360) + 360, 360) / 60;
  int i = (int)h;
  float f = h - (float)i, p = val * (1 - sat), q = val * (1 - sat * f), t = val * (1 - sat * (1 - f));
  switch (i) {
  case 0: return (t3_color){ val, t, p };
  case 1: return (t3_color){ q, val, p };
  case 2: return (t3_color){ p, val, t };
  case 3: return (t3_color){ p, q, val };
  case 4: return (t3_color){ t, p, val };
  default: return (t3_color){ val, p, q };
  }
}

/* the section at a time */
static section_kind section_at(const song *s, double ms) {
  section_kind k = SEC_INTRO;
  for (int i = 0; s && i < s->sections; i++)
    if (s->section[i].ms <= ms) k = s->section[i].kind;
  return k;
}

/* ── the styles (src/styles.c): what each asks of the picture, between the one fading out and the one fading in ── */
#define sty(f) (C.styled ? C.sa.f + (C.sb.f - C.sa.f) * C.st : (float)NEUTRAL.f)
static const club_style NEUTRAL = { .land_body = 1, .land_alpha = 1, .sky = 1, .burst = 1, .flow = 1, .note_light = 1, .pocket = 0 };
static t3_color style_lane(const club_style *x, int l) {
  if (!C.styled || x->pal <= 0) return C.sec.lane[l];
  t3_color c = even((t3_color){ x->lane[l][0], x->lane[l][1], x->lane[l][2] });
  return mix(C.sec.lane[l], c, x->pal);
}
static t3_color style_land1(const club_style *x, t3_color c) {
  if (x->land_tint <= 0) return c;
  return mix(c, even((t3_color){ x->land_rgb[0], x->land_rgb[1], x->land_rgb[2] }), x->land_tint);
}
static t3_color style_land(t3_color c) { return C.styled ? mix(style_land1(&C.sa, c), style_land1(&C.sb, c), C.st) : c; }
static float note_gain = 1;
void club_set_note_gain(float k) { note_gain = k; }
void club_set_style(const club_style *a, const club_style *b, float t) {
  C.sa = *a, C.sb = *b, C.st = fminf(1, fmaxf(0, t)), C.styled = true;
}

/* the land (see the top of this file): rows every half beat from below the gate to the far end, riding down with the
 * notes; each the other track at its beat */
static void land_update(const club_view *v, float beat, float now, float dim) {
  (void)now;
  float amax = 0; /* the farthest point a road converges to (two players' roads) */
  for (int l = 0; l < LANES; l++) amax = fmaxf(amax, fabsf(C.ax[l]) * C.spread * (C.on[l] > 0.5f));
  float first = floorf((beat - PAST) * 2) / 2;
  float on = C.land_mode == 2 ? 0 : 1;
  C.land_n = 0;
  for (int r = 0; r < LAND_ROWS; r++) {
    float rb = first + 0.5f * (float)r, d = rb - beat;
    bool show = d <= C.ahead && d >= -PAST && on > 0;
    float h, hue, br, ton, band[3];
    song_look_at(v->s, rb, &h, &hue, &br, &ton);
    song_bands_at(v->s, rb, band);
    /* drawn, each row's bands are spread over a beat either side (weights 1 2 3 2 1): rows of different heights passing
     * one point of the land at the half beat (an offbeat hat: treble high, low, high) would be a regular flicker near 4 Hz */
    float drawn[3] = { 0, 0, 0 };
    for (int t = -2; t <= 2; t++) {
      float bt[3], wt = (float)(3 - abs(t)) / 9.0f;
      song_bands_at(v->s, rb + 0.5f * (float)t, bt);
      for (int k = 0; k < 3; k++) drawn[k] += wt * bt[k];
    }
    int stab = song_stab_in(v->s, rb, rb + 0.5f);
    bool crest = song_change_in(v->s, rb, rb + 0.5f);
    if (C.land_mode == 1) h = 0.4f, hue = 220, br = 0.3f, ton = 0.4f, band[0] = band[1] = band[2] = 1, drawn[0] = drawn[1] = drawn[2] = 1, stab = 0, crest = false;
    if (show && C.land_n < LAND_ROWS) C.land_probe[C.land_n++] = (struct land_probe){ rb, h, hue, { band[0], band[1], band[2] }, stab, crest };
    float s = persp(d), pp = (1 - s) / (1 - FAR), y0 = GATE_Y + (TOP_Y - GATE_Y) * pp;
    /* each row reaches down to the nearer row's base, behind it: near the gates the rows are further apart than they are
     * tall, and the dark gaps between them passing one point of the screen were a regular flicker at the half beat */
    float yb = GATE_Y + (TOP_Y - GATE_Y) * (1 - persp(d - 0.5f)) / (1 - FAR);
    /* no fade below the gate: a row is off the screen long before PAST, and each row fading as it passed one point was
     * a sawtooth at the half beat */
    float in = fminf(1, (C.ahead - d) / 0.8f);
    float fade = show ? in * haze(s, 0.4f) * dim : 0;
    t3_color col_c = style_land(even(hsv(hue, 0.45f + 0.5f * ton, 1)));
    float light = 0.5f * (0.5f + 0.35f * br) * (0.5f + 0.5f * h) * fade; /* under the rivers' light: they stay what the eye follows */
    float body = sty(land_body), ridge = sty(ridge), capk = fmaxf(sty(land_alpha), ridge) * fminf(1, body * 1.6f);
    uint32_t row = (uint32_t)(int32_t)lroundf(rb * 2 + 100000);
    float zr = -0.3f - 0.02f * d; /* a nearer row in front of a farther one */
    /* the row's stretches of land: the width out to the edges, less the roads (each shown river and a margin, a gap
     * narrower than a river's left as road): left of the roads, between two players' roads, right of them */
    float xo = 17.0f * s + amax * (1 - s); /* the edges converge with the roads (with two roads, around their points) */
    float blk[LANES][2];
    int nb = 0;
    for (int l = 0; l < LANES; l++) {
      if (C.on[l] < 0.5f) continue;
      float xc = on_river(l, d, 0, 0).x, hw = (RIVER_W * 0.5f + 0.8f) * s * C.spread;
      int k = nb++;
      while (k > 0 && blk[k - 1][0] > xc - hw) { blk[k][0] = blk[k - 1][0], blk[k][1] = blk[k - 1][1]; k--; }
      blk[k][0] = xc - hw, blk[k][1] = xc + hw;
    }
    float span[LAND_SPANS][2];
    int ns = 0;
    float at = -xo;
    for (int b = 0; b <= nb; b++) {
      float a = b < nb ? blk[b][0] : xo;
      if (a - at > 2.0f * s && ns < LAND_SPANS) span[ns][0] = at, span[ns][1] = a, ns++;
      if (b < nb) at = fmaxf(at, blk[b][1]);
    }
    float total = 0;
    for (int k = 0; k < ns; k++) total += span[k][1] - span[k][0];
    int col = 0;
    for (int k = 0; k < ns && show; k++) {
      float a = span[k][0], b = span[k][1], w = b - a;
      bool leftEdge = a <= -xo + 1e-3f, rightEdge = b >= xo - 1e-3f; /* an end at the screen's edge, not a road */
      if (!leftEdge && !rightEdge && sty(land_no_mid) > 0.5f) { /* between two roads: left out in a style that asks */
        int n0 = k == ns - 1 ? 2 * LAND_COLS - col : (int)lroundf(2.0f * LAND_COLS * w / total);
        for (int j = 0; j < n0 && col < 2 * LAND_COLS; j++, col++) hide(C.land, (LAND_ROWS - 1 - r) * 2 * LAND_COLS + col), hide(C.caps, r * 2 * LAND_COLS + col), hide(C.ridge, r * 2 * LAND_COLS + col);
        hide(C.crests, r * LAND_SPANS + k);
        continue;
      }
      int n = k == ns - 1 ? 2 * LAND_COLS - col : (int)lroundf(2.0f * LAND_COLS * w / total);
      float cwk = w / (float)CW_MAX(n, 1);
      for (int j = 0; j < n && col < 2 * LAND_COLS; j++, col++) {
        int idx = r * 2 * LAND_COLS + col, li = (LAND_ROWS - 1 - r) * 2 * LAND_COLS + col; /* li: the land's own order, far first */
        float X = a + cwk * ((float)j + 0.5f);
        /* u: 0 beside a road, 1 farthest from one (the screen's edge, or the middle between two roads) */
        float u = leftEdge ? (b - X) / w : rightEdge ? (X - a) / w : 1 - fabsf(X - (a + b) * 0.5f) / (w * 0.5f);
        /* the spectrum laid outward: bass beside the rivers, mid between, treble far from them */
        float bv = u < 0.5f ? drawn[0] + (drawn[1] - drawn[0]) * (u / 0.5f) : drawn[1] + (drawn[2] - drawn[1]) * ((u - 0.5f) / 0.5f);
        bv = fminf(2.2f, bv);
        /* rough by column, never by row: rows of different heights passing one point of the screen at the half beat
         * would be a regular flicker near 4 Hz; a column keeps its roughness and its height follows the music smoothly */
        float rough = 0.7f + 0.6f * hf(7u, (uint32_t)col);
        float H = s * fminf(3.4f, 0.4f + 2.4f * (0.4f + 0.6f * h) * powf(bv, 0.8f) * rough * (0.3f + 1.0f * u));
        set_inst(C.land, li, (t3_vec3){ X, (yb + y0 + H) * 0.5f, zr }, 0, (t3_vec3){ cwk * 0.86f, y0 + H - yb, 1 });
        t3_instanced_mesh_set_color_at(C.land, li, scale(col_c, body * light * (0.9f + 0.1f * fminf(1.5f, bv)) * (0.55f + 0.45f * u)));
        /* a style's ridge: the column's top as a line of the land's colour (the land as a lit skyline) */
        /* near the gates the rows pass fastest (one a half beat): a ridge there would be a regular flicker, so it
         * fades out over the last two beats before the gate */
        float near = fminf(1, fmaxf(0, (d - 0.5f) / 2.0f));
        if (ridge > 0.003f && near > 0) {
          float th = fmaxf(0.05f, 0.14f * s);
          set_inst(C.ridge, idx, (t3_vec3){ X, y0 + H - th * 0.5f, zr + 0.005f }, 0, (t3_vec3){ cwk * 0.86f, th, 1 });
          t3_instanced_mesh_set_color_at(C.ridge, idx, scale(mix(col_c, (t3_color){ 1, 1, 1 }, 0.2f), ridge * fade * (0.6f + 0.4f * fminf(1, h + 0.3f)) * near));
        } else hide(C.ridge, idx);
        /* a stab: lights on the ridge, more for a stronger one */
        if (stab && hf(row + (uint32_t)stab * 7919u, (uint32_t)col) < 0.12f + 0.06f * (float)stab) {
          float cs = cwk * 0.8f;
          set_inst(C.caps, idx, (t3_vec3){ X, y0 + H + cs * 0.5f, zr + 0.01f }, 0, (t3_vec3){ cs, cs, 1 });
          t3_instanced_mesh_set_color_at(C.caps, idx, scale(mix(col_c, (t3_color){ 1, 1, 1 }, 0.35f), (0.16f + 0.03f * (float)stab) * fade * capk));
        } else hide(C.caps, idx);
      }
      /* the harmony moves on this row: a crest of light along each stretch in the new colour */
      int ci = r * LAND_SPANS + k;
      if (crest) {
        set_inst(C.crests, ci, (t3_vec3){ (a + b) * 0.5f, y0, zr + 0.02f }, 0, (t3_vec3){ w, 0.16f * s, 1 });
        t3_instanced_mesh_set_color_at(C.crests, ci, scale(mix(col_c, (t3_color){ 1, 1, 1 }, 0.3f), 0.7f * fade * capk));
      } else hide(C.crests, ci);
    }
    for (int k = show ? ns : 0; k < LAND_SPANS; k++) hide(C.crests, r * LAND_SPANS + k);
    for (; col < 2 * LAND_COLS; col++) hide(C.land, (LAND_ROWS - 1 - r) * 2 * LAND_COLS + col), hide(C.caps, r * 2 * LAND_COLS + col), hide(C.ridge, r * 2 * LAND_COLS + col);
  }
  changed(C.ridge);
  changed(C.land);
  changed(C.caps);
  changed(C.crests);
  /* the sky: a faint glow above the horizon in the harmony sounding now, easing to a new chord over about half a beat */
  float h, hue, br, ton;
  song_look_at(v->s, beat, &h, &hue, &br, &ton);
  if (C.land_mode == 1) hue = 220, ton = 0.4f;
  t3_color want = scale(style_land(even(hsv(hue, 0.45f + 0.5f * ton, 1))), on * dim * (0.06f + 0.06f * h) * sty(sky));
  C.sky_col = mix(C.sky_col, want, 0.08f);
  set_inst(C.sky, 0, (t3_vec3){ 0, TOP_Y + 1.5f, -4 }, 0, (t3_vec3){ 34, 2.6f, 1 });
  t3_instanced_mesh_set_color_at(C.sky, 0, C.sky_col);
  changed(C.sky);
}

/* the streak's moments (the banks' charge and drain are drawn with the banks): a multiplier step sends a band of light
 * up each river from its gate to the far end; a broken streak throws its lit bank pieces off, falling and fading */
static void streak_update(const club_view *v, float now, float dim) {
  (void)now;
  for (int l = 0; l < LANES; l++) {
    int pp = v->lane_player[l] > 0 ? 1 : 0;
    if (v->tier_t[pp] < 0 || v->tier_t[pp] >= TIER_S || C.on[l] < 0.5f) { hide(C.band, l); continue; }
    float u = v->tier_t[pp] / TIER_S, d = C.ahead * (1 - (1 - u) * (1 - u)), s = persp(d);
    set_inst(C.band, l, on_river(l, d, 0, 0.08f), 0, (t3_vec3){ RIVER_W * 1.1f * s, 0.5f * s, 1 });
    t3_instanced_mesh_set_color_at(C.band, l, scale(mix(C.p.lane[l], (t3_color){ 1, 1, 1 }, 0.5f), 1.4f * v->tier_light[pp] * haze(s, 0.5f) * dim));
  }
  changed(C.band);
  for (int pp = 0; pp < CLUB_PLAYERS; pp++) {
    if (v->break_seq[pp] == C.last_break[pp]) continue;
    C.last_break[pp] = v->break_seq[pp];
    for (int l = 0; l < LANES; l++)
      for (int side = 0; side < 2 && C.on[l] >= 0.5f && (v->lane_player[l] > 0 ? 1 : 0) == pp; side++)
        for (int j = 0; j < BANK_SEGS; j++) {
          float span = C.ahead + PAST, dm = C.ahead - span * ((float)j + 0.5f) / BANK_SEGS;
          if (dm < 0 || dm / C.ahead > v->drain_from[pp]) continue;
          t3_vec3 a = on_river(l, dm, side ? 0.53f : -0.53f, 0.12f);
          int i = C.debris_next++ % DEBRIS;
          float sg = side ? 1.0f : -1.0f, r1 = hf((uint32_t)v->break_seq[pp], (uint32_t)(l * 97 + side * 31 + j)), r2 = hf((uint32_t)v->break_seq[pp] + 7, (uint32_t)(l * 97 + side * 31 + j));
          C.bit[i] = (typeof(C.bit[0])){ a.x, a.y, sg * (0.8f + 1.6f * r1), 0.5f + 1.5f * r2, v->t, persp(dm) * (0.7f + 0.5f * r2), (uint8_t)l };
        }
  }
  for (int i = 0; i < DEBRIS; i++) {
    float age = v->t - C.bit[i].born;
    if (age < 0 || age > DRAIN_S) { hide(C.debris, i); continue; }
    float x = C.bit[i].x + C.bit[i].vx * age, y = C.bit[i].y + C.bit[i].vy * age - 6.0f * age * age;
    float env = (1 - age / DRAIN_S) * (1 - age / DRAIN_S);
    set_inst(C.debris, i, (t3_vec3){ x, y, 0.12f }, age * 4 + (float)i, (t3_vec3){ C.bit[i].size, C.bit[i].size, 1 });
    t3_instanced_mesh_set_color_at(C.debris, i, scale(mix(C.p.lane[C.bit[i].lane], (t3_color){ 1, 0.07f, 0.05f }, 0.92f), 1.1f * env * v->drain_light[v->lane_player[C.bit[i].lane] > 0 ? 1 : 0] * dim));
  }
  changed(C.debris);
}

void club_update(const club_view *v) {
  if (!C.built) return;
  float now = (float)v->ms, dt = 1.0f / 60.0f;
  float beat = v->s ? song_beat_at(v->s, now) : v->t * 2;
  float beat_ms = v->s && v->s->bpm > 0 ? 60000.0f / v->s->bpm : 500;
  float dim = 1 - 0.55f * v->dim;
  /* the camera and the highway's shape, eased (they glide, never cut): the title spreads the rivers round its box */
  float zw = v->zoom > 0 ? v->zoom : 1, sw = v->spread > 0 ? v->spread : 1, aw = v->ahead > 0 ? v->ahead : 3;
  C.zoom += (zw - C.zoom) * 0.04f;
  C.spread += (sw - C.spread) * 0.04f;
  C.ahead += (aw - C.ahead) * 0.06f;
  if (C.camera.id) cw_node3d_set_position(C.camera, (t3_vec3){ 0, 0, 22.0f / C.zoom });
  /* the rivers glide to the layout and fade in or out (a hidden river keeps its place) */
  {
    float tx[LANES], ton[LANES], tax[LANES];
    layout_targets(v->layout, tx, ton, tax);
    for (int l = 0; l < LANES; l++) {
      if (!C.laid) C.x[l] = tx[l], C.on[l] = ton[l], C.ax[l] = tax[l];
      C.x[l] += (tx[l] - C.x[l]) * 0.08f;
      C.ax[l] += (tax[l] - C.ax[l]) * 0.08f;
      C.on[l] = ton[l] > C.on[l] ? fminf(ton[l], C.on[l] + dt / 0.4f) : fmaxf(ton[l], C.on[l] - dt / 0.3f);
    }
    C.laid = true;
  }

  /* the palette: the section's, eased toward over about a bar */
  section_kind sec = section_at(v->s, now);
  const pal *want = &PALS[sec];
  float k = fminf(1, dt * 1000.0f / (beat_ms * 4) * 3);
  for (int l = 0; l < LANES; l++) C.sec.lane[l] = mix(C.sec.lane[l], even(want->lane[l]), k);
  C.sec.bg = mix(C.sec.bg, even(want->bg), k);
  /* the styles' palettes over the section's (a style with its own lanes holds them through every section) */
  for (int l = 0; l < LANES; l++) C.p.lane[l] = mix(style_lane(&C.sa, l), style_lane(&C.sb, l), C.st);
  C.p.bg = C.sec.bg;

  {
    float la = fminf(1, fmaxf(0, sty(land_alpha)));
    if (C.land_mat->opacity != la) C.land_mat->opacity = la, C.land_mat->version++;
    T3_OBJ(C.land)->visible = la > 0.002f;
    float pl = sty(plate);
    T3_OBJ(C.plate)->visible = pl > 0.002f;
    if (C.plate_mat->opacity != pl) C.plate_mat->opacity = pl, C.plate_mat->version++;
  }
  /* the lit picture's share and the field's lights (the low ones sweep slowly across, over eight bars) */
  const float P = C.styled ? fminf(1, fmaxf(0, sty(lit))) : 0;
  {
    float k = C.st;
    light_set(C.key, C.sa.key, C.sb.key, k, (lit_off & 128) ? 0 : 1.0f);
    float sw = sty(sweep), ph = 2 * PI_F * beat / 32.0f;
    light_set(C.fill_l, C.sa.fill_l, C.sb.fill_l, k, (lit_off & 16) ? 0 : 90.0f);
    light_set(C.fill_r, C.sa.fill_r, C.sb.fill_r, k, (lit_off & 16) ? 0 : 90.0f);
    if (C.fill_l.id) cw_node3d_set_position(C.fill_l, (t3_vec3){ -12 + 7 * sw * sinf(ph), -9 + 4 * sw * cosf(ph * 0.7f), 7 });
    if (C.fill_r.id) cw_node3d_set_position(C.fill_r, (t3_vec3){ 12 - 7 * sw * sinf(ph + 1.3f), -9 + 4 * sw * cosf(ph * 0.7f + 2), 7 });
  }
  float span = C.ahead + PAST; /* the current runs from the far end to below the gate */
  for (int l = 0; l < LANES; l++) {
    const float ldim = dim * C.on[l]; /* a river fading in or out with the layout (0: not shown) */
    const int pp = v->lane_player[l] > 0 ? 1 : 0; /* whose streak this river shows */
    if (C.on[l] < 0.01f && !C.hidden_draw) { /* not shown: left out, never drawn dark (it would cover a river where it rests) */
      for (int i = 0; i < FLOW; i++) hide(C.flow, l * FLOW + i);
      for (int j = 0; j < PELLETS; j++) hide(C.pellet, l * PELLETS + j);
      for (int j = 0; j < BANK_SEGS * 2; j++) hide(C.banks, l * BANK_SEGS * 2 + j), hide(C.bank_glow, l * BANK_SEGS * 2 + j);
      for (int j = 0; j < TICKS; j++) hide(C.ticks, l * TICKS + j);
      hide(C.front, l), hide(C.gates, l), hide(C.flare, l), hide(C.gate_glow, l);
      continue;
    }
    /* a river plays at full strength while it is hit, dimmed to dots while it is missed out (the streak shows on the
     * banks, which stay still: brightening the moving current with it made dense eighths flicker at the half beat) */
    float lv = (0.3f + 0.7f * v->level[l]) * ldim, fl = v->flare[l] * sty(burst);

    /* the current: streaks riding down at the notes' speed, a gentle sway across; a hit surges the water round the
     * gate (brighter and a little wider), swelling with the flare and fading with it */
    for (int i = 0; i < FLOW; i++) {
      float d = C.ahead - fmodf(beat + C.flow_ph[i] * span + (float)l * 0.37f, span);
      float u = C.flow_u[i] + 0.04f * sinf(beat * 1.3f + C.flow_ph[i] * 9);
      float near_gate = expf(-fabsf(d) * 1.8f), surge = 1 + 2.4f * fl * near_gate;
      u *= 1 + 0.25f * fl * near_gate;
      float s = persp(d), in = fminf(1, (C.ahead - d) / 0.6f), out = d < 0 ? fmaxf(0, 1 + d / PAST) : 1;
      float b = (0.34f + 0.14f * hf((uint32_t)i, (uint32_t)l + 40)) * lv * surge * in * out * haze(s, 0.3f) * sty(flow) * (1 - sty(pellets));
      set_inst(C.flow, l * FLOW + i, on_river(l, d, u, -0.2f), 0, (t3_vec3){ C.flow_len[i] * s, s, 1 });
      t3_instanced_mesh_set_color_at(C.flow, l * FLOW + i, scale(C.p.lane[l], b));
    }

    /* the arcade maze: a line of pellets down the middle of the river, still (dots riding down at a half beat apart
     * pass a point four times a second, a regular flicker in the chunky pixels); the notes ride down them */
    {
      float pel = sty(pellets);
      float off = 0;
      for (int j = 0; j < PELLETS; j++) {
        int idx = l * PELLETS + j;
        float d = 0.5f * (float)j - off;
        if (pel < 0.002f || d > C.ahead || d < -PAST) { hide(C.pellet, idx); continue; }
        float s = persp(d), in = fminf(1, (C.ahead - d) / 0.6f), out = d < 0 ? fmaxf(0, 1 + d / PAST) : 1;
        set_inst(C.pellet, idx, on_river(l, d, 0, -0.18f), 0, (t3_vec3){ 0.3f * s, 0.3f * s, 1 });
        t3_instanced_mesh_set_color_at(C.pellet, idx, scale((t3_color){ 1, 0.72f, 0.55f }, 0.5f * pel * ldim * in * out * haze(s, 0.4f)));
      }
    }
    /* the banks: two lines down the river's sides, fading out at the far end and below the gate */
    for (int side = 0; side < 2; side++)
      for (int j = 0; j < BANK_SEGS; j++) {
        float d0 = C.ahead - span * (float)j / BANK_SEGS, d1 = C.ahead - span * (float)(j + 1) / BANK_SEGS, u = side ? 0.53f : -0.53f;
        t3_vec3 a = on_river(l, d0, u, -0.1f), b2 = on_river(l, d1, u, -0.1f);
        float dx = b2.x - a.x, dy = b2.y - a.y, len = sqrtf(dx * dx + dy * dy), dm = 0.5f * (d0 + d1);
        float fade = fminf(1, (C.ahead - dm) / 0.8f) * (dm < 0 ? fmaxf(0, 1 + dm / PAST) : 1);
        /* a long wait for notes: the banks fill with light from the gate up toward the far end as it passes, a soft
         * front moving at the wait's pace (seconds a river), swelling in and fading out */
        float q = dm / C.ahead, lit = fminf(1, fmaxf(0, (v->gap_fill - q) / 0.08f + 0.5f)) * (dm >= -0.2f);
        fade += 1.6f * v->gap_alpha * lit;
        /* the streak's charge: the banks lit from the gate up, a soft front at how far the streak has come toward
         * the top multiplier (the far end); a broken streak's charge drains back down to the gate, reddening */
        float qc = fmaxf(0, q), on = dm >= -0.2f ? fminf(1, fmaxf(0, (v->charge[pp] - qc) / 0.06f + 0.5f)) : 0;
        float drain = 0;
        if (v->drain_t[pp] >= 0 && v->drain_t[pp] < DRAIN_S) {
          float front = v->drain_from[pp] * (1 - v->drain_t[pp] / DRAIN_S);
          drain = dm >= -0.2f ? fminf(1, fmaxf(0, (front - qc) / 0.06f + 0.5f)) * v->drain_light[pp] : 0;
        }
        /* each style's bank colour (its lane's, or the style's own: the arcade maze's blue walls), between the two */
        t3_color ba = mix(C.p.lane[l], (t3_color){ C.sa.bank_rgb[0], C.sa.bank_rgb[1], C.sa.bank_rgb[2] }, C.styled ? C.sa.bank_tint : 0);
        t3_color bb = mix(C.p.lane[l], (t3_color){ C.sb.bank_rgb[0], C.sb.bank_rgb[1], C.sb.bank_rgb[2] }, C.styled ? C.sb.bank_tint : 0);
        t3_color bc = mix(mix(ba, bb, C.styled ? C.st : 0), (t3_color){ 1, 1, 1 }, 0.4f * on);
        bc = mix(bc, (t3_color){ 1, 0.07f, 0.05f }, 0.92f * drain);
        float bl = 0.48f * lv * fade * haze(persp(dm), 0.6f) + (0.4f * on + 0.9f * drain) * ldim * haze(persp(dm), 0.5f) * (dm < 0 ? fmaxf(0, 1 + dm / PAST) : 1);
        int idx = (l * 2 + side) * BANK_SEGS + j;
        set_inst(C.banks, idx, (t3_vec3){ (a.x + b2.x) * 0.5f, (a.y + b2.y) * 0.5f, -0.1f }, -atan2f(dx, dy), (t3_vec3){ 1 + 1.6f * fmaxf(on, drain), len * 1.04f, 1 });
        t3_instanced_mesh_set_color_at(C.banks, idx, scale(bc, bl * (1 - 0.1f * P)));
        /* the lit picture: a soft glow along the bank, wider than its line (the bloom spreads it further) */
        if (P > 0.002f) {
          set_inst(C.bank_glow, idx, (t3_vec3){ (a.x + b2.x) * 0.5f, (a.y + b2.y) * 0.5f, -0.11f }, -atan2f(dx, dy), (t3_vec3){ 1.1f * persp(dm) + 0.25f, len * 1.04f, 1 });
          t3_instanced_mesh_set_color_at(C.bank_glow, idx, scale(bc, bl * 0.24f * P * !(lit_off & 8)));
        } else hide(C.bank_glow, idx);
      }

    /* the road under the river (a style's): dark glass from the far end to below the gate, a little wider than the
     * banks, so the notes and the current read against it whatever the backdrop behind */
    for (int j = 0; j < BANK_SEGS; j++) {
      int idx = l * BANK_SEGS + j;
      if (!T3_OBJ(C.plate)->visible) { hide(C.plate, idx); continue; }
      float d0 = C.ahead - span * (float)j / BANK_SEGS, d1 = C.ahead - span * (float)(j + 1) / BANK_SEGS, dm = 0.5f * (d0 + d1);
      t3_vec3 a = on_river(l, d0, 0, -0.25f), b2 = on_river(l, d1, 0, -0.25f);
      float dx = b2.x - a.x, dy = b2.y - a.y, len = sqrtf(dx * dx + dy * dy);
      set_inst(C.plate, idx, (t3_vec3){ (a.x + b2.x) * 0.5f, (a.y + b2.y) * 0.5f, -0.25f }, -atan2f(dx, dy), (t3_vec3){ RIVER_W * 1.08f * persp(dm) * C.spread * C.on[l], len * 1.02f, 1 }); /* edges under the banks (each segment is a rectangle) */
    }

    /* the beat lines: across the river on every beat, brighter on the bar's first, riding down with the notes */
    int down = v->s ? v->s->downbeat : 0;
    float b0 = floorf(beat);
    for (int j = 0; j < TICKS; j++) {
      float bt = b0 + (float)j, d = bt - beat;
      int idx = l * TICKS + j;
      if (d > C.ahead || d < -PAST) { hide(C.ticks, idx); continue; }
      int bb = (((int)bt - down) % 4 + 4) % 4;
      float s = persp(d), fade = fminf(1, (C.ahead - d) / 0.6f) * (d < 0 ? fmaxf(0, 1 + d / PAST) : 1);
      set_inst(C.ticks, idx, on_river(l, d, 0, -0.15f), 0, (t3_vec3){ RIVER_W * s, s, 1 });
      t3_instanced_mesh_set_color_at(C.ticks, idx, scale(C.p.lane[l], (bb == 0 ? 0.5f : 0.26f) * lv * fade * haze(s, 0.45f)));
    }

    /* a long wait's front: a line across the river where the banks' light has reached, climbing as the wait passes */
    if (v->gap_alpha > 0) {
      float fd = v->gap_fill * C.ahead, s = persp(fd);
      set_inst(C.front, l, on_river(l, fd, 0, -0.12f), 0, (t3_vec3){ RIVER_W * 1.06f * s, s, 1 });
      t3_instanced_mesh_set_color_at(C.front, l, scale(C.p.lane[l], 0.8f * v->gap_alpha * ldim));
    } else hide(C.front, l);

    /* the gate: a bar across the river at the hit line, lit while the river plays; it blooms on a hit and a press
     * lifts it a little; behind it the flare, a soft glow that swells and fades */
    float pr = v->press[l] * sty(burst);
    t3_vec3 g = on_river(l, 0, 0, 0.06f);
    set_inst(C.gates, l, g, 0, (t3_vec3){ (RIVER_W + 0.5f) * (1 + 0.08f * fl), 1 + 0.5f * fl + 0.2f * pr, 1 });
    /* the `rivers hidden draw` control: a river not shown drawn dark over where it rests, as 0.36.0's first build's
     * dark flare did (the flare is light that adds now, so the gate bar stands in for it, in front, flare-sized) */
    if (C.hidden_draw && C.on[l] < 0.01f) { /* in front of everything the field draws (a gem stands out of the plane), where it covers on screen */
      float pk = (22.0f / C.zoom - 2.0f) / (22.0f / C.zoom);
      set_inst(C.gates, l, (t3_vec3){ g.x * pk, (g.y + 0.6f) * pk, 2.0f }, 0, (t3_vec3){ 1.7f * 1.4f * 2 * pk, 0.8f * 1.4f * 2 / 0.34f * 1.6f * pk, 1 });
    }
    t3_instanced_mesh_set_color_at(C.gates, l, scale(mix(C.p.lane[l], (t3_color){ 1, 1, 1 }, 0.3f * fl), (0.85f + 0.2f * pr) * (0.45f + 0.55f * v->level[l]) * ldim + 0.55f * fl * ldim));
    set_inst(C.gate_glow, l, (t3_vec3){ g.x, g.y, 0.05f }, 0, (t3_vec3){ (RIVER_W + 1.6f) * (1 + 0.1f * fl), 1.3f + 0.6f * fl + 0.2f * pr, 1 });
    t3_instanced_mesh_set_color_at(C.gate_glow, l, scale(C.p.lane[l], P * ldim * 0.22f * (0.45f + 0.55f * v->level[l]) * !(lit_off & 4) /* steady: a glow pulsing on every hit of a run is a regular flicker */));
    float fs = 0.6f + 1.0f * fl + 0.15f * pr;
    set_inst(C.flare, l, (t3_vec3){ g.x, g.y, 0.04f }, 0, (t3_vec3){ fs * 1.7f, fs * 0.8f, 1 });
    t3_instanced_mesh_set_color_at(C.flare, l, scale(mix(C.p.lane[l], (t3_color){ 1, 1, 1 }, 0.3f * fl), (0.08f + 0.6f * fl + 0.12f * pr) * ldim * sty(burst)));
  }
  changed(C.flow);
  changed(C.pellet);
  changed(C.plate);
  changed(C.banks);
  changed(C.ticks);
  changed(C.front);
  changed(C.gates);
  changed(C.flare);
  changed(C.bank_glow);
  changed(C.gate_glow);

  /* the notes: up their river by how many beats away they are, in the river's perspective (small far off, growing all
   * the way to the gate), a little brighter far off where they are smaller than a character, and no brighter as they
   * near the gate: their light there grows only with their size, so a run of notes riding down is a steady light,
   * never a burst on each beat (the flash check's regular-flicker bound); a played note opens out at the gate and
   * fades, a missed one greys and drifts on down */
  int cnt[LANES] = { 0 }, npk = 0, ngem[LANES] = { 0 }, nhalo = 0;
  C.probe_n = 0;
  for (int i = 0; v->notes && i < v->count; i++) {
    float nb = song_beat_at(v->s, (float)v->notes[i].ms), ahead = nb - beat;
    if (ahead > C.ahead) break;
    uint8_t st = v->state ? v->state[i] : NOTE_PENDING;
    if (st == NOTE_SKIPPED) st = NOTE_PENDING;
    float since = now - (float)v->notes[i].ms; /* a played note fades over 400 ms: never gone in a frame */
    if ((st == NOTE_HIT && since > 400) || st == NOTE_GONE || ahead < -PAST) continue;
    int l = v->notes[i].lane;
    if (cnt[l] >= MAX_NOTES_SHOWN || C.on[l] < 0.01f) continue; /* a river not shown carries no notes */
    float in = fminf(1, (C.ahead - ahead) / 0.5f);    /* they fade in at the far end */
    float nl = v->note_light > 0 ? v->note_light : 1, nb0 = NB0 * nl;
    /* the bass note is the widest across its river: on a run of eighths it is the most light passing the gate, so it is
     * drawn a little dimmer than the round notes (the flash check's regular-flicker bound on the densest song) */
    if (v->notes[i].lane == LANE_BASS) nb0 *= 0.62f; /* (the triangle is the bar's area times 1.28: its light the bar's) */
    /* on two lanes the clap river carries the hats too, a note on every eighth: drawn dimmer still (the densest drums
     * on two lanes flickered at 0.043 against the 0.02 bound with the clap at full) */
    if (v->notes[i].lane == LANE_CLAP && v->layout != CLUB_SOLO) nb0 *= 0.5f;
    if (v->notes[i].lane == LANE_BASS && v->layout != CLUB_SOLO) nb0 *= 0.4f;  /* the groove's Hard bass runs in sixteenths */
    if (v->notes[i].lane == LANE_SYNTH) nb0 *= 0.7f;
    /* a run of eighths on one river (Hard's claps in a busy stretch) passes each point of it four times a second: drawn
     * at 60% so the run is a steady light, not a regular flicker over the 0.02 bound (it read 0.029 on Test Groove's
     * second half) */
    {
      float gap = 1e9f;
      for (int k = i - 1; k >= 0 && k >= i - 12; k--)
        if (v->notes[k].lane == v->notes[i].lane) { gap = fminf(gap, nb - song_beat_at(v->s, (float)v->notes[k].ms)); break; }
      for (int k = i + 1; k < v->count && k <= i + 12; k++)
        if (v->notes[k].lane == v->notes[i].lane) { gap = fminf(gap, song_beat_at(v->s, (float)v->notes[k].ms) - nb); break; }
      if (gap < 0.6f) nb0 *= 0.6f;
    }
    float d = ahead, s = persp(ahead), far = fminf(1, fmaxf(0, (1 - s) / (1 - FAR)));
    float size = note_size(s), bright = nb0 * (1 + NB_FAR * far) * in;
    if (st == NOTE_MISSED) bright = 0.3f * fmaxf(0, 1 + ahead / PAST), size = note_size(s) * 0.8f;
    if (st == NOTE_HIT) { /* played: it stays at the gate, opens out and fades */
      float f = fmaxf(0, 1 - since / 400.0f);
      d = 0, bright = nb0 * f, size = note_size(1) * (1 + 0.5f * (1 - f)); /* from the size it had */
    }
    if (st == NOTE_PENDING && C.probe_n < PROBE_MAX) { /* what the size gate reads: where the note is and how big */
      float cx, cy, k = 1080.0f * 0.5f / VIEW_HALF_H * (C.zoom > 0 ? C.zoom : 1);
      to_canvas(on_river(l, d, 0, 0), 1920, 1080, &cx, &cy);
      C.probe[C.probe_n++] = (struct probe){ (uint8_t)l, ahead, cx, cy, size * NOTE_W[l] * k, bright };
    }
    bright *= C.on[l] * note_gain * sty(note_light);
    t3_color c = st == NOTE_MISSED ? (t3_color){ bright, bright, bright } : scale(C.p.lane[l], bright * dim);
    /* the lit picture: a gem in front of the flat note (a missed note stays a flat grey shape) */
    float gk = st == NOTE_PENDING ? P * (0.25f + 0.75f * in) : 0; /* a played note opens out flat in its burst */
    t3_vec3 at = on_river(l, d, 0, 0.1f);
    /* at the gate a note is its flat shape again (the flat notes were tuned with the bursts so a run of them at the gate
     * is a steady light; a gem arriving there on every note of a run read as a regular flicker over the 0.02 bound) */
    { float g0 = fminf(1, fmaxf(0, (ahead - 0.35f) / 0.65f)); gk *= g0 * g0 * (3 - 2 * g0); }
    if (lit_off & 1) gk = 0;
    if (gk > 0.002f && ngem[l] < MAX_NOTES_SHOWN) {
      float gs = size * (0.4f + 0.6f * gk);
      /* a slow rock about the upright, each note its own phase, so the highlights travel across it */
      float wob = 0.35f * sinf(v->t * 1.3f + (float)i * 0.9f);
      t3_quat q = t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, (l == LANE_BASS || l == LANE_SYNTH ? 0.6f : 1) * wob + (l == LANE_CLAP ? PI_F / 4 : 0));
      set_inst_q(C.gem[l], ngem[l], (t3_vec3){ at.x, at.y, at.z + 0.75f * gs }, q, (t3_vec3){ gs, gs, gs });
      /* the gem's own colour, a little brighter than the flat note (the light falls off its sides) */
      t3_instanced_mesh_set_color_at(C.gem[l], ngem[l]++, scale(C.p.lane[l], fminf(1.2f, bright * dim * ((lit_off & 64) ? 0.4f : 1.1f)) * gk) /* the note's own light (a run of eighths dimmer) */);
      if (nhalo < MAX_NOTES_SHOWN * LANES && !(lit_off & 2)) {
        float hs = size * NOTE_W[l] * 1.7f;
        set_inst(C.halo, nhalo, (t3_vec3){ at.x, at.y, at.z - 0.02f }, 0, (t3_vec3){ hs, hs, 1 });
        t3_instanced_mesh_set_color_at(C.halo, nhalo++, scale(C.p.lane[l], 0.35f * bright * dim * gk));
      }
    }
    /* the flat note gives way to its gem: it shrinks out (drawn black behind the gem, its rim passing over the river
     * on every note of a run was a dark regular flicker) */
    float fsz = size * (1 - gk);
    t3_instanced_mesh *nm = C.old_shapes && l == LANE_BASS ? C.old_bass : C.old_shapes && l == LANE_SYNTH ? C.old_synth : C.notes[l];
    set_inst(nm, cnt[l], at, 0, (t3_vec3){ fsz, fsz, 1 });
    t3_instanced_mesh_set_color_at(nm, cnt[l]++, c);
    float pk = sty(pocket) * in * C.on[l] * (1 - 0.35f * P); /* a gem has a rim of its own: a smaller pocket */
    if (st == NOTE_PENDING && pk > 0.01f && npk < MAX_NOTES_SHOWN * LANES) {
      /* the pocket the size of a round note's under every shape: the triangle's own width made a dark disc a quarter
       * wider passing the bass gate in sixteenths (a regular flicker over the bound on two lanes) */
      float rx = 0.5f * fminf(NOTE_W[l], NOTE_W[LANE_KICK]) * size * 1.45f * pk, ry = C.old_shapes && l == LANE_BASS ? 0.33f * size * 1.9f * pk : rx;
      set_inst(C.pocket, npk++, on_river(l, d, 0, 0.04f), 0, (t3_vec3){ rx, ry, 1 });
    }
  }
  for (int l = 0; l < LANES; l++) {
    t3_instanced_mesh *nm = C.old_shapes && l == LANE_BASS ? C.old_bass : C.old_shapes && l == LANE_SYNTH ? C.old_synth : C.notes[l];
    nm->count = cnt[l];
    changed(nm);
    if (nm != C.notes[l] && C.notes[l]->count) C.notes[l]->count = 0, changed(C.notes[l]);
  }
  if (!C.old_shapes && (C.old_bass->count || C.old_synth->count)) C.old_bass->count = C.old_synth->count = 0, changed(C.old_bass), changed(C.old_synth);
  C.pocket->count = npk;
  for (int l = 0; l < LANES; l++) {
    /* the gem glows its lane's colour from within; the lights and the studio add the highlights over it */
    t3_material *gm = C.gem[l]->mesh.materials[0];
    t3_color ec = scale(C.p.lane[l], ((lit_off & 32) ? 0 : 0.8f) * note_gain * sty(note_light) * dim);
    if (gm->emissive.r != ec.r || gm->emissive.g != ec.g || gm->emissive.b != ec.b) gm->emissive = ec, gm->version++;
    C.gem[l]->count = ngem[l];
    changed(C.gem[l]);
  }
  C.halo->count = nhalo;
  changed(C.halo);
  changed(C.pocket);

  /* the player's hits: a burst at the gate in the river's colour, shards thrown out (up and to the sides, falling a
   * little) and a ring opening out; a Perfect throws more, faster and further, its ring wider and whiter. Each swells
   * in over 40 ms and fades over half a second, so a run of hits keeps the gates alive without a flash */
  for (int l = 0; l < LANES; l++) {
    if (v->hit_seq[l] == C.last_hit[l]) continue;
    C.last_hit[l] = v->hit_seq[l];
    C.bursts++;
    bool pf = v->hit_perfect[l];
    t3_vec3 o = on_river(l, 0, 0, 0);
    int n = pf ? 34 : 20;
    for (int k2 = 0; k2 < n; k2++) {
      int i = C.shard_next++ % SHARDS;
      float ang = -1.35f + 2.7f * ((float)k2 + hf((uint32_t)C.bursts, (uint32_t)k2 * 5 + 1)) / (float)n; /* the upper half and a little below */
      float sp = (pf ? 10.0f : 6.5f) * (0.55f + 0.6f * hf((uint32_t)C.bursts, (uint32_t)k2 * 5 + 2));
      C.shard[i] = (typeof(C.shard[0])){ o.x, o.y, sinf(ang) * sp, cosf(ang) * sp * 0.8f, now, (pf ? 1.15f : 0.9f) * (0.7f + 0.6f * hf((uint32_t)C.bursts, (uint32_t)k2 * 5 + 3)), (uint8_t)l, pf };
    }
    int ri = C.ring_next++ % RINGS;
    C.ring[ri].x = o.x, C.ring[ri].y = o.y, C.ring[ri].born = now, C.ring[ri].lane = (uint8_t)l, C.ring[ri].perfect = pf;
  }
  for (int i = 0; i < SHARDS; i++) {
    float age = (now - C.shard[i].born) / 1000.0f;
    if (age < 0 || age > 0.55f) { hide(C.shards, i); continue; }
    float drag = (1 - expf(-age * 4.0f)) / 4.0f; /* the distance a shard covers, slowing as it goes */
    float x = C.shard[i].x + C.shard[i].vx * drag, y = C.shard[i].y + C.shard[i].vy * drag - 2.2f * age * age;
    float env = fminf(1, age / 0.04f) * (1 - age / 0.55f) * (1 - age / 0.55f);
    float sz = C.shard[i].size * (1 - 0.5f * age / 0.55f);
    set_inst(C.shards, i, (t3_vec3){ x, y, 0.14f }, age * 5 + (float)i, (t3_vec3){ sz, sz, 1 });
    t3_color c = mix(C.p.lane[C.shard[i].lane], (t3_color){ 1, 1, 1 }, C.shard[i].perfect ? 0.4f : 0.15f);
    t3_instanced_mesh_set_color_at(C.shards, i, scale(c, 1.9f * env * dim * sty(burst)));
  }
  changed(C.shards);
  for (int i = 0; i < RINGS; i++) {
    float age = (now - C.ring[i].born) / 1000.0f, life = C.ring[i].perfect ? 0.5f : 0.4f;
    if (age < 0 || age > life) { hide(C.rings, i); continue; }
    float u = age / life, r = 0.8f + (C.ring[i].perfect ? 3.4f : 2.2f) * (1 - (1 - u) * (1 - u));
    float env = fminf(1, age / 0.04f) * (1 - u) * (1 - u);
    set_inst(C.rings, i, (t3_vec3){ C.ring[i].x, C.ring[i].y, 0.12f }, 0, (t3_vec3){ r, r * 0.55f, 1 });
    t3_color c = mix(C.p.lane[C.ring[i].lane], (t3_color){ 1, 1, 1 }, C.ring[i].perfect ? 0.45f : 0.1f);
    t3_instanced_mesh_set_color_at(C.rings, i, scale(c, 1.3f * env * dim * sty(burst)));
  }
  changed(C.rings);

  land_update(v, beat, now, dim);
  streak_update(v, now, dim);
  if (C.notes_solo) /* only the notes (the footprint gate) */
    for (int k = 0; k < C.root->child_count; k++) {
      t3_object *o = C.root->children[k];
      bool note = o == T3_OBJ(C.old_bass) || o == T3_OBJ(C.old_synth);
      for (int l = 0; l < LANES; l++) note |= o == T3_OBJ(C.notes[l]);
      o->visible = note;
    }
}

/* the camera: z 22 looking at the origin, 40 degrees: the plane z = 0 spans VIEW_HALF_H up and down */
static void to_canvas(t3_vec3 p, float cw, float ch, float *x, float *y) {
  float k = (ch * 0.5f) / VIEW_HALF_H * (C.zoom > 0 ? C.zoom : 1);
  *x = cw * 0.5f + p.x * k;
  *y = ch * 0.5f - p.y * k;
}
void club_set_place(const int place[SOLO_LANES]) {
  for (int l = 0; l < SOLO_LANES; l++) C.place[l] = place[l] >= 0 && place[l] < SOLO_LANES ? place[l] : l;
}
float club_lane_shown(int lane) { return C.built && lane >= 0 && lane < LANES ? C.on[lane] : lane < SOLO_LANES; }
int club_place(int lane) { return C.place[lane]; }
void club_gate_screen(int lane, float cw, float ch, float *x, float *y) {
  if (!C.built) { *x = cw * 0.5f, *y = ch * 0.8f; return; }
  to_canvas(on_river(lane, 0, 0, 0), cw, ch, x, y);
}
void club_center_screen(float cw, float ch, float *x, float *y, float *half_w) {
  to_canvas((t3_vec3){ 0, (GATE_Y + TOP_Y) * 0.5f, 0 }, cw, ch, x, y);
  *half_w = (outer_x() * C.spread + RIVER_W * 0.5f) * (ch * 0.5f) / VIEW_HALF_H * (C.zoom > 0 ? C.zoom : 1);
}
void club_lane_rgb(int lane, float rgb[3]) {
  t3_color c = C.built && lane >= 0 && lane < LANES ? C.p.lane[lane] : (t3_color){ 1, 1, 1 };
  float m = fmaxf(c.r, fmaxf(c.g, c.b));
  if (m <= 0) m = 1;
  /* a little lighter than the river, so the HUD's text reads on its box */
  rgb[0] = 0.25f + 0.75f * c.r / m, rgb[1] = 0.25f + 0.75f * c.g / m, rgb[2] = 0.25f + 0.75f * c.b / m;
}

/* `rivers`: each lane's river as last drawn; `rivers hidden draw|hide` */
static void cmd_rivers(int argc, const char **argv, cw_reply *r) {
  if (argc >= 3 && !strcmp(argv[1], "hidden")) C.hidden_draw = !strcmp(argv[2], "draw");
  cw_reply_printf(r, "{\"hidden_draw\":%d,\"rivers\":[", C.hidden_draw);
  for (int l = 0; l < LANES; l++) {
    float x, y;
    to_canvas(on_river(l, 0, 0, 0), 1920, 1080, &x, &y);
    cw_reply_printf(r, "%s{\"lane\":%d,\"shown\":%.3f,\"x\":%.1f,\"y\":%.1f}", l ? "," : "", l, C.on[l], x, y);
  }
  cw_reply_printf(r, "]}");
}

/* `notes`: the notes on the rivers as last drawn ({lane, ahead in beats, y and width in pixels of a 1920 x 1080 frame,
 * brightness}); `notescale game|flat|reverse`: how a note's size follows its distance (flat and reverse are the size
 * gate's controls, tests/beatsync-depth.mjs) */
static void cmd_notes(int argc, const char **argv, cw_reply *r) {
  if (argc >= 3 && !strcmp(argv[1], "hide")) { /* `notes hide 1`: the whole field hidden (the readability gate looks for note-like shapes in the backdrop alone) */
    cw_node lv = cw_node_get(cw_tree_root(cw_scene_app_tree()), "Level");
    cw_node_set(cw_node_get(lv, "Field"), "visible", cw_val_bool(atoi(argv[2]) == 0));
    cw_reply_printf(r, "{\"hidden\":%d}", atoi(argv[2]) != 0);
    return;
  }
  if (argc >= 3 && !strcmp(argv[1], "solo")) {
    C.notes_solo = atoi(argv[2]) != 0;
    for (int i = 0; i < C.root->child_count; i++) C.root->children[i]->visible = true;
    cw_reply_printf(r, "{\"solo\":%d}", C.notes_solo);
    return;
  }
  if (argc >= 2 && (!strcmp(argv[1], "old") || !strcmp(argv[1], "new"))) { /* `notes old`: the old bass bar and synth hexagon (the footprint gate's control) */
    C.old_shapes = !strcmp(argv[1], "old");
    cw_reply_printf(r, "{\"old\":%d}", C.old_shapes);
    return;
  }
  float gx, gy;
  to_canvas(on_river(0, 0, 0, 0), 1920, 1080, &gx, &gy);
  cw_reply_printf(r, "{\"notescale\":%d,\"ahead\":%.3f,\"gate_y\":%.1f,\"notes\":[", C.notescale, C.ahead, gy);
  for (int i = 0; i < C.probe_n; i++)
    cw_reply_printf(r, "%s{\"lane\":%d,\"ahead\":%.3f,\"x\":%.1f,\"y\":%.1f,\"px\":%.2f,\"unit\":%.3f,\"bright\":%.3f}", i ? "," : "", C.probe[i].lane, C.probe[i].ahead, C.probe[i].x, C.probe[i].y, C.probe[i].px, C.probe[i].px / NOTE_W[C.probe[i].lane], C.probe[i].bright);
  cw_reply_printf(r, "]}");
}
/* `road tilt|rough|metal|env <v>`: the glass road's reflection (a look at it); `road` reads it */
static void cmd_road(int argc, const char **argv, cw_reply *r) {
  t3_material *m = C.plate_mat;
  if (argc >= 3 && m) {
    float v = (float)atof(argv[2]);
    if (!strcmp(argv[1], "tilt")) {
      t3_geometry *g = road_geometry(v), *old = C.plate->mesh.geometry;
      C.plate->mesh.geometry = g, t3_release(old);
    } else if (!strcmp(argv[1], "rough")) m->roughness = v;
    else if (!strcmp(argv[1], "metal")) m->metalness = v;
    else if (!strcmp(argv[1], "env")) m->env_map_intensity = v;
    else if (!strcmp(argv[1], "coat")) m->clearcoat = v;
    m->version++;
  }
  cw_reply_printf(r, "{\"rough\":%.3f,\"metal\":%.3f,\"env\":%.3f,\"coat\":%.3f}", m ? m->roughness : 0, m ? m->metalness : 0, m ? m->env_map_intensity : 0, m ? m->clearcoat : 0);
}
static void cmd_lit(int argc, const char **argv, cw_reply *r) {
  if (argc >= 2) lit_off = atoi(argv[1]);
  cw_reply_printf(r, "{\"off\":%d}", lit_off);
}
static void cmd_notescale(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "notescale game|flat|reverse"); return; }
  C.notescale = !strcmp(argv[1], "flat") ? 1 : !strcmp(argv[1], "reverse") ? 2 : 0;
  cw_reply_printf(r, "{\"notescale\":%d}", C.notescale);
}
/* `land`: the land's rows as last drawn ({beat, height, hue, bands, stab, crest}); `land game|flat|off`: the song's
 * look, one flat height and colour (the land gate's control), or no land */
static void cmd_land(int argc, const char **argv, cw_reply *r) {
  if (argc >= 2) {
    C.land_mode = !strcmp(argv[1], "flat") ? 1 : !strcmp(argv[1], "off") ? 2 : 0;
    cw_reply_printf(r, "{\"land\":%d}", C.land_mode);
    return;
  }
  cw_reply_printf(r, "{\"mode\":%d,\"rows\":[", C.land_mode);
  for (int i = 0; i < C.land_n; i++) {
    const struct land_probe *q = &C.land_probe[i];
    cw_reply_printf(r, "%s{\"beat\":%.2f,\"height\":%.3f,\"hue\":%.1f,\"bands\":[%.2f,%.2f,%.2f],\"stab\":%d,\"crest\":%d}", i ? "," : "", q->beat, q->height, q->hue, q->bands[0], q->bands[1], q->bands[2], q->stab, q->crest);
  }
  cw_reply_printf(r, "]}");
}
void club_register_debug(void) {
  cw_debug_command("rivers", "rivers: each lane's river as last drawn (how far it shows, its gate x and y in pixels of a 1920 x 1080 frame); rivers hidden draw|hide: a river not shown drawn dark (the rivers gate's control) or left out (src/club.c)", cmd_rivers);
  cw_debug_command("land", "land [game|flat|off]: the land's rows as last drawn (beat, height, hue, bands, stab, crest), or how it reads the song (src/club.c)", cmd_land);
  cw_debug_command("notes", "notes: the notes on the rivers as last drawn (lane, beats ahead, y and width in pixels) (src/club.c)", cmd_notes);
  cw_debug_command("notescale", "notescale game|flat|reverse: how a note's size follows its distance (the size gate's controls)", cmd_notescale);
  cw_debug_command("road", "road [tilt|rough|metal|env|coat <value>]: the glass road's reflection, read or set (a look at it)", cmd_road);
  cw_debug_command("lit", "lit <mask>: leave parts of the lit picture out (1 gems, 2 halos, 4 gate glow, 8 bank glow, 16 the low lights)", cmd_lit);
}
