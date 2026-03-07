/*                   B S G / P O L Y G O N . H
 * BRL-CAD
 *
 * Copyright (c) 2004-2025 United States Government as represented by
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
/** @addtogroup bsg_polygon
 *
 * @brief
 * Polygon functions for the BRL-CAD Scene Graph (BSG) API.
 *
 * These functions replace the corresponding @c bv_polygon_* and
 * @c bv_create_polygon* / @c bv_update_polygon / @c bv_select_polygon /
 * @c bv_move_polygon / @c bv_dup_view_polygon / @c bv_polygon_csg functions
 * declared in @c <bv/polygon.h>.  New code should use these interfaces.
 *
 * In Phase 1 each @c bsg_* function is a trivial wrapper around its
 * @c bv_* counterpart.  Because @c bsg_shape and @c bv_scene_obj share
 * identical memory layout in Phase 1 the wrappers incur no conversion cost,
 * but unlike the old API they accept @c bsg_shape * directly — eliminating
 * the explicit @c (struct bv_scene_obj *) casts that appeared at every
 * call site.
 *
 * ### Function naming convention
 *
 * | Old (legacy) function         | New (BSG) function               |
 * |-------------------------------|----------------------------------|
 * | bv_create_polygon_obj()       | bsg_create_polygon_obj()         |
 * | bv_create_polygon()           | bsg_create_polygon()             |
 * | bv_update_polygon()           | bsg_update_polygon()             |
 * | bv_polygon_vlist()            | bsg_polygon_vlist()              |
 * | bv_select_polygon()           | bsg_select_polygon()             |
 * | bv_move_polygon()             | bsg_move_polygon()               |
 * | bv_dup_view_polygon()         | bsg_dup_view_polygon()           |
 * | bv_polygon_cpy()              | bsg_polygon_cpy()                |
 * | bv_polygon_calc_fdelta()      | bsg_polygon_calc_fdelta()        |
 * | bv_polygon_fill_segments()    | bsg_polygon_fill_segments()      |
 * | bv_polygon_csg()              | bsg_polygon_csg()                |
 */
/** @{ */
/* @file bsg/polygon.h */

#ifndef BSG_POLYGON_H
#define BSG_POLYGON_H

#include "common.h"
#include "vmath.h"
#include "bu/color.h"
#include "bg/polygon.h"
#include "bg/polygon_types.h"
#include "bv/polygon.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/* ====================================================================== *
 * Polygon update mode constants                                           *
 * (parallel to the BV_POLYGON_UPDATE_* set in bv/polygon.h)             *
 * ====================================================================== */

/** @brief Recompute the vlist from the stored polygon geometry. */
#define BSG_POLYGON_UPDATE_DEFAULT          0
/** @brief Only refresh display properties (color, line-width, fill);
 *         do not recompute geometry. */
#define BSG_POLYGON_UPDATE_PROPS_ONLY       1
/** @brief Select the nearest contour point to the mouse cursor. */
#define BSG_POLYGON_UPDATE_PT_SELECT        2
/** @brief Clear the current point selection. */
#define BSG_POLYGON_UPDATE_PT_SELECT_CLEAR  3
/** @brief Move the currently-selected point to the mouse cursor. */
#define BSG_POLYGON_UPDATE_PT_MOVE          4
/** @brief Append a new contour point at the mouse cursor. */
#define BSG_POLYGON_UPDATE_PT_APPEND        5

/* ====================================================================== *
 * Polygon shape type constants                                            *
 * (parallel to the BV_POLYGON_* set in bv/polygon.h)                    *
 * ====================================================================== */

#define BSG_POLYGON_GENERAL    0
#define BSG_POLYGON_CIRCLE     1
#define BSG_POLYGON_ELLIPSE    2
#define BSG_POLYGON_RECTANGLE  3
#define BSG_POLYGON_SQUARE     4

/* ====================================================================== *
 * Polygon scene object creation                                           *
 * ====================================================================== */

/**
 * @brief Create a scene object that wraps an existing @c bv_polygon.
 *
 * The new @c bsg_shape takes ownership of @p p.  Returns NULL on error.
 * Replaces bv_create_polygon_obj().
 */
BSG_EXPORT bsg_shape *
bsg_create_polygon_obj(bsg_view *v, int flags, struct bv_polygon *p);

/**
 * @brief Create a scene object containing a new default polygon of @p type.
 *
 * @p fp, if non-NULL, supplies the first polygon point; otherwise a default
 * position is used.  Returns NULL on error.  Replaces bv_create_polygon().
 */
BSG_EXPORT bsg_shape *
bsg_create_polygon(bsg_view *v, int flags, int type, point_t *fp);

/* ====================================================================== *
 * Polygon update                                                          *
 * ====================================================================== */

