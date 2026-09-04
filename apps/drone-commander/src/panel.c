#include "panel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Layout. Everything is on a grid of two rows and a 20 px margin with a 12 px
 * gutter, so no control can collide with a section border and no panel is left
 * mostly empty. The numbers are here, once, rather than scattered through the
 * drawing code.
 */
#define MARGIN     20.0f
#define ROW1_Y     68.0f
#define ROW1_H     380.0f
#define ROW2_Y     460.0f
#define ROW2_H     268.0f
#define FOOTER_Y   740.0f

#define COL_W      168.0f      /* a column inside the three-column panels */
#define CELL_H     54.0f       /* a fader cell */
#define STEP_H     36.0f       /* a switch or enum cell */
#define TALL_H     62.0f       /* a fader cell in the single-column panels */

enum {
    P_O1_FREQ, P_O1_LEVEL, P_O1_PULSE, P_O1_WAVE,
    P_O2_FREQ, P_O2_LEVEL, P_O2_PULSE, P_O2_WAVE,
    P_O3_FREQ, P_O3_LEVEL, P_O3_PULSE, P_O3_WAVE,
    P_FM_DEPTH, P_FM_ALIAS,
    P_L1_RATE, P_L1_LEVEL,
    P_L2_RATE, P_L2_LEVEL, P_L2_SYNC,
    P_L3_RATE, P_L3_LEVEL, P_L3_SYNC,
    P_VCF_CUTOFF, P_VCF_RES, P_VCF_MOD,
    P_VCA_LEVEL, P_VCA_MOD,
    P_OUT_DRIVE,
    P_COUNT
};

static const char *const WAVE_NAMES[WAVE_COUNT] = { "Sine", "Saw", "Square", "Triangle" };
static const char *const SYNC_NAMES[2] = { "free", "sync" };

/*
 * The parameter table. Every row carries a group and a key, which is the
 * address a preset, a MIDI binding or a sequencer step will use, so none of
 * them has to know where the control sits on screen.
 *
 * Neutral is what reset returns to: the value that makes a control stop acting
 * where there is one (every depth, and FM), and the patch default where there
 * is not (frequencies, levels, cutoff, drive).
 *
 * Frequencies are exponential so both ends of the range are dialable. A 20 Hz
 * to 12 kHz cutoff on a linear track put 115 Hz in a pixel; on this one an
 * equal movement anywhere is an equal musical interval.
 */
