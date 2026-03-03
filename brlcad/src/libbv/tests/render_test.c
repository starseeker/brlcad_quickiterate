/*                   R E N D E R _ T E S T . C
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/**
 * Unit tests for the bv_render_ctx C API defined in bv/render.h.
 *
 * These tests are designed to compile and pass whether or not the Obol
 * rendering backend is available:
 *
 *  - When Obol IS available:  bv_render_ctx_available() == 1, full lifecycle
 *    is exercised.
 *  - When Obol is NOT available: every function is a safe no-op that returns
 *    NULL / 0.
 *
 * Exit code: 0 on success, non-zero on failure.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu.h"
#include "bv.h"
#include "bv/render.h"

/* Simple test pass/fail counters */
static int pass_count = 0;
static int fail_count = 0;

#define CHECK(label, expr) \
    do { \
	if (!(expr)) { \
	    fprintf(stderr, "FAIL: %s\n", (label)); \
	    fail_count++; \
	} else { \
	    printf("pass: %s\n", (label)); \
	    pass_count++; \
	} \
    } while (0)


/* ------------------------------------------------------------------ */
/* test_available                                                       */
/* ------------------------------------------------------------------ */
static void
test_available(void)
{
    /* bv_render_ctx_available() must return 0 or 1 — never anything else. */
    int avail = bv_render_ctx_available();
    CHECK("available_returns_0_or_1", avail == 0 || avail == 1);
    printf("  Obol backend available: %s\n", avail ? "yes" : "no");
}


/* ------------------------------------------------------------------ */
/* test_null_scene                                                      */
/* ------------------------------------------------------------------ */
static void
test_null_scene(void)
{
    /* Passing NULL scene must not crash; must return NULL. */
    struct bv_render_ctx *ctx = bv_render_ctx_create(NULL, NULL, 64, 64);
    CHECK("null_scene_returns_null", ctx == NULL);
    bv_render_ctx_destroy(ctx);    /* must not crash on NULL */
}


/* ------------------------------------------------------------------ */
/* test_invalid_dimensions                                             */
/* ------------------------------------------------------------------ */
static void
test_invalid_dimensions(void)
{
    struct bv_scene *s = bv_scene_create();

    struct bv_render_ctx *c1 = bv_render_ctx_create(s, NULL, 0, 64);
    CHECK("zero_width_returns_null", c1 == NULL);

    struct bv_render_ctx *c2 = bv_render_ctx_create(s, NULL, 64, 0);
    CHECK("zero_height_returns_null", c2 == NULL);

    struct bv_render_ctx *c3 = bv_render_ctx_create(s, NULL, -1, 64);
    CHECK("negative_width_returns_null", c3 == NULL);

    bv_scene_destroy(s);
}


/* ------------------------------------------------------------------ */
/* test_null_destroy                                                    */
/* ------------------------------------------------------------------ */
static void
test_null_destroy(void)
{
    bv_render_ctx_destroy(NULL);      /* must not crash */
    CHECK("null_destroy_ok", 1);
}


/* ------------------------------------------------------------------ */
/* test_null_set_size                                                   */
/* ------------------------------------------------------------------ */
static void
test_null_set_size(void)
{
    bv_render_ctx_set_size(NULL, 256, 256);  /* must not crash */
    CHECK("null_set_size_ok", 1);
}


/* ------------------------------------------------------------------ */
/* test_null_render_frame                                              */
/* ------------------------------------------------------------------ */
static void
test_null_render_frame(void)
{
    int r = bv_render_frame(NULL, NULL);
    CHECK("null_ctx_render_returns_0", r == 0);
}


/* ------------------------------------------------------------------ */
/* test_osmesa_mgr                                                      */
/* ------------------------------------------------------------------ */
static void
test_osmesa_mgr(void)
{
    void *mgr = bv_render_ctx_osmesa_mgr_create();
    /* Either NULL (Obol unavailable / no OSMesa) or non-NULL — both fine. */
    CHECK("osmesa_mgr_no_crash", 1);
    bv_render_ctx_osmesa_mgr_destroy(mgr);  /* must not crash whether NULL or not */
    CHECK("osmesa_mgr_destroy_no_crash", 1);
}


/* ------------------------------------------------------------------ */
/* test_lifecycle_empty_scene                                           */
/* ------------------------------------------------------------------ */
static void
test_lifecycle_empty_scene(void)
{
    if (!bv_render_ctx_available()) {
	printf("  skip (Obol not available)\n");
	pass_count++;
	return;
    }

    struct bv_scene *scene = bv_scene_create();
    void *mgr = bv_render_ctx_osmesa_mgr_create();

    struct bv_render_ctx *ctx = bv_render_ctx_create(scene, mgr, 64, 64);
    CHECK("lifecycle_ctx_created", ctx != NULL);

    if (ctx) {
	bv_render_ctx_set_size(ctx, 128, 128);
	CHECK("set_size_no_crash", 1);

	int r = bv_render_frame(ctx, NULL);
	CHECK("render_empty_scene", r == 1);

	bv_render_ctx_destroy(ctx);
	CHECK("destroy_no_crash", 1);
    }

    bv_render_ctx_osmesa_mgr_destroy(mgr);
    bv_scene_destroy(scene);
}


/* ------------------------------------------------------------------ */
/* test_lifecycle_geometry_node                                         */
/* ------------------------------------------------------------------ */
static void
test_lifecycle_geometry_node(void)
{
    if (!bv_render_ctx_available()) {
	printf("  skip (Obol not available)\n");
	pass_count++;
	return;
    }

    struct bv_scene *scene = bv_scene_create();

    /* Build a small geometry node with a minimal vlist (one line segment). */
    struct bv_node *geom = bv_node_create("test_line", BV_NODE_GEOMETRY);
    struct bu_list vlfree;
    BU_LIST_INIT(&vlfree);
    struct bu_list vlist;
    BU_LIST_INIT(&vlist);

    point_t p0 = {0.0, 0.0, 0.0};
    point_t p1 = {1.0, 1.0, 1.0};
    BV_ADD_VLIST(&vlfree, &vlist, p0, BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(&vlfree, &vlist, p1, BV_VLIST_LINE_DRAW);

    bv_node_vlist_set(geom, &vlist);
    bv_scene_add_node(scene, geom);

    void *mgr = bv_render_ctx_osmesa_mgr_create();
    struct bv_render_ctx *ctx = bv_render_ctx_create(scene, mgr, 32, 32);
    CHECK("geom_ctx_created", ctx != NULL);

    if (ctx) {
	int r = bv_render_frame(ctx, NULL);
	CHECK("render_with_geometry", r == 1);

	/* Mark the node stale and re-render (tests incremental rebuild path) */
	bv_node_dlist_stale_set(geom, 1);
	r = bv_render_frame(ctx, NULL);
	CHECK("render_after_stale", r == 1);

	bv_render_ctx_destroy(ctx);
    }

    bv_render_ctx_osmesa_mgr_destroy(mgr);

    /* Clear vlist then destroy the node via the public API */
    bv_node_vlist_set(geom, NULL);
    BV_FREE_VLIST(&vlfree, &vlist);
    bv_node_destroy(geom);
    bv_scene_destroy(scene);
}


/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */
int
main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    bu_setprogname(argv[0]);

    printf("bv_render_ctx tests\n");
    printf("-------------------\n");

    test_available();
    test_null_scene();
    test_invalid_dimensions();
    test_null_destroy();
    test_null_set_size();
    test_null_render_frame();
    test_osmesa_mgr();
    test_lifecycle_empty_scene();
    test_lifecycle_geometry_node();

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    return (fail_count > 0) ? 1 : 0;
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
