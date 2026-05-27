/*                     R E N D E R . C
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
/** @file libbsg/render.c
 *
 * Phase D5: BSG render-request — pre-render traversal, render-item
 * production, phase-ordered dispatch via backend adapter or legacy
 * bsg_payload_dispatch fallback.
 *
 * Traversal overview
 * ------------------
 * _render_collect() walks the subtree recursively (not via bsg_visit)
 * so it can maintain a matrix stack for BSG_NODE_TRANSFORM nodes.  For
 * each BSG_NODE_SHAPE it:
 *   1. Checks visibility (BSG_RENDER_FLAG_VISIBLE_ONLY).
 *   2. Calls bsg_payload_dispatch for update-only types (CSG/MESH/BREP).
 *   3. Resolves appearance from s_os / s_color / s_iflag.
 *   4. Classifies the item into one of the four bsg_render_phase buckets.
 *   5. Allocates a bsg_render_item and appends it to the appropriate
 *      phase bucket (bu_ptbl).
 *
 * After collection, transparent items are sorted back-to-front when
 * BSG_RENDER_FLAG_SORTED_ALPHA is set.  The current sort key uses the
 * transformed node origin as an approximate depth proxy; payload-specific
 * depth sorting can refine this in a later slice.
 *
 * Phase-ordered dispatch
 * ----------------------
 * Items are dispatched in phase order: OPAQUE → TRANSPARENT → OVERLAY →
 * HUD.  Per item:
 *   - If req->adapter is set: adapter->prepare() + adapter->draw().
 *   - Otherwise: bsg_payload_dispatch (legacy fallback).
 * If BSG_RENDER_FLAG_COLLECT_ITEMS is set items are appended to req->items
 * instead of being dispatched (and are NOT freed — caller owns them).
 */

#include "common.h"

#include <limits.h>
#include <string.h>

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/sort.h"

#include "vmath.h"
#include "bn/mat.h"

#include "bsg/defines.h"
#include "bsg/visit.h"
#include "bsg/payload.h"
#include "bsg/render.h"
#include "bsg/render_item.h"
#include "bsg/backend_adapter.h"
#include "bsg/hud.h"
#include "bsg/appearance.h"


/* ------------------------------------------------------------------ */
/* Internal collection state                                            */
/* ------------------------------------------------------------------ */

struct collect_state {
    const struct bsg_render_request *req;
    /* Per-phase item buckets (indexed by bsg_render_phase) */
    struct bu_ptbl phase_items[BSG_RENDER_PHASE_COUNT];
};


/**
 * Resolve the render phase for a shape node.
 *
 * Priority (highest to lowest):
 *   1. BSG_PAYLOAD_OVERLAY → OVERLAY or HUD (HUD if node is a HUD child)
 *   2. BSG_RENDER_FLAG_HUD_PASS request → HUD
 *   3. transparency < 1.0 → TRANSPARENT
 *   4. default → OPAQUE
 */
static bsg_render_phase
_classify_phase(const struct bsg_render_request *req,
		const bsg_node *node,
		fastf_t transparency)
{
    unsigned long long ptype = bsg_node_get_payload_type((bsg_node *)node);

    if (ptype & BSG_PAYLOAD_OVERLAY) {
	/* HUD pass flag promotes overlays into the HUD phase */
	if (req->flags & BSG_RENDER_FLAG_HUD_PASS) {
	    const struct bsg_hud_node_meta *meta =
		bsg_hud_node_get_meta((bsg_node *)node);
	    if (meta)
		return BSG_RENDER_PHASE_HUD;
	}
	return BSG_RENDER_PHASE_OVERLAY;
    }

    if (transparency < 1.0)
	return BSG_RENDER_PHASE_TRANSPARENT;

    return BSG_RENDER_PHASE_OPAQUE;
}


/**
 * Resolve the sort_key for @p item.
 *
 * For HUD items: use bsg_hud_node_meta::sort_order.
 * For transparent items: use the negated view-space Z of the transformed
 * node origin so larger values sort farther items first (back-to-front).
 * For all others: 0.
 */
static int
_sort_key(const struct bsg_render_request *req,
	  const struct bsg_render_item *item)
{
    if (item->phase == BSG_RENDER_PHASE_HUD) {
	const struct bsg_hud_node_meta *meta =
	    bsg_hud_node_get_meta(item->node);
	if (meta)
	    return meta->sort_order;
    }

    if (item->phase == BSG_RENDER_PHASE_TRANSPARENT && req && req->view) {
	mat_t view_mat;
	point_t model_origin = VINIT_ZERO;
	point_t view_origin;
	fastf_t depth_key;

	bn_mat_mul(view_mat, req->view->gv_model2view, item->model_mat);
	MAT4X3PNT(view_origin, view_mat, model_origin);
	depth_key = -view_origin[Z] * 1000000.0;

	if (depth_key > (fastf_t)INT_MAX)
	    return INT_MAX;
	if (depth_key < (fastf_t)INT_MIN)
	    return INT_MIN;
	return (int)depth_key;
    }

    return 0;
}


