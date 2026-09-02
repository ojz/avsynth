#include "project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

struct Project {
    sqlite3 *db;
    char     path[1024];
};

static const char *SCHEMA =
    "PRAGMA foreign_keys = ON;"
    "CREATE TABLE IF NOT EXISTS project ("
    "  id INTEGER PRIMARY KEY CHECK (id = 1), name TEXT, schema_version INTEGER,"
    "  cap_x INT, cap_y INT, cap_w INT, cap_h INT, cap_fps INT,"
    "  win_x INT, win_y INT, win_w INT, win_h INT);"
    "CREATE TABLE IF NOT EXISTS module ("
    "  id INTEGER PRIMARY KEY, position INT, name TEXT UNIQUE, filter TEXT, static_args TEXT);"
    "CREATE TABLE IF NOT EXISTS knob ("
    "  id INTEGER PRIMARY KEY, module_id INT REFERENCES module(id), option TEXT, label TEXT,"
    "  min REAL, max REAL, neutral REAL, curve TEXT DEFAULT 'lin', UNIQUE (module_id, option));"
    "CREATE TABLE IF NOT EXISTS patch ("
    "  id INTEGER PRIMARY KEY, slot INT UNIQUE, name TEXT, created_at TEXT);"
    "CREATE TABLE IF NOT EXISTS patch_knob ("
    "  patch_id INT REFERENCES patch(id) ON DELETE CASCADE, knob_id INT REFERENCES knob(id),"
    "  value REAL, PRIMARY KEY (patch_id, knob_id));"
    "CREATE TABLE IF NOT EXISTS patch_bypass ("
    "  patch_id INT REFERENCES patch(id) ON DELETE CASCADE, module_id INT REFERENCES module(id),"
    "  enabled INT, PRIMARY KEY (patch_id, module_id));"
    "INSERT OR IGNORE INTO project (id, name, schema_version) VALUES (1, 'untitled', 1);";

static int exec(sqlite3 *db, const char *sql)
{
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "project: sql error: %s\n", err ? err : "?");
        sqlite3_free(err);
    }
    return rc == SQLITE_OK ? 0 : -1;
}

static int upsert_rack(Project *p, const Rack *rack)
{
    sqlite3_stmt *ins_mod = NULL, *ins_knob = NULL;
    int rc = sqlite3_prepare_v2(p->db,
        "INSERT INTO module (position, name, filter, static_args) VALUES (?, ?, ?, ?)"
        " ON CONFLICT(name) DO UPDATE SET position = excluded.position,"
        "   filter = excluded.filter, static_args = excluded.static_args", -1, &ins_mod, NULL);
    if (rc) goto fail;
    rc = sqlite3_prepare_v2(p->db,
        "INSERT INTO knob (module_id, option, label, min, max, neutral)"
        " SELECT id, ?, ?, ?, ?, ? FROM module WHERE name = ?"
        " ON CONFLICT(module_id, option) DO UPDATE SET label = excluded.label,"
        "   min = excluded.min, max = excluded.max, neutral = excluded.neutral", -1, &ins_knob, NULL);
    if (rc) goto fail;

    exec(p->db, "BEGIN");
    for (int m = 0; m < rack->nmods; m++) {
        const ModuleDef *md = &rack->mods[m];
        sqlite3_reset(ins_mod);
        sqlite3_bind_int(ins_mod, 1, m);
        sqlite3_bind_text(ins_mod, 2, md->name, -1, SQLITE_STATIC);
        sqlite3_bind_text(ins_mod, 3, md->filter, -1, SQLITE_STATIC);
        sqlite3_bind_text(ins_mod, 4, md->static_args ? md->static_args : "", -1, SQLITE_STATIC);
        if (sqlite3_step(ins_mod) != SQLITE_DONE) goto fail_tx;
        for (int k = 0; k < md->nknobs; k++) {
            const KnobDef *kd = &md->knobs[k];
            sqlite3_reset(ins_knob);
            sqlite3_bind_text(ins_knob, 1, kd->opt, -1, SQLITE_STATIC);
            sqlite3_bind_text(ins_knob, 2, kd->label, -1, SQLITE_STATIC);
            sqlite3_bind_double(ins_knob, 3, kd->min);
            sqlite3_bind_double(ins_knob, 4, kd->max);
            sqlite3_bind_double(ins_knob, 5, kd->neutral);
            sqlite3_bind_text(ins_knob, 6, md->name, -1, SQLITE_STATIC);
            if (sqlite3_step(ins_knob) != SQLITE_DONE) goto fail_tx;
        }
    }
    exec(p->db, "COMMIT");
    sqlite3_finalize(ins_mod);
    sqlite3_finalize(ins_knob);
    return 0;

fail_tx:
    exec(p->db, "ROLLBACK");
fail:
    fprintf(stderr, "project: upsert rack: %s\n", sqlite3_errmsg(p->db));
    sqlite3_finalize(ins_mod);
    sqlite3_finalize(ins_knob);
    return -1;
}

