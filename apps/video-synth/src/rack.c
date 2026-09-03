#include "rack.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavutil/avstring.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>

/* The old fixed rack as text. {W} and {H} are replaced by the capture size
 * when the graph is built (graph.c); everything else is plain ffmpeg. */
const char RACK_DEFAULT_CHAIN[] =
    "rgbashift@shift=rh=0:rv=0:gh=0:gv=0:bh=0:bv=0,\n"
    "crop@zoom=w=iw/1.0:h=ih/1.0,\n"
    "scale@unzoom={W}:{H},\n"
    "rotate@rot=angle=0:c=black:ow=iw:oh=ih,\n"
    "shear@shear=shx=0:shy=0:c=black,\n"
    "lenscorrection@lens=k1=0:k2=0,\n"
    "lagfun@trail=decay=0,\n"
    "tmix@mix=frames=3:enable=0,\n"
    "tblend@diff=all_mode=difference:all_opacity=1:enable=0,\n"
    "edgedetect@edge=mode=colormix:enable=0,\n"
    "gblur@blur=sigma=0.5:enable=0,\n"
    "unsharp@sharp=5:5:1.5:enable=0,\n"
    "hue@hue=h=0:s=1:b=0,\n"
    "eq@eq=contrast=1:brightness=0:saturation=1:gamma=1,\n"
    "negate@neg=enable=0,\n"
    "vignette@vig=enable=0,\n"
    "noise@noise=alls=20:enable=0";

/* ---------- override table ----------
 * Good ranges for options whose libavfilter bounds are useless, plus virtual
 * knobs (parse != NULL) that read one number out of an expression option and
 * drive one or two options with printf formats. */
typedef struct Override {
    const char *filter, *opt, *label;
    double min, max, step;
    const char *fmt;    /* NULL = %g */
    const char *opt2, *fmt2;
    const char *parse;  /* sscanf format reading the value from the option string */
} Override;

static const Override OVERRIDES[] = {
    { "rgbashift", "rh", "red h",   -64, 64, 1, "%.0f" },
    { "rgbashift", "rv", "red v",   -64, 64, 1, "%.0f" },
    { "rgbashift", "gh", "green h", -64, 64, 1, "%.0f" },
    { "rgbashift", "gv", "green v", -64, 64, 1, "%.0f" },
    { "rgbashift", "bh", "blue h",  -64, 64, 1, "%.0f" },
    { "rgbashift", "bv", "blue v",  -64, 64, 1, "%.0f" },
    { "crop", "w", "zoom", 0.5, 4.0, 0.01, "iw/%.4f", "h", "ih/%.4f", "iw/%lf" },
    { "rotate", "angle", "angle", -3.1416, 3.1416, 0.005 },
    { "shear", "shx", "shear x", -2, 2, 0.01 },
    { "shear", "shy", "shear y", -2, 2, 0.01 },
    { "lenscorrection", "k1", "k1", -1, 1, 0.01 },
    { "lenscorrection", "k2", "k2", -1, 1, 0.01 },
    { "lagfun", "decay", "decay", 0, 1, 0.01 },
    { "tblend", "all_opacity", "opacity", 0, 1, 0.05 },
    { "blend",  "all_opacity", "opacity", 0, 1, 0.05 },
    { "gblur", "sigma", "sigma", 0.1, 30, 0.1 },
    { "gblur", "sigmaV", "sigma v", -1, 30, 0.1 },
    { "hue", "h", "hue",        -180, 180, 1 },
    { "hue", "s", "saturation", 0, 3, 0.02 },
    { "hue", "b", "brightness", -2, 2, 0.02 },
    { "eq", "contrast",   "contrast",   0, 3, 0.02 },
    { "eq", "brightness", "brightness", -1, 1, 0.01 },
    { "eq", "saturation", "saturation", 0, 3, 0.02 },
    { "eq", "gamma",      "gamma",      0.1, 3, 0.02 },
    { "eq", "gamma_r",    "gamma r",    0.1, 3, 0.02 },
    { "eq", "gamma_g",    "gamma g",    0.1, 3, 0.02 },
    { "eq", "gamma_b",    "gamma b",    0.1, 3, 0.02 },
    { "colorbalance", "rs", "red s",   -1, 1, 0.01 },
    { "colorbalance", "gs", "green s", -1, 1, 0.01 },
    { "colorbalance", "bs", "blue s",  -1, 1, 0.01 },
    { "colorbalance", "rm", "red m",   -1, 1, 0.01 },
    { "colorbalance", "gm", "green m", -1, 1, 0.01 },
    { "colorbalance", "bm", "blue m",  -1, 1, 0.01 },
    { "colorbalance", "rh", "red h",   -1, 1, 0.01 },
    { "colorbalance", "gh", "green h", -1, 1, 0.01 },
    { "colorbalance", "bh", "blue h",  -1, 1, 0.01 },
    { "unsharp", "la", "luma amount", -2, 5, 0.1 },
    { "unsharp", "ca", "chroma amount", -2, 5, 0.1 },
    { "boxblur", "lr", "luma radius", 0, 30, 1, "%.0f" },
    { "boxblur", "cr", "chroma radius", 0, 30, 1, "%.0f" },
    { "overlay", "x", "x", -1920, 1920, 1, "%.0f" },
    { "overlay", "y", "y", -1080, 1080, 1, "%.0f" },
    /* step 0: never a knob. scale's size options change the frame size mid-stream. */
    { "scale", "w", "w", 0, 0, 0 },
    { "scale", "h", "h", 0, 0, 0 },
    { "scale", "width", "width", 0, 0, 0 },
    { "scale", "height", "height", 0, 0, 0 },
};

