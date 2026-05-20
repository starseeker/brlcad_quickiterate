/*            T E S T _ N O D E _ C O R E . C
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
/** @file libbsg/tests/test_node_core.c
 *
 * Phase 10 unit tests for the inline bsg_node_core storage.
 *
 * These tests verify:
 *  10A - struct bsg_node_core is present in bv_scene_obj.
 *  10B - kind and parent BSG accessors round-trip through bsg_core.
 *  10C - settings, material, appearance, and payload pointers are stored in bsg_core
 *        and are freed by bv_obj_reset() (via _bsg_core_release).
 *  10D - identity fields and revision counters are stored inline in bsg_core;
 *        identity_clear does NOT reset revisions (Phase 10D semantic).
 *  10E - bsg_node_core_get / bsg_node_core_init / bsg_node_core_initialized
 *        public API.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/defines.h"
#include "bsg/identity.h"
#include "bsg/material.h"
#include "bsg/appearance.h"
#include "bsg/node.h"
#include "bsg/node_core.h"
#include "bsg/node_group.h"
#include "bsg/payload.h"
#include "bsg/settings.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

/* ------------------------------------------------------------------ */
/* Minimal view / node helpers                                          */
/* ------------------------------------------------------------------ */

static struct bview *
make_view(const char *name)
{
    struct bview *v;
    BU_GET(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "%s", name ? name : "test");
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v)
	return;
    bv_free(v);
    BU_PUT(v, struct bview);
}


/* ------------------------------------------------------------------ */
/* Test 10A: struct bsg_node is first member of bv_scene_obj            */
/* ------------------------------------------------------------------ */

