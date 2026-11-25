/* tale.c: Ember Throne's story, voices and cinematics, on the engine's modules (cartwheel/story.h, speech.h,
 * director.h). The script is story/ember.story (compiled to assets/story/ember.cwstory, voiced into
 * assets/story/ember.cwspeech by tools/story.sh).
 *
 * What it does:
 *   - the scenes: the guide's welcome at the first start, each warden's intro when it wakes, the finisher shots and
 *     the choice over each beaten warden (spare or kill: which wardens can be called in the King's fight), the gate when
 *     both keys open it, the King's intro, his defeat and the choice of the ending, the ending. A scene holds the rules
 *     (tale_blocking); it plays on the pad (subtitles, choices, East held to skip);
 *   - the barks: each warden's phase, spent, winning and losing lines in its fight, the guide when you fall, the
 *     keepers when you rest (once each, keyed to deaths at each warden, the keys, a second player);
 *   - the people: the guide and a keeper at each shrine, figures of the cast, faces that speak (cw_face) on every
 *     speaker that has a face;
 *   - the camera: the director's shots in scenes, a react shot at a warden's phase change, an insert and hero on the
 *     special and the killing blow. It never shakes (the pad rumbles, fx.c).
 * The autopilot skips scenes at once (their defaults: both wardens spared) unless ADV_STORY=1 lets them play. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/anim.h"
#include "cartwheel/debug.h"
#include "cartwheel/director.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "cartwheel/shell.h"
#include "cartwheel/speech.h"
#include "cartwheel/story.h"
#include "cartwheel/ui.h"
#include "game.h"
#include "tale.h"

/* the cast, in the script's order (story/ember.story) */
enum { C_WANDERER, C_GUIDE, C_THORN, C_TIDE, C_KING, C_KEEPER, C_KEEPER2, C_N };
static const char *const CAST_ID[C_N] = { "wanderer", "guide", "thorn", "tide", "king", "keeper", "keeper2" };
/* the director's subjects: the cast's (by cast index), then a second player and the foes near the lead player */
#define SUBJ_P2 C_N
#define SUBJ_FOES (C_N + 1)

typedef struct npc { cw_node node; t3_object *obj; cw_face *face; float x, y, z, yaw; int cast; bool shown; } npc;
#define MAXNPC 12

static struct {
  cw_story *st;
  cw_speech *sp;
  cw_director *dir;
  int cast[C_N];               /* the script's index of each */
  npc npc[MAXNPC];
  int nnpc, guide;             /* the guide's npc index */
  bool guide_set; float guide_x, guide_z;
  cw_face *face_actor[MAXA];   /* faces on the rigs (made when a rig's model appears) */
  t3_object *face_inst[MAXA];
  bool play_scenes;            /* the autopilot skips scenes unless ADV_STORY=1 */
  bool built, ending_done;
  int finisher_t;              /* ticks of the finisher shots left (the rules wait) */
  int finisher_kind, special_t, react_t;
  float hp_frac_prev;
  int fall_seen, lines_said;
  int32_t dbg_scene_ticks, dbg_finishers, dbg_reacts, dbg_ending, mouth_ok;
  uint32_t bark_key;
  float jaw_max;
  cw_dir_camera cam;
  bool cam_on;
} T;

static int cast_actor(int c) {
  if (c == C_WANDERER) return 0;
  int kind = c == C_THORN ? K_THORN : c == C_TIDE ? K_TIDE : c == C_KING ? K_EMBER : -1;
  if (kind < 0) return -1;
  for (int i = MAXP; i < MAXA; i++) if (S.a[i].used && S.a[i].kind == kind) return i;
  return -1;
}
/* the npc speaking for a keeper: the nearest shown one */
static int cast_npc(int c) {
  if (c == C_GUIDE) return T.guide;
  if (c != C_KEEPER && c != C_KEEPER2) return -1;
  int best = -1; float bd = 1e30f;
  t3_vec3 p = S.a[0].mv.position;
  for (int k = 0; k < T.nnpc; k++) {
    if (T.npc[k].cast != C_KEEPER && T.npc[k].cast != C_KEEPER2) continue;
    float dx = T.npc[k].x - p.x, dz = T.npc[k].z - p.z, d = dx * dx + dz * dz;
    if (d < bd) bd = d, best = k;
  }
  return best;
}

