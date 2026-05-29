/*                          P I C K . C
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
/** @file libbsg/pick.c
 *
 * Phase D3 (drawing_modernization): typed pick records and pick actions.
 *
 * Implementation of bsg/pick.h.
 *
 * Picking strategy:
 *   bsg_pick_point() and bsg_pick_rect() delegate the candidate-node
 *   collection to the existing bsg_view_objs_select() /
 *   bsg_view_objs_rect_select() helpers (bv_lod.cpp) which use an OBB SAT
 *   test to find nodes whose bounding boxes overlap the pick volume.
 *
 *   For each candidate the hit distance is estimated as the model-space
 *   distance from the view's back-out point to the centre of the node's
 *   bounding box; this gives a "nearest first" ordering consistent with the
 *   bbox-based closest_obj_bbox() logic in QgSelectFilter.cpp.
 *
 *   Source-path strings are extracted from the node's draw-intent (Phase D2)
 *   when present, falling back to the legacy s_name field.
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bg/aabb_ray.h"
#include "vmath.h"

#include "bsg/defines.h"
#include "bsg/draw_intent.h"
#include "bsg/lod.h"
#include "bsg/node.h"
#include "bsg/pick.h"
#include "bsg/selection.h"
#include "bsg/util.h"
#include "bsg/node_private.h"


/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/*
 * Estimate the model-space distance from the view back-out point to the
 * centre of node @p s's bounding box.
 */
static fastf_t
_node_hit_dist(const bsg_node *s, const struct bsg_view *v)
{
    point_t centre;
    VADD2SCALE(centre, s->bmin, s->bmax, 0.5);
    return DIST_PNT_PNT(centre, v->gv_vc_backout);
}

/*
 * Comparator for qsort: order bsg_pick_record* by ascending pr_hit_dist.
 */
static int
_record_cmp(const void *a, const void *b)
{
    const struct bsg_pick_record *ra = *(const struct bsg_pick_record **)a;
    const struct bsg_pick_record *rb = *(const struct bsg_pick_record **)b;
    if (ra->pr_hit_dist < rb->pr_hit_dist) return -1;
    if (ra->pr_hit_dist > rb->pr_hit_dist) return  1;
    return 0;
}

/*
 * Allocate a single pick record for @p node in view @p v at screen (@p sx,
 * @p sy).  Source path comes from the node's draw intent when available,
 * falling back to the legacy s_name field.
 *
 * Returns NULL on allocation failure.
 */
static struct bsg_pick_record *
_record_create(bsg_node *node, struct bsg_view *v, int sx, int sy)
{
    struct bsg_pick_record *pr;
    BU_GET(pr, struct bsg_pick_record);
    bu_vls_init(&pr->pr_source_path);
    bu_vls_init(&pr->pr_instance_path);

    pr->pr_node     = node;
    pr->pr_view     = v;
    pr->pr_screen_x = sx;
    pr->pr_screen_y = sy;
    pr->pr_hit_dist = (v) ? _node_hit_dist(node, v) : -1.0;
    pr->pr_primitive_id = -1;
    pr->pr_subelement_id = -1;

    /* Prefer draw-intent path; fall back to s_name. */
    const struct bsg_draw_intent *di = bsg_node_get_draw_intent(node);
    const char *di_path = (di) ? bsg_draw_intent_path(di) : NULL;
    if (di_path && *di_path) {
	bu_vls_sprintf(&pr->pr_source_path, "%s", di_path);
    } else {
	bu_vls_sprintf(&pr->pr_source_path, "%s",
		       bu_vls_cstr(&node->s_name));
    }
    bu_vls_sprintf(&pr->pr_instance_path, "%s", bu_vls_cstr(&pr->pr_source_path));

    return pr;
}

