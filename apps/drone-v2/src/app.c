/*
 * Drone V2: five triangle voices, each a frequency and a level, summed. No
 * modulation, no FM, no filter, nothing else. The first app written on the
 * finished shell (ROADMAP P5), and the proof of its exit criterion: a window
 * with working faders in about two hundred lines of its own code.
 *
 * It opens its own audio device, as Drone Commander does, until P6 gives
 * every app a bus and MONITOR the one device. Audio starts hard-muted and
 * takes a deliberate gesture to enable, like every app in the lab.
 */
#include "dronev2_app.h"

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOICES       5
#define NPARAMS      (VOICES * 2)
#define SAMPLE_RATE  48000
#define CHUNK        512

#define WIN_W        900
#define WIN_H        300
#define MARGIN       20.0f
#define COL_W        160.0f
#define COL_GAP      12.0f
#define CELL_H       54.0f

/* Audio-thread state of one voice. The targets it glides towards come from
 * the panel through the atomics below; the one-pole glide is what keeps a
 * fader move from clicking or zippering. */
typedef struct Voice {
    double phase;
    float  freq, amp;
} Voice;

typedef struct Drone2 {
    AppHost  *host;
    Param     params[NPARAMS];
    double    values[NPARAMS];
    ParamSet  set;
    UiControl controls[NPARAMS];
    UiSurface surface;
    int       laid_out;

    _Atomic float target[NPARAMS];   /* panel thread writes, audio thread reads */
    Voice     voice[VOICES];
    SDL_AudioStream *stream;
    bool      audio_on;
    char      title[160];
} Drone2;

/* A harmonic stack on A: each voice's neutral, so reset lands on a chord. */
static const double DEFAULT_HZ[VOICES] = { 55.0, 82.41, 110.0, 164.81, 220.0 };

static const SDL_FRect BANNER = { WIN_W - 335.0f, 14.0f, 315.0f, 34.0f };
static const SDL_FRect PANEL  = { MARGIN, 68.0f, WIN_W - 2 * MARGIN, 192.0f };

/* ---------- audio ---------- */

static float triangle(double phase)
{
    return (float)(4.0 * fabs(phase - 0.5) - 1.0);   /* -1 at 0, +1 at 0.5 */
}

static void SDLCALL provide_audio(void *ud, SDL_AudioStream *stream, int additional, int total)
{
    Drone2 *d = ud;
    float buf[CHUNK];
    int left = additional / (int)sizeof(float);
    (void)total;
    while (left > 0) {
        int n = left < CHUNK ? left : CHUNK;
        for (int i = 0; i < n; i++) {
            float sum = 0.0f;
            for (int v = 0; v < VOICES; v++) {
                Voice *o = &d->voice[v];
                float tf = atomic_load_explicit(&d->target[v * 2], memory_order_relaxed);
                float ta = atomic_load_explicit(&d->target[v * 2 + 1], memory_order_relaxed);
                o->freq += (tf - o->freq) * 0.0005f;
                o->amp  += (ta - o->amp)  * 0.0005f;
                o->phase += o->freq / SAMPLE_RATE;
                if (o->phase >= 1.0) o->phase -= 1.0;
                sum += o->amp * triangle(o->phase);
            }
            buf[i] = sum * (1.0f / VOICES);   /* five voices at full level meet at 1.0 */
        }
        if (!SDL_PutAudioStreamData(stream, buf, n * (int)sizeof(float))) return;
        left -= n;
    }
}

static void set_audio(Drone2 *d, bool on)
{
    if (!d->stream) return;
    if (on && SDL_ResumeAudioStreamDevice(d->stream)) d->audio_on = true;
    else if (!on && SDL_PauseAudioStreamDevice(d->stream)) d->audio_on = false;
}

static void publish(Drone2 *d)
{
    for (int i = 0; i < NPARAMS; i++)
        atomic_store_explicit(&d->target[i], (float)d->values[i], memory_order_relaxed);
}

/* ---------- the panel ---------- */

static void layout(Drone2 *d)
{
    const float lh = app_text_height(d->host);
    const float top = PANEL.y + lh + 12.0f + 10.0f + lh + 8.0f;   /* below the title bar and the voice name */
    for (int v = 0; v < VOICES; v++) {
        float x = PANEL.x + 12.0f + v * (COL_W + COL_GAP);
        for (int k = 0; k < 2; k++) {
            UiControl *c = &d->controls[v * 2 + k];
            c->param = v * 2 + k;
            c->box = (SDL_FRect){ x, top + k * (CELL_H + 8.0f), COL_W, CELL_H };
            ui_fader_layout(&c->fader, c->box, UI_H, &d->host->text);
        }
    }
    d->surface.items = d->controls;
    d->surface.n = NPARAMS;
    d->laid_out = 1;
}

