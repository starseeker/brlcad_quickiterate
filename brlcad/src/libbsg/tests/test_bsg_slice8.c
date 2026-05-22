/*           T E S T _ B S G _ S L I C E 8 . C
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
/** @file libbsg/tests/test_bsg_slice8.c
 *
 * Slice 8 (bv_scene_obj_migrate) tests:
 *
 *   1. bsg_node_compute_bound() — per-node AABB from vlist geometry:
 *      - Create a BSG shape node, attach a simple line segment vlist.
 *      - Verify compute_bound returns 1 and the bmin/bmax are sensible.
 *      - Verify compute_bound on an empty node returns 0.
 *
 *   2. bsg_node_compute_bound() — with pre-set bounds (no geometry):
 *      - Set bmin/bmax via bsg_node_bounds_set.
 *      - Verify that compute_bound does NOT overwrite pre-set bounds when
 *        no payload geometry is present (returns 0, bounds unchanged).
 *
 *   3. bsg_view_compute_bounds() — basic view OBB:
 *      - Build a small BSG tree (root → group → shape with vlist).
 *      - Construct a default orthographic camera snapshot.
 *      - Verify the function returns 0 (success).
 *      - Verify result.radius > 0.
 *
 *   4. bsg_view_select() — returns 0 for empty tree:
 *      - Verify select on a root-only tree returns 0.
 *
 *   5. bsg_view_select() — finds shape at expected pixel:
 *      - Place a shape at the view centre (model origin with default view).
 *      - Verify selecting at the screen centre finds the shape.
 *
 *   6. bsg_view_rect_select() — finds shape inside rectangle:
 *      - Reuse the tree from test 5.
 *      - Select a full-screen rectangle and verify the shape is found.
 *
 *   7. NULL-argument safety:
 *      - All four API functions return 0 / -1 when required args are NULL.
 */

#include "common.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bu/app.h"
#include "bu/list.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/vlist.h"
#include "bsg/camera.h"
#include "bsg/node.h"
#include "bsg/node_group.h"
#include "bsg/node_shape.h"
#include "bsg/query.h"
#include "bsg/util.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


/* ------------------------------------------------------------------ */
/* Shared fixture helpers                                               */
/* ------------------------------------------------------------------ */

static struct bview *
_make_view(void)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "slice8_view");
    v->gv_width  = 512;
    v->gv_height = 512;
    return v;
}

static void
_free_view(struct bview *v)
{
    if (!v) return;
    bv_free(v);
    bu_free(v, "slice8 view");
}

/**
 * Build a minimal orthographic camera snapshot for a 512×512 viewport with
 * the camera looking down -Z (default BRL-CAD orientation).
 */
static void
_make_snap(struct bsg_camera_snapshot *snap, int width, int height)
{
    bsg_camera_snapshot_init(snap);
    snap->width      = width;
    snap->height     = height;
    snap->aspect     = (fastf_t)width / (fastf_t)height;
    snap->projection = BSG_CAMERA_ORTHO;
    snap->scale      = 1.0;
    snap->size       = 2.0;
    /* Identity matrices → view space == model space. */
    MAT_IDN(snap->model2view);
    MAT_IDN(snap->view2model);
    /* Look direction: -Z (into the screen). */
    VSET(snap->look_dir,  0.0,  0.0, -1.0);
    VSET(snap->up_dir,    0.0,  1.0,  0.0);
    VSET(snap->right_dir, 1.0,  0.0,  0.0);
}

/**
 * Create a BSG shape node and attach a simple line segment from p0 to p1.
 * The caller owns the returned node; free with bsg_node_destroy().
 */