static Param PARAMS[P_COUNT] = {
    { .group="osc1", .key="freq",  .label="FREQUENCY", .unit="Hz", .kind=PARAM_FADER,
      .taper=PARAM_EXP, .min=20, .max=2000, .neutral=110.0,   .coarse=10, .fine=1, .ultra=0.1 },
    { .group="osc1", .key="level", .label="LEVEL",     .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0, .max=1, .neutral=0.70 },
    /* Duty cycle of the square. Inert on the other waveforms, which is why it
     * sits below LEVEL rather than next to WAVE. */
    { .group="osc1", .key="pulse", .label="PULSE WIDTH", .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0.02, .max=0.98, .neutral=0.5,
      .coarse=0.01, .fine=0.001, .ultra=0.0001 },
    { .group="osc1", .key="wave",  .label="WAVE",      .kind=PARAM_ENUM,
      .min=0, .max=WAVE_COUNT-1, .neutral=WAVE_SAW, .names=WAVE_NAMES, .nnames=WAVE_COUNT },

    { .group="osc2", .key="freq",  .label="FREQUENCY", .unit="Hz", .kind=PARAM_FADER,
      .taper=PARAM_EXP, .min=20, .max=2000, .neutral=164.81, .coarse=10, .fine=1, .ultra=0.1 },
    { .group="osc2", .key="level", .label="LEVEL",     .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0, .max=1, .neutral=0.55 },
    { .group="osc2", .key="pulse", .label="PULSE WIDTH", .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0.02, .max=0.98, .neutral=0.5,
      .coarse=0.01, .fine=0.001, .ultra=0.0001 },
    { .group="osc2", .key="wave",  .label="WAVE",      .kind=PARAM_ENUM,
      .min=0, .max=WAVE_COUNT-1, .neutral=WAVE_SAW, .names=WAVE_NAMES, .nnames=WAVE_COUNT },

    { .group="osc3", .key="freq",  .label="FREQUENCY", .unit="Hz", .kind=PARAM_FADER,
      .taper=PARAM_EXP, .min=20, .max=2000, .neutral=220.0,  .coarse=10, .fine=1, .ultra=0.1 },
    { .group="osc3", .key="level", .label="LEVEL",     .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0, .max=1, .neutral=0.40 },
    { .group="osc3", .key="pulse", .label="PULSE WIDTH", .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0.02, .max=0.98, .neutral=0.5,
      .coarse=0.01, .fine=0.001, .ultra=0.0001 },
    { .group="osc3", .key="wave",  .label="WAVE",      .kind=PARAM_ENUM,
      .min=0, .max=WAVE_COUNT-1, .neutral=WAVE_SINE, .names=WAVE_NAMES, .nnames=WAVE_COUNT },

    { .group="fm",   .key="depth", .label="FM CASCADE", .unit="Hz", .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0, .max=1000, .neutral=0, .coarse=10, .fine=1, .ultra=0.1 },
    { .group="fm",   .key="alias", .label="ANTI-ALIAS", .kind=PARAM_SWITCH,
      .min=0, .max=1, .neutral=1 },

    /* An LFO level is bipolar (ROADMAP D9): negative is the same depth,
     * inverted, and the neutral in the middle is no modulation at all. */
    { .group="mod1", .key="rate",  .label="RATE",  .unit="Hz", .kind=PARAM_FADER,
      .taper=PARAM_EXP, .min=0.01, .max=20, .neutral=0.35, .coarse=0.1, .fine=0.01, .ultra=0.001 },
    { .group="mod1", .key="level", .label="LEVEL", .kind=PARAM_FADER,
      .taper=PARAM_BIPOLAR, .min=-1, .max=1, .neutral=0 },

    { .group="mod2", .key="rate",  .label="RATE",  .unit="Hz", .kind=PARAM_FADER,
      .taper=PARAM_EXP, .min=0.01, .max=20, .neutral=0.20, .coarse=0.1, .fine=0.01, .ultra=0.001 },
    { .group="mod2", .key="level", .label="LEVEL", .kind=PARAM_FADER,
      .taper=PARAM_BIPOLAR, .min=-1, .max=1, .neutral=0 },
    { .group="mod2", .key="sync",  .label="TO LFO 1", .kind=PARAM_ENUM,
      .min=0, .max=1, .neutral=0, .names=SYNC_NAMES, .nnames=2 },

    { .group="mod3", .key="rate",  .label="RATE",  .unit="Hz", .kind=PARAM_FADER,
      .taper=PARAM_EXP, .min=0.01, .max=20, .neutral=0.12, .coarse=0.1, .fine=0.01, .ultra=0.001 },
    { .group="mod3", .key="level", .label="LEVEL", .kind=PARAM_FADER,
      .taper=PARAM_BIPOLAR, .min=-1, .max=1, .neutral=0 },
    { .group="mod3", .key="sync",  .label="TO LFO 2", .kind=PARAM_ENUM,
      .min=0, .max=1, .neutral=0, .names=SYNC_NAMES, .nnames=2 },

    { .group="vcf",  .key="cutoff", .label="CUTOFF", .unit="Hz", .kind=PARAM_FADER,
      .taper=PARAM_EXP, .min=20, .max=12000, .neutral=1400, .coarse=100, .fine=10, .ultra=1 },
    { .group="vcf",  .key="res",    .label="RESONANCE", .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0, .max=1, .neutral=0.25 },
    { .group="vcf",  .key="moddepth", .label="MOD DEPTH", .unit="Hz", .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0, .max=6000, .neutral=0, .coarse=100, .fine=10, .ultra=1 },

    /* Steps given rather than derived, so this reads to the same four decimals
     * as every other level instead of five off the back of its 0.8 range. */
    { .group="vca",  .key="level",    .label="LEVEL", .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0, .max=0.8, .neutral=0.22,
      .coarse=0.01, .fine=0.001, .ultra=0.0001 },
    { .group="vca",  .key="moddepth", .label="MOD DEPTH", .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0, .max=1, .neutral=0 },

    { .group="out",  .key="drive",    .label="DRIVE", .kind=PARAM_FADER,
      .taper=PARAM_LINEAR, .min=0.1, .max=8, .neutral=1.6, .coarse=0.1, .fine=0.01, .ultra=0.001 },
};

