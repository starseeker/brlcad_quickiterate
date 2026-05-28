/*               B S G _ M O V E _ H E L P E R S . H
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
/**
 * @file bsg_move_helpers.h
 *
 * Private helpers for BSG-native pick/move/scale operations in libtclcad.
 *
 * Included by commands.c, mouse.c, view/arrows.c, and view/axes.c.
 *
 * These are drawn from the Phase T3 (drawing_stack_modernization) migration
 * work that removed gv_tcl as the source of truth for interactive pick, move,
 * and scale operations.  The BSG vlist (or child table for labels) is now the
 * sole store; gv_tcl is no longer mirrored for these paths.
 */

#ifndef LIBTCLCAD_BSG_MOVE_HELPERS_H
#define LIBTCLCAD_BSG_MOVE_HELPERS_H

#include "common.h"

#include "vmath.h"
#include "bu/malloc.h"
#include "bsg/defines.h"
#include "bsg/vlist.h"
#include "bsg/draw_source.h"
#include "bsg/util.h"
#include "dm.h"

__BEGIN_DECLS

#if defined(__GNUC__)
#  define _BSG_HELPER_STATIC static __attribute__((unused))
#else
#  define _BSG_HELPER_STATIC static
#endif

/* --------------------------------------------------------------------------
 * Point extraction
 * -------------------------------------------------------------------------- */

/**
 * Extract a flat point_t array from the vlist of a BSG scene object.
 * On success, *pts_out points to a bu_calloc'd array of npts points.
 * Caller must bu_free(*pts_out, "bsg pts").
 * Returns the number of points extracted (0 if none or s is NULL).
 */
_BSG_HELPER_STATIC int
_bsg_extract_pts(struct bsg_node *s, point_t **pts_out)
{
    if (!s || !pts_out) return 0;

    /* Count points in vlist */
    int total = 0;
    bsg_vlist *vp;
    for (BU_LIST_FOR(vp, bsg_vlist, bsg_node_vlist_head(s)))
	total += vp->nused;

    if (total < 1) { *pts_out = NULL; return 0; }

    point_t *pts = (point_t *)bu_calloc(total, sizeof(point_t), "bsg pts");
    int k = 0;
    for (BU_LIST_FOR(vp, bsg_vlist, bsg_node_vlist_head(s)))
	for (size_t j = 0; j < (size_t)vp->nused; j++)
	    VMOVE(pts[k++], vp->pt[j]);

    *pts_out = pts;
    return total;
}

/**
 * Extract the center points stored in a data-axes BSG object.
 *
 * Each axes point generates 6 vlist entries (X/Y/Z-axis MOVE+DRAW pairs).
 * The center for group i is the midpoint of vlist[6i] and vlist[6i+1]
 * (the X-axis endpoints).
 *
 * On success, *pts_out points to a bu_calloc'd array of ncenters center
 * points.  Caller must bu_free(*pts_out, "bsg axes pts").
 * Returns the number of center points extracted.
 */
_BSG_HELPER_STATIC int
_bsg_extract_axes_centers(struct bsg_node *s, point_t **pts_out)
{
    if (!pts_out) return 0;
    *pts_out = NULL;

    point_t *all = NULL;
    int ntotal = _bsg_extract_pts(s, &all);
    if (ntotal < 6 || (ntotal % 6) != 0) {
	bu_free(all, "bsg pts");
	return 0;
    }

    int ncenters = ntotal / 6;
    point_t *centers = (point_t *)bu_calloc(ncenters, sizeof(point_t), "bsg axes pts");
    for (int i = 0; i < ncenters; i++) {
	/* X-axis MOVE/DRAW midpoint = (x-h, y, z) + (x+h, y, z) / 2 = (x, y, z) */
	centers[i][X] = (all[6*i+0][X] + all[6*i+1][X]) * 0.5;
	centers[i][Y] = all[6*i+0][Y];
	centers[i][Z] = all[6*i+0][Z];
    }
    bu_free(all, "bsg pts");
    *pts_out = centers;
    return ncenters;
}

/* --------------------------------------------------------------------------
 * Object rebuild helpers
 * -------------------------------------------------------------------------- */

#define _BSG_HELPERS_DEFAULT_DM_WIDTH 512

/**
 * Rebuild a BSG arrow object from an explicit flat point array.
 * Consecutive pairs of points define arrow shafts (pt[0]→pt[1], pt[2]→pt[3]).
 * Existing object (if any) is removed first.
 */
_BSG_HELPER_STATIC void
_bsg_rebuild_arrows(struct bsg_view *v,
		    const char *bsg_name,
		    point_t *pts, int npts,
		    int color[3], int lw,
		    int tip_len, int tip_wid,
		    int visible)
{
    if (!v || !bsg_name) return;
    bsg_view_obj_remove(v, bsg_name);
    if (!pts || npts < 2) return;

    struct bsg_node *ns = bsg_view_obj_arrow_create(v, bsg_name, 1 /* local */);
    if (!ns) return;

    for (int i = 0; i + 1 < npts; i += 2) {
	bsg_node_append_vlist_payload(ns, pts[i],   BSG_VLIST_LINE_MOVE);
	bsg_node_append_vlist_payload(ns, pts[i+1], BSG_VLIST_LINE_DRAW);
    }
    if (color)
	bsg_view_obj_set_color(ns, color[0], color[1], color[2]);
    bsg_view_obj_set_line_width(ns, lw);
    if (ns->s_os) {
	ns->s_os->s_arrow_tip_length = (fastf_t)tip_len;
	ns->s_os->s_arrow_tip_width  = (fastf_t)tip_wid;
    }
    bsg_view_obj_set_visible(ns, visible);
}

