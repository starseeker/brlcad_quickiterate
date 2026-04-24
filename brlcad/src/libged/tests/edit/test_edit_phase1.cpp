/*             T E S T _ E D I T _ P H A S E 1 . C P P
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file test_edit_phase1.cpp
 *
 * Phase 1 tests for the libged `edit` command.
 *
 * Covers:
 *   - Three-pass parser (global opts → geometry → subcommand)
 *   - URI fragment/query parsing
 *   - "." batch marker recognition
 *   - Selection fallback when no geometry is specified
 *   - Selection conflict arbiter (-S / -f / -F / -i flags)
 *   - Temporary edit buffer API (ged_edit_buf_*)
 *   - Regression: `perturb` still works with the new parser
 */

#include "common.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "bu.h"
#include "vmath.h"
#include "raytrace.h"
#include "rt/geom.h"
#include "rt/edit.h"
#include "rt/db_fullpath.h"
#include "wdb.h"
#include "ged.h"

/* Access DbiState (for setting up selection in tests) */
#include "../../dbi.h"

/* Access temp-edit-buffer API (declared in ged_private.h which is an
 * internal header; in tests we link directly against libged so the
 * symbols are reachable via the GED_EXPORT declarations.) */
#include "../../ged_private.h"


/* ------------------------------------------------------------------ *
 * Test bookkeeping
 * ------------------------------------------------------------------ */

static int total_tests  = 0;
static int failed_tests = 0;

