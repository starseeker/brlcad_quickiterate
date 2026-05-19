/*                   D R A W _ S E T . C
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
/** @file libbsg/draw_set.c
 *
 * Phase 7 step 7 A3+C1/C3 (drawing_stack_modernization):
 * Pure-BSG draw-tree helpers — no dependency on GED types.
 *
 * These functions implement the tree-navigation layer that libged/
 * bsg_view_obj.c previously handled entirely as file-private helpers.
 * Moving them here allows the GED wrapper to be a thin bridge that
 * supplies the GED-specific context (db_lookup, vlist callbacks) while
 * delegating all BSG tree manipulation to this library.
 *
 * Dependencies: libbv (bv_obj_create, bv/defines.h), bu (bu_ptbl, bu_vls).
 * No librt, no libged.
 */

#include "common.h"

#include "bu/list.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/vlist.h"

#include "bsg/defines.h"
#include "bsg/draw_ctx.h"
#include "bsg/draw_set.h"
#include "bsg_private.h"


/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * FREE_BV_SCENE_OBJ: recycle a bv_scene_obj back into the free-pool
 * list @p fp and free its vlist data using the vlist pool @p vlf.
 *
 * This mirrors the identical macro defined in src/libged/bsg_view_obj.c
 * and src/libbv/vlist.c.  It is defined here as a file-private macro so
 * that libbsg/draw_set.c can call it without pulling in libged or librt.
 */
#define FREE_BV_SCENE_OBJ(p, fp, vlf) { \
    BU_LIST_APPEND(fp, &((p)->l)); \
    BV_FREE_VLIST(vlf, &((p)->s_vlist)); }


/* ------------------------------------------------------------------ */


int
bsg_draw_tree_depth(const bsg_node *g)
{
    if (!g)
	return 0;

    int depth = 0;
    const struct bv_scene_obj *cur = (const struct bv_scene_obj *)g;
    while (cur->parent) {
	depth++;
	cur = (const struct bv_scene_obj *)cur->parent;
    }
    return depth;
}


bsg_node *
bsg_group_find_child(bsg_node *parent, const char *name)
{
    if (!parent || !name)
	return NULL;

    struct bv_scene_obj *p = (struct bv_scene_obj *)parent;
    for (size_t i = 0; i < BU_PTBL_LEN(&p->children); i++) {
	struct bv_scene_obj *c =
	    (struct bv_scene_obj *)BU_PTBL_GET(&p->children, i);
	if (!c)
	    continue;
	if ((c->s_type_flags & BSG_NODE_GROUP) &&
	    BU_STR_EQUAL(name, bu_vls_cstr(&c->s_name)))
	    return (bsg_node *)c;
    }
    return NULL;
}


bsg_node *
bsg_group_ensure_child(bsg_node *parent, struct bview *v,
		       const char *name, void *dp_hint)
{
    if (!parent || !name)
	return NULL;

    /* Fast path: child already exists */
    bsg_node *existing = bsg_group_find_child(parent, name);
    if (existing)
	return existing;

    /* Need a view for allocation */
    if (!v)
	return NULL;

    struct bv_scene_obj *p = (struct bv_scene_obj *)parent;

    /* Allocate a new GROUP node through libbv. */
    struct bv_scene_obj *child = bv_obj_create(v, BV_CHILD_OBJS);
    if (!child)
	return NULL;

    child->s_type_flags = (unsigned long long)BSG_NODE_GROUP;
    child->s_flag       = UP;
    child->s_iflag      = DOWN;
    child->dp           = dp_hint;
    child->parent       = parent;
    bu_vls_sprintf(&child->s_name, "%s", name);
    bu_ptbl_ins(&p->children, (long *)child);

    /* Phase 9.1: a new child invalidates the parent chain's cached
     * aggregate bbox.  The new group itself is leaf-empty so its own
     * cache (when computed later) will simply read back as the empty
     * box; clearing now on the parent and above is what matters. */
    bsg_node_bbox_invalidate(parent);

    return (bsg_node *)child;
}


/* ------------------------------------------------------------------ */
/* Free-group helpers (Phase 7 Step 11)                               */
/* ------------------------------------------------------------------ */

