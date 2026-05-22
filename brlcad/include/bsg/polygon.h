/*                      P O L Y G O N . H
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
 * Slice 7 (bv_scene_obj_migrate): BSG polygon overlay payload API.
 *
 * A polygon payload stores all the state of a view-space polygon overlay:
 * the shape type, the geometric contour data (@c bg_polygon), the view
 * plane it was created on, optional fill parameters, and edit-cursor
 * indices.  It is the typed BSG replacement for the legacy @c bv_polygon
 * stored in @c s_i_data on a @c bv_scene_obj.
 *
 * Geometric polygon operations (clipper CSG, fill segments, plane math)
 * stay in libbg.  This module provides the BSG scene-graph payload API.
 *
 * Usage sketch:
 * @code
 *   // Create a payload
 *   struct bsg_payload *pp = bsg_payload_polygon_create(BSG_POLYGON_GENERAL);
 *   // Set up view plane from the rendering view
 *   plane_t vp;
 *   bv_view_plane(&vp, v);
 *   bsg_payload_polygon_view_plane_set(pp, vp);
 *   // Attach to a BSG shape node
 *   bsg_node *shape = bsg_node_create(v, BSG_NODE_SHAPE);
 *   bsg_node_payload_set(shape, pp);
 *   // Generate vlist wireframe
 *   bsg_polygon_vlist_update(shape, NULL);
 * @endcode
 */
/** @{ */
/* @file bsg/polygon.h */

#ifndef BSG_POLYGON_H
#define BSG_POLYGON_H

#include "common.h"
#include "vmath.h"
#include "bu/color.h"
#include "bu/ptbl.h"
#include "bg/polygon_types.h"
#include "bsg/defines.h"
#include "bsg/payload.h"

__BEGIN_DECLS

/**
 * Polygon shape type constants.
 * These mirror the @c BV_POLYGON_* constants in @c bv/polygon.h so that
 * existing code can migrate without including bv headers.
 */
#define BSG_POLYGON_GENERAL   0
#define BSG_POLYGON_CIRCLE    1
#define BSG_POLYGON_ELLIPSE   2
#define BSG_POLYGON_RECTANGLE 3
#define BSG_POLYGON_SQUARE    4

/**
 * Update mode flags passed to bsg_polygon_update().
 * These mirror the @c BV_POLYGON_UPDATE_* constants in @c bv/polygon.h.
 */
#define BSG_POLYGON_UPDATE_DEFAULT         0
#define BSG_POLYGON_UPDATE_PROPS_ONLY      1
#define BSG_POLYGON_UPDATE_PT_SELECT       2
#define BSG_POLYGON_UPDATE_PT_SELECT_CLEAR 3
#define BSG_POLYGON_UPDATE_PT_MOVE         4
#define BSG_POLYGON_UPDATE_PT_APPEND       5


/* ---------------------------------------------------------------------- */
/* Payload lifecycle                                                        */
/* ---------------------------------------------------------------------- */

/**
 * Create a BSG polygon payload of shape @p type (one of BSG_POLYGON_*).
 *
 * The new payload owns an empty @c bg_polygon, has fill disabled, fill
 * color defaulting to blue, @c curr_contour_i = -1, and
 * @c curr_point_i = -1.
 *
 * Returns NULL on allocation failure.
 * Caller must free with bsg_payload_destroy().
 */
BSG_EXPORT extern struct bsg_payload *
bsg_payload_polygon_create(int type);


/* ---------------------------------------------------------------------- */
/* Shape type                                                               */
/* ---------------------------------------------------------------------- */

/**
 * Return the shape type of @p p (one of BSG_POLYGON_*).
 * Returns BSG_POLYGON_GENERAL if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_polygon_type_get(const struct bsg_payload *p);

/**
 * Set the shape type of @p p.
 * No-op if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_type_set(struct bsg_payload *p, int type);


/* ---------------------------------------------------------------------- */
/* Fill state                                                               */
/* ---------------------------------------------------------------------- */

/**
 * Return non-zero if interior fill is enabled for @p p.
 * Returns 0 if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern int
bsg_payload_polygon_fill_flag_get(const struct bsg_payload *p);

/**
 * Enable (non-zero) or disable (zero) interior fill for @p p.
 * No-op if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_fill_flag_set(struct bsg_payload *p, int flag);

/**
 * Copy the 2-D fill direction vector from @p p into @p out.
 * No-op if either argument is NULL or @p p is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_fill_dir_get(const struct bsg_payload *p, vect2d_t out);

/**
 * Set the 2-D fill direction vector of @p p.
 * No-op if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_fill_dir_set(struct bsg_payload *p, const vect2d_t dir);

/**
 * Return the fill-line spacing (model-space units) of @p p.
 * Returns 0.0 if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern fastf_t
bsg_payload_polygon_fill_delta_get(const struct bsg_payload *p);

/**
 * Set the fill-line spacing of @p p.
 * No-op if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_fill_delta_set(struct bsg_payload *p, fastf_t delta);

/**
 * Copy the fill color from @p p into @p out.
 * No-op if either argument is NULL or @p p is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_fill_color_get(const struct bsg_payload *p, struct bu_color *out);

/**
 * Set the fill color of @p p to @p c.
 * No-op if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_fill_color_set(struct bsg_payload *p, const struct bu_color *c);


/* ---------------------------------------------------------------------- */
/* Edit state                                                               */
/* ---------------------------------------------------------------------- */

