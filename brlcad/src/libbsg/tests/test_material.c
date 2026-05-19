/*               T E S T _ M A T E R I A L . C
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
/** @file libbsg/tests/test_material.c
 *
 * Phase 3 tests for BSG material and appearance compatibility mapping.
 */

#include "common.h"

#include <stdio.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/util.h"
#include "bsg/appearance.h"
#include "bsg/identity.h"
#include "bsg/material.h"
#include "bsg/node_shape.h"
#include "bsg/util.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

static struct bview *
make_view(void)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_view_material");
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v)
	return;
    bv_free(v);
    bu_free(v, "test_view_material");
}

static int
test_material_mapping(void)
{
    printf("=== Test 1: material_mapping ===\n");
    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    struct bsg_material m;
    struct bsg_material out;
    if (!root || !shape)
	FAIL("create nodes");

    bsg_material_init(&m);
    bsg_material_set_rgba(&m, 10, 20, 30, 191);
    m.transparency = 0.75;
    m.revision = 17;
    bsg_node_material_set(shape, &m);
    if (bsg_node_revision(shape, BSG_NODE_REV_MATERIAL) == 0)
	FAIL("material revision bump");

    (void)bsg_node_material_get(shape, &out);
    if (out.rgba[0] != 10 || out.rgba[1] != 20 || out.rgba[2] != 30)
	FAIL("material getter rgba");
    if (((struct bv_scene_obj *)shape)->s_color[0] != 10 || ((struct bv_scene_obj *)shape)->s_color[1] != 20 || ((struct bv_scene_obj *)shape)->s_color[2] != 30)
	FAIL("legacy s_color sync");
    if (((struct bv_scene_obj *)shape)->s_color_rev != 17)
	FAIL("legacy s_color_rev sync");

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("material_mapping");
    return 0;
}

static int
test_appearance_mapping(void)
{
    printf("=== Test 2: appearance_mapping ===\n");
    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    struct bsg_appearance a;
    struct bsg_appearance out;
    if (!root || !shape)
	FAIL("create nodes");

    bsg_appearance_init(&a);
    a.draw_mode = 3;
    a.line_width = 4;
    a.line_style = BSG_APPEARANCE_LINE_DASHED;
    a.transparency = 0.5;
    bsg_node_appearance_set(shape, &a);
    if (bsg_node_revision(shape, BSG_NODE_REV_APPEARANCE) == 0)
	FAIL("appearance revision bump");

    (void)bsg_node_appearance_get(shape, &out);
    if (out.line_width != 4 || out.draw_mode != 3)
	FAIL("appearance getter");
    if (!((struct bv_scene_obj *)shape)->bsg.settings_local)
	FAIL("settings sidecar not created by appearance_set");
    if (((struct bv_scene_obj *)shape)->bsg.settings_local->line_width != 4)
	FAIL("BSG settings line width sync");
    if (((struct bv_scene_obj *)shape)->s_os->line_width != 4)
	FAIL("legacy line width sync");
    if (((struct bv_scene_obj *)shape)->s_soldash != 1)
	FAIL("legacy soldash sync");
    if (!NEAR_EQUAL(((struct bv_scene_obj *)shape)->s_local_os.transparency, 1.0, SMALL_FASTF))
	FAIL("appearance should not own transparency mirror");

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("appearance_mapping");
    return 0;
}

static int
test_view_setter_hooks(void)
{
    printf("=== Test 3: view_setter_hooks ===\n");
    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    struct bv_scene_obj *obj = bv_view_obj_lines_create(v, "mat_hook_obj", 1);
    if (!root || !obj)
	FAIL("create view object");

    bv_view_obj_set_color(obj, 1, 2, 3);
    bv_view_obj_set_line_width(obj, 6);

    struct bsg_material m;
    struct bsg_appearance a;
    (void)bsg_node_material_get((const bsg_node *)obj, &m);
    (void)bsg_node_appearance_get((const bsg_node *)obj, &a);
    if (m.rgba[0] != 1 || m.rgba[1] != 2 || m.rgba[2] != 3)
	FAIL("color hook routed to bsg material");
    if (a.line_width != 6)
	FAIL("line width hook routed to bsg appearance");

    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("view_setter_hooks");
    return 0;
}

int
main(int UNUSED(argc), const char **argv)
{
    bu_setprogname(argv[0]);
    int failures = 0;
    failures += test_material_mapping();
    failures += test_appearance_mapping();
    failures += test_view_setter_hooks();
    if (failures) {
	printf("FAIL: %d test group(s) failed\n", failures);
	return 1;
    }
    printf("PASS: all material/appearance tests passed\n");
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