void
bsg_bump_rev_node(bsg_node *n)
{
    if (!n)
	return;
    struct bv_scene_obj *cur = (struct bv_scene_obj *)n;
    /* Walk up to root (parent == NULL) */
    while (cur->parent)
	cur = (struct bv_scene_obj *)cur->parent;
    /* cur is the draw root; s_i_data holds the bsg_draw_ctx */
    if (cur->s_i_data) {
	struct bsg_draw_ctx *ctx = (struct bsg_draw_ctx *)cur->s_i_data;
	if (ctx->draw_rev)
	    ++(*ctx->draw_rev);
    }
}


void
bsg_free_children_recursive(bsg_node *gn, struct bv_scene_obj *fso)
{
    struct bv_scene_obj *g = (struct bv_scene_obj *)gn;

    /* Phase 9.1: removing children invalidates this group's aggregate
     * bbox cache (and that of all of its ancestors). */
    if (BU_PTBL_LEN(&g->children) > 0)
	bsg_node_bbox_invalidate(gn);

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&g->children); i++)
	bu_ptbl_ins(&snap, BU_PTBL_GET(&g->children, i));

    for (size_t i = 0; i < BU_PTBL_LEN(&snap); i++) {
	struct bv_scene_obj *child =
	    (struct bv_scene_obj *)BU_PTBL_GET(&snap, i);
	if (child->s_type_flags & BSG_NODE_GROUP) {
	    bsg_free_children_recursive((bsg_node *)child, fso);
	    child->parent = NULL;
	    struct bv_scene_obj *cfso = child->free_scene_obj;
	    if (cfso)
		FREE_BV_SCENE_OBJ(child, &cfso->l, child->vlfree);
	} else {
	    /* Fire per-object teardown callbacks before recycling.
	     * Phase 11: bv_scene_obj_release_backend releases display-list
	     * GPU resources via the new backend contract.  s_free_callback
	     * fires the illumination-clear registered as ged_bv_illum_free_cb
	     * at shape-creation time (Phase 7 Steps 8-9). */
	    bv_scene_obj_release_backend(child);
	    if (child->s_free_callback)
		(*child->s_free_callback)(child);
	    child->parent = NULL;
	    struct bv_scene_obj *sfso = fso ? fso : child->free_scene_obj;
	    if (sfso)
		FREE_BV_SCENE_OBJ(child, &sfso->l, child->vlfree);
	}
    }
    bu_ptbl_free(&snap);
    bu_ptbl_reset(&g->children);
}


void
bsg_free_group_contents(bsg_node *gn)
{
    struct bv_scene_obj *g = (struct bv_scene_obj *)gn;
    if (!g || BU_PTBL_LEN(&g->children) == 0)
	return;

    /* Obtain the free-object pool pointer from the draw-tree context
     * stored in the root's s_i_data.  Fall back to the group's own
     * free_scene_obj if no context is present. */
    struct bsg_draw_ctx *ctx = _ctx_of_node(g);
    struct bv_scene_obj *fso = (ctx && ctx->fso) ? ctx->fso : g->free_scene_obj;

    bsg_free_children_recursive(gn, fso);
}


void
bsg_free_group(bsg_node *gn)
{
    struct bv_scene_obj *g = (struct bv_scene_obj *)gn;
    if (!g)
	return;

    bsg_free_group_contents(gn);

    struct bv_scene_obj *parent = (struct bv_scene_obj *)g->parent;
    if (parent)
	bu_ptbl_rm(&parent->children, (const long *)g);

    /* g->parent is still set at this point; walk up to root for ctx */
    bsg_bump_rev_node(gn);
    /* Phase 9.1: removing this subtree invalidates ancestors' bbox caches. */
    bsg_node_bbox_invalidate(gn);

    g->parent = NULL;
    struct bv_scene_obj *fso = g->free_scene_obj;
    if (fso)
	FREE_BV_SCENE_OBJ(g, &fso->l, g->vlfree);
}


/* ------------------------------------------------------------------ */
/* Path-navigation erase helper (Phase 7 Step 12)                     */
/* ------------------------------------------------------------------ */

