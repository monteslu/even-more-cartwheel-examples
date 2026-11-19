/* The picture (club.h): the sequencer ring and the field, as flat neon shapes for the ASCII pass.
 *
 * The camera looks straight at the plane z = 0 (main.cwscene: Camera at z 22, 40 degrees): 16 units of height on
 * screen, about 28 of width. Everything is unlit colour (MeshBasicMaterial) on black: a shape's brightness is the
 * character the ASCII pass picks for it, so every effect here is a brightness that rises and falls. The ring sits a
 * little above the middle; the bass waveform runs along the bottom.
 *
 * The ring turns with the song: a note b beats ahead of now sits b/4 of a turn before 12 o'clock (anticlockwise from
 * it), so it moves clockwise and reaches 12 o'clock on its beat. It is drawn brighter and larger as it comes, so its
 * character grows denser (+ # @) exactly on the beat. The 16 step marks of the bar turn with it, and the marks just
 * past 12 o'clock glow and fade: the playhead's tail.
 *
 * Grounding: nothing here stands on anything (it is a picture of light): the Field node is in the group airborne. */
#include "club.h"

#include <math.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/scene.h"
#include "cartwheel/scene_file.h"

#define CX 0.0f
#define CY 0.9f
#define VIEW_HALF_H 8.0f /* camera at z 22, 40 degrees */
static const float RADIUS[LANES] = { 2.5f, 3.9f, 5.3f };
#define AHEAD_BEATS 3.4f /* how far ahead the ring shows notes (of the 4 a turn) */
#define MAX_NOTES_SHOWN 64
#define STEPS 16
#define WAVES 6
#define SPARKS 120
#define WAVE_COLS 72
#define FIELD_W 54
#define FIELD_H 28
#define TAIL 10
/* a note's brightness far off (NB0) and what it gains by its beat (NB1), and its size far off (NS0) */
#define NB0 0.6f
#define NB1 2.0f
#define NS0 0.95f

typedef struct pal { t3_color lane[LANES], bg; } pal;
/* a palette per section: intro violet, build warming, drop hot magenta and orange, break cool cyan, outro blue */
static const pal PALS[6] = {
  [SEC_INTRO] = { { { 0.75f, 0.25f, 1 }, { 0.2f, 0.7f, 1 }, { 1, 0.45f, 0.15f } }, { 0.35f, 0.18f, 0.9f } },
  [SEC_BUILD] = { { { 1, 0.45f, 0.1f }, { 1, 0.9f, 0.3f }, { 0.95f, 0.2f, 0.75f } }, { 0.85f, 0.4f, 0.08f } },
  [SEC_DROP] = { { { 1, 0.1f, 0.65f }, { 1, 0.55f, 0.05f }, { 0.2f, 0.65f, 1 } }, { 0.9f, 0.08f, 0.45f } },
  [SEC_BREAK] = { { { 0.1f, 0.8f, 1 }, { 0.45f, 1, 0.6f }, { 0.6f, 0.3f, 1 } }, { 0.05f, 0.5f, 0.75f } },
  [SEC_OUTRO] = { { { 0.35f, 0.45f, 1 }, { 0.75f, 0.35f, 1 }, { 0.3f, 1, 0.6f } }, { 0.2f, 0.25f, 0.8f } },
  [SEC_OTHER] = { { { 0.75f, 0.25f, 1 }, { 0.2f, 0.7f, 1 }, { 1, 0.45f, 0.15f } }, { 0.35f, 0.18f, 0.9f } },
};
/* how busy the background is in each kind of section */
static const float FIELD_DENSITY[6] = { [SEC_INTRO] = 0.35f, [SEC_BUILD] = 0.55f, [SEC_DROP] = 0.9f, [SEC_BREAK] = 0.15f, [SEC_OUTRO] = 0.3f, [SEC_OTHER] = 0.4f };

static struct {
  t3_object *root;
  t3_instanced_mesh *notes[LANES], *steps, *tail, *waves, *sparks, *wave_cols, *field, *flare, *core;
  t3_material *ring_mat[LANES];
  cw_node camera;
  float zoom;
  float wave_t[WAVES]; /* song ms each shockwave started at */
  int wave_next;
  int32_t last_kick, last_clap;
  struct { float x, y, vx, vy, born; } spark[SPARKS];
  int spark_next, claps;
  pal p;
  float density;
  float field_phase[FIELD_W * FIELD_H];
  float bass_amp;
  bool built;
} C;

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

static t3_material *unlit(void) { return t3_mesh_basic_material_new(0xffffff); }

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

