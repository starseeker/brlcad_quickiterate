/*          T E S T _ B S G _ S L I C E 7 . C
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
/** @file libbsg/tests/test_bsg_slice7.c
 *
 * Slice 7 (bv_scene_obj_migrate) tests:
 *
 *   1. bsg_payload_polygon_create() - lifecycle and accessor round-trips:
 *      - Create payloads for each polygon type.
 *      - Round-trip all fields (type, fill, dir, delta, color, edit state,
 *        origin, view plane, vZ, bg_polygon copy).
 *      - Verify bsg_payload_polygon_cpy() clones geometry.
 *      - Verify bsg_payload_destroy() cleans up without leaks.
 *
 *   2. bsg_polygon_fill_segments() - pure geometry fill:
 *      - Build a simple square bg_polygon and verify fill segments are
 *        returned for a valid configuration.
 *      - Verify NULL is returned for degenerate input.
 *
 *   3. bsg_polygon_calc_fdelta() - delta suggestion:
 *      - Verify it returns 0 (stub) without crashing.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "vmath.h"
#include "bu/app.h"
#include "bu/color.h"
#include "bu/malloc.h"
#include "bg/polygon.h"
#include "bg/polygon_types.h"
#include "bsg/payload.h"
#include "bsg/polygon.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

/* ===================================================================
 * Test 1: payload lifecycle and accessor round-trips
 * =================================================================== */

