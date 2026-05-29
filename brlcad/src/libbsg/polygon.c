/*                     P O L Y G O N  . C
 * BRL-CAD
 *
 * Copyright (c) 2020-2026 United States Government as represented by
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
/** @file polygons.c
 *
 * Utility functions for working with polygons in a view context.
 *
 */

#include "common.h"
#include <string.h>
#include "vmath.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "bn/mat.h"
#include "bn/tol.h"
#include "bsg/vlist.h"
#include "bsg/defines.h"
#include "bsg/pick.h"
#include "bsg/util.h"
#include "bg/lseg.h"
#include "bg/plane.h"
#include "bg/polygon.h"
#include "bsg/defines.h"
#include "bsg/polygon.h"
#include "bsg/payload_typed.h"
#include "bsg/snap.h"
#include "bsg/node_private.h"

struct bsg_polygon *
bsg_node_polygon(const struct bsg_node *node)
{
    if (!node)
	return NULL;
    if (node->pl && node->pl->pl_type == BSG_PL_POLYGON)
	return bsg_payload_polygon_get(node->pl);
    return (struct bsg_polygon *)node->s_i_data;
}

void
bsg_polygon_contour(struct bsg_node *s, struct bg_poly_contour *c, int curr_c, int curr_i, int do_pnt)
{
    if (!s || !c || !s->s_v)
	return;

    if (do_pnt) {
	BSG_ADD_VLIST(s->vlfree, &s->s_vlist, c->point[0], BSG_VLIST_POINT_DRAW);
	return;
    }

    BSG_ADD_VLIST(s->vlfree, &s->s_vlist, c->point[0], BSG_VLIST_LINE_MOVE);
    for (size_t i = 0; i < c->num_points; i++) {
	BSG_ADD_VLIST(s->vlfree, &s->s_vlist, c->point[i], BSG_VLIST_LINE_DRAW);
    }
    if (!c->open)
	BSG_ADD_VLIST(s->vlfree, &s->s_vlist, c->point[0], BSG_VLIST_LINE_DRAW);

    if (curr_c && curr_i >= 0) {
	point_t psize;
	VSET(psize, 10, 0, 0);
	BSG_ADD_VLIST(s->vlfree, &s->s_vlist, c->point[curr_i], BSG_VLIST_LINE_MOVE);
	BSG_ADD_VLIST(s->vlfree, &s->s_vlist, psize, BSG_VLIST_POINT_SIZE);
	BSG_ADD_VLIST(s->vlfree, &s->s_vlist, c->point[curr_i], BSG_VLIST_POINT_DRAW);
    }
}

void
bsg_fill_polygon(struct bsg_node *s)
{
    if (!s)
	return;

    // free old fill, if present
    struct bsg_node *fobj = bsg_find_child(s, "*fill*");
    if (fobj)
	bsg_obj_put(fobj);

    struct bsg_polygon *p = bsg_node_polygon(s);

    if (!p || !p->polygon.num_contours)
	return;

    if (!p->polygon.contour || p->polygon.contour[0].open)
	return;

    if (p->fill_delta < BN_TOL_DIST)
	return;

    struct bg_polygon *fill = bsg_polygon_fill_segments(&p->polygon, &p->vp, p->fill_dir, p->fill_delta);
    if (!fill)
	return;

    // Got fill, create lines
    fobj = bsg_obj_get_child(s);
    bu_vls_printf(&fobj->s_name, ":fill");
    fobj->s_os->s_line_width = 1;
    fobj->s_soldash = 0;
    bu_color_to_rgb_chars(&p->fill_color, fobj->s_color);
    for (size_t i = 0; i < fill->num_contours; i++) {
	bsg_polygon_contour(fobj, &fill->contour[i], 0, -1, 0);
    }
}

