/*            T E S T _ R E N D E R . C
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
/** @file libbsg/tests/test_render.c
 *
 * Phase 8 unit tests: render-request pre-render traversal.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/node.h"
#include "bsg/node_shape.h"
#include "bsg/node_transform.h"
#include "bsg/overlay.h"
#include "bsg/render.h"
#include "bsg/render_item.h"
#include "bsg/node_private.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


static struct bsg_view *
make_view(void)
{
    struct bsg_view *v;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "render_test_view");
    return v;
}

static void
free_view(struct bsg_view *v)
{
    if (!v) return;
    bsg_view_free(v);
    bu_free(v, "render_test_view");
}


/* ------------------------------------------------------------------ */
/* Test 1: create / destroy                                             */
/* ------------------------------------------------------------------ */

static int
test_create_destroy(void)
{
    printf("=== Test 1: create_destroy ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    if (!req) FAIL("bsg_render_request_create returned NULL");

    if (req->view != v)    FAIL("view pointer mismatch");
    if (req->root != root) FAIL("root pointer mismatch");
    if (req->dmp  != NULL) FAIL("dmp should be NULL");

    bsg_render_request_destroy(req);
    bsg_render_request_destroy(NULL);  /* must not crash */

    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("create_destroy");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 2: execute on empty subtree returns 0                          */
/* ------------------------------------------------------------------ */

static int
test_empty_subtree(void)
{
    printf("=== Test 2: empty_subtree ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);

    int n = bsg_render_request_execute(req);
    if (n != 0) FAIL("empty subtree should dispatch 0");

    bsg_render_request_destroy(req);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("empty_subtree");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 3: visible-only flag skips DOWN shapes                         */
/* ------------------------------------------------------------------ */

static int
test_visible_only(void)
{
    printf("=== Test 3: visible_only ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);

    /* Attach shapes to root so bsg_visit can reach them */
    bsg_node_add_child(root, s1);
    bsg_node_add_child(root, s2);

    s1->s_flag = UP;    /* visible */
    s2->s_flag = DOWN;  /* hidden */

    struct bsg_render_request *req =
	bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_VISIBLE_ONLY;

    int n = bsg_render_request_execute(req);
    /* bsg_render_request_execute returns the count of shapes that passed all
     * active filters, regardless of whether PAYLOAD_DISPATCH is set.  With
     * VISIBLE_ONLY: s1 (UP) passes, s2 (DOWN) is skipped — expect count = 1. */
    if (n != 1) FAIL("only 1 visible shape should be counted");

    bsg_render_request_destroy(req);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("visible_only");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 4: NULL request returns -1                                     */
/* ------------------------------------------------------------------ */

static int
test_null_request(void)
{
    printf("=== Test 4: null_request ===\n");

    int n = bsg_render_request_execute(NULL);
    if (n != -1) FAIL("execute(NULL) should return -1");

    PASS("null_request");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 5: transparency back-to-front sort                             */
/* ------------------------------------------------------------------ */

/* Allocate an s_os carrying the given transparency so bsg_appearance_resolve
 * classifies the shape into the TRANSPARENT phase. */
static void
_make_transparent(bsg_node *s, fastf_t transparency)
{
    struct bsg_obj_settings *os;
    BU_ALLOC(os, struct bsg_obj_settings);
    memset(os, 0, sizeof(struct bsg_obj_settings));
    os->transparency = transparency;
    os->s_line_width = 1;
    s->s_os = os;
}

static void
_free_os(bsg_node *s)
{
    if (s && s->s_os) {
	bu_free(s->s_os, "os");
	s->s_os = NULL;
    }
}

static int
test_transparency_sort(void)
{
    printf("=== Test 5: transparency_sort ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);

    /* Two transparent shapes at different model depths, attached via
     * transform nodes.  The view's model2view is identity, so the shapes'
     * view-space Z equals their model translation Z.  In BRL-CAD view space
     * more-negative Z is farther from the camera, so the Z=-10 shape must be
     * drawn before the Z=+10 shape (back-to-front). */
    mat_t mfar, mnear;
    MAT_IDN(mfar);
    MAT_IDN(mnear);
    mfar[MDZ]  = -10.0;   /* translation Z = -10 (farther) */
    mnear[MDZ] = 10.0;    /* translation Z = +10 (nearer) */

    bsg_node *tnear = bsg_transform_create(v);
    bsg_node *tfar  = bsg_transform_create(v);
    bsg_transform_set_matrix(tnear, mnear);
    bsg_transform_set_matrix(tfar, mfar);

    bsg_node *snear = bsg_shape_create(v);
    bsg_node *sfar  = bsg_shape_create(v);
    _make_transparent(snear, 0.5);
    _make_transparent(sfar, 0.5);

    bsg_node_add_child(tnear, snear);
    bsg_node_add_child(tfar, sfar);

    /* Insert near first, far second — the sort must reorder them. */
    bsg_node_add_child(root, tnear);
    bsg_node_add_child(root, tfar);

    struct bu_ptbl items = BU_PTBL_INIT_ZERO;
    bu_ptbl_init(&items, 8, "collected items");

    struct bsg_render_request *req = bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_VISIBLE_ONLY | BSG_RENDER_FLAG_COLLECT_ITEMS
		 | BSG_RENDER_FLAG_SORTED_ALPHA;
    req->items = &items;

    int n = bsg_render_request_execute(req);
    if (n != 2) FAIL("expected 2 transparent items collected");
    if (BU_PTBL_LEN(&items) != 2) FAIL("items table should hold 2 entries");

    struct bsg_render_item *i0 = (struct bsg_render_item *)BU_PTBL_GET(&items, 0);
    struct bsg_render_item *i1 = (struct bsg_render_item *)BU_PTBL_GET(&items, 1);
    if (i0->phase != BSG_RENDER_PHASE_TRANSPARENT)
	FAIL("first item should be in the transparent phase");
    if (i0->node != sfar)
	FAIL("farther shape (Z=-10) must be drawn first (back-to-front)");
    if (i1->node != snear)
	FAIL("nearer shape (Z=+10) must be drawn second");

    for (size_t i = 0; i < BU_PTBL_LEN(&items); i++)
	bsg_render_item_free((struct bsg_render_item *)BU_PTBL_GET(&items, i));
    bu_ptbl_free(&items);
    bsg_render_request_destroy(req);

    _free_os(snear);
    _free_os(sfar);
    bsg_shape_destroy(snear);
    bsg_shape_destroy(sfar);
    bsg_transform_destroy(tnear);
    bsg_transform_destroy(tfar);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("transparency_sort");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 6: overlay ordering                                            */
/* ------------------------------------------------------------------ */

static int
test_overlay_ordering(void)
{
    printf("=== Test 6: overlay_ordering ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);

    /* Three overlay shapes registered with distinct (ordering, sort_order)
     * keys.  The overlay sort key is ordering*1000 + sort_order and the
     * bucket is sorted ascending, so the expected draw order is b, a, c. */
    bsg_node *oa = bsg_shape_create(v);  /* SCREEN(1)*1000 + 5 = 1005 */
    bsg_node *ob = bsg_shape_create(v);  /* MODEL(0)*1000  + 2 =    2 */
    bsg_node *oc = bsg_shape_create(v);  /* XRAY(2)*1000   + 0 = 2000 */

    if (!bsg_overlay_register_owner(oa, v, BSG_OVERLAY_ROLE_SCREEN,
	    BSG_OVERLAY_CLASS_MEASURE, BSG_OVERLAY_LC_PERSISTENT,
	    BSG_OVERLAY_ORDER_SCREEN, NULL, 5))
	FAIL("register overlay a");
    if (!bsg_overlay_register_owner(ob, v, BSG_OVERLAY_ROLE_MODEL,
	    BSG_OVERLAY_CLASS_MEASURE, BSG_OVERLAY_LC_PERSISTENT,
	    BSG_OVERLAY_ORDER_MODEL, NULL, 2))
	FAIL("register overlay b");
    if (!bsg_overlay_register_owner(oc, v, BSG_OVERLAY_ROLE_XRAY,
	    BSG_OVERLAY_CLASS_MEASURE, BSG_OVERLAY_LC_PERSISTENT,
	    BSG_OVERLAY_ORDER_XRAY, NULL, 0))
	FAIL("register overlay c");

    /* Insert in the unsorted order a, b, c. */
    bsg_node_add_child(root, oa);
    bsg_node_add_child(root, ob);
    bsg_node_add_child(root, oc);

    struct bu_ptbl items = BU_PTBL_INIT_ZERO;
    bu_ptbl_init(&items, 8, "collected items");

    struct bsg_render_request *req = bsg_render_request_create(v, root, NULL);
    req->flags = BSG_RENDER_FLAG_VISIBLE_ONLY | BSG_RENDER_FLAG_COLLECT_ITEMS;
    req->items = &items;

    int n = bsg_render_request_execute(req);
    if (n != 3) FAIL("expected 3 overlay items collected");
    if (BU_PTBL_LEN(&items) != 3) FAIL("items table should hold 3 entries");

    struct bsg_render_item *i0 = (struct bsg_render_item *)BU_PTBL_GET(&items, 0);
    struct bsg_render_item *i1 = (struct bsg_render_item *)BU_PTBL_GET(&items, 1);
    struct bsg_render_item *i2 = (struct bsg_render_item *)BU_PTBL_GET(&items, 2);
    if (i0->phase != BSG_RENDER_PHASE_OVERLAY)
	FAIL("collected items should be in the overlay phase");
    if (i0->node != ob) FAIL("overlay b (key 2) should sort first");
    if (i1->node != oa) FAIL("overlay a (key 1005) should sort second");
    if (i2->node != oc) FAIL("overlay c (key 2000) should sort third");

    for (size_t i = 0; i < BU_PTBL_LEN(&items); i++)
	bsg_render_item_free((struct bsg_render_item *)BU_PTBL_GET(&items, i));
    bu_ptbl_free(&items);
    bsg_render_request_destroy(req);

    bsg_node_destroy(oa);
    bsg_node_destroy(ob);
    bsg_node_destroy(oc);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("overlay_ordering");
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

    failures += test_create_destroy();
    failures += test_empty_subtree();
    failures += test_visible_only();
    failures += test_null_request();
    failures += test_transparency_sort();
    failures += test_overlay_ordering();

    if (failures == 0)
	printf("RESULT: all Phase 8 render tests PASSED\n");
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
