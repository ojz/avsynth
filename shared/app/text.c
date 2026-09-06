#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3_ttf/SDL_ttf.h>

#define FIRST_GLYPH 32
#define LAST_GLYPH  126
#define NGLYPH (LAST_GLYPH - FIRST_GLYPH + 1)

typedef struct Glyph { SDL_Rect src; int advance; } Glyph;

struct AppText {
    SDL_Renderer *ren;
    TTF_Font     *font;
    SDL_Texture  *atlas;
    Glyph         glyphs[NGLYPH];
    int           line_h, char_w;
    char          source[1024];
};

/* Where the lab keeps its face: next to a packaged exe, or at the root of a
 * checkout when the exe is in build/bin. The first .ttf or .otf wins, so
 * shipping the typeface is a matter of putting one file there. */
static int find_lab_font(char *out, size_t cap)
{
    const char *base = SDL_GetBasePath();
    if (!base) return 0;
    static const char *const REL[] = { "fonts", "assets/fonts", "../../assets/fonts", NULL };
    static const char *const PAT[] = { "*.ttf", "*.otf", NULL };
    for (int i = 0; REL[i]; i++) {
        char dir[1024];
        snprintf(dir, sizeof dir, "%s%s", base, REL[i]);
        for (int j = 0; PAT[j]; j++) {
            int n = 0;
            char **found = SDL_GlobDirectory(dir, PAT[j], SDL_GLOB_CASEINSENSITIVE, &n);
            if (found && n > 0) {
                snprintf(out, cap, "%s/%s", dir, found[0]);
                SDL_free(found);
                return 1;
            }
            if (found) SDL_free(found);
        }
    }
    return 0;
}