/* ── the people at the shrines ── */
static void tale_tint(t3_object *o, t3_color tint);
static void npc_add(cw_node root, const char *model, int cast, float x, float z, float yaw, float scale, t3_color tint) {
  if (T.nnpc >= MAXNPC) return;
  cw_res r = cw_res_load(model, CW_RES_GLTF);
  const cw_gltf *g = cw_res_gltf(r);
  if (!g) { cw_error("tale: %s did not load", model); return; }
  npc *n = &T.npc[T.nnpc];
  char nm[24];
  snprintf(nm, sizeof nm, "Npc%d", T.nnpc);
  n->node = cw_node_new("Node3D", nm);
  cw_node_add_child(root, n->node);
  n->x = x, n->z = z, n->yaw = yaw, n->cast = cast;
  n->y = world_ground(x, z, world_height(x, z) + 3);
  cw_node3d_set_position(n->node, (t3_vec3){ x, n->y, z });
  cw_node3d_set_rotation(n->node, (t3_vec3){ 0, yaw, 0 });
  cw_node3d_set_scale(n->node, (t3_vec3){ scale, scale, scale });
  t3_object *o = cw_gltf_instantiate(r);
  /* a keeper's robes in their own colour (cloned materials: the player's figure keeps its own) */
  if (tint.r != 1 || tint.g != 1 || tint.b != 1) tale_tint(o, tint);
  cw_node3d_set_object(n->node, o);
  t3_release(o);
  n->obj = o;
  cw_node anim = cw_node_new("AnimationPlayer", "Anim");
  cw_node_add_child(n->node, anim);
  for (int k = 0; k < g->gltf->animation_count; k++) {
    cw_anim a = cw_anim_from_t3_clip(g->gltf->animations[k], "");
    cw_anim_player_add(anim, g->gltf->animations[k]->name, a);
    cw_anim_release(a);
  }
  cw_anim_player_play_ex(anim, "idle", 0, 0.9f + 0.05f * (float)T.nnpc, false);
  n->face = cw_face_new(o, NULL);
  cw_face_set_blinking(n->face, true, (uint32_t)T.nnpc + 11);
  T.nnpc++;
}
static void tint_mat(t3_object *o, void *u) {
  if (o->type != T3_MESH && o->type != T3_SKINNED_MESH) return;
  const t3_color *c = u;
  t3_mesh *m = (t3_mesh *)o;
  for (int i = 0; i < m->material_count; i++) {
    t3_material *mt = m->materials[i];
    if (!mt || !mt->name || strncmp(mt->name, "outfit", 6)) continue; /* the cloth only: skin, hair and eyes stay */
    t3_material *cl = t3_material_clone(mt);
    cl->color.r *= c->r, cl->color.g *= c->g, cl->color.b *= c->b;
    m->materials[i] = cl;
  }
}
static void tale_tint(t3_object *o, t3_color tint) { t3_object_traverse(o, tint_mat, &tint); }

static void npcs_build(cw_node root) {
  /* a keeper beside every shrine, on the side away from where the wanderer wakes, facing the shrine's approach;
   * old and young keepers in turn */
  for (int s = 0; s < 8; s++) {
    int k = world_shrine_obj(s);
    if (k < 0) break;
    const wobj *o = &g_obj[k];
    float side = cw_cos(o->yaw), fwd = -cw_sin(o->yaw);
    float x = o->x + side * 1.9f, z = o->z + fwd * 1.9f;
    npc_add(root, "models/warden.glb", s & 1 ? C_KEEPER2 : C_KEEPER, x, z, o->yaw + CW_PI * 0.75f, s & 1 ? 0.97f : 1.0f,
            s & 1 ? (t3_color){ 0.55f, 0.75f, 0.62f } : (t3_color){ 0.75f, 0.62f, 0.45f });
  }
  /* the guide: hooded, at the first shrine, on its other side */
  int k = world_shrine_obj(0);
  if (k >= 0) {
    const wobj *o = &g_obj[k];
    float side = cw_cos(o->yaw), fwd = -cw_sin(o->yaw);
    T.guide = T.nnpc;
    npc_add(root, "models/guide.glb", C_GUIDE, o->x - side * 2.2f, o->z - fwd * 1.2f, o->yaw + CW_PI * 1.2f, 1.0f, (t3_color){ 0.62f, 0.7f, 0.85f });
  }
}

