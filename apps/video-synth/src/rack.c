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
 * drive one or two options with printf formats. step is the fine grain. */
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

/* A range for an option the override table does not know. Bounded and not
 * absurd: take libavfilter's. Otherwise a window around the written value. */
static void heuristic_range(const AVOption *o, double cur, Param *p, KnobCmd *cmd, double *step)
{
    int is_int = option_is_int(o);
    double lo = o->type == AV_OPT_TYPE_STRING ? -INFINITY : o->min;
    double hi = o->type == AV_OPT_TYPE_STRING ?  INFINITY : o->max;
    if (o->type == AV_OPT_TYPE_BOOL) { lo = 0; hi = 1; }

    int bounded = isfinite(lo) && isfinite(hi) && hi - lo <= 1000 && hi > lo;
    if (bounded) {
        p->min = lo; p->max = hi;
    } else {
        double span = fabs(cur) * 2;
        if (span < 1) span = 1;
        p->min = cur - span; p->max = cur + span;
        if (isfinite(lo) && p->min < lo) p->min = lo;
        if (isfinite(hi) && p->max > hi) p->max = hi;
    }
    if (is_int) {
        *step = 1;
        copy_str(cmd->fmt, sizeof cmd->fmt, "%.0f");
    } else {
        *step = (p->max - p->min) / 200;
        cmd->fmt[0] = 0;
    }
}

/* The three grains from the one step the table or the heuristic gave. An
 * integer option has no grain below 1: its ultra step is its fine step. */
static void set_grains(Param *p, double step, int is_int)
{
    p->fine   = step;
    p->coarse = step * 10.0;
    p->ultra  = is_int ? step : step / 10.0;
}

/* Fill an enum's constant table from the option's unit. Returns the index of
 * the current value, or -1 if it is not one of the constants. */
static int enum_table(AVFilterContext *f, const AVOption *o, long long cur, KnobCmd *cmd)
{
    const AVClass *cls = f->filter->priv_class;
    const AVOption *c = NULL;
    int at = -1;
    cmd->nenum = 0;
    while ((c = av_opt_next(&cls, c))) {
        if (c->type != AV_OPT_TYPE_CONST || !c->unit || strcmp(c->unit, o->unit)) continue;
        if (cmd->nenum >= RACK_MAX_ENUM) break;
        /* Two names for one value (a long and a short form): keep the first. */
        int dup = 0;
        for (int i = 0; i < cmd->nenum; i++) if (cmd->enum_vals[i] == c->default_val.i64) dup = 1;
        if (dup) continue;
        cmd->enum_names[cmd->nenum] = c->name;
        cmd->enum_vals[cmd->nenum] = c->default_val.i64;
        if (c->default_val.i64 == cur) at = cmd->nenum;
        cmd->nenum++;
    }
    return at;
}

