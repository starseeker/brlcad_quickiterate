/*                       Q U E R Y . C
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
/** @file libbsg/query.c
 *
 * Slice 8 (bv_scene_obj_migrate):
 * BSG-native bounds and camera-query implementations.
 *
 * This file provides libbsg-owned replacements for:
 *   bv_scene_obj_bound       → bsg_node_compute_bound
 *   bv_view_bounds           → bsg_view_compute_bounds
 *   bv_view_objs_select      → bsg_view_select
 *   bv_view_objs_rect_select → bsg_view_rect_select
 *
 * No compiled libbv symbols are called from this file.  All required
 * helpers come from libbsg (bsg/node.h, bsg/vlist.h, bsg/payload.h,
 * bsg/lod_ops.h, bsg/camera.h), libbg (bg/plane.h, bg/sat.h), and
 * libbu.
 */

#include "common.h"

#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bu/list.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bg/plane.h"
#include "bg/sat.h"
#include "bv/defines.h"  /* struct bv_mesh_lod (storage type only) */

#include "bsg/camera.h"
#include "bsg/defines.h"
#include "bsg/lod_ops.h"
#include "bsg/node.h"
#include "bsg/payload.h"
#include "bsg/query.h"
#include "bsg/vlist.h"


/* ====================================================================
 * Internal helpers
 * ==================================================================== */

/**
 * Convert a screen-pixel position to normalised view-space coordinates,
 * matching bv_screen_to_view() but operating on a bsg_camera_snapshot.
 *
 * The conversion is pure arithmetic; snap-to-grid/lines is intentionally
 * omitted (those require live bview state and are not relevant for bounds
 * or selection queries).
 *
 * Returns 0 on success, -1 if the snapshot is NULL or has zero dimensions.
 */
static int
_bsg_screen_to_view(const struct bsg_camera_snapshot *snap,
		    fastf_t *fx, fastf_t *fy,
		    fastf_t x, fastf_t y)
{
    if (!snap || snap->width == 0 || snap->height == 0)
	return -1;

    if (fx)
	*fx = x / (fastf_t)snap->width * 2.0 - 1.0;

    if (fy) {
	fastf_t ty = y / (fastf_t)snap->height * -2.0 + 1.0;
	*fy = (snap->aspect > 0.0) ? ty / snap->aspect : ty;
    }

    return 0;
}


/* ====================================================================
 * Scene radius accumulation
 * ==================================================================== */

struct _bsg_scene_radius_ctx {
    int     *have_objs;
    vect_t  *min;
    vect_t  *max;
    struct bview *v; /* may be NULL — used only for LoD cursor lookup */
};

/**
 * Recursive DFS over the BSG subtree rooted at @p n.
 * For every childless node (leaf) bsg_node_compute_bound() is called and
 * the result is accumulated into ctx->min / ctx->max.
 */
static void
_bsg_accumulate_bounds(const struct _bsg_scene_radius_ctx *ctx, bsg_node *n)
{
    if (!n)
	return;

    size_t child_cnt = bsg_node_child_count(n);

    if (child_cnt == 0) {
	if (bsg_node_compute_bound(n, ctx->v)) {
	    *ctx->have_objs = 1;
	    point_t bmin, bmax;
	    bsg_node_bounds_get(n, bmin, bmax);
	    VMIN(*ctx->min, bmin);
	    VMAX(*ctx->max, bmax);
	}
    } else {
	size_t i;
	for (i = 0; i < child_cnt; i++)
	    _bsg_accumulate_bounds(ctx, bsg_node_child(n, (size_t)i));
    }
}

/**
 * Return the scene bounding sphere (centre @p sbbc, radius @p *radius) by
 * traversing the BSG subtree rooted at @p root.  If no objects have geometry
 * the radius is left at 1.0 and sbbc at the origin.
 */