static struct bsg_pick_record *
_record_clone(const struct bsg_pick_record *src)
{
    if (!src)
	return NULL;
    struct bsg_pick_record *pr;
    BU_GET(pr, struct bsg_pick_record);
    bu_vls_init(&pr->pr_source_path);
    bu_vls_init(&pr->pr_instance_path);
    pr->pr_node = src->pr_node;
    pr->pr_view = src->pr_view;
    pr->pr_screen_x = src->pr_screen_x;
    pr->pr_screen_y = src->pr_screen_y;
    pr->pr_hit_dist = src->pr_hit_dist;
    pr->pr_primitive_id = src->pr_primitive_id;
    pr->pr_subelement_id = src->pr_subelement_id;
    bu_vls_sprintf(&pr->pr_source_path, "%s", bu_vls_cstr(&src->pr_source_path));
    bu_vls_sprintf(&pr->pr_instance_path, "%s", bu_vls_cstr(&src->pr_instance_path));
    return pr;
}

static const struct bsg_draw_intent *
_nearest_intent(const bsg_node *node)
{
    const bsg_node *n = node;
    while (n) {
	const struct bsg_draw_intent *di = bsg_node_get_draw_intent(n);
	if (di)
	    return di;
	n = bsg_node_parent((bsg_node *)n);
    }
    return NULL;
}

static void
_record_free(struct bsg_pick_record *pr)
{
    if (!pr)
	return;
    bu_vls_free(&pr->pr_source_path);
    bu_vls_free(&pr->pr_instance_path);
    BU_PUT(pr, struct bsg_pick_record);
}

/*
 * Sort the pr_records table in @p res by ascending pr_hit_dist.
 */
static void
_result_sort(struct bsg_pick_result *res)
{
    size_t n = BU_PTBL_LEN(&res->pr_records);
    if (n < 2)
	return;
    qsort(res->pr_records.buffer, n,
	  sizeof(long *), _record_cmp);
}


/* -----------------------------------------------------------------------
 * bsg_pick_result lifecycle
 * ----------------------------------------------------------------------- */

struct bsg_pick_result *
bsg_pick_result_create(void)
{
    struct bsg_pick_result *res;
    BU_GET(res, struct bsg_pick_result);
    bu_ptbl_init(&res->pr_records, 8, "bsg_pick_result");
    return res;
}

void
bsg_pick_result_free(struct bsg_pick_result *res)
{
    if (!res)
	return;
    for (size_t i = 0; i < BU_PTBL_LEN(&res->pr_records); i++) {
	struct bsg_pick_record *pr =
	    (struct bsg_pick_record *)BU_PTBL_GET(&res->pr_records, i);
	_record_free(pr);
    }
    bu_ptbl_free(&res->pr_records);
    BU_PUT(res, struct bsg_pick_result);
}

size_t
bsg_pick_result_count(const struct bsg_pick_result *res)
{
    if (!res)
	return 0;
    return BU_PTBL_LEN(&res->pr_records);
}

struct bsg_pick_record *
bsg_pick_result_get(const struct bsg_pick_result *res, size_t i)
{
    if (!res || i >= BU_PTBL_LEN(&res->pr_records))
	return NULL;
    return (struct bsg_pick_record *)BU_PTBL_GET(&res->pr_records, i);
}


/* -----------------------------------------------------------------------
 * Pick actions
 * ----------------------------------------------------------------------- */

struct bsg_pick_result *
bsg_pick_point(struct bsg_view *v, int x, int y, int first_only)
{
    if (!v)
	return NULL;

    struct bsg_pick_result *res = bsg_pick_result_create();
    if (!res)
	return NULL;

    struct bu_ptbl sset = BU_PTBL_INIT_ZERO;
    int scnt = bsg_view_objs_select(&sset, v, x, y);
    if (scnt <= 0) {
	bu_ptbl_free(&sset);
	return res;
    }

    for (size_t i = 0; i < BU_PTBL_LEN(&sset); i++) {
	bsg_node *node = (bsg_node *)BU_PTBL_GET(&sset, i);
	struct bsg_pick_record *pr = _record_create(node, v, x, y);
	if (pr)
	    bu_ptbl_ins(&res->pr_records, (long *)pr);
    }
    bu_ptbl_free(&sset);

    _result_sort(res);

    /* Apply first_only filter after sorting so the closest survives. */
    if (first_only && BU_PTBL_LEN(&res->pr_records) > 1) {
	for (size_t i = 1; i < BU_PTBL_LEN(&res->pr_records); i++) {
	    struct bsg_pick_record *pr =
		(struct bsg_pick_record *)BU_PTBL_GET(&res->pr_records, i);
	    _record_free(pr);
	}
	/* Truncate table to one entry. */
	res->pr_records.end = 1;
    }

    return res;
}