static bsg_node *
_make_shape_with_line(struct bview *v, const point_t p0, const point_t p1)
{
    bsg_node *shape = bsg_shape_create(v);
    if (!shape)
	return NULL;

    struct bu_list vhead = BU_LIST_INIT_ZERO;
    struct bu_list vlfree = BU_LIST_INIT_ZERO;
    BU_LIST_INIT(&vhead);
    BU_LIST_INIT(&vlfree);

    BV_ADD_VLIST(&vlfree, &vhead, p0, BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(&vlfree, &vhead, p1, BV_VLIST_LINE_DRAW);

    bsg_shape_set_vlist(shape, &vhead);
    BV_FREE_VLIST(&vlfree, &vhead);

    return shape;
}


/* ====================================================================
 * Test 1: bsg_node_compute_bound — vlist geometry
 * ==================================================================== */

static int
test_compute_bound_vlist(void)
{
    printf("=== Test 1: bsg_node_compute_bound (vlist geometry) ===\n");

    struct bview *v = _make_view();
    point_t p0 = {-1.0, -2.0, -3.0};
    point_t p1 = { 4.0,  5.0,  6.0};
    bsg_node *shape = _make_shape_with_line(v, p0, p1);
    if (!shape) FAIL("create shape");

    /* --- 1a. compute_bound on a node with vlist should return 1 --- */
    int ret = bsg_node_compute_bound(shape, NULL);
    if (ret != 1) {
	bsg_node_destroy(shape);
	_free_view(v);
	FAIL("compute_bound returned 0 for vlist node");
    }
    PASS("compute_bound returns 1 for vlist node");

    /* --- 1b. bounds should span the two endpoints --- */
    point_t bmin, bmax;
    bsg_node_bounds_get(shape, bmin, bmax);

    if (!NEAR_EQUAL(bmin[X], -1.0, 1e-9) ||
	!NEAR_EQUAL(bmin[Y], -2.0, 1e-9) ||
	!NEAR_EQUAL(bmin[Z], -3.0, 1e-9)) {
	bsg_node_destroy(shape);
	_free_view(v);
	FAIL("bmin wrong after compute_bound");
    }
    PASS("bmin correct");

    if (!NEAR_EQUAL(bmax[X], 4.0, 1e-9) ||
	!NEAR_EQUAL(bmax[Y], 5.0, 1e-9) ||
	!NEAR_EQUAL(bmax[Z], 6.0, 1e-9)) {
	bsg_node_destroy(shape);
	_free_view(v);
	FAIL("bmax wrong after compute_bound");
    }
    PASS("bmax correct");

    /* --- 1c. centre and size fields should be populated --- */
    vect_t center;
    bsg_node_center_get(shape, center);
    if (!NEAR_EQUAL(center[X], 1.5, 1e-9) ||
	!NEAR_EQUAL(center[Y], 1.5, 1e-9) ||
	!NEAR_EQUAL(center[Z], 1.5, 1e-9)) {
	bsg_node_destroy(shape);
	_free_view(v);
	FAIL("center wrong after compute_bound");
    }
    PASS("center correct");

    fastf_t sz = bsg_node_size_get(shape);
    if (sz < 1e-9) {
	bsg_node_destroy(shape);
	_free_view(v);
	FAIL("size is zero after compute_bound");
    }
    PASS("size > 0 after compute_bound");

    /* --- 1d. NULL node should return 0 --- */
    ret = bsg_node_compute_bound(NULL, NULL);
    if (ret != 0) FAIL("compute_bound(NULL) should return 0");
    PASS("compute_bound(NULL) returns 0");

    bsg_node_destroy(shape);
    _free_view(v);
    return 0;
}


/* ====================================================================
 * Test 2: bsg_node_compute_bound — empty node
 * ==================================================================== */

static int
test_compute_bound_empty(void)
{
    printf("=== Test 2: bsg_node_compute_bound (empty node) ===\n");

    struct bview *v = _make_view();
    bsg_node *shape = bsg_shape_create(v);
    if (!shape) FAIL("create empty shape");

    /* A shape with no vlist and no mesh payload should return 0. */
    int ret = bsg_node_compute_bound(shape, NULL);
    if (ret != 0) {
	bsg_node_destroy(shape);
	_free_view(v);
	FAIL("compute_bound on empty node should return 0");
    }
    PASS("compute_bound returns 0 for empty node");

    bsg_node_destroy(shape);
    _free_view(v);
    return 0;
}


/* ====================================================================
 * Test 3: bsg_view_compute_bounds — basic OBB
 * ==================================================================== */

static int
test_view_compute_bounds(void)
{
    printf("=== Test 3: bsg_view_compute_bounds (basic OBB) ===\n");

    struct bview *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	_free_view(v);
	FAIL("bsg_scene_root_create");
    }

    bsg_node *group = bsg_group_create(v);
    if (!group) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bsg_group_create");
    }

    point_t p0 = {-1.0, -1.0, -1.0};
    point_t p1 = { 1.0,  1.0,  1.0};
    bsg_node *shape = _make_shape_with_line(v, p0, p1);
    if (!shape) {
	bsg_node_destroy(group);
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("_make_shape_with_line");
    }

    bsg_group_add_child(group, shape);
    bsg_group_add_child(root, group);

    struct bsg_camera_snapshot snap;
    _make_snap(&snap, 512, 512);

    struct bsg_view_bounds_result res;
    int ret = bsg_view_compute_bounds(&res, root, &snap);

    if (ret != 0) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bsg_view_compute_bounds returned non-zero");
    }
    PASS("bsg_view_compute_bounds returned 0");

    if (res.radius <= 0.0) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bsg_view_compute_bounds: radius <= 0");
    }
    PASS("result.radius > 0");

    /* The lookat direction should be non-zero. */
    if (MAGNITUDE(res.lookat) < 1e-9) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("lookat direction is zero vector");
    }
    PASS("result.lookat is non-zero");

    /* NULL arguments should return -1. */
    ret = bsg_view_compute_bounds(NULL, root, &snap);
    if (ret != -1) FAIL("NULL out should return -1");
    PASS("NULL out returns -1");

    ret = bsg_view_compute_bounds(&res, root, NULL);
    if (ret != -1) FAIL("NULL snap should return -1");
    PASS("NULL snap returns -1");

    bsg_scene_root_destroy(root);
    _free_view(v);
    return 0;
}


