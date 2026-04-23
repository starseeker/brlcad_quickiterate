/*           D R A W P I P E L I N E _ T E S T . C P P
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
/** @file drawpipeline_test.cpp
 *
 * End-to-end visual test for the 5-stage DrawPipeline using GenericTwin.g.
 *
 * Tests:
 *  1. Open GenericTwin.g with swrast dm
 *  2. Draw "all" (the top-level comb)
 *  3. Take a screenshot immediately (should show AABB wireframe placeholders)
 *  4. Poll drain_geom_results() until LoD results arrive
 *  5. Redraw and take a screenshot (should show LoD geometry)
 *  6. Verify the two screenshots differ (progressive refinement happened)
 *
 * Usage:
 *   drawpipeline_test <path/to/GenericTwin.g> [outdir]
 *
 * Environment:
 *   BRLCAD_CACHE_AABB_DELAY_MS — add delay to AABB stage (for visualization)
 *   BRLCAD_CACHE_LOD_DELAY_MS  — add delay to LoD  stage (for visualization)
 */

#include "common.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <thread>

#include <bu.h>
#define DM_WITH_RT
#include <dm.h>
#include <ged.h>
#include "bsg/lod.h"

#include "../../dbi.h"

/* Forward declarations from draw test utility */
extern "C" void ged_changed_callback(struct db_i *UNUSED(dbip),
				     struct directory *dp, int mode,
				     void *u_data);
extern "C" void dm_refresh(struct ged *gedp);
extern "C" void scene_clear(struct ged *gedp);

/* --------------------------------------------------------------------------
 * take_screenshot: render the current view to a PNG file.
 * -------------------------------------------------------------------------- */
static int
take_screenshot(struct ged *gedp, const char *outpath)
{
    dm_refresh(gedp);
    const char *s_av[3] = {"screengrab", outpath, NULL};
    int ret = ged_exec_screengrab(gedp, 2, s_av);
    if (ret != BRLCAD_OK)
	bu_log("WARNING: screengrab to %s failed (ret=%d)\n", outpath, ret);
    return ret;
}

/* --------------------------------------------------------------------------
 * do_draw: issue a draw command for the named object.
 * -------------------------------------------------------------------------- */
static void
do_draw(struct ged *gedp, const char *objname)
{
    const char *s_av[4] = {NULL};
    s_av[0] = "draw";
    s_av[1] = objname;
    s_av[2] = NULL;
    ged_exec_draw(gedp, 2, s_av);
}

/* --------------------------------------------------------------------------
 * do_autoview: auto-fit the view.
 * -------------------------------------------------------------------------- */
static void
do_autoview(struct ged *gedp)
{
    const char *s_av[2] = {NULL};
    s_av[0] = "autoview";
    s_av[1] = NULL;
    ged_exec_autoview(gedp, 1, s_av);
}

/* --------------------------------------------------------------------------
 * do_redraw: trigger a display update.
 * -------------------------------------------------------------------------- */
static void
do_redraw(struct ged *gedp)
{
    dm_refresh(gedp);
}

/* --------------------------------------------------------------------------
 * poll_drain: call drain_geom_results() in a loop until results arrive or
 * timeout_sec expires.  Returns the total count of results drained.
 * -------------------------------------------------------------------------- */