static const Override *find_override(const char *filter, const char *opt)
{
    for (size_t i = 0; i < sizeof OVERRIDES / sizeof OVERRIDES[0]; i++)
        if (!strcmp(OVERRIDES[i].filter, filter) && !strcmp(OVERRIDES[i].opt, opt))
            return &OVERRIDES[i];
    return NULL;
}

/* ---------- what the text mentions ----------
 * A knob appears only for options written in the chain text: "what you
 * wrote, you can turn". Everything else a filter could do live stays hidden
 * until you add it to the text. This keeps the panel the size of your chain.
 *
 * The scan mirrors libavfilter's graph parser: filter segments separated by
 * , and ; with [labels] in between, name[@inst][=args], args split on ':'
 * with av_get_token handling quotes, positional args mapped onto the option
 * list in declaration order (skipping constants and aliases). */

#define WRITTEN_MAX_OPTS 24

typedef struct Written {
    char opts[WRITTEN_MAX_OPTS][32];
    int  n;
} Written;

static void written_add(Written *w, const char *name)
{
    if (w->n < WRITTEN_MAX_OPTS) snprintf(w->opts[w->n++], sizeof w->opts[0], "%s", name);
}

static void written_positional(Written *w, const AVFilter *flt, int pos)
{
    if (!flt || !flt->priv_class) return;
    const AVClass *cls = flt->priv_class;
    const AVOption *o = NULL;
    int i = 0, prev_off = -1;
    while ((o = av_opt_next(&cls, o))) {
        if (o->type == AV_OPT_TYPE_CONST || o->offset == prev_off) continue;
        prev_off = o->offset;
        if (i++ == pos) { written_add(w, o->name); return; }
    }
}

static int scan_written(const char *chain, Written *out, int cap)
{
    const char *p = chain;
    int nf = 0;
    while (*p) {
        while (*p && (av_isspace(*p) || *p == ',' || *p == ';')) p++;
        if (!*p) break;
        if (*p == '[') {
            while (*p && *p != ']') p++;
            if (*p) p++;
            continue;
        }
        char *name = av_get_token(&p, "=,;[ \t\r\n");
        if (!name) break;
        if (!*name) { av_free(name); p++; continue; }
        char *args = NULL;
        if (*p == '=') { p++; args = av_get_token(&p, "[],;"); }

        if (nf < cap) {
            Written *w = &out[nf];
            memset(w, 0, sizeof *w);
            char fname[64];
            snprintf(fname, sizeof fname, "%s", name);
            char *at = strchr(fname, '@');
            if (at) *at = 0;
            const AVFilter *flt = avfilter_get_by_name(fname);
            if (args) {
                const char *q = args;
                int pos = 0;
                while (*q) {
                    char *tok = av_get_token(&q, ":");
                    if (*q == ':') q++;
                    if (!tok) break;
                    char *eq = strchr(tok, '=');
                    if (eq) { *eq = 0; written_add(w, tok); }
                    else if (*tok) written_positional(w, flt, pos++);
                    av_free(tok);
                }
            }
        }
        nf++;
        av_free(name);
        av_free(args);
    }
    return nf;
}

