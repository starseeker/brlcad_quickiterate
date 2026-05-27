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

#include "bu/app.h"
#include "bu/malloc.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/node.h"
#include "bsg/node_shape.h"
#include "bsg/render.h"

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
