/*                          S N A P . C
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
/** @file libbsg/snap.c
 *
 * Slice 10 (bv_scene_obj_migrate): BSG-native snap implementation.
 *
 * Provides BSG-native replacements for bv_snap_lines_3d,
 * bv_snap_lines_2d, and bv_snap_grid_2d.  All functions operate on
 * bsg_node trees and bsg_camera_snapshot; no direct libbv function
 * calls are made.
 */

#include "common.h"

#include <float.h>
#include <math.h>
#include <string.h>

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bn/tol.h"
#include "bg/lseg.h"
#include "bv/vlist.h"
#include "bsg/camera.h"
#include "bsg/defines.h"
#include "bsg/hud.h"
#include "bsg/node.h"
#include "bsg/payload.h"
#include "bsg/settings.h"
#include "bsg/snap.h"
#include "bsg/visit.h"


/* ---------------------------------------------------------------------- */
/* Internal closest-point-on-lines accumulator                             */
/* ---------------------------------------------------------------------- */

struct _bsg_cp_info {
    double ctol_sq; /**< @brief squared tolerance: points farther than this are ignored */
    point_t cp;     /**< @brief closest point on closest line */
    double dsq;     /**< @brief squared distance to closest line */
    point_t cp2;    /**< @brief closest point on second-closest line */
    double dsq2;    /**< @brief squared distance to second-closest line */
};

#define _BSG_CP_INFO_INIT {BN_TOL_DIST, VINIT_ZERO, DBL_MAX, VINIT_ZERO, DBL_MAX}


/**
 * Compute the line-snapping tolerance (squared) for a node with a
 * given line width in pixels.
 *
 * The tolerance is proportional to the view size and scaled by
 * @p snap_tol_factor.  Returns 100*100 as a safe fallback when
 * viewport dimensions are zero.
 */
static double
_snap_line_tol_sq(const struct bsg_camera_snapshot *snap,
		  int lwidth,
		  double snap_tol_factor)
{
    if (!snap || lwidth <= 0)
	return 100.0 * 100.0;

    int width  = snap->width;
    int height = snap->height;
    if (!width || !height)
	return 100.0 * 100.0;

    double lavg   = ((double)width + (double)height) * 0.5;
    double lratio = ((double)lwidth) / lavg;
    double lrsize = snap->size * lratio * snap_tol_factor;
    return lrsize * lrsize;
}


/**
 * Scan the vlist of @p n, updating @p s with the closest and
 * second-closest points on any line segment within @p s->ctol_sq of
 * the world-space query point @p p.
 *
 * Returns the number of improvements made (0, 1, or 2), matching
 * the convention used by libbv/snap.c.
 */
static int
_find_closest_node_point(struct _bsg_cp_info *s,
			 point_t *p,
			 bsg_node *n)
{
    int ret = 0;
    if (!s || !p || !n)
	return 0;

    struct bu_list *vhead = bsg_node_vlist_head(n);
    if (!vhead || !bu_list_len(vhead))
	return 0;

    struct bv_vlist *tvp;
    for (BU_LIST_FOR(tvp, bv_vlist, vhead)) {
	int     nused = tvp->nused;
	int    *cmd   = tvp->cmd;
	point_t *pt   = tvp->pt;
	point_t *pt1  = NULL;
	point_t *pt2  = NULL;

	for (int i = 0; i < nused; i++, cmd++, pt++) {
	    switch (*cmd) {
		case BV_VLIST_LINE_MOVE:
		    pt2 = pt;
		    break;
		case BV_VLIST_LINE_DRAW:
		    pt1 = pt2;
		    pt2 = pt;
		    break;
		default:
		    break;
	    }

	    if (pt1 && pt2) {
		point_t c;
		double dsq = bg_distsq_lseg3_pt(&c, *pt1, *pt2, *p);

		if (dsq > s->ctol_sq)
		    continue;

		if (s->dsq > dsq) {
		    /* New closest: old closest becomes second-closest. */
		    VMOVE(s->cp2, s->cp);
		    s->dsq2 = s->dsq;
		    VMOVE(s->cp, c);
		    s->dsq = dsq;
		    ret = 1;
		    continue;
		}
		if (s->dsq2 > dsq) {
		    VMOVE(s->cp2, c);
		    s->dsq2 = dsq;
		    ret = 2;
		    continue;
		}
	    }
	}
    }

    return ret;
}


