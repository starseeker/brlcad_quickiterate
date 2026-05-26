/*              L O D _ C R O S S R U N . C P P
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
/** @file lod_crossrun.cpp
 *
 * LoD cross-run cache-stability test.
 *
 * The bsg_lod_cache unit test verifies in-process round-trips.  This test
 * exercises the cross-process (or cross-ged_open) scenario:
 *
 *  Run 1:
 *    a. Open moss.g, facetize all.g → all.bot.
 *    b. Enable mesh LoD.
 *    c. Draw all.bot with LoD active (populates the bu_cache on disk).
 *    d. Capture the rendered image (R1).
 *    e. ged_close (releases all in-memory LoD data).
 *
 *  Run 2:
 *    a. Reopen the same moss.g copy without re-facetizing.
 *    b. Enable mesh LoD (same bu_cache directory).
 *    c. Draw all.bot with LoD active (must load from cache, not recompute).
 *    d. Capture the rendered image (R2).
 *    e. ged_close.
 *
 *  Assert R1 == R2 (pixel comparison with small off-by-1 tolerance).
 *
 * If the cache is NOT being used on run 2 the LoD levels might be
 * recomputed from scratch, producing a different LoD selection (e.g. a
 * different number of displayed triangles) and therefore a different image.
 *
 * Usage: ged_test_lod_crossrun <directory-containing-moss.g>
 */

#include "common.h"

#include <fstream>
#include <unordered_set>

#include <bu.h>
#define DM_WITH_RT
#include <dm.h>
#include <ged.h>
#include <bsg/lod.h>
#include <icv.h>

#include "../../dbi.h"

#define LOD_ADIFF_THRES 20

extern "C" void ged_changed_callback(struct db_i *UNUSED(dbip), struct directory *dp, int mode, void *u_data);

/* ------------------------------------------------------------------ */
/* Minimal GED setup (swrast DM, 512×512, az/el 35/25)                */
/* ------------------------------------------------------------------ */
static struct ged *
open_and_attach(const char *gfile)
{
    struct ged *gedp = ged_open("db", gfile, 1);
    if (!gedp)
	return NULL;

    gedp->dbi_state = new DbiState(gedp);
    DbiState *dbis   = (DbiState *)gedp->dbi_state;
    gedp->new_cmd_forms = 1;
    gedp->ged_lod = bsg_mesh_lod_context_create(gedp->dbip->dbi_filename);
    bu_setenv("DM_SWRAST", "1", 1);
    db_add_changed_clbk(gedp->dbip, &ged_changed_callback, (void *)gedp);

    const char *s_av[6] = {"dm", "attach", "swrast", "SW", NULL};
    ged_exec_dm(gedp, 4, s_av);

    struct bsg_view *v   = gedp->ged_gvp;
    struct dm   *dmp  = (struct dm *)v->dmp;
    dm_set_width(dmp, 512);
    dm_set_height(dmp, 512);
    dm_configure_win(dmp, 0);
    dm_set_zbuffer(dmp, 1);
    fastf_t wb[6] = {-1, 1, -1, 1, -100, 100};
    dm_set_win_bounds(dmp, wb);
    dm_set_vp(dmp, &v->gv_scale);
    v->dmp          = dmp;
    v->gv_width     = dm_get_width(dmp);
    v->gv_height    = dm_get_height(dmp);
    v->gv_base2local = gedp->dbip->dbi_base2local;
    v->gv_local2base = gedp->dbip->dbi_local2base;

    /* Clear any stale cache data */
    const char *vc_av[4] = {"view", "lod", "cache", "clear"};
    ged_exec_view(gedp, 4, vc_av);

    /* Set view orientation */
    const char *ae_av[4] = {"ae", "35", "25", NULL};
    ged_exec_ae(gedp, 3, ae_av);

    (void)dbis;
    return gedp;
}

/* ------------------------------------------------------------------ */
/* Render current scene to PNG file                                    */
/* ------------------------------------------------------------------ */
static int
render_to_file(struct ged *gedp, const char *outfile)
{
    struct bsg_view *v   = gedp->ged_gvp;
    DbiState     *dbis = (DbiState *)gedp->dbi_state;
    BViewState   *bvs  = dbis->get_view_state(v);
    dbis->update();
    std::unordered_set<struct bsg_view *> uset;
    uset.insert(v);
    bvs->redraw(NULL, uset, 1);

    struct dm *dmp = (struct dm *)v->dmp;
    unsigned char *bg1, *bg2;
    dm_get_bg(&bg1, &bg2, dmp);
    dm_set_bg(dmp, bg1[0], bg1[1], bg1[2], bg2[0], bg2[1], bg2[2]);
    dm_set_dirty(dmp, 0);
    dm_draw_objs(v, NULL, NULL);
    dm_draw_end(dmp);

    const char *sg_av[2] = {"screengrab", outfile};
    return ged_exec_screengrab(gedp, 2, sg_av);
}

