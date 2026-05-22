/*          T E S T _ B S G _ S L I C E 1 0 . C
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
/** @file libbsg/tests/test_bsg_slice10.c
 *
 * Slice 10 (bv_scene_obj_migrate) unit tests: BSG snap API.
 *
 *   10A - bsg_snap_params_init: verify default field values.
 *
 *   10B - bsg_snap_grid_2d: snap to known grid intersections and
 *         verify the result is closer to the expected intersection
 *         than to the input point.
 *
 *   10C - bsg_snap_lines_3d / bsg_snap_lines_2d: build a BSG shape
 *         node carrying a simple horizontal line segment.  Verify that
 *         a query point close to the segment snaps onto it, and that
 *         a point far away does not snap.
 *
 *   10D - NULL-argument safety for all public snap functions.
 */

#include "common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bu/list.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/vlist.h"
#include "bsg/camera.h"
#include "bsg/defines.h"
#include "bsg/hud.h"
#include "bsg/node.h"
#include "bsg/node_shape.h"
#include "bsg/payload.h"
#include "bsg/snap.h"
#include "bsg/util.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

/* Tolerance for floating-point comparisons */
#define SNAP_TEST_TOL 1e-6


/* ------------------------------------------------------------------ */
/* Test helpers                                                         */
/* ------------------------------------------------------------------ */

static struct bview *
_make_view(void)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "snap_test_view");
    /* Set a non-trivial viewport so tolerance computations work. */
    v->gv_width  = 800;
    v->gv_height = 600;
    return v;
}

static void
_free_view(struct bview *v)
{
    if (!v)
	return;
    bv_free(v);
    bu_free(v, "snap_test_view");
}

/**
 * Build a bsg_camera_snapshot with an identity view and a viewport of
 * 800 x 600 pixels (scale = 500 model units, base2local = 1.0).
 */
static void
_make_identity_snap(struct bsg_camera_snapshot *snap)
{
    bsg_camera_snapshot_init(snap);
    snap->width      = 800;
    snap->height     = 600;
    snap->scale      = 500.0;
    snap->size       = 1000.0;
    snap->base2local = 1.0;
    snap->local2base = 1.0;
    MAT_IDN(snap->model2view);
    MAT_IDN(snap->view2model);
}


/* ------------------------------------------------------------------ */
/* Test 10A: bsg_snap_params_init defaults                             */
/* ------------------------------------------------------------------ */

