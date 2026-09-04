#include "panel.h"

#include <math.h>
#include <stdio.h>

#define SLIDER_WIDTH 104.0f

typedef enum { CONTROL_KNOB, CONTROL_SWITCH } ControlType;
typedef enum {
    OSC1_FREQ, OSC1_AMP, OSC1_WAVE, OSC2_FREQ, OSC2_AMP, OSC2_WAVE,
    OSC3_FREQ, OSC3_AMP, OSC3_WAVE, FM_DEPTH, ANTI_ALIAS,
    LFO1_FREQ, LFO1_AMP, LFO2_FREQ, LFO2_AMP, LFO2_SYNC,
    LFO3_FREQ, LFO3_AMP, LFO3_SYNC, CUTOFF, CUTOFF_MOD, RESONANCE,
    VCA_AMP, VCA_MOD, DRIVE, CONTROL_COUNT
} ControlId;

typedef struct {
    ControlId id;
    ControlType type;
    const char *label;
    float x, y, minimum, maximum, step;
} Control;

static const Control controls[CONTROL_COUNT] = {
    {OSC1_FREQ, CONTROL_KNOB, "FREQUENCY", 105, 170, 20, 2000, 1},
    {OSC1_AMP, CONTROL_KNOB, "LEVEL", 105, 260, 0, 1, .01f},
    {OSC1_WAVE, CONTROL_SWITCH, "WAVE", 105, 335, 0, WAVE_COUNT - 1, 1},
    {OSC2_FREQ, CONTROL_KNOB, "FREQUENCY", 255, 170, 20, 2000, 1},
    {OSC2_AMP, CONTROL_KNOB, "LEVEL", 255, 260, 0, 1, .01f},
    {OSC2_WAVE, CONTROL_SWITCH, "WAVE", 255, 335, 0, WAVE_COUNT - 1, 1},
    {OSC3_FREQ, CONTROL_KNOB, "FREQUENCY", 405, 170, 20, 2000, 1},
    {OSC3_AMP, CONTROL_KNOB, "LEVEL", 405, 260, 0, 1, .01f},
    {OSC3_WAVE, CONTROL_SWITCH, "WAVE", 405, 335, 0, WAVE_COUNT - 1, 1},
    {FM_DEPTH, CONTROL_KNOB, "FM CASCADE", 510, 205, 0, 1000, 5},
    {ANTI_ALIAS, CONTROL_SWITCH, "ANTI-ALIAS", 510, 300, 0, 1, 1},
    {LFO1_FREQ, CONTROL_KNOB, "RATE", 105, 515, .01f, 20, .01f},
    {LFO1_AMP, CONTROL_KNOB, "LEVEL", 105, 615, 0, 1, .01f},
    {LFO2_FREQ, CONTROL_KNOB, "RATE", 260, 515, .01f, 20, .01f},
    {LFO2_AMP, CONTROL_KNOB, "LEVEL", 260, 615, 0, 1, .01f},
    {LFO2_SYNC, CONTROL_SWITCH, "TO LFO 1", 260, 690, 0, 1, 1},
    {LFO3_FREQ, CONTROL_KNOB, "RATE", 415, 515, .01f, 20, .01f},
    {LFO3_AMP, CONTROL_KNOB, "LEVEL", 415, 615, 0, 1, .01f},
    {LFO3_SYNC, CONTROL_SWITCH, "TO LFO 2", 415, 690, 0, 1, 1},
    {CUTOFF, CONTROL_KNOB, "CUTOFF", 650, 510, 20, 12000, 10},
    {RESONANCE, CONTROL_KNOB, "RESONANCE", 570, 620, 0, 1, .01f},
    {CUTOFF_MOD, CONTROL_KNOB, "MOD DEPTH", 730, 620, 0, 6000, 10},
    {VCA_AMP, CONTROL_KNOB, "LEVEL", 860, 510, 0, .8f, .01f},
    {VCA_MOD, CONTROL_KNOB, "MOD DEPTH", 860, 620, 0, 1, .01f},
    {DRIVE, CONTROL_KNOB, "DRIVE", 1055, 535, .1f, 8, .05f},
};

