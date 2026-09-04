#include "dsp.h"
#include "panel.h"

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdbool.h>

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

static void set_audio(SDL_AudioStream *stream, SDL_Window *window,
                      bool *audio_enabled, bool enabled)
{
    if (stream == NULL) return;
    if (enabled && SDL_ResumeAudioStreamDevice(stream)) {
        *audio_enabled = true;
        SDL_SetWindowTitle(window, "Drone Commander - AUDIO LIVE");
    } else if (!enabled && SDL_PauseAudioStreamDevice(stream)) {
        *audio_enabled = false;
        SDL_SetWindowTitle(window, "Drone Commander - HARD MUTED");
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
    ControlQueue controls = {.read_index = ATOMIC_VAR_INIT(0U), .write_index = ATOMIC_VAR_INIT(0U)};
    AudioContext audio_context = {0};
    PanelState panel = {0};
    Synth preview;
    bool running = true;
    bool audio_enabled = false;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return 1;
    if (!SDL_CreateWindowAndRenderer("Drone Commander - HARD MUTED", PANEL_WIDTH, PANEL_HEIGHT,
                                     0, &window, &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    synth_init(&preview, SAMPLE_RATE, &parameters);
    synth_init(&audio_context.synth, SAMPLE_RATE, &parameters);
    audio_context.controls = &controls;
    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec,
                                             provide_audio, &audio_context);
    if (audio_stream == NULL) SDL_Log("Audio unavailable; continuing muted: %s", SDL_GetError());

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                bool audio_clicked = false;
                if (panel_mouse_down(&panel, &parameters, event.button.x, event.button.y, &audio_clicked))
                    publish_controls(&controls, &parameters);
                if (audio_clicked) set_audio(audio_stream, window, &audio_enabled, !audio_enabled);
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) panel_mouse_up(&panel);
            else if (event.type == SDL_EVENT_MOUSE_MOTION &&
                     panel_mouse_motion(&panel, &parameters, event.motion.x)) {
                publish_controls(&controls, &parameters);
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_SPACE) set_audio(audio_stream, window, &audio_enabled, false);
                else if (event.key.key == SDLK_A && (event.key.mod & SDL_KMOD_CTRL) &&
                         (event.key.mod & SDL_KMOD_SHIFT)) set_audio(audio_stream, window, &audio_enabled, true);
                else if (event.key.key == SDLK_R) {
                    parameters = synth_default_parameters();
                    publish_controls(&controls, &parameters);
                }
            }
        }
        panel_render(renderer, &preview, &parameters, &panel, audio_enabled);
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