struct bsg_pick_result *
bsg_pick_rect(struct bsg_view *v, int x0, int y0, int x1, int y1)
{
    if (!v)
	return NULL;

    struct bsg_pick_result *res = bsg_pick_result_create();
    if (!res)
	return NULL;

    struct bu_ptbl sset = BU_PTBL_INIT_ZERO;
    int scnt = bsg_view_objs_rect_select(&sset, v, x0, y0, x1, y1);
    if (scnt <= 0) {
	bu_ptbl_free(&sset);
	return res;
    }

    /* Use rect centre as representative screen coordinates. */
    int cx = (x0 + x1) / 2;
    int cy = (y0 + y1) / 2;

    for (size_t i = 0; i < BU_PTBL_LEN(&sset); i++) {
	bsg_node *node = (bsg_node *)BU_PTBL_GET(&sset, i);
	struct bsg_pick_record *pr = _record_create(node, v, cx, cy);
	if (pr)
	    bu_ptbl_ins(&res->pr_records, (long *)pr);
    }
    bu_ptbl_free(&sset);

    _result_sort(res);
    return res;
}


struct bsg_pick_result *
bsg_pick_nearest(struct bsg_view *v, int x, int y)
{
    return bsg_pick_point(v, x, y, 1 /* first_only */);
}

struct bsg_pick_result *
bsg_pick_ray(struct bsg_view *v, const point_t orig, const vect_t dir,
	     bsg_pick_flags flags)
{
    if (!v)
	return NULL;

    struct bsg_pick_result *res = bsg_pick_result_create();
    if (!res)
	return NULL;

    bsg_node *root = (bsg_node *)v->gv_draw_root;
    if (!root)
	return res;

    int include_scene = (flags & BSG_PICK_INCLUDE_SCENE) ? 1 : 0;
    int include_overlays = (flags & BSG_PICK_INCLUDE_OVERLAYS) ? 1 : 0;
    int first_only = (flags & BSG_PICK_FIRST_ONLY) ? 1 : 0;
    if (!include_scene && !include_overlays)
	include_scene = 1;

    vect_t invdir;
    bg_ray_invdir(&invdir, (fastf_t *)dir);

    struct bu_ptbl groups = BU_PTBL_INIT_ZERO;
    bsg_collect_draw_groups(root, &groups, 1 /* include overlays */);
    for (size_t i = 0; i < BU_PTBL_LEN(&groups); i++) {
	bsg_node *g = (bsg_node *)BU_PTBL_GET(&groups, i);
	const struct bsg_draw_intent *di = bsg_node_get_draw_intent(g);
	if (!di)
	    continue;
	const int is_overlay = bsg_draw_intent_is_overlay(di);
	if ((is_overlay && !include_overlays) || (!is_overlay && !include_scene))
	    continue;
	fastf_t tmin = 0.0, tmax = 0.0;
	if (!bg_isect_aabb_ray(&tmin, &tmax, (fastf_t *)orig, (const fastf_t *)invdir,
		    (const fastf_t *)g->bmin, (const fastf_t *)g->bmax))
	    continue;
	struct bsg_pick_record *pr = _record_create(g, v, -1, -1);
	if (!pr)
	    continue;
	pr->pr_hit_dist = (tmin >= 0.0) ? tmin : tmax;
	bu_ptbl_ins(&res->pr_records, (long *)pr);
    }
    bu_ptbl_free(&groups);

    _result_sort(res);
    if (first_only && BU_PTBL_LEN(&res->pr_records) > 1) {
	for (size_t i = 1; i < BU_PTBL_LEN(&res->pr_records); i++) {
	    struct bsg_pick_record *pr =
		(struct bsg_pick_record *)BU_PTBL_GET(&res->pr_records, i);
	    _record_free(pr);
	}
	res->pr_records.end = 1;
    }

    return res;
}

