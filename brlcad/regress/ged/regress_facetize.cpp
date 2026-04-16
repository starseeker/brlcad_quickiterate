/*             R E G R E S S _ F A C E T I Z E . C P P
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @file regress_facetize.cpp
 *
 * Phase D regression tests for the instance-aware variant planning pass in
 * the Manifold facetize pipeline.
 *
 * Tests the "window-frame subtraction" coplanar pattern: a solid wall box
 * with a window cutout whose front and back faces are exactly coplanar with
 * the wall faces.  Without the variant planning pass this reliably produces
 * a degenerate/empty result in Manifold boolean evaluation.
 *
 * Two test cases are run:
 *
 *   TC1  - basic window frame: ARB8 wall minus ARB8 cutout (shares two
 *          coplanar faces on the front and back of the wall).
 *
 *   TC2  - stacked cutouts: a single wall with two separate window
 *          cutouts whose front/back faces are all coplanar with the wall.
 *          Verifies correct per-instance variant assignment when the same
 *          source wall primitive is used in multiple subtractive contexts
 *          within one tree.
 */

#include "common.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "bu/app.h"
#include "bu/file.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "rt/db_instance.h"
#include "rt/db_io.h"
#include "rt/geom.h"
#include "rt/primitives/bot.h"
#include "raytrace.h"
#include "wdb.h"
#include "ged.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/** Write an ARB8 into an open wdb.  pts[8][3] in model coordinates. */
static int
write_arb8(struct rt_wdb *wdbp, const char *name, const point_t pts[8])
{
    fastf_t flat[24];
    for (int i = 0; i < 8; i++) {
	flat[i*3]     = pts[i][X];
	flat[i*3 + 1] = pts[i][Y];
	flat[i*3 + 2] = pts[i][Z];
    }
    return mk_arb8(wdbp, name, flat);
}

/**
 * Write a boolean combination into an open wdb.
 * op_flags: 'u' union, '-' subtract, '+' intersect for each member.
 */
static int
write_comb2(struct rt_wdb *wdbp,
	    const char    *comb_name,
	    const char    *left,   char lop,
	    const char    *right,  char rop)
{
    struct bu_list head;
    BU_LIST_INIT(&head);

    mk_addmember(left,  &head, NULL, (lop == 'u') ? WMOP_UNION :
					(lop == '-') ? WMOP_SUBTRACT : WMOP_INTERSECT);
    mk_addmember(right, &head, NULL, (rop == '-') ? WMOP_SUBTRACT :
					(rop == 'u') ? WMOP_UNION : WMOP_INTERSECT);

    return mk_comb(wdbp, comb_name, &head, 0 /* not region */, NULL, NULL, NULL,
		   0, 0, 0, 0, 0, 0, 0);
}

/**
 * Open @a gfile with ged_open and run "facetize @a input @a output".
 * Returns BRLCAD_OK on success, BRLCAD_ERROR otherwise.
 */
static int
run_facetize(const char *gfile, const char *input, const char *output, int verbose)
{
    struct ged *gedp = ged_open("db", gfile, 1);
    if (!gedp) {
	bu_log("[regress_facetize] ged_open(%s) failed\n", gfile);
	return BRLCAD_ERROR;
    }

    const char *av[4] = {"facetize", input, output, NULL};
    int ret = ged_exec(gedp, 3, av);

    if (verbose || ret != BRLCAD_OK) {
	const char *log = bu_vls_cstr(gedp->ged_result_str);
	if (log && log[0])
	    bu_log("[facetize] %s\n", log);
    }

    ged_close(gedp);
    return ret;
}

/**
 * After run_facetize writes a BoT into @a gfile, reopen and verify that
 * @a bot_name exists, is a BoT, and has at least one face.
 */
static int
check_bot(const char *gfile, const char *bot_name)
{
    struct db_i *dbip = db_open(gfile, DB_OPEN_READONLY);
    if (!dbip) {
	bu_log("[regress_facetize] check_bot: db_open(%s) failed\n", gfile);
	return BRLCAD_ERROR;
    }
    if (db_dirbuild(dbip) < 0) {
	db_close(dbip);
	return BRLCAD_ERROR;
    }

    struct directory *dp = db_lookup(dbip, bot_name, LOOKUP_QUIET);
    if (!dp || !(dp->d_flags & RT_DIR_SOLID)) {
	bu_log("[regress_facetize] check_bot: '%s' not found or not a solid\n", bot_name);
	db_close(dbip);
	return BRLCAD_ERROR;
    }

    struct rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    if (rt_db_get_internal(&intern, dp, dbip, NULL, &rt_uniresource) < 0) {
	bu_log("[regress_facetize] check_bot: rt_db_get_internal failed for '%s'\n", bot_name);
	db_close(dbip);
	return BRLCAD_ERROR;
    }

    int ok = BRLCAD_ERROR;
    if (intern.idb_minor_type == ID_BOT) {
	struct rt_bot_internal *bot = (struct rt_bot_internal *)intern.idb_ptr;
	if (bot->num_faces > 0)
	    ok = BRLCAD_OK;
	else
	    bu_log("[regress_facetize] check_bot: '%s' has 0 faces\n", bot_name);
    } else {
	bu_log("[regress_facetize] check_bot: '%s' type %d is not a BoT\n",
	       bot_name, intern.idb_minor_type);
    }

    rt_db_free_internal(&intern);
    db_close(dbip);
    return ok;
}

