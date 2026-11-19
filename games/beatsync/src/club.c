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
 * highway recedes a little: a river narrows toward the top, so a note far off is small and grows as it comes, and it
 * brightens over its last beat, so its characters grow denser (+ # @) on the beat. How many beats a river shows is
 * the view's `ahead`: more on Easy (slower, readable well ahead), fewer on Hard.
 *
 * The mapping: d beats ahead is a scale s = 1 / (1 + c d), 1 at the gate and FAR at the top (c from `ahead`); the
 * height on screen follows (1 - s), so the rivers converge toward a point above the screen, and x and sizes scale by
 * s. Past the gate (d < 0) the same mapping carries the outflow on down.
 *
 * Grounding: nothing here stands on anything (it is a picture of light): the Field node is in the group airborne. */
#include "club.h"

#include <math.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/scene.h"
#include "cartwheel/scene_file.h"

#define VIEW_HALF_H 8.0f /* camera at z 22, 40 degrees */
#define GATE_Y (-4.7f)   /* the gate line */
#define TOP_Y 7.6f       /* where a river starts (its far end, `ahead` beats up) */
#define LANE_DX 4.5f     /* the rivers' centres at the gate: -LANE_DX, 0, LANE_DX */
#define RIVER_W 3.0f     /* a river's width at the gate (a gap of 1.5 between rivers) */
#define FAR 0.62f        /* a river's scale at its far end */
#define PAST 0.7f        /* beats a river runs on below the gate */
#define MAX_NOTES_SHOWN 64
#define FLOW 70          /* current streaks a river */
#define BANK_SEGS 22     /* segments in each bank */
#define TICKS 12         /* beat lines a river */
#define WAVES 6
#define SPARKS 120
#define WAVE_COLS 72
#define FIELD_W 54
#define FIELD_H 28
/* a note's brightness far off (NB0) and what it gains by its beat (NB1) */
#define NB0 0.6f
#define NB1 2.0f

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
  t3_instanced_mesh *notes[LANES], *flow, *banks, *ticks, *gates, *waves, *sparks, *wave_cols, *field, *flare;
  cw_node camera;
  float zoom, spread, ahead;
  int place[LANES]; /* the river each lane is drawn as: 0 left, 1 middle, 2 right (the layout gate swaps it) */
  float wave_t[WAVES]; /* song ms each shockwave started at */
  int wave_next;
  int32_t last_kick, last_clap;
  struct { float x, y, vx, vy, born; } spark[SPARKS];
  int spark_next, claps;
  pal p;
  float density;
  float field_phase[FIELD_W * FIELD_H];
  float flow_u[FLOW], flow_ph[FLOW], flow_len[FLOW];
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

/* the highway: d beats ahead of the gate (negative past it) as a scale, and a point across a river at it (u from -0.5,
 * the left bank, to 0.5, the right) */
static float persp(float d) {
  float c = (1 / FAR - 1) / fmaxf(C.ahead, 0.5f);
  return 1 / fmaxf(0.2f, 1 + c * d);
}
static t3_vec3 on_river(int lane, float d, float u, float z) {
  float s = persp(d), p = (1 - s) / (1 - FAR);
  float cx = (float)(C.place[lane] - 1) * LANE_DX * C.spread;
  return (t3_vec3){ (cx + u * RIVER_W) * s, GATE_Y + (TOP_Y - GATE_Y) * p, z };
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
  for (int l = 0; l < LANES; l++) C.place[l] = l;
  /* the notes: kick round, clap a diamond, bass a long bar across its river */
  C.notes[LANE_KICK] = instanced(t3_circle_geometry_new(0.72f, 24), MAX_NOTES_SHOWN);
  C.notes[LANE_CLAP] = instanced(t3_circle_geometry_new(0.88f, 4), MAX_NOTES_SHOWN);
  C.notes[LANE_BASS] = instanced(t3_plane_geometry_new(2.0f, 0.66f, 1, 1), MAX_NOTES_SHOWN);
  C.flow = instanced(t3_plane_geometry_new(0.55f, 0.12f, 1, 1), FLOW * LANES);
  C.banks = instanced(t3_plane_geometry_new(0.14f, 1.0f, 1, 1), BANK_SEGS * 2 * LANES);
  C.ticks = instanced(t3_plane_geometry_new(1.0f, 0.1f, 1, 1), TICKS * LANES);
  C.gates = instanced(t3_plane_geometry_new(1.0f, 0.34f, 1, 1), LANES);
  C.flare = instanced(t3_circle_geometry_new(1.0f, 32), LANES);
  C.waves = instanced(t3_ring_geometry_new(0.9f, 1.0f, 72, 1), WAVES);
  C.sparks = instanced(t3_plane_geometry_new(0.22f, 0.22f, 1, 1), SPARKS);
  C.wave_cols = instanced(t3_plane_geometry_new(0.3f, 1.0f, 1, 1), WAVE_COLS);
  C.field = instanced(t3_plane_geometry_new(0.2f, 0.2f, 1, 1), FIELD_W * FIELD_H);
  for (int i = 0; i < FIELD_W * FIELD_H; i++) C.field_phase[i] = hf(i, 77) * 6.2831853f;
  for (int i = 0; i < FLOW; i++) {
    C.flow_u[i] = -0.42f + 0.84f * hf(i, 21);
    C.flow_ph[i] = hf(i, 22);
    C.flow_len[i] = 0.5f + 0.7f * hf(i, 23);
  }
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
  /* the camera and the highway's shape, eased (they glide, never cut): the title spreads the rivers round its box */
  float zw = v->zoom > 0 ? v->zoom : 1, sw = v->spread > 0 ? v->spread : 1, aw = v->ahead > 0 ? v->ahead : 3;
  C.zoom += (zw - C.zoom) * 0.04f;
  C.spread += (sw - C.spread) * 0.04f;
  C.ahead += (aw - C.ahead) * 0.06f;
  if (C.camera.id) cw_node3d_set_position(C.camera, (t3_vec3){ 0, 0, 22.0f / C.zoom });

  /* the palette: the section's, eased toward over about a bar */
  section_kind sec = section_at(v->s, now);
  const pal *want = &PALS[sec];
  float k = fminf(1, dt * 1000.0f / (beat_ms * 4) * 3);
  for (int l = 0; l < LANES; l++) C.p.lane[l] = mix(C.p.lane[l], even(want->lane[l]), k);
  C.p.bg = mix(C.p.bg, even(want->bg), k);
  C.density += (FIELD_DENSITY[sec] - C.density) * k;

  float span = C.ahead + PAST; /* the current runs from the far end to below the gate */
  for (int l = 0; l < LANES; l++) {
    /* a river plays at full strength while it is hit, dimmed to dots while it is missed out; a streak brightens it */
    float lv = (0.3f + 0.7f * v->level[l]) * (1 + 0.4f * v->streak) * dim, fl = v->flare[l];

    /* the current: streaks riding down at the notes' speed, a gentle sway across; a hit surges the water round the
     * gate (brighter and a little wider), swelling with the flare and fading with it */
    for (int i = 0; i < FLOW; i++) {
      float d = C.ahead - fmodf(beat + C.flow_ph[i] * span + (float)l * 0.37f, span);
      float u = C.flow_u[i] + 0.04f * sinf(beat * 1.3f + C.flow_ph[i] * 9);
      float near_gate = expf(-fabsf(d) * 1.8f), surge = 1 + 2.4f * fl * near_gate;
      u *= 1 + 0.25f * fl * near_gate;
      float s = persp(d), in = fminf(1, (C.ahead - d) / 0.6f), out = d < 0 ? fmaxf(0, 1 + d / PAST) : 1;
      float b = (0.34f + 0.14f * hf((uint32_t)i, (uint32_t)l + 40)) * lv * surge * in * out;
      set_inst(C.flow, l * FLOW + i, on_river(l, d, u, -0.2f), 0, (t3_vec3){ C.flow_len[i] * s, s, 1 });
      t3_instanced_mesh_set_color_at(C.flow, l * FLOW + i, scale(C.p.lane[l], b));
    }

    /* the banks: two lines down the river's sides, fading out at the far end and below the gate */
    for (int side = 0; side < 2; side++)
      for (int j = 0; j < BANK_SEGS; j++) {
        float d0 = C.ahead - span * (float)j / BANK_SEGS, d1 = C.ahead - span * (float)(j + 1) / BANK_SEGS, u = side ? 0.53f : -0.53f;
        t3_vec3 a = on_river(l, d0, u, -0.1f), b2 = on_river(l, d1, u, -0.1f);
        float dx = b2.x - a.x, dy = b2.y - a.y, len = sqrtf(dx * dx + dy * dy), dm = 0.5f * (d0 + d1);
        float fade = fminf(1, (C.ahead - dm) / 0.8f) * (dm < 0 ? fmaxf(0, 1 + dm / PAST) : 1);
        int idx = (l * 2 + side) * BANK_SEGS + j;
        set_inst(C.banks, idx, (t3_vec3){ (a.x + b2.x) * 0.5f, (a.y + b2.y) * 0.5f, -0.1f }, -atan2f(dx, dy), (t3_vec3){ 1, len * 1.04f, 1 });
        t3_instanced_mesh_set_color_at(C.banks, idx, scale(C.p.lane[l], 0.42f * lv * fade));
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
      t3_instanced_mesh_set_color_at(C.ticks, idx, scale(C.p.lane[l], (bb == 0 ? 0.7f : 0.38f) * lv * fade));
    }

    /* the gate: a bar across the river at the hit line, lit while the river plays; it blooms on a hit and a press
     * lifts it a little; behind it the flare, a soft glow that swells and fades */
    float pr = v->press[l];
    t3_vec3 g = on_river(l, 0, 0, 0.06f);
    set_inst(C.gates, l, g, 0, (t3_vec3){ (RIVER_W + 0.5f) * (1 + 0.08f * fl), 1 + 0.5f * fl + 0.2f * pr, 1 });
    t3_instanced_mesh_set_color_at(C.gates, l, scale(mix(C.p.lane[l], (t3_color){ 1, 1, 1 }, 0.3f * fl), (0.85f + 0.2f * pr) * (0.45f + 0.55f * v->level[l]) * dim + 0.55f * fl * dim));
    float fs = 0.6f + 1.0f * fl + 0.15f * pr;
    set_inst(C.flare, l, (t3_vec3){ g.x, g.y, 0.04f }, 0, (t3_vec3){ fs * 1.7f, fs * 0.8f, 1 });
    t3_instanced_mesh_set_color_at(C.flare, l, scale(mix(C.p.lane[l], (t3_color){ 1, 1, 1 }, 0.3f * fl), (0.08f + 0.6f * fl + 0.12f * pr) * dim));
  }
  changed(C.flow);
  changed(C.banks);
  changed(C.ticks);
  changed(C.gates);
  changed(C.flare);

  /* the notes: up their river by how many beats away they are, nearer and larger as they come, and over the last beat
   * they condense: denser characters (+ # @ on the beat) on a smaller block, the light (area times brightness) the
   * same, so a run of notes riding down is a steady light, never a flicker (the flash check's regular-flicker bound);
   * a played note opens out at the gate and fades, a missed one greys and drifts on down */
  int cnt[LANES] = { 0 };
  for (int i = 0; v->notes && i < v->count; i++) {
    float nb = song_beat_at(v->s, (float)v->notes[i].ms), ahead = nb - beat;
    if (ahead > C.ahead) break;
    uint8_t st = v->state ? v->state[i] : NOTE_PENDING;
    float since = now - (float)v->notes[i].ms; /* a played note fades over 400 ms: never gone in a frame */
    if ((st == NOTE_HIT && since > 400) || ahead < -PAST) continue;
    int l = v->notes[i].lane;
    if (cnt[l] >= MAX_NOTES_SHOWN) continue;
    float in = fminf(1, (C.ahead - ahead) / 0.5f);    /* they fade in at the far end */
    float near = ahead > 0 ? fmaxf(0, 1 - ahead) : 1; /* 0 a beat away .. 1 on the beat */
    float d = ahead, s = persp(ahead), size = s * sqrtf(NB0 / (NB0 + NB1 * near * near));
    float bright = (NB0 + NB1 * near * near) * in;
    if (st == NOTE_MISSED) bright = 0.3f * fmaxf(0, 1 + ahead / PAST), size = s * 0.8f;
    if (st == NOTE_HIT) { /* played: it stays at the gate, opens out and fades */
      float f = fmaxf(0, 1 - since / 400.0f);
      d = 0, bright = (NB0 + NB1) * f, size = sqrtf(NB0 / (NB0 + NB1)) + 0.5f * (1 - f); /* from the size it had */
    }
    t3_color c = st == NOTE_MISSED ? (t3_color){ bright, bright, bright } : scale(C.p.lane[l], bright * dim);
    set_inst(C.notes[l], cnt[l], on_river(l, d, 0, 0.1f), 0, (t3_vec3){ size, size, 1 });
    t3_instanced_mesh_set_color_at(C.notes[l], cnt[l]++, c);
  }
  for (int l = 0; l < LANES; l++) {
    C.notes[l]->count = cnt[l];
    changed(C.notes[l]);
  }

  /* the kick's shockwaves: a ring from below the middle of the gates out past the screen's edge in under a second,
   * fading as it goes, behind the rivers */
  t3_vec3 wo = { 0, GATE_Y, -0.5f };
  if (v->kick_ms != C.last_kick) {
    C.last_kick = v->kick_ms;
    C.wave_t[C.wave_next++ % WAVES] = (float)v->kick_ms;
  }
  for (int i = 0; i < WAVES; i++) {
    float age = (now - C.wave_t[i]) / 900.0f;
    if (age < 0 || age > 1) { hide(C.waves, i); continue; }
    float r = 1.2f + 17 * (1 - (1 - age) * (1 - age));
    set_inst(C.waves, i, wo, 0, (t3_vec3){ r, r, 1 });
    float b = (1 - age) * (1 - age) * 0.3f * (0.4f + 0.6f * v->level[LANE_KICK]) * dim;
    t3_instanced_mesh_set_color_at(C.waves, i, scale(C.p.lane[LANE_KICK], b));
  }
  changed(C.waves);

  /* the clap's sparks: thrown up and out from the clap's gate, falling and fading */
  if (v->clap_ms != C.last_clap) {
    C.last_clap = v->clap_ms;
    C.claps++;
    int burst = 10 + (int)(10 * v->level[LANE_CLAP]);
    t3_vec3 o = on_river(LANE_CLAP, 0, 0, 0);
    for (int k2 = 0; k2 < burst; k2++) {
      int i = C.spark_next++ % SPARKS;
      float ang = -1.2f + 2.4f * hf((uint32_t)C.claps, (uint32_t)k2 * 3 + 1), sp = 4 + 7 * hf((uint32_t)C.claps, (uint32_t)k2 * 3 + 2);
      C.spark[i].x = o.x + (hf((uint32_t)C.claps, (uint32_t)k2 * 3 + 3) - 0.5f) * 1.2f;
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
    float b = (1 - age / 0.9f) * 0.6f * dim;
    t3_instanced_mesh_set_color_at(C.sparks, i, scale(mix(C.p.lane[LANE_CLAP], (t3_color){ 1, 1, 0.9f }, 0.3f), b));
  }
  changed(C.sparks);

  /* the bass: a waveform of columns along the bottom, under the gates, thick while the bass plays, thin while it is
   * missed out */
  C.bass_amp += (v->bass * v->level[LANE_BASS] - C.bass_amp) * 0.25f;
  float wspan = VIEW_HALF_H * 16.0f / 9.0f * 2 * 0.96f;
  for (int i = 0; i < WAVE_COLS; i++) {
    float x = -wspan / 2 + wspan * ((float)i + 0.5f) / WAVE_COLS, ph = beat * 3.14159f;
    float w = 0.5f + 0.5f * sinf(x * 0.55f - ph * 2) * cosf(x * 0.21f + ph * 0.5f);
    float h = 0.15f + (0.2f + 1.5f * C.bass_amp) * w * (0.35f + 0.65f * v->level[LANE_BASS]);
    set_inst(C.wave_cols, i, (t3_vec3){ x, -VIEW_HALF_H + 0.35f + h * 0.5f, 0 }, 0, (t3_vec3){ 1, h, 1 });
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
      float d = sqrtf((px - wo.x) * (px - wo.x) + (py - wo.y) * (py - wo.y)), lift = 0;
      for (int w = 0; w < WAVES; w++) {
        float age = (now - C.wave_t[w]) / 900.0f;
        if (age < 0 || age > 1) continue;
        float r = 1.2f + 17 * (1 - (1 - age) * (1 - age));
        lift += expf(-(d - r) * (d - r) * 1.5f) * (1 - age) * 0.5f;
      }
      float b = (on * (0.18f + 0.3f * v->streak) + lift * (0.4f + 0.6f * on)) * dim;
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
void club_set_place(const int place[LANES]) {
  for (int l = 0; l < LANES; l++) C.place[l] = place[l] >= 0 && place[l] < LANES ? place[l] : l;
}
int club_place(int lane) { return C.place[lane]; }
void club_gate_screen(int lane, float cw, float ch, float *x, float *y) {
  if (!C.built) { *x = cw * 0.5f, *y = ch * 0.8f; return; }
  to_canvas(on_river(lane, 0, 0, 0), cw, ch, x, y);
}
void club_center_screen(float cw, float ch, float *x, float *y, float *half_w) {
  to_canvas((t3_vec3){ 0, (GATE_Y + TOP_Y) * 0.5f, 0 }, cw, ch, x, y);
  *half_w = (LANE_DX * C.spread + RIVER_W * 0.5f) * (ch * 0.5f) / VIEW_HALF_H * (C.zoom > 0 ? C.zoom : 1);
}
void club_lane_rgb(int lane, float rgb[3]) {
  t3_color c = C.built && lane >= 0 && lane < LANES ? C.p.lane[lane] : (t3_color){ 1, 1, 1 };
  float m = fmaxf(c.r, fmaxf(c.g, c.b));
  if (m <= 0) m = 1;
  /* a little lighter than the river, so the HUD's text reads on its box */
  rgb[0] = 0.25f + 0.75f * c.r / m, rgb[1] = 0.25f + 0.75f * c.g / m, rgb[2] = 0.25f + 0.75f * c.b / m;
}