/* Until the lab ships a face, a system monospace stands in. */
static const char *const SYSTEM_FONTS[] = {
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

static int build_atlas(AppText *t, float px)
{
    t->line_h = TTF_GetFontHeight(t->font);

    int total_w = 0, maxadv = 0;
    for (int c = FIRST_GLYPH; c <= LAST_GLYPH; c++) {
        int minx, maxx, miny, maxy, adv;
        if (!TTF_GetGlyphMetrics(t->font, (Uint32)c, &minx, &maxx, &miny, &maxy, &adv)) adv = (int)px;
        Glyph *g = &t->glyphs[c - FIRST_GLYPH];
        g->advance = adv;
        g->src = (SDL_Rect){ total_w, 0, adv + 2, t->line_h };
        total_w += adv + 2;
        if (adv > maxadv) maxadv = adv;
    }
    t->char_w = maxadv;

    SDL_Surface *atlas = SDL_CreateSurface(total_w, t->line_h, SDL_PIXELFORMAT_ARGB8888);
    if (!atlas) return -1;
    SDL_FillSurfaceRect(atlas, NULL, 0);
    SDL_Color white = { 255, 255, 255, 255 };
    for (int c = FIRST_GLYPH; c <= LAST_GLYPH; c++) {
        SDL_Surface *gs = TTF_RenderGlyph_Blended(t->font, (Uint32)c, white);
        if (!gs) continue;
        /* SDL3_ttf renders the glyph into its full advance cell, bearing
         * included, so the surface goes at the cell's left as it is. */
        SDL_Rect dst = t->glyphs[c - FIRST_GLYPH].src;
        /* The one liberty taken with the face: B612 Mono sets its full stop,
         * comma, colon and semicolon at the far left of the cell, which reads
         * as a gap after every decimal point ("110. 0 Hz"). Centre those four
         * in their cell, because the readouts are what this lab stares at.
         * Any face whose punctuation is already centred is left alone. */
        if (c == '.' || c == ',' || c == ':' || c == ';') {
            int minx, maxx, miny, maxy, adv;
            if (TTF_GetGlyphMetrics(t->font, (Uint32)c, &minx, &maxx, &miny, &maxy, &adv) && maxx > minx)
                dst.x += (adv - (maxx - minx)) / 2 - minx;
        }
        SDL_SetSurfaceBlendMode(gs, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(gs, NULL, atlas, &dst);
        SDL_DestroySurface(gs);
    }
    t->atlas = SDL_CreateTextureFromSurface(t->ren, atlas);
    SDL_DestroySurface(atlas);
    if (!t->atlas) return -1;
    SDL_SetTextureBlendMode(t->atlas, SDL_BLENDMODE_BLEND);
    return 0;
}

AppText *apptext_create(SDL_Renderer *r, float px)
{
    AppText *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->ren = r;
    t->line_h = (int)(px + 2);   /* metrics for a face that failed to load */
    t->char_w = (int)(px * 0.6f);

    if (TTF_WasInit() == 0 && !TTF_Init()) {
        fprintf(stderr, "text: TTF_Init: %s\n", SDL_GetError());
        return t;
    }
    char path[1200];
    if (find_lab_font(path, sizeof path)) {
        t->font = TTF_OpenFont(path, px);
        if (t->font) snprintf(t->source, sizeof t->source, "%s", path);
    }
    if (!t->font) {
        for (int i = 0; SYSTEM_FONTS[i]; i++) {
            t->font = TTF_OpenFont(SYSTEM_FONTS[i], px);
            if (t->font) {
                snprintf(t->source, sizeof t->source, "%s (system stand-in: no face in assets/fonts)", SYSTEM_FONTS[i]);
                break;
            }
        }
    }
    if (!t->font) {
        fprintf(stderr, "text: no typeface found; the interface has no text\n");
        return t;
    }
    if (build_atlas(t, px) != 0) fprintf(stderr, "text: atlas failed: %s\n", SDL_GetError());
    return t;
}

void apptext_destroy(AppText *t)
{
    if (!t) return;
    if (t->atlas) SDL_DestroyTexture(t->atlas);
    if (t->font) TTF_CloseFont(t->font);
    free(t);
}

const char *apptext_source(const AppText *t) { return t->source[0] ? t->source : "none"; }
int apptext_line_h(const AppText *t) { return t->line_h; }
int apptext_char_w(const AppText *t) { return t->char_w; }

int apptext_width(const AppText *t, const char *s)
{
    if (!t->atlas) return (int)strlen(s) * t->char_w;
    int w = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < FIRST_GLYPH || c > LAST_GLYPH) c = 63; /* '?' */
        w += t->glyphs[c - FIRST_GLYPH].advance;
    }
    return w;
}

void apptext_draw(AppText *t, float x, float y, const char *s, SDL_Color col)
{
    if (!t->atlas) return;
    /* Whole pixels: a glyph drawn between two pixels is blurred twice. */
    x = SDL_floorf(x);
    y = SDL_floorf(y);
    SDL_SetTextureColorMod(t->atlas, col.r, col.g, col.b);
    SDL_SetTextureAlphaMod(t->atlas, col.a);
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < FIRST_GLYPH || c > LAST_GLYPH) c = 63;
        const Glyph *g = &t->glyphs[c - FIRST_GLYPH];
        SDL_FRect src = { (float)g->src.x, (float)g->src.y, (float)g->src.w, (float)g->src.h };
        SDL_FRect dst = { x, y, (float)g->src.w, (float)g->src.h };
        SDL_RenderTexture(t->ren, t->atlas, &src, &dst);
        x += (float)g->advance;
    }
}

static void ui_draw(void *ud, float x, float y, const char *s, SDL_Color c) { apptext_draw(ud, x, y, s, c); }
static float ui_width(void *ud, const char *s) { return (float)apptext_width(ud, s); }
static float ui_height(void *ud) { return (float)((const AppText *)ud)->line_h; }

UiText apptext_ui(AppText *t)
{
    UiText u = { ui_draw, ui_width, ui_height, t };
    return u;
}
