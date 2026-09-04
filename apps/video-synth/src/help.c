#include "help.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavfilter/avfilter.h>
#include <libavutil/avstring.h>
#include <libavutil/opt.h>

#define HELP_MAX_FILTERS 600
#define HELP_MAX_SHOWN   256
#define QUERY_CAP 64
#define SNIPPET_CAP 512

typedef struct OptRow {
    char line[256];
    int  is_head;    /* description line, not an option */
} OptRow;

struct Help {
    Hud *hud;
    int  open;

    const AVFilter *filters[HELP_MAX_FILTERS];
    int  nfilters;

    char query[QUERY_CAP];
    int  shown[HELP_MAX_SHOWN];   /* indices into filters */
    int  nshown;
    int  sel, scroll;

    /* detail view */
    const AVFilter *detail;
    OptRow rows[128];
    int  nrows;
    int  dscroll;

    char snippet[SNIPPET_CAP];
};

static const SDL_Color C_TEXT  = { 235, 235, 235, 255 };
static const SDL_Color C_DIM   = { 150, 150, 150, 255 };
static const SDL_Color C_TITLE = { 255, 150,  30, 255 };
static const SDL_Color C_LIVE  = { 120, 220, 120, 255 };

/* ---------- filter list ---------- */

static int is_video_filter(const AVFilter *f)
{
    unsigned ni = avfilter_filter_pad_count(f, 0), no = avfilter_filter_pad_count(f, 1);
    for (unsigned i = 0; i < ni; i++)
        if (avfilter_pad_get_type(f->inputs, i) != AVMEDIA_TYPE_VIDEO) return 0;
    for (unsigned i = 0; i < no; i++)
        if (avfilter_pad_get_type(f->outputs, i) != AVMEDIA_TYPE_VIDEO) return 0;
    if (ni == 0 && no == 0) return 0;
    /* sinks are useless in a chain; sources (0 inputs) are fine as extra inputs */
    if (no == 0 && !(f->flags & AVFILTER_FLAG_DYNAMIC_OUTPUTS)) return 0;
    if (!strncmp(f->name, "a", 1) && f->description && strstr(f->description, "audio")) return 0;
    return 1;
}

static int cmp_name(const void *a, const void *b)
{
    const AVFilter *const *fa = a, *const *fb = b;
    return strcmp((*fa)->name, (*fb)->name);
}

static void build_list(Help *h)
{
    void *it = NULL;
    const AVFilter *f;
    while ((f = av_filter_iterate(&it)) && h->nfilters < HELP_MAX_FILTERS)
        if (is_video_filter(f)) h->filters[h->nfilters++] = f;
    qsort(h->filters, h->nfilters, sizeof h->filters[0], cmp_name);
}

static int contains_ci(const char *hay, const char *needle)
{
    if (!hay) return 0;
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (!av_strncasecmp(hay, needle, n)) return 1;
    return 0;
}

static void filter_list(Help *h)
{
    h->nshown = 0;
    /* names first, then description matches */
    for (int pass = 0; pass < 2 && h->nshown < HELP_MAX_SHOWN; pass++)
        for (int i = 0; i < h->nfilters && h->nshown < HELP_MAX_SHOWN; i++) {
            const AVFilter *f = h->filters[i];
            int name_hit = !h->query[0] || contains_ci(f->name, h->query);
            int desc_hit = h->query[0] && !name_hit && contains_ci(f->description, h->query);
            if ((pass == 0 && name_hit) || (pass == 1 && desc_hit)) h->shown[h->nshown++] = i;
        }
    if (h->sel >= h->nshown) h->sel = h->nshown ? h->nshown - 1 : 0;
    if (h->sel < 0) h->sel = 0;
}

/* ---------- detail view ---------- */

static const char *type_name(enum AVOptionType t)
{
    switch (t) {
    case AV_OPT_TYPE_FLAGS: return "flags";
    case AV_OPT_TYPE_INT: case AV_OPT_TYPE_INT64: case AV_OPT_TYPE_UINT64: return "int";
    case AV_OPT_TYPE_DOUBLE: case AV_OPT_TYPE_FLOAT: return "float";
    case AV_OPT_TYPE_STRING: return "string";
    case AV_OPT_TYPE_RATIONAL: return "ratio";
    case AV_OPT_TYPE_BINARY: return "binary";
    case AV_OPT_TYPE_DICT: return "dict";
    case AV_OPT_TYPE_IMAGE_SIZE: return "size";
    case AV_OPT_TYPE_PIXEL_FMT: return "pixfmt";
    case AV_OPT_TYPE_SAMPLE_FMT: return "samplefmt";
    case AV_OPT_TYPE_VIDEO_RATE: return "rate";
    case AV_OPT_TYPE_DURATION: return "duration";
    case AV_OPT_TYPE_COLOR: return "color";
    case AV_OPT_TYPE_BOOL: return "bool";
    default: return "?";
    }
}

