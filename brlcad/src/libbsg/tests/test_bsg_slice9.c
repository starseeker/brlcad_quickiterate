/*           T E S T _ B S G _ S L I C E 9 . C
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
/** @file libbsg/tests/test_bsg_slice9.c
 *
 * Slice 9 (bv_scene_obj_migrate) tests: material/appearance/settings.
 *
 * These tests verify that the "ordinary consumer" code paths in libbv that
 * previously wrote directly to legacy fields (s_color, s_soldash, s_arrow)
 * now route through BSG material/appearance APIs.
 *
 *   9A - bv_view_obj_create with arrow flag routes through BSG appearance.
 *        Create an arrow overlay object and verify that bsg_node_draw_arrows()
 *        returns 1.
 *
 *   9B - bv_view_obj_set_color routes through BSG material.
 *        Set a colour via bv_view_obj_set_color() and verify bsg_node_material_get()
 *        returns the same RGB.
 *
 *   9C - bv_obj_sync routes material and appearance through BSG APIs.
 *        Set a non-default colour and draw_arrows on a source object; sync to a
 *        dest object; verify dest has the same material/appearance via BSG
 *        getters.
 *
 *   9D - bv_view_obj_labels_sync routes color through BSG material.
 *        Build a label data state with a specific colour; call
 *        bv_view_obj_labels_sync(); verify the child scene objects' material
 *        colour matches via bsg_node_material_get().
 *
 *   9E - NULL-argument safety for paths exercised above.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/appearance.h"
#include "bsg/material.h"
#include "bsg/node.h"
#include "bsg/node_shape.h"
#include "bsg/util.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

static struct bview *
_make_view(void)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "slice9_view");
    return v;
}

static void
_free_view(struct bview *v)
{
    if (!v) return;
    bv_free(v);
    bu_free(v, "slice9 view");
}


/* ====================================================================
 * Test 9A: bv_view_obj_create arrow flag → BSG appearance
 * ==================================================================== */

static int
test_arrow_create_routes_bsg(void)
{
    printf("=== Test 9A: bv_view_obj_create arrow → BSG appearance ===\n");

    struct bview *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	_free_view(v);
	FAIL("bsg_scene_root_create");
    }

    /* bv_view_obj_arrow_create internally sets opts.arrow = 1, which was
     * previously written to s->s_arrow directly.  Slice 9 routes this
     * through bsg_node_set_draw_arrows() instead. */
    struct bv_scene_obj *obj = bv_view_obj_arrow_create(v, "test_arrow_9a", 1);
    if (!obj) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bv_view_obj_arrow_create returned NULL");
    }

    /* BSG appearance should report draw_arrows == 1. */
    int da = bsg_node_draw_arrows((const bsg_node *)obj);
    if (!da) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bsg_node_draw_arrows returned 0 after arrow create");
    }
    PASS("bsg_node_draw_arrows == 1 after arrow create");

    /* Legacy field should also be set for backward compat. */
    if (!obj->s_arrow) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("legacy s_arrow not set after BSG path");
    }
    PASS("legacy s_arrow == 1 after BSG path");

    bsg_scene_root_destroy(root);
    _free_view(v);
    return 0;
}


/* ====================================================================
 * Test 9B: bv_view_obj_set_color → BSG material
 * ==================================================================== */

static int
test_set_color_routes_bsg(void)
{
    printf("=== Test 9B: bv_view_obj_set_color → BSG material ===\n");

    struct bview *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	_free_view(v);
	FAIL("bsg_scene_root_create");
    }

    struct bv_scene_obj *obj = bv_view_obj_lines_create(v, "test_color_9b", 1);
    if (!obj) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bv_view_obj_lines_create returned NULL");
    }

    bv_view_obj_set_color(obj, 77, 88, 99);

    struct bsg_material m;
    bsg_material_init(&m);
    (void)bsg_node_material_get((const bsg_node *)obj, &m);

    if (m.rgba[0] != 77 || m.rgba[1] != 88 || m.rgba[2] != 99) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("BSG material rgba not updated by bv_view_obj_set_color");
    }
    PASS("BSG material rgba matches bv_view_obj_set_color");

    bsg_scene_root_destroy(root);
    _free_view(v);
    return 0;
}


/* ====================================================================
 * Test 9C: bv_obj_sync routes material and appearance through BSG APIs
 * ==================================================================== */

