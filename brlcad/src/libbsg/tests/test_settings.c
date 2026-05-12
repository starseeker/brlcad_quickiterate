/*            T E S T _ S E T T I N G S . C
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
/** @file libbsg/tests/test_settings.c
 *
 * Phase 12 unit tests for the BSG settings-inheritance API.
 *
 * These tests verify:
 *  12A - bsg_settings_init returns safe defaults.
 *  12B - bsg_settings_from/to_legacy_obj_settings round-trip all fields.
 *  12C - bsg_node_settings_get reads from s_os when set.
 *  12D - bsg_node_settings_get reads from s_local_os when s_os is NULL.
 *  12E - bsg_node_settings_set writes to s_local_os and updates s_os.
 *  12F - NULL-safety: all public functions tolerate NULL arguments.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/node_shape.h"
#include "bsg/settings.h"
#include "bsg/util.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

static struct bview *
make_view(void)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_view_settings");
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v)
	return;
    bv_free(v);
    bu_free(v, "test_view_settings");
}


/* ------------------------------------------------------------------ */
/* Test 12A: bsg_settings_init defaults                                 */
/* ------------------------------------------------------------------ */

static int
test_settings_init(void)
{
    printf("=== Test 12A: bsg_settings_init defaults ===\n");
    struct bsg_settings s;
    bsg_settings_init(&s);

    if (s.draw_mode != 0)               FAIL("draw_mode default");
    if (s.mixed_modes != 0)             FAIL("mixed_modes default");
    if (s.transparency < 0.999)         FAIL("transparency default (not 1.0)");
    if (s.color_override != 0)          FAIL("color_override default");
    if (s.color[0] != 255 || s.color[1] != 255 || s.color[2] != 255)
	FAIL("color default");
    if (s.line_width != 1)              FAIL("line_width default");
    if (s.draw_solid_lines_only != 0)   FAIL("draw_solid_lines_only default");
    if (s.draw_non_subtract_only != 0)  FAIL("draw_non_subtract_only default");

    bsg_settings_init(NULL); /* should not crash */
    PASS("bsg_settings_init");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 12B: round-trip through legacy bv_obj_settings                  */
/* ------------------------------------------------------------------ */

static int
test_legacy_roundtrip(void)
{
    printf("=== Test 12B: legacy round-trip ===\n");

    struct bv_obj_settings os;
    struct bsg_settings s;
    struct bsg_settings back;
    struct bv_obj_settings os2;

    memset(&os, 0, sizeof(os));
    os.s_dmode             = 2;
    os.mixed_modes         = 1;
    os.transparency        = 0.5;
    os.color_override      = 1;
    os.color[0]            = 10;
    os.color[1]            = 20;
    os.color[2]            = 30;
    os.s_line_width        = 3;
    os.s_arrow_tip_length  = 0.1;
    os.s_arrow_tip_width   = 0.05;
    os.draw_solid_lines_only   = 1;
    os.draw_non_subtract_only  = 1;

    bsg_settings_from_legacy_obj_settings(&os, &s);

    if (s.draw_mode != 2)              FAIL("draw_mode from legacy");
    if (s.mixed_modes != 1)            FAIL("mixed_modes from legacy");
    if (s.transparency < 0.49)         FAIL("transparency from legacy low");
    if (s.transparency > 0.51)         FAIL("transparency from legacy high");
    if (s.color_override != 1)         FAIL("color_override from legacy");
    if (s.color[0] != 10)              FAIL("color[0] from legacy");
    if (s.color[1] != 20)              FAIL("color[1] from legacy");
    if (s.color[2] != 30)              FAIL("color[2] from legacy");
    if (s.line_width != 3)             FAIL("line_width from legacy");
    if (s.draw_solid_lines_only != 1)  FAIL("draw_solid_lines_only from legacy");
    if (s.draw_non_subtract_only != 1) FAIL("draw_non_subtract_only from legacy");

    /* Round-trip back */
    memset(&os2, 0, sizeof(os2));
    bsg_settings_to_legacy_obj_settings(&s, &os2);
    if (os2.s_dmode != 2)              FAIL("s_dmode to legacy");
    if (os2.color[0] != 10)            FAIL("color[0] to legacy");
    if (os2.color[1] != 20)            FAIL("color[1] to legacy");
    if (os2.color[2] != 30)            FAIL("color[2] to legacy");
    if (os2.transparency < 0.49)       FAIL("transparency to legacy low");
    if (os2.transparency > 0.51)       FAIL("transparency to legacy high");
    if (os2.draw_solid_lines_only != 1) FAIL("draw_solid_lines_only to legacy");

    /* NULL safety */
    bsg_settings_from_legacy_obj_settings(NULL, &back);
    if (back.transparency < 0.999) FAIL("null-os transparency should default to 1.0");
    bsg_settings_from_legacy_obj_settings(&os, NULL); /* should not crash */
    bsg_settings_to_legacy_obj_settings(NULL, &os2);  /* should not crash */
    bsg_settings_to_legacy_obj_settings(&s, NULL);    /* should not crash */

    PASS("legacy_roundtrip");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 12C: bsg_node_settings_get reads from s_os when set             */
/* ------------------------------------------------------------------ */

static int
test_get_from_s_os(void)
{
    printf("=== Test 12C: get from s_os ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    if (!root || !shape)
	FAIL("create nodes");

    struct bv_scene_obj *s = (struct bv_scene_obj *)shape;
    s->s_local_os.color[0]    = 7;
    s->s_local_os.color[1]    = 14;
    s->s_local_os.color[2]    = 21;
    s->s_local_os.transparency = 0.75;
    s->s_os = &s->s_local_os;

    struct bsg_settings out;
    int rc = bsg_node_settings_get((const bsg_node *)shape, &out);
    if (!rc)
	FAIL("settings_get returned 0");
    if (out.color[0] != 7 || out.color[1] != 14 || out.color[2] != 21)
	FAIL("color from s_os");
    if (out.transparency < 0.74 || out.transparency > 0.76)
	FAIL("transparency from s_os");

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("get_from_s_os");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 12D: bsg_node_settings_get reads from s_local_os when s_os NULL */
/* ------------------------------------------------------------------ */

static int
test_get_from_s_local_os(void)
{
    printf("=== Test 12D: get from s_local_os (s_os NULL) ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    if (!root || !shape)
	FAIL("create nodes");

    struct bv_scene_obj *s = (struct bv_scene_obj *)shape;
    s->s_os = NULL;
    s->s_local_os.color[0]    = 50;
    s->s_local_os.color[1]    = 100;
    s->s_local_os.color[2]    = 150;
    s->s_local_os.transparency = 0.6;

    struct bsg_settings out;
    int rc = bsg_node_settings_get((const bsg_node *)shape, &out);
    if (!rc)
	FAIL("settings_get returned 0");
    if (out.color[0] != 50 || out.color[1] != 100 || out.color[2] != 150)
	FAIL("color from s_local_os");
    if (out.transparency < 0.59 || out.transparency > 0.61)
	FAIL("transparency from s_local_os");

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("get_from_s_local_os");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 12E: bsg_node_settings_set writes s_local_os and updates s_os  */
/* ------------------------------------------------------------------ */

static int
test_settings_set(void)
{
    printf("=== Test 12E: bsg_node_settings_set ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    if (!root || !shape)
	FAIL("create nodes");

    struct bsg_settings s;
    bsg_settings_init(&s);
    s.color[0]        = 99;
    s.color[1]        = 88;
    s.color[2]        = 77;
    s.transparency    = 0.3;
    s.color_override  = 1;
    s.draw_mode       = 2;
    s.line_width      = 5;

    bsg_node_settings_set(shape, &s);

    struct bv_scene_obj *obj = (struct bv_scene_obj *)shape;
    if (obj->s_os != &obj->s_local_os)
	FAIL("s_os not updated to &s_local_os");
    if (obj->s_local_os.color[0] != 99 || obj->s_local_os.color[1] != 88 || obj->s_local_os.color[2] != 77)
	FAIL("color not written to s_local_os");
    if (obj->s_local_os.transparency < 0.29 || obj->s_local_os.transparency > 0.31)
	FAIL("transparency not written to s_local_os");
    if (obj->s_local_os.color_override != 1)
	FAIL("color_override not written");
    if (obj->s_local_os.s_dmode != 2)
	FAIL("s_dmode not written");
    if (obj->s_local_os.s_line_width != 5)
	FAIL("s_line_width not written");

    /* Round-trip via getter */
    struct bsg_settings out;
    bsg_node_settings_get((const bsg_node *)shape, &out);
    if (out.color[0] != 99)       FAIL("getter after set color[0]");
    if (out.color_override != 1)  FAIL("getter after set color_override");
    if (out.line_width != 5)      FAIL("getter after set line_width");

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("settings_set");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 12F: NULL safety                                                */
/* ------------------------------------------------------------------ */

static int
test_null_safety(void)
{
    printf("=== Test 12F: NULL safety ===\n");

    struct bsg_settings s;
    bsg_settings_init(&s);

    int rc = bsg_node_settings_get(NULL, &s);
    if (rc)
	FAIL("get NULL node should return 0");

    bsg_node_settings_get(NULL, NULL);  /* no crash */
    bsg_node_settings_set(NULL, &s);    /* no crash */
    bsg_node_settings_set(NULL, NULL);  /* no crash */

    PASS("null_safety");
    return 0;
}


/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);
    (void)argc;

    int failures = 0;
    failures += test_settings_init();
    failures += test_legacy_roundtrip();
    failures += test_get_from_s_os();
    failures += test_get_from_s_local_os();
    failures += test_settings_set();
    failures += test_null_safety();

    if (failures) {
	printf("FAILED: %d test(s)\n", failures);
	return 1;
    }
    printf("ALL PASS\n");
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
