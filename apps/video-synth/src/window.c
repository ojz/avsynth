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

Window *window_create(const char *title, int x, int y, int w, int h)
{
    Window *win = calloc(1, sizeof *win);
    if (!win) return NULL;

    /* Keep rendering while the window is being moved; we own the move loop anyway. */
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    win->sdl = SDL_CreateWindow(title,
                                x < 0 ? SDL_WINDOWPOS_CENTERED : x,
                                y < 0 ? SDL_WINDOWPOS_CENTERED : y,
                                w, h,
                                SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win->sdl) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        free(win);
        return NULL;
    }

    win->ren = SDL_CreateRenderer(win->sdl, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!win->ren)
        win->ren = SDL_CreateRenderer(win->sdl, -1, 0);
    if (!win->ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win->sdl);
        free(win);
        return NULL;
    }

    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(win->ren, &info) == 0)
        fprintf(stderr, "renderer: %s\n", info.name);

    SDL_SetRenderDrawColor(win->ren, 0, 0, 0, 255);
    SDL_RenderClear(win->ren);
    SDL_RenderPresent(win->ren);
    return win;
}

void window_destroy(Window *win)
{
    if (!win) return;
    if (win->tex) SDL_DestroyTexture(win->tex);
    if (win->ren) SDL_DestroyRenderer(win->ren);
    if (win->sdl) SDL_DestroyWindow(win->sdl);
    free(win);
}

static int alt_down(void)
{
    return (SDL_GetModState() & KMOD_ALT) != 0;
}

int window_handle_event(Window *win, const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (win->fullscreen) return 0;
        if (ev->button.button == SDL_BUTTON_LEFT)
            win->drag = DRAG_MOVE;
        else if (ev->button.button == SDL_BUTTON_RIGHT && alt_down())
            win->drag = DRAG_RESIZE;
        else
            return 0;
        SDL_GetGlobalMouseState(&win->grab_mx, &win->grab_my);
        SDL_GetWindowPosition(win->sdl, &win->grab_wx, &win->grab_wy);
        SDL_GetWindowSize(win->sdl, &win->grab_ww, &win->grab_wh);
        SDL_CaptureMouse(SDL_TRUE);
        return 1;

    case SDL_MOUSEMOTION: {
        if (win->drag == DRAG_NONE) return 0;
        int mx, my;
        SDL_GetGlobalMouseState(&mx, &my);
        int dx = mx - win->grab_mx, dy = my - win->grab_my;
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

    case SDL_MOUSEBUTTONUP:
        if (win->drag == DRAG_NONE) return 0;
        win->drag = DRAG_NONE;
        SDL_CaptureMouse(SDL_FALSE);
        return 1;

    case SDL_WINDOWEVENT:
        if (ev->window.event == SDL_WINDOWEVENT_EXPOSED ||
            ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            window_present(win);
        return 0;

    default:
        return 0;
    }
}

void window_present_frame(Window *win, const uint8_t *bgra, int w, int h, int stride)
{
    if (!win->tex || win->tex_w != w || win->tex_h != h) {
        if (win->tex) SDL_DestroyTexture(win->tex);
        win->tex = SDL_CreateTexture(win->ren, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!win->tex) {
            fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
            return;
        }
        win->tex_w = w;
        win->tex_h = h;
    }
    SDL_UpdateTexture(win->tex, NULL, bgra, stride);
    win->has_frame = 1;
    window_present(win);
}

void window_present(Window *win)
{
    SDL_RenderClear(win->ren);
    if (win->has_frame)
        SDL_RenderCopy(win->ren, win->tex, NULL, NULL);
    SDL_RenderPresent(win->ren);
}

void window_get_geometry(const Window *win, int *x, int *y, int *w, int *h)
{
    SDL_GetWindowPosition(win->sdl, x, y);
    SDL_GetWindowSize(win->sdl, w, h);
}

void window_set_title(Window *win, const char *title)
{
    SDL_SetWindowTitle(win->sdl, title);
}

void window_toggle_fullscreen(Window *win)
{
    win->fullscreen = !win->fullscreen;
    SDL_SetWindowFullscreen(win->sdl, win->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}
