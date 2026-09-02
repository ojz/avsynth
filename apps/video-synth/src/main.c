/*
 * vsynth — video feedback synthesizer. See PRD.md.
 *
 * M0: borderless window that keeps drawing while moved/resized.
 *     A moving test pattern stands in for the capture graph until M1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <SDL2/SDL.h>

#include "window.h"

#define TEST_W 320
#define TEST_H 240

static void fill_test_pattern(uint8_t *bgra, int w, int h, double t)
{
    for (int y = 0; y < h; y++) {
        uint8_t *row = bgra + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            double u = (double)x / w, v = (double)y / h;
            double r = 0.5 + 0.5 * sin(6.28 * (u + t * 0.3));
            double g = 0.5 + 0.5 * sin(6.28 * (v - t * 0.2));
            double b = 0.5 + 0.5 * sin(6.28 * (u + v + t * 0.5));
            row[x * 4 + 0] = (uint8_t)(b * 255);
            row[x * 4 + 1] = (uint8_t)(g * 255);
            row[x * 4 + 2] = (uint8_t)(r * 255);
            row[x * 4 + 3] = 255;
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    Window *win = window_create("vsynth", -1, -1, 640, 480);
    if (!win) return 1;

    uint8_t *frame = malloc((size_t)TEST_W * TEST_H * 4);
    if (!frame) return 1;

    fprintf(stderr, "vsynth M0: left-drag moves, alt+right-drag resizes, f fullscreen, q/esc quits\n");

    int running = 1;
    Uint64 t0 = SDL_GetPerformanceCounter();
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

        double t = (double)(SDL_GetPerformanceCounter() - t0) / SDL_GetPerformanceFrequency();
        fill_test_pattern(frame, TEST_W, TEST_H, t);
        window_present_frame(win, frame, TEST_W, TEST_H, TEST_W * 4);
    }

    int x, y, w, h;
    window_get_geometry(win, &x, &y, &w, &h);
    fprintf(stderr, "window geometry at exit: %d,%d %dx%d\n", x, y, w, h);

    free(frame);
    window_destroy(win);
    SDL_Quit();
    return 0;
}
