/* A flash meter (src/flash_meter.c): an effect's drawn value each tick in, flashes a second out as debug fields. */
#ifndef FLASH_METER_H
#define FLASH_METER_H
#include <stdbool.h>
#include <stdint.h>

#define FLASH_METER_RING 32

typedef struct flash_meter {
  char name[24], f_name[48], p_name[48];
  int32_t max_flashes; /* fl.<name>_flashes */
  float peak;          /* fl.<name>_peak */
  float ext;           /* the last extreme */
  int dir, n, strobe, clock;
  bool started;
  int times[FLASH_METER_RING]; /* ticks of the latest transitions */
} flash_meter;

void flash_meter_init(flash_meter *m, const char *name); /* in init: publishes its fields */
void flash_meter_register(flash_meter *m);               /* adds it to the flash_strobe debug command (the control) */
/* every tick: the value the effect would draw (0..1) in, the value to draw out (the control strobes it) */
float flash_meter_feed(flash_meter *m, float value);
#endif
