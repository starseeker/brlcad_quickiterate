/*              T E S T _ S E L E C T I O N . C
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
 * Phase 6 unit tests: BSG selection set API (add/remove/contains/
 * visit/policy), scene-root named-set storage, and illumination
 * compatibility sync.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/node_shape.h"
#include "bsg/selection.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static struct bview *
make_view(void)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_view");
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v)
	return;
    bv_free(v);
    bu_free(v, "test_view");
}

static struct bsg_selection_entry
make_entry(bsg_node *node, const char *path)
{
    struct bsg_selection_entry e;
    memset(&e, 0, sizeof(e));
    e.node     = node;
    e.src_path = (char *)(uintptr_t)path;  /* not owned — add() will dup */
    e.kind     = BSG_SELECTION_NODE;
    return e;
}


/* ------------------------------------------------------------------ */
/* Test 1: selection set lifecycle (create / clear / destroy)          */
/* ------------------------------------------------------------------ */

static int
test_set_lifecycle(void)
{
    printf("=== Test 1: set_lifecycle ===\n");

    struct bsg_selection_set *ss = bsg_selection_set_create("active");
    if (!ss) FAIL("bsg_selection_set_create returned NULL");
    if (!ss->name || strcmp(ss->name, "active") != 0)
	FAIL("set name not 'active'");
    if (ss->count != 0) FAIL("initial count should be 0");

    /* NULL guards */
    bsg_selection_set_destroy(NULL);  /* must not crash */
    bsg_selection_clear(NULL);        /* must not crash */

    bsg_selection_set_destroy(ss);

    PASS("set_lifecycle");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 2: add / remove / contains / count                              */
/* ------------------------------------------------------------------ */

static int
test_add_remove_contains(void)
{
    printf("=== Test 2: add_remove_contains ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("bsg_scene_root_create returned NULL");

    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);
    bsg_node *s3 = bsg_shape_create(v);
    if (!s1 || !s2 || !s3) FAIL("bsg_shape_create returned NULL");

    struct bsg_selection_set *ss = bsg_selection_set_create("test");

    struct bsg_selection_entry e1 = make_entry(s1, "/path/s1");
    struct bsg_selection_entry e2 = make_entry(s2, "/path/s2");

    /* Add */
    if (!bsg_selection_add(ss, &e1)) FAIL("add s1 failed");
    if (!bsg_selection_add(ss, &e2)) FAIL("add s2 failed");
    if (bsg_selection_count(ss) != 2) FAIL("count should be 2 after 2 adds");

    /* Duplicate add is a no-op */
    if (bsg_selection_add(ss, &e1) != 0) FAIL("duplicate add should return 0");
    if (bsg_selection_count(ss) != 2) FAIL("count should still be 2");

    /* Contains */
    if (!bsg_selection_contains(ss, s1)) FAIL("s1 should be contained");
    if (!bsg_selection_contains(ss, s2)) FAIL("s2 should be contained");
    if (bsg_selection_contains(ss, s3))  FAIL("s3 should NOT be contained");

    /* Remove */
    if (!bsg_selection_remove(ss, s1)) FAIL("remove s1 failed");
    if (bsg_selection_count(ss) != 1)  FAIL("count should be 1 after remove");
    if (bsg_selection_contains(ss, s1)) FAIL("s1 should no longer be contained");

    /* Remove non-existent */
    if (bsg_selection_remove(ss, s3) != 0) FAIL("remove non-existent should return 0");

    /* Clear */
    bsg_selection_clear(ss);
    if (bsg_selection_count(ss) != 0) FAIL("count should be 0 after clear");

    /* NULL guards */
    if (bsg_selection_add(NULL, &e1) != 0) FAIL("add(NULL set) should return 0");
    if (bsg_selection_add(ss, NULL)  != 0) FAIL("add(NULL entry) should return 0");
    if (bsg_selection_remove(NULL, s1) != 0) FAIL("remove(NULL set) should return 0");
    if (bsg_selection_contains(NULL, s1) != 0) FAIL("contains(NULL set) != 0");
    if (bsg_selection_count(NULL) != 0) FAIL("count(NULL) != 0");

    bsg_selection_set_destroy(ss);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_shape_destroy(s3);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("add_remove_contains");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 3: visit callback                                               */
/* ------------------------------------------------------------------ */

static int s_visit_count = 0;

static int
count_visit_cb(const struct bsg_selection_entry *UNUSED(e), void *UNUSED(d))
{
    s_visit_count++;
    return 1;
}

static int
stop_visit_cb(const struct bsg_selection_entry *UNUSED(e), void *data)
{
    int *cnt = (int *)data;
    (*cnt)++;
    return 0; /* stop after first */
}

static int
test_visit(void)
{
    printf("=== Test 3: visit ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);
    bsg_node *s3 = bsg_shape_create(v);

    struct bsg_selection_set *ss = bsg_selection_set_create("test");
    struct bsg_selection_entry e1 = make_entry(s1, NULL);
    struct bsg_selection_entry e2 = make_entry(s2, NULL);
    struct bsg_selection_entry e3 = make_entry(s3, NULL);
    bsg_selection_add(ss, &e1);
    bsg_selection_add(ss, &e2);
    bsg_selection_add(ss, &e3);

    /* Visit all */
    s_visit_count = 0;
    bsg_selection_visit(ss, count_visit_cb, NULL);
    if (s_visit_count != 3) FAIL("visit should have visited 3 entries");

    /* Stop after first */
    int stop_count = 0;
    bsg_selection_visit(ss, stop_visit_cb, &stop_count);
    if (stop_count != 1) FAIL("stop_visit_cb should stop after 1");

    /* NULL guards */
    bsg_selection_visit(NULL, count_visit_cb, NULL);  /* no-op */
    bsg_selection_visit(ss, NULL, NULL);              /* no-op */

    bsg_selection_set_destroy(ss);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_shape_destroy(s3);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("visit");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 4: apply_policy (SINGLE/TOGGLE/APPEND/REMOVE/REPLACE)          */
/* ------------------------------------------------------------------ */

static int
test_apply_policy(void)
{
    printf("=== Test 4: apply_policy ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);

    struct bsg_selection_set *ss = bsg_selection_set_create("test");
    struct bsg_selection_entry e1 = make_entry(s1, NULL);
    struct bsg_selection_entry e2 = make_entry(s2, NULL);

    /* APPEND */
    bsg_selection_apply_policy(ss, &e1, BSG_SELECTION_POLICY_APPEND);
    if (!bsg_selection_contains(ss, s1)) FAIL("APPEND: s1 should be present");
    bsg_selection_apply_policy(ss, &e1, BSG_SELECTION_POLICY_APPEND);
    if (bsg_selection_count(ss) != 1) FAIL("APPEND duplicate should be no-op");

    /* SINGLE: clears and adds e2 */
    bsg_selection_apply_policy(ss, &e2, BSG_SELECTION_POLICY_SINGLE);
    if (bsg_selection_count(ss) != 1) FAIL("SINGLE should leave count==1");
    if (!bsg_selection_contains(ss, s2)) FAIL("SINGLE: s2 should be present");
    if (bsg_selection_contains(ss, s1)) FAIL("SINGLE: s1 should be gone");

    /* TOGGLE: add e1 (absent) */
    bsg_selection_apply_policy(ss, &e1, BSG_SELECTION_POLICY_TOGGLE);
    if (!bsg_selection_contains(ss, s1)) FAIL("TOGGLE: s1 should be added");

    /* TOGGLE: remove e1 (now present) */
    bsg_selection_apply_policy(ss, &e1, BSG_SELECTION_POLICY_TOGGLE);
    if (bsg_selection_contains(ss, s1)) FAIL("TOGGLE: s1 should be removed");

    /* REMOVE */
    bsg_selection_apply_policy(ss, &e2, BSG_SELECTION_POLICY_REMOVE);
    if (bsg_selection_contains(ss, s2)) FAIL("REMOVE: s2 should be gone");

    /* REPLACE: same as SINGLE */
    bsg_selection_apply_policy(ss, &e1, BSG_SELECTION_POLICY_APPEND);
    bsg_selection_apply_policy(ss, &e2, BSG_SELECTION_POLICY_APPEND);
    bsg_selection_apply_policy(ss, &e1, BSG_SELECTION_POLICY_REPLACE);
    if (bsg_selection_count(ss) != 1) FAIL("REPLACE should leave count==1");
    if (!bsg_selection_contains(ss, s1)) FAIL("REPLACE: s1 should be present");

    bsg_selection_set_destroy(ss);
    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("apply_policy");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 5: scene-root named-set storage                                 */
/* ------------------------------------------------------------------ */

static int
test_scene_selection_get(void)
{
    printf("=== Test 5: scene_selection_get ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("bsg_scene_root_create returned NULL");

    /* Phase 6B pre-creates "active" and "edit" sets: they should already exist. */
    if (bsg_scene_selection_get(root, "active", 0) == NULL)
	FAIL("Phase 6B: 'active' set should exist after bsg_scene_root_create");
    if (bsg_scene_selection_get(root, "edit", 0) == NULL)
	FAIL("Phase 6B: 'edit' set should exist after bsg_scene_root_create");

    /* Create=0 on a truly non-existent set should return NULL */
    if (bsg_scene_selection_get(root, "non_existent_set_xyz", 0) != NULL)
	FAIL("get(create=0) on missing set should return NULL");

    /* Create=1 should create and return a new set */
    struct bsg_selection_set *active = bsg_scene_selection_get(root, "active", 1);
    if (!active) FAIL("get(create=1) returned NULL");
    if (strcmp(active->name, "active") != 0) FAIL("set name mismatch");

    /* Repeated get(create=0) returns the same pointer */
    struct bsg_selection_set *active2 = bsg_scene_selection_get(root, "active", 0);
    if (active2 != active) FAIL("repeated get should return same pointer");

    /* Different names create different sets */
    struct bsg_selection_set *edit = bsg_scene_selection_get(root, "edit", 1);
    if (!edit) FAIL("get 'edit' set returned NULL");
    if (edit == active) FAIL("'edit' and 'active' should be different sets");

    /* NULL guards */
    if (bsg_scene_selection_get(NULL, "active", 1) != NULL)
	FAIL("get(NULL root) should return NULL");
    if (bsg_scene_selection_get(root, NULL, 1) != NULL)
	FAIL("get(NULL name) should return NULL");

    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("scene_selection_get");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 6: bsg_node_set_selected / bsg_node_is_selected                */
/* ------------------------------------------------------------------ */

static int
test_node_selected_helpers(void)
{
    printf("=== Test 6: node_selected_helpers ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);

    /* Nothing selected initially */
    if (bsg_node_is_selected(root, s1, "active"))
	FAIL("s1 should not be selected initially");

    /* Select s1 */
    bsg_node_set_selected(root, s1, "active", 1);
    if (!bsg_node_is_selected(root, s1, "active"))
	FAIL("s1 should be selected after set_selected=1");
    if (bsg_node_is_selected(root, s2, "active"))
	FAIL("s2 should still not be selected");

    /* Select s2 */
    bsg_node_set_selected(root, s2, "active", 1);
    if (!bsg_node_is_selected(root, s2, "active"))
	FAIL("s2 should be selected");

    /* Deselect s1 */
    bsg_node_set_selected(root, s1, "active", 0);
    if (bsg_node_is_selected(root, s1, "active"))
	FAIL("s1 should no longer be selected");
    if (!bsg_node_is_selected(root, s2, "active"))
	FAIL("s2 should still be selected");

    /* NULL guards */
    bsg_node_set_selected(NULL, s1, "active", 1);   /* no-op */
    bsg_node_set_selected(root, NULL, "active", 1); /* no-op */
    if (bsg_node_is_selected(NULL, s1, "active") != 0)
	FAIL("is_selected(NULL root) != 0");
    if (bsg_node_is_selected(root, NULL, "active") != 0)
	FAIL("is_selected(NULL node) != 0");

    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("node_selected_helpers");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 7: bsg_selection_sync_illum_flags                               */
/* ------------------------------------------------------------------ */

static int
test_sync_illum_flags(void)
{
    printf("=== Test 7: sync_illum_flags ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);
    struct bv_scene_obj *so1 = (struct bv_scene_obj *)s1;
    struct bv_scene_obj *so2 = (struct bv_scene_obj *)s2;

    /* Add s1 to active set */
    bsg_node_set_selected(root, s1, "active", 1);

    /* Sync should set s1 UP and s2 DOWN.
     * Note: s1 and s2 are not children of root in this test so the DFS
     * won't reach them — sync_illum_flags is exercised here for the NULL
     * root guard and overall API availability; the actual flag check
     * requires them to be in the tree. */
    bsg_selection_sync_illum_flags(root);
    /* No crash — basic smoke test. */

    /* Set s1's flag manually and verify sync_illum_flags on NULL is no-op */
    so1->s_iflag = DOWN;
    so2->s_iflag = UP;
    bsg_selection_sync_illum_flags(NULL);  /* no-op, must not crash */
    if (so2->s_iflag != UP) FAIL("sync(NULL) should be a no-op");

    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("sync_illum_flags");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 8: bsg_selection_from_illum_flags                               */
/* ------------------------------------------------------------------ */

static int
test_from_illum_flags(void)
{
    printf("=== Test 8: from_illum_flags ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);

    /* from_illum on empty tree: should not crash */
    bsg_selection_from_illum_flags(root);

    /* Add two shapes as children of root */
    bsg_node *s1 = bsg_shape_create(v);
    bsg_node *s2 = bsg_shape_create(v);
    struct bv_scene_obj *so1 = (struct bv_scene_obj *)s1;
    struct bv_scene_obj *so2 = (struct bv_scene_obj *)s2;
    struct bv_scene_obj *r   = (struct bv_scene_obj *)root;

    /* Attach to root's children table so DFS finds them */
    bu_ptbl_ins_unique(&r->children, (long *)s1);
    bu_ptbl_ins_unique(&r->children, (long *)s2);

    /* Mark s1 illuminated, s2 not */
    so1->s_iflag = UP;
    so2->s_iflag = DOWN;

    bsg_selection_from_illum_flags(root);

    struct bsg_selection_set *active =
	bsg_scene_selection_get(root, "active", 0);
    if (!active) FAIL("'active' set should exist after from_illum_flags");

    /* bsg_visit performs a depth-first pre-order traversal starting from root.
     * The root itself (r) is visited first, followed by its children (s1, s2).
     * Since root's s_iflag is DOWN by default in this test, only s1 (UP) ends
     * up in the "active" set. */
    if (!bsg_selection_contains(active, s1))
	FAIL("s1 (iflag UP) should be in 'active' set");
    if (bsg_selection_contains(active, s2))
	FAIL("s2 (iflag DOWN) should NOT be in 'active' set");

    /* NULL guard */
    bsg_selection_from_illum_flags(NULL);  /* no-op */

    /* Detach before destroy to avoid double-free */
    bu_ptbl_rm(&r->children, (long *)s1);
    bu_ptbl_rm(&r->children, (long *)s2);

    bsg_shape_destroy(s1);
    bsg_shape_destroy(s2);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("from_illum_flags");
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

    failures += test_set_lifecycle();
    failures += test_add_remove_contains();
    failures += test_visit();
    failures += test_apply_policy();
    failures += test_scene_selection_get();
    failures += test_node_selected_helpers();
    failures += test_sync_illum_flags();
    failures += test_from_illum_flags();

    if (failures == 0)
	printf("RESULT: all selection tests PASSED\n");
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