void
bsg_polygon_vlist(struct bsg_node *s)
{
    if (!s)
	return;

    // Reset obj drawing data
    if (BU_LIST_IS_INITIALIZED(&s->s_vlist)) {
	BSG_FREE_VLIST(s->vlfree, &s->s_vlist);
    }
    BU_LIST_INIT(&(s->s_vlist));

    struct bsg_polygon *p = bsg_node_polygon(s);
    int type = p->type;

    // Clear any old holes
    for (size_t i = 0; i < BU_PTBL_LEN(&s->children); i++) {
	struct bsg_node *s_c = (struct bsg_node *)BU_PTBL_GET(&s->children, i);
	bsg_obj_put(s_c);
    }

    for (size_t i = 0; i < p->polygon.num_contours; ++i) {
	/* Draw holes using segmented lines.  Since vlists don't have a style
	 * command for that, we make child shape nodes for the holes. */
	size_t pcnt = p->polygon.contour[i].num_points;
	int do_pnt = 0;
	if (pcnt == 1)
	    do_pnt = 1;
	if (type == BSG_POLYGON_CIRCLE && pcnt == 3)
	    do_pnt = 1;
	if (type == BSG_POLYGON_ELLIPSE && pcnt == 4)
	    do_pnt = 1;
	if (type == BSG_POLYGON_RECTANGLE) {
	    if (NEAR_ZERO(DIST_PNT_PNT_SQ(p->polygon.contour[0].point[0], p->polygon.contour[0].point[1]), SMALL_FASTF) &&
		    NEAR_ZERO(DIST_PNT_PNT_SQ(p->polygon.contour[0].point[0], p->polygon.contour[0].point[2]), SMALL_FASTF))
		do_pnt = 1;
	}
	if (type == BSG_POLYGON_SQUARE) {
	    if (NEAR_ZERO(DIST_PNT_PNT_SQ(p->polygon.contour[0].point[0], p->polygon.contour[0].point[1]), SMALL_FASTF) &&
		    NEAR_ZERO(DIST_PNT_PNT_SQ(p->polygon.contour[0].point[0], p->polygon.contour[0].point[2]), SMALL_FASTF))
		do_pnt = 1;
	}

	if (p->polygon.hole[i]) {
	    struct bsg_node *s_c = bsg_obj_get_child(s);
	    s_c->s_soldash = 1;
	    s_c->s_color[0] = s->s_color[0];
	    s_c->s_color[1] = s->s_color[1];
	    s_c->s_color[2] = s->s_color[2];
	    s_c->s_v = s->s_v;
	    bsg_polygon_contour(s_c, &p->polygon.contour[i], ((int)i == p->curr_contour_i), p->curr_point_i, do_pnt);
	    bu_ptbl_ins(&s->children, (long *)s_c);
	    continue;
	}

	bsg_polygon_contour(s, &p->polygon.contour[i], ((int)i == p->curr_contour_i), p->curr_point_i, do_pnt);
    }

    if (p->fill_flag) {
	bsg_fill_polygon(s);
    } else {
	struct bsg_node *fobj = bsg_find_child(s, "*fill*");
	if (fobj)
	    bsg_obj_put(fobj);

    }
}

/* pl_pick hook for BSG_PL_POLYGON payloads: find the nearest polygon vertex
 * to a model-coordinate sample point and fill a pick record for that vertex.
 */
static int
_bsg_polygon_pl_pick(struct bsg_payload *pl, const point_t sample,
		     struct bsg_pick_record *out)
{
    if (!pl || !out)
	return -1;
    struct bsg_polygon *p = pl->pl.polygon;
    if (!p || p->type != BSG_POLYGON_GENERAL)
	return -1;

    plane_t zpln;
    HMOVE(zpln, p->vp);
    zpln[3] += p->vZ;
    fastf_t fx, fy;
    point_t sample_pt;
    VMOVE(sample_pt, sample);
    bg_plane_closest_pt(&fx, &fy, &zpln, &sample_pt);
    point_t m_pt;
    bg_plane_pt_at(&m_pt, &zpln, fx, fy);

    double dist_min_sq = DBL_MAX;
    long closest_i = -1, closest_contour = -1;
    for (size_t j = 0; j < p->polygon.num_contours; j++) {
	struct bg_poly_contour *c = &p->polygon.contour[j];
	for (size_t i = 0; i < c->num_points; i++) {
	    double dcand = DIST_PNT_PNT_SQ(c->point[i], m_pt);
	    if (dcand < dist_min_sq) {
		closest_i = (long)i;
		closest_contour = (long)j;
		dist_min_sq = dcand;
	    }
	}
    }

    if (closest_i < 0)
	return 0;

    /* Fill the pick record with the closest vertex location.
     * pr_primitive_id encodes the contour index, pr_subelement_id the
     * point index within that contour so callers can unambiguously locate
     * the vertex.  pr_hit_dist carries the Euclidean distance. */
    out->pr_hit_dist = sqrt(dist_min_sq);
    out->pr_primitive_id = (int)closest_contour;
    out->pr_subelement_id = (int)closest_i;
    /* pr_node and path fields must be filled by the caller from context. */
    out->pr_node = NULL;
    bu_vls_init(&out->pr_source_path);
    bu_vls_init(&out->pr_instance_path);
    out->pr_screen_x = 0;
    out->pr_screen_y = 0;
    out->pr_view = NULL;
    return 1;
}