static int add_control(Rack *r, int m, int k)
{
    if (r->ncontrols >= RACK_MAX_CONTROLS) return -1;
    int c = r->ncontrols++;
    r->controls[c] = (Control){ m, k };
    memset(&r->params[c], 0, sizeof r->params[c]);
    memset(&r->cmds[c], 0, sizeof r->cmds[c]);
    r->values[c] = 0;
    return c;
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
    md->bypass_control = -1;
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
            if (ov && ov->step <= 0) continue;   /* explicitly never a knob */

            double cur;
            char opt2[32] = "", fmt2[24] = "";
            if (ov && ov->parse) {
                uint8_t *s = NULL;
                if (av_opt_get(f->priv, o->name, 0, &s) < 0 || !s) continue;
                int ok = sscanf((const char *)s, ov->parse, &cur) == 1;
                av_free(s);
                if (!ok) continue;
                copy_str(opt2, sizeof opt2, ov->opt2);
                copy_str(fmt2, sizeof fmt2, ov->fmt2);
            } else {
                if (!option_number(f, o, &cur)) continue;
            }

            int c = add_control(r, m, md->nknobs);
            if (c < 0) break;
            Param *p = &r->params[c];
            KnobCmd *cmd = &r->cmds[c];
            copy_str(cmd->opt, sizeof cmd->opt, o->name);
            copy_str(cmd->opt2, sizeof cmd->opt2, opt2);
            copy_str(cmd->fmt2, sizeof cmd->fmt2, fmt2);
            if (opt2[0] && ncovered < (int)(sizeof covered / sizeof covered[0])) covered[ncovered++] = ov->opt2;

            copy_str(p->group, sizeof p->group, md->label);
            copy_str(p->key, sizeof p->key, o->name);
            p->kind = PARAM_FADER;

            double step;
            int is_int = option_is_int(o);
            if (ov) {
                copy_str(p->label, sizeof p->label, ov->label);
                p->min = ov->min; p->max = ov->max; step = ov->step;
                copy_str(cmd->fmt, sizeof cmd->fmt, ov->fmt);
                if (cur < p->min) p->min = cur;
                if (cur > p->max) p->max = cur;
                if (ov->fmt && !strcmp(ov->fmt, "%.0f")) is_int = 1;
            } else {
                copy_str(p->label, sizeof p->label, o->name);
                heuristic_range(o, cur, p, cmd, &step);
            }
            p->neutral = cur;
            /* A range that crosses zero fills outward from the written value,
             * so a shift left and a shift right read as what they are. */
            p->taper = (p->min < 0 && p->max > 0) ? PARAM_BIPOLAR : PARAM_LINEAR;
            set_grains(p, step, is_int);

            if (o->unit && option_is_int(o)) {
                /* enum: the fader walks the constants and shows their names */
                int at = enum_table(f, o, (long long)cur, cmd);
                if (at >= 0 && cmd->nenum > 0) {
                    p->kind = PARAM_ENUM;
                    p->taper = PARAM_LINEAR;
                    p->min = 0; p->max = cmd->nenum - 1;
                    p->neutral = at;
                    p->nnames = cmd->nenum;
                    p->coarse = p->fine = p->ultra = 1;   /* one constant per step, whatever the grain */
                    cur = at;
                } else {
                    cmd->nenum = 0;
                    copy_str(cmd->fmt, sizeof cmd->fmt, "%.0f");
                    set_grains(p, 1, 1);
                }
            }
            r->values[c] = cur;
            md->control[md->nknobs] = c;
            offsets[noffsets++] = o->offset;
            md->nknobs++;
        }
    }

    if (md->nknobs == 0 && md->bypassable) {
        int c = add_control(r, m, -1);
        if (c >= 0) {
            Param *p = &r->params[c];
            copy_str(p->group, sizeof p->group, md->label);
            copy_str(p->key, sizeof p->key, "enable");
            copy_str(p->label, sizeof p->label, "enable");
            p->kind = PARAM_SWITCH;
            p->min = 0; p->max = 1;
            p->neutral = md->enabled_default;
            r->values[c] = md->enabled_default;
            md->bypass_control = c;
        }
    }
}

/* Point the parameter set and the enum name tables at this Rack's own
 * storage. Needed after every struct copy. */
static void relink(Rack *r)
{
    for (int c = 0; c < r->ncontrols; c++)
        r->params[c].names = r->cmds[c].nenum ? r->cmds[c].enum_names : NULL;
    r->set.defs = r->params;
    r->set.values = r->values;
    r->set.n = r->ncontrols;
    if (r->set.sel < 0 || r->set.sel >= r->ncontrols) r->set.sel = 0;
}

void rack_adopt(Rack *dst, const Rack *src)
{
    if (dst != src) *dst = *src;
    relink(dst);
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

    param_finish_all(tmp.params, tmp.ncontrols);
    tmp.set.sel = 0;
    tmp.rng_state = r->rng_state;   /* a re-derived rack keeps rolling the same dice */
    rack_adopt(r, &tmp);
    return 0;
}

int rack_find_module(const Rack *r, const char *label)
{
    for (int m = 0; m < r->nmods; m++)
        if (!strcmp(r->mods[m].label, label) || !strcmp(r->mods[m].name, label)) return m;
    return -1;
}

int rack_find_control(const Rack *r, int m, const char *opt)
{
    if (m < 0 || m >= r->nmods) return -1;
    const ModuleDef *md = &r->mods[m];
    for (int k = 0; k < md->nknobs; k++) {
        int c = md->control[k];
        if (!strcmp(r->cmds[c].opt, opt) || !strcmp(r->params[c].label, opt)) return c;
    }
    return -1;
}

/* ---------- raw values ---------- */

