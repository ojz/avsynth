#include "hud.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL_ttf.h>

#define FIRST_GLYPH 32
#define LAST_GLYPH  126
#define NGLYPH (LAST_GLYPH - FIRST_GLYPH + 1)
#define NOTICE_MS 2500

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

    int    visible;
    int    patch_slot;
    char   chain_name[64];

    SDL_Texture *tap_tex[GRAPH_MAX_TAPS];
    int          tap_w[GRAPH_MAX_TAPS], tap_h[GRAPH_MAX_TAPS];
    char         tap_name[GRAPH_MAX_TAPS][32];
    int          ntaps;
    int    cap_x, cap_y, cap_w, cap_h;
    double fps;
    char   notice[160];
    Uint32 notice_until;

    /* layout from the last draw, for hit testing */
    SDL_Rect panel;
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

static int load_font(Hud *h, int px)
{
    if (TTF_WasInit() == 0 && TTF_Init() != 0) {
        fprintf(stderr, "hud: TTF_Init: %s\n", TTF_GetError());
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
    h->line_h = TTF_FontHeight(h->font);

    /* Render every printable ASCII glyph once into a single-row atlas. */
    int total_w = 0, maxadv = 0;
    for (int c = FIRST_GLYPH; c <= LAST_GLYPH; c++) {
        int minx, maxx, miny, maxy, adv;
        if (TTF_GlyphMetrics(h->font, (Uint16)c, &minx, &maxx, &miny, &maxy, &adv) != 0) adv = px;
        Glyph *g = &h->glyphs[c - FIRST_GLYPH];
        g->advance = adv;
        g->src = (SDL_Rect){ total_w, 0, adv + 2, h->line_h };
        total_w += adv + 2;
        if (adv > maxadv) maxadv = adv;
    }
    h->char_w = maxadv;

    SDL_Surface *atlas = SDL_CreateRGBSurfaceWithFormat(0, total_w, h->line_h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!atlas) return -1;
    SDL_FillRect(atlas, NULL, 0);
    SDL_Color white = { 255, 255, 255, 255 };
    for (int c = FIRST_GLYPH; c <= LAST_GLYPH; c++) {
        SDL_Surface *gs = TTF_RenderGlyph_Blended(h->font, (Uint16)c, white);
        if (!gs) continue;
        SDL_Rect dst = h->glyphs[c - FIRST_GLYPH].src;
        SDL_SetSurfaceBlendMode(gs, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(gs, NULL, atlas, &dst);
        SDL_FreeSurface(gs);
    }
    h->atlas = SDL_CreateTextureFromSurface(h->ren, atlas);
    SDL_FreeSurface(atlas);
    if (!h->atlas) return -1;
    SDL_SetTextureBlendMode(h->atlas, SDL_BLENDMODE_BLEND);
    return 0;
}

static int text_width(const Hud *h, const char *s)
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

static void text(Hud *h, int x, int y, SDL_Color col, const char *s)
{
    if (!h->atlas) return;
    SDL_SetTextureColorMod(h->atlas, col.r, col.g, col.b);
    SDL_SetTextureAlphaMod(h->atlas, col.a);
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < FIRST_GLYPH || c > LAST_GLYPH) c = 63;
        const Glyph *g = &h->glyphs[c - FIRST_GLYPH];
        SDL_Rect dst = { x, y, g->src.w, g->src.h };
        SDL_RenderCopy(h->ren, h->atlas, &g->src, &dst);
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
    text(h, x, y, col, buf);
}

/* ---------- lifecycle ---------- */

Hud *hud_create(SDL_Renderer *ren, const Rack *rack)
{
    Hud *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->rack = rack;
    h->ren = ren;
    h->visible = 1;
    h->drag_control = -1;
    if (load_font(h, 13) != 0) {
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

void hud_toggle(Hud *h)              { h->visible = !h->visible; }
int  hud_visible(const Hud *h)       { return h->visible; }
void hud_set_patch(Hud *h, int slot) { h->patch_slot = slot; }
void hud_set_fps(Hud *h, double fps) { h->fps = fps; }
void hud_set_capture(Hud *h, int x, int y, int w, int h_)
{
    h->cap_x = x; h->cap_y = y; h->cap_w = w; h->cap_h = h_;
}
void hud_notice(Hud *h, const char *msg)
{
    snprintf(h->notice, sizeof h->notice, "%s", msg);
    h->notice_until = SDL_GetTicks() + NOTICE_MS;
}
void hud_set_chain_name(Hud *h, const char *name)
{
    snprintf(h->chain_name, sizeof h->chain_name, "%s", name ? name : "");
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
        h->tap_w[tap] = w;
        h->tap_h[tap] = h_;
    }
    SDL_UpdateTexture(h->tap_tex[tap], NULL, bgra, stride);
}

/* ---------- shared text API ---------- */

static void text(Hud *h, int x, int y, SDL_Color col, const char *s);
void hud_text(Hud *h, int x, int y, SDL_Color col, const char *s) { text(h, x, y, col, s); }
int  hud_text_width(const Hud *h, const char *s) { return text_width(h, s); }
int  hud_line_h(const Hud *h) { return h->line_h; }
int  hud_char_w(const Hud *h) { return h->char_w; }

/* ---------- drawing ---------- */

static const SDL_Color C_TEXT   = { 235, 235, 235, 255 };
static const SDL_Color C_DIM    = { 150, 150, 150, 255 };
static const SDL_Color C_OFF    = {  95,  95,  95, 255 };
static const SDL_Color C_SEL    = { 255, 150,  30, 255 };
static const SDL_Color C_NOTICE = { 120, 220, 120, 255 };

static void fill(SDL_Renderer *ren, SDL_Rect r, Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    SDL_SetRenderDrawColor(ren, R, G, B, A);
    SDL_RenderFillRect(ren, &r);
}

void hud_draw(SDL_Renderer *ren, int W, int H, void *ud)
{
    Hud *h = ud;
    if (!h->visible) { h->nrows = 0; h->panel = (SDL_Rect){ 0, 0, 0, 0 }; return; }
    const Rack *r = h->rack;

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    /* tap thumbnails along the bottom edge, right-aligned */
    if (h->ntaps > 0) {
        int th = H / 5;
        if (th < 60) th = 60;
        int tx = W - 8;
        for (int i = h->ntaps - 1; i >= 0; i--) {
            int tw = h->tap_h[i] > 0 ? th * h->tap_w[i] / h->tap_h[i] : th * 4 / 3;
            tx -= tw;
            SDL_Rect dst = { tx, H - 8 - th, tw, th };
            fill(ren, (SDL_Rect){ dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2 }, 0, 0, 0, 200);
            if (h->tap_tex[i]) SDL_RenderCopy(ren, h->tap_tex[i], NULL, &dst);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 60);
            SDL_RenderDrawRect(ren, &dst);
            fill(ren, (SDL_Rect){ dst.x, dst.y, text_width(h, h->tap_name[i]) + 6, h->line_h }, 0, 0, 0, 170);
            text(h, dst.x + 3, dst.y, C_DIM, h->tap_name[i]);
            tx -= 6;
        }
    }

    const int pad = 6, rowh = h->line_h + 2;
    const int name_w  = h->char_w * 8 + 4;
    const int label_w = h->char_w * 11 + 4;
    const int bar_w   = 110;
    const int val_w   = h->char_w * 8;
    static const char *FOOTER[3] = {
        "tab/arrows knob  space bypass  bksp reset  r reset all  x random",
        "1-0 load preset  shift+1-0 save  e edit chain  pgup/pgdn chain",
        "c region  f fullscreen  h hide panel  q quit" };
    int panel_w = pad * 2 + name_w + label_w + bar_w + 8 + val_w;
    for (int i = 0; i < 3; i++)
        if (text_width(h, FOOTER[i]) + pad * 2 > panel_w) panel_w = text_width(h, FOOTER[i]) + pad * 2;
    if (panel_w > W - 16) panel_w = W - 16;

    const int header_rows = 1, footer_rows = 3;
    int avail_h = H - 16 - pad * 2 - (header_rows + footer_rows) * rowh - 8;
    int max_rows = avail_h / rowh;
    if (max_rows < 3) max_rows = 3;
    int shown = r->ncontrols < max_rows ? r->ncontrols : max_rows;

    /* keep the selection in view */
    if (r->sel < h->scroll) h->scroll = r->sel;
    if (r->sel >= h->scroll + shown) h->scroll = r->sel - shown + 1;
    if (h->scroll > r->ncontrols - shown) h->scroll = r->ncontrols - shown;
    if (h->scroll < 0) h->scroll = 0;

    int panel_h = pad * 2 + (header_rows + shown + footer_rows) * rowh + 8;
    h->panel = (SDL_Rect){ 8, 8, panel_w, panel_h };
    fill(ren, h->panel, 0, 0, 0, 215);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 40);
    SDL_RenderDrawRect(ren, &h->panel);

    int x = h->panel.x + pad, y = h->panel.y + pad;

    /* header */
    if (h->notice[0] && !SDL_TICKS_PASSED(SDL_GetTicks(), h->notice_until)) {
        text(h, x, y, C_NOTICE, h->notice);
    } else {
        char hdr[200];
        int n = 0;
        n += snprintf(hdr + n, sizeof hdr - n, "%.16s ", h->chain_name[0] ? h->chain_name : "chain");
        if (h->patch_slot > 0) n += snprintf(hdr + n, sizeof hdr - n, "P%d  ", h->patch_slot);
        else                   n += snprintf(hdr + n, sizeof hdr - n, "--  ");
        snprintf(hdr + n, sizeof hdr - n, "cap %d,%d %dx%d   %.0f fps",
                 h->cap_x, h->cap_y, h->cap_w, h->cap_h, h->fps);
        text(h, x, y, C_DIM, hdr);
    }
    if (r->ncontrols > shown)
        textf(h, x + panel_w - pad * 2 - h->char_w * 5, y, C_DIM, "%d/%d", r->sel + 1, r->ncontrols);
    y += rowh + 2;
    fill(ren, (SDL_Rect){ x, y - 1, panel_w - pad * 2, 1 }, 255, 255, 255, 60);
    y += 2;

    /* rows */
    h->nrows = 0;
    for (int i = h->scroll; i < h->scroll + shown && i < r->ncontrols; i++) {
        Control c = r->controls[i];
        const ModuleDef *md = &r->mods[c.module];
        int on = md->bypassable ? r->enabled[c.module] : 1;
        int selected = (i == r->sel);

        Row *row = &h->rows[h->nrows++];
        row->control = i;
        row->rect = (SDL_Rect){ h->panel.x, y, panel_w, rowh };
        row->name = (SDL_Rect){ x, y, name_w, rowh };
        row->bar  = (SDL_Rect){ x + name_w + label_w, y + 3, bar_w, rowh - 6 };

        if (selected) fill(ren, row->rect, C_SEL.r, C_SEL.g, C_SEL.b, 70);

        SDL_Color tc = on ? (selected ? C_SEL : C_TEXT) : C_OFF;
        SDL_Color nc = on ? C_DIM : C_OFF;

        textf(h, x, y + 1, nc, "%.8s", md->label);

        if (c.knob < 0) {
            text(h, x + name_w, y + 1, tc, on ? "[x] on" : "[ ] off");
        } else {
            const KnobDef *kd = &md->knobs[c.knob];
            text(h, x + name_w, y + 1, tc, kd->label);

            /* bar: track, fill from neutral to value, marker at value */
            double v = r->values[c.module][c.knob];
            double span = kd->max - kd->min;
            double tv = span > 0 ? (v - kd->min) / span : 0;
            double tn = span > 0 ? (kd->neutral - kd->min) / span : 0;
            fill(ren, row->bar, 255, 255, 255, on ? 30 : 15);
            int xv = row->bar.x + (int)(tv * (row->bar.w - 1));
            int xn = row->bar.x + (int)(tn * (row->bar.w - 1));
            SDL_Rect f = { xn < xv ? xn : xv, row->bar.y, abs(xv - xn) + 1, row->bar.h };
            fill(ren, f, tc.r, tc.g, tc.b, on ? 110 : 50);
            fill(ren, (SDL_Rect){ xv - 1, row->bar.y - 1, 3, row->bar.h + 2 }, tc.r, tc.g, tc.b, 255);

            textf(h, row->bar.x + row->bar.w + 8, y + 1, tc, "%g", v);
        }
        y += rowh;
    }

    /* footer */
    y += 3;
    fill(ren, (SDL_Rect){ x, y, panel_w - pad * 2, 1 }, 255, 255, 255, 60);
    y += 4;
    text(h, x, y, C_DIM, FOOTER[0]);
    y += rowh;
    text(h, x, y, C_DIM, FOOTER[1]);
    y += rowh;
    text(h, x, y, C_DIM, FOOTER[2]);
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
    if (!h->visible) return 0;

    switch (ev->type) {
    case SDL_MOUSEBUTTONDOWN: {
        int mx = ev->button.x, my = ev->button.y;
        if (!in_rect(mx, my, &h->panel)) return 0;
        if (ev->button.button != SDL_BUTTON_LEFT) return 1;
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
    case SDL_MOUSEMOTION:
        if (h->drag_control < 0) return in_rect(ev->motion.x, ev->motion.y, &h->panel);
        for (int i = 0; i < h->nrows; i++)
            if (h->rows[i].control == h->drag_control) {
                set_from_bar(rack, voice, &h->rows[i], ev->motion.x);
                break;
            }
        return 1;
    case SDL_MOUSEBUTTONUP:
        if (h->drag_control >= 0) { h->drag_control = -1; return 1; }
        return in_rect(ev->button.x, ev->button.y, &h->panel);
    case SDL_MOUSEWHEEL: {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        const Row *row = row_at(h, mx, my);
        if (!row) return in_rect(mx, my, &h->panel);
        rack->sel = row->control;
        int dir = ev->wheel.y > 0 ? 1 : ev->wheel.y < 0 ? -1 : 0;
        if (dir) {
            SDL_Keymod mod = SDL_GetModState();
            rack_nudge(rack, voice, dir, (mod & KMOD_SHIFT) ? 0.1 : (mod & KMOD_CTRL) ? 10.0 : 1.0);
        }
        return 1;
    }
    default:
        return 0;
    }
}