static void
_bsg_scene_radius(point_t *sbbc, fastf_t *radius,
		  bsg_node *root, struct bview *v)
{
    if (!sbbc || !radius || !root)
	return;

    VSET(*sbbc, 0, 0, 0);
    *radius = 1.0;

    vect_t min, max, work;
    VSETALL(min,  INFINITY);
    VSETALL(max, -INFINITY);
    int have_objs = 0;

    struct _bsg_scene_radius_ctx ctx;
    ctx.have_objs = &have_objs;
    ctx.min       = &min;
    ctx.max       = &max;
    ctx.v         = v;
    _bsg_accumulate_bounds(&ctx, root);

    if (have_objs) {
	VADD2SCALE(*sbbc, max, min, 0.5);
	VSUB2SCALE(work, max, min, 0.5);
	(*radius) = MAGNITUDE(work);
    }
}


/* ====================================================================
 * OBB construction helper
 * ==================================================================== */

/**
 * Build an oriented bounding box from the scene sphere (sbbc / radius),
 * the look direction (dir, unit vector INTO scene), the view-centre model-
 * space point (ec), and the two edge mid-points (ep1, ep2).
 *
 * The geometry matches the view_obb() helper in libbv/lod.cpp, allowing
 * BSG callers to replicate libbv behaviour without linking to libbv.
 */
static void
_bsg_build_obb(point_t *obb_c,
	       vect_t  *obb_e1,
	       vect_t  *obb_e2,
	       vect_t  *obb_e3,
	       const point_t sbbc, fastf_t radius,
	       const vect_t dir,
	       const point_t ec,
	       const point_t ep1,
	       const point_t ep2)
{
    /* Box centre: closest point to the view-centre on the plane defined
     * by the scene's bounding-sphere centre and the look direction.
     * bg_plane_pt_nrml takes non-const args, so copy to locals first. */
    plane_t p;
    point_t lsbbc;
    vect_t  ldir;
    VMOVE(lsbbc, sbbc);
    VMOVE(ldir,  dir);
    bg_plane_pt_nrml(&p, lsbbc, ldir);
    fastf_t pu, pv;
    point_t lec;
    VMOVE(lec, ec);
    bg_plane_closest_pt(&pu, &pv, &p, &lec);
    bg_plane_pt_at(obb_c, &p, pu, pv);

    /* Extent 1: scene radius along the look direction. */
    vect_t d;
    VMOVE(d, dir);
    VSCALE(d, d, radius);
    VMOVE(*obb_e1, d);

    /* Extents 2 and 3: derived from the pixel/window edge mid-points. */
    VSUB2(*obb_e2, ep1, ec);
    VSUB2(*obb_e3, ep2, ec);
}


/* ====================================================================
 * bsg_node_compute_bound
 * ==================================================================== */

