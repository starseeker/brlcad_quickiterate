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
 * BSG render-request — pre-render traversal / payload dispatch (Phase 8).
 *
 * A `bsg_render_request` bundles together:
 *   - a target view (bsg_view*)
 *   - a root subtree to traverse (bsg_node*)
 *   - a display-manager handle (void*) forwarded to bsg_payload_dispatch
 *   - a set of BSG_RENDER_FLAG_* control flags
 *
 * bsg_render_request_execute() walks the subtree and calls
 * bsg_payload_dispatch() for every shape node, honouring the flags.
 * Actual rasterisation still happens in libdm; this layer only handles
 * the pre-render update pass to prevent a libbsg → libdm circular
 * dependency.
 */
/** @{ */
/* @file bsg/render.h */

#ifndef BSG_RENDER_H
#define BSG_RENDER_H

#include "common.h"
#include "bsg/defines.h"

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
 * Render request descriptor.
 *
 * Allocate with bsg_render_request_create(); release with
 * bsg_render_request_destroy().
 */
struct bsg_render_request {
    struct bsg_view *view;   /**< @brief target view (borrowed, not owned) */
    bsg_node        *root;   /**< @brief root of subtree to render (borrowed) */
    void            *dmp;    /**< @brief display-manager handle (may be NULL) */
    unsigned int     flags;  /**< @brief BSG_RENDER_FLAG_* bitmask */
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
 *  1. Walk the subtree (req->root) with bsg_visit.
 *  2. For each BSG_NODE_SHAPE node:
 *     - If BSG_RENDER_FLAG_VISIBLE_ONLY is set, skip shapes with s_flag==DOWN.
 *     - If BSG_RENDER_FLAG_PAYLOAD_DISPATCH is set, call bsg_payload_dispatch.
 *     - If BSG_RENDER_FLAG_OVERLAY_LAST is set, BSG_NODE_OVERLAY shapes are
 *       deferred and dispatched after all non-overlay shapes.
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
