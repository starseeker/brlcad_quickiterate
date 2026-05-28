/*               T E S T _ P I C K . C
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
/** @file libbsg/tests/test_pick.c
 *
 * Phase D3 unit tests: typed pick records and pick actions.
 *
 * Tests that do not require a real view use NULL-view guards and
 * result-lifecycle checks.  The scene-pick tests build a minimal
 * bsg_view with a flat draw root and shape nodes to exercise
 * bsg_pick_point(), bsg_pick_rect(), bsg_pick_nearest(), and the
 * bsg_pick_apply() / bsg_pick_result_to_ptbl() helpers.
 */

#include "common.h"

#include <stdio.h>
#include <math.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bsg/defines.h"
#include "bsg/draw_intent.h"
#include "bsg/node.h"
#include "bsg/node_group.h"
#include "bsg/node_shape.h"
#include "bsg/pick.h"
#include "bsg/selection.h"
#include "bsg/util.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static struct bsg_view *
make_view(void)
{
    struct bsg_view *v;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "pick_test_view");
    /* Set a non-zero viewport so bsg_view_objs_select guards pass. */
    v->gv_width  = 512;
    v->gv_height = 512;
    return v;
}

static void
free_view(struct bsg_view *v)
{
    if (!v) return;
    bsg_view_free(v);
    bu_free(v, "pick_test_view");
}

static int
append_record(struct bsg_pick_result *res, bsg_node *node, struct bsg_view *v,
	int sx, int sy, const char *path, fastf_t hit_dist)
{
    if (!res || !node)
	return 0;

    struct bsg_pick_record *pr;
    BU_GET(pr, struct bsg_pick_record);
    bu_vls_init(&pr->pr_source_path);
    bu_vls_init(&pr->pr_instance_path);
    pr->pr_node = node;
    pr->pr_view = v;
    pr->pr_screen_x = sx;
    pr->pr_screen_y = sy;
    pr->pr_hit_dist = hit_dist;
    pr->pr_primitive_id = -1;
    pr->pr_subelement_id = -1;
    bu_vls_sprintf(&pr->pr_source_path, "%s", path ? path : bu_vls_cstr(&node->s_name));
    bu_vls_sprintf(&pr->pr_instance_path, "%s", bu_vls_cstr(&pr->pr_source_path));
    bu_ptbl_ins(&res->pr_records, (long *)pr);
    return 1;
}


/* -----------------------------------------------------------------------
 * Test 1: bsg_pick_result lifecycle — no view
 * ----------------------------------------------------------------------- */

