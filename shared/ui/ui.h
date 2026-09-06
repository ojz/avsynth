#ifndef AVSYNTH_UI_H
#define AVSYNTH_UI_H

#include <SDL3/SDL.h>
#include "param.h"

/*
 * The lab's control surface on SDL3. Today it owns the fader: how it is laid
 * out, how it is drawn, and which gesture does what. ROADMAP.md section 6 is
 * the specification and this is the only implementation of it, so every app's
 * controls behave identically by construction.
 *
 * Text comes from the app through UiText. The apps do not yet agree on a font
 * (Drone Commander uses SDL's debug text, vsynth has a glyph atlas from
 * SDL3_ttf) and the fader does not need them to.
 */

typedef struct UiText {
    void  (*draw)(void *ud, float x, float y, const char *s, SDL_Color col);
    float (*width)(void *ud, const char *s);
    float (*height)(void *ud);
    void   *ud;
} UiText;

/* One palette for the lab. */
typedef struct UiTheme {
    SDL_Color bg, panel, panel_head;
    SDL_Color ink, ink_dim, ink_faint;
    SDL_Color accent;                 /* selection and highlight */
    SDL_Color track, fill, fill_off;
    SDL_Color handle, rule;
    SDL_Color ok, warn;
    float track_h;                    /* track thickness across its short axis */
    float handle_w;                   /* handle thickness along the track */
    float handle_over;                /* how far the handle overhangs the track */
} UiTheme;

const UiTheme *ui_theme(void);

typedef enum { UI_H, UI_V } UiOrient;

/* A laid-out fader. The app owns `box`; the rest is computed from it. */
typedef struct UiFader {
    SDL_FRect box;      /* the whole cell, and what the pointer must be inside */
    SDL_FRect track;    /* where dragging works */
    SDL_FRect label;
    SDL_FRect value;
    UiOrient  orient;
} UiFader;

void ui_fader_layout(UiFader *f, SDL_FRect box, UiOrient o, const UiText *t);
void ui_fader_draw(SDL_Renderer *r, const UiText *t, const UiFader *f,
                   const Param *p, double value, int selected);

/* The fader as one line, for an app whose controls are a list rather than a
 * panel: label on the left in label_w, readout right-aligned in value_w, the
 * track between them. Drawn by ui_fader_draw and driven by the same gestures;
 * only the arrangement differs. */
void ui_fader_layout_row(UiFader *f, SDL_FRect box, float label_w, float value_w,
                         const UiText *t);

/* A switch or enum stepper, drawn to the same rhythm as a fader. */
void ui_stepper_draw(SDL_Renderer *r, const UiText *t, SDL_FRect box,
                     const Param *p, double value, int selected);
/* The same stepper on one line, to sit in a list next to row faders. */
void ui_stepper_draw_row(SDL_Renderer *r, const UiText *t, SDL_FRect box,
                         float label_w, float value_w,
                         const Param *p, double value, int selected);

/*
 * The gesture table, in one place (ROADMAP section 6):
 *
 *   drag the track            absolute, follows the pointer
 *   wheel                     one fine step
 *   ctrl + wheel              one coarse step
 *   shift + wheel             one ultra-fine step
 *   middle click, double click  reset to the neutral
 *   arrows when selected      one fine step; ctrl coarse, shift ultra-fine
 *   backspace when selected   reset to the neutral
 *
 * An app composes these three calls; it does not invent its own mapping.
 */
ParamGrain ui_grain(SDL_Keymod mod);
int    ui_is_reset_click(const SDL_Event *ev);
double ui_fader_value_at(const UiFader *f, const Param *p, float x, float y);

int  ui_hit(const SDL_FRect *box, float x, float y);
void ui_fill(SDL_Renderer *r, SDL_FRect box, SDL_Color c);
void ui_rect(SDL_Renderer *r, SDL_FRect box, SDL_Color c);
/* A framed panel with a title bar; returns the usable area inside it. */
SDL_FRect ui_panel(SDL_Renderer *r, const UiText *t, SDL_FRect box, const char *title);

#endif