static const char *const_name(const AVClass *cls, const AVOption *o, long long v)
{
    if (!o->unit) return NULL;
    const AVOption *c = NULL;
    while ((c = av_opt_next(&cls, c)))
        if (c->type == AV_OPT_TYPE_CONST && c->unit && !strcmp(c->unit, o->unit) && c->default_val.i64 == v)
            return c->name;
    return NULL;
}

static void format_default(const AVClass *cls, const AVOption *o, char *buf, size_t cap)
{
    switch (o->type) {
    case AV_OPT_TYPE_INT: case AV_OPT_TYPE_INT64: case AV_OPT_TYPE_UINT64: case AV_OPT_TYPE_FLAGS: {
        const char *cn = const_name(cls, o, o->default_val.i64);
        if (cn) snprintf(buf, cap, "%s", cn);
        else snprintf(buf, cap, "%lld", (long long)o->default_val.i64);
        break;
    }
    case AV_OPT_TYPE_BOOL:
        snprintf(buf, cap, "%s", o->default_val.i64 < 0 ? "auto" : o->default_val.i64 ? "true" : "false");
        break;
    case AV_OPT_TYPE_DOUBLE: case AV_OPT_TYPE_FLOAT:
        snprintf(buf, cap, "%g", o->default_val.dbl);
        break;
    case AV_OPT_TYPE_RATIONAL:
        snprintf(buf, cap, "%d/%d", o->default_val.q.num, o->default_val.q.den);
        break;
    default:
        snprintf(buf, cap, "%s", o->default_val.str ? o->default_val.str : "");
        break;
    }
}

/* A number-ish default we can write into a snippet: int/float/bool, or a string that parses. */
static int snippet_value(const AVOption *o, char *buf, size_t cap)
{
    switch (o->type) {
    case AV_OPT_TYPE_INT: case AV_OPT_TYPE_INT64: case AV_OPT_TYPE_UINT64:
        if (o->unit) return 0;   /* enum: leave to the user */
        snprintf(buf, cap, "%lld", (long long)o->default_val.i64); return 1;
    case AV_OPT_TYPE_BOOL:
        if (o->default_val.i64 < 0) return 0;
        snprintf(buf, cap, "%d", o->default_val.i64 ? 1 : 0); return 1;
    case AV_OPT_TYPE_DOUBLE: case AV_OPT_TYPE_FLOAT:
        snprintf(buf, cap, "%g", o->default_val.dbl); return 1;
    case AV_OPT_TYPE_STRING: {
        if (!o->default_val.str) return 0;
        char *end;
        strtod(o->default_val.str, &end);
        if (end == o->default_val.str || *end) return 0;
        snprintf(buf, cap, "%s", o->default_val.str); return 1;
    }
    default: return 0;
    }
}

static void add_row(Help *h, int head, const char *fmt, ...)
{
    if (h->nrows >= (int)(sizeof h->rows / sizeof h->rows[0])) return;
    OptRow *r = &h->rows[h->nrows++];
    r->is_head = head;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->line, sizeof r->line, fmt, ap);
    va_end(ap);
}