/* the guide stands at the keep's gate once both keys are found */
static void guide_place(void) {
  if (T.guide < 0 || T.guide >= T.nnpc) return;
  npc *n = &T.npc[T.guide];
  bool at_gate = (S.sv.keys & 3) == 3;
  float x = n->x, z = n->z;
  if (at_gate) {
    for (int k = 0; k < g_nobj; k++)
      if (g_obj[k].kind == OB_GATE) { x = g_obj[k].x + 4.2f, z = g_obj[k].z + 3.0f; break; }
  } else if (T.guide_set) x = T.guide_x, z = T.guide_z; /* where the wanderer first woke: a few steps ahead of them */
  if (x != n->x || z != n->z) {
    n->x = x, n->z = z, n->y = world_ground(x, z, world_height(x, z) + 3);
    cw_node3d_set_position(n->node, (t3_vec3){ x, n->y, z });
  }
}

/* ── hooks ── */
/* back to the game's camera: a cut, as a film cuts back to the action */
static void back_to_play(void) { if (T.dir) cw_director_stop(T.dir); }
static int map_subject[C_N];
static void on_shot(const cw_story_shot *s, void *u) {
  (void)u;
  if (!T.dir) return;
  if (s->type == CW_SHOT_RELEASE) { back_to_play(); return; }
  cw_director_cut_story(T.dir, s, map_subject);
}
static void on_emit(const char *name, int arg, void *u) {
  (void)u, (void)arg;
  if (!strcmp(name, "ending")) T.dbg_ending = cw_story_flag(T.st, "ending_frost") ? 2 : 1;
}
static void on_scene_end(const char *scene, bool skipped, void *u) {
  (void)skipped, (void)u;
  if (T.dir) back_to_play();
  if (!strcmp(scene, "king_defeat")) cw_story_play(T.st, cw_story_flag(T.st, "ending_frost") ? "ending_frost" : "ending_keeper");
  if (!strncmp(scene, "ending_", 7)) T.ending_done = true;
  if (!T.play_scenes && cw_story_active(T.st)) cw_story_skip_scene(T.st);
}

/* the world for the director: the camera's own collision (the world and the trees' crowns), and the terrain under it */
static bool world_ray(t3_vec3 a, t3_vec3 b, float *t, void *u) {
  (void)u;
  cw_phys_filter f = cw_phys_filter_layers(1u | 8u);
  cw_ray_hit3d h;
  t3_vec3 d = { b.x - a.x, b.y - a.y, b.z - a.z };
  float best = 2;
  if (cw_world3d_raycast(world_phys(), a, d, &f, &h) && h.fraction < 1) best = h.fraction;
  /* the ground: a few samples along the segment */
  for (int k = 1; k <= 8; k++) {
    float q = (float)k / 8, x = a.x + d.x * q, y = a.y + d.y * q, z = a.z + d.z * q;
    if (y < world_height(x, z) + 0.05f) { if (q < best) best = q; break; }
  }
  if (best > 1) return false;
  *t = best;
  return true;
}
static char PENDING[32];
static void play(const char *scene) {
  if (!T.st || S.ladder >= 0 || cw_story_seen(T.st, scene) || cw_story_active(T.st)) return;
  if (!T.play_scenes) { cw_story_play(T.st, scene); cw_story_skip_scene(T.st); return; } /* the autopilot: the defaults at once */
  snprintf(PENDING, sizeof PENDING, "%s", scene); /* the next tick, with everyone placed and facing (tale_tick) */
}
static void bark(const char *pool) { if (T.st) cw_story_bark(T.st, pool); }
static const char *boss_key(int kind) { return kind == K_THORN ? "thorn" : kind == K_TIDE ? "tide" : kind == K_EMBER ? "king" : NULL; }

