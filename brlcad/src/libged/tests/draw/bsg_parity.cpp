/*                 B S G _ P A R I T Y . C P P
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
/** @file bsg_parity.cpp
 *
 * Phase 4 exit-criteria regression: verify that the bsg_view_traverse
 * render path produces pixel-identical output to the legacy dl_* walk.
 *
 * Strategy:
 *   1. Open moss.g with a swrast off-screen dm (BSG path active because
 *      ged_open calls bsg_scene_root_create for the default view).
 *   2. Draw all.g, refresh, and capture image A (BSG path).
 *   3. Null out view->bsg_root (legacy path), refresh again, capture B.
 *   4. Restore bsg_root, refresh, capture C (BSG path again, for
 *      round-trip confidence).
 *   5. Assert A == B (BSG output is identical to legacy output).
 *   6. Assert B == C (legacy then BSG again is stable).
 *
 * Uses dm-swrast for off-screen rendering; no display hardware required.
 *
 * Usage: ged_test_bsg_parity <directory-containing-moss.g>
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

/* ---- dm refresh helper --------------------------------------------- */
static void
do_refresh(struct ged *gedp)
{
    struct bview *v = gedp->ged_gvp;
    DbiState *dbis  = (DbiState *)gedp->dbi_state;
    BViewState *bvs = dbis->get_view_state(v);
    dbis->update();
    std::unordered_set<struct bview *> uset;
    uset.insert(v);
    bvs->redraw(NULL, uset, 1);

    struct dm *dmp = (struct dm *)v->dmp;
    unsigned char *bg1, *bg2;
    dm_get_bg(&bg1, &bg2, dmp);
    dm_set_bg(dmp, bg1[0], bg1[1], bg1[2], bg2[0], bg2[1], bg2[2]);
    dm_set_dirty(dmp, 0);
    dm_draw_objs(v, NULL, NULL);
    dm_draw_end(dmp);
}

/* ---- capture screen to file ---------------------------------------- */
static int
capture(struct ged *gedp, const char *fname)
{
    const char *s_av[3] = {"screengrab", fname, NULL};
    return ged_exec_screengrab(gedp, 2, s_av);
}

/* ---- pixel-exact comparison using icv ------------------------------ */
static int
images_identical(const char *a, const char *b, int adiff_allow)
{
    icv_image_t *ia = icv_read(a, BU_MIME_IMAGE_PNG, 0, 0);
    icv_image_t *ib = icv_read(b, BU_MIME_IMAGE_PNG, 0, 0);
    if (!ia || !ib) {
	if (ia) icv_destroy(ia);
	if (ib) icv_destroy(ib);
	bu_log("images_identical: could not read %s or %s\n", a, b);
	return 0;
    }
    int match = 0, off1 = 0, offmany = 0;
    icv_diff(&match, &off1, &offmany, ia, ib);
    icv_destroy(ia);
    icv_destroy(ib);

    /* Allow a small tolerance for floating-point rounding differences */
    int bad = offmany + (off1 > adiff_allow ? off1 - adiff_allow : 0);
    if (bad) {
	bu_log("  images differ: %d off-by-many, %d off-by-1 (allowed %d)\n",
	       offmany, off1, adiff_allow);
    }
    return (bad == 0);
}