static void build_detail(Help *h, const AVFilter *f)
{
    h->detail = f;
    h->nrows = 0;
    h->dscroll = 0;

    unsigned ni = avfilter_filter_pad_count(f, 0), no = avfilter_filter_pad_count(f, 1);
    add_row(h, 1, "%s   %u in -> %u out%s%s", f->name, ni, no,
            (f->flags & AVFILTER_FLAG_SUPPORT_TIMELINE) ? "   bypassable (timeline)" : "",
            (f->flags & AVFILTER_FLAG_SLICE_THREADS) ? "   threaded" : "");
    add_row(h, 1, "%s", f->description ? f->description : "");
    add_row(h, 1, "");

    /* snippet: filter@filter=live numeric options with defaults */
    size_t n = snprintf(h->snippet, sizeof h->snippet, "%s@%s", f->name, f->name);
    int nopts = 0;

    if (!f->priv_class) { add_row(h, 1, "(no options)"); return; }
    add_row(h, 1, "%-18s %-7s %-14s %s", "option", "type", "default", "range   T = live");
    const AVClass *cls = f->priv_class;
    const AVOption *o = NULL;
    int prev_off = -1;
    while ((o = av_opt_next(&cls, o))) {
        if (o->type == AV_OPT_TYPE_CONST) continue;
        if (o->offset == prev_off) continue;   /* alias of the previous option */
        prev_off = o->offset;

        char def[64], range[48] = "";
        format_default(cls, o, def, sizeof def);
        int live = (o->flags & AV_OPT_FLAG_RUNTIME_PARAM) != 0;
        if (o->type == AV_OPT_TYPE_INT || o->type == AV_OPT_TYPE_INT64 || o->type == AV_OPT_TYPE_DOUBLE ||
            o->type == AV_OPT_TYPE_FLOAT || o->type == AV_OPT_TYPE_UINT64) {
            if (o->max - o->min < 1e9) snprintf(range, sizeof range, "%g..%g", o->min, o->max);
        }
        add_row(h, 0, "%-18.18s %-7s %-14.14s %-12s %s %s", o->name, type_name(o->type), def, range,
                live ? "T" : " ", o->help ? o->help : "");

        /* enum constants on a second line */
        if (o->unit) {
            char consts[200] = "";
            size_t c = 0;
            const AVOption *k = NULL;
            while ((k = av_opt_next(&cls, k)) && c < sizeof consts - 4)
                if (k->type == AV_OPT_TYPE_CONST && k->unit && !strcmp(k->unit, o->unit))
                    c += snprintf(consts + c, sizeof consts - c, "%s%s", c ? " " : "", k->name);
            add_row(h, 0, "%-18s   %s", "", consts);
        }

        char val[64];
        if (live && nopts < 8 && snippet_value(o, val, sizeof val) && n + strlen(o->name) + strlen(val) + 3 < sizeof h->snippet) {
            n += snprintf(h->snippet + n, sizeof h->snippet - n, "%s%s=%s", nopts ? ":" : "=", o->name, val);
            nopts++;
        }
    }
}

/* ---------- lifecycle ---------- */

Help *help_create(Hud *hud)
{
    Help *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->hud = hud;
    build_list(h);
    filter_list(h);
    return h;
}

void help_destroy(Help *h) { free(h); }

void help_open(Help *h, const char *query)
{
    h->open = 1;
    h->detail = NULL;
    h->query[0] = 0;
    h->sel = h->scroll = 0;
    /* seed only with an exact filter name (opens its options); any other word
     * under the cursor would just be a bad search */
    if (query && *query)
        for (int i = 0; i < h->nfilters; i++)
            if (!strcmp(h->filters[i]->name, query)) {
                snprintf(h->query, sizeof h->query, "%s", query);
                build_detail(h, h->filters[i]);
                break;
            }
    filter_list(h);
    hud_text_input(h->hud, 1);
}

void help_close(Help *h)
{
    h->open = 0;
    hud_text_input(h->hud, 0);
}

int         help_is_open(const Help *h) { return h->open; }
const char *help_snippet(const Help *h) { return h->snippet; }

/* ---------- events ---------- */

