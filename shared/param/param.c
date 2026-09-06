#include "param.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static double clamp01(double t) { return t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t; }

/* The largest of 1, 2 or 5 times a power of ten that is not above x, so a
 * derived step is a number a person would have chosen. */
static double nice_step(double x)
{
    if (!(x > 0.0)) return 0.0;
    double exponent = floor(log10(x));
    double power = pow(10.0, exponent);
    double mantissa = x / power;
    double pick = mantissa >= 5.0 ? 5.0 : mantissa >= 2.0 ? 2.0 : 1.0;
    return pick * power;
}

void param_finish(Param *p)
{
    if (p->kind != PARAM_FADER) {
        if (p->coarse <= 0.0) p->coarse = 1.0;
        p->fine = p->ultra = p->coarse;
        p->decimals = 0;
        return;
    }

    double range = p->max - p->min;
    if (range < 0.0) range = -range;

    if (p->coarse <= 0.0) p->coarse = nice_step(range / 100.0);
    if (p->fine   <= 0.0) p->fine   = nice_step(range / 1000.0);
    if (p->ultra  <= 0.0) p->ultra  = nice_step(range / 10000.0);
    /* Degenerate ranges still need usable steps. */
    if (p->coarse <= 0.0) p->coarse = range > 0.0 ? range : 1.0;
    if (p->fine   <= 0.0) p->fine   = p->coarse;
    if (p->ultra  <= 0.0) p->ultra  = p->fine;

    /* Enough digits that one ultra step always changes the readout. */
    p->decimals = 0;
    if (p->ultra < 1.0) {
        int d = (int)ceil(-log10(p->ultra) - 1e-9);
        p->decimals = d < 0 ? 0 : d > 6 ? 6 : d;
    }
}

void param_finish_all(Param *p, int n)
{
    for (int i = 0; i < n; i++) param_finish(&p[i]);
}

double param_step(const Param *p, ParamGrain g)
{
    switch (g) {
    case PARAM_COARSE: return p->coarse;
    case PARAM_ULTRA:  return p->ultra;
    case PARAM_FINE:
    default:           return p->fine;
    }
}

double param_clamp(const Param *p, double v)
{
    if (v != v) return p->neutral;          /* NaN in, something sane out */
    if (v < p->min) return p->min;
    if (v > p->max) return p->max;
    return v;
}

double param_to_norm(const Param *p, double v)
{
    double lo = p->min, hi = p->max;
    if (hi <= lo) return 0.0;
    v = param_clamp(p, v);

    if (p->taper == PARAM_EXP && lo > 0.0 && hi > 0.0)
        return clamp01(log(v / lo) / log(hi / lo));

    if (p->taper == PARAM_BIPOLAR) {
        double n = p->neutral;
        if (v >= n) {
            double span = hi - n;
            return span > 0.0 ? clamp01(0.5 + 0.5 * (v - n) / span) : 0.5;
        }
        double span = n - lo;
        return span > 0.0 ? clamp01(0.5 - 0.5 * (n - v) / span) : 0.5;
    }

    return clamp01((v - lo) / (hi - lo));
}

double param_from_norm(const Param *p, double t)
{
    double lo = p->min, hi = p->max;
    t = clamp01(t);
    if (hi <= lo) return lo;

    if (p->taper == PARAM_EXP && lo > 0.0 && hi > 0.0)
        return param_clamp(p, lo * pow(hi / lo, t));

    if (p->taper == PARAM_BIPOLAR) {
        double n = p->neutral;
        if (t >= 0.5) return param_clamp(p, n + (t - 0.5) * 2.0 * (hi - n));
        return param_clamp(p, n - (0.5 - t) * 2.0 * (n - lo));
    }

    return param_clamp(p, lo + t * (hi - lo));
}

double param_snap(const Param *p, double v, ParamGrain g)
{
    double step = param_step(p, g);
    if (step <= 0.0) return param_clamp(p, v);
    double k = (v - p->neutral) / step;
    return param_clamp(p, p->neutral + floor(k + 0.5) * step);
}

