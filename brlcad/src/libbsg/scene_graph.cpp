/*                 S C E N E _ G R A P H . C P P
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
/** @file libbsg/scene_graph.cpp
 *
 * Phase 4 of drawing_stack_modernization: core BSG scene-graph
 * lifecycle and query helpers.
 *
 * bsg_node is a typedef for struct bv_scene_obj; this file implements:
 *   bsg_scene_root_create  — wire v->bsg_root to v->gv_draw_root (Phase F)
 *   bsg_scene_root_sync    — no-op shim kept for binary compatibility
 *   bsg_scene_root_destroy — clear v->bsg_root pointer
 *   bsg_view_find_by_type  — locate first child matching type flags
 *   bsg_sensor_fire        — invoke callbacks on BSG_NODE_SENSOR nodes
 *
 * Phase F (drawing_stack_modernization): bsg_root is no longer a separate
 * synthetic node.  v->bsg_root is an alias for v->gv_draw_root — the same
 * bv_scene_obj that the BSG draw tree uses as its root.  bsg_root->children
 * IS gv_draw_root->children; it is maintained live by draw/erase mutations
 * (bsg_group_ensure_child / bsg_free_group in libbsg/draw_set.c) and by
 * bsg_view_obj_zap.  No per-frame rebuild is needed; bsg_scene_root_sync is
 * now a no-op.  View-only objects (BV_VIEW_OBJS ptbls) are iterated directly
 * in the render loops (dm_draw_objs in libdm/view.c) after the main BSG
 * traversal.
 *
 * bsg_view_traverse() is intentionally NOT implemented here because it
 * must call dm_draw_obj() and other libdm rendering functions.  It lives
 * in src/libdm/view.c (DM_EXPORT) so that libdm can reach those symbols
 * directly without creating a libbsg → libdm dependency cycle.
 */

#include "common.h"

#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/view_sets.h"

#include "bsg/defines.h"
#include "bsg/identity.h"
#include "bsg/util.h"
#include "bsg/visit.h"
#include "bsg/scene_set.h"

/* ---------------------------------------------------------------------- */
/* Helpers                                                                  */
/* ---------------------------------------------------------------------- */