double rack_get_raw(const Rack *r, int c)
{
    if (c < 0 || c >= r->ncontrols) return 0;
    const KnobCmd *cmd = &r->cmds[c];
    if (cmd->nenum) {
        int i = (int)floor(r->values[c] + 0.5);
        if (i < 0) i = 0;
        if (i >= cmd->nenum) i = cmd->nenum - 1;
        return (double)cmd->enum_vals[i];
    }
    return r->values[c];
}

void rack_set_raw(Rack *r, int c, double raw)
{
    if (c < 0 || c >= r->ncontrols) return;
    const KnobCmd *cmd = &r->cmds[c];
    if (cmd->nenum) {
        long long v = (long long)floor(raw + 0.5);
        for (int i = 0; i < cmd->nenum; i++)
            if (cmd->enum_vals[i] == v) { r->values[c] = i; return; }
        return;   /* not one of the constants: leave the value alone */
    }
    r->values[c] = param_clamp(&r->params[c], raw);
}

/* ---------- commands ---------- */

static void format_raw(const Rack *r, int c, char *buf, size_t cap)
{
    const KnobCmd *cmd = &r->cmds[c];
    if (cmd->nenum) snprintf(buf, cap, "%lld", (long long)rack_get_raw(r, c));
    else snprintf(buf, cap, cmd->fmt[0] ? cmd->fmt : "%g", r->values[c]);
}

void rack_send_control(const Rack *r, Voice *v, int c)
{
    if (!v || c < 0 || c >= r->ncontrols) return;   /* no voice yet: values only */
    Control ct = r->controls[c];
    if (ct.knob < 0) { rack_send_enable(r, v, ct.module); return; }
    const ModuleDef *md = &r->mods[ct.module];
    const KnobCmd *cmd = &r->cmds[c];
    char val[64];
    format_raw(r, c, val, sizeof val);
    voice_send_command(v, md->name, cmd->opt, val);
    if (cmd->opt2[0]) {
        snprintf(val, sizeof val, cmd->fmt2[0] ? cmd->fmt2 : "%g", r->values[c]);
        voice_send_command(v, md->name, cmd->opt2, val);
    }
}

void rack_send_enable(const Rack *r, Voice *v, int m)
{
    const ModuleDef *md = &r->mods[m];
    if (!v || !md->bypassable) return;
    voice_send_command(v, md->name, "enable", r->enabled[m] ? "1" : "0");
}

void rack_send_all(const Rack *r, Voice *v)
{
    for (int c = 0; c < r->ncontrols; c++)
        if (r->controls[c].knob >= 0) rack_send_control(r, v, c);
    for (int m = 0; m < r->nmods; m++)
        rack_send_enable(r, v, m);
}

/* ---------- editing ---------- */

/* The bypass switch, where a module has one, mirrors enabled[]. */
static void sync_switch(Rack *r, int m)
{
    int c = r->mods[m].bypass_control;
    if (c >= 0) r->values[c] = r->enabled[m] ? 1 : 0;
}

void rack_set_enabled(Rack *r, Voice *v, int m, int on)
{
    if (m < 0 || m >= r->nmods || !r->mods[m].bypassable) return;
    r->enabled[m] = on ? 1 : 0;
    sync_switch(r, m);
    rack_send_enable(r, v, m);
}

void rack_toggle_module(Rack *r, Voice *v, int m)
{
    if (m < 0 || m >= r->nmods || !r->mods[m].bypassable) return;
    rack_set_enabled(r, v, m, !r->enabled[m]);
}

void rack_select_next(Rack *r, int dir)
{
    paramset_select(&r->set, dir);
}

/* Kill float dust so "%g" prints cleanly. */
static double tidy(double x) { return round(x * 1e6) / 1e6; }

void rack_nudge(Rack *r, Voice *v, int dir, ParamGrain g)
{
    if (r->ncontrols == 0) return;
    int c = r->set.sel;
    if (r->controls[c].knob < 0) {
        rack_toggle_selected(r, v);
        return;
    }
    if (r->params[c].kind == PARAM_ENUM) {
        /* An enum steps one constant at a time and wraps. */
        int n = r->cmds[c].nenum;
        int i = (int)floor(r->values[c] + 0.5) + dir;
        r->values[c] = n > 0 ? (i % n + n) % n : 0;
    } else {
        paramset_nudge(&r->set, c, dir, g);
        r->values[c] = tidy(r->values[c]);
    }
    rack_send_control(r, v, c);
}