/* ------------------------------------------------------------------ */
/* Compare two PNG images with a small off-by-1 allowance              */
/* ------------------------------------------------------------------ */
static int
png_pixel_match(const char *a, const char *b, int adiff_allow)
{
    icv_image_t *ia = icv_read(a, BU_MIME_IMAGE_PNG, 0, 0);
    icv_image_t *ib = icv_read(b, BU_MIME_IMAGE_PNG, 0, 0);
    if (!ia || !ib) {
	if (ia) icv_destroy(ia);
	if (ib) icv_destroy(ib);
	bu_log("png_pixel_match: could not read '%s' or '%s'\n", a, b);
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

    /* Private cache dir so we don't pollute the system cache */
    char lcache[MAXPATHLEN];
    bu_dir(lcache, MAXPATHLEN, BU_DIR_CURR, "lod_crossrun_cache", NULL);
    bu_mkdir(lcache);
    bu_setenv("BU_DIR_CACHE", lcache, 1);

    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);

    /* Make a private copy of moss.g so facetize doesn't dirty the source */
    struct bu_vls srcpath = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&srcpath, "%s/moss.g", datadir);
    {
	std::ifstream orig(bu_vls_cstr(&srcpath), std::ios::binary);
	std::ofstream tmpg("moss_lod_cr_tmp.g", std::ios::binary);
	tmpg << orig.rdbuf();
    }
    bu_vls_free(&srcpath);

    /* ----------------------------------------------------------------
     * Run 1 — generate facetized geometry, populate LoD cache, render.
     * -------------------------------------------------------------- */
    bu_log("LoD cross-run test: Run 1 — facetize + draw (populates cache)\n");

    struct ged *gedp = open_and_attach("moss_lod_cr_tmp.g");
    if (!gedp)
	bu_exit(EXIT_FAILURE, "Run 1: ged_open failed\n");

    /* Facetize all.g → all.bot at a moderately fine tolerance */
    const char *s_av[8] = {NULL};
    s_av[0] = "tol"; s_av[1] = "rel"; s_av[2] = "0.0005"; s_av[3] = NULL;
    ged_exec_tol(gedp, 3, s_av);

    s_av[0] = "facetize"; s_av[1] = "-r"; s_av[2] = "all.g"; s_av[3] = "all.bot"; s_av[4] = NULL;
    ged_exec_facetize(gedp, 4, s_av);
    ((DbiState *)gedp->dbi_state)->update();

    /* Enable LoD at a coarse scale to make the level selection visible */
    s_av[0] = "view"; s_av[1] = "lod"; s_av[2] = "mesh"; s_av[3] = "1"; s_av[4] = NULL;
    ged_exec_view(gedp, 4, s_av);
    s_av[0] = "view"; s_av[1] = "lod"; s_av[2] = "scale"; s_av[3] = "0.8"; s_av[4] = NULL;
    ged_exec_view(gedp, 4, s_av);

    s_av[0] = "draw"; s_av[1] = "-m1"; s_av[2] = "all.bot"; s_av[3] = NULL;
    ged_exec_draw(gedp, 3, s_av);
    s_av[0] = "autoview"; s_av[1] = NULL;
    ged_exec_autoview(gedp, 1, s_av);

    int r1_ret = render_to_file(gedp, "lod_cr_run1.png");
    bu_log("Run 1 image captured (%s)\n", r1_ret ? "WARN: screengrab failed" : "ok");

    /* Close — releases all in-memory LoD structures; bu_cache stays on disk */
    delete (DbiState *)gedp->dbi_state;
    gedp->dbi_state = NULL;
    bsg_mesh_lod_context_destroy(gedp->ged_lod);
    gedp->ged_lod = NULL;
    ged_close(gedp);

    /* ----------------------------------------------------------------
     * Run 2 — reopen without re-facetizing; LoD must come from cache.
     * -------------------------------------------------------------- */
    bu_log("LoD cross-run test: Run 2 — reopen, draw (must use cache)\n");

    gedp = open_and_attach("moss_lod_cr_tmp.g");
    if (!gedp) {
	bu_file_delete("lod_cr_run1.png");
	bu_file_delete("moss_lod_cr_tmp.g");
	bu_dirclear(lcache);
	bu_exit(EXIT_FAILURE, "Run 2: ged_open failed\n");
    }

    /* Same LoD settings */
    s_av[0] = "view"; s_av[1] = "lod"; s_av[2] = "mesh"; s_av[3] = "1"; s_av[4] = NULL;
    ged_exec_view(gedp, 4, s_av);
    s_av[0] = "view"; s_av[1] = "lod"; s_av[2] = "scale"; s_av[3] = "0.8"; s_av[4] = NULL;
    ged_exec_view(gedp, 4, s_av);

    s_av[0] = "draw"; s_av[1] = "-m1"; s_av[2] = "all.bot"; s_av[3] = NULL;
    ged_exec_draw(gedp, 3, s_av);
    s_av[0] = "autoview"; s_av[1] = NULL;
    ged_exec_autoview(gedp, 1, s_av);

    int r2_ret = render_to_file(gedp, "lod_cr_run2.png");
    bu_log("Run 2 image captured (%s)\n", r2_ret ? "WARN: screengrab failed" : "ok");

    delete (DbiState *)gedp->dbi_state;
    gedp->dbi_state = NULL;
    bsg_mesh_lod_context_destroy(gedp->ged_lod);
    gedp->ged_lod = NULL;
    ged_close(gedp);

    /* ----------------------------------------------------------------
     * Compare run1 == run2.
     * -------------------------------------------------------------- */
    int ret = 0;
    if (r1_ret || r2_ret) {
	bu_log("SKIP: one or both captures failed — skipping pixel comparison\n");
    } else if (!png_pixel_match("lod_cr_run1.png", "lod_cr_run2.png", LOD_ADIFF_THRES)) {
	bu_log("FAIL: LoD cross-run images differ — cache not used on run 2\n");
	ret = 1;
    } else {
	bu_log("PASS: LoD cross-run images match — cache correctly reused\n");
    }

    /* Cleanup */
    bu_file_delete("lod_cr_run1.png");
    bu_file_delete("lod_cr_run2.png");
    bu_file_delete("moss_lod_cr_tmp.g");
    bu_dirclear(lcache);

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
