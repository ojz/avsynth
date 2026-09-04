#ifndef VSYNTH_HUD_H
#define VSYNTH_HUD_H

#include <stdint.h>
#include <SDL2/SDL.h>
#include "rack.h"
#include "voice.h"

/*
 * The HUD owns everything drawn over the video that is shared between modes:
 *
 *   - the sheet: one translucent frame, same place and size in every mode,
 *     with a header (F-key tabs, active mode highlighted, chain / preset / fps
 *     on the right) and a footer hint line. Each mode draws its body inside
 *     the rect hud_sheet() returns;
 *   - the knobs panel body (mode MODE_PANEL): one row per control with a bar;
 *   - tap thumbnails along the bottom edge, in any mode that shows the picture;
 *   - transient notices, shown even on the bare picture;
 *   - the glyph atlas (SDL2_ttf, system monospace font) via hud_text().
 *
 * Panel mouse: click a row to select it, drag on its bar to set the value,
 * click the module name to bypass the module, wheel to nudge. Only the left
 * button is consumed, so right-drag resizes the window even over the panel.
 */

enum Mode { MODE_MAIN, MODE_PANEL, MODE_EDIT, MODE_HELP, MODE_PROJECT, MODE_COUNT };

typedef struct Hud Hud;

Hud  *hud_create(SDL_Renderer *ren, const Rack *rack);
void  hud_destroy(Hud *h);

void  hud_set_mode(Hud *h, enum Mode m);
enum Mode hud_mode(const Hud *h);

void  hud_set_patch(Hud *h, int slot);
void  hud_set_chain(Hud *h, const char *name, int index, int count);
void  hud_set_capture(Hud *h, int x, int y, int w, int h_);
void  hud_set_fps(Hud *h, double fps);
void  hud_notice(Hud *h, const char *msg);

/* Preview taps. Call hud_set_tap_count() after a (re)start; frames are BGRA. */
void  hud_set_tap_count(Hud *h, int n, const char names[][32]);
void  hud_set_tap_frame(Hud *h, int tap, const uint8_t *bgra, int w, int h_, int stride);

/* Draws taps, notices, and in MODE_PANEL the sheet with the knob rows.
 * Other modes call hud_sheet() themselves from their draw function. */
void  hud_draw(Hud *h, SDL_Renderer *ren, int w, int h_);

/* Draw the frame and header for the current mode; returns the body rect
 * (between header and footer). hud_footer() writes the hint line under it. */
SDL_Rect hud_sheet(Hud *h, SDL_Renderer *ren, int W, int H);
void     hud_footer(Hud *h, const SDL_Rect *body, const char *left, const char *right);

/* Panel mouse. Returns 1 if the event was consumed. */
int   hud_handle_event(Hud *h, const SDL_Event *ev, Rack *rack, Voice *voice);

/* Text drawing with the shared glyph atlas. */
void  hud_text(Hud *h, int x, int y, SDL_Color col, const char *s);
int   hud_text_width(const Hud *h, const char *s);
int   hud_line_h(const Hud *h);
int   hud_char_w(const Hud *h);
void  hud_fill(SDL_Renderer *ren, SDL_Rect r, Uint8 R, Uint8 G, Uint8 B, Uint8 A);

/* Shared palette */
extern const SDL_Color HUD_TEXT, HUD_DIM, HUD_OFF, HUD_SEL, HUD_OK, HUD_ERR;

#endif
