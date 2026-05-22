/*                         S N A P . H
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
 * Slice 10 (bv_scene_obj_migrate): BSG-native snap API.
 *
 * BSG-native replacements for the libbv snap functions
 * (@c bv_snap_lines_2d, @c bv_snap_lines_3d, @c bv_snap_grid_2d).
 *
 * All functions operate on @c bsg_node trees and use
 * @c bsg_camera_snapshot to carry camera/viewport state, so no direct
 * libbv function calls or @c struct @c bview access is required.
 *
 * Key types:
 *   @c bsg_snap_params — aggregates the snap-flags, tolerance factor,
 *   and optional explicit candidate list (replacing the @c gv_snap_*
 *   fields of @c bview_settings).
 *
 * Key functions:
 *   @c bsg_snap_params_init  — initialize a @c bsg_snap_params to defaults.
 *   @c bsg_snap_lines_3d     — snap 3D world-space point to nearby line objects.
 *   @c bsg_snap_lines_2d     — 2D view-space wrapper for @c bsg_snap_lines_3d.
 *   @c bsg_snap_grid_2d      — snap 2D view-space point to the grid.
 *
 * Snap-flag constants (replace @c BV_SNAP_* from @c bv/defines.h):
 *
 * | Constant             | Value | Meaning                              |
 * | -------------------- | ----- | ------------------------------------ |
 * | @c BSG_SNAP_SHARED   | 0x1   | Include shared view-scope objects    |
 * | @c BSG_SNAP_LOCAL    | 0x2   | Include local view-scope objects     |
 * | @c BSG_SNAP_DB       | 0x4   | Include DB-backed scene shapes       |
 * | @c BSG_SNAP_VIEW     | 0x8   | Include view-only overlay objects    |
 * | 0                    | 0     | Include all object types             |
 */
/** @{ */
/* @file bsg/snap.h */

#ifndef BSG_SNAP_H
#define BSG_SNAP_H

#include "common.h"

#include "vmath.h"
#include "bu/ptbl.h"
#include "bsg/camera.h"
#include "bsg/defines.h"
#include "bsg/hud.h"

__BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Snap-flag constants                                                  */
/* ------------------------------------------------------------------ */

/** @brief Include shared-scope view-only objects in snap traversal. */
#define BSG_SNAP_SHARED  0x1

/** @brief Include local-scope view-only objects in snap traversal. */
#define BSG_SNAP_LOCAL   0x2

/** @brief Include DB-backed scene shapes in snap traversal. */
#define BSG_SNAP_DB      0x4

/** @brief Include view-only overlay objects (shared + local) in snap traversal. */
#define BSG_SNAP_VIEW    0x8


/* ------------------------------------------------------------------ */
/* Snap parameters                                                      */
/* Replaces the gv_snap_* fields of struct bview_settings              */
/* ------------------------------------------------------------------ */

/**
 * Aggregated snap settings.
 *
 * Replaces the three @c gv_snap_* fields of @c bview_settings:
 *
 * | @c bsg_snap_params field | @c bview_settings field      |
 * | ------------------------ | ---------------------------- |
 * | @c snap_flags            | @c gv_snap_flags             |
 * | @c snap_tol_factor       | @c gv_snap_tol_factor        |
 * | @c snap_candidates       | @c gv_snap_objs              |
 *
 * Initialize with @c bsg_snap_params_init() before use.
 *
 * @c snap_candidates is a borrowed reference to a @c bu_ptbl of
 * @c bsg_node pointers.  When non-NULL, only the listed nodes are
 * considered during line-snap traversal.  When NULL the full scene
 * tree rooted at the @c scene_root argument to @c bsg_snap_lines_3d
 * is traversed.
 *
 * @c snap_flags is a bitmask of @c BSG_SNAP_* constants.  When 0
 * all visible nodes are considered.
 */
struct bsg_snap_params {
    int              snap_flags;       /**< @brief BSG_SNAP_* mask; 0 = all */
    double           snap_tol_factor;  /**< @brief tolerance scale (default 1.0) */
    struct bu_ptbl  *snap_candidates;  /**< @brief optional explicit candidate table */
};


