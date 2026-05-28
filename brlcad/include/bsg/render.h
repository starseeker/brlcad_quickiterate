/*                      R E N D E R . H
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
/** @addtogroup libbsg
 *
 * @brief
 * BSG render-request — phase-ordered render-item execution (Phase D5).
 *
 * A `bsg_render_request` bundles together:
 *   - a target view (bsg_view*)
 *   - a root subtree to traverse (bsg_node*)
 *   - a display-manager handle (void*) forwarded to bsg_payload_dispatch
 *   - a set of BSG_RENDER_FLAG_* control flags
 *
 * bsg_render_request_execute() walks the subtree, resolves each shape to a
 * bsg_render_item, orders by render phase, and dispatches either through a
 * backend adapter or via legacy bsg_payload_dispatch fallback.
 */
/** @{ */
/* @file bsg/render.h */

#ifndef BSG_RENDER_H
#define BSG_RENDER_H

#include "common.h"
#include "bu/ptbl.h"
#include "bsg/defines.h"
#include "bsg/render_item.h"
#include "bsg/backend_adapter.h"

__BEGIN_DECLS

/** Only dispatch shapes whose s_flag != DOWN (visible / active). */
#define BSG_RENDER_FLAG_VISIBLE_ONLY      0x01

/** Invoke bsg_payload_dispatch for every qualifying shape node. */
#define BSG_RENDER_FLAG_PAYLOAD_DISPATCH  0x02

/** Queue overlay nodes (BSG_NODE_OVERLAY) for dispatch after all solids. */
#define BSG_RENDER_FLAG_OVERLAY_LAST      0x04

/** Sort translucent shapes back-to-front before dispatching (placeholder). */
#define BSG_RENDER_FLAG_SORTED_ALPHA      0x08

/**
 * Execute the HUD pass: after the main scene, traverse gv_hud_root and
 * dispatch BSG_PAYLOAD_OVERLAY nodes ordered by bsg_hud_node_meta::sort_order.
 * Ignored by bsg_render_request_execute() when req->root is not the HUD root;
 * use bsg_hud_sync() + a separate request against gv_hud_root for HUD draws.
 */
#define BSG_RENDER_FLAG_HUD_PASS          0x10

/**
 * Collect render items into req->items instead of dispatching them.
 *
 * When this flag is set the executor populates req->items (which must be
 * an initialised bu_ptbl allocated and owned by the caller) with pointers
 * to heap-allocated bsg_render_item objects — one per qualifying shape.
 * Items are appended in phase order (OPAQUE → TRANSPARENT → OVERLAY → HUD)
 * rather than tree-traversal order.  The caller is responsible for calling
 * bsg_render_item_free() on each item and bu_ptbl_free() on the table.
 *
 * When this flag is clear (the default) items are allocated internally,
 * dispatched to the adapter (or bsg_payload_dispatch), and then freed
 * before bsg_render_request_execute() returns.
 */
#define BSG_RENDER_FLAG_COLLECT_ITEMS     0x20

/**
 * Render request descriptor.
 *
 * Allocate with bsg_render_request_create(); release with
 * bsg_render_request_destroy().
 */
struct bsg_render_request {
    struct bsg_view            *view;    /**< @brief target view (borrowed, not owned) */
    bsg_node                   *root;    /**< @brief root of subtree to render (borrowed) */
    void                       *dmp;     /**< @brief display-manager handle (may be NULL) */
    unsigned int                flags;   /**< @brief BSG_RENDER_FLAG_* bitmask */

    /**
     * Optional backend adapter.
     *
     * When non-NULL, `bsg_render_request_execute` calls adapter->prepare()
     * and adapter->draw() for each render item instead of calling
     * `bsg_payload_dispatch`.  The adapter is borrowed; the request does
     * not own it.
     */
    struct bsg_backend_adapter *adapter;

    /**
     * Optional output item list (used only with BSG_RENDER_FLAG_COLLECT_ITEMS).
     *
     * When BSG_RENDER_FLAG_COLLECT_ITEMS is set the caller must set this to
     * a pointer to a caller-owned, initialised bu_ptbl.  The executor
     * appends one bsg_render_item* per qualifying shape (in phase order).
     * Ignored when BSG_RENDER_FLAG_COLLECT_ITEMS is clear.
     */
    struct bu_ptbl             *items;
};

/**
 * Allocate and return a render request with default flags
 * (BSG_RENDER_FLAG_VISIBLE_ONLY | BSG_RENDER_FLAG_PAYLOAD_DISPATCH).
 * @p view, @p root, and @p dmp are borrowed references — the request
 * does not take ownership of them.
 * Returns NULL on allocation failure.
 */
BSG_EXPORT extern struct bsg_render_request *
bsg_render_request_create(struct bsg_view *view,
			  bsg_node        *root,
			  void            *dmp);

/**
 * Release a render request previously allocated by
 * bsg_render_request_create().
 * The referenced view/root/dmp are NOT freed.
 * No-op if @p req is NULL.
 */
BSG_EXPORT extern void
bsg_render_request_destroy(struct bsg_render_request *req);

/**
 * Execute the render request:
 *  1. Walk the subtree and collect shape items with resolved transform and
 *     appearance.
 *  2. Sort and dispatch in phase order:
 *     OPAQUE -> TRANSPARENT -> OVERLAY -> HUD.
 *  3. For each item: call adapter prepare/draw callbacks when an adapter is
 *     attached; otherwise use bsg_payload_dispatch fallback.
 * Returns the number of shapes dispatched, or -1 if @p req is NULL.
 */
BSG_EXPORT extern int
bsg_render_request_execute(struct bsg_render_request *req);

__END_DECLS

#endif /* BSG_RENDER_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