/* Does the text set this option (by any alias name)? */
static int written_has(const Written *w, AVFilterContext *f, const AVOption *o)
{
    for (int i = 0; i < w->n; i++) {
        if (!strcmp(w->opts[i], o->name)) return 1;
        const AVOption *alias = av_opt_find(f->priv, w->opts[i], NULL, 0, 0);
        if (alias && alias->offset == o->offset && alias->type != AV_OPT_TYPE_CONST) return 1;
    }
    return 0;
}

/* ---------- derive ---------- */

static void copy_str(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src ? src : "");
}

/* Read the option's current value as a number. Returns 0 if it is not one. */
static int option_number(AVFilterContext *f, const AVOption *o, double *out)
{
    if (o->type == AV_OPT_TYPE_STRING) {
        uint8_t *s = NULL;
        if (av_opt_get(f->priv, o->name, 0, &s) < 0 || !s) return 0;
        char *end = NULL;
        double v = strtod((const char *)s, &end);
        int ok = end && end != (const char *)s && *end == 0;
        av_free(s);
        if (!ok) return 0;
        *out = v;
        return 1;
    }
    switch (o->type) {
    case AV_OPT_TYPE_INT: case AV_OPT_TYPE_INT64: case AV_OPT_TYPE_UINT64:
    case AV_OPT_TYPE_DOUBLE: case AV_OPT_TYPE_FLOAT: case AV_OPT_TYPE_BOOL:
        return av_opt_get_double(f->priv, o->name, 0, out) >= 0;
    default:
        return 0;
    }
}

static int option_is_int(const AVOption *o)
{
    return o->type == AV_OPT_TYPE_INT || o->type == AV_OPT_TYPE_INT64 ||
           o->type == AV_OPT_TYPE_UINT64 || o->type == AV_OPT_TYPE_BOOL;
}

static void heuristic_range(const AVOption *o, double cur, KnobDef *kd)
{
    int is_int = option_is_int(o);
    double lo = o->type == AV_OPT_TYPE_STRING ? -INFINITY : o->min;
    double hi = o->type == AV_OPT_TYPE_STRING ?  INFINITY : o->max;
    if (o->type == AV_OPT_TYPE_BOOL) { lo = 0; hi = 1; }

    int bounded = isfinite(lo) && isfinite(hi) && hi - lo <= 1000 && hi > lo;
    if (bounded) {
        kd->min = lo; kd->max = hi;
    } else {
        double span = fabs(cur) * 2;
        if (span < 1) span = 1;
        kd->min = cur - span; kd->max = cur + span;
        if (isfinite(lo) && kd->min < lo) kd->min = lo;
        if (isfinite(hi) && kd->max > hi) kd->max = hi;
    }
    if (is_int) {
        kd->step = 1;
        copy_str(kd->fmt, sizeof kd->fmt, "%.0f");
    } else {
        kd->step = (kd->max - kd->min) / 200;
        kd->fmt[0] = 0;
    }
}

static void add_control(Rack *r, int m, int k)
{
    if (r->ncontrols < RACK_MAX_CONTROLS)
        r->controls[r->ncontrols++] = (Control){ m, k };
}

static void module_label(Rack *r, int m, const AVFilterContext *f)
{
    ModuleDef *md = &r->mods[m];
    const char *at = strchr(f->name, '@');
    copy_str(md->label, sizeof md->label, at ? at + 1 : f->filter->name);
    /* two unnamed instances of the same filter: hue, hue#2 */
    int dup = 0;
    for (int i = 0; i < m; i++)
        if (!strcmp(r->mods[i].label, md->label)) dup++;
    if (dup) {
        char l[24];
        snprintf(l, sizeof l, "%.19s#%d", md->label, dup + 1);
        copy_str(md->label, sizeof md->label, l);
    }
}

