/*                        H U D . H
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
 * Phase D4 (drawing_modernization): per-view HUD root and overlay
 * role/lifecycle/order metadata.
 *
 * A @c bsg_view owns a HUD scene root (@c gv_hud_root) that is separate
 * from the model draw root (@c gv_draw_root).  The HUD root contains one
 * child @c bsg_node per faceplate feature (center dot, axes, scale, ADC,
 * grid, rubber-band rect, params text).  Each feature node carries a
 * @c bsg_hud_node_meta descriptor that records:
 *
 *   - which faceplate feature it represents (@c bsg_hud_feature_type)
 *   - its coordinate space (@c bsg_hud_coord)
 *   - its overlay role (@c bsg_overlay_role)
 *   - its lifecycle policy (@c bsg_overlay_lifecycle)
 *   - its render-phase sort order
 *
 * @c bsg_hud_sync() reads the current @c bsg_view_settings faceplate flags
 * and updates each feature node's @c s_flag (UP = enabled, DOWN = disabled).
 * The render pass orders nodes by @c sort_order so earlier phases (center dot,
 * axes) precede later ones (grid, rect, params text).
 *
 * Actual rasterisation stays in libdm; libbsg only manages structure and
 * ordering.  @c dm_draw_faceplate() calls @c bsg_hud_sync() at the start of
 * each frame to propagate the current settings into the HUD tree before
 * drawing.
 */
/** @{ */
/* @file bsg/hud.h */

#ifndef BSG_HUD_H
#define BSG_HUD_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

struct bsg_view;   /* forward declaration */


/* -----------------------------------------------------------------------
 * Overlay role
 *
 * Describes where in the render stack a node lives.
 * ----------------------------------------------------------------------- */

/**
 * Overlay rendering role — controls when in the render pipeline a node is
 * drawn relative to scene geometry.
 */
typedef enum bsg_overlay_role {
    BSG_OVERLAY_ROLE_MODEL  = 0, /**< @brief drawn after scene solids, in model space */
    BSG_OVERLAY_ROLE_SCREEN = 1, /**< @brief drawn after model overlays, in screen/HUD space */
    BSG_OVERLAY_ROLE_XRAY   = 2  /**< @brief always on top (depth-ignore), in model space */
} bsg_overlay_role;


/* -----------------------------------------------------------------------
 * HUD coordinate space
 * ----------------------------------------------------------------------- */

/**
 * Coordinate space used for geometry in a HUD node.
 */
typedef enum bsg_hud_coord {
    BSG_HUD_COORD_SCREEN_PX       = 0, /**< @brief pixel coordinates (0,0 = top-left) */
    BSG_HUD_COORD_NDC             = 1, /**< @brief normalized device coords (+-1) */
    BSG_HUD_COORD_VIEW_PLANE      = 2, /**< @brief view-plane coords (same as NDC for ortho) */
    BSG_HUD_COORD_MODEL_ANCHORED  = 3  /**< @brief label anchored to a model-space point */
} bsg_hud_coord;


/* -----------------------------------------------------------------------
 * Overlay lifecycle policy
 * ----------------------------------------------------------------------- */

/**
 * Lifecycle policy for an overlay or HUD node.
 */
typedef enum bsg_overlay_lifecycle {
    BSG_OVERLAY_LC_PERSISTENT = 0, /**< @brief node survives across frames; only rebuilt on settings change */
    BSG_OVERLAY_LC_PER_FRAME  = 1  /**< @brief node is rebuilt/refreshed every frame */
} bsg_overlay_lifecycle;


/* -----------------------------------------------------------------------
 * Faceplate feature type
 *
 * Identifies which faceplate feature a HUD node represents.
 * Values also serve as the default sort_order (render-phase position).
 * ----------------------------------------------------------------------- */

/**
 * Faceplate feature tag stored in @c bsg_hud_node_meta::feature_type.
 * Numeric values define the default render-phase order: lower values are
 * drawn first.
 */
