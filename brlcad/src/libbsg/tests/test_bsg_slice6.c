/*              T E S T _ B S G _ S L I C E 6 . C
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
/** @file libbsg/tests/test_bsg_slice6.c
 *
 * Slice 6 (bv_scene_obj_migrate) tests:
 *
 *   1. bsg_payload_vlist_create_owned() — BSG-owned vlist storage:
 *      - Create an owned vlist payload from a bu_list of bv_vlist commands.
 *      - Verify bsg_payload_vlist_head() returns the payload's own list.
 *      - Verify bsg_payload_vlist_count() returns the correct count.
 *      - Verify bsg_payload_bounds() works on the owned payload.
 *      - Verify bsg_payload_wire_from_vlist() works on the owned payload.
 *      - Verify bsg_payload_destroy() cleans up without leaks.
 *
 *   2. bsg_payload_text_create() — text/label payload:
 *      - Create and round-trip every text payload field.
 *      - Build vlist wireframe and verify non-empty output.
 *
 *   3. bsg_payload_axes_create() — axes overlay payload:
 *      - Create and round-trip every axes payload field.
 *
 *   4. bsg_vlist_3string() / bsg_vlist_2string() — vector font wrappers:
 *      - Stroke a known string and verify the vlist is non-empty.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "vmath.h"
#include "bu/app.h"
#include "bu/list.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/vlist.h"
#include "bsg/axes.h"
#include "bsg/payload.h"
#include "bsg/text.h"
#include "bsg/vlist.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


/* ===================================================================
 * Test 1: BSG-owned vlist payload
 * =================================================================== */

