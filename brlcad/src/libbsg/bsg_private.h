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

#include "bv/defines.h"
#include "bsg/defines.h"
#include "bsg/draw_ctx.h"

/*
 * Walk node @p n up to the draw root and return the bsg_draw_ctx stored
 * in root->s_i_data.  Returns NULL if the root has no context.
 */
static inline struct bsg_draw_ctx *
_ctx_of_node(struct bv_scene_obj *n)
{
    if (!n)
	return NULL;
    while (n->parent)
	n = (struct bv_scene_obj *)n->parent;
    return (struct bsg_draw_ctx *)n->s_i_data;
}

/* ------------------------------------------------------------------ */
/* Phase 10: BSG node-core helpers                                      */
/* ------------------------------------------------------------------ */

/**
 * Cleanup function stored in bsg_core.bsg_core_free_fn.
 * Defined in node_core.c; frees material, appearance, and payload data
 * allocated by libbsg, then zeros the pointers.
 * Called by bv_obj_reset() (via the function pointer) before the core
 * is zeroed.
 */
extern void _bsg_core_release(struct bsg_node_core *core);

/**
 * Ensure the BSG node core for @p n has been initialized.
 *
 * If the core magic is already BSG_NODE_CORE_MAGIC this is a very cheap
 * check (one comparison).  On first use it zeroes the core, copies the
 * existing s_type_flags and parent pointer into core.kind / core.parent,
 * and installs _bsg_core_release as the cleanup callback so that
 * bv_obj_reset() will free any material/appearance/payload allocated later.
 *
 * Returns the initialized core, or NULL if @p n is NULL.
 */
static inline struct bsg_node_core *
_bsg_core_ensure(bsg_node *n)
{
    struct bv_scene_obj *s;

    if (!n)
	return NULL;

    s = (struct bv_scene_obj *)n;
    if (s->bsg_core.bsg_magic == BSG_NODE_CORE_MAGIC)
	return &s->bsg_core;

    memset(&s->bsg_core, 0, sizeof(s->bsg_core));
    s->bsg_core.bsg_magic        = BSG_NODE_CORE_MAGIC;
    s->bsg_core.kind             = s->s_type_flags;
    s->bsg_core.parent           = s->parent;
    s->bsg_core.bsg_core_free_fn = _bsg_core_release;
    return &s->bsg_core;
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
