#ifndef AVSYNTH_APP_H
#define AVSYNTH_APP_H

#include <stdbool.h>
#include <SDL3/SDL.h>
#include "param.h"
#include "ui.h"

/*
 * The shell (ROADMAP D14, section 5.3). SDL init, the window, the frame loop,
 * the event pump, the lab's typeface and the D8 gesture dispatch live here,
 * once. An app supplies a struct it owns and a table of functions, and never
 * writes an event switch for a fader.
 *
 * Event order, per event:
 *
 *   mouse     the shell first, hit-testing the app's UiSurface: drag sets,
 *             wheel nudges by grain, middle or double click resets, a click on
 *             a switch or enum steps it. Only a hit is consumed. Then the app.
 *   keyboard  the app first, so a mode that owns the keyboard (a text editor)
 *             sees everything. Then the shell: Tab selects, arrows nudge,
 *             Backspace resets.
 *
 * Mouse events reach the app already converted to renderer coordinates, so an
 * app hit-tests what it drew without caring about pixel density.
 *
 * The shell also handles what every app wants and none should write twice:
 * --screenshot FILE.bmp (the window, two seconds after start) and F12
 * (shot-NNN.bmp into --shots DIR, default the app's data folder). These flags
 * are removed from argv before the app sees it.
 */

typedef struct AppHost AppHost;

/* A control the app has laid out: which parameter, and where. The app fills
 * a UiSurface whenever it lays out; the shell hit-tests it. */
typedef struct UiControl {
    int       param;   /* index into the app's ParamSet */
    SDL_FRect box;     /* where the pointer must be */
    UiFader   fader;   /* valid when the parameter is a PARAM_FADER */
} UiControl;

typedef struct UiSurface {
    const UiControl *items;
    int n;
} UiSurface;

typedef struct AppSpec {
    const char *name;               /* "vsynth": the data folder and the log prefix */
    const char *title;              /* the window's first title */
    int  window_w, window_h;
    SDL_WindowFlags window_flags;   /* added to the shell's own; e.g. SDL_WINDOW_BORDERLESS */
    SDL_InitFlags   init_flags;     /* beyond video and events; e.g. SDL_INIT_AUDIO */

    /* Returns the instance, or NULL to refuse to start (the shell exits 1). */
    void *(*create)(AppHost *host, int argc, char **argv);
    void  (*destroy)(void *self);

    /* Return true to consume. See the event order above. May be NULL. */
    bool  (*event)(void *self, const SDL_Event *ev);
    /* Once per loop before frame(): poll a thread, check a failure. May be NULL. */
    void  (*tick)(void *self);
    /* Draw the whole window. The shell clears before and presents after. */
    void  (*frame)(void *self, SDL_Renderer *r);

    /* The controls the shell drives. params() must not return NULL; surface()
     * may, meaning there is nothing under the mouse right now. */
    ParamSet        *(*params)(void *self);
    const UiSurface *(*surface)(void *self);
    /* The shell changed values[param] (param >= 0) or the selection (-1). */
    void  (*changed)(void *self, int param);
} AppSpec;

/* What the shell gives an app: its window, its renderer, the lab's text, a
 * writable folder. Supplied by the exe stub when run alone and by the launcher
 * when hosted (P6), so an app never asks SDL for these itself. */
struct AppHost {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    UiText        text;         /* the lab's typeface, drawn through the shell */
    char          data_dir[1024];   /* per-user, per-app, trailing separator */
    bool          quit;         /* set it to leave the loop after this frame */
};

/* Run one app in its own process. The exe stub is exactly this call. */
int app_run(const AppSpec *spec, int argc, char **argv);

/* Text metrics for layout. Height is the line height; char_w is the widest
 * glyph advance, which for a monospace face is every glyph. */
float app_text_height(const AppHost *h);
float app_text_char_w(const AppHost *h);
float app_text_width(const AppHost *h, const char *s);

/* Save the window as it was last presented. Used by the shell for F12 and
 * --screenshot; an app may call it too. */
int   app_save_bmp(AppHost *h, const char *path);

#endif
