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
 *   bsg_scene_root_create  — allocate a synthetic root node for a view
 *   bsg_scene_root_sync    — mirror view objs into root children (shim)
 *   bsg_scene_root_destroy — release root node back to the view pool
 *   bsg_view_find_by_type  — locate first child matching type flags
 *   bsg_sensor_fire        — invoke callbacks on BSG_NODE_SENSOR nodes
 *
 * bsg_view_traverse() is intentionally NOT implemented here because it
 * must call dm_draw_obj() and other libdm rendering functions.  It lives
 * in src/libdm/view.c (DM_EXPORT) so that libdm can reach those symbols
 * directly without creating a libbsg → libdm dependency cycle.
 */

#include "common.h"

#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "bv/util.h"

#include "bsg/defines.h"
#include "bsg/draw_set.h"
#include "bsg/util.h"
#include "bsg/visit.h"
#include "bsg/scene_set.h"

/* ---------------------------------------------------------------------- */
/* Internal helpers                                                         */
/* ---------------------------------------------------------------------- */

/**
 * bsg_node_init — minimal initialisation of a freshly allocated root node.
 * The node is expected to have been produced by bv_obj_create() so that
 * its i-pointer, vlfree, free_scene_obj and children ptbl are valid.
 */
static void
bsg_node_init_root(bsg_node *root, struct bview *v)
{
    root->s_type_flags = (unsigned long long)BSG_NODE_ROOT;
    root->s_flag = UP;
    root->s_v = v;
    bu_vls_sprintf(&root->s_name, "bsg_root");
}

/* ---------------------------------------------------------------------- */
/* Public API                                                               */
/* ---------------------------------------------------------------------- */

bsg_node *
bsg_scene_root_create(struct bview *v)
{
    if (!v)
	return NULL;

    /* Allocate through libbv so the node gets a proper i-pointer,
     * vlfree reference, etc.  bv_obj_create does NOT insert the object
     * into any view table, which is exactly what we need for a synthetic
     * root. */
    struct bv_scene_obj *root = bv_obj_create(v, BV_VIEW_OBJS | BV_LOCAL_OBJS);
    if (!root) {
	bu_log("bsg_scene_root_create: bv_obj_create failed for view %s\n",
	       bu_vls_cstr(&v->gv_name));
	return NULL;
    }

    bsg_node_init_root(root, v);

    v->bsg_root = root;
    return (bsg_node *)root;
}


void
bsg_scene_root_sync(bsg_node *root, struct bview *v)
{
    if (!root || !v)
	return;

    struct bv_scene_obj *r = (struct bv_scene_obj *)root;

    /* Reset children without freeing them — they are borrowed references. */
    bu_ptbl_reset(&r->children);

    /* Phase 7 step 7 A3: when a GED draw-tree root is registered on the view,
     * use it as the authoritative source for db-objects.  The draw tree's
     * top-level children (groups + overlay group) are inserted directly into
     * the render root; bsg_view_traverse() handles the nested structure
     * recursively.
     *
     * View-only objects (faceplate polygons, axes, labels, etc.) were never
     * moved into the BSG draw tree — they continue to live in the view's
     * BV_VIEW_OBJS / BV_VIEW_OBJS|BV_LOCAL_OBJS ptbls.  We must therefore
     * also append those when a GED draw root is set, otherwise faceplate /
     * view-only geometry is invisible to the BSG render path.
     *
     * BV_DB_OBJS is intentionally not folded in here when gv_draw_root is
     * set: that ptbl is a derived/compat index and must not become a second
     * source of truth for db-objects rendering. */
    if (v->gv_draw_root) {
	struct bv_scene_obj *dr = (struct bv_scene_obj *)v->gv_draw_root;
	for (size_t i = 0; i < BU_PTBL_LEN(&dr->children); i++)
	    bu_ptbl_ins(&r->children, BU_PTBL_GET(&dr->children, i));

	/* Shared view-only objects */
	struct bu_ptbl *vobjs_a = bv_view_objs(v, BV_VIEW_OBJS);
	if (vobjs_a) {
	    for (size_t i = 0; i < BU_PTBL_LEN(vobjs_a); i++)
		bu_ptbl_ins(&r->children, BU_PTBL_GET(vobjs_a, i));
	}

	/* Local view-only objects (only if distinct from shared) */
	struct bu_ptbl *lvobjs_a = bv_view_objs(v, BV_VIEW_OBJS | BV_LOCAL_OBJS);
	if (lvobjs_a && lvobjs_a != vobjs_a) {
	    for (size_t i = 0; i < BU_PTBL_LEN(lvobjs_a); i++)
		bu_ptbl_ins(&r->children, BU_PTBL_GET(lvobjs_a, i));
	}
	return;
    }

    /* Legacy fallback: read from the view's gv_objs ptbls.  Used when no GED
     * draw tree has been registered (e.g. non-GED BSG consumers). */

    /* Shared db objects */
    struct bu_ptbl *sobjs = bv_view_objs(v, BV_DB_OBJS);
    if (sobjs) {
	for (size_t i = 0; i < BU_PTBL_LEN(sobjs); i++)
	    bu_ptbl_ins(&r->children, BU_PTBL_GET(sobjs, i));
    }

    /* Local db objects (only if distinct from shared) */
    struct bu_ptbl *lobjs = bv_view_objs(v, BV_DB_OBJS | BV_LOCAL_OBJS);
    if (lobjs && lobjs != sobjs) {
	for (size_t i = 0; i < BU_PTBL_LEN(lobjs); i++)
	    bu_ptbl_ins(&r->children, BU_PTBL_GET(lobjs, i));
    }

    /* Shared view-only objects */
    struct bu_ptbl *vobjs = bv_view_objs(v, BV_VIEW_OBJS);
    if (vobjs) {
	for (size_t i = 0; i < BU_PTBL_LEN(vobjs); i++)
	    bu_ptbl_ins(&r->children, BU_PTBL_GET(vobjs, i));
    }

    /* Local view-only objects (only if distinct from shared) */
    struct bu_ptbl *lvobjs = bv_view_objs(v, BV_VIEW_OBJS | BV_LOCAL_OBJS);
    if (lvobjs && lvobjs != vobjs) {
	for (size_t i = 0; i < BU_PTBL_LEN(lvobjs); i++)
	    bu_ptbl_ins(&r->children, BU_PTBL_GET(lvobjs, i));
    }
}


void
bsg_scene_root_destroy(bsg_node *root)
{
    if (!root)
	return;

    struct bv_scene_obj *r = (struct bv_scene_obj *)root;

    /* Children are borrowed refs — clear without freeing. */
    bu_ptbl_reset(&r->children);

    /* Clear the view's back-reference if it still points at us. */
    if (r->s_v && r->s_v->bsg_root == root)
	r->s_v->bsg_root = NULL;

    /* Return the node to the libbv free pool. */
    bv_obj_put(r);
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
