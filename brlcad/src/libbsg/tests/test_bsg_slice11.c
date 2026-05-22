/*         T E S T _ B S G _ S L I C E 1 1 . C
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
/** @file libbsg/tests/test_bsg_slice11.c
 *
 * Slice 11 (bv_scene_obj_migrate) unit tests: BSG renderer attachment.
 *
 * Verifies the BSG-native renderer attachment API introduced in
 * bsg/renderer_attach.h:
 *
 *   11A - BSG_BACKEND_* constants have expected numeric values.
 *
 *   11B - bsg_node_backend_get/set round-trip: attach a descriptor,
 *         retrieve it, verify fields.
 *
 *   11C - bsg_node_backend_release: fires the free callback and clears
 *         the attachment slot.
 *
 *   11D - bsg_node_backend_invalidate: fires the invalidate callback
 *         without clearing the attachment slot.
 *
 *   11E - bsg_node_stale: recursively invalidates a node and all its
 *         children.
 *
 *   11F - NULL-argument safety: all public functions tolerate NULL
 *         without crashing.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/defines.h"
#include "bsg/node.h"
#include "bsg/renderer_attach.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

/* Per-test callback counters. */
static int g_free_calls       = 0;
static int g_invalidate_calls = 0;
static struct bv_scene_obj *g_last_obj = NULL;

static void
stub_free(struct bv_scene_obj *s)
{
    g_free_calls++;
    g_last_obj = s;
    /* Detach the descriptor from the node before the caller frees it. */
    struct bsg_renderer_attach *be = bsg_node_backend_get((const bsg_node *)s);
    bsg_node_backend_set((bsg_node *)s, NULL);
    BU_PUT(be, struct bsg_renderer_attach);
}

static void
stub_invalidate(struct bv_scene_obj *s)
{
    g_invalidate_calls++;
    g_last_obj = s;
}

/* ------------------------------------------------------------------ */
/* View / node helpers                                                  */
/* ------------------------------------------------------------------ */

static struct bview *
_make_view(void)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "ra_test_view");
    return v;
}

static void
_free_view(struct bview *v)
{
    if (!v)
	return;
    bv_free(v);
    bu_free(v, "ra_test_view");
}

static bsg_node *
_make_node(struct bview *v)
{
    return bsg_node_create(v, BSG_NODE_SHAPE);
}

static struct bsg_renderer_attach *
_alloc_attach(void)
{
    struct bsg_renderer_attach *be;
    BU_GET(be, struct bsg_renderer_attach);
    be->type_tag   = BSG_BACKEND_GL;
    be->handle     = NULL;
    be->free       = stub_free;
    be->invalidate = stub_invalidate;
    return be;
}

/* ------------------------------------------------------------------ */
/* Test 11A: constant values                                            */
/* ------------------------------------------------------------------ */

