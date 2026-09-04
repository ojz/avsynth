#ifndef VSYNTH_WINDOW_H
#define VSYNTH_WINDOW_H

#include <stdint.h>
#include <SDL2/SDL.h>

/*
 * Borderless output window with app-implemented move and resize.
 *
 * Left-drag moves the window. Right-drag resizes it. No Alt modifier: AltSnap
 * and similar tools hook Alt+mouse globally before the app ever sees it.
 * Both are done from our own event loop with SDL_SetWindowPosition/Size, so the
 * OS never enters its modal move loop and the picture keeps updating.
 */
typedef struct Window Window;

Window *window_create(const char *title, int x, int y, int w, int h);
void    window_destroy(Window *win);

/* Returns 1 if the event was consumed by drag/resize handling, else 0. */
int     window_handle_event(Window *win, const SDL_Event *ev);

/* Upload a BGRA frame (ffmpeg AV_PIX_FMT_BGRA) and present it. */
void    window_present_frame(Window *win, const uint8_t *bgra, int w, int h, int stride);

/* Re-present whatever frame was uploaded last (keeps the window painted). */
void    window_present(Window *win);

void    window_get_geometry(const Window *win, int *x, int *y, int *w, int *h);
void    window_set_title(Window *win, const char *title);
void    window_toggle_fullscreen(Window *win);

/* Save what is currently on screen (frame + overlay) as a BMP. */
int     window_save_bmp(Window *win, const char *path);

/* Renderer, for building textures (HUD atlas). */
SDL_Renderer *window_renderer(Window *win);

/* Overlay drawn on top of every presented frame, in window pixel coordinates. */
typedef void (*WindowOverlayFn)(SDL_Renderer *ren, int w, int h, void *ud);
void    window_set_overlay(Window *win, WindowOverlayFn fn, void *ud);

#endif
