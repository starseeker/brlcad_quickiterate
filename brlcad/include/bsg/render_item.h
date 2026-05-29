/*                  R E N D E R _ I T E M . H
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
 * Render item — the resolved, flat descriptor emitted by BSG traversal
 * that a backend draws without further scene-graph access (Phase D5).
 *
 * During `bsg_render_request_execute` the traversal resolves each shape's
 * accumulated world transform, appearance properties, and payload type into
 * a `bsg_render_item`.  Items are then sorted into four render phases and
 * dispatched to the active `bsg_backend_adapter` (or the legacy
 * `bsg_payload_dispatch` fallback when no adapter is set).
 *
 * Backends receive items in this order:
 *   BSG_RENDER_PHASE_OPAQUE → BSG_RENDER_PHASE_TRANSPARENT →
 *   BSG_RENDER_PHASE_OVERLAY → BSG_RENDER_PHASE_HUD
 *
 * Within BSG_RENDER_PHASE_TRANSPARENT items are ordered back-to-front
 * when `BSG_RENDER_FLAG_SORTED_ALPHA` is set on the request.
 */
/** @{ */
/* @file bsg/render_item.h */

#ifndef BSG_RENDER_ITEM_H
#define BSG_RENDER_ITEM_H

#include "common.h"

#include "vmath.h"
#include "bu/vls.h"
#include "bsg/defines.h"
#include "bsg/appearance_action.h"

__BEGIN_DECLS

/**
 * Render phase — controls the order in which items are dispatched.
 *
 * Phase ordering: OPAQUE (0) → TRANSPARENT (1) → OVERLAY (2) → HUD (3).
 * Do not change the integer values; they serve as indices into the
 * per-phase bucket arrays inside the render-request executor.
 */
typedef enum bsg_render_phase {
    BSG_RENDER_PHASE_OPAQUE      = 0, /**< @brief  solid geometry, no alpha */
    BSG_RENDER_PHASE_TRANSPARENT = 1, /**< @brief  geometry with transparency > 0 */
    BSG_RENDER_PHASE_OVERLAY     = 2, /**< @brief  BSG_PAYLOAD_OVERLAY shapes */
    BSG_RENDER_PHASE_HUD         = 3, /**< @brief  HUD / faceplate shapes */
    BSG_RENDER_PHASE_COUNT       = 4  /**< @brief  sentinel — number of phases */
} bsg_render_phase;


/**
 * A fully resolved, flat description of one drawable shape.
 *
 * Produced by `bsg_render_request_execute` from a scene-graph shape node.
 * The `node` pointer is borrowed; the item does not own the node.
 * The `model_mat` is the accumulated product of all `BSG_NODE_TRANSFORM`
 * ancestor matrices at the time the shape was visited.
 */
struct bsg_render_item {
    bsg_node          *node;          /**< @brief  source shape node (borrowed) */
    struct bsg_view   *view;          /**< @brief  request view context (borrowed) */
    mat_t              model_mat;     /**< @brief  accumulated model-to-world matrix */

    /**
     * Fully resolved appearance (Phase D5).
     *
     * Populated by bsg_appearance_resolve() during bsg_render_request_execute.
     * Backends MUST read appearance fields from here and must NOT re-derive
     * them by accessing node internals during drawing.
     */
    struct bsg_resolved_appearance appearance;

    /* Payload / phase classification */
    unsigned long long payload_flags; /**< @brief  BSG_PAYLOAD_* bits copied from s_type_flags */
    bsg_render_phase   phase;         /**< @brief  render phase this item belongs to */

    /**
     * Within-phase ordering hint.
     *
     * For BSG_RENDER_PHASE_TRANSPARENT: a larger value means the item is
     * farther from the camera and should be drawn first (back-to-front).
     * For BSG_RENDER_PHASE_HUD: corresponds to bsg_hud_node_meta::sort_order.
     * For other phases: 0 (insertion order).
     */
    int                sort_key;
};


/* -----------------------------------------------------------------------
 * Item lifecycle
 * ----------------------------------------------------------------------- */

/**
 * Allocate and zero-initialise a render item.
 * Returns NULL on allocation failure.
 * The caller owns the returned item and must release it with
 * bsg_render_item_free().
 */
BSG_EXPORT extern struct bsg_render_item *
bsg_render_item_create(void);

/**
 * Release a render item previously allocated by bsg_render_item_create().
 * No-op if @p item is NULL.
 */
BSG_EXPORT extern void
bsg_render_item_free(struct bsg_render_item *item);

__END_DECLS

#endif /* BSG_RENDER_ITEM_H */

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
