/*                     P O L Y G O N . C
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
/** @file libbsg/polygon.c
 *
 * Slice 7 (bv_scene_obj_migrate): BSG polygon overlay payload.
 *
 * Provides create/destroy and accessor implementations for
 * BSG_PAYLOAD_TYPE_POLYGON payloads.  Also provides the BSG-native
 * vlist-generation, point-editing, selection, and CSG helpers that were
 * previously exposed via bv_polygon / bv/polygon.h in libbv.
 *
 * No dependency on struct bview fields is required for the payload data
 * itself.  Node creation still uses bsg_node_create_child() which accepts
 * a bview* for bootstrapping, but the polygon geometry and all editing
 * operations are fully BSG-native.
 */

#include "common.h"

#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bu/color.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bn/tol.h"
#include "bg/lseg.h"
#include "bg/plane.h"
#include "bg/polygon.h"
#include "bv/defines.h"
#include "bv/vlist.h"
#include "bsg/appearance.h"
#include "bsg/defines.h"
#include "bsg/material.h"
#include "bsg/node.h"
#include "bsg/payload.h"
#include "bsg/polygon.h"

/*
 * bg_plane_closest_pt / bg_plane_pt_at / bg_polygon_cpy don't declare their
 * read-only pointer arguments as const.  Use these thin wrappers to cast once
 * in one place rather than scattering casts through the implementation.
 */
#define _bg_plane_closest(u, v, plane, pt) \
    bg_plane_closest_pt((u), (v), (plane), (point_t *)(pt))
#define _bg_poly_cpy(dst, src) \
    bg_polygon_cpy((dst), (struct bg_polygon *)(src))

/* ------------------------------------------------------------------ */
/* Private implementation struct                                       */
/* ------------------------------------------------------------------ */

struct _bsg_payload_polygon {
    struct bsg_payload  base;

    int                 type;           /* BSG_POLYGON_* */
    int                 fill_flag;
    vect2d_t            fill_dir;
    fastf_t             fill_delta;
    struct bu_color     fill_color;
    long                curr_contour_i;
    long                curr_point_i;
    point_t             origin_point;
    plane_t             vp;             /* view plane at creation time */
    fastf_t             vZ;             /* plane Z offset */

    struct bg_polygon   polygon;        /* geometry; owned by this struct */
};

/* ------------------------------------------------------------------ */
/* Internal cast helper with type check                                */
/* ------------------------------------------------------------------ */

static struct _bsg_payload_polygon *
_poly_cast(struct bsg_payload *p)
{
    if (!p || p->type != BSG_PAYLOAD_TYPE_POLYGON)
	return NULL;
    return (struct _bsg_payload_polygon *)p;
}

static const struct _bsg_payload_polygon *
_poly_cast_c(const struct bsg_payload *p)
{
    if (!p || p->type != BSG_PAYLOAD_TYPE_POLYGON)
	return NULL;
    return (const struct _bsg_payload_polygon *)p;
}

/* ------------------------------------------------------------------ */
/* Lifecycle helpers                                                   */
/* ------------------------------------------------------------------ */

static void
_payload_polygon_free(struct bsg_payload *payload)
{
    struct _bsg_payload_polygon *pp = (struct _bsg_payload_polygon *)payload;
    if (!pp)
	return;
    bg_polygon_free(&pp->polygon);
}

/* ------------------------------------------------------------------ */
/* Public lifecycle                                                    */
/* ------------------------------------------------------------------ */

struct bsg_payload *
bsg_payload_polygon_create(int type)
{
    struct _bsg_payload_polygon *pp = NULL;
    struct bsg_payload *p = NULL;

    BU_ALLOC(pp, struct _bsg_payload_polygon);
    if (!pp)
	return NULL;
    memset(pp, 0, sizeof(*pp));

    p = &pp->base;
    p->type     = BSG_PAYLOAD_TYPE_POLYGON;
    p->free_fn  = _payload_polygon_free;

    pp->type            = type;
    pp->fill_flag       = 0;
    pp->curr_contour_i  = -1;
    pp->curr_point_i    = -1;
    pp->fill_delta      = 0.0;
    V2SETALL(pp->fill_dir, 0.0);
    VSETALL(pp->origin_point, 0.0);
    HSETALL(pp->vp, 0.0);
    pp->vZ = 0.0;

    /* Default fill color: blue */
    {
	unsigned char frgb[3] = {0, 0, 255};
	bu_color_from_rgb_chars(&pp->fill_color, frgb);
    }

    /* bg_polygon starts zeroed (memset above) */

    return p;
}

/* ------------------------------------------------------------------ */
/* Shape type                                                          */
/* ------------------------------------------------------------------ */

int
bsg_payload_polygon_type_get(const struct bsg_payload *p)
{
    const struct _bsg_payload_polygon *pp = _poly_cast_c(p);
    return pp ? pp->type : BSG_POLYGON_GENERAL;
}

void
bsg_payload_polygon_type_set(struct bsg_payload *p, int type)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp) return;
    pp->type = type;
}