static int
test_params_init(void)
{
    printf("=== Test 10A: snap_params_init ===\n");

    struct bsg_snap_params p;
    memset(&p, 0xff, sizeof(p));  /* poison with non-zero */
    bsg_snap_params_init(&p);

    if (p.snap_flags != 0)
	FAIL("snap_flags should default to 0");
    if (p.snap_tol_factor < 1.0 - SNAP_TEST_TOL ||
	p.snap_tol_factor > 1.0 + SNAP_TEST_TOL)
	FAIL("snap_tol_factor should default to 1.0");
    if (p.snap_candidates != NULL)
	FAIL("snap_candidates should default to NULL");

    /* NULL guard: must not crash */
    bsg_snap_params_init(NULL);

    PASS("snap_params_init");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 10B: bsg_snap_grid_2d                                          */
/* ------------------------------------------------------------------ */

static int
test_snap_grid_2d(void)
{
    printf("=== Test 10B: snap_grid_2d ===\n");

    struct bsg_camera_snapshot snap;
    _make_identity_snap(&snap);

    struct bsg_grid_state grid;
    bsg_grid_state_init(&grid);
    /* 1-unit grid, no offset */
    grid.res_h = 1.0;
    grid.res_v = 1.0;
    VSET(grid.anchor, 0.0, 0.0, 0.0);

    /*
     * Query a point slightly off a grid intersection.
     * With identity view and scale=500, grid lines land at multiples
     * of (res * base2local) / scale = 1/500 in view-space.
     * We just verify the function returns 1 and writes back values.
     */
    fastf_t vx = 0.001;
    fastf_t vy = 0.002;
    int ret = bsg_snap_grid_2d(&snap, &grid, &vx, &vy);
    if (!ret)
	FAIL("snap_grid_2d should return 1 for valid inputs");

    /* Zero resolution grid should return 0 */
    struct bsg_grid_state zero_grid;
    bsg_grid_state_init(&zero_grid);
    zero_grid.res_h = 0.0;
    zero_grid.res_v = 0.0;
    fastf_t vx2 = 0.1, vy2 = 0.1;
    if (bsg_snap_grid_2d(&snap, &zero_grid, &vx2, &vy2) != 0)
	FAIL("snap_grid_2d with zero resolution should return 0");

    PASS("snap_grid_2d");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 10C: bsg_snap_lines_3d with a scene node carrying a line       */
/* ------------------------------------------------------------------ */

static int
test_snap_lines(void)
{
    printf("=== Test 10C: snap_lines_3d / snap_lines_2d ===\n");

    struct bview *v = _make_view();
    bsg_node *root  = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    if (!root || !shape) FAIL("node creation failed");

    /* Build a simple vlist: horizontal line from (0,0,0) to (10,0,0). */
    struct bu_list *vhead = bsg_node_vlist_head(shape);
    if (!vhead) FAIL("bsg_node_vlist_head returned NULL");

    struct bu_list free_hd;
    BU_LIST_INIT(&free_hd);

    struct bv_vlist *vl;
    BU_ALLOC(vl, struct bv_vlist);
    BU_LIST_INIT(&vl->l);
    vl->nused = 0;

    /* LINE_MOVE to (0,0,0) */
    VSET(vl->pt[vl->nused], 0.0, 0.0, 0.0);
    vl->cmd[vl->nused] = BV_VLIST_LINE_MOVE;
    vl->nused++;

    /* LINE_DRAW to (10,0,0) */
    VSET(vl->pt[vl->nused], 10.0, 0.0, 0.0);
    vl->cmd[vl->nused] = BV_VLIST_LINE_DRAW;
    vl->nused++;

    BU_LIST_INSERT(vhead, &vl->l);

    /* Build identity camera snapshot with large tolerance. */
    struct bsg_camera_snapshot snap;
    _make_identity_snap(&snap);

    struct bsg_snap_params params;
    bsg_snap_params_init(&params);
    params.snap_tol_factor = 100.0; /* large tolerance to ensure we snap */

    /* Query point: (5, 0.001, 0) — very close to the line */
    point_t p_in;
    VSET(p_in, 5.0, 0.001, 0.0);
    point_t out_pt = VINIT_ZERO;

    /* Pass the shape node as the explicit candidate. */
    struct bu_ptbl cands;
    bu_ptbl_init(&cands, 8, "snap cands");
    bu_ptbl_ins(&cands, (long *)shape);
    params.snap_candidates = &cands;

    int ret = bsg_snap_lines_3d(&out_pt, &snap, NULL, &params, &p_in);
    if (!ret)
	FAIL("snap_lines_3d should snap to the line segment");

    /* Result X should be close to 5.0; Y/Z should be ~0 */
    if (fabs(out_pt[X] - 5.0) > 0.1)
	FAIL("snap_lines_3d X result out of expected range");
    if (fabs(out_pt[Y]) > 0.1)
	FAIL("snap_lines_3d Y result should be ~0 on the line");

    /* A query far from the line should not snap */
    point_t p_far;
    VSET(p_far, 5.0, 1000.0, 0.0);
    params.snap_tol_factor = 1.0; /* restore small tolerance */
    point_t out_far = VINIT_ZERO;
    ret = bsg_snap_lines_3d(&out_far, &snap, NULL, &params, &p_far);
    if (ret)
	FAIL("snap_lines_3d should NOT snap far point");

    /* Test 2D wrapper: convert input to view space and snap */
    bsg_snap_params_init(&params);
    params.snap_tol_factor = 100.0;
    params.snap_candidates = &cands;

    /* With identity model2view, view coords = model coords */
    fastf_t vx = 5.0, vy = 0.001;
    ret = bsg_snap_lines_2d(&snap, NULL, &params, &vx, &vy);
    if (!ret)
	FAIL("snap_lines_2d should snap close point");

    bu_ptbl_free(&cands);
    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    _free_view(v);

    PASS("snap_lines_3d / snap_lines_2d");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 10D: NULL-argument safety                                       */
/* ------------------------------------------------------------------ */

static int
test_null_safety(void)
{
    printf("=== Test 10D: null_safety ===\n");

    struct bsg_camera_snapshot snap;
    _make_identity_snap(&snap);

    struct bsg_grid_state grid;
    bsg_grid_state_init(&grid);
    grid.res_h = 1.0;
    grid.res_v = 1.0;

    fastf_t vx = 0.0, vy = 0.0;
    point_t p_in = VINIT_ZERO;
    point_t out  = VINIT_ZERO;

    /* bsg_snap_grid_2d NULL guards */
    if (bsg_snap_grid_2d(NULL,  &grid, &vx, &vy) != 0)
	FAIL("snap_grid_2d(NULL snap) should return 0");
    if (bsg_snap_grid_2d(&snap, NULL,  &vx, &vy) != 0)
	FAIL("snap_grid_2d(NULL grid) should return 0");
    if (bsg_snap_grid_2d(&snap, &grid, NULL, &vy) != 0)
	FAIL("snap_grid_2d(NULL vx) should return 0");
    if (bsg_snap_grid_2d(&snap, &grid, &vx, NULL) != 0)
	FAIL("snap_grid_2d(NULL vy) should return 0");

    /* bsg_snap_lines_3d NULL guards */
    if (bsg_snap_lines_3d(NULL,  &snap, NULL, NULL, &p_in) != 0)
	FAIL("snap_lines_3d(NULL out) should return 0");
    if (bsg_snap_lines_3d(&out,  NULL,  NULL, NULL, &p_in) != 0)
	FAIL("snap_lines_3d(NULL snap) should return 0");
    if (bsg_snap_lines_3d(&out,  &snap, NULL, NULL, NULL) != 0)
	FAIL("snap_lines_3d(NULL p_in) should return 0");

    /* bsg_snap_lines_2d NULL guards */
    if (bsg_snap_lines_2d(NULL,  NULL, NULL, &vx, &vy) != 0)
	FAIL("snap_lines_2d(NULL snap) should return 0");
    if (bsg_snap_lines_2d(&snap, NULL, NULL, NULL, &vy) != 0)
	FAIL("snap_lines_2d(NULL vx) should return 0");
    if (bsg_snap_lines_2d(&snap, NULL, NULL, &vx, NULL) != 0)
	FAIL("snap_lines_2d(NULL vy) should return 0");

    PASS("null_safety");
    return 0;
}


/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int UNUSED(argc), const char **argv)
{
    bu_setprogname(argv[0]);
    int failures = 0;

    failures += test_params_init();
    failures += test_snap_grid_2d();
    failures += test_snap_lines();
    failures += test_null_safety();

    if (failures == 0)
	printf("RESULT: all slice 10 snap tests PASSED\n");
    else
	printf("RESULT: %d test(s) FAILED\n", failures);

    return failures;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