Project *project_open(const char *path, const Rack *rack)
{
    Project *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    snprintf(p->path, sizeof p->path, "%s", path);

    if (sqlite3_open(path, &p->db) != SQLITE_OK) {
        fprintf(stderr, "project: cannot open %s: %s\n", path, sqlite3_errmsg(p->db));
        sqlite3_close(p->db);
        free(p);
        return NULL;
    }
    if (exec(p->db, SCHEMA) < 0 || upsert_rack(p, rack) < 0) {
        project_close(p);
        return NULL;
    }
    fprintf(stderr, "project: %s\n", path);
    return p;
}

void project_close(Project *p)
{
    if (!p) return;
    sqlite3_close(p->db);
    free(p);
}

const char *project_path(const Project *p) { return p->path; }

int project_load_geometry(Project *p, Geometry *g)
{
    sqlite3_stmt *st = NULL;
    memset(g, 0, sizeof *g);
    if (sqlite3_prepare_v2(p->db,
            "SELECT cap_x, cap_y, cap_w, cap_h, cap_fps, win_x, win_y, win_w, win_h"
            " FROM project WHERE id = 1 AND cap_w IS NOT NULL", -1, &st, NULL))
        return -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        g->cap_x = sqlite3_column_int(st, 0); g->cap_y = sqlite3_column_int(st, 1);
        g->cap_w = sqlite3_column_int(st, 2); g->cap_h = sqlite3_column_int(st, 3);
        g->cap_fps = sqlite3_column_int(st, 4);
        g->win_x = sqlite3_column_int(st, 5); g->win_y = sqlite3_column_int(st, 6);
        g->win_w = sqlite3_column_int(st, 7); g->win_h = sqlite3_column_int(st, 8);
        g->valid = 1;
    }
    sqlite3_finalize(st);
    return 0;
}

int project_save_geometry(Project *p, const Geometry *g)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(p->db,
            "UPDATE project SET cap_x=?, cap_y=?, cap_w=?, cap_h=?, cap_fps=?,"
            " win_x=?, win_y=?, win_w=?, win_h=? WHERE id = 1", -1, &st, NULL))
        return -1;
    sqlite3_bind_int(st, 1, g->cap_x); sqlite3_bind_int(st, 2, g->cap_y);
    sqlite3_bind_int(st, 3, g->cap_w); sqlite3_bind_int(st, 4, g->cap_h);
    sqlite3_bind_int(st, 5, g->cap_fps);
    sqlite3_bind_int(st, 6, g->win_x); sqlite3_bind_int(st, 7, g->win_y);
    sqlite3_bind_int(st, 8, g->win_w); sqlite3_bind_int(st, 9, g->win_h);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    return rc;
}