struct bsg_node *
bsg_create_polygon_obj(struct bsg_view *v, int flags, struct bsg_polygon *p)
{
    struct bsg_node *s = NULL;
    if (flags & BSG_OBJ_VIEW) {
	/* Phase V3: view-only polygon producers now attach directly under
	 * BSG view-scope nodes rather than relying on ptbl registration +
	 * bridge proxy nodes. */
	s = bsg_view_obj_overlay_create(v, NULL, (flags & BSG_OBJ_LOCAL) ? 1 : 0);
    } else {
	s = bsg_obj_get(v, flags);
    }
    if (!s)
	return NULL;
    s->s_type_flags |= BSG_SHAPE_POLYGONS;
    s->s_type_flags |= BSG_SHAPE_VIEWONLY;

    // Construct the plane
    bsg_view_plane(&p->vp, v);

    s->s_os->s_line_width = 1;
    s->s_color[0] = 255;
    s->s_color[1] = 255;
    s->s_color[2] = 0;
    s->s_i_data = (void *)p;
    {
	struct bsg_payload *_pl = bsg_payload_polygon_create(p);
	if (_pl)
	    _pl->pl_pick = _bsg_polygon_pl_pick;
	bsg_node_set_payload(s, _pl);
    }
    s->s_update_callback = &bsg_update_polygon;

    /* Have new polygon, now update shape payload vlist */
    bsg_polygon_vlist(s);

    /* updated */
    s->s_changed++;

    return s;
}

struct bsg_node *
bsg_create_polygon(struct bsg_view *v, int flags, int type, point_t *fp)
{
    struct bsg_polygon *p;
    BU_GET(p, struct bsg_polygon);
    p->type = type;
    p->curr_contour_i = -1;
    p->curr_point_i = -1;

    // Set default fill color to blue
    unsigned char frgb[3] = {0, 0, 255};
    bu_color_from_rgb_chars(&p->fill_color, frgb);

    // Construct the plane
    bsg_view_plane(&p->vp, v);

    // Construct closest point to fp on plane
    fastf_t fx, fy;
    bg_plane_closest_pt(&fx, &fy, &p->vp, fp);
    point_t m_pt;
    bg_plane_pt_at(&m_pt, &p->vp, fx, fy);

    // This is now the origin point
    VMOVE(p->origin_point, m_pt);

    int pcnt = 1;
    if (type == BSG_POLYGON_CIRCLE)
	pcnt = 3;
    if (type == BSG_POLYGON_ELLIPSE)
	pcnt = 4;
    if (type == BSG_POLYGON_RECTANGLE)
	pcnt = 4;
    if (type == BSG_POLYGON_SQUARE)
	pcnt = 4;

    p->polygon.num_contours = 1;
    p->polygon.hole = (int *)bu_calloc(1, sizeof(int), "hole");
    p->polygon.contour = (struct bg_poly_contour *)bu_calloc(1, sizeof(struct bg_poly_contour), "contour");
    p->polygon.contour[0].num_points = pcnt;
    p->polygon.contour[0].open = 0;
    p->polygon.contour[0].point = (point_t *)bu_calloc(pcnt, sizeof(point_t), "point");
    p->polygon.hole[0] = 0;
    for (int i = 0; i < pcnt; i++) {
	VMOVE(p->polygon.contour[0].point[i], m_pt);
    }

    // Only the general polygon isn't closed out of the gate
    if (type == BSG_POLYGON_GENERAL)
	p->polygon.contour[0].open = 1;

    // Have polygon, now make shape node
    struct bsg_node *s = bsg_create_polygon_obj(v, flags, p);
    if (!s)
	BU_PUT(p, struct bsg_polygon);
    return s;
}

void
bsg_polygon_cpy(struct bsg_polygon *dest, struct bsg_polygon *src)
{
    if (!src || !dest)
	return;

    dest->type = src->type;
    dest->fill_flag = src->fill_flag;
    V2MOVE(dest->fill_dir, src->fill_dir);
    dest->fill_delta = src->fill_delta;
    BU_COLOR_CPY(&dest->fill_color, &src->fill_color);
    dest->curr_contour_i = src->curr_contour_i;
    dest->curr_point_i = src->curr_point_i;
    VMOVE(dest->origin_point, src->origin_point);
    HMOVE(dest->vp, src->vp);
    dest->vZ = src->vZ;
    bg_polygon_free(&dest->polygon);
    bg_polygon_cpy(&dest->polygon, &src->polygon);
    dest->u_data = src->u_data;
}

