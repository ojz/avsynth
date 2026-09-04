#include "dsp.h"
#include "panel.h"

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
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

static void set_audio(SDL_AudioStream *stream, bool *audio_enabled, bool enabled)
{
    if (stream == NULL) return;
    if (enabled && SDL_ResumeAudioStreamDevice(stream)) *audio_enabled = true;
    else if (!enabled && SDL_PauseAudioStreamDevice(stream)) *audio_enabled = false;
}

/* The title carries the selected control and its value, so a precise setting
 * can be read even while the pointer is somewhere else. */
static void refresh_title(SDL_Window *window, const PanelState *panel, bool audio_enabled)
{
    static char last[256] = "";
    char selection[128], title[256];
    panel_describe_selection(panel, selection, sizeof selection);
    snprintf(title, sizeof title, "Drone Commander  %s  %s",
             audio_enabled ? "AUDIO LIVE" : "HARD MUTED", selection);
    if (strcmp(title, last) != 0) {
        SDL_SetWindowTitle(window, title);
        snprintf(last, sizeof last, "%s", title);
    }
}

int main(void)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_AudioStream *audio_stream;
    SDL_AudioSpec audio_spec = {SDL_AUDIO_F32, 1, SAMPLE_RATE};
    SDL_Event event;
    SynthParameters parameters = synth_default_parameters();
    ControlQueue controls = {.read_index = 0U, .write_index = 0U};
    AudioContext audio_context = {0};
    PanelState panel;
    Synth preview;
    bool running = true;
    bool audio_enabled = false;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return 1;
    if (!SDL_CreateWindowAndRenderer("Drone Commander  HARD MUTED", PANEL_WIDTH, PANEL_HEIGHT,
                                     0, &window, &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    panel_init(&panel, &parameters);
    synth_init(&preview, SAMPLE_RATE, &parameters);
    synth_init(&audio_context.synth, SAMPLE_RATE, &parameters);
    audio_context.controls = &controls;
    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec,
                                             provide_audio, &audio_context);
    if (audio_stream == NULL) SDL_Log("Audio unavailable; continuing muted: %s", SDL_GetError());

    while (running) {
        while (SDL_PollEvent(&event)) {
            bool audio_clicked = false;

            if (event.type == SDL_EVENT_QUIT) { running = false; break; }

            /* App-level keys first, so the panel never sees them. Repeats are
             * ignored here but passed to the panel, where holding an arrow to
             * nudge is exactly what you want. */
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                SDL_Keymod mod = SDL_GetModState();
                if (event.key.key == SDLK_ESCAPE) { running = false; break; }
                if (event.key.key == SDLK_SPACE) {
                    set_audio(audio_stream, &audio_enabled, false);
                    continue;
                }
                if (event.key.key == SDLK_A && (mod & SDL_KMOD_CTRL) && (mod & SDL_KMOD_SHIFT)) {
                    set_audio(audio_stream, &audio_enabled, true);
                    continue;
                }
                if (event.key.key == SDLK_R && !(mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT))) {
                    parameters = synth_default_parameters();
                    panel_from_parameters(&panel, &parameters);
                    publish_controls(&controls, &parameters);
                    continue;
                }
            }

            if (panel_handle_event(&panel, &event, &audio_clicked)) {
                panel_to_parameters(&panel, &parameters);
                publish_controls(&controls, &parameters);
            }
            if (audio_clicked) set_audio(audio_stream, &audio_enabled, !audio_enabled);
        }

        refresh_title(window, &panel, audio_enabled);
        panel_render(renderer, &panel, &preview, audio_enabled);
        SDL_Delay(16);
    }

    if (audio_stream != NULL) {
        SDL_PauseAudioStreamDevice(audio_stream);
        SDL_DestroyAudioStream(audio_stream);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
