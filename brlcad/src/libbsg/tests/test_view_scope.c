/*           T E S T _ V I E W _ S C O P E . C
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
/** @file libbsg/tests/test_view_scope.c
 *
 * Phase V1 unit tests: BSG_NODE_VIEW_SCOPE lifecycle and visibility predicate.
 *
 * Tests (no display manager, no .g file required):
 *   1. create         — bsg_view_scope_create returns a BSG_NODE_VIEW_SCOPE node.
 *   2. visible_match  — bsg_view_scope_visible returns 1 when v matches s_v.
 *   3. visible_nomatch — bsg_view_scope_visible returns 0 when v != s_v.
 *   4. visible_shared — node with s_v==NULL is visible to any view.
 *   5. null_guards    — NULL inputs must not crash.
 *   6. destroy        — bsg_view_scope_destroy clears children and frees node.
 *   7. wrong_type     — bsg_view_scope_visible returns 0 for non-scope nodes.
 *
 * Usage: test_bsg_view_scope
 *   Returns 0 on success, non-zero on failure.
 */

#include "common.h"

#include <stdio.h>

#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bv/defines.h"
#include "bv/util.h"

#include "bsg/defines.h"
#include "bsg/node_group.h"
#include "bsg/view_scope.h"

static int g_fail = 0;

#define CHECK(cond, msg) \
    do { \
	if (!(cond)) { \
	    bu_log("FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); \
	    g_fail++; \
	} \
    } while (0)


/* ---- helpers -------------------------------------------------------- */

static struct bview *
make_view(const char *name)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "%s", name);
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v) return;
    bv_free(v);
    bu_free(v, "test view");
}


/* ================================================================== */
/* Tests                                                               */
/* ================================================================== */

/* Test 1: create */
static int
test_create(void)
{
    printf("=== Test 1: create ===\n");
    struct bview *v = make_view("t1");

    bsg_node *scope = bsg_view_scope_create(v);
    CHECK(scope != NULL, "bsg_view_scope_create returned non-NULL");

    struct bv_scene_obj *s = (struct bv_scene_obj *)scope;
    CHECK((s->bsg.bsg_kind & BSG_NODE_VIEW_SCOPE) != 0,
	  "type flag is BSG_NODE_VIEW_SCOPE");
    CHECK(s->s_v == v, "s_v set to the creating view");
    CHECK(s->bsg.bsg_flag == UP, "s_flag is UP");

    bsg_view_scope_destroy(scope);
    free_view(v);
    return 0;
}


/* Test 2: visible_match — visible when view matches */
static int
test_visible_match(void)
{
    printf("=== Test 2: visible_match ===\n");
    struct bview *v = make_view("t2");

    bsg_node *scope = bsg_view_scope_create(v);
    CHECK(scope != NULL, "create");

    CHECK(bsg_view_scope_visible(scope, v) == 1,
	  "visible to owner view");

    bsg_view_scope_destroy(scope);
    free_view(v);
    return 0;
}


/* Test 3: visible_nomatch — not visible when view differs */
static int
test_visible_nomatch(void)
{
    printf("=== Test 3: visible_nomatch ===\n");
    struct bview *v1 = make_view("t3a");
    struct bview *v2 = make_view("t3b");

    bsg_node *scope = bsg_view_scope_create(v1);
    CHECK(scope != NULL, "create");

    CHECK(bsg_view_scope_visible(scope, v2) == 0,
	  "not visible to a different view");

    bsg_view_scope_destroy(scope);
    free_view(v1);
    free_view(v2);
    return 0;
}


/* Test 4: visible_shared — NULL s_v means shared, visible to all views */
static int
test_visible_shared(void)
{
    printf("=== Test 4: visible_shared ===\n");
    struct bview *v1 = make_view("t4a");
    struct bview *v2 = make_view("t4b");

    bsg_node *scope = bsg_view_scope_create(v1);
    CHECK(scope != NULL, "create");

    /* Manually clear s_v to simulate a shared scope. */
    ((struct bv_scene_obj *)scope)->s_v = NULL;

    CHECK(bsg_view_scope_visible(scope, v1) == 1,
	  "shared scope visible to v1");
    CHECK(bsg_view_scope_visible(scope, v2) == 1,
	  "shared scope visible to v2");
    CHECK(bsg_view_scope_visible(scope, NULL) == 1,
	  "shared scope visible to NULL view");

    bsg_view_scope_destroy(scope);
    free_view(v1);
    free_view(v2);
    return 0;
}


/* Test 5: null_guards */
static int
test_null_guards(void)
{
    printf("=== Test 5: null_guards ===\n");
    struct bview *v = make_view("t5");

    /* create with NULL view must return NULL without crash. */
    bsg_node *scope = bsg_view_scope_create(NULL);
    CHECK(scope == NULL, "create(NULL) returns NULL");

    /* visible with NULL node must return 0 without crash. */
    CHECK(bsg_view_scope_visible(NULL, v) == 0,
	  "visible(NULL, v) returns 0");

    /* visible with NULL view on a valid scope. */
    bsg_node *s2 = bsg_view_scope_create(v);
    CHECK(s2 != NULL, "create non-NULL");
    CHECK(bsg_view_scope_visible(s2, NULL) == 0,
	  "visible(scope, NULL) returns 0 for per-view scope");
    bsg_view_scope_destroy(s2);

    /* destroy with NULL must not crash. */
    bsg_view_scope_destroy(NULL);

    free_view(v);
    return 0;
}


/* Test 6: destroy — children ptbl is reset, node is freed */
static int
test_destroy(void)
{
    printf("=== Test 6: destroy ===\n");
    struct bview *v = make_view("t6");

    bsg_node *scope = bsg_view_scope_create(v);
    CHECK(scope != NULL, "create");

    /* Add a dummy child reference (just the pointer — we own it). */
    bsg_node *child = bsg_group_create(v);
    CHECK(child != NULL, "child create");
    bu_ptbl_ins(&((struct bv_scene_obj *)scope)->bsg.bsg_children, (long *)child);
    CHECK(BU_PTBL_LEN(&((struct bv_scene_obj *)scope)->bsg.bsg_children) == 1,
	  "one child before destroy");

    /* destroy resets children and frees the node. */
    bsg_view_scope_destroy(scope);

    /* Clean up the child separately. */
    bsg_group_destroy(child);

    free_view(v);
    return 0;
}


/* Test 7: wrong_type — visible returns 0 for a non-scope node */
static int
test_wrong_type(void)
{
    printf("=== Test 7: wrong_type ===\n");
    struct bview *v = make_view("t7");

    bsg_node *grp = bsg_group_create(v);
    CHECK(grp != NULL, "group create");

    CHECK(bsg_view_scope_visible(grp, v) == 0,
	  "visible returns 0 for a BSG_NODE_GROUP node");

    bsg_group_destroy(grp);
    free_view(v);
    return 0;
}


/* ================================================================== */
/* main                                                                */
/* ================================================================== */

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);
    (void)argc;

    test_create();
    test_visible_match();
    test_visible_nomatch();
    test_visible_shared();
    test_null_guards();
    test_destroy();
    test_wrong_type();

    if (g_fail == 0)
	printf("All tests PASSED\n");
    else
	printf("%d test(s) FAILED\n", g_fail);

    return (g_fail != 0) ? 1 : 0;
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