int
bsg_append_polygon_pt(struct bsg_node *s, point_t *np)
{
    struct bsg_polygon *p = bsg_node_polygon(s);
    if (p->type != BSG_POLYGON_GENERAL)
	return -1;

    if (p->curr_contour_i < 0)
	return -1;

    // Construct closest point to np on plane
    fastf_t fx, fy;
    bg_plane_closest_pt(&fx, &fy, &p->vp, np);
    point_t m_pt;
    bg_plane_pt_at(&m_pt, &p->vp, fx, fy);

    struct bg_poly_contour *c = &p->polygon.contour[p->curr_contour_i];
    c->num_points++;
    c->point = (point_t *)bu_realloc(c->point,c->num_points * sizeof(point_t), "realloc contour points");
    VMOVE(c->point[c->num_points-1], m_pt);

    /* Have new polygon, now update shape payload vlist */
    bsg_polygon_vlist(s);

    /* Updated */
    s->s_changed++;

    return 0;
}

// NOTE: This is a naive brute force search for the closest projected edge at
// the moment...  Would be better for repeated sampling of relatively static
// scenes to build an RTree first...
struct bsg_node *
bsg_select_polygon(struct bu_ptbl *objs, point_t *cp)
{
    if (!objs)
	return NULL;

    struct bsg_node *closest = NULL;
    double dist_min_sq = DBL_MAX;

    for (size_t i = 0; i < BU_PTBL_LEN(objs); i++) {
	struct bsg_node *s = (struct bsg_node *)BU_PTBL_GET(objs, i);
	if (s->s_type_flags & BSG_SHAPE_POLYGONS) {
	    struct bsg_polygon *p = bsg_node_polygon(s);
	    // Because we're working in 2D orthogonal when processing polygons,
	    // the specific value of Z for each individual polygon isn't
	    // relevant - we want to find the closest edge in the projected
	    // view plane.  Accordingly, always construct the test point using
	    // whatever the current vZ is for the polygon being tested.
	    plane_t zpln;
	    HMOVE(zpln, p->vp);
	    zpln[3] += p->vZ;
	    fastf_t fx, fy;
	    bg_plane_closest_pt(&fx, &fy, &zpln, cp);
	    point_t m_pt;
	    bg_plane_pt_at(&m_pt, &zpln, fx, fy);

	    for (size_t j = 0; j < p->polygon.num_contours; j++) {
		struct bg_poly_contour *c = &p->polygon.contour[j];
		for (size_t k = 0; k < c->num_points; k++) {
		    double dcand;
		    if (k < c->num_points - 1) {
			dcand = bg_distsq_lseg3_pt(NULL, c->point[k], c->point[k+1], m_pt);
		    } else {
			dcand = bg_distsq_lseg3_pt(NULL, c->point[k], c->point[0], m_pt);
		    }
		    if (dcand < dist_min_sq) {
			dist_min_sq = dcand;
			closest = s;
		    }
		}
	    }
	}
    }

    return closest;
}

/* Phase A0/A2 (drawing_stack_modernization): typed version of bsg_select_polygon
 * that uses bsg_view_obj_visit internally rather than a caller-supplied ptbl.
 * Walks all BSG view-scope nodes visible to v, finds the polygon object whose
 * edge is closest to cp, and returns it. */
struct _bv_poly_select_ptbl {
    struct bu_ptbl objs;
};

static int
_bv_poly_collect_cb(struct bsg_node *obj, void *data)
{
    struct _bv_poly_select_ptbl *s = (struct _bv_poly_select_ptbl *)data;
    if (obj->s_type_flags & BSG_SHAPE_POLYGONS)
	bu_ptbl_ins(&s->objs, (long *)obj);
    return 1;
}

struct bsg_node *
bsg_view_select_polygon(struct bsg_view *v, point_t *cp)
{
    if (!v || !cp)
	return NULL;

    struct _bv_poly_select_ptbl state;
    bu_ptbl_init(&state.objs, 8, "bsg_view_select_polygon objs");
    bsg_view_obj_visit(v, BSG_VIEW_OBJ_SCOPE_ALL, _bv_poly_collect_cb, &state);
    struct bsg_node *result = bsg_select_polygon(&state.objs, cp);
    bu_ptbl_free(&state.objs);
    return result;
}

