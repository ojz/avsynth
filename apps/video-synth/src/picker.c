#include "picker.h"

#include <stdio.h>
#include <SDL3/SDL.h>

#define MIN_REGION 16

static SDL_Rect desktop_bounds(void)
{
    SDL_Rect u = { 0, 0, 0, 0 };
    /* SDL3 enumerates displays by id rather than by index. */
    int n = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    for (int i = 0; ids && i < n; i++) {
        SDL_Rect b;
        if (!SDL_GetDisplayBounds(ids[i], &b)) continue;
        if (u.w == 0) u = b; else SDL_GetRectUnion(&u, &b, &u);
    }
    SDL_free(ids);
    if (u.w <= 0) { u.w = 1920; u.h = 1080; }
    return u;
}

static SDL_Rect rect_from_points(int x0, int y0, int x1, int y1)
{
    SDL_Rect r;
    r.x = x0 < x1 ? x0 : x1;
    r.y = y0 < y1 ? y0 : y1;
    r.w = (x0 < x1 ? x1 - x0 : x0 - x1);
    r.h = (y0 < y1 ? y1 - y0 : y0 - y1);
    return r;
}

static void draw(SDL_Renderer *ren, const SDL_Rect *desk, const SDL_Rect *r, int live)
{
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (r && r->w > 0 && r->h > 0) {
        /* desk and r are desktop coordinates, the same space as the window;
         * the renderer draws in output pixels. Convert both corners so the
         * outline lands on the region the user is dragging out even when the
         * pixel density is above 1. */
        float x0, y0, x1, y1;
        SDL_RenderCoordinatesFromWindow(ren, (float)(r->x - desk->x), (float)(r->y - desk->y), &x0, &y0);
        SDL_RenderCoordinatesFromWindow(ren, (float)(r->x - desk->x + r->w),
                                        (float)(r->y - desk->y + r->h), &x1, &y1);
        SDL_FRect loc = { x0, y0, x1 - x0, y1 - y0 };
        SDL_SetRenderDrawColor(ren, 255, 40, 40, live ? 70 : 35);
        SDL_RenderFillRect(ren, &loc);
        SDL_SetRenderDrawColor(ren, 255, 40, 40, 255);
        for (int i = 0; i < 3; i++) {
            SDL_FRect o = { loc.x - i, loc.y - i, loc.w + 2 * i, loc.h + 2 * i };
            SDL_RenderRect(ren, &o);
        }
    }
    SDL_RenderPresent(ren);
}

int picker_run(const PickRect *prev, PickRect *out)
{
    SDL_Rect desk = desktop_bounds();
    /* SDL3 creates a window without a position, so it would appear centred on
     * the primary display at full desktop size, opaque, before being moved.
     * Start hidden, place it, set the opacity, then show it. */
    SDL_Window *w = SDL_CreateWindow("vsynth region", desk.w, desk.h,
                                     SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
                                     SDL_WINDOW_UTILITY | SDL_WINDOW_HIDDEN);
    if (!w) {
        fprintf(stderr, "picker: SDL_CreateWindow: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetWindowPosition(w, desk.x, desk.y);
    if (!SDL_SetWindowOpacity(w, 0.55f))
        fprintf(stderr, "picker: no window opacity here, overlay is solid\n");
    SDL_Renderer *ren = SDL_CreateRenderer(w, NULL);
    if (!ren) {
        fprintf(stderr, "picker: SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(w);
        return 0;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_ShowWindow(w);
    SDL_Cursor *cross = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    SDL_Cursor *old = SDL_GetCursor();
    if (cross) SDL_SetCursor(cross);
    SDL_RaiseWindow(w);

    SDL_Rect cur = { prev->x, prev->y, prev->w, prev->h };
    int dragging = 0, x0 = 0, y0 = 0, result = 0, done = 0;
    draw(ren, &desk, &cur, 0);

    while (!done) {
        SDL_Event ev;
        if (!SDL_WaitEventTimeout(&ev, 50)) { draw(ren, &desk, &cur, dragging); continue; }
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            done = 1;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.key == SDLK_ESCAPE || ev.key.key == SDLK_RETURN ||
                ev.key.key == SDLK_C || ev.key.key == SDLK_Q)
                done = 1;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                float fx, fy;
                SDL_GetGlobalMouseState(&fx, &fy);
                x0 = (int)fx;
                y0 = (int)fy;
                dragging = 1;
                cur = rect_from_points(x0, y0, x0, y0);
            } else {
                done = 1;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (dragging) {
                float fx, fy;
                SDL_GetGlobalMouseState(&fx, &fy);
                cur = rect_from_points(x0, y0, (int)fx, (int)fy);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (dragging && ev.button.button == SDL_BUTTON_LEFT) {
                float fx, fy;
                SDL_GetGlobalMouseState(&fx, &fy);
                cur = rect_from_points(x0, y0, (int)fx, (int)fy);
                cur.w &= ~1;   /* even sizes keep every pixel format happy */
                cur.h &= ~1;
                if (cur.w >= MIN_REGION && cur.h >= MIN_REGION) {
                    *out = (PickRect){ cur.x, cur.y, cur.w, cur.h };
                    result = 1;
                }
                done = 1;
            }
            break;
        default:
            break;
        }
        draw(ren, &desk, &cur, dragging);
    }

    if (cross) { SDL_SetCursor(old); SDL_DestroyCursor(cross); }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(w);
    /* Drop the events the overlay generated so the main window does not see
     * a stray button-up or the key that closed us. */
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_MOUSE_WHEEL);
    return result;
}