/* ------------------------------------------------------------------ */
/* Fill state                                                          */
/* ------------------------------------------------------------------ */

int
bsg_payload_polygon_fill_flag_get(const struct bsg_payload *p)
{
    const struct _bsg_payload_polygon *pp = _poly_cast_c(p);
    return pp ? pp->fill_flag : 0;
}

void
bsg_payload_polygon_fill_flag_set(struct bsg_payload *p, int flag)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp) return;
    pp->fill_flag = flag;
}

void
bsg_payload_polygon_fill_dir_get(const struct bsg_payload *p, vect2d_t out)
{
    const struct _bsg_payload_polygon *pp = _poly_cast_c(p);
    if (!pp || !out) return;
    V2MOVE(out, pp->fill_dir);
}

void
bsg_payload_polygon_fill_dir_set(struct bsg_payload *p, const vect2d_t dir)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp || !dir) return;
    V2MOVE(pp->fill_dir, dir);
}

fastf_t
bsg_payload_polygon_fill_delta_get(const struct bsg_payload *p)
{
    const struct _bsg_payload_polygon *pp = _poly_cast_c(p);
    return pp ? pp->fill_delta : 0.0;
}

void
bsg_payload_polygon_fill_delta_set(struct bsg_payload *p, fastf_t delta)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp) return;
    pp->fill_delta = delta;
}

void
bsg_payload_polygon_fill_color_get(const struct bsg_payload *p, struct bu_color *out)
{
    const struct _bsg_payload_polygon *pp = _poly_cast_c(p);
    if (!pp || !out) return;
    BU_COLOR_CPY(out, &pp->fill_color);
}

void
bsg_payload_polygon_fill_color_set(struct bsg_payload *p, const struct bu_color *c)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp || !c) return;
    BU_COLOR_CPY(&pp->fill_color, c);
}

/* ------------------------------------------------------------------ */
/* Edit state                                                          */
/* ------------------------------------------------------------------ */

long
bsg_payload_polygon_curr_contour_get(const struct bsg_payload *p)
{
    const struct _bsg_payload_polygon *pp = _poly_cast_c(p);
    return pp ? pp->curr_contour_i : -1L;
}

void
bsg_payload_polygon_curr_contour_set(struct bsg_payload *p, long idx)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp) return;
    pp->curr_contour_i = idx;
}

long
bsg_payload_polygon_curr_point_get(const struct bsg_payload *p)
{
    const struct _bsg_payload_polygon *pp = _poly_cast_c(p);
    return pp ? pp->curr_point_i : -1L;
}

void
bsg_payload_polygon_curr_point_set(struct bsg_payload *p, long idx)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp) return;
    pp->curr_point_i = idx;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

void
bsg_payload_polygon_origin_get(const struct bsg_payload *p, point_t out)
{
    const struct _bsg_payload_polygon *pp = _poly_cast_c(p);
    if (!pp || !out) return;
    VMOVE(out, pp->origin_point);
}

void
bsg_payload_polygon_origin_set(struct bsg_payload *p, const point_t origin)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp || !origin) return;
    VMOVE(pp->origin_point, origin);
}

void
bsg_payload_polygon_view_plane_get(const struct bsg_payload *p, plane_t out)
{
    const struct _bsg_payload_polygon *pp = _poly_cast_c(p);
    if (!pp || !out) return;
    HMOVE(out, pp->vp);
}

void
bsg_payload_polygon_view_plane_set(struct bsg_payload *p, const plane_t vp)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp || !vp) return;
    HMOVE(pp->vp, vp);
}

fastf_t
bsg_payload_polygon_vZ_get(const struct bsg_payload *p)
{
    const struct _bsg_payload_polygon *pp = _poly_cast_c(p);
    return pp ? pp->vZ : 0.0;
}

void
bsg_payload_polygon_vZ_set(struct bsg_payload *p, fastf_t vZ)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp) return;
    pp->vZ = vZ;
}

struct bg_polygon *
bsg_payload_polygon_bg_get(struct bsg_payload *p)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    return pp ? &pp->polygon : NULL;
}

void
bsg_payload_polygon_bg_set(struct bsg_payload *p, const struct bg_polygon *poly)
{
    struct _bsg_payload_polygon *pp = _poly_cast(p);
    if (!pp || !poly)
	return;
    bg_polygon_free(&pp->polygon);
    if (poly->num_contours > 0)
	_bg_poly_cpy(&pp->polygon, poly);
}

