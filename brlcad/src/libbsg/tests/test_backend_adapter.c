/*  T E S T _ B A C K E N D _ A D A P T E R . C
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
/** @file libbsg/tests/test_backend_adapter.c
 *
 * Phase D5 unit tests: bsg_backend_adapter — invalidation reason mask
 * coverage (BSG_INVALIDATE_* flags) and adapter capability flags.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "vmath.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/node.h"
#include "bsg/node_shape.h"
#include "bsg/render.h"
#include "bsg/render_item.h"
#include "bsg/backend_adapter.h"
#include "bsg/node_private.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

/* Accumulate the last reason_mask seen in the invalidate callback. */
static unsigned int g_last_reason = 0;
static int g_invalidate_count = 0;

static void
_invalidate_cb(void *UNUSED(dmp), const struct bsg_render_item *UNUSED(item),
	       unsigned int reason_mask)
{
    g_last_reason = reason_mask;
    g_invalidate_count++;
}

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
 * Test 1: BSG_INVALIDATE_* constants are distinct non-overlapping bits
 * ----------------------------------------------------------------------- */
static int
test_reason_bits_distinct(void)
{
    printf("=== Test 1: reason bits are distinct ===\n");

    unsigned int bits[] = {
	BSG_INVALIDATE_PAYLOAD,
	BSG_INVALIDATE_APPEARANCE,
	BSG_INVALIDATE_TRANSFORM,
	BSG_INVALIDATE_RENDER_MODE
    };
    int nbits = (int)(sizeof(bits) / sizeof(bits[0]));

    for (int i = 0; i < nbits; i++) {
	if (bits[i] == 0) FAIL("reason bit must not be 0");
	for (int j = i + 1; j < nbits; j++) {
	    if (bits[i] & bits[j])
		FAIL("reason bits must not overlap");
	}
    }

    /* BSG_INVALIDATE_ALL must be the OR of all individual bits */
    unsigned int all = (BSG_INVALIDATE_PAYLOAD | BSG_INVALIDATE_APPEARANCE |
			BSG_INVALIDATE_TRANSFORM | BSG_INVALIDATE_RENDER_MODE);
    if (BSG_INVALIDATE_ALL != all)
	FAIL("BSG_INVALIDATE_ALL must equal OR of all individual bits");

    PASS("reason bits are distinct and BSG_INVALIDATE_ALL is correct");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 2: invalidate callback receives the reason mask unchanged
 * ----------------------------------------------------------------------- */
static int
test_reason_passed_through(void)
{
    printf("=== Test 2: reason mask passed through callback ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *s = bsg_shape_create(v);
    s->s_flag = UP;

    struct bsg_render_item *item = bsg_render_item_create();
    item->node = s;
    item->view = v;

    struct bsg_backend_adapter adapter;
    memset(&adapter, 0, sizeof(adapter));
    adapter.invalidate = _invalidate_cb;

    /* Test each reason individually */
    unsigned int reasons[] = {
	BSG_INVALIDATE_PAYLOAD,
	BSG_INVALIDATE_APPEARANCE,
	BSG_INVALIDATE_TRANSFORM,
	BSG_INVALIDATE_RENDER_MODE,
	BSG_INVALIDATE_ALL,
	BSG_INVALIDATE_PAYLOAD | BSG_INVALIDATE_APPEARANCE
    };
    int nreasons = (int)(sizeof(reasons) / sizeof(reasons[0]));

    for (int i = 0; i < nreasons; i++) {
	g_last_reason = 0;
	adapter.invalidate(NULL, item, reasons[i]);
	if (g_last_reason != reasons[i])
	    FAIL("callback must receive exact reason_mask");
    }

    bsg_render_item_free(item);
    bsg_node_destroy(s);
    _free_view(v);
    PASS("reason mask passed through callback");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 3: adapter capability flags are distinct
 * ----------------------------------------------------------------------- */
static int
test_capability_bits_distinct(void)
{
    printf("=== Test 3: capability bits distinct ===\n");

    unsigned int caps[] = {
	BSG_ADAPTER_CAP_TRANSPARENCY,
	BSG_ADAPTER_CAP_WIREFRAME,
	BSG_ADAPTER_CAP_SHADED,
	BSG_ADAPTER_CAP_HUD,
	BSG_ADAPTER_CAP_SORTED_ALPHA,
	BSG_ADAPTER_CAP_BREP
    };
    int ncaps = (int)(sizeof(caps) / sizeof(caps[0]));

    for (int i = 0; i < ncaps; i++) {
	if (caps[i] == 0) FAIL("capability bit must not be 0");
	for (int j = i + 1; j < ncaps; j++) {
	    if (caps[i] & caps[j])
		FAIL("capability bits must not overlap");
	}
    }

    PASS("capability bits are distinct");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 4: NULL adapter invalidate callback is safe to skip
 * ----------------------------------------------------------------------- */
static int
test_null_invalidate_safe(void)
{
    printf("=== Test 4: NULL invalidate is safe ===\n");

    struct bsg_backend_adapter adapter;
    memset(&adapter, 0, sizeof(adapter));
    /* adapter.invalidate == NULL */

    /* Mimick what the executor does: check for NULL before calling */
    if (adapter.invalidate)
	adapter.invalidate(NULL, NULL, BSG_INVALIDATE_ALL);
    /* Reaching here means no crash */

    PASS("NULL invalidate is safe to skip");
    return 0;
}


/* -----------------------------------------------------------------------
 * Test 5: render request carries per-frame settings (render_settings smoke)
 * ----------------------------------------------------------------------- */
static int
test_request_settings_populated(void)
{
    printf("=== Test 5: bsg_render_request settings populated ===\n");

    struct bsg_view *v = _make_view();
    bsg_node *root = bsg_scene_root_create(v);

    struct bsg_render_request *req = bsg_render_request_create(v, root, NULL);
    if (!req) FAIL("bsg_render_request_create returned NULL");
    if (!req->settings) FAIL("req->settings must not be NULL after create");

    /* Defaults: transparency_policy = sorted, lod_policy = auto */
    if (req->settings->transparency_policy != BSG_TRANSPARENCY_SORTED)
	FAIL("default transparency_policy should be BSG_TRANSPARENCY_SORTED");
    if (req->settings->lod_policy != BSG_LOD_AUTO)
	FAIL("default lod_policy should be BSG_LOD_AUTO");
    if (!req->settings->hud_enabled)
	FAIL("default hud_enabled should be 1");

    bsg_render_request_destroy(req);
    bsg_node_destroy(root);
    _free_view(v);
    PASS("render request settings populated at creation");
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
    failures += test_reason_bits_distinct();
    failures += test_reason_passed_through();
    failures += test_capability_bits_distinct();
    failures += test_null_invalidate_safe();
    failures += test_request_settings_populated();

    if (failures) {
	printf("\n%d test(s) FAILED\n", failures);
	return 1;
    }
    printf("\nAll backend_adapter tests PASSED\n");
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