int
bsg_node_compute_bound(bsg_node *n, struct bview *v)
{
    if (!n)
	return 0;

    struct bv_scene_obj *sp = (struct bv_scene_obj *)n;
    struct bv_scene_obj *s  = sp;
    int calc = 0;

    /* Reset bmin/bmax on the original input node. */
    {
	point_t inf_pt, neg_inf_pt;
	VSET(inf_pt,     INFINITY,  INFINITY,  INFINITY);
	VSET(neg_inf_pt, -INFINITY, -INFINITY, -INFINITY);
	bsg_node_bounds_set(n, inf_pt, neg_inf_pt);
    }

    /* ---- LoD resolution ------------------------------------------ */
    /* Determine whether this node or its parent is a BSG_NODE_LOD proxy
     * and redirect 's' to the currently active level child. */
    bsg_node *lod = NULL;
    if (bsg_node_has_kind(n, BSG_NODE_LOD)) {
	lod = n;
    } else {
	bsg_node *parent = bsg_node_parent(n);
	if (parent && bsg_node_has_kind(parent, BSG_NODE_LOD))
	    lod = parent;
    }

    if (lod) {
	int nlevels = bsg_lod_node_level_count(lod);
	int active  = (v) ? bsg_lod_node_active_level(lod, v) : 0;
	if (nlevels > 0) {
	    if (active < 0 || active >= nlevels)
		active = 0;
	    bsg_node *ls = bsg_node_child(lod, (size_t)active);
	    if (ls) {
		s = (struct bv_scene_obj *)ls;
		/* If the level child already has valid cached bounds, use
		 * them directly. */
		point_t lmin, lmax;
		bsg_node_bounds_get(ls, lmin, lmax);
		if (isfinite(lmin[X]) && isfinite(lmin[Y]) && isfinite(lmin[Z]) &&
		    isfinite(lmax[X]) && isfinite(lmax[Y]) && isfinite(lmax[Z])) {
		    calc = 1;
		}
	    }
	}
    }

    /* ---- Mesh LoD payload (cached bounds in mesh-local space) ----- */
    if (!calc) {
	struct bsg_payload *pl = bsg_node_payload_get((bsg_node *)s);
	if (pl) {
	    const struct bv_mesh_lod *mld = bsg_payload_mesh_lod_get(pl);
	    if (mld) {
		mat_t smat;
		bsg_node_transform_get((const bsg_node *)s, smat);
		/* Transform the two diagonal corners from mesh-local to
		 * model space.  This is the same approximation as
		 * bv_scene_obj_bound(); transforming only the two AABB
		 * corners is not perfectly tight for rotated instances but
		 * matches the legacy behaviour. */
		MAT4X3PNT(s->bmin, smat, mld->bmin);
		MAT4X3PNT(s->bmax, smat, mld->bmax);
		calc = 1;
	    }
	}
    }

    /* ---- Vlist geometry ------------------------------------------- */
    if (!calc) {
	struct bu_list *vhead = bsg_node_vlist_head((bsg_node *)s);
	if (vhead && bu_list_len(vhead)) {
	    bsg_vlist_bbox(vhead, &s->bmin, &s->bmax);
	    calc = 1;
	}
    }

    /* ---- Store results and propagate to original node ------------- */
    if (calc) {
	point_t bmin, bmax;
	VMOVE(bmin, s->bmin);
	VMOVE(bmax, s->bmax);

	vect_t center;
	center[X] = (bmin[X] + bmax[X]) * 0.5;
	center[Y] = (bmin[Y] + bmax[Y]) * 0.5;
	center[Z] = (bmin[Z] + bmax[Z]) * 0.5;
	bsg_node_center_set((bsg_node *)s, center);

	fastf_t sz = bmax[X] - bmin[X];
	V_MAX(sz, bmax[Y] - bmin[Y]);
	V_MAX(sz, bmax[Z] - bmin[Z]);
	bsg_node_size_set((bsg_node *)s, sz);

	/* If LoD resolution redirected us to a different child, propagate
	 * the computed bounds back to the original input node. */
	if ((bsg_node *)s != n) {
	    bsg_node_bounds_set(n, bmin, bmax);
	    bsg_node_center_set(n, center);
	    bsg_node_size_set(n, sz);
	}

	return 1;
    }

    return 0;
}


/* ====================================================================
 * bsg_view_compute_bounds
 * ==================================================================== */