void
bsg_payload_polygon_cpy(struct bsg_payload *dst, const struct bsg_payload *src)
{
    struct _bsg_payload_polygon *d = _poly_cast(dst);
    const struct _bsg_payload_polygon *s = _poly_cast_c(src);
    if (!d || !s)
	return;

    d->type            = s->type;
    d->fill_flag       = s->fill_flag;
    V2MOVE(d->fill_dir, s->fill_dir);
    d->fill_delta      = s->fill_delta;
    BU_COLOR_CPY(&d->fill_color, &s->fill_color);
    d->curr_contour_i  = s->curr_contour_i;
    d->curr_point_i    = s->curr_point_i;
    VMOVE(d->origin_point, s->origin_point);
    HMOVE(d->vp, s->vp);
    d->vZ              = s->vZ;

    bg_polygon_free(&d->polygon);
    if (s->polygon.num_contours > 0)
	_bg_poly_cpy(&d->polygon, &s->polygon);
}

/* ------------------------------------------------------------------ */
/* Internal vlist helpers (no bview dependency)                        */
/* ------------------------------------------------------------------ */

/*
 * Get the effective vlfree list for node n: use the supplied vlfree
 * if non-NULL, otherwise fall back to bsg_node_vlfree().
 */
static struct bu_list *
_effective_vlfree(bsg_node *n, struct bu_list *vlfree)
{
    if (vlfree)
	return vlfree;
    return bsg_node_vlfree(n);
}

/*
 * RGB color helpers using BSG material API (no bv raw-field access).
 */
static void
_node_rgb_get(const bsg_node *n, unsigned char rgb[3])
{
    struct bsg_material m;
    if (!rgb) return;
    rgb[0] = rgb[1] = rgb[2] = 0;
    if (!n) return;
    bsg_material_init(&m);
    bsg_node_material_get(n, &m);
    rgb[0] = m.rgba[0];
    rgb[1] = m.rgba[1];
    rgb[2] = m.rgba[2];
}

static void
_node_rgb_set(bsg_node *n, const unsigned char rgb[3])
{
    struct bsg_material m;
    if (!n || !rgb) return;
    bsg_material_init(&m);
    bsg_node_material_get(n, &m);
    bsg_material_set_rgba(&m, rgb[0], rgb[1], rgb[2], m.rgba[3]);
    bsg_node_material_set(n, &m);
}

/*
 * Append vlist commands for one polygon contour into n's vlist.
 * curr_c / curr_i control the highlighted current point marker.
 */
static void
_polygon_contour_vlist(bsg_node *n, struct bu_list *vlfree,
		       struct bg_poly_contour *c,
		       int curr_c, int curr_i, int do_pnt)
{
    struct bu_list *vhead = bsg_node_vlist_head(n);
    if (!vhead) return;

    if (do_pnt) {
	BV_ADD_VLIST(vlfree, vhead, c->point[0], BV_VLIST_POINT_DRAW);
	return;
    }

    BV_ADD_VLIST(vlfree, vhead, c->point[0], BV_VLIST_LINE_MOVE);
    for (size_t i = 0; i < c->num_points; i++) {
	BV_ADD_VLIST(vlfree, vhead, c->point[i], BV_VLIST_LINE_DRAW);
    }
    if (!c->open)
	BV_ADD_VLIST(vlfree, vhead, c->point[0], BV_VLIST_LINE_DRAW);

    if (curr_c && curr_i >= 0) {
	point_t psize;
	VSET(psize, 10, 0, 0);
	BV_ADD_VLIST(vlfree, vhead, c->point[curr_i], BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(vlfree, vhead, psize, BV_VLIST_POINT_SIZE);
	BV_ADD_VLIST(vlfree, vhead, c->point[curr_i], BV_VLIST_POINT_DRAW);
    }
}

/*
 * Find a direct child of n whose name matches pattern (glob-style "*...*").
 * Returns the first match or NULL.
 */
static bsg_node *
_find_child_by_name(bsg_node *n, const char *name)
{
    if (!n || !name)
	return NULL;
    size_t cnt = bsg_node_child_count(n);
    for (size_t i = 0; i < cnt; i++) {
	bsg_node *c = bsg_node_child(n, i);
	const char *cname = bsg_node_name(c);
	if (cname && BU_STR_EQUAL(cname, name))
	    return c;
    }
    return NULL;
}

/*
 * Remove and destroy a direct named child of n (e.g. ":fill").
 */
static void
_remove_child_by_name(bsg_node *n, const char *name)
{
    bsg_node *c = _find_child_by_name(n, name);
    if (!c) return;
    bsg_node_remove_child(n, c);
    bsg_node_destroy(c);
}

/*
 * Remove and destroy all direct children of n that were created as hole
 * or fill child nodes (distinguished by a ":" prefix in the name).
 * We only remove children whose name starts with ':'.
 */
static void
_remove_shape_children(bsg_node *n)
{
    if (!n) return;

    /* Snapshot child list before modification */
    size_t cnt = bsg_node_child_count(n);
    if (cnt == 0) return;

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < cnt; i++)
	bu_ptbl_ins(&snap, (long *)bsg_node_child(n, i));

    for (size_t i = 0; i < BU_PTBL_LEN(&snap); i++) {
	bsg_node *c = (bsg_node *)BU_PTBL_GET(&snap, i);
	const char *cname = bsg_node_name(c);
	if (cname && cname[0] == ':') {
	    bsg_node_remove_child(n, c);
	    bsg_node_destroy(c);
	}
    }
    bu_ptbl_free(&snap);
}