static int
_transparent_item_cmp(const void *a, const void *b, void *UNUSED(context))
{
    const struct bsg_render_item *ia =
	*(const struct bsg_render_item * const *)a;
    const struct bsg_render_item *ib =
	*(const struct bsg_render_item * const *)b;

    if (ia->sort_key > ib->sort_key)
	return -1;
    if (ia->sort_key < ib->sort_key)
	return 1;
    return 0;
}


static void
_sort_transparent_bucket(struct bu_ptbl *bucket)
{
    if (!bucket || BU_PTBL_LEN(bucket) < 2)
	return;

    bu_sort((void *)BU_PTBL_BASEADDR(bucket),
	    BU_PTBL_LEN(bucket),
	    sizeof(long *),
	    _transparent_item_cmp,
	    NULL);
}


/**
 * Recursive traversal: collect render items from the subtree rooted at
 * @p node, accumulating the model matrix from ancestor transforms.
 */
static void
_render_collect(const bsg_node *node,
		const mat_t parent_mat,
		struct collect_state *st)
{
    if (!node)
	return;

    const struct bsg_render_request *req = st->req;

    /* --------------------------------------------------------------- */
    /* Transform node: push matrix and recurse into children            */
    /* --------------------------------------------------------------- */
    if (node->s_type_flags & BSG_NODE_TRANSFORM) {
	mat_t new_mat;
	bn_mat_mul(new_mat, parent_mat, ((const bsg_node *)node)->s_mat);
	for (size_t i = 0; i < BU_PTBL_LEN(&((bsg_node *)node)->children); i++) {
	    bsg_node *child =
		(bsg_node *)BU_PTBL_GET(&((bsg_node *)node)->children, i);
	    _render_collect(child, new_mat, st);
	}
	return;
    }

    /* --------------------------------------------------------------- */
    /* Non-shape (group/root/…): recurse with same matrix               */
    /* --------------------------------------------------------------- */
    if (!(node->s_type_flags & BSG_NODE_SHAPE)) {
	for (size_t i = 0; i < BU_PTBL_LEN(&((bsg_node *)node)->children); i++) {
	    bsg_node *child =
		(bsg_node *)BU_PTBL_GET(&((bsg_node *)node)->children, i);
	    _render_collect(child, parent_mat, st);
	}
	return;
    }

    /* --------------------------------------------------------------- */
    /* Shape node: visibility check, LoD update, item creation          */
    /* --------------------------------------------------------------- */

    /* Visibility filter */
    if ((req->flags & BSG_RENDER_FLAG_VISIBLE_ONLY) &&
	((const bsg_node *)node)->s_flag == DOWN) {
	/* shape is hidden — recurse into children only if any exist */
	for (size_t i = 0; i < BU_PTBL_LEN(&((bsg_node *)node)->children); i++) {
	    bsg_node *child =
		(bsg_node *)BU_PTBL_GET(&((bsg_node *)node)->children, i);
	    _render_collect(child, parent_mat, st);
	}
	return;
    }

    /* Pre-render update pass for geometry types that need it (CSG / MESH /
     * BREP) — bsg_payload_dispatch calls s_update_callback when set. */
    bsg_payload_dispatch(req->dmp, (bsg_node *)node, req->view);

    /* Resolve appearance from s_os (when set) or direct node fields */
    fastf_t transparency = 1.0;
    int     dmode        = 0;
    int     line_width   = 1;
    unsigned char color[3] = {255, 0, 0};

    const struct bsg_obj_settings *os =
	((const bsg_node *)node)->s_os;
    if (os) {
	transparency = os->transparency;
	dmode        = os->s_dmode;
	line_width   = os->s_line_width;
	if (os->color_override) {
	    color[0] = os->color[0];
	    color[1] = os->color[1];
	    color[2] = os->color[2];
	} else {
	    color[0] = ((const bsg_node *)node)->s_color[0];
	    color[1] = ((const bsg_node *)node)->s_color[1];
	    color[2] = ((const bsg_node *)node)->s_color[2];
	}
    } else {
	color[0] = ((const bsg_node *)node)->s_color[0];
	color[1] = ((const bsg_node *)node)->s_color[1];
	color[2] = ((const bsg_node *)node)->s_color[2];
    }

