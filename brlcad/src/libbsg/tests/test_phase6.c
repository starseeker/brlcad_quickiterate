/*                 T E S T _ P H A S E 6 . C
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
/** @file libbsg/tests/test_phase6.c
 *
 * Phase 6 unit tests: field accessors, sensors, node taxonomy, payload types.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/field.h"
#include "bsg/sensor.h"
#include "bsg/node_group.h"
#include "bsg/node_shape.h"
#include "bsg/node_transform.h"
#include "bsg/payload.h"

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
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_view");
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v)
	return;
    bsg_view_free(v);
    bu_free(v, "test_view");
}


/* ------------------------------------------------------------------ */
/* Test 1: Field accessors (flag, color, visible)                       */
/* ------------------------------------------------------------------ */

static int
test_field_accessor(void)
{
    printf("=== Test 1: field_accessor ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("bsg_scene_root_create returned NULL");

    /* Use the root node itself as a target (it's a plain bv_scene_obj) */
    bsg_node_set_flag(root, UP);
    if (bsg_node_get_flag(root) != UP) FAIL("flag UP round-trip");

    bsg_node_set_flag(root, DOWN);
    if (bsg_node_get_flag(root) != DOWN) FAIL("flag DOWN round-trip");

    bsg_node_set_color(root, 10, 20, 30);
    unsigned char r = 0, g = 0, b = 0;
    bsg_node_get_color(root, &r, &g, &b);
    if (r != 10 || g != 20 || b != 30) FAIL("color round-trip");

    bsg_node_set_visible(root, 1);
    if (!bsg_node_get_visible(root)) FAIL("set_visible(1)");

    bsg_node_set_visible(root, 0);
    if (bsg_node_get_visible(root)) FAIL("set_visible(0)");

    /* NULL guards */
    bsg_node_set_flag(NULL, UP);
    bsg_node_set_color(NULL, 1, 2, 3);
    bsg_node_set_visible(NULL, 1);
    bsg_node_get_color(NULL, &r, &g, &b);
    if (bsg_node_get_flag(NULL) != 0) FAIL("get_flag(NULL) != 0");
    if (bsg_node_get_visible(NULL) != 0) FAIL("get_visible(NULL) != 0");

    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("field_accessor");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 2: FieldSensor fires on specific field touch                    */
/* ------------------------------------------------------------------ */

static int s_field_cb_count = 0;
static bsg_field_id_t s_field_cb_last_fid = BSG_FIELD_UNKNOWN;

static int
field_sensor_cb(bsg_node *UNUSED(n), bsg_field_id_t fid, void *data)
{
    s_field_cb_count++;
    s_field_cb_last_fid = fid;
    if (data) *(int *)data = 1;
    return 0;
}

static int
test_field_touch_sensor(void)
{
    printf("=== Test 2: field_touch_sensor ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("bsg_scene_root_create returned NULL");

    int fired = 0;
    s_field_cb_count = 0;
    s_field_cb_last_fid = BSG_FIELD_UNKNOWN;

    bsg_node *sensor = bsg_field_sensor_create(root, root,
					       BSG_FIELD_COLOR,
					       field_sensor_cb, &fired);
    if (!sensor) FAIL("bsg_field_sensor_create returned NULL");

    /* Touch a different field — should NOT fire the color sensor */
    bsg_node_field_touch(root, BSG_FIELD_FLAG);
    if (s_field_cb_count != 0) FAIL("sensor fired for wrong field");

    /* Touch the watched field — should fire */
    bsg_node_set_color(root, 255, 0, 0);
    if (s_field_cb_count != 1) FAIL("sensor did not fire for BSG_FIELD_COLOR");
    if (s_field_cb_last_fid != BSG_FIELD_COLOR) FAIL("wrong fid in callback");
    if (!fired) FAIL("data pointer not updated by callback");

    bsg_sensor_destroy(sensor);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("field_touch_sensor");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 3: NodeSensor fires on any field touch                          */
/* ------------------------------------------------------------------ */

static int s_node_cb_count = 0;

static int
node_sensor_cb(bsg_node *UNUSED(n), void *UNUSED(data))
{
    s_node_cb_count++;
    return 0;
}

static int
test_node_sensor(void)
{
    printf("=== Test 3: node_sensor ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("bsg_scene_root_create returned NULL");

    s_node_cb_count = 0;

    bsg_node *sensor = bsg_node_sensor_create(root, root,
					      node_sensor_cb, NULL);
    if (!sensor) FAIL("bsg_node_sensor_create returned NULL");

    bsg_node_field_touch(root, BSG_FIELD_FLAG);
    bsg_node_field_touch(root, BSG_FIELD_COLOR);
    if (s_node_cb_count != 2)
	FAIL("NodeSensor did not fire twice");

    bsg_sensor_destroy(sensor);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("node_sensor");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 4: Group node create / add / remove                             */
/* ------------------------------------------------------------------ */

static int
test_group_create_add_remove(void)
{
    printf("=== Test 4: group_create_add_remove ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("bsg_scene_root_create returned NULL");

    bsg_node *grp = bsg_group_create(v);
    if (!grp) FAIL("bsg_group_create returned NULL");

    bsg_node *g = (bsg_node *)grp;
    if (!(g->s_type_flags & BSG_NODE_GROUP)) FAIL("type flag not BSG_NODE_GROUP");

    bsg_node *child1 = bsg_group_create(v);
    bsg_node *child2 = bsg_group_create(v);
    if (!child1 || !child2) FAIL("child group_create returned NULL");

    bsg_group_add_child(grp, child1);
    bsg_group_add_child(grp, child2);
    if (BU_PTBL_LEN(&g->children) != 2) FAIL("expected 2 children after add");

    /* Adding the same child twice should be a no-op */
    bsg_group_add_child(grp, child1);
    if (BU_PTBL_LEN(&g->children) != 2) FAIL("duplicate add should be no-op");

    bsg_group_remove_child(grp, child1);
    if (BU_PTBL_LEN(&g->children) != 1) FAIL("expected 1 child after remove");

    bsg_group_destroy(child1);
    bsg_group_destroy(child2);
    bsg_group_destroy(grp);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("group_create_add_remove");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 5: Transform node matrix round-trip                             */
/* ------------------------------------------------------------------ */

static int
test_transform_matrix(void)
{
    printf("=== Test 5: transform_matrix ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("bsg_scene_root_create returned NULL");

    bsg_node *xf = bsg_transform_create(v);
    if (!xf) FAIL("bsg_transform_create returned NULL");

    bsg_node *xs = (bsg_node *)xf;
    if (!(xs->s_type_flags & BSG_NODE_TRANSFORM))
	FAIL("type flag not BSG_NODE_TRANSFORM");

    /* Verify identity matrix after create */
    mat_t got;
    bsg_transform_get_matrix(xf, got);
    mat_t ident;
    MAT_IDN(ident);
    if (memcmp(got, ident, sizeof(mat_t)) != 0)
	FAIL("initial matrix not identity");

    /* Set a non-identity matrix and read it back */
    mat_t set_mat;
    MAT_IDN(set_mat);
    set_mat[0] = 2.0; set_mat[5] = 3.0; set_mat[10] = 4.0;
    bsg_transform_set_matrix(xf, set_mat);
    bsg_transform_get_matrix(xf, got);
    if (memcmp(got, set_mat, sizeof(mat_t)) != 0)
	FAIL("set/get matrix round-trip failed");

    bsg_transform_destroy(xf);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("transform_matrix");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 6: Payload type set/get                                         */
/* ------------------------------------------------------------------ */

static int
test_payload_type(void)
{
    printf("=== Test 6: payload_type ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("bsg_scene_root_create returned NULL");

    bsg_node *shape = bsg_shape_create(v);
    if (!shape) FAIL("bsg_shape_create returned NULL");

    /* Initially no payload bits */
    if (bsg_node_get_payload_type(shape) != 0)
	FAIL("initial payload type should be 0");

    bsg_node_set_payload_type(shape, BSG_PAYLOAD_VLIST);
    if (bsg_node_get_payload_type(shape) != BSG_PAYLOAD_VLIST)
	FAIL("BSG_PAYLOAD_VLIST round-trip failed");

    /* Replace with a different payload type */
    bsg_node_set_payload_type(shape, BSG_PAYLOAD_CSG | BSG_PAYLOAD_MESH);
    unsigned long long pt = bsg_node_get_payload_type(shape);
    if (!(pt & BSG_PAYLOAD_CSG)) FAIL("BSG_PAYLOAD_CSG not set");
    if (!(pt & BSG_PAYLOAD_MESH)) FAIL("BSG_PAYLOAD_MESH not set");
    if (pt & BSG_PAYLOAD_VLIST) FAIL("BSG_PAYLOAD_VLIST should be cleared");

    /* NULL guards */
    bsg_node_set_payload_type(NULL, BSG_PAYLOAD_VLIST);
    if (bsg_node_get_payload_type(NULL) != 0)
	FAIL("get_payload_type(NULL) should be 0");

    bsg_payload_dispatch(NULL, NULL, NULL);  /* no-op, must not crash */
    bsg_payload_dispatch(NULL, shape, NULL); /* PAYLOAD_CSG: no crash */

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("payload_type");
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

    failures += test_field_accessor();
    failures += test_field_touch_sensor();
    failures += test_node_sensor();
    failures += test_group_create_add_remove();
    failures += test_transform_matrix();
    failures += test_payload_type();

    if (failures == 0)
	printf("RESULT: all Phase 6 tests PASSED\n");
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