/**
 * Rebuild a BSG lines object from an explicit flat point array.
 * Consecutive pairs of points define line segments.
 */
_BSG_HELPER_STATIC void
_bsg_rebuild_lines(struct bsg_view *v,
		   const char *bsg_name,
		   point_t *pts, int npts,
		   int color[3], int lw,
		   int visible)
{
    if (!v || !bsg_name) return;
    bsg_view_obj_remove(v, bsg_name);
    if (!pts || npts < 2) return;

    struct bsg_node *ns = bsg_view_obj_lines_create(v, bsg_name, 1 /* local */);
    if (!ns) return;

    bsg_node_clear_vlist_payload(ns);
    for (int i = 0; i + 1 < npts; i += 2) {
	bsg_node_append_vlist_payload(ns, pts[i], BSG_VLIST_LINE_MOVE);
	bsg_node_append_vlist_payload(ns, pts[i+1], BSG_VLIST_LINE_DRAW);
    }
    if (color)
	bsg_view_obj_set_color(ns, color[0], color[1], color[2]);
    bsg_view_obj_set_line_width(ns, lw);
    bsg_view_obj_set_visible(ns, visible);
}

/**
 * Rebuild a BSG data-axes object from an array of center points and a
 * half-axes-size.  Generates 6 vlist entries per center (X/Y/Z axis pairs).
 */
_BSG_HELPER_STATIC void
_bsg_rebuild_axes(struct bsg_view *v,
		  const char *bsg_name,
		  point_t *centers, int ncenters,
		  fastf_t halfAxesSize,
		  int color[3], int lw,
		  int visible)
{
    if (!v || !bsg_name) return;
    bsg_view_obj_remove(v, bsg_name);
    if (!centers || ncenters < 1) return;

    struct bsg_node *ns = bsg_view_obj_lines_create(v, bsg_name, 1 /* local */);
    if (!ns) return;

    bsg_node_clear_vlist_payload(ns);
    for (int i = 0; i < ncenters; i++) {
	point_t ptA, ptB;

	VSET(ptA, centers[i][X] - halfAxesSize, centers[i][Y], centers[i][Z]);
	VSET(ptB, centers[i][X] + halfAxesSize, centers[i][Y], centers[i][Z]);
	bsg_node_append_vlist_payload(ns, ptA, BSG_VLIST_LINE_MOVE);
	bsg_node_append_vlist_payload(ns, ptB, BSG_VLIST_LINE_DRAW);

	VSET(ptA, centers[i][X], centers[i][Y] - halfAxesSize, centers[i][Z]);
	VSET(ptB, centers[i][X], centers[i][Y] + halfAxesSize, centers[i][Z]);
	bsg_node_append_vlist_payload(ns, ptA, BSG_VLIST_LINE_MOVE);
	bsg_node_append_vlist_payload(ns, ptB, BSG_VLIST_LINE_DRAW);

	VSET(ptA, centers[i][X], centers[i][Y], centers[i][Z] - halfAxesSize);
	VSET(ptB, centers[i][X], centers[i][Y], centers[i][Z] + halfAxesSize);
	bsg_node_append_vlist_payload(ns, ptA, BSG_VLIST_LINE_MOVE);
	bsg_node_append_vlist_payload(ns, ptB, BSG_VLIST_LINE_DRAW);
    }

    if (color)
	bsg_view_obj_set_color(ns, color[0], color[1], color[2]);
    bsg_view_obj_set_line_width(ns, lw);
    bsg_view_obj_set_visible(ns, visible);
}

/* --------------------------------------------------------------------------
 * Style extraction from existing BSG object
 * -------------------------------------------------------------------------- */

/**
 * Read display style fields from an existing BSG scene object into caller-
 * supplied output variables.  Safe to call with a NULL @p s (fills defaults).
 */
_BSG_HELPER_STATIC void
_bsg_read_style(struct bsg_node *s,
		int color_out[3],
		int *lw_out,
		int *tip_len_out,
		int *tip_wid_out,
		int *visible_out)
{
    /* Defaults */
    if (color_out)   { color_out[0] = 255; color_out[1] = 255; color_out[2] = 0; }
    if (lw_out)       *lw_out       = 0;
    if (tip_len_out)  *tip_len_out  = 0;
    if (tip_wid_out)  *tip_wid_out  = 0;
    if (visible_out)  *visible_out  = 1;

    if (!s) return;

    if (color_out) {
	color_out[0] = (int)s->s_color[0];
	color_out[1] = (int)s->s_color[1];
	color_out[2] = (int)s->s_color[2];
    }
    if (lw_out && s->s_os)
	*lw_out = s->s_os->s_line_width;
    if (tip_len_out && s->s_os)
	*tip_len_out = (int)s->s_os->s_arrow_tip_length;
    if (tip_wid_out && s->s_os)
	*tip_wid_out = (int)s->s_os->s_arrow_tip_width;
    if (visible_out)
	*visible_out = (s->s_flag == UP) ? 1 : 0;
}

__END_DECLS

#undef _BSG_HELPER_STATIC

#endif /* LIBTCLCAD_BSG_MOVE_HELPERS_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
