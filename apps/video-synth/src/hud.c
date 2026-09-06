#include "hud.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"

#define NOTICE_MS 2500
#define SHEET_MAX_W 820
#define PAD 6

/* The lab's palette (shared/ui), by value until the shell hands it over. */
const SDL_Color HUD_TEXT = { 235, 231, 216, 255 };
const SDL_Color HUD_DIM  = { 156, 159, 150, 255 };
const SDL_Color HUD_OFF  = { 104, 108, 102, 255 };
const SDL_Color HUD_SEL  = { 232, 168,  56, 255 };
const SDL_Color HUD_OK   = { 108, 200, 138, 255 };
const SDL_Color HUD_ERR  = { 214,  92,  78, 255 };


/* One control on the panel: the module name column the HUD draws itself,
 * then a shared fader (or a switch) laid out as a row over the rest. */
typedef struct Row {
    SDL_FRect rect;     /* whole row */
    SDL_FRect name;     /* module name column, click toggles bypass */
    SDL_FRect cell;     /* what the fader or switch occupies */
    UiFader   fader;    /* laid out when the control is a fader */
    int control;
} Row;

struct Hud {
    const Rack    *rack;
    const AppHost *host;
    SDL_Renderer  *ren;
    int            line_h, char_w;   /* the lab typeface's, cached for layout */

    enum Mode mode;
    int    patch_slot;
    char   chain_name[64];
    int    chain_index, chain_count;

    SDL_Texture *tap_tex[GRAPH_MAX_TAPS];
    int          tap_w[GRAPH_MAX_TAPS], tap_h[GRAPH_MAX_TAPS];
    char         tap_name[GRAPH_MAX_TAPS][32];
    int          ntaps;
    int    cap_x, cap_y, cap_w, cap_h;
    double fps;
    char   notice[160];
    Uint64 notice_until;

    /* layout from the last draw, for hit testing */
    SDL_Rect  panel;        /* the sheet */
    int       body_top;
    Row       rows[RACK_MAX_CONTROLS];
    int       nrows;
    int       scroll;       /* first control row shown */
    UiControl controls[RACK_MAX_CONTROLS];   /* the rows' faders, for the shell */
    UiSurface surface;
};

/* ---------- text: the lab's typeface, through the shell ---------- */

int hud_text_width(const Hud *h, const char *s)
{
    return (int)app_text_width(h->host, s);
}

void hud_text(Hud *h, int x, int y, SDL_Color col, const char *s)
{
    const UiText *t = &h->host->text;
    if (t->draw) t->draw(t->ud, (float)x, (float)y, s, col);
}

static void textf(Hud *h, int x, int y, SDL_Color col, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    hud_text(h, x, y, col, buf);
}

int  hud_line_h(const Hud *h) { return h->line_h; }
int  hud_char_w(const Hud *h) { return h->char_w; }

static UiText ui_text_for(Hud *h) { return h->host->text; }

void hud_fill(SDL_Renderer *ren, SDL_Rect r, Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    SDL_FRect f = hud_frect(r);
    SDL_SetRenderDrawColor(ren, R, G, B, A);
    SDL_RenderFillRect(ren, &f);
}

void hud_text_input(Hud *h, int on)
{
    SDL_Window *w = SDL_GetRenderWindow(h->ren);
    if (!w) return;
    if (on) SDL_StartTextInput(w);
    else    SDL_StopTextInput(w);
}

/* ---------- lifecycle & state ---------- */

Hud *hud_create(const AppHost *host, const Rack *rack)
{
    Hud *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->rack = rack;
    h->host = host;
    h->ren = host->renderer;
    h->mode = MODE_PANEL;
    h->line_h = (int)app_text_height(host);
    h->char_w = (int)app_text_char_w(host);
    if (h->line_h <= 0) h->line_h = 14;
    if (h->char_w <= 0) h->char_w = 7;
    return h;
}

void hud_destroy(Hud *h)
{
    if (!h) return;
    for (int i = 0; i < GRAPH_MAX_TAPS; i++)
        if (h->tap_tex[i]) SDL_DestroyTexture(h->tap_tex[i]);
    free(h);
}

const UiSurface *hud_surface(const Hud *h)
{
    return (h->mode == MODE_PANEL && h->surface.n > 0) ? &h->surface : NULL;
}

