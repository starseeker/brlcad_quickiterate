/*                S N A P _ A C T I O N . C
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
/** @file libbsg/snap_action.c */

#include "common.h"

#include "bu/malloc.h"
#include "bsg/snap.h"
#include "bsg/snap_action.h"

void
bsg_snap_result_init(struct bsg_snap_result *out)
{
    if (!out)
	return;
    out->sr_candidates = NULL;
    out->sr_cnt = 0;
}

void
bsg_snap_result_free(struct bsg_snap_result *out)
{
    if (!out)
	return;
    if (out->sr_candidates)
	bu_free((void *)out->sr_candidates, "bsg_snap_result candidates");
    out->sr_candidates = NULL;
    out->sr_cnt = 0;
}

static int
_bsg_snap_result_append(struct bsg_snap_result *out, const point_t p, bsg_snap_kind kind, fastf_t dist)
{
    if (!out)
	return 0;
    size_t ncnt = out->sr_cnt + 1;
    struct bsg_snap_candidate *nc =
	(struct bsg_snap_candidate *)bu_realloc((void *)out->sr_candidates,
		ncnt * sizeof(struct bsg_snap_candidate),
		"grow bsg_snap_result candidates");
    if (!nc)
	return 0;
    out->sr_candidates = nc;
    VMOVE(out->sr_candidates[out->sr_cnt].sc_point, p);
    out->sr_candidates[out->sr_cnt].sc_kind = kind;
    out->sr_candidates[out->sr_cnt].sc_distance = dist;
    out->sr_candidates[out->sr_cnt].sc_node = NULL;
    out->sr_cnt = ncnt;
    return 1;
}

int
bsg_snap_candidates(struct bsg_view *v, point_t sample, double tol,
		    bsg_snap_kind_mask kinds, struct bsg_snap_result *out)
{
    if (!v || !out)
	return 0;

    bsg_snap_result_free(out);
    bsg_snap_result_init(out);

    point_t sample_model = VINIT_ZERO;
    point_t snapped_model = VINIT_ZERO;
    VMOVE(sample_model, sample);
    VMOVE(snapped_model, sample_model);

    if ((kinds & BSG_SNAP_KIND_ENDPOINT) ||
	(kinds & BSG_SNAP_KIND_MIDPOINT) ||
	(kinds & BSG_SNAP_KIND_INTERSECTION) ||
	(kinds & BSG_SNAP_KIND_PERPENDICULAR) ||
	(kinds & BSG_SNAP_KIND_TANGENT) ||
	(kinds & BSG_SNAP_KIND_OVERLAY_HANDLE)) {
	if (bsg_snap_lines_3d(&snapped_model, v, &sample_model)) {
	    fastf_t dist = DIST_PNT_PNT(sample_model, snapped_model);
	    if (tol <= 0.0 || dist <= tol)
		_bsg_snap_result_append(out, snapped_model, BSG_SNAP_KIND_ENDPOINT, dist);
	}
    }

    if (kinds & BSG_SNAP_KIND_GRID) {
	point_t view_pt = VINIT_ZERO;
	point_t grid_model = VINIT_ZERO;
	fastf_t fx = 0.0;
	fastf_t fy = 0.0;

	MAT4X3PNT(view_pt, v->gv_model2view, sample_model);
	fx = view_pt[X];
	fy = view_pt[Y];
	if (bsg_snap_grid_2d(v, &fx, &fy)) {
	    VSET(view_pt, fx, fy, view_pt[Z]);
	    MAT4X3PNT(grid_model, v->gv_view2model, view_pt);
	    fastf_t dist = DIST_PNT_PNT(sample_model, grid_model);
	    if (tol <= 0.0 || dist <= tol)
		_bsg_snap_result_append(out, grid_model, BSG_SNAP_KIND_GRID, dist);
	}
    }

    return (int)out->sr_cnt;
}

int
bsg_snap_point_2d(struct bsg_view *v, fastf_t *vx, fastf_t *vy,
		  bsg_snap_kind_mask kinds)
{
    if (!v || !vx || !vy)
	return 0;

    /* Convert the 2D view-space sample to a 3D model point. */
    point_t view_pt = VINIT_ZERO;
    point_t model_pt = VINIT_ZERO;
    VSET(view_pt, *vx, *vy, 0.0);
    MAT4X3PNT(model_pt, v->gv_view2model, view_pt);

    struct bsg_snap_result sr;
    bsg_snap_result_init(&sr);
    if (!bsg_snap_candidates(v, model_pt, 0.0, kinds, &sr) || !sr.sr_cnt) {
	bsg_snap_result_free(&sr);
	return 0;
    }

    /* Pick the nearest candidate. */
    size_t best = 0;
    for (size_t i = 1; i < sr.sr_cnt; i++) {
	if (sr.sr_candidates[i].sc_distance < sr.sr_candidates[best].sc_distance)
	    best = i;
    }

    /* Convert winner back to 2D view space. */
    point_t snapped_view = VINIT_ZERO;
    MAT4X3PNT(snapped_view, v->gv_model2view, sr.sr_candidates[best].sc_point);
    *vx = snapped_view[X];
    *vy = snapped_view[Y];

    bsg_snap_result_free(&sr);
    return 1;
}