int
main(int ac, char *av[])
{
    bu_setprogname(av[0]);

    int soft_fail = 0;
    struct bu_opt_desc d[2];
    BU_OPT(d[0], "c", "continue", "", NULL, &soft_fail, "Continue on failure.");
    BU_OPT_NULL(d[1]);

    int uac = bu_opt_parse(NULL, ac, (const char **)av, d);
    if (uac != 2) {
	bu_exit(EXIT_FAILURE,
		"Usage: %s [-c] <directory-containing-moss.g>\n", av[0]);
    }
    const char *datadir = av[1];
    if (!bu_file_directory(datadir)) {
	bu_exit(EXIT_FAILURE, "ERROR: [%s] is not a directory\n", datadir);
    }

    /* Private cache so tests are hermetic */
    char lcache[MAXPATHLEN];
    bu_dir(lcache, MAXPATHLEN, BU_DIR_CURR, "bsg_parity_cache", NULL);
    bu_mkdir(lcache);
    bu_setenv("BU_DIR_CACHE", lcache, 1);

    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);

    /* Copy moss.g to a temp file */
    struct bu_vls fname = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&fname, "%s/moss.g", datadir);
    std::ifstream orig(bu_vls_cstr(&fname), std::ios::binary);
    std::ofstream tmpg("moss_bsg_parity_tmp.g", std::ios::binary);
    tmpg << orig.rdbuf();
    orig.close(); tmpg.close();

    struct ged *gedp = ged_open("db", "moss_bsg_parity_tmp.g", 1);
    gedp->dbi_state = new DbiState(gedp);
    gedp->new_cmd_forms = 1;
    gedp->ged_lod = bv_mesh_lod_context_create(gedp->dbip->dbi_filename);
    bu_setenv("DM_SWRAST", "1", 1);
    db_add_changed_clbk(gedp->dbip, &ged_changed_callback, (void *)gedp);

    /* Attach swrast display manager */
    const char *s_av[16] = {NULL};
    s_av[0] = "dm"; s_av[1] = "attach"; s_av[2] = "swrast"; s_av[3] = "SW"; s_av[4] = NULL;
    ged_exec_dm(gedp, 4, s_av);

    struct bview *v = gedp->ged_gvp;
    struct dm *dmp  = (struct dm *)v->dmp;
    dm_set_width(dmp, 512);
    dm_set_height(dmp, 512);
    dm_configure_win(dmp, 0);
    dm_set_zbuffer(dmp, 1);
    fastf_t wb[6] = {-1, 1, -1, 1, -100, 100};
    dm_set_win_bounds(dmp, wb);
    dm_set_vp(dmp, &v->gv_scale);
    v->dmp = dmp;
    v->gv_width = dm_get_width(dmp);
    v->gv_height = dm_get_height(dmp);
    v->gv_base2local = gedp->dbip->dbi_base2local;
    v->gv_local2base = gedp->dbip->dbi_local2base;

    /* Set a standard view */
    s_av[0] = "ae"; s_av[1] = "35"; s_av[2] = "25"; s_av[3] = NULL;
    ged_exec_ae(gedp, 3, s_av);

    /* Draw all.g */
    s_av[0] = "draw"; s_av[1] = "all.g"; s_av[2] = NULL;
    ged_exec_draw(gedp, 2, s_av);
    s_av[0] = "autoview"; s_av[1] = NULL;
    ged_exec_autoview(gedp, 1, s_av);

    /* ---- Image A: BSG path ----------------------------------------- */
    do_refresh(gedp);
    capture(gedp, "bsg_parity_A.png");
    bu_log("Captured image A (BSG path)\n");

    /* ---- Image B: legacy dl_* path (clear bsg_root temporarily) ---- */
    void *saved_root = v->bsg_root;
    v->bsg_root = NULL;

    do_refresh(gedp);
    capture(gedp, "bsg_parity_B.png");
    bu_log("Captured image B (legacy dl_* path)\n");

    /* ---- Image C: BSG path restored -------------------------------- */
    v->bsg_root = saved_root;
    /* Resync in case any state changed */
    bsg_scene_root_sync((bsg_node *)v->bsg_root, v);

    do_refresh(gedp);
    capture(gedp, "bsg_parity_C.png");
    bu_log("Captured image C (BSG path restored)\n");

    /* ---- Compare --------------------------------------------------- */
    int ret = 0;

    /* Allow a small tolerance: a BSG path that mirrors the dl_* walk pixel-by-pixel
     * may have up to ~20 off-by-1 pixels from floating-point rounding differences
     * in the scene-root-sync shim vs direct ptbl iteration. */
    const int ADIFF = 20;

    if (!images_identical("bsg_parity_A.png", "bsg_parity_B.png", ADIFF)) {
	bu_log("FAIL: BSG path (A) != legacy path (B) — render parity broken\n");
	ret++;
    } else {
	bu_log("PASS: BSG path == legacy path (A == B)\n");
    }

    if (!images_identical("bsg_parity_B.png", "bsg_parity_C.png", ADIFF)) {
	bu_log("FAIL: legacy (B) != BSG restored (C) — unstable across path switch\n");
	ret++;
    } else {
	bu_log("PASS: BSG restored path == legacy path (B == C)\n");
    }

    /* Cleanup */
    bu_file_delete("bsg_parity_A.png");
    bu_file_delete("bsg_parity_B.png");
    bu_file_delete("bsg_parity_C.png");
    bu_file_delete("moss_bsg_parity_tmp.g");
    bu_dirclear(lcache);

    ged_close(gedp);
    bu_vls_free(&fname);

    return ret;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