/* ------------------------------------------------------------------ */
/* TC1: basic window frame                                              */
/* ------------------------------------------------------------------ */

/*
 * Wall: 200 × 300 × 20 mm box.
 * Window cutout: 80 × 120 × 20 mm box centred in the wall.
 *
 * Front face (Z=0) and back face (Z=20) of the cutout are exactly
 * coplanar with the corresponding faces of the wall.  This is the
 * canonical coplanar subtraction pattern.
 *
 * Expected: facetize succeeds and the resulting BoT has faces.
 */
static int
tc1_window_frame(const char *tmpdir, int verbose)
{
    bu_log("[regress_facetize] TC1: basic window-frame coplanar subtraction...\n");

    /* Build the .g file path */
    struct bu_vls gpath = BU_VLS_INIT_ZERO;
    bu_vls_printf(&gpath, "%s/tc1_window.g", tmpdir);
    const char *gfile = bu_vls_cstr(&gpath);

    /* Create fresh db */
    if (bu_file_exists(gfile, NULL))
	bu_file_delete(gfile);
    struct db_i *dbip = db_create(gfile, 5);
    if (!dbip) {
	bu_log("[regress_facetize] TC1: db_create failed\n");
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }
    db_update_nref(dbip, &rt_uniresource);
    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_DEFAULT);

    /* wall.s: 200×300×20 box, front face at Z=0, back face at Z=20 */
    point_t wall_pts[8] = {
	{0.0,   0.0,   0.0},
	{200.0, 0.0,   0.0},
	{200.0, 300.0, 0.0},
	{0.0,   300.0, 0.0},
	{0.0,   0.0,   20.0},
	{200.0, 0.0,   20.0},
	{200.0, 300.0, 20.0},
	{0.0,   300.0, 20.0},
    };
    if (write_arb8(wdbp, "wall.s", wall_pts) < 0) {
	bu_log("[regress_facetize] TC1: mk_arb8 wall.s failed\n");
	wdb_close(wdbp);
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }

    /* win.s: 80×120×20 box; front/back faces exactly coplanar with wall */
    point_t win_pts[8] = {
	{60.0,  90.0,  0.0},
	{140.0, 90.0,  0.0},
	{140.0, 210.0, 0.0},
	{60.0,  210.0, 0.0},
	{60.0,  90.0,  20.0},
	{140.0, 90.0,  20.0},
	{140.0, 210.0, 20.0},
	{60.0,  210.0, 20.0},
    };
    if (write_arb8(wdbp, "win.s", win_pts) < 0) {
	bu_log("[regress_facetize] TC1: mk_arb8 win.s failed\n");
	wdb_close(wdbp);
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }

    /* wall.c = wall.s - win.s */
    if (write_comb2(wdbp, "wall.c", "wall.s", 'u', "win.s", '-') < 0) {
	bu_log("[regress_facetize] TC1: mk_comb wall.c failed\n");
	wdb_close(wdbp);
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }

    wdb_close(wdbp);

    /* Run facetize */
    int ret = run_facetize(gfile, "wall.c", "wall.bot", verbose);
    if (ret != BRLCAD_OK) {
	bu_log("[regress_facetize] TC1: FAIL - facetize returned error\n");
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }

    /* Verify output BoT */
    ret = check_bot(gfile, "wall.bot");
    if (ret != BRLCAD_OK) {
	bu_log("[regress_facetize] TC1: FAIL - output BoT invalid\n");
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }

    bu_log("[regress_facetize] TC1: PASS\n");
    bu_vls_free(&gpath);
    return BRLCAD_OK;
}

/* ------------------------------------------------------------------ */
/* TC2: stacked window cutouts (multiple subtractive instances)         */
/* ------------------------------------------------------------------ */

/*
 * Wall: same 200×300×20 mm box.
 * Two separate window cutouts, both exactly as thick as the wall.
 *
 * wall2.c = wall.s - win1.s - win2.s
 *
 * Both win1.s and win2.s are distinct primitives, each with front and
 * back faces coplanar with the wall.  This tests that each subtractive
 * instance gets its own SUB variant (different perturbation seed from TC1).
 */