int
bsg_select_polygon_pt(struct bsg_node *s, point_t *cp)
{
    struct bsg_polygon *p = bsg_node_polygon(s);
    if (p->type != BSG_POLYGON_GENERAL)
	return -1;

    plane_t zpln;
    HMOVE(zpln, p->vp);
    zpln[3] += p->vZ;
    fastf_t fx, fy;
    bg_plane_closest_pt(&fx, &fy, &zpln, cp);
    point_t m_pt;
    bg_plane_pt_at(&m_pt, &zpln, fx, fy);

    // If a contour is selected, restrict our closest point candidates to
    // that contour's points
    double dist_min_sq = DBL_MAX;
    long closest_ind = -1;
    long closest_contour = -1;
    if (p->curr_contour_i >= 0) {
	struct bg_poly_contour *c = &p->polygon.contour[p->curr_contour_i];
	closest_contour = p->curr_contour_i;
	for (size_t i = 0; i < c->num_points; i++) {
	    double dcand = DIST_PNT_PNT_SQ(c->point[i], m_pt);
	    if (dcand < dist_min_sq) {
		closest_ind = (long)i;
		dist_min_sq = dcand;
	    }
	}
    } else {
	for (size_t j = 0; j < p->polygon.num_contours; j++) {
	    struct bg_poly_contour *c = &p->polygon.contour[j];
	    for (size_t i = 0; i < c->num_points; i++) {
		double dcand = DIST_PNT_PNT_SQ(c->point[i], m_pt);
		if (dcand < dist_min_sq) {
		    closest_ind = (long)i;
		    closest_contour = (long)j;
		    dist_min_sq = dcand;
		}
	    }
	}
    }

    p->curr_point_i = closest_ind;
    p->curr_contour_i = closest_contour;

    /* Have new polygon, now update shape payload vlist */
    bsg_polygon_vlist(s);

    /* Updated */
    s->s_changed++;

    return 0;
}


void
bsg_select_clear_polygon_pt(struct bsg_node *s)
{
    if (!s)
	return;

    if (s->s_type_flags & BSG_SHAPE_POLYGONS) {
	struct bsg_polygon *p = bsg_node_polygon(s);
	p->curr_point_i = -1;
	p->curr_contour_i = -1;
	bsg_polygon_vlist(s);
	/* Updated */
	s->s_changed++;
    }
}


int
bsg_move_polygon(struct bsg_node *s, point_t *cp, point_t *prev_point)
{
    fastf_t pfx, pfy, fx, fy;
    struct bsg_polygon *p = bsg_node_polygon(s);

    plane_t zpln;
    HMOVE(zpln, p->vp);
    zpln[3] += p->vZ;
    bg_plane_closest_pt(&pfx, &pfy, &zpln, prev_point);
    bg_plane_closest_pt(&fx, &fy, &zpln, cp);
    point_t pm_pt, m_pt;
    bg_plane_pt_at(&pm_pt, &p->vp, pfx, pfy);
    bg_plane_pt_at(&m_pt, &p->vp, fx, fy);
    vect_t v_mv;
    VSUB2(v_mv, m_pt, pm_pt);

    for (size_t j = 0; j < p->polygon.num_contours; j++) {
	struct bg_poly_contour *c = &p->polygon.contour[j];
	for (size_t i = 0; i < c->num_points; i++) {
	    VADD2(c->point[i], c->point[i], v_mv);
	}
    }

    /* Have new polygon, now update shape payload vlist */
    bsg_polygon_vlist(s);

    // Shift the origin point.
    VADD2(p->origin_point, p->origin_point, v_mv);

    /* Updated */
    s->s_changed++;

    return 0;
}

int
bsg_move_polygon_pt(struct bsg_node *s, point_t *mp)
{
    struct bsg_polygon *p = bsg_node_polygon(s);
    if (p->type != BSG_POLYGON_GENERAL)
	return -1;

    // Need to have a point selected before we can move
    if (p->curr_point_i < 0 || p->curr_contour_i < 0)
	return -1;

    fastf_t fx, fy;
    plane_t zpln;
    HMOVE(zpln, p->vp);
    zpln[3] += p->vZ;
    bg_plane_closest_pt(&fx, &fy, &zpln, mp);
    point_t m_pt;
    bg_plane_pt_at(&m_pt, &zpln, fx, fy);

    struct bg_poly_contour *c = &p->polygon.contour[p->curr_contour_i];
    VMOVE(c->point[p->curr_point_i], m_pt);

    /* Have new polygon, now update shape payload vlist */
    bsg_polygon_vlist(s);

    /* Updated */
    s->s_changed++;

    return 0;
}

