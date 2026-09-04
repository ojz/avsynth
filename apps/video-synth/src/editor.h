#ifndef VSYNTH_EDITOR_H
#define VSYNTH_EDITOR_H

#include <SDL3/SDL.h>
#include "hud.h"

/*
 * Chain editor: a plain multi-line text box drawn over the video with the
 * panel's glyph atlas. Just enough editor to write a filtergraph: cursor
 * keys, Home/End, Shift-selection, Ctrl+A/C/X/V through the SDL clipboard,
 * Tab inserts two spaces. Ctrl+Enter applies (the caller validates and
 * restarts the voice), Esc closes without applying. A status line under the
 * text shows the last error from libavfilter, or the key hints.
 *
 * While open it consumes every key and text-input event. It never takes the
 * mouse: dragging anywhere, including over the editor, moves or resizes the
 * window as usual.
 */
typedef struct Editor Editor;

/* EDITOR_NONE: not consumed. EDITOR_CONSUMED: swallowed. EDITOR_APPLY: the
 * caller reads editor_text() and validates. EDITOR_CLOSE: caller closes. */
enum EditorAction { EDITOR_NONE = 0, EDITOR_CONSUMED, EDITOR_APPLY, EDITOR_CLOSE };

Editor *editor_create(Hud *hud);
void    editor_destroy(Editor *e);

/* Replace the buffer with text (chain switched, or applied). */
void    editor_load(Editor *e, const char *text, const char *title);
/* Show the editor. Unsaved edits are kept; a clean buffer is refreshed from text. */
void    editor_open(Editor *e, const char *text, const char *title);
void    editor_close(Editor *e);
int     editor_is_open(const Editor *e);

const char *editor_text(const Editor *e);
void    editor_set_status(Editor *e, const char *msg, int is_error);
/* After a successful apply: the buffer now matches the chain; cursor stays put. */
void    editor_mark_clean(Editor *e);
int     editor_dirty(const Editor *e);

/* Insert at the cursor, adding a separating comma when the text needs one. */
void    editor_insert_filter(Editor *e, const char *snippet);
/* The identifier under the cursor (letters, digits, _), "" if none. */
void    editor_word_at_cursor(const Editor *e, char *buf, size_t cap);

/* Returns an EditorAction. EDITOR_APPLY: caller reads editor_text(). */
int     editor_handle_event(Editor *e, const SDL_Event *ev);

void    editor_draw(Editor *e, SDL_Renderer *ren, int W, int H);

#endif
