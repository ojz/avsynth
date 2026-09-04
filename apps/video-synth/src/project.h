#ifndef VSYNTH_PROJECT_H
#define VSYNTH_PROJECT_H

#include "rack.h"

/*
 * A project is one SQLite file holding chains. A chain is filtergraph text
 * plus up to ten presets; a preset is a snapshot of knob values and bypass
 * flags, keyed by (filter instance name, option name) so it survives edits
 * to unrelated parts of the text. The file also remembers which chain was
 * last used and the capture/window geometry.
 *
 *   project 1 -- * chain 1 -- * preset 1 -- * preset_value / preset_enable
 */

typedef struct Project Project;

typedef struct Geometry {
    int cap_x, cap_y, cap_w, cap_h, cap_fps;
    int win_x, win_y, win_w, win_h;
    int valid;   /* 0 if the project had no geometry saved yet */
} Geometry;

typedef struct ChainInfo {
    int  id;
    char name[64];
} ChainInfo;

/* Opens or creates. Returns NULL and logs on error. */
Project *project_open(const char *path);
void     project_close(Project *p);
const char *project_path(const Project *p);

int  project_load_geometry(Project *p, Geometry *g);
int  project_save_geometry(Project *p, const Geometry *g);

/* Chains. Text buffers are RACK_CHAIN_CAP bytes. */
int  project_chain_count(Project *p);
int  project_chain_list(Project *p, ChainInfo *out, int cap);       /* returns count */
int  project_chain_add(Project *p, const char *name, const char *text); /* returns id, or -1 */
int  project_chain_get(Project *p, int id, char *name, size_t name_cap, char *text, size_t text_cap);
int  project_chain_set_text(Project *p, int id, const char *text);
int  project_chain_rename(Project *p, int id, const char *name);
int  project_chain_delete(Project *p, int id);
int  project_current_chain(Project *p);                /* id, or 0 if none */
int  project_set_current_chain(Project *p, int id);

/* Presets, slot is 1..10 within a chain. Save replaces the slot. */
int  project_save_preset(Project *p, int chain_id, int slot, const char *name, const Rack *rack);
/* 0 and fills rack values/enables; 1 if the slot is empty; <0 on error.
 * Does not talk to the voice: call rack_send_all() afterwards. */
int  project_load_preset(Project *p, int chain_id, int slot, Rack *rack);
int  project_preset_count(Project *p, int chain_id);

#endif