int
bsg_update_polygon_circle(struct bsg_node *s, point_t *cp, fastf_t pixel_size)
{
    struct bsg_polygon *p = bsg_node_polygon(s);

    fastf_t curr_fx, curr_fy;
    fastf_t r, arc;
    int nsegs, n;

    fastf_t pfx, pfy, fx, fy;
    plane_t zpln;
    HMOVE(zpln, p->vp);
    zpln[3] += p->vZ;
    bg_plane_closest_pt(&fx, &fy, &zpln, cp);
    bg_plane_closest_pt(&pfx, &pfy, &zpln, &p->origin_point);

    point_t pcp;
    bg_plane_pt_at(&pcp, &zpln, fx, fy);

    r = DIST_PNT_PNT(pcp, p->origin_point);

    /* use a variable number of segments based on the size of the
     * circle being created so small circles have few segments and
     * large ones are nice and smooth.
     */
    nsegs = M_PI_2 * r / pixel_size;
    if (nsegs < 32)
	nsegs = 32;

    struct bg_polygon gp;
    struct bg_polygon *gpp = &gp;
    gpp->num_contours = 1;
    gpp->hole = (int *)bu_calloc(1, sizeof(int), "hole");;
    gpp->contour = (struct bg_poly_contour *)bu_calloc(1, sizeof(struct bg_poly_contour), "contour");
    gpp->contour[0].num_points = nsegs;
    gpp->contour[0].open = 0;
    gpp->contour[0].point = (point_t *)bu_calloc(nsegs, sizeof(point_t), "point");

    arc = 360.0 / nsegs;
    for (n = 0; n < nsegs; ++n) {
	fastf_t ang = n * arc;

	curr_fx = cos(ang*DEG2RAD) * r + pfx;
	curr_fy = sin(ang*DEG2RAD) * r + pfy;
	point_t v_pt;
	bg_plane_pt_at(&v_pt, &p->vp, curr_fx, curr_fy);
	VMOVE(gpp->contour[0].point[n], v_pt);
    }

    bg_polygon_free(&p->polygon);

    /* Not doing a struct copy to avoid overwriting other properties. */
    p->polygon.num_contours = gp.num_contours;
    p->polygon.hole = gp.hole;
    p->polygon.contour = gp.contour;

    /* Have new polygon, now update shape payload vlist */
    bsg_polygon_vlist(s);

    /* Updated */
    s->s_changed++;

    return 1;
}

int
bsg_update_polygon_ellipse(struct bsg_node *s, point_t *cp, fastf_t pixel_size)
{
    struct bsg_polygon *p = bsg_node_polygon(s);

    /* use a variable number of segments based on the size of the
     * circle being created so small circles have few segments and
     * large ones are nice and smooth.  select a chord length that
     * results in segments approximately 4 pixels in length.
     *
     * circumference / 4 = PI * diameter / 4
     *
     */

    fastf_t r = DIST_PNT_PNT(*cp, p->origin_point);

    /* use a variable number of segments based on the size of the
     * circle being created so small circles have few segments and
     * large ones are nice and smooth.
     */
    int nsegs = M_PI_2 * r / pixel_size;
    if (nsegs < 32)
	nsegs = 32;

    fastf_t pfx, pfy, fx, fy;
    plane_t zpln;
    HMOVE(zpln, p->vp);
    zpln[3] += p->vZ;
    bg_plane_closest_pt(&fx, &fy, &zpln, cp);
    bg_plane_closest_pt(&pfx, &pfy, &zpln, &p->origin_point);

    fastf_t a, b, arc;
    point_t pv_pt;
    point_t ellout;
    point_t A, B;

    VSET(pv_pt, pfx, pfy, 0);
    a = fx - pfx;
    b = fy - pfy;

    /*
     * For angle alpha, compute surface point as
     *
     * V + cos(alpha) * A + sin(alpha) * B
     *
     * note that sin(alpha) is cos(90-alpha).
     */

    VSET(A, a, 0, 0);
    VSET(B, 0, b, 0);

    struct bg_polygon gp;
    struct bg_polygon *gpp = &gp;
    gpp->num_contours = 1;
    gpp->hole = (int *)bu_calloc(1, sizeof(int), "hole");;
    gpp->contour = (struct bg_poly_contour *)bu_calloc(1, sizeof(struct bg_poly_contour), "contour");
    gpp->contour[0].num_points = nsegs;
    gpp->contour[0].open = 0;
    gpp->contour[0].point = (point_t *)bu_calloc(nsegs, sizeof(point_t), "point");

    arc = 360.0 / nsegs;
    for (int n = 0; n < nsegs; ++n) {
	fastf_t cosa = cos(n * arc * DEG2RAD);
	fastf_t sina = sin(n * arc * DEG2RAD);

	VJOIN2(ellout, pv_pt, cosa, A, sina, B);

	// Use the polygon's plane for actually adding the points
	bg_plane_pt_at(&gpp->contour[0].point[n], &zpln, ellout[0], ellout[1]);
    }

    bg_polygon_free(&p->polygon);

    /* Not doing a struct copy to avoid overwriting other properties. */
    p->polygon.num_contours = gp.num_contours;
    p->polygon.hole = gp.hole;
    p->polygon.contour = gp.contour;

    /* Have new polygon, now update shape payload vlist */
    bsg_polygon_vlist(s);

    /* Updated */
    s->s_changed++;

    return 1;
}

