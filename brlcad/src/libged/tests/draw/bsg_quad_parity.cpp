/*           B S G _ Q U A D _ P A R I T Y . C P P
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
/** @file bsg_quad_parity.cpp
 *
 * Multi-view quad-pane BSG parity test (Phase 4 extended coverage).
 *
 * The single-view bsg_parity test verifies that bsg_view_traverse produces
 * pixel-identical output to the legacy dl_* walk for one view.  This test
 * extends that verification to four independent views (matching the quad-pane
 * layout used in qged), specifically checking that:
 *
 *   1. Each view's bsg_root is independent (disabling one doesn't affect others).
 *   2. BSG and legacy paths produce identical pixels in every view.
 *   3. Re-enabling BSG after the legacy pass restores the original output.
 *
 * Strategy (for each view V0–V3):
 *   A. Draw all.g, refresh all views, capture frame A[i] (BSG active in all).
 *   B. Null out V_i.bsg_root only, refresh V_i, capture frame B[i] (legacy).
 *   C. Verify that for every OTHER view V_j (j≠i), a fresh grab still matches
 *      A[j] — proving per-view independence.
 *   D. Restore V_i.bsg_root, re-sync, capture C[i].
 *   E. Assert A[i] == B[i] and A[i] == C[i].
 *
 * All rendering uses dm-swrast (off-screen; no display hardware required).
 *
 * Usage: ged_test_bsg_quad_parity [-c] <directory-containing-moss.g>
 */

#include "common.h"

#include <fstream>
#include <unordered_set>

#include <bu.h>
#include "bu/opt.h"
#include <icv.h>
#define DM_WITH_RT
#include <dm.h>
#include <ged.h>

#include "../../dbi.h"
#include "bsg/util.h"

extern "C" void ged_changed_callback(struct db_i *UNUSED(dbip), struct directory *dp, int mode, void *u_data);

/* ---- per-view refresh ---------------------------------------------- */
static void
refresh_view(struct ged *gedp, int vnum)
{
    struct bu_ptbl *views = bv_set_views(&gedp->ged_views);
    struct bview *v = (struct bview *)BU_PTBL_GET(views, vnum);
    if (!v)
	return;

    DbiState *dbis = (DbiState *)gedp->dbi_state;
    BViewState *bvs = dbis->get_view_state(v);
    dbis->update();
    std::unordered_set<struct bview *> uset;
    uset.insert(v);
    bvs->redraw(NULL, uset, 1);

    struct dm *dmp = (struct dm *)v->dmp;
    dm_make_current(dmp);
    unsigned char *bg1, *bg2;
    dm_get_bg(&bg1, &bg2, dmp);
    dm_set_bg(dmp, bg1[0], bg1[1], bg1[2], bg2[0], bg2[1], bg2[2]);
    dm_set_dirty(dmp, 0);
    dm_draw_objs(v, NULL, NULL);
    dm_draw_end(dmp);
}

/* ---- screengrab for view vnum ------------------------------------- */
static int
grab_view(struct ged *gedp, int vnum, const char *fname)
{
    struct bu_ptbl *views = bv_set_views(&gedp->ged_views);
    struct bview *v = (struct bview *)BU_PTBL_GET(views, vnum);
    if (!v)
	return -1;
    struct dm *dmp = (struct dm *)v->dmp;
    const char *s_av[4] = {"screengrab", "-D",
	bu_vls_cstr(dm_get_pathname(dmp)), fname};
    return ged_exec_screengrab(gedp, 4, s_av);
}

/* ---- pixel-exact compare ------------------------------------------ */
static int
images_match(const char *a, const char *b, int adiff_allow)
{
    icv_image_t *ia = icv_read(a, BU_MIME_IMAGE_PNG, 0, 0);
    icv_image_t *ib = icv_read(b, BU_MIME_IMAGE_PNG, 0, 0);
    if (!ia || !ib) {
	if (ia) icv_destroy(ia);
	if (ib) icv_destroy(ib);
	bu_log("images_match: could not read %s or %s\n", a, b);
	return 0;
    }
    int match = 0, off1 = 0, offmany = 0;
    icv_diff(&match, &off1, &offmany, ia, ib);
    icv_destroy(ia);
    icv_destroy(ib);
    int bad = offmany + (off1 > adiff_allow ? off1 - adiff_allow : 0);
    if (bad)
	bu_log("  images differ: %d off-by-many, %d off-by-1 (allowed %d)\n",
	       offmany, off1, adiff_allow);
    return (bad == 0);
}

