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
 * Phase 4 regression: unit tests for the libbsg scene-graph lifecycle
 * and query helpers.
 *
 * Tests (no display manager, no .g file required):
 *   1. create_destroy  — bsg_scene_root_create and bsg_scene_root_destroy
 *      on a freshly initialised bview; verifies that bsg_root is set and
 *      then cleared.
 *   2. create_null     — NULL bview input returns NULL without crashing.
 *   3. sync_children   — after adding a scene obj to the view's BV_VIEW_OBJS
 *      table, bsg_scene_root_sync makes it appear in root->children.
 *   4. find_by_type    — bsg_view_find_by_type locates a child whose
 *      s_type_flags contain a specific set of bits.
 *   5. sensor_fire     — a BSG_NODE_SENSOR child's s_update_callback is
 *      invoked by bsg_sensor_fire.
 *   6. null_guards     — NULL inputs to sync/find/sensor_fire must not
 *      crash.
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

/* ---- Test 1: create / destroy --------------------------------------- */
static void
test_create_destroy(void)
{
    bu_log("=== Test 1: create_destroy ===\n");
    struct bview *v = make_view();

    bsg_node *root = bsg_scene_root_create(v);
    BSGCHECK(root != NULL, "bsg_scene_root_create returns non-NULL");
    BSGCHECK(v->bsg_root == root, "view->bsg_root points at the new root");
    BSGCHECK((root->s_type_flags & BSG_NODE_ROOT) != 0,
	     "root has BSG_NODE_ROOT flag set");

    bsg_scene_root_destroy(root);
    BSGCHECK(v->bsg_root == NULL, "view->bsg_root cleared after destroy");

    if (root != NULL)
	bu_log("  PASS: create/destroy cycle\n");

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

/* ---- Test 3: sync_children ----------------------------------------- */
static void
test_sync_children(void)
{
    bu_log("=== Test 3: sync_children ===\n");
    struct bview *v = make_view();

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) { g_fail++; free_view(v); return; }

    /* Add a synthetic scene object to the view's VIEW_OBJS table */
    struct bv_scene_obj *obj = bv_obj_create(v, BV_VIEW_OBJS | BV_LOCAL_OBJS);
    BSGCHECK(obj != NULL, "bv_obj_create succeeded");

    bsg_scene_root_sync(root, v);

    /* root->children must now contain at least our object */
    struct bv_scene_obj *r = (struct bv_scene_obj *)root;
    int found = 0;
    for (size_t i = 0; i < BU_PTBL_LEN(&r->children); i++) {
	if (BU_PTBL_GET(&r->children, i) == obj) {
	    found = 1;
	    break;
	}
    }
    BSGCHECK(found, "after sync, root->children contains the new view obj");

    if (found)
	bu_log("  PASS: sync_children populated correctly\n");

    bsg_scene_root_destroy(root);
    free_view(v);
}

/* ---- Test 4: find_by_type ------------------------------------------ */
static void
test_find_by_type(void)
{
    bu_log("=== Test 4: find_by_type ===\n");
    struct bview *v = make_view();

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) { g_fail++; free_view(v); return; }

    /* Create a child and tag it with a custom type flag */
    struct bv_scene_obj *child = bv_obj_create(v, BV_VIEW_OBJS | BV_LOCAL_OBJS);
    if (!child) { g_fail++; bsg_scene_root_destroy(root); free_view(v); return; }

    child->s_type_flags |= BSG_NODE_SHAPE;

    bsg_scene_root_sync(root, v);

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

    bsg_scene_root_destroy(root);
    free_view(v);
}

/* ---- Test 5: sensor_fire ------------------------------------------- */
static int g_sensor_fired = 0;

static void
sensor_callback(struct bv_scene_obj *UNUSED(s), struct bview *UNUSED(v), int UNUSED(mode))
{
    g_sensor_fired++;
}

static void
test_sensor_fire(void)
{
    bu_log("=== Test 5: sensor_fire ===\n");
    g_sensor_fired = 0;

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) { g_fail++; free_view(v); return; }

    /* Create a child tagged as BSG_NODE_SENSOR with a callback */
    struct bv_scene_obj *sensor_child =
	bv_obj_create(v, BV_VIEW_OBJS | BV_LOCAL_OBJS);
    if (!sensor_child) { g_fail++; bsg_scene_root_destroy(root); free_view(v); return; }

    sensor_child->s_type_flags   |= BSG_NODE_SENSOR;
    sensor_child->s_update_callback = sensor_callback;

    bsg_scene_root_sync(root, v);
    bsg_sensor_fire(root, v);

    BSGCHECK(g_sensor_fired == 1, "sensor callback invoked exactly once");
    if (g_sensor_fired == 1)
	bu_log("  PASS: sensor_fire invoked callback\n");

    /* Fire again — counter should increment */
    bsg_sensor_fire(root, v);
    BSGCHECK(g_sensor_fired == 2, "sensor callback invoked again on second fire");

    bsg_scene_root_destroy(root);
    free_view(v);
}

/* ---- Test 6: null guards ------------------------------------------- */
static void
test_null_guards(void)
{
    bu_log("=== Test 6: null_guards ===\n");
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

    test_create_destroy();
    test_create_null();
    test_sync_children();
    test_find_by_type();
    test_sensor_fire();
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