static void derive_module(Rack *r, AVFilterContext *f, const Written *written)
{
    if (r->nmods >= RACK_MAX_MODULES) return;
    int m = r->nmods++;
    ModuleDef *md = &r->mods[m];
    memset(md, 0, sizeof *md);
    copy_str(md->name, sizeof md->name, f->name);
    copy_str(md->filter, sizeof md->filter, f->filter->name);
    module_label(r, m, f);

    md->bypassable = (f->filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE) != 0;
    md->enabled_default = 1;
    if (md->bypassable) {
        uint8_t *en = NULL;
        if (av_opt_get(f, "enable", 0, &en) >= 0 && en) {
            if (!strcmp((const char *)en, "0")) md->enabled_default = 0;
            av_free(en);
        }
    }
    r->enabled[m] = md->enabled_default;

    const char *covered[RACK_MAX_KNOBS_PER_MODULE * 2];
    int ncovered = 0;
    int offsets[RACK_MAX_KNOBS_PER_MODULE];
    int noffsets = 0;

    if (f->filter->priv_class) {
        const AVOption *o = NULL;
        while ((o = av_opt_next(f->priv, o)) && md->nknobs < RACK_MAX_KNOBS_PER_MODULE) {
            if (o->type == AV_OPT_TYPE_CONST) continue;
            if (!(o->flags & AV_OPT_FLAG_RUNTIME_PARAM)) continue;
            if (written && !written_has(written, f, o)) continue;

            int skip = 0;
            for (int i = 0; i < ncovered; i++) if (!strcmp(covered[i], o->name)) skip = 1;
            for (int i = 0; i < noffsets; i++) if (offsets[i] == o->offset) skip = 1;
            if (skip) continue;

            const Override *ov = find_override(md->filter, o->name);
            KnobDef *kd = &md->knobs[md->nknobs];
            memset(kd, 0, sizeof *kd);
            double cur;

            if (ov && ov->parse) {
                uint8_t *s = NULL;
                if (av_opt_get(f->priv, o->name, 0, &s) < 0 || !s) continue;
                int ok = sscanf((const char *)s, ov->parse, &cur) == 1;
                av_free(s);
                if (!ok) continue;
                copy_str(kd->opt2, sizeof kd->opt2, ov->opt2);
                copy_str(kd->fmt2, sizeof kd->fmt2, ov->fmt2);
                if (ov->opt2 && ncovered < (int)(sizeof covered / sizeof covered[0])) covered[ncovered++] = ov->opt2;
            } else {
                if (!option_number(f, o, &cur)) continue;
            }
            if (ov && ov->step <= 0) continue;   /* explicitly never a knob */

            copy_str(kd->opt, sizeof kd->opt, o->name);
            if (ov) {
                copy_str(kd->label, sizeof kd->label, ov->label);
                kd->min = ov->min; kd->max = ov->max; kd->step = ov->step;
                copy_str(kd->fmt, sizeof kd->fmt, ov->fmt);
                if (cur < kd->min) kd->min = cur;
                if (cur > kd->max) kd->max = cur;
            } else {
                copy_str(kd->label, sizeof kd->label, o->name);
                heuristic_range(o, cur, kd);
            }
            kd->neutral = cur;
            if (o->unit && option_is_int(o)) {
                /* enum: step through the constants by value, show their names */
                kd->cls = f->filter->priv_class;
                kd->unit = o->unit;
                kd->step = 1;
                copy_str(kd->fmt, sizeof kd->fmt, "%.0f");
            }
            r->values[m][md->nknobs] = cur;
            offsets[noffsets++] = o->offset;
            md->nknobs++;
        }
    }

    if (md->nknobs == 0 && md->bypassable) add_control(r, m, -1);
    for (int k = 0; k < md->nknobs; k++) add_control(r, m, k);
}

int rack_from_chain(Rack *r, const char *chain, int cap_w, int cap_h, char *err, size_t err_cap)
{
    static Rack tmp;   /* large; main thread only */
    memset(&tmp, 0, sizeof tmp);
    if (strlen(chain) >= RACK_CHAIN_CAP) {
        if (err) snprintf(err, err_cap, "chain longer than %d bytes", RACK_CHAIN_CAP - 1);
        return -1;
    }

    GraphSpec spec = { chain, cap_w, cap_h, AV_PIX_FMT_BGRA, { 1, 30 } };
    Graph g;
    if (graph_build(&spec, &g, err, err_cap) < 0) return -1;

    copy_str(tmp.chain, sizeof tmp.chain, chain);
    static Written written[RACK_MAX_MODULES];
    int nwritten = scan_written(chain, written, RACK_MAX_MODULES);
    int seg = 0;
    for (unsigned i = 0; i < g.graph->nb_filters; i++) {
        AVFilterContext *f = g.graph->filters[i];
        if (graph_is_helper(f)) continue;
        /* filters are created in text order, so the n-th user filter is the n-th segment */
        derive_module(&tmp, f, seg < nwritten ? &written[seg] : NULL);
        seg++;
    }
    tmp.ntaps = g.ntaps;
    for (int i = 0; i < g.ntaps; i++) copy_str(tmp.tap_names[i], sizeof tmp.tap_names[i], g.tap_names[i]);
    graph_free(&g);

    tmp.sel = 0;
    *r = tmp;
    return 0;
}

