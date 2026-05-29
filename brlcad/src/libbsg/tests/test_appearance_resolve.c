/*  T E S T _ A P P E A R A N C E _ R E S O L V E . C
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
/** @file libbsg/tests/test_appearance_resolve.c
 *
 * Phase D5 unit tests: bsg_appearance_resolve — layer composition,
 * path accumulation, highlight, and inheritance.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "vmath.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/node.h"
#include "bsg/node_shape.h"
#include "bsg/appearance.h"
#include "bsg/appearance_action.h"
#include "bsg/node_private.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

static struct bsg_view *
_make_view(void)
{
    struct bsg_view *v;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    return v;
}

static void
_free_view(struct bsg_view *v)
{
    if (!v) return;
    bsg_view_free(v);
    bu_free(v, "bsg_view");
}


/* -----------------------------------------------------------------------
 * Test 1: null inputs return 0
 * ----------------------------------------------------------------------- */
static int
test_null_inputs(void)
{
    printf("=== Test 1: null inputs ===\n");

    struct bsg_resolved_appearance ra;
    memset(&ra, 0, sizeof(ra));

    /* NULL node */
    int r = bsg_appearance_resolve(NULL, NULL, NULL, &ra);
    if (r != 0) FAIL("NULL node should return 0");

    /* NULL out */
    struct bsg_view *v = _make_view();
    bsg_node *s = bsg_shape_create(v);
    r = bsg_appearance_resolve(v, s, NULL, NULL);
    if (r != 0) FAIL("NULL out should return 0");

    bsg_node_destroy(s);
    _free_view(v);
    PASS("null inputs");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 2: base layer — node s_color, no override
 * ----------------------------------------------------------------------- */
static int
test_base_color(void)
{
    printf("=== Test 2: base layer color ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *s = bsg_shape_create(v);
    s->s_color[0] = 200;
    s->s_color[1] = 100;
    s->s_color[2] = 50;

    struct bsg_resolved_appearance ra;
    memset(&ra, 0, sizeof(ra));
    int r = bsg_appearance_resolve(v, s, NULL, &ra);
    if (!r)          FAIL("should succeed for valid node");
    if (ra.color[0] != 200) FAIL("color[0] should be 200");
    if (ra.color[1] != 100) FAIL("color[1] should be 100");
    if (ra.color[2] != 50)  FAIL("color[2] should be 50");
    if (!(ra.active_layers & BSG_ALAY_BASE)) FAIL("BSG_ALAY_BASE should be set");

    bsg_node_destroy(s);
    _free_view(v);
    PASS("base layer color");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 3: command override (s_os color_override)
 * ----------------------------------------------------------------------- */
static int
test_command_override(void)
{
    printf("=== Test 3: command override ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *s = bsg_shape_create(v);
    s->s_color[0] = 10;
    s->s_color[1] = 20;
    s->s_color[2] = 30;

    struct bsg_obj_settings *os;
    BU_ALLOC(os, struct bsg_obj_settings);
    memset(os, 0, sizeof(struct bsg_obj_settings));
    os->color_override = 1;
    os->color[0] = 255;
    os->color[1] = 0;
    os->color[2] = 0;
    os->transparency = 1.0;
    os->s_line_width = 1;
    s->s_os = os;

    struct bsg_resolved_appearance ra;
    memset(&ra, 0, sizeof(ra));
    bsg_appearance_resolve(v, s, NULL, &ra);

    if (ra.color[0] != 255) FAIL("override color[0] should be 255");
    if (ra.color[1] != 0)   FAIL("override color[1] should be 0");
    if (ra.color[2] != 0)   FAIL("override color[2] should be 0");
    if (!(ra.active_layers & BSG_ALAY_COMMAND)) FAIL("BSG_ALAY_COMMAND should be set");

    bu_free(os, "os");
    s->s_os = NULL;
    bsg_node_destroy(s);
    _free_view(v);
    PASS("command override");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 4: highlight state (s_iflag == UP)
 * ----------------------------------------------------------------------- */
static int
test_highlight(void)
{
    printf("=== Test 4: highlight state ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *s = bsg_shape_create(v);
    s->s_iflag = UP;

    struct bsg_resolved_appearance ra;
    memset(&ra, 0, sizeof(ra));
    bsg_appearance_resolve(v, s, NULL, &ra);

    if (!ra.highlighted) FAIL("highlighted should be non-zero when s_iflag==UP");
    if (!(ra.active_layers & BSG_ALAY_HIGHLIGHT)) FAIL("BSG_ALAY_HIGHLIGHT should be set");

    bsg_node_destroy(s);
    _free_view(v);
    PASS("highlight state");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 5: transparency layer
 * ----------------------------------------------------------------------- */
static int
test_transparency(void)
{
    printf("=== Test 5: transparency layer ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *s = bsg_shape_create(v);

    struct bsg_obj_settings *os;
    BU_ALLOC(os, struct bsg_obj_settings);
    memset(os, 0, sizeof(struct bsg_obj_settings));
    os->transparency = 0.5;
    os->s_line_width = 1;
    s->s_os = os;

    struct bsg_resolved_appearance ra;
    memset(&ra, 0, sizeof(ra));
    bsg_appearance_resolve(v, s, NULL, &ra);

    if (fabs(ra.transparency - 0.5) > 1e-6)
	FAIL("transparency should be 0.5");
    if (!(ra.active_layers & BSG_ALAY_TRANSPARENCY))
	FAIL("BSG_ALAY_TRANSPARENCY should be set for transparency < 1");

    bu_free(os, "os");
    s->s_os = NULL;
    bsg_node_destroy(s);
    _free_view(v);
    PASS("transparency layer");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 6: fully opaque node has transparency == 1.0
 * ----------------------------------------------------------------------- */
static int
test_opaque_default(void)
{
    printf("=== Test 6: opaque default ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *s = bsg_shape_create(v);

    struct bsg_resolved_appearance ra;
    memset(&ra, 0, sizeof(ra));
    bsg_appearance_resolve(v, s, NULL, &ra);

    if (fabs(ra.transparency - 1.0) > 1e-6)
	FAIL("default transparency should be 1.0 (fully opaque)");
    if (ra.active_layers & BSG_ALAY_TRANSPARENCY)
	FAIL("BSG_ALAY_TRANSPARENCY should NOT be set for fully opaque node");

    bsg_node_destroy(s);
    _free_view(v);
    PASS("opaque default");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 7: line style / dmode from s_os
 * ----------------------------------------------------------------------- */
static int
test_line_style_dmode(void)
{
    printf("=== Test 7: line style and dmode ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *s = bsg_shape_create(v);

    struct bsg_obj_settings *os;
    BU_ALLOC(os, struct bsg_obj_settings);
    memset(os, 0, sizeof(struct bsg_obj_settings));
    os->transparency = 1.0;
    os->s_dmode      = 3;   /* arbitrary non-zero display mode */
    os->s_line_width = 2;
    os->s_soldash    = 1;
    s->s_os = os;

    struct bsg_resolved_appearance ra;
    memset(&ra, 0, sizeof(ra));
    bsg_appearance_resolve(v, s, NULL, &ra);

    if (ra.dmode != 3)      FAIL("dmode should be 3");
    if (ra.line_width != 2) FAIL("line_width should be 2");
    if (ra.line_style != 1) FAIL("line_style should be 1");

    bu_free(os, "os");
    s->s_os = NULL;
    bsg_node_destroy(s);
    _free_view(v);
    PASS("line style and dmode");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 8: inherited_os color override
 * ----------------------------------------------------------------------- */
static int
test_inherited_override(void)
{
    printf("=== Test 8: inherited_os color override ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *s = bsg_shape_create(v);
    /* Node has its own color */
    s->s_color[0] = 50;
    s->s_color[1] = 50;
    s->s_color[2] = 50;

    /* Inherited settings provide a group override */
    struct bsg_obj_settings inherited;
    memset(&inherited, 0, sizeof(inherited));
    inherited.color_override = 1;
    inherited.color[0] = 100;
    inherited.color[1] = 150;
    inherited.color[2] = 200;
    inherited.transparency = 1.0;
    inherited.s_line_width = 1;

    struct bsg_resolved_appearance ra;
    memset(&ra, 0, sizeof(ra));
    bsg_appearance_resolve(v, s, &inherited, &ra);

    /* Inherited color should win over node's base color */
    if (ra.color[0] != 100) FAIL("inherited color[0] should be 100");
    if (ra.color[1] != 150) FAIL("inherited color[1] should be 150");
    if (ra.color[2] != 200) FAIL("inherited color[2] should be 200");
    if (!(ra.active_layers & BSG_ALAY_INHERITED)) FAIL("BSG_ALAY_INHERITED should be set");

    bsg_node_destroy(s);
    _free_view(v);
    PASS("inherited_os color override");
    return 0;
}


/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);
    (void)argc;

    int failures = 0;
    failures += test_null_inputs();
    failures += test_base_color();
    failures += test_command_override();
    failures += test_highlight();
    failures += test_transparency();
    failures += test_opaque_default();
    failures += test_line_style_dmode();
    failures += test_inherited_override();

    if (failures) {
	printf("\n%d test(s) FAILED\n", failures);
	return 1;
    }
    printf("\nAll appearance_resolve tests PASSED\n");
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