static int
test_result_lifecycle(void)
{
    printf("=== Test 1: pick_result lifecycle ===\n");

    struct bsg_pick_result *res = bsg_pick_result_create();
    if (!res) FAIL("bsg_pick_result_create returned NULL");
    if (bsg_pick_result_count(res) != 0) FAIL("initial count != 0");
    if (bsg_pick_result_get(res, 0) != NULL) FAIL("get(0) on empty != NULL");

    bsg_pick_result_free(res);
    bsg_pick_result_free(NULL); /* must not crash */

    PASS("pick_result lifecycle");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 2: bsg_pick_point with NULL view returns empty result
 * ----------------------------------------------------------------------- */

static int
test_pick_point_null_view(void)
{
    printf("=== Test 2: pick_point NULL view ===\n");

    struct bsg_pick_result *res = bsg_pick_point(NULL, 100, 100, 0);
    if (res) FAIL("pick_point(NULL) should return NULL");

    PASS("pick_point NULL view");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 3: bsg_pick_rect with NULL view returns NULL
 * ----------------------------------------------------------------------- */

static int
test_pick_rect_null_view(void)
{
    printf("=== Test 3: pick_rect NULL view ===\n");

    struct bsg_pick_result *res = bsg_pick_rect(NULL, 0, 0, 100, 100);
    if (res) FAIL("pick_rect(NULL) should return NULL");

    PASS("pick_rect NULL view");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 4: bsg_pick_point on empty scene returns empty result
 * ----------------------------------------------------------------------- */

static int
test_pick_empty_scene(void)
{
    printf("=== Test 4: pick_point on empty scene ===\n");

    struct bsg_view *v = make_view();
    if (!v) FAIL("make_view failed");

    struct bsg_pick_result *res = bsg_pick_point(v, 256, 256, 0);
    if (!res) FAIL("bsg_pick_point returned NULL on empty scene");
    if (bsg_pick_result_count(res) != 0)
	FAIL("expected 0 records on empty scene");

    bsg_pick_result_free(res);
    free_view(v);

    PASS("pick_point on empty scene");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 5: bsg_pick_result_to_ptbl on empty result
 * ----------------------------------------------------------------------- */

static int
test_to_ptbl_empty(void)
{
    printf("=== Test 5: result_to_ptbl empty ===\n");

    struct bsg_pick_result *res = bsg_pick_result_create();
    struct bu_ptbl out = BU_PTBL_INIT_ZERO;

    bsg_pick_result_to_ptbl(res, &out);
    if (BU_PTBL_LEN(&out) != 0) FAIL("ptbl should be empty");

    bu_ptbl_free(&out);
    bsg_pick_result_free(res);

    /* NULL guards */
    bsg_pick_result_to_ptbl(NULL, NULL);
    bsg_pick_result_to_ptbl(res, NULL); /* after free — crash guard */

    PASS("result_to_ptbl empty");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 6: bsg_pick_apply with empty result is a no-op
 * ----------------------------------------------------------------------- */

static int
test_pick_apply_empty(void)
{
    printf("=== Test 6: pick_apply empty result ===\n");

    struct bsg_selection *sel = bsg_selection_create();
    struct bsg_pick_result *res = bsg_pick_result_create();

    bsg_pick_apply(sel, res, BSG_PICK_OP_SET);
    if (bsg_selection_count(sel) != 0) FAIL("set with empty result should leave sel empty");

    bsg_pick_apply(sel, res, BSG_PICK_OP_ADD);
    if (bsg_selection_count(sel) != 0) FAIL("add with empty result should leave sel empty");

    bsg_pick_apply(sel, res, BSG_PICK_OP_REMOVE);
    if (bsg_selection_count(sel) != 0) FAIL("remove with empty result should leave sel empty");

    /* NULL guards */
    bsg_pick_apply(NULL, res, BSG_PICK_OP_SET);
    bsg_pick_apply(sel, NULL, BSG_PICK_OP_SET);

    bsg_pick_result_free(res);
    bsg_selection_destroy(sel);

    PASS("pick_apply empty result");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 7: bsg_pick_nearest returns NULL on NULL view
 * ----------------------------------------------------------------------- */

static int
test_pick_nearest_null(void)
{
    printf("=== Test 7: pick_nearest NULL view ===\n");

    struct bsg_pick_result *res = bsg_pick_nearest(NULL, 256, 256);
    if (res) FAIL("pick_nearest(NULL) should return NULL");

    PASS("pick_nearest NULL view");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 8: pick_op enum values are sane (compile-time style check)
 * ----------------------------------------------------------------------- */

static int
test_pick_op_values(void)
{
    printf("=== Test 8: pick_op enum values ===\n");

    if (BSG_PICK_OP_SET != 0) FAIL("BSG_PICK_OP_SET should be 0");
    if (BSG_PICK_OP_ADD != 1) FAIL("BSG_PICK_OP_ADD should be 1");
    if (BSG_PICK_OP_REMOVE != 2) FAIL("BSG_PICK_OP_REMOVE should be 2");

    PASS("pick_op enum values");
    return 0;
}

static int
test_pick_apply_nonempty(void)
{
    printf("=== Test 9: pick_apply non-empty result ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);
    if (!root || !s1 || !s2) FAIL("scene setup failed");
    bsg_node_add_child(root, s1);
    bsg_node_add_child(root, s2);

    struct bsg_pick_result *res = bsg_pick_result_create();
    struct bsg_selection *sel = bsg_selection_create();
    if (!append_record(res, s1, v, 10, 20, "/a", 1.0)) FAIL("append_record s1");
    if (!append_record(res, s2, v, 10, 20, "/b", 2.0)) FAIL("append_record s2");

    bsg_pick_apply(sel, res, BSG_PICK_OP_SET);
    if (bsg_selection_count(sel) != 2) FAIL("SET should populate both nodes");
    if (!bsg_selection_contains(sel, s1) || !bsg_selection_contains(sel, s2))
	FAIL("selection should contain both nodes after SET");

    bsg_pick_apply(v->gv_s->gv_selected, res, BSG_PICK_OP_SET);
    if (bsg_selection_count(v->gv_s->gv_selected) != 2)
	FAIL("view selection storage should accept pick_apply");

    bsg_pick_apply(sel, res, BSG_PICK_OP_REMOVE);
    if (bsg_selection_count(sel) != 0) FAIL("REMOVE should clear both nodes");

    bsg_selection_destroy(sel);
    bsg_pick_result_free(res);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("pick_apply non-empty result");
    return 0;
}

static int
test_pick_result_to_ptbl_nonempty(void)
{
    printf("=== Test 10: result_to_ptbl non-empty ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    if (!root || !s1) FAIL("scene setup failed");
    bsg_node_add_child(root, s1);

    struct bsg_pick_result *res = bsg_pick_result_create();
    struct bu_ptbl out = BU_PTBL_INIT_ZERO;
    if (!append_record(res, s1, v, 5, 6, "/picked", 3.0)) FAIL("append_record");

    bsg_pick_result_to_ptbl(res, &out);
    if (BU_PTBL_LEN(&out) != 1) FAIL("ptbl should have one node");
    if ((bsg_node *)BU_PTBL_GET(&out, 0) != s1) FAIL("ptbl should reference s1");

    bu_ptbl_free(&out);
    bsg_pick_result_free(res);
    bsg_shape_destroy(s1);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("result_to_ptbl non-empty");
    return 0;
}

static int
test_pick_ray_null_view(void)
{
    printf("=== Test 11: pick_ray NULL view ===\n");
    point_t o = VINIT_ZERO;
    vect_t d = {0.0, 0.0, 1.0};
    struct bsg_pick_result *res = bsg_pick_ray(NULL, o, d, BSG_PICK_INCLUDE_SCENE);
    if (res) FAIL("pick_ray(NULL) should return NULL");
    PASS("pick_ray NULL view");
    return 0;
}

static int
test_pick_semantic_path_basic(void)
{
    printf("=== Test 12: pick_semantic_path basic ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *g1 = bsg_group_create(v);
    bsg_node *g2 = bsg_group_create(v);
    if (!v || !root || !g1 || !g2) FAIL("scene setup failed");

    bsg_node_set_draw_intent(g1, bsg_draw_intent_create("hull/main", BSG_DRAW_MODE_WIRE));
    bsg_node_set_draw_intent(g2, bsg_draw_intent_create("engine/aux", BSG_DRAW_MODE_SHADED));
    bsg_node_add_child(root, g1);
    bsg_node_add_child(root, g2);

    struct bsg_pick_result *res = bsg_pick_semantic_path(v, "hull/*");
    if (!res) FAIL("pick_semantic_path returned NULL");
    if (bsg_pick_result_count(res) != 1) FAIL("semantic path should match exactly one group");

    struct bsg_pick_record *pr = bsg_pick_result_get(res, 0);
    if (!pr) FAIL("missing semantic pick record");
    if (!BU_STR_EQUAL(bu_vls_cstr(&pr->pr_source_path), "hull/main"))
	FAIL("semantic path source mismatch");
    if (!BU_STR_EQUAL(bu_vls_cstr(&pr->pr_instance_path), "hull/main"))
	FAIL("instance path should mirror semantic source path");
    if (pr->pr_primitive_id != -1 || pr->pr_subelement_id != -1)
	FAIL("default primitive/subelement ids should be -1");

    bsg_pick_result_free(res);
    bsg_group_destroy(g1);
    bsg_group_destroy(g2);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("pick_semantic_path basic");
    return 0;
}


/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);
    (void)argc;

    int failures = 0;
    failures += test_result_lifecycle();
    failures += test_pick_point_null_view();
    failures += test_pick_rect_null_view();
    failures += test_pick_empty_scene();
    failures += test_to_ptbl_empty();
    failures += test_pick_apply_empty();
    failures += test_pick_nearest_null();
    failures += test_pick_op_values();
    failures += test_pick_apply_nonempty();
    failures += test_pick_result_to_ptbl_nonempty();
    failures += test_pick_ray_null_view();
    failures += test_pick_semantic_path_basic();

    if (failures) {
	printf("FAILED: %d test(s) failed\n", failures);
	return 1;
    }
    printf("ALL TESTS PASSED\n");
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
