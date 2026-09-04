#ifndef DRONE_COMMANDER_PANEL_H
#define DRONE_COMMANDER_PANEL_H

#include "dsp.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

#define PANEL_WIDTH 1180
#define PANEL_HEIGHT 760

typedef struct {
    int active_control;
    bool dragging;
    float drag_start_y;
    float drag_start_value;
} PanelState;

void panel_render(SDL_Renderer *renderer, Synth *preview, const SynthParameters *parameters,
                  const PanelState *panel, bool audio_enabled);
bool panel_mouse_down(PanelState *panel, SynthParameters *parameters,
                      float x, float y, bool *audio_button_clicked);
bool panel_mouse_motion(PanelState *panel, SynthParameters *parameters, float y);
void panel_mouse_up(PanelState *panel);

#endif