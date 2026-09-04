#ifndef VSYNTH_HELP_H
#define VSYNTH_HELP_H

#include <SDL2/SDL.h>
#include "hud.h"

/*
 * Filter browser: libavfilter's own manual, drawn over the video. A list of
 * every video filter with its description, narrowed as you type. Enter on a
 * filter shows its options (type, default, range, whether it can be changed
 * live, the constants of an enum). Enter again inserts a snippet into the
 * chain editor: filter@filter=opt=default:... for every live numeric option,
 * so the new module arrives with knobs.
 *
 * Keyboard only. Esc is handled by the caller (mode switch).
 */
typedef struct Help Help;

enum HelpAction { HELP_NONE = 0, HELP_CONSUMED, HELP_CLOSE, HELP_INSERT };

Help *help_create(Hud *hud);
void  help_destroy(Help *h);

/* Show the list. query may be NULL; if it names a filter exactly, open its details. */
void  help_open(Help *h, const char *query);
void  help_close(Help *h);
int   help_is_open(const Help *h);

int   help_handle_event(Help *h, const SDL_Event *ev);
void  help_draw(Help *h, SDL_Renderer *ren, int W, int H);

/* After HELP_INSERT: the snippet to put into the editor. */
const char *help_snippet(const Help *h);

#endif
