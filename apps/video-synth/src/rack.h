#ifndef VSYNTH_RACK_H
#define VSYNTH_RACK_H

#include <stddef.h>
#include "voice.h"
#include "graph.h"

/*
 * The rack is derived from chain text, not written by hand. rack_from_chain()
 * parses the text into a throwaway libavfilter graph, then walks every filter
 * instance in it:
 *
 *   - each filter becomes a module; its instance name (rotate@rot, or the
 *     Parsed_hue_3 libavfilter assigns when unnamed) is the command target;
 *   - every option flagged runtime-settable whose current value is a plain
 *     number becomes a knob; expression-valued options (t*0.1, iw/2) stay
 *     in the text;
 *   - a filter with timeline support gets a bypass switch.
 *
 * A small override table supplies good ranges, labels and steps for options
 * whose libavfilter bounds are useless (rotate angle is +-DBL_MAX), and a
 * few virtual knobs like crop's zoom that drive two expression options.
 *
 * The values in the text are each knob's neutral: Backspace goes back to
 * what you wrote.
 */

#define RACK_MAX_KNOBS_PER_MODULE 12
#define RACK_MAX_MODULES 48
#define RACK_MAX_CONTROLS 256
#define RACK_CHAIN_CAP 8192

typedef struct KnobDef {
    char   label[24];     /* shown to the player */
    char   opt[32];       /* option name sent in the command */
    char   fmt[24];       /* printf format for the value, "" = %g */
    char   opt2[32];      /* optional second option driven by the same value */
    char   fmt2[24];
    double min, max, neutral, step;
    const void *cls;      /* AVClass of the filter; with unit, the knob is an enum */
    const char *unit;     /* AVOption unit naming the constants, or NULL */
} KnobDef;

typedef struct ModuleDef {
    char  name[64];       /* instance name: the command target */
    char  label[24];      /* short name shown in the panel */
    char  filter[32];     /* libavfilter name */
    int   bypassable;     /* filter has timeline support -> "enable" works */
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
    char      chain[RACK_CHAIN_CAP];   /* the text this rack was derived from */
    ModuleDef mods[RACK_MAX_MODULES];
    int       nmods;
    double    values[RACK_MAX_MODULES][RACK_MAX_KNOBS_PER_MODULE];
    int       enabled[RACK_MAX_MODULES];

    Control   controls[RACK_MAX_CONTROLS];
    int       ncontrols;
    int       sel;      /* selected control */

    int       ntaps;
    char      tap_names[GRAPH_MAX_TAPS][32];
} Rack;

/* The chain new projects start with: the old fixed rack, written out. */
extern const char RACK_DEFAULT_CHAIN[];

/* Derive a rack from chain text for a capture of cap_w x cap_h. Returns 0, or
 * <0 with a one-line reason in err. On failure *r is left untouched. */
int  rack_from_chain(Rack *r, const char *chain, int cap_w, int cap_h, char *err, size_t err_cap);

/* Send one knob's current value / one module's enable state to the voice. */
void rack_send_knob(const Rack *r, Voice *v, int module, int knob);
void rack_send_enable(const Rack *r, Voice *v, int module);
/* Send everything (after loading a preset or restarting the voice). */
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

/* The knob's value for display: the constant's name for an enum, else the number. */
void rack_format_value(const Rack *r, int module, int knob, char *buf, size_t cap);

/* Lookups by module label / knob option; -1 when absent. */
int  rack_find_module(const Rack *r, const char *label);
int  rack_find_knob(const Rack *r, int module, const char *opt);

#endif
