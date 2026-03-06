/*               L I B B S G _ D M / L I B B S G _ D M . H
 * BRL-CAD
 *
 * Copyright (c) 2024-2025 United States Government as represented by
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
/**
 * @file libbsg_dm/libbsg_dm.h
 *
 * @brief libbsg_dm — Display-manager adapter for libbsg.
 *
 * This library is the bridge between the fully independent libbsg scene
 * graph and BRL-CAD's display-manager (DM) rendering layer.  It is the
 * **only** module that may include both @c libbsg/libbsg.h and the legacy
 * @c dm.h / @c bv/vlist.h headers.
 *
 * ### What this library provides
 *
 *   - @c libbsg_dm_draw_scene(): traverse a libbsg scene graph and draw
 *     each @c libbsg_shape via @c dm_draw_vlist().
 *   - @c libbsg_dm_load_camera(): push a @c libbsg_camera's matrices into the
 *     display manager.
 *   - @c libbsg_shape_wireframe(): convert an @c rt_db_internal wireframe
 *     into a @c libbsg_shape's vlist via the primitive's @c ft_plot()
 *     functab entry.  This replaces the @c draw2.cpp pipeline for the
 *     libbsg rendering path.
 */

#ifndef LIBBSG_DM_H
#define LIBBSG_DM_H

#include "common.h"
#include "libbsg/libbsg.h"   /* libbsg_shape, bsg_node, libbsg_camera, etc. */

/* Export macro ------------------------------------------------------------ */
#ifndef LIBBSG_DM_EXPORT
#  ifdef LIBBSG_DM_DLL_EXPORTS
#    define LIBBSG_DM_EXPORT COMPILER_DLLEXPORT
#  elif defined(LIBBSG_DM_DLL_IMPORTS)
#    define LIBBSG_DM_EXPORT COMPILER_DLLIMPORT
#  else
#    define LIBBSG_DM_EXPORT
#  endif
#endif

/* Forward-declare dm and rt types so callers don't need to pull in the
 * full dm.h / raytrace.h unless they actually call these functions. */
struct dm;
struct rt_db_internal;
struct bg_tess_tol;
struct bn_tol;

__BEGIN_DECLS

/** @addtogroup libbsg_dm
 * @{ */

/** @file libbsg_dm/libbsg_dm.h */

/* ========================================================================
 * Draw parameters
 * ======================================================================== */

/**
 * @brief Per-draw-call parameters for @c libbsg_dm_draw_scene().
 *
 * These control appearance attributes that are applied as defaults when a
 * @c libbsg_shape has not overridden them.
 */
struct libbsg_dm_draw_params {
    short        r;           /**< default wireframe red   (0–255) */
    short        g;           /**< default wireframe green (0–255) */
    short        b;           /**< default wireframe blue  (0–255) */
    int          line_width;  /**< wireframe line width in pixels  */
};
typedef struct libbsg_dm_draw_params libbsg_dm_draw_params;

/* ========================================================================
 * Camera
 * ======================================================================== */

/**
 * @brief Load a @c libbsg_camera's matrices into the display manager.
 *
 * Calls @c dm_loadpmatrix() with the perspective matrix and
 * @c dm_loadmatrix() with the model-to-view matrix.
 *
 * @param dmp  Display manager (must not be NULL).
 * @param cam  Camera to load (must not be NULL).
 * @return 0 on success, -1 on error.
 */
LIBBSG_DM_EXPORT int libbsg_dm_load_camera(struct dm         *dmp,
					   const libbsg_camera  *cam);

/* ========================================================================
 * Scene drawing
 * ======================================================================== */

/**
 * @brief Draw a libbsg scene graph through the display manager.
 *
 * Traverses @p root with @c libbsg_traverse() and, for each
 * @c LIBBSG_NODE_SHAPE leaf that has non-empty vlists:
 *   1. Sets the foreground colour via @c dm_set_fg().
 *   2. Loads the accumulated transform via @c dm_loadmatrix().
 *   3. Calls @c dm_draw_vlist() for the shape's vlist chain.
 *
 * If @p view is non-NULL and contains a camera, that camera is loaded
 * at the start of the draw (before shape drawing begins).
 *
 * @param dmp     Display manager (must not be NULL).
 * @param root    Root of the scene sub-graph to draw.
 * @param view    Optional view parameters (camera + viewport size).
 * @param params  Optional draw parameters; if NULL, defaults are used
 *                (white wireframe, 1-pixel lines).
 * @return Number of shapes drawn, or -1 on error.
 */
LIBBSG_DM_EXPORT int libbsg_dm_draw_scene(struct dm                    *dmp,
					  bsg_node                     *root,
					  const libbsg_view_params     *view,
					  const libbsg_dm_draw_params  *params);

/* ========================================================================
 * Geometry ingestion (Step 6)
 * ======================================================================== */

/**
 * @brief Populate a @c libbsg_shape's vlist from an @c rt_db_internal.
 *
 * Calls the primitive's @c ft_plot() functab entry to generate a
 * wireframe vlist and appends the result to @p s->vlist.  This is the
 * equivalent of the @c draw2.cpp pipeline for the libbsg rendering path.
 *
 * @param s     Destination shape (must not be NULL).
 * @param ip    Source database object (must not be NULL).
 * @param ttol  Tessellation tolerances; if NULL, defaults are used.
 * @param tol   BN geometric tolerance; if NULL, defaults are used.
 * @return 0 on success, -1 on failure.
 */
LIBBSG_DM_EXPORT int libbsg_shape_wireframe(libbsg_shape              *s,
					    struct rt_db_internal     *ip,
					    const struct bg_tess_tol  *ttol,
					    const struct bn_tol       *tol);

/** @} */

__END_DECLS

#endif /* LIBBSG_DM_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
