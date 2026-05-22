/*         T E S T _ S O U R C E _ I D E N T I T Y . C
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
/** @file libbsg/tests/test_source_identity.c
 *
 * Tests for the Slice 5 pimpl user-pointer API:
 *
 *   bsg_node_uptr_get(node, idx)      — indexed void * retrieval
 *   bsg_node_uptr_set(node, idx, ptr) — indexed void * storage
 *   BSG_NODE_UPTR_MAXIND              — highest valid index
 *
 * Storage is opaque (pimpl): no direct field access is possible or
 * needed.  Slots are lazily allocated on first non-NULL set, so a
 * fresh node carries zero overhead until a pointer is stored.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/defines.h"   /* struct bv_scene_obj */
#include "bsg/node.h"     /* bsg_node_uptr_get/set, BSG_NODE_UPTR_MAXIND */

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


/* Allocate a minimal bv_scene_obj on the stack.
 * Zero-init is sufficient; _uptr_impl is NULL at all-zero. */
static void
node_init(struct bv_scene_obj *s)
{
    memset(s, 0, sizeof(*s));
}


/* ------------------------------------------------------------------ */
/* Test 1: round-trip and NULL-clear for each valid slot               */
/* ------------------------------------------------------------------ */

static int
test_roundtrip(void)
{
    printf("=== Test 1: round-trip get/set for all valid slots ===\n");

    struct bv_scene_obj s;
    node_init(&s);
    bsg_node *n = (bsg_node *)&s;

    /* Sentinel pointers (not dereferenced) */
    void *vals[BSG_NODE_UPTR_MAXIND + 1];
    vals[0] = (void *)0xDEADBEEF;
    vals[1] = (void *)0xCAFEBABE;
    vals[2] = (void *)0xFEEDFACE;

    /* Fresh node: all slots NULL */
    for (int i = 0; i <= BSG_NODE_UPTR_MAXIND; i++) {
if (bsg_node_uptr_get(n, i) != NULL)
    FAIL("fresh node slot should be NULL");
    }

    /* Set each slot and verify */
    for (int i = 0; i <= BSG_NODE_UPTR_MAXIND; i++) {
bsg_node_uptr_set(n, i, vals[i]);
if (bsg_node_uptr_get(n, i) != vals[i])
    FAIL("round-trip mismatch");
    }

    /* Clear each slot and verify NULL */
    for (int i = 0; i <= BSG_NODE_UPTR_MAXIND; i++) {
bsg_node_uptr_set(n, i, NULL);
if (bsg_node_uptr_get(n, i) != NULL)
    FAIL("clear to NULL failed");
    }

    /* Manual cleanup: free impl allocated during this test */
    if (n->_uptr_impl) {
bu_free(n->_uptr_impl, "test_roundtrip cleanup");
n->_uptr_impl = NULL;
    }

    PASS("roundtrip");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 2: slot independence — overwriting one does not affect others  */
/* ------------------------------------------------------------------ */

static int
test_independence(void)
{
    printf("=== Test 2: slot independence ===\n");

    struct bv_scene_obj s;
    node_init(&s);
    bsg_node *n = (bsg_node *)&s;

    void *a = (void *)0x1111;
    void *b = (void *)0x2222;
    void *c = (void *)0x3333;

    bsg_node_uptr_set(n, 0, a);
    bsg_node_uptr_set(n, 1, b);
    bsg_node_uptr_set(n, 2, c);

    if (bsg_node_uptr_get(n, 0) != a) FAIL("slot 0 should be a");
    if (bsg_node_uptr_get(n, 1) != b) FAIL("slot 1 should be b");
    if (bsg_node_uptr_get(n, 2) != c) FAIL("slot 2 should be c");

    /* Clear slot 0; slots 1 and 2 must be unaffected */
    bsg_node_uptr_set(n, 0, NULL);
    if (bsg_node_uptr_get(n, 0) != NULL) FAIL("slot 0 should be NULL after clear");
    if (bsg_node_uptr_get(n, 1) != b)    FAIL("slot 1 unchanged after slot 0 clear");
    if (bsg_node_uptr_get(n, 2) != c)    FAIL("slot 2 unchanged after slot 0 clear");

    if (n->_uptr_impl) {
bu_free(n->_uptr_impl, "test_independence cleanup");
n->_uptr_impl = NULL;
    }

    PASS("independence");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 3: NULL-node safety and out-of-range index safety              */
/* ------------------------------------------------------------------ */

static int
test_safety(void)
{
    printf("=== Test 3: safety (NULL node, out-of-range index) ===\n");

    struct bv_scene_obj s;
    node_init(&s);
    bsg_node *n = (bsg_node *)&s;
    void *val = (void *)0xABCDABCD;

    /* NULL node: both accessors must be no-ops */
    bsg_node_uptr_set(NULL, 0, val); /* no-op */
    if (bsg_node_uptr_get(NULL, 0) != NULL)
FAIL("get(NULL, 0) should return NULL");

    /* Negative index */
    bsg_node_uptr_set(n, -1, val); /* no-op */
    if (bsg_node_uptr_get(n, -1) != NULL)
FAIL("get(n, -1) should return NULL");

    /* Index beyond BSG_NODE_UPTR_MAXIND */
    bsg_node_uptr_set(n, BSG_NODE_UPTR_MAXIND + 1, val); /* no-op */
    if (bsg_node_uptr_get(n, BSG_NODE_UPTR_MAXIND + 1) != NULL)
FAIL("get(n, MAXIND+1) should return NULL");

    /* The valid slots must not have been touched by the out-of-range calls */
    for (int i = 0; i <= BSG_NODE_UPTR_MAXIND; i++) {
if (bsg_node_uptr_get(n, i) != NULL)
    FAIL("valid slot dirtied by out-of-range set");
    }

    PASS("safety");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 4: lazy allocation — NULL-set on fresh node does not allocate  */
/* ------------------------------------------------------------------ */

static int
test_lazy_alloc(void)
{
    printf("=== Test 4: lazy allocation (NULL-set skip) ===\n");

    struct bv_scene_obj s;
    node_init(&s);
    bsg_node *n = (bsg_node *)&s;

    /* Setting NULL on a fresh node must not crash; _uptr_impl stays NULL */
    for (int i = 0; i <= BSG_NODE_UPTR_MAXIND; i++)
bsg_node_uptr_set(n, i, NULL);

    if (n->_uptr_impl != NULL)
FAIL("_uptr_impl should remain NULL after NULL-only sets");

    /* Now set a real value; impl must be allocated */
    bsg_node_uptr_set(n, 0, (void *)0x9999);
    if (n->_uptr_impl == NULL)
FAIL("_uptr_impl should be allocated after non-NULL set");

    bu_free(n->_uptr_impl, "test_lazy_alloc cleanup");
    n->_uptr_impl = NULL;

    PASS("lazy_alloc");
    return 0;
}


int
main(int UNUSED(argc), const char **argv)
{
    bu_setprogname(argv[0]);

    int failures = 0;
    failures += test_roundtrip();
    failures += test_independence();
    failures += test_safety();
    failures += test_lazy_alloc();

    if (failures) {
printf("FAIL: %d test group(s) failed\n", failures);
return 1;
    }

    printf("PASS: all uptr tests passed\n");
    return 0;
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
