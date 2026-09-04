#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ED_CAP RACK_CHAIN_CAP

struct Editor {
    Hud  *hud;
    int   open;

    char  text[ED_CAP];
    char  loaded[ED_CAP];   /* what the buffer last matched on disk; differs when dirty */
    int   len;
    int   cursor;
    int   anchor;      /* selection anchor, -1 when no selection */

    int   scroll_line; /* first visible line */
    int   scroll_col;  /* first visible column */

    char  title[64];
    char  status[GRAPH_ERR_CAP];
    int   status_err;

    SDL_Rect box, area;   /* whole box; text area */
    Uint64   blink0;
};


static const char *HINT = "ctrl+enter apply   F1 help on word   esc back   ctrl+a/c/x/v   {W} {H} = capture size";

Editor *editor_create(Hud *hud)
{
    Editor *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    e->hud = hud;
    e->anchor = -1;
    return e;
}

void editor_destroy(Editor *e) { free(e); }

void editor_load(Editor *e, const char *text, const char *title)
{
    snprintf(e->text, sizeof e->text, "%s", text ? text : "");
    snprintf(e->loaded, sizeof e->loaded, "%s", e->text);
    e->len = (int)strlen(e->text);
    e->cursor = e->len;
    e->anchor = -1;
    e->scroll_line = e->scroll_col = 0;
    snprintf(e->title, sizeof e->title, "%s", title ? title : "chain");
    e->status[0] = 0;
    e->status_err = 0;
}

int editor_dirty(const Editor *e) { return strcmp(e->text, e->loaded) != 0; }

void editor_mark_clean(Editor *e)
{
    snprintf(e->loaded, sizeof e->loaded, "%s", e->text);
}

void editor_open(Editor *e, const char *text, const char *title)
{
    if (!editor_dirty(e)) editor_load(e, text, title);
    e->open = 1;
    e->blink0 = SDL_GetTicks();
    hud_text_input(e->hud, 1);
}

static void insert(Editor *e, const char *s);

void editor_insert_filter(Editor *e, const char *snippet)
{
    /* need a separator unless we are at the start or right after , ; ] or a newline */
    int i = e->cursor;
    while (i > 0 && (e->text[i - 1] == ' ' || e->text[i - 1] == '\n')) i--;
    int need_sep = i > 0 && !strchr(",;[", e->text[i - 1]);
    if (need_sep) insert(e, ",\n");
    insert(e, snippet);
    e->status[0] = 0;
}

