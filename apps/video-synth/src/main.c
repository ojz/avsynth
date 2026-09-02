/*
 * vsynth — video feedback synthesizer. See PRD.md.
 *
 * M3: screen region -> fixed rack of libavfilter modules -> borderless window.
 *     Keyboard drives the knobs live. Patches live in a SQLite project file.
 *
 *   vsynth [--project FILE] [--region X,Y,W,H] [--fps N] [--win X,Y,W,H]
 *          [--vf "chain"] [--selftest]
 *
 *   Geometry comes from the project file when present; CLI flags override it.
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
#include "project.h"

static int parse_rect(const char *s, int *x, int *y, int *w, int *h)
{
    return sscanf(s, "%d,%d,%d,%d", x, y, w, h) == 4 ? 0 : -1;
}

static void show_status(Window *win, const Rack *rack, int patch_slot)
{
    char buf[256], title[320];
    rack_describe_selected(rack, buf, sizeof buf);
    if (patch_slot > 0)
        snprintf(title, sizeof title, "P%d  %s", patch_slot, buf);
    else
        snprintf(title, sizeof title, "%s", buf);
    window_set_title(win, title);
    fprintf(stderr, "%s\n", title);
}

static void notice(Window *win, const char *msg)
{
    window_set_title(win, msg);
    fprintf(stderr, "%s\n", msg);
}

/* 1..9 -> 1..9, 0 -> 10, else 0 */
static int slot_for_key(SDL_Keycode k)
{
    if (k >= SDLK_1 && k <= SDLK_9) return (int)(k - SDLK_1) + 1;
    if (k == SDLK_0) return 10;
    return 0;
}