/* a point at clock angle a (radians clockwise from 12 o'clock) on radius r */
static t3_vec3 at_clock(float a, float r, float z) { return (t3_vec3){ CX + r * sinf(a), CY + r * cosf(a), z }; }

void club_build(void) {
  if (C.built) return;
  memset(&C, 0, sizeof C);
  cw_node level = cw_node_get(cw_tree_root(cw_scene_app_tree()), "Level");
  C.root = cw_node3d_object(cw_node_get(level, "Field"));
  if (!C.root) { cw_error("main.cwscene: Level/Field (Node3D) is missing"); return; }
  C.built = true;
  C.camera = cw_node_get(level, "Camera");
  C.zoom = 1;
  /* the rings: a thin circle per instrument */
  for (int l = 0; l < LANES; l++) {
    t3_geometry *g = t3_torus_geometry_new(RADIUS[l], 0.12f, 6, 128, 6.2831853f);
    C.ring_mat[l] = unlit();
    t3_mesh *m = t3_mesh_new(g, C.ring_mat[l]);
    t3_release(g);
    t3_object_set_position(m, CX, CY, 0);
    t3_object_add(C.root, m);
    t3_release(m);
  }
  /* the notes: kick round, clap a diamond, bass a long bar along the ring */
  C.notes[LANE_KICK] = instanced(t3_circle_geometry_new(0.68f, 24), MAX_NOTES_SHOWN);
  C.notes[LANE_CLAP] = instanced(t3_circle_geometry_new(0.85f, 4), MAX_NOTES_SHOWN);
  C.notes[LANE_BASS] = instanced(t3_plane_geometry_new(1.7f, 0.66f, 1, 1), MAX_NOTES_SHOWN);
  C.steps = instanced(t3_plane_geometry_new(0.2f, 0.62f, 1, 1), STEPS * LANES);
  C.tail = instanced(t3_plane_geometry_new(0.2f, 3.6f, 1, 1), TAIL);
  C.flare = instanced(t3_circle_geometry_new(0.75f, 24), LANES);
  C.core = instanced(t3_circle_geometry_new(1.0f, 32), 1);
  C.waves = instanced(t3_ring_geometry_new(0.88f, 1.0f, 72, 1), WAVES);
  C.sparks = instanced(t3_plane_geometry_new(0.22f, 0.22f, 1, 1), SPARKS);
  C.wave_cols = instanced(t3_plane_geometry_new(0.3f, 1.0f, 1, 1), WAVE_COLS);
  C.field = instanced(t3_plane_geometry_new(0.2f, 0.2f, 1, 1), FIELD_W * FIELD_H);
  for (int i = 0; i < FIELD_W * FIELD_H; i++) C.field_phase[i] = hf(i, 77) * 6.2831853f;
  for (int i = 0; i < WAVES; i++) C.wave_t[i] = -1e9f;
  for (int i = 0; i < SPARKS; i++) C.spark[i].born = -1e9f;
  C.last_kick = C.last_clap = -1;
  for (int l = 0; l < LANES; l++) C.p.lane[l] = even(PALS[SEC_INTRO].lane[l]);
  C.p.bg = even(PALS[SEC_INTRO].bg);
}

/* the section at a time */
static section_kind section_at(const song *s, double ms) {
  section_kind k = SEC_INTRO;
  for (int i = 0; s && i < s->sections; i++)
    if (s->section[i].ms <= ms) k = s->section[i].kind;
  return k;
}