static double VALUES[P_COUNT];

/* Screen geometry, computed once. */
static SDL_FRect BOXES[P_COUNT];
static UiFader   FADERS[P_COUNT];
static int       LAID_OUT = 0;

static const SDL_FRect PANEL_OSC = { MARGIN, ROW1_Y, 556.0f, ROW1_H };
static const SDL_FRect PANEL_MON = { 588.0f, ROW1_Y, 572.0f, ROW1_H };
static const SDL_FRect PANEL_MOD = { MARGIN, ROW2_Y, 556.0f, ROW2_H };
static const SDL_FRect PANEL_VCF = { 588.0f, ROW2_Y, 270.0f, ROW2_H };
static const SDL_FRect PANEL_VCA = { 870.0f, ROW2_Y, 290.0f, ROW2_H };

static const SDL_FRect AUDIO_BOX = { 845.0f, 14.0f, 315.0f, 34.0f };

/* Column origins inside the three-column panels. */
static const float COL_X[3] = { 32.0f, 214.0f, 396.0f };

static const int OSC_FREQ[3]  = { P_O1_FREQ, P_O2_FREQ, P_O3_FREQ };
static const int OSC_LEVEL[3] = { P_O1_LEVEL, P_O2_LEVEL, P_O3_LEVEL };
static const int OSC_PULSE[3] = { P_O1_PULSE, P_O2_PULSE, P_O3_PULSE };
static const int OSC_WAVE[3]  = { P_O1_WAVE, P_O2_WAVE, P_O3_WAVE };
static const int LFO_RATE[3]  = { P_L1_RATE, P_L2_RATE, P_L3_RATE };
static const int LFO_LEVEL[3] = { P_L1_LEVEL, P_L2_LEVEL, P_L3_LEVEL };
static const int LFO_SYNC[3]  = { -1, P_L2_SYNC, P_L3_SYNC };   /* LFO 1 is the master */

/* ---------- text, through SDL's 8x8 debug font ---------- */

static void dbg_draw(void *ud, float x, float y, const char *s, SDL_Color c)
{
    SDL_Renderer *r = ud;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDebugText(r, x, y, s);
}
static float dbg_width(void *ud, const char *s) { (void)ud; return (float)SDL_strlen(s) * 8.0f; }
static float dbg_height(void *ud) { (void)ud; return 8.0f; }

static UiText text_for(SDL_Renderer *r)
{
    UiText t = { dbg_draw, dbg_width, dbg_height, r };
    return t;
}

/* ---------- layout ---------- */

static void place(int index, SDL_FRect box, const UiText *t)
{
    BOXES[index] = box;
    if (PARAMS[index].kind == PARAM_FADER) ui_fader_layout(&FADERS[index], box, UI_H, t);
}

