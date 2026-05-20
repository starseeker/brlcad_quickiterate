/*                          L O D . C P P
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
/** @file libbsg/lod.cpp
 *
 * Phase L1 (drawing_stack_modernization):
 * Level-of-detail update helpers for the BSG scene graph.
 *
 * bsg_lod_update() does a depth-first walk of the BSG tree and, at each
 * BSG_NODE_LOD node, calls the node's ops->is_stale / select_level /
 * activate_level cycle.  The traversal handles nested LoD nodes and skips
 * any node that has no ops installed.
 *
 * bsg_lod_stale() is a thin accessor: it looks up the per-view cursor and
 * returns whether the cached metrics still match current view state.
 */

#include "common.h"

#include "bu/ptbl.h"
#include "bv/defines.h"

#include "bsg/defines.h"
#include "bsg/lod.h"
#include "bsg/lod_ops.h"
#include "bsg/node.h"


/* ------------------------------------------------------------------ */
/* Internal recursive walker                                           */
/* ------------------------------------------------------------------ */

static void
_lod_update_recursive(bsg_node *node, struct bview *v)
{
    if (!node || !v)
	return;

    struct bv_scene_obj *n = (struct bv_scene_obj *)node;

    if (n->bsg.bsg_kind & BSG_NODE_LOD) {
	struct bsg_lod_payload *pl =
	    (struct bsg_lod_payload *)bsg_node_user_data_get((const bsg_node *)n);
	if (pl && pl->ops) {
	    /* Ensure a cursor exists for this view. */
	    bsg_lod_node_get_cursor(node, v);

	    if (pl->ops->is_stale(node, v)) {
		int lvl = pl->ops->select_level(node, v);
		pl->ops->activate_level(node, v, lvl);
	    }
	}
	/* Recurse into the LoD node's children (level representations
	 * may themselves contain further LoD nodes in nested cases). */
    }

    /* Walk children regardless of node type so we find nested LoD nodes. */
    for (size_t i = 0; i < BU_PTBL_LEN(&n->bsg.bsg_children); i++) {
	struct bv_scene_obj *child =
	    (struct bv_scene_obj *)BU_PTBL_GET(&n->bsg.bsg_children, i);
	if (!child)
	    continue;
	/* Skip leaf nodes — they cannot contain BSG_NODE_LOD children. */
	if (child->bsg.bsg_kind & BSG_NODE_SHAPE)
	    continue;
	_lod_update_recursive((bsg_node *)child, v);
    }
}


/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void
bsg_lod_update(bsg_node *root, struct bview *v)
{
    if (!root || !v)
	return;

    /* Walk the entire subtree looking for BSG_NODE_LOD nodes. */
    _lod_update_recursive(root, v);
}


int
bsg_lod_stale(bsg_node *n, struct bview *v)
{
    if (!n || !v)
	return 0;

    /* Only BSG_NODE_LOD nodes carry staleness state. */
    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    if (!(s->bsg.bsg_kind & BSG_NODE_LOD))
	return 0;

    struct bsg_lod_payload *pl = (struct bsg_lod_payload *)bsg_node_user_data_get((const bsg_node *)s);
    if (!pl || !pl->ops || !pl->ops->is_stale)
	return 0;

    return pl->ops->is_stale(n, v);
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
