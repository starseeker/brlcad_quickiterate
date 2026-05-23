/*              T E S T _ L O D _ N O D E . C
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
/** @file libbsg/tests/test_lod_node.c
 *
 * Phase L0 unit tests: BSG_NODE_LOD lifecycle and per-view cursor map.
 *
 * Tests (no display manager, no .g file required):
 *   1. create        — bsg_lod_node_create returns a BSG_NODE_LOD node.
 *   2. set_ops       — bsg_lod_node_set_ops stores ops + user_data.
 *   3. attach_level  — levels appear as children in insertion order.
 *   4. cursor_create — bsg_lod_node_get_cursor creates a fresh cursor.
 *   5. cursor_reuse  — second call returns the same cursor.
 *   6. cursor_multi  — multiple views each get an independent cursor.
 *   7. active_level  — bsg_lod_node_active_level returns -1 initially,
 *                      then the level set via the cursor.
 *   8. level_count   — bsg_lod_node_level_count matches children added.
 *   9. synthetic_ops — a synthetic ops set toggles between two children
 *                      and the cursor tracks the selected level.
 *  10. null_guards   — NULL inputs must not crash.
 *  11. ops_free      — the ops->free callback fires when the node is freed.
 *  12. insert_above  — bsg_lod_node_insert_above wraps a leaf in-place.
 *
 * Usage: test_bsg_lod_node
 *   Returns 0 on success, non-zero on failure.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bsg/defines.h"
#include "bsg/util.h"

#include "bsg/lod_ops.h"
#include "bsg/node_group.h"
#include "bsg/node_shape.h"

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
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "%s", name);
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v) return;
    bsg_view_free(v);
    bu_free(v, "test view");
}


/* ---- Synthetic ops state -------------------------------------------- */

struct synth_state {
    int select_calls;   /* how many times select_level was called */
    int activate_calls; /* how many times activate_level was called */
    int stale_result;   /* value is_stale should return */
    int select_result;  /* value select_level should return */
    int free_calls;     /* how many times free was called */
};

static int
synth_select_level(bsg_node *node, struct bview *v)
{
    (void)v;
    struct bsg_lod_payload *pl =
	(struct bsg_lod_payload *)((bsg_node *)node)->s_i_data;
    struct synth_state *st = (struct synth_state *)pl->user_data;
    st->select_calls++;
    return st->select_result;
}

static void
synth_activate_level(bsg_node *node, struct bview *v, int level)
{
    struct bsg_lod_payload *pl =
	(struct bsg_lod_payload *)((bsg_node *)node)->s_i_data;
    struct synth_state *st = (struct synth_state *)pl->user_data;
    st->activate_calls++;

    /* Update the cursor so bsg_lod_node_active_level returns the new level. */
    struct bsg_lod_view_cursor *cur = bsg_lod_node_get_cursor(node, v);
    if (cur)
	cur->level = level;
}

static int
synth_is_stale(bsg_node *node, struct bview *v)
{
    (void)v;
    struct bsg_lod_payload *pl =
	(struct bsg_lod_payload *)((bsg_node *)node)->s_i_data;
    struct synth_state *st = (struct synth_state *)pl->user_data;
    return st->stale_result;
}

static void
synth_free(bsg_node *node)
{
    struct bsg_lod_payload *pl =
	(struct bsg_lod_payload *)((bsg_node *)node)->s_i_data;
    struct synth_state *st = (struct synth_state *)pl->user_data;
    st->free_calls++;
    /* Do not free st itself — it lives on the C stack in the test. */
}

static struct bsg_lod_ops g_synth_ops = {
    synth_select_level,
    synth_activate_level,
    synth_is_stale,
    synth_free
};


/* ================================================================== */
/* Tests                                                               */
/* ================================================================== */

/* Test 1: create */
static int
test_create(void)
{
    printf("=== Test 1: create ===\n");
    struct bview *v = make_view("t1");

    bsg_node *lod = bsg_lod_node_create(v);
    CHECK(lod != NULL, "bsg_lod_node_create returned non-NULL");

    bsg_node *n = (bsg_node *)lod;
    CHECK((n->s_type_flags & BSG_NODE_LOD) != 0,
	  "type flag is BSG_NODE_LOD");
    CHECK(n->s_i_data != NULL, "s_i_data (payload) allocated");

    /* Cleanup: return node to pool. */
    bsg_lod_node_level_count(lod); /* just exercises the path */

    free_view(v);
    return 0;
}


