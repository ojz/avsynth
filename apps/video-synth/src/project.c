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
    "  win_x INT, win_y INT, win_w INT, win_h INT, current_chain INT);"
    "CREATE TABLE IF NOT EXISTS chain ("
    "  id INTEGER PRIMARY KEY, position INT, name TEXT, text TEXT NOT NULL,"
    "  created_at TEXT DEFAULT (datetime('now')));"
    "CREATE TABLE IF NOT EXISTS preset ("
    "  id INTEGER PRIMARY KEY, chain_id INT NOT NULL REFERENCES chain(id) ON DELETE CASCADE,"
    "  slot INT NOT NULL, name TEXT, created_at TEXT, UNIQUE (chain_id, slot));"
    "CREATE TABLE IF NOT EXISTS preset_value ("
    "  preset_id INT NOT NULL REFERENCES preset(id) ON DELETE CASCADE,"
    "  target TEXT NOT NULL, option TEXT NOT NULL, value REAL,"
    "  PRIMARY KEY (preset_id, target, option));"
    "CREATE TABLE IF NOT EXISTS preset_enable ("
    "  preset_id INT NOT NULL REFERENCES preset(id) ON DELETE CASCADE,"
    "  target TEXT NOT NULL, enabled INT, PRIMARY KEY (preset_id, target));"
    "INSERT OR IGNORE INTO project (id, name, schema_version) VALUES (1, 'untitled', 2);";

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

/* A v1 file (fixed rack) lacks the current_chain column. Add it; the old
 * module/knob/patch tables are simply left alone. */
static void migrate(sqlite3 *db)
{
    sqlite3_stmt *st = NULL;
    int has = 0;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM pragma_table_info('project') WHERE name = 'current_chain'", -1, &st, NULL) == SQLITE_OK) {
        has = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
    }
    if (!has) exec(db, "ALTER TABLE project ADD COLUMN current_chain INT");
    exec(db, "UPDATE project SET schema_version = 2 WHERE id = 1");
}

Project *project_open(const char *path)
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
    /* create the project table first so migrate() can inspect it */
    exec(p->db, "CREATE TABLE IF NOT EXISTS project (id INTEGER PRIMARY KEY CHECK (id = 1), name TEXT, schema_version INTEGER,"
                " cap_x INT, cap_y INT, cap_w INT, cap_h INT, cap_fps INT, win_x INT, win_y INT, win_w INT, win_h INT)");
    migrate(p->db);
    if (exec(p->db, SCHEMA) < 0) {
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

/* ---------- geometry ---------- */

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

/* ---------- chains ---------- */

static int scalar_int(Project *p, const char *sql, int bind)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(p->db, sql, -1, &st, NULL)) return -1;
    if (bind >= 0) sqlite3_bind_int(st, 1, bind);
    int n = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    return n;
}

int project_chain_count(Project *p)
{
    return scalar_int(p, "SELECT count(*) FROM chain", -1);
}

int project_chain_list(Project *p, ChainInfo *out, int cap)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(p->db, "SELECT id, name FROM chain ORDER BY position, id", -1, &st, NULL)) return -1;
    int n = 0;
    while (n < cap && sqlite3_step(st) == SQLITE_ROW) {
        out[n].id = sqlite3_column_int(st, 0);
        const char *nm = (const char *)sqlite3_column_text(st, 1);
        snprintf(out[n].name, sizeof out[n].name, "%s", nm ? nm : "");
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

int project_chain_add(Project *p, const char *name, const char *text)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(p->db,
            "INSERT INTO chain (position, name, text)"
            " VALUES ((SELECT coalesce(max(position), 0) + 1 FROM chain), ?, ?)", -1, &st, NULL)) return -1;
    sqlite3_bind_text(st, 1, name ? name : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, text ? text : "", -1, SQLITE_STATIC);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    if (rc) { fprintf(stderr, "project: add chain: %s\n", sqlite3_errmsg(p->db)); return -1; }
    return (int)sqlite3_last_insert_rowid(p->db);
}

int project_chain_get(Project *p, int id, char *name, size_t name_cap, char *text, size_t text_cap)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(p->db, "SELECT name, text FROM chain WHERE id = ?", -1, &st, NULL)) return -1;
    sqlite3_bind_int(st, 1, id);
    int rc = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *nm = (const char *)sqlite3_column_text(st, 0);
        const char *tx = (const char *)sqlite3_column_text(st, 1);
        if (name) snprintf(name, name_cap, "%s", nm ? nm : "");
        if (text) snprintf(text, text_cap, "%s", tx ? tx : "");
        rc = 0;
    }
    sqlite3_finalize(st);
    return rc;
}

static int update_chain(Project *p, const char *sql, int id, const char *value)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(p->db, sql, -1, &st, NULL)) return -1;
    sqlite3_bind_text(st, 1, value ? value : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, id);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    return rc;
}

int project_chain_set_text(Project *p, int id, const char *text)
{
    return update_chain(p, "UPDATE chain SET text = ? WHERE id = ?", id, text);
}

int project_chain_rename(Project *p, int id, const char *name)
{
    return update_chain(p, "UPDATE chain SET name = ? WHERE id = ?", id, name);
}

int project_chain_delete(Project *p, int id)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(p->db, "DELETE FROM chain WHERE id = ?", -1, &st, NULL)) return -1;
    sqlite3_bind_int(st, 1, id);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    return rc;
}

