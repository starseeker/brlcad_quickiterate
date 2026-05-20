/*              T E S T _ S C E N E _ G R A P H . C
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
/** @file libbsg/tests/test_scene_graph.c
 *
 * Phase 4 / Phase F regression: unit tests for the libbsg scene-graph
 * lifecycle and query helpers.
 *
 * Phase F semantics (drawing_stack_modernization):
 *   bsg_root is an alias for gv_draw_root — no separate synthetic node.
 *   bsg_scene_root_sync() is a deliberate no-op; bsg_root->bsg.bsg_children IS
 *   gv_draw_root->bsg.bsg_children, maintained live by draw/erase mutations.
 *
 * Tests (no display manager, no .g file required):
 *   1. create_alias  — bsg_scene_root_create wires bsg_root = gv_draw_root.
 *      Without a draw root the call returns NULL; with one set it returns
 *      the draw root and bsg_root == gv_draw_root.
 *      bsg_scene_root_destroy clears the pointer without freeing the node.
 *   2. create_null   — NULL bview input returns NULL without crashing.
 *   3. sync_noop     — bsg_scene_root_sync is a no-op; calling it does not
 *      change root->bsg.bsg_children.
 *   4. find_by_type  — bsg_view_find_by_type locates a child whose
 *      s_type_flags contain a specific set of bits.
 *   5. sensor_fire   — a BSG_NODE_SENSOR child's s_update_callback is
 *      invoked by bsg_sensor_fire.
 *   6. null_guards   — NULL inputs to sync/find/sensor_fire must not crash.
 *
 * Usage: test_bsg_scene_graph
 *   Returns 0 on success, non-zero on failure.
 */

#include "common.h"

#include <string.h>

#include "vmath.h"
#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/defines.h"
#include "bsg/identity.h"
#include "bsg/node.h"
#include "bsg/util.h"

static int g_fail = 0;