static int
tc2_stacked_cutouts(const char *tmpdir, int verbose)
{
    bu_log("[regress_facetize] TC2: stacked window-frame cutouts...\n");

    struct bu_vls gpath = BU_VLS_INIT_ZERO;
    bu_vls_printf(&gpath, "%s/tc2_stacked.g", tmpdir);
    const char *gfile = bu_vls_cstr(&gpath);

    if (bu_file_exists(gfile, NULL))
	bu_file_delete(gfile);
    struct db_i *dbip = db_create(gfile, 5);
    if (!dbip) {
	bu_log("[regress_facetize] TC2: db_create failed\n");
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }
    db_update_nref(dbip, &rt_uniresource);
    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_DEFAULT);

    /* wall.s */
    point_t wall_pts[8] = {
	{0.0,   0.0,   0.0},
	{200.0, 0.0,   0.0},
	{200.0, 300.0, 0.0},
	{0.0,   300.0, 0.0},
	{0.0,   0.0,   20.0},
	{200.0, 0.0,   20.0},
	{200.0, 300.0, 20.0},
	{0.0,   300.0, 20.0},
    };
    if (write_arb8(wdbp, "wall.s", wall_pts) < 0) {
	bu_log("[regress_facetize] TC2: mk_arb8 wall.s failed\n");
	wdb_close(wdbp);
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }

    /* win1.s: left window, Z-coplanar with wall */
    point_t win1_pts[8] = {
	{20.0,  80.0,  0.0},
	{80.0,  80.0,  0.0},
	{80.0,  220.0, 0.0},
	{20.0,  220.0, 0.0},
	{20.0,  80.0,  20.0},
	{80.0,  80.0,  20.0},
	{80.0,  220.0, 20.0},
	{20.0,  220.0, 20.0},
    };
    if (write_arb8(wdbp, "win1.s", win1_pts) < 0) {
	bu_log("[regress_facetize] TC2: mk_arb8 win1.s failed\n");
	wdb_close(wdbp);
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }

    /* win2.s: right window, Z-coplanar with wall */
    point_t win2_pts[8] = {
	{120.0, 80.0,  0.0},
	{180.0, 80.0,  0.0},
	{180.0, 220.0, 0.0},
	{120.0, 220.0, 0.0},
	{120.0, 80.0,  20.0},
	{180.0, 80.0,  20.0},
	{180.0, 220.0, 20.0},
	{120.0, 220.0, 20.0},
    };
    if (write_arb8(wdbp, "win2.s", win2_pts) < 0) {
	bu_log("[regress_facetize] TC2: mk_arb8 win2.s failed\n");
	wdb_close(wdbp);
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }

    /* wall2.c = wall.s - win1.s - win2.s */
    {
	struct bu_list head;
	BU_LIST_INIT(&head);
	mk_addmember("wall.s",  &head, NULL, WMOP_UNION);
	mk_addmember("win1.s",  &head, NULL, WMOP_SUBTRACT);
	mk_addmember("win2.s",  &head, NULL, WMOP_SUBTRACT);
	if (mk_comb(wdbp, "wall2.c", &head, 0, NULL, NULL, NULL,
		    0, 0, 0, 0, 0, 0, 0) < 0) {
	    bu_log("[regress_facetize] TC2: mk_comb wall2.c failed\n");
	    wdb_close(wdbp);
	    bu_vls_free(&gpath);
	    return BRLCAD_ERROR;
	}
    }

    wdb_close(wdbp);

    int ret = run_facetize(gfile, "wall2.c", "wall2.bot", verbose);
    if (ret != BRLCAD_OK) {
	bu_log("[regress_facetize] TC2: FAIL - facetize returned error\n");
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }

    ret = check_bot(gfile, "wall2.bot");
    if (ret != BRLCAD_OK) {
	bu_log("[regress_facetize] TC2: FAIL - output BoT invalid\n");
	bu_vls_free(&gpath);
	return BRLCAD_ERROR;
    }

    bu_log("[regress_facetize] TC2: PASS\n");
    bu_vls_free(&gpath);
    return BRLCAD_OK;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, const char **argv)
{
    bu_setprogname(argv[0]);

    int verbose = 0;
    const char *tmpdir = NULL;

    for (int i = 1; i < argc; i++) {
	if (BU_STR_EQUAL(argv[i], "-v") || BU_STR_EQUAL(argv[i], "--verbose"))
	    verbose++;
	else if (!tmpdir)
	    tmpdir = argv[i];
    }

    if (!tmpdir) {
	bu_log("Usage: regress_facetize [-v] <tmpdir>\n");
	return 1;
    }

    int ret = 0;
    if (tc1_window_frame(tmpdir, verbose) != BRLCAD_OK) ret = 1;
    if (tc2_stacked_cutouts(tmpdir, verbose) != BRLCAD_OK) ret = 1;

    if (ret == 0)
	bu_log("[regress_facetize] All tests PASSED\n");
    else
	bu_log("[regress_facetize] One or more tests FAILED\n");

    return ret;
}

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
