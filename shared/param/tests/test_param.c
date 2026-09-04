/* Offline tests for the fader model. No SDL, no audio device. */
#include "param.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) { printf("FAIL: %s\n", what); failures++; }
}

static void check_near(double got, double want, double tol, const char *what)
{
    if (fabs(got - want) > tol) {
        printf("FAIL: %s (got %.9g, want %.9g)\n", what, got, want);
        failures++;
    }
}

static Param linear_level(void)
{
    Param p = {0};
    snprintf(p.group, sizeof p.group, "%s", "vca");
    snprintf(p.key, sizeof p.key, "%s", "level");
    snprintf(p.label, sizeof p.label, "%s", "LEVEL");
    p.kind = PARAM_FADER;
    p.taper = PARAM_LINEAR;
    p.min = 0.0; p.max = 1.0; p.neutral = 0.0;
    param_finish(&p);
    return p;
}

static Param exp_cutoff(void)
{
    Param p = {0};
    snprintf(p.group, sizeof p.group, "%s", "vcf");
    snprintf(p.key, sizeof p.key, "%s", "cutoff");
    snprintf(p.unit, sizeof p.unit, "%s", "Hz");
    p.kind = PARAM_FADER;
    p.taper = PARAM_EXP;
    p.min = 20.0; p.max = 12000.0; p.neutral = 1400.0;
    p.coarse = 100.0; p.fine = 10.0; p.ultra = 1.0;
    param_finish(&p);
    return p;
}

static Param bipolar_depth(void)
{
    Param p = {0};
    snprintf(p.group, sizeof p.group, "%s", "mod1");
    snprintf(p.key, sizeof p.key, "%s", "level");
    p.kind = PARAM_FADER;
    p.taper = PARAM_BIPOLAR;
    p.min = -1.0; p.max = 1.0; p.neutral = 0.0;
    param_finish(&p);
    return p;
}

static void test_derived_steps(void)
{
    Param level = linear_level();
    check_near(level.coarse, 0.01, 1e-12, "linear 0..1 coarse is 0.01");
    check_near(level.fine, 0.001, 1e-12, "linear 0..1 fine is 0.001");
    check_near(level.ultra, 0.0001, 1e-15, "linear 0..1 ultra is 0.0001");
    check(level.decimals == 4, "linear 0..1 shows 4 decimals");

    /* Explicit steps are kept, not overwritten. */
    Param cutoff = exp_cutoff();
    check_near(cutoff.coarse, 100.0, 1e-12, "explicit coarse kept");
    check_near(cutoff.ultra, 1.0, 1e-12, "explicit ultra kept");
    check(cutoff.decimals == 0, "1 Hz ultra step needs no decimals");

    /* A switch has one grain and no decimals. */
    Param sw = {0};
    sw.kind = PARAM_SWITCH;
    sw.min = 0; sw.max = 1; sw.neutral = 1;
    param_finish(&sw);
    check_near(sw.coarse, 1.0, 1e-12, "switch step is 1");
    check(sw.decimals == 0, "switch shows no decimals");

    /* Idempotent. */
    Param again = exp_cutoff();
    param_finish(&again);
    param_finish(&again);
    check_near(again.coarse, 100.0, 1e-12, "param_finish is idempotent");
}

static void test_precision_covers_ultra(void)
{
    /* The readout must always be able to show one ultra step. */
    const Param ps[3] = { linear_level(), exp_cutoff(), bipolar_depth() };
    for (int i = 0; i < 3; i++) {
        const Param *p = &ps[i];
        double a = p->neutral;
        double b = param_nudge(p, a, 1, PARAM_ULTRA);
        char sa[64], sb[64];
        param_format_value(p, a, sa, sizeof sa);
        param_format_value(p, b, sb, sizeof sb);
        check(strcmp(sa, sb) != 0, "one ultra step changes the readout");
    }
}

