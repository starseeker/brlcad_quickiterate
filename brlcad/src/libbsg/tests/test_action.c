/*               T E S T _ A C T I O N . C
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
/** @file libbsg/tests/test_action.c
 *
 * Phase 5 unit tests: BSG action framework (BBOX, COLLECT, EXPORT).
 */

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/node.h"
#include "bsg/node_group.h"
#include "bsg/node_shape.h"
#include "bsg/vlist.h"
#include "bsg/action.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static struct bsg_view *
make_view(void)
{
    struct bsg_view *v;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_view");
    return v;
}

static void
free_view(struct bsg_view *v)
{
    if (!v) return;
    bsg_view_free(v);
    bu_free(v, "test_view");
}

/** Create a shape with a bounding box set */
static bsg_node *
make_bbox_shape(struct bsg_view *v,
		double x0, double y0, double z0,
		double x1, double y1, double z1)
{
    bsg_node *s = bsg_shape_create(v);
    if (!s) return NULL;
    point_t bmin, bmax;
    VSET(bmin, x0, y0, z0);
    VSET(bmax, x1, y1, z1);
    bsg_node_set_bounds(s, bmin, bmax, 1);
    return s;
}


/* ------------------------------------------------------------------ */
/* Test 1: BBOX action — no shapes → invalid result                    */
/* ------------------------------------------------------------------ */

static int
test_bbox_empty(void)
{
    printf("=== Test 1: bbox_empty ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("scene_root_create");

    struct bsg_action *a = bsg_action_create(BSG_ACTION_BBOX);
    if (!a) FAIL("bsg_action_create");

    bsg_action_execute(a, root, NULL);
    if (a->result.bbox.valid != 0) FAIL("expected valid=0 for empty tree");

    bsg_action_destroy(a, NULL);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("bbox_empty");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 2: BBOX action — two shapes → correct aggregate bounds         */
/* ------------------------------------------------------------------ */

static int
test_bbox_two_shapes(void)
{
    printf("=== Test 2: bbox_two_shapes ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("scene_root_create");

    bsg_node *s1 = make_bbox_shape(v,  0.0,  0.0,  0.0,  1.0,  1.0,  1.0);
    bsg_node *s2 = make_bbox_shape(v, -2.0, -2.0, -2.0,  3.0,  3.0,  3.0);
    if (!s1 || !s2) FAIL("make_bbox_shape");

    bsg_node_add_child(root, s1);
    bsg_node_add_child(root, s2);

    struct bsg_action *a = bsg_action_create(BSG_ACTION_BBOX);
    bsg_action_execute(a, root, NULL);

    if (!a->result.bbox.valid) FAIL("expected valid=1");
    if (a->result.bbox.bmin[0] > -1.9999) FAIL("bmin X should be ~ -2");
    if (a->result.bbox.bmax[0] < 2.9999)  FAIL("bmax X should be ~ 3");

    bsg_action_destroy(a, NULL);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("bbox_two_shapes");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 3: COLLECT action — collect all shapes                         */
/* ------------------------------------------------------------------ */

static int
test_collect_shapes(void)
{
    printf("=== Test 3: collect_shapes ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("scene_root_create");

    bsg_node *g   = bsg_group_create(v);
    bsg_node *s1  = bsg_shape_create(v);
    bsg_node *s2  = bsg_shape_create(v);
    if (!g || !s1 || !s2) FAIL("node creation");

    bsg_node_add_child(root, g);
    bsg_node_add_child(g, s1);
    bsg_node_add_child(g, s2);

    struct bsg_action *a = bsg_action_create(BSG_ACTION_COLLECT);
    a->params.collect_mask = BSG_NODE_SHAPE;
    bsg_action_execute(a, root, NULL);

    size_t cnt = BU_PTBL_LEN(&a->result.nodes);
    if (cnt != 2) {
	printf("  FAIL: collect_shapes expected 2, got %zu\n", cnt);
	bsg_action_destroy(a, NULL);
	bsg_shape_destroy(s1);
	bsg_shape_destroy(s2);
	bsg_group_destroy(g);
	bsg_scene_root_destroy(root);
	free_view(v);
	return 1;
    }

    bsg_action_destroy(a, NULL);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_group_destroy(g);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("collect_shapes");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 4: COLLECT action — collect all (mask=0)                       */
/* ------------------------------------------------------------------ */

static int
test_collect_all(void)
{
    printf("=== Test 4: collect_all ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("scene_root_create");

    bsg_node *g  = bsg_group_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node_add_child(root, g);
    bsg_node_add_child(g, s1);

    /* mask=0 should collect all nodes: root + g + s1 */
    struct bsg_action *a = bsg_action_create(BSG_ACTION_COLLECT);
    a->params.collect_mask = 0;
    bsg_action_execute(a, root, NULL);

    size_t cnt = BU_PTBL_LEN(&a->result.nodes);
    if (cnt < 3) {
	printf("  FAIL: collect_all expected >= 3, got %zu\n", cnt);
	bsg_action_destroy(a, NULL);
	bsg_shape_destroy(s1);
	bsg_group_destroy(g);
	bsg_scene_root_destroy(root);
	free_view(v);
	return 1;
    }

    bsg_action_destroy(a, NULL);
    bsg_shape_destroy(s1);
    bsg_group_destroy(g);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("collect_all");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 5: NULL-guard — no crash on NULL inputs                        */
/* ------------------------------------------------------------------ */

static int
test_null_guards(void)
{
    printf("=== Test 5: null_guards ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);

    bsg_action_execute(NULL, root, NULL);   /* NULL action — no crash */
    bsg_action_execute(NULL, NULL, NULL);   /* both NULL — no crash */

    struct bsg_action *a = bsg_action_create(BSG_ACTION_BBOX);
    bsg_action_execute(a, NULL, NULL);     /* NULL root — no crash */
    if (a->result.bbox.valid != 0) FAIL("NULL root should leave valid=0");
    bsg_action_destroy(a, NULL);

    bsg_action_destroy(NULL, NULL);        /* NULL action — no crash */

    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("null_guards");
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

    failures += test_bbox_empty();
    failures += test_bbox_two_shapes();
    failures += test_collect_shapes();
    failures += test_collect_all();
    failures += test_null_guards();

    if (failures == 0)
	printf("RESULT: all Phase 5 action tests PASSED\n");
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
