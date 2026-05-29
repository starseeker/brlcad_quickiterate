/*          T E S T _ R E N D E R _ I T E M . C
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
/** @file libbsg/tests/test_render_item.c
 *
 * Phase D5 unit tests: render items, phase classification, backend adapter
 * dispatch, and transform accumulation via bsg_render_request_execute.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/vls.h"
#include "vmath.h"
#include "bn/mat.h"
#include "bn/tol.h"

#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/node.h"
#include "bsg/node_shape.h"
#include "bsg/node_transform.h"
#include "bsg/hud.h"
#include "bsg/render.h"
#include "bsg/render_item.h"
#include "bsg/backend_adapter.h"
#include "bsg/payload.h"
#include "bsg/appearance.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


/* ------------------------------------------------------------------ */
/* View helpers (same pattern as test_pick.c)                          */
/* ------------------------------------------------------------------ */

static struct bsg_view *
_make_view(void)
{
    struct bsg_view *v;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "ri_test_view");
    return v;
}

static void
_free_view(struct bsg_view *v)
{
    if (!v) return;
    bsg_view_free(v);
    bu_free(v, "ri_test_view");
}


/* ------------------------------------------------------------------ */
/* Test 1: bsg_render_item_create / free                               */
/* ------------------------------------------------------------------ */

