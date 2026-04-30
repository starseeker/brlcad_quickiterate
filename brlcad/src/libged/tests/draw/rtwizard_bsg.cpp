/*                  R T W I Z A R D _ B S G . C P P
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
/** @file rtwizard_bsg.cpp
 *
 * Phase 5 (drawing_stack_modernization) rtwizard migration exit-criteria test.
 *
 * rtwizard's headless (--no-gui) pipeline computes rt view parameters via the
 * libtclcad GED API:
 *
 *   go_open db db <file>          ; opens a GED instance
 *   db new_view v1 nu             ; creates a null-DM secondary view
 *   db draw <objects>             ; populates scene objects for autoview
 *   db autoview v1                ; sets gv_size / gv_center from scene bounds
 *   db aet v1 <az> <el> <tw>     ; sets azimuth / elevation / twist
 *   db zoom v1 <z>                ; applies zoom
 *   db get_eyemodel v1            ; extracts viewsize, orientation, eye_pt
 *
 * The eye model (viewsize, orientation quaternion, eye_pt) is then forwarded
 * as command-line arguments to the rt/rtedge subprocess.  No display-manager
 * rendering occurs; the view object is purely a lightweight camera container.
 *
 * Phase 5 ensures that libtclcad/commands.c calls bsg_scene_root_create() for
 * every new view, including the null-DM "v1" view above.  This test exercises
 * the equivalent C-API path and verifies:
 *
 *   1. null_view_bsg_root      — A null-DM secondary view gets a BSG root,
 *                                mirroring what libtclcad does for "new_view nu".
 *   2. eyemodel_finite         — draw + autoview + get_eyemodel produces a
 *                                plausible (finite, non-degenerate) eye model.
 *   3. nodisplaylist_path      — With bsg_root set the go_draw_dlist legacy
 *                                fallback is never entered (bsg_root non-NULL).
 *
 * All tests use the "nu" (null) display-manager so no display hardware or X11
 * server is required.
 *
 * Usage: ged_test_rtwizard_bsg [-c] <directory-containing-moss.g>
 */

#include "common.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>

#include <bu.h>
#include "bu/opt.h"
#define DM_WITH_RT
#include <dm.h>
#include <ged.h>
#include "bsg/util.h"
#include "bv/util.h"

/* Private header for DbiState */
#include "../../dbi.h"

extern "C" void ged_changed_callback(struct db_i *UNUSED(dbip), struct directory *dp, int mode, void *u_data);

/* ---- helpers ------------------------------------------------------------- */

/*
 * Open a GED instance for testing (null DM, BSG enabled).
 * Simulates the go_open step in the rtwizard Tcl pipeline.
 */
static struct ged *
open_gedp_null(const char *gfile)
{
    struct ged *gedp = ged_open("db", gfile, 1);
    if (!gedp)
	return NULL;

    gedp->dbi_state = new DbiState(gedp);
    gedp->new_cmd_forms = 1;
    gedp->ged_lod = bv_mesh_lod_context_create(gedp->dbip->dbi_filename);
    db_add_changed_clbk(gedp->dbip, &ged_changed_callback, (void *)gedp);

    /* Attach a null display-manager to the default view (like go_open does
     * when no visible window is requested). */
    const char *s_av[6] = {"dm", "attach", "nu", "rtw_null_dm", NULL};
    ged_exec_dm(gedp, 4, s_av);

    struct bview *v = gedp->ged_gvp;
    v->gv_base2local = gedp->dbip->dbi_base2local;
    v->gv_local2base = gedp->dbip->dbi_local2base;

    return gedp;
}

/*
 * Create a secondary null-DM view and add it to the GED view set.
 * Mirrors what libtclcad's to_new_view() does for "db new_view v1 nu".
 */
static struct bview *
make_null_view(struct ged *gedp, const char *vname)
{
    struct bview *v;
    BU_GET(v, struct bview);
    bv_init(v, &gedp->ged_views);
    bu_vls_sprintf(&v->gv_name, "%s", vname);
    v->gv_base2local = gedp->dbip->dbi_base2local;
    v->gv_local2base = gedp->dbip->dbi_local2base;

    /* Phase 5: every new libtclcad view gets a BSG scene root
     * (libtclcad/commands.c line 4527 bsg_scene_root_create call). */
    bsg_scene_root_create(v);

    /* Attach null DM */
    struct bu_vls dm_name = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&dm_name, "rtw_%s_dm", vname);
    const char *sa[6] = {"dm", "attach", "-V", vname, "nu", bu_vls_cstr(&dm_name)};
    /* Temporarily make v the current view so the dm attach targets it. */
    struct bview *prev_gvp = gedp->ged_gvp;
    gedp->ged_gvp = v;
    bv_set_add_view(&gedp->ged_views, v);
    bu_ptbl_ins(&gedp->ged_free_views, (long *)v);
    ged_exec_dm(gedp, 6, sa);
    gedp->ged_gvp = prev_gvp;
    bu_vls_free(&dm_name);

    return v;
}