static int
test_constants(void)
{
    printf("=== Test 11A: BSG_BACKEND_* constants ===\n");

    if (BSG_BACKEND_NONE != 0u)
	FAIL("BSG_BACKEND_NONE must be 0");
    if (BSG_BACKEND_GL != 1u)
	FAIL("BSG_BACKEND_GL must be 1");

    /* Compat aliases must map to the same values. */
    if (BV_BACKEND_NONE != BSG_BACKEND_NONE)
	FAIL("BV_BACKEND_NONE must equal BSG_BACKEND_NONE");
    if (BV_BACKEND_GL != BSG_BACKEND_GL)
	FAIL("BV_BACKEND_GL must equal BSG_BACKEND_GL");

    PASS("BSG_BACKEND_NONE == 0, BSG_BACKEND_GL == 1, compat aliases match");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test 11B: get/set round-trip                                         */
/* ------------------------------------------------------------------ */

static int
test_get_set(void)
{
    printf("=== Test 11B: backend get/set round-trip ===\n");

    struct bview *v   = _make_view();
    bsg_node     *n   = _make_node(v);

    if (bsg_node_backend_get(n) != NULL)
	FAIL("fresh node must have NULL backend attachment");

    struct bsg_renderer_attach *be = _alloc_attach();
    bsg_node_backend_set(n, be);

    struct bsg_renderer_attach *got = bsg_node_backend_get(n);
    if (got != be)
	FAIL("bsg_node_backend_get must return the set descriptor");
    if (got->type_tag != BSG_BACKEND_GL)
	FAIL("type_tag must survive the round-trip");
    if (got->free != stub_free)
	FAIL("free callback must survive the round-trip");
    if (got->invalidate != stub_invalidate)
	FAIL("invalidate callback must survive the round-trip");

    /* Detach before destroying the node to avoid a double-free. */
    bsg_node_backend_set(n, NULL);
    BU_PUT(be, struct bsg_renderer_attach);

    bsg_node_destroy(n);
    _free_view(v);

    PASS("get/set round-trip preserves all fields");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test 11C: bsg_node_backend_release                                  */
/* ------------------------------------------------------------------ */

static int
test_release(void)
{
    printf("=== Test 11C: bsg_node_backend_release ===\n");

    struct bview *v = _make_view();
    bsg_node     *n = _make_node(v);

    g_free_calls = 0;
    g_last_obj   = NULL;

    struct bsg_renderer_attach *be = _alloc_attach();
    bsg_node_backend_set(n, be);

    bsg_node_backend_release(n);

    if (g_free_calls != 1)
	FAIL("free callback must fire exactly once");
    if (g_last_obj != (struct bv_scene_obj *)n)
	FAIL("free callback must receive the correct node");
    if (bsg_node_backend_get(n) != NULL)
	FAIL("attachment slot must be NULL after release");

    /* Second release must be a no-op (slot already NULL). */
    g_free_calls = 0;
    bsg_node_backend_release(n);
    if (g_free_calls != 0)
	FAIL("second release on NULL slot must not fire the callback");

    bsg_node_destroy(n);
    _free_view(v);

    PASS("bsg_node_backend_release fires free callback and clears slot");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test 11D: bsg_node_backend_invalidate                               */
/* ------------------------------------------------------------------ */

static int
test_invalidate(void)
{
    printf("=== Test 11D: bsg_node_backend_invalidate ===\n");

    struct bview *v = _make_view();
    bsg_node     *n = _make_node(v);

    g_invalidate_calls = 0;
    g_last_obj         = NULL;

    struct bsg_renderer_attach *be = _alloc_attach();
    bsg_node_backend_set(n, be);

    bsg_node_backend_invalidate(n);

    if (g_invalidate_calls != 1)
	FAIL("invalidate callback must fire exactly once");
    if (g_last_obj != (struct bv_scene_obj *)n)
	FAIL("invalidate callback must receive the correct node");
    if (bsg_node_backend_get(n) == NULL)
	FAIL("attachment slot must remain set after invalidate");

    /* Verify the compat wrapper also routes to the BSG implementation. */
    g_invalidate_calls = 0;
    bv_scene_obj_invalidate_backend((struct bv_scene_obj *)n);
    if (g_invalidate_calls != 1)
	FAIL("bv_scene_obj_invalidate_backend must delegate to BSG invalidate");

    /* Clean up: manually release the descriptor. */
    bsg_node_backend_release(n);

    bsg_node_destroy(n);
    _free_view(v);

    PASS("bsg_node_backend_invalidate fires callback without clearing slot");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test 11E: bsg_node_stale — recursive invalidation                   */
/* ------------------------------------------------------------------ */

static int
test_stale_recursive(void)
{
    printf("=== Test 11E: bsg_node_stale (recursive) ===\n");

    struct bview *v    = _make_view();
    bsg_node     *root = bsg_node_create(v, BSG_NODE_GROUP);
    bsg_node     *c0   = bsg_node_create_child(v, BSG_NODE_SHAPE);
    bsg_node     *c1   = bsg_node_create_child(v, BSG_NODE_SHAPE);

    /* Wire children under root */
    bsg_node_add_child(root, c0);
    bsg_node_add_child(root, c1);

    /* Attach descriptors to all three nodes. */
    g_invalidate_calls = 0;

    struct bsg_renderer_attach *be_root = _alloc_attach();
    struct bsg_renderer_attach *be_c0   = _alloc_attach();
    struct bsg_renderer_attach *be_c1   = _alloc_attach();
    bsg_node_backend_set(root, be_root);
    bsg_node_backend_set(c0,   be_c0);
    bsg_node_backend_set(c1,   be_c1);

    bsg_node_stale(root);

    if (g_invalidate_calls != 3)
	FAIL("bsg_node_stale must invalidate root + 2 children (3 total)");

    /* Verify the compat bv_obj_stale wrapper also recurses. */
    g_invalidate_calls = 0;
    bv_obj_stale((struct bv_scene_obj *)root);
    if (g_invalidate_calls != 3)
	FAIL("bv_obj_stale must also recurse via BSG (3 invalidations)");

    /* Release descriptors before tearing down the tree. */
    bsg_node_backend_release(root);
    bsg_node_backend_release(c0);
    bsg_node_backend_release(c1);

    bsg_node_destroy(root);
    _free_view(v);

    PASS("bsg_node_stale recursively invalidates root and all children");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test 11F: NULL-argument safety                                       */
/* ------------------------------------------------------------------ */

static int
test_null_safety(void)
{
    printf("=== Test 11F: NULL-argument safety ===\n");

    /* These must not crash. */
    bsg_node_backend_get(NULL);
    bsg_node_backend_set(NULL, NULL);
    bsg_node_backend_release(NULL);
    bsg_node_backend_invalidate(NULL);
    bsg_node_stale(NULL);
    bv_scene_obj_release_backend(NULL);
    bv_scene_obj_invalidate_backend(NULL);
    bv_obj_stale(NULL);

    PASS("NULL arguments do not crash any public function");
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);
    (void)argc;

    int failures = 0;

    failures += test_constants();
    failures += test_get_set();
    failures += test_release();
    failures += test_invalidate();
    failures += test_stale_recursive();
    failures += test_null_safety();

    if (failures) {
	printf("\nSLICE 11: %d test(s) FAILED\n", failures);
	return 1;
    }
    printf("\nSLICE 11: all tests PASSED\n");
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
