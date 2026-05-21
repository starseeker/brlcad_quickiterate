/*                    B S G _ P R I V A T E . H
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
/** @file libbsg/bsg_private.h
 *
 * Internal libbsg helpers shared between multiple translation units.
 * This header is NOT installed; it is for libbsg source use only.
 */

#ifndef LIBBSG_BSG_PRIVATE_H
#define LIBBSG_BSG_PRIVATE_H

#include <string.h>

#include "bu/malloc.h"
#include "bv/defines.h"
#include "bsg/defines.h"
#include "bsg/draw_ctx.h"

/*
 * Walk node @p n up to the draw root and return the bsg_draw_ctx stored
 * in root->s_i_data.  Returns NULL if the root has no context.
 */
static inline struct bsg_draw_ctx *
_ctx_of_node(bsg_node *n)
{
    if (!n)
	return NULL;
    while (n->bsg_parent)
	n = n->bsg_parent;
    return (struct bsg_draw_ctx *)((struct bv_scene_obj *)n)->s_i_data;
}

/* ------------------------------------------------------------------ */
/* Phase 10E: BSG node helpers                                          */
/* ------------------------------------------------------------------ */

/**
 * Cleanup function stored in bsg_node.bsg_core_free_fn.
 * Defined in node_core.c; frees material, appearance, and payload data
 * allocated by libbsg, then zeros the pointers.
 * Called by bsg_node_destroy() (via the function pointer) before storage
 * release.
 */
extern void _bsg_core_release(struct bsg_node *core);

/**
 * Ensure the BSG node for @p n has been initialized.
 *
 * If the node magic is already BSG_NODE_CORE_MAGIC this is a very cheap
 * check (one comparison).  On first use it zeroes the identity/revision
 * fields and installs _bsg_core_release as the cleanup callback so
 * bsg_node_destroy() will free any material/appearance/payload allocated
 * later.
 *
 * Returns the initialized node, or NULL if @p n is NULL.
 */
static inline bsg_node *
_bsg_core_ensure(bsg_node *n)
{
    if (!n)
	return NULL;
    if (n->bsg_magic == BSG_NODE_CORE_MAGIC)
	return n;
    n->have_identity = 0;
    n->identity_node_id = 0;
    n->identity_part_id = 0;
    n->identity_instance_id = 0;
    n->identity_source_kind = 0;
    memset(n->revisions, 0, sizeof(n->revisions));
    n->material = NULL;
    n->appearance = NULL;
    n->payload = NULL;
    n->bsg_core_free_fn = _bsg_core_release;
    n->bsg_magic = BSG_NODE_CORE_MAGIC;
    return n;
}

static inline const struct bsg_settings *
_bsg_settings_local(const bsg_node *n)
{
    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    if (!s)
	return NULL;
    if (n->settings_local)
	return n->settings_local;
    return &s->s_local_os;
}

static inline const struct bsg_settings *
_bsg_settings_effective(const bsg_node *n)
{
    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    if (!s)
	return NULL;
    if (n->settings_effective)
	return n->settings_effective;
    return (s->s_os) ? s->s_os : &s->s_local_os;
}

static inline struct bsg_settings *
_bsg_settings_local_get_or_create(bsg_node *n)
{
    bsg_node *core = _bsg_core_ensure(n);
    if (!core)
	return NULL;
    if (!core->settings_local) {
	struct bsg_settings defaults = BSG_SETTINGS_INIT;
	BU_ALLOC(core->settings_local, struct bsg_settings);
	*(core->settings_local) = defaults;
    }
    return core->settings_local;
}

static inline struct bsg_settings *
_bsg_settings_effective_get_or_create(bsg_node *n)
{
    bsg_node *core = _bsg_core_ensure(n);
    if (!core)
	return NULL;
    if (!core->settings_effective) {
	struct bsg_settings defaults = BSG_SETTINGS_INIT;
	BU_ALLOC(core->settings_effective, struct bsg_settings);
	*(core->settings_effective) = defaults;
    }
    return core->settings_effective;
}

static inline void
_bsg_settings_legacy_sync(bsg_node *n)
{
    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    const struct bsg_settings *local = _bsg_settings_local(n);
    const struct bsg_settings *effective = _bsg_settings_effective(n);

    if (!s || !local || !effective)
	return;

    s->s_local_os = *local;
    s->s_os = &s->s_local_os;
}

#endif /* LIBBSG_BSG_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
