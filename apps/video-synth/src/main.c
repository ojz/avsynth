/*
 * vsynth — video feedback synthesizer. See PRD.md.
 *
 * Screen region -> user-written libavfilter chain -> borderless window.
 * The chain is text (ffmpeg -filter_complex syntax) edited in an overlay;
 * knobs are derived from it. A project (SQLite) holds chains, each with ten
 * presets of knob values. The capture region is picked by dragging a
 * rectangle on a translucent overlay.
 *
 *   vsynth [--project FILE] [--region X,Y,W,H] [--fps N] [--win X,Y,W,H]
 *          [--vf "chain"] [--selftest] [--screenshot FILE.bmp]
 *
 *   Geometry comes from the project file when present; CLI flags override it.
 *   --vf runs the given chain instead of the project's current one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <libavutil/frame.h>

#include "window.h"
#include "voice.h"
#include "rack.h"
#include "project.h"
#include "picker.h"
#include "hud.h"
#include "editor.h"

/* ---- app state, one instance ---- */

typedef struct App {
    VoiceConfig cfg;
    Project *proj;
    Window  *win;
    Hud     *hud;
    Editor  *ed;
    Voice   *voice;
    Rack     rack;

    int   chain_id;
    char  chain_name[64];
    char  chain_text[RACK_CHAIN_CAP];
    int   preset_slot;   /* last loaded/saved slot, for the title */
} App;

static int parse_rect(const char *s, int *x, int *y, int *w, int *h)
{
    return sscanf(s, "%d,%d,%d,%d", x, y, w, h) == 4 ? 0 : -1;
}

static void show_status(App *a)
{
    char buf[256], title[320];
    rack_describe_selected(&a->rack, buf, sizeof buf);
    if (a->preset_slot > 0)
        snprintf(title, sizeof title, "%s P%d  %s", a->chain_name, a->preset_slot, buf);
    else
        snprintf(title, sizeof title, "%s  %s", a->chain_name, buf);
    window_set_title(a->win, title);
    fprintf(stderr, "%s\n", title);
}

static void notice(App *a, const char *msg)
{
    window_set_title(a->win, msg);
    hud_notice(a->hud, msg);
    fprintf(stderr, "%s\n", msg);
}

static void overlay(SDL_Renderer *ren, int w, int h, void *ud)
{
    App *a = ud;
    hud_draw(ren, w, h, a->hud);
    editor_draw(a->ed, ren, w, h);
}

/* Tear the voice down and bring it back on the current geometry and chain,
 * then push the rack's values so knob positions survive the restart. */
static int restart_voice(App *a)
{
    voice_stop(a->voice);
    a->cfg.chain = a->rack.chain;
    a->voice = voice_start(&a->cfg);
    if (!a->voice) return -1;
    rack_send_all(&a->rack, a->voice);
    if (a->hud) {
        hud_set_tap_count(a->hud, a->rack.ntaps, a->rack.tap_names);
        hud_set_capture(a->hud, a->cfg.cap_x, a->cfg.cap_y, a->cfg.cap_w, a->cfg.cap_h);
    }
    return 0;
}

/* ---- starter content ---- */

static void set_knob(Rack *r, const char *mod, const char *opt, double v)
{
    int m = rack_find_module(r, mod), k = rack_find_knob(r, m, opt);
    if (m >= 0 && k >= 0) r->values[m][k] = v;
}

static void set_on(Rack *r, const char *mod, int on)
{
    int m = rack_find_module(r, mod);
    if (m >= 0) r->enabled[m] = on;
}

static void neutral(Rack *r)
{
    for (int m = 0; m < r->nmods; m++) {
        r->enabled[m] = r->mods[m].enabled_default;
        for (int k = 0; k < r->mods[m].nknobs; k++)
            r->values[m][k] = r->mods[m].knobs[k].neutral;
    }
}

