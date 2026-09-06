#include "app.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LAB_TEXT_PX 13.0f
#define SCREENSHOT_DELAY_MS 2000

typedef struct App {
    const AppSpec *spec;
    void          *self;
    AppHost        host;
    AppText       *text;

    int   drag;            /* param being dragged, -1 when not */

    /* screenshots, the shell's */
    const char *shot_file;     /* --screenshot */
    Uint64      shot_at;
    char        shots_dir[1024];
    int         shot_n;
} App;

/* ---------- text ---------- */

float app_text_height(const AppHost *h) { return h->text.height ? h->text.height(h->text.ud) : 0.0f; }
float app_text_width(const AppHost *h, const char *s) { return h->text.width ? h->text.width(h->text.ud, s) : 0.0f; }
float app_text_char_w(const AppHost *h) { return (float)apptext_char_w((const AppText *)h->text.ud); }

/* ---------- screenshots ---------- */

int app_save_bmp(AppHost *h, const char *path)
{
    /* SDL3 hands back a fresh surface instead of filling one we allocated. */
    SDL_Surface *s = SDL_RenderReadPixels(h->renderer, NULL);
    if (!s) {
        fprintf(stderr, "screenshot: %s\n", SDL_GetError());
        return -1;
    }
    /* Read the error before destroying the surface: SDL_DestroySurface can
     * overwrite it, so reporting afterwards prints an unrelated message. */
    int rc = SDL_SaveBMP(s, path) ? 0 : -1;
    char err[256] = "";
    if (rc != 0) snprintf(err, sizeof err, "%s", SDL_GetError());
    SDL_DestroySurface(s);
    if (rc == 0) fprintf(stderr, "screenshot saved: %s\n", path);
    else         fprintf(stderr, "screenshot: %s\n", err);
    return rc;
}

static void take_shot(App *a)
{
    char path[1200];
    snprintf(path, sizeof path, "%sshot-%03d.bmp", a->shots_dir, ++a->shot_n);
    app_save_bmp(&a->host, path);
}

/* ---------- gestures: the one gesture table (ROADMAP section 6) ---------- */

static const UiControl *hit(const UiSurface *sf, float x, float y)
{
    if (!sf) return NULL;
    for (int i = 0; i < sf->n; i++)
        if (ui_hit(&sf->items[i].box, x, y)) return &sf->items[i];
    return NULL;
}

static const UiControl *control_for(const UiSurface *sf, int param)
{
    if (!sf) return NULL;
    for (int i = 0; i < sf->n; i++)
        if (sf->items[i].param == param) return &sf->items[i];
    return NULL;
}

static void changed(App *a, int param)
{
    if (a->spec->changed) a->spec->changed(a->self, param);
}

static void select_param(App *a, ParamSet *s, int i)
{
    if (s->sel == i) return;
    s->sel = i;
    changed(a, -1);
}

/* The event is already in renderer coordinates. */
static bool dispatch_mouse(App *a, const SDL_Event *ev)
{
    ParamSet *s = a->spec->params(a->self);
    const UiSurface *sf = a->spec->surface ? a->spec->surface(a->self) : NULL;

    switch (ev->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        /* The right button is never a fader gesture: an app may use it for
         * moving or resizing a borderless window. */
        if (ev->button.button == SDL_BUTTON_RIGHT) return false;
        const UiControl *c = hit(sf, ev->button.x, ev->button.y);
        if (!c) return false;
        int i = c->param;
        select_param(a, s, i);
        if (ui_is_reset_click(ev)) {
            a->drag = -1;
            paramset_reset(s, i);
            changed(a, i);
            return true;
        }
        if (ev->button.button != SDL_BUTTON_LEFT) return true;
        if (s->defs[i].kind == PARAM_FADER) {
            a->drag = i;
            paramset_set(s, i, ui_fader_value_at(&c->fader, &s->defs[i], ev->button.x, ev->button.y));
        } else {
            paramset_step(s, i, +1);
        }
        changed(a, i);
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (a->drag < 0) return false;
        const UiControl *c = control_for(sf, a->drag);
        if (!c) { a->drag = -1; return false; }
        paramset_set(s, a->drag, ui_fader_value_at(&c->fader, &s->defs[a->drag], ev->motion.x, ev->motion.y));
        changed(a, a->drag);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (a->drag < 0) return false;
        a->drag = -1;
        return true;
    case SDL_EVENT_MOUSE_WHEEL: {
        /* The wheel event carries no usable position, so ask for the pointer.
         * That answer is in window coordinates and needs converting. */
        float wx, wy, mx, my;
        SDL_GetMouseState(&wx, &wy);
        SDL_RenderCoordinatesFromWindow(a->host.renderer, wx, wy, &mx, &my);
        const UiControl *c = hit(sf, mx, my);
        if (!c) return false;
        int dir = ev->wheel.y > 0 ? 1 : ev->wheel.y < 0 ? -1 : 0;
        select_param(a, s, c->param);
        if (dir == 0) return true;
        if (s->defs[c->param].kind == PARAM_FADER)
            paramset_nudge(s, c->param, dir, ui_grain(SDL_GetModState()));
        else
            paramset_step(s, c->param, dir);
        changed(a, c->param);
        return true;
    }
    default:
        return false;
    }
}

