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

#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "bv/util.h"

#include "bsg/defines.h"
#include "bsg/draw_set.h"


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

    return (bsg_node *)child;
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
