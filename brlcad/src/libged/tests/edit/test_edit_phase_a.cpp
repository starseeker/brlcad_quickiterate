/*         T E S T _ E D I T _ P H A S E _ A . C P P
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
/** @file test_edit_phase_a.cpp
 *
 * Phase A tests for the libged `edit` command — DbiState null-safety.
 *
 * Verifies that ged_edit2_core works correctly when gedp->dbi_state is
 * NULL (i.e., on older code paths that have not initialized DbiState).
 * This is the primary regression guard for the Phase A null-safety fix.
 *
 * Covers:
 *   - translate (absolute and relative) without DbiState
 *   - rotate without DbiState
 *   - scale without DbiState
 *   - that -S / conflict-arbiter blocks are skipped gracefully without DbiState
 */

#include "common.h"

#include <cstdio>
#include <cstring>
#include <cmath>

#include "bu.h"
#include "vmath.h"
#include "raytrace.h"
#include "rt/geom.h"
#include "rt/edit.h"
#include "wdb.h"
#include "ged.h"


/* ------------------------------------------------------------------ *
 * Test bookkeeping
 * ------------------------------------------------------------------ */

static int total_tests  = 0;
static int failed_tests = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        ++total_tests;                                               \
        if (!(cond)) {                                               \
            ++failed_tests;                                          \
            bu_log("FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);  \
        } else {                                                     \
            bu_log("PASS: %s\n", msg);                               \
        }                                                            \
    } while (0)

#define NEAR_ENOUGH 1e-8


/* ------------------------------------------------------------------ *
 * Fixture helpers
 * ------------------------------------------------------------------ */

/** Read back the rt_ell_internal for an ell/sphere by name. */
static int
read_ell(struct ged *gedp, const char *name, struct rt_ell_internal *out)
{
    struct directory *dp = db_lookup(gedp->dbip, name, LOOKUP_QUIET);
    if (dp == RT_DIR_NULL)
        return BRLCAD_ERROR;

    struct rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    if (rt_db_get_internal(&intern, dp, gedp->dbip, NULL) < 0)
        return BRLCAD_ERROR;

    if (intern.idb_minor_type != DB5_MINORTYPE_BRLCAD_ELL) {
        rt_db_free_internal(&intern);
        return BRLCAD_ERROR;
    }

    *out = *(struct rt_ell_internal *)intern.idb_ptr;
    intern.idb_ptr = NULL;
    rt_db_free_internal(&intern);
    return BRLCAD_OK;
}

/**
 * Create the Phase A test fixture.
 *
 *   pa_sph.s  — sphere at (10, 0, 0), r=2
 *   pa_ell.s  — ellipsoid at origin, A=(5,0,0), B=(0,2,0), C=(0,0,3)
 *   pa_sca.s  — sphere at origin, r=3
 */
static int
create_pa_fixture(const char *path)
{
    struct rt_wdb *wdbp = wdb_fopen(path);
    if (!wdbp)
        return BRLCAD_ERROR;

    point_t sp = {10.0, 0.0, 0.0};
    if (mk_sph(wdbp, "pa_sph.s", sp, 2.0) != 0) goto fail;

    {
        point_t ev = VINIT_ZERO;
        vect_t  ea = {5.0, 0.0, 0.0};
        vect_t  eb = {0.0, 2.0, 0.0};
        vect_t  ec = {0.0, 0.0, 3.0};
        if (mk_ell(wdbp, "pa_ell.s", ev, ea, eb, ec) != 0) goto fail;
    }

    {
        point_t sv = VINIT_ZERO;
        if (mk_sph(wdbp, "pa_sca.s", sv, 3.0) != 0) goto fail;
    }

    wdb_close(wdbp);
    return BRLCAD_OK;

fail:
    wdb_close(wdbp);
    return BRLCAD_ERROR;
}

/**
 * Open fixture with new_cmd_forms=1 but WITHOUT DbiState.
 * This is the key difference from the standard Phase 2 fixture.
 */
static struct ged *
open_fixture_no_dbistate(const char *path)
{
    struct ged *gedp = ged_open("db", path, 1);
    if (!gedp)
        return NULL;
    gedp->new_cmd_forms = 1;
    /* Deliberately leave gedp->dbi_state = NULL */
    return gedp;
}


/* ================================================================== *
 * Phase A tests (no DbiState)
 * ================================================================== */

/* PA-1: translate -a without DbiState */
static void
test_pa_translate_abs(struct ged *gedp)
{
    const char *av[] = { "edit", "pa_sph.s", "translate", "-a", "15", "0", "0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 7, av);
    CHECK(ret == BRLCAD_OK, "no-DbiState translate -a returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "pa_sph.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(ell.v[X], 15.0, NEAR_ENOUGH),
              "no-DbiState translate -a: pa_sph.s V.x == 15");
    } else {
        CHECK(0, "no-DbiState translate -a: read_ell succeeded");
    }
}

/* PA-2: translate -r without DbiState */
static void
test_pa_translate_rel(struct ged *gedp)
{
    /* pa_sph.s is now at (15, 0, 0) after PA-1 */
    const char *av[] = { "edit", "pa_sph.s", "translate", "-r", "-5", "0", "0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 7, av);
    CHECK(ret == BRLCAD_OK, "no-DbiState translate -r returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "pa_sph.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(ell.v[X], 10.0, NEAR_ENOUGH),
              "no-DbiState translate -r: pa_sph.s V.x == 10 (restored)");
    } else {
        CHECK(0, "no-DbiState translate -r: read_ell succeeded");
    }
}

/* PA-3: rotate 0 0 90 without DbiState */
static void
test_pa_rotate(struct ged *gedp)
{
    const char *av[] = { "edit", "pa_ell.s", "rotate", "0", "0", "90", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 6, av);
    CHECK(ret == BRLCAD_OK, "no-DbiState rotate 0 0 90 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "pa_ell.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(ell.a[X], 0.0, NEAR_ENOUGH),
              "no-DbiState rotate: pa_ell.s A.x ≈ 0");
        CHECK(NEAR_EQUAL(ell.a[Y], 5.0, NEAR_ENOUGH),
              "no-DbiState rotate: pa_ell.s A.y ≈ 5");
    } else {
        CHECK(0, "no-DbiState rotate: read_ell succeeded");
    }
}

/* PA-4: scale 2 without DbiState */
static void
test_pa_scale(struct ged *gedp)
{
    const char *av[] = { "edit", "pa_sca.s", "scale", "2", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 4, av);
    CHECK(ret == BRLCAD_OK, "no-DbiState scale 2 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "pa_sca.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(MAGNITUDE(ell.a), 6.0, NEAR_ENOUGH),
              "no-DbiState scale 2: pa_sca.s |A| == 6");
    } else {
        CHECK(0, "no-DbiState scale 2: read_ell succeeded");
    }
}