int
bsg_view_compute_bounds(struct bsg_view_bounds_result *out,
			bsg_node *root,
			const struct bsg_camera_snapshot *snap)
{
    if (!out || !snap || snap->width == 0 || snap->height == 0)
	return -1;

    memset(out, 0, sizeof(*out));

    /* Gather scene bounding sphere from the BSG tree. */
    point_t sbbc = VINIT_ZERO;
    fastf_t radius = 1.0;
    _bsg_scene_radius(&sbbc, &radius, root, NULL);

    out->radius      = radius;
    out->have_result = (root != NULL);

    /* Compute view-space coordinates for viewport corners and the centre. */
    int w = snap->width;
    int h = snap->height;
    int xc = (int)(w * 0.5);
    int yc = (int)(h * 0.5);

    fastf_t x1 = 0.0, y1 = 0.0;   /* top edge mid-point */
    fastf_t x2 = 0.0, y2 = 0.0;   /* right edge mid-point */
    fastf_t xcf = 0.0, ycf = 0.0; /* view centre */
    _bsg_screen_to_view(snap, &x1, &y1, (fastf_t)xc, (fastf_t)h);
    _bsg_screen_to_view(snap, &x2, &y2, (fastf_t)w,  (fastf_t)yc);
    _bsg_screen_to_view(snap, &xcf, &ycf, (fastf_t)xc, (fastf_t)yc);

    /* Compute window bounds (view-space AABB of the screen). */
    fastf_t w0 = 0.0, w1 = 0.0, w2 = 0.0, w3 = 0.0;
    _bsg_screen_to_view(snap, &w0, &w1, 0.0, 0.0);
    _bsg_screen_to_view(snap, &w2, &w3, (fastf_t)w, (fastf_t)h);
    out->wmin[0] = (w0 < w2) ? w0 : w2;
    out->wmin[1] = (w1 < w3) ? w1 : w3;
    out->wmax[0] = (w0 > w2) ? w0 : w2;
    out->wmax[1] = (w1 > w3) ? w1 : w3;

    /* Transform view-space edge and centre points to model space. */
    point_t vp1, vp2, vc, ep1, ep2, ec;
    VSET(vp1, x1,  y1,  0);
    VSET(vp2, x2,  y2,  0);
    VSET(vc,  xcf, ycf, 0);
    MAT4X3PNT(ep1, snap->view2model, vp1);
    MAT4X3PNT(ep2, snap->view2model, vp2);
    MAT4X3PNT(ec,  snap->view2model, vc);

    /* Look direction: use the pre-computed unit look vector from the snapshot.
     * This matches bv_view_bounds() which extracts VMOVEN(dir, gv_rotation+8)
     * and negates it — snap->look_dir is derived the same way in camera.c. */
    VMOVE(out->lookat, snap->look_dir);

    /* Backed-out eye position: scene centre offset backward by radius. */
    vect_t dir;
    VMOVE(dir, snap->look_dir);
    VSCALE(dir, dir, -radius);
    VADD2(out->vc_backout, sbbc, dir);

    /* Build OBB only for orthographic views. */
    if (snap->projection == BSG_CAMERA_ORTHO) {
	_bsg_build_obb(&out->obb_center,
		       &out->obb_extent1,
		       &out->obb_extent2,
		       &out->obb_extent3,
		       sbbc, radius,
		       snap->look_dir,
		       ec, ep1, ep2);
    }

    return 0;
}


/* ====================================================================
 * Selection helpers
 * ==================================================================== */

struct _bsg_select_ctx {
    struct bu_ptbl *sset;
    const struct bsg_camera_snapshot *snap;
    point_t obb_c;
    vect_t  obb_e1;
    vect_t  obb_e2;
    vect_t  obb_e3;
};

/**
 * Recursive DFS: for every leaf node, compute its AABB and test it against
 * the OBB in ctx.  Matching leaves are appended to ctx->sset.
 */
static void
_bsg_find_active(struct _bsg_select_ctx *ctx, bsg_node *n)
{
    if (!n)
	return;

    size_t child_cnt = bsg_node_child_count(n);

    if (child_cnt == 0) {
	/* Only test against OBB when the node actually has geometry. */
	if (bsg_node_compute_bound(n, NULL)) {
	    point_t bmin, bmax;
	    bsg_node_bounds_get(n, bmin, bmax);
	    if (bg_sat_aabb_obb(bmin, bmax,
			       ctx->obb_c, ctx->obb_e1,
			       ctx->obb_e2, ctx->obb_e3))
		bu_ptbl_ins(ctx->sset, (long *)n);
	}
    } else {
	size_t i;
	for (i = 0; i < child_cnt; i++)
	    _bsg_find_active(ctx, bsg_node_child(n, i));
    }
}

/**
 * Shared OBB-construction and search core used by both bsg_view_select()
 * and bsg_view_rect_select().
 *
 * Parameters ep1, ep2, ec are model-space edge mid-points and view centre
 * for the pixel or rectangle being queried.  sbbc / radius describe the
 * scene bounding sphere.
 */
static int
_bsg_obb_select(struct bu_ptbl *sset, bsg_node *root,
		const struct bsg_camera_snapshot *snap,
		const point_t sbbc, fastf_t radius,
		const point_t ec, const point_t ep1, const point_t ep2)
{
    point_t obb_c;
    vect_t  obb_e1, obb_e2, obb_e3;

    _bsg_build_obb(&obb_c, &obb_e1, &obb_e2, &obb_e3,
		   sbbc, radius, snap->look_dir,
		   ec, ep1, ep2);

    struct _bsg_select_ctx ctx;
    ctx.sset = sset;
    ctx.snap = snap;
    VMOVE(ctx.obb_c,  obb_c);
    VMOVE(ctx.obb_e1, obb_e1);
    VMOVE(ctx.obb_e2, obb_e2);
    VMOVE(ctx.obb_e3, obb_e3);

    _bsg_find_active(&ctx, root);

    return (int)BU_PTBL_LEN(sset);
}