/* Test 2: set_ops */
static int
test_set_ops(void)
{
    printf("=== Test 2: set_ops ===\n");
    struct bview *v = make_view("t2");

    bsg_node *lod = bsg_lod_node_create(v);
    CHECK(lod != NULL, "create");

    int sentinel = 42;
    bsg_lod_node_set_ops(lod, &g_synth_ops, &sentinel);

    struct bsg_lod_payload *pl =
	(struct bsg_lod_payload *)((bsg_node *)lod)->s_i_data;
    CHECK(pl->ops == &g_synth_ops, "ops pointer stored");
    CHECK(pl->user_data == (void *)&sentinel, "user_data stored");

    free_view(v);
    return 0;
}


/* Test 3: attach_level */
static int
test_attach_level(void)
{
    printf("=== Test 3: attach_level ===\n");
    struct bview *v = make_view("t3");

    bsg_node *lod = bsg_lod_node_create(v);
    CHECK(lod != NULL, "create");

    CHECK(bsg_lod_node_level_count(lod) == 0, "initially 0 children");

    bsg_node *lvl0 = bsg_group_create(v);
    bsg_node *lvl1 = bsg_group_create(v);
    CHECK(lvl0 != NULL && lvl1 != NULL, "level nodes created");

    bsg_lod_node_attach_level(lod, lvl0);
    CHECK(bsg_lod_node_level_count(lod) == 1, "1 child after first attach");

    bsg_lod_node_attach_level(lod, lvl1);
    CHECK(bsg_lod_node_level_count(lod) == 2, "2 children after second attach");

    /* Duplicate attach must not add a third entry. */
    bsg_lod_node_attach_level(lod, lvl0);
    CHECK(bsg_lod_node_level_count(lod) == 2, "no duplicate attach");

    free_view(v);
    return 0;
}


/* Test 4+5: cursor create and reuse */
static int
test_cursor_create_reuse(void)
{
    printf("=== Test 4+5: cursor create and reuse ===\n");
    struct bview *v = make_view("t45");

    bsg_node *lod = bsg_lod_node_create(v);
    CHECK(lod != NULL, "create");

    /* First call creates a new cursor. */
    struct bsg_lod_view_cursor *c1 = bsg_lod_node_get_cursor(lod, v);
    CHECK(c1 != NULL, "cursor created");
    CHECK(c1->v == v, "cursor.v matches view");
    CHECK(c1->level == -1, "initial level is -1");

    /* Second call returns the same cursor (same pointer). */
    struct bsg_lod_view_cursor *c2 = bsg_lod_node_get_cursor(lod, v);
    CHECK(c2 == c1, "cursor reused (same pointer)");

    free_view(v);
    return 0;
}


/* Test 6: multiple views get independent cursors */
static int
test_cursor_multi(void)
{
    printf("=== Test 6: cursor multi-view ===\n");
    struct bview *va = make_view("va");
    struct bview *vb = make_view("vb");

    bsg_node *lod = bsg_lod_node_create(va);
    CHECK(lod != NULL, "create");

    struct bsg_lod_view_cursor *ca = bsg_lod_node_get_cursor(lod, va);
    struct bsg_lod_view_cursor *cb = bsg_lod_node_get_cursor(lod, vb);
    CHECK(ca != NULL && cb != NULL, "both cursors created");
    CHECK(ca != cb, "cursors are independent");
    CHECK(ca->v == va, "cursor a.v == va");
    CHECK(cb->v == vb, "cursor b.v == vb");

    /* Modifying one does not affect the other. */
    ca->level = 3;
    CHECK(cb->level == -1, "cursor b level unaffected");

    free_view(va);
    free_view(vb);
    return 0;
}