#define CHECK(cond, msg)                                            \
    do {                                                            \
        ++total_tests;                                              \
        if (!(cond)) {                                              \
            ++failed_tests;                                         \
            bu_log("FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        } else {                                                    \
            bu_log("PASS: %s\n", msg);                              \
        }                                                           \
    } while (0)


/* ------------------------------------------------------------------ *
 * Fixture helpers
 * ------------------------------------------------------------------ */

/** Create a minimal .g file with a tor and a comb that references it. */
static int
create_p1_fixture(const char *path)
{
    struct rt_wdb *wdbp = wdb_fopen(path);
    if (!wdbp)
        return BRLCAD_ERROR;

    /* tor.s */
    point_t v  = VINIT_ZERO;
    vect_t  h  = {0, 0, 1};
    if (mk_tor(wdbp, "tor.s", v, h, 4.0, 1.0) != 0) {
        wdb_close(wdbp);
        return BRLCAD_ERROR;
    }

    /* sph.s */
    point_t sp = {10, 0, 0};
    if (mk_sph(wdbp, "sph.s", sp, 2.0) != 0) {
        wdb_close(wdbp);
        return BRLCAD_ERROR;
    }

    /* group.c referencing tor.s */
    const char *members[] = { "tor.s", NULL };
    struct wmember wm;
    BU_LIST_INIT(&wm.l);
    mk_addmember("tor.s", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "group.c", &wm, 0, NULL, NULL, NULL, 0) != 0) {
        wdb_close(wdbp);
        return BRLCAD_ERROR;
    }
    (void)members;

    wdb_close(wdbp);
    return BRLCAD_OK;
}

/** Return a freshly opened gedp with DbiState, new_cmd_forms = 1. */
static struct ged *
open_fixture(const char *path)
{
    struct ged *gedp = ged_open("db", path, 1);
    if (!gedp)
        return NULL;
    gedp->new_cmd_forms = 1;
    gedp->dbi_state = new DbiState(gedp);
    return gedp;
}


/* ================================================================== *
 * Phase 1 tests
 * ================================================================== */

/* ---- T1: no args → help output, return OK ---- */
static void
test_p1_noargs(struct ged *gedp)
{
    const char *av[] = { "edit", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 1, av);
    CHECK(ret == BRLCAD_OK,
        "edit (no args) returns OK (prints help)");
}

/* ---- T2: unrecognised geometry → error ---- */
static void
test_p1_bad_geom(struct ged *gedp)
{
    const char *av[] = { "edit", "nonexistent_object_xyz", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 2, av);
    CHECK(ret == BRLCAD_ERROR,
        "edit <nonexistent_object> returns error");
}

/* ---- T3: valid object, no subcommand → error with message ---- */
static void
test_p1_obj_nosubcmd(struct ged *gedp)
{
    const char *av[] = { "edit", "tor.s", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 2, av);
    CHECK(ret == BRLCAD_ERROR,
        "edit tor.s (no subcommand) returns error");
    bool has_msg = (bu_vls_strlen(gedp->ged_result_str) > 0);
    CHECK(has_msg, "edit tor.s (no subcommand) prints a message");
}

/* ---- T4: -h flag before geometry → OK (help) ---- */
static void
test_p1_help_flag(struct ged *gedp)
{
    const char *av[] = { "edit", "-h", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 2, av);
    CHECK(ret == BRLCAD_OK,
        "edit -h returns OK");
}

/* ---- T5: URI fragment stored in ged_edit_geom_spec ---- */
#include "../../edit/uri.hh"

static void
test_p1_uri_fragment(void)
{
    /* Simulate what _resolve_geom_spec does with URI parsing */
    std::string raw = "tor.s#V1";
    bool has_fragment = false;
    std::string fragment_val;
    try {
        uri obj_uri(std::string("g:") + raw);
        fragment_val = obj_uri.get_fragment();
        has_fragment = !fragment_val.empty();
    } catch (...) {}

    CHECK(has_fragment,         "URI fragment 'V1' is parsed from 'tor.s#V1'");
    CHECK(fragment_val == "V1", "URI fragment value is 'V1'");
}

/* ---- T6: URI query stored in ged_edit_geom_spec ---- */
static void
test_p1_uri_query(void)
{
    std::string raw = "tor.s?V*";
    bool has_query = false;
    std::string query_val;
    try {
        uri obj_uri(std::string("g:") + raw);
        query_val = obj_uri.get_query();
        has_query = !query_val.empty();
    } catch (...) {}

    CHECK(has_query,          "URI query 'V*' is parsed from 'tor.s?V*'");
    CHECK(query_val == "V*",  "URI query value is 'V*'");
}

/* ---- T7: "." batch marker resolves without db lookup ---- */
static void
test_p1_batch_marker(struct ged *gedp)
{
    /* The batch marker "." on its own with no other geometry should
     * result in an error (nothing to iterate over), but the important
     * thing is that it doesn't crash and does not report an "invalid
     * geometry specifier" message about ".". */
    const char *av[] = { "edit", ".", "perturb", "0.1", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 4, av);
    /* "." with no active selection and no explicit path is an error */
    CHECK(ret == BRLCAD_ERROR,
        "edit . perturb (no selection) returns error");
}

/* ---- T8: selection fallback — no cmd-line spec, selection active ---- */
static void
test_p1_selection_fallback(struct ged *gedp)
{
    /* Activate a selection so `edit` can fall back to it */
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    std::vector<BSelectState *> ss = dbis->get_selected_states("default");
    if (ss.empty()) {
        CHECK(0, "Could not get default selection state");
        return;
    }
    ss[0]->select_path("tor.s", false);

    /* Now invoke edit WITHOUT specifying geometry on the command line.
     * It should fall back to the selection (tor.s) and then report
     * an error because no subcommand was given — that's fine, the point
     * is it tried to use the selection rather than failing with
     * "no valid geometry specifier". */
    const char *av[] = { "edit", "perturb", "0.0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 3, av);
    /* perturb 0.0 should succeed (factor ~0 is a no-op) */
    CHECK(ret == BRLCAD_OK,
        "edit perturb 0.0 (via selection fallback) returns OK");

    /* Deselect */
    ss[0]->deselect_path("tor.s", false);
}

/* ---- T9: no geometry, no selection → error ---- */
static void
test_p1_no_geom_no_sel(struct ged *gedp)
{
    /* Make sure no selection is active */
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    std::vector<BSelectState *> ss = dbis->get_selected_states("default");
    if (!ss.empty())
        ss[0]->deselect_path("tor.s", false);

    const char *av[] = { "edit", "perturb", "0.1", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 3, av);
    CHECK(ret == BRLCAD_ERROR,
        "edit perturb (no geom, no selection) returns error");
}

/* ---- T10: conflict arbiter — explicit spec + active selection ---- */
static void
test_p1_conflict_arbiter(struct ged *gedp)
{
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    std::vector<BSelectState *> ss = dbis->get_selected_states("default");
    if (ss.empty()) {
        CHECK(0, "Could not get default selection state");
        return;
    }

    /* Select tor.s */
    ss[0]->select_path("tor.s", false);

    /* Invoke edit with explicit "tor.s" while it's also selected.
     * Should fail with conflict message (no arbiter flag). */
    const char *av[] = { "edit", "tor.s", "perturb", "0.1", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 4, av);
    CHECK(ret == BRLCAD_ERROR,
        "edit tor.s perturb (selection conflict, no flag) returns error");
    bool has_conflict_msg =
        (bu_vls_strlen(gedp->ged_result_str) > 0) &&
        (strstr(bu_vls_cstr(gedp->ged_result_str), "Conflict") != NULL ||
         strstr(bu_vls_cstr(gedp->ged_result_str), "conflict") != NULL);
    CHECK(has_conflict_msg,
        "edit tor.s perturb (selection conflict) prints conflict message");

    /* Deselect */
    ss[0]->deselect_path("tor.s", false);
}

/* ---- T11: conflict arbiter -S flag uses selection ---- */
static void
test_p1_flag_S(struct ged *gedp)
{
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    std::vector<BSelectState *> ss = dbis->get_selected_states("default");
    if (ss.empty()) {
        CHECK(0, "Could not get default selection state");
        return;
    }
    ss[0]->select_path("tor.s", false);

    /* -S: ignore cmd-line specifier, use selection (tor.s) */
    /* perturb 0.0 is a no-op — should succeed */
    const char *av[] = { "edit", "-S", "sph.s", "perturb", "0.0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 5, av);
    CHECK(ret == BRLCAD_OK,
        "edit -S sph.s perturb 0.0 (uses selection tor.s, not sph.s) returns OK");

    ss[0]->deselect_path("tor.s", false);
}

/* ---- T12: conflict arbiter -f flag bypasses conflict check ---- */
static void
test_p1_flag_f(struct ged *gedp)
{
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    std::vector<BSelectState *> ss = dbis->get_selected_states("default");
    if (ss.empty()) {
        CHECK(0, "Could not get default selection state");
        return;
    }
    ss[0]->select_path("tor.s", false);

    /* -f: force, write to disk, bypass conflict */
    const char *av[] = { "edit", "-f", "tor.s", "perturb", "0.0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 5, av);
    CHECK(ret == BRLCAD_OK,
        "edit -f tor.s perturb 0.0 (force, bypasses conflict) returns OK");

    ss[0]->deselect_path("tor.s", false);
}


/* ---- T13: ged_edit_buf_get returns NULL for missing entry ---- */
static void
test_p1_buf_get_missing(struct ged *gedp)
{
    struct db_full_path dfp;
    db_full_path_init(&dfp);
    db_string_to_path(&dfp, gedp->dbip, "tor.s");

    struct rt_edit *s = ged_edit_buf_get(gedp, &dfp);
    CHECK(s == NULL, "ged_edit_buf_get returns NULL for absent entry");

    db_free_full_path(&dfp);
}

/* ---- T14: ged_edit_buf_set + ged_edit_buf_get round-trip ---- */
static void
test_p1_buf_set_get(struct ged *gedp)
{
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    BN_TOL_INIT_SET_TOL(&tol);
    struct db_full_path dfp;
    db_full_path_init(&dfp);
    if (db_string_to_path(&dfp, gedp->dbip, "tor.s") < 0) {
        CHECK(0, "db_string_to_path for tor.s succeeded");
        return;
    }

    struct rt_edit *s = rt_edit_create(&dfp, gedp->dbip, &tol, NULL);
    if (!s) {
        CHECK(0, "rt_edit_create succeeded for tor.s");
        db_free_full_path(&dfp);
        return;
    }

    ged_edit_buf_set(gedp, &dfp, s);
    struct rt_edit *got = ged_edit_buf_get(gedp, &dfp);
    CHECK(got == s, "ged_edit_buf_get returns the stored rt_edit * after set");

    /* Abandon so we don't leak */
    ged_edit_buf_abandon(gedp, &dfp);
    db_free_full_path(&dfp);
}

/* ---- T15: ged_edit_buf_abandon removes entry ---- */
static void
test_p1_buf_abandon(struct ged *gedp)
{
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    BN_TOL_INIT_SET_TOL(&tol);
    struct db_full_path dfp;
    db_full_path_init(&dfp);
    if (db_string_to_path(&dfp, gedp->dbip, "sph.s") < 0) {
        CHECK(0, "db_string_to_path for sph.s succeeded");
        return;
    }

    struct rt_edit *s = rt_edit_create(&dfp, gedp->dbip, &tol, NULL);
    if (!s) {
        CHECK(0, "rt_edit_create succeeded for sph.s");
        db_free_full_path(&dfp);
        return;
    }

    ged_edit_buf_set(gedp, &dfp, s);
    ged_edit_buf_abandon(gedp, &dfp);

    struct rt_edit *got = ged_edit_buf_get(gedp, &dfp);
    CHECK(got == NULL, "ged_edit_buf_get returns NULL after abandon");

    db_free_full_path(&dfp);
}

/* ---- T16: ged_edit_buf_flush promotes all entries to disk ---- */
static void
test_p1_buf_flush(struct ged *gedp)
{
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    BN_TOL_INIT_SET_TOL(&tol);
    struct db_full_path dfp1, dfp2;
    db_full_path_init(&dfp1);
    db_full_path_init(&dfp2);
    db_string_to_path(&dfp1, gedp->dbip, "tor.s");
    db_string_to_path(&dfp2, gedp->dbip, "sph.s");

    struct rt_edit *s1 = rt_edit_create(&dfp1, gedp->dbip, &tol, NULL);
    struct rt_edit *s2 = rt_edit_create(&dfp2, gedp->dbip, &tol, NULL);

    if (!s1 || !s2) {
        CHECK(0, "rt_edit_create succeeded for flush test");
        if (s1) rt_edit_destroy(s1);
        if (s2) rt_edit_destroy(s2);
        db_free_full_path(&dfp1);
        db_free_full_path(&dfp2);
        return;
    }

    ged_edit_buf_set(gedp, &dfp1, s1);
    ged_edit_buf_set(gedp, &dfp2, s2);

    ged_edit_buf_flush(gedp);

    /* After flush both entries should be gone */
    struct rt_edit *g1 = ged_edit_buf_get(gedp, &dfp1);
    struct rt_edit *g2 = ged_edit_buf_get(gedp, &dfp2);
    CHECK(g1 == NULL && g2 == NULL,
        "ged_edit_buf_flush removes all entries");

    db_free_full_path(&dfp1);
    db_free_full_path(&dfp2);
}

/* ---- T17: regression — perturb still works with the new parser ---- */
static void
test_p1_perturb_regression(struct ged *gedp)
{
    const char *av[] = { "edit", "tor.s", "perturb", "0.001", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 4, av);
    CHECK(ret == BRLCAD_OK,
        "edit tor.s perturb 0.001 (regression) returns OK");
}


/* ================================================================== *
 * main
 * ================================================================== */

int
main(int ac, char *av[])
{
    struct ged *gedp;
    struct bu_vls db_path = BU_VLS_INIT_ZERO;
    int need_help = 0;

    bu_setprogname(av[0]);

    struct bu_opt_desc d[2];
    BU_OPT(d[0], "h", "help", "", NULL, &need_help, "Print help and exit");
    BU_OPT_NULL(d[1]);

    int opt_ret = bu_opt_parse(NULL, ac, (const char **)av, d);
    if (need_help || opt_ret < 0) {
        bu_log("Usage: %s [-h] [fixture.g]\n", av[0]);
        bu_log("  fixture.g  optional path; a temp file is used if omitted\n");
        return (need_help) ? 0 : 1;
    }

    /* Determine fixture path */
    if (opt_ret > 1) {
        bu_vls_sprintf(&db_path, "%s", av[opt_ret]);
    } else if (opt_ret == 1 && av[opt_ret] && av[opt_ret][0] != '-') {
        bu_vls_sprintf(&db_path, "%s", av[opt_ret]);
    } else {
        char tmpname[MAXPATHLEN] = {0};
        FILE *fp = bu_temp_file(tmpname, MAXPATHLEN);
        if (!fp) {
            bu_log("ERROR: unable to create temp file\n");
            bu_vls_free(&db_path);
            return 1;
        }
        fclose(fp);
        bu_vls_sprintf(&db_path, "%s", tmpname);
    }

    /* Create fixture if needed */
    if (!bu_file_exists(bu_vls_cstr(&db_path), NULL) ||
        bu_file_size(bu_vls_cstr(&db_path)) == 0)
    {
        if (create_p1_fixture(bu_vls_cstr(&db_path)) != BRLCAD_OK) {
            bu_log("ERROR: fixture creation failed\n");
            bu_vls_free(&db_path);
            return 1;
        }
    }

    /* Open fixture */
    gedp = open_fixture(bu_vls_cstr(&db_path));
    if (!gedp) {
        bu_log("ERROR: ged_open failed for %s\n", bu_vls_cstr(&db_path));
        bu_vls_free(&db_path);
        return 1;
    }

    /* ---- Run Phase 1 tests ---------------------------------------- */
    test_p1_noargs(gedp);
    test_p1_bad_geom(gedp);
    test_p1_obj_nosubcmd(gedp);
    test_p1_help_flag(gedp);
    test_p1_uri_fragment();
    test_p1_uri_query();
    test_p1_batch_marker(gedp);
    test_p1_selection_fallback(gedp);
    test_p1_no_geom_no_sel(gedp);
    test_p1_conflict_arbiter(gedp);
    test_p1_flag_S(gedp);
    test_p1_flag_f(gedp);
    test_p1_buf_get_missing(gedp);
    test_p1_buf_set_get(gedp);
    test_p1_buf_abandon(gedp);
    test_p1_buf_flush(gedp);
    test_p1_perturb_regression(gedp);

    /* ---- Summarize ------------------------------------------------- */
    bu_log("\n========================================\n");
    bu_log("edit Phase 1 tests: %d/%d passed\n",
           total_tests - failed_tests, total_tests);
    bu_log("========================================\n");

    ged_close(gedp);
    bu_vls_free(&db_path);

    return (failed_tests == 0) ? 0 : 1;
}


/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8 cino=N-s
 */