/* the tests' requests: tale scene <name> | flag <name> <0|1> | stat <name> <n> | bark <pool> (state set now; a scene
 * plays over the next ticks) */
static void cmd_tale(int argc, const char **argv, cw_reply *r) {
  if (!T.st || argc < 3) { cw_reply_error(r, "usage: tale scene <name> | flag <name> <0|1> | stat <name> <n> | bark <pool>"); return; }
  if (!strcmp(argv[1], "scene")) { bool ok = cw_story_play(T.st, argv[2]); if (ok && !T.play_scenes) cw_story_skip_scene(T.st); cw_reply_printf(r, "{\"scene\":%d}", ok ? 1 : 0); }
  else if (!strcmp(argv[1], "flag") && argc > 3) { cw_story_set_flag(T.st, argv[2], atoi(argv[3]) != 0); cw_reply_printf(r, "{\"flag\":%d}", cw_story_flag(T.st, argv[2]) ? 1 : 0); }
  else if (!strcmp(argv[1], "stat") && argc > 3) { cw_story_set_stat(T.st, argv[2], atoi(argv[3])); cw_reply_printf(r, "{\"stat\":%d}", cw_story_stat(T.st, argv[2])); }
  else if (!strcmp(argv[1], "bark")) { const cw_story_line *l = cw_story_bark(T.st, argv[2]); cw_reply_printf(r, "{\"bark\":%u}", l ? l->key : 0u); }
  else cw_reply_error(r, "tale: unknown request %s", argv[1]);
}
void tale_init(void) {
  T.guide = -1;
  for (int c = 0; c < C_N; c++) map_subject[c] = c;
  T.sp = cw_speech_load("story/ember.cwspeech");
  T.st = cw_story_new(&(cw_story_desc){ .path = "story/ember.cwstory", .speech = T.sp, .any_player = true, .on_shot = on_shot,
                                        .on_emit = on_emit, .on_scene_end = on_scene_end });
  if (!T.st) return;
  for (int c = 0; c < C_N; c++) {
    T.cast[c] = cw_story_cast_find(T.st, CAST_ID[c]);
    if (T.cast[c] != c) cw_error("tale: the script's cast order is not tale.c's (%s at %d)", CAST_ID[c], T.cast[c]);
  }
  T.dir = cw_director_new(&(cw_director_desc){ .aspect = 16.f / 9, .ease = 5, .ray = world_ray });
  cw_story_publish(T.st, "story");
  cw_speech_publish(T.sp, "speech");
  cw_director_publish(T.dir, "dir");
  cw_debug_i32("tale.scene_ticks", &T.dbg_scene_ticks);
  cw_debug_i32("tale.finishers", &T.dbg_finishers);
  cw_debug_i32("tale.reacts", &T.dbg_reacts);
  cw_debug_i32("tale.ending", &T.dbg_ending);
  cw_debug_u32("tale.bark_key", &T.bark_key);         /* the last aside or bark said */
  cw_debug_f32("tale.jaw_max", &T.jaw_max);            /* the widest any speaking face opened its jaw */
  cw_debug_i32("tale.mouth_ok", &T.mouth_ok);          /* a face has spoken (jaw past 0.3) */
  cw_debug_command("tale", "tale scene <name> | flag <name> <0|1> | stat <name> <n> | bark <pool>: the story, for the tests", cmd_tale);
#ifdef ADV_AUTOPILOT
  T.play_scenes = false;
#else
  T.play_scenes = true;
#endif
}
void tale_build(cw_node root) {
  if (T.built) return;
  T.built = true;
  npcs_build(root);
}
void tale_settings(cw_shell_setting rows[2]) { cw_speech_settings(rows); }

