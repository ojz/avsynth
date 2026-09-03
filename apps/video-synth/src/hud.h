#ifndef VSYNTH_HUD_H
#define VSYNTH_HUD_H

#include <stdint.h>
#include <SDL2/SDL.h>
#include "rack.h"
#include "voice.h"

/*
 * On-screen panel drawn over the video in the output window. One row per
 * control: module, knob, a value bar, the number. Header shows chain name,
 * preset slot, capture region and fps; footer shows the key map. A transient
 * notice line replaces the header for a few seconds. Preview taps (open
 * outputs of the chain) are drawn as thumbnails along the bottom edge.
 *
 * Mouse: click a row to select it, drag on its bar to set the value, click the
 * module name to bypass the module, wheel to nudge. Everything inside the
 * panel is consumed so the window does not start moving underneath.
 *
 * Text comes from SDL2_ttf and a system monospace font; without one the panel
 * still draws bars and highlights. The glyph atlas is shared with the editor
 * through hud_text().
 */
typedef struct Hud Hud;

Hud  *hud_create(SDL_Renderer *ren, const Rack *rack);
void  hud_destroy(Hud *h);

void  hud_toggle(Hud *h);
void  hud_set_visible(Hud *h, int on);
int   hud_visible(const Hud *h);

void  hud_set_patch(Hud *h, int slot);
void  hud_set_chain_name(Hud *h, const char *name);
void  hud_set_capture(Hud *h, int x, int y, int w, int h_);
void  hud_set_fps(Hud *h, double fps);
void  hud_notice(Hud *h, const char *msg);

/* Preview taps. Call hud_set_tap_count() after a (re)start; frames are BGRA. */
void  hud_set_tap_count(Hud *h, int n, const char names[][32]);
void  hud_set_tap_frame(Hud *h, int tap, const uint8_t *bgra, int w, int h_, int stride);

/* Overlay callback for window_set_overlay(); ud is the Hud*. */
void  hud_draw(SDL_Renderer *ren, int w, int h, void *ud);

/* Returns 1 if the event was consumed. */
int   hud_handle_event(Hud *h, const SDL_Event *ev, Rack *rack, Voice *voice);

/* Text drawing with the panel's glyph atlas, for other overlays. */
void  hud_text(Hud *h, int x, int y, SDL_Color col, const char *s);
int   hud_text_width(const Hud *h, const char *s);
int   hud_line_h(const Hud *h);
int   hud_char_w(const Hud *h);

#endif
