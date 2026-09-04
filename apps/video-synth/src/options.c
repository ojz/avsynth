#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_CAPTURE 0
#define ROW_FIRST_CHAIN 1
#define CONFIRM_MS 3000

struct Options {
    Hud *hud;
    int  open;

    char path[1024];
    ChainInfo chains[OPT_MAX_CHAINS];
    int  presets[OPT_MAX_CHAINS];
    int  nchains;
    int  current_id;
    int  cap_x, cap_y, cap_w, cap_h, fps;

    int  sel;            /* ROW_CAPTURE or ROW_FIRST_CHAIN + i */
    int  renaming;       /* typing a new name for the selected chain */
    char name[64];
    Uint32 delete_armed_until;
};

Options *options_create(Hud *hud)
{
    Options *o = calloc(1, sizeof *o);
    if (!o) return NULL;
    o->hud = hud;
    o->sel = ROW_FIRST_CHAIN;
    return o;
}

void options_destroy(Options *o) { free(o); }

void options_set_project(Options *o, const char *path)
{
    snprintf(o->path, sizeof o->path, "%s", path ? path : "");
}

void options_set_chains(Options *o, const ChainInfo *list, const int *preset_counts, int n, int current_id)
{
    if (n > OPT_MAX_CHAINS) n = OPT_MAX_CHAINS;
    memcpy(o->chains, list, n * sizeof list[0]);
    memcpy(o->presets, preset_counts, n * sizeof preset_counts[0]);
    o->nchains = n;
    o->current_id = current_id;
    if (o->sel >= ROW_FIRST_CHAIN + n) o->sel = ROW_FIRST_CHAIN + n - 1;
    if (o->sel < 0) o->sel = 0;
}

void options_set_capture(Options *o, int x, int y, int w, int h, int fps)
{
    o->cap_x = x; o->cap_y = y; o->cap_w = w; o->cap_h = h; o->fps = fps;
}

void options_open(Options *o)
{
    o->open = 1;
    o->renaming = 0;
    o->delete_armed_until = 0;
    /* land on the current chain */
    for (int i = 0; i < o->nchains; i++)
        if (o->chains[i].id == o->current_id) o->sel = ROW_FIRST_CHAIN + i;
}

void options_close(Options *o)
{
    if (o->renaming) SDL_StopTextInput();
    o->open = 0;
    o->renaming = 0;
}

static int selected_chain(const Options *o)
{
    int i = o->sel - ROW_FIRST_CHAIN;
    return i >= 0 && i < o->nchains ? i : -1;
}

int options_handle_event(Options *o, const SDL_Event *ev, OptResult *out)
{
    if (!o->open) return OPT_NONE;
    memset(out, 0, sizeof *out);

    if (o->renaming) {
        switch (ev->type) {
        case SDL_TEXTINPUT:
            if (strlen(o->name) + strlen(ev->text.text) < sizeof o->name) strcat(o->name, ev->text.text);
            return OPT_CONSUMED;
        case SDL_KEYUP:
            return OPT_CONSUMED;
        case SDL_KEYDOWN:
            switch (ev->key.keysym.sym) {
            case SDLK_ESCAPE:
                o->renaming = 0;
                SDL_StopTextInput();
                return OPT_CONSUMED;
            case SDLK_BACKSPACE:
                if (o->name[0]) o->name[strlen(o->name) - 1] = 0;
                return OPT_CONSUMED;
            case SDLK_RETURN:
            case SDLK_KP_ENTER: {
                int i = selected_chain(o);
                o->renaming = 0;
                SDL_StopTextInput();
                if (i < 0 || !o->name[0]) return OPT_CONSUMED;
                out->chain_id = o->chains[i].id;
                snprintf(out->name, sizeof out->name, "%s", o->name);
                return OPT_RENAME_CHAIN;
            }
            default:
                return OPT_CONSUMED;
            }
        default:
            return OPT_NONE;
        }
    }

    if (ev->type == SDL_KEYUP || ev->type == SDL_TEXTINPUT) return OPT_CONSUMED;
    if (ev->type != SDL_KEYDOWN) return OPT_NONE;

    int i = selected_chain(o);
    switch (ev->key.keysym.sym) {
    case SDLK_ESCAPE:
        return OPT_CLOSE;
    case SDLK_UP:
        if (o->sel > 0) o->sel--;
        o->delete_armed_until = 0;
        break;
    case SDLK_DOWN:
        if (o->sel < ROW_FIRST_CHAIN + o->nchains - 1) o->sel++;
        o->delete_armed_until = 0;
        break;
    case SDLK_LEFT:
    case SDLK_RIGHT:
        if (o->sel == ROW_CAPTURE) {
            int fps = o->fps + (ev->key.keysym.sym == SDLK_RIGHT ? 5 : -5);
            if (fps < 5) fps = 5;
            if (fps > 60) fps = 60;
            if (fps != o->fps) { out->fps = fps; return OPT_SET_FPS; }
        }
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (o->sel == ROW_CAPTURE) return OPT_PICK_REGION;
        if (i >= 0) { out->chain_id = o->chains[i].id; return OPT_SWITCH_CHAIN; }
        break;
    case SDLK_c:
        return OPT_PICK_REGION;
    case SDLK_n:
        return OPT_NEW_CHAIN;
    case SDLK_r:
        if (i >= 0) {
            o->renaming = 1;
            snprintf(o->name, sizeof o->name, "%s", o->chains[i].name);
            SDL_StartTextInput();
        }
        break;
    case SDLK_DELETE:
        if (i < 0) break;
        if (o->nchains <= 1) break;
        if (o->delete_armed_until && !SDL_TICKS_PASSED(SDL_GetTicks(), o->delete_armed_until)) {
            o->delete_armed_until = 0;
            out->chain_id = o->chains[i].id;
            return OPT_DELETE_CHAIN;
        }
        o->delete_armed_until = SDL_GetTicks() + CONFIRM_MS;
        break;
    default:
        break;
    }
    return OPT_CONSUMED;
}

