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
#include "bu/str.h"

#include "vmath.h"
#include "bn/mat.h"

#include "bsg/defines.h"
#include "bsg/visit.h"
#include "bsg/payload.h"
#include "bsg/render.h"
#include "bsg/render_item.h"
#include "bsg/render_settings.h"
#include "bsg/backend_adapter.h"
#include "bsg/hud.h"
#include "bsg/overlay.h"
#include "bsg/appearance.h"
#include "bsg/appearance_action.h"
#include "bsg/lod.h"
#include "bsg/lod_ops.h"
#include "bsg/util.h"
#include "bsg/node_private.h"


/* ------------------------------------------------------------------ */
/* Internal collection state                                            */
/* ------------------------------------------------------------------ */

struct collect_state {
    const struct bsg_render_request *req;
    /* Per-phase item buckets (indexed by bsg_render_phase) */
    struct bu_ptbl phase_items[BSG_RENDER_PHASE_COUNT];
};

/* Preserve six decimal places of view-space depth when projecting the
 * floating-point Z value into the integer sort_key field. */
static const fastf_t depth_key_scale_factor = 1000000.0;

/* In independent-view mode, only overlays/view-scope subtrees are rendered
 * from the shared root.  Legacy root-level non-overlay children are skipped. */
static int
_independent_root_skip_child(const bsg_node *node)
{
    if (!node)
	return 1;
    if (node->s_type_flags & BSG_NODE_VIEW_SCOPE)
	return 0;
    if (!BU_VLS_IS_INITIALIZED(&node->s_name))
	return 1;
    return BU_STR_EQUAL("_overlays", bu_vls_cstr(&node->s_name)) ? 0 : 1;
}


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

    if (item->phase == BSG_RENDER_PHASE_OVERLAY) {
	const struct bsg_overlay_info *info =
	    bsg_overlay_info_get(item->node);
	if (info)
	    return ((int)info->ordering * 1000) + info->sort_order;
    }

    if (item->phase == BSG_RENDER_PHASE_TRANSPARENT && req && req->view) {
	mat_t view_mat;
	point_t model_origin = VINIT_ZERO;
	point_t view_origin;
	fastf_t depth_key;

	bn_mat_mul(view_mat, req->view->gv_model2view, item->model_mat);
	MAT4X3PNT(view_origin, view_mat, model_origin);
	/* In BRL-CAD view space, geometry in front of the camera has negative Z.
	 * Negating Z makes farther items larger so descending sort order yields
	 * a back-to-front transparent draw sequence. */
	depth_key = -view_origin[Z] * depth_key_scale_factor;

	if (depth_key >= (fastf_t)INT_MAX)
	    return INT_MAX;
	if (depth_key <= (fastf_t)INT_MIN)
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

    bu_sort(BU_PTBL_BASEADDR(bucket),
	    BU_PTBL_LEN(bucket),
	    sizeof(void *),
	    _transparent_item_cmp,
	    NULL);
}


static int
_hud_item_cmp(const void *a, const void *b, void *UNUSED(context))
{
    const struct bsg_render_item *ia =
	*(const struct bsg_render_item * const *)a;
    const struct bsg_render_item *ib =
	*(const struct bsg_render_item * const *)b;

    if (ia->sort_key < ib->sort_key)
	return -1;
    if (ia->sort_key > ib->sort_key)
	return 1;
    return 0;
}


static void
_sort_hud_bucket(struct bu_ptbl *bucket)
{
    if (!bucket || BU_PTBL_LEN(bucket) < 2)
	return;

    bu_sort(BU_PTBL_BASEADDR(bucket),
	    BU_PTBL_LEN(bucket),
	    sizeof(void *),
	    _hud_item_cmp,
	    NULL);
}


static void
_sort_overlay_bucket(struct bu_ptbl *bucket)
{
    if (!bucket || BU_PTBL_LEN(bucket) < 2)
	return;

    bu_sort(BU_PTBL_BASEADDR(bucket),
	    BU_PTBL_LEN(bucket),
	    sizeof(void *),
	    _hud_item_cmp,
	    NULL);
}


/**
 * Recursive traversal: collect render items from the subtree rooted at
 * @p node, accumulating the model matrix from ancestor transforms.
 */
