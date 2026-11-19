/* A flash meter: does an effect strobe? Game-agnostic: copy it into any cartwheel game.
 *
 * Feed it, every tick, the value an effect draws (a light's strength, an overlay's opacity, 0..1). A transition is a
 * change of 0.1 or more from the last extreme the other way (changes the same way only stretch the extreme); a
 * flash is a pair of opposing transitions (WCAG 2.3.1 and the Harding test count them so). The meter publishes, as
 * debug fields:
 *   fl.<name>_flashes   the most flashes in any one second (60 ticks) so far: at most 3 passes
 *   fl.<name>_peak      the strongest value drawn so far (a meter that saw nothing proves nothing)
 * tests/beatsync-flash.mjs reads them after a ranged romdev run. Its control is the `flash_strobe <name>` debug command,
 * which makes that effect switch on and off every 4 ticks for 2 seconds: the meter must then read more than 3.
 *
 * Until romdev can hand over every frame of a run (the engine's tools/flash-check.mjs measures the picture itself),
 * this is how a game proves its effects swell and fade instead of flashing. */
#include <stdio.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "flash_meter.h"

void flash_meter_init(flash_meter *m, const char *name) {
  memset(m, 0, sizeof *m);
  snprintf(m->f_name, sizeof m->f_name, "fl.%s_flashes", name);
  snprintf(m->p_name, sizeof m->p_name, "fl.%s_peak", name);
  snprintf(m->name, sizeof m->name, "%s", name);
  cw_debug_i32(m->f_name, &m->max_flashes);
  cw_debug_f32(m->p_name, &m->peak);
}

/* the value the effect would draw this tick (call it once every tick); returns the value to draw (the control
 * replaces it while it strobes) */
float flash_meter_feed(flash_meter *m, float v) {
  int tick = m->clock++; /* its own clock: one call a tick, never reset with the game */
  if (m->strobe > 0) { /* the control: the effect forced on and off every 4 ticks */
    v = (m->strobe / 4) % 2 ? 1.0f : 0.0f;
    m->strobe--;
  }
  if (v > m->peak) m->peak = v;
  if (!m->started) { m->started = true; m->ext = v; m->dir = 0; return v; }
  bool transition = false;
  if (m->dir > 0 && v > m->ext) m->ext = v;      /* still rising: the extreme stretches */
  else if (m->dir < 0 && v < m->ext) m->ext = v; /* still falling */
  else if (m->dir <= 0 && v - m->ext >= 0.1f) { m->dir = 1; m->ext = v; transition = true; }
  else if (m->dir >= 0 && m->ext - v >= 0.1f) { m->dir = -1; m->ext = v; transition = true; }
  if (transition) m->times[m->n++ % FLASH_METER_RING] = tick;
  int in_second = 0, have = m->n < FLASH_METER_RING ? m->n : FLASH_METER_RING;
  for (int i = 0; i < have; i++)
    if (tick - m->times[i] < 60) in_second++;
  int flashes = in_second / 2;
  if (flashes > m->max_flashes) m->max_flashes = flashes;
  return v;
}

static flash_meter *meters[8];
static int n_meters;

static void cmd_strobe(int argc, const char **argv, cw_reply *r) {
  for (int i = 0; argc > 1 && i < n_meters; i++)
    if (!strcmp(meters[i]->name, argv[1])) {
      meters[i]->strobe = 120;
      cw_reply_printf(r, "{\"strobing\":\"%s\",\"ticks\":120}", argv[1]);
      return;
    }
  cw_reply_error(r, "usage: flash_strobe <effect> (a flash meter's name, src/flash_meter.c)");
}

void flash_meter_register(flash_meter *m) {
  if (n_meters < 8) meters[n_meters++] = m;
  if (n_meters == 1)
    cw_debug_command("flash_strobe", "flash_strobe <effect>: the flash check's control, that effect on and off every 4 ticks for 2 s (src/flash_meter.c)", cmd_strobe);
}