void hud_set_mode(Hud *h, enum Mode m)  { h->mode = m; }
enum Mode hud_mode(const Hud *h)        { return h->mode; }
void hud_set_patch(Hud *h, int slot)    { h->patch_slot = slot; }
void hud_set_fps(Hud *h, double fps)    { h->fps = fps; }
void hud_set_capture(Hud *h, int x, int y, int w, int h_)
{
    h->cap_x = x; h->cap_y = y; h->cap_w = w; h->cap_h = h_;
}
void hud_notice(Hud *h, const char *msg)
{
    snprintf(h->notice, sizeof h->notice, "%s", msg);
    h->notice_until = SDL_GetTicks() + NOTICE_MS;
}
void hud_set_chain(Hud *h, const char *name, int index, int count)
{
    snprintf(h->chain_name, sizeof h->chain_name, "%s", name ? name : "");
    h->chain_index = index;
    h->chain_count = count;
}

void hud_set_tap_count(Hud *h, int n, const char names[][32])
{
    if (n > GRAPH_MAX_TAPS) n = GRAPH_MAX_TAPS;
    for (int i = 0; i < GRAPH_MAX_TAPS; i++) {
        if (h->tap_tex[i]) { SDL_DestroyTexture(h->tap_tex[i]); h->tap_tex[i] = NULL; }
        h->tap_w[i] = h->tap_h[i] = 0;
        if (i < n) snprintf(h->tap_name[i], sizeof h->tap_name[i], "%s", names[i]);
    }
    h->ntaps = n;
}

void hud_set_tap_frame(Hud *h, int tap, const uint8_t *bgra, int w, int h_, int stride)
{
    if (tap < 0 || tap >= h->ntaps) return;
    if (!h->tap_tex[tap] || h->tap_w[tap] != w || h->tap_h[tap] != h_) {
        if (h->tap_tex[tap]) SDL_DestroyTexture(h->tap_tex[tap]);
        h->tap_tex[tap] = SDL_CreateTexture(h->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h_);
        if (!h->tap_tex[tap]) return;
        SDL_SetTextureScaleMode(h->tap_tex[tap], SDL_SCALEMODE_LINEAR);
        h->tap_w[tap] = w;
        h->tap_h[tap] = h_;
    }
    SDL_UpdateTexture(h->tap_tex[tap], NULL, bgra, stride);
}

/* ---------- sheet ---------- */

static const char *TABS[MODE_COUNT] = { "", "F2 knobs", "F3 chain", "F1 help", "F4 project" };
static const enum Mode TAB_ORDER[] = { MODE_HELP, MODE_PANEL, MODE_EDIT, MODE_PROJECT };

SDL_Rect hud_sheet(Hud *h, SDL_Renderer *ren, int W, int H)
{
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    int w = W - 16;
    if (w > SHEET_MAX_W) w = SHEET_MAX_W;
    SDL_Rect sheet = { 8, 8, w, H - 16 };
    hud_fill(ren, sheet, 0, 0, 0, 215);
    SDL_FRect sheet_f = hud_frect(sheet);
    SDL_SetRenderDrawColor(ren, 255, 150, 30, 110);
    SDL_RenderRect(ren, &sheet_f);

    int x = sheet.x + PAD, y = sheet.y + PAD;
    int rowh = h->line_h + 2;

    /* tabs */
    for (size_t i = 0; i < sizeof TAB_ORDER / sizeof TAB_ORDER[0]; i++) {
        enum Mode m = TAB_ORDER[i];
        int tw = hud_text_width(h, TABS[m]) + 10;
        if (m == h->mode) hud_fill(ren, (SDL_Rect){ x - 2, y - 2, tw, rowh + 2 }, HUD_SEL.r, HUD_SEL.g, HUD_SEL.b, 70);
        hud_text(h, x + 3, y, m == h->mode ? HUD_SEL : HUD_DIM, TABS[m]);
        x += tw + 8;
    }

    /* status, right-aligned */
    char st[160];
    int n = snprintf(st, sizeof st, "%.20s", h->chain_name[0] ? h->chain_name : "chain");
    if (h->chain_count > 1) n += snprintf(st + n, sizeof st - n, " %d/%d", h->chain_index, h->chain_count);
    if (h->patch_slot > 0)  n += snprintf(st + n, sizeof st - n, "  P%d", h->patch_slot);
    snprintf(st + n, sizeof st - n, "  %dx%d  %.0f fps", h->cap_w, h->cap_h, h->fps);
    int sw = hud_text_width(h, st);
    if (x + sw < sheet.x + sheet.w - PAD)
        hud_text(h, sheet.x + sheet.w - PAD - sw, y, HUD_DIM, st);

    y += rowh + 2;
    hud_fill(ren, (SDL_Rect){ sheet.x + PAD, y, sheet.w - PAD * 2, 1 }, 255, 255, 255, 60);
    y += 4;

    SDL_Rect body = { sheet.x + PAD, y, sheet.w - PAD * 2, sheet.y + sheet.h - PAD - rowh - 6 - y };
    h->panel = sheet;
    h->body_top = y;
    return body;
}