static void layout(const UiText *t)
{
    const float head = dbg_height(NULL) + 12.0f;
    const float osc_y = ROW1_Y + head + 10.0f;      /* 98 */
    const float mod_y = ROW2_Y + head + 10.0f;      /* 438 */

    for (int c = 0; c < 3; c++) {
        place(OSC_FREQ[c],  (SDL_FRect){ COL_X[c], osc_y + 20.0f,  COL_W, CELL_H }, t);
        place(OSC_LEVEL[c], (SDL_FRect){ COL_X[c], osc_y + 82.0f,  COL_W, CELL_H }, t);
        place(OSC_PULSE[c], (SDL_FRect){ COL_X[c], osc_y + 144.0f, COL_W, CELL_H }, t);
        place(OSC_WAVE[c],  (SDL_FRect){ COL_X[c], osc_y + 206.0f, COL_W, STEP_H }, t);
    }
    place(P_FM_DEPTH, (SDL_FRect){ COL_X[0], osc_y + 258.0f, 350.0f, CELL_H }, t);
    place(P_FM_ALIAS, (SDL_FRect){ COL_X[2], osc_y + 267.0f, COL_W, STEP_H }, t);

    for (int c = 0; c < 3; c++) {
        place(LFO_RATE[c],  (SDL_FRect){ COL_X[c], mod_y + 38.0f, COL_W, CELL_H }, t);
        place(LFO_LEVEL[c], (SDL_FRect){ COL_X[c], mod_y + 100.0f, COL_W, CELL_H }, t);
        if (LFO_SYNC[c] >= 0)
            place(LFO_SYNC[c], (SDL_FRect){ COL_X[c], mod_y + 162.0f, COL_W, STEP_H }, t);
    }

    const float vcf_x = PANEL_VCF.x + 12.0f, vcf_w = PANEL_VCF.w - 24.0f;
    place(P_VCF_CUTOFF, (SDL_FRect){ vcf_x, mod_y + 2.0f,   vcf_w, TALL_H }, t);
    place(P_VCF_RES,    (SDL_FRect){ vcf_x, mod_y + 72.0f,  vcf_w, TALL_H }, t);
    place(P_VCF_MOD,    (SDL_FRect){ vcf_x, mod_y + 142.0f, vcf_w, TALL_H }, t);

    const float vca_x = PANEL_VCA.x + 12.0f, vca_w = PANEL_VCA.w - 24.0f;
    place(P_VCA_LEVEL, (SDL_FRect){ vca_x, mod_y + 2.0f,   vca_w, TALL_H }, t);
    place(P_VCA_MOD,   (SDL_FRect){ vca_x, mod_y + 72.0f,  vca_w, TALL_H }, t);
    place(P_OUT_DRIVE, (SDL_FRect){ vca_x, mod_y + 142.0f, vca_w, TALL_H }, t);

    LAID_OUT = 1;
}

/* ---------- engine view ---------- */

void panel_to_parameters(const PanelState *panel, SynthParameters *out)
{
    const double *v = panel->set.values;
    for (int c = 0; c < 3; c++) {
        out->oscillators[c].frequency = (float)v[OSC_FREQ[c]];
        out->oscillators[c].amplitude = (float)v[OSC_LEVEL[c]];
        out->oscillators[c].waveform  = (Waveform)(int)floor(v[OSC_WAVE[c]] + 0.5);
        out->oscillators[c].pulse_width = (float)v[OSC_PULSE[c]];
        out->lfos[c].frequency = (float)v[LFO_RATE[c]];
        out->lfos[c].amplitude = (float)v[LFO_LEVEL[c]];
        out->lfos[c].sync_to_previous =
            LFO_SYNC[c] >= 0 ? (v[LFO_SYNC[c]] >= 0.5) : 0;
    }
    out->fm_depth = (float)v[P_FM_DEPTH];
    out->blep_enabled = v[P_FM_ALIAS] >= 0.5 ? 1.0f : 0.0f;
    out->cutoff = (float)v[P_VCF_CUTOFF];
    out->resonance = (float)v[P_VCF_RES];
    out->cutoff_mod_depth = (float)v[P_VCF_MOD];
    out->vca_amplitude = (float)v[P_VCA_LEVEL];
    out->vca_mod_depth = (float)v[P_VCA_MOD];
    out->drive = (float)v[P_OUT_DRIVE];
}