static bool dispatch_key(App *a, const SDL_Event *ev)
{
    if (ev->type != SDL_EVENT_KEY_DOWN) return false;
    ParamSet *s = a->spec->params(a->self);
    if (s->n <= 0) return false;
    SDL_Keymod mod = SDL_GetModState();
    int i = s->sel;
    switch (ev->key.key) {
    case SDLK_TAB:
        paramset_select(s, (mod & SDL_KMOD_SHIFT) ? -1 : 1);
        changed(a, -1);
        return true;
    case SDLK_BACKSPACE:
        paramset_reset(s, i);
        changed(a, i);
        return true;
    case SDLK_UP:
    case SDLK_RIGHT:
    case SDLK_DOWN:
    case SDLK_LEFT: {
        int dir = (ev->key.key == SDLK_UP || ev->key.key == SDLK_RIGHT) ? 1 : -1;
        if (s->defs[i].kind == PARAM_FADER) paramset_nudge(s, i, dir, ui_grain(mod));
        else paramset_step(s, i, dir);
        changed(a, i);
        return true;
    }
    default:
        return false;
    }
}

static bool is_mouse(const SDL_Event *ev)
{
    return ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP ||
           ev->type == SDL_EVENT_MOUSE_MOTION || ev->type == SDL_EVENT_MOUSE_WHEEL;
}

/* ---------- the shell's own flags ---------- */

static int take_flags(App *a, int argc, char **argv, char **out)
{
    int n = 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0 && !strcmp(argv[i], "--screenshot") && i + 1 < argc) { a->shot_file = argv[++i]; continue; }
        if (i > 0 && !strcmp(argv[i], "--shots") && i + 1 < argc) {
            snprintf(a->shots_dir, sizeof a->shots_dir, "%s", argv[++i]);
            size_t len = strlen(a->shots_dir);
            if (len && a->shots_dir[len - 1] != '\\' && a->shots_dir[len - 1] != '/')
                snprintf(a->shots_dir + len, sizeof a->shots_dir - len, "/");
            continue;
        }
        out[n++] = argv[i];
    }
    out[n] = NULL;
    return n;
}

/* ---------- run ---------- */

int app_run(const AppSpec *spec, int argc, char **argv)
{
    App a = {0};
    a.spec = spec;
    a.drag = -1;

    char **app_argv = calloc((size_t)argc + 1, sizeof *app_argv);
    if (!app_argv) return 1;
    int app_argc = take_flags(&a, argc, argv, app_argv);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | spec->init_flags)) {
        fprintf(stderr, "%s: SDL_Init: %s\n", spec->name, SDL_GetError());
        free(app_argv);
        return 1;
    }
    /* Keep rendering while a borderless window is being moved; the app owns
     * that move loop, so the OS never gets to pause us. */
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

    char *pref = SDL_GetPrefPath("", spec->name);
    snprintf(a.host.data_dir, sizeof a.host.data_dir, "%s", pref ? pref : "");
    if (pref) SDL_free(pref);
    if (!a.shots_dir[0]) snprintf(a.shots_dir, sizeof a.shots_dir, "%s", a.host.data_dir);

    if (!SDL_CreateWindowAndRenderer(spec->title ? spec->title : spec->name,
                                     spec->window_w, spec->window_h,
                                     spec->window_flags | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &a.host.window, &a.host.renderer)) {
        fprintf(stderr, "%s: window: %s\n", spec->name, SDL_GetError());
        SDL_Quit();
        free(app_argv);
        return 1;
    }
    /* vsync is a renderer property in SDL3. It paces the frame loop. */
    SDL_SetRenderVSync(a.host.renderer, 1);
    /* SDL may start with text input on, and it needs the window in SDL3. An
     * app that wants it turns it on. */
    SDL_StopTextInput(a.host.window);

    a.text = apptext_create(a.host.renderer, LAB_TEXT_PX);
    a.host.text = apptext_ui(a.text);
    fprintf(stderr, "%s: renderer %s, typeface %s\n", spec->name,
            SDL_GetRendererName(a.host.renderer), apptext_source(a.text));

    a.self = spec->create(&a.host, app_argc, app_argv);
    if (!a.self) {
        apptext_destroy(a.text);
        SDL_DestroyRenderer(a.host.renderer);
        SDL_DestroyWindow(a.host.window);
        SDL_Quit();
        free(app_argv);
        return 1;
    }

    const UiTheme *th = ui_theme();
    a.shot_at = a.shot_file ? SDL_GetTicks() + SCREENSHOT_DELAY_MS : 0;

    while (!a.host.quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) { a.host.quit = true; break; }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_F12 && !ev.key.repeat) {
                take_shot(&a);
                continue;
            }
            if (is_mouse(&ev)) {
                /* Layout happens in renderer output pixels; SDL reports the
                 * mouse in window coordinates. The two differ on a display
                 * whose pixel density is above 1. Convert once, here. */
                SDL_ConvertEventToRenderCoordinates(a.host.renderer, &ev);
                if (dispatch_mouse(&a, &ev)) continue;
                if (spec->event) spec->event(a.self, &ev);
            } else {
                if (spec->event && spec->event(a.self, &ev)) continue;
                dispatch_key(&a, &ev);
            }
        }

        if (spec->tick) spec->tick(a.self);

        SDL_SetRenderDrawBlendMode(a.host.renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(a.host.renderer, th->bg.r, th->bg.g, th->bg.b, 255);
        SDL_RenderClear(a.host.renderer);
        spec->frame(a.self, a.host.renderer);

        if (a.shot_at && SDL_GetTicks() >= a.shot_at) {
            /* Read back before presenting: what is drawn is what is saved. */
            app_save_bmp(&a.host, a.shot_file);
            a.shot_at = 0;
        }
        SDL_RenderPresent(a.host.renderer);
    }

    spec->destroy(a.self);
    apptext_destroy(a.text);
    SDL_DestroyRenderer(a.host.renderer);
    SDL_DestroyWindow(a.host.window);
    SDL_Quit();
    free(app_argv);
    return 0;
}