/* Presets for the "rack" chain, ported from the feedback.ps1 presets. */
static void seed_rack_presets(Project *proj, int chain_id, Rack *r)
{
    neutral(r);
    project_save_preset(proj, chain_id, 1, "init", r);

    neutral(r); set_knob(r, "rot", "angle", 0.026);
    project_save_preset(proj, chain_id, 2, "spin", r);

    neutral(r); set_knob(r, "zoom", "w", 1.02); set_knob(r, "rot", "angle", 0.014); set_knob(r, "hue", "s", 1.15);
    project_save_preset(proj, chain_id, 3, "tunnel", r);

    neutral(r); set_knob(r, "trail", "decay", 0.94);
    project_save_preset(proj, chain_id, 4, "trail", r);

    neutral(r); set_on(r, "neg", 1);
    project_save_preset(proj, chain_id, 5, "invert", r);

    neutral(r); set_on(r, "edge", 1);
    project_save_preset(proj, chain_id, 6, "edge", r);

    neutral(r); set_knob(r, "shift", "rh", 4); set_knob(r, "shift", "bh", -4); set_knob(r, "zoom", "w", 0.98);
    project_save_preset(proj, chain_id, 7, "chroma", r);

    neutral(r); set_knob(r, "trail", "decay", 0.9); set_on(r, "blur", 1); set_knob(r, "blur", "sigma", 1.5);
    set_knob(r, "zoom", "w", 1.03);
    project_save_preset(proj, chain_id, 8, "melt", r);

    neutral(r);
}

static const char CHAIN_MIRROR[] =
    "crop=iw/2:ih:0:0,split[a][b];\n"
    "[b]hflip[c];\n"
    "[a][c]hstack,\n"
    "hue@hue=h=0:s=1:b=0,\n"
    "rotate@rot=angle=0:c=black:ow=iw:oh=ih";

static const char CHAIN_KALEIDO[] =
    "crop=iw/2:ih/2:0:0,split[a][b];\n"
    "[b]hflip[c];\n"
    "[a][c]hstack,split=3[d][e][half];\n"
    "[e]vflip[f];\n"
    "[d][f]vstack,\n"
    "lagfun@trail=decay=0.5,\n"
    "eq@eq=contrast=1:saturation=1.2";

static void seed_project(App *a)
{
    int id = project_chain_add(a->proj, "rack", RACK_DEFAULT_CHAIN);
    Rack r;
    char err[GRAPH_ERR_CAP];
    if (id > 0 && rack_from_chain(&r, RACK_DEFAULT_CHAIN, a->cfg.cap_w, a->cfg.cap_h, err, sizeof err) == 0)
        seed_rack_presets(a->proj, id, &r);
    else
        fprintf(stderr, "seed: default chain failed: %s\n", err);
    project_chain_add(a->proj, "mirror", CHAIN_MIRROR);
    project_chain_add(a->proj, "kaleido", CHAIN_KALEIDO);
    project_set_current_chain(a->proj, id);
    fprintf(stderr, "seeded chains: rack (8 presets), mirror, kaleido\n");
}

/* ---- chains ---- */

/* Make chain `id` current: derive the rack, restart the voice. On a parse
 * failure the running chain stays and the editor opens on the broken text. */
static int switch_chain(App *a, int id)
{
    char name[64], text[RACK_CHAIN_CAP];
    if (project_chain_get(a->proj, id, name, sizeof name, text, sizeof text) < 0) return -1;

    char err[GRAPH_ERR_CAP];
    Rack r;
    if (rack_from_chain(&r, text, a->cfg.cap_w, a->cfg.cap_h, err, sizeof err) < 0) {
        fprintf(stderr, "chain %s: %s\n", name, err);
        editor_open(a->ed, text, name);
        editor_set_status(a->ed, err, 1);
        return -1;
    }
    a->rack = r;
    a->chain_id = id;
    snprintf(a->chain_name, sizeof a->chain_name, "%s", name);
    snprintf(a->chain_text, sizeof a->chain_text, "%s", text);
    a->preset_slot = 0;
    project_set_current_chain(a->proj, id);
    hud_set_chain_name(a->hud, name);
    hud_set_patch(a->hud, 0);
    if (project_load_preset(a->proj, id, 1, &a->rack) == 0) { a->preset_slot = 1; hud_set_patch(a->hud, 1); }
    return restart_voice(a);
}

static void step_chain(App *a, int dir)
{
    ChainInfo list[64];
    int n = project_chain_list(a->proj, list, 64);
    if (n <= 1) { notice(a, "only one chain (ctrl+n adds one)"); return; }
    int cur = 0;
    for (int i = 0; i < n; i++) if (list[i].id == a->chain_id) cur = i;
    int next = (cur + dir + n) % n;
    if (switch_chain(a, list[next].id) == 0) {
        char msg[128];
        snprintf(msg, sizeof msg, "chain %d/%d: %s", next + 1, n, a->chain_name);
        notice(a, msg);
    }
}