void panel_from_parameters(PanelState *panel, const SynthParameters *in)
{
    ParamSet *s = &panel->set;
    for (int c = 0; c < 3; c++) {
        paramset_set(s, OSC_FREQ[c], in->oscillators[c].frequency);
        paramset_set(s, OSC_LEVEL[c], in->oscillators[c].amplitude);
        paramset_set(s, OSC_WAVE[c], (double)in->oscillators[c].waveform);
        paramset_set(s, OSC_PULSE[c], in->oscillators[c].pulse_width);
        paramset_set(s, LFO_RATE[c], in->lfos[c].frequency);
        paramset_set(s, LFO_LEVEL[c], in->lfos[c].amplitude);
        if (LFO_SYNC[c] >= 0) paramset_set(s, LFO_SYNC[c], in->lfos[c].sync_to_previous ? 1 : 0);
    }
    paramset_set(s, P_FM_DEPTH, in->fm_depth);
    paramset_set(s, P_FM_ALIAS, in->blep_enabled >= 0.5f ? 1 : 0);
    paramset_set(s, P_VCF_CUTOFF, in->cutoff);
    paramset_set(s, P_VCF_RES, in->resonance);
    paramset_set(s, P_VCF_MOD, in->cutoff_mod_depth);
    paramset_set(s, P_VCA_LEVEL, in->vca_amplitude);
    paramset_set(s, P_VCA_MOD, in->vca_mod_depth);
    paramset_set(s, P_OUT_DRIVE, in->drive);
}

void panel_init(PanelState *panel, const SynthParameters *from)
{
    paramset_init(&panel->set, PARAMS, VALUES, P_COUNT);
    panel->drag = -1;
    panel_from_parameters(panel, from);
}

void panel_describe_selection(const PanelState *panel, char *buf, size_t cap)
{
    paramset_describe(&panel->set, panel->set.sel, buf, cap);
}

/* ---------- events ---------- */

static double step_discrete(const Param *p, double v, int dir)
{
    double next = floor(v + 0.5) + dir;
    if (next > p->max) next = p->min;
    if (next < p->min) next = p->max;
    return next;
}

static int cell_at(float x, float y)
{
    for (int i = 0; i < P_COUNT; i++) {
        if (BOXES[i].w <= 0.0f) continue;
        if (ui_hit(&BOXES[i], x, y)) return i;
    }
    return -1;
}

bool panel_handle_event(PanelState *panel, const SDL_Event *ev, bool *audio_clicked)
{
    ParamSet *s = &panel->set;
    *audio_clicked = false;
    if (!LAID_OUT) return false;

    switch (ev->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        float x = ev->button.x, y = ev->button.y;
        if (ui_hit(&AUDIO_BOX, x, y)) {
            if (ev->button.button == SDL_BUTTON_LEFT) *audio_clicked = true;
            return false;
        }
        int i = cell_at(x, y);
        if (i < 0) return false;
        s->sel = i;

        if (ui_is_reset_click(ev)) { paramset_reset(s, i); return true; }
        if (ev->button.button != SDL_BUTTON_LEFT) return false;

        if (PARAMS[i].kind == PARAM_FADER) {
            panel->drag = i;
            paramset_set(s, i, ui_fader_value_at(&FADERS[i], &PARAMS[i], x, y));
            return true;
        }
        paramset_set(s, i, step_discrete(&PARAMS[i], s->values[i], +1));
        return true;
    }

    case SDL_EVENT_MOUSE_MOTION: {
        int i = panel->drag;
        if (i < 0) return false;
        paramset_set(s, i, ui_fader_value_at(&FADERS[i], &PARAMS[i], ev->motion.x, ev->motion.y));
        return true;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
        panel->drag = -1;
        return false;

    case SDL_EVENT_MOUSE_WHEEL: {
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        int i = cell_at(mx, my);
        if (i < 0) i = s->sel;
        if (i < 0) return false;
        int dir = ev->wheel.y > 0 ? 1 : ev->wheel.y < 0 ? -1 : 0;
        if (dir == 0) return false;
        s->sel = i;
        if (PARAMS[i].kind == PARAM_FADER)
            paramset_nudge(s, i, dir, ui_grain(SDL_GetModState()));
        else
            paramset_set(s, i, step_discrete(&PARAMS[i], s->values[i], dir));
        return true;
    }

    case SDL_EVENT_KEY_DOWN: {
        SDL_Keymod mod = SDL_GetModState();
        int i = s->sel;
        switch (ev->key.key) {
        case SDLK_TAB:
            paramset_select(s, (mod & SDL_KMOD_SHIFT) ? -1 : 1);
            return false;
        case SDLK_BACKSPACE:
            paramset_reset(s, i);
            return true;
        case SDLK_UP:
        case SDLK_RIGHT:
            if (PARAMS[i].kind == PARAM_FADER) paramset_nudge(s, i, 1, ui_grain(mod));
            else paramset_set(s, i, step_discrete(&PARAMS[i], s->values[i], +1));
            return true;
        case SDLK_DOWN:
        case SDLK_LEFT:
            if (PARAMS[i].kind == PARAM_FADER) paramset_nudge(s, i, -1, ui_grain(mod));
            else paramset_set(s, i, step_discrete(&PARAMS[i], s->values[i], -1));
            return true;
        default:
            return false;
        }
    }

    default:
        return false;
    }
}