static void
_render_collect(const bsg_node *node,
		const mat_t parent_mat,
		struct collect_state *st,
		int inherited_force_draw)
{
    if (!node)
	return;

    const struct bsg_render_request *req = st->req;
    /* force_draw propagates from ancestors and bypasses s_flag visibility
     * filtering for descendant shapes. */
    int force_draw = inherited_force_draw || node->s_force_draw;

    /* --------------------------------------------------------------- */
    /* Transform node: push matrix and recurse into children            */
    /* --------------------------------------------------------------- */
    if (node->s_type_flags & BSG_NODE_TRANSFORM) {
	mat_t new_mat;
	bn_mat_mul(new_mat, parent_mat, ((const bsg_node *)node)->s_mat);
	for (size_t i = 0; i < BU_PTBL_LEN(&((bsg_node *)node)->children); i++) {
	    bsg_node *child =
		(bsg_node *)BU_PTBL_GET(&((bsg_node *)node)->children, i);
	    _render_collect(child, new_mat, st, force_draw);
	}
	return;
    }

    /* Sensor nodes are not drawable */
    if (node->s_type_flags & BSG_NODE_SENSOR)
	return;

    /* Skip stale bridge placeholders from pre-V4 trees. */
    if (node->s_type_flags & BSG_NODE_VIEW_BRIDGE)
	return;

    /* View-scope visibility filtering.  Shared (s_v == NULL) is visible in all
     * views; owned scopes are visible only to the owning view. */
    if (node->s_type_flags & BSG_NODE_VIEW_SCOPE) {
	if (node->s_v != NULL && req->view != NULL && node->s_v != req->view)
	    return;
	for (size_t i = 0; i < BU_PTBL_LEN(&((bsg_node *)node)->children); i++) {
	    bsg_node *child =
		(bsg_node *)BU_PTBL_GET(&((bsg_node *)node)->children, i);
	    _render_collect(child, parent_mat, st, force_draw);
	}
	return;
    }

    /* LoD traversal: recurse only into the active level child. */
    if (node->s_type_flags & BSG_NODE_LOD) {
	int active = bsg_lod_node_active_level((bsg_node *)node, req->view);
	int nlevels = bsg_lod_node_level_count((bsg_node *)node);
	if (nlevels > 0) {
	    if (active < 0 || active >= nlevels)
		active = 0;
	    bsg_node *child = bsg_node_child_at(node, (size_t)active);
	    _render_collect(child, parent_mat, st, force_draw);
	}
	return;
    }

    /* --------------------------------------------------------------- */
    /* Non-shape (group/root/…): recurse with same matrix               */
    /* --------------------------------------------------------------- */
    if (!(node->s_type_flags & BSG_NODE_SHAPE) &&
	!(node->s_type_flags & BSG_OBJ_DB)) {
	int independent_root = 0;
	if (req->view && req->view->bsg_root &&
	    bsg_view_is_independent(req->view) &&
	    node == (const bsg_node *)req->view->bsg_root) {
	    independent_root = 1;
	}

	for (size_t i = 0; i < BU_PTBL_LEN(&((bsg_node *)node)->children); i++) {
	    bsg_node *child =
		(bsg_node *)BU_PTBL_GET(&((bsg_node *)node)->children, i);
	    if (independent_root && _independent_root_skip_child(child))
		continue;
	    _render_collect(child, parent_mat, st, force_draw);
	}
	return;
    }

    /* --------------------------------------------------------------- */
    /* Shape node: visibility check, LoD update, item creation          */
    /* --------------------------------------------------------------- */

    /* Visibility filter */
    if ((req->flags & BSG_RENDER_FLAG_VISIBLE_ONLY) &&
	((const bsg_node *)node)->s_flag == DOWN && !force_draw) {
	/* shape is hidden — recurse into children only if any exist */
	for (size_t i = 0; i < BU_PTBL_LEN(&((bsg_node *)node)->children); i++) {
	    bsg_node *child =
		(bsg_node *)BU_PTBL_GET(&((bsg_node *)node)->children, i);
	    _render_collect(child, parent_mat, st, force_draw);
	}
	return;
    }

    /* Pre-render update pass for geometry types that need it (CSG / MESH /
     * BREP) — bsg_payload_dispatch calls s_update_callback when set. */
    if (req->flags & BSG_RENDER_FLAG_PAYLOAD_DISPATCH)
	bsg_payload_dispatch(req->dmp, (bsg_node *)node, req->view);

    /* Phase D5: resolve appearance via bsg_appearance_resolve so that
     * backends read from item->appearance and never re-derive from node. */
    struct bsg_resolved_appearance ra;
    memset(&ra, 0, sizeof(ra));
    /* Pass NULL for inherited_os; future group-propagation work will
     * supply a non-NULL inherited_os from the group traversal stack. */
    bsg_appearance_resolve(req->view, node, NULL, &ra);

    bsg_render_phase phase =
	_classify_phase(req, node, ra.transparency);

    /* Build the render item */
    struct bsg_render_item *item = bsg_render_item_create();
    item->node          = (bsg_node *)node;
    item->view          = req->view;
    MAT_COPY(item->model_mat, parent_mat);
    item->appearance    = ra;
    item->payload_flags =
	bsg_node_get_payload_type((bsg_node *)node);
    item->phase         = phase;
    item->sort_key      = _sort_key(req, item);

    bu_ptbl_ins(&st->phase_items[(int)phase], (long *)item);

    /* Recurse into shape children if any */
    for (size_t i = 0; i < BU_PTBL_LEN(&((bsg_node *)node)->children); i++) {
	bsg_node *child =
	    (bsg_node *)BU_PTBL_GET(&((bsg_node *)node)->children, i);
	_render_collect(child, parent_mat, st, force_draw);
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
	if (req->flags & BSG_RENDER_FLAG_PAYLOAD_DISPATCH)
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

    /* Phase D5: populate render settings from view at request creation time.
     * The executor and backends read policy fields (LoD, HUD, etc.) from here
     * rather than reaching back into the view directly. */
    req->settings = bsg_render_settings_create();
    if (view)
	bsg_render_settings_from_view(req->settings, view);

    return req;
}


void
bsg_render_request_destroy(struct bsg_render_request *req)
{
    if (!req)
	return;
    bsg_render_settings_destroy(req->settings);
    req->settings = NULL;
    bu_free(req, "bsg_render_request");
}


int
bsg_render_request_execute(struct bsg_render_request *req)
{
    if (!req)
	return -1;

    if (req->view && req->root)
	bsg_lod_update(req->root, req->view);

    unsigned int adapter_caps = 0;
    int has_capability_query = (req->adapter && req->adapter->capabilities);
    if (has_capability_query)
	adapter_caps = req->adapter->capabilities(req->dmp);
    int do_sorted_alpha = ((req->flags & BSG_RENDER_FLAG_SORTED_ALPHA) != 0);
    if (do_sorted_alpha && has_capability_query)
	do_sorted_alpha = ((adapter_caps & BSG_ADAPTER_CAP_SORTED_ALPHA) != 0);

    /* Identity matrix as the initial accumulated transform */
    mat_t identity;
    MAT_IDN(identity);

    /* Set up per-phase collection buckets */
    struct collect_state st;
    st.req = req;
    for (int p = 0; p < BSG_RENDER_PHASE_COUNT; p++)
	bu_ptbl_init(&st.phase_items[p], 8, "render phase items");

    /* Phase D5/G7: bind this request's settings to the view for the duration
     * of the traversal so view-context resolvers (bsg_appearance_resolve)
     * can read render policy such as the geometry-default color.  Save and
     * restore any previously-bound settings to avoid leaking the request's
     * lifetime onto the view. */
    struct bsg_render_settings *saved_view_settings = NULL;
    int rebound_view_settings = 0;
    if (req->view && req->settings) {
	saved_view_settings = req->view->gv_render_settings;
	req->view->gv_render_settings = req->settings;
	rebound_view_settings = 1;
    }

    /* Collect items from the subtree */
    _render_collect(req->root, identity, &st, 0);

    if (rebound_view_settings)
	req->view->gv_render_settings = saved_view_settings;

    if (do_sorted_alpha)
	_sort_transparent_bucket(&st.phase_items[BSG_RENDER_PHASE_TRANSPARENT]);
    _sort_overlay_bucket(&st.phase_items[BSG_RENDER_PHASE_OVERLAY]);
    if (req->flags & BSG_RENDER_FLAG_HUD_PASS)
	_sort_hud_bucket(&st.phase_items[BSG_RENDER_PHASE_HUD]);

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
