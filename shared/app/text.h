#ifndef AVSYNTH_APP_TEXT_H
#define AVSYNTH_APP_TEXT_H

#include <SDL3/SDL.h>
#include "ui.h"

/*
 * The lab's typeface, rendered once into a glyph atlas (D13). The face comes
 * from assets/fonts/ next to the executable or at the repository root; until
 * one is shipped there, a system monospace font stands in and says so on
 * stderr. Every app draws text through this and nothing else.
 */
typedef struct AppText AppText;

AppText *apptext_create(SDL_Renderer *r, float px);
void     apptext_destroy(AppText *t);

/* The UiText shared/ui and the apps draw with. */
UiText   apptext_ui(AppText *t);

int      apptext_line_h(const AppText *t);
int      apptext_char_w(const AppText *t);
int      apptext_width(const AppText *t, const char *s);
void     apptext_draw(AppText *t, float x, float y, const char *s, SDL_Color col);

/* Where the face was loaded from, for the log. */
const char *apptext_source(const AppText *t);

#endif