/* ---------- drawing ---------- */

static void draw_phase(SDL_Renderer *r, const UiText *t, SDL_FRect box, float phase)
{
    const UiTheme *th = ui_theme();
    /* The square LFO is bipolar: its first half is negative, its second
     * positive. One bar, split where the sign changes, with the live half lit. */
    int positive = phase >= 0.5f;
    SDL_FRect left  = { box.x, box.y, box.w * 0.5f, box.h };
    SDL_FRect right = { box.x + box.w * 0.5f, box.y, box.w * 0.5f, box.h };
    ui_fill(r, left,  positive ? th->fill_off : th->warn);
    ui_fill(r, right, positive ? th->ok : th->fill_off);
    float x = box.x + phase * box.w;
    ui_fill(r, (SDL_FRect){ x - 0.5f, box.y - 2.0f, 1.0f, box.h + 4.0f }, th->ink);
    (void)t;
}

static void draw_scope(SDL_Renderer *r, const UiText *t, SDL_FRect area,
                       Synth *preview, const SynthParameters *parameters)
{
    const UiTheme *th = ui_theme();
    /* Two and a half cycles of the lowest oscillator read as a waveform; one
     * cycle reads as a wandering line, which is what a stride of 1 gave. */
    enum { SCOPE_STRIDE = 2, SCOPE_LEAD = 160, SCOPE_SAMPLES = 1600 };
    float samples[SCOPE_SAMPLES];
    SDL_FPoint points[600];
    int width = (int)area.w;
    if (width > 600) width = 600;
    while (SCOPE_LEAD + width * SCOPE_STRIDE >= SCOPE_SAMPLES) width--;

    synth_set_parameters(preview, parameters);
    synth_process(preview, samples, SCOPE_SAMPLES);

    int start = 0;
    for (int i = 0; i < SCOPE_LEAD; ++i)
        if (samples[i] <= 0.0f && samples[i + 1] > 0.0f) { start = i; break; }

    ui_fill(r, area, (SDL_Color){ 10, 13, 12, 255 });

    /* Gridlines at values, not at fractions of the box, so the height of the
     * trace can be read off them. */
    const float mid = area.y + area.h * 0.5f;
    const float full = area.h * 0.44f;      /* pixels per 1.0 of signal */
    static const float MARKS[] = { 1.0f, 0.5f, -0.5f, -1.0f };
    for (int i = 0; i < 4; ++i)
        ui_fill(r, (SDL_FRect){ area.x, mid - MARKS[i] * full, area.w, 1.0f }, th->rule);
    ui_fill(r, (SDL_FRect){ area.x, mid, area.w, 1.0f }, (SDL_Color){ 52, 74, 64, 255 });
    ui_rect(r, area, th->rule);
    if (t->draw) {
        /* No label on the centre line: the trace runs along it. */
        t->draw(t->ud, area.x + 6.0f, mid - full - 10.0f, "+1.0", th->ink_faint);
        t->draw(t->ud, area.x + 6.0f, mid + full + 2.0f, "-1.0", th->ink_faint);
    }

    float peak = 0.0f;
    for (int i = 0; i < width; ++i) {
        float sample = samples[start + i * SCOPE_STRIDE];
        float magnitude = sample < 0.0f ? -sample : sample;
        if (magnitude > peak) peak = magnitude;
        points[i].x = area.x + (float)i;
        points[i].y = mid - sample * full;
    }
    SDL_SetRenderDrawColor(r, th->ok.r, th->ok.g, th->ok.b, 255);
    SDL_RenderLines(r, points, width);

    char label[64];
    snprintf(label, sizeof label, "SIGNAL   peak %.3f", peak);
    if (t->draw) t->draw(t->ud, area.x + area.w - 8.0f * (float)SDL_strlen(label) - 6.0f,
                         area.y + 6.0f, label, peak > 0.98f ? th->warn : th->ink_dim);
}