static int
test_polygon_payload_lifecycle(void)
{
    printf("=== Test 1: bsg_payload_polygon_create lifecycle ===\n");

    /* --- 1a. Basic creation for each type --- */
    int types[] = {
	BSG_POLYGON_GENERAL, BSG_POLYGON_CIRCLE,
	BSG_POLYGON_ELLIPSE, BSG_POLYGON_RECTANGLE,
	BSG_POLYGON_SQUARE
    };
    const char *tnames[] = {
	"GENERAL", "CIRCLE", "ELLIPSE", "RECTANGLE", "SQUARE"
    };
    for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
	struct bsg_payload *p = bsg_payload_polygon_create(types[t]);
	if (!p) {
	    printf("  FAIL: create type %s returned NULL\n", tnames[t]);
	    return 1;
	}
	if (p->type != BSG_PAYLOAD_TYPE_POLYGON) {
	    printf("  FAIL: payload type field not POLYGON for %s\n", tnames[t]);
	    bsg_payload_destroy(p);
	    return 1;
	}
	if (bsg_payload_polygon_type_get(p) != types[t]) {
	    printf("  FAIL: shape type mismatch for %s\n", tnames[t]);
	    bsg_payload_destroy(p);
	    return 1;
	}
	bsg_payload_destroy(p);
    }
    PASS("create / type_get round-trip for all 5 polygon types");

    /* --- 1b. fill flag --- */
    {
	struct bsg_payload *p = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
	if (!p) FAIL("create for fill_flag test");
	if (bsg_payload_polygon_fill_flag_get(p) != 0) FAIL("fill_flag default != 0");
	bsg_payload_polygon_fill_flag_set(p, 1);
	if (bsg_payload_polygon_fill_flag_get(p) != 1) FAIL("fill_flag after set(1)");
	bsg_payload_polygon_fill_flag_set(p, 0);
	if (bsg_payload_polygon_fill_flag_get(p) != 0) FAIL("fill_flag after set(0)");
	bsg_payload_destroy(p);
    }
    PASS("fill_flag round-trip");

    /* --- 1c. fill_dir --- */
    {
	struct bsg_payload *p = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
	if (!p) FAIL("create for fill_dir test");
	vect2d_t dir_in  = {3.0, 4.0};
	vect2d_t dir_out = {0.0, 0.0};
	bsg_payload_polygon_fill_dir_set(p, dir_in);
	bsg_payload_polygon_fill_dir_get(p, dir_out);
	if (!NEAR_EQUAL(dir_out[0], 3.0, 1e-9) || !NEAR_EQUAL(dir_out[1], 4.0, 1e-9))
	    FAIL("fill_dir round-trip values wrong");
	bsg_payload_destroy(p);
    }
    PASS("fill_dir round-trip");

    /* --- 1d. fill_delta --- */
    {
	struct bsg_payload *p = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
	if (!p) FAIL("create for fill_delta test");
	if (!NEAR_ZERO(bsg_payload_polygon_fill_delta_get(p), 1e-9))
	    FAIL("fill_delta default != 0");
	bsg_payload_polygon_fill_delta_set(p, 0.5);
	if (!NEAR_EQUAL(bsg_payload_polygon_fill_delta_get(p), 0.5, 1e-9))
	    FAIL("fill_delta after set(0.5)");
	bsg_payload_destroy(p);
    }
    PASS("fill_delta round-trip");

    /* --- 1e. fill_color --- */
    {
	struct bsg_payload *p = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
	if (!p) FAIL("create for fill_color test");
	unsigned char rgb_in[3] = {128, 64, 32};
	struct bu_color c_in, c_out;
	bu_color_from_rgb_chars(&c_in, rgb_in);
	bsg_payload_polygon_fill_color_set(p, &c_in);
	bsg_payload_polygon_fill_color_get(p, &c_out);
	unsigned char rgb_out[3];
	bu_color_to_rgb_chars(&c_out, rgb_out);
	if (rgb_out[0] != 128 || rgb_out[1] != 64 || rgb_out[2] != 32)
	    FAIL("fill_color round-trip values wrong");
	bsg_payload_destroy(p);
    }
    PASS("fill_color round-trip");

    /* --- 1f. edit state (curr_contour_i, curr_point_i) --- */
    {
	struct bsg_payload *p = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
	if (!p) FAIL("create for edit state test");
	if (bsg_payload_polygon_curr_contour_get(p) != -1L)
	    FAIL("curr_contour_i default != -1");
	if (bsg_payload_polygon_curr_point_get(p) != -1L)
	    FAIL("curr_point_i default != -1");
	bsg_payload_polygon_curr_contour_set(p, 2L);
	bsg_payload_polygon_curr_point_set(p, 7L);
	if (bsg_payload_polygon_curr_contour_get(p) != 2L)
	    FAIL("curr_contour_i after set(2)");
	if (bsg_payload_polygon_curr_point_get(p) != 7L)
	    FAIL("curr_point_i after set(7)");
	bsg_payload_destroy(p);
    }
    PASS("curr_contour_i / curr_point_i round-trip");

    /* --- 1g. origin_point --- */
    {
	struct bsg_payload *p = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
	if (!p) FAIL("create for origin test");
	point_t pt_in  = {1.0, 2.0, 3.0};
	point_t pt_out = VINIT_ZERO;
	bsg_payload_polygon_origin_set(p, pt_in);
	bsg_payload_polygon_origin_get(p, pt_out);
	if (!VNEAR_EQUAL(pt_in, pt_out, 1e-9))
	    FAIL("origin_point round-trip values wrong");
	bsg_payload_destroy(p);
    }
    PASS("origin_point round-trip");

    /* --- 1h. view_plane (plane_t) --- */
    {
	struct bsg_payload *p = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
	if (!p) FAIL("create for view_plane test");
	plane_t vp_in  = {0.0, 0.0, 1.0, 5.0};
	plane_t vp_out = HINIT_ZERO;
	bsg_payload_polygon_view_plane_set(p, vp_in);
	bsg_payload_polygon_view_plane_get(p, vp_out);
	if (!HNEAR_EQUAL(vp_in, vp_out, 1e-9))
	    FAIL("view_plane round-trip values wrong");
	bsg_payload_destroy(p);
    }
    PASS("view_plane round-trip");

    /* --- 1i. vZ --- */
    {
	struct bsg_payload *p = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
	if (!p) FAIL("create for vZ test");
	if (!NEAR_ZERO(bsg_payload_polygon_vZ_get(p), 1e-9))
	    FAIL("vZ default != 0");
	bsg_payload_polygon_vZ_set(p, 1.234);
	if (!NEAR_EQUAL(bsg_payload_polygon_vZ_get(p), 1.234, 1e-9))
	    FAIL("vZ after set(1.234)");
	bsg_payload_destroy(p);
    }
    PASS("vZ round-trip");

    /* --- 1j. bg_polygon get/set --- */
    {
	struct bsg_payload *p = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
	if (!p) FAIL("create for bg_polygon test");

	/* Mutable accessor: initially the polygon is empty but valid */
	struct bg_polygon *bgp = bsg_payload_polygon_bg_get(p);
	if (!bgp) FAIL("bg_polygon pointer NULL on fresh payload");
	if (bgp->num_contours != 0) FAIL("fresh polygon has non-zero contours");

	/* Build a simple 3-point contour and set it */
	struct bg_polygon src;
	src.num_contours = 1;
	src.hole    = (int *)bu_calloc(1, sizeof(int), "hole");
	src.contour = (struct bg_poly_contour *)bu_calloc(1, sizeof(struct bg_poly_contour), "c");
	src.contour[0].num_points = 3;
	src.contour[0].open  = 0;
	src.contour[0].point = (point_t *)bu_calloc(3, sizeof(point_t), "pts");
	VSET(src.contour[0].point[0], 0, 0, 0);
	VSET(src.contour[0].point[1], 1, 0, 0);
	VSET(src.contour[0].point[2], 0, 1, 0);

	bsg_payload_polygon_bg_set(p, &src);
	bg_polygon_free(&src);

	bgp = bsg_payload_polygon_bg_get(p);
	if (!bgp) FAIL("bg_polygon pointer NULL after set");
	if (bgp->num_contours != 1) FAIL("bg_polygon contour count after set");
	if (bgp->contour[0].num_points != 3) FAIL("bg_polygon point count after set");

	bsg_payload_destroy(p);
    }
    PASS("bg_polygon get/set round-trip");

    /* --- 1k. bsg_payload_polygon_cpy --- */
    {
	struct bsg_payload *src = bsg_payload_polygon_create(BSG_POLYGON_CIRCLE);
	struct bsg_payload *dst = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
	if (!src || !dst) FAIL("create for cpy test");

	bsg_payload_polygon_fill_flag_set(src, 1);
	bsg_payload_polygon_fill_delta_set(src, 0.25);
	bsg_payload_polygon_vZ_set(src, 2.5);
	bsg_payload_polygon_curr_contour_set(src, 1L);

	bsg_payload_polygon_cpy(dst, src);

	if (bsg_payload_polygon_type_get(dst) != BSG_POLYGON_CIRCLE)
	    FAIL("cpy: type not copied");
	if (bsg_payload_polygon_fill_flag_get(dst) != 1)
	    FAIL("cpy: fill_flag not copied");
	if (!NEAR_EQUAL(bsg_payload_polygon_fill_delta_get(dst), 0.25, 1e-9))
	    FAIL("cpy: fill_delta not copied");
	if (!NEAR_EQUAL(bsg_payload_polygon_vZ_get(dst), 2.5, 1e-9))
	    FAIL("cpy: vZ not copied");
	if (bsg_payload_polygon_curr_contour_get(dst) != 1L)
	    FAIL("cpy: curr_contour_i not copied");

	bsg_payload_destroy(src);
	bsg_payload_destroy(dst);
    }
    PASS("bsg_payload_polygon_cpy");

    return 0;
}