static void new_chain(App *a)
{
    char name[64];
    snprintf(name, sizeof name, "chain %d", project_chain_count(a->proj) + 1);
    int id = project_chain_add(a->proj, name, a->chain_text);
    if (id <= 0) { notice(a, "could not add chain"); return; }
    if (switch_chain(a, id) == 0) {
        notice(a, "new chain (copy of the current one); e edits it");
        editor_open(a->ed, a->chain_text, a->chain_name);
    }
}

/* Editor said apply: validate, and only then swap racks and restart. */
static void apply_editor(App *a)
{
    const char *text = editor_text(a->ed);
    char err[GRAPH_ERR_CAP];
    Rack r;
    if (rack_from_chain(&r, text, a->cfg.cap_w, a->cfg.cap_h, err, sizeof err) < 0) {
        editor_set_status(a->ed, err, 1);
        fprintf(stderr, "chain error: %s\n", err);
        return;
    }
    a->rack = r;
    snprintf(a->chain_text, sizeof a->chain_text, "%s", text);
    project_chain_set_text(a->proj, a->chain_id, text);
    a->preset_slot = 0;
    hud_set_patch(a->hud, 0);
    if (restart_voice(a) < 0) { editor_set_status(a->ed, "voice failed to start", 1); return; }
    char msg[160];
    snprintf(msg, sizeof msg, "applied: %d modules, %d controls, %d tap%s",
             a->rack.nmods, a->rack.ncontrols, a->rack.ntaps, a->rack.ntaps == 1 ? "" : "s");
    editor_set_status(a->ed, msg, 0);
    show_status(a);
}

/* Digit row by physical position, so it works on AZERTY where the digits
 * are shifted: 1..9 -> 1..9, 0 -> 10, else 0 */
static int slot_for_scancode(SDL_Scancode s)
{
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return (int)(s - SDL_SCANCODE_1) + 1;
    if (s == SDL_SCANCODE_0) return 10;
    return 0;
}

static void default_project_path(char *buf, size_t cap)
{
    char *pref = SDL_GetPrefPath("", "vsynth");   /* %APPDATA%\vsynth\ or ~/.local/share/vsynth/ */
    if (pref) {
        snprintf(buf, cap, "%sdefault.vsynth", pref);
        SDL_free(pref);
    } else {
        snprintf(buf, cap, "default.vsynth");
    }
}

/* ---- selftest ---- */