void
bsg_erase_nested_subpath(bsg_node *parent_node,
			 const char * const *comp_names, size_t comp_count,
			 size_t depth_start,
			 bsg_path_match_fn match_fn, void *match_ctx)
{
    if (!parent_node || !comp_names || comp_count == 0 || depth_start >= comp_count)
	return;

    struct bv_scene_obj *cur = (struct bv_scene_obj *)parent_node;

    for (size_t i = depth_start; i < comp_count; i++) {
	const char *comp = comp_names[i];
	struct bv_scene_obj *child_group = NULL;

	for (size_t j = 0; j < BU_PTBL_LEN(&cur->children); j++) {
	    struct bv_scene_obj *c =
		(struct bv_scene_obj *)BU_PTBL_GET(&cur->children, j);
	    if ((c->s_type_flags & BSG_NODE_GROUP) &&
		BU_STR_EQUAL(bu_vls_cstr(&c->s_name), comp)) {
		child_group = c;
		break;
	    }
	}

	if (i < comp_count - 1) {
	    /* Intermediate component — must be a group */
	    if (!child_group)
		return;
	    cur = child_group;
	    continue;
	}

	/* Final component */
	if (child_group) {
	    /* Case (a): a group node with this name — free its entire subtree */
	    bsg_free_group_contents((bsg_node *)child_group);
	    bu_ptbl_rm(&cur->children, (const long *)child_group);
	    /* cur is still in the tree; bump rev before clearing parent */
	    bsg_bump_rev_node((bsg_node *)cur);
	    /* Phase 9.1: shrinking cur invalidates its (and ancestors') bbox cache. */
	    bsg_node_bbox_invalidate((bsg_node *)cur);
	    child_group->parent = NULL;
	    struct bv_scene_obj *cfso = child_group->free_scene_obj;
	    if (cfso)
		FREE_BV_SCENE_OBJ(child_group, &cfso->l, child_group->vlfree);
	} else {
	    /* Case (b): the final component is a leaf primitive — erase
	     * matching BSG_NODE_SHAPE children by calling match_fn. */
	    struct bsg_draw_ctx *ctx = _ctx_of_node(cur);
	    struct bv_scene_obj *fso = (ctx && ctx->fso) ? ctx->fso : NULL;

	    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
	    for (size_t j = 0; j < BU_PTBL_LEN(&cur->children); j++) {
		struct bv_scene_obj *c =
		    (struct bv_scene_obj *)BU_PTBL_GET(&cur->children, j);
		if (!(c->s_type_flags & BSG_NODE_SHAPE))
		    continue;
		if (!match_fn || match_fn(c->s_u_data, match_ctx))
		    bu_ptbl_ins(&snap, (long *)c);
	    }
	    for (size_t j = 0; j < BU_PTBL_LEN(&snap); j++) {
		struct bv_scene_obj *sp =
		    (struct bv_scene_obj *)BU_PTBL_GET(&snap, j);
		/* Phase 11: route teardown through the backend contract. */
		bv_scene_obj_release_backend(sp);
		if (sp->s_free_callback)
		    (*sp->s_free_callback)(sp);
		bu_ptbl_rm(&cur->children, (const long *)sp);
		/* cur is in the tree; bump rev then clear parent */
		bsg_bump_rev_node((bsg_node *)cur);
		/* Phase 9.1: shrinking cur invalidates its (and ancestors') bbox cache. */
		bsg_node_bbox_invalidate((bsg_node *)cur);
		sp->parent = NULL;
		struct bv_scene_obj *sfso = fso ? fso : sp->free_scene_obj;
		if (sfso)
		    FREE_BV_SCENE_OBJ(sp, &sfso->l, sp->vlfree);
	    }
	    bu_ptbl_free(&snap);
	}
	return;
    }
}


/* ------------------------------------------------------------------ */
/* Subtree bbox cache (Phase 9.1, B3 residual)                        */
/* ------------------------------------------------------------------ */

void
bsg_node_bbox_invalidate(bsg_node *n)
{
    if (!n)
	return;
    struct bv_scene_obj *cur = (struct bv_scene_obj *)n;

    /* Walk up the parent chain; clear s_bbox_cached on every ancestor
     * that is still cached.  Stop at the first already-dirty ancestor
     * (any further ancestor must already be dirty too).
     *
     * The starting node itself is included in the walk: if @p n is a
     * leaf shape we skip to its parent (leaves have no aggregate
     * cache); otherwise we clear @p n too.
     */
    while (cur) {
	if (cur->s_type_flags & (BSG_NODE_GROUP | BSG_NODE_ROOT)) {
	    if (!cur->s_bbox_cached)
		return;  /* already dirty up from here */
	    cur->s_bbox_cached = 0;
	}
	cur = (struct bv_scene_obj *)cur->parent;
    }
}


