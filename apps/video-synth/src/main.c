/*
 * vsynth — video feedback synthesizer. See PRD.md.
 *
 * M1: screen region -> libavfilter graph -> borderless window that keeps
 *     drawing while moved/resized.
 *
 *   vsynth [--region X,Y,W,H] [--fps N] [--vf "filterchain"] [--win X,Y,W,H]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <libavutil/frame.h>

#include "window.h"
#include "voice.h"

static const char *DEFAULT_VF =
    "scale=iw*1.02:ih*1.02,crop=iw/1.02:ih/1.02,"
    "rotate@rot=0.8*PI/180:c=black:ow=iw:oh=ih,"
    "hue@hue=h=t*25:s=1.15";

static int parse_rect(const char *s, int *x, int *y, int *w, int *h)
{
    return sscanf(s, "%d,%d,%d,%d", x, y, w, h) == 4 ? 0 : -1;
}

int main(int argc, char **argv)
{
    VoiceConfig cfg = { .cap_x = 0, .cap_y = 0, .cap_w = 800, .cap_h = 600,
                        .cap_fps = 30, .filters = DEFAULT_VF };
    int win_x = -1, win_y = -1, win_w = 800, win_h = 600;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--region") && i + 1 < argc) {
            if (parse_rect(argv[++i], &cfg.cap_x, &cfg.cap_y, &cfg.cap_w, &cfg.cap_h)) goto usage;
        } else if (!strcmp(argv[i], "--win") && i + 1 < argc) {
            if (parse_rect(argv[++i], &win_x, &win_y, &win_w, &win_h)) goto usage;
        } else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
            cfg.cap_fps = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--vf") && i + 1 < argc) {
            cfg.filters = argv[++i];
        } else {
        usage:
            fprintf(stderr, "usage: %s [--region X,Y,W,H] [--win X,Y,W,H] [--fps N] [--vf CHAIN]\n", argv[0]);
            return 2;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    Window *win = window_create("vsynth", win_x, win_y, win_w, win_h);
    if (!win) return 1;

    Voice *voice = voice_start(&cfg);
    if (!voice) return 1;

    fprintf(stderr, "vsynth M1: capture %d,%d %dx%d @%dfps\n"
                    "  left-drag moves, alt+right-drag resizes, f fullscreen, q/esc quits\n",
            cfg.cap_x, cfg.cap_y, cfg.cap_w, cfg.cap_h, cfg.cap_fps);

    int running = 1;
    int frames = 0;
    Uint32 fps_t0 = SDL_GetTicks();
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (window_handle_event(win, &ev))
                continue;
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    running = 0;
                    break;
                case SDLK_f:
                    window_toggle_fullscreen(win);
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }

        if (voice_failed(voice)) {
            fprintf(stderr, "voice thread failed; exiting\n");
            running = 0;
        }

        AVFrame *f = voice_take_frame(voice);
        if (f) {
            window_present_frame(win, f->data[0], f->width, f->height, f->linesize[0]);
            av_frame_free(&f);
            frames++;
            Uint32 now = SDL_GetTicks();
            if (now - fps_t0 >= 2000) {
                fprintf(stderr, "fps: %.1f\n", frames * 1000.0 / (now - fps_t0));
                frames = 0;
                fps_t0 = now;
            }
        } else {
            window_present(win);   /* vsync paces this; keeps window painted during drags */
            SDL_Delay(1);
        }
    }

    int x, y, w, h;
    window_get_geometry(win, &x, &y, &w, &h);
    fprintf(stderr, "window geometry at exit: %d,%d %dx%d\n", x, y, w, h);

    voice_stop(voice);
    window_destroy(win);
    SDL_Quit();
    return 0;
}