static void
_bsg_scene_root_identity_assign(struct bv_scene_obj *root)
{
    struct bsg_identity id;

    if (!root)
	return;

    bsg_identity_from_path_str(&id, "_draw_root", BSG_SOURCE_GENERATED);
    bsg_node_identity_set((bsg_node *)root, &id);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                               */
/* ---------------------------------------------------------------------- */

bsg_node *
bsg_scene_root_create(struct bview *v)
{
    if (!v)
	return NULL;

    bsg_identity_enable_view_obj_derivation();

    /* If this view is part of a set and doesn't yet have a draw root, inherit
     * the active draw root from another view in the set.  This keeps secondary
     * views (e.g. libtclcad null-DM views) aligned with the GED draw tree. */
    if (!v->gv_draw_root && v->vset) {
	struct bu_ptbl *views = bv_set_views(v->vset);
	if (views) {
	    for (size_t i = 0; i < BU_PTBL_LEN(views); i++) {
		struct bview *sv = (struct bview *)BU_PTBL_GET(views, i);
		if (!sv || sv == v)
		    continue;
		if (sv->gv_draw_root) {
		    v->gv_draw_root = sv->gv_draw_root;
		    break;
		}
	    }
	}
    }

    /* Phase F (drawing_stack_modernization): bsg_root is normally an alias for
     * gv_draw_root.  GED-backed views create gv_draw_root in _sg_root(); for
     * standalone libbsg/libbv consumers and unit tests, allocate a minimal root
     * here so the public scene-root API remains usable without libged. */
    if (!v->gv_draw_root) {
	/* This path is for non-GED callers.  GED command flows continue to use
	 * libged's _sg_root() so gd_draw_root and bsg_draw_ctx are installed. */
	struct bv_scene_obj *root = bv_obj_get_unregistered(v, BV_CHILD_OBJS);
	if (!root) {
	    bu_log("bsg_scene_root_create: failed to allocate standalone draw root\n");
	    return NULL;
	}
	root->s_type_flags = BSG_NODE_GROUP;
	root->s_flag = UP;
	root->parent = NULL;
	bu_vls_sprintf(&root->s_name, "_draw_root");
	v->gv_draw_root = root;
    }

    _bsg_scene_root_identity_assign((struct bv_scene_obj *)v->gv_draw_root);
    v->bsg_root = v->gv_draw_root;
    return (bsg_node *)v->bsg_root;
}


/* Phase F: bsg_root->children IS gv_draw_root->children — maintained live by
 * the draw-tree mutation helpers (bsg_group_ensure_child / bsg_free_group /
 * bsg_view_obj_zap).  No per-frame rebuild is required; this function is kept
 * only for binary / source compatibility with callers that have not yet been
 * updated.  It is a deliberate no-op. */
void
bsg_scene_root_sync(bsg_node *UNUSED(root), struct bview *UNUSED(v))
{
}


void
bsg_scene_root_destroy(bsg_node *root)
{
    if (!root)
	return;

    /* Phase F: bsg_root is now the same pointer as gv_draw_root, which has its
     * own lifecycle managed by bsg_ged_draw.c / bsg_view_obj_zap.  Do NOT call
     * bv_obj_put here — that would free the live draw-tree root.  Just clear
     * the view's back-reference. */
    struct bv_scene_obj *r = (struct bv_scene_obj *)root;
    if (r->s_v && r->s_v->bsg_root == root)
	r->s_v->bsg_root = NULL;
}


bsg_node *
bsg_view_find_by_type(bsg_node *root, unsigned long long flags)
{
    if (!root || !flags)
	return NULL;

    struct bv_scene_obj *r = (struct bv_scene_obj *)root;
    for (size_t i = 0; i < BU_PTBL_LEN(&r->children); i++) {
	struct bv_scene_obj *child =
	    (struct bv_scene_obj *)BU_PTBL_GET(&r->children, i);
	if (!child)
	    continue;
	if ((child->s_type_flags & flags) == flags)
	    return (bsg_node *)child;
    }
    return NULL;
}


void
bsg_sensor_fire(bsg_node *root, struct bview *v)
{
    if (!root)
	return;

    struct bv_scene_obj *r = (struct bv_scene_obj *)root;

    /* Depth-first traversal of the subtree. */
    for (size_t i = 0; i < BU_PTBL_LEN(&r->children); i++) {
	struct bv_scene_obj *child =
	    (struct bv_scene_obj *)BU_PTBL_GET(&r->children, i);
	if (!child)
	    continue;

	/* Recurse into subtree first. */
	bsg_sensor_fire((bsg_node *)child, v);

	/* Fire the sensor callback on this node if applicable. */
	if ((child->s_type_flags & BSG_NODE_SENSOR) && child->s_update_callback)
	    child->s_update_callback(child, v, 0);
    }
}

/* ---------------------------------------------------------------------- */
/* bsg_scene_set (stub implementation for Phase 4; grows in Phase 5+)      */
/* ---------------------------------------------------------------------- */

/* Simple linked-list cell for the registry. */
struct _bsg_ss_entry {
    struct bview *v;
    bsg_node *root;
    struct _bsg_ss_entry *next;
};

struct bsg_scene_set {
    struct _bsg_ss_entry *head;
};

struct bsg_scene_set *
bsg_scene_set_create(void)
{
    struct bsg_scene_set *ss;
    BU_ALLOC(ss, struct bsg_scene_set);
    ss->head = NULL;
    return ss;
}

void
bsg_scene_set_destroy(struct bsg_scene_set *ss)
{
    if (!ss)
	return;

    struct _bsg_ss_entry *e = ss->head;
    while (e) {
	struct _bsg_ss_entry *next = e->next;
	bu_free(e, "bsg_ss_entry");
	e = next;
    }
    bu_free(ss, "bsg_scene_set");
}

void
bsg_scene_set_add(struct bsg_scene_set *ss, struct bview *v, bsg_node *root)
{
    if (!ss || !v)
	return;

    /* Update existing entry if present. */
    for (struct _bsg_ss_entry *e = ss->head; e; e = e->next) {
	if (e->v == v) {
	    e->root = root;
	    return;
	}
    }

    /* Otherwise prepend a new entry. */
    struct _bsg_ss_entry *ne;
    BU_ALLOC(ne, struct _bsg_ss_entry);
    ne->v = v;
    ne->root = root;
    ne->next = ss->head;
    ss->head = ne;
}

bsg_node *
bsg_scene_set_get(struct bsg_scene_set *ss, struct bview *v)
{
    if (!ss || !v)
	return NULL;

    for (struct _bsg_ss_entry *e = ss->head; e; e = e->next) {
	if (e->v == v)
	    return e->root;
    }
    return NULL;
}

void
bsg_scene_set_remove(struct bsg_scene_set *ss, struct bview *v)
{
    if (!ss || !v)
	return;

    struct _bsg_ss_entry **pp = &ss->head;
    while (*pp) {
	if ((*pp)->v == v) {
	    struct _bsg_ss_entry *del = *pp;
	    *pp = del->next;
	    bu_free(del, "bsg_ss_entry");
	    return;
	}
	pp = &(*pp)->next;
    }
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