/*
 * Create a new BSG shape child node attached to n.
 * Uses the view associated with n for node allocation.
 */
static bsg_node *
_create_shape_child(bsg_node *n)
{
    struct bview *v = bsg_node_view_get(n);
    if (!v) return NULL;
    return bsg_node_create_child(v, BSG_NODE_SHAPE);
}

/*
 * Regenerate fill child for node n.
 */
static void
_polygon_fill_update(bsg_node *n, struct _bsg_payload_polygon *pp,
		     struct bu_list *vlfree)
{
    /* Remove any existing fill child */
    _remove_child_by_name(n, ":fill");

    if (!pp->fill_flag)
	return;
    if (!pp->polygon.num_contours)
	return;
    if (!pp->polygon.contour || pp->polygon.contour[0].open)
	return;
    if (pp->fill_delta < BN_TOL_DIST)
	return;

    struct bg_polygon *fill = bsg_polygon_fill_segments(
	&pp->polygon, &pp->vp, pp->fill_dir, pp->fill_delta);
    if (!fill)
	return;

    bsg_node *fchild = _create_shape_child(n);
    if (!fchild) {
	bg_polygon_free(fill);
	BU_PUT(fill, struct bg_polygon);
	return;
    }

    bsg_node_set_name(fchild, ":fill");
    {
	struct bsg_appearance a;
	bsg_appearance_init(&a);
	a.line_width = 1;
	a.line_style = BSG_APPEARANCE_LINE_SOLID;
	bsg_node_appearance_set(fchild, &a);
    }

    /* Fill color */
    {
	unsigned char frgb[3];
	bu_color_to_rgb_chars(&pp->fill_color, frgb);
	_node_rgb_set(fchild, frgb);
    }

    /* Append vlist commands for fill segments */
    struct bu_list *fvhead = bsg_node_vlist_head(fchild);
    if (fvhead) {
	for (size_t i = 0; i < fill->num_contours; i++) {
	    struct bg_poly_contour *c = &fill->contour[i];
	    if (c->num_points < 2) continue;
	    BV_ADD_VLIST(vlfree, fvhead, c->point[0], BV_VLIST_LINE_MOVE);
	    for (size_t j = 1; j < c->num_points; j++)
		BV_ADD_VLIST(vlfree, fvhead, c->point[j], BV_VLIST_LINE_DRAW);
	}
    }

    bsg_node_add_child(n, fchild);

    bg_polygon_free(fill);
    BU_PUT(fill, struct bg_polygon);
}


/* ------------------------------------------------------------------ */
/* Public: vlist generation                                            */
/* ------------------------------------------------------------------ */

void
bsg_polygon_vlist_update(bsg_node *n, struct bu_list *vlfree)
{
    if (!n) return;

    struct bsg_payload *base = bsg_node_payload_get(n);
    struct _bsg_payload_polygon *pp = _poly_cast(base);
    if (!pp) return;

    struct bu_list *vfl = _effective_vlfree(n, vlfree);

    /* Reset node vlist */
    struct bu_list *vhead = bsg_node_vlist_head(n);
    if (vhead && BU_LIST_IS_INITIALIZED(vhead))
	BV_FREE_VLIST(vfl, vhead);
    if (vhead)
	BU_LIST_INIT(vhead);

    /* Remove all polygon-generated child nodes (holes, fill) */
    _remove_shape_children(n);

    int type = pp->type;

    for (size_t i = 0; i < pp->polygon.num_contours; ++i) {
	size_t pcnt = pp->polygon.contour[i].num_points;
	int do_pnt = 0;

	if (pcnt == 1)
	    do_pnt = 1;
	if (type == BSG_POLYGON_CIRCLE && pcnt == 3)
	    do_pnt = 1;
	if (type == BSG_POLYGON_ELLIPSE && pcnt == 4)
	    do_pnt = 1;
	if (type == BSG_POLYGON_RECTANGLE) {
	    if (pcnt >= 3 &&
		NEAR_ZERO(DIST_PNT_PNT_SQ(pp->polygon.contour[0].point[0],
					   pp->polygon.contour[0].point[1]), SMALL_FASTF) &&
		NEAR_ZERO(DIST_PNT_PNT_SQ(pp->polygon.contour[0].point[0],
					   pp->polygon.contour[0].point[2]), SMALL_FASTF))
		do_pnt = 1;
	}
	if (type == BSG_POLYGON_SQUARE) {
	    if (pcnt >= 3 &&
		NEAR_ZERO(DIST_PNT_PNT_SQ(pp->polygon.contour[0].point[0],
					   pp->polygon.contour[0].point[1]), SMALL_FASTF) &&
		NEAR_ZERO(DIST_PNT_PNT_SQ(pp->polygon.contour[0].point[0],
					   pp->polygon.contour[0].point[2]), SMALL_FASTF))
		do_pnt = 1;
	}

	if (pp->polygon.hole[i]) {
	    /* Hole contours: draw as dashed child shapes */
	    bsg_node *hchild = _create_shape_child(n);
	    if (hchild) {
		/* Build a name so _remove_shape_children finds it */
		struct bu_vls vname = BU_VLS_INIT_ZERO;
		bu_vls_sprintf(&vname, ":hole%zu", i);
		bsg_node_set_name(hchild, bu_vls_cstr(&vname));
		bu_vls_free(&vname);

		{
		    struct bsg_appearance a;
		    bsg_appearance_init(&a);
		    a.line_style = BSG_APPEARANCE_LINE_DASHED;
		    bsg_node_appearance_set(hchild, &a);
		}
		unsigned char srgb[3];
		_node_rgb_get(n, srgb);
		_node_rgb_set(hchild, srgb);
		bsg_node_view_set(hchild, bsg_node_view_get(n));
		_polygon_contour_vlist(hchild, vfl,
				       &pp->polygon.contour[i],
				       ((long)i == pp->curr_contour_i),
				       (int)pp->curr_point_i, do_pnt);
		bsg_node_add_child(n, hchild);
	    }
	    continue;
	}

	_polygon_contour_vlist(n, vfl, &pp->polygon.contour[i],
			       ((long)i == pp->curr_contour_i),
			       (int)pp->curr_point_i, do_pnt);
    }

    if (pp->fill_flag)
	_polygon_fill_update(n, pp, vfl);
}


