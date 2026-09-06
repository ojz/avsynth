#ifndef AVSYNTH_PARAM_H
#define AVSYNTH_PARAM_H

#include <stddef.h>

/*
 * The lab's parameter model. ROADMAP.md section 6 is the specification: every
 * continuous control in every app is a fader, and this is what one is.
 *
 * Plain C: no SDL, no SQLite, no ffmpeg. A DSP engine may depend on it and it
 * is testable offline. How a fader is drawn and driven lives in shared/ui,
 * how it is stored lives in shared/store, and neither is needed here.
 */

/* How a track position maps to a value. */
typedef enum {
    PARAM_LINEAR,    /* levels, depths, anything already perceptually even */
    PARAM_EXP,       /* frequencies; needs min > 0, so a wide range stays
                      * dialable at both ends instead of being a cliff */
    PARAM_BIPOLAR    /* signed: symmetric about the neutral, both halves alike */
} ParamTaper;

typedef enum {
    PARAM_FADER,     /* continuous */
    PARAM_SWITCH,    /* off / on */
    PARAM_ENUM       /* one of names[] */
} ParamKind;

/* The three grains every fader moves by. */
typedef enum { PARAM_COARSE, PARAM_FINE, PARAM_ULTRA } ParamGrain;

#define PARAM_GROUP_CAP 24
#define PARAM_KEY_CAP   24
#define PARAM_LABEL_CAP 24
#define PARAM_UNIT_CAP  8

typedef struct Param {
    /* Identity. group plus key is the address a preset row, a MIDI binding
     * and a sequencer target all use. It never depends on screen position. */
    char group[PARAM_GROUP_CAP];
    char key[PARAM_KEY_CAP];
    char label[PARAM_LABEL_CAP];
    char unit[PARAM_UNIT_CAP];

    ParamKind  kind;
    ParamTaper taper;

    double min, max;

    /* What reset returns to. Where a control has a value that makes it stop
     * acting, that is the neutral (a bipolar depth's 0); where it does not,
     * the neutral is the patch default. A bipolar fader's bar fills outward
     * from its neutral, so an inverted setting reads at a glance; every other
     * fader fills from min and marks the neutral with a tick. */
    double neutral;

    /* Step grains. Leave at 0 and param_finish() derives round numbers from
     * the range. Set them when the parameter has a natural resolution, such
     * as 1 Hz on a cutoff. */
    double coarse, fine, ultra;

    const char *const *names;   /* PARAM_ENUM */
    int nnames;

    int decimals;               /* derived: digits the readout needs */
} Param;

/* Fill in the derived fields. Idempotent, so calling it twice is harmless. */
void   param_finish(Param *p);
void   param_finish_all(Param *p, int n);

double param_step(const Param *p, ParamGrain g);
double param_clamp(const Param *p, double v);

/* Value against track position, honouring the taper. */
double param_to_norm(const Param *p, double v);
double param_from_norm(const Param *p, double t);

/* Snap to the grain's grid, measured from the neutral so the neutral is
 * always exactly reachable. */
double param_snap(const Param *p, double v, ParamGrain g);

/* Move by whole steps of a grain and land on the grid. */
double param_nudge(const Param *p, double v, int steps, ParamGrain g);

/* Readout. param_format adds the unit; param_format_value is the bare number,
 * the enum name, or on/off. Both show a fader at full precision, so the
 * smallest movement it has is always visible in the number. */
void   param_format(const Param *p, double v, char *buf, size_t cap);
void   param_format_value(const Param *p, double v, char *buf, size_t cap);

/* A set of parameters with a selection, which is what an app's panel is. */
typedef struct ParamSet {
    Param  *defs;
    double *values;
    int     n;
    int     sel;
} ParamSet;

void paramset_init(ParamSet *s, Param *defs, double *values, int n);
int  paramset_find(const ParamSet *s, const char *group, const char *key);
void paramset_select(ParamSet *s, int dir);
void paramset_set(ParamSet *s, int i, double v);
void paramset_set_norm(ParamSet *s, int i, double t);
void paramset_nudge(ParamSet *s, int i, int steps, ParamGrain g);
/* One position in either direction: a fader moves one fine step, a switch
 * or enum advances to the next name and wraps. What a click or an arrow does. */
void paramset_step(ParamSet *s, int i, int dir);
void paramset_reset(ParamSet *s, int i);
void paramset_reset_all(ParamSet *s);
/* "vcf.cutoff  1400.0 Hz", one line for a title bar or a log. */
void paramset_describe(const ParamSet *s, int i, char *buf, size_t cap);

#endif
