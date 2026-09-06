#include "window.h"

#include <stdio.h>
#include <stdlib.h>

enum DragMode { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE };

struct Window {
    SDL_Window   *sdl;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    int           tex_w, tex_h;
    int           has_frame;

    enum DragMode drag;
    int           grab_mx, grab_my;     /* global mouse at grab */
    int           grab_wx, grab_wy;     /* window pos at grab */
    int           grab_ww, grab_wh;     /* window size at grab */

    int           fullscreen;
};

#define MIN_SIZE 64

Window *window_attach(SDL_Window *sdl, SDL_Renderer *ren)
{
    Window *win = calloc(1, sizeof *win);
    if (!win) return NULL;
    win->sdl = sdl;
    win->ren = ren;
    return win;
}

void window_destroy(Window *win)
{
    if (!win) return;
    if (win->tex) SDL_DestroyTexture(win->tex);
    free(win);
}

int window_handle_event(Window *win, const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (win->fullscreen) return 0;
        if (ev->button.button == SDL_BUTTON_LEFT)
            win->drag = DRAG_MOVE;
        else if (ev->button.button == SDL_BUTTON_RIGHT)
            win->drag = DRAG_RESIZE;
        else
            return 0;
        float gx, gy;
        SDL_GetGlobalMouseState(&gx, &gy);   /* floats in SDL3; window geometry stays integer */
        win->grab_mx = (int)gx;
        win->grab_my = (int)gy;
        SDL_GetWindowPosition(win->sdl, &win->grab_wx, &win->grab_wy);
        SDL_GetWindowSize(win->sdl, &win->grab_ww, &win->grab_wh);
        SDL_CaptureMouse(true);
        return 1;

    case SDL_EVENT_MOUSE_MOTION: {
        if (win->drag == DRAG_NONE) return 0;
        float fmx, fmy;
        SDL_GetGlobalMouseState(&fmx, &fmy);
        int dx = (int)fmx - win->grab_mx, dy = (int)fmy - win->grab_my;
        if (win->drag == DRAG_MOVE) {
            SDL_SetWindowPosition(win->sdl, win->grab_wx + dx, win->grab_wy + dy);
        } else {
            int nw = win->grab_ww + dx, nh = win->grab_wh + dy;
            if (nw < MIN_SIZE) nw = MIN_SIZE;
            if (nh < MIN_SIZE) nh = MIN_SIZE;
            SDL_SetWindowSize(win->sdl, nw, nh);
        }
        return 1;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (win->drag == DRAG_NONE) return 0;
        win->drag = DRAG_NONE;
        SDL_CaptureMouse(false);
        return 1;

    default:
        return 0;
    }
}

void window_set_frame(Window *win, const uint8_t *bgra, int w, int h, int stride)
{
    if (!win->tex || win->tex_w != w || win->tex_h != h) {
        if (win->tex) SDL_DestroyTexture(win->tex);
        win->tex = SDL_CreateTexture(win->ren, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!win->tex) {
            fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
            return;
        }
        /* SDL3 dropped the render-scale-quality hint; smoothing is per texture. */
        SDL_SetTextureScaleMode(win->tex, SDL_SCALEMODE_LINEAR);
        win->tex_w = w;
        win->tex_h = h;
    }
    SDL_UpdateTexture(win->tex, NULL, bgra, stride);
    win->has_frame = 1;
}

void window_draw(Window *win)
{
    /* The picture is black until the first frame arrives, whatever the lab's
     * panel colour is: this window is the picture. */
    SDL_SetRenderDrawColor(win->ren, 0, 0, 0, 255);
    SDL_RenderClear(win->ren);
    if (win->has_frame)
        SDL_RenderTexture(win->ren, win->tex, NULL, NULL);
}

SDL_Renderer *window_renderer(Window *win) { return win->ren; }
SDL_Window   *window_sdl(Window *win)      { return win->sdl; }

void window_get_geometry(const Window *win, int *x, int *y, int *w, int *h)
{
    SDL_GetWindowPosition(win->sdl, x, y);
    SDL_GetWindowSize(win->sdl, w, h);
}

void window_set_geometry(Window *win, int x, int y, int w, int h)
{
    SDL_SetWindowSize(win->sdl, w, h);
    SDL_SetWindowPosition(win->sdl,
                          x < 0 ? (int)SDL_WINDOWPOS_CENTERED : x,
                          y < 0 ? (int)SDL_WINDOWPOS_CENTERED : y);
}

void window_set_title(Window *win, const char *title)
{
    SDL_SetWindowTitle(win->sdl, title);
}

void window_toggle_fullscreen(Window *win)
{
    win->fullscreen = !win->fullscreen;
    /* SDL3 fullscreen is a bool; borderless desktop is the default mode. */
    SDL_SetWindowFullscreen(win->sdl, win->fullscreen);
}