int project_current_chain(Project *p)
{
    int id = scalar_int(p, "SELECT coalesce(current_chain, 0) FROM project WHERE id = 1", -1);
    if (id > 0 && scalar_int(p, "SELECT count(*) FROM chain WHERE id = ?", id) == 0) id = 0;
    return id;
}

int project_set_current_chain(Project *p, int id)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(p->db, "UPDATE project SET current_chain = ? WHERE id = 1", -1, &st, NULL)) return -1;
    sqlite3_bind_int(st, 1, id);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    return rc;
}

/* ---------- presets ---------- */

int project_preset_count(Project *p, int chain_id)
{
    return scalar_int(p, "SELECT count(*) FROM preset WHERE chain_id = ?", chain_id);
}

int project_save_preset(Project *p, int chain_id, int slot, const char *name, const Rack *rack)
{
    sqlite3_stmt *st = NULL, *ins_val = NULL, *ins_en = NULL;
    exec(p->db, "BEGIN");

    if (sqlite3_prepare_v2(p->db, "DELETE FROM preset WHERE chain_id = ? AND slot = ?", -1, &st, NULL)) goto fail;
    sqlite3_bind_int(st, 1, chain_id);
    sqlite3_bind_int(st, 2, slot);
    sqlite3_step(st);
    sqlite3_finalize(st); st = NULL;

    if (sqlite3_prepare_v2(p->db,
            "INSERT INTO preset (chain_id, slot, name, created_at) VALUES (?, ?, ?, datetime('now'))", -1, &st, NULL)) goto fail;
    sqlite3_bind_int(st, 1, chain_id);
    sqlite3_bind_int(st, 2, slot);
    sqlite3_bind_text(st, 3, name ? name : "", -1, SQLITE_STATIC);
    if (sqlite3_step(st) != SQLITE_DONE) goto fail;
    sqlite3_finalize(st); st = NULL;
    sqlite3_int64 preset_id = sqlite3_last_insert_rowid(p->db);

    if (sqlite3_prepare_v2(p->db,
            "INSERT INTO preset_value (preset_id, target, option, value) VALUES (?, ?, ?, ?)", -1, &ins_val, NULL)) goto fail;
    if (sqlite3_prepare_v2(p->db,
            "INSERT INTO preset_enable (preset_id, target, enabled) VALUES (?, ?, ?)", -1, &ins_en, NULL)) goto fail;

    for (int m = 0; m < rack->nmods; m++) {
        const ModuleDef *md = &rack->mods[m];
        for (int k = 0; k < md->nknobs; k++) {
            sqlite3_reset(ins_val);
            sqlite3_bind_int64(ins_val, 1, preset_id);
            sqlite3_bind_text(ins_val, 2, md->name, -1, SQLITE_STATIC);
            sqlite3_bind_text(ins_val, 3, md->knobs[k].opt, -1, SQLITE_STATIC);
            sqlite3_bind_double(ins_val, 4, rack->values[m][k]);
            sqlite3_step(ins_val);
        }
        if (md->bypassable) {
            sqlite3_reset(ins_en);
            sqlite3_bind_int64(ins_en, 1, preset_id);
            sqlite3_bind_text(ins_en, 2, md->name, -1, SQLITE_STATIC);
            sqlite3_bind_int(ins_en, 3, rack->enabled[m] ? 1 : 0);
            sqlite3_step(ins_en);
        }
    }
    sqlite3_finalize(ins_val);
    sqlite3_finalize(ins_en);
    exec(p->db, "COMMIT");
    return 0;

fail:
    fprintf(stderr, "project: save preset: %s\n", sqlite3_errmsg(p->db));
    if (st) sqlite3_finalize(st);
    if (ins_val) sqlite3_finalize(ins_val);
    if (ins_en) sqlite3_finalize(ins_en);
    exec(p->db, "ROLLBACK");
    return -1;
}

int project_load_preset(Project *p, int chain_id, int slot, Rack *rack)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(p->db, "SELECT id FROM preset WHERE chain_id = ? AND slot = ?", -1, &st, NULL)) return -1;
    sqlite3_bind_int(st, 1, chain_id);
    sqlite3_bind_int(st, 2, slot);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return 1; }
    sqlite3_int64 preset_id = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);

    /* Start from neutral so knobs missing in an old preset land somewhere sane. */
    for (int m = 0; m < rack->nmods; m++) {
        rack->enabled[m] = rack->mods[m].enabled_default;
        for (int k = 0; k < rack->mods[m].nknobs; k++)
            rack->values[m][k] = rack->mods[m].knobs[k].neutral;
    }

    if (sqlite3_prepare_v2(p->db, "SELECT target, option, value FROM preset_value WHERE preset_id = ?", -1, &st, NULL)) return -1;
    sqlite3_bind_int64(st, 1, preset_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int m = rack_find_module(rack, (const char *)sqlite3_column_text(st, 0));
        int k = rack_find_knob(rack, m, (const char *)sqlite3_column_text(st, 1));
        if (m >= 0 && k >= 0) rack->values[m][k] = sqlite3_column_double(st, 2);
    }
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(p->db, "SELECT target, enabled FROM preset_enable WHERE preset_id = ?", -1, &st, NULL)) return -1;
    sqlite3_bind_int64(st, 1, preset_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int m = rack_find_module(rack, (const char *)sqlite3_column_text(st, 0));
        if (m >= 0) rack->enabled[m] = sqlite3_column_int(st, 1) ? 1 : 0;
    }
    sqlite3_finalize(st);
    return 0;
}
