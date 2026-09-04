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

    WindowOverlayFn overlay;
    void           *overlay_ud;
};

#define MIN_SIZE 64

Window *window_create(const char *title, int x, int y, int w, int h)
{
    Window *win = calloc(1, sizeof *win);
    if (!win) return NULL;

    /* Keep rendering while the window is being moved; we own the move loop anyway. */
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

    /* SDL3 creates the window at a size and places it afterwards. */
    win->sdl = SDL_CreateWindow(title, w, h,
                                SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win->sdl) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        free(win);
        return NULL;
    }
    SDL_SetWindowPosition(win->sdl,
                          x < 0 ? (int)SDL_WINDOWPOS_CENTERED : x,
                          y < 0 ? (int)SDL_WINDOWPOS_CENTERED : y);

    win->ren = SDL_CreateRenderer(win->sdl, NULL);
    if (!win->ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win->sdl);
        free(win);
        return NULL;
    }
    /* vsync is a renderer property in SDL3, not a creation flag. Failing to get
     * it is not fatal: we throttle on the capture fps anyway. */
    SDL_SetRenderVSync(win->ren, 1);

    const char *name = SDL_GetRendererName(win->ren);
    fprintf(stderr, "renderer: %s\n", name ? name : "unknown");

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

    /* SDL3 gives each window change its own event type. */
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
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
        /* SDL3 dropped the render-scale-quality hint; smoothing is per texture. */
        SDL_SetTextureScaleMode(win->tex, SDL_SCALEMODE_LINEAR);
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
        SDL_RenderTexture(win->ren, win->tex, NULL, NULL);
    if (win->overlay) {
        int w, h;
        SDL_GetRenderOutputSize(win->ren, &w, &h);
        win->overlay(win->ren, w, h, win->overlay_ud);
    }
    SDL_RenderPresent(win->ren);
}

SDL_Renderer *window_renderer(Window *win) { return win->ren; }
SDL_Window   *window_sdl(Window *win)      { return win->sdl; }

void window_set_overlay(Window *win, WindowOverlayFn fn, void *ud)
{
    win->overlay = fn;
    win->overlay_ud = ud;
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
    /* SDL3 fullscreen is a bool; borderless desktop is the default mode. */
    SDL_SetWindowFullscreen(win->sdl, win->fullscreen);
}

int window_save_bmp(Window *win, const char *path)
{
    window_present(win);   /* redraw so the read-back sees a complete frame */
    /* SDL3 hands back a fresh surface instead of filling one we allocated. */
    SDL_Surface *s = SDL_RenderReadPixels(win->ren, NULL);
    if (!s) {
        fprintf(stderr, "screenshot: %s\n", SDL_GetError());
        return -1;
    }
    /* Read the error before destroying the surface: SDL_DestroySurface can
     * overwrite it, so reporting afterwards prints an unrelated message. */
    int rc = SDL_SaveBMP(s, path) ? 0 : -1;
    char err[256] = "";
    if (rc != 0) snprintf(err, sizeof err, "%s", SDL_GetError());
    SDL_DestroySurface(s);
    if (rc == 0) fprintf(stderr, "screenshot saved: %s\n", path);
    else         fprintf(stderr, "screenshot: %s\n", err);
    return rc;
}