static int is_word(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

void editor_word_at_cursor(const Editor *e, char *buf, size_t cap)
{
    int a = e->cursor, b = e->cursor;
    while (a > 0 && is_word(e->text[a - 1])) a--;
    while (b < e->len && is_word(e->text[b])) b++;
    int n = b - a;
    if (n >= (int)cap) n = (int)cap - 1;
    if (n < 0) n = 0;
    memcpy(buf, e->text + a, n);
    buf[n] = 0;
}

void editor_close(Editor *e)
{
    e->open = 0;
    hud_text_input(e->hud, 0);
}

int         editor_is_open(const Editor *e) { return e->open; }
const char *editor_text(const Editor *e)    { return e->text; }

void editor_set_status(Editor *e, const char *msg, int is_error)
{
    snprintf(e->status, sizeof e->status, "%s", msg ? msg : "");
    e->status_err = is_error;
}

/* ---------- text model ---------- */

static int line_start(const Editor *e, int pos)
{
    while (pos > 0 && e->text[pos - 1] != '\n') pos--;
    return pos;
}

static int line_end(const Editor *e, int pos)
{
    while (pos < e->len && e->text[pos] != '\n') pos++;
    return pos;
}

static int line_of(const Editor *e, int pos)
{
    int n = 0;
    for (int i = 0; i < pos && i < e->len; i++) if (e->text[i] == '\n') n++;
    return n;
}

static int line_count(const Editor *e) { return line_of(e, e->len) + 1; }

static int pos_of_line(const Editor *e, int line)
{
    int pos = 0;
    while (line > 0 && pos < e->len) { if (e->text[pos] == '\n') line--; pos++; }
    return pos;
}

static void sel_range(const Editor *e, int *a, int *b)
{
    if (e->anchor < 0 || e->anchor == e->cursor) { *a = *b = e->cursor; return; }
    *a = e->anchor < e->cursor ? e->anchor : e->cursor;
    *b = e->anchor < e->cursor ? e->cursor : e->anchor;
}

static int has_sel(const Editor *e) { return e->anchor >= 0 && e->anchor != e->cursor; }

static void delete_range(Editor *e, int a, int b)
{
    if (b <= a) return;
    memmove(e->text + a, e->text + b, e->len - b + 1);
    e->len -= b - a;
    e->cursor = a;
    e->anchor = -1;
}

static void delete_sel(Editor *e)
{
    int a, b;
    sel_range(e, &a, &b);
    delete_range(e, a, b);
}

static void insert(Editor *e, const char *s)
{
    if (has_sel(e)) delete_sel(e);
    int n = (int)strlen(s);
    if (e->len + n >= ED_CAP) n = ED_CAP - 1 - e->len;
    if (n <= 0) return;
    memmove(e->text + e->cursor + n, e->text + e->cursor, e->len - e->cursor + 1);
    memcpy(e->text + e->cursor, s, n);
    e->len += n;
    e->cursor += n;
    e->anchor = -1;
}

static void move_to(Editor *e, int pos, int extend)
{
    if (pos < 0) pos = 0;
    if (pos > e->len) pos = e->len;
    if (extend) { if (e->anchor < 0) e->anchor = e->cursor; }
    else e->anchor = -1;
    e->cursor = pos;
    e->blink0 = SDL_GetTicks();
}

static void move_line(Editor *e, int dir, int extend)
{
    int ls = line_start(e, e->cursor), col = e->cursor - ls;
    int line = line_of(e, e->cursor) + dir;
    if (line < 0 || line >= line_count(e)) {
        move_to(e, dir < 0 ? 0 : e->len, extend);
        return;
    }
    int nls = pos_of_line(e, line), nle = line_end(e, nls);
    move_to(e, nls + (col < nle - nls ? col : nle - nls), extend);
}

static void copy_sel(Editor *e)
{
    int a, b;
    if (has_sel(e)) sel_range(e, &a, &b); else { a = 0; b = e->len; }
    char *tmp = malloc(b - a + 1);
    if (!tmp) return;
    memcpy(tmp, e->text + a, b - a);
    tmp[b - a] = 0;
    SDL_SetClipboardText(tmp);
    free(tmp);
}

static void paste(Editor *e)
{
    char *clip = SDL_GetClipboardText();
    if (!clip) return;
    /* strip CR, keep everything else */
    char *w = clip;
    for (char *r = clip; *r; r++) if (*r != '\r') *w++ = *r;
    *w = 0;
    insert(e, clip);
    SDL_free(clip);
}

/* ---------- events ---------- */



int editor_handle_event(Editor *e, const SDL_Event *ev)
{
    if (!e->open) return EDITOR_NONE;

    switch (ev->type) {
    case SDL_EVENT_TEXT_INPUT:
        insert(e, ev->text.text);
        return EDITOR_CONSUMED;

    case SDL_EVENT_KEY_UP:
        return EDITOR_CONSUMED;

    case SDL_EVENT_KEY_DOWN: {
        SDL_Keymod mod = SDL_GetModState();
        int ctrl = (mod & SDL_KMOD_CTRL) != 0, shift = (mod & SDL_KMOD_SHIFT) != 0;
        switch (ev->key.key) {
        case SDLK_ESCAPE:
            return EDITOR_CLOSE;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (ctrl) return EDITOR_APPLY;
            insert(e, "\n");
            break;
        case SDLK_TAB:
            insert(e, "  ");
            break;
        case SDLK_BACKSPACE:
            if (has_sel(e)) delete_sel(e);
            else if (e->cursor > 0) delete_range(e, e->cursor - 1, e->cursor);
            break;
        case SDLK_DELETE:
            if (has_sel(e)) delete_sel(e);
            else if (e->cursor < e->len) delete_range(e, e->cursor, e->cursor + 1);
            break;
        case SDLK_LEFT:  move_to(e, e->cursor - 1, shift); break;
        case SDLK_RIGHT: move_to(e, e->cursor + 1, shift); break;
        case SDLK_UP:    move_line(e, -1, shift); break;
        case SDLK_DOWN:  move_line(e, +1, shift); break;
        case SDLK_HOME:  move_to(e, ctrl ? 0 : line_start(e, e->cursor), shift); break;
        case SDLK_END:   move_to(e, ctrl ? e->len : line_end(e, e->cursor), shift); break;
        case SDLK_A: if (ctrl) { e->anchor = 0; e->cursor = e->len; } break;
        case SDLK_C: if (ctrl) copy_sel(e); break;
        case SDLK_X: if (ctrl) { copy_sel(e); if (has_sel(e)) delete_sel(e); } break;
        case SDLK_V: if (ctrl) paste(e); break;
        default: break;
        }
        return EDITOR_CONSUMED;
    }

    default:
        return EDITOR_NONE;   /* mouse goes to the window: drag and resize keep working */
    }
}

/* ---------- drawing ---------- */

void editor_draw(Editor *e, SDL_Renderer *ren, int W, int H)
{
    if (!e->open) return;
    Hud *hud = e->hud;
    int lh = hud_line_h(hud), cw = hud_char_w(hud);
    if (lh <= 0) lh = 14;
    if (cw <= 0) cw = 7;

    SDL_Rect body = hud_sheet(hud, ren, W, H);
    int x = body.x, y = body.y;
    int rows = (body.h - lh - 4) / lh;      /* one line kept for the status */
    if (rows < 1) rows = 1;
    int cols = body.w / cw;
    if (cols < 8) cols = 8;
    e->area = (SDL_Rect){ x, y, body.w, rows * lh };
    e->box = body;

    /* keep the cursor visible */
    int cline = line_of(e, e->cursor), ccol = e->cursor - line_start(e, e->cursor);
    if (cline < e->scroll_line) e->scroll_line = cline;
    if (cline >= e->scroll_line + rows) e->scroll_line = cline - rows + 1;
    int nlines = line_count(e);
    if (e->scroll_line > nlines - 1) e->scroll_line = nlines - 1;
    if (e->scroll_line < 0) e->scroll_line = 0;
    if (ccol < e->scroll_col) e->scroll_col = ccol;
    if (ccol >= e->scroll_col + cols) e->scroll_col = ccol - cols + 1;

    int sa, sb;
    sel_range(e, &sa, &sb);
    int pos = pos_of_line(e, e->scroll_line);
    char line[1024];
    for (int row = 0; row < rows && pos <= e->len; row++) {
        int ls = pos, le = line_end(e, pos);
        int ly = y + row * lh;

        /* selection highlight */
        if (sb > sa) {
            int a = sa > ls ? sa : ls, b = sb < le ? sb : le;
            if (sb > le && sa <= le) b = le + 1;   /* include the newline cell */
            if (b > a) {
                int c0 = a - ls - e->scroll_col, c1 = b - ls - e->scroll_col;
                if (c0 < 0) c0 = 0;
                if (c1 > cols) c1 = cols;
                if (c1 > c0) hud_fill(ren, (SDL_Rect){ x + c0 * cw, ly, (c1 - c0) * cw, lh }, HUD_SEL.r, HUD_SEL.g, HUD_SEL.b, 70);
            }
        }

        int n = le - ls - e->scroll_col;
        if (n > 0) {
            if (n > cols) n = cols;
            if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
            memcpy(line, e->text + ls + e->scroll_col, n);
            line[n] = 0;
            hud_text(hud, x, ly, HUD_TEXT, line);
        }

        /* cursor */
        if (ls <= e->cursor && e->cursor <= le && ((SDL_GetTicks() - e->blink0) / 500) % 2 == 0) {
            int c = e->cursor - ls - e->scroll_col;
            if (c >= 0 && c <= cols)
                hud_fill(ren, (SDL_Rect){ x + c * cw, ly, 2, lh }, HUD_SEL.r, HUD_SEL.g, HUD_SEL.b, 255);
        }

        if (le >= e->len) break;
        pos = le + 1;
    }

    /* status: last error or result, on the last body line */
    if (e->status[0]) {
        char st[512];
        snprintf(st, sizeof st, "%.*s", cols, e->status);
        hud_text(hud, x, body.y + body.h - lh, e->status_err ? HUD_ERR : HUD_OK, st);
    }

    char right[48];
    snprintf(right, sizeof right, "%d:%d%s", cline + 1, ccol + 1, editor_dirty(e) ? "  modified" : "");
    hud_footer(hud, &body, HINT, right);
}