static int
test_item_create_free(void)
{
    printf("=== Test 1: item_create_free ===\n");

    struct bsg_render_item *item = bsg_render_item_create();
    if (!item) FAIL("bsg_render_item_create returned NULL");
    if (item->node         != NULL)              FAIL("node should be NULL");
    if (!NEAR_EQUAL(item->transparency, 1.0, BN_TOL_DIST))               FAIL("default transparency should be 1.0");
    if (item->phase        != BSG_RENDER_PHASE_OPAQUE) FAIL("default phase should be OPAQUE");

    bsg_render_item_free(item);
    bsg_render_item_free(NULL);  /* must not crash */

    PASS("item_create_free");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 2: bsg_render_phase enum ordering                              */
/* ------------------------------------------------------------------ */

static int
test_phase_enum_values(void)
{
    printf("=== Test 2: phase_enum_values ===\n");

    if (BSG_RENDER_PHASE_OPAQUE      != 0) FAIL("OPAQUE must be 0");
    if (BSG_RENDER_PHASE_TRANSPARENT != 1) FAIL("TRANSPARENT must be 1");
    if (BSG_RENDER_PHASE_OVERLAY     != 2) FAIL("OVERLAY must be 2");
    if (BSG_RENDER_PHASE_HUD         != 3) FAIL("HUD must be 3");
    if (BSG_RENDER_PHASE_COUNT       != 4) FAIL("COUNT must be 4");

    PASS("phase_enum_values");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 3: execute on empty tree returns 0                             */
/* ------------------------------------------------------------------ */

static int
test_empty_tree(void)
{
    printf("=== Test 3: empty_tree ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    int n = bsg_render_request_execute(req);
    if (n != 0) FAIL("empty tree should return 0");

    bsg_render_request_destroy(req);
    bsg_scene_root_destroy(root);
    _free_view(v);
    PASS("empty_tree");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 4: visible-only flag skips DOWN shapes                         */
/* ------------------------------------------------------------------ */

static int
test_visible_only(void)
{
    printf("=== Test 4: visible_only ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);
    bsg_node_add_child(root, s1);
    bsg_node_add_child(root, s2);

    s1->s_flag = UP;
    s2->s_flag = DOWN;

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_VISIBLE_ONLY;

    int n = bsg_render_request_execute(req);
    if (n != 1) FAIL("only 1 UP shape should be counted");

    bsg_render_request_destroy(req);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    _free_view(v);
    PASS("visible_only");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 5: BSG_RENDER_FLAG_COLLECT_ITEMS populates req->items          */
/* ------------------------------------------------------------------ */

static int
test_collect_items(void)
{
    printf("=== Test 5: collect_items ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);
    bsg_node_add_child(root, s1);
    bsg_node_add_child(root, s2);
    s1->s_flag = UP;
    s2->s_flag = UP;

    struct bu_ptbl items;
    bu_ptbl_init(&items, 4, "test items");

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_COLLECT_ITEMS;
    req->items = &items;

    int n = bsg_render_request_execute(req);
    if (n != 2) FAIL("should collect 2 items");
    if ((size_t)BU_PTBL_LEN(&items) != 2) FAIL("items table should have 2 entries");

    /* Verify items point to the correct nodes */
    int found1 = 0, found2 = 0;
    for (size_t i = 0; i < BU_PTBL_LEN(&items); i++) {
	struct bsg_render_item *item =
	    (struct bsg_render_item *)BU_PTBL_GET(&items, i);
	if (item->node == s1) found1 = 1;
	if (item->node == s2) found2 = 1;
	bsg_render_item_free(item);
    }
    if (!found1) FAIL("item for s1 not found");
    if (!found2) FAIL("item for s2 not found");

    bu_ptbl_free(&items);
    bsg_render_request_destroy(req);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    _free_view(v);
    PASS("collect_items");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 6: transparent shape lands in TRANSPARENT phase                */
/* ------------------------------------------------------------------ */

static int
test_transparent_phase(void)
{
    printf("=== Test 6: transparent_phase ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s = bsg_shape_create(v);
    bsg_node_add_child(root, s);
    s->s_flag = UP;

    /* Give the shape a settings block with transparency < 1 */
    struct bsg_obj_settings *os;
    BU_ALLOC(os, struct bsg_obj_settings);
    *os = (struct bsg_obj_settings)BV_OBJ_SETTINGS_INIT;
    os->transparency = 0.5;
    s->s_os = os;

    struct bu_ptbl items;
    bu_ptbl_init(&items, 4, "trans items");

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_COLLECT_ITEMS;
    req->items = &items;

    bsg_render_request_execute(req);
    if (BU_PTBL_LEN(&items) != 1) FAIL("should collect 1 item");

    struct bsg_render_item *item =
	(struct bsg_render_item *)BU_PTBL_GET(&items, 0);
    if (item->phase != BSG_RENDER_PHASE_TRANSPARENT)
	FAIL("transparent shape should be in TRANSPARENT phase");
    if (!NEAR_EQUAL(item->transparency, 0.5, BN_TOL_DIST))
	FAIL("transparency value not preserved");

    bsg_render_item_free(item);
    bu_ptbl_free(&items);
    bsg_render_request_destroy(req);
    s->s_os = NULL;
    bu_free(os, "test os");
    bsg_shape_destroy(s);
    bsg_scene_root_destroy(root);
    _free_view(v);
    PASS("transparent_phase");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 7: adapter draw callback is invoked once per visible shape     */
/* ------------------------------------------------------------------ */

static int g_draw_count = 0;

static void
_test_draw_cb(void *dmp, const struct bsg_render_item *item)
{
    (void)dmp;
    (void)item;
    g_draw_count++;
}

static int
test_adapter_draw(void)
{
    printf("=== Test 7: adapter_draw ===\n");
    g_draw_count = 0;

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);
    bsg_node *s3 = bsg_shape_create(v);
    bsg_node_add_child(root, s1);
    bsg_node_add_child(root, s2);
    bsg_node_add_child(root, s3);
    s1->s_flag = UP;
    s2->s_flag = DOWN;   /* hidden */
    s3->s_flag = UP;

    struct bsg_backend_adapter adapter;
    memset(&adapter, 0, sizeof(adapter));
    adapter.draw = _test_draw_cb;

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags   = BSG_RENDER_FLAG_VISIBLE_ONLY;
    req->adapter = &adapter;

    int n = bsg_render_request_execute(req);
    if (n != 2)          FAIL("should dispatch 2 visible shapes");
    if (g_draw_count != 2) FAIL("draw callback should be called twice");

    bsg_render_request_destroy(req);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_shape_destroy(s3);
    bsg_scene_root_destroy(root);
    _free_view(v);
    PASS("adapter_draw");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 8: adapter prepare + draw both invoked                         */
/* ------------------------------------------------------------------ */

static int g_prepare_count = 0;
static int g_draw2_count   = 0;

static int
_test_prepare_cb(void *dmp, const struct bsg_render_item *item)
{
    (void)dmp;
    (void)item;
    g_prepare_count++;
    return 1;
}

static void
_test_draw2_cb(void *dmp, const struct bsg_render_item *item)
{
    (void)dmp;
    (void)item;
    g_draw2_count++;
}

static int
test_adapter_prepare_draw(void)
{
    printf("=== Test 8: adapter_prepare_draw ===\n");
    g_prepare_count = 0;
    g_draw2_count   = 0;

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s = bsg_shape_create(v);
    bsg_node_add_child(root, s);
    s->s_flag = UP;

    struct bsg_backend_adapter adapter;
    memset(&adapter, 0, sizeof(adapter));
    adapter.prepare = _test_prepare_cb;
    adapter.draw    = _test_draw2_cb;

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags   = BSG_RENDER_FLAG_VISIBLE_ONLY;
    req->adapter = &adapter;

    bsg_render_request_execute(req);
    if (g_prepare_count != 1) FAIL("prepare should be called once");
    if (g_draw2_count   != 1) FAIL("draw should be called once");

    bsg_render_request_destroy(req);
    bsg_shape_destroy(s);
    bsg_scene_root_destroy(root);
    _free_view(v);
    PASS("adapter_prepare_draw");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 9: items in collect mode reflect model_mat identity by default */
/* ------------------------------------------------------------------ */

static int
test_item_model_mat_identity(void)
{
    printf("=== Test 9: item_model_mat_identity ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s = bsg_shape_create(v);
    bsg_node_add_child(root, s);
    s->s_flag = UP;

    struct bu_ptbl items;
    bu_ptbl_init(&items, 4, "mat items");

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_COLLECT_ITEMS;
    req->items = &items;

    bsg_render_request_execute(req);
    if (BU_PTBL_LEN(&items) != 1) FAIL("should have 1 item");

    struct bsg_render_item *item =
	(struct bsg_render_item *)BU_PTBL_GET(&items, 0);

    mat_t eye;
    MAT_IDN(eye);
    struct bn_tol tol = BN_TOL_INIT_TOL;
    if (!bn_mat_is_equal(item->model_mat, eye, &tol))
	FAIL("model_mat should be identity when no transform node present");

    bsg_render_item_free(item);
    bu_ptbl_free(&items);
    bsg_render_request_destroy(req);
    bsg_shape_destroy(s);
    bsg_scene_root_destroy(root);
    _free_view(v);
    PASS("item_model_mat_identity");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 10: NULL request returns -1                                    */
/* ------------------------------------------------------------------ */

static int
test_null_request(void)
{
    printf("=== Test 10: null_request ===\n");
    int n = bsg_render_request_execute(NULL);
    if (n != -1) FAIL("execute(NULL) should return -1");
    PASS("null_request");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 11: sorted alpha orders transparent items back-to-front        */
/* ------------------------------------------------------------------ */

static int
test_sorted_alpha(void)
{
    printf("=== Test 11: sorted_alpha ===\n");

    struct bsg_view *v = _make_view();
    MAT_IDN(v->gv_model2view);

    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *xf1 = bsg_transform_create(v);
    bsg_node *xf2 = bsg_transform_create(v);
    bsg_node *xf3 = bsg_transform_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);
    bsg_node *s3 = bsg_shape_create(v);
    bsg_node_add_child(root, xf1);
    bsg_node_add_child(root, xf2);
    bsg_node_add_child(root, xf3);
    bsg_node_add_child(xf1, s1);
    bsg_node_add_child(xf2, s2);
    bsg_node_add_child(xf3, s3);

    s1->s_flag = UP;
    s2->s_flag = UP;
    s3->s_flag = UP;

    struct bsg_obj_settings os1 = BV_OBJ_SETTINGS_INIT;
    struct bsg_obj_settings os2 = BV_OBJ_SETTINGS_INIT;
    struct bsg_obj_settings os3 = BV_OBJ_SETTINGS_INIT;
    os1.transparency = 0.5;
    os2.transparency = 0.5;
    os3.transparency = 0.5;
    s1->s_os = &os1;
    s2->s_os = &os2;
    s3->s_os = &os3;

    mat_t m1, m2, m3;
    MAT_IDN(m1);
    MAT_IDN(m2);
    MAT_IDN(m3);
    /* Use three distinct negative view-space Z depths so the expected
     * back-to-front order is unambiguous: far (-5), middle (-3), near (-1). */
    MAT_DELTAS(m1, 0.0, 0.0, -1.0);
    MAT_DELTAS(m2, 0.0, 0.0, -5.0);
    MAT_DELTAS(m3, 0.0, 0.0, -3.0);
    bsg_transform_set_matrix(xf1, m1);
    bsg_transform_set_matrix(xf2, m2);
    bsg_transform_set_matrix(xf3, m3);

    struct bu_ptbl items;
    bu_ptbl_init(&items, 4, "sorted alpha items");

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_COLLECT_ITEMS | BSG_RENDER_FLAG_SORTED_ALPHA;
    req->items = &items;

    int n = bsg_render_request_execute(req);
    if (n != 3) FAIL("should collect 3 transparent items");
    if (BU_PTBL_LEN(&items) != 3) FAIL("items table should have 3 entries");

    struct bsg_render_item *i0 =
	(struct bsg_render_item *)BU_PTBL_GET(&items, 0);
    struct bsg_render_item *i1 =
	(struct bsg_render_item *)BU_PTBL_GET(&items, 1);
    struct bsg_render_item *i2 =
	(struct bsg_render_item *)BU_PTBL_GET(&items, 2);

    /* With the identity gv_model2view, points in front of the camera have
     * negative Z.  Back-to-front sorting therefore expects the items table
     * to contain Z=-5 at index 0, Z=-3 at index 1, and Z=-1 at index 2. */
    if (i0->node != s2 || i1->node != s3 || i2->node != s1)
	FAIL("transparent items should sort back-to-front by transformed depth");
    if (!(i0->sort_key > i1->sort_key && i1->sort_key > i2->sort_key))
	FAIL("sort keys should be in descending order");

    for (size_t i = 0; i < BU_PTBL_LEN(&items); i++) {
	struct bsg_render_item *item =
	    (struct bsg_render_item *)BU_PTBL_GET(&items, i);
	bsg_render_item_free(item);
    }
    bu_ptbl_free(&items);
    bsg_render_request_destroy(req);
    s1->s_os = NULL;
    s2->s_os = NULL;
    s3->s_os = NULL;
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_shape_destroy(s3);
    bsg_transform_destroy(xf1);
    bsg_transform_destroy(xf2);
    bsg_transform_destroy(xf3);
    bsg_scene_root_destroy(root);
    _free_view(v);
    PASS("sorted_alpha");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 12: HUD pass sorts by hud sort_order                           */
/* ------------------------------------------------------------------ */

static int
test_hud_phase_sort(void)
{
    printf("=== Test 12: hud_phase_sort ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_hud_root_create(v);
    if (!root) FAIL("failed to create HUD root");

    bsg_node *c0 = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_CENTER_DOT);
    bsg_node *c1 = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_MODEL_AXES);
    bsg_node *c2 = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_VIEW_PARAMS);
    if (!c0 || !c1 || !c2) FAIL("missing HUD children");

    c0->s_flag = UP;
    c1->s_flag = UP;
    c2->s_flag = UP;

    struct bsg_hud_node_meta *m0 = bsg_hud_node_get_meta(c0);
    struct bsg_hud_node_meta *m1 = bsg_hud_node_get_meta(c1);
    struct bsg_hud_node_meta *m2 = bsg_hud_node_get_meta(c2);
    if (!m0 || !m1 || !m2) FAIL("missing HUD metadata");
    m0->sort_order = 30;
    m1->sort_order = 10;
    m2->sort_order = 20;

    struct bu_ptbl items;
    bu_ptbl_init(&items, 4, "hud phase items");

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_COLLECT_ITEMS | BSG_RENDER_FLAG_VISIBLE_ONLY | BSG_RENDER_FLAG_HUD_PASS;
    req->items = &items;

    int n = bsg_render_request_execute(req);
    if (n != 3) FAIL("should collect 3 HUD items");
    if (BU_PTBL_LEN(&items) != 3) FAIL("items table should have 3 entries");

    struct bsg_render_item *i0 = (struct bsg_render_item *)BU_PTBL_GET(&items, 0);
    struct bsg_render_item *i1 = (struct bsg_render_item *)BU_PTBL_GET(&items, 1);
    struct bsg_render_item *i2 = (struct bsg_render_item *)BU_PTBL_GET(&items, 2);

    if (i0->node != c1 || i1->node != c2 || i2->node != c0)
	FAIL("HUD items should sort in ascending sort_order");
    if (!(i0->sort_key < i1->sort_key && i1->sort_key < i2->sort_key))
	FAIL("HUD sort keys should be ascending");

    for (size_t i = 0; i < BU_PTBL_LEN(&items); i++) {
	struct bsg_render_item *item =
	    (struct bsg_render_item *)BU_PTBL_GET(&items, i);
	bsg_render_item_free(item);
    }
    bu_ptbl_free(&items);
    bsg_render_request_destroy(req);
    bsg_hud_root_destroy(v);
    _free_view(v);
    PASS("hud_phase_sort");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test 13: capabilities callback is queried during execute            */
/* ------------------------------------------------------------------ */

static int g_caps_count = 0;

static unsigned int
_test_caps_cb(void *UNUSED(dmp))
{
    g_caps_count++;
    return BSG_ADAPTER_CAP_SORTED_ALPHA | BSG_ADAPTER_CAP_TRANSPARENCY;
}

static int
test_adapter_capabilities(void)
{
    printf("=== Test 13: adapter_capabilities ===\n");
    g_caps_count = 0;

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s = bsg_shape_create(v);
    bsg_node_add_child(root, s);
    s->s_flag = UP;

    struct bsg_backend_adapter adapter;
    memset(&adapter, 0, sizeof(adapter));
    adapter.capabilities = _test_caps_cb;

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_VISIBLE_ONLY | BSG_RENDER_FLAG_SORTED_ALPHA;
    req->adapter = &adapter;

    int n = bsg_render_request_execute(req);
    if (n != 1) FAIL("should dispatch one shape");
    if (g_caps_count != 1) FAIL("capabilities should be called once per execute");

    bsg_render_request_destroy(req);
    bsg_shape_destroy(s);
    bsg_scene_root_destroy(root);
    _free_view(v);
    PASS("adapter_capabilities");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test 14: invalidate/free lifecycle callbacks are callable           */
/* ------------------------------------------------------------------ */

static int g_invalidate_count = 0;
static int g_free_count = 0;

static void
_test_invalidate_cb(void *UNUSED(dmp), const struct bsg_render_item *UNUSED(item),
		    unsigned int UNUSED(reason_mask))
{
    g_invalidate_count++;
}

static void
_test_free_cb(void *UNUSED(dmp), const struct bsg_render_item *UNUSED(item))
{
    g_free_count++;
}

static int
test_adapter_invalidate_free(void)
{
    printf("=== Test 14: adapter_invalidate_free ===\n");
    g_invalidate_count = 0;
    g_free_count = 0;

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s = bsg_shape_create(v);
    bsg_node_add_child(root, s);
    s->s_flag = UP;

    struct bu_ptbl items;
    bu_ptbl_init(&items, 4, "invalidate_free items");
    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_COLLECT_ITEMS | BSG_RENDER_FLAG_VISIBLE_ONLY;
    req->items = &items;
    int n = bsg_render_request_execute(req);
    if (n != 1) FAIL("should collect one shape item");

    struct bsg_backend_adapter adapter;
    memset(&adapter, 0, sizeof(adapter));
    adapter.invalidate = _test_invalidate_cb;
    adapter.free = _test_free_cb;

    for (size_t i = 0; i < BU_PTBL_LEN(&items); i++) {
	struct bsg_render_item *item =
	    (struct bsg_render_item *)BU_PTBL_GET(&items, i);
	adapter.invalidate(NULL, item, BSG_INVALIDATE_ALL);
	adapter.free(NULL, item);
	bsg_render_item_free(item);
    }
    if (g_invalidate_count != 1) FAIL("invalidate callback should be called once");
    if (g_free_count != 1) FAIL("free callback should be called once");

    bu_ptbl_free(&items);
    bsg_render_request_destroy(req);
    bsg_shape_destroy(s);
    bsg_scene_root_destroy(root);
    _free_view(v);
    PASS("adapter_invalidate_free");
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

    failures += test_item_create_free();
    failures += test_phase_enum_values();
    failures += test_empty_tree();
    failures += test_visible_only();
    failures += test_collect_items();
    failures += test_transparent_phase();
    failures += test_adapter_draw();
    failures += test_adapter_prepare_draw();
    failures += test_item_model_mat_identity();
    failures += test_null_request();
    failures += test_sorted_alpha();
    failures += test_hud_phase_sort();
    failures += test_adapter_capabilities();
    failures += test_adapter_invalidate_free();

    if (failures == 0)
	printf("RESULT: all Phase D5 render-item tests PASSED\n");
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