int project_save_patch(Project *p, int slot, const char *name, const Rack *rack)
{
    sqlite3_stmt *st = NULL;
    exec(p->db, "BEGIN");

    if (sqlite3_prepare_v2(p->db, "DELETE FROM patch WHERE slot = ?", -1, &st, NULL)) goto fail;
    sqlite3_bind_int(st, 1, slot);
    sqlite3_step(st);
    sqlite3_finalize(st); st = NULL;

    if (sqlite3_prepare_v2(p->db,
            "INSERT INTO patch (slot, name, created_at) VALUES (?, ?, datetime('now'))", -1, &st, NULL)) goto fail;
    sqlite3_bind_int(st, 1, slot);
    sqlite3_bind_text(st, 2, name ? name : "", -1, SQLITE_STATIC);
    if (sqlite3_step(st) != SQLITE_DONE) goto fail;
    sqlite3_finalize(st); st = NULL;
    sqlite3_int64 patch_id = sqlite3_last_insert_rowid(p->db);

    sqlite3_stmt *ins_val = NULL, *ins_en = NULL;
    if (sqlite3_prepare_v2(p->db,
            "INSERT INTO patch_knob (patch_id, knob_id, value)"
            " SELECT ?, k.id, ? FROM knob k JOIN module m ON m.id = k.module_id"
            " WHERE m.name = ? AND k.option = ?", -1, &ins_val, NULL)) goto fail;
    if (sqlite3_prepare_v2(p->db,
            "INSERT INTO patch_bypass (patch_id, module_id, enabled)"
            " SELECT ?, id, ? FROM module WHERE name = ?", -1, &ins_en, NULL)) { sqlite3_finalize(ins_val); goto fail; }

    for (int m = 0; m < rack->nmods; m++) {
        const ModuleDef *md = &rack->mods[m];
        for (int k = 0; k < md->nknobs; k++) {
            sqlite3_reset(ins_val);
            sqlite3_bind_int64(ins_val, 1, patch_id);
            sqlite3_bind_double(ins_val, 2, rack->values[m][k]);
            sqlite3_bind_text(ins_val, 3, md->name, -1, SQLITE_STATIC);
            sqlite3_bind_text(ins_val, 4, md->knobs[k].opt, -1, SQLITE_STATIC);
            sqlite3_step(ins_val);
        }
        if (md->bypassable) {
            sqlite3_reset(ins_en);
            sqlite3_bind_int64(ins_en, 1, patch_id);
            sqlite3_bind_int(ins_en, 2, rack->enabled[m] ? 1 : 0);
            sqlite3_bind_text(ins_en, 3, md->name, -1, SQLITE_STATIC);
            sqlite3_step(ins_en);
        }
    }
    sqlite3_finalize(ins_val);
    sqlite3_finalize(ins_en);
    exec(p->db, "COMMIT");
    return 0;

fail:
    fprintf(stderr, "project: save patch: %s\n", sqlite3_errmsg(p->db));
    if (st) sqlite3_finalize(st);
    exec(p->db, "ROLLBACK");
    return -1;
}

static int find_module(const Rack *rack, const char *name)
{
    for (int m = 0; m < rack->nmods; m++)
        if (!strcmp(rack->mods[m].name, name)) return m;
    return -1;
}

int project_load_patch(Project *p, int slot, Rack *rack)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(p->db, "SELECT id FROM patch WHERE slot = ?", -1, &st, NULL)) return -1;
    sqlite3_bind_int(st, 1, slot);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return 1; }
    sqlite3_int64 patch_id = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);

    /* Start from neutral so knobs missing in an old patch land somewhere sane. */
    for (int m = 0; m < rack->nmods; m++) {
        rack->enabled[m] = rack->mods[m].enabled_default;
        for (int k = 0; k < rack->mods[m].nknobs; k++)
            rack->values[m][k] = rack->mods[m].knobs[k].neutral;
    }

    if (sqlite3_prepare_v2(p->db,
            "SELECT m.name, k.option, pk.value FROM patch_knob pk"
            " JOIN knob k ON k.id = pk.knob_id JOIN module m ON m.id = k.module_id"
            " WHERE pk.patch_id = ?", -1, &st, NULL)) return -1;
    sqlite3_bind_int64(st, 1, patch_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int m = find_module(rack, (const char *)sqlite3_column_text(st, 0));
        if (m < 0) continue;
        const char *opt = (const char *)sqlite3_column_text(st, 1);
        for (int k = 0; k < rack->mods[m].nknobs; k++)
            if (!strcmp(rack->mods[m].knobs[k].opt, opt))
                rack->values[m][k] = sqlite3_column_double(st, 2);
    }
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(p->db,
            "SELECT m.name, pb.enabled FROM patch_bypass pb JOIN module m ON m.id = pb.module_id"
            " WHERE pb.patch_id = ?", -1, &st, NULL)) return -1;
    sqlite3_bind_int64(st, 1, patch_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int m = find_module(rack, (const char *)sqlite3_column_text(st, 0));
        if (m >= 0) rack->enabled[m] = sqlite3_column_int(st, 1) ? 1 : 0;
    }
    sqlite3_finalize(st);
    return 0;
}