/* ------------------------------------------------------------------ */
/* Public: point editing                                               */
/* ------------------------------------------------------------------ */

int
bsg_polygon_append_pt(bsg_node *n, const point_t *np, struct bu_list *vlfree)
{
    if (!n || !np) return -1;

    struct bsg_payload *base = bsg_node_payload_get(n);
    struct _bsg_payload_polygon *pp = _poly_cast(base);
    if (!pp || pp->type != BSG_POLYGON_GENERAL) return -1;
    if (pp->curr_contour_i < 0) return -1;

    /* Project np onto the view plane */
    plane_t zpln;
    HMOVE(zpln, pp->vp);
    zpln[3] += pp->vZ;
    fastf_t fx, fy;
    _bg_plane_closest(&fx, &fy, &zpln, np);
    point_t m_pt;
    bg_plane_pt_at(&m_pt, &zpln, fx, fy);

    struct bg_poly_contour *c = &pp->polygon.contour[pp->curr_contour_i];
    c->num_points++;
    c->point = (point_t *)bu_realloc(c->point,
				     c->num_points * sizeof(point_t),
				     "polygon contour points");
    VMOVE(c->point[c->num_points - 1], m_pt);

    bsg_polygon_vlist_update(n, vlfree);
    return 0;
}


int
bsg_polygon_select_pt(bsg_node *n, const point_t *cp, struct bu_list *vlfree)
{
    if (!n || !cp) return -1;

    struct bsg_payload *base = bsg_node_payload_get(n);
    struct _bsg_payload_polygon *pp = _poly_cast(base);
    if (!pp || pp->type != BSG_POLYGON_GENERAL) return -1;

    plane_t zpln;
    HMOVE(zpln, pp->vp);
    zpln[3] += pp->vZ;
    fastf_t fx, fy;
    _bg_plane_closest(&fx, &fy, &zpln, cp);
    point_t m_pt;
    bg_plane_pt_at(&m_pt, &zpln, fx, fy);

    double dist_min_sq = DBL_MAX;
    long closest_ind     = -1;
    long closest_contour = -1;

    if (pp->curr_contour_i >= 0) {
	struct bg_poly_contour *c = &pp->polygon.contour[pp->curr_contour_i];
	closest_contour = pp->curr_contour_i;
	for (size_t i = 0; i < c->num_points; i++) {
	    double d = DIST_PNT_PNT_SQ(c->point[i], m_pt);
	    if (d < dist_min_sq) {
		dist_min_sq = d;
		closest_ind = (long)i;
	    }
	}
    } else {
	for (size_t j = 0; j < pp->polygon.num_contours; j++) {
	    struct bg_poly_contour *c = &pp->polygon.contour[j];
	    for (size_t i = 0; i < c->num_points; i++) {
		double d = DIST_PNT_PNT_SQ(c->point[i], m_pt);
		if (d < dist_min_sq) {
		    dist_min_sq = d;
		    closest_ind     = (long)i;
		    closest_contour = (long)j;
		}
	    }
	}
    }

    pp->curr_point_i   = closest_ind;
    pp->curr_contour_i = closest_contour;

    bsg_polygon_vlist_update(n, vlfree);
    return 0;
}


void
bsg_polygon_select_clear_pt(bsg_node *n, struct bu_list *vlfree)
{
    if (!n) return;

    struct bsg_payload *base = bsg_node_payload_get(n);
    struct _bsg_payload_polygon *pp = _poly_cast(base);
    if (!pp) return;

    pp->curr_point_i   = -1;
    pp->curr_contour_i = -1;
    bsg_polygon_vlist_update(n, vlfree);
}


