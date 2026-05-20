/*                 T E S T _ B S G _ N O D E . C
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
/** @file libbsg/tests/test_bsg_node.c
 *
 * Phase 1 unit tests for the generic BSG node API.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/field.h"
#include "bsg/node.h"
#include "bsg/node_group.h"
#include "bsg/node_shape.h"
#include "bsg/node_transform.h"
#include "bsg/payload.h"
#include "bsg/sensor.h"
#include "bsg/util.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

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

static int s_field_hits = 0;
static bsg_field_id_t s_last_field = BSG_FIELD_UNKNOWN;
static int s_free_hits = 0;
static int s_update_hits = 0;
static int s_update_flags = 0;

static int
field_cb(bsg_node *UNUSED(n), bsg_field_id_t fid, void *UNUSED(data))
{
    s_field_hits++;
    s_last_field = fid;
    return 0;
}

static void
reset_field_callback_state(void)
{
    s_field_hits = 0;
    s_last_field = BSG_FIELD_UNKNOWN;
}

static void
free_cb(bsg_node *UNUSED(n))
{
    s_free_hits++;
}

static int
update_cb(bsg_node *UNUSED(n), struct bview *UNUSED(v), int flags)
{
    s_update_hits++;
    s_update_flags = flags;
    return flags + 1;
}

static int
expect_field_fire(bsg_node *root, bsg_node *target, bsg_field_id_t fid,
		  void (*op)(bsg_node *, void *), void *data)
{
    reset_field_callback_state();
    bsg_node *sensor = bsg_field_sensor_create(root, target, fid, field_cb, NULL);
    if (!sensor)
	return 0;
    op(target, data);
    bsg_sensor_destroy(sensor);
    return (s_field_hits == 1 && s_last_field == fid) ? 1 : 0;
}

static void
op_set_name(bsg_node *n, void *data)
{
    bsg_node_set_name(n, (const char *)data);
}

static void
op_set_kind(bsg_node *n, void *data)
{
    bsg_node_set_kind(n, *(unsigned long long *)data);
}

static void
op_set_visible(bsg_node *n, void *data)
{
    bsg_node_set_visible(n, *(int *)data);
}

static void
op_set_force_draw(bsg_node *n, void *data)
{
    bsg_node_set_force_draw(n, *(int *)data);
}

static void
op_set_transform(bsg_node *n, void *data)
{
    const mat_t *m = (const mat_t *)data;
    bsg_node_transform_set(n, *m);
}

struct bounds_data {
    point_t bmin;
    point_t bmax;
};

static void
op_set_bounds(bsg_node *n, void *data)
{
    struct bounds_data *bd = (struct bounds_data *)data;
    bsg_node_bounds_set(n, bd->bmin, bd->bmax);
}

static void
op_set_user_data(bsg_node *n, void *data)
{
    bsg_node_user_data_set(n, data);
}

static void
op_set_payload(bsg_node *n, void *data)
{
    bsg_node_set_payload_type(n, *(unsigned long long *)data);
}

static void
op_add_child(bsg_node *n, void *data)
{
    bsg_node_add_child(n, (bsg_node *)data);
}

static int
test_null_safety(void)
{
    printf("=== Test 1: null_safety ===\n");

    point_t bmin = VINIT_ZERO;
    point_t bmax = VINIT_ZERO;
    mat_t mat;
    MAT_IDN(mat);

    bsg_node_set_kind(NULL, BSG_NODE_GROUP);
    bsg_node_set_name(NULL, "ignored");
    bsg_node_add_child(NULL, NULL);
    bsg_node_remove_child(NULL, NULL);
    bsg_node_set_visible(NULL, 1);
    bsg_node_set_force_draw(NULL, 1);
    bsg_node_transform_get(NULL, mat);
    bsg_node_transform_set(NULL, mat);
    bsg_node_user_data_set(NULL, &mat);
    bsg_node_set_free_callback(NULL, free_cb);
    bsg_node_set_update_callback(NULL, update_cb);
    if (bsg_node_invoke_update_callback(NULL, NULL, 0) != 0) FAIL("update_callback(NULL)");
    bsg_node_invoke_free_callback(NULL);
    bsg_node_bounds_get(NULL, bmin, bmax);
    bsg_node_bounds_set(NULL, bmin, bmax);
    bsg_node_mark_stale(NULL);
    bsg_node_set_payload_type(NULL, BSG_PAYLOAD_VLIST);

    if (bsg_node_kind(NULL) != 0) FAIL("kind(NULL)");
    if (bsg_node_has_kind(NULL, BSG_NODE_GROUP) != 0) FAIL("has_kind(NULL)");
    if (bsg_node_name(NULL) != NULL) FAIL("name(NULL)");
    if (bsg_node_parent(NULL) != NULL) FAIL("parent(NULL)");
    if (bsg_node_child_count(NULL) != 0) FAIL("child_count(NULL)");
    if (bsg_node_child(NULL, 0) != NULL) FAIL("child(NULL)");
    if (bsg_node_visible(NULL) != 0) FAIL("visible(NULL)");
    if (bsg_node_force_draw(NULL) != 0) FAIL("force_draw(NULL)");
    if (bsg_node_user_data_get(NULL) != NULL) FAIL("user_data_get(NULL)");

    PASS("null_safety");
    return 0;
}


static int
test_basic_accessors(void)
{
    printf("=== Test 2: basic_accessors ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    if (!root || !shape) FAIL("create nodes");

    bsg_node_set_kind(shape, BSG_NODE_SHAPE);
    if (!bsg_node_has_kind(shape, BSG_NODE_SHAPE)) FAIL("has_kind(shape)");

    bsg_node_set_payload_type(shape, BSG_PAYLOAD_VLIST);
    if (bsg_node_get_payload_type(shape) != BSG_PAYLOAD_VLIST)
	FAIL("payload round-trip");

    bsg_node_set_name(shape, "shape-a");
    if (!bsg_node_name(shape) || !BU_STR_EQUAL(bsg_node_name(shape), "shape-a"))
	FAIL("name round-trip");

    bsg_node_set_visible(shape, 1);
    if (!bsg_node_visible(shape))
	FAIL("visible on");
    bsg_node_set_visible(shape, 0);
    if (bsg_node_visible(shape)) FAIL("visible off");

    bsg_node_set_force_draw(shape, 1);
    if (!bsg_node_force_draw(shape)) FAIL("force_draw on");
    bsg_node_set_force_draw(shape, 0);
    if (bsg_node_force_draw(shape)) FAIL("force_draw off");

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("basic_accessors");
    return 0;
}

static int
test_children_and_transform(void)
{
    printf("=== Test 3: children_and_transform ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *group = bsg_group_create(v);
    bsg_node *child = bsg_shape_create(v);
    bsg_node *xf = bsg_transform_create(v);
    if (!root || !group || !child || !xf) FAIL("create nodes");

    bsg_node_add_child(group, child);
    if (bsg_node_child_count(group) != 1) FAIL("child_count after add");
    if (bsg_node_child(group, 0) != child) FAIL("child accessor");
    if (bsg_node_parent(child) != group) FAIL("parent accessor");

    bsg_node_remove_child(group, child);
    if (bsg_node_child_count(group) != 0) FAIL("child_count after remove");
    if (bsg_node_parent(child) != NULL) FAIL("parent cleared");

    mat_t setmat, got;
    MAT_IDN(setmat);
    setmat[0] = 2.0;
    setmat[5] = 3.0;
    setmat[10] = 4.0;
    bsg_node_transform_set(xf, setmat);
    bsg_transform_get_matrix(xf, got);
    for (size_t i = 0; i < 16; i++) {
	if (!NEAR_EQUAL(setmat[i], got[i], SMALL_FASTF))
	    FAIL("transform round-trip");
    }

    bsg_shape_destroy(child);
    bsg_transform_destroy(xf);
    bsg_group_destroy(group);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("children_and_transform");
    return 0;
}

static int
test_bounds_and_user_data(void)
{
    printf("=== Test 4: bounds_and_user_data ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    if (!root || !shape) FAIL("create nodes");

    int payload = 42;
    point_t in_min, in_max;
    point_t out_min, out_max;
    VSET(in_min, 1.0, 2.0, 3.0);
    VSET(in_max, 4.0, 5.0, 6.0);

    bsg_node_user_data_set(shape, &payload);
    if (bsg_node_user_data_get(shape) != &payload) FAIL("user_data round-trip");

    bsg_node_bounds_set(shape, in_min, in_max);
    bsg_node_bounds_get(shape, out_min, out_max);
    if (!VEQUAL(in_min, out_min) || !VEQUAL(in_max, out_max))
	FAIL("bounds round-trip");

    bsg_node_mark_stale(shape);

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("bounds_and_user_data");
    return 0;
}

static int
test_field_notifications(void)
{
    printf("=== Test 5: field_notifications ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    bsg_node *child = bsg_group_create(v);
    if (!root || !shape || !child) FAIL("create nodes");

    unsigned long long kind = BSG_NODE_GROUP;
    unsigned long long payload = BSG_PAYLOAD_CSG;
    int one = 1;
    int user_data_value = 7;
    mat_t mat;
    MAT_IDN(mat);
    mat[15] = 2.0;
    struct bounds_data bd;
    VSET(bd.bmin, 1.0, 1.0, 1.0);
    VSET(bd.bmax, 2.0, 2.0, 2.0);

    if (!expect_field_fire(root, shape, BSG_FIELD_NAME, op_set_name, (void *)"notify-name"))
	FAIL("name field notification");
    if (!expect_field_fire(root, shape, BSG_FIELD_KIND, op_set_kind, &kind))
	FAIL("kind field notification");
    if (!expect_field_fire(root, shape, BSG_FIELD_VISIBILITY, op_set_visible, &one))
	FAIL("visibility field notification");
    if (!expect_field_fire(root, shape, BSG_FIELD_FORCE_DRAW, op_set_force_draw, &one))
	FAIL("force_draw field notification");
    if (!expect_field_fire(root, shape, BSG_FIELD_TRANSFORM, op_set_transform, mat))
	FAIL("transform field notification");
    if (!expect_field_fire(root, shape, BSG_FIELD_BOUNDS, op_set_bounds, &bd))
	FAIL("bounds field notification");
    if (!expect_field_fire(root, shape, BSG_FIELD_USER_DATA, op_set_user_data, &user_data_value))
	FAIL("user_data field notification");
    if (!expect_field_fire(root, shape, BSG_FIELD_PAYLOAD, op_set_payload, &payload))
	FAIL("payload field notification");
    if (!expect_field_fire(root, root, BSG_FIELD_CHILDREN, op_add_child, child))
	FAIL("children field notification");

    bsg_group_destroy(child);
    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("field_notifications");
    return 0;
}

static int
test_lifecycle_callbacks(void)
{
    printf("=== Test 6: lifecycle_callbacks ===\n");

    struct bview *v = make_view();
    bsg_node *shape = bsg_shape_create(v);
    if (!shape) FAIL("create shape");

    s_free_hits = 0;
    s_update_hits = 0;
    s_update_flags = 0;

    bsg_node_set_free_callback(shape, free_cb);
    bsg_node_invoke_free_callback(shape);
    if (s_free_hits != 1) FAIL("free callback invoke");

    bsg_node_set_update_callback(shape, update_cb);
    if (bsg_node_invoke_update_callback(shape, v, 7) != 8)
	FAIL("update callback result");
    if (s_update_hits != 1 || s_update_flags != 7)
	FAIL("update callback invoke");

    bsg_node_set_free_callback(shape, NULL);
    bsg_node_set_update_callback(shape, NULL);
    if (bsg_node_invoke_update_callback(shape, v, 3) != 0)
	FAIL("cleared update callback");
    bsg_node_invoke_free_callback(shape);
    if (s_free_hits != 1) FAIL("cleared free callback");

    bsg_shape_destroy(shape);
    free_view(v);

    PASS("lifecycle_callbacks");
    return 0;
}

int
main(int UNUSED(argc), const char **argv)
{
    bu_setprogname(argv[0]);

    int failures = 0;
    failures += test_null_safety();
    failures += test_basic_accessors();
    failures += test_children_and_transform();
    failures += test_bounds_and_user_data();
    failures += test_field_notifications();
    failures += test_lifecycle_callbacks();

    if (failures) {
	printf("FAIL: %d test group(s) failed\n", failures);
	return 1;
    }

    printf("PASS: all bsg node tests passed\n");
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
