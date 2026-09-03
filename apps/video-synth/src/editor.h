#ifndef VSYNTH_EDITOR_H
#define VSYNTH_EDITOR_H

#include <SDL2/SDL.h>
#include "hud.h"

/*
 * Chain editor: a plain multi-line text box drawn over the video with the
 * panel's glyph atlas. Just enough editor to write a filtergraph: cursor
 * keys, Home/End, Shift-selection, Ctrl+A/C/X/V through the SDL clipboard,
 * Tab inserts two spaces. Ctrl+Enter applies (the caller validates and
 * restarts the voice), Esc closes without applying. A status line under the
 * text shows the last error from libavfilter, or the key hints.
 *
 * While open it consumes every key and text-input event, and mouse events
 * inside its box.
 */
typedef struct Editor Editor;

/* EDITOR_NONE: not consumed. EDITOR_CONSUMED: swallowed. EDITOR_APPLY: the
 * caller reads editor_text() and validates. EDITOR_CLOSE: caller closes. */
enum EditorAction { EDITOR_NONE = 0, EDITOR_CONSUMED, EDITOR_APPLY, EDITOR_CLOSE };

Editor *editor_create(Hud *hud);
void    editor_destroy(Editor *e);

void    editor_open(Editor *e, const char *text, const char *title);
void    editor_close(Editor *e);
int     editor_is_open(const Editor *e);

const char *editor_text(const Editor *e);
void    editor_set_status(Editor *e, const char *msg, int is_error);
void    editor_set_title(Editor *e, const char *title);

/* Returns an EditorAction. EDITOR_APPLY: caller reads editor_text(). */
int     editor_handle_event(Editor *e, const SDL_Event *ev);

void    editor_draw(Editor *e, SDL_Renderer *ren, int W, int H);

#endif