void hud_footer(Hud *h, const SDL_Rect *body, const char *left, const char *right)
{
    int y = body->y + body->h + 2;
    hud_fill(h->ren, (SDL_Rect){ body->x, y, body->w, 1 }, 255, 255, 255, 60);
    y += 3;
    int cw = h->char_w > 0 ? h->char_w : 7;
    int rw = right ? hud_text_width(h, right) : 0;
    int cols = (body->w - rw - (rw ? 2 * cw : 0)) / cw;   /* left text stops short of the right text */
    if (cols < 0) cols = 0;
    char buf[256];
    if (h->notice[0] && SDL_GetTicks() < h->notice_until) {
        snprintf(buf, sizeof buf, "%.*s", cols, h->notice);
        hud_text(h, body->x, y, HUD_OK, buf);
    } else if (left) {
        snprintf(buf, sizeof buf, "%.*s", cols, left);
        hud_text(h, body->x, y, HUD_DIM, buf);
    }
    if (right) hud_text(h, body->x + body->w - rw, y, HUD_DIM, right);
}

/* ---------- drawing ---------- */

/* Bottom-right of the picture; with a sheet up, top-right of its body, where
 * the rows never reach and the footer stays readable. */
static void draw_taps(Hud *h, SDL_Renderer *ren, int W, int H, int in_sheet)
{
    if (h->ntaps <= 0) return;
    int th = in_sheet ? H / 8 : H / 5;   /* smaller inside the sheet so knob values stay readable */
    if (th < 48) th = 48;
    int tx = in_sheet ? h->panel.x + h->panel.w - PAD : W - 8;
    int ty = in_sheet ? h->body_top : H - 8 - th;
    for (int i = h->ntaps - 1; i >= 0; i--) {
        int tw = h->tap_h[i] > 0 ? th * h->tap_w[i] / h->tap_h[i] : th * 4 / 3;
        tx -= tw;
        SDL_Rect dst = { tx, ty, tw, th };
        SDL_FRect dst_f = hud_frect(dst);
        hud_fill(ren, (SDL_Rect){ dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2 }, 0, 0, 0, 200);
        if (h->tap_tex[i]) SDL_RenderTexture(ren, h->tap_tex[i], NULL, &dst_f);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 60);
        SDL_RenderRect(ren, &dst_f);
        hud_fill(ren, (SDL_Rect){ dst.x, dst.y, hud_text_width(h, h->tap_name[i]) + 6, h->line_h }, 0, 0, 0, 170);
        hud_text(h, dst.x + 3, dst.y, HUD_DIM, h->tap_name[i]);
        tx -= 6;
    }
}

/* Row geometry, once. A row is one line of text plus room for the fader's
 * handle to overhang its track. */
#define ROW_H(h)      ((h)->line_h + 8)
#define NAME_W(h)     ((h)->char_w * 8 + 4)
#define LABEL_W(h)    ((float)((h)->char_w * 13 + 4))
#define VALUE_W(h)    ((float)((h)->char_w * 11))

