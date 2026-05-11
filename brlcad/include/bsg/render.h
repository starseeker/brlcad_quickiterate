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
 * Phase 8 render action and renderer contract for BSG.
 *
 * A bsg_render_action packages a bsg_renderer_ops callback table with a
 * view reference and is applied to a BSG scene root.  The action handles
 * the BSG structural traversal (view-scope, LoD, transform) and resolves
 * BSG material, appearance, and selection state for each drawable node.
 * The renderer ops then perform the backend-specific draw calls.
 *
 * Design constraint: libbsg must NOT depend on libdm.  The action type
 * lives in libbsg; libdm (or any other renderer) provides an ops
 * implementation that uses dm_* functions internally.
 *
 * Typical usage (from libdm):
 *
 *   struct bsg_render_action ra;
 *   bsg_render_action_init(&ra, &my_renderer_ops, my_renderer_data);
 *   bsg_render_action_set_view(&ra, view);
 *   bsg_render_action_apply(&ra, (bsg_node *)view->bsg_root);
 */
/** @{ */
/* @file bsg/render.h */

#ifndef BSG_RENDER_H
#define BSG_RENDER_H

#include "common.h"

#include "vmath.h"
#include "bsg/defines.h"
#include "bsg/appearance.h"
#include "bsg/camera.h"
#include "bsg/material.h"

__BEGIN_DECLS

struct bview; /* forward declaration (from bv/defines.h) */

/**
 * Render-pass selector.
 *
 * BSG_RENDER_PASS_ALL         — single-pass: every drawable node is visited.
 * BSG_RENDER_PASS_OPAQUE      — first pass of two-pass transparency: opaque
 *                               nodes only (transparency >= 1.0).
 * BSG_RENDER_PASS_TRANSPARENT — second pass: transparent nodes only
 *                               (transparency < 1.0).
 */
#define BSG_RENDER_PASS_ALL          0
#define BSG_RENDER_PASS_OPAQUE       1
#define BSG_RENDER_PASS_TRANSPARENT  2

/**
 * Capability flags for bsg_renderer_ops::query_capability.
 */
#define BSG_RENDERER_CAP_TRANSPARENCY  (1 << 0) /**< depth-sorted two-pass transparency */
#define BSG_RENDERER_CAP_DEPTH_MASK    (1 << 1) /**< set_depth_mask callback is functional */

/**
 * Renderer ops callback table for Phase 8 render actions.
 *
 * Every field is optional — a NULL pointer is silently skipped by
 * bsg_render_action_apply.  Implement only the callbacks you need.
 *
 * The @p renderer_data pointer supplied at bsg_render_action_init time is
 * forwarded unchanged to every callback.
 *
 * Callbacks must not call bsg_render_action_apply recursively.
 */
struct bsg_renderer_ops {

    /** Called once before traversal begins for the current frame. */
    void (*begin_frame)(void *renderer_data, struct bview *v);

    /** Called once after traversal completes for the current frame. */
    void (*end_frame)(void *renderer_data, struct bview *v);

    /**
     * Optional: supply a camera snapshot to the renderer before traversal.
     * bsg_render_action_apply derives the snapshot from @p ra->view.
     */
    void (*set_camera)(void *renderer_data,
		       const struct bsg_camera_snapshot *cam);

    /**
     * Called when a BSG_NODE_TRANSFORM is entered.
     *
     * @param new_xform   accumulated world transform after the local matrix
     * @param old_xform   accumulated world transform before the local matrix
     */
    void (*push_transform)(void *renderer_data,
			   const mat_t new_xform, const mat_t old_xform);

    /**
     * Called when a BSG_NODE_TRANSFORM is left.
     *
     * @param restored_xform  the world transform that was active before the
     *                        matching push_transform
     */
    void (*pop_transform)(void *renderer_data, const mat_t restored_xform);

    /**
     * Called for each drawable node before draw_payload.
     * bsg_render_action_apply has already resolved material via BSG APIs.
     *
     * @param mat           BSG material (valid only when have_material != 0)
     * @param have_material non-zero if the node has a BSG material side-car
     * @param is_highlighted non-zero if the node is in the active selection set
     * @param transparency  effective transparency [0..1] for the node
     */
    void (*set_material)(void *renderer_data, bsg_node *node,
			 const struct bsg_material *mat, int have_material,
			 int is_highlighted, fastf_t transparency);