struct bsg_pick_result *
bsg_pick_nearest_overlay_control(struct bsg_view *v, int x, int y,
				 unsigned long long role_mask)
{
    (void)role_mask;
    if (!v)
	return NULL;

    struct bsg_pick_result *candidates = bsg_pick_point(v, x, y, 0);
    if (!candidates)
	return NULL;

    struct bsg_pick_result *res = bsg_pick_result_create();
    if (!res) {
	bsg_pick_result_free(candidates);
	return NULL;
    }

    for (size_t i = 0; i < BU_PTBL_LEN(&candidates->pr_records); i++) {
	const struct bsg_pick_record *pr =
	    (const struct bsg_pick_record *)BU_PTBL_GET(&candidates->pr_records, i);
	if (!pr || !pr->pr_node)
	    continue;
	const struct bsg_draw_intent *di = _nearest_intent(pr->pr_node);
	if (!di || !bsg_draw_intent_is_overlay(di))
	    continue;
	struct bsg_pick_record *copy = _record_clone(pr);
	if (copy)
	    bu_ptbl_ins(&res->pr_records, (long *)copy);
    }
    bsg_pick_result_free(candidates);
    _result_sort(res);

    if (BU_PTBL_LEN(&res->pr_records) > 1) {
	for (size_t i = 1; i < BU_PTBL_LEN(&res->pr_records); i++) {
	    struct bsg_pick_record *pr =
		(struct bsg_pick_record *)BU_PTBL_GET(&res->pr_records, i);
	    _record_free(pr);
	}
	res->pr_records.end = 1;
    }

    return res;
}

struct bsg_pick_result *
bsg_pick_semantic_path(struct bsg_view *v, const char *path_pattern)
{
    if (!v || !path_pattern || !*path_pattern)
	return NULL;

    struct bsg_pick_result *res = bsg_pick_result_create();
    if (!res)
	return NULL;

    bsg_node *root = (bsg_node *)v->gv_draw_root;
    if (!root)
	return res;

    struct bu_ptbl groups = BU_PTBL_INIT_ZERO;
    bsg_draw_intent_match(root, path_pattern, &groups);
    for (size_t i = 0; i < BU_PTBL_LEN(&groups); i++) {
	bsg_node *g = (bsg_node *)BU_PTBL_GET(&groups, i);
	struct bsg_pick_record *pr = _record_create(g, v, -1, -1);
	if (!pr)
	    continue;
	bu_ptbl_ins(&res->pr_records, (long *)pr);
    }
    bu_ptbl_free(&groups);
    _result_sort(res);
    return res;
}


/* -----------------------------------------------------------------------
 * Compatibility / integration helpers
 * ----------------------------------------------------------------------- */

void
bsg_pick_result_to_ptbl(const struct bsg_pick_result *res,
			struct bu_ptbl                *out)
{
    if (!res || !out)
	return;
    for (size_t i = 0; i < BU_PTBL_LEN(&res->pr_records); i++) {
	const struct bsg_pick_record *pr =
	    (const struct bsg_pick_record *)BU_PTBL_GET(&res->pr_records, i);
	bu_ptbl_ins(out, (long *)pr->pr_node);
    }
}


void
bsg_pick_apply(struct bsg_selection   *sel,
	       struct bsg_pick_result  *res,
	       bsg_pick_op              op)
{
    if (!sel || !res)
	return;

    if (op == BSG_PICK_OP_SET)
	bsg_selection_clear(sel);

    for (size_t i = 0; i < BU_PTBL_LEN(&res->pr_records); i++) {
	struct bsg_pick_record *pr =
	    (struct bsg_pick_record *)BU_PTBL_GET(&res->pr_records, i);
	if (!pr || !pr->pr_node)
	    continue;
	if (op == BSG_PICK_OP_REMOVE)
	    bsg_selection_remove(sel, pr->pr_node);
	else
	    bsg_selection_add(sel, pr->pr_node);
    }
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
