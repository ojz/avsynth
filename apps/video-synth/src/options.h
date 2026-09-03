#ifndef VSYNTH_OPTIONS_H
#define VSYNTH_OPTIONS_H

#include <SDL2/SDL.h>
#include "hud.h"
#include "project.h"

/*
 * Project mode (F4): the things that are not knobs. The capture setting
 * (region, fps) and the list of chains in the project, with switch, new,
 * rename and delete. Keyboard only, drawn in the shared sheet.
 *
 * The module holds a copy of what it shows; main.c refreshes it after every
 * change and carries out the actions it returns.
 */
typedef struct Options Options;

enum OptAction {
    OPT_NONE = 0, OPT_CONSUMED, OPT_CLOSE,
    OPT_SWITCH_CHAIN, OPT_NEW_CHAIN, OPT_RENAME_CHAIN, OPT_DELETE_CHAIN,
    OPT_SET_FPS, OPT_PICK_REGION
};

typedef struct OptResult {
    int  chain_id;
    char name[64];
    int  fps;
} OptResult;

#define OPT_MAX_CHAINS 64

Options *options_create(Hud *hud);
void     options_destroy(Options *o);

void options_set_project(Options *o, const char *path);
void options_set_chains(Options *o, const ChainInfo *list, const int *preset_counts, int n, int current_id);
void options_set_capture(Options *o, int x, int y, int w, int h, int fps);

void options_open(Options *o);
void options_close(Options *o);

int  options_handle_event(Options *o, const SDL_Event *ev, OptResult *out);
void options_draw(Options *o, SDL_Renderer *ren, int W, int H);

#endif