    /**
     * Called for each drawable node before draw_payload, after set_material.
     * bsg_render_action_apply has already resolved appearance via BSG APIs.
     *
     * @param app           BSG appearance (valid only when have_appearance != 0)
     * @param have_appearance non-zero if the node has a BSG appearance side-car
     */
    void (*set_appearance)(void *renderer_data, bsg_node *node,
			   const struct bsg_appearance *app, int have_appearance);

    /**
     * Called to render the geometry payload for a drawable node.
     *
     * set_material and set_appearance have already been called (when non-NULL)
     * for this node before draw_payload is invoked.
     *
     * Implementations are responsible for handling shape sub-hierarchies
     * (child shapes), bounds culling, edit-matrix swaps, frame stamps, and
     * any post-draw decorations (arrows, axes, labels).
     *
     * @param world_xform  accumulated model-space transform for this node
     * @param pass         BSG_RENDER_PASS_* value for the current pass
     */
    void (*draw_payload)(void *renderer_data, bsg_node *node,
			 struct bview *v, const mat_t world_xform, int pass);

    /**
     * Optional: draw a BSG overlay node (2D / HUD geometry).
     * Called when a node marked as an overlay is reached during traversal.
     */
    void (*draw_overlay)(void *renderer_data, bsg_node *node, struct bview *v);

    /**
     * Set the depth-write mask.
     * @param on  non-zero to enable depth writes; 0 to disable.
     * Called by bsg_render_action_apply between the opaque and transparent
     * passes when two-pass transparency is active.
     */
    void (*set_depth_mask)(void *renderer_data, int on);

    /**
     * Query a renderer capability.
     * @param cap  one of BSG_RENDERER_CAP_* flags.
     * Returns non-zero if the capability is available, 0 otherwise.
     */
    int  (*query_capability)(void *renderer_data, int cap);
};


/**
 * A render action packages renderer ops with traversal state.
 *
 * Initialize with bsg_render_action_init(); set the active view with
 * bsg_render_action_set_view(); run with bsg_render_action_apply().
 */
struct bsg_render_action {
    const struct bsg_renderer_ops *ops;           /**< renderer callbacks */
    void                          *renderer_data; /**< opaque renderer handle */
    struct bview                  *view;          /**< active view (may be NULL) */
};


/* ---------------------------------------------------------------------- */
/* API                                                                      */
/* ---------------------------------------------------------------------- */

/**
 * Initialize @p ra with @p ops and @p renderer_data.
 * No-op if @p ra or @p ops is NULL.
 */
BSG_EXPORT extern void
bsg_render_action_init(struct bsg_render_action *ra,
		       const struct bsg_renderer_ops *ops,
		       void *renderer_data);

/**
 * Set the active view for @p ra.  No-op if @p ra is NULL.
 */
BSG_EXPORT extern void
bsg_render_action_set_view(struct bsg_render_action *ra, struct bview *v);

/**
 * Traverse the scene rooted at @p root, invoking renderer ops callbacks.
 *
 * Traversal order:
 *   - BSG_NODE_SENSOR and BSG_NODE_VIEW_BRIDGE nodes are skipped.
 *   - BSG_NODE_VIEW_SCOPE nodes are filtered by ra->view and recursed.
 *   - BSG_NODE_LOD nodes select the active LoD level and recurse into it.
 *   - BSG_NODE_TRANSFORM nodes call push_transform/pop_transform around descent.
 *   - All other drawable nodes receive set_material, set_appearance, and
 *     draw_payload callbacks.
 *
 * Two-pass transparency:
 *   When query_capability returns non-zero for BSG_RENDERER_CAP_TRANSPARENCY
 *   the traversal is performed twice — first with BSG_RENDER_PASS_OPAQUE,
 *   then set_depth_mask(0), then BSG_RENDER_PASS_TRANSPARENT, then
 *   set_depth_mask(1).  Otherwise a single BSG_RENDER_PASS_ALL traversal
 *   is performed.
 *
 * Camera snapshot:
 *   When ra->view is non-NULL and set_camera is non-NULL, a camera snapshot
 *   is derived from ra->view and passed to set_camera before traversal.
 *
 * Returns 1 on success, 0 if @p ra or @p root is NULL.
 */
BSG_EXPORT extern int
bsg_render_action_apply(struct bsg_render_action *ra, bsg_node *root);


/**
 * A renderer ops instance with every callback set to NULL.
 * Useful as a base for partial implementations or no-op tests.
 */
BSG_EXPORT extern const struct bsg_renderer_ops bsg_renderer_noop;

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