int
bsg_polygon_move_pt(bsg_node *n, const point_t *mp, struct bu_list *vlfree)
{
    if (!n || !mp) return -1;

    struct bsg_payload *base = bsg_node_payload_get(n);
    struct _bsg_payload_polygon *pp = _poly_cast(base);
    if (!pp || pp->type != BSG_POLYGON_GENERAL) return -1;
    if (pp->curr_point_i < 0 || pp->curr_contour_i < 0) return -1;

    plane_t zpln;
    HMOVE(zpln, pp->vp);
    zpln[3] += pp->vZ;
    fastf_t fx, fy;
    _bg_plane_closest(&fx, &fy, &zpln, mp);
    point_t m_pt;
    bg_plane_pt_at(&m_pt, &zpln, fx, fy);

    struct bg_poly_contour *c = &pp->polygon.contour[pp->curr_contour_i];
    VMOVE(c->point[pp->curr_point_i], m_pt);

    bsg_polygon_vlist_update(n, vlfree);
    return 0;
}


int
bsg_polygon_move(bsg_node *n, const point_t *cp, const point_t *prev,
		 struct bu_list *vlfree)
{
    if (!n || !cp || !prev) return -1;

    struct bsg_payload *base = bsg_node_payload_get(n);
    struct _bsg_payload_polygon *pp = _poly_cast(base);
    if (!pp) return -1;

    plane_t zpln;
    HMOVE(zpln, pp->vp);
    zpln[3] += pp->vZ;

    fastf_t pfx, pfy, fx, fy;
    _bg_plane_closest(&pfx, &pfy, &zpln, prev);
    _bg_plane_closest(&fx,  &fy,  &zpln, cp);

    point_t pm_pt, m_pt;
    bg_plane_pt_at(&pm_pt, &pp->vp, pfx, pfy);
    bg_plane_pt_at(&m_pt,  &pp->vp, fx,  fy);

    vect_t v_mv;
    VSUB2(v_mv, m_pt, pm_pt);

    for (size_t j = 0; j < pp->polygon.num_contours; j++) {
	struct bg_poly_contour *c = &pp->polygon.contour[j];
	for (size_t i = 0; i < c->num_points; i++)
	    VADD2(c->point[i], c->point[i], v_mv);
    }
    VADD2(pp->origin_point, pp->origin_point, v_mv);

    bsg_polygon_vlist_update(n, vlfree);
    return 0;
}


/* ------------------------------------------------------------------ */
/* Public: shape-specific updates                                      */
/* ------------------------------------------------------------------ */

int
bsg_polygon_update_circle(bsg_node *n, const point_t *cp, fastf_t pixel_size,
			  struct bu_list *vlfree)
{
    if (!n || !cp) return 0;

    struct bsg_payload *base = bsg_node_payload_get(n);
    struct _bsg_payload_polygon *pp = _poly_cast(base);
    if (!pp) return 0;

    plane_t zpln;
    HMOVE(zpln, pp->vp);
    zpln[3] += pp->vZ;

    fastf_t pfx, pfy, fx, fy;
    _bg_plane_closest(&fx,  &fy,  &zpln, cp);
    _bg_plane_closest(&pfx, &pfy, &zpln, &pp->origin_point);

    point_t pcp;
    bg_plane_pt_at(&pcp, &zpln, fx, fy);

    fastf_t r = DIST_PNT_PNT(pcp, pp->origin_point);

    int nsegs = (pixel_size > 0.0) ? (int)(M_PI_2 * r / pixel_size) : 32;
    if (nsegs < 32)
	nsegs = 32;

    fastf_t arc = 360.0 / nsegs;

    struct bg_polygon gp;
    gp.num_contours = 1;
    gp.hole     = (int *)bu_calloc(1, sizeof(int), "hole");
    gp.contour  = (struct bg_poly_contour *)bu_calloc(1, sizeof(struct bg_poly_contour), "contour");
    gp.contour[0].num_points = nsegs;
    gp.contour[0].open       = 0;
    gp.contour[0].point      = (point_t *)bu_calloc(nsegs, sizeof(point_t), "point");

    for (int k = 0; k < nsegs; ++k) {
	fastf_t ang     = k * arc;
	fastf_t curr_fx = cos(ang * DEG2RAD) * r + pfx;
	fastf_t curr_fy = sin(ang * DEG2RAD) * r + pfy;
	point_t v_pt;
	bg_plane_pt_at(&v_pt, &pp->vp, curr_fx, curr_fy);
	VMOVE(gp.contour[0].point[k], v_pt);
    }

    bg_polygon_free(&pp->polygon);
    pp->polygon.num_contours = gp.num_contours;
    pp->polygon.hole         = gp.hole;
    pp->polygon.contour      = gp.contour;

    bsg_polygon_vlist_update(n, vlfree);
    return 1;
}


