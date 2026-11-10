/* destructo_check: Destructo's checks as a cart, run on romdev like the game (tests/destructo-*.mjs drive it).
 * The cart holds the check tools built from the game's own sources: levelcheck.c (levels, landmarks, kegs, their
 * controls), humancheck.c (the human-like player), physcheck.c (what Box3D does for stacks and shot) and soak.c (level
 * after level with the bot). Each tool's main() is compiled in under a name of its own (tool_levels, tool_human,
 * tool_phys, tool_soak) and its printf() lines go to this cart's output buffer (checkout.h).
 *
 * A check runs on a background job of the engine's thread pool, so its physics runs on the pool's other workers as in
 * the game, and the cart's frames stay short however long the check takes: the gate steps frames in long ranges and
 * reads check.state between them. Debug channel:
 *   check run <tool> [arg...]   starts a check (one at a time): the tool's own arguments, as natively
 *   check status                {"state":0 idle | 1 running | 2 done,"status":exit status,"bytes":output so far,"ms":wall}
 *   check out <from> [max]      the output from byte <from> (at most max bytes, 64 KiB by default) as a JSON string
 * Debug fields: check.state, check.status, check.bytes, check.ms (the same), check.threads (the pool's threads). */
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/jobs.h"
#include "cartwheel/platform.h"
#include "cartwheel/profile.h"
#include "checkout.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

void cw__mem_sync(void); /* the engine's: this thread sees memory another thread grew (src/core/mem.c) */
int tool_levels(int argc, char **argv);
int tool_human(int argc, char **argv);
int tool_phys(int argc, char **argv);
int tool_soak(int argc, char **argv);

static const struct { const char *name; int (*fn)(int, char **); } TOOLS[] = {
  { "levels", tool_levels }, { "human", tool_human }, { "phys", tool_phys }, { "soak", tool_soak },
};

/* the output: appended by the check's thread, read by the main thread's commands */
static struct {
  atomic_flag lock;
  char *buf;
  size_t len, cap;
} OUT = { ATOMIC_FLAG_INIT };

int check_printf(const char *fmt, ...) {
  char tmp[4096];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
  va_end(ap);
  if (n < 0) return n;
  char *big = NULL;
  const char *s = tmp;
  if ((size_t)n >= sizeof tmp) { /* a long line: format it again into a buffer of its size */
    big = malloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    s = big;
  }
  while (atomic_flag_test_and_set_explicit(&OUT.lock, memory_order_acquire)) {}
  if (OUT.len + (size_t)n + 1 > OUT.cap) {
    size_t cap = OUT.cap ? OUT.cap : 1 << 16;
    while (OUT.len + (size_t)n + 1 > cap) cap *= 2;
    OUT.buf = realloc(OUT.buf, cap), OUT.cap = cap;
  }
  memcpy(OUT.buf + OUT.len, s, (size_t)n);
  OUT.len += (size_t)n;
  OUT.buf[OUT.len] = 0;
  atomic_flag_clear_explicit(&OUT.lock, memory_order_release);
  free(big);
  return n;
}

static struct {
  int (*fn)(int, char **);
  int argc;
  char *argv[48];
  char words[4096];
  cw_job *job;
  atomic_int state; /* 0 idle, 1 running, 2 done */
  int32_t state_dbg, status, bytes, ms, threads;
  double t0;
} C;

static void run_job(void *u) {
  (void)u;
  C.status = C.fn(C.argc, C.argv);
  C.ms = (int32_t)((double)cw_profile_now_ns() * 1e-6 - C.t0);
  atomic_store(&C.state, 2);
}