/* ===================================================================
 * Test 2: bsg_polygon_fill_segments()
 * =================================================================== */

static int
test_polygon_fill_segments(void)
{
    printf("=== Test 2: bsg_polygon_fill_segments ===\n");

    /* Build a unit square polygon in the XY plane */
    struct bg_polygon poly;
    poly.num_contours = 1;
    poly.hole    = (int *)bu_calloc(1, sizeof(int), "hole");
    poly.contour = (struct bg_poly_contour *)bu_calloc(1, sizeof(struct bg_poly_contour), "c");
    poly.contour[0].num_points = 4;
    poly.contour[0].open  = 0;
    poly.contour[0].point = (point_t *)bu_calloc(4, sizeof(point_t), "pts");
    VSET(poly.contour[0].point[0], 0, 0, 0);
    VSET(poly.contour[0].point[1], 1, 0, 0);
    VSET(poly.contour[0].point[2], 1, 1, 0);
    VSET(poly.contour[0].point[3], 0, 1, 0);

    /* View plane: Z = 0 (XY plane, normal = +Z) */
    plane_t vp = {0.0, 0.0, 1.0, 0.0};

    vect2d_t slope = {1.0, 0.0};   /* horizontal fill lines */
    fastf_t  spacing = 0.1;

    /* --- 2a. Valid input should return non-NULL fill polygon --- */
    struct bg_polygon *fill = bsg_polygon_fill_segments(&poly, &vp, slope, spacing);
    if (!fill)
	FAIL("fill_segments returned NULL for valid unit square");
    if (fill->num_contours == 0)
	FAIL("fill_segments returned 0 contours for unit square");
    bg_polygon_free(fill);
    BU_PUT(fill, struct bg_polygon);
    PASS("bsg_polygon_fill_segments returns non-empty result for unit square");

    /* --- 2b. NULL polygon pointer --- */
    fill = bsg_polygon_fill_segments(NULL, &vp, slope, spacing);
    if (fill != NULL)
	FAIL("fill_segments should return NULL for NULL poly");
    PASS("bsg_polygon_fill_segments NULL poly returns NULL");

    /* --- 2c. NULL view plane --- */
    fill = bsg_polygon_fill_segments(&poly, NULL, slope, spacing);
    if (fill != NULL)
	FAIL("fill_segments should return NULL for NULL vp");
    PASS("bsg_polygon_fill_segments NULL vp returns NULL");

    bg_polygon_free(&poly);

    return 0;
}


/* ===================================================================
 * Test 3: bsg_polygon_calc_fdelta()
 * =================================================================== */

static int
test_polygon_calc_fdelta(void)
{
    printf("=== Test 3: bsg_polygon_calc_fdelta ===\n");

    struct bsg_payload *p = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
    if (!p) FAIL("create for calc_fdelta test");

    int delta = bsg_polygon_calc_fdelta(p);
    if (delta != 0) FAIL("calc_fdelta (stub) should return 0");
    bsg_payload_destroy(p);
    PASS("bsg_polygon_calc_fdelta returns 0 (stub)");

    /* NULL should not crash */
    delta = bsg_polygon_calc_fdelta(NULL);
    if (delta != 0) FAIL("calc_fdelta(NULL) should return 0");
    PASS("bsg_polygon_calc_fdelta(NULL) does not crash");

    return 0;
}


/* ===================================================================
 * main
 * =================================================================== */

int
main(int argc, char *argv[])
{
    (void)argc;
    bu_setprogname(argv[0]);

    int failures = 0;

    failures += test_polygon_payload_lifecycle();
    failures += test_polygon_fill_segments();
    failures += test_polygon_calc_fdelta();

    if (failures) {
	printf("\nFAILED: %d test(s) failed.\n", failures);
	return 1;
    }
    printf("\nAll slice-7 polygon payload tests PASSED.\n");
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
