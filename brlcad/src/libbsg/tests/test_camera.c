/*              T E S T _ C A M E R A . C
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
/** @file libbsg/tests/test_camera.c
 *
 * Phase 7 unit tests: BSG camera snapshot and light data model.
 *
 * Test groups:
 *   Test 1: camera snapshot init/null safety
 *   Test 2: camera snapshot from bview (identity view)
 *   Test 3: camera snapshot from bview (perspective mode)
 *   Test 4: light init and bsg_light_set lifecycle
 *   Test 5: default light set content
 *   Test 6: scene-root light set registry and enable/disable
 */

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/camera.h"
#include "bsg/light.h"
#include "bsg/node_shape.h"
#include "bsg/util.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

#define BSG_NEAR_ZERO(v) (fabs((double)(v)) < 1e-10)

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static struct bview *
make_view(void)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_view_camera");
    v->gv_width  = 800;
    v->gv_height = 600;
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v)
	return;
    bv_free(v);
    bu_free(v, "test_view_camera");
}


/* ------------------------------------------------------------------ */
/* Test 1: camera snapshot init / null safety                          */
/* ------------------------------------------------------------------ */

static int
test_camera_init(void)
{
    struct bsg_camera_snapshot snap;
    int rc;

    printf("Test 1: camera snapshot init / null safety\n");

    /* null safety */
    bsg_camera_snapshot_init(NULL);      /* must not crash */
    rc = bsg_camera_snapshot_from_bview(NULL, NULL);
    if (rc != -1)
	FAIL("from_bview(NULL,NULL) should return -1");

    {
	struct bview *v = make_view();
	rc = bsg_camera_snapshot_from_bview(NULL, v);
	if (rc != -1)
	    FAIL("from_bview(NULL,v) should return -1");
	rc = bsg_camera_snapshot_from_bview(&snap, NULL);
	if (rc != -1)
	    FAIL("from_bview(snap,NULL) should return -1");
	free_view(v);
    }

    /* init produces sane defaults */
    bsg_camera_snapshot_init(&snap);
    if (snap.width  != 512) FAIL("init width != 512");
    if (snap.height != 512) FAIL("init height != 512");
    if (!BSG_NEAR_ZERO(snap.aspect - 1.0)) FAIL("init aspect != 1.0");
    if (snap.projection != BSG_CAMERA_ORTHO) FAIL("init projection != ORTHO");

    PASS("camera snapshot init / null safety");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 2: camera snapshot from bview — default (identity) view        */
/* ------------------------------------------------------------------ */

static int
test_camera_from_bview_identity(void)
{
    struct bview *v;
    struct bsg_camera_snapshot snap;
    int rc;

    printf("Test 2: camera snapshot from identity bview\n");

    v = make_view();
    /* bv_init sets up identity matrices and gv_perspective = 0 */
    rc = bsg_camera_snapshot_from_bview(&snap, v);
    if (rc != 0)
	FAIL("from_bview should succeed");

    /* Viewport */
    if (snap.width  != 800) FAIL("width mismatch");
    if (snap.height != 600) FAIL("height mismatch");
    if (!BSG_NEAR_ZERO(snap.aspect - (800.0 / 600.0)))
	FAIL("aspect mismatch");

    /* Orthographic for perspective == 0 */
    if (snap.projection != BSG_CAMERA_ORTHO)
	FAIL("should be ortho projection");
    if (!BSG_NEAR_ZERO(snap.perspective_angle))
	FAIL("perspective_angle should be 0 for ortho");

    /* Matrices should match bview's */
    {
	int i;
	for (i = 0; i < 16; i++) {
	    if (!BSG_NEAR_ZERO(snap.model2view[i] - v->gv_model2view[i]))
		FAIL("model2view mismatch");
	    if (!BSG_NEAR_ZERO(snap.view2model[i] - v->gv_view2model[i]))
		FAIL("view2model mismatch");
	}
    }

    /* Scale data */
    if (!BSG_NEAR_ZERO(snap.scale - v->gv_scale))
	FAIL("scale mismatch");

    free_view(v);
    PASS("camera snapshot from identity bview");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 3: camera snapshot from bview — perspective mode               */
/* ------------------------------------------------------------------ */

static int
test_camera_perspective(void)
{
    struct bview *v;
    struct bsg_camera_snapshot snap;
    int rc;

    printf("Test 3: camera snapshot perspective mode\n");

    v = make_view();
    v->gv_perspective = 45.0;
    rc = bsg_camera_snapshot_from_bview(&snap, v);
    if (rc != 0)
	FAIL("from_bview (perspective) should succeed");

    if (snap.projection != BSG_CAMERA_PERSPECTIVE)
	FAIL("should be perspective projection");
    if (!BSG_NEAR_ZERO(snap.perspective_angle - 45.0))
	FAIL("perspective_angle should be 45.0");

    free_view(v);
    PASS("camera snapshot perspective mode");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 4: light init and bsg_light_set lifecycle                      */
/* ------------------------------------------------------------------ */

static int
test_light_lifecycle(void)
{
    struct bsg_light   light;
    struct bsg_light_set *ls;

    printf("Test 4: light init and bsg_light_set lifecycle\n");

    /* null safety */
    bsg_light_init(NULL);   /* must not crash */

    bsg_light_init(&light);
    if (light.kind    != BSG_LIGHT_AMBIENT) FAIL("init kind != AMBIENT");
    if (light.enabled != 0)                 FAIL("init enabled != 0");
    if (!BSG_NEAR_ZERO(light.intensity - 1.0))  FAIL("init intensity != 1.0");

    ls = bsg_light_set_create();
    if (!ls)                      FAIL("create returned NULL");
    if (ls->count != 0)           FAIL("new set count != 0");
    if (ls->enabled != 0)         FAIL("new set enabled != 0");

    bsg_light_set_add(ls, &light);
    if (ls->count != 1)           FAIL("count after add != 1");

    bsg_light_set_clear(ls);
    if (ls->count != 0)           FAIL("count after clear != 0");

    bsg_light_set_destroy(NULL);  /* must not crash */
    bsg_light_set_destroy(ls);

    PASS("light init and bsg_light_set lifecycle");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 5: default light set content                                   */
/* ------------------------------------------------------------------ */

static int
test_default_light_set(void)
{
    struct bsg_light_set *ls;
    const struct bsg_light *l;

    printf("Test 5: default light set content\n");

    ls = bsg_light_set_create();
    if (!ls) FAIL("create returned NULL");

    bsg_light_set_create_default(ls);
    if (ls->enabled == 0)         FAIL("default set should be enabled");
    if (ls->count < 2)            FAIL("default set should have >= 2 lights");

    /* First light: ambient */
    l = bsg_light_set_get(ls, 0);
    if (!l)                       FAIL("get(0) returned NULL");
    if (l->kind != BSG_LIGHT_AMBIENT) FAIL("light[0] not ambient");
    if (!l->enabled)              FAIL("light[0] not enabled");

    /* Second light: directional */
    l = bsg_light_set_get(ls, 1);
    if (!l)                       FAIL("get(1) returned NULL");
    if (l->kind != BSG_LIGHT_DIRECTIONAL) FAIL("light[1] not directional");
    if (!l->enabled)              FAIL("light[1] not enabled");
    if (!l->view_scoped)          FAIL("key light should be view-scoped");

    /* Out-of-range access */
    l = bsg_light_set_get(ls, 999);
    if (l)                        FAIL("out-of-range get should return NULL");

    bsg_light_set_destroy(ls);
    PASS("default light set content");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 6: scene-root light set registry and enable/disable            */
/* ------------------------------------------------------------------ */

static int
test_scene_light_registry(void)
{
    struct bview *v;
    bsg_node *root;
    struct bsg_light_set *ls;

    printf("Test 6: scene-root light set registry and enable/disable\n");

    v = make_view();
    root = bsg_scene_root_create(v);
    if (!root) FAIL("scene root creation failed");

    /* No light set present initially (create=0) */
    ls = bsg_scene_light_set_get(root, 0);
    if (ls)                       FAIL("no light set expected before creation");

    /* Create on demand */
    ls = bsg_scene_light_set_get(root, 1);
    if (!ls)                      FAIL("light set creation failed");
    if (!ls->enabled)             FAIL("auto-created set should be enabled");
    if (ls->count < 2)            FAIL("auto-created set should have default lights");

    /* Same pointer returned on subsequent call */
    {
	struct bsg_light_set *ls2 = bsg_scene_light_set_get(root, 0);
	if (ls2 != ls)
	    FAIL("second get should return same pointer");
    }

    /* is_enabled */
    if (!bsg_scene_light_is_enabled(root))
	FAIL("should be enabled");

    /* Disable */
    bsg_scene_light_enable(root, 0);
    if (bsg_scene_light_is_enabled(root))
	FAIL("should be disabled after disable call");

    /* Re-enable */
    bsg_scene_light_enable(root, 1);
    if (!bsg_scene_light_is_enabled(root))
	FAIL("should be enabled after re-enable");

    /* Null safety */
    bsg_scene_light_enable(NULL, 1);           /* must not crash */
    if (bsg_scene_light_is_enabled(NULL) != 0)
	FAIL("is_enabled(NULL) should return 0");
    if (bsg_scene_light_set_get(NULL, 1))
	FAIL("get(NULL,...) should return NULL");

    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("scene-root light set registry and enable/disable");
    return 0;
}


/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    int failures = 0;

    bu_setprogname(argv[0]);
    (void)argc;

    printf("=== BSG Phase 7: camera and light tests ===\n");

    failures += test_camera_init();
    failures += test_camera_from_bview_identity();
    failures += test_camera_perspective();
    failures += test_light_lifecycle();
    failures += test_default_light_set();
    failures += test_scene_light_registry();

    if (failures == 0)
	printf("ALL TESTS PASSED\n");
    else
	printf("%d TEST(S) FAILED\n", failures);

    return failures ? 1 : 0;
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