#define BSGCHECK(cond, msg) \
    do { \
	if (!(cond)) { \
	    bu_log("FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); \
	    g_fail++; \
	} \
    } while (0)

/* ---- helpers -------------------------------------------------------- */

static struct bview *
make_view(void)
{
    struct bview *v;
    BU_GET(v, struct bview);
    bv_init(v, NULL);
    return v;
}

static void
free_view(struct bview *v)
{
    bv_free(v);
    BU_PUT(v, struct bview);
}

/* Create a minimal synthetic draw-root group on a view so that
 * bsg_scene_root_create can wire bsg_root to it. */
static struct bv_scene_obj *
attach_fake_draw_root(struct bview *v)
{
    struct bv_scene_obj *dr = (struct bv_scene_obj *)bsg_node_create_child(v, BSG_NODE_GROUP);
    if (!dr)
	return NULL;
    v->gv_draw_root  = dr;
    return dr;
}

/* ---- Test 1: create / alias / destroy ------------------------------- */
static void
test_create_alias(void)
{
    bu_log("=== Test 1: create_alias ===\n");
    struct bsg_identity id_no_root, id_with_root;

    /* Without a draw root: standalone libbsg consumers get a minimal root. */
    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    BSGCHECK(root != NULL,     "bsg_scene_root_create(no draw root) creates root");
    BSGCHECK(v->bsg_root == root, "view->bsg_root is set when no draw root");
    BSGCHECK(v->gv_draw_root == root, "view->gv_draw_root is set when no draw root");
    BSGCHECK(bsg_node_identity_get(root, &id_no_root) == 1,
	     "scene root identity assigned for standalone root");
    BSGCHECK(id_no_root.node_id.value != 0, "scene root identity node_id is non-zero");
    BSGCHECK(id_no_root.source_kind == BSG_SOURCE_GENERATED,
	     "scene root identity source_kind is generated");
    bsg_scene_root_destroy(root);
    v->gv_draw_root = NULL;
    bsg_node_identity_clear(root);
    bsg_node_destroy(root);

    /* Set up a fake draw root and re-run */
    struct bv_scene_obj *dr = attach_fake_draw_root(v);
    if (!dr) { g_fail++; free_view(v); return; }

    root = bsg_scene_root_create(v);
    BSGCHECK(root != NULL,               "bsg_scene_root_create(with draw root) returns non-NULL");
    BSGCHECK(v->bsg_root == root,        "view->bsg_root == returned root");
    BSGCHECK(v->bsg_root == v->gv_draw_root,
	     "bsg_root is an alias for gv_draw_root (Phase F)");
    BSGCHECK(bsg_node_identity_get(root, &id_with_root) == 1,
	     "scene root identity assigned for existing draw root");
    BSGCHECK(id_with_root.node_id.value == id_no_root.node_id.value,
	     "scene root identity matches between standalone and pre-existing roots");
    BSGCHECK(id_with_root.source_kind == BSG_SOURCE_GENERATED,
	     "existing draw root identity source_kind is generated");

    /* Destroy: clears bsg_root but does NOT free the node */
    bsg_scene_root_destroy(root);
    BSGCHECK(v->bsg_root == NULL,        "view->bsg_root cleared after destroy");
    BSGCHECK(v->gv_draw_root == (void *)dr,
	     "gv_draw_root still valid after bsg_scene_root_destroy");

    bu_log("  PASS: create/alias/destroy cycle\n");

    /* Clean up the fake draw root manually (bsg_scene_root_destroy does not
     * free it, as it is owned by the draw-tree lifecycle). */
    v->gv_draw_root = NULL;
    bsg_node_identity_clear((bsg_node *)dr);
    bsg_node_destroy((bsg_node *)dr);
    free_view(v);
}

/* ---- Test 2: NULL bview -------------------------------------------- */
static void
test_create_null(void)
{
    bu_log("=== Test 2: create_null ===\n");
    bsg_node *root = bsg_scene_root_create(NULL);
    BSGCHECK(root == NULL, "bsg_scene_root_create(NULL) returns NULL");
    bu_log("  PASS: null bview guard\n");
}

/* ---- Test 3: sync is a no-op --------------------------------------- */
static void
test_sync_noop(void)
{
    bu_log("=== Test 3: sync_noop ===\n");
    struct bview *v = make_view();

    struct bv_scene_obj *dr = attach_fake_draw_root(v);
    if (!dr) { g_fail++; free_view(v); return; }

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) { g_fail++; v->gv_draw_root = NULL; free_view(v); return; }
    struct bv_scene_obj *r = (struct bv_scene_obj *)root;
    BSGCHECK(BU_PTBL_LEN(&r->bsg.bsg_children) == 0, "children empty before sync");

    bsg_scene_root_sync(root, v);
    BSGCHECK(BU_PTBL_LEN(&r->bsg.bsg_children) == 0, "sync is a no-op: children still empty");

    bu_log("  PASS: sync_noop\n");

    bsg_scene_root_destroy(root);
    v->gv_draw_root = NULL;
    bsg_node_identity_clear((bsg_node *)dr);
    bsg_node_destroy((bsg_node *)dr);
    free_view(v);
}

/* ---- Test 4: find_by_type ------------------------------------------ */
static void
test_find_by_type(void)
{
    bu_log("=== Test 4: find_by_type ===\n");
    struct bview *v = make_view();

    struct bv_scene_obj *dr = attach_fake_draw_root(v);
    if (!dr) { g_fail++; free_view(v); return; }

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) { g_fail++; v->gv_draw_root = NULL; bsg_node_destroy((bsg_node *)dr); free_view(v); return; }

    /* Add a child directly to root->bsg.bsg_children with a specific type flag.
     * Phase F: root IS the draw root, so this is identical to adding a
     * child to the draw tree. */
    struct bv_scene_obj *child = (struct bv_scene_obj *)bsg_node_create_child(v, BSG_NODE_SHAPE);
    if (!child) { g_fail++; bsg_scene_root_destroy(root); v->gv_draw_root = NULL; bsg_node_destroy((bsg_node *)dr); free_view(v); return; }

    bu_ptbl_ins(&((struct bv_scene_obj *)root)->bsg.bsg_children, (long *)child);

    bsg_node *found = bsg_view_find_by_type(root, BSG_NODE_SHAPE);
    BSGCHECK(found != NULL, "bsg_view_find_by_type finds BSG_NODE_SHAPE child");
    BSGCHECK(found == (bsg_node *)child,
	     "bsg_view_find_by_type returns the correct child pointer");

    /* Must NOT find a type that wasn't set */
    bsg_node *notfound = bsg_view_find_by_type(root, BSG_NODE_LOD);
    BSGCHECK(notfound == NULL,
	     "bsg_view_find_by_type returns NULL for absent type");

    if (found && !notfound)
	bu_log("  PASS: find_by_type\n");

    /* Cleanup: clear bsg_root and draw-root pointers; gv_free() handles the
     * rest.  The child object and fake draw root are freed when the view's
     * free pool is collected.  We do NOT destroy individual child objects
     * here — let free_view() sweep the pool. */
    bsg_scene_root_destroy(root);
    v->gv_draw_root = NULL;
    bsg_node_identity_clear(root);
    free_view(v);
}

/* ---- Test 5: sensor_fire ------------------------------------------- */
static int g_sensor_fired = 0;

static int
sensor_callback(struct bv_scene_obj *UNUSED(s), struct bview *UNUSED(v), int UNUSED(mode))
{
    g_sensor_fired++;
    return 0;
}

static void
test_sensor_fire(void)
{
    bu_log("=== Test 5: sensor_fire ===\n");
    g_sensor_fired = 0;

    struct bview *v = make_view();

    struct bv_scene_obj *dr = attach_fake_draw_root(v);
    if (!dr) { g_fail++; free_view(v); return; }

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) { g_fail++; v->gv_draw_root = NULL; free_view(v); return; }

    /* Add a sensor child directly to root->bsg.bsg_children. */
    struct bv_scene_obj *sensor_child = (struct bv_scene_obj *)bsg_node_create_child(v, BSG_NODE_SENSOR);
    if (!sensor_child) {
	g_fail++;
	bsg_scene_root_destroy(root);
	v->gv_draw_root = NULL;
	free_view(v);
	return;
    }

    bsg_node_set_update_callback((bsg_node *)sensor_child, (bsg_node_update_fn)sensor_callback);
    bu_ptbl_ins(&((struct bv_scene_obj *)root)->bsg.bsg_children, (long *)sensor_child);

    bsg_sensor_fire(root, v);

    BSGCHECK(g_sensor_fired == 1, "sensor callback invoked exactly once");
    if (g_sensor_fired == 1)
	bu_log("  PASS: sensor_fire invoked callback\n");

    /* Fire again — counter should increment */
    bsg_sensor_fire(root, v);
    BSGCHECK(g_sensor_fired == 2, "sensor callback invoked again on second fire");

    /* Cleanup: same pattern as test 4 — clear bsg_root, reset draw-root
     * pointer; free_view handles the pool sweep. */
    bsg_scene_root_destroy(root);
    v->gv_draw_root = NULL;
    bsg_node_identity_clear(root);
    free_view(v);
}

