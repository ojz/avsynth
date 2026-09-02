#ifndef VSYNTH_HUD_H
#define VSYNTH_HUD_H

#include <SDL2/SDL.h>
#include "rack.h"
#include "voice.h"

/*
 * On-screen panel drawn over the video in the output window. One row per
 * control: module, knob, a value bar, the number. Header shows patch slot,
 * capture region and fps; footer shows the key map. A transient notice line
 * replaces the header for a few seconds.
 *
 * Mouse: click a row to select it, drag on its bar to set the value, click the
 * module name to bypass the module, wheel to nudge. Everything inside the
 * panel is consumed so the window does not start moving underneath.
 *
 * Text comes from SDL2_ttf and a system monospace font; without one the panel
 * still draws bars and highlights.
 */
typedef struct Hud Hud;

Hud  *hud_create(SDL_Renderer *ren, const Rack *rack);
void  hud_destroy(Hud *h);

void  hud_toggle(Hud *h);
int   hud_visible(const Hud *h);

void  hud_set_patch(Hud *h, int slot);
void  hud_set_capture(Hud *h, int x, int y, int w, int h_);
void  hud_set_fps(Hud *h, double fps);
void  hud_notice(Hud *h, const char *msg);

/* Overlay callback for window_set_overlay(); ud is the Hud*. */
void  hud_draw(SDL_Renderer *ren, int w, int h, void *ud);

/* Returns 1 if the event was consumed. */
int   hud_handle_event(Hud *h, const SDL_Event *ev, Rack *rack, Voice *voice);

#endif
