/*                        P O L Y G O N . H
 * BRL-CAD
 *
 * Copyright (c) 2004-2026 United States Government as represented by
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

/*----------------------------------------------------------------------*/
/** @addtogroup bsg_polygon
 *
 *  @brief Functions for working with polygons.
 *
 *  Canonical home; bv/polygon.h is a backward-compatibility bridge.
 */
/* @file bsg/polygon.h */
/** @{ */

#ifndef BSG_POLYGON_H
#define BSG_POLYGON_H

#include "common.h"
#include "vmath.h"
#include "bu/color.h"
#include "bsg/defines.h"
#include "bg/polygon.h"
#include "bg/polygon_types.h"

__BEGIN_DECLS

/* View polygon logic and types */

#define BV_POLYGON_GENERAL 0
#define BV_POLYGON_CIRCLE 1
#define BV_POLYGON_ELLIPSE 2
#define BV_POLYGON_RECTANGLE 3
#define BV_POLYGON_SQUARE 4

struct bsg_polygon {
    int                 type;
    int                 fill_flag;         /* set to shade the interior */
    vect2d_t            fill_dir;
    fastf_t             fill_delta;
    struct bu_color     fill_color;
    long                curr_contour_i;
    long                curr_point_i;
    point_t             origin_point;      /* For non-general polygons  */

    /* We stash the view plane creation, so we know how to return
     * to it for future 2D alterations */
    plane_t             vp;

    /* Offset of polygon plane from the view plane.  Allows for moving
     * the polygon "towards" and "away from" the viewer. */
    fastf_t vZ;

    /* Actual polygon info */
    struct bg_polygon polygon;

    /* Arbitrary data */
    void *u_data;
};

/* Given a polygon, create a scene object */
BV_EXPORT extern struct bsg_node *bsg_create_polygon_obj(struct bsg_view *v, int flags, struct bsg_polygon *p);

/* Return the polygon payload attached to a node, or the legacy internal
 * polygon pointer when called on pre-D1 nodes. */
BV_EXPORT extern struct bsg_polygon *bsg_node_polygon(const struct bsg_node *node);

/* Creates a scene object with a default polygon */
BV_EXPORT extern struct bsg_node *bsg_create_polygon(struct bsg_view *v, int flags, int type, point_t *fp);

/* Various update modes have similar logic - we pass in the flags to the update
 * routine to enable/disable specific portions of the overall flow. */
#define BV_POLYGON_UPDATE_DEFAULT 0
#define BV_POLYGON_UPDATE_PROPS_ONLY 1
#define BV_POLYGON_UPDATE_PT_SELECT 2
#define BV_POLYGON_UPDATE_PT_SELECT_CLEAR 3
#define BV_POLYGON_UPDATE_PT_MOVE 4
#define BV_POLYGON_UPDATE_PT_APPEND 5
BV_EXPORT extern int bsg_update_polygon(struct bsg_node *s, struct bsg_view *v, int utype);

/* Update just the scene obj vlist, without altering the source polygon */
BV_EXPORT extern void bsg_polygon_vlist(struct bsg_node *s);

/* Find the closest polygon obj to a point (caller-supplied ptbl) */
BV_EXPORT extern struct bsg_node *bsg_select_polygon(struct bu_ptbl *objs, point_t *cp);

/* Phase A0/A2: typed variant - walks all BSG view-scope nodes visible to v
 * and returns the polygon object closest to cp. */
BV_EXPORT extern struct bsg_node *bsg_view_select_polygon(struct bsg_view *v, point_t *cp);

BV_EXPORT extern int bsg_move_polygon(struct bsg_node *s, point_t *cp, point_t *pp);
BV_EXPORT extern struct bsg_node *bsg_dup_view_polygon(const char *nname, struct bsg_node *s);

/* Copy a bv polygon.  Note that this also performs a
 * view sync - if the user is copying the polygon into
 * another view, they will have to update the output's
 * bsg_view to match their target view. */
BV_EXPORT extern void bsg_polygon_cpy(struct bsg_polygon *dest , struct bsg_polygon *src);

/* Calculate a suggested default fill delta based on the polygon structure.  The
 * idea is to try and strike a balance between line count and having enough fill
 * lines to highlight interior holes. */
BV_EXPORT extern int bsg_polygon_calc_fdelta(struct bsg_polygon *p);

BV_EXPORT extern struct bg_polygon *
bsg_polygon_fill_segments(struct bg_polygon *poly, plane_t *vp, vect2d_t line_slope, fastf_t line_spacing);

/* For all polygon bsg_scene_objs in the objs table, apply the specified boolean
 * op using p and replace the original polygon geometry in objs with the results. */
BV_EXPORT extern int bsg_polygon_csg(struct bsg_node *target, struct bsg_node *stencil, bg_clip_t op);

__END_DECLS

#endif  /* BSG_POLYGON_H */
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
