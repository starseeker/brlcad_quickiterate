/*                         S N A P . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2026 United States Government as represented by
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
/** @file libbv/snap.c
 *
 * Logic for snapping points to visual elements.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include "bu/opt.h"
#include "bu/ptbl.h"
#include "bu/vls.h"
#include "bn/tol.h"
#include "bg/lseg.h"
#include "bv/defines.h"
#include "bv/snap.h"
#include "bv/util.h"
#include "bv/vlist.h"

struct bv_cp_info {
    double ctol_sq; // square of the distance that defines "close to a line"
    point_t cp;  // closest point on closest line
    double dsq;  // squared distance to closest line
    point_t cp2;  // closest point on closest line
    double dsq2; // squared distance to 2nd closest line
};
#define BV_CP_INFO_INIT {BN_TOL_DIST, VINIT_ZERO, DBL_MAX, VINIT_ZERO, DBL_MAX}

/* Phase T-final (drawing_stack_modernization): the legacy gv_tcl
 * data_line_state snapping helpers (bv_cp_info_tcl /
 * _find_closest_tcl_point / _find_close_isect_tcl) were removed.  Tcl
 * data_lines / sdata_lines are now stored as BSG VIEW_SCOPE line objects
 * (`_tcl_data_lines`, `_tcl_sdata_lines`) and snapped through the same
 * vlist-based path used for every other view-only line object. */