void club_update(const club_view *v) {
  if (!C.built) return;
  float now = (float)v->ms, dt = 1.0f / 60.0f;
  float beat = v->s ? song_beat_at(v->s, now) : v->t * 2;
  float beat_ms = v->s && v->s->bpm > 0 ? 60000.0f / v->s->bpm : 500;
  float dim = 1 - 0.55f * v->dim;
  /* the camera: closer on the title, so the rings stand round the title's box; eased (it glides, never cuts) */
  float zw = v->zoom > 0 ? v->zoom : 1;
  C.zoom += (zw - C.zoom) * 0.04f;
  if (C.camera.id) cw_node3d_set_position(C.camera, (t3_vec3){ 0, 0, 22.0f / C.zoom });

  /* the palette: the section's, eased toward over about a bar */
  section_kind sec = section_at(v->s, now);
  const pal *want = &PALS[sec];
  float k = fminf(1, dt * 1000.0f / (beat_ms * 4) * 3);
  for (int l = 0; l < LANES; l++) C.p.lane[l] = mix(C.p.lane[l], even(want->lane[l]), k);
  C.p.bg = mix(C.p.bg, even(want->bg), k);
  C.density += (FIELD_DENSITY[sec] - C.density) * k;

  /* the rings: an instrument's circle bright while it plays, dimmed to dots while it is missed out */
  for (int l = 0; l < LANES; l++) C.ring_mat[l]->color = scale(C.p.lane[l], (0.05f + 0.3f * v->level[l]) * (1 + 0.6f * v->streak) * dim);

  /* the step marks: 16 a bar on each ring, turning with the song; the bar's first step and each beat brighter, the
   * marks just past 12 o'clock lit by the playhead's tail */
  int n = 0;
  float step0 = floorf(beat * 4);
  int down = v->s ? v->s->downbeat : 0;
  for (int l = 0; l < LANES; l++)
    for (int j = -4; j < STEPS - 4; j++) {
      float st = step0 + (float)j, a = (beat - st / 4) / 4 * 6.2831853f; /* clockwise from 12: past steps positive */
      int sb = (int)st - down * 4;
      sb = ((sb % 16) + 16) % 16;
      float base = sb == 0 ? 0.6f : (sb % 4 == 0) ? 0.34f : 0.14f;
      float tail = a >= 0 ? expf(-a / 0.35f) : 0;
      float bright = (base + 1.4f * tail) * (0.3f + 0.7f * v->level[l]) * dim;
      set_inst(C.steps, n, at_clock(a, RADIUS[l], 0.02f), -a, (t3_vec3){ 1, 1, 1 });
      t3_instanced_mesh_set_color_at(C.steps, n++, scale(C.p.lane[l], bright));
    }
  changed(C.steps);

  /* the playhead at 12 o'clock and its fading tail: spokes across the rings, fading clockwise */
  for (int i = 0; i < TAIL; i++) {
    float a = (float)i * 0.06f;
    set_inst(C.tail, i, at_clock(a, (RADIUS[0] + RADIUS[LANES - 1]) * 0.5f, -0.01f), -a, (t3_vec3){ i == 0 ? 1.4f : 1, i == 0 ? 1.15f : 1.0f, 1 });
    float b = (i == 0 ? 0.9f : 0.5f * expf(-(float)i * 0.45f)) * dim;
    t3_instanced_mesh_set_color_at(C.tail, i, scale(mix(C.p.lane[LANE_CLAP], C.p.lane[LANE_KICK], 0.5f), b * 1.2f));
  }
  changed(C.tail);

  /* the notes: on their ring by how many beats away they are, swelling as they come (the last beat: + # @) */
  int cnt[LANES] = { 0 };
  for (int i = 0; v->notes && i < v->count; i++) {
    float nb = song_beat_at(v->s, (float)v->notes[i].ms), ahead = nb - beat;
    if (ahead > AHEAD_BEATS) break;
    uint8_t st = v->state ? v->state[i] : NOTE_PENDING;
    float since = now - (float)v->notes[i].ms; /* a played note fades over 400 ms: never gone in a frame */
    if ((st == NOTE_HIT && since > 400) || ahead < -0.6f) continue;
    int l = v->notes[i].lane;
    if (cnt[l] >= MAX_NOTES_SHOWN) continue;
    float a = -ahead / 4 * 6.2831853f;
    float in = fminf(1, (AHEAD_BEATS - ahead) / 0.8f); /* they fade in at the far side */
    float near = ahead > 0 ? fmaxf(0, 1 - ahead) : 1;   /* 0 a beat away .. 1 on the beat */
    /* the note condenses as it comes: denser characters (+ # @) on a smaller block, its light (area times brightness)
     * the same all the way, so a run of notes through 12 o'clock is a steady light, never a flicker */
    float bright = st == NOTE_MISSED ? 0.18f * fmaxf(0, 1 + ahead / 0.6f) : (NB0 + NB1 * near * near) * in;
    float size = st == NOTE_MISSED ? 0.75f : NS0 * sqrtf(NB0 / (NB0 + NB1 * near * near));
    if (st == NOTE_HIT) { /* played: it opens out and fades */
      float f = fmaxf(0, 1 - since / 400.0f);
      bright = (NB0 + NB1) * f, size = NS0 * sqrtf(NB0 / (NB0 + NB1)) + 0.45f * (1 - f); /* from the size it had */
    }
    t3_color c = st == NOTE_MISSED ? (t3_color){ bright, bright, bright } : scale(C.p.lane[l], bright * dim);
    set_inst(C.notes[l], cnt[l], at_clock(a, RADIUS[l], 0.05f), -a, (t3_vec3){ size, size, 1 });
    t3_instanced_mesh_set_color_at(C.notes[l], cnt[l]++, c);
  }
  for (int l = 0; l < LANES; l++) {
    C.notes[l]->count = cnt[l];
    changed(C.notes[l]);
  }

  /* the hit flares at 12 o'clock, and the pad pressed */
  for (int l = 0; l < LANES; l++) {
    float f = v->flare[l], pr = v->press[l] * 0.2f, s = 0.5f + 0.4f * f + pr;
    set_inst(C.flare, l, at_clock(0, RADIUS[l], 0.04f), 0, (t3_vec3){ s, s, 1 });
    t3_instanced_mesh_set_color_at(C.flare, l, scale(mix(C.p.lane[l], (t3_color){ 1, 1, 1 }, 0.35f * f), (0.15f + 1.2f * f + pr) * dim));
  }
  changed(C.flare);

  /* the core: breathes with the kick */
  float cs = 0.7f + 0.5f * v->kick;
  set_inst(C.core, 0, (t3_vec3){ CX, CY, 0.01f }, 0, (t3_vec3){ cs, cs, 1 });
  t3_instanced_mesh_set_color_at(C.core, 0, scale(C.p.lane[LANE_KICK], (0.25f + 1.6f * v->kick) * dim));
  changed(C.core);

  /* the kick's shockwaves: a ring from the centre out past the screen's edge in under a second, fading as it goes */
  if (v->kick_ms != C.last_kick) {
    C.last_kick = v->kick_ms;
    C.wave_t[C.wave_next++ % WAVES] = (float)v->kick_ms;
  }
  for (int i = 0; i < WAVES; i++) {
    float age = (now - C.wave_t[i]) / 900.0f;
    if (age < 0 || age > 1) { hide(C.waves, i); continue; }
    float r = 1.2f + 15 * (1 - (1 - age) * (1 - age));
    set_inst(C.waves, i, (t3_vec3){ CX, CY, -0.5f }, 0, (t3_vec3){ r, r, 1 });
    float b = (1 - age) * (1 - age) * 1.15f * (0.4f + 0.6f * v->level[LANE_KICK]) * dim;
    t3_instanced_mesh_set_color_at(C.waves, i, scale(C.p.lane[LANE_KICK], b));
  }
  changed(C.waves);

  /* the clap's sparks: thrown up and out from the clap ring's hit point, falling and fading */
  if (v->clap_ms != C.last_clap) {
    C.last_clap = v->clap_ms;
    C.claps++;
    int burst = 10 + (int)(10 * v->level[LANE_CLAP]);
    t3_vec3 o = at_clock(0, RADIUS[LANE_CLAP], 0);
    for (int k2 = 0; k2 < burst; k2++) {
      int i = C.spark_next++ % SPARKS;
      float ang = -1.2f + 2.4f * hf((uint32_t)C.claps, (uint32_t)k2 * 3 + 1), sp = 4 + 7 * hf((uint32_t)C.claps, (uint32_t)k2 * 3 + 2);
      C.spark[i].x = o.x + (hf((uint32_t)C.claps, (uint32_t)k2 * 3 + 3) - 0.5f) * 0.6f;
      C.spark[i].y = o.y;
      C.spark[i].vx = sinf(ang) * sp;
      C.spark[i].vy = cosf(ang) * sp * 0.7f;
      C.spark[i].born = (float)v->clap_ms;
    }
  }
  for (int i = 0; i < SPARKS; i++) {
    float age = (now - C.spark[i].born) / 1000.0f;
    if (age < 0 || age > 0.9f) { hide(C.sparks, i); continue; }
    float x = C.spark[i].x + C.spark[i].vx * age, y = C.spark[i].y + C.spark[i].vy * age - 4.5f * age * age;
    set_inst(C.sparks, i, (t3_vec3){ x, y, 0.1f }, age * 3, (t3_vec3){ 1, 1, 1 });
    float b = (1 - age / 0.9f) * 1.6f * dim;
    t3_instanced_mesh_set_color_at(C.sparks, i, scale(mix(C.p.lane[LANE_CLAP], (t3_color){ 1, 1, 0.9f }, 0.3f), b));
  }
  changed(C.sparks);

  /* the bass: a waveform of columns along the bottom, thick while the bass plays, thin while it is missed out */
  C.bass_amp += (v->bass * v->level[LANE_BASS] - C.bass_amp) * 0.25f;
  float span = VIEW_HALF_H * 16.0f / 9.0f * 2 * 0.96f;
  for (int i = 0; i < WAVE_COLS; i++) {
    float x = -span / 2 + span * ((float)i + 0.5f) / WAVE_COLS, ph = beat * 3.14159f;
    float w = 0.5f + 0.5f * sinf(x * 0.55f - ph * 2) * cosf(x * 0.21f + ph * 0.5f);
    float h = 0.18f + (0.25f + 2.6f * C.bass_amp) * w * (0.35f + 0.65f * v->level[LANE_BASS]);
    set_inst(C.wave_cols, i, (t3_vec3){ x, -VIEW_HALF_H + 0.5f + h * 0.5f, 0 }, 0, (t3_vec3){ 1, h, 1 });
    t3_instanced_mesh_set_color_at(C.wave_cols, i, scale(C.p.lane[LANE_BASS], (0.35f + 1.3f * C.bass_amp * w) * (0.3f + 0.7f * v->level[LANE_BASS]) * dim));
  }
  changed(C.wave_cols);

  /* the field: faint glyphs behind everything, sparse in a break, dense in a drop, fuller with a streak; each
   * twinkles slowly (over seconds, never a flicker), and the kick's wave lifts the ones it passes */
  float dens = fminf(1, C.density * (0.6f + 0.4f * v->level[0]) + 0.35f * v->streak);
  float fw = VIEW_HALF_H * 16.0f / 9.0f * 2, fh = VIEW_HALF_H * 2;
  for (int y = 0; y < FIELD_H; y++)
    for (int x = 0; x < FIELD_W; x++) {
      int i = y * FIELD_W + x;
      /* a scatter, not a grid: a grid of dots lines up with the character cells into columns */
      float px = -fw / 2 + fw * ((float)x + hf((uint32_t)i, 11)) / FIELD_W, py = -fh / 2 + fh * ((float)y + hf((uint32_t)i, 12)) / FIELD_H;
      float tw = 0.5f + 0.5f * sinf(v->t * (0.6f + hf((uint32_t)i, 3)) + C.field_phase[i]);
      float on = hf((uint32_t)i, 5) < dens ? tw : 0;
      float d = sqrtf((px - CX) * (px - CX) + (py - CY) * (py - CY)), lift = 0;
      for (int w = 0; w < WAVES; w++) {
        float age = (now - C.wave_t[w]) / 900.0f;
        if (age < 0 || age > 1) continue;
        float r = 1.2f + 15 * (1 - (1 - age) * (1 - age));
        lift += expf(-(d - r) * (d - r) * 1.5f) * (1 - age) * 0.6f;
      }
      float b = (on * (0.2f + 0.35f * v->streak) + lift * (0.4f + 0.6f * on)) * dim;
      set_inst(C.field, i, (t3_vec3){ px * 25.0f / 22.0f, py * 25.0f / 22.0f, -3 }, 0, (t3_vec3){ 1, 1, 1 }); /* z -3: a little farther, a little larger */
      t3_instanced_mesh_set_color_at(C.field, i, scale(C.p.bg, b));
    }
  changed(C.field);
}

