/* Destructo's run options, set through the debug channel's `dm` command (opts.h). */
#include "opts.h"

#include <stdio.h>
#include <string.h>

#include "cartwheel/debug.h"

enum { NOPT = 48, NAME_MAX_ = 32, VALUE_MAX = 96 };
static struct { char name[NAME_MAX_], value[VALUE_MAX]; } O[NOPT];
static int nopt;
static void (*on_restart)(void);
static void (*on_changed)(const char *name);

static int find(const char *name) {
  for (int i = 0; i < nopt; i++) if (!strcmp(O[i].name, name)) return i;
  return -1;
}
const char *opt_get(const char *name) {
  int i = find(name);
  return i >= 0 ? O[i].value : NULL;
}
bool opt_on(const char *name) {
  const char *v = opt_get(name);
  return v && strcmp(v, "0") != 0;
}
void opt_set(const char *name, const char *value) {
  int i = find(name);
  if (!value) {
    if (i >= 0) O[i] = O[--nopt];
    return;
  }
  if (i < 0) {
    if (nopt == NOPT) return;
    i = nopt++;
    snprintf(O[i].name, sizeof O[i].name, "%s", name);
  }
  snprintf(O[i].value, sizeof O[i].value, "%s", value);
}

static void cmd_dm(int argc, const char **argv, cw_reply *r) {
  const char *op = argc > 1 ? argv[1] : "list";
  if (!strcmp(op, "set") && argc > 3) {
    char v[VALUE_MAX] = { 0 };
    for (int k = 3; k < argc; k++) {
      if (k > 3) strncat(v, " ", sizeof v - strlen(v) - 1);
      strncat(v, argv[k], sizeof v - strlen(v) - 1);
    }
    opt_set(argv[2], v);
    if (on_changed) on_changed(argv[2]);
  } else if (!strcmp(op, "unset") && argc > 2) {
    opt_set(argv[2], NULL);
    if (on_changed) on_changed(argv[2]);
  } else if (!strcmp(op, "restart")) {
    if (on_restart) on_restart();
  } else if (strcmp(op, "list")) {
    cw_reply_error(r, "dm: expected set NAME VALUE, unset NAME, list or restart");
    return;
  }
  cw_reply_raw(r, "{\"options\":{");
  for (int i = 0; i < nopt; i++) {
    cw_reply_printf(r, "%s\"%s\":", i ? "," : "", O[i].name);
    cw_reply_str(r, O[i].value);
  }
  cw_reply_raw(r, "}}");
}

void opt_init(void (*restart)(void), void (*changed)(const char *name)) {
  on_restart = restart, on_changed = changed;
  cw_debug_command("dm", "dm set NAME VALUE | unset NAME | list | restart: the game's run options (games/destructo/src/opts.h)", cmd_dm);
}
