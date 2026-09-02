#include "rack.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define K(label, opt, min, max, neutral, step) \
    { label, opt, NULL, NULL, NULL, min, max, neutral, step }
#define KF(label, opt, fmt, min, max, neutral, step) \
    { label, opt, fmt, NULL, NULL, min, max, neutral, step }
#define K2(label, opt, fmt, opt2, fmt2, min, max, neutral, step) \
    { label, opt, fmt, opt2, fmt2, min, max, neutral, step }

/* Default rack v1. Order is the signal path. See PRD.md section 5. */
static const ModuleDef DEFAULT_RACK[] = {
    { "shift", "rgbashift", NULL, 1, 1, {
        KF("red h",   "rh", "%.0f", -64, 64, 0, 1),
        KF("red v",   "rv", "%.0f", -64, 64, 0, 1),
        KF("green h", "gh", "%.0f", -64, 64, 0, 1),
        KF("green v", "gv", "%.0f", -64, 64, 0, 1),
        KF("blue h",  "bh", "%.0f", -64, 64, 0, 1),
        KF("blue v",  "bv", "%.0f", -64, 64, 0, 1) }, 6 },

    /* zoom: crop to iw/z x ih/z centered, then the following scale restores size */
    { "zoom", "crop", NULL, 0, 1, {   /* crop has no timeline support: not bypassable */
        K2("zoom", "w", "iw/%.4f", "h", "ih/%.4f", 0.5, 4.0, 1.0, 0.01) }, 1 },
    { "unzoom", "scale", "__CAPSIZE__", 0, 1, { {0} }, 0 },

    { "rot", "rotate", "c=black:ow=iw:oh=ih", 1, 1, {
        K("angle", "angle", -3.1416, 3.1416, 0, 0.005) }, 1 },

    { "shear", "shear", "c=black", 1, 1, {
        K("shear x", "shx", -2, 2, 0, 0.01),
        K("shear y", "shy", -2, 2, 0, 0.01) }, 2 },

    { "lens", "lenscorrection", NULL, 1, 1, {
        K("k1", "k1", -1, 1, 0, 0.01),
        K("k2", "k2", -1, 1, 0, 0.01) }, 2 },

    { "trail", "lagfun", NULL, 1, 1, {
        K("decay", "decay", 0, 1, 0, 0.01) }, 1 },

    { "mix", "tmix", "frames=3", 1, 0, { {0} }, 0 },

    { "diff", "tblend", "all_mode=difference", 1, 0, {
        K("opacity", "all_opacity", 0, 1, 1, 0.05) }, 1 },

    { "edge", "edgedetect", "mode=colormix", 1, 0, { {0} }, 0 },

    { "blur", "gblur", NULL, 1, 0, {
        K("sigma", "sigma", 0.1, 30, 0.5, 0.1) }, 1 },

    { "sharp", "unsharp", "5:5:1.5", 1, 0, { {0} }, 0 },

    { "hue", "hue", NULL, 1, 1, {
        K("hue",        "h", -180, 180, 0, 1),
        K("saturation", "s", 0, 3, 1, 0.02),
        K("brightness", "b", -2, 2, 0, 0.02) }, 3 },

    { "eq", "eq", NULL, 1, 1, {
        K("contrast",   "contrast",   0, 3, 1, 0.02),
        K("brightness", "brightness", -1, 1, 0, 0.01),
        K("saturation", "saturation", 0, 3, 1, 0.02),
        K("gamma",      "gamma",      0.1, 3, 1, 0.02) }, 4 },

    { "neg",   "negate",   NULL,      1, 0, { {0} }, 0 },
    { "vig",   "vignette", NULL,      1, 0, { {0} }, 0 },
    { "noise", "noise",    "alls=20", 1, 0, { {0} }, 0 },
};

void rack_init_default(Rack *r)
{
    memset(r, 0, sizeof *r);
    r->nmods = (int)(sizeof DEFAULT_RACK / sizeof DEFAULT_RACK[0]);
    for (int m = 0; m < r->nmods; m++) {
        r->mods[m] = DEFAULT_RACK[m];
        r->enabled[m] = r->mods[m].enabled_default;
        for (int k = 0; k < r->mods[m].nknobs; k++)
            r->values[m][k] = r->mods[m].knobs[k].neutral;

        if (r->mods[m].nknobs == 0 && r->mods[m].bypassable) {
            r->controls[r->ncontrols++] = (Control){ m, -1 };
        }
        for (int k = 0; k < r->mods[m].nknobs; k++)
            r->controls[r->ncontrols++] = (Control){ m, k };
    }
    r->sel = 0;
}