/* the camera: z 22 looking at the origin, 40 degrees: the plane z = 0 spans VIEW_HALF_H up and down */
static void to_canvas(t3_vec3 p, float cw, float ch, float *x, float *y) {
  float k = (ch * 0.5f) / VIEW_HALF_H * (C.zoom > 0 ? C.zoom : 1);
  *x = cw * 0.5f + p.x * k;
  *y = ch * 0.5f - p.y * k;
}
void club_hit_screen(int lane, float cw, float ch, float *x, float *y) { to_canvas(at_clock(0, RADIUS[lane], 0), cw, ch, x, y); }
void club_center_screen(float cw, float ch, float *x, float *y, float *r) {
  to_canvas((t3_vec3){ CX, CY, 0 }, cw, ch, x, y);
  *r = RADIUS[LANES - 1] * (ch * 0.5f) / VIEW_HALF_H * (C.zoom > 0 ? C.zoom : 1);
}
void club_lane_rgb(int lane, float rgb[3]) {
  t3_color c = C.built && lane >= 0 && lane < LANES ? C.p.lane[lane] : (t3_color){ 1, 1, 1 };
  float m = fmaxf(c.r, fmaxf(c.g, c.b));
  if (m <= 0) m = 1;
  /* a little lighter than the ring, so the HUD's text reads on its box */
  rgb[0] = 0.25f + 0.75f * c.r / m, rgb[1] = 0.25f + 0.75f * c.g / m, rgb[2] = 0.25f + 0.75f * c.b / m;
}