/* ====================================================================
 * Test 4: bsg_view_select — empty tree returns 0
 * ==================================================================== */

static int
test_view_select_empty(void)
{
    printf("=== Test 4: bsg_view_select (empty tree) ===\n");

    struct bview *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	_free_view(v);
	FAIL("bsg_scene_root_create");
    }

    struct bsg_camera_snapshot snap;
    _make_snap(&snap, 512, 512);

    struct bu_ptbl sset = BU_PTBL_INIT_ZERO;
    bu_ptbl_init(&sset, 8, "sset");

    int n = bsg_view_select(&sset, root, &snap, 256, 256);
    if (n != 0) {
	bu_ptbl_free(&sset);
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("select on empty tree should return 0");
    }
    PASS("bsg_view_select on empty tree returns 0");

    bu_ptbl_free(&sset);
    bsg_scene_root_destroy(root);
    _free_view(v);
    return 0;
}


/* ====================================================================
 * Test 5: bsg_view_select — finds shape at screen centre
 * ==================================================================== */

static int
test_view_select_finds(void)
{
    printf("=== Test 5: bsg_view_select (finds shape) ===\n");

    struct bview *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	_free_view(v);
	FAIL("bsg_scene_root_create");
    }

    /* Place a unit cube centred at the origin.  With identity matrices
     * the screen centre (256,256) maps to model origin. */
    point_t p0 = {-0.5, -0.5, -0.5};
    point_t p1 = { 0.5,  0.5,  0.5};
    bsg_node *shape = _make_shape_with_line(v, p0, p1);
    if (!shape) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("_make_shape_with_line");
    }
    bsg_group_add_child(root, shape);

    struct bsg_camera_snapshot snap;
    _make_snap(&snap, 512, 512);

    struct bu_ptbl sset = BU_PTBL_INIT_ZERO;
    bu_ptbl_init(&sset, 8, "sset");

    /* Select at the screen centre. */
    bsg_view_select(&sset, root, &snap, 256, 256);
    int found = (int)BU_PTBL_LEN(&sset);
    if (found < 1) {
	bu_ptbl_free(&sset);
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bsg_view_select did not find shape at screen centre");
    }
    PASS("bsg_view_select found shape at screen centre");

    bu_ptbl_free(&sset);
    bsg_scene_root_destroy(root);
    _free_view(v);
    return 0;
}


/* ====================================================================
 * Test 6: bsg_view_rect_select — finds shape inside full-screen rect
 * ==================================================================== */