/* ---------------------------------------------------------------------- */
/* Snap-type predicate                                                     */
/* ---------------------------------------------------------------------- */

/**
 * Return non-zero when node @p n should be considered for snapping
 * given @p snap_flags.
 *
 * When @p snap_flags is 0 every node is accepted.
 *
 * The BSG_SNAP_DB flag selects DB-backed shape nodes
 * (BSG_NODE_SHAPE).  The BSG_SNAP_VIEW flag selects raw vlist overlay
 * nodes (BSG_NODE_VLIST).  BSG_SNAP_SHARED and BSG_SNAP_LOCAL are
 * direction hints for the caller's traversal scope; this predicate
 * maps them to BSG_NODE_VIEW_SCOPE containment, which is not inspected
 * here — callers that care about shared/local scope must filter at the
 * scope node level before calling this function.
 */
static int
_snap_node_accepted(const bsg_node *n, int snap_flags)
{
    if (!n)
	return 0;
    if (!snap_flags)
	return 1;  /* 0 = accept all */

    /* DB objects: BSG_NODE_SHAPE */
    if ((snap_flags & BSG_SNAP_DB) && bsg_node_has_kind(n, BSG_NODE_SHAPE))
	return 1;

    /* View-only overlays: BSG_NODE_VLIST */
    if ((snap_flags & BSG_SNAP_VIEW) && bsg_node_has_kind(n, BSG_NODE_VLIST))
	return 1;

    /* No flag matched */
    return 0;
}


/* ---------------------------------------------------------------------- */
/* bsg_visit callback context for line-snap traversal                      */
/* ---------------------------------------------------------------------- */

struct _bsg_snap_visit_ctx {
    struct _bsg_cp_info      *s;
    point_t                  *p;
    int                      *ret;
    const struct bsg_snap_params *params;
    const struct bsg_camera_snapshot *snap;
};

static int
_snap_visit_cb(bsg_node *n, void *data)
{
    struct _bsg_snap_visit_ctx *ctx = (struct _bsg_snap_visit_ctx *)data;
    if (!n || !ctx)
	return 1;  /* continue */

    /* Apply snap-flag filter */
    if (!_snap_node_accepted(n, ctx->params ? ctx->params->snap_flags : 0))
	return 1;

    /* Per-node tolerance: use the node's line width if available. */
    struct bsg_settings node_s;
    memset(&node_s, 0, sizeof(node_s));
    bsg_node_settings_get(n, &node_s);
    int lw = node_s.line_width ? node_s.line_width : 1;

    double tol_factor = ctx->params ? ctx->params->snap_tol_factor : 1.0;
    ctx->s->ctol_sq = _snap_line_tol_sq(ctx->snap, lw, tol_factor);

    *ctx->ret += _find_closest_node_point(ctx->s, ctx->p, n);
    return 1;  /* continue */
}


/* ---------------------------------------------------------------------- */
/* Public API                                                               */
/* ---------------------------------------------------------------------- */

void
bsg_snap_params_init(struct bsg_snap_params *p)
{
    if (!p)
	return;
    p->snap_flags       = 0;
    p->snap_tol_factor  = 1.0;
    p->snap_candidates  = NULL;
}


