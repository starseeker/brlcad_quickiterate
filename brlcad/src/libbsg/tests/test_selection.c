/*            T E S T _ S E L E C T I O N . C
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
/** @file libbsg/tests/test_selection.c
 *
 * Phase 6 unit tests: first-class selection model.
 */

#include "common.h"

#include <stdio.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/node_shape.h"
#include "bsg/selection.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static struct bsg_view *
make_view(void)
{
    struct bsg_view *v;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "sel_test_view");
    return v;
}

static void
free_view(struct bsg_view *v)
{
    if (!v) return;
    bsg_view_free(v);
    bu_free(v, "sel_test_view");
}


/* ------------------------------------------------------------------ */
/* Test 1: create / destroy lifecycle                                   */
/* ------------------------------------------------------------------ */

static int
test_create_destroy(void)
{
    printf("=== Test 1: create_destroy ===\n");

    struct bsg_selection *sel = bsg_selection_create();
    if (!sel) FAIL("bsg_selection_create returned NULL");

    if (bsg_selection_count(sel) != 0) FAIL("initial count should be 0");

    bsg_selection_destroy(sel);
    bsg_selection_destroy(NULL);   /* must not crash */

    PASS("create_destroy");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 2: add / contains / count                                       */
/* ------------------------------------------------------------------ */

static int
test_add_contains_count(void)
{
    printf("=== Test 2: add_contains_count ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("scene_root_create");

    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);
    if (!s1 || !s2) FAIL("shape_create");

    struct bsg_selection *sel = bsg_selection_create();

    bsg_selection_add(sel, s1);
    if (bsg_selection_count(sel) != 1) FAIL("count should be 1 after first add");
    if (!bsg_selection_contains(sel, s1)) FAIL("s1 not found after add");
    if (bsg_selection_contains(sel, s2))  FAIL("s2 should not be in sel");

    /* Duplicate add is a no-op */
    bsg_selection_add(sel, s1);
    if (bsg_selection_count(sel) != 1) FAIL("duplicate add should be no-op");

    bsg_selection_add(sel, s2);
    if (bsg_selection_count(sel) != 2) FAIL("count should be 2");

    bsg_selection_destroy(sel);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("add_contains_count");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 3: remove                                                       */
/* ------------------------------------------------------------------ */

static int
test_remove(void)
{
    printf("=== Test 3: remove ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);

    struct bsg_selection *sel = bsg_selection_create();
    bsg_selection_add(sel, s1);
    bsg_selection_add(sel, s2);

    bsg_selection_remove(sel, s1);
    if (bsg_selection_contains(sel, s1)) FAIL("s1 should be gone after remove");
    if (bsg_selection_count(sel) != 1)   FAIL("count should be 1 after remove");

    /* remove of non-member is a no-op */
    bsg_selection_remove(sel, s1);
    if (bsg_selection_count(sel) != 1) FAIL("remove of absent node should be no-op");

    bsg_selection_destroy(sel);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("remove");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 4: clear                                                        */
/* ------------------------------------------------------------------ */

static int
test_clear(void)
{
    printf("=== Test 4: clear ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);

    struct bsg_selection *sel = bsg_selection_create();
    bsg_selection_add(sel, s1);
    bsg_selection_add(sel, s2);
    bsg_selection_clear(sel);
    if (bsg_selection_count(sel) != 0) FAIL("count should be 0 after clear");

    bsg_selection_destroy(sel);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("clear");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 5: highlight / unhighlight                                      */
/* ------------------------------------------------------------------ */

static int
test_highlight(void)
{
    printf("=== Test 5: highlight ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);

    struct bsg_selection *sel = bsg_selection_create();
    bsg_selection_add(sel, s1);

    s1->s_iflag = DOWN;
    bsg_selection_highlight(sel);
    if (s1->s_iflag != UP) FAIL("s_iflag should be UP after highlight");

    bsg_selection_unhighlight(sel);
    if (s1->s_iflag != DOWN) FAIL("s_iflag should be DOWN after unhighlight");

    bsg_selection_destroy(sel);
    bsg_shape_destroy(s1);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("highlight");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 6: NULL guards                                                  */
/* ------------------------------------------------------------------ */

static int
test_null_guards(void)
{
    printf("=== Test 6: null_guards ===\n");

    bsg_selection_add(NULL, NULL);
    bsg_selection_remove(NULL, NULL);
    bsg_selection_clear(NULL);
    bsg_selection_highlight(NULL);
    bsg_selection_unhighlight(NULL);

    if (bsg_selection_contains(NULL, NULL) != 0) FAIL("contains(NULL,NULL) should be 0");
    if (bsg_selection_count(NULL) != 0)          FAIL("count(NULL) should be 0");
    if (bsg_selection_nodes(NULL) != NULL)        FAIL("nodes(NULL) should be NULL");

    PASS("null_guards");
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
    failures += test_add_contains_count();
    failures += test_remove();
    failures += test_clear();
    failures += test_highlight();
    failures += test_null_guards();

    if (failures == 0)
	printf("RESULT: all Phase 6 selection tests PASSED\n");
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