static void format_value(const KnobDef *kd, const char *fmt, double v, char *buf, size_t cap)
{
    snprintf(buf, cap, fmt ? fmt : "%g", v);
    (void)kd;
}

static void target_name(const ModuleDef *md, char *buf, size_t cap)
{
    snprintf(buf, cap, "%s@%s", md->filter, md->name);
}

int rack_build_chain(const Rack *r, int cap_w, int cap_h, char *buf, size_t cap)
{
    size_t n = 0;
    for (int m = 0; m < r->nmods; m++) {
        const ModuleDef *md = &r->mods[m];
        char args[512] = "";
        size_t a = 0;

        if (md->static_args) {
            if (!strcmp(md->static_args, "__CAPSIZE__"))
                a += snprintf(args + a, sizeof args - a, "%d:%d", cap_w, cap_h);
            else
                a += snprintf(args + a, sizeof args - a, "%s", md->static_args);
        }
        for (int k = 0; k < md->nknobs; k++) {
            const KnobDef *kd = &md->knobs[k];
            char val[64];
            format_value(kd, kd->fmt, r->values[m][k], val, sizeof val);
            a += snprintf(args + a, sizeof args - a, "%s%s=%s", a ? ":" : "", kd->opt, val);
            if (kd->opt2) {
                format_value(kd, kd->fmt2, r->values[m][k], val, sizeof val);
                a += snprintf(args + a, sizeof args - a, ":%s=%s", kd->opt2, val);
            }
        }
        if (md->bypassable)
            a += snprintf(args + a, sizeof args - a, "%senable=%d", a ? ":" : "", r->enabled[m] ? 1 : 0);

        n += snprintf(buf + n, cap > n ? cap - n : 0, "%s%s@%s%s%s",
                      m ? "," : "", md->filter, md->name, a ? "=" : "", args);
        if (n >= cap) return -1;
    }
    return 0;
}

void rack_send_knob(const Rack *r, Voice *v, int m, int k)
{
    const ModuleDef *md = &r->mods[m];
    const KnobDef *kd = &md->knobs[k];
    char target[64], val[64];
    target_name(md, target, sizeof target);
    format_value(kd, kd->fmt, r->values[m][k], val, sizeof val);
    voice_send_command(v, target, kd->opt, val);
    if (kd->opt2) {
        format_value(kd, kd->fmt2, r->values[m][k], val, sizeof val);
        voice_send_command(v, target, kd->opt2, val);
    }
}

void rack_send_enable(const Rack *r, Voice *v, int m)
{
    const ModuleDef *md = &r->mods[m];
    if (!md->bypassable) return;
    char target[64];
    target_name(md, target, sizeof target);
    voice_send_command(v, target, "enable", r->enabled[m] ? "1" : "0");
}

void rack_send_all(const Rack *r, Voice *v)
{
    for (int m = 0; m < r->nmods; m++) {
        for (int k = 0; k < r->mods[m].nknobs; k++)
            rack_send_knob(r, v, m, k);
        rack_send_enable(r, v, m);
    }
}

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
    Control c = r->controls[r->sel];
    if (!r->mods[c.module].bypassable) return;
    r->enabled[c.module] = !r->enabled[c.module];
    rack_send_enable(r, v, c.module);
}

void rack_reset_selected(Rack *r, Voice *v)
{
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

void rack_describe_selected(const Rack *r, char *buf, size_t cap)
{
    Control c = r->controls[r->sel];
    const ModuleDef *md = &r->mods[c.module];
    const char *state = md->bypassable ? (r->enabled[c.module] ? "on" : "OFF") : "fixed";
    if (c.knob < 0) {
        snprintf(buf, cap, "[%d/%d] %s  [%s]", r->sel + 1, r->ncontrols, md->name, state);
    } else {
        const KnobDef *kd = &md->knobs[c.knob];
        snprintf(buf, cap, "[%d/%d] %s.%s = %g  [%s]", r->sel + 1, r->ncontrols,
                 md->name, kd->label, r->values[c.module][c.knob], state);
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