/* ====================================================================
 * bsg_view_select
 * ==================================================================== */

int
bsg_view_select(struct bu_ptbl *sset,
		bsg_node *root,
		const struct bsg_camera_snapshot *snap,
		int x, int y)
{
    if (!sset || !snap || snap->width == 0 || snap->height == 0)
	return 0;
    if (x < 0 || y < 0 || x > snap->width || y > snap->height)
	return 0;

    bu_ptbl_reset(sset);

    /* Scene bounding sphere. */
    point_t sbbc = VINIT_ZERO;
    fastf_t radius = 1.0;
    _bsg_scene_radius(&sbbc, &radius, root, NULL);

    /* Map the single pixel (plus a one-pixel step in each direction) to
     * view space — this produces the "pixel + 1" box used in libbv. */
    fastf_t x1 = 0.0, y1 = 0.0;
    fastf_t x2 = 0.0, y2 = 0.0;
    fastf_t xc = 0.0, yc = 0.0;
    _bsg_screen_to_view(snap, &x1, &y1, (fastf_t)x,   (fastf_t)(y + 1));
    _bsg_screen_to_view(snap, &x2, &y2, (fastf_t)(x + 1), (fastf_t)y);
    _bsg_screen_to_view(snap, &xc, &yc, (fastf_t)x,   (fastf_t)y);

    /* Convert to model space. */
    point_t vp1, vp2, vc, ep1, ep2, ec;
    VSET(vp1, x1, y1, 0);
    VSET(vp2, x2, y2, 0);
    VSET(vc,  xc, yc, 0);
    MAT4X3PNT(ep1, snap->view2model, vp1);
    MAT4X3PNT(ep2, snap->view2model, vp2);
    MAT4X3PNT(ec,  snap->view2model, vc);

    return _bsg_obb_select(sset, root, snap, sbbc, radius, ec, ep1, ep2);
}


/* ====================================================================
 * bsg_view_rect_select
 * ==================================================================== */

int
bsg_view_rect_select(struct bu_ptbl *sset,
		     bsg_node *root,
		     const struct bsg_camera_snapshot *snap,
		     int x1, int y1, int x2, int y2)
{
    if (!sset || !snap || snap->width == 0 || snap->height == 0)
	return 0;
    if (x1 < 0 || y1 < 0 || x1 > snap->width || y1 > snap->height)
	return 0;
    if (x2 < 0 || y2 < 0 || x2 > snap->width || y2 > snap->height)
	return 0;

    bu_ptbl_reset(sset);

    /* Scene bounding sphere. */
    point_t sbbc = VINIT_ZERO;
    fastf_t radius = 1.0;
    _bsg_scene_radius(&sbbc, &radius, root, NULL);

    /* Map rectangle mid-points and centre to view space. */
    int xmid = (int)(0.5 * (x1 + x2));
    int ymid = (int)(0.5 * (y1 + y2));

    fastf_t fx1 = 0.0, fy1 = 0.0;
    fastf_t fx2 = 0.0, fy2 = 0.0;
    fastf_t fxc = 0.0, fyc = 0.0;
    _bsg_screen_to_view(snap, &fx1, &fy1, (fastf_t)xmid, (fastf_t)y2);
    _bsg_screen_to_view(snap, &fx2, &fy2, (fastf_t)x2,   (fastf_t)ymid);
    _bsg_screen_to_view(snap, &fxc, &fyc, (fastf_t)xmid, (fastf_t)ymid);

    /* Convert to model space. */
    point_t vp1, vp2, vc, ep1, ep2, ec;
    VSET(vp1, fx1, fy1, 0);
    VSET(vp2, fx2, fy2, 0);
    VSET(vc,  fxc, fyc, 0);
    MAT4X3PNT(ep1, snap->view2model, vp1);
    MAT4X3PNT(ep2, snap->view2model, vp2);
    MAT4X3PNT(ec,  snap->view2model, vc);

    return _bsg_obb_select(sset, root, snap, sbbc, radius, ec, ep1, ep2);
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
