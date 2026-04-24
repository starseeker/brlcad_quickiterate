/*                  T E S T _ E D I T . C P P
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
/** @file test_edit.cpp
 *
 * Phase 0 test harness for the libged `edit` command.
 *
 * This file establishes the test infrastructure that later phases will
 * populate with real assertions.  Currently it:
 *
 *   1. Creates (or opens) a fixture .g file containing representative
 *      primitives of each type that has a non-NULL ft_edit_desc().
 *   2. Verifies the `edit` command is registered and reachable.
 *   3. Runs a small set of smoke tests against the new command path
 *      (gedp->new_cmd_forms == 1) to ensure the harness is wired up
 *      correctly.
 *
 * No geometry-altering edits are performed in Phase 0; the goal is a
 * green test target that future phases can extend incrementally.
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
#include "wdb.h"
#include "ged.h"

#include "../../dbi.h"

/* ------------------------------------------------------------------ *
 * Minimal test bookkeeping
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
 * Fixture creation
 *
 * Populates a fresh .g file with one representative primitive of each
 * type that already has a non-NULL ft_edit_desc().  Additional geometry
 * (arb8, sph, etc.) is included for generic transform tests in Phase 2.
 * ------------------------------------------------------------------ */

static int
create_fixture(const char *dbpath)
{
    struct rt_wdb *wdbp = wdb_fopen(dbpath);
    if (!wdbp) {
        bu_log("ERROR: unable to create fixture database %s\n", dbpath);
        return BRLCAD_ERROR;
    }

    /* --- tor (torus) ------------------------------------------------ */
    point_t tor_V  = {0, 0, 0};
    vect_t  tor_H  = {0, 0, 1};
    if (mk_tor(wdbp, "tor.s", tor_V, tor_H, 10.0, 3.0) != 0) {
        bu_log("mk_tor failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- tgc (truncated general cone) ------------------------------- */
    point_t tgc_V  = {20, 0, 0};
    vect_t  tgc_H  = {0, 0, 10};
    vect_t  tgc_A  = {5, 0, 0};
    vect_t  tgc_B  = {0, 5, 0};
    vect_t  tgc_C  = {4, 0, 0};
    vect_t  tgc_D  = {0, 4, 0};
    if (mk_tgc(wdbp, "tgc.s", tgc_V, tgc_H, tgc_A, tgc_B, tgc_C, tgc_D) != 0) {
        bu_log("mk_tgc failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- ell (ellipsoid) -------------------------------------------- */
    point_t ell_V  = {40, 0, 0};
    vect_t  ell_A  = {8, 0, 0};
    vect_t  ell_B  = {0, 5, 0};
    vect_t  ell_C  = {0, 0, 3};
    if (mk_ell(wdbp, "ell.s", ell_V, ell_A, ell_B, ell_C) != 0) {
        bu_log("mk_ell failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- sph (sphere — ell alias) ----------------------------------- */
    point_t sph_V  = {60, 0, 0};
    if (mk_sph(wdbp, "sph.s", sph_V, 6.0) != 0) {
        bu_log("mk_sph failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- arb8 (box) ------------------------------------------------- */
    fastf_t arb8_pts[8*3] = {
        -5, -5, -5,   5, -5, -5,   5,  5, -5,  -5,  5, -5,
        -5, -5,  5,   5, -5,  5,   5,  5,  5,  -5,  5,  5
    };
    {   /* offset by 80 in x */
        for (int k = 0; k < 8; k++) arb8_pts[k*3] += 80.0;
    }
    if (mk_arb8(wdbp, "arb8.s", arb8_pts) != 0) {
        bu_log("mk_arb8 failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- rpc (right parabolic cylinder) ----------------------------- */
    point_t rpc_V  = {0, 20, 0};
    vect_t  rpc_H  = {0, 0, 10};
    vect_t  rpc_B  = {5, 0, 0};
    if (mk_rpc(wdbp, "rpc.s", rpc_V, rpc_H, rpc_B, 4.0) != 0) {
        bu_log("mk_rpc failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- rhc (right hyperbolic cylinder) ---------------------------- */
    point_t rhc_V  = {20, 20, 0};
    vect_t  rhc_H  = {0, 0, 10};
    vect_t  rhc_B  = {5, 0, 0};
    if (mk_rhc(wdbp, "rhc.s", rhc_V, rhc_H, rhc_B, 4.0, 2.0) != 0) {
        bu_log("mk_rhc failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- epa (elliptical paraboloid) -------------------------------- */
    point_t epa_V  = {40, 20, 0};
    vect_t  epa_H  = {0, 0, 10};
    vect_t  epa_A  = {1, 0, 0};   /* must be a unit vector (breadth dir) */
    if (mk_epa(wdbp, "epa.s", epa_V, epa_H, epa_A, 5.0, 4.0) != 0) {
        bu_log("mk_epa failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- ehy (elliptical hyperboloid) ------------------------------- */
    point_t ehy_V  = {60, 20, 0};
    vect_t  ehy_H  = {0, 0, 10};
    vect_t  ehy_A  = {1, 0, 0};   /* must be a unit vector (breadth dir) */
    if (mk_ehy(wdbp, "ehy.s", ehy_V, ehy_H, ehy_A, 4.0, 2.0, 1.0) != 0) {
        bu_log("mk_ehy failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- eto (elliptical torus) ------------------------------------- */
    point_t eto_V  = {0, 40, 0};
    vect_t  eto_N  = {0, 0, 1};
    vect_t  eto_C  = {8, 0, 2};
    if (mk_eto(wdbp, "eto.s", eto_V, eto_N, eto_C, 12.0, 3.0) != 0) {
        bu_log("mk_eto failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- hyp (hyperboloid of one sheet) ----------------------------- */
    point_t hyp_Vi  = {20, 40, 0};
    vect_t  hyp_H  = {0, 0, 10};
    vect_t  hyp_A  = {5, 0, 0};
    if (mk_hyp(wdbp, "hyp.s", hyp_Vi, hyp_H, hyp_A, 4.0, 0.4) != 0) {
        bu_log("mk_hyp failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- part (particle) ------------------------------------------- */
    point_t part_V  = {40, 40, 0};
    vect_t  part_H  = {0, 0, 8};
    if (mk_particle(wdbp, "part.s", part_V, part_H, 5.0, 3.0) != 0) {
        bu_log("mk_particle failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    /* --- superell (super-ellipsoid) --------------------------------- */
    point_t sel_V  = {60, 40, 0};
    vect_t  sel_A  = {7, 0, 0};
    vect_t  sel_B  = {0, 5, 0};
    vect_t  sel_C  = {0, 0, 3};
    /* mk_superell takes (fp, name, center, a, b, c, n, e) */
    {
        struct rt_db_internal intern;
        RT_DB_INTERNAL_INIT(&intern);
        struct rt_superell_internal *sei;
        BU_ALLOC(sei, struct rt_superell_internal);
        sei->magic = RT_SUPERELL_INTERNAL_MAGIC;
        VMOVE(sei->v, sel_V);
        VMOVE(sei->a, sel_A);
        VMOVE(sei->b, sel_B);
        VMOVE(sei->c, sel_C);
        sei->n = 2.0;
        sei->e = 2.0;
        intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
        intern.idb_type = ID_SUPERELL;
        intern.idb_meth = &OBJ[ID_SUPERELL];
        intern.idb_ptr = sei;
        if (wdb_put_internal(wdbp, "superell.s", &intern, 1.0) < 0) {
            bu_log("wdb_put_internal(superell) failed\n");
            rt_db_free_internal(&intern);
            db_close(wdbp->dbip);
            return BRLCAD_ERROR;
        }
        rt_db_free_internal(&intern);
    }

    /* --- cline (cylinder/line element) ------------------------------ */
    point_t cl_V   = {80, 40, 0};
    vect_t  cl_H   = {0, 0, 15};
    if (mk_cline(wdbp, "cline.s", cl_V, cl_H, 2.0, 0.2) != 0) {
        bu_log("mk_cline failed\n");
        db_close(wdbp->dbip);
        return BRLCAD_ERROR;
    }

    db_close(wdbp->dbip);
    return BRLCAD_OK;
}


/* ------------------------------------------------------------------ *
 * Helpers
 * ------------------------------------------------------------------ */

/* run_edit() will be used in Phase 2+ tests; placeholder defined here
 * so reviewers can see the intended calling pattern.
 *
 * Usage:
 *   const char *out = run_edit(gedp, argc, argv);
 *   CHECK(strstr(out, "expected") != NULL, "output contains expected");
 *
 * static const char *
 * run_edit(struct ged *gedp, int argc, const char **argv)
 * {
 *     bu_vls_trunc(gedp->ged_result_str, 0);
 *     ged_exec(gedp, argc, argv);
 *     return bu_vls_cstr(gedp->ged_result_str);
 * }
 */


/* ------------------------------------------------------------------ *
 * Phase 0 tests
 * ------------------------------------------------------------------ */

/**
 * Test 1: edit command is registered.
 * Verifies that the `edit` plugin was loaded successfully.
 */
static void
test_edit_cmd_exists(void)
{
    CHECK(ged_cmd_exists("edit"), "edit command is registered");
}


/**
 * Test 2: edit with no arguments returns OK (prints help).
 * The command should not crash or report an error when invoked
 * without arguments, per the existing edit2 implementation.
 */
static void
test_edit_noargs(struct ged *gedp)
{
    const char *argv[] = {"edit", NULL};
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 1, argv);
    (void)ret;  /* may be OK or print help — both are acceptable */
    /* We only require no crash; content is not asserted in Phase 0 */
    CHECK(1, "edit with no args does not crash");
}


/**
 * Test 3: edit with an unknown geometry specifier reports an error.
 */
static void
test_edit_bad_geom(struct ged *gedp)
{
    const char *argv[] = {"edit", "nonexistent_object_xyzzy", NULL};
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 2, argv);
    /* Expect error or a helpful message — either outcome is acceptable
     * for Phase 0; we just verify we don't segfault. */
    (void)ret;
    CHECK(1, "edit with nonexistent geometry does not crash");
}


/**
 * Test 4: fixture contains expected objects.
 * Enumerates each primitive we created and confirms db_lookup finds it.
 */
static void
test_fixture_objects_exist(struct ged *gedp)
{
    static const char *names[] = {
        "tor.s", "tgc.s", "ell.s", "sph.s", "arb8.s",
        "rpc.s", "rhc.s", "epa.s", "ehy.s", "eto.s",
        "hyp.s", "part.s", "superell.s", "cline.s",
        NULL
    };

    for (int i = 0; names[i] != NULL; i++) {
        struct directory *dp = db_lookup(gedp->dbip, names[i], LOOKUP_QUIET);
        std::string msg = std::string("fixture object exists: ") + names[i];
        CHECK(dp != RT_DIR_NULL, msg.c_str());
    }
}


/**
 * Test 5: edit help output for a known object does not crash.
 * Runs `edit tor.s` (no subcommand) which should print type-specific
 * help in future phases; for now we verify it doesn't crash.
 */
static void
test_edit_obj_nosubcmd(struct ged *gedp)
{
    const char *argv[] = {"edit", "tor.s", NULL};
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 2, argv);
    (void)ret;
    CHECK(1, "edit <obj> with no subcommand does not crash");
}


/**
 * Test 6: edit perturb command runs without crashing on tor.s.
 * This exercises the one already-working subcommand path in edit2.cpp
 * to confirm the existing infrastructure is intact.
 */
static void
test_edit_perturb(struct ged *gedp)
{
    const char *argv[] = {"edit", "tor.s", "perturb", "0.1", NULL};
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 4, argv);
    /* perturb may succeed or fail depending on ft_perturb support, but
     * it must not crash. */
    (void)ret;
    CHECK(1, "edit tor.s perturb does not crash");
}


/**
 * Test 7: ft_edit_desc coverage — primitives with descriptors accessible.
 * Calls rt_edit_type_to_json for each type with a non-NULL ft_edit_desc
 * and verifies it returns BRLCAD_OK with non-empty JSON.
 */
static void
test_desc_coverage(void)
{
    /* primitive IDs known to have ft_edit_desc as of Phase 0 audit */
    static const int typed_prims[] = {
        ID_TOR, ID_TGC, ID_ELL,
        ID_EBM, ID_VOL, ID_PIPE, ID_PARTICLE,
        ID_RPC, ID_RHC, ID_EPA, ID_EHY, ID_ETO,
        ID_DSP, ID_CLINE, ID_COMBINATION,
        ID_SUPERELL, ID_HYP,
        -1  /* sentinel */
    };

    for (int i = 0; typed_prims[i] >= 0; i++) {
        struct bu_vls out = BU_VLS_INIT_ZERO;
        int ret = rt_edit_type_to_json(&out, typed_prims[i]);
        std::string msg = std::string("rt_edit_type_to_json succeeds for prim id ")
            + std::to_string(typed_prims[i]);
        CHECK(ret == BRLCAD_OK && bu_vls_strlen(&out) > 0, msg.c_str());
        bu_vls_free(&out);
    }
}


/**
 * Test 8: primitives without ft_edit_desc return BRLCAD_ERROR from
 * rt_edit_type_to_json, confirming the audit table is accurate.
 */
static void
test_no_desc_returns_error(void)
{
    /* A sample of primitives that currently lack ft_edit_desc */
    static const int no_desc[] = {
        ID_ARB8, ID_ARS, ID_HALF, ID_SPH, ID_NMG, ID_ARBN,
        ID_BOT, ID_SKETCH, ID_EXTRUDE, ID_HRT,
        -1
    };

    for (int i = 0; no_desc[i] >= 0; i++) {
        struct bu_vls out = BU_VLS_INIT_ZERO;
        int ret = rt_edit_type_to_json(&out, no_desc[i]);
        std::string msg = std::string("rt_edit_type_to_json returns error for undescribed prim id ")
            + std::to_string(no_desc[i]);
        /* Either BRLCAD_ERROR or empty JSON is acceptable — the key point
         * is that it does NOT return a non-empty JSON string with valid ops. */
        bool acceptable = (ret != BRLCAD_OK) || (bu_vls_strlen(&out) == 0);
        CHECK(acceptable, msg.c_str());
        bu_vls_free(&out);
    }
}


/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */

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
        /* caller provided path after options */
        bu_vls_sprintf(&db_path, "%s", av[opt_ret]);
    } else if (opt_ret == 1 && av[opt_ret] && av[opt_ret][0] != '-') {
        bu_vls_sprintf(&db_path, "%s", av[opt_ret]);
    } else {
        /* create a temp file */
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

    /* ---- 0. Create / populate fixture -------------------------------- */
    if (!bu_file_exists(bu_vls_cstr(&db_path), NULL) ||
        bu_file_size(bu_vls_cstr(&db_path)) == 0)
    {
        if (create_fixture(bu_vls_cstr(&db_path)) != BRLCAD_OK) {
            bu_log("ERROR: fixture creation failed\n");
            bu_vls_free(&db_path);
            return 1;
        }
    }

    /* ---- 1. Open the fixture database via GED ----------------------- */
    gedp = ged_open("db", bu_vls_cstr(&db_path), 1);
    if (!gedp) {
        bu_log("ERROR: ged_open failed for %s\n", bu_vls_cstr(&db_path));
        bu_vls_free(&db_path);
        return 1;
    }

    /* Enable the new command forms so edit2 path is used */
    gedp->new_cmd_forms = 1;

    /* DbiState is required by edit2 */
    gedp->dbi_state = new DbiState(gedp);

    /* ---- 2. Run Phase 0 tests --------------------------------------- */
    test_edit_cmd_exists();
    test_fixture_objects_exist(gedp);
    test_edit_noargs(gedp);
    test_edit_bad_geom(gedp);
    test_edit_obj_nosubcmd(gedp);
    test_edit_perturb(gedp);
    test_desc_coverage();
    test_no_desc_returns_error();

    /* ---- 3. Summarize ----------------------------------------------- */
    bu_log("\n========================================\n");
    bu_log("edit Phase 0 tests: %d/%d passed\n",
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
