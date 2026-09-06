#ifndef DRONE_COMMANDER_PANEL_H
#define DRONE_COMMANDER_PANEL_H

#include "dsp.h"
#include "app.h"
#include "param.h"
#include "ui.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

#define PANEL_WIDTH 1180
#define PANEL_HEIGHT 760
#define PANEL_PARAM_COUNT 28

/*
 * The panel is a ParamSet laid out on a grid. Every continuous control is a
 * fader from shared/ui, laid out here and driven by the shell, so this file
 * never sees a fader gesture: it lays out, it draws, and it maps values to
 * and from the engine's SynthParameters. Everything it knows lives in this
 * struct (D12); the parameter table is copied in at init so two panels can
 * exist at once.
 */
typedef struct PanelState {
    const AppHost *host;
    Param     params[PANEL_PARAM_COUNT];
    double    values[PANEL_PARAM_COUNT];
    ParamSet  set;
    UiControl controls[PANEL_PARAM_COUNT];
    UiSurface surface;    /* what the shell hit-tests */
    int       laid_out;
} PanelState;

void panel_init(PanelState *panel, const AppHost *host, const SynthParameters *from);

/* Pull the panel's values into the engine's view, and the reverse. */
void panel_to_parameters(const PanelState *panel, SynthParameters *out);
void panel_from_parameters(PanelState *panel, const SynthParameters *in);

/* The one control that is not a fader: was this click on the mute banner? */
bool panel_banner_hit(const PanelState *panel, float x, float y);

void panel_render(SDL_Renderer *r, PanelState *panel, Synth *preview, bool audio_enabled);

/* One line naming the selected control and its value, for the window title. */
void panel_describe_selection(const PanelState *panel, char *buf, size_t cap);

#endif