int
bsg_polygon_update_ellipse(bsg_node *n, const point_t *cp, fastf_t pixel_size,
			   struct bu_list *vlfree)
{
    if (!n || !cp) return 0;

    struct bsg_payload *base = bsg_node_payload_get(n);
    struct _bsg_payload_polygon *pp = _poly_cast(base);
    if (!pp) return 0;

    plane_t zpln;
    HMOVE(zpln, pp->vp);
    zpln[3] += pp->vZ;

    fastf_t pfx, pfy, fx, fy;
    _bg_plane_closest(&fx,  &fy,  &zpln, cp);
    _bg_plane_closest(&pfx, &pfy, &zpln, &pp->origin_point);

    fastf_t r = DIST_PNT_PNT(*cp, pp->origin_point);
    int nsegs = (pixel_size > 0.0) ? (int)(M_PI_2 * r / pixel_size) : 32;
    if (nsegs < 32)
	nsegs = 32;

    fastf_t a = fx - pfx;
    fastf_t b = fy - pfy;

    point_t pv_pt, A, B, ellout;
    VSET(pv_pt, pfx, pfy, 0);
    VSET(A, a, 0, 0);
    VSET(B, 0, b, 0);

    fastf_t arc = 360.0 / nsegs;

    struct bg_polygon gp;
    gp.num_contours = 1;
    gp.hole     = (int *)bu_calloc(1, sizeof(int), "hole");
    gp.contour  = (struct bg_poly_contour *)bu_calloc(1, sizeof(struct bg_poly_contour), "contour");
    gp.contour[0].num_points = nsegs;
    gp.contour[0].open       = 0;
    gp.contour[0].point      = (point_t *)bu_calloc(nsegs, sizeof(point_t), "point");

    for (int k = 0; k < nsegs; ++k) {
	fastf_t cosa = cos(k * arc * DEG2RAD);
	fastf_t sina = sin(k * arc * DEG2RAD);
	VJOIN2(ellout, pv_pt, cosa, A, sina, B);
	bg_plane_pt_at(&gp.contour[0].point[k], &zpln, ellout[0], ellout[1]);
    }

    bg_polygon_free(&pp->polygon);
    pp->polygon.num_contours = gp.num_contours;
    pp->polygon.hole         = gp.hole;
    pp->polygon.contour      = gp.contour;

    bsg_polygon_vlist_update(n, vlfree);
    return 1;
}


int
bsg_polygon_update_rectangle(bsg_node *n, const point_t *cp,
			     struct bu_list *vlfree)
{
    if (!n || !cp) return 0;

    struct bsg_payload *base = bsg_node_payload_get(n);
    struct _bsg_payload_polygon *pp = _poly_cast(base);
    if (!pp) return 0;
    if (pp->polygon.num_contours < 1 || pp->polygon.contour[0].num_points < 4) return 0;

    plane_t zpln;
    HMOVE(zpln, pp->vp);
    zpln[3] += pp->vZ;

    fastf_t pfx, pfy, fx, fy;
    _bg_plane_closest(&pfx, &pfy, &zpln, &pp->origin_point);
    _bg_plane_closest(&fx,  &fy,  &zpln, cp);

    bg_plane_pt_at(&pp->polygon.contour[0].point[0], &zpln, pfx, pfy);
    bg_plane_pt_at(&pp->polygon.contour[0].point[1], &zpln, pfx, fy);
    bg_plane_pt_at(&pp->polygon.contour[0].point[2], &zpln, fx,  fy);
    bg_plane_pt_at(&pp->polygon.contour[0].point[3], &zpln, fx,  pfy);
    pp->polygon.contour[0].open = 0;

    bsg_polygon_vlist_update(n, vlfree);
    return 1;
}


int
bsg_polygon_update_square(bsg_node *n, const point_t *cp,
			  struct bu_list *vlfree)
{
    if (!n || !cp) return 0;

    struct bsg_payload *base = bsg_node_payload_get(n);
    struct _bsg_payload_polygon *pp = _poly_cast(base);
    if (!pp) return 0;
    if (pp->polygon.num_contours < 1 || pp->polygon.contour[0].num_points < 4) return 0;

    plane_t zpln;
    HMOVE(zpln, pp->vp);
    zpln[3] += pp->vZ;

    fastf_t pfx, pfy, fx, fy;
    _bg_plane_closest(&pfx, &pfy, &zpln, &pp->origin_point);
    _bg_plane_closest(&fx,  &fy,  &zpln, cp);

    fastf_t dx = fx - pfx;
    fastf_t dy = fy - pfy;

    if (fabs(dx) > fabs(dy)) {
	fy = (dy < 0.0) ? pfy - fabs(dx) : pfy + fabs(dx);
    } else {
	fx = (dx < 0.0) ? pfx - fabs(dy) : pfx + fabs(dy);
    }