/* Test 7: active_level */
static int
test_active_level(void)
{
    printf("=== Test 7: active_level ===\n");
    struct bview *v = make_view("t7");

    bsg_node *lod = bsg_lod_node_create(v);
    CHECK(lod != NULL, "create");

    /* No cursor yet → -1. */
    CHECK(bsg_lod_node_active_level(lod, v) == -1,
	  "active_level == -1 before any cursor");

    /* Create cursor and update level. */
    struct bsg_lod_view_cursor *c = bsg_lod_node_get_cursor(lod, v);
    CHECK(c != NULL, "cursor created");
    c->level = 2;
    CHECK(bsg_lod_node_active_level(lod, v) == 2,
	  "active_level == 2 after cursor update");

    free_view(v);
    return 0;
}


/* Test 8: level_count */
static int
test_level_count(void)
{
    printf("=== Test 8: level_count ===\n");
    struct bview *v = make_view("t8");

    CHECK(bsg_lod_node_level_count(NULL) == 0, "NULL node → 0");

    bsg_node *lod = bsg_lod_node_create(v);
    CHECK(bsg_lod_node_level_count(lod) == 0, "fresh node → 0");

    bsg_node *lvl = bsg_group_create(v);
    bsg_lod_node_attach_level(lod, lvl);
    CHECK(bsg_lod_node_level_count(lod) == 1, "after one attach → 1");

    free_view(v);
    return 0;
}


/* Test 9: synthetic ops toggle */
static int
test_synthetic_ops(void)
{
    printf("=== Test 9: synthetic ops toggle ===\n");
    struct bview *v = make_view("t9");

    bsg_node *lod = bsg_lod_node_create(v);
    CHECK(lod != NULL, "create");

    /* Attach two level children. */
    bsg_node *lvl0 = bsg_group_create(v);
    bsg_node *lvl1 = bsg_group_create(v);
    bsg_lod_node_attach_level(lod, lvl0);
    bsg_lod_node_attach_level(lod, lvl1);

    struct synth_state st;
    memset(&st, 0, sizeof(st));
    bsg_lod_node_set_ops(lod, &g_synth_ops, &st);

    /* Simulate the bsg_lod_update loop: is_stale → select → activate. */
    /* Round 1: stale, select returns 0. */
    st.stale_result  = 1;
    st.select_result = 0;

    struct bsg_lod_view_cursor *cur = bsg_lod_node_get_cursor(lod, v);
    CHECK(cur != NULL, "cursor pre-created");

    if (g_synth_ops.is_stale(lod, v)) {
	int lvl = g_synth_ops.select_level(lod, v);
	g_synth_ops.activate_level(lod, v, lvl);
    }
    CHECK(st.select_calls   == 1, "select called once");
    CHECK(st.activate_calls == 1, "activate called once");
    CHECK(bsg_lod_node_active_level(lod, v) == 0, "active level == 0");

    /* Round 2: stale again, select returns 1. */
    st.stale_result  = 1;
    st.select_result = 1;
    if (g_synth_ops.is_stale(lod, v)) {
	int lvl = g_synth_ops.select_level(lod, v);
	g_synth_ops.activate_level(lod, v, lvl);
    }
    CHECK(st.select_calls   == 2, "select called twice");
    CHECK(st.activate_calls == 2, "activate called twice");
    CHECK(bsg_lod_node_active_level(lod, v) == 1, "active level == 1");

    /* Round 3: not stale — callbacks must NOT be called again. */
    st.stale_result = 0;
    if (g_synth_ops.is_stale(lod, v)) {
	int lvl = g_synth_ops.select_level(lod, v);
	g_synth_ops.activate_level(lod, v, lvl);
    }
    CHECK(st.select_calls   == 2, "select NOT called when not stale");
    CHECK(st.activate_calls == 2, "activate NOT called when not stale");
    CHECK(bsg_lod_node_active_level(lod, v) == 1, "active level unchanged");

    free_view(v);
    return 0;
}


