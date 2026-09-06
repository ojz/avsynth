#ifndef VSYNTH_RACK_H
#define VSYNTH_RACK_H

#include <stddef.h>
#include "param.h"
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
 *     number becomes a fader (shared/param); expression-valued options
 *     (t*0.1, iw/2) stay in the text;
 *   - a filter with timeline support gets a bypass, which is a switch control
 *     of its own only when the module has no faders.
 *
 * A small override table supplies good ranges, labels and steps for options
 * whose libavfilter bounds are useless (rotate angle is +-DBL_MAX), and a
 * few virtual knobs like crop's zoom that drive two expression options.
 *
 * The values in the text are each fader's neutral: Backspace goes back to
 * what you wrote. The fader's address is module label plus option name, so
 * `rot.angle` names the same thing in a preset, on the panel and in a log.
 *
 * Everything the player can reach is one flat list of controls, indexed the
 * same way in params[], cmds[], values[] and controls[]. That list is what
 * shared/ui draws and what the cursor walks.
 */

#define RACK_MAX_KNOBS_PER_MODULE 12
#define RACK_MAX_MODULES 48
#define RACK_MAX_CONTROLS 256
#define RACK_MAX_ENUM 24
#define RACK_CHAIN_CAP 8192

/* How a control reaches libavfilter: which option it sets and how the number
 * is printed into the command. For an enum option the fader's value is an
 * index into the unit's constants, kept here in declaration order. */
typedef struct KnobCmd {
    char   opt[32];       /* option name sent in the command */
    char   fmt[24];       /* printf format for the value, "" = %g */
    char   opt2[32];      /* optional second option driven by the same value */
    char   fmt2[24];
    const char *enum_names[RACK_MAX_ENUM];
    long long   enum_vals[RACK_MAX_ENUM];
    int         nenum;    /* 0 for a plain number */
} KnobCmd;

typedef struct ModuleDef {
    char  name[64];       /* instance name: the command target */
    char  label[24];      /* short name shown in the panel, and the fader group */
    char  filter[32];     /* libavfilter name */
    int   bypassable;     /* filter has timeline support -> "enable" works */
    int   enabled_default;
    int   control[RACK_MAX_KNOBS_PER_MODULE];   /* control index per fader */
    int   nknobs;
    int   bypass_control; /* control index of the on/off switch, or -1 */
} ModuleDef;

/* A control is what the cursor lands on: a fader, or the on/off of a module
 * that has no faders. */
typedef struct Control {
    int module;   /* index into rack->mods */
    int knob;     /* index into the module's faders, or -1 for the bypass switch */
} Control;

typedef struct Rack {
    char      chain[RACK_CHAIN_CAP];   /* the text this rack was derived from */
    ModuleDef mods[RACK_MAX_MODULES];
    int       nmods;
    int       enabled[RACK_MAX_MODULES];

    Param     params[RACK_MAX_CONTROLS];
    KnobCmd   cmds[RACK_MAX_CONTROLS];
    double    values[RACK_MAX_CONTROLS];
    Control   controls[RACK_MAX_CONTROLS];
    int       ncontrols;
    ParamSet  set;      /* over params/values; set.sel is the cursor */

    int       ntaps;
    char      tap_names[GRAPH_MAX_TAPS][32];

    unsigned  rng_state;   /* for randomize; owned here, not file-scope (D12) */
} Rack;

/* The chain new projects start with: the old fixed rack, written out. */
extern const char RACK_DEFAULT_CHAIN[];

/* Derive a rack from chain text for a capture of cap_w x cap_h. Returns 0, or
 * <0 with a one-line reason in err. On failure *r is left untouched. */
int  rack_from_chain(Rack *r, const char *chain, int cap_w, int cap_h, char *err, size_t err_cap);

/* A Rack holds pointers into itself (the parameter set, enum name tables), so
 * a struct copy must go through here. */
void rack_adopt(Rack *dst, const Rack *src);

/* Send one control's current value / one module's enable state to the voice. */
void rack_send_control(const Rack *r, Voice *v, int control);
void rack_send_enable(const Rack *r, Voice *v, int module);
/* Send everything (after loading a preset or restarting the voice). */
void rack_send_all(const Rack *r, Voice *v);

/* Cursor and editing, the fader gestures of ROADMAP section 6. */
void rack_select_next(Rack *r, int dir);
void rack_nudge(Rack *r, Voice *v, int dir, ParamGrain g);
void rack_toggle_selected(Rack *r, Voice *v);
void rack_reset_selected(Rack *r, Voice *v);
void rack_reset_control(Rack *r, Voice *v, int control);
void rack_reset_all(Rack *r, Voice *v);
/* Back to the text values without talking to the voice. */
void rack_neutral(Rack *r);

/* Set one control directly (panel drag). Clamps, sends. */
void rack_set_control(Rack *r, Voice *v, int control, double value);
void rack_toggle_module(Rack *r, Voice *v, int module);
void rack_set_enabled(Rack *r, Voice *v, int module, int on);

/* Randomize: faders wander away from neutral by up to depth (0..1) of their
 * range, with a bias towards small moves; off-by-default modules switch on
 * with a probability that grows with depth. */
void rack_randomize(Rack *r, Voice *v, double depth);

/* One-line status for the selected control, e.g. "rot.angle = 0.031  [on]". */
void rack_describe_selected(const Rack *r, char *buf, size_t cap);

/* The control's value for display: the constant's name for an enum, else the number. */
void rack_format_value(const Rack *r, int control, char *buf, size_t cap);

/* The value as libavfilter sees it: an enum's constant rather than its index.
 * This is what a preset file stores, so old files keep loading. */
double rack_get_raw(const Rack *r, int control);
void   rack_set_raw(Rack *r, int control, double raw);

/* Lookups by module label / option name; -1 when absent. rack_find_control
 * returns the control index of a module's fader. */
int  rack_find_module(const Rack *r, const char *label);
int  rack_find_control(const Rack *r, int module, const char *opt);

#endif