/*
 * Internal: aggregate the bbox of a single shape leaf into (*min, *max).
 * Uses (s_center - s_size, s_center + s_size) to match the historical
 * _sg_bounding_sph behaviour exactly.
 */
static void
_bsg_shape_bbox_accum(const struct bv_scene_obj *sp,
		      vect_t *min, vect_t *max)
{
    vect_t lmin, lmax;
    lmin[X] = sp->s_center[X] - sp->s_size;
    lmin[Y] = sp->s_center[Y] - sp->s_size;
    lmin[Z] = sp->s_center[Z] - sp->s_size;
    lmax[X] = sp->s_center[X] + sp->s_size;
    lmax[Y] = sp->s_center[Y] + sp->s_size;
    lmax[Z] = sp->s_center[Z] + sp->s_size;
    VMIN(*min, lmin);
    VMAX(*max, lmax);
}


/*
 * Internal recursive aggregator.  Returns 1 when the subtree contributes
 * something to the bbox, 0 when it is empty.  When @p cache is non-zero
 * the caller is in the cached (no-overlay) mode and we may store the
 * aggregate at GROUP/ROOT nodes.
 */
static int
_bsg_subtree_bbox_walk(struct bv_scene_obj *n,
		       vect_t *min, vect_t *max,
		       int include_overlays, int cache)
{
    if (!n)
	return 0;

    if (n->s_type_flags & BSG_NODE_SHAPE) {
	if (!include_overlays && (n->s_type_flags & BSG_PAYLOAD_OVERLAY))
	    return 0;
	_bsg_shape_bbox_accum(n, min, max);
	return 1;
    }

    if (!(n->s_type_flags & (BSG_NODE_GROUP | BSG_NODE_ROOT)))
	return 0;

    /* Cache hit: in cached mode and this group's aggregate is current. */
    if (cache && n->s_bbox_cached) {
	/* An empty subtree caches as an empty box (bmin=+INF, bmax=-INF).
	 * Detect that by checking VMIN(bmin, bmax) — accumulating an
	 * empty box is a no-op. */
	if (n->bmin[X] <= n->bmax[X]) {
	    VMIN(*min, n->bmin);
	    VMAX(*max, n->bmax);
	    return 1;
	}
	return 0;
    }

    /* Recompute: aggregate over children into a local min/max so we can
     * cache the result for this subtree before merging into the caller's
     * accumulator. */
    vect_t lmin, lmax;
    VSETALL(lmin,  INFINITY);
    VSETALL(lmax, -INFINITY);
    int contributed = 0;

    for (size_t i = 0; i < BU_PTBL_LEN(&n->children); i++) {
	struct bv_scene_obj *c =
	    (struct bv_scene_obj *)BU_PTBL_GET(&n->children, i);
	if (!c)
	    continue;
	if (_bsg_subtree_bbox_walk(c, &lmin, &lmax, include_overlays, cache))
	    contributed = 1;
    }

    if (cache) {
	/* Store local result into the node cache.  Empty subtrees cache
	 * the (+INF, -INF) sentinel pair so a subsequent hit detects them. */
	VMOVE(n->bmin, lmin);
	VMOVE(n->bmax, lmax);
	n->s_bbox_cached = 1;
    }

    if (contributed) {
	VMIN(*min, lmin);
	VMAX(*max, lmax);
    }
    return contributed;
}


int
bsg_subtree_bbox(bsg_node *n,
		 vect_t *min, vect_t *max,
		 int include_overlays)
{
    if (!min || !max)
	return 1;

    VSETALL((*min),  INFINITY);
    VSETALL((*max), -INFINITY);

    if (!n)
	return 1;

    int cache = include_overlays ? 0 : 1;
    int contributed =
	_bsg_subtree_bbox_walk((struct bv_scene_obj *)n,
			       min, max, include_overlays, cache);

    /* Return 1 for empty (matches _sg_bounding_sph contract). */
    return contributed ? 0 : 1;
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