/* a new journey, or one loaded: the story's state from the save (or fresh), the welcome when it is new */
void tale_start(bool fresh) {
  if (!T.st) return;
  cw_story_reset(T.st);
  if (!fresh && !cw_story_load(T.st, S.sv.story, sizeof S.sv.story)) cw_story_reset(T.st);
  T.ending_done = false, T.finisher_t = 0, T.dbg_ending = 0;
  if (T.dir) cw_director_stop(T.dir);
  if (!T.guide_set) { /* the guide waits a few steps to the side of where the wanderer first wakes (the shrine is ahead of her) */
    const actor *p = &S.a[0];
    float fx = cw_sin(p->yaw), fz = cw_cos(p->yaw);
    T.guide_x = p->mv.position.x + fx * 0.8f - fz * 2.6f, T.guide_z = p->mv.position.z + fz * 0.8f + fx * 2.6f; /* beside her, not at the shrine ahead */
    T.guide_set = true;
  }
  if (!cw_story_seen(T.st, "intro")) play("intro");
}
void tale_save(void) {
  if (!T.st || S.ladder >= 0) return;
  size_t n = cw_story_save(T.st, S.sv.story, sizeof S.sv.story);
  if (n > sizeof S.sv.story) cw_error("tale: the story's save needs %zu bytes (save_data.story holds %zu)", n, sizeof S.sv.story);
}

bool tale_blocking(void) { return T.st && (cw_story_active(T.st) || T.finisher_t > 0 || PENDING[0]); }

/* the rules' events of this tick: scenes, barks, shots */
void tale_events(void) {
  if (!T.st) return;
  for (int k = 0; k < S.ev_n; k++) {
    const event *e = &S.ev[k];
    const actor *a = e->a >= 0 && e->a < MAXA ? &S.a[e->a] : NULL;
    switch (e->kind) {
    case EV_BOSS:
      if (!a || !boss_key(a->kind)) break;
      if (e->b == 1) { char sc[32]; snprintf(sc, sizeof sc, "%s_intro", boss_key(a->kind)); T.hp_frac_prev = 1; play(sc); }
      if (e->b == 2) { /* the killing blow: an insert on the warden, then the pull back as it sinks, then its scene */
        T.finisher_kind = a->kind, T.finisher_t = S.ladder >= 0 ? 0 : 100;
        T.dbg_finishers++;
        if (T.dir && S.ladder < 0) {
          cw_director_cut(T.dir, &(cw_shot){ .type = CW_SHOT_INSERT, .subj = { a->kind == K_THORN ? C_THORN : a->kind == K_TIDE ? C_TIDE : C_KING }, .nsubj = 1, .part = CW_SHOT_PART_HEAD });
        }
      }
      break;
    case EV_ROAR: { /* a phase: its line, and a react shot under a second */
      if (!a || !boss_key(a->kind)) break;
      char p[32]; snprintf(p, sizeof p, "%s.phase", boss_key(a->kind));
      bark(p);
      if (T.dir && !cw_story_active(T.st)) {
        int c = a->kind == K_THORN ? C_THORN : a->kind == K_TIDE ? C_TIDE : C_KING;
        cw_director_cut(T.dir, &(cw_shot){ .type = CW_SHOT_REACT, .subj = { c, C_WANDERER }, .nsubj = 2 });
        T.react_t = 52, T.dbg_reacts++;
      }
      break;
    }
    case EV_LAND: if (a && boss_key(a->kind)) { char p[32]; snprintf(p, sizeof p, "%s.spent", boss_key(a->kind)); bark(p); } break;
    case EV_FALL:
      if (e->a < MAXP) {
        cw_story_add_stat(T.st, "deaths", 1);
        if (S.boss >= 0) { char st[32]; snprintf(st, sizeof st, "deaths_%s", boss_key(S.a[S.boss].kind) ? boss_key(S.a[S.boss].kind) : "thorn"); cw_story_add_stat(T.st, st, 1); }
        bark("fall");
      }
      break;
    case EV_REST:
      if (!cw_story_bark(T.st, "keeper.rest")) bark("keeper.idle");
      tale_save();
      break;
    case EV_GATE:
      if (e->b >= 0 && e->b < g_nobj && g_obj[e->b].kind == OB_GATE) play("gate");
      break;
    case EV_ASSIST:
      if (e->v > 0) { bark(e->b == K_THORN ? "assist.thorn" : "assist.tide");
        if (T.dir && !cw_story_active(T.st)) { cw_director_cut(T.dir, &(cw_shot){ .type = CW_SHOT_HERO, .subj = { e->b == K_THORN ? C_THORN : C_TIDE }, .nsubj = 1 }); T.react_t = 70; } }
      break;
    case EV_SPECIAL: /* the ember strike: an insert on the blade hand, then a hero angle as it lands */
      if (T.dir && !cw_story_active(T.st)) {
        cw_director_cut(T.dir, &(cw_shot){ .type = CW_SHOT_INSERT, .subj = { C_WANDERER }, .nsubj = 1, .part = CW_SHOT_PART_HAND_R });
        T.special_t = 70;
      }
      break;
    default: break;
    }
  }
}