static int
test_sync_routes_bsg(void)
{
    printf("=== Test 9C: bv_obj_sync → BSG material/appearance ===\n");

    struct bview *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	_free_view(v);
	FAIL("bsg_scene_root_create");
    }

    struct bv_scene_obj *src = bv_view_obj_lines_create(v, "test_sync_src_9c", 1);
    struct bv_scene_obj *dst = bv_view_obj_lines_create(v, "test_sync_dst_9c", 1);
    if (!src || !dst) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bv_view_obj_lines_create returned NULL");
    }

    /* Set a distinct colour and enable arrows on the source. */
    bv_view_obj_set_color(src, 10, 20, 30);
    bsg_node_set_draw_arrows((bsg_node *)src, 1);

    /* Sync source → dest. */
    bv_obj_sync(dst, src);

    /* Verify material was synced via BSG. */
    struct bsg_material dm;
    bsg_material_init(&dm);
    (void)bsg_node_material_get((const bsg_node *)dst, &dm);
    if (dm.rgba[0] != 10 || dm.rgba[1] != 20 || dm.rgba[2] != 30) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bv_obj_sync did not sync material rgba via BSG");
    }
    PASS("bv_obj_sync synced material rgba via BSG");

    /* Verify appearance (draw_arrows) was synced via BSG. */
    if (!bsg_node_draw_arrows((const bsg_node *)dst)) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bv_obj_sync did not sync draw_arrows via BSG");
    }
    PASS("bv_obj_sync synced draw_arrows via BSG");

    /* Legacy fields should be consistent. */
    if (dst->s_color[0] != 10 || dst->s_color[1] != 20 || dst->s_color[2] != 30) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("legacy s_color not updated by bv_obj_sync BSG path");
    }
    PASS("legacy s_color consistent after bv_obj_sync");

    if (!dst->s_arrow) {
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("legacy s_arrow not set after bv_obj_sync BSG path");
    }
    PASS("legacy s_arrow consistent after bv_obj_sync");

    bsg_scene_root_destroy(root);
    _free_view(v);
    return 0;
}


/* ====================================================================
 * Test 9D: bv_view_obj_labels_sync → BSG material color on children
 * ==================================================================== */

static int
test_labels_sync_routes_bsg(void)
{
    printf("=== Test 9D: bv_view_obj_labels_sync → BSG material ===\n");

    struct bview *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	_free_view(v);
	FAIL("bsg_scene_root_create");
    }

    /* Build a minimal bv_data_label_state. */
    struct bv_data_label_state gdls;
    memset(&gdls, 0, sizeof(gdls));
    gdls.gdls_draw       = 1;
    gdls.gdls_num_labels = 1;
    gdls.gdls_color[0]   = 55;
    gdls.gdls_color[1]   = 66;
    gdls.gdls_color[2]   = 77;

    /* Allocate label text and point arrays. */
    struct bu_vls label_text = BU_VLS_INIT_ZERO;
    bu_vls_init(&label_text);
    bu_vls_sprintf(&label_text, "slice9_label");

    char *labels[1];
    labels[0] = (char *)bu_vls_cstr(&label_text);
    gdls.gdls_labels = labels;

    point_t pts[1] = {{0.0, 0.0, 0.0}};
    gdls.gdls_points = pts;

    /* Sync labels into the view. */
    bv_view_obj_labels_sync(v, &gdls, "slice9_labels");

    /* Find the parent object. */
    struct bv_scene_obj *parent = bv_view_obj_find(v, "slice9_labels");
    if (!parent) {
	bu_vls_free(&label_text);
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bv_view_obj_labels_sync: parent not found");
    }

    /* The first child should carry the colour set via bv_view_obj_set_color. */
    if (BU_PTBL_LEN(&parent->bsg.bsg_children) < 1) {
	bu_vls_free(&label_text);
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("bv_view_obj_labels_sync: no children on parent");
    }

    struct bv_scene_obj *child =
	(struct bv_scene_obj *)BU_PTBL_GET(&parent->bsg.bsg_children, 0);

    struct bsg_material cm;
    bsg_material_init(&cm);
    (void)bsg_node_material_get((const bsg_node *)child, &cm);

    if (cm.rgba[0] != 55 || cm.rgba[1] != 66 || cm.rgba[2] != 77) {
	bu_vls_free(&label_text);
	bsg_scene_root_destroy(root);
	_free_view(v);
	FAIL("child BSG material rgba not set by labels_sync");
    }
    PASS("child BSG material rgba correct after labels_sync");

    bu_vls_free(&label_text);
    bsg_scene_root_destroy(root);
    _free_view(v);
    return 0;
}


/* ====================================================================
 * Test 9E: NULL-argument safety
 * ==================================================================== */

static int
test_null_safety(void)
{
    printf("=== Test 9E: NULL safety ===\n");

    /* bv_view_obj_set_color on NULL should not crash. */
    bv_view_obj_set_color(NULL, 1, 2, 3);
    PASS("bv_view_obj_set_color(NULL) does not crash");

    /* bv_obj_sync on NULL should not crash. */
    bv_obj_sync(NULL, NULL);
    PASS("bv_obj_sync(NULL, NULL) does not crash");

    /* bv_view_obj_labels_sync on NULL should not crash. */
    bv_view_obj_labels_sync(NULL, NULL, NULL);
    PASS("bv_view_obj_labels_sync(NULL, NULL, NULL) does not crash");

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

    failures += test_arrow_create_routes_bsg();
    failures += test_set_color_routes_bsg();
    failures += test_sync_routes_bsg();
    failures += test_labels_sync_routes_bsg();
    failures += test_null_safety();

    if (failures) {
	printf("\nFAILED: %d test(s) failed.\n", failures);
	return 1;
    }
    printf("\nAll slice-9 material/appearance/settings tests PASSED.\n");
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
