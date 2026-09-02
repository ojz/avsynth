#ifndef VSYNTH_RACK_H
#define VSYNTH_RACK_H

#include <stddef.h>
#include "voice.h"

/*
 * The rack: a fixed, ordered list of libavfilter modules, each with knobs.
 * Every knob is a runtime-settable option; every bypassable module has
 * timeline support so the generic "enable" command works.
 *
 * A knob maps one numeric value onto one or two filter options through a
 * printf format (e.g. zoom -> crop w="iw/%g" and h="ih/%g").
 */

#define RACK_MAX_KNOBS_PER_MODULE 8
#define RACK_MAX_MODULES 24
#define RACK_MAX_CONTROLS 96

typedef struct KnobDef {
    const char *label;      /* shown to the player */
    const char *opt;        /* option name sent in the command */
    const char *fmt;        /* printf format for the value, NULL = "%g" */
    const char *opt2;       /* optional second option driven by the same value */
    const char *fmt2;
    double min, max, neutral, step;
} KnobDef;

typedef struct ModuleDef {
    const char *name;        /* instance name: command target is "<filter>@<name>" */
    const char *filter;      /* libavfilter name */
    const char *static_args; /* fixed options, may be NULL */
    int   bypassable;        /* filter has timeline support -> "enable" works */
    int   enabled_default;
    KnobDef knobs[RACK_MAX_KNOBS_PER_MODULE];
    int   nknobs;
} ModuleDef;

/* A control is what the cursor lands on: a knob, or the on/off of a module
 * that has no knobs. */
typedef struct Control {
    int module;   /* index into rack->mods */
    int knob;     /* index into module knobs, or -1 for the bypass switch */
} Control;

typedef struct Rack {
    ModuleDef mods[RACK_MAX_MODULES];
    int       nmods;
    double    values[RACK_MAX_MODULES][RACK_MAX_KNOBS_PER_MODULE];
    int       enabled[RACK_MAX_MODULES];

    Control   controls[RACK_MAX_CONTROLS];
    int       ncontrols;
    int       sel;      /* selected control */
} Rack;

/* Fill with the default v1 rack, all knobs at neutral. */
void rack_init_default(Rack *r);

/* Build the filtergraph chain string for voice_start(). cap_w/cap_h are the
 * capture size, used to restore size after the zoom crop. */
int  rack_build_chain(const Rack *r, int cap_w, int cap_h, char *buf, size_t cap);

/* Send one knob's current value / one module's enable state to the voice. */
void rack_send_knob(const Rack *r, Voice *v, int module, int knob);
void rack_send_enable(const Rack *r, Voice *v, int module);
/* Send everything (after loading a patch). */
void rack_send_all(const Rack *r, Voice *v);

/* Cursor and editing. factor: 1 normal, 0.1 fine, 10 coarse. */
void rack_select_next(Rack *r, int dir);
void rack_nudge(Rack *r, Voice *v, int dir, double factor);
void rack_toggle_selected(Rack *r, Voice *v);
void rack_reset_selected(Rack *r, Voice *v);
void rack_reset_all(Rack *r, Voice *v);

/* Set one control directly (panel drag). Clamps, snaps to step, sends. */
void rack_set_control(Rack *r, Voice *v, int control, double value);
void rack_toggle_module(Rack *r, Voice *v, int module);

/* Randomize: knobs wander away from neutral by up to depth (0..1) of their
 * range, with a bias towards small moves; off-by-default modules switch on
 * with a probability that grows with depth. */
void rack_randomize(Rack *r, Voice *v, double depth);

/* One-line status for the selected control, e.g. "rot.angle = 0.031  [on]". */
void rack_describe_selected(const Rack *r, char *buf, size_t cap);

#endif
