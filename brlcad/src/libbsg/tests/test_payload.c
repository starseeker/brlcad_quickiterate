/*                  T E S T _ P A Y L O A D . C
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
/** @file libbsg/tests/test_payload.c */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/vlist.h"
#include "bsg/identity.h"
#include "bsg/node_shape.h"
#include "bsg/payload.h"
#include "bsg/util.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

static struct bview *
make_view(void)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_payload_view");
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v)
	return;
    bv_free(v);
    bu_free(v, "test_payload_view");
}

static int
test_vlist_payload_and_wire(void)
{
    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    struct bv_scene_obj *s = (struct bv_scene_obj *)shape;
    struct bu_list vhead;
    struct bu_list vlfree;
    struct bsg_payload *payload = NULL;
    struct bsg_payload *wire = NULL;
    const struct bsg_wire_polyline *pl = NULL;
    point_t p0 = VINIT_ZERO;
    point_t p1 = {1.0, 2.0, 3.0};
    point_t p2 = {2.0, 4.0, 6.0};
    point_t bmin, bmax;

    if (!root || !shape)
	FAIL("view/root/shape setup");

    BU_LIST_INIT(&vhead);
    BU_LIST_INIT(&vlfree);
    BV_ADD_VLIST(&vlfree, &vhead, p0, BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(&vlfree, &vhead, p1, BV_VLIST_LINE_DRAW);
    BV_ADD_VLIST(&vlfree, &vhead, p2, BV_VLIST_LINE_DRAW);

    bsg_shape_set_vlist(shape, &vhead);
    BV_FREE_VLIST(&vlfree, &vhead);

    payload = bsg_node_payload_get(shape);
    if (!payload)
	FAIL("bsg_node_payload_get(shape)");
    if (bsg_payload_type(payload) != BSG_PAYLOAD_TYPE_VLIST)
	FAIL("payload type is vlist");
    if (bsg_payload_vlist_head(payload) != &s->s_vlist)
	FAIL("payload vlist head maps to shape s_vlist");
    if (bsg_payload_vlist_count(payload) < 3)
	FAIL("payload vlist count");
    if (bsg_node_revision(shape, BSG_NODE_REV_PAYLOAD) == 0)
	FAIL("node payload revision bumped");
    if (bsg_payload_revision(payload) == 0)
	FAIL("payload revision bumped");
    if (!bsg_payload_bounds(payload, &bmin, &bmax))
	FAIL("payload bounds from vlist");
    if (bmax[X] < bmin[X] || bmax[Y] < bmin[Y] || bmax[Z] < bmin[Z])
	FAIL("payload bounds monotonic");

    wire = bsg_payload_wire_from_vlist(payload);
    if (!wire)
	FAIL("wire conversion from vlist");
    if (bsg_payload_type(wire) != BSG_PAYLOAD_TYPE_WIRE)
	FAIL("wire payload type");
    if (bsg_payload_wire_polyline_count(wire) != 1)
	FAIL("wire polyline count");
    pl = bsg_payload_wire_polyline_get(wire, 0);
    if (!pl || pl->point_count != 3)
	FAIL("wire polyline contents");
    if (!NEAR_EQUAL(pl->points[2][X], p2[X], SMALL_FASTF))
	FAIL("wire polyline coordinates");

    bsg_payload_destroy(wire);
    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("vlist payload and wire conversion");
    return 0;
}

static int
test_mesh_payload(void)
{
    struct bsg_payload *mesh = bsg_payload_create(BSG_PAYLOAD_TYPE_MESH);
    struct bv_mesh_lod lod;
    point_t bmin, bmax;

    if (!mesh)
	FAIL("mesh payload create");

    memset(&lod, 0, sizeof(lod));
    VSET(lod.bmin, -1.0, -2.0, -3.0);
    VSET(lod.bmax, 4.0, 5.0, 6.0);

    bsg_payload_mesh_set(mesh, &lod);
    if (bsg_payload_mesh_get(mesh) != &lod)
	FAIL("mesh payload get");
    if (bsg_payload_mesh_lod_get(mesh) != &lod)
	FAIL("mesh payload lod get");
    if (!bsg_payload_bounds(mesh, &bmin, &bmax))
	FAIL("mesh payload bounds");
    if (!NEAR_EQUAL(bmax[Z], 6.0, SMALL_FASTF))
	FAIL("mesh bounds z");

    bsg_payload_destroy(mesh);
    PASS("mesh payload wrapper");
    return 0;
}

int
main(int UNUSED(argc), const char **argv)
{
    int failures = 0;
    bu_setprogname(argv[0]);

    failures += test_vlist_payload_and_wire();
    failures += test_mesh_payload();

    if (failures == 0)
	printf("RESULT: all payload tests PASSED\n");
    else
	printf("RESULT: %d payload test(s) FAILED\n", failures);

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