int rack_find_module(const Rack *r, const char *label)
{
    for (int m = 0; m < r->nmods; m++)
        if (!strcmp(r->mods[m].label, label) || !strcmp(r->mods[m].name, label)) return m;
    return -1;
}

int rack_find_knob(const Rack *r, int m, const char *opt)
{
    if (m < 0 || m >= r->nmods) return -1;
    for (int k = 0; k < r->mods[m].nknobs; k++)
        if (!strcmp(r->mods[m].knobs[k].opt, opt) || !strcmp(r->mods[m].knobs[k].label, opt)) return k;
    return -1;
}

/* ---------- commands ---------- */

static void format_value(const char *fmt, double v, char *buf, size_t cap)
{
    snprintf(buf, cap, fmt && *fmt ? fmt : "%g", v);
}

void rack_send_knob(const Rack *r, Voice *v, int m, int k)
{
    const ModuleDef *md = &r->mods[m];
    const KnobDef *kd = &md->knobs[k];
    char val[64];
    format_value(kd->fmt, r->values[m][k], val, sizeof val);
    voice_send_command(v, md->name, kd->opt, val);
    if (kd->opt2[0]) {
        format_value(kd->fmt2, r->values[m][k], val, sizeof val);
        voice_send_command(v, md->name, kd->opt2, val);
    }
}

void rack_send_enable(const Rack *r, Voice *v, int m)
{
    const ModuleDef *md = &r->mods[m];
    if (!md->bypassable) return;
    voice_send_command(v, md->name, "enable", r->enabled[m] ? "1" : "0");
}

void rack_send_all(const Rack *r, Voice *v)
{
    for (int m = 0; m < r->nmods; m++) {
        for (int k = 0; k < r->mods[m].nknobs; k++)
            rack_send_knob(r, v, m, k);
        rack_send_enable(r, v, m);
    }
}

/* ---------- editing ---------- */

void rack_select_next(Rack *r, int dir)
{
    if (r->ncontrols == 0) return;
    r->sel = (r->sel + dir + r->ncontrols) % r->ncontrols;
}

