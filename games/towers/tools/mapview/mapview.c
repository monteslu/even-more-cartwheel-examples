/* towers_mapview: a map as the game builds it (mapbuild.c), for review (in romdev, as a cart) and for the native
 * bakes of every map's lightmap.
 *
 * Input (the game's actions): left / right the previous or next map, up / down the camera (the game's view, low
 * over the core), the south button the next theme.
 *
 * Native, development: --map <n> starts there; --bake <dir> bakes every map (or --only a,b,c, or --map n alone) into
 * <dir>/<nnn>.cwlm with <nnn>.hash beside it, prints the time and the size of each, and exits. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/project.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/vfs.h"
#include "game.h"
#include "mapbuild.h"

static struct {
  int map, nmaps, view, frames;
  bool built, baking, lm_done;
  tdmap m;
  char *text;
  mb_built b;
  cw_node root, camera;
  cw_action left, right, up, down, next;
  const char *bake_dir, *only;
  int want;
  double t0;
  size_t bytes;
  int baked;
} V;

static void unload(void) {
  if (V.built) mapbuild_free(&V.b);
  V.built = false;
  free(V.text), V.text = NULL;
}

static bool wanted(int n) {
  if (V.want && !V.only) return n == V.want;
  if (!V.only) return true;
  for (const char *p = V.only; *p;) {
    if (atoi(p) == n) return true;
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
  }
  return false;
}

static void load(int n) {
  unload();
  V.map = ((n - 1 + V.nmaps) % V.nmaps) + 1;
  char path[64];
  map_path(V.map, path, sizeof path);
  size_t len = 0;
  char *buf = (char *)cw_vfs_read(path, &len);
  if (!buf) { cw_error("mapview: cannot read %s", path); return; }
  V.text = malloc(len + 1);
  memcpy(V.text, buf, len), V.text[len] = 0;
  free(buf);
  char err[160];
  if (!map_parse(V.text, &V.m, sim_kind_by_name, err, sizeof err)) { cw_error("mapview: %s: %s", path, err); return; }
  if (!mapbuild_build(V.root, &V.m, &V.b)) return;
  V.built = true, V.lm_done = false, V.frames = 0;
  cw_info("mapview: map %d (%s, %dx%d): %d props, %d lights", V.map, MP_THEME_ID[V.m.theme], V.m.w, V.m.h, V.m.nprop, V.m.nlight);
}

static int next_wanted(int from) {
  for (int n = from; n <= V.nmaps; n++)
    if (wanted(n)) return n;
  return 0;
}

static void init(void *u) {
  (void)u;
  V.root = cw_tree_root(cw_scene_app_tree());
  V.camera = cw_node_get(V.root, "View/Camera");
  cw_project_load_input();
  V.left = cw_action_find("move_left"), V.right = cw_action_find("move_right");
  V.up = cw_action_find("move_up"), V.down = cw_action_find("move_down");
  V.next = cw_action_find("act_s");
  V.nmaps = map_count();
  if (V.nmaps <= 0) V.nmaps = 1;
#if !CW_CART
  V.baking = V.bake_dir && *V.bake_dir;
  if (V.baking) {
    cw_info("mapview: baking into %s", V.bake_dir);
    V.t0 = cw_time();
    int n = next_wanted(1);
    if (!n) exit(0);
    load(n);
    return;
  }
#endif
  load(V.want ? V.want : 1);
}

static void place_camera(void) {
  /* 0: the game's camera (the whole map, tilted 15 degrees), 1: low over the core */
  float th = tanf(7.0f * 3.14159265f / 180.0f), d = (float)V.m.h * 1.1f / th;
  if (V.view == 0) {
    cw_node3d_set_position(V.camera, (t3_vec3){ 0, cw_cos(0.2618f) * d, 0.5f + cw_sin(0.2618f) * d }), cw_node_set(V.camera, "fov", cw_val_float(14));
    cw_node3d_look_at(V.camera, (t3_vec3){ 0, 0, 0.5f }, (t3_vec3){ 0, 1, 0 });
  } else {
    float cx = mp_wx(&V.m, V.m.core[0]), cz = mp_wz(&V.m, V.m.core[1]);
    cw_node3d_set_position(V.camera, (t3_vec3){ cx, 9.0f, cz + 16.0f }), cw_node_set(V.camera, "fov", cw_val_float(50));
    cw_node3d_look_at(V.camera, (t3_vec3){ cx, 0.5f, cz }, (t3_vec3){ 0, 1, 0 });
  }
}

static void update(void *u, double dt) {
  (void)u, (void)dt;
#if !CW_CART
  if (!V.built && V.baking) {
    cw_warn("mapview: map %d did not load, not baked", V.map);
    int n = next_wanted(V.map + 1);
    if (!n) exit(1);
    load(n);
    return;
  }
#endif
  if (!V.built) {
    if (cw_action_just_pressed(0, V.right)) load(V.map + 1);
    else if (cw_action_just_pressed(0, V.left)) load(V.map - 1);
    return;
  }
  V.frames++;
  place_camera();
  if (!V.lm_done && V.frames > 1) { /* after a frame: the light scene has placed its sun */
    char out[512] = { 0 };
    if (V.baking) snprintf(out, sizeof out, "%s/%03d.cwlm", V.bake_dir, V.map);
    mapbuild_lightmap(cw_scene_app_tree(), &V.b, V.baking ? out : NULL, V.text);
    V.lm_done = true;
#if !CW_CART
    if (V.baking) {
      if (V.b.lm) { size_t len = 0; cw_lightmap_bytes(V.b.lm, &len); V.bytes += len, V.baked++; }
      int n = next_wanted(V.map + 1);
      if (!n) {
        fprintf(stderr, "mapview: baked %d maps in %.1f s, %zu bytes\n", V.baked, cw_time() - V.t0, V.bytes);
        exit(0);
      }
      load(n);
      return;
    }
#endif
  }
  if (cw_action_just_pressed(0, V.right)) { load(V.map + 1); return; }
  if (cw_action_just_pressed(0, V.left)) { load(V.map - 1); return; }
  if (cw_action_just_pressed(0, V.next)) { load(((V.map - 1) / 5 + 1) * 5 + 1); return; }
  if (cw_action_just_pressed(0, V.up) || cw_action_just_pressed(0, V.down)) V.view = !V.view;
}

static void render(void *u, double alpha) { (void)u, (void)alpha; }

cw_app_desc cw_main(int argc, char **argv) {
  for (int i = 1; i + 1 < argc; i++) {
    if (!strcmp(argv[i], "--bake")) V.bake_dir = argv[++i];
    else if (!strcmp(argv[i], "--map")) V.want = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--only")) V.only = argv[++i];
  }
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Towers maps", .width = 1280, .height = 720, .debug = true, .init = init, .update = update, .render = render };
  cw_project_apply_app(&app);
  return cw_scene_app("scenes/mapview.cwscene", &app);
}