void rack_toggle_selected(Rack *r, Voice *v)
{
    if (r->ncontrols == 0) return;
    rack_toggle_module(r, v, r->controls[r->set.sel].module);
}

void rack_reset_control(Rack *r, Voice *v, int c)
{
    if (c < 0 || c >= r->ncontrols) return;
    Control ct = r->controls[c];
    if (ct.knob < 0) {
        rack_set_enabled(r, v, ct.module, r->mods[ct.module].enabled_default);
        return;
    }
    paramset_reset(&r->set, c);
    rack_send_control(r, v, c);
}

void rack_reset_selected(Rack *r, Voice *v)
{
    if (r->ncontrols == 0) return;
    rack_reset_control(r, v, r->set.sel);
}

void rack_neutral(Rack *r)
{
    paramset_reset_all(&r->set);
    for (int m = 0; m < r->nmods; m++) {
        r->enabled[m] = r->mods[m].enabled_default;
        sync_switch(r, m);
    }
}

void rack_reset_all(Rack *r, Voice *v)
{
    rack_neutral(r);
    rack_send_all(r, v);
}

void rack_format_value(const Rack *r, int c, char *buf, size_t cap)
{
    if (c < 0 || c >= r->ncontrols) { snprintf(buf, cap, "-"); return; }
    param_format(&r->params[c], r->values[c], buf, cap);
}

void rack_describe_selected(const Rack *r, char *buf, size_t cap)
{
    if (r->ncontrols == 0) { snprintf(buf, cap, "no controls"); return; }
    int c = r->set.sel;
    Control ct = r->controls[c];
    const ModuleDef *md = &r->mods[ct.module];
    const char *state = md->bypassable ? (r->enabled[ct.module] ? "on" : "OFF") : "fixed";
    if (ct.knob < 0) {
        snprintf(buf, cap, "[%d/%d] %s  [%s]", c + 1, r->ncontrols, md->label, state);
    } else {
        char val[48];
        rack_format_value(r, c, val, sizeof val);
        snprintf(buf, cap, "[%d/%d] %s.%s = %s  [%s]", c + 1, r->ncontrols,
                 r->params[c].group, r->params[c].key, val, state);
    }
}

void rack_set_control(Rack *r, Voice *v, int c, double value)
{
    if (c < 0 || c >= r->ncontrols) return;
    Control ct = r->controls[c];
    if (ct.knob < 0) {
        rack_set_enabled(r, v, ct.module, value >= 0.5);
        return;
    }
    paramset_set(&r->set, c, value);
    r->values[c] = tidy(r->values[c]);
    rack_send_control(r, v, c);
}

/* xorshift32; seeded from the clock on first use. Quality is irrelevant here. */
static double rnd(Rack *r)   /* [0,1) */
{
    if (!r->rng_state) {
        r->rng_state = (unsigned)time(NULL) ^ ((unsigned)clock() << 8) ^ 0x9e3779b9u;
        if (!r->rng_state) r->rng_state = 1;
    }
    unsigned x = r->rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->rng_state = x;
    return (x >> 8) / 16777216.0;
}

void rack_randomize(Rack *r, Voice *v, double depth)
{
    if (depth < 0) depth = 0;
    if (depth > 1) depth = 1;
    for (int m = 0; m < r->nmods; m++) {
        const ModuleDef *md = &r->mods[m];
        if (md->bypassable) {
            if (md->enabled_default)
                r->enabled[m] = rnd(r) < 0.92;
            else
                r->enabled[m] = rnd(r) < 0.15 + 0.35 * depth;
            sync_switch(r, m);
        }
        for (int k = 0; k < md->nknobs; k++) {
            int c = md->control[k];
            const Param *p = &r->params[c];
            double val = p->neutral;
            if (rnd(r) < 0.65) {
                double u = rnd(r) * 2 - 1;                 /* [-1,1] */
                double side = u < 0 ? p->neutral - p->min : p->max - p->neutral;
                val = p->neutral + (u < 0 ? -1 : 1) * u * u * side * depth;
            }
            r->values[c] = tidy(param_snap(p, val, PARAM_FINE));
        }
    }
    rack_send_all(r, v);
}
