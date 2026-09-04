#include "hud.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3_ttf/SDL_ttf.h>

#define FIRST_GLYPH 32
#define LAST_GLYPH  126
#define NGLYPH (LAST_GLYPH - FIRST_GLYPH + 1)
#define NOTICE_MS 2500
#define SHEET_MAX_W 820
#define PAD 6

const SDL_Color HUD_TEXT = { 235, 235, 235, 255 };
const SDL_Color HUD_DIM  = { 150, 150, 150, 255 };
const SDL_Color HUD_OFF  = {  95,  95,  95, 255 };
const SDL_Color HUD_SEL  = { 255, 150,  30, 255 };
const SDL_Color HUD_OK   = { 120, 220, 120, 255 };
const SDL_Color HUD_ERR  = { 255,  90,  90, 255 };

typedef struct Glyph { SDL_Rect src; int advance; } Glyph;

typedef struct Row {
    SDL_Rect rect;     /* whole row */
    SDL_Rect name;     /* module name column, click toggles bypass */
    SDL_Rect bar;      /* value bar, drag sets */
    int control;
} Row;

struct Hud {
    const Rack   *rack;
    SDL_Renderer *ren;

    TTF_Font     *font;
    SDL_Texture  *atlas;
    Glyph         glyphs[NGLYPH];
    int           line_h, char_w;

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
    SDL_Rect panel;        /* the sheet */
    int      body_top;
    Row      rows[RACK_MAX_CONTROLS];
    int      nrows;
    int      scroll;       /* first control row shown */

    int      drag_control; /* -1 when not dragging a bar */
};

/* ---------- font ---------- */

static const char *FONT_CANDIDATES[] = {
#ifdef _WIN32
    "C:\\Windows\\Fonts\\consola.ttf",
    "C:\\Windows\\Fonts\\lucon.ttf",
    "C:\\Windows\\Fonts\\cour.ttf",
#else
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/LiberationMono-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
#endif
    NULL
};

static int load_font(Hud *h, float px)
{
    if (TTF_WasInit() == 0 && !TTF_Init()) {
        fprintf(stderr, "hud: TTF_Init: %s\n", SDL_GetError());
        return -1;
    }
    for (int i = 0; FONT_CANDIDATES[i]; i++) {
        h->font = TTF_OpenFont(FONT_CANDIDATES[i], px);
        if (h->font) break;
    }
    if (!h->font) {
        fprintf(stderr, "hud: no monospace font found; panel has no text\n");
        return -1;
    }
    h->line_h = TTF_GetFontHeight(h->font);

    /* Render every printable ASCII glyph once into a single-row atlas. */
    int total_w = 0, maxadv = 0;
    for (int c = FIRST_GLYPH; c <= LAST_GLYPH; c++) {
        int minx, maxx, miny, maxy, adv;
        if (!TTF_GetGlyphMetrics(h->font, (Uint32)c, &minx, &maxx, &miny, &maxy, &adv)) adv = (int)px;
        Glyph *g = &h->glyphs[c - FIRST_GLYPH];
        g->advance = adv;
        g->src = (SDL_Rect){ total_w, 0, adv + 2, h->line_h };
        total_w += adv + 2;
        if (adv > maxadv) maxadv = adv;
    }
    h->char_w = maxadv;

    SDL_Surface *atlas = SDL_CreateSurface(total_w, h->line_h, SDL_PIXELFORMAT_ARGB8888);
    if (!atlas) return -1;
    SDL_FillSurfaceRect(atlas, NULL, 0);
    SDL_Color white = { 255, 255, 255, 255 };
    for (int c = FIRST_GLYPH; c <= LAST_GLYPH; c++) {
        SDL_Surface *gs = TTF_RenderGlyph_Blended(h->font, (Uint32)c, white);
        if (!gs) continue;
        SDL_Rect dst = h->glyphs[c - FIRST_GLYPH].src;
        SDL_SetSurfaceBlendMode(gs, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(gs, NULL, atlas, &dst);
        SDL_DestroySurface(gs);
    }
    h->atlas = SDL_CreateTextureFromSurface(h->ren, atlas);
    SDL_DestroySurface(atlas);
    if (!h->atlas) return -1;
    SDL_SetTextureBlendMode(h->atlas, SDL_BLENDMODE_BLEND);
    return 0;
}

int hud_text_width(const Hud *h, const char *s)
{
    if (!h->atlas) return 0;
    int w = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < FIRST_GLYPH || c > LAST_GLYPH) c = 63; /* '?' */
        w += h->glyphs[c - FIRST_GLYPH].advance;
    }
    return w;
}