static float clampf(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float value_of(const SynthParameters *p, ControlId id)
{
    if (id <= OSC3_WAVE) {
        int oscillator = id / 3;
        int field = id % 3;
        if (field == 0) return p->oscillators[oscillator].frequency;
        if (field == 1) return p->oscillators[oscillator].amplitude;
        return (float)p->oscillators[oscillator].waveform;
    }
    switch (id) {
    case FM_DEPTH: return p->fm_depth;
    case ANTI_ALIAS: return p->blep_enabled;
    case LFO1_FREQ: return p->lfos[0].frequency;
    case LFO1_AMP: return p->lfos[0].amplitude;
    case LFO2_FREQ: return p->lfos[1].frequency;
    case LFO2_AMP: return p->lfos[1].amplitude;
    case LFO2_SYNC: return (float)p->lfos[1].sync_to_previous;
    case LFO3_FREQ: return p->lfos[2].frequency;
    case LFO3_AMP: return p->lfos[2].amplitude;
    case LFO3_SYNC: return (float)p->lfos[2].sync_to_previous;
    case CUTOFF: return p->cutoff;
    case CUTOFF_MOD: return p->cutoff_mod_depth;
    case RESONANCE: return p->resonance;
    case VCA_AMP: return p->vca_amplitude;
    case VCA_MOD: return p->vca_mod_depth;
    case DRIVE: return p->drive;
    default: return 0;
    }
}

static void set_value(SynthParameters *p, ControlId id, float value)
{
    if (id <= OSC3_WAVE) {
        int oscillator = id / 3;
        int field = id % 3;
        if (field == 0) p->oscillators[oscillator].frequency = value;
        else if (field == 1) p->oscillators[oscillator].amplitude = value;
        else p->oscillators[oscillator].waveform = (Waveform)(int)value;
        return;
    }
    switch (id) {
    case FM_DEPTH: p->fm_depth = value; break;
    case ANTI_ALIAS: p->blep_enabled = value; break;
    case LFO1_FREQ: p->lfos[0].frequency = value; break;
    case LFO1_AMP: p->lfos[0].amplitude = value; break;
    case LFO2_FREQ: p->lfos[1].frequency = value; break;
    case LFO2_AMP: p->lfos[1].amplitude = value; break;
    case LFO2_SYNC: p->lfos[1].sync_to_previous = value >= .5f; break;
    case LFO3_FREQ: p->lfos[2].frequency = value; break;
    case LFO3_AMP: p->lfos[2].amplitude = value; break;
    case LFO3_SYNC: p->lfos[2].sync_to_previous = value >= .5f; break;
    case CUTOFF: p->cutoff = value; break;
    case CUTOFF_MOD: p->cutoff_mod_depth = value; break;
    case RESONANCE: p->resonance = value; break;
    case VCA_AMP: p->vca_amplitude = value; break;
    case VCA_MOD: p->vca_mod_depth = value; break;
    case DRIVE: p->drive = value; break;
    default: break;
    }
}

static void text(SDL_Renderer *r, float x, float y, const char *s, Uint8 red, Uint8 green, Uint8 blue)
{
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
    SDL_RenderDebugText(r, x, y, s);
}

static void section(SDL_Renderer *r, float x, float y, float w, float h,
                    const char *title, Uint8 red, Uint8 green, Uint8 blue)
{
    SDL_FRect body = {x, y, w, h};
    SDL_FRect bar = {x, y, w, 25};
    SDL_SetRenderDrawColor(r, 29, 31, 30, 255);
    SDL_RenderFillRect(r, &body);
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
    SDL_RenderRect(r, &body);
    SDL_SetRenderDrawColor(r, red / 3, green / 3, blue / 3, 255);
    SDL_RenderFillRect(r, &bar);
    text(r, x + 9, y + 9, title, 236, 231, 213);
}

static void slider(SDL_Renderer *r, const Control *c, float value, bool active)
{
    float normalized = (value - c->minimum) / (c->maximum - c->minimum);
    SDL_FRect track = {c->x - SLIDER_WIDTH / 2, c->y - 3, SLIDER_WIDTH, 6};
    SDL_FRect fill = track;
    SDL_FRect handle = {track.x + normalized * track.w - 5, c->y - 12, 10, 24};
    char display[20];
    fill.w *= normalized;
    SDL_SetRenderDrawColor(r, 9, 10, 10, 255);
    SDL_RenderFillRect(r, &track);
    SDL_SetRenderDrawColor(r, active ? 232 : 126, active ? 180 : 128, active ? 67 : 116, 255);
    SDL_RenderFillRect(r, &fill);
    SDL_SetRenderDrawColor(r, 231, 224, 196, 255);
    SDL_RenderFillRect(r, &handle);
    text(r, c->x - SDL_strlen(c->label) * 4.0f, c->y - 30, c->label, 174, 170, 154);
    if (c->maximum > 100) SDL_snprintf(display, sizeof(display), "%.0f", value);
    else SDL_snprintf(display, sizeof(display), "%.2f", value);
    text(r, c->x - SDL_strlen(display) * 4.0f, c->y + 19, display, 228, 219, 190);
}

static void lfo_led(SDL_Renderer *r, float x, float y, bool positive)
{
    SDL_FRect red = {x - 17, y, 12, 12};
    SDL_FRect green = {x + 5, y, 12, 12};
    SDL_SetRenderDrawColor(r, positive ? 58 : 239, positive ? 27 : 62, positive ? 24 : 48, 255);
    SDL_RenderFillRect(r, &red);
    SDL_SetRenderDrawColor(r, positive ? 67 : 22, positive ? 221 : 61, positive ? 94 : 34, 255);
    SDL_RenderFillRect(r, &green);
    text(r, x - 28, y + 18, "-     +", 121, 126, 116);
}

static void toggle(SDL_Renderer *r, const Control *c, float value, bool active)
{
    SDL_FRect box = {c->x - 42, c->y - 13, 84, 26};
    const char *display;
    if (c->id == OSC1_WAVE || c->id == OSC2_WAVE || c->id == OSC3_WAVE)
        display = waveform_name((Waveform)(int)value);
    else if (c->id == ANTI_ALIAS) display = value >= .5f ? "CLEAN" : "RAW";
    else display = value >= .5f ? "SYNC" : "FREE";
    SDL_SetRenderDrawColor(r, active ? 232 : 108, active ? 180 : 111, active ? 67 : 103, 255);
    SDL_RenderRect(r, &box);
    box.x += 2; box.y += 2; box.w -= 4; box.h -= 4;
    SDL_SetRenderDrawColor(r, 11, 12, 12, 255);
    SDL_RenderFillRect(r, &box);
    text(r, c->x - SDL_strlen(display) * 4.0f, c->y - 3, display, 226, 221, 204);
    text(r, c->x - SDL_strlen(c->label) * 4.0f, c->y + 20, c->label, 158, 155, 143);
}

static void scope(SDL_Renderer *r, Synth *preview, const SynthParameters *parameters)
{
    float samples[768];
    SDL_FPoint points[550];
    SDL_FRect display = {590, 85, 550, 265};
    int index;
    int start = 0;
    synth_set_parameters(preview, parameters);
    synth_process(preview, samples, 768);
    for (index = 0; index < 200; ++index)
        if (samples[index] <= 0 && samples[index + 1] > 0) { start = index; break; }
    SDL_SetRenderDrawColor(r, 5, 14, 12, 255);
    SDL_RenderFillRect(r, &display);
    SDL_SetRenderDrawColor(r, 34, 64, 53, 255);
    SDL_RenderRect(r, &display);
    for (index = 1; index < 5; ++index) SDL_RenderLine(r, 590, 85 + index * 53, 1140, 85 + index * 53);
    for (index = 0; index < 550; ++index) {
        points[index].x = 590 + index;
        points[index].y = 217.5f - samples[start + index] * 112;
    }
    SDL_SetRenderDrawColor(r, 91, 239, 160, 255);
    SDL_RenderLines(r, points, 550);
    text(r, 602, 96, "SIGNAL MONITOR", 101, 180, 139);
}

void panel_render(SDL_Renderer *r, Synth *preview, const SynthParameters *parameters,
                  const PanelState *panel, bool audio_enabled)
{
    SDL_FRect audio = {845, 18, 310, 36};
    int index;
    SDL_SetRenderDrawColor(r, 17, 18, 17, 255);
    SDL_RenderClear(r);
    text(r, 28, 20, "DRONE", 232, 177, 49);
    text(r, 28, 35, "COMMANDER", 235, 228, 205);
    text(r, 168, 27, "THREE VOICE ANALOG SIGNAL LAB", 130, 127, 116);
    section(r, 25, 75, 540, 315, "OSCILLATOR BANK", 190, 128, 45);
    section(r, 25, 415, 455, 315, "MODULATION / 3 x SQUARE LFO", 55, 139, 122);
    section(r, 500, 415, 300, 315, "VOLTAGE CONTROLLED FILTER", 151, 79, 61);
    section(r, 820, 415, 130, 315, "VCA", 85, 112, 151);
    section(r, 970, 415, 185, 315, "OUTPUT", 172, 135, 52);
    scope(r, preview, parameters);
    for (index = 0; index < CONTROL_COUNT; ++index) {
        float value = value_of(parameters, controls[index].id);
        bool active = panel->active_control == index;
        if (controls[index].type == CONTROL_KNOB) slider(r, &controls[index], value, active);
        else toggle(r, &controls[index], value, active);
    }
    text(r, 82, 108, "VCO 1", 225, 174, 73);
    text(r, 232, 108, "VCO 2", 225, 174, 73);
    text(r, 382, 108, "VCO 3", 225, 174, 73);
    text(r, 82, 448, "LFO 1", 83, 190, 158);
    text(r, 237, 448, "LFO 2", 83, 190, 158);
    text(r, 392, 448, "LFO 3", 83, 190, 158);
    lfo_led(r, 105, 465, preview->lfos[0].phase < 0.5f);
    lfo_led(r, 260, 465, preview->lfos[1].phase < 0.5f);
    lfo_led(r, 415, 465, preview->lfos[2].phase < 0.5f);
    SDL_SetRenderDrawColor(r, audio_enabled ? 41 : 76, audio_enabled ? 111 : 39,
                           audio_enabled ? 74 : 35, 255);
    SDL_RenderFillRect(r, &audio);
    SDL_SetRenderDrawColor(r, audio_enabled ? 92 : 205, audio_enabled ? 226 : 80,
                           audio_enabled ? 143 : 70, 255);
    SDL_RenderRect(r, &audio);
    text(r, 861, 32, audio_enabled ? "AUDIO LIVE / CLICK OR SPACE TO MUTE"
                                  : "HARD MUTED / CLICK TO ENABLE AUDIO", 235, 226, 205);
    SDL_RenderPresent(r);
}

static int hit_control(float x, float y)
{
    int index;
    for (index = CONTROL_COUNT - 1; index >= 0; --index) {
        float half_width = controls[index].type == CONTROL_KNOB ? SLIDER_WIDTH / 2 + 6 : 45;
        float half_height = controls[index].type == CONTROL_KNOB ? 18 : 18;
        if (fabsf(x - controls[index].x) <= half_width &&
            fabsf(y - controls[index].y) <= half_height) return index;
    }
    return -1;
}

bool panel_mouse_down(PanelState *panel, SynthParameters *parameters,
                      float x, float y, bool *audio_button_clicked)
{
    int hit;
    *audio_button_clicked = x >= 845 && x <= 1155 && y >= 18 && y <= 54;
    if (*audio_button_clicked) return false;
    hit = hit_control(x, y);
    if (hit < 0) return false;
    panel->active_control = hit;
    if (controls[hit].type == CONTROL_SWITCH) {
        float value = value_of(parameters, controls[hit].id) + 1;
        if (value > controls[hit].maximum) value = controls[hit].minimum;
        set_value(parameters, controls[hit].id, value);
        return true;
    }
    panel->dragging = true;
    return panel_mouse_motion(panel, parameters, x);
}

bool panel_mouse_motion(PanelState *panel, SynthParameters *parameters, float x)
{
    const Control *control;
    float value;
    if (!panel->dragging) return false;
    control = &controls[panel->active_control];
    value = control->minimum + clampf((x - (control->x - SLIDER_WIDTH / 2)) / SLIDER_WIDTH, 0, 1) *
            (control->maximum - control->minimum);
    value = control->minimum + roundf((value - control->minimum) / control->step) * control->step;
    set_value(parameters, control->id, clampf(value, control->minimum, control->maximum));
    return true;
}

void panel_mouse_up(PanelState *panel)
{
    panel->dragging = false;
}