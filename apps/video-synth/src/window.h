#ifndef VSYNTH_WINDOW_H
#define VSYNTH_WINDOW_H

#include <stdint.h>
#include <SDL3/SDL.h>

/*
 * The picture, and app-implemented move and resize for the shell's borderless
 * window.
 *
 * Left-drag moves the window. Right-drag resizes it. No Alt modifier: AltSnap
 * and similar tools hook Alt+mouse globally before the app ever sees it.
 * Both are done from the app's event callback with SDL_SetWindowPosition/Size,
 * so the OS never enters its modal move loop and the picture keeps updating.
 *
 * The window and renderer belong to the shell (shared/app); this wraps them.
 */
typedef struct Window Window;

Window *window_attach(SDL_Window *sdl, SDL_Renderer *ren);
void    window_destroy(Window *win);

/* Returns 1 if the event was consumed by drag/resize handling, else 0. */
int     window_handle_event(Window *win, const SDL_Event *ev);

/* Upload a BGRA frame (ffmpeg AV_PIX_FMT_BGRA). */
void    window_set_frame(Window *win, const uint8_t *bgra, int w, int h, int stride);

/* Draw the last frame over the whole window. The shell presents. */
void    window_draw(Window *win);

void    window_get_geometry(const Window *win, int *x, int *y, int *w, int *h);
void    window_set_geometry(Window *win, int x, int y, int w, int h);
void    window_set_title(Window *win, const char *title);
void    window_toggle_fullscreen(Window *win);

SDL_Renderer *window_renderer(Window *win);
SDL_Window   *window_sdl(Window *win);

#endif