/**
 * @brief Update the scene object @p s for the polygon it contains.
 *
 * @p utype selects the update mode (one of the BSG_POLYGON_UPDATE_* constants).
 * @p v is the view to use for the update (usually @c s->s_v).
 * Returns 0 on success, non-zero on error.  Replaces bv_update_polygon().
 */
BSG_EXPORT int
bsg_update_polygon(bsg_shape *s, bsg_view *v, int utype);

/**
 * @brief Rebuild only the vlist of @p s from its stored polygon geometry.
 *
 * Unlike bsg_update_polygon() this does NOT alter the polygon data itself.
 * Replaces bv_polygon_vlist().
 */
BSG_EXPORT void
bsg_polygon_vlist(bsg_shape *s);

/* ====================================================================== *
 * Polygon selection / movement                                           *
 * ====================================================================== */

/**
 * @brief Find the polygon scene object in @p objs closest to @p cp.
 *
 * Returns NULL if no polygon is near the point.  Replaces bv_select_polygon().
 */
BSG_EXPORT bsg_shape *
bsg_select_polygon(struct bu_ptbl *objs, point_t *cp);

/**
 * @brief Translate the polygon in @p s so that @p cp maps to @p pp.
 *
 * Returns 0 on success.  Replaces bv_move_polygon().
 */
BSG_EXPORT int
bsg_move_polygon(bsg_shape *s, point_t *cp, point_t *pp);

/**
 * @brief Duplicate the polygon scene object @p s, giving the copy @p nname.
 *
 * The copy is view-synced to the view of @p s; the caller may reassign
 * @c s->s_v to re-target it.  Returns NULL on error.
 * Replaces bv_dup_view_polygon().
 */
BSG_EXPORT bsg_shape *
bsg_dup_view_polygon(const char *nname, bsg_shape *s);

/* ====================================================================== *
 * Polygon data helpers                                                    *
 * ====================================================================== */

/**
 * @brief Deep-copy polygon data from @p src to @p dest.
 *
 * Also performs a view-sync; if the destination is in a different view the
 * caller must update @c dest->s_v accordingly.
 * Replaces bv_polygon_cpy().
 */
BSG_EXPORT void
bsg_polygon_cpy(struct bv_polygon *dest, struct bv_polygon *src);

/**
 * @brief Suggest a fill-line spacing for polygon @p p.
 *
 * Returns a non-negative value.  Replaces bv_polygon_calc_fdelta().
 */
BSG_EXPORT int
bsg_polygon_calc_fdelta(struct bv_polygon *p);

/**
 * @brief Compute fill line segments for @p poly projected onto @p vp.
 *
 * Returns a newly-allocated @c bg_polygon (caller must free), or NULL.
 * Replaces bv_polygon_fill_segments().
 */
BSG_EXPORT struct bg_polygon *
bsg_polygon_fill_segments(struct bg_polygon *poly, plane_t *vp,
			   vect2d_t line_slope, fastf_t line_spacing);

/* ====================================================================== *
 * Polygon Boolean operations                                              *
 * ====================================================================== */

/**
 * @brief Apply Boolean op @p op to @p target using @p stencil as the operand.
 *
 * @p op may be 'u' (union), '-' (difference), or '+' (intersection).
 * Returns the number of contours modified.  Replaces bv_polygon_csg().
 */
BSG_EXPORT int
bsg_polygon_csg(bsg_shape *target, bsg_shape *stencil, bg_clip_t op);

/* ====================================================================== *
 * Lower-level polygon helpers                                             *
 * ====================================================================== */

BSG_EXPORT void bsg_fill_polygon(bsg_shape *s);
BSG_EXPORT void bsg_polygon_contour(bsg_shape *s, struct bg_poly_contour *c, int curr_c, int curr_i, int do_pnt);
BSG_EXPORT int bsg_append_polygon_pt(bsg_shape *s, point_t *np);
BSG_EXPORT int bsg_select_polygon_pt(bsg_shape *s, point_t *cp);
BSG_EXPORT void bsg_select_clear_polygon_pt(bsg_shape *s);
BSG_EXPORT int bsg_move_polygon_pt(bsg_shape *s, point_t *mp);
BSG_EXPORT int bsg_update_polygon_circle(bsg_shape *s, point_t *cp, fastf_t pixel_size);
BSG_EXPORT int bsg_update_polygon_ellipse(bsg_shape *s, point_t *cp, fastf_t pixel_size);
BSG_EXPORT int bsg_update_polygon_rectangle(bsg_shape *s, point_t *cp);
BSG_EXPORT int bsg_update_polygon_square(bsg_shape *s, point_t *cp);
BSG_EXPORT int bsg_update_general_polygon(bsg_shape *s, int utype, point_t *cp);

__END_DECLS

#endif /* BSG_POLYGON_H */
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