static void test_taper_round_trip(void)
{
    const Param ps[3] = { linear_level(), exp_cutoff(), bipolar_depth() };
    for (int i = 0; i < 3; i++) {
        const Param *p = &ps[i];
        for (int k = 0; k <= 10; k++) {
            double t = k / 10.0;
            double v = param_from_norm(p, t);
            double back = param_to_norm(p, v);
            check_near(back, t, 1e-9, "norm -> value -> norm round trips");
        }
        check_near(param_from_norm(p, 0.0), p->min, 1e-9, "t=0 is min");
        check_near(param_from_norm(p, 1.0), p->max, 1e-9, "t=1 is max");
    }
}

static void test_exp_is_musical(void)
{
    Param p = exp_cutoff();
    /* Half the track is the geometric mean, not the arithmetic one: that is
     * the whole point of the exponential taper. */
    check_near(param_from_norm(&p, 0.5), sqrt(20.0 * 12000.0), 1e-6,
               "exp midpoint is the geometric mean");
    /* Equal track distances are equal ratios. */
    double a = param_from_norm(&p, 0.25);
    double b = param_from_norm(&p, 0.50);
    double c = param_from_norm(&p, 0.75);
    check_near(b / a, c / b, 1e-6, "equal track steps are equal frequency ratios");
}

static void test_bipolar_centre(void)
{
    Param p = bipolar_depth();
    check_near(param_to_norm(&p, 0.0), 0.5, 1e-12, "bipolar neutral sits centre track");
    check_near(param_from_norm(&p, 0.5), 0.0, 1e-12, "centre track is the neutral");
    /* Symmetry: equal distance either side of centre is equal magnitude. */
    check_near(param_from_norm(&p, 0.75), -param_from_norm(&p, 0.25), 1e-12,
               "bipolar halves are symmetric");
    /* An inverted setting is representable, which is the point of D9. */
    check_near(param_clamp(&p, -1.0), -1.0, 1e-12, "bipolar reaches -1");
}

static void test_snap_hits_neutral(void)
{
    /* Snapping is measured from the neutral, so the neutral is always
     * exactly reachable however odd the step. */
    Param p = bipolar_depth();
    p.coarse = 0.03; p.fine = 0.03; p.ultra = 0.03;
    param_finish(&p);
    double v = param_snap(&p, 0.014, PARAM_COARSE);
    check_near(v, 0.0, 1e-12, "a value near the neutral snaps onto it");

    Param c = exp_cutoff();
    double snapped = param_snap(&c, 1437.0, PARAM_COARSE);
    check_near(snapped, 1400.0, 1e-9, "coarse snap lands on the neutral grid");
}

static void test_nudge_grains(void)
{
    Param p = exp_cutoff();
    check_near(param_nudge(&p, 1400.0, 1, PARAM_COARSE), 1500.0, 1e-9, "coarse nudge is 100 Hz");
    check_near(param_nudge(&p, 1400.0, 1, PARAM_FINE), 1410.0, 1e-9, "fine nudge is 10 Hz");
    check_near(param_nudge(&p, 1400.0, 1, PARAM_ULTRA), 1401.0, 1e-9, "ultra nudge is 1 Hz");
    check_near(param_nudge(&p, 1400.0, -3, PARAM_FINE), 1370.0, 1e-9, "negative nudge");
    check_near(param_nudge(&p, 1400.0, 0, PARAM_FINE), 1400.0, 1e-9, "zero steps does nothing");

    /* Nudging never leaves the range. */
    check_near(param_nudge(&p, 11990.0, 100, PARAM_COARSE), 12000.0, 1e-9, "clamped at max");
    check_near(param_nudge(&p, 21.0, -100, PARAM_COARSE), 20.0, 1e-9, "clamped at min");

    /* Precision is reachable: 440 Hz exactly, by ultra steps. */
    double v = param_snap(&p, 440.0, PARAM_ULTRA);
    check_near(v, 440.0, 1e-9, "an exact frequency is reachable at the ultra grain");
}

static void test_clamp_and_nan(void)
{
    Param p = linear_level();
    check_near(param_clamp(&p, -5.0), 0.0, 1e-12, "clamp below min");
    check_near(param_clamp(&p, 5.0), 1.0, 1e-12, "clamp above max");
    check_near(param_clamp(&p, NAN), p.neutral, 1e-12, "NaN clamps to the neutral");
}

