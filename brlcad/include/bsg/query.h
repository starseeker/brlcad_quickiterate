/*                        Q U E R Y . H
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
 * Slice 8 (bv_scene_obj_migrate):
 * Bounds computation and camera-space view-query APIs for the BSG scene graph.
 *
 * This header provides BSG-native replacements for the libbv view-query
 * functions (bv_scene_obj_bound, bv_view_bounds, bv_view_objs_select, and
 * bv_view_objs_rect_select).  All implementations in libbsg/query.c operate
 * exclusively on bsg_node trees and use the bsg_camera_snapshot to carry
 * camera state, so no direct libbv function calls are required at runtime.
 *
 * Key types:
 *   bsg_view_bounds_result — holds the oriented bounding box (OBB) and
 *   derived camera-query data computed from the scene tree.
 *
 * Key functions:
 *   bsg_node_compute_bound   — compute AABB for one node from its payload.
 *   bsg_view_compute_bounds  — build view OBB from the full scene tree.
 *   bsg_view_select          — find nodes intersecting a single pixel.
 *   bsg_view_rect_select     — find nodes intersecting a screen rectangle.
 */
/** @{ */
/* @file bsg/query.h */

#ifndef BSG_QUERY_H
#define BSG_QUERY_H

#include "common.h"

#include "vmath.h"
#include "bu/ptbl.h"
#include "bsg/camera.h"
#include "bsg/defines.h"

__BEGIN_DECLS

struct bview; /* forward declaration */

/* ------------------------------------------------------------------ */
/* View bounds result                                                   */
/* ------------------------------------------------------------------ */

/**
 * Result of bsg_view_compute_bounds().
 *
 * Mirrors the fields that bv_view_bounds() writes into struct bview,
 * allowing callers to read the derived camera-query state without
 * accessing bview internals.
 *
 * All spatial fields use model-space coordinates; the OBB extents are
 * half-lengths along the three principal axes of the box.
 */
struct bsg_view_bounds_result {
    /* Oriented bounding box of the view frustum projected through the scene. */
    point_t obb_center;   /**< @brief OBB center in model space */
    vect_t  obb_extent1;  /**< @brief OBB half-extent along look direction */
    vect_t  obb_extent2;  /**< @brief OBB half-extent along screen-right */
    vect_t  obb_extent3;  /**< @brief OBB half-extent along screen-up */

    /* View-space window bounds (normalized, matching gv_wmin/gv_wmax). */
    fastf_t wmin[2];      /**< @brief lower-left corner in view space */
    fastf_t wmax[2];      /**< @brief upper-right corner in view space */

    /* Look-at direction and backed-out eye position (model space). */
    vect_t  lookat;       /**< @brief look-at direction (unit vector) */
    point_t vc_backout;   /**< @brief eye position backed out by scene radius */

    /* Scene bounding-sphere radius. */
    fastf_t radius;       /**< @brief scene bounding-sphere radius */

    /* Non-zero when the scene contains at least one visible object. */
    int     have_result;
};


/* ------------------------------------------------------------------ */
/* Per-node bounds computation                                          */
/* ------------------------------------------------------------------ */

/**
 * BSG-native replacement for bv_scene_obj_bound().
 *
 * Computes the axis-aligned bounding box for node @p n by inspecting its
 * payload: mesh-LoD cached bounds (with the per-node transform applied) or
 * vlist geometry, whichever is available.  If @p n is or is owned by a
 * BSG_NODE_LOD proxy the active level child is resolved first.
 *
 * On success the result is stored into @p n's backing fields via the BSG
 * accessor functions (bsg_node_bounds_set, bsg_node_center_set,
 * bsg_node_size_set).  When the active level child differs from @p n the
 * bounds are also propagated to @p n.
 *
 * @param n  Node to compute bounds for.  Must not be NULL.
 * @param v  Optional view pointer used only to look up the per-view LoD
 *           cursor.  Pass NULL to skip LoD resolution (uses level 0).
 * @return   1 if bounds were successfully computed, 0 otherwise.
 */
BSG_EXPORT extern int
bsg_node_compute_bound(bsg_node *n, struct bview *v);


/* ------------------------------------------------------------------ */
/* View OBB and camera-query functions                                  */
/* ------------------------------------------------------------------ */

/**
 * BSG-native replacement for bv_view_bounds().
 *
 * Traverses the subtree rooted at @p root, calls bsg_node_compute_bound()
 * on every leaf, accumulates the scene AABB, and fills @p out with the
 * resulting OBB and derived camera-query state.
 *
 * The OBB is only computed for orthographic views (snap->projection ==
 * BSG_CAMERA_ORTHO); for perspective views @p out->have_result is set but
 * the OBB extents are left at zero.
 *
 * @param out   Caller-allocated result struct.  Must not be NULL.
 * @param root  BSG scene-tree root node.
 * @param snap  Camera snapshot providing viewport dimensions and matrices.
 * @return  0 on success, -1 if any required argument is NULL or the
 *          viewport dimensions are zero.
 */
BSG_EXPORT extern int
bsg_view_compute_bounds(struct bsg_view_bounds_result *out,
			bsg_node *root,
			const struct bsg_camera_snapshot *snap);

/**
 * BSG-native replacement for bv_view_objs_select().
 *
 * Fills @p sset with the bsg_node pointers of all leaf nodes in the subtree
 * @p root whose AABB intersects the oriented bounding box produced by
 * projecting screen pixel (@p x, @p y) through the scene.
 *
 * @p snap provides all camera/viewport data; no struct bview pointer is
 * required.
 *
 * @param sset  Pre-initialised bu_ptbl to receive matching bsg_node pointers.
 * @param root  BSG scene-tree root.
 * @param snap  Camera snapshot.
 * @param x     Screen pixel X coordinate (0 == left edge).
 * @param y     Screen pixel Y coordinate (0 == top edge).
 * @return  Number of matching nodes, or 0 if arguments are invalid.
 */
BSG_EXPORT extern int
bsg_view_select(struct bu_ptbl *sset,
		bsg_node *root,
		const struct bsg_camera_snapshot *snap,
		int x, int y);

/**
 * BSG-native replacement for bv_view_objs_rect_select().
 *
 * Fills @p sset with the bsg_node pointers of all leaf nodes in the subtree
 * @p root whose AABB intersects the oriented bounding box produced by
 * projecting screen rectangle (@p x1, @p y1)–(@p x2, @p y2) through the
 * scene.
 *
 * @param sset        Pre-initialised bu_ptbl to receive matching bsg_node pointers.
 * @param root        BSG scene-tree root.
 * @param snap        Camera snapshot.
 * @param x1,y1       Top-left corner of the rectangle (screen pixels).
 * @param x2,y2       Bottom-right corner of the rectangle (screen pixels).
 * @return  Number of matching nodes, or 0 if arguments are invalid.
 */
BSG_EXPORT extern int
bsg_view_rect_select(struct bu_ptbl *sset,
		     bsg_node *root,
		     const struct bsg_camera_snapshot *snap,
		     int x1, int y1, int x2, int y2);

__END_DECLS

#endif /* BSG_QUERY_H */

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