static void draw_panel(Hud *h, SDL_Renderer *ren, int W, int H)
{
    const Rack *r = h->rack;
    SDL_Rect body = hud_sheet(h, ren, W, H);
    UiText t = ui_text_for(h);
    const UiTheme *th = ui_theme();

    const int rowh = ROW_H(h);
    const int name_w = NAME_W(h);
    /* Rows stop growing past a comfortable width so a wide window does not
     * turn every fader into a metre of track. */
    int row_w = body.w;
    if (row_w > name_w + 520) row_w = name_w + 520;

    int max_rows = body.h / rowh;
    if (max_rows < 1) max_rows = 1;
    int shown = r->ncontrols < max_rows ? r->ncontrols : max_rows;
    int sel = r->set.sel;

    /* keep the selection in view */
    if (sel < h->scroll) h->scroll = sel;
    if (sel >= h->scroll + shown) h->scroll = sel - shown + 1;
    if (h->scroll > r->ncontrols - shown) h->scroll = r->ncontrols - shown;
    if (h->scroll < 0) h->scroll = 0;

    int x = body.x, y = body.y;
    h->nrows = 0;
    if (r->ncontrols == 0) hud_text(h, x, y, HUD_DIM, "no knobs: write options into the chain text (F3)");
    for (int i = h->scroll; i < h->scroll + shown && i < r->ncontrols; i++) {
        Control c = r->controls[i];
        const ModuleDef *md = &r->mods[c.module];
        const Param *p = &r->params[i];
        int on = md->bypassable ? r->enabled[c.module] : 1;
        int selected = (i == sel);

        Row *row = &h->rows[h->nrows];
        UiControl *uc = &h->controls[h->nrows];
        h->nrows++;
        row->control = i;
        row->rect = (SDL_FRect){ (float)body.x, (float)y, (float)row_w, (float)rowh };
        row->name = (SDL_FRect){ (float)x, (float)y, (float)name_w, (float)rowh };
        row->cell = (SDL_FRect){ (float)(x + name_w), (float)y, (float)(row_w - name_w), (float)rowh };

        /* The module name is the HUD's own column: it is not a control, it
         * is where a click bypasses the module. */
        textf(h, x, y + 4, on ? HUD_DIM : HUD_OFF, "%.8s", md->label);

        /* The cell is the shell's: it hit-tests uc->box and drives the fader. */
        uc->param = i;
        uc->box = row->cell;
        if (p->kind == PARAM_FADER) {
            ui_fader_layout_row(&uc->fader, row->cell, LABEL_W(h), VALUE_W(h), &t);
            ui_fader_draw(ren, &t, &uc->fader, p, r->values[i], selected);
        } else {
            ui_stepper_draw_row(ren, &t, row->cell, LABEL_W(h), VALUE_W(h), p, r->values[i], selected);
        }

        /* A bypassed module's controls still show their values but sit
         * behind a wash, so the eye skips them the way the picture does. */
        if (!on) {
            SDL_Color wash = { th->bg.r, th->bg.g, th->bg.b, 150 };
            ui_fill(ren, row->cell, wash);
        }
        y += rowh;
    }

    h->surface.items = h->controls;
    h->surface.n = h->nrows;

    char right[32] = "";
    if (r->ncontrols > shown) snprintf(right, sizeof right, "%d/%d", sel + 1, r->ncontrols);
    hud_footer(h, &body, "tab/arrows fader  ctrl coarse  shift ultra  space bypass  bksp reset  r reset all  x random  1-0 preset  shift+1-0 save", right);
}

void hud_draw(Hud *h, SDL_Renderer *ren, int W, int H)
{
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    /* called last in the overlay, so the taps land on top of whatever sheet is up */
    if (h->mode == MODE_PANEL) {
        draw_panel(h, ren, W, H);
        draw_taps(h, ren, W, H, 1);
        return;
    }
    if (h->mode == MODE_EDIT) draw_taps(h, ren, W, H, 1);
    if (h->mode == MODE_MAIN) draw_taps(h, ren, W, H, 0);
    h->nrows = 0;
    h->surface.n = 0;
    if (h->mode == MODE_MAIN) {
        h->panel = (SDL_Rect){ 0, 0, 0, 0 };
        /* the bare picture still gets transient notices (mode hints, preset loads) */
        if (h->notice[0] && SDL_GetTicks() < h->notice_until) {
            hud_fill(ren, (SDL_Rect){ 8, 8, hud_text_width(h, h->notice) + 12, h->line_h + 4 }, 0, 0, 0, 180);
            hud_text(h, 14, 10, HUD_OK, h->notice);
        }
    }
}

/* ---------- mouse ---------- */

static const Row *row_at(const Hud *h, float x, float y)
{
    for (int i = 0; i < h->nrows; i++)
        if (ui_hit(&h->rows[i].rect, x, y)) return &h->rows[i];
    return NULL;
}

static int in_panel(const Hud *h, float x, float y)
{
    SDL_FRect p = hud_frect(h->panel);
    return ui_hit(&p, x, y);
}

/* What is left of the panel's mouse once the shell has taken the fader
 * gestures (it hit-tests hud_surface() first): the module name column, which
 * bypasses, and swallowing left clicks on the sheet so they do not start a
 * window drag. Right-drag still resizes, even over the panel. Events arrive
 * in renderer coordinates. */
int hud_handle_event(Hud *h, const SDL_Event *ev, Rack *rack, Voice *voice)
{
    if (h->mode != MODE_PANEL) return 0;

    switch (ev->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        float mx = ev->button.x, my = ev->button.y;
        if (ev->button.button != SDL_BUTTON_LEFT) return 0;
        if (!in_panel(h, mx, my)) return 0;
        const Row *row = row_at(h, mx, my);
        if (row && ui_hit(&row->name, mx, my)) {
            rack->set.sel = row->control;
            rack_toggle_module(rack, voice, rack->controls[row->control].module);
        }
        return 1;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return ev->button.button == SDL_BUTTON_LEFT && in_panel(h, ev->button.x, ev->button.y);
    case SDL_EVENT_MOUSE_WHEEL: {
        float wx, wy, mx, my;
        SDL_GetMouseState(&wx, &wy);
        SDL_RenderCoordinatesFromWindow(h->ren, wx, wy, &mx, &my);
        return in_panel(h, mx, my);
    }
    default:
        return 0;
    }
}