/* the director's subjects from the figures as drawn */
static void subjects(void) {
  if (!T.dir) return;
  for (int c = 0; c < C_N; c++) {
    cw_dir_subject s = { 0 };
    int ai = cast_actor(c), ni = ai < 0 ? cast_npc(c) : -1;
    t3_vec3 b[5]; float yaw;
    if (ai >= 0 && S.a[ai].active && view_subject(ai, b, &yaw)) {
      float sc = KIND[S.a[ai].kind].height / 1.8f;
      s = (cw_dir_subject){ .eye = b[0], .chest = b[1], .feet = b[2], .face = { cw_sin(yaw), 0, cw_cos(yaw) }, .hand_l = b[3], .hand_r = b[4],
                            .radius = 0.25f * sc, .head_radius = 0.14f * sc, .visible = true };
    } else if (ni >= 0) {
      npc *n = &T.npc[ni];
      t3_vec3 head = { n->x, n->y + 1.62f, n->z };
      t3_object *hb = n->obj ? t3_object_get_by_name(n->obj, "Head") : NULL;
      if (hb) { t3_object_update_matrix_world(hb, false); head = t3_object_get_world_position(hb); head.y += 0.07f; }
      s = (cw_dir_subject){ .eye = { head.x + cw_sin(n->yaw) * 0.08f, head.y, head.z + cw_cos(n->yaw) * 0.08f }, .chest = { n->x, n->y + 1.3f, n->z },
                            .feet = { n->x, n->y, n->z }, .face = { cw_sin(n->yaw), 0, cw_cos(n->yaw) }, .radius = 0.25f, .head_radius = 0.14f, .visible = n->shown };
    } else { cw_director_set_subject(T.dir, c, NULL); continue; }
    cw_director_set_subject(T.dir, c, &s);
  }
  /* a second player and the nearest foes are in the way, never framed */
  int si = SUBJ_P2;
  t3_vec3 b[5]; float yaw;
  if (S.a[1].used && view_subject(1, b, &yaw))
    cw_director_set_subject(T.dir, si, &(cw_dir_subject){ .eye = b[0], .chest = b[1], .feet = b[2], .face = { cw_sin(yaw), 0, cw_cos(yaw) }, .visible = true });
  else cw_director_set_subject(T.dir, si, NULL);
  int n = SUBJ_FOES;
  for (int i = MAXP; i < MAXA && n < CW_DIR_MAX_SUBJECTS; i++) {
    const actor *a = &S.a[i];
    if (!a->used || !a->active || a->boss || a->state == ST_DEAD) continue;
    t3_vec3 p = a->mv.position, l = S.a[0].mv.position;
    if ((p.x - l.x) * (p.x - l.x) + (p.z - l.z) * (p.z - l.z) > 15 * 15 || !view_subject(i, b, &yaw)) continue;
    cw_director_set_subject(T.dir, n++, &(cw_dir_subject){ .eye = b[0], .chest = b[1], .feet = b[2], .face = { cw_sin(yaw), 0, cw_cos(yaw) }, .visible = true });
  }
  for (; n < CW_DIR_MAX_SUBJECTS; n++) cw_director_set_subject(T.dir, n, NULL);
}