static int
test_core_embedded(void)
{
    printf("=== Test 10A: core_embedded ===\n");

    /* The embedded bsg node must be accessible via the raw struct field. */
    struct bv_scene_obj s;
    memset(&s, 0, sizeof(s));

    if (s.bsg.bsg_magic != 0)
	FAIL("fresh zero-init should have magic 0");

    s.bsg.bsg_magic = BSG_NODE_CORE_MAGIC;
    if (s.bsg.bsg_magic != BSG_NODE_CORE_MAGIC)
	FAIL("magic round-trip");

    /* Phase 10E: bsg is the FIRST member of bv_scene_obj (offset == 0). */
    if (offsetof(struct bv_scene_obj, bsg) != 0)
	FAIL("bsg must be at offset 0 (first member)");

    /* BSG_NODE_REV_MAX must accommodate all defined revision kinds. */
    if (BSG_NODE_REV_MAX < BSG_NODE_REV_COUNT)
	FAIL("BSG_NODE_REV_MAX < BSG_NODE_REV_COUNT");

    PASS("core_embedded");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 10B: kind and parent routing through bsg_core                   */
/* ------------------------------------------------------------------ */

static int
test_kind_parent_routing(void)
{
    printf("=== Test 10B: kind_parent_routing ===\n");

    struct bview *v = make_view("kind_parent");
    bsg_node *parent = bsg_group_create(v);
    bsg_node *child  = bsg_group_create(v);
    if (!parent || !child) {
	free_view(v);
	FAIL("create nodes");
    }

    /* Set kind -- should land in bsg_core after first BSG setter. */
    bsg_node_set_kind(child, BSG_NODE_SHAPE);
    if (!bsg_node_core_initialized(child))
	FAIL("core not initialized after set_kind");

    if (bsg_node_kind(child) != BSG_NODE_SHAPE)
	FAIL("kind round-trip via core");

    /* Add child: parent field should be synced to core. */
    bsg_node_add_child(parent, child);
    if (bsg_node_parent(child) != parent)
	FAIL("parent via core after add_child");

    bsg_node *core = bsg_node_core_get(child);
    if (!core)
	FAIL("bsg_node_core_get returned NULL");
    if (core->bsg_parent != parent)
	FAIL("core->bsg_parent direct field mismatch");

    bsg_node_remove_child(parent, child);
    if (bsg_node_parent(child) != NULL)
	FAIL("parent is NULL after remove_child");
    if (core->bsg_parent != NULL)
	FAIL("core->bsg_parent not cleared after remove_child");

    /* Payload bits must also be reflected in bsg_node_kind. */
    bsg_node_set_payload_type(child, BSG_PAYLOAD_VLIST);
    if (bsg_node_get_payload_type(child) != BSG_PAYLOAD_VLIST)
	FAIL("payload type round-trip via core");
    if (!(bsg_node_kind(child) & BSG_PAYLOAD_VLIST))
	FAIL("payload bits visible in bsg_node_kind");

    bv_obj_put((struct bv_scene_obj *)parent);
    bv_obj_put((struct bv_scene_obj *)child);
    free_view(v);
    PASS("kind_parent_routing");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 10C: settings, material, appearance, payload stored in bsg_core */
/* ------------------------------------------------------------------ */

static int
test_sidecars_in_core(void)
{
    printf("=== Test 10C: sidecars_in_core ===\n");

    struct bview *v = make_view("sidecars");
    bsg_node *n = bsg_group_create(v);
    if (!n) {
	free_view(v);
	FAIL("create node");
    }

    /* --- settings --- */
    struct bsg_settings s_in, s_out;
    bsg_settings_init(&s_in);
    s_in.line_width = 6;
    s_in.draw_mode = 2;
    bsg_node_settings_set(n, &s_in);

    bsg_node *core = bsg_node_core_get(n);
    if (!core || !core->settings_local || !core->settings_effective)
	FAIL("settings pointers not in core after settings_set");
    if (!bsg_node_settings_get(n, &s_out))
	FAIL("settings_get should return 1 for BSG-set settings");
    if (s_out.line_width != 6 || s_out.draw_mode != 2)
	FAIL("settings content round-trip via core");

    /* --- material --- */
    struct bsg_material m_in, m_out;
    bsg_material_init(&m_in);
    m_in.rgba[0] = 128;
    m_in.rgba[1] = 64;
    m_in.rgba[2] = 32;
    bsg_node_material_set(n, &m_in);

    /* Core should now have the material pointer. */
    core = bsg_node_core_get(n);
    if (!core || !core->material)
	FAIL("material pointer not in core after set");

    if (!bsg_node_material_get(n, &m_out))
	FAIL("material_get should return 1 for BSG-set material");
    if (m_out.rgba[0] != 128 || m_out.rgba[1] != 64 || m_out.rgba[2] != 32)
	FAIL("material content round-trip via core");

    /* --- appearance --- */
    struct bsg_appearance a_in, a_out;
    bsg_appearance_init(&a_in);
    a_in.line_width = 3;
    bsg_node_appearance_set(n, &a_in);

    if (!core->appearance)
	FAIL("appearance pointer not in core after set");
    if (!bsg_node_appearance_get(n, &a_out))
	FAIL("appearance_get should return 1 for BSG-set appearance");
    if (a_out.line_width != 3)
	FAIL("appearance content round-trip via core");

    /* --- payload --- */
    bsg_node_set_kind(n, BSG_NODE_SHAPE);
    struct bsg_payload *p = bsg_payload_create(BSG_PAYLOAD_TYPE_VLIST);
    if (!p) {
	bv_obj_put((struct bv_scene_obj *)n);
	free_view(v);
	FAIL("payload_create");
    }
    bsg_node_payload_set(n, p);

    if (!core->payload)
	FAIL("payload pointer not in core after set");
    if (bsg_node_payload_get(n) != p)
	FAIL("payload_get round-trip via core");

    /* bv_obj_put resets the node; the cleanup hook must free material,
     * appearance, and payload without a crash. */
    bv_obj_put((struct bv_scene_obj *)n);
    free_view(v);

    PASS("sidecars_in_core");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 10D: identity and revisions inline; identity_clear preserves    */
/*           revisions (Phase 10D semantic change)                      */
/* ------------------------------------------------------------------ */

static int
test_identity_revisions_inline(void)
{
    printf("=== Test 10D: identity_revisions_inline ===\n");

    struct bv_scene_obj raw;
    memset(&raw, 0, sizeof(raw));
    bsg_node *n = (bsg_node *)&raw;

    /* Core starts uninitialised. */
    if (bsg_node_core_initialized(n))
	FAIL("core should not be initialized on a fresh zeroed node");

    /* Setting identity initialises the core. */
    struct bsg_identity id;
    bsg_identity_from_path_str(&id, "/world/box", BSG_SOURCE_DB_OBJECT);
    bsg_node_identity_set(n, &id);

    if (!bsg_node_core_initialized(n))
	FAIL("core should be initialized after identity_set");

    struct bsg_identity got;
    if (!bsg_node_identity_get(n, &got))
	FAIL("identity_get should return 1 after set");
    if (got.node_id.value != id.node_id.value)
	FAIL("identity node_id round-trip via core");
    if (got.source_kind != BSG_SOURCE_DB_OBJECT)
	FAIL("identity source_kind round-trip via core");

    /* Bump revisions -- separate from identity. */
    bsg_node_bump_revision(n, BSG_NODE_REV_MATERIAL);
    bsg_node_bump_revision(n, BSG_NODE_REV_MATERIAL);
    bsg_node_bump_revision(n, BSG_NODE_REV_PAYLOAD);

    if (bsg_node_revision(n, BSG_NODE_REV_MATERIAL) != 2)
	FAIL("material revision should be 2");
    if (bsg_node_revision(n, BSG_NODE_REV_PAYLOAD) != 1)
	FAIL("payload revision should be 1");

    /* Phase 10D: clearing identity does NOT reset revisions. */
    bsg_node_identity_clear(n);
    if (bsg_node_identity_get(n, &got))
	FAIL("identity should be cleared");
    if (bsg_node_revision(n, BSG_NODE_REV_MATERIAL) != 2)
	FAIL("material revision preserved after identity_clear");
    if (bsg_node_revision(n, BSG_NODE_REV_PAYLOAD) != 1)
	FAIL("payload revision preserved after identity_clear");

    /* identity_clear is idempotent */
    bsg_node_identity_clear(n);
    if (bsg_node_revision(n, BSG_NODE_REV_MATERIAL) != 2)
	FAIL("material revision still 2 after double identity_clear");

    PASS("identity_revisions_inline");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 10E: public node_core API                                       */
/* ------------------------------------------------------------------ */

static int
test_node_core_api(void)
{
    printf("=== Test 10E: node_core_api ===\n");

    /* NULL safety */
    if (bsg_node_core_get(NULL) != NULL)
	FAIL("core_get(NULL) must return NULL");
    if (bsg_node_core_initialized(NULL) != 0)
	FAIL("core_initialized(NULL) must return 0");
    bsg_node_core_init(NULL); /* must not crash */

    /* Using a raw zero-initialised bv_scene_obj */
    struct bv_scene_obj raw;
    memset(&raw, 0, sizeof(raw));
    bsg_node *n = (bsg_node *)&raw;

    if (bsg_node_core_initialized(n))
	FAIL("should not be initialized yet");

    /* Explicit init */
    bsg_node_core_init(n);
    if (!bsg_node_core_initialized(n))
	FAIL("should be initialized after core_init");

    /* core_get returns the node itself */
    bsg_node *core = bsg_node_core_get(n);
    if (!core)
	FAIL("core_get returned NULL after init");
    if (core != n)
	FAIL("core_get must return the node itself (Phase 10E)");

    /* Double init is a no-op */
    core->bsg_kind = 0xABCDEFULL;
    bsg_node_core_init(n);
    if (core->bsg_kind != 0xABCDEFULL)
	FAIL("double init must not reset existing core fields");

    PASS("node_core_api");
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
    failures += test_core_embedded();
    failures += test_kind_parent_routing();
    failures += test_sidecars_in_core();
    failures += test_identity_revisions_inline();
    failures += test_node_core_api();

    if (failures)
	printf("FAIL: %d test group(s) failed\n", failures);
    else
	printf("PASS: all node_core tests passed\n");
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