int
bsg_update_polygon_rectangle(struct bsg_node *s, point_t *cp)
{
    struct bsg_polygon *p = bsg_node_polygon(s);

    fastf_t pfx, pfy, fx, fy;
    plane_t zpln;
    HMOVE(zpln, p->vp);
    zpln[3] += p->vZ;
    bg_plane_closest_pt(&pfx, &pfy, &zpln, &p->origin_point);
    bg_plane_closest_pt(&fx, &fy, &zpln, cp);

    // Use the polygon's plane for actually adjusting the points
    bg_plane_pt_at(&p->polygon.contour[0].point[0], &zpln, pfx, pfy);
    bg_plane_pt_at(&p->polygon.contour[0].point[1], &zpln, pfx, fy);
    bg_plane_pt_at(&p->polygon.contour[0].point[2], &zpln, fx, fy);
    bg_plane_pt_at(&p->polygon.contour[0].point[3], &zpln, fx, pfy);

    p->polygon.contour[0].open = 0;

    /* Polygon updated, now update shape payload vlist */
    bsg_polygon_vlist(s);

    /* Updated */
    s->s_changed++;

    return 1;
}

int
bsg_update_polygon_square(struct bsg_node *s, point_t *cp)
{
    struct bsg_polygon *p = bsg_node_polygon(s);

    fastf_t pfx, pfy, fx, fy;
    plane_t zpln;
    HMOVE(zpln, p->vp);
    zpln[3] += p->vZ;
    bg_plane_closest_pt(&pfx, &pfy, &zpln, &p->origin_point);
    bg_plane_closest_pt(&fx, &fy, &zpln, cp);

    fastf_t dx = fx - pfx;
    fastf_t dy = fy - pfy;

    if (fabs(dx) > fabs(dy)) {
	if (dy < 0.0)
	    fy = pfy - fabs(dx);
	else
	    fy = pfy + fabs(dx);
    } else {
	if (dx < 0.0)
	    fx = pfx - fabs(dy);
	else
	    fx = pfx + fabs(dy);
    }


    // Use the polygon's plane for actually adjusting the points
    bg_plane_pt_at(&p->polygon.contour[0].point[0], &zpln, pfx, pfy);
    bg_plane_pt_at(&p->polygon.contour[0].point[1], &zpln, pfx, fy);
    bg_plane_pt_at(&p->polygon.contour[0].point[2], &zpln, fx, fy);
    bg_plane_pt_at(&p->polygon.contour[0].point[3], &zpln, fx, pfy);

    /* Polygon updated, now update shape payload vlist */
    bsg_polygon_vlist(s);

    /* Updated */
    s->s_changed++;

    return 1;
}