void panel_render(SDL_Renderer *r, PanelState *panel, Synth *preview, bool audio_enabled)
{
    const UiTheme *th = ui_theme();
    UiText t = text_for(r);
    SynthParameters parameters;

    if (!LAID_OUT) layout(&t);
    panel_to_parameters(panel, &parameters);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    ui_fill(r, (SDL_FRect){ 0, 0, PANEL_WIDTH, PANEL_HEIGHT }, th->bg);

    t.draw(r, MARGIN, 16.0f, "DRONE COMMANDER", th->accent);
    t.draw(r, MARGIN, 30.0f, "three oscillator drone synth", th->ink_faint);

    /* Mute banner: the one control that is not a fader, because it must never
     * be reachable by a drag or a wheel. */
    ui_fill(r, AUDIO_BOX, audio_enabled ? (SDL_Color){ 32, 74, 50, 255 }
                                        : (SDL_Color){ 76, 34, 30, 255 });
    ui_rect(r, AUDIO_BOX, audio_enabled ? th->ok : th->warn);
    t.draw(r, AUDIO_BOX.x + 12.0f, AUDIO_BOX.y + 13.0f,
           audio_enabled ? "AUDIO LIVE   click or space to mute"
                         : "HARD MUTED   click to enable audio",
           audio_enabled ? th->ok : th->warn);

    SDL_FRect osc = ui_panel(r, &t, PANEL_OSC, "OSCILLATORS");
    SDL_FRect mon = ui_panel(r, &t, PANEL_MON, "SIGNAL MONITOR");
    ui_panel(r, &t, PANEL_MOD, "MODULATION   three square LFOs");
    ui_panel(r, &t, PANEL_VCF, "FILTER");
    ui_panel(r, &t, PANEL_VCA, "VCA AND OUTPUT");
    (void)osc;

    static const char *const VCO[3] = { "VCO 1", "VCO 2", "VCO 3" };
    static const char *const LFO[3] = { "LFO 1  master", "LFO 2", "LFO 3" };
    const float osc_y = ROW1_Y + dbg_height(NULL) + 22.0f;
    const float mod_y = ROW2_Y + dbg_height(NULL) + 22.0f;
    for (int c = 0; c < 3; c++) {
        t.draw(r, COL_X[c], osc_y, VCO[c], th->ink_dim);
        t.draw(r, COL_X[c], mod_y, LFO[c], th->ink_dim);
        draw_phase(r, &t, (SDL_FRect){ COL_X[c], mod_y + 16.0f, COL_W, 10.0f },
                   preview->lfos[c].phase);
    }

    for (int i = 0; i < P_COUNT; i++) {
        if (BOXES[i].w <= 0.0f) continue;
        int selected = (panel->set.sel == i);
        if (PARAMS[i].kind == PARAM_FADER)
            ui_fader_draw(r, &t, &FADERS[i], &PARAMS[i], panel->set.values[i], selected);
        else
            ui_stepper_draw(r, &t, BOXES[i], &PARAMS[i], panel->set.values[i], selected);
    }

    draw_scope(r, &t, mon, preview, &parameters);

    t.draw(r, MARGIN, FOOTER_Y,
           "DRAG coarse   WHEEL fine   CTRL+WHEEL coarse   SHIFT+WHEEL ultra   "
           "MIDDLE or DOUBLE CLICK reset   TAB select   ARROWS nudge   R default patch",
           th->ink_faint);

    SDL_RenderPresent(r);
}