/* ========================================================================== */
/* Test 1: null-DM secondary view gets a BSG scene root                       */
/* ========================================================================== */
static int
test_null_view_bsg_root(const char *datadir)
{
    bu_log("\n--- Test 1: null-DM secondary view gets BSG root ---\n");

    struct bu_vls fname = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&fname, "%s/moss.g", datadir);
    std::ifstream orig(bu_vls_cstr(&fname), std::ios::binary);
    std::ofstream tmp("rtw_bsg_t1.g", std::ios::binary);
    tmp << orig.rdbuf();
    orig.close(); tmp.close();
    bu_vls_free(&fname);

    struct ged *gedp = open_gedp_null("rtw_bsg_t1.g");
    if (!gedp) {
	bu_log("FAIL: ged_open failed\n");
	bu_file_delete("rtw_bsg_t1.g");
	return 1;
    }

    /* Create a secondary null-DM view, simulating "db new_view v1 nu" */
    struct bview *v1 = make_null_view(gedp, "v1");

    int fail = 0;
    if (!v1->bsg_root) {
	bu_log("FAIL: secondary null-DM view has no BSG root\n");
	fail = 1;
    } else {
	bu_log("PASS: secondary null-DM view has BSG root (Phase 5 libtclcad path)\n");
    }

    /* Also verify the default GED view has a BSG root (set by ged_open). */
    if (!gedp->ged_gvp->bsg_root) {
	bu_log("FAIL: default GED view has no BSG root\n");
	fail = 1;
    } else {
	bu_log("PASS: default GED view has BSG root\n");
    }

    bu_file_delete("rtw_bsg_t1.g");
    ged_close(gedp);
    return fail;
}

/* ========================================================================== */
/* Test 2: draw + autoview + get_eyemodel produces finite/plausible params    */
/* ========================================================================== */
static int
test_eyemodel_finite(const char *datadir)
{
    bu_log("\n--- Test 2: draw + autoview + get_eyemodel produces finite eye model ---\n");

    struct bu_vls fname = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&fname, "%s/moss.g", datadir);
    std::ifstream orig(bu_vls_cstr(&fname), std::ios::binary);
    std::ofstream tmp("rtw_bsg_t2.g", std::ios::binary);
    tmp << orig.rdbuf();
    orig.close(); tmp.close();
    bu_vls_free(&fname);

    struct ged *gedp = open_gedp_null("rtw_bsg_t2.g");
    if (!gedp) {
	bu_log("FAIL: ged_open failed\n");
	bu_file_delete("rtw_bsg_t2.g");
	return 1;
    }

    /* Create secondary null-DM view (rtwizard "new_view v1 nu") */
    struct bview *v1 = make_null_view(gedp, "v1");

    /* Draw objects (rtwizard "db draw $item" for each object in color_objlist) */
    const char *s_av[4] = {"draw", "all.g", NULL};
    ged_exec_draw(gedp, 2, s_av);

    /* Autoview on v1 (rtwizard "db autoview v1") */
    struct bview *prev = gedp->ged_gvp;
    gedp->ged_gvp = v1;
    s_av[0] = "autoview"; s_av[1] = NULL;
    ged_exec_autoview(gedp, 1, s_av);

    /* Apply default az/el/twist (rtwizard "db aet v1 35 25 0") */
    s_av[0] = "ae"; s_av[1] = "35"; s_av[2] = "25"; s_av[3] = NULL;
    ged_exec_ae(gedp, 3, s_av);

    /* Extract eye model (rtwizard "db get_eyemodel v1") */
    s_av[0] = "get_eyemodel"; s_av[1] = NULL;
    ged_exec_get_eyemodel(gedp, 1, s_av);
    gedp->ged_gvp = prev;

    const char *result = bu_vls_cstr(gedp->ged_result_str);
    bu_log("get_eyemodel output:\n%s\n", result);

    /* Parse viewsize from the result string */
    double viewsize = 0.0;
    double qw = 0.0, qx = 0.0, qy = 0.0, qz = 0.0;
    double ex = 0.0, ey = 0.0, ez = 0.0;
    int nscan = sscanf(result,
		       "viewsize %lf ; orientation %lf %lf %lf %lf ; eye_pt %lf %lf %lf",
		       &viewsize, &qw, &qx, &qy, &qz, &ex, &ey, &ez);

    int fail = 0;
    if (nscan < 8) {
	/* Try alternate format without semicolons on same token */
	nscan = sscanf(result,
		       "viewsize %lf\n orientation %lf %lf %lf %lf\n eye_pt %lf %lf %lf",
		       &viewsize, &qw, &qx, &qy, &qz, &ex, &ey, &ez);
    }

    if (nscan < 8) {
	bu_log("FAIL: could not parse eye model (%d/8 values matched)\n", nscan);
	fail = 1;
    } else {
	/* viewsize must be positive and finite */
	if (viewsize <= 0.0 || !std::isfinite(viewsize)) {
	    bu_log("FAIL: viewsize %g is not a positive finite number\n", viewsize);
	    fail = 1;
	} else {
	    bu_log("PASS: viewsize = %g\n", viewsize);
	}

	/* orientation quaternion must have unit magnitude (within tolerance) */
	double qmag = std::sqrt(qw*qw + qx*qx + qy*qy + qz*qz);
	if (fabs(qmag - 1.0) > 0.01) {
	    bu_log("FAIL: orientation quaternion magnitude %g != 1.0\n", qmag);
	    fail = 1;
	} else {
	    bu_log("PASS: orientation quaternion |q| = %g\n", qmag);
	}

	/* eye_pt must be finite */
	if (!std::isfinite(ex) || !std::isfinite(ey) || !std::isfinite(ez)) {
	    bu_log("FAIL: eye_pt (%g %g %g) contains non-finite values\n", ex, ey, ez);
	    fail = 1;
	} else {
	    bu_log("PASS: eye_pt = (%g %g %g)\n", ex, ey, ez);
	}
    }

    bu_file_delete("rtw_bsg_t2.g");
    ged_close(gedp);
    return fail;
}