int main(int argc, char **argv)
{
    VoiceConfig cfg = { .cap_x = 0, .cap_y = 0, .cap_w = 800, .cap_h = 600,
                        .cap_fps = 30, .filters = NULL };
    int win_x = -1, win_y = -1, win_w = 800, win_h = 600;
    int cli_region = 0, cli_win = 0, cli_fps = 0;
    const char *raw_vf = NULL;
    const char *project_file = "default.vsynth";
    int selftest = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) { selftest = 1; continue; }
        if (!strcmp(argv[i], "--project") && i + 1 < argc) {
            project_file = argv[++i];
        } else if (!strcmp(argv[i], "--region") && i + 1 < argc) {
            if (parse_rect(argv[++i], &cfg.cap_x, &cfg.cap_y, &cfg.cap_w, &cfg.cap_h)) goto usage;
            cli_region = 1;
        } else if (!strcmp(argv[i], "--win") && i + 1 < argc) {
            if (parse_rect(argv[++i], &win_x, &win_y, &win_w, &win_h)) goto usage;
            cli_win = 1;
        } else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
            cfg.cap_fps = atoi(argv[++i]);
            cli_fps = 1;
        } else if (!strcmp(argv[i], "--vf") && i + 1 < argc) {
            raw_vf = argv[++i];
        } else {
        usage:
            fprintf(stderr, "usage: %s [--project FILE] [--region X,Y,W,H] [--win X,Y,W,H] "
                            "[--fps N] [--vf CHAIN] [--selftest]\n", argv[0]);
            return 2;
        }
    }

    static Rack rack;
    static char chain[8192];
    rack_init_default(&rack);

    Project *proj = project_open(project_file, &rack);
    if (!proj) return 1;

    Geometry geo;
    project_load_geometry(proj, &geo);
    if (geo.valid) {
        if (!cli_region) { cfg.cap_x = geo.cap_x; cfg.cap_y = geo.cap_y;
                           cfg.cap_w = geo.cap_w; cfg.cap_h = geo.cap_h; }
        if (!cli_fps)    cfg.cap_fps = geo.cap_fps > 0 ? geo.cap_fps : cfg.cap_fps;
        if (!cli_win)    { win_x = geo.win_x; win_y = geo.win_y; win_w = geo.win_w; win_h = geo.win_h; }
    }

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
        "vsynth M3: capture %d,%d %dx%d @%dfps\n"
        "  mouse: left-drag move, alt+right-drag resize\n"
        "  tab/shift+tab select knob, up/down nudge (shift fine, ctrl coarse)\n"
        "  space bypass module, backspace reset knob, r reset all\n"
        "  1-9,0 load patch slot, shift+1-9,0 save slot, f fullscreen, q quit\n",
        cfg.cap_x, cfg.cap_y, cfg.cap_w, cfg.cap_h, cfg.cap_fps);

    int patch_slot = 0;   /* last loaded/saved slot, for the title */
    show_status(win, &rack, patch_slot);

    if (selftest) {
        /* Push every knob and every enable through the command path once, so
         * any option that does not accept runtime commands shows up in the log.
         * Then round-trip a patch through the project file. */
        rack_send_all(&rack, voice);
        for (int i = 0; i < rack.ncontrols; i++) {
            rack.sel = i;
            rack_nudge(&rack, voice, +1, 1.0);
            rack_nudge(&rack, voice, -1, 1.0);
        }
        rack.sel = 0;
        rack.values[0][0] = 7;          /* shift.rh */
        rack.enabled[rack.nmods - 1] = 1;   /* noise on */
        if (project_save_patch(proj, 10, "selftest", &rack) < 0) fprintf(stderr, "selftest: save failed\n");
        rack_reset_all(&rack, voice);
        int rc = project_load_patch(proj, 10, &rack);
        fprintf(stderr, "selftest: patch round-trip %s (rh=%g noise=%d)\n",
                rc == 0 && rack.values[0][0] == 7 && rack.enabled[rack.nmods - 1] ? "OK" : "FAILED",
                rack.values[0][0], rack.enabled[rack.nmods - 1]);
        rack_send_all(&rack, voice);
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
                SDL_Keycode key = ev.key.keysym.sym;
                int slot = slot_for_key(key);
                if (slot) {
                    char msg[128];
                    if (mod & KMOD_SHIFT) {
                        snprintf(msg, sizeof msg, "patch %d", slot);
                        if (project_save_patch(proj, slot, msg, &rack) == 0) {
                            patch_slot = slot;
                            snprintf(msg, sizeof msg, "saved patch %d", slot);
                        } else {
                            snprintf(msg, sizeof msg, "save to slot %d FAILED", slot);
                        }
                    } else {
                        int rc = project_load_patch(proj, slot, &rack);
                        if (rc == 0) {
                            rack_send_all(&rack, voice);
                            patch_slot = slot;
                            snprintf(msg, sizeof msg, "loaded patch %d", slot);
                        } else if (rc == 1) {
                            snprintf(msg, sizeof msg, "slot %d is empty (shift+%d saves)", slot, slot % 10);
                        } else {
                            snprintf(msg, sizeof msg, "load slot %d FAILED", slot);
                        }
                    }
                    notice(win, msg);
                    break;
                }
                switch (key) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    running = 0;
                    break;
                case SDLK_f:
                    window_toggle_fullscreen(win);
                    break;
                case SDLK_TAB:
                    rack_select_next(&rack, (mod & KMOD_SHIFT) ? -1 : 1);
                    show_status(win, &rack, patch_slot);
                    break;
                case SDLK_UP:
                case SDLK_RIGHT:
                    rack_nudge(&rack, voice, +1, factor);
                    show_status(win, &rack, patch_slot);
                    break;
                case SDLK_DOWN:
                case SDLK_LEFT:
                    rack_nudge(&rack, voice, -1, factor);
                    show_status(win, &rack, patch_slot);
                    break;
                case SDLK_SPACE:
                    rack_toggle_selected(&rack, voice);
                    show_status(win, &rack, patch_slot);
                    break;
                case SDLK_BACKSPACE:
                    rack_reset_selected(&rack, voice);
                    show_status(win, &rack, patch_slot);
                    break;
                case SDLK_r:
                    rack_reset_all(&rack, voice);
                    patch_slot = 0;
                    show_status(win, &rack, patch_slot);
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

    Geometry out = { cfg.cap_x, cfg.cap_y, cfg.cap_w, cfg.cap_h, cfg.cap_fps, 0, 0, 0, 0, 1 };
    window_get_geometry(win, &out.win_x, &out.win_y, &out.win_w, &out.win_h);
    project_save_geometry(proj, &out);
    fprintf(stderr, "saved geometry: capture %d,%d %dx%d window %d,%d %dx%d\n",
            out.cap_x, out.cap_y, out.cap_w, out.cap_h, out.win_x, out.win_y, out.win_w, out.win_h);

    voice_stop(voice);
    window_destroy(win);
    project_close(proj);
    SDL_Quit();
    return 0;
}