static double clampd(double x, double lo, double hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

void rack_nudge(Rack *r, Voice *v, int dir, double factor)
{
    if (r->ncontrols == 0) return;
    Control c = r->controls[r->sel];
    if (c.knob < 0) {
        rack_toggle_selected(r, v);
        return;
    }
    const KnobDef *kd = &r->mods[c.module].knobs[c.knob];
    double *val = &r->values[c.module][c.knob];
    *val = clampd(*val + dir * kd->step * factor, kd->min, kd->max);
    /* kill float dust so "%g" prints cleanly */
    *val = round(*val * 1e6) / 1e6;
    rack_send_knob(r, v, c.module, c.knob);
}

void rack_toggle_selected(Rack *r, Voice *v)
{
    if (r->ncontrols == 0) return;
    Control c = r->controls[r->sel];
    if (!r->mods[c.module].bypassable) return;
    r->enabled[c.module] = !r->enabled[c.module];
    rack_send_enable(r, v, c.module);
}

void rack_reset_selected(Rack *r, Voice *v)
{
    if (r->ncontrols == 0) return;
    Control c = r->controls[r->sel];
    if (c.knob < 0) {
        r->enabled[c.module] = r->mods[c.module].enabled_default;
        rack_send_enable(r, v, c.module);
        return;
    }
    r->values[c.module][c.knob] = r->mods[c.module].knobs[c.knob].neutral;
    rack_send_knob(r, v, c.module, c.knob);
}

void rack_reset_all(Rack *r, Voice *v)
{
    for (int m = 0; m < r->nmods; m++) {
        r->enabled[m] = r->mods[m].enabled_default;
        for (int k = 0; k < r->mods[m].nknobs; k++)
            r->values[m][k] = r->mods[m].knobs[k].neutral;
    }
    rack_send_all(r, v);
}

void rack_format_value(const Rack *r, int m, int k, char *buf, size_t cap)
{
    const KnobDef *kd = &r->mods[m].knobs[k];
    double v = r->values[m][k];
    if (kd->cls && kd->unit) {
        const AVClass *cls = kd->cls;
        const AVOption *c = NULL;
        long long iv = (long long)v;
        while ((c = av_opt_next(&cls, c)))
            if (c->type == AV_OPT_TYPE_CONST && c->unit && !strcmp(c->unit, kd->unit) && c->default_val.i64 == iv) {
                snprintf(buf, cap, "%s", c->name);
                return;
            }
    }
    snprintf(buf, cap, "%g", v);
}

void rack_describe_selected(const Rack *r, char *buf, size_t cap)
{
    if (r->ncontrols == 0) { snprintf(buf, cap, "no controls"); return; }
    Control c = r->controls[r->sel];
    const ModuleDef *md = &r->mods[c.module];
    const char *state = md->bypassable ? (r->enabled[c.module] ? "on" : "OFF") : "fixed";
    if (c.knob < 0) {
        snprintf(buf, cap, "[%d/%d] %s  [%s]", r->sel + 1, r->ncontrols, md->label, state);
    } else {
        const KnobDef *kd = &md->knobs[c.knob];
        char val[48];
        rack_format_value(r, c.module, c.knob, val, sizeof val);
        snprintf(buf, cap, "[%d/%d] %s.%s = %s  [%s]", r->sel + 1, r->ncontrols,
                 md->label, kd->label, val, state);
    }
}

static double snap(const KnobDef *kd, double val)
{
    if (kd->step > 0)
        val = kd->neutral + round((val - kd->neutral) / kd->step) * kd->step;
    val = clampd(val, kd->min, kd->max);
    return round(val * 1e6) / 1e6;
}

void rack_set_control(Rack *r, Voice *v, int control, double value)
{
    if (control < 0 || control >= r->ncontrols) return;
    Control c = r->controls[control];
    if (c.knob < 0) {
        r->enabled[c.module] = value >= 0.5;
        rack_send_enable(r, v, c.module);
        return;
    }
    const KnobDef *kd = &r->mods[c.module].knobs[c.knob];
    r->values[c.module][c.knob] = snap(kd, value);
    rack_send_knob(r, v, c.module, c.knob);
}

void rack_toggle_module(Rack *r, Voice *v, int m)
{
    if (m < 0 || m >= r->nmods || !r->mods[m].bypassable) return;
    r->enabled[m] = !r->enabled[m];
    rack_send_enable(r, v, m);
}

/* xorshift32; seeded from the clock on first use. Quality is irrelevant here. */
static unsigned rng_state;

static double rnd(void)   /* [0,1) */
{
    if (!rng_state) {
        rng_state = (unsigned)time(NULL) ^ ((unsigned)clock() << 8) ^ 0x9e3779b9u;
        if (!rng_state) rng_state = 1;
    }
    unsigned x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x;
    return (x >> 8) / 16777216.0;
}

void rack_randomize(Rack *r, Voice *v, double depth)
{
    depth = clampd(depth, 0, 1);
    for (int m = 0; m < r->nmods; m++) {
        const ModuleDef *md = &r->mods[m];
        if (md->bypassable) {
            if (md->enabled_default)
                r->enabled[m] = rnd() < 0.92;
            else
                r->enabled[m] = rnd() < 0.15 + 0.35 * depth;
        }
        for (int k = 0; k < md->nknobs; k++) {
            const KnobDef *kd = &md->knobs[k];
            double val = kd->neutral;
            if (rnd() < 0.65) {
                double u = rnd() * 2 - 1;                 /* [-1,1] */
                double side = u < 0 ? kd->neutral - kd->min : kd->max - kd->neutral;
                val = kd->neutral + (u < 0 ? -1 : 1) * u * u * side * depth;
            }
            r->values[m][k] = snap(kd, val);
        }
    }
    rack_send_all(r, v);
}