/**
 * Return the currently-active contour index, or -1 if none.
 * Returns -1 if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern long
bsg_payload_polygon_curr_contour_get(const struct bsg_payload *p);

/**
 * Set the currently-active contour index.  Use -1 to clear.
 * No-op if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_curr_contour_set(struct bsg_payload *p, long idx);

/**
 * Return the currently-active point index within the active contour,
 * or -1 if none.
 * Returns -1 if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern long
bsg_payload_polygon_curr_point_get(const struct bsg_payload *p);

/**
 * Set the currently-active point index.  Use -1 to clear.
 * No-op if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_curr_point_set(struct bsg_payload *p, long idx);


/* ---------------------------------------------------------------------- */
/* Geometry                                                                 */
/* ---------------------------------------------------------------------- */

/**
 * Copy the polygon origin point from @p p into @p out.
 * The origin is used as the anchor for non-GENERAL polygon types during
 * interactive resizing.
 * No-op if either argument is NULL or @p p is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_origin_get(const struct bsg_payload *p, point_t out);

/**
 * Set the polygon origin point of @p p.
 * No-op if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_origin_set(struct bsg_payload *p, const point_t origin);

/**
 * Copy the view plane from @p p into @p out.
 * The view plane records the plane the polygon was created on so that
 * future 2-D edits can be projected back onto it.
 * No-op if either argument is NULL or @p p is the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_view_plane_get(const struct bsg_payload *p, plane_t out);

/**
 * Set the view plane of @p p.
 * No-op if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_view_plane_set(struct bsg_payload *p, const plane_t vp);

/**
 * Return the view-plane Z offset of @p p.
 * The offset allows moving the polygon "toward" and "away from" the viewer
 * relative to its creation plane.
 * Returns 0.0 if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern fastf_t
bsg_payload_polygon_vZ_get(const struct bsg_payload *p);

/**
 * Set the view-plane Z offset of @p p.
 * No-op if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_vZ_set(struct bsg_payload *p, fastf_t vZ);

/**
 * Return a mutable pointer to the @c bg_polygon embedded in @p p.
 *
 * Callers may modify the polygon directly.  After changes, call
 * bsg_polygon_vlist_update() to regenerate the node vlist.
 * Returns NULL if @p p is NULL or the wrong type.
 */
BSG_EXPORT extern struct bg_polygon *
bsg_payload_polygon_bg_get(struct bsg_payload *p);

/**
 * Replace the @c bg_polygon embedded in @p p with a deep copy of @p poly.
 *
 * The existing polygon data in @p p is freed before the copy is made.
 * No-op if @p p is NULL, the wrong type, or @p poly is NULL.
 */
BSG_EXPORT extern void
bsg_payload_polygon_bg_set(struct bsg_payload *p, const struct bg_polygon *poly);

/**
 * Deep-copy all fields from @p src into @p dst.
 *
 * The @c bg_polygon inside is cloned (not shared).  No-op if either
 * argument is NULL or the wrong type.
 */
BSG_EXPORT extern void
bsg_payload_polygon_cpy(struct bsg_payload *dst, const struct bsg_payload *src);


/* ---------------------------------------------------------------------- */
/* vlist generation                                                         */
/* ---------------------------------------------------------------------- */

/**
 * Regenerate the vlist for node @p n from its polygon payload.
 *
 * Clears any existing vlist on @p n.  Creates or destroys dashed child
 * nodes for polygon hole contours.  Creates or destroys the fill child
 * node if fill is enabled.
 *
 * @p vlfree  vlist chunk free-list.  If NULL, bsg_node_vlfree() is used.
 *
 * No-op if @p n is NULL or carries no polygon payload.
 */
BSG_EXPORT extern void
bsg_polygon_vlist_update(bsg_node *n, struct bu_list *vlfree);


/* ---------------------------------------------------------------------- */
/* Point editing                                                            */
/* ---------------------------------------------------------------------- */

/**
 * Append a new point to the current contour of the polygon in @p n.
 *
 * @p np       3-D position to project onto the polygon's view plane.
 * @p vlfree   vlist chunk free-list (may be NULL).
 *
 * Returns 0 on success, -1 if @p n carries no polygon payload, the
 * polygon is not GENERAL type, or no contour is selected.
 */
BSG_EXPORT extern int
bsg_polygon_append_pt(bsg_node *n, const point_t *np, struct bu_list *vlfree);

/**
 * Select the polygon point in @p n closest to @p cp.
 *
 * Sets @c curr_contour_i and @c curr_point_i in the polygon payload.
 * Regenerates the vlist.
 *
 * Returns 0 on success, -1 on failure.
 */