static int
test_owned_vlist(void)
{
    printf("=== Test 1: bsg_payload_vlist_create_owned ===\n");

    struct bu_list vhead, vlfree;
    BU_LIST_INIT(&vhead);
    BU_LIST_INIT(&vlfree);

    point_t p0 = VINIT_ZERO;
    point_t p1 = {1.0, 0.0, 0.0};
    point_t p2 = {1.0, 1.0, 0.0};
    point_t p3 = {0.0, 1.0, 0.0};

    BV_ADD_VLIST(&vlfree, &vhead, p0, BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(&vlfree, &vhead, p1, BV_VLIST_LINE_DRAW);
    BV_ADD_VLIST(&vlfree, &vhead, p2, BV_VLIST_LINE_DRAW);
    BV_ADD_VLIST(&vlfree, &vhead, p3, BV_VLIST_LINE_DRAW);
    BV_ADD_VLIST(&vlfree, &vhead, p0, BV_VLIST_LINE_DRAW); /* close square */

    /* Create an owned payload from the vhead. */
    struct bsg_payload *op = bsg_payload_vlist_create_owned(&vhead, &vlfree);

    /* Release the source list now that it has been copied. */
    BV_FREE_VLIST(&vlfree, &vhead);
    bv_vlist_cleanup(&vlfree);

    if (!op)
	FAIL("bsg_payload_vlist_create_owned returned NULL");

    if (bsg_payload_type(op) != BSG_PAYLOAD_TYPE_VLIST)
	FAIL("owned vlist payload has correct type");

    struct bu_list *own_head = bsg_payload_vlist_head(op);
    if (!own_head)
	FAIL("bsg_payload_vlist_head non-NULL for owned payload");

    size_t cnt = bsg_payload_vlist_count(op);
    if (cnt != 5)
	FAIL("owned vlist count equals 5");

    point_t bmin, bmax;
    if (!bsg_payload_bounds(op, &bmin, &bmax))
	FAIL("bsg_payload_bounds succeeds on owned vlist");

    if (bmin[X] > bmax[X] || bmin[Y] > bmax[Y])
	FAIL("owned vlist bounds are monotonic");

    /* Wire conversion must work too. */
    struct bsg_payload *wire = bsg_payload_wire_from_vlist(op);
    if (!wire)
	FAIL("bsg_payload_wire_from_vlist succeeds on owned vlist");
    if (bsg_payload_wire_polyline_count(wire) != 1)
	FAIL("wire has 1 polyline from closed square");
    bsg_payload_destroy(wire);

    /* Revision must be non-zero. */
    if (bsg_payload_revision(op) == 0)
	FAIL("owned vlist payload revision is non-zero");

    bsg_payload_destroy(op);

    PASS("BSG-owned vlist payload lifecycle");
    return 0;
}


/* ===================================================================
 * Test 2: text/label payload
 * =================================================================== */

static int
test_text_payload(void)
{
    printf("=== Test 2: bsg_payload_text_create ===\n");

    point_t origin = {1.0, 2.0, 3.0};
    struct bsg_payload *tp = bsg_payload_text_create("hello BSG", origin, 2.5);

    if (!tp)
	FAIL("bsg_payload_text_create returned NULL");

    if (bsg_payload_type(tp) != BSG_PAYLOAD_TYPE_TEXT)
	FAIL("text payload type is BSG_PAYLOAD_TYPE_TEXT");

    const char *s = bsg_payload_text_get(tp);
    if (!s || strcmp(s, "hello BSG") != 0)
	FAIL("text payload string round-trips correctly");

    double sc = bsg_payload_text_scale_get(tp);
    if (sc < 2.4 || sc > 2.6)
	FAIL("text payload scale round-trips correctly");

    point_t got_origin;
    bsg_payload_text_origin_get(tp, got_origin);
    if (!NEAR_EQUAL(got_origin[X], 1.0, SMALL_FASTF) ||
	!NEAR_EQUAL(got_origin[Y], 2.0, SMALL_FASTF) ||
	!NEAR_EQUAL(got_origin[Z], 3.0, SMALL_FASTF))
	FAIL("text payload origin round-trips correctly");

    /* Rotation: set identity, verify get */
    mat_t rot_in, rot_out;
    MAT_IDN(rot_in);
    rot_in[0] = 2.0; /* perturb */
    bsg_payload_text_rot_set(tp, rot_in);
    bsg_payload_text_rot_get(tp, rot_out);
    if (!NEAR_EQUAL(rot_out[0], 2.0, SMALL_FASTF))
	FAIL("text payload rotation round-trips correctly");

    /* Anchor */
    bsg_payload_text_anchor_set(tp, BSG_TEXT_ANCHOR_MIDDLE_CENTER);
    if (bsg_payload_text_anchor_get(tp) != BSG_TEXT_ANCHOR_MIDDLE_CENTER)
	FAIL("text payload anchor round-trips correctly");

    /* Leader line flags */
    point_t tgt = {10.0, 20.0, 30.0};
    bsg_payload_text_line_flag_set(tp, 1);
    bsg_payload_text_target_set(tp, tgt);
    bsg_payload_text_arrow_set(tp, 1);
    if (!bsg_payload_text_line_flag_get(tp))
	FAIL("text payload line_flag round-trips correctly");
    if (!bsg_payload_text_arrow_get(tp))
	FAIL("text payload arrow flag round-trips correctly");
    point_t tgt_got;
    bsg_payload_text_target_get(tp, tgt_got);
    if (!NEAR_EQUAL(tgt_got[X], 10.0, SMALL_FASTF))
	FAIL("text payload target round-trips correctly");

    /* Update the string */
    bsg_payload_text_set(tp, "updated");
    if (strcmp(bsg_payload_text_get(tp), "updated") != 0)
	FAIL("bsg_payload_text_set updates string");

    /* Build vlist wireframe */
    struct bu_list vhead, vlfree;
    BU_LIST_INIT(&vhead);
    BU_LIST_INIT(&vlfree);
    /* Reset rotation to identity so vlist_3string won't produce degenerate output */
    MAT_IDN(rot_in);
    bsg_payload_text_rot_set(tp, rot_in);
    bsg_payload_text_scale_set(tp, 1.0);
    int rc = bsg_payload_text_build_vlist(tp, &vhead, &vlfree);
    if (rc != 0)
	FAIL("bsg_payload_text_build_vlist returns 0 on success");
    /* A non-empty text string should produce at least a few vlist commands. */
    if (BU_LIST_IS_EMPTY(&vhead))
	FAIL("bsg_payload_text_build_vlist produces non-empty vlist");
    BV_FREE_VLIST(&vlfree, &vhead);
    bv_vlist_cleanup(&vlfree);

    /* NULL input returns -1 */
    if (bsg_payload_text_build_vlist(NULL, &vhead, &vlfree) != -1)
	FAIL("bsg_payload_text_build_vlist(NULL) returns -1");

    bsg_payload_destroy(tp);

    /* Scale <= 0 is rejected by create (defaults to 1.0) */
    struct bsg_payload *tp2 = bsg_payload_text_create("x", origin, -1.0);
    if (!tp2)
	FAIL("bsg_payload_text_create with negative scale uses default");
    if (bsg_payload_text_scale_get(tp2) <= 0.0)
	FAIL("scale default is positive after bad input");
    bsg_payload_destroy(tp2);

    PASS("text/label payload lifecycle");
    return 0;
}


/* ===================================================================
 * Test 3: axes overlay payload
 * =================================================================== */

static int
test_axes_payload(void)
{
    printf("=== Test 3: bsg_payload_axes_create ===\n");

    struct bsg_payload *ap = bsg_payload_axes_create();
    if (!ap)
	FAIL("bsg_payload_axes_create returned NULL");

    if (bsg_payload_type(ap) != BSG_PAYLOAD_TYPE_AXES)
	FAIL("axes payload type is BSG_PAYLOAD_TYPE_AXES");

    /* draw flag */
    bsg_payload_axes_draw_set(ap, 1);
    if (!bsg_payload_axes_draw_get(ap))
	FAIL("axes draw flag round-trips correctly");

    /* position */
    point_t pos = {5.0, 6.0, 7.0};
    bsg_payload_axes_pos_set(ap, pos);
    point_t pos_got;
    bsg_payload_axes_pos_get(ap, pos_got);
    if (!NEAR_EQUAL(pos_got[X], 5.0, SMALL_FASTF) ||
	!NEAR_EQUAL(pos_got[Y], 6.0, SMALL_FASTF) ||
	!NEAR_EQUAL(pos_got[Z], 7.0, SMALL_FASTF))
	FAIL("axes position round-trips correctly");

    /* size */
    bsg_payload_axes_size_set(ap, (fastf_t)3.14);
    if (!NEAR_EQUAL((double)bsg_payload_axes_size_get(ap), 3.14, 0.01))
	FAIL("axes size round-trips correctly");

    /* line width */
    bsg_payload_axes_line_width_set(ap, 3);
    if (bsg_payload_axes_line_width_get(ap) != 3)
	FAIL("axes line width round-trips correctly");

    /* axes color */
    int rgb_in[3] = {200, 100, 50};
    bsg_payload_axes_color_set(ap, rgb_in);
    int rgb_out[3] = {0, 0, 0};
    bsg_payload_axes_color_get(ap, rgb_out);
    if (rgb_out[0] != 200 || rgb_out[1] != 100 || rgb_out[2] != 50)
	FAIL("axes color round-trips correctly");

    /* label flag */
    bsg_payload_axes_label_flag_set(ap, 1);
    if (!bsg_payload_axes_label_flag_get(ap))
	FAIL("axes label flag round-trips correctly");

    /* label color */
    int lrgb_in[3] = {10, 20, 30};
    bsg_payload_axes_label_color_set(ap, lrgb_in);
    int lrgb_out[3] = {0, 0, 0};
    bsg_payload_axes_label_color_get(ap, lrgb_out);
    if (lrgb_out[0] != 10 || lrgb_out[1] != 20 || lrgb_out[2] != 30)
	FAIL("axes label color round-trips correctly");

    /* triple color */
    bsg_payload_axes_triple_color_set(ap, 1);
    if (!bsg_payload_axes_triple_color_get(ap))
	FAIL("axes triple_color round-trips correctly");

    /* pos_only */
    bsg_payload_axes_pos_only_set(ap, 1);
    if (!bsg_payload_axes_pos_only_get(ap))
	FAIL("axes pos_only round-trips correctly");

    /* ticks */
    bsg_payload_axes_tick_enabled_set(ap, 1);
    bsg_payload_axes_tick_length_set(ap, 5);
    bsg_payload_axes_tick_major_length_set(ap, 10);
    bsg_payload_axes_tick_interval_set(ap, (fastf_t)2.0);
    bsg_payload_axes_ticks_per_major_set(ap, 4);
    bsg_payload_axes_tick_threshold_set(ap, 8);

    if (!bsg_payload_axes_tick_enabled_get(ap))
	FAIL("axes tick_enabled round-trips correctly");
    if (bsg_payload_axes_tick_length_get(ap) != 5)
	FAIL("axes tick_length round-trips correctly");
    if (bsg_payload_axes_tick_major_length_get(ap) != 10)
	FAIL("axes tick_major_length round-trips correctly");
    if (bsg_payload_axes_ticks_per_major_get(ap) != 4)
	FAIL("axes ticks_per_major round-trips correctly");
    if (bsg_payload_axes_tick_threshold_get(ap) != 8)
	FAIL("axes tick_threshold round-trips correctly");

    /* tick colors */
    int tc_in[3] = {50, 60, 70};
    bsg_payload_axes_tick_color_set(ap, tc_in);
    int tc_out[3] = {0, 0, 0};
    bsg_payload_axes_tick_color_get(ap, tc_out);
    if (tc_out[0] != 50)
	FAIL("axes tick_color round-trips correctly");

    int tmc_in[3] = {80, 90, 100};
    bsg_payload_axes_tick_major_color_set(ap, tmc_in);
    int tmc_out[3] = {0, 0, 0};
    bsg_payload_axes_tick_major_color_get(ap, tmc_out);
    if (tmc_out[2] != 100)
	FAIL("axes tick_major_color round-trips correctly");

    /* NULL-safety */
    bsg_payload_axes_draw_set(NULL, 1);
    if (bsg_payload_axes_draw_get(NULL) != 0)
	FAIL("axes NULL draw get returns 0");
    if (!NEAR_ZERO((double)bsg_payload_axes_size_get(NULL), SMALL_FASTF))
	FAIL("axes NULL size get returns 0");

    bsg_payload_destroy(ap);
    PASS("axes overlay payload lifecycle");
    return 0;
}


/* ===================================================================
 * Test 4: bsg_vlist_3string / bsg_vlist_2string wrappers
 * =================================================================== */

static int
test_vlist_string_wrappers(void)
{
    printf("=== Test 4: bsg_vlist_3string / bsg_vlist_2string ===\n");

    struct bu_list vhead3, vhead2, vlfree;
    BU_LIST_INIT(&vhead3);
    BU_LIST_INIT(&vhead2);
    BU_LIST_INIT(&vlfree);

    mat_t rot;
    MAT_IDN(rot);
    point_t origin = VINIT_ZERO;

    /* bsg_vlist_3string: stroke "A" */
    bsg_vlist_3string(&vhead3, &vlfree, "A", origin, rot, 1.0);
    if (BU_LIST_IS_EMPTY(&vhead3))
	FAIL("bsg_vlist_3string produces non-empty vlist for 'A'");

    /* bsg_vlist_2string: stroke "B" */
    bsg_vlist_2string(&vhead2, &vlfree, "B", 0.0, 0.0, 1.0, 0.0);
    if (BU_LIST_IS_EMPTY(&vhead2))
	FAIL("bsg_vlist_2string produces non-empty vlist for 'B'");

    /* NULL-safety: should not crash */
    bsg_vlist_3string(NULL, &vlfree, "X", origin, rot, 1.0);
    bsg_vlist_3string(&vhead3, NULL, "X", origin, rot, 1.0);
    bsg_vlist_3string(&vhead3, &vlfree, NULL, origin, rot, 1.0);
    bsg_vlist_2string(NULL, &vlfree, "X", 0.0, 0.0, 1.0, 0.0);
    bsg_vlist_2string(&vhead2, NULL, "X", 0.0, 0.0, 1.0, 0.0);
    bsg_vlist_2string(&vhead2, &vlfree, NULL, 0.0, 0.0, 1.0, 0.0);

    BV_FREE_VLIST(&vlfree, &vhead3);
    BV_FREE_VLIST(&vlfree, &vhead2);
    bv_vlist_cleanup(&vlfree);

    PASS("bsg_vlist_3string / bsg_vlist_2string");
    return 0;
}


/* ===================================================================
 * Test 5: bsg_payload_create() with TEXT and AXES types
 * =================================================================== */

static int
test_payload_create_dispatch(void)
{
    printf("=== Test 5: bsg_payload_create TEXT/AXES dispatch ===\n");

    struct bsg_payload *tp = bsg_payload_create(BSG_PAYLOAD_TYPE_TEXT);
    if (!tp)
	FAIL("bsg_payload_create(TEXT) returns non-NULL");
    if (bsg_payload_type(tp) != BSG_PAYLOAD_TYPE_TEXT)
	FAIL("bsg_payload_create(TEXT) produces TEXT type");
    bsg_payload_destroy(tp);

    struct bsg_payload *ap = bsg_payload_create(BSG_PAYLOAD_TYPE_AXES);
    if (!ap)
	FAIL("bsg_payload_create(AXES) returns non-NULL");
    if (bsg_payload_type(ap) != BSG_PAYLOAD_TYPE_AXES)
	FAIL("bsg_payload_create(AXES) produces AXES type");
    bsg_payload_destroy(ap);

    PASS("bsg_payload_create TEXT/AXES dispatch");
    return 0;
}


/* ===================================================================
 * main
 * =================================================================== */

int
main(int UNUSED(argc), const char **argv)
{
    int failures = 0;
    bu_setprogname(argv[0]);

    failures += test_owned_vlist();
    failures += test_text_payload();
    failures += test_axes_payload();
    failures += test_vlist_string_wrappers();
    failures += test_payload_create_dispatch();

    if (failures == 0)
	printf("RESULT: all slice6 tests PASSED\n");
    else
	printf("RESULT: %d slice6 test(s) FAILED\n", failures);

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