void options_draw(Options *o, SDL_Renderer *ren, int W, int H)
{
    if (!o->open) return;
    Hud *hud = o->hud;
    int lh = hud_line_h(hud);
    if (lh <= 0) lh = 14;
    int rowh = lh + 2;

    SDL_Rect body = hud_sheet(hud, ren, W, H);
    int x = body.x, y = body.y;
    char line[1200];

    snprintf(line, sizeof line, "project  %s", o->path);
    hud_text(hud, x, y, HUD_DIM, line);
    y += rowh + 4;

    /* capture row */
    if (o->sel == ROW_CAPTURE) hud_fill(ren, (SDL_Rect){ body.x, y, body.w, rowh }, HUD_SEL.r, HUD_SEL.g, HUD_SEL.b, 70);
    snprintf(line, sizeof line, "capture  %d,%d  %dx%d  @ %d fps", o->cap_x, o->cap_y, o->cap_w, o->cap_h, o->fps);
    hud_text(hud, x, y + 1, o->sel == ROW_CAPTURE ? HUD_SEL : HUD_TEXT, line);
    if (o->sel == ROW_CAPTURE) hud_text(hud, x + hud_text_width(hud, line) + 16, y + 1, HUD_DIM, "left/right: fps   enter: pick region");
    y += rowh + 4;

    hud_fill(ren, (SDL_Rect){ body.x, y, body.w, 1 }, 255, 255, 255, 40);
    y += 4;
    snprintf(line, sizeof line, "chains   %d", o->nchains);
    hud_text(hud, x, y, HUD_DIM, line);
    y += rowh;

    int armed = o->delete_armed_until && !SDL_TICKS_PASSED(SDL_GetTicks(), o->delete_armed_until);
    for (int i = 0; i < o->nchains && y + rowh <= body.y + body.h; i++) {
        int row = ROW_FIRST_CHAIN + i;
        int sel = row == o->sel;
        int cur = o->chains[i].id == o->current_id;
        if (sel) hud_fill(ren, (SDL_Rect){ body.x, y, body.w, rowh }, HUD_SEL.r, HUD_SEL.g, HUD_SEL.b, 70);
        if (sel && o->renaming)
            snprintf(line, sizeof line, "%s %s_", cur ? ">" : " ", o->name);
        else
            snprintf(line, sizeof line, "%s %-24.24s %d preset%s", cur ? ">" : " ", o->chains[i].name,
                     o->presets[i], o->presets[i] == 1 ? "" : "s");
        hud_text(hud, x, y + 1, sel ? HUD_SEL : (cur ? HUD_TEXT : HUD_DIM), line);
        if (sel && armed) hud_text(hud, x + hud_text_width(hud, line) + 16, y + 1, HUD_ERR, "delete again to confirm");
        y += rowh;
    }

    hud_footer(hud, &body, o->renaming ? "type the new name   enter: rename   esc: cancel"
                                        : "up/down   enter: switch   n new   r rename   del delete   c region",
               NULL);
}