int help_handle_event(Help *h, const SDL_Event *ev)
{
    if (!h->open) return HELP_NONE;
    switch (ev->type) {
    case SDL_EVENT_TEXT_INPUT:
        if (h->detail) return HELP_CONSUMED;
        if (strlen(h->query) + strlen(ev->text.text) < QUERY_CAP) {
            strcat(h->query, ev->text.text);
            h->sel = h->scroll = 0;
            filter_list(h);
        }
        return HELP_CONSUMED;
    case SDL_EVENT_KEY_UP:
        return HELP_CONSUMED;
    case SDL_EVENT_KEY_DOWN:
        switch (ev->key.key) {
        case SDLK_ESCAPE:
            return HELP_CLOSE;
        case SDLK_BACKSPACE:
            if (h->detail) { h->detail = NULL; break; }
            if (h->query[0]) { h->query[strlen(h->query) - 1] = 0; h->sel = h->scroll = 0; filter_list(h); }
            break;
        case SDLK_LEFT:
            if (h->detail) h->detail = NULL;
            break;
        case SDLK_UP:
            if (h->detail) { if (h->dscroll > 0) h->dscroll--; }
            else if (h->sel > 0) h->sel--;
            break;
        case SDLK_DOWN:
            if (h->detail) h->dscroll++;
            else if (h->sel + 1 < h->nshown) h->sel++;
            break;
        case SDLK_PAGEUP:
            if (h->detail) h->dscroll = h->dscroll > 10 ? h->dscroll - 10 : 0;
            else h->sel = h->sel > 10 ? h->sel - 10 : 0;
            break;
        case SDLK_PAGEDOWN:
            if (h->detail) h->dscroll += 10;
            else { h->sel += 10; if (h->sel >= h->nshown) h->sel = h->nshown ? h->nshown - 1 : 0; }
            break;
        case SDLK_RIGHT:
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (h->detail) return ev->key.key == SDLK_RIGHT ? HELP_CONSUMED : HELP_INSERT;
            if (h->nshown) build_detail(h, h->filters[h->shown[h->sel]]);
            break;
        default:
            break;
        }
        return HELP_CONSUMED;
    default:
        return HELP_NONE;
    }
}

/* ---------- drawing ---------- */

void help_draw(Help *h, SDL_Renderer *ren, int W, int H)
{
    if (!h->open) return;
    Hud *hud = h->hud;
    int lh = hud_line_h(hud), cw = hud_char_w(hud);
    if (lh <= 0) lh = 14;
    if (cw <= 0) cw = 7;

    SDL_Rect body = hud_sheet(hud, ren, W, H);
    int x = body.x, y = body.y;
    int cols = body.w / cw;
    if (cols < 8) cols = 8;
    char line[512];

    if (!h->detail) {
        snprintf(line, sizeof line, "search: %s_", h->query);
        hud_text(hud, x, y, C_TITLE, line);
        snprintf(line, sizeof line, "%d filters", h->nshown);
        hud_text(hud, body.x + body.w - hud_text_width(hud, line), y, C_DIM, line);
        y += lh + 4;

        int rows = (body.y + body.h - y) / lh;
        if (rows < 1) rows = 1;
        if (h->sel < h->scroll) h->scroll = h->sel;
        if (h->sel >= h->scroll + rows) h->scroll = h->sel - rows + 1;
        if (h->scroll < 0) h->scroll = 0;
        for (int i = h->scroll; i < h->scroll + rows && i < h->nshown; i++) {
            const AVFilter *f = h->filters[h->shown[i]];
            int ly = y + (i - h->scroll) * lh;
            if (i == h->sel) hud_fill(ren, (SDL_Rect){ body.x, ly, body.w, lh }, C_TITLE.r, C_TITLE.g, C_TITLE.b, 70);
            snprintf(line, sizeof line, "%-16.16s %s%s", f->name,
                     (f->flags & AVFILTER_FLAG_SUPPORT_TIMELINE) ? "T " : "  ",
                     f->description ? f->description : "");
            line[cols < (int)sizeof line - 1 ? cols : (int)sizeof line - 1] = 0;
            hud_text(hud, x, ly, i == h->sel ? C_TITLE : C_TEXT, line);
        }
        hud_footer(hud, &body, "type to search   up/down   enter: options of the filter", "T = bypassable");
    } else {
        int rows = body.h / lh;
        if (rows < 1) rows = 1;
        if (h->dscroll > h->nrows - rows) h->dscroll = h->nrows - rows;
        if (h->dscroll < 0) h->dscroll = 0;
        for (int i = h->dscroll; i < h->dscroll + rows && i < h->nrows; i++) {
            const OptRow *r = &h->rows[i];
            int ly = y + (i - h->dscroll) * lh;
            snprintf(line, sizeof line, "%s", r->line);
            line[cols < (int)sizeof line - 1 ? cols : (int)sizeof line - 1] = 0;
            SDL_Color c = C_TEXT;
            if (i == 0) c = C_TITLE;
            else if (r->is_head) c = C_DIM;
            else if (strstr(r->line, " T ")) c = C_LIVE;
            hud_text(hud, x, ly, c, line);
        }
        snprintf(line, sizeof line, "enter: insert  %s", h->snippet);
        hud_footer(hud, &body, line, "left/bksp: list");
    }
}