void hud_text(Hud *h, int x, int y, SDL_Color col, const char *s)
{
    if (!h->atlas) return;
    SDL_SetTextureColorMod(h->atlas, col.r, col.g, col.b);
    SDL_SetTextureAlphaMod(h->atlas, col.a);
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < FIRST_GLYPH || c > LAST_GLYPH) c = 63;
        const Glyph *g = &h->glyphs[c - FIRST_GLYPH];
        SDL_FRect src = hud_frect(g->src);
        SDL_FRect dst = hud_frect((SDL_Rect){ x, y, g->src.w, g->src.h });
        SDL_RenderTexture(h->ren, h->atlas, &src, &dst);
        x += g->advance;
    }
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

Hud *hud_create(SDL_Renderer *ren, const Rack *rack)
{
    Hud *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->rack = rack;
    h->ren = ren;
    h->mode = MODE_PANEL;
    h->drag_control = -1;
    if (load_font(h, 13.0f) != 0) {
        h->line_h = 14;
        h->char_w = 7;
    }
    return h;
}

void hud_destroy(Hud *h)
{
    if (!h) return;
    for (int i = 0; i < GRAPH_MAX_TAPS; i++)
        if (h->tap_tex[i]) SDL_DestroyTexture(h->tap_tex[i]);
    if (h->atlas) SDL_DestroyTexture(h->atlas);
    if (h->font) TTF_CloseFont(h->font);
    free(h);
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

static void draw_panel(Hud *h, SDL_Renderer *ren, int W, int H)
{
    const Rack *r = h->rack;
    SDL_Rect body = hud_sheet(h, ren, W, H);

    const int rowh = h->line_h + 2;
    const int name_w  = h->char_w * 8 + 4;
    const int label_w = h->char_w * 13 + 4;
    int bar_w = body.w - name_w - label_w - h->char_w * 12;
    if (bar_w > 220) bar_w = 220;
    if (bar_w < 60) bar_w = 60;

    int max_rows = body.h / rowh;
    if (max_rows < 1) max_rows = 1;
    int shown = r->ncontrols < max_rows ? r->ncontrols : max_rows;

    /* keep the selection in view */
    if (r->sel < h->scroll) h->scroll = r->sel;
    if (r->sel >= h->scroll + shown) h->scroll = r->sel - shown + 1;
    if (h->scroll > r->ncontrols - shown) h->scroll = r->ncontrols - shown;
    if (h->scroll < 0) h->scroll = 0;

    int x = body.x, y = body.y;
    h->nrows = 0;
    if (r->ncontrols == 0) hud_text(h, x, y, HUD_DIM, "no knobs: write options into the chain text (F3)");
    for (int i = h->scroll; i < h->scroll + shown && i < r->ncontrols; i++) {
        Control c = r->controls[i];
        const ModuleDef *md = &r->mods[c.module];
        int on = md->bypassable ? r->enabled[c.module] : 1;
        int selected = (i == r->sel);

        Row *row = &h->rows[h->nrows++];
        row->control = i;
        row->rect = (SDL_Rect){ body.x, y, body.w, rowh };
        row->name = (SDL_Rect){ x, y, name_w, rowh };
        row->bar  = (SDL_Rect){ x + name_w + label_w, y + 3, bar_w, rowh - 6 };

        if (selected) hud_fill(ren, row->rect, HUD_SEL.r, HUD_SEL.g, HUD_SEL.b, 70);

        SDL_Color tc = on ? (selected ? HUD_SEL : HUD_TEXT) : HUD_OFF;
        SDL_Color nc = on ? HUD_DIM : HUD_OFF;

        textf(h, x, y + 1, nc, "%.8s", md->label);

        if (c.knob < 0) {
            hud_text(h, x + name_w, y + 1, tc, on ? "[x] on" : "[ ] off");
        } else {
            const KnobDef *kd = &md->knobs[c.knob];
            textf(h, x + name_w, y + 1, tc, "%.13s", kd->label);

            /* bar: track, fill from neutral to value, marker at value */
            double v = r->values[c.module][c.knob];
            double span = kd->max - kd->min;
            double tv = span > 0 ? (v - kd->min) / span : 0;
            double tn = span > 0 ? (kd->neutral - kd->min) / span : 0;
            hud_fill(ren, row->bar, 255, 255, 255, on ? 30 : 15);
            int xv = row->bar.x + (int)(tv * (row->bar.w - 1));
            int xn = row->bar.x + (int)(tn * (row->bar.w - 1));
            SDL_Rect f = { xn < xv ? xn : xv, row->bar.y, abs(xv - xn) + 1, row->bar.h };
            hud_fill(ren, f, tc.r, tc.g, tc.b, on ? 110 : 50);
            hud_fill(ren, (SDL_Rect){ xv - 1, row->bar.y - 1, 3, row->bar.h + 2 }, tc.r, tc.g, tc.b, 255);

            char val[48];
            rack_format_value(r, c.module, c.knob, val, sizeof val);
            hud_text(h, row->bar.x + row->bar.w + 8, y + 1, tc, val);
        }
        y += rowh;
    }

    char right[32] = "";
    if (r->ncontrols > shown) snprintf(right, sizeof right, "%d/%d", r->sel + 1, r->ncontrols);
    hud_footer(h, &body, "tab/arrows knob  space bypass  bksp reset  r reset all  x random  1-0 preset  shift+1-0 save", right);
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

static int in_rect(int x, int y, const SDL_Rect *r)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static const Row *row_at(const Hud *h, int x, int y)
{
    for (int i = 0; i < h->nrows; i++)
        if (in_rect(x, y, &h->rows[i].rect)) return &h->rows[i];
    return NULL;
}

static void set_from_bar(Rack *rack, Voice *voice, const Row *row, int mx)
{
    Control c = rack->controls[row->control];
    if (c.knob < 0) return;
    const KnobDef *kd = &rack->mods[c.module].knobs[c.knob];
    double t = (double)(mx - row->bar.x) / (row->bar.w - 1);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    rack_set_control(rack, voice, row->control, kd->min + t * (kd->max - kd->min));
}

int hud_handle_event(Hud *h, const SDL_Event *ev, Rack *rack, Voice *voice)
{
    if (h->mode != MODE_PANEL) return 0;

    switch (ev->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int mx = ev->button.x, my = ev->button.y;
        if (!in_rect(mx, my, &h->panel)) return 0;
        if (ev->button.button != SDL_BUTTON_LEFT) return 0;   /* right-drag resizes, even over the panel */
        const Row *row = row_at(h, mx, my);
        if (!row) return 1;
        Control c = rack->controls[row->control];
        rack->sel = row->control;
        if (in_rect(mx, my, &row->name) || c.knob < 0) {
            rack_toggle_module(rack, voice, c.module);
        } else if (mx >= row->bar.x - 4 && mx < row->bar.x + row->bar.w + 4) {
            h->drag_control = row->control;
            set_from_bar(rack, voice, row, mx);
        }
        return 1;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (h->drag_control < 0) return in_rect(ev->motion.x, ev->motion.y, &h->panel);
        for (int i = 0; i < h->nrows; i++)
            if (h->rows[i].control == h->drag_control) {
                set_from_bar(rack, voice, &h->rows[i], ev->motion.x);
                break;
            }
        return 1;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (h->drag_control >= 0) { h->drag_control = -1; return 1; }
        return ev->button.button == SDL_BUTTON_LEFT && in_rect(ev->button.x, ev->button.y, &h->panel);
    case SDL_EVENT_MOUSE_WHEEL: {
        float fmx, fmy;
        SDL_GetMouseState(&fmx, &fmy);   /* floats in SDL3; the rows are integer */
        int mx = (int)fmx, my = (int)fmy;
        const Row *row = row_at(h, mx, my);
        if (!row) return in_rect(mx, my, &h->panel);
        rack->sel = row->control;
        int dir = ev->wheel.y > 0 ? 1 : ev->wheel.y < 0 ? -1 : 0;
        if (dir) {
            SDL_Keymod mod = SDL_GetModState();
            rack_nudge(rack, voice, dir, (mod & SDL_KMOD_SHIFT) ? 0.1 : (mod & SDL_KMOD_CTRL) ? 10.0 : 1.0);
        }
        return 1;
    }
    default:
        return 0;
    }
}
