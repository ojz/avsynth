#include "picker.h"

#include <stdio.h>
#include <SDL2/SDL.h>

#define MIN_REGION 16

static SDL_Rect desktop_bounds(void)
{
    SDL_Rect u = { 0, 0, 0, 0 };
    int n = SDL_GetNumVideoDisplays();
    for (int i = 0; i < n; i++) {
        SDL_Rect b;
        if (SDL_GetDisplayBounds(i, &b) != 0) continue;
        if (u.w == 0) u = b; else SDL_UnionRect(&u, &b, &u);
    }
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
        SDL_Rect loc = { r->x - desk->x, r->y - desk->y, r->w, r->h };
        SDL_SetRenderDrawColor(ren, 255, 40, 40, live ? 70 : 35);
        SDL_RenderFillRect(ren, &loc);
        SDL_SetRenderDrawColor(ren, 255, 40, 40, 255);
        for (int i = 0; i < 3; i++) {
            SDL_Rect o = { loc.x - i, loc.y - i, loc.w + 2 * i, loc.h + 2 * i };
            SDL_RenderDrawRect(ren, &o);
        }
    }
    SDL_RenderPresent(ren);
}

int picker_run(const PickRect *prev, PickRect *out)
{
    SDL_Rect desk = desktop_bounds();
    SDL_Window *w = SDL_CreateWindow("vsynth region", desk.x, desk.y, desk.w, desk.h,
                                     SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
                                     SDL_WINDOW_SKIP_TASKBAR);
    if (!w) {
        fprintf(stderr, "picker: SDL_CreateWindow: %s\n", SDL_GetError());
        return 0;
    }
    if (SDL_SetWindowOpacity(w, 0.55f) != 0)
        fprintf(stderr, "picker: no window opacity here, overlay is solid\n");
    SDL_Renderer *ren = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(w, -1, 0);
    if (!ren) {
        fprintf(stderr, "picker: SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(w);
        return 0;
    }
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
        case SDL_QUIT:
            done = 1;
            break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_RETURN ||
                ev.key.keysym.sym == SDLK_c || ev.key.keysym.sym == SDLK_q)
                done = 1;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                SDL_GetGlobalMouseState(&x0, &y0);
                dragging = 1;
                cur = rect_from_points(x0, y0, x0, y0);
            } else {
                done = 1;
            }
            break;
        case SDL_MOUSEMOTION:
            if (dragging) {
                int x1, y1;
                SDL_GetGlobalMouseState(&x1, &y1);
                cur = rect_from_points(x0, y0, x1, y1);
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (dragging && ev.button.button == SDL_BUTTON_LEFT) {
                int x1, y1;
                SDL_GetGlobalMouseState(&x1, &y1);
                cur = rect_from_points(x0, y0, x1, y1);
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

    if (cross) { SDL_SetCursor(old); SDL_FreeCursor(cross); }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(w);
    /* Drop the events the overlay generated so the main window does not see
     * a stray button-up or the key that closed us. */
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_KEYDOWN, SDL_MOUSEWHEEL);
    return result;
}
