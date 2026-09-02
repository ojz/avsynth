#ifndef VSYNTH_PROJECT_H
#define VSYNTH_PROJECT_H

#include "rack.h"
#include "voice.h"

/*
 * A project is one SQLite file: the rack definition, patches (knob snapshots
 * in numbered slots), and the last capture/window geometry.
 *
 * Knobs are matched by (module name, option name), never by row id, so the
 * code-defined rack and an older project file stay compatible when modules
 * are added.
 */

typedef struct Project Project;

typedef struct Geometry {
    int cap_x, cap_y, cap_w, cap_h, cap_fps;
    int win_x, win_y, win_w, win_h;
    int valid;   /* 0 if the project had no geometry saved yet */
} Geometry;

/* Opens or creates. On create, inserts the rack definition. Always upserts
 * any modules/knobs the rack has that the file lacks. */
Project *project_open(const char *path, const Rack *rack);
void     project_close(Project *p);

int  project_load_geometry(Project *p, Geometry *g);
int  project_save_geometry(Project *p, const Geometry *g);

/* slot is 1..N. Save replaces whatever was in the slot. */
int  project_save_patch(Project *p, int slot, const char *name, const Rack *rack);
/* Returns 0 and fills rack values/enables; 1 if the slot is empty; <0 on error.
 * Does not talk to the voice: call rack_send_all() afterwards. */
int  project_load_patch(Project *p, int slot, Rack *rack);

/* Number of saved patches, or -1 on error. */
int  project_patch_count(Project *p);

const char *project_path(const Project *p);

#endif