int
bsg_update_general_polygon(struct bsg_node *s, int utype, point_t *cp)
{
    struct bsg_polygon *p = bsg_node_polygon(s);
    if (p->type != BSG_POLYGON_GENERAL)
	return 0;

    if (utype == BSG_POLYGON_UPDATE_PT_APPEND) {
	return bsg_append_polygon_pt(s, cp);
    }

    if (utype == BSG_POLYGON_UPDATE_PT_SELECT) {
	return bsg_select_polygon_pt(s, cp);
    }

    if (utype == BSG_POLYGON_UPDATE_PT_SELECT_CLEAR) {
	bsg_select_clear_polygon_pt(s);
	return 1;
    }

    if (utype == BSG_POLYGON_UPDATE_PT_MOVE) {
	return bsg_move_polygon_pt(s, cp);
    }

    /* Polygon updated, now update shape payload vlist */
    bsg_polygon_vlist(s);

    /* Updated */
    s->s_changed++;

    return 0;
}

int
bsg_update_polygon(struct bsg_node *s, struct bsg_view *v, int utype)
{
    if (!s)
	return 0;

    struct bsg_polygon *p = bsg_node_polygon(s);

    // Regardless of type, sync fill color
    struct bsg_node *fobj = bsg_find_child(s, "*fill*");
    if (fobj) {
	bu_color_to_rgb_chars(&p->fill_color, fobj->s_color);
    }

    if (utype == BSG_POLYGON_UPDATE_PROPS_ONLY) {

	for (size_t i = 0; i < BU_PTBL_LEN(&s->children); i++) {
	    struct bsg_node *s_c = (struct bsg_node *)BU_PTBL_GET(&s->children, i);
	    if (!s_c)
		continue;
	    s_c->s_color[0] = s->s_color[0];
	    s_c->s_color[1] = s->s_color[1];
	    s_c->s_color[2] = s->s_color[2];
	}

	if (p->fill_flag) {
	    bsg_fill_polygon(s);
	} else {
	    if (fobj)
		bsg_obj_put(fobj);
	}

	/* Phase D6: props-only change still advances the revision so renderers
	 * downstream (and any attached bsg_live_source) can detect color/fill
	 * updates without polling s_changed. */
	if (s->pl)
	    bsg_payload_bump_revision(s->pl);

	return 0;
    }

    /* Need pixel dimension for calculating segment approximations on these
     * shapes - based on view info */
    int changed = 0;
    if (p->type == BSG_POLYGON_CIRCLE || p->type == BSG_POLYGON_ELLIPSE) {

	// Need the length of the diagonal of a pixel
	vect_t c1 = VINIT_ZERO;
	vect_t c2 = VINIT_ZERO;
	bsg_screen_to_view(v, &c1[0], &c1[1], 0, 0);
	bsg_screen_to_view(v, &c2[0], &c2[1], 1, 1);
	point_t p1, p2;
	MAT4X3PNT(p1, v->gv_view2model, c1);
	MAT4X3PNT(p2, v->gv_view2model, c2);
	fastf_t d = DIST_PNT_PNT(p1, p2);

	if (p->type == BSG_POLYGON_CIRCLE)
	    changed = bsg_update_polygon_circle(s, &v->gv_point, d);
	else
	    changed = bsg_update_polygon_ellipse(s, &v->gv_point, d);
    } else if (p->type == BSG_POLYGON_RECTANGLE) {
	changed = bsg_update_polygon_rectangle(s, &v->gv_point);
    } else if (p->type == BSG_POLYGON_SQUARE) {
	changed = bsg_update_polygon_square(s, &v->gv_point);
    } else if (p->type == BSG_POLYGON_GENERAL) {
	changed = bsg_update_general_polygon(s, utype, &v->gv_point);
    }

    /* Phase D6: whenever geometry actually changed, advance the payload
     * revision.  Any bsg_live_source attached to this node can compare
     * last_realized_revision to detect the change without the caller
     * having to check s_changed directly. */
    if (changed && s->pl)
	bsg_payload_bump_revision(s->pl);

    return changed;
}

struct bsg_node *
bsg_dup_view_polygon(const char *nname, struct bsg_node *s)
{
    if (!nname || !s)
	return NULL;

    struct bsg_polygon *ip = bsg_node_polygon(s);

    struct bsg_polygon *p;
    BU_GET(p, struct bsg_polygon);
    bsg_polygon_cpy(p, ip);

    struct bsg_node *np = bsg_create_polygon_obj(s->s_v, s->s_type_flags, p);

    // Have geometry, now copy visual settings
    VMOVE(np->s_color, s->s_color);

    // Update scene obj vlist
    bsg_polygon_vlist(np);

    // Set new name (s_name was initialized by bsg_obj_reset; just overwrite it)
    bu_vls_sprintf(&np->s_name, "%s", nname);

    // Return new object
    return np;
}


/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