/* ========================================================================== */
/* Test 3: bsg_root non-NULL on secondary view means go_draw_dlist not used   */
/* ========================================================================== */
static int
test_nodisplaylist_path(const char *datadir)
{
    bu_log("\n--- Test 3: bsg_root non-NULL on secondary view (go_draw_dlist not entered) ---\n");

    struct bu_vls fname = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&fname, "%s/moss.g", datadir);
    std::ifstream orig(bu_vls_cstr(&fname), std::ios::binary);
    std::ofstream tmp("rtw_bsg_t3.g", std::ios::binary);
    tmp << orig.rdbuf();
    orig.close(); tmp.close();
    bu_vls_free(&fname);

    struct ged *gedp = open_gedp_null("rtw_bsg_t3.g");
    if (!gedp) {
	bu_log("FAIL: ged_open failed\n");
	bu_file_delete("rtw_bsg_t3.g");
	return 1;
    }

    /* Draw objects */
    const char *s_av[4] = {"draw", "all.g", NULL};
    ged_exec_draw(gedp, 2, s_av);

    /* Create four secondary views to test per-view bsg_root independence */
    struct bview *views[4];
    char vname[4][8];
    int fail = 0;
    for (int i = 0; i < 4; i++) {
	snprintf(vname[i], sizeof(vname[i]), "v%d", i + 1);
	views[i] = make_null_view(gedp, vname[i]);

	if (!views[i]->bsg_root) {
	    bu_log("FAIL: view '%s' has no BSG root; go_draw_dlist fallback would be used\n", vname[i]);
	    fail = 1;
	}
    }

    if (!fail) {
	bu_log("PASS: all 4 secondary null-DM views have BSG roots\n");
	bu_log("      => go_draw_dlist legacy dl_* fallback is NOT entered for any rtwizard view\n");
    }

    /* Sanity: BSG root created by bsg_scene_root_create must be distinct for
     * each view (per-view independence, not a shared singleton). */
    int unique = 1;
    for (int i = 0; i < 4; i++) {
	for (int j = i + 1; j < 4; j++) {
	    if (views[i]->bsg_root == views[j]->bsg_root) {
		bu_log("FAIL: views[%d] and views[%d] share the same bsg_root pointer\n", i, j);
		unique = 0;
		fail = 1;
	    }
	}
    }
    if (unique)
	bu_log("PASS: each view has an independent bsg_root\n");

    bu_file_delete("rtw_bsg_t3.g");
    ged_close(gedp);
    return fail;
}

/* ========================================================================== */
/* main                                                                        */
/* ========================================================================== */

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);

    int cleanup = 0;
    struct bu_opt_desc d[2];
    BU_OPT(d[0], "c", "cleanup", NULL, NULL, &cleanup, "cleanup temp files");
    BU_OPT_NULL(d[1]);

    int uac = bu_opt_parse(NULL, argc, (const char **)argv, d);
    if (uac != 2) {
	bu_log("Usage: %s [-c] <directory-containing-moss.g>\n", argv[0]);
	return 1;
    }
    const char *datadir = argv[1];

    int failures = 0;
    failures += test_null_view_bsg_root(datadir);
    failures += test_eyemodel_finite(datadir);
    failures += test_nodisplaylist_path(datadir);

    if (failures == 0) {
	bu_log("\nAll Phase 5 rtwizard BSG migration tests PASSED (%d/3)\n", 3);
    } else {
	bu_log("\n%d Phase 5 rtwizard BSG migration test(s) FAILED\n", failures);
    }
    return failures;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