/* ================================================================== */
int
main(int ac, char *av[])
{
    bu_setprogname(av[0]);

    int soft_fail = 0;
    struct bu_opt_desc d[2];
    BU_OPT(d[0], "c", "continue", "", NULL, &soft_fail, "Continue on failure.");
    BU_OPT_NULL(d[1]);

    int uac = bu_opt_parse(NULL, ac, (const char **)av, d);
    if (uac != 2)
	bu_exit(EXIT_FAILURE,
		"Usage: %s [-c] <directory-containing-moss.g>\n", av[0]);
    const char *datadir = av[1];
    if (!bu_file_directory(datadir))
	bu_exit(EXIT_FAILURE, "ERROR: [%s] is not a directory\n", datadir);

    char lcache[MAXPATHLEN];
    bu_dir(lcache, MAXPATHLEN, BU_DIR_CURR, "bsg_qparity_cache", NULL);
    bu_mkdir(lcache);
    bu_setenv("BU_DIR_CACHE", lcache, 1);
    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);

    struct bu_vls srcpath = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&srcpath, "%s/moss.g", datadir);
    {
	std::ifstream orig(bu_vls_cstr(&srcpath), std::ios::binary);
	std::ofstream tmpg("moss_bsg_quad_tmp.g", std::ios::binary);
	tmpg << orig.rdbuf();
    }
    bu_vls_free(&srcpath);

    /* ---- Open .g and set up 4 independent views ------------------- */
    struct ged *gedp = ged_open("db", "moss_bsg_quad_tmp.g", 1);
    if (!gedp)
	bu_exit(EXIT_FAILURE, "ged_open failed\n");

    gedp->dbi_state = new DbiState(gedp);
    gedp->new_cmd_forms = 1;
    gedp->ged_lod = bv_mesh_lod_context_create(gedp->dbip->dbi_filename);
    bu_setenv("DM_SWRAST", "1", 1);
    db_add_changed_clbk(gedp->dbip, &ged_changed_callback, (void *)gedp);

    /* Remove the default view; we'll add our own four */
    bv_set_rm_view(&gedp->ged_views, NULL);

    /* az/el per view matching the quad test reference */
    const double aet[4][3] = {{35,25,0},{90,0,0},{0,90,0},{0,0,90}};

    const char *s_av[8] = {NULL};
    for (int i = 0; i < 4; i++) {
	struct bview *v;
	BU_GET(v, struct bview);
	if (!i)
	    gedp->ged_gvp = v;
	bv_init(v, &gedp->ged_views);
	bu_vls_sprintf(&v->gv_name, "V%d", i);
	bv_set_add_view(&gedp->ged_views, v);
	bu_ptbl_ins(&gedp->ged_free_views, (long *)v);
	v->independent = 1;

	/* Attach one swrast DM per view */
	struct bu_vls dm_name = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&dm_name, "SW%d", i);
	s_av[0] = "dm"; s_av[1] = "attach"; s_av[2] = "-V"; s_av[3] = bu_vls_cstr(&v->gv_name);
	s_av[4] = "swrast"; s_av[5] = bu_vls_cstr(&dm_name); s_av[6] = NULL;
	ged_exec_dm(gedp, 6, s_av);
	bu_vls_free(&dm_name);

	struct dm *dmp = (struct dm *)v->dmp;
	dm_set_width(dmp, 512);
	dm_set_height(dmp, 512);
	dm_configure_win(dmp, 0);
	dm_set_zbuffer(dmp, 1);
	fastf_t wb[6] = {-1, 1, -1, 1, -100, 100};
	dm_set_win_bounds(dmp, wb);
	dm_set_vp(dmp, &v->gv_scale);
	v->dmp           = dmp;
	v->gv_width      = dm_get_width(dmp);
	v->gv_height     = dm_get_height(dmp);
	v->gv_base2local = gedp->dbip->dbi_base2local;
	v->gv_local2base = gedp->dbip->dbi_local2base;

	/* BSG root must be created for each view (Phase 4) */
	if (!v->bsg_root)
	    bsg_scene_root_create(v);

	/* Set distinct az/el */
	struct bu_vls vname = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&vname, "V%d", i);
	struct bu_vls a_s = BU_VLS_INIT_ZERO, e_s = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&a_s, "%g", aet[i][0]);
	bu_vls_sprintf(&e_s, "%g", aet[i][1]);
	s_av[0] = "view"; s_av[1] = "-V"; s_av[2] = bu_vls_cstr(&vname);
	s_av[3] = "aet"; s_av[4] = bu_vls_cstr(&a_s); s_av[5] = bu_vls_cstr(&e_s);
	s_av[6] = "0"; s_av[7] = NULL;
	ged_exec_view(gedp, 7, s_av);
	bu_vls_free(&vname);
	bu_vls_free(&a_s);
	bu_vls_free(&e_s);
    }

    /* Draw all.g into the shared view pool */
    s_av[0] = "draw"; s_av[1] = "-m0"; s_av[2] = "all.g"; s_av[3] = NULL;
    ged_exec_draw(gedp, 3, s_av);

    /* Force per-view autoview */
    struct bu_ptbl *views = bv_set_views(&gedp->ged_views);
    for (int i = 0; i < 4; i++) {
	struct bview *v = (struct bview *)BU_PTBL_GET(views, i);
	gedp->ged_gvp = v;
	s_av[0] = "autoview"; s_av[1] = NULL;
	ged_exec_autoview(gedp, 1, s_av);
    }

    /* ---- Capture baseline images with BSG active in every view ----- */
    char fname_A[4][64], fname_B[4][64], fname_C[4][64];
    for (int i = 0; i < 4; i++) {
	refresh_view(gedp, i);
	snprintf(fname_A[i], 64, "bsg_qparity_A%d.png", i);
	grab_view(gedp, i, fname_A[i]);
    }
    bu_log("Captured baseline images A[0..3] (all BSG active)\n");

    /* ---- Per-view BSG toggle --------------------------------------- */
    /* Allow a small tolerance — same as single-view bsg_parity */
    const int ADIFF = 20;
    int failures = 0;

    for (int i = 0; i < 4; i++) {
	struct bview *vi = (struct bview *)BU_PTBL_GET(views, i);

	/* Save and disable bsg_root on view i only */
	void *saved_root = vi->bsg_root;
	vi->bsg_root = NULL;

	refresh_view(gedp, i);
	snprintf(fname_B[i], 64, "bsg_qparity_B%d.png", i);
	grab_view(gedp, i, fname_B[i]);
	bu_log("View %d: captured legacy image B[%d]\n", i, i);

	/* Verify other views are unaffected (still match their A image) */
	for (int j = 0; j < 4; j++) {
	    if (j == i)
		continue;
	    refresh_view(gedp, j);
	    char tmp[64];
	    snprintf(tmp, 64, "bsg_qparity_chk%d_%d.png", i, j);
	    grab_view(gedp, j, tmp);
	    if (!images_match(fname_A[j], tmp, ADIFF)) {
		bu_log("FAIL: disabling V%d BSG corrupted V%d output\n", i, j);
		failures++;
	    } else {
		bu_log("PASS: V%d independent — disabling V%d BSG did not affect it\n", j, i);
	    }
	    bu_file_delete(tmp);
	}

	/* Restore BSG on view i */
	vi->bsg_root = saved_root;
	bsg_scene_root_sync((bsg_node *)vi->bsg_root, vi);

	refresh_view(gedp, i);
	snprintf(fname_C[i], 64, "bsg_qparity_C%d.png", i);
	grab_view(gedp, i, fname_C[i]);
	bu_log("View %d: captured restored BSG image C[%d]\n", i, i);

	/* A[i] == B[i]: BSG and legacy paths are pixel-identical */
	if (!images_match(fname_A[i], fname_B[i], ADIFF)) {
	    bu_log("FAIL V%d: A (BSG) != B (legacy)\n", i);
	    failures++;
	} else {
	    bu_log("PASS V%d: A (BSG) == B (legacy)\n", i);
	}

	/* A[i] == C[i]: BSG is stable after toggle */
	if (!images_match(fname_A[i], fname_C[i], ADIFF)) {
	    bu_log("FAIL V%d: A (initial BSG) != C (restored BSG)\n", i);
	    failures++;
	} else {
	    bu_log("PASS V%d: A (initial BSG) == C (restored BSG)\n", i);
	}
    }

    /* Cleanup */
    for (int i = 0; i < 4; i++) {
	bu_file_delete(fname_A[i]);
	bu_file_delete(fname_B[i]);
	bu_file_delete(fname_C[i]);
    }
    bu_file_delete("moss_bsg_quad_tmp.g");
    bu_dirclear(lcache);

    ged_close(gedp);

    if (failures)
	bu_log("RESULT: %d failure(s)\n", failures);
    else
	bu_log("RESULT: all quad-BSG parity tests PASSED\n");

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