static void drone2_frame(void *self, SDL_Renderer *r)
{
    Drone2 *d = self;
    const UiTheme *th = ui_theme();
    const UiText *t = &d->host->text;
    const float lh = app_text_height(d->host);
    if (!d->laid_out) layout(d);

    char sel[96];
    paramset_describe(&d->set, d->set.sel, sel, sizeof sel);
    snprintf(d->title, sizeof d->title, "Drone V2  %s  %s", d->audio_on ? "AUDIO LIVE" : "HARD MUTED", sel);
    SDL_SetWindowTitle(d->host->window, d->title);

    t->draw(t->ud, MARGIN, 12.0f, "DRONE V2", th->accent);
    t->draw(t->ud, MARGIN, 12.0f + lh + 2.0f, "five triangle voices, nothing else", th->ink_faint);

    ui_fill(r, BANNER, d->audio_on ? (SDL_Color){ 32, 74, 50, 255 } : (SDL_Color){ 76, 34, 30, 255 });
    ui_rect(r, BANNER, d->audio_on ? th->ok : th->warn);
    t->draw(t->ud, BANNER.x + 12.0f, BANNER.y + (BANNER.h - lh) * 0.5f,
            d->audio_on ? "AUDIO LIVE   click or space to mute" : "HARD MUTED   click to enable audio",
            d->audio_on ? th->ok : th->warn);

    ui_panel(r, t, PANEL, "VOICES");
    for (int v = 0; v < VOICES; v++) {
        char name[16];
        snprintf(name, sizeof name, "VOICE %d", v + 1);
        t->draw(t->ud, d->controls[v * 2].box.x, d->controls[v * 2].box.y - lh - 8.0f, name, th->ink_dim);
    }
    for (int i = 0; i < NPARAMS; i++)
        ui_fader_draw(r, t, &d->controls[i].fader, &d->params[i], d->values[i], d->set.sel == i);

    t->draw(t->ud, MARGIN, WIN_H - lh - 10.0f,
            "DRAG set   WHEEL fine   CTRL coarse   SHIFT ultra   MIDDLE CLICK reset   "
            "TAB select   ARROWS nudge   R chord   SPACE mute",
            th->ink_faint);
}

/* ---------- the AppSpec ---------- */

static void *drone2_create(AppHost *host, int argc, char **argv)
{
    (void)argc; (void)argv;
    Drone2 *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->host = host;

    /* Two faders per voice. Frequency is exponential so the low end is as
     * dialable as the high; the written chord is each voice's neutral. */
    for (int v = 0; v < VOICES; v++) {
        Param *f = &d->params[v * 2], *a = &d->params[v * 2 + 1];
        snprintf(f->group, sizeof f->group, "v%d", v + 1);
        snprintf(f->key, sizeof f->key, "freq");
        snprintf(f->label, sizeof f->label, "FREQUENCY");
        snprintf(f->unit, sizeof f->unit, "Hz");
        f->kind = PARAM_FADER; f->taper = PARAM_EXP;
        f->min = 20.0; f->max = 2000.0; f->neutral = DEFAULT_HZ[v];
        f->coarse = 10.0; f->fine = 1.0; f->ultra = 0.1;

        snprintf(a->group, sizeof a->group, "v%d", v + 1);
        snprintf(a->key, sizeof a->key, "level");
        snprintf(a->label, sizeof a->label, "LEVEL");
        a->kind = PARAM_FADER; a->taper = PARAM_LINEAR;
        a->min = 0.0; a->max = 1.0; a->neutral = 0.5;

        d->values[v * 2] = f->neutral;
        d->values[v * 2 + 1] = a->neutral;
        d->voice[v].freq = (float)f->neutral;   /* start on the chord, not gliding up from silence */
        d->voice[v].amp = 0.0f;
        d->voice[v].phase = (double)v / VOICES;  /* spread phases so the sum does not start at a peak */
    }
    paramset_init(&d->set, d->params, d->values, NPARAMS);
    publish(d);

    SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, SAMPLE_RATE };
    d->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, provide_audio, d);
    if (!d->stream) SDL_Log("Audio unavailable; continuing muted: %s", SDL_GetError());
    return d;
}

static void drone2_destroy(void *self)
{
    Drone2 *d = self;
    if (d->stream) { SDL_PauseAudioStreamDevice(d->stream); SDL_DestroyAudioStream(d->stream); }
    free(d);
}

static bool drone2_event(void *self, const SDL_Event *ev)
{
    Drone2 *d = self;
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
        SDL_Keymod mod = SDL_GetModState();
        switch (ev->key.key) {
        case SDLK_ESCAPE: d->host->quit = true; return true;
        case SDLK_SPACE:  set_audio(d, false); return true;
        case SDLK_A:
            if ((mod & SDL_KMOD_CTRL) && (mod & SDL_KMOD_SHIFT)) { set_audio(d, true); return true; }
            return false;
        case SDLK_R:
            if (mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) return false;
            paramset_reset_all(&d->set);
            publish(d);
            return true;
        default: return false;
        }
    }
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev->button.button == SDL_BUTTON_LEFT &&
        ui_hit(&BANNER, ev->button.x, ev->button.y)) {
        set_audio(d, !d->audio_on);
        return true;
    }
    return false;
}

static void drone2_changed(void *self, int param) { if (param >= 0) publish(self); }
static ParamSet *drone2_params(void *self) { return &((Drone2 *)self)->set; }
static const UiSurface *drone2_surface(void *self) { return &((Drone2 *)self)->surface; }

const AppSpec DRONE_V2 = {
    .name = "drone-v2",
    .title = "Drone V2  HARD MUTED",
    .window_w = WIN_W,
    .window_h = WIN_H,
    .init_flags = SDL_INIT_AUDIO,
    .create = drone2_create,
    .destroy = drone2_destroy,
    .event = drone2_event,
    .frame = drone2_frame,
    .params = drone2_params,
    .surface = drone2_surface,
    .changed = drone2_changed,
};