/* ---- Test 6: view_obj_identity ------------------------------------- */
static void
test_view_obj_identity(void)
{
    bu_log("=== Test 6: view_obj_identity ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) { g_fail++; free_view(v); return; }

    struct bv_view_obj_opts opts = BV_VIEW_OBJ_OPTS_INIT;
    struct bsg_identity id_shared_0, id_shared_1, id_local;

    opts.local = 0;
    struct bv_scene_obj *shared0 = bv_view_obj_create(v, "phase2d_obj", 0, &opts);
    struct bv_scene_obj *shared1 = bv_view_obj_create(v, "phase2d_obj", 0, &opts);
    opts.local = 1;
    struct bv_scene_obj *local0 = bv_view_obj_create(v, "phase2d_obj", 0, &opts);

    BSGCHECK(shared0 != NULL, "shared view object #0 created");
    BSGCHECK(shared1 != NULL, "shared view object #1 created");
    BSGCHECK(local0 != NULL, "local view object created");

    BSGCHECK(bsg_node_identity_get((bsg_node *)shared0, &id_shared_0) == 1,
	     "shared view object #0 has identity");
    BSGCHECK(bsg_node_identity_get((bsg_node *)shared1, &id_shared_1) == 1,
	     "shared view object #1 has identity");
    BSGCHECK(bsg_node_identity_get((bsg_node *)local0, &id_local) == 1,
	     "local view object has identity");

    BSGCHECK(id_shared_0.source_kind == BSG_SOURCE_VIEW_OBJECT,
	     "shared view object source kind is view object");
    BSGCHECK(id_shared_1.source_kind == BSG_SOURCE_VIEW_OBJECT,
	     "second shared view object source kind is view object");
    BSGCHECK(id_local.source_kind == BSG_SOURCE_VIEW_OBJECT,
	     "local view object source kind is view object");

    BSGCHECK(id_shared_0.node_id.value != 0,
	     "shared view object identity is non-zero");
    BSGCHECK(id_shared_1.node_id.value != id_shared_0.node_id.value,
	     "duplicate shared names get distinct derived identities");
    BSGCHECK(id_local.node_id.value != id_shared_0.node_id.value,
	     "local and shared objects get distinct derived identities");

    bsg_node_identity_clear((bsg_node *)shared0);
    bsg_node_identity_clear((bsg_node *)shared1);
    bsg_node_identity_clear((bsg_node *)local0);
    bsg_scene_root_destroy(root);
    v->gv_draw_root = NULL;
    bsg_node_identity_clear(root);
    free_view(v);
}

/* ---- Test 7: null guards ------------------------------------------- */
static void
test_null_guards(void)
{
    bu_log("=== Test 7: null_guards ===\n");
    int fails_before = g_fail;

    /* These must not crash */
    bsg_scene_root_sync(NULL, NULL);
    bsg_scene_root_destroy(NULL);

    bsg_node *r = bsg_view_find_by_type(NULL, BSG_NODE_SHAPE);
    BSGCHECK(r == NULL, "find_by_type(NULL root) returns NULL");

    r = bsg_view_find_by_type((bsg_node *)&r, 0); /* flags == 0 */
    BSGCHECK(r == NULL, "find_by_type(flags=0) returns NULL");

    bsg_sensor_fire(NULL, NULL);  /* must not crash */

    if (g_fail == fails_before)
	bu_log("  PASS: all null guards\n");
}

/* -------------------------------------------------------------------- */
int
main(int UNUSED(argc), char *argv[])
{
    bu_setprogname(argv[0]);

    test_create_alias();
    test_create_null();
    test_sync_noop();
    test_find_by_type();
    test_sensor_fire();
    test_view_obj_identity();
    test_null_guards();

    if (g_fail) {
	bu_log("RESULT: %d failure(s)\n", g_fail);
	return 1;
    }
    bu_log("RESULT: all scene-graph tests PASSED\n");
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