/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

/**
 * Initialize @p p to safe defaults (0 flags, 1.0 tolerance factor,
 * NULL candidate table).  No-op if @p p is NULL.
 */
BSG_EXPORT extern void
bsg_snap_params_init(struct bsg_snap_params *p);


/* ------------------------------------------------------------------ */
/* Line snapping                                                        */
/* Replaces bv_snap_lines_3d / bv_snap_lines_2d / bv_view_center_linesnap */
/* ------------------------------------------------------------------ */

/**
 * BSG-native replacement for @c bv_snap_lines_3d().
 *
 * Finds the closest point on any line segment within the visible scene
 * objects to the world-space input point @p p_in, subject to the snap
 * tolerance derived from @p snap's viewport dimensions and
 * @p params->snap_tol_factor.
 *
 * Scene traversal:
 *   - When @p params->snap_candidates is non-NULL, only those nodes
 *     are considered.
 *   - When NULL, the subtree rooted at @p scene_root is walked (pass
 *     NULL for @p scene_root to skip tree traversal entirely).
 *
 * @p params->snap_flags filters which node types are considered.
 * See the @c BSG_SNAP_* constants for details; 0 = all types.
 *
 * @param out_pt     Receives the snapped world-space point on success.
 *                   Must not be NULL.
 * @param snap       Camera/viewport snapshot.  Must not be NULL.
 * @param scene_root Optional BSG scene-tree root for traversal.
 * @param params     Snap settings.  NULL uses default parameters.
 * @param p_in       World-space input point to snap.  Must not be NULL.
 * @return  1 if a snap candidate was found and @p out_pt was set,
 *          0 otherwise.
 */
BSG_EXPORT extern int
bsg_snap_lines_3d(point_t *out_pt,
		  const struct bsg_camera_snapshot *snap,
		  bsg_node *scene_root,
		  const struct bsg_snap_params *params,
		  point_t *p_in);

/**
 * BSG-native replacement for @c bv_snap_lines_2d().
 *
 * Converts the view-space 2D coordinates (@p *vx, @p *vy) to a
 * world-space point using @p snap's view-to-model matrix, calls
 * @c bsg_snap_lines_3d(), and converts the result back to view space.
 * The in-place values @p *vx and @p *vy are updated only on success.
 *
 * @return  1 if snapped, 0 otherwise.
 */
BSG_EXPORT extern int
bsg_snap_lines_2d(const struct bsg_camera_snapshot *snap,
		  bsg_node *scene_root,
		  const struct bsg_snap_params *params,
		  fastf_t *vx, fastf_t *vy);


/* ------------------------------------------------------------------ */
/* Grid snapping                                                        */
/* Replaces bv_snap_grid_2d                                            */
/* ------------------------------------------------------------------ */

/**
 * BSG-native replacement for @c bv_snap_grid_2d().
 *
 * Snaps the view-space 2D coordinates (@p *vx, @p *vy) to the nearest
 * grid intersection defined by @p grid, using the viewport scale and
 * unit conversion from @p snap.
 *
 * @p *vx and @p *vy are updated in-place only on success.
 *
 * @param snap   Camera/viewport snapshot providing the model-to-view
 *               matrix and scale factors.  Must not be NULL.
 * @param grid   Grid state providing anchor, resolution, and snap flag.
 *               Must not be NULL.  When @p grid->snap is zero this
 *               function still performs the computation and returns 1;
 *               callers that want to respect the snap toggle should
 *               check @p grid->snap themselves.
 * @return  1 on success, 0 if any argument is NULL or the grid
 *          resolution is zero.
 */
BSG_EXPORT extern int
bsg_snap_grid_2d(const struct bsg_camera_snapshot *snap,
		 const struct bsg_grid_state *grid,
		 fastf_t *vx, fastf_t *vy);

__END_DECLS

#endif /* BSG_SNAP_H */

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