typedef enum bsg_hud_feature_type {
    BSG_HUD_FEATURE_CENTER_DOT  = 0, /**< @brief single center dot (screen-space point) */
    BSG_HUD_FEATURE_MODEL_AXES  = 1, /**< @brief model-space axes widget */
    BSG_HUD_FEATURE_VIEW_AXES   = 2, /**< @brief view-space axes widget (corner) */
    BSG_HUD_FEATURE_VIEW_SCALE  = 3, /**< @brief linear scale bar */
    BSG_HUD_FEATURE_ADC         = 4, /**< @brief angle/distance cursor */
    BSG_HUD_FEATURE_GRID        = 5, /**< @brief reference grid */
    BSG_HUD_FEATURE_RECT        = 6, /**< @brief rubber-band selection rectangle */
    BSG_HUD_FEATURE_VIEW_PARAMS = 7  /**< @brief text overlay (size, center, az/el, FPS) */
} bsg_hud_feature_type;

/** Number of distinct faceplate features managed by the HUD root. */
#define BSG_HUD_FEATURE_COUNT 8


/* -----------------------------------------------------------------------
 * Per-node HUD metadata
 * ----------------------------------------------------------------------- */

/**
 * Metadata attached to each HUD child node via
 * @c bsg_node_set_internal_data() / @c bsg_node_get_internal_data().
 *
 * Consumers cast the result of @c bsg_node_get_internal_data() to
 * @c struct @c bsg_hud_node_meta* when the node's @c s_type_flags
 * includes @c BSG_PAYLOAD_OVERLAY.
 */
struct bsg_hud_node_meta {
    bsg_hud_feature_type  feature_type; /**< @brief which faceplate feature */
    bsg_hud_coord         coord_space;  /**< @brief geometry coordinate convention */
    bsg_overlay_role      role;         /**< @brief render-pass placement */
    bsg_overlay_lifecycle lifecycle;    /**< @brief rebuild frequency */
    int                   sort_order;   /**< @brief render-phase ordering (lower = earlier) */
};


/* -----------------------------------------------------------------------
 * HUD root management
 * ----------------------------------------------------------------------- */

/**
 * Create the per-view HUD root for @p v and store it in @p v->gv_hud_root.
 *
 * Pre-allocates @c BSG_HUD_FEATURE_COUNT child nodes (one per faceplate
 * feature), each with @c s_flag = DOWN.  @c bsg_hud_sync() subsequently
 * updates @c s_flag to reflect the current @c bsg_view_settings.
 *
 * Returns the HUD root node, or NULL if @p v is NULL or allocation fails.
 * If the HUD root already exists this is a no-op that returns the existing
 * root.
 */
BSG_EXPORT extern bsg_node *
bsg_hud_root_create(struct bsg_view *v);


/**
 * Return the HUD root previously created by @c bsg_hud_root_create(), or
 * NULL if it has not been created yet.
 */
BSG_EXPORT extern bsg_node *
bsg_hud_root_get(struct bsg_view *v);


/**
 * Destroy the HUD root and all its child feature nodes, and clear
 * @p v->gv_hud_root.
 *
 * No-op if @p v is NULL or the HUD root has not been created.
 */
BSG_EXPORT extern void
bsg_hud_root_destroy(struct bsg_view *v);


/**
 * Synchronize the HUD root's child nodes with the current faceplate settings
 * in @p v->gv_s (or @p v->gv_ls if @p v->gv_s is NULL).
 *
 * For each faceplate feature: if enabled, the corresponding child node's
 * @c s_flag is set to UP; if disabled, to DOWN.  The HUD root is created
 * automatically on first call if it does not yet exist.
 *
 * Returns 0 on success, -1 if @p v is NULL or the root cannot be created.
 */
BSG_EXPORT extern int
bsg_hud_sync(struct bsg_view *v);


/**
 * Return the @c bsg_hud_node_meta for @p node, or NULL if @p node is not a
 * HUD feature node (i.e. does not carry @c BSG_PAYLOAD_OVERLAY metadata
 * installed by @c bsg_hud_root_create()).
 */
BSG_EXPORT extern struct bsg_hud_node_meta *
bsg_hud_node_get_meta(bsg_node *node);

__END_DECLS

#endif /* BSG_HUD_H */

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