static int selftest(App *a)
{
    int fails = 0;
    char err[GRAPH_ERR_CAP];
    Rack r;

    /* the default chain derives a rack with the knobs we expect */
    if (rack_from_chain(&r, RACK_DEFAULT_CHAIN, 640, 480, err, sizeof err) < 0) {
        fprintf(stderr, "selftest: default chain FAILED: %s\n", err); fails++;
    } else {
        fprintf(stderr, "selftest: default chain -> %d modules, %d controls\n", r.nmods, r.ncontrols);
        for (int i = 0; i < r.ncontrols; i++) {
            Control c = r.controls[i];
            const ModuleDef *md = &r.mods[c.module];
            if (c.knob < 0) fprintf(stderr, "  %-8s bypass\n", md->label);
            else fprintf(stderr, "  %-8s %-12s %g [%g..%g] step %g\n", md->label, md->knobs[c.knob].label,
                         md->knobs[c.knob].neutral, md->knobs[c.knob].min, md->knobs[c.knob].max, md->knobs[c.knob].step);
        }
        int m = rack_find_module(&r, "zoom");
        if (m < 0 || rack_find_knob(&r, m, "w") < 0) { fprintf(stderr, "selftest: zoom knob missing FAILED\n"); fails++; }
        m = rack_find_module(&r, "rot");
        if (m < 0 || rack_find_knob(&r, m, "angle") < 0) { fprintf(stderr, "selftest: rot.angle missing FAILED\n"); fails++; }
        m = rack_find_module(&r, "mix");
        if (m < 0 || r.enabled[m] != 0) { fprintf(stderr, "selftest: mix should start bypassed FAILED\n"); fails++; }
    }

    /* a broken chain is rejected with a reason */
    if (rack_from_chain(&r, "hue=h=0,nosuchfilter=1", 640, 480, err, sizeof err) == 0) {
        fprintf(stderr, "selftest: broken chain accepted FAILED\n"); fails++;
    } else {
        fprintf(stderr, "selftest: broken chain rejected: %s\n", err);
    }

    /* an open output becomes a tap */
    if (rack_from_chain(&r, CHAIN_KALEIDO, 640, 480, err, sizeof err) < 0 || r.ntaps != 1) {
        fprintf(stderr, "selftest: kaleido tap FAILED (%s)\n", err); fails++;
    } else {
        fprintf(stderr, "selftest: kaleido -> %d modules, tap '%s'\n", r.nmods, r.tap_names[0]);
    }
    if (rack_from_chain(&r, CHAIN_MIRROR, 640, 480, err, sizeof err) < 0) {
        fprintf(stderr, "selftest: mirror FAILED (%s)\n", err); fails++;
    }

    /* live path: push every knob through the command path, round-trip a preset */
    rack_send_all(&a->rack, a->voice);
    for (int i = 0; i < a->rack.ncontrols; i++) {
        a->rack.sel = i;
        rack_nudge(&a->rack, a->voice, +1, 1.0);
        rack_nudge(&a->rack, a->voice, -1, 1.0);
    }
    a->rack.sel = 0;
    int ms = rack_find_module(&a->rack, "shift"), ks = rack_find_knob(&a->rack, ms, "rh");
    int mn = rack_find_module(&a->rack, "noise");
    if (ms < 0 || ks < 0 || mn < 0) { fprintf(stderr, "selftest: current chain is not the rack; skipping preset test\n"); }
    else {
        a->rack.values[ms][ks] = 7;
        a->rack.enabled[mn] = 1;
        if (project_save_preset(a->proj, a->chain_id, 10, "selftest", &a->rack) < 0) { fprintf(stderr, "selftest: save FAILED\n"); fails++; }
        rack_reset_all(&a->rack, a->voice);
        int rc = project_load_preset(a->proj, a->chain_id, 10, &a->rack);
        int ok = rc == 0 && a->rack.values[ms][ks] == 7 && a->rack.enabled[mn];
        fprintf(stderr, "selftest: preset round-trip %s (rh=%g noise=%d)\n", ok ? "OK" : "FAILED",
                a->rack.values[ms][ks], a->rack.enabled[mn]);
        if (!ok) fails++;
        rack_send_all(&a->rack, a->voice);
        rack_randomize(&a->rack, a->voice, 0.3);
        rack_set_control(&a->rack, a->voice, 0, 3);
        ok = a->rack.values[a->rack.controls[0].module][a->rack.controls[0].knob] == 3;
        fprintf(stderr, "selftest: set_control %s\n", ok ? "OK" : "FAILED");
        if (!ok) fails++;
    }

    /* restart on a different region */
    a->cfg.cap_w = 640; a->cfg.cap_h = 480;
    if (rack_from_chain(&r, a->rack.chain, a->cfg.cap_w, a->cfg.cap_h, err, sizeof err) == 0) a->rack = r;
    if (restart_voice(a) < 0) { fprintf(stderr, "selftest: restart FAILED\n"); fails++; }
    else fprintf(stderr, "selftest: voice restarted on %dx%d\n", a->cfg.cap_w, a->cfg.cap_h);

    fprintf(stderr, "selftest: %s (%d failure%s)\n", fails ? "FAILED" : "OK", fails, fails == 1 ? "" : "s");
    return fails;
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    static App a;
    a.cfg = (VoiceConfig){ .cap_x = 0, .cap_y = 0, .cap_w = 800, .cap_h = 600, .cap_fps = 30, .chain = NULL };
    int win_x = -1, win_y = -1, win_w = 800, win_h = 600;
    int cli_region = 0, cli_win = 0, cli_fps = 0;
    const char *raw_vf = NULL;
    const char *project_file = NULL;
    int do_selftest = 0;
    const char *screenshot = NULL;   /* --screenshot FILE: dump the window after 2 s */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) { do_selftest = 1; continue; }
        if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) { screenshot = argv[++i]; continue; }
        if (!strcmp(argv[i], "--project") && i + 1 < argc) {
            project_file = argv[++i];
        } else if (!strcmp(argv[i], "--region") && i + 1 < argc) {
            if (parse_rect(argv[++i], &a.cfg.cap_x, &a.cfg.cap_y, &a.cfg.cap_w, &a.cfg.cap_h)) goto usage;
            cli_region = 1;
        } else if (!strcmp(argv[i], "--win") && i + 1 < argc) {
            if (parse_rect(argv[++i], &win_x, &win_y, &win_w, &win_h)) goto usage;
            cli_win = 1;
        } else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
            a.cfg.cap_fps = atoi(argv[++i]);
            cli_fps = 1;
        } else if (!strcmp(argv[i], "--vf") && i + 1 < argc) {
            raw_vf = argv[++i];
        } else {
        usage:
            fprintf(stderr, "usage: %s [--project FILE] [--region X,Y,W,H] [--win X,Y,W,H] "
                            "[--fps N] [--vf CHAIN] [--selftest] [--screenshot FILE.bmp]\n", argv[0]);
            return 2;
        }
    }

    static char path[1024];
    if (!project_file) { default_project_path(path, sizeof path); project_file = path; }

    a.proj = project_open(project_file);
    if (!a.proj) return 1;

    Geometry geo;
    project_load_geometry(a.proj, &geo);
    if (geo.valid) {
        if (!cli_region) { a.cfg.cap_x = geo.cap_x; a.cfg.cap_y = geo.cap_y;
                           a.cfg.cap_w = geo.cap_w; a.cfg.cap_h = geo.cap_h; }
        if (!cli_fps)    a.cfg.cap_fps = geo.cap_fps > 0 ? geo.cap_fps : a.cfg.cap_fps;
        if (!cli_win)    { win_x = geo.win_x; win_y = geo.win_y; win_w = geo.win_w; win_h = geo.win_h; }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_StopTextInput();   /* SDL turns it on by default; the editor turns it on when open */

    a.win = window_create("vsynth", win_x, win_y, win_w, win_h);
    if (!a.win) return 1;
    a.hud = hud_create(window_renderer(a.win), &a.rack);
    if (!a.hud) return 1;
    a.ed = editor_create(a.hud);
    if (!a.ed) return 1;
    window_set_overlay(a.win, overlay, &a);

    if (project_chain_count(a.proj) == 0) seed_project(&a);

    /* pick the chain to run: --vf, else the project's current one, else the first */
    if (raw_vf) {
        char err[GRAPH_ERR_CAP];
        if (rack_from_chain(&a.rack, raw_vf, a.cfg.cap_w, a.cfg.cap_h, err, sizeof err) < 0) {
            fprintf(stderr, "--vf: %s\n", err);
            return 1;
        }
        snprintf(a.chain_name, sizeof a.chain_name, "--vf");
        snprintf(a.chain_text, sizeof a.chain_text, "%s", raw_vf);
        a.chain_id = project_current_chain(a.proj);
        hud_set_chain_name(a.hud, a.chain_name);
        if (restart_voice(&a) < 0) return 1;
    } else {
        int id = project_current_chain(a.proj);
        if (id <= 0) {
            ChainInfo first;
            if (project_chain_list(a.proj, &first, 1) == 1) id = first.id;
        }
        if (id <= 0 || switch_chain(&a, id) < 0) {
            /* fall back to the built-in chain so the window is never empty */
            char err[GRAPH_ERR_CAP];
            if (rack_from_chain(&a.rack, RACK_DEFAULT_CHAIN, a.cfg.cap_w, a.cfg.cap_h, err, sizeof err) < 0) {
                fprintf(stderr, "built-in chain failed: %s\n", err);
                return 1;
            }
            snprintf(a.chain_name, sizeof a.chain_name, "built-in");
            snprintf(a.chain_text, sizeof a.chain_text, "%s", RACK_DEFAULT_CHAIN);
            hud_set_chain_name(a.hud, a.chain_name);
            if (restart_voice(&a) < 0) return 1;
        }
    }

    fprintf(stderr,
        "vsynth: capture %d,%d %dx%d @%dfps, chain '%s'\n"
        "  mouse: left-drag move, right-drag resize; panel: click row, drag bar, wheel nudges\n"
        "  tab/shift+tab select knob, up/down nudge (shift fine, ctrl coarse)\n"
        "  space bypass module, backspace reset knob, r reset all, x randomize (X wild)\n"
        "  1-9,0 load preset, shift+1-9,0 save preset\n"
        "  e edit chain (ctrl+enter applies), pgup/pgdn switch chain, ctrl+n new chain\n"
        "  c pick capture region, h toggle panel, f fullscreen, q quit\n",
        a.cfg.cap_x, a.cfg.cap_y, a.cfg.cap_w, a.cfg.cap_h, a.cfg.cap_fps, a.chain_name);
    show_status(&a);

    if (do_selftest && selftest(&a) != 0) {
        fprintf(stderr, "selftest failed; keeping the window up for inspection\n");
    }

    int running = 1;
    int frames = 0;
    Uint32 fps_t0 = SDL_GetTicks();
    Uint32 shot_at = screenshot ? fps_t0 + 2000 : 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { running = 0; break; }

            if (editor_is_open(a.ed)) {
                int act = editor_handle_event(a.ed, &ev);
                if (act == EDITOR_APPLY) { apply_editor(&a); continue; }
                if (act == EDITOR_CLOSE) { editor_close(a.ed); show_status(&a); continue; }
                if (act == EDITOR_CONSUMED) continue;
            }
            if (hud_handle_event(a.hud, &ev, &a.rack, a.voice)) {
                if (ev.type != SDL_MOUSEMOTION) show_status(&a);
                continue;
            }
            if (window_handle_event(a.win, &ev))
                continue;
            if (ev.type != SDL_KEYDOWN) continue;

            SDL_Keymod mod = SDL_GetModState();
            double factor = (mod & KMOD_SHIFT) ? 0.1 : (mod & KMOD_CTRL) ? 10.0 : 1.0;
            SDL_Keycode key = ev.key.keysym.sym;
            int slot = slot_for_scancode(ev.key.keysym.scancode);
            if (slot) {
                char msg[128];
                if (mod & KMOD_SHIFT) {
                    snprintf(msg, sizeof msg, "preset %d", slot);
                    if (project_save_preset(a.proj, a.chain_id, slot, msg, &a.rack) == 0) {
                        a.preset_slot = slot;
                        snprintf(msg, sizeof msg, "saved preset %d", slot);
                    } else {
                        snprintf(msg, sizeof msg, "save to slot %d FAILED", slot);
                    }
                } else {
                    int rc = project_load_preset(a.proj, a.chain_id, slot, &a.rack);
                    if (rc == 0) {
                        rack_send_all(&a.rack, a.voice);
                        a.preset_slot = slot;
                        snprintf(msg, sizeof msg, "loaded preset %d", slot);
                    } else if (rc == 1) {
                        snprintf(msg, sizeof msg, "slot %d is empty (shift+%d saves)", slot, slot % 10);
                    } else {
                        snprintf(msg, sizeof msg, "load slot %d FAILED", slot);
                    }
                }
                hud_set_patch(a.hud, a.preset_slot);
                notice(&a, msg);
                continue;
            }
            switch (key) {
            case SDLK_ESCAPE:
            case SDLK_q:
                running = 0;
                break;
            case SDLK_f:
                window_toggle_fullscreen(a.win);
                break;
            case SDLK_h:
                hud_toggle(a.hud);
                break;
            case SDLK_e:
                editor_open(a.ed, a.chain_text, a.chain_name);
                break;
            case SDLK_PAGEUP:
                step_chain(&a, -1);
                break;
            case SDLK_PAGEDOWN:
                step_chain(&a, +1);
                break;
            case SDLK_n:
                if (mod & KMOD_CTRL) new_chain(&a);
                break;
            case SDLK_TAB:
                rack_select_next(&a.rack, (mod & KMOD_SHIFT) ? -1 : 1);
                show_status(&a);
                break;
            case SDLK_UP:
            case SDLK_RIGHT:
                rack_nudge(&a.rack, a.voice, +1, factor);
                show_status(&a);
                break;
            case SDLK_DOWN:
            case SDLK_LEFT:
                rack_nudge(&a.rack, a.voice, -1, factor);
                show_status(&a);
                break;
            case SDLK_SPACE:
                rack_toggle_selected(&a.rack, a.voice);
                show_status(&a);
                break;
            case SDLK_BACKSPACE:
                rack_reset_selected(&a.rack, a.voice);
                show_status(&a);
                break;
            case SDLK_r:
                rack_reset_all(&a.rack, a.voice);
                a.preset_slot = 0;
                hud_set_patch(a.hud, 0);
                show_status(&a);
                break;
            case SDLK_x: {
                int wild = (mod & KMOD_SHIFT) != 0;
                rack_randomize(&a.rack, a.voice, wild ? 1.0 : 0.3);
                a.preset_slot = 0;
                hud_set_patch(a.hud, 0);
                notice(&a, wild ? "randomized (wild)" : "randomized");
                break;
            }
            case SDLK_c: {
                PickRect prev = { a.cfg.cap_x, a.cfg.cap_y, a.cfg.cap_w, a.cfg.cap_h }, next;
                if (picker_run(&prev, &next)) {
                    a.cfg.cap_x = next.x; a.cfg.cap_y = next.y;
                    a.cfg.cap_w = next.w; a.cfg.cap_h = next.h;
                    /* re-derive: {W}/{H} and any expression defaults depend on the size */
                    char err[GRAPH_ERR_CAP];
                    Rack r;
                    if (rack_from_chain(&r, a.rack.chain, a.cfg.cap_w, a.cfg.cap_h, err, sizeof err) == 0) {
                        memcpy(r.values, a.rack.values, sizeof r.values);
                        memcpy(r.enabled, a.rack.enabled, sizeof r.enabled);
                        r.sel = a.rack.sel;
                        a.rack = r;
                    }
                    if (restart_voice(&a) < 0) { running = 0; break; }
                    char msg[128];
                    snprintf(msg, sizeof msg, "capture %d,%d %dx%d", a.cfg.cap_x, a.cfg.cap_y, a.cfg.cap_w, a.cfg.cap_h);
                    notice(&a, msg);
                } else {
                    notice(&a, "capture region unchanged");
                }
                break;
            }
            default:
                break;
            }
        }

        if (shot_at && SDL_TICKS_PASSED(SDL_GetTicks(), shot_at)) {
            window_save_bmp(a.win, screenshot);
            shot_at = 0;
        }

        if (a.voice && voice_failed(a.voice)) {
            char msg[GRAPH_ERR_CAP + 32];
            snprintf(msg, sizeof msg, "voice failed: %s", voice_error(a.voice));
            fprintf(stderr, "%s\n", msg);
            if (!editor_is_open(a.ed)) editor_open(a.ed, a.chain_text, a.chain_name);
            editor_set_status(a.ed, msg, 1);
            voice_stop(a.voice);
            a.voice = NULL;
        }

        int drew = 0;
        if (a.voice) {
            for (int t = 0; t < voice_tap_count(a.voice); t++) {
                AVFrame *tf = voice_take_tap(a.voice, t);
                if (tf) { hud_set_tap_frame(a.hud, t, tf->data[0], tf->width, tf->height, tf->linesize[0]); av_frame_free(&tf); }
            }
            AVFrame *f = voice_take_frame(a.voice);
            if (f) {
                window_present_frame(a.win, f->data[0], f->width, f->height, f->linesize[0]);
                av_frame_free(&f);
                drew = 1;
                frames++;
                Uint32 now = SDL_GetTicks();
                if (now - fps_t0 >= 2000) {
                    double fps = frames * 1000.0 / (now - fps_t0);
                    hud_set_fps(a.hud, fps);
                    fprintf(stderr, "fps: %.1f\n", fps);
                    frames = 0;
                    fps_t0 = now;
                }
            }
        }
        if (!drew) {
            window_present(a.win);   /* vsync paces this; keeps window painted during drags */
            SDL_Delay(1);
        }
    }

    Geometry out = { a.cfg.cap_x, a.cfg.cap_y, a.cfg.cap_w, a.cfg.cap_h, a.cfg.cap_fps, 0, 0, 0, 0, 1 };
    window_get_geometry(a.win, &out.win_x, &out.win_y, &out.win_w, &out.win_h);
    project_save_geometry(a.proj, &out);
    fprintf(stderr, "saved geometry: capture %d,%d %dx%d window %d,%d %dx%d\n",
            out.cap_x, out.cap_y, out.cap_w, out.cap_h, out.win_x, out.win_y, out.win_w, out.win_h);

    voice_stop(a.voice);
    editor_destroy(a.ed);
    hud_destroy(a.hud);
    window_destroy(a.win);
    project_close(a.proj);
    SDL_Quit();
    return 0;
}
