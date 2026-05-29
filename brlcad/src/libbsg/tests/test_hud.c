/*                  T E S T _ H U D . C
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
/** @file libbsg/tests/test_hud.c
 *
 * Phase D4 unit tests for the BSG HUD root and overlay metadata API.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/vls.h"
#include "bsg/defines.h"
#include "bsg/node.h"
#include "bsg/payload_typed.h"
#include "bsg/payload.h"
#include "bsg/util.h"
#include "bsg/hud.h"


/* -----------------------------------------------------------------------
 * Minimal view factory using the proper bsg_view_init / bsg_view_free API.
 * ----------------------------------------------------------------------- */

static struct bsg_view *
_make_view(void)
{
    struct bsg_view *v;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_hud_view");
    return v;
}

static void
_free_view(struct bsg_view *v)
{
    if (!v)
	return;
    bsg_hud_root_destroy(v);
    bsg_view_free(v);
    bu_free(v, "test_hud view");
}

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while(0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while(0)
#define TEST(name) do { printf("=== Test %d: %s ===\n", ++_tc, (name)); } while(0)

static int _tc = 0;


/* -----------------------------------------------------------------------
 * Test 1: bsg_hud_root_create NULL guard
 * ----------------------------------------------------------------------- */
static int
test_null_guard(void)
{
    TEST("bsg_hud_root_create NULL view");
    bsg_node *r = bsg_hud_root_create(NULL);
    if (r != NULL)
	FAIL("Expected NULL for NULL view");
    PASS("bsg_hud_root_create NULL view");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 2: bsg_hud_root_create creates a valid root
 * ----------------------------------------------------------------------- */
static int
test_create(void)
{
    TEST("bsg_hud_root_create creates root");
    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_hud_root_create(v);
    if (!root)
	FAIL("bsg_hud_root_create returned NULL");
    if (v->gv_hud_root != root)
	FAIL("gv_hud_root not set");
    if (!(root->s_type_flags & BSG_NODE_GROUP))
	FAIL("root is not a BSG_NODE_GROUP");
    _free_view(v);
    PASS("bsg_hud_root_create creates root");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 3: root has exactly BSG_HUD_FEATURE_COUNT children
 * ----------------------------------------------------------------------- */
static int
test_child_count(void)
{
    TEST("HUD root has correct child count");
    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_hud_root_create(v);
    if (!root)
	FAIL("bsg_hud_root_create returned NULL");
    size_t n = BU_PTBL_LEN(&root->children);
    if ((int)n != BSG_HUD_FEATURE_COUNT)
	FAIL("Wrong number of HUD children");
    _free_view(v);
    PASS("HUD root has correct child count");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 4: all children start with s_flag == DOWN (disabled)
 * ----------------------------------------------------------------------- */
static int
test_children_initially_down(void)
{
    TEST("HUD children initially DOWN");
    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_hud_root_create(v);
    if (!root)
	FAIL("bsg_hud_root_create returned NULL");
    for (int i = 0; i < BSG_HUD_FEATURE_COUNT; i++) {
	bsg_node *c = (bsg_node *)BU_PTBL_GET(&root->children, (size_t)i);
	if (!c)
	    FAIL("NULL child node");
	if (c->s_flag != DOWN)
	    FAIL("Child not initially DOWN");
    }
    _free_view(v);
    PASS("HUD children initially DOWN");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 5: each child carries BSG_PAYLOAD_OVERLAY, valid meta, and payload
 * ----------------------------------------------------------------------- */
static int
test_children_have_meta(void)
{
    TEST("HUD children have OVERLAY payload, meta, and payload snapshot");
    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_hud_root_create(v);
    if (!root)
	FAIL("bsg_hud_root_create returned NULL");
    for (int i = 0; i < BSG_HUD_FEATURE_COUNT; i++) {
	bsg_node *c = (bsg_node *)BU_PTBL_GET(&root->children, (size_t)i);
	if (!c)
	    FAIL("NULL child node");
	if (!(bsg_node_get_payload_type(c) & BSG_PAYLOAD_OVERLAY))
	    FAIL("Child missing BSG_PAYLOAD_OVERLAY");
	struct bsg_hud_node_meta *m = bsg_hud_node_get_meta(c);
	if (!m)
	    FAIL("bsg_hud_node_get_meta returned NULL");
	const struct bsg_hud_payload *p = bsg_hud_node_get_payload(c);
	if (!p)
	    FAIL("bsg_hud_node_get_payload returned NULL");
	if ((int)m->feature_type != i)
	    FAIL("feature_type does not match sort order index");
	if ((int)p->feature_type != i)
	    FAIL("payload feature_type does not match sort order index");
	if (m->sort_order != i)
	    FAIL("sort_order does not match index");
    }
    _free_view(v);
    PASS("HUD children have OVERLAY payload, meta, and payload snapshot");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 6: bsg_hud_sync copies faceplate payload state
 * ----------------------------------------------------------------------- */
static int
test_sync_payloads(void)
{
    TEST("bsg_hud_sync updates payload snapshots");
    struct bsg_view *v = _make_view();
    v->gv_ls.gv_center_dot.gos_draw = 1;
    VSET(v->gv_ls.gv_center_dot.gos_line_color, 1, 2, 3);
    v->gv_ls.gv_fb_mode = 2;

    int rc = bsg_hud_sync(v);
    if (rc != 0)
	FAIL("bsg_hud_sync returned error");

    bsg_node *root = bsg_hud_root_get(v);
    const struct bsg_hud_payload *center =
	bsg_hud_node_get_payload((bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_CENTER_DOT));
    const struct bsg_hud_payload *fb =
	bsg_hud_node_get_payload((bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_FRAMEBUFFER));
    if (!center || !fb)
	FAIL("missing HUD payload snapshots");
    if (center->data.other.gos_line_color[0] != 1 ||
	center->data.other.gos_line_color[1] != 2 ||
	center->data.other.gos_line_color[2] != 3)
	FAIL("center-dot payload colors not copied");
    if (fb->data.framebuffer.mode != 2)
	FAIL("framebuffer mode not copied");
    if (((bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_FRAMEBUFFER))->s_flag != UP)
	FAIL("framebuffer feature should be enabled");

    _free_view(v);
    PASS("bsg_hud_sync updates payload snapshots");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 7: bsg_hud_sync with center_dot enabled → child UP
 * ----------------------------------------------------------------------- */
static int
test_sync_center_dot(void)
{
    TEST("bsg_hud_sync enables center_dot child");
    struct bsg_view *v = _make_view();
    v->gv_ls.gv_center_dot.gos_draw = 1;

    int rc = bsg_hud_sync(v);
    if (rc != 0)
	FAIL("bsg_hud_sync returned error");

    bsg_node *root = bsg_hud_root_get(v);
    if (!root)
	FAIL("HUD root not created by sync");

    /* feature index 0 = CENTER_DOT */
    bsg_node *c = (bsg_node *)BU_PTBL_GET(&root->children, 0);
    if (!c)
	FAIL("NULL center_dot child");
    if (c->s_flag != UP)
	FAIL("center_dot child not UP after sync");

    /* All other features should still be DOWN */
    for (int i = 1; i < BSG_HUD_FEATURE_COUNT; i++) {
	bsg_node *other = (bsg_node *)BU_PTBL_GET(&root->children, (size_t)i);
	if (other && other->s_flag != DOWN)
	    FAIL("Unexpected UP child after sync");
    }

    _free_view(v);
    PASS("bsg_hud_sync enables center_dot child");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 8: bsg_hud_sync called twice keeps correct state
 * ----------------------------------------------------------------------- */
static int
test_sync_idempotent(void)
{
    TEST("bsg_hud_sync idempotent on repeated calls");
    struct bsg_view *v = _make_view();
    v->gv_ls.gv_center_dot.gos_draw = 1;
    v->gv_ls.gv_model_axes.draw     = 1;

    bsg_hud_sync(v);
    v->gv_ls.gv_center_dot.gos_draw = 0; /* disable center dot */
    bsg_hud_sync(v);

    bsg_node *root = bsg_hud_root_get(v);
    bsg_node *cdot = (bsg_node *)BU_PTBL_GET(&root->children, 0); /* CENTER_DOT */
    bsg_node *maxes = (bsg_node *)BU_PTBL_GET(&root->children, 1); /* MODEL_AXES */

    if (cdot->s_flag != DOWN)
	FAIL("center_dot should be DOWN after second sync");
    if (maxes->s_flag != UP)
	FAIL("model_axes should remain UP after second sync");

    _free_view(v);
    PASS("bsg_hud_sync idempotent on repeated calls");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 9: bsg_hud_root_create returns existing root on second call
 * ----------------------------------------------------------------------- */
static int
test_create_idempotent(void)
{
    TEST("bsg_hud_root_create idempotent");
    struct bsg_view *v = _make_view();
    bsg_node *r1 = bsg_hud_root_create(v);
    bsg_node *r2 = bsg_hud_root_create(v);
    if (r1 != r2)
	FAIL("Second create returned different pointer");
    _free_view(v);
    PASS("bsg_hud_root_create idempotent");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 10: overlay enums are distinct and in range
 * ----------------------------------------------------------------------- */
static int
test_enum_values(void)
{
    TEST("overlay/hud enum values are distinct");
    if (BSG_OVERLAY_ROLE_MODEL == BSG_OVERLAY_ROLE_SCREEN ||
	BSG_OVERLAY_ROLE_SCREEN == BSG_OVERLAY_ROLE_XRAY)
	FAIL("overlay role enum values collide");
    if (BSG_OVERLAY_CLASS_FACEPLATE == BSG_OVERLAY_CLASS_EDIT_HANDLE ||
	BSG_OVERLAY_CLASS_SELECTION_RUBBER_BAND == BSG_OVERLAY_CLASS_DIAGNOSTIC)
	FAIL("overlay class enum values collide");
    if (BSG_HUD_COORD_SCREEN_PX == BSG_HUD_COORD_NDC ||
	BSG_HUD_COORD_NDC == BSG_HUD_COORD_VIEW_PLANE)
	FAIL("hud coord enum values collide");
    if (BSG_OVERLAY_LC_PERSISTENT == BSG_OVERLAY_LC_PER_FRAME)
	FAIL("lifecycle enum values collide");
    PASS("overlay/hud enum values are distinct");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 11: bsg_hud_sync NULL guard
 * ----------------------------------------------------------------------- */
static int
test_sync_null(void)
{
    TEST("bsg_hud_sync NULL view");
    int rc = bsg_hud_sync(NULL);
    if (rc != -1)
	FAIL("Expected -1 for NULL view");
    PASS("bsg_hud_sync NULL view");
    return 0;
}

static int
test_typed_payload_realization(void)
{
    TEST("bsg_hud_sync realizes typed payloads");
    struct bsg_view *v = _make_view();
    v->gv_ls.gv_center_dot.gos_draw = 1;
    v->gv_ls.gv_model_axes.draw = 1;
    v->gv_ls.gv_view_axes.draw = 1;
    v->gv_ls.gv_grid.draw = 1;
    v->gv_ls.gv_view_params.draw = 1;
    v->gv_ls.gv_view_params.font_size = 1;
    v->gv_ls.gv_view_scale.draw = 1;
    v->gv_ls.gv_adc_state.draw = 1;
    v->gv_ls.gv_rect.draw = 1;
    v->gv_ls.gv_fb_mode = 1;

    if (bsg_hud_sync(v) != 0)
	FAIL("bsg_hud_sync returned error");

    bsg_node *root = bsg_hud_root_get(v);
    bsg_node *center  = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_CENTER_DOT);
    bsg_node *mAxes   = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_MODEL_AXES);
    bsg_node *vAxes   = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_VIEW_AXES);
    bsg_node *grid    = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_GRID);
    bsg_node *params  = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_VIEW_PARAMS);
    bsg_node *scale   = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_VIEW_SCALE);
    bsg_node *adc     = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_ADC);
    bsg_node *rect    = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_RECT);
    bsg_node *fb      = (bsg_node *)BU_PTBL_GET(&root->children, BSG_HUD_FEATURE_FRAMEBUFFER);

    if (!center || !mAxes || !vAxes || !grid || !params || !scale || !adc || !rect || !fb)
	FAIL("missing HUD feature nodes");
    if (!bsg_node_get_payload(center) || bsg_node_get_payload(center)->pl_type != BSG_PL_LINE_SET)
	FAIL("center dot should realize as line set");
    if (!bsg_node_get_payload(mAxes) || bsg_node_get_payload(mAxes)->pl_type != BSG_PL_AXES)
	FAIL("model axes should realize as axes payload");
    if (!bsg_node_get_payload(vAxes) || bsg_node_get_payload(vAxes)->pl_type != BSG_PL_AXES)
	FAIL("view axes should realize as axes payload");
    if (!bsg_node_get_payload(grid) || bsg_node_get_payload(grid)->pl_type != BSG_PL_GRID)
	FAIL("grid should realize as grid payload");
    if (!bsg_node_get_payload(params) || bsg_node_get_payload(params)->pl_type != BSG_PL_HUD_TEXT)
	FAIL("view params should realize as HUD text");
    if (!bsg_node_get_payload(scale) || bsg_node_get_payload(scale)->pl_type != BSG_PL_LINE_SET)
	FAIL("view scale should realize as line set");
    if (!bsg_node_get_payload(adc) || bsg_node_get_payload(adc)->pl_type != BSG_PL_LINE_SET)
	FAIL("ADC should realize as line set");
    if (!bsg_node_get_payload(rect) || bsg_node_get_payload(rect)->pl_type != BSG_PL_LINE_SET)
	FAIL("selection rect should realize as line set");
    if (!bsg_node_get_payload(fb) || bsg_node_get_payload(fb)->pl_type != BSG_PL_FRAMEBUFFER)
	FAIL("framebuffer should realize as framebuffer payload");

    _free_view(v);
    PASS("bsg_hud_sync realizes typed payloads");
    return 0;
}


/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int
main(int UNUSED(argc), char **UNUSED(argv))
{
    int fail = 0;

    fail += test_null_guard();
    fail += test_create();
    fail += test_child_count();
    fail += test_children_initially_down();
    fail += test_children_have_meta();
    fail += test_sync_payloads();
    fail += test_sync_center_dot();
    fail += test_sync_idempotent();
    fail += test_create_idempotent();
    fail += test_enum_values();
    fail += test_sync_null();
    fail += test_typed_payload_realization();

    if (fail)
	printf("\n%d TEST(S) FAILED\n", fail);
    else
	printf("\nALL TESTS PASSED\n");

    return fail;
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
