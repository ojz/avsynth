/*
 * vsynth — video feedback synthesizer. See PRD.md.
 *
 * M2: screen region -> fixed rack of libavfilter modules -> borderless window.
 *     Keyboard drives the knobs live.
 *
 *   vsynth [--region X,Y,W,H] [--fps N] [--win X,Y,W,H] [--vf "chain"]
 *
 *   --vf bypasses the rack and runs a raw chain (M1 behaviour).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <libavutil/frame.h>

#include "window.h"
#include "voice.h"
#include "rack.h"

static int parse_rect(const char *s, int *x, int *y, int *w, int *h)
{
    return sscanf(s, "%d,%d,%d,%d", x, y, w, h) == 4 ? 0 : -1;
}

static void show_status(Window *win, const Rack *rack)
{
    char buf[256];
    rack_describe_selected(rack, buf, sizeof buf);
    window_set_title(win, buf);
    fprintf(stderr, "%s\n", buf);
}

int main(int argc, char **argv)
{
    VoiceConfig cfg = { .cap_x = 0, .cap_y = 0, .cap_w = 800, .cap_h = 600,
                        .cap_fps = 30, .filters = NULL };
    int win_x = -1, win_y = -1, win_w = 800, win_h = 600;
    const char *raw_vf = NULL;
    int selftest = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) { selftest = 1; continue; }
        if (!strcmp(argv[i], "--region") && i + 1 < argc) {
            if (parse_rect(argv[++i], &cfg.cap_x, &cfg.cap_y, &cfg.cap_w, &cfg.cap_h)) goto usage;
        } else if (!strcmp(argv[i], "--win") && i + 1 < argc) {
            if (parse_rect(argv[++i], &win_x, &win_y, &win_w, &win_h)) goto usage;
        } else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
            cfg.cap_fps = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--vf") && i + 1 < argc) {
            raw_vf = argv[++i];
        } else {
        usage:
            fprintf(stderr, "usage: %s [--region X,Y,W,H] [--win X,Y,W,H] [--fps N] [--vf CHAIN]\n", argv[0]);
            return 2;
        }
    }

    static Rack rack;
    static char chain[8192];
    rack_init_default(&rack);
    if (raw_vf) {
        cfg.filters = raw_vf;
    } else {
        if (rack_build_chain(&rack, cfg.cap_w, cfg.cap_h, chain, sizeof chain) < 0) {
            fprintf(stderr, "rack chain too long\n");
            return 1;
        }
        cfg.filters = chain;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    Window *win = window_create("vsynth", win_x, win_y, win_w, win_h);
    if (!win) return 1;

    Voice *voice = voice_start(&cfg);
    if (!voice) return 1;

    fprintf(stderr,
        "vsynth M2: capture %d,%d %dx%d @%dfps\n"
        "  mouse: left-drag move, alt+right-drag resize\n"
        "  tab/shift+tab select knob, up/down nudge (shift fine, ctrl coarse)\n"
        "  space bypass module, backspace reset knob, r reset all, f fullscreen, q quit\n",
        cfg.cap_x, cfg.cap_y, cfg.cap_w, cfg.cap_h, cfg.cap_fps);
    show_status(win, &rack);

    if (selftest) {
        /* Push every knob and every enable through the command path once, so
         * any option that does not accept runtime commands shows up in the log. */
        rack_send_all(&rack, voice);
        for (int i = 0; i < rack.ncontrols; i++) {
            rack.sel = i;
            rack_nudge(&rack, voice, +1, 1.0);
            rack_nudge(&rack, voice, -1, 1.0);
        }
        rack.sel = 0;
    }

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
            case SDL_KEYDOWN: {
                SDL_Keymod mod = SDL_GetModState();
                double factor = (mod & KMOD_SHIFT) ? 0.1 : (mod & KMOD_CTRL) ? 10.0 : 1.0;
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    running = 0;
                    break;
                case SDLK_f:
                    window_toggle_fullscreen(win);
                    break;
                case SDLK_TAB:
                    rack_select_next(&rack, (mod & KMOD_SHIFT) ? -1 : 1);
                    show_status(win, &rack);
                    break;
                case SDLK_UP:
                case SDLK_RIGHT:
                    rack_nudge(&rack, voice, +1, factor);
                    show_status(win, &rack);
                    break;
                case SDLK_DOWN:
                case SDLK_LEFT:
                    rack_nudge(&rack, voice, -1, factor);
                    show_status(win, &rack);
                    break;
                case SDLK_SPACE:
                    rack_toggle_selected(&rack, voice);
                    show_status(win, &rack);
                    break;
                case SDLK_BACKSPACE:
                    rack_reset_selected(&rack, voice);
                    show_status(win, &rack);
                    break;
                case SDLK_r:
                    rack_reset_all(&rack, voice);
                    show_status(win, &rack);
                    break;
                default:
                    break;
                }
                break;
            }
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