/* the faces: one per figure that has a model now (a rig made again gets a new face) */
static cw_face *actor_face(int i) {
  t3_object *o = view_inst(i);
  if (o != T.face_inst[i]) {
    cw_face_free(T.face_actor[i]);
    T.face_actor[i] = o ? cw_face_new(o, NULL) : NULL;
    T.face_inst[i] = o;
    if (T.face_actor[i]) cw_face_set_blinking(T.face_actor[i], true, (uint32_t)i + 1);
  }
  return T.face_actor[i];
}

/* every tick, after the rules and view_fixed */
void tale_tick(float dt) {
  if (!T.st) return;
  /* the tests' switches reach a cart host at frame 2: ADV_STORY=1 lets the autopilot's scenes play (lines on their
   * own time, choices on their defaults); ADV_DIR_NOISE=1 shakes the director (a gate's control) */
  static bool env_read;
  if (!env_read && cw_frame_index() >= 2) {
    env_read = true;
    const char *e = cw_test_env("ADV_STORY");
    if (e) T.play_scenes = atoi(e) != 0;
    if (cw_test_env("ADV_DIR_NOISE")) {
      cw_director_free(T.dir);
      T.dir = cw_director_new(&(cw_director_desc){ .aspect = 16.f / 9, .ease = 5, .ray = world_ray, .test_noise = 0.01f });
      cw_director_publish(T.dir, "dir");
    }
  }
  if (cw_story_active(T.st)) T.dbg_scene_ticks++;
  /* the people near the lead player are shown (the rest cost nothing) */
  t3_vec3 l = S.a[0].mv.position;
  guide_place();
  for (int k = 0; k < T.nnpc; k++) {
    npc *n = &T.npc[k];
    bool want = (n->x - l.x) * (n->x - l.x) + (n->z - l.z) * (n->z - l.z) < 70 * 70;
    if (k == T.guide && cw_story_flag(T.st, "gate_open") && !cw_story_active(T.st)) want = false; /* gone into the keep ahead of you */
    if (want != n->shown) { n->shown = want; cw_node_set(n->node, "visible", cw_val_bool(want)); }
    if (want) { /* who is near is faced (turned slowly, as a person turns to someone walking up) */
      float dx = l.x - n->x, dz = l.z - n->z;
      if (dx * dx + dz * dz < 12 * 12) {
        float to = cw_atan2(dx, dz), d = cw_wrap_angle(to - n->yaw);
        n->yaw += d * (1 - expf(-3.0f * dt));
        cw_node3d_set_rotation(n->node, (t3_vec3){ 0, n->yaw, 0 });
      }
    }
  }
  /* the spared wardens can be called in the King's fight; a second player is in the story */
  S.assist_mask = (cw_story_flag(T.st, "spared_thorn") ? 1u : 0) | (cw_story_flag(T.st, "spared_tide") ? 2u : 0);
  if (S.a[1].used && !cw_story_flag(T.st, "duo")) cw_story_set_flag(T.st, "duo", true);
  /* the fights' barks on the run of the fight: a warden near its end, the wanderer near theirs */
  if (S.boss >= 0 && S.ladder < 0) {
    const actor *b = &S.a[S.boss];
    const char *bk = boss_key(b->kind);
    float f = (float)b->hp / (float)b->hp_max;
    char p[32];
    if (bk && f < 0.25f && T.hp_frac_prev >= 0.25f) { snprintf(p, sizeof p, "%s.losing", bk); bark(p); }
    if (bk && S.a[0].hp < S.a[0].hp_max / 4 && S.tick % 300 == 0) { snprintf(p, sizeof p, "%s.winning", bk); bark(p); }
    T.hp_frac_prev = f;
  }
  /* the finisher: after its shots, the beaten warden's scene */
  if (T.finisher_t > 0 && --T.finisher_t == 0) {
    char sc[32];
    snprintf(sc, sizeof sc, "%s_defeat", boss_key(T.finisher_kind) ? boss_key(T.finisher_kind) : "thorn");
    play(sc);
    if (!cw_story_active(T.st) && T.dir) back_to_play();
  } else if (T.finisher_t == 50 && T.dir) { /* the pull back: a wide on the warden and the wanderer */
    int c = T.finisher_kind == K_THORN ? C_THORN : T.finisher_kind == K_TIDE ? C_TIDE : C_KING;
    cw_director_cut(T.dir, &(cw_shot){ .type = CW_SHOT_TWO, .subj = { c, C_WANDERER }, .nsubj = 2, .dist = 3.5f });
  }
  if (T.special_t > 0 && --T.special_t == 40 && T.dir) cw_director_cut(T.dir, &(cw_shot){ .type = CW_SHOT_HERO, .subj = { C_WANDERER }, .nsubj = 1 });
  else if (T.special_t == 1 && T.dir && !cw_story_active(T.st)) back_to_play();
  if (T.react_t > 0 && --T.react_t == 0 && T.dir && !cw_story_active(T.st) && !T.finisher_t) back_to_play();
  cw_story_tick(T.st, dt);
  cw_speech_tick(T.sp, dt);
  /* the faces of everyone in the cast who has one */
  cw_face *faces[C_N] = { 0 };
  for (int c = 0; c < C_N; c++) {
    int ai = cast_actor(c);
    if (ai >= 0) faces[c] = actor_face(ai);
    else { int ni = cast_npc(c); if (ni >= 0 && T.npc[ni].shown) faces[c] = T.npc[ni].face; }
  }
  cw_speech_drive_faces(T.st, T.sp, faces, C_N, dt, true);
  for (int c = 0; c < C_N; c++) if (faces[c]) { float j = cw_face_weight(faces[c], CW_FACE_JAW_OPEN); if (j > T.jaw_max) T.jaw_max = j; }
  T.mouth_ok = T.jaw_max > 0.3f;
  { const cw_story_line *a = cw_story_aside(T.st); if (a) T.bark_key = a->key; }
  /* the camera */
  subjects();
  if (PENDING[0]) { /* a scene waiting: the people near turn to the wanderer, then it starts with them where they stand */
    for (int k = 0; k < T.nnpc; k++) {
      npc *n = &T.npc[k];
      float dx = l.x - n->x, dz = l.z - n->z;
      if (n->shown && dx * dx + dz * dz < 12 * 12) { n->yaw = cw_atan2(dx, dz); cw_node3d_set_rotation(n->node, (t3_vec3){ 0, n->yaw, 0 }); }
    }
    subjects();
    cw_story_play(T.st, PENDING);
    PENDING[0] = 0;
  }
  cw_director_set_gameplay(T.dir, &(cw_dir_camera){ S.cam_pos, S.cam_target, 60 });
  T.cam_on = cw_director_update(T.dir, dt, &T.cam);
  { /* the log names each frame a measure counts (what to look at when a gate fails) */
    static float last[4];
    float m[4] = { cw_director_measure(T.dir, CW_DIR_M_BLOCKED), cw_director_measure(T.dir, CW_DIR_M_AWAY), cw_director_measure(T.dir, CW_DIR_M_OUT), cw_director_measure(T.dir, CW_DIR_M_REVERSALS) };
    static const char *const NAME[4] = { "blocked", "away", "out", "reversal" };
    for (int k = 0; k < 4; k++) if (m[k] > last[k]) { cw_info("tale: director %s at tick %u (shot %d, scene %s)", NAME[k], S.tick, cw_director_solved_type(T.dir), cw_story_scene(T.st) ? cw_story_scene(T.st) : "-"); last[k] = m[k]; }
  }
}

bool tale_camera(cw_dir_camera *cam) { if (!T.cam_on) return false; *cam = T.cam; return true; }
bool tale_ended(void) { return T.ending_done; }
int tale_ending(void) { return T.st ? (cw_story_flag(T.st, "ending_frost") ? 2 : cw_story_flag(T.st, "ending_keeper") ? 1 : 0) : 0; }
float tale_music_duck(void) { return T.sp ? cw_speech_duck(T.sp) : 1; }
bool tale_flag(const char *f) { return T.st && cw_story_flag(T.st, f); }

void tale_draw(cw_font font) {
  if (!T.st) return;
  cw_story_draw(T.st, font, (cw_rect){ 0 });
}