    bg_plane_pt_at(&pp->polygon.contour[0].point[0], &zpln, pfx, pfy);
    bg_plane_pt_at(&pp->polygon.contour[0].point[1], &zpln, pfx, fy);
    bg_plane_pt_at(&pp->polygon.contour[0].point[2], &zpln, fx,  fy);
    bg_plane_pt_at(&pp->polygon.contour[0].point[3], &zpln, fx,  pfy);

    bsg_polygon_vlist_update(n, vlfree);
    return 1;
}


/* ------------------------------------------------------------------ */
/* Public: closest-polygon selection                                   */
/* ------------------------------------------------------------------ */

bsg_node *
bsg_polygon_select_closest(const struct bu_ptbl *objs, const point_t *cp)
{
    if (!objs || !cp)
	return NULL;

    bsg_node *closest   = NULL;
    double dist_min_sq  = DBL_MAX;

    for (size_t i = 0; i < BU_PTBL_LEN(objs); i++) {
	bsg_node *node = (bsg_node *)BU_PTBL_GET(objs, i);
	if (!node) continue;

	struct bsg_payload *base = bsg_node_payload_get(node);
	struct _bsg_payload_polygon *pp = _poly_cast(base);
	if (!pp) continue;

	plane_t zpln;
	HMOVE(zpln, pp->vp);
	zpln[3] += pp->vZ;
	fastf_t fx, fy;
	_bg_plane_closest(&fx, &fy, &zpln, cp);
	point_t m_pt;
	bg_plane_pt_at(&m_pt, &zpln, fx, fy);

	for (size_t j = 0; j < pp->polygon.num_contours; j++) {
	    struct bg_poly_contour *c = &pp->polygon.contour[j];
	    for (size_t k = 0; k < c->num_points; k++) {
		double dcand;
		if (k < c->num_points - 1)
		    dcand = bg_distsq_lseg3_pt(NULL, c->point[k], c->point[k+1], m_pt);
		else
		    dcand = bg_distsq_lseg3_pt(NULL, c->point[k], c->point[0], m_pt);
		if (dcand < dist_min_sq) {
		    dist_min_sq = dcand;
		    closest = node;
		}
	    }
	}
    }

    return closest;
}


/* ------------------------------------------------------------------ */
/* Public: CSG (boolean) operations                                    */
/* ------------------------------------------------------------------ */

int
bsg_polygon_csg(bsg_node *target, bsg_node *stencil, bg_clip_t op,
		fastf_t view_scale, struct bu_list *vlfree)
{
    if (!target || !stencil)
	return 0;

    struct bsg_payload *tbase = bsg_node_payload_get(target);
    struct bsg_payload *sbase = bsg_node_payload_get(stencil);
    struct _bsg_payload_polygon *polyA = _poly_cast(tbase);
    struct _bsg_payload_polygon *polyB = _poly_cast_c(sbase) ?
					 (struct _bsg_payload_polygon *)sbase : NULL;
    if (!polyA || !polyB)
	return 0;

    if (op == bg_None)
	return 0;

    /* Empty stencil: nothing to do */
    if (!polyB->polygon.num_contours)
	return 0;

    if (polyA->polygon.num_contours || op != bg_Union) {
	const struct bn_tol poly_tol = BN_TOL_INIT_TOL;
	int ovlp = bg_polygons_overlap(&polyA->polygon, &polyB->polygon,
				       &polyA->vp, &poly_tol,
				       (view_scale > 0.0) ? view_scale : 1.0);
	if (!ovlp)
	    return 0;
    } else {
	/* Union into empty: copy stencil directly, preserving type */
	bg_polygon_free(&polyA->polygon);
	_bg_poly_cpy(&polyA->polygon, &polyB->polygon);
	polyA->type            = polyB->type;
	polyA->vZ              = polyB->vZ;
	polyA->curr_contour_i  = polyB->curr_contour_i;
	polyA->curr_point_i    = polyB->curr_point_i;
	VMOVE(polyA->origin_point, polyB->origin_point);
	HMOVE(polyA->vp, polyB->vp);
	bsg_polygon_vlist_update(target, vlfree);
	return 1;
    }

    struct bg_polygon *cp = bg_clip_polygon(op, &polyA->polygon, &polyB->polygon,
					    CLIPPER_MAX, &polyA->vp);
    if (!cp)
	return 0;

    bg_polygon_free(&polyA->polygon);
    polyA->polygon.num_contours = cp->num_contours;
    polyA->polygon.hole         = cp->hole;
    polyA->polygon.contour      = cp->contour;
    BU_PUT(cp, struct bg_polygon);

    polyA->type = BSG_POLYGON_GENERAL;

    bsg_polygon_vlist_update(target, vlfree);
    return 1;
}


/* ------------------------------------------------------------------ */
/* Public: fill delta suggestion                                       */
/* ------------------------------------------------------------------ */

int
bsg_polygon_calc_fdelta(struct bsg_payload *p)
{
    if (!p)
	return 0;
    /* TODO: project all contours onto a 2D fit plane, get bounding boxes,
     * and return 20% of the smallest dimension. */
    return 0;
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