static int
_find_closest_obj_point(struct bv_cp_info *s, point_t *p, struct bv_scene_obj *o)
{
    int ret = 0;
    if (!s || !p || !o)
	return 0;
    if (!bu_list_len(&o->s_vlist))
	return 0;

    struct bv_vlist *tvp;
    for (BU_LIST_FOR(tvp, bv_vlist, &o->s_vlist)) {
	int nused = tvp->nused;
	int *cmd = tvp->cmd;
	point_t *pt = tvp->pt;
	point_t *pt1 = NULL;
	point_t *pt2 = NULL;
	for (int i = 0; i < nused; i++, cmd++, pt++) {
	    switch (*cmd) {
		case BV_VLIST_LINE_MOVE:
		    pt2 = pt;
		    break;
		case BV_VLIST_LINE_DRAW:
		    pt1 = pt2;
		    pt2 = pt;
		    break;
	    }
	    if (pt1 && pt2) {
		point_t c;
		double dsq = bg_distsq_lseg3_pt(&c, *pt1, *pt2, *p);
		// If we're outside tolerance, continue
		if (dsq > s->ctol_sq) {
		    continue;
		}
		// If this is the closest we've seen, record it
		if (s->dsq > dsq) {
		    // Closest is now second closest
		    VMOVE(s->cp2, s->cp);
		    s->dsq2 = s->dsq;

		    // set new closest
		    VMOVE(s->cp, c);
		    s->dsq = dsq;
		    ret = 1;
		    continue;
		}
		// Not the closest - is it closer than the second closest?
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

static double
line_tol_sq(struct bview *v, int lwidth)
{
    if (!v || lwidth <= 0)
	return 100*100;

    // NOTE - make sure calling applications update these values from
    // the display manager info before command execution.
    int width = v->gv_width;
    int height = v->gv_height;

    if (!width || !height)
	return 100*100;

    double lavg = ((double)width + (double)height) * 0.5;
    double lratio = ((double)lwidth)/lavg;

    struct bview_settings *gv_s = (v->gv_s) ? v->gv_s : &v->gv_ls;
    double lrsize = v->gv_size * lratio * gv_s->gv_snap_tol_factor;

    return lrsize*lrsize;
}

/* Phase B: context for snap BV_DB_OBJS bv_view_objs_visit_db callback. */
struct _bv_snap_db_ctx {
    struct bv_cp_info *s;
    point_t *p;
    int *ret;
};

static int
_bv_snap_db_obj_cb(struct bv_scene_obj *so, void *data)
{
    struct _bv_snap_db_ctx *ctx = (struct _bv_snap_db_ctx *)data;
    *ctx->ret += _find_closest_obj_point(ctx->s, ctx->p, so);
    return 1;
}

/* Phase A0 (drawing_stack_modernization): callback for snap_lines view-only
 * scope iteration via bv_view_obj_visit. */
static int
_bv_snap_view_obj_cb(struct bv_scene_obj *so, void *data)
{
    struct _bv_snap_db_ctx *ctx = (struct _bv_snap_db_ctx *)data;
    *ctx->ret += _find_closest_obj_point(ctx->s, ctx->p, so);
    return 1;
}

int
bv_snap_lines_3d(point_t *out_pt, struct bview *v, point_t *p)
{
    int ret = 0;
    struct bview_settings *gv_s = (v->gv_s) ? v->gv_s : &v->gv_ls;
    struct bv_cp_info cpinfo = BV_CP_INFO_INIT;

    if (!p || !v) return 0;

    // If we're not in Tcl mode only, we are looking at objects - either
    // all of them, or a specified subset
    if (gv_s->gv_snap_flags != BV_SNAP_TCL) {
	struct bv_cp_info *s = &cpinfo;
	s->ctol_sq = line_tol_sq(v, 1);
	if (BU_PTBL_LEN(&gv_s->gv_snap_objs) > 0) {
	    for (size_t i = 0; i < BU_PTBL_LEN(&gv_s->gv_snap_objs); i++) {
		struct bv_scene_obj *so = (struct bv_scene_obj *)BU_PTBL_GET(&gv_s->gv_snap_objs, i);
		if (gv_s->gv_snap_flags) {
		if (gv_s->gv_snap_flags == BV_SNAP_DB && (!(so->bsg.bsg_kind & BV_DB_OBJS)))
		    continue;
		if (gv_s->gv_snap_flags == BV_SNAP_VIEW && (!(so->bsg.bsg_kind & BV_VIEW_OBJS)))
		    continue;
	    }
	    struct bsg_settings so_settings = BSG_SETTINGS_INIT;
	    (void)bv_scene_obj_settings_get(so, &so_settings);
	    s->ctol_sq = line_tol_sq(v, (so_settings.line_width) ? so_settings.line_width : 1);
	    ret += _find_closest_obj_point(s, p, so);
	}
	} else {
	    if (!gv_s->gv_snap_flags || (gv_s->gv_snap_flags & BV_SNAP_DB)) {
		/* Phase B: use bv_view_objs_visit_db to traverse BSG tree when
		 * gv_draw_root is set; falls back to shared+local ptbls for
		 * non-GED consumers.  The BV_SNAP_SHARED/LOCAL sub-distinction
		 * is handled transparently by the helper's two-ptbl fallback. */
		struct _bv_snap_db_ctx snap_ctx;
		snap_ctx.s = s;
		snap_ctx.p = p;
		snap_ctx.ret = &ret;
		bv_view_objs_visit_db(v, _bv_snap_db_obj_cb, &snap_ctx);
	    }
	    if (!gv_s->gv_snap_flags || (gv_s->gv_snap_flags & BV_SNAP_VIEW)) {
		/* Phase A0 (drawing_stack_modernization): use bv_view_obj_visit
		 * for the view-only scope.  scope_mask honors the same
		 * BV_SNAP_SHARED / BV_SNAP_LOCAL distinction as the legacy
		 * ptbl scan. */
		int scope_mask = 0;
		if (!gv_s->gv_snap_flags || (gv_s->gv_snap_flags & BV_SNAP_SHARED))
		    scope_mask |= BV_VIEW_OBJ_SCOPE_SHARED;
		if (!gv_s->gv_snap_flags || (gv_s->gv_snap_flags & BV_SNAP_LOCAL))
		    scope_mask |= BV_VIEW_OBJ_SCOPE_LOCAL;
		if (scope_mask) {
		    struct _bv_snap_db_ctx snap_ctx;
		    snap_ctx.s = s;
		    snap_ctx.p = p;
		    snap_ctx.ret = &ret;
		    bv_view_obj_visit(v, scope_mask, _bv_snap_view_obj_cb, &snap_ctx);
		}
	    }
	}
    }

    // There are some issues with line snapping that don't come up with grid
    // snapping - in particular, when are we "close enough" to a line to snap,
    // and how do we handle snapping when close enough to multiple lines?  We
    // probably want to prefer intersections between lines to closest line
    // point if we are close to multiple lines...
    //
    // Phase T-final (drawing_stack_modernization): the legacy gv_tcl
    // data_lines / sdata_lines snap branch was removed.  After T1, the Tcl
    // data_lines state is mirrored into BSG view-scope objects
    // (`_tcl_data_lines`, `_tcl_sdata_lines`), which the BV_SNAP_VIEW
    // branch above already snaps against via bv_view_obj_visit.  The
    // BV_SNAP_TCL flag is now equivalent to BV_SNAP_VIEW and is retained
    // only for caller backward-compatibility.
    if (gv_s->gv_snap_flags == BV_SNAP_TCL) {
	int scope_mask = BV_VIEW_OBJ_SCOPE_ALL;
	struct _bv_snap_db_ctx snap_ctx;
	snap_ctx.s = &cpinfo;
	snap_ctx.p = p;
	snap_ctx.ret = &ret;
	bv_view_obj_visit(v, scope_mask, _bv_snap_view_obj_cb, &snap_ctx);
    }

    // If we found something, we can snap
    if (ret) {
	VMOVE(*out_pt, cpinfo.cp);
	return 1;
    }

    return 0;
}

int
bv_snap_lines_2d(struct bview *v, fastf_t *vx, fastf_t *vy)
{
    if (!v || !vx || !vy) return 0;

    point2d_t p2d = {0.0, 0.0};
    V2SET(p2d, *vx, *vy);
    point_t vp = VINIT_ZERO;
    VSET(vp, p2d[0], p2d[1], 0);
    point_t p = VINIT_ZERO;
    MAT4X3PNT(p, v->gv_view2model, vp);
    point_t out_pt = VINIT_ZERO;
    if (bv_snap_lines_3d(&out_pt, v, &p) == 1) {
	MAT4X3PNT(vp, v->gv_model2view, out_pt);
	(*vx) = vp[0];
	(*vy) = vp[1];
	return 1;
    }

    return 0;
}

void
bv_view_center_linesnap(struct bview *v)
{
    point_t view_pt;
    point_t model_pt;

    MAT_DELTAS_GET_NEG(model_pt, v->gv_center);
    MAT4X3PNT(view_pt, v->gv_model2view, model_pt);
    bv_snap_lines_2d(v, &view_pt[X], &view_pt[Y]);
    MAT4X3PNT(model_pt, v->gv_view2model, view_pt);
    MAT_DELTAS_VEC_NEG(v->gv_center, model_pt);
    bv_update(v);
}

int
bv_snap_grid_2d(struct bview *v, fastf_t *vx, fastf_t *vy)
{
    point_t view_pt;
    point_t grid_origin;
    fastf_t inv_grid_res_h, inv_grid_res_v;

    if (!v || !vx || !vy)
	return 0;

    struct bview_settings *gv_s = (v->gv_s) ? v->gv_s : &v->gv_ls;

    if (ZERO(gv_s->gv_grid.res_h) ||
	ZERO(gv_s->gv_grid.res_v))
	return 0;

    inv_grid_res_h = 1/(gv_s->gv_grid.res_h * v->gv_base2local);
    inv_grid_res_v = 1/(gv_s->gv_grid.res_v * v->gv_base2local);

    VSET(view_pt, *vx, *vy, 0.0);
    VSCALE(view_pt, view_pt, v->gv_scale);
    MAT4X3PNT(grid_origin, v->gv_model2view, gv_s->gv_grid.anchor);
    VSCALE(grid_origin, grid_origin, v->gv_scale);

    fastf_t grid_units_h = (view_pt[X] - grid_origin[X]) * inv_grid_res_h;
    fastf_t grid_units_v = (view_pt[Y] - grid_origin[Y]) * inv_grid_res_v;
    int nh, nv;		/* whole grid units */
    nh = floor(view_pt[X]);
    nv = floor(view_pt[Y]);
    grid_units_h -= nh;		/* now contains only the fraction part */
    grid_units_v -= nv;		/* now contains only the fraction part */
    int hstep = round(grid_units_h);
    int vstep = round(grid_units_v);
    fastf_t fnh = nh + hstep + grid_origin[X];
    fastf_t fnv = nv + vstep + grid_origin[Y];
    VSET(view_pt, fnh * gv_s->gv_grid.res_h * v->gv_base2local, fnv * gv_s->gv_grid.res_v * v->gv_base2local, 0.0);
    VSCALE(view_pt, view_pt, 1.0/v->gv_scale);

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