/* Test 10: null guards */
static int
test_null_guards(void)
{
    printf("=== Test 10: null guards ===\n");
    struct bview *v = make_view("t10");

    /* None of these must crash. */
    bsg_lod_node_create(NULL);
    bsg_lod_node_set_ops(NULL, NULL, NULL);
    bsg_lod_node_attach_level(NULL, NULL);
    bsg_lod_node_get_cursor(NULL, v);
    bsg_lod_node_get_cursor(NULL, NULL);
    bsg_lod_node_active_level(NULL, v);
    bsg_lod_node_level_count(NULL);

    bsg_node *lod = bsg_lod_node_create(v);
    bsg_lod_node_get_cursor(lod, NULL);
    bsg_lod_node_active_level(lod, NULL);
    bsg_lod_node_attach_level(lod, NULL);
    bsg_lod_node_attach_level(NULL, lod);

    CHECK(1, "all NULL guards survived");
    free_view(v);
    return 0;
}


/* Test 11: ops->free fires on node destroy */
static int
test_ops_free(void)
{
    printf("=== Test 11: ops->free fires on destroy ===\n");
    struct bview *v = make_view("t11");

    bsg_node *lod = bsg_lod_node_create(v);
    CHECK(lod != NULL, "create");

    struct synth_state st;
    memset(&st, 0, sizeof(st));
    bsg_lod_node_set_ops(lod, &g_synth_ops, &st);

    /* Fire the free callback directly (simulates node destruction). */
    bsg_node *n = (bsg_node *)lod;
    if (n->s_free_callback)
	n->s_free_callback(n);

    CHECK(st.free_calls == 1, "ops->free called once on destroy");
    /* After free_callback, s_i_data must be NULL (payload freed). */
    CHECK(n->s_i_data == NULL, "s_i_data cleared after free");

    free_view(v);
    return 0;
}

/* Test 12: insert_above preserves parent slot and wraps leaf */
static int
test_insert_above(void)
{
    printf("=== Test 12: insert_above ===\n");
    struct bview *v = make_view("t12");

    bsg_node *parent = (bsg_node *)bsg_group_create(v);
    bsg_node *leaf = (bsg_node *)bsg_shape_create(v);
    bsg_node *sib = (bsg_node *)bsg_shape_create(v);
    CHECK(parent && leaf && sib, "test nodes created");

    leaf->parent = parent;
    sib->parent = parent;
    bu_ptbl_ins(&parent->children, (long *)leaf);
    bu_ptbl_ins(&parent->children, (long *)sib);
    CHECK(BU_PTBL_LEN(&parent->children) == 2, "parent has two children");

    bsg_node *lod = bsg_lod_node_insert_above((bsg_node *)leaf, v);
    CHECK(lod != NULL, "insert_above returned lod node");
    CHECK((((bsg_node *)lod)->s_type_flags & BSG_NODE_LOD) != 0,
	  "inserted node is BSG_NODE_LOD");
    CHECK(BU_PTBL_LEN(&parent->children) == 2, "parent child count unchanged");
    CHECK((bsg_node *)BU_PTBL_GET(&parent->children, 0) == (bsg_node *)lod,
	  "lod replaced original leaf slot");
    CHECK((bsg_node *)BU_PTBL_GET(&parent->children, 1) == sib,
	  "sibling order preserved");
    CHECK(leaf->parent == (bsg_node *)lod, "leaf parent updated to lod");
    CHECK(BU_PTBL_LEN(&((bsg_node *)lod)->children) == 1, "lod has one child");
    CHECK((bsg_node *)BU_PTBL_GET(&((bsg_node *)lod)->children, 0) == leaf,
	  "lod level-0 child is original leaf");

    free_view(v);
    return 0;
}


/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);
    (void)argc;

    int ret = 0;
    ret |= test_create();
    ret |= test_set_ops();
    ret |= test_attach_level();
    ret |= test_cursor_create_reuse();
    ret |= test_cursor_multi();
    ret |= test_active_level();
    ret |= test_level_count();
    ret |= test_synthetic_ops();
    ret |= test_null_guards();
    ret |= test_ops_free();
    ret |= test_insert_above();

    if (g_fail == 0) {
	printf("\nAll LoD node tests PASSED.\n");
    } else {
	printf("\n%d LoD node test assertion(s) FAILED.\n", g_fail);
    }
    return (g_fail == 0) ? 0 : 1;
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