static void cmd_check(int argc, const char **argv, cw_reply *r) {
  const char *op = argc > 1 ? argv[1] : "status";
  if (!strcmp(op, "run")) {
    if (atomic_load(&C.state) == 1) { cw_reply_error(r, "check: a check is running (check status)"); return; }
    if (argc < 3) { cw_reply_error(r, "check run <levels|human|phys|soak> [arg...]"); return; }
    C.fn = NULL;
    for (size_t i = 0; i < NELEM(TOOLS); i++) if (!strcmp(TOOLS[i].name, argv[2])) C.fn = TOOLS[i].fn;
    if (!C.fn) { cw_reply_error(r, "check: no tool %s (levels, human, phys, soak)", argv[2]); return; }
    if (C.job) cw_job_wait(C.job), C.job = NULL; /* the last one's job (done) */
    size_t at = 0;
    C.argc = 0;
    for (int k = 2; k < argc && C.argc < (int)NELEM(C.argv) - 1; k++) {
      size_t n = strlen(argv[k]) + 1;
      if (at + n > sizeof C.words) break;
      memcpy(C.words + at, argv[k], n);
      C.argv[C.argc++] = C.words + at, at += n;
    }
    C.argv[C.argc] = NULL;
    while (atomic_flag_test_and_set_explicit(&OUT.lock, memory_order_acquire)) {}
    OUT.len = 0;
    if (OUT.buf) OUT.buf[0] = 0;
    atomic_flag_clear_explicit(&OUT.lock, memory_order_release);
    C.status = -1;
    C.t0 = (double)cw_profile_now_ns() * 1e-6;
    atomic_store(&C.state, 1);
    C.job = cw_job_background(run_job, NULL);
    cw_reply_printf(r, "{\"started\":\"%s\",\"args\":%d,\"threads\":%d}", argv[2], C.argc - 1, cw_jobs_thread_count());
    return;
  }
  if (!strcmp(op, "status")) {
    cw_reply_printf(r, "{\"state\":%d,\"status\":%d,\"bytes\":%d,\"ms\":%d,\"threads\":%d}", atomic_load(&C.state), C.status, C.bytes, C.ms, cw_jobs_thread_count());
    return;
  }
  if (!strcmp(op, "out")) {
    size_t from = argc > 2 ? (size_t)strtoull(argv[2], NULL, 10) : 0, max = argc > 3 ? (size_t)strtoull(argv[3], NULL, 10) : 65536;
    cw__mem_sync();
    while (atomic_flag_test_and_set_explicit(&OUT.lock, memory_order_acquire)) {}
    size_t len = OUT.len;
    if (from > len) from = len;
    size_t n = len - from < max ? len - from : max;
    char *chunk = malloc(n + 1);
    if (n) memcpy(chunk, OUT.buf + from, n);
    chunk[n] = 0;
    atomic_flag_clear_explicit(&OUT.lock, memory_order_release);
    cw_reply_printf(r, "{\"from\":%zu,\"to\":%zu,\"len\":%zu,\"text\":", from, from + n, len);
    cw_reply_str(r, chunk);
    cw_reply_raw(r, "}");
    free(chunk);
    return;
  }
  cw_reply_error(r, "check: expected run, status or out");
}

static void init(void *u) {
  (void)u;
  cw_debug_command("check", "check run <levels|human|phys|soak> [arg...] | status | out <from> [max]: Destructo's checks (tools/checkcart.c)", cmd_check);
  cw_debug_i32("check.state", &C.state_dbg);
  cw_debug_i32("check.status", &C.status);
  cw_debug_i32("check.bytes", &C.bytes);
  cw_debug_i32("check.ms", &C.ms);
  cw_debug_i32("check.threads", &C.threads);
  C.status = -1;
}

static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  int s = atomic_load(&C.state);
  if (s == 1) C.ms = (int32_t)((double)cw_profile_now_ns() * 1e-6 - C.t0);
  C.state_dbg = s;
  C.threads = cw_jobs_thread_count();
  while (atomic_flag_test_and_set_explicit(&OUT.lock, memory_order_acquire)) {}
  C.bytes = (int32_t)OUT.len;
  atomic_flag_clear_explicit(&OUT.lock, memory_order_release);
}

static void render(void *u, double alpha) { (void)u, (void)alpha; }

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  return (cw_app_desc){ .title = "Destructo checks", .width = 320, .height = 180, .debug = true, .init = init, .fixed_update = fixed_update, .render = render };
}