static void test_format(void)
{
    Param c = exp_cutoff();
    char buf[64];
    param_format(&c, 1400.0, buf, sizeof buf);
    check(strcmp(buf, "1400 Hz") == 0, "fader readout carries its unit");

    Param level = linear_level();
    param_format_value(&level, 0.7, buf, sizeof buf);
    check(strcmp(buf, "0.7000") == 0, "level shows its full precision");

    /* A tiny negative must not read as "-0.0000". */
    Param b = bipolar_depth();
    param_format_value(&b, -1e-9, buf, sizeof buf);
    check(buf[0] != '-', "a value rounding to zero has no minus sign");

    static const char *const waves[] = { "Sine", "Saw", "Square", "Triangle" };
    Param e = {0};
    e.kind = PARAM_ENUM;
    e.min = 0; e.max = 3; e.neutral = 1;
    e.names = waves; e.nnames = 4;
    param_finish(&e);
    param_format_value(&e, 2.0, buf, sizeof buf);
    check(strcmp(buf, "Square") == 0, "enum reads as its name");
    param_format_value(&e, 9.0, buf, sizeof buf);
    check(strcmp(buf, "9") == 0, "an out-of-range enum falls back to the number");

    Param sw = {0};
    sw.kind = PARAM_SWITCH;
    sw.min = 0; sw.max = 1; sw.neutral = 1;
    param_finish(&sw);
    param_format_value(&sw, 1.0, buf, sizeof buf);
    check(strcmp(buf, "on") == 0, "switch reads on");
    param_format_value(&sw, 0.0, buf, sizeof buf);
    check(strcmp(buf, "off") == 0, "switch reads off");
}

static void test_set(void)
{
    Param defs[3];
    defs[0] = exp_cutoff();
    defs[1] = linear_level();
    defs[2] = bipolar_depth();
    double values[3] = { 1400.0, 0.7, 0.5 };

    ParamSet s;
    paramset_init(&s, defs, values, 3);
    check(s.n == 3 && s.sel == 0, "set initialises");

    check(paramset_find(&s, "vcf", "cutoff") == 0, "find by address");
    check(paramset_find(&s, "mod1", "level") == 2, "find the bipolar one");
    check(paramset_find(&s, "nope", "nope") < 0, "a missing address is -1");

    paramset_select(&s, 1);
    check(s.sel == 1, "select forward");
    paramset_select(&s, -1);
    check(s.sel == 0, "select back");
    paramset_select(&s, -1);
    check(s.sel == 2, "selection wraps backwards");
    paramset_select(&s, 1);
    check(s.sel == 0, "selection wraps forwards");

    paramset_reset(&s, 2);
    check_near(values[2], 0.0, 1e-12, "reset returns the bipolar to its neutral");

    paramset_set(&s, 1, 99.0);
    check_near(values[1], 1.0, 1e-12, "set clamps");

    paramset_set_norm(&s, 0, 0.5);
    check_near(values[0], sqrt(20.0 * 12000.0), 1e-6, "set_norm honours the taper");

    paramset_nudge(&s, 0, 1, PARAM_ULTRA);
    paramset_reset_all(&s);
    check_near(values[0], 1400.0, 1e-9, "reset_all restores every neutral");
    check_near(values[1], 0.0, 1e-12, "reset_all on the level");

    char buf[96];
    paramset_describe(&s, 0, buf, sizeof buf);
    check(strstr(buf, "vcf.cutoff") != NULL, "describe names the address");
    check(strstr(buf, "Hz") != NULL, "describe carries the unit");
    paramset_describe(&s, 99, buf, sizeof buf);
    check(strcmp(buf, "-") == 0, "describe survives a bad index");
}

int main(void)
{
    test_derived_steps();
    test_precision_covers_ultra();
    test_taper_round_trip();
    test_exp_is_musical();
    test_bipolar_centre();
    test_snap_hits_neutral();
    test_nudge_grains();
    test_clamp_and_nan();
    test_format();
    test_set();

    if (failures == 0) printf("param: OK\n");
    else printf("param: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
