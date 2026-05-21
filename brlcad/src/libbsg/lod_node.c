/*                    L O D _ N O D E . C
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
/** @file libbsg/lod_node.c
 *
 * Phase L0 (drawing_stack_modernization):
 * BSG_NODE_LOD lifecycle — create / set_ops / attach_level / cursor.
 *
 * A LoD node sits between a path-group node and its level representation
 * children.  Its s_i_data points to a bsg_lod_payload containing the
 * policy vtable (bsg_lod_ops), opaque user_data, and a small per-view
 * cursor array.
 *
 * No level-selection policy is implemented here; policy is supplied by
 * libbv (mesh pop-buffer) and libged (CSG adaptive wireframe) via
 * bsg_lod_node_set_ops().
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bsg/defines.h"
#include "bsg/draw_set.h"
#include "bsg/lod_ops.h"
#include "bsg/node.h"
#include "bsg/node_group.h"


/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * Return the bsg_lod_payload stored in node->s_i_data, or NULL when
 * node is NULL, not a BSG_NODE_LOD node, or has no payload.
 */
static struct bsg_lod_payload *
_lod_payload(bsg_node *node)
{
    if (!bsg_node_has_kind(node, BSG_NODE_LOD))
	return NULL;
    return (struct bsg_lod_payload *)bsg_node_user_data_get(node);
}


/*
 * s_free_callback installed on every BSG_NODE_LOD node.
 * Frees the bsg_lod_payload and calls ops->free() if present.
 */
static void
_lod_node_free_cb(bsg_node *s)
{
    if (!s)
	return;
    struct bsg_lod_payload *pl = (struct bsg_lod_payload *)bsg_node_user_data_get((const bsg_node *)s);
    if (!pl)
	return;

    /* Let the policy release its state first. */
    if (pl->ops && pl->ops->free)
	pl->ops->free(s);

    bu_free(pl->cursors, "bsg_lod_payload cursors");
    bu_free(pl, "bsg_lod_payload");
    bsg_node_user_data_set(s, NULL);
}


/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

bsg_node *
bsg_lod_node_create(struct bview *v)
{
    struct bv_scene_obj *n = (struct bv_scene_obj *)bsg_node_create(v, BSG_NODE_LOD);
    if (!n)
	return NULL;

    /* Allocate the payload. */
    struct bsg_lod_payload *pl;
    BU_GET(pl, struct bsg_lod_payload);
    memset(pl, 0, sizeof(struct bsg_lod_payload));

    /* Pre-allocate 4 cursor slots (enough for Quad view). */
    pl->cursor_alloc = 4;
    pl->cursors = (struct bsg_lod_view_cursor *)bu_malloc(
	pl->cursor_alloc * sizeof(struct bsg_lod_view_cursor),
	"bsg_lod_payload cursors");
    memset(pl->cursors, 0,
	   pl->cursor_alloc * sizeof(struct bsg_lod_view_cursor));
    pl->cursor_count = 0;

    bsg_node_user_data_set((bsg_node *)n, pl);
    bsg_node_set_free_callback((bsg_node *)n, (bsg_node_free_fn)_lod_node_free_cb);

    return (bsg_node *)n;
}


void
bsg_lod_node_set_ops(bsg_node *node,
		     struct bsg_lod_ops *ops,
		     void *user_data)
{
    struct bsg_lod_payload *pl = _lod_payload(node);
    if (!pl)
	return;
    pl->ops       = ops;
    pl->user_data = user_data;
}


void
bsg_lod_node_attach_level(bsg_node *lod_node, bsg_node *level_node)
{
    if (!lod_node || !level_node)
	return;
    if (!bsg_node_has_kind(lod_node, BSG_NODE_LOD))
	return;

    bsg_node_add_child(lod_node, level_node);
}


struct bsg_lod_view_cursor *
bsg_lod_node_get_cursor(bsg_node *node, struct bview *v)
{
    struct bsg_lod_payload *pl = _lod_payload(node);
    if (!pl || !v)
	return NULL;

    /* Linear scan — in practice cursor_count <= 4. */
    for (size_t i = 0; i < pl->cursor_count; i++) {
	if (pl->cursors[i].v == v)
	    return &pl->cursors[i];
    }

    /* Not found — create a new slot. */
    if (pl->cursor_count >= pl->cursor_alloc) {
	size_t new_alloc = pl->cursor_alloc * 2;
	pl->cursors = (struct bsg_lod_view_cursor *)bu_realloc(
	    pl->cursors,
	    new_alloc * sizeof(struct bsg_lod_view_cursor),
	    "bsg_lod_payload cursors grow");
	/* Zero new slots. */
	size_t added = new_alloc - pl->cursor_alloc;
	memset(&pl->cursors[pl->cursor_alloc], 0,
	       added * sizeof(struct bsg_lod_view_cursor));
	pl->cursor_alloc = new_alloc;
    }

    struct bsg_lod_view_cursor *c = &pl->cursors[pl->cursor_count++];
    memset(c, 0, sizeof(struct bsg_lod_view_cursor));
    c->v     = v;
    c->level = -1;  /* not yet selected */
    return c;
}


int
bsg_lod_node_active_level(bsg_node *node, struct bview *v)
{
    struct bsg_lod_payload *pl = _lod_payload(node);
    if (!pl || !v)
	return -1;
    for (size_t i = 0; i < pl->cursor_count; i++) {
	if (pl->cursors[i].v == v)
	    return pl->cursors[i].level;
    }
    return -1;
}


int
bsg_lod_node_level_count(bsg_node *node)
{
    if (!bsg_node_has_kind(node, BSG_NODE_LOD))
	return 0;
    return (int)bsg_node_child_count(node);
}


bsg_node *
bsg_lod_node_insert_above(bsg_node *leaf, struct bview *v)
{
    if (!leaf || !v)
	return NULL;

    bsg_node *sleaf = leaf;
    bsg_node *parent = sleaf->bsg_parent;
    if (!parent)
	return NULL;

    intmax_t loc = bu_ptbl_locate(&parent->bsg_children, (const long *)sleaf);
    if (loc < 0)
	return NULL;

    bsg_node *lod = bsg_lod_node_create(v);
    if (!lod)
	return NULL;

    bsg_node *slod = lod;
    slod->bsg_parent = parent;
    BU_PTBL_SET(&parent->bsg_children, (size_t)loc, slod);

    sleaf->bsg_parent = slod;
    bsg_lod_node_attach_level(lod, leaf);

    bsg_bump_rev_node(parent);
    bsg_node_bbox_invalidate(parent);

    return lod;
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
