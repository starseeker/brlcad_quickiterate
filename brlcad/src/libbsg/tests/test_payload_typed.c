/*            T E S T _ P A Y L O A D _ T Y P E D . C
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
/** @file libbsg/tests/test_payload_typed.c
 *
 * Phase D1 typed payload regression tests.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bsg/faceplate.h"
#include "bsg/node_shape.h"
#include "bsg/payload_typed.h"
#include "bsg/polygon.h"
#include "bsg/util.h"
#include "bsg/vlist.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

static struct bsg_view *
make_view(void)
{
    struct bsg_view *v;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "payload_view");
    return v;
}

static void
free_view(struct bsg_view *v)
{
    if (!v)
	return;
    bsg_view_free(v);
    bu_free(v, "payload_view");
}

static int
test_vlist_node_helpers(void)
{
    printf("=== Test 1: vlist node helpers ===\n");

    struct bsg_view *v = make_view();
    bsg_node *shape = bsg_shape_create(v);
    if (!shape) FAIL("bsg_shape_create returned NULL");

    point_t p1 = VINIT_ZERO;
    point_t p2 = VINIT_ZERO;
    point_t p3 = VINIT_ZERO;
    VSET(p1, 0.0, 0.0, 0.0);
    VSET(p2, 1.0, 0.0, 0.0);
    VSET(p3, 1.0, 1.0, 0.0);

    if (!bsg_node_clear_vlist_payload(shape)) FAIL("clear vlist payload");
    if (!bsg_node_append_vlist_payload(shape, p1, BSG_VLIST_LINE_MOVE)) FAIL("append move");
    if (!bsg_node_append_vlist_payload(shape, p2, BSG_VLIST_LINE_DRAW)) FAIL("append draw 1");
    if (!bsg_node_append_vlist_payload(shape, p3, BSG_VLIST_LINE_DRAW)) FAIL("append draw 2");

    struct bsg_payload *pl = bsg_node_get_payload(shape);
    if (!pl || pl->pl_type != BSG_PL_VLIST) FAIL("shape missing vlist payload");
    if (shape->s_vlen != 3) FAIL("shape vlen not updated");
    if (pl->pl_revision != 4) FAIL("payload revision not updated");

    if (!bsg_payload_vlist_get(pl) || !bsg_payload_vlist_get(pl)->vlist)
	FAIL("vlist payload data missing");

    bsg_shape_destroy(shape);
    free_view(v);

    PASS("vlist node helpers");
    return 0;
}

static int
test_polygon_payload(void)
{
    printf("=== Test 2: polygon payload ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("bsg_scene_root_create returned NULL");
    point_t origin = VINIT_ZERO;
    bsg_node *poly = bsg_create_polygon(v, BSG_OBJ_VIEW, BV_POLYGON_RECTANGLE, &origin);
    if (!poly) FAIL("bsg_create_polygon returned NULL");
    if (!bsg_node_polygon(poly)) FAIL("node polygon accessor returned NULL");
    if (!bsg_node_get_payload(poly) || bsg_node_get_payload(poly)->pl_type != BSG_PL_POLYGON)
	FAIL("polygon node missing typed payload");

    bsg_obj_put(poly);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("polygon payload");
    return 0;
}

static int
test_remaining_payload_builders(void)
{
    printf("=== Test 3: remaining payload builders ===\n");

    struct bsg_label *label;
    BU_GET(label, struct bsg_label);
    memset(label, 0, sizeof(*label));
    BU_VLS_INIT(&label->label);
    bu_vls_sprintf(&label->label, "hud");

    struct bsg_payload *hud = bsg_payload_hud_text_create(label);
    if (!hud || !bsg_payload_hud_text_get(hud)) FAIL("hud text payload");
    bsg_payload_free(hud);

    point_t pts[2] = {VINIT_ZERO, VINIT_ZERO};
    int cmds[2] = {BSG_VLIST_LINE_MOVE, BSG_VLIST_LINE_DRAW};
    struct bsg_payload *line_set = bsg_payload_line_set_create(pts, cmds, 2);
    if (!line_set || !bsg_payload_line_set_get(line_set)) FAIL("line set payload");
    bsg_payload_free(line_set);

    unsigned char px[4] = {255, 0, 0, 255};
    struct bsg_payload *image = bsg_payload_image_create(1, 1, 4, px);
    if (!image || !bsg_payload_image_get(image)) FAIL("image payload");
    bsg_payload_free(image);

    struct bsg_payload *fb = bsg_payload_framebuffer_create(NULL, 7);
    if (!fb || !bsg_payload_framebuffer_get(fb) || bsg_payload_framebuffer_get(fb)->mode != 7)
	FAIL("framebuffer payload");
    bsg_payload_free(fb);

    struct bsg_grid_state grid;
    memset(&grid, 0, sizeof(grid));
    grid.draw = 1;
    struct bsg_payload *gpl = bsg_payload_grid_create(&grid);
    if (!gpl || !bsg_payload_grid_get(gpl) || !bsg_payload_grid_get(gpl)->draw)
	FAIL("grid payload");
    bsg_payload_free(gpl);

    struct bsg_payload *ann = bsg_payload_annotation_create("measure", pts, 2);
    if (!ann || !bsg_payload_annotation_get(ann)) FAIL("annotation payload");
    bsg_payload_free(ann);

    struct bsg_payload *mesh = bsg_payload_mesh_create(NULL);
    struct bsg_payload *csg = bsg_payload_csg_create(NULL);
    struct bsg_payload *brep = bsg_payload_brep_create(NULL);
    if (!mesh || mesh->pl_type != BSG_PL_MESH) FAIL("mesh payload");
    if (!csg || csg->pl_type != BSG_PL_CSG) FAIL("csg payload");
    if (!brep || brep->pl_type != BSG_PL_BREP) FAIL("brep payload");
    bsg_payload_free(mesh);
    bsg_payload_free(csg);
    bsg_payload_free(brep);

    PASS("remaining payload builders");
    return 0;
}

int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);
    if (argc > 1)
	fprintf(stderr, "Unexpected arguments\n");

    int ret = 0;
    ret |= test_vlist_node_helpers();
    ret |= test_polygon_payload();
    ret |= test_remaining_payload_builders();

    return ret;
}