/* PA-5: -S flag with no DbiState must not crash (it just has nothing to do) */
static void
test_pa_S_flag_no_crash(struct ged *gedp)
{
    /* -S without DbiState should return an error (no geometry resolved),
     * but must not crash or dereference a null pointer. */
    const char *av[] = { "edit", "-S", "translate", "-r", "1", "0", "0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    /* We don't assert OK/ERROR here — just that it doesn't crash. */
    ged_exec(gedp, 7, av);
    CHECK(1, "no-DbiState -S flag does not crash");
}

/* PA-6: unknown object without DbiState gives a clean error */
static void
test_pa_unknown_obj(struct ged *gedp)
{
    const char *av[] = { "edit", "nonexistent.s", "translate", "-r", "1", "0", "0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 7, av);
    CHECK(ret == BRLCAD_ERROR, "no-DbiState unknown object returns error");
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

    /* Create fixture */
    if (!bu_file_exists(bu_vls_cstr(&db_path), NULL) ||
        bu_file_size(bu_vls_cstr(&db_path)) == 0)
    {
        if (create_pa_fixture(bu_vls_cstr(&db_path)) != BRLCAD_OK) {
            bu_log("ERROR: fixture creation failed\n");
            bu_vls_free(&db_path);
            return 1;
        }
    }

    gedp = open_fixture_no_dbistate(bu_vls_cstr(&db_path));
    if (!gedp) {
        bu_log("ERROR: ged_open failed for %s\n", bu_vls_cstr(&db_path));
        bu_vls_free(&db_path);
        return 1;
    }

    /* Confirm DbiState is actually NULL */
    CHECK(gedp->dbi_state == NULL, "fixture opened without DbiState (dbi_state == NULL)");

    /* ---- Phase A tests -------------------------------------------- */
    test_pa_translate_abs(gedp);
    test_pa_translate_rel(gedp);
    test_pa_rotate(gedp);
    test_pa_scale(gedp);
    test_pa_S_flag_no_crash(gedp);
    test_pa_unknown_obj(gedp);

    /* ---- Summary -------------------------------------------------- */
    bu_log("\n========================================\n");
    bu_log("edit Phase A tests: %d/%d passed\n",
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
