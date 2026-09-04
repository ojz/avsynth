#ifndef DRONE_COMMANDER_PANEL_H
#define DRONE_COMMANDER_PANEL_H

#include "dsp.h"
#include "param.h"
#include "ui.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

#define PANEL_WIDTH 1180
#define PANEL_HEIGHT 760

/*
 * The panel is a ParamSet laid out on a grid. Every continuous control is a
 * fader from shared/ui, so the gestures here are the same ones every other
 * app in the lab has (ROADMAP section 6). The DSP engine keeps its own view of
 * the values in SynthParameters; the panel fills it.
 */
typedef struct PanelState {
    ParamSet set;
    int      drag;      /* index being dragged, -1 when not */
} PanelState;

void panel_init(PanelState *panel, const SynthParameters *from);

/* Pull the panel's values into the engine's view, and the reverse. */
void panel_to_parameters(const PanelState *panel, SynthParameters *out);
void panel_from_parameters(PanelState *panel, const SynthParameters *in);

/* Returns true when a value changed and the audio thread needs telling.
 * Sets *audio_clicked when the mute banner was hit. */
bool panel_handle_event(PanelState *panel, const SDL_Event *ev, bool *audio_clicked);

void panel_render(SDL_Renderer *r, PanelState *panel, Synth *preview, bool audio_enabled);

/* One line naming the selected control and its value, for the window title. */
void panel_describe_selection(const PanelState *panel, char *buf, size_t cap);

#endif