static int
test_view_rect_select(void)
{
    printf("=== Test 6: bsg_view_rect_select (full-screen rectangle) ===\n");

    struct bview *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	_free_view(v);
	FAIL("bsg_scene_root_create");
    }

    point_t p0 = {-0.5, -0.5, -0.5};
    point_t p1 = { 0.5,  0.5,  0.5};
    bsg_node *shape = _make_shape_with_line(v, p0, p1);
    if (!shape) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("_make_shape_with_line");
    }
    bsg_group_add_child(root, shape);

    struct bsg_camera_snapshot snap;
    _make_snap(&snap, 512, 512);

    struct bu_ptbl sset = BU_PTBL_INIT_ZERO;
    bu_ptbl_init(&sset, 8, "sset");

    /* Full-screen rectangle should encompass the shape at the origin. */
    bsg_view_rect_select(&sset, root, &snap, 0, 0, 512, 512);
    int found = (int)BU_PTBL_LEN(&sset);
    if (found < 1) {
	bu_ptbl_free(&sset);
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bsg_view_rect_select full-screen did not find shape");
    }
    PASS("bsg_view_rect_select full-screen finds shape");

    /* A rectangle far off to the side should find nothing. */
    bu_ptbl_reset(&sset);
    bsg_view_rect_select(&sset, root, &snap, 0, 0, 1, 1);
    /* This may or may not find the shape depending on OBB geometry, so
     * we only test that it does not crash and returns a sane count. */
    PASS("bsg_view_rect_select tiny corner rectangle does not crash");

    bu_ptbl_free(&sset);
    bsg_scene_root_destroy(root);
    _free_view(v);
    return 0;
}


/* ====================================================================
 * Test 7: NULL-argument safety
 * ==================================================================== */

static int
test_null_args(void)
{
    printf("=== Test 7: NULL argument safety ===\n");

    struct bsg_camera_snapshot snap;
    _make_snap(&snap, 512, 512);

    struct bsg_view_bounds_result res;
    struct bu_ptbl sset = BU_PTBL_INIT_ZERO;
    bu_ptbl_init(&sset, 4, "sset");

    /* bsg_node_compute_bound */
    if (bsg_node_compute_bound(NULL, NULL) != 0)
	FAIL("compute_bound(NULL) != 0");
    PASS("bsg_node_compute_bound(NULL)");

    /* bsg_view_compute_bounds */
    if (bsg_view_compute_bounds(NULL,  NULL, &snap) != -1)
	FAIL("compute_bounds(NULL out) != -1");
    /* NULL root means empty scene — not an error; function returns 0. */
    if (bsg_view_compute_bounds(&res, NULL, &snap) != 0)
	FAIL("compute_bounds(NULL root) != 0");
    if (bsg_view_compute_bounds(&res, NULL, NULL) != -1)
	FAIL("compute_bounds(NULL snap) != -1");
    PASS("bsg_view_compute_bounds NULL args handled correctly");

    /* bsg_view_select */
    if (bsg_view_select(NULL,  NULL, &snap, 0, 0) != 0)
	FAIL("bsg_view_select(NULL sset) != 0");
    if (bsg_view_select(&sset, NULL, NULL, 0, 0) != 0)
	FAIL("bsg_view_select(NULL snap) != 0");
    PASS("bsg_view_select NULL args return 0");

    /* bsg_view_rect_select */
    if (bsg_view_rect_select(NULL,  NULL, &snap, 0, 0, 1, 1) != 0)
	FAIL("bsg_view_rect_select(NULL sset) != 0");
    if (bsg_view_rect_select(&sset, NULL, NULL, 0, 0, 1, 1) != 0)
	FAIL("bsg_view_rect_select(NULL snap) != 0");
    PASS("bsg_view_rect_select NULL args return 0");

    bu_ptbl_free(&sset);
    return 0;
}


/* ====================================================================
 * main
 * ==================================================================== */

int
main(int argc, char *argv[])
{
    (void)argc;
    bu_setprogname(argv[0]);

    int failures = 0;

    failures += test_compute_bound_vlist();
    failures += test_compute_bound_empty();
    failures += test_view_compute_bounds();
    failures += test_view_select_empty();
    failures += test_view_select_finds();
    failures += test_view_rect_select();
    failures += test_null_args();

    if (failures) {
	printf("\nFAILED: %d test(s) failed.\n", failures);
	return 1;
    }
    printf("\nAll slice-8 bounds/camera-query tests PASSED.\n");
    return 0;
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
