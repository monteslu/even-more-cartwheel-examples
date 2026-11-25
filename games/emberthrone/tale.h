/* tale.c: Ember Throne's story, voices and cinematics (see the top of tale.c). */
#ifndef ADV_TALE_H
#define ADV_TALE_H
#include "cartwheel/director.h"
#include "cartwheel/render2d.h"
#include "cartwheel/scene.h"
#include "cartwheel/shell.h"

void tale_init(void);                      /* init: the story, the voices, the director */
void tale_build(cw_node root);             /* once the world is built: the guide and the keepers */
void tale_start(bool fresh);               /* a journey starts (fresh) or is loaded from S.sv */
void tale_save(void);                      /* the story's state into S.sv.story */
bool tale_blocking(void);                  /* a scene (or the finisher's shots) holds the rules */
void tale_events(void);                    /* after each rule tick: the tick's events */
void tale_tick(float dt);                  /* every tick after view_fixed: the story, faces, the camera */
bool tale_camera(cw_dir_camera *cam);      /* the director's camera now (false: the game's own) */
void tale_draw(cw_font font);              /* in the HUD pass: subtitles and choices */
void tale_settings(cw_shell_setting rows[2]); /* Voices and Subtitles for the Settings screen */
bool tale_ended(void);                     /* the ending has been told */
int tale_ending(void);                     /* 0 none, 1 the keeper's, 2 the frost's */
float tale_music_duck(void);               /* multiply the music's volume by this */
bool tale_flag(const char *flag);
#endif