BSG_EXPORT extern int
bsg_polygon_select_pt(bsg_node *n, const point_t *cp, struct bu_list *vlfree);

/**
 * Clear the current-point selection on the polygon in @p n.
 * Sets @c curr_contour_i = -1 and @c curr_point_i = -1.
 */
BSG_EXPORT extern void
bsg_polygon_select_clear_pt(bsg_node *n, struct bu_list *vlfree);

/**
 * Move the currently-selected point in @p n to the projection of @p mp
 * on the polygon's view plane.
 *
 * Returns 0 on success, -1 on failure.
 */
BSG_EXPORT extern int
bsg_polygon_move_pt(bsg_node *n, const point_t *mp, struct bu_list *vlfree);

/**
 * Translate all points of the polygon in @p n by the vector
 * @c (cp - prev).
 *
 * @p cp    New cursor position.
 * @p prev  Previous cursor position.
 *
 * Returns 0 on success, -1 on failure.
 */
BSG_EXPORT extern int
bsg_polygon_move(bsg_node *n, const point_t *cp, const point_t *prev,
		 struct bu_list *vlfree);


/* ---------------------------------------------------------------------- */
/* Shape-specific updates                                                   */
/* ---------------------------------------------------------------------- */

/**
 * Recompute the circle polygon in @p n from its origin and @p cp.
 *
 * @p pixel_size  Diagonal length of a screen pixel in model space, used
 *                to choose an appropriate segment count.
 *
 * Returns 1 if the polygon was updated, 0 on failure.
 */
BSG_EXPORT extern int
bsg_polygon_update_circle(bsg_node *n, const point_t *cp, fastf_t pixel_size,
			  struct bu_list *vlfree);

/**
 * Recompute the ellipse polygon in @p n from its origin and @p cp.
 *
 * @p pixel_size  Diagonal length of a screen pixel in model space.
 *
 * Returns 1 if updated, 0 on failure.
 */
BSG_EXPORT extern int
bsg_polygon_update_ellipse(bsg_node *n, const point_t *cp, fastf_t pixel_size,
			   struct bu_list *vlfree);

/**
 * Recompute the rectangle polygon in @p n from its origin and @p cp.
 *
 * Returns 1 if updated, 0 on failure.
 */
BSG_EXPORT extern int
bsg_polygon_update_rectangle(bsg_node *n, const point_t *cp,
			     struct bu_list *vlfree);

/**
 * Recompute the square polygon in @p n from its origin and @p cp.
 *
 * Returns 1 if updated, 0 on failure.
 */
BSG_EXPORT extern int
bsg_polygon_update_square(bsg_node *n, const point_t *cp,
			  struct bu_list *vlfree);


/* ---------------------------------------------------------------------- */
/* Closest-polygon selection                                                */
/* ---------------------------------------------------------------------- */

/**
 * Return the @c bsg_node from @p objs whose polygon edge is closest to
 * @p cp.
 *
 * Only nodes carrying a polygon payload are considered.
 * Returns NULL if @p objs is NULL, empty, or no polygon node is found.
 */
BSG_EXPORT extern bsg_node *
bsg_polygon_select_closest(const struct bu_ptbl *objs, const point_t *cp);


/* ---------------------------------------------------------------------- */
/* CSG (boolean) operations                                                 */
/* ---------------------------------------------------------------------- */

/**
 * Apply boolean clip operation @p op to @p target using @p stencil.
 *
 * Both nodes must carry a polygon payload.
 *
 * @p view_scale  View scale used by the overlap-detection pre-check.
 *               Pass 0.0 to skip the pre-check.
 * @p vlfree      vlist chunk free-list (may be NULL).
 *
 * Returns 1 if @p target was modified, 0 otherwise.
 */
BSG_EXPORT extern int
bsg_polygon_csg(bsg_node *target, bsg_node *stencil, bg_clip_t op,
		fastf_t view_scale, struct bu_list *vlfree);


/* ---------------------------------------------------------------------- */
/* Fill segment geometry                                                    */
/* ---------------------------------------------------------------------- */

/**
 * Compute fill line segments for @p poly clipped to its interior.
 *
 * This is a pure-geometry helper; it uses the libbg clipping routines
 * and the 2-D projection defined by @p vp.
 *
 * @p poly          Source polygon (read-only; must have >= 3 points in
 *                  the first contour).
 * @p vp            View plane used to project contours to 2-D.
 * @p line_slope    2-D fill direction vector.
 * @p line_spacing  Perpendicular spacing between fill lines.
 */
BSG_EXPORT extern struct bg_polygon *
bsg_polygon_fill_segments(struct bg_polygon *poly, plane_t *vp,
			  vect2d_t line_slope, fastf_t line_spacing);

/**
 * Suggest a fill-line spacing for @p p based on contour bounding box.
 *
 * Returns a suggested delta > 0 on success, 0 if no estimate could be
 * made (currently a stub).
 */
BSG_EXPORT extern int
bsg_polygon_calc_fdelta(struct bsg_payload *p);


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
