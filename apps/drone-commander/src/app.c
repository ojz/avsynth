/*
 * Drone Commander as an app on the lab's shell (ROADMAP D14). The shell owns
 * the window, the frame loop and every fader gesture; this file owns the
 * synth, the audio device, the mute banner and the four keys that are not
 * fader gestures. Everything it knows lives in a Drone it allocates (D12).
 */
#include "drone_app.h"
#include "dsp.h"
#include "panel.h"

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 48000
#define AUDIO_CHUNK_FRAMES 1024
#define CONTROL_QUEUE_CAPACITY 64

typedef struct {
    SynthParameters entries[CONTROL_QUEUE_CAPACITY];
    atomic_uint read_index;
    atomic_uint write_index;
} ControlQueue;

typedef struct {
    Synth synth;
    ControlQueue *controls;
} AudioContext;

typedef struct Drone {
    AppHost         *host;
    PanelState       panel;
    SynthParameters  parameters;
    Synth            preview;        /* feeds the oscilloscope; never the speakers */
    ControlQueue     controls;
    AudioContext     audio;
    SDL_AudioStream *stream;
    bool             audio_enabled;
    char             last_title[256];
} Drone;

/* ---------- control queue: panel thread to audio thread, lock-free ---------- */

static void publish_controls(ControlQueue *queue, const SynthParameters *parameters)
{
    unsigned int write_index = atomic_load_explicit(&queue->write_index, memory_order_relaxed);
    unsigned int next_index = (write_index + 1U) % CONTROL_QUEUE_CAPACITY;
    unsigned int read_index = atomic_load_explicit(&queue->read_index, memory_order_acquire);
    if (next_index == read_index) return;
    queue->entries[write_index] = *parameters;
    atomic_store_explicit(&queue->write_index, next_index, memory_order_release);
}

static bool consume_controls(ControlQueue *queue, SynthParameters *parameters)
{
    unsigned int read_index = atomic_load_explicit(&queue->read_index, memory_order_relaxed);
    unsigned int write_index = atomic_load_explicit(&queue->write_index, memory_order_acquire);
    bool consumed = false;
    while (read_index != write_index) {
        *parameters = queue->entries[read_index];
        read_index = (read_index + 1U) % CONTROL_QUEUE_CAPACITY;
        consumed = true;
    }
    if (consumed) atomic_store_explicit(&queue->read_index, read_index, memory_order_release);
    return consumed;
}

static void SDLCALL provide_audio(void *userdata, SDL_AudioStream *stream,
                                  int additional_amount, int total_amount)
{
    AudioContext *context = userdata;
    float samples[AUDIO_CHUNK_FRAMES];
    int frames_remaining = additional_amount / (int)sizeof(float);
    SynthParameters parameters;
    (void)total_amount;

    if (consume_controls(context->controls, &parameters)) synth_set_parameters(&context->synth, &parameters);
    while (frames_remaining > 0) {
        int frames = frames_remaining < AUDIO_CHUNK_FRAMES ? frames_remaining : AUDIO_CHUNK_FRAMES;
        synth_process(&context->synth, samples, (size_t)frames);
        if (!SDL_PutAudioStreamData(stream, samples, frames * (int)sizeof(float))) return;
        frames_remaining -= frames;
    }
}

static void set_audio(Drone *d, bool enabled)
{
    if (d->stream == NULL) return;
    if (enabled && SDL_ResumeAudioStreamDevice(d->stream)) d->audio_enabled = true;
    else if (!enabled && SDL_PauseAudioStreamDevice(d->stream)) d->audio_enabled = false;
}

static void apply(Drone *d)
{
    panel_to_parameters(&d->panel, &d->parameters);
    publish_controls(&d->controls, &d->parameters);
}

/* ---------- the AppSpec ---------- */

static void *drone_create(AppHost *host, int argc, char **argv)
{
    (void)argc; (void)argv;
    Drone *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->host = host;
    d->parameters = synth_default_parameters();
    panel_init(&d->panel, host, &d->parameters);
    synth_init(&d->preview, SAMPLE_RATE, &d->parameters);
    synth_init(&d->audio.synth, SAMPLE_RATE, &d->parameters);
    d->audio.controls = &d->controls;

    /* Opened paused: audio always starts hard-muted and takes a deliberate
     * keypress or click to enable. */
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, SAMPLE_RATE };
    d->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, provide_audio, &d->audio);
    if (d->stream == NULL) SDL_Log("Audio unavailable; continuing muted: %s", SDL_GetError());
    return d;
}

static void drone_destroy(void *self)
{
    Drone *d = self;
    if (d->stream != NULL) {
        SDL_PauseAudioStreamDevice(d->stream);
        SDL_DestroyAudioStream(d->stream);
    }
    free(d);
}

/* App-level keys, none of them a fader gesture. Repeats are ignored: holding
 * space should not toggle anything twice. */
static bool drone_event(void *self, const SDL_Event *ev)
{
    Drone *d = self;
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
        SDL_Keymod mod = SDL_GetModState();
        switch (ev->key.key) {
        case SDLK_ESCAPE:
            d->host->quit = true;
            return true;
        case SDLK_SPACE:
            set_audio(d, false);
            return true;
        case SDLK_A:
            if ((mod & SDL_KMOD_CTRL) && (mod & SDL_KMOD_SHIFT)) { set_audio(d, true); return true; }
            return false;
        case SDLK_R:
            if (mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) return false;
            d->parameters = synth_default_parameters();
            panel_from_parameters(&d->panel, &d->parameters);
            publish_controls(&d->controls, &d->parameters);
            return true;
        default:
            return false;
        }
    }
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev->button.button == SDL_BUTTON_LEFT &&
        panel_banner_hit(&d->panel, ev->button.x, ev->button.y)) {
        set_audio(d, !d->audio_enabled);
        return true;
    }
    return false;
}

static void drone_changed(void *self, int param)
{
    if (param >= 0) apply(self);
}

static ParamSet *drone_params(void *self) { return &((Drone *)self)->panel.set; }
static const UiSurface *drone_surface(void *self) { return &((Drone *)self)->panel.surface; }

/* The title carries the selected control and its value, so a precise setting
 * can be read even while the pointer is somewhere else. */
static void refresh_title(Drone *d)
{
    char selection[128], title[256];
    panel_describe_selection(&d->panel, selection, sizeof selection);
    snprintf(title, sizeof title, "Drone Commander  %s  %s",
             d->audio_enabled ? "AUDIO LIVE" : "HARD MUTED", selection);
    if (strcmp(title, d->last_title) != 0) {
        SDL_SetWindowTitle(d->host->window, title);
        snprintf(d->last_title, sizeof d->last_title, "%s", title);
    }
}

static void drone_frame(void *self, SDL_Renderer *r)
{
    Drone *d = self;
    refresh_title(d);
    panel_render(r, &d->panel, &d->preview, d->audio_enabled);
}

const AppSpec DRONE_COMMANDER = {
    .name = "drone-commander",
    .title = "Drone Commander  HARD MUTED",
    .window_w = PANEL_WIDTH,
    .window_h = PANEL_HEIGHT,
    .init_flags = SDL_INIT_AUDIO,
    .create = drone_create,
    .destroy = drone_destroy,
    .event = drone_event,
    .frame = drone_frame,
    .params = drone_params,
    .surface = drone_surface,
    .changed = drone_changed,
};
