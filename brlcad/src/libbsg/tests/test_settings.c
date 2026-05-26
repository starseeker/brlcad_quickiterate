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
 *  12B - bsg_settings storage directly carries all fields.
 *  12C - bsg_node_settings_get reads from s_os when set.
 *  12D - bsg_node_settings_get reads from s_local_os when s_os is NULL.
 *  12E - bsg_node_settings_set populates BSG-owned settings sidecars, mirrors legacy local storage, and syncs semantic sidecars.
 *  12F - bsg_settings_sync copies mixed_modes.
 *  12G - NULL-safety: all public functions tolerate NULL arguments.
 *  12H - bv compatibility settings shims expose effective/local/reset behavior.
 *  12I - draw-request split helpers map compatibility settings into appearance/material/policy.
 *  12J - node draw-request/policy helpers round-trip through BSG-owned storage.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/appearance.h"
#include "bsg/material.h"
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
/* Test 12B: direct bsg_settings storage                                */
/* ------------------------------------------------------------------ */

static int
test_settings_storage(void)
{
    printf("=== Test 12B: direct settings storage ===\n");

    struct bsg_settings s;
    struct bsg_settings back;

    bsg_settings_init(&s);
    s.draw_mode             = 2;
    s.mixed_modes           = 1;
    s.transparency          = 0.5;
    s.color_override        = 1;
    s.color[0]              = 10;
    s.color[1]              = 20;
    s.color[2]              = 30;
    s.line_width            = 3;
    s.arrow_tip_length      = 0.1;
    s.arrow_tip_width       = 0.05;
    s.draw_solid_lines_only = 1;
    s.draw_non_subtract_only = 1;

    back = s;

    if (back.draw_mode != 2)              FAIL("draw_mode copy");
    if (back.mixed_modes != 1)            FAIL("mixed_modes copy");
    if (back.transparency < 0.49)         FAIL("transparency copy low");
    if (back.transparency > 0.51)         FAIL("transparency copy high");
    if (back.color_override != 1)         FAIL("color_override copy");
    if (back.color[0] != 10)              FAIL("color[0] copy");
    if (back.color[1] != 20)              FAIL("color[1] copy");
    if (back.color[2] != 30)              FAIL("color[2] copy");
    if (back.line_width != 3)             FAIL("line_width copy");
    if (back.draw_solid_lines_only != 1)  FAIL("draw_solid_lines_only copy");
    if (back.draw_non_subtract_only != 1) FAIL("draw_non_subtract_only copy");

    PASS("settings_storage");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 12C: bsg_node_settings_set + bsg_node_settings_get round-trip     */
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

    /* Use BSG API to set settings.  Direct s_local_os writes are no longer
     * visible to bsg_node_settings_get since the BSG settings sidecars
     * (settings_local / settings_effective) are created at node-alloc time
     * by bv_scene_obj_settings_reset().  Phase 12: Doxygen @deprecated. */
    struct bsg_settings s;
    bsg_settings_init(&s);
    s.color[0]        = 7;
    s.color[1]        = 14;
    s.color[2]        = 21;
    s.color_override  = 1;
    s.transparency    = 0.75;
    bsg_node_settings_set(shape, &s);

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


/* Test 12D: bsg_node_settings_set without color_override round-trip      */
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

    /* Use BSG API: direct s_local_os / s_os writes bypass the BSG
     * settings sidecars created at alloc time.  Phase 12: @deprecated. */
    struct bsg_settings s;
    bsg_settings_init(&s);
    s.color[0]        = 50;
    s.color[1]        = 100;
    s.color[2]        = 150;
    s.color_override  = 1;
    s.transparency    = 0.6;
    bsg_node_settings_set(shape, &s);

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
/* Test 12E: bsg_node_settings_set populates sidecars and legacy mirror */
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
    s.mixed_modes     = 1;

    bsg_node_settings_set(shape, &s);

    struct bv_scene_obj *obj = (struct bv_scene_obj *)shape;
    if (!obj->bsg.settings_local || !obj->bsg.settings_effective)
	FAIL("BSG settings sidecars not populated");
    if (obj->s_os != &obj->s_local_os)
	FAIL("legacy s_os not updated to local mirror");
    if (obj->bsg.settings_local->color[0] != 99 || obj->bsg.settings_local->color[1] != 88 || obj->bsg.settings_local->color[2] != 77)
	FAIL("color not written to settings_local");
    if (obj->bsg.settings_effective->mixed_modes != 1)
	FAIL("mixed_modes not written to effective settings");
    if (obj->s_local_os.color[0] != 99 || obj->s_local_os.color[1] != 88 || obj->s_local_os.color[2] != 77)
	FAIL("color not mirrored to legacy local storage");
    if (obj->s_local_os.transparency < 0.29 || obj->s_local_os.transparency > 0.31)
	FAIL("transparency not mirrored to legacy local storage");
    if (obj->s_local_os.color_override != 1)
	FAIL("color_override not written");
    if (obj->s_local_os.draw_mode != 2)
	FAIL("draw_mode not mirrored to legacy local storage");
    if (obj->s_local_os.line_width != 5)
	FAIL("line_width not mirrored to legacy local storage");
    if (obj->s_local_os.mixed_modes != 1)
	FAIL("mixed_modes not mirrored to legacy local storage");
    struct bsg_appearance a;
    struct bsg_material m;
    bsg_appearance_init(&a);
    bsg_material_init(&m);
    if (!bsg_node_appearance_get((const bsg_node *)shape, &a))
	FAIL("appearance sidecar not populated");
    if (!bsg_node_material_get((const bsg_node *)shape, &m))
	FAIL("material sidecar not populated");
    if (a.draw_mode != 2 || a.line_width != 5)
	FAIL("appearance semantic mapping");
    if (!m.use_override_color)
	FAIL("material override mapping");
    if (m.override_rgb[0] != 99 || m.override_rgb[1] != 88 || m.override_rgb[2] != 77)
	FAIL("material override color mapping");
    if (m.transparency < 0.29 || m.transparency > 0.31)
	FAIL("material transparency mapping");

    /* Round-trip via getter */
    struct bsg_settings out;
    bsg_node_settings_get((const bsg_node *)shape, &out);
    if (out.color[0] != 99)       FAIL("getter after set color[0]");
    if (out.color_override != 1)  FAIL("getter after set color_override");
    if (out.line_width != 5)      FAIL("getter after set line_width");
    if (out.mixed_modes != 1)     FAIL("getter after set mixed_modes");
    if (out.transparency < 0.29 || out.transparency > 0.31)
	FAIL("getter after set transparency");

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("settings_set");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 12F: bsg_settings_sync                                          */
/* ------------------------------------------------------------------ */

static int
test_settings_sync(void)
{
    printf("=== Test 12F: bsg_settings_sync ===\n");

    struct bsg_settings dest;
    struct bsg_settings src;
    bsg_settings_init(&dest);
    bsg_settings_init(&src);

    src.mixed_modes = 1;
    src.draw_mode = 4;

    int changed = bsg_settings_sync(&dest, &src);
    if (!changed)
	FAIL("settings_sync should report change");
    if (dest.mixed_modes != 1)
	FAIL("mixed_modes sync");
    if (dest.draw_mode != 4)
	FAIL("draw_mode sync");

    PASS("settings_sync");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 12G: NULL safety                                                */
/* ------------------------------------------------------------------ */

static int
test_null_safety(void)
{
    printf("=== Test 12G: NULL safety ===\n");

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
/* Test 12H: bv compatibility settings helpers                         */
/* ------------------------------------------------------------------ */

static int
test_bv_settings_helpers(void)
{
    printf("=== Test 12H: bv settings helpers ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    if (!root || !shape)
	FAIL("create nodes");

    struct bv_scene_obj *obj = (struct bv_scene_obj *)shape;
    struct bsg_settings local = BSG_SETTINGS_INIT;
    struct bsg_settings out = BSG_SETTINGS_INIT;

    /* Use the compat setter rather than writing s_local_os / s_os directly.
     * Direct writes are no longer visible once the BSG sidecars exist. */
    local.line_width = 2;
    local.draw_mode = 1;
    bv_scene_obj_settings_set(obj, &local);

    if (!bv_scene_obj_settings_local_get(obj, &out))
	FAIL("local helper returned 0");
    if (out.line_width != 2 || out.draw_mode != 1)
	FAIL("local helper values");

    if (!bv_scene_obj_settings_get(obj, &out))
	FAIL("effective helper returned 0");
    if (out.line_width != 2 || out.draw_mode != 1)
	FAIL("effective helper values");

    out.line_width = 5;
    out.draw_mode = 3;
    out.mixed_modes = 1;
    bv_scene_obj_settings_set(obj, &out);
    if (!obj->bsg.settings_local || !obj->bsg.settings_effective)
	FAIL("compat helper did not create BSG settings sidecars");
    if (obj->s_os != &obj->s_local_os)
	FAIL("settings_set did not restore s_os to local storage");
    if (obj->s_local_os.line_width != 5 || obj->s_local_os.draw_mode != 3 || obj->s_local_os.mixed_modes != 1)
	FAIL("settings_set did not update local storage");

    bv_scene_obj_settings_reset(obj);
    if (obj->s_local_os.line_width != 1 || obj->s_local_os.draw_mode != 0 || obj->s_local_os.mixed_modes != 0)
	FAIL("settings_reset defaults");
    if (obj->s_os != &obj->s_local_os)
	FAIL("settings_reset did not preserve local storage pointer");

    bv_scene_obj_settings_get(NULL, &out);
    bv_scene_obj_settings_local_get(NULL, &out);
    bv_scene_obj_settings_set(NULL, &out);
    bv_scene_obj_settings_reset(NULL);

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("bv_settings_helpers");
    return 0;
}

static int
test_draw_request_from_settings(void)
{
    printf("=== Test 12I: draw_request_from_settings ===\n");

    struct bsg_settings s;
    struct bsg_draw_request r;
    bsg_settings_init(&s);
    s.draw_mode = 4;
    s.line_width = 7;
    s.transparency = 0.4;
    s.color_override = 1;
    s.color[0] = 9;
    s.color[1] = 8;
    s.color[2] = 7;
    s.mixed_modes = 1;
    s.draw_non_subtract_only = 1;

    bsg_draw_request_from_settings(&r, &s);
    if (r.appearance.draw_mode != 4 || r.appearance.line_width != 7)
	FAIL("appearance mapping from settings");
    if (r.appearance.draw_non_subtract_only != 1)
	FAIL("appearance policy mapping from settings");
    if (r.material.use_override_color != 1)
	FAIL("material override flag mapping from settings");
    if (r.material.override_rgb[0] != 9 || r.material.override_rgb[1] != 8 || r.material.override_rgb[2] != 7)
	FAIL("material override color mapping from settings");
    if (r.material.transparency < 0.39 || r.material.transparency > 0.41)
	FAIL("material transparency mapping from settings");
    if (r.policy.mixed_modes != 1)
	FAIL("draw policy mapping from settings");

    PASS("draw_request_from_settings");
    return 0;
}

static int
test_node_draw_request_helpers(void)
{
    printf("=== Test 12J: node_draw_request_helpers ===\n");

    struct bview *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    if (!root || !shape)
	FAIL("create nodes");

    struct bsg_draw_request in;
    struct bsg_draw_request out;
    struct bsg_draw_policy policy;
    bsg_draw_request_init(&in);
    in.appearance.draw_mode = 2;
    in.appearance.line_width = 6;
    in.material.transparency = 0.5;
    in.material.rgba[3] = bsg_material_alpha_from_transparency(in.material.transparency);
    in.material.use_override_color = 1;
    in.material.override_rgb[0] = 1;
    in.material.override_rgb[1] = 2;
    in.material.override_rgb[2] = 3;
    in.policy.mixed_modes = 1;

    bsg_node_draw_request_set(shape, &in);
    if (!bsg_node_draw_request_get((const bsg_node *)shape, &out))
	FAIL("draw_request_get returned 0");
    if (out.appearance.draw_mode != 2 || out.appearance.line_width != 6)
	FAIL("draw request appearance round-trip");
    if (!out.material.use_override_color || out.material.override_rgb[0] != 1 || out.material.override_rgb[1] != 2 || out.material.override_rgb[2] != 3)
	FAIL("draw request material round-trip");
    if (out.material.transparency < 0.49 || out.material.transparency > 0.51)
	FAIL("draw request transparency round-trip");
    if (!bsg_node_draw_policy_get((const bsg_node *)shape, &policy))
	FAIL("draw_policy_get returned 0");
    if (policy.mixed_modes != 1)
	FAIL("draw policy round-trip");

    bsg_shape_destroy(shape);
    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("node_draw_request_helpers");
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
    failures += test_settings_storage();
    failures += test_get_from_s_os();
    failures += test_get_from_s_local_os();
    failures += test_settings_set();
    failures += test_settings_sync();
    failures += test_null_safety();
    failures += test_bv_settings_helpers();
    failures += test_draw_request_from_settings();
    failures += test_node_draw_request_helpers();

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