int
bsg_snap_lines_3d(point_t *out_pt,
		  const struct bsg_camera_snapshot *snap,
		  bsg_node *scene_root,
		  const struct bsg_snap_params *params,
		  point_t *p_in)
{
    int ret = 0;
    if (!out_pt || !snap || !p_in)
	return 0;

    struct _bsg_cp_info cpinfo = _BSG_CP_INFO_INIT;

    int snap_flags      = params ? params->snap_flags       : 0;
    double tol_factor   = params ? params->snap_tol_factor  : 1.0;
    struct bu_ptbl *cands = params ? params->snap_candidates : NULL;

    if (cands && BU_PTBL_LEN(cands) > 0) {
	/*
	 * Explicit candidate list: iterate and filter by snap_flags.
	 */
	for (size_t i = 0; i < BU_PTBL_LEN(cands); i++) {
	    bsg_node *n = (bsg_node *)BU_PTBL_GET(cands, i);
	    if (!_snap_node_accepted(n, snap_flags))
		continue;

	    struct bsg_settings node_s;
	    memset(&node_s, 0, sizeof(node_s));
	    bsg_node_settings_get(n, &node_s);
	    int lw = node_s.line_width ? node_s.line_width : 1;
	    cpinfo.ctol_sq = _snap_line_tol_sq(snap, lw, tol_factor);
	    ret += _find_closest_node_point(&cpinfo, p_in, n);
	}
    } else if (scene_root) {
	/*
	 * No explicit candidates: DFS over scene tree.
	 */
	struct _bsg_snap_visit_ctx ctx;
	ctx.s      = &cpinfo;
	ctx.p      = p_in;
	ctx.ret    = &ret;
	ctx.params = params;
	ctx.snap   = snap;
	bsg_visit(scene_root, 0, _snap_visit_cb, &ctx);
    }

    if (ret) {
	VMOVE(*out_pt, cpinfo.cp);
	return 1;
    }

    return 0;
}


int
bsg_snap_lines_2d(const struct bsg_camera_snapshot *snap,
		  bsg_node *scene_root,
		  const struct bsg_snap_params *params,
		  fastf_t *vx, fastf_t *vy)
{
    if (!snap || !vx || !vy)
	return 0;

    /* Convert 2D view coords to 3D world space. */
    point_t vp = VINIT_ZERO;
    VSET(vp, *vx, *vy, 0.0);
    point_t p = VINIT_ZERO;
    MAT4X3PNT(p, snap->view2model, vp);

    point_t out_pt = VINIT_ZERO;
    if (bsg_snap_lines_3d(&out_pt, snap, scene_root, params, &p) == 1) {
	MAT4X3PNT(vp, snap->model2view, out_pt);
	*vx = vp[X];
	*vy = vp[Y];
	return 1;
    }

    return 0;
}


int
bsg_snap_grid_2d(const struct bsg_camera_snapshot *snap,
		 const struct bsg_grid_state *grid,
		 fastf_t *vx, fastf_t *vy)
{
    if (!snap || !grid || !vx || !vy)
	return 0;

    if (ZERO(grid->res_h) || ZERO(grid->res_v))
	return 0;

    fastf_t inv_grid_res_h = 1.0 / (grid->res_h * snap->base2local);
    fastf_t inv_grid_res_v = 1.0 / (grid->res_v * snap->base2local);

    point_t view_pt = VINIT_ZERO;
    VSET(view_pt, *vx, *vy, 0.0);
    VSCALE(view_pt, view_pt, snap->scale);

    /* Project grid anchor into view space */
    point_t anchor_model = VINIT_ZERO;
    VMOVE(anchor_model, grid->anchor);
    point_t grid_origin = VINIT_ZERO;
    MAT4X3PNT(grid_origin, snap->model2view, anchor_model);
    VSCALE(grid_origin, grid_origin, snap->scale);

    fastf_t grid_units_h = (view_pt[X] - grid_origin[X]) * inv_grid_res_h;
    fastf_t grid_units_v = (view_pt[Y] - grid_origin[Y]) * inv_grid_res_v;

    /* Extract the integer (whole) and fractional parts in grid-unit space.
     * Note: the floor/round calls return 'double'; we must assign to a
     * double first before casting to int to avoid -Wbad-function-cast. */
    double _floor_h = floor((double)grid_units_h);
    double _floor_v = floor((double)grid_units_v);
    int nh = (int)_floor_h;
    int nv = (int)_floor_v;

    grid_units_h -= (fastf_t)nh;   /* fractional part only */
    grid_units_v -= (fastf_t)nv;

    double _round_h = round((double)grid_units_h);
    double _round_v = round((double)grid_units_v);
    int hstep = (int)_round_h;
    int vstep = (int)_round_v;

    fastf_t fnh = (fastf_t)(nh + hstep) + grid_origin[X];
    fastf_t fnv = (fastf_t)(nv + vstep) + grid_origin[Y];

    VSET(view_pt,
	 fnh * grid->res_h * snap->base2local,
	 fnv * grid->res_v * snap->base2local,
	 0.0);
    VSCALE(view_pt, view_pt, 1.0 / snap->scale);

    *vx = view_pt[X];
    *vy = view_pt[Y];

    return 1;
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