    bsg_render_phase phase =
	_classify_phase(req, node, transparency);

    /* Build the render item */
    struct bsg_render_item *item = bsg_render_item_create();
    item->node         = (bsg_node *)node;
    MAT_COPY(item->model_mat, parent_mat);
    item->color[0]     = color[0];
    item->color[1]     = color[1];
    item->color[2]     = color[2];
    item->transparency = transparency;
    item->dmode        = dmode;
    item->line_width   = line_width;
    item->line_style   = 0;  /* solid; extended in future slice */
    item->highlighted  = (((const bsg_node *)node)->s_iflag == UP) ? 1 : 0;
    item->payload_flags =
	bsg_node_get_payload_type((bsg_node *)node);
    item->phase        = phase;
    item->sort_key     = _sort_key(req, item);

    bu_ptbl_ins(&st->phase_items[(int)phase], (long *)item);

    /* Recurse into shape children if any */
    for (size_t i = 0; i < BU_PTBL_LEN(&((bsg_node *)node)->children); i++) {
	bsg_node *child =
	    (bsg_node *)BU_PTBL_GET(&((bsg_node *)node)->children, i);
	_render_collect(child, parent_mat, st);
    }
}


/* ------------------------------------------------------------------ */
/* Dispatch helpers                                                     */
/* ------------------------------------------------------------------ */

/**
 * Dispatch a single item: adapter callbacks, collect, or legacy fallback.
 * Returns 1 if the item was dispatched (or collected), 0 if skipped.
 */
static int
_dispatch_item(struct bsg_render_request *req,
	       struct bsg_render_item *item)
{
    if (req->flags & BSG_RENDER_FLAG_COLLECT_ITEMS) {
	if (req->items)
	    bu_ptbl_ins(req->items, (long *)item);
	return 1;
    }

    if (req->adapter) {
	if (req->adapter->prepare)
	    req->adapter->prepare(req->dmp, item);
	if (req->adapter->draw)
	    req->adapter->draw(req->dmp, item);
    } else {
	/* Legacy fallback: dispatch via bsg_payload_dispatch.
	 * The update-pass call was already made during collection; this
	 * second call is intentionally a no-op for update-only types. */
	bsg_payload_dispatch(req->dmp, item->node, req->view);
    }

    bsg_render_item_free(item);
    return 1;
}


/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

struct bsg_render_request *
bsg_render_request_create(struct bsg_view *view,
			  bsg_node        *root,
			  void            *dmp)
{
    struct bsg_render_request *req;
    BU_ALLOC(req, struct bsg_render_request);
    memset(req, 0, sizeof(struct bsg_render_request));
    req->view    = view;
    req->root    = root;
    req->dmp     = dmp;
    req->flags   = BSG_RENDER_FLAG_VISIBLE_ONLY | BSG_RENDER_FLAG_PAYLOAD_DISPATCH;
    req->adapter = NULL;
    req->items   = NULL;
    return req;
}


void
bsg_render_request_destroy(struct bsg_render_request *req)
{
    if (!req)
	return;
    bu_free(req, "bsg_render_request");
}


int
bsg_render_request_execute(struct bsg_render_request *req)
{
    if (!req)
	return -1;

    /* Identity matrix as the initial accumulated transform */
    mat_t identity;
    MAT_IDN(identity);

    /* Set up per-phase collection buckets */
    struct collect_state st;
    st.req = req;
    for (int p = 0; p < BSG_RENDER_PHASE_COUNT; p++)
	bu_ptbl_init(&st.phase_items[p], 8, "render phase items");

    /* Collect items from the subtree */
    _render_collect(req->root, identity, &st);

    if (req->flags & BSG_RENDER_FLAG_SORTED_ALPHA)
	_sort_transparent_bucket(&st.phase_items[BSG_RENDER_PHASE_TRANSPARENT]);

    /* Dispatch in phase order */
    int dispatched = 0;
    for (int p = 0; p < BSG_RENDER_PHASE_COUNT; p++) {
	struct bu_ptbl *bucket = &st.phase_items[p];
	for (size_t i = 0; i < BU_PTBL_LEN(bucket); i++) {
	    struct bsg_render_item *item =
		(struct bsg_render_item *)BU_PTBL_GET(bucket, i);
	    dispatched += _dispatch_item(req, item);
	    /* item is freed inside _dispatch_item unless COLLECT_ITEMS */
	}
	bu_ptbl_free(bucket);
    }

    return dispatched;
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