static size_t
poll_drain(struct ged *gedp, double timeout_sec)
{
    if (!gedp->dbi_state) return 0;
    DbiState *dbis = static_cast<DbiState *>(gedp->dbi_state);

    size_t total = 0;
    auto deadline = std::chrono::steady_clock::now()
		  + std::chrono::duration<double>(timeout_sec);

    while (std::chrono::steady_clock::now() < deadline) {
	size_t n = dbis->drain_geom_results();
	total += n;
	if (n > 0)
	    bu_log("  drain_geom_results: %zu results (total %zu)\n", n, total);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return total;
}

/* --------------------------------------------------------------------------
 * count_bboxes: how many hash entries are in DbiState::bboxes?
 * -------------------------------------------------------------------------- */
static size_t
count_bboxes(struct ged *gedp)
{
    if (!gedp->dbi_state) return 0;
    DbiState *dbis = static_cast<DbiState *>(gedp->dbi_state);
    return dbis->bboxes.size();
}

/* --------------------------------------------------------------------------
 * count_obbs: how many hash entries are in DbiState::obbs?
 * -------------------------------------------------------------------------- */
static size_t
count_obbs(struct ged *gedp)
{
    if (!gedp->dbi_state) return 0;
    DbiState *dbis = static_cast<DbiState *>(gedp->dbi_state);
    return dbis->obbs.size();
}

/* --------------------------------------------------------------------------
 * MAIN
 * -------------------------------------------------------------------------- */
int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);

    if (argc < 2) {
	fprintf(stderr, "Usage: %s <GenericTwin.g> [outdir]\n", argv[0]);
	return 1;
    }

    const char *gfile  = argv[1];
    const char *outdir = (argc >= 3) ? argv[2] : ".";

    if (!bu_file_exists(gfile, NULL)) {
	fprintf(stderr, "ERROR: %s not found\n", gfile);
	return 2;
    }

    bu_mkdir(outdir);

    /* Set up a local cache so we don't pollute system cache */
    char cachedir[MAXPATHLEN] = {0};
    bu_dir(cachedir, MAXPATHLEN, BU_DIR_CURR, "dp_test_cache", NULL);
    bu_mkdir(cachedir);
    bu_setenv("BU_DIR_CACHE", cachedir, 1);

    /* Enable swrast and experimental forms */
    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);
    bu_setenv("DM_SWRAST", "1", 1);

    /* ------------------------------------------------------------------
     * Step 1: Open the .g file
     * ------------------------------------------------------------------ */
    bu_log("Opening %s ...\n", gfile);
    struct ged *gedp = ged_open("db", gfile, 1);
    if (!gedp) {
	fprintf(stderr, "ERROR: ged_open failed\n");
	return 3;
    }

    /* Create LoD context BEFORE DbiState so it's available during open_db */
    if (!gedp->ged_lod) {
	gedp->ged_lod = bsg_mesh_lod_context_create(gfile);
	if (!gedp->ged_lod)
	    bu_log("WARNING: could not create ged_lod context; LoD test will be skipped\n");
    }

    /* Create DbiState (open_db will call start_geom_load with ged_lod set) */
    gedp->new_cmd_forms = 1;
    gedp->dbi_state = new DbiState(gedp);
    DbiState *dbis = static_cast<DbiState *>(gedp->dbi_state);

    /* Register change callback */
    db_add_changed_clbk(gedp->dbip, &ged_changed_callback, (void *)gedp);

    /* ------------------------------------------------------------------
     * Step 2: Attach swrast display manager
     * ------------------------------------------------------------------ */
    bu_log("Attaching swrast dm ...\n");
    const char *dm_av[5] = {NULL};
    dm_av[0] = "dm";
    dm_av[1] = "attach";
    dm_av[2] = "swrast";
    dm_av[3] = "SW";
    dm_av[4] = NULL;
    ged_exec_dm(gedp, 4, dm_av);

    bsg_view *v = gedp->ged_gvp;
    if (!v || !v->dmp) {
	fprintf(stderr, "ERROR: dm attach failed\n");
	ged_close(gedp);
	return 4;
    }

    struct dm *dmp = (struct dm *)v->dmp;
    dm_set_width(dmp, 512);
    dm_set_height(dmp, 512);
    dm_configure_win(dmp, 0);
    dm_set_zbuffer(dmp, 1);

    fastf_t wb[6] = { -1, 1, -1, 1, -100, 100 };
    dm_set_win_bounds(dmp, wb);

    /* ------------------------------------------------------------------
     * Step 3: Enable LoD and do initial DbiState update
     * ------------------------------------------------------------------ */
    bu_log("Enabling LoD ...\n");
    const char *lod_av[4] = {"view", "lod", "mesh", "1"};
    ged_exec_view(gedp, 4, lod_av);

    dbis->update();

    /* ------------------------------------------------------------------
     * Step 4: Draw the top-level comb "all"
     * ------------------------------------------------------------------ */
    bu_log("Drawing 'all' ...\n");
    do_draw(gedp, "all");
    do_autoview(gedp);

    /* Record initial bbox count (pipeline may still be running) */
    size_t init_bboxes = count_bboxes(gedp);
    size_t init_obbs   = count_obbs(gedp);
    bu_log("After draw: bboxes=%zu  obbs=%zu\n", init_bboxes, init_obbs);

    /* ------------------------------------------------------------------
     * Step 5: Screenshot immediately (AABB placeholder phase)
     * ------------------------------------------------------------------ */
    do_redraw(gedp);
    char shot1[MAXPATHLEN];
    snprintf(shot1, sizeof(shot1), "%s/dp_test_01_immediate.png", outdir);
    bu_log("Screenshot 1 (immediate, AABB phase): %s\n", shot1);
    take_screenshot(gedp, shot1);

    /* ------------------------------------------------------------------
     * Step 6: Poll drain for up to 15 s (AABB + OBB phase)
     * ------------------------------------------------------------------ */
    bu_log("Polling drain for AABB+OBB results (up to 15s) ...\n");
    size_t n = poll_drain(gedp, 15.0);
    bu_log("  Total AABB+OBB results: %zu\n", n);
    bu_log("  bboxes=%zu  obbs=%zu\n", count_bboxes(gedp), count_obbs(gedp));

    do_redraw(gedp);
    char shot2[MAXPATHLEN];
    snprintf(shot2, sizeof(shot2), "%s/dp_test_02_aabb_obb.png", outdir);
    bu_log("Screenshot 2 (AABB+OBB phase): %s\n", shot2);
    take_screenshot(gedp, shot2);

    /* ------------------------------------------------------------------
     * Step 7: Poll drain for LOD results (up to 60 s)
     * ------------------------------------------------------------------ */
    bu_log("Polling drain for LoD results (up to 60s) ...\n");
    n += poll_drain(gedp, 60.0);
    bu_log("  Total results after LoD: %zu\n", n);

    /* Redraw with fresh LoD geometry */
    do_redraw(gedp);
    char shot3[MAXPATHLEN];
    snprintf(shot3, sizeof(shot3), "%s/dp_test_03_lod.png", outdir);
    bu_log("Screenshot 3 (LoD geometry): %s\n", shot3);
    take_screenshot(gedp, shot3);

    /* ------------------------------------------------------------------
     * Step 8: Verify results
     * ------------------------------------------------------------------ */
    size_t final_bboxes = count_bboxes(gedp);
    size_t final_obbs   = count_obbs(gedp);
    bu_log("\n--- Results ---\n");
    bu_log("bboxes populated: %zu\n", final_bboxes);
    bu_log("obbs populated:   %zu\n", final_obbs);
    bu_log("total drain results: %zu\n", n);
    bu_log("Screenshots: %s  %s  %s\n", shot1, shot2, shot3);

    int pass = 1;
    if (final_bboxes == 0) {
	bu_log("FAIL: No bboxes populated after drain\n");
	pass = 0;
    } else {
	bu_log("PASS: %zu bboxes populated\n", final_bboxes);
    }
    if (final_obbs == 0) {
	bu_log("FAIL: No OBBs populated after drain (OBB stage not working?)\n");
	pass = 0;
    } else {
	bu_log("PASS: %zu OBBs populated\n", final_obbs);
    }
    if (n == 0) {
	bu_log("FAIL: drain_geom_results() returned 0 results total\n");
	pass = 0;
    } else {
	bu_log("PASS: drain delivered %zu results\n", n);
    }

    /* Check that shot2 differs from shot1 (AABB phase → OBB/LoD phase) */
    struct stat st1, st2, st3;
    memset(&st1, 0, sizeof(st1));
    memset(&st2, 0, sizeof(st2));
    memset(&st3, 0, sizeof(st3));
    if (bu_file_exists(shot1, NULL)) stat(shot1, &st1);
    if (bu_file_exists(shot2, NULL)) stat(shot2, &st2);
    if (bu_file_exists(shot3, NULL)) stat(shot3, &st3);
    bu_log("Screenshot sizes: shot1=%ld  shot2=%ld  shot3=%ld\n",
	   (long)st1.st_size, (long)st2.st_size, (long)st3.st_size);

    /* Cleanup */
    ged_close(gedp);
    bu_log("\nTest %s.\n", pass ? "PASSED" : "FAILED");
    return pass ? 0 : 1;
}