double param_nudge(const Param *p, double v, int steps, ParamGrain g)
{
    double step = param_step(p, g);
    if (step <= 0.0 || steps == 0) return param_clamp(p, v);
    return param_snap(p, v + (double)steps * step, g);
}

void param_format_value(const Param *p, double v, char *buf, size_t cap)
{
    if (!buf || cap == 0) return;

    if (p->kind == PARAM_ENUM) {
        int i = (int)floor(v + 0.5);
        if (p->names && i >= 0 && i < p->nnames) snprintf(buf, cap, "%s", p->names[i]);
        else snprintf(buf, cap, "%d", i);
        return;
    }
    if (p->kind == PARAM_SWITCH) {
        snprintf(buf, cap, "%s", v >= 0.5 ? "on" : "off");
        return;
    }

    /* Keep a value that rounds to zero from printing as "-0.0000". */
    double half = p->ultra > 0.0 ? p->ultra * 0.5 : 0.0;
    if (v > -half && v < half) v = 0.0;
    snprintf(buf, cap, "%.*f", p->decimals, v);
}

void param_format(const Param *p, double v, char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    char number[64];
    param_format_value(p, v, number, sizeof number);
    if (p->kind == PARAM_FADER && p->unit[0]) snprintf(buf, cap, "%s %s", number, p->unit);
    else snprintf(buf, cap, "%s", number);
}

void paramset_init(ParamSet *s, Param *defs, double *values, int n)
{
    s->defs = defs;
    s->values = values;
    s->n = n;
    s->sel = 0;
    param_finish_all(defs, n);
    for (int i = 0; i < n; i++) values[i] = param_clamp(&defs[i], values[i]);
}

int paramset_find(const ParamSet *s, const char *group, const char *key)
{
    for (int i = 0; i < s->n; i++)
        if (strcmp(s->defs[i].group, group) == 0 && strcmp(s->defs[i].key, key) == 0) return i;
    return -1;
}

void paramset_select(ParamSet *s, int dir)
{
    if (s->n <= 0) return;
    s->sel = (s->sel + dir % s->n + s->n) % s->n;
}

void paramset_set(ParamSet *s, int i, double v)
{
    if (i < 0 || i >= s->n) return;
    s->values[i] = param_clamp(&s->defs[i], v);
}

void paramset_set_norm(ParamSet *s, int i, double t)
{
    if (i < 0 || i >= s->n) return;
    s->values[i] = param_from_norm(&s->defs[i], t);
}

void paramset_nudge(ParamSet *s, int i, int steps, ParamGrain g)
{
    if (i < 0 || i >= s->n) return;
    s->values[i] = param_nudge(&s->defs[i], s->values[i], steps, g);
}

void paramset_step(ParamSet *s, int i, int dir)
{
    if (i < 0 || i >= s->n) return;
    const Param *p = &s->defs[i];
    if (p->kind == PARAM_FADER) { paramset_nudge(s, i, dir, PARAM_FINE); return; }
    int n = (int)floor(p->max - p->min + 0.5) + 1;
    if (n < 1) n = 1;
    int at = (int)floor(s->values[i] - p->min + 0.5);
    at = ((at + dir) % n + n) % n;
    s->values[i] = p->min + at;
}

void paramset_reset(ParamSet *s, int i)
{
    if (i < 0 || i >= s->n) return;
    s->values[i] = param_clamp(&s->defs[i], s->defs[i].neutral);
}

void paramset_reset_all(ParamSet *s)
{
    for (int i = 0; i < s->n; i++) paramset_reset(s, i);
}

void paramset_describe(const ParamSet *s, int i, char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    if (i < 0 || i >= s->n) { snprintf(buf, cap, "-"); return; }
    char value[64];
    param_format(&s->defs[i], s->values[i], value, sizeof value);
    snprintf(buf, cap, "%s.%s  %s", s->defs[i].group, s->defs[i].key, value);
}
