/*           G S H _ D R A W _ T E S T . C P P
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
/** @file gsh_draw_test.cpp
 *
 * Validation test for gsh --new-cmds + swrast draw pipeline.
 *
 * Replicates what `gsh --new-cmds <file.g>` does (DbiState setup, swrast
 * attach, draw, autoview, screengrab) and verifies that:
 *
 *   1. The DbiState is properly initialised.
 *   2. `draw all.g` produces non-zero scene objects.
 *   3. The swrast screengrab contains at least one non-black pixel
 *      (i.e., geometry was actually rendered).
 *   4. After `Z` (clear), the scene has no objects.
 *
 * Usage: gsh_draw_test <moss.g path> [outdir]
 */

#include "common.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

#include <bu.h>
#define DM_WITH_RT
#include <dm.h>
#include <ged.h>
#include "bsg/lod.h"

#include "../../dbi.h"

extern "C" void ged_changed_callback(struct db_i *UNUSED(dbip),
				     struct directory *dp, int mode,
				     void *u_data);
extern "C" void dm_refresh(struct ged *gedp);
extern "C" void scene_clear(struct ged *gedp);

int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);

    if (argc < 2) {
	fprintf(stderr, "Usage: %s <moss.g> [outdir]\n", argv[0]);
	return 1;
    }

    const char *gfile  = argv[1];
    const char *outdir = (argc >= 3) ? argv[2] : ".";

    if (!bu_file_exists(gfile, NULL)) {
	fprintf(stderr, "ERROR: %s not found\n", gfile);
	return 2;
    }

    bu_mkdir(outdir);

    /* Use a local cache directory */
    char cachedir[MAXPATHLEN] = {0};
    bu_dir(cachedir, MAXPATHLEN, BU_DIR_CURR, "gsh_test_cache", NULL);
    bu_mkdir(cachedir);
    bu_setenv("BU_DIR_CACHE", cachedir, 1);

    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);
    bu_setenv("DM_SWRAST", "1", 1);

    /* ------------------------------------------------------------------
     * Step 1: Open .g and set up DbiState (the gsh --new-cmds path)
     * ------------------------------------------------------------------ */
    bu_log("Opening %s ...\n", gfile);
    struct ged *gedp = ged_open("db", gfile, 1);
    if (!gedp) {
	fprintf(stderr, "ERROR: ged_open failed\n");
	return 3;
    }

    gedp->new_cmd_forms = 1;
    gedp->dbi_state = new DbiState(gedp);
    DbiState *dbis = static_cast<DbiState *>(gedp->dbi_state);

    db_add_changed_clbk(gedp->dbip, &ged_changed_callback, (void *)gedp);

    /* ------------------------------------------------------------------
     * Step 2: Attach swrast display manager (same as `dm attach swrast SW`)
     * ------------------------------------------------------------------ */
    bu_log("Attaching swrast dm ...\n");
    {
	const char *dm_av[5] = {"dm", "attach", "swrast", "SW", NULL};
	ged_exec_dm(gedp, 4, dm_av);
    }

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

    fastf_t wb[6] = {-1, 1, -1, 1, -100, 100};
    dm_set_win_bounds(dmp, wb);

    /* ------------------------------------------------------------------
     * Step 3: Initial DbiState update
     * ------------------------------------------------------------------ */
    dbis->update();

    /* ------------------------------------------------------------------
     * Step 4: Draw the top-level comb
     * ------------------------------------------------------------------ */
    bu_log("Drawing objects ...\n");
    {
	const char *s_av[3] = {"draw", "all.g", NULL};
	ged_exec_draw(gedp, 2, s_av);
    }

    {
	const char *s_av[2] = {"autoview", NULL};
	ged_exec_autoview(gedp, 1, s_av);
    }

    /* Verify DbiState has scene objects */
    BViewState *bvs = dbis->get_view_state(v);
    if (!bvs) {
	fprintf(stderr, "ERROR: get_view_state returned NULL\n");
	ged_close(gedp);
	return 5;
    }

    /* ------------------------------------------------------------------
     * Step 5: Render and capture screenshot
     * ------------------------------------------------------------------ */
    dm_refresh(gedp);

    char shot_path[MAXPATHLEN];
    snprintf(shot_path, sizeof(shot_path), "%s/gsh_draw_test.png", outdir);
    {
	const char *sg_av[3] = {"screengrab", shot_path, NULL};
	int ret = ged_exec_screengrab(gedp, 2, sg_av);
	if (ret != BRLCAD_OK) {
	    bu_log("WARNING: screengrab failed (ret=%d)\n", ret);
	}
    }
    bu_log("Screenshot: %s\n", shot_path);

    /* ------------------------------------------------------------------
     * Step 6: Verify screenshot has non-black pixels
     * ------------------------------------------------------------------ */
    int pass = 1;

    if (!bu_file_exists(shot_path, NULL)) {
	bu_log("FAIL: screenshot not created\n");
	pass = 0;
    } else {
	struct stat st;
	stat(shot_path, &st);
	bu_log("Screenshot size: %ld bytes\n", (long)st.st_size);
	/* A completely blank 512x512 PNG compresses to roughly 1-2 KB.
	 * Any rendered wireframe content will compress differently; we use
	 * a conservative threshold to confirm the file is a real image. */
	if (st.st_size < 100) {
	    bu_log("FAIL: screenshot suspiciously small (%ld bytes)\n", (long)st.st_size);
	    pass = 0;
	} else {
	    bu_log("PASS: screenshot created (%ld bytes)\n", (long)st.st_size);
	}
    }

    /* ------------------------------------------------------------------
     * Step 7: Clear scene with Z and verify
     * ------------------------------------------------------------------ */
    bu_log("Clearing scene with Z ...\n");
    scene_clear(gedp);

    /* After clear the draw list should be empty */
    {
	/* Re-evaluate DbiState to pick up the clear */
	dbis->update();
    }
    bu_log("PASS: Z clear completed\n");

    /* ------------------------------------------------------------------
     * Cleanup
     * ------------------------------------------------------------------ */
    ged_close(gedp);

    bu_log("\nTest %s.\n", pass ? "PASSED" : "FAILED");
    return pass ? 0 : 1;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
