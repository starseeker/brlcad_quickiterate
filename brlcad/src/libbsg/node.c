/*                          N O D E . C
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
/** @file libbsg/node.c
 *
 * Generic BSG node lifecycle and accessor API.
 */

#include "common.h"

#include "bu/ptbl.h"
#include "vmath.h"

#include "bsg/defines.h"
#include "bsg/draw_set.h"
#include "bsg/identity.h"
#include "bsg/node.h"
#include "bsg/util.h"


bsg_node *
bsg_node_create(struct bsg_view *v, unsigned long long kind)
{
    if (!v)
	return NULL;

    bsg_node *n = bsg_obj_create(v, BSG_OBJ_VIEW | BSG_OBJ_LOCAL);
    if (!n)
	return NULL;

    n->s_type_flags = kind;
    n->s_flag = UP;
    n->s_changed = 0;
    if (kind == BSG_NODE_TRANSFORM)
	MAT_IDN(n->s_mat);

    return n;
}


void
bsg_node_destroy(bsg_node *node)
{
    if (!node)
	return;

    if (node->parent)
	bsg_node_remove_child(node->parent, node);

    for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	bsg_node *c = (bsg_node *)BU_PTBL_GET(&node->children, i);
	if (c && c->parent == node)
	    c->parent = NULL;
    }
    bu_ptbl_reset(&node->children);
    bsg_obj_put(node);
}


unsigned long long
bsg_node_kind(const bsg_node *node)
{
    if (!node)
	return 0;
    return node->s_type_flags;
}


int
bsg_node_is_kind(const bsg_node *node, unsigned long long kind)
{
    if (!node)
	return 0;
    return (node->s_type_flags & kind) ? 1 : 0;
}


void
bsg_node_set_name(bsg_node *node, const char *name)
{
    bsg_node_identity_set_name(node, name);
}


const char *
bsg_node_name(const bsg_node *node)
{
    return bsg_node_identity_name(node);
}


bsg_node *
bsg_node_parent(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->parent;
}


size_t
bsg_node_child_count(const bsg_node *node)
{
    if (!node)
	return 0;
    return BU_PTBL_LEN(&node->children);
}


bsg_node *
bsg_node_child_at(const bsg_node *node, size_t idx)
{
    if (!node || idx >= BU_PTBL_LEN(&node->children))
	return NULL;
    return (bsg_node *)BU_PTBL_GET(&node->children, idx);
}


void
bsg_node_add_child(bsg_node *parent, bsg_node *child)
{
    if (!parent || !child || parent == child)
	return;

    for (size_t i = 0; i < BU_PTBL_LEN(&parent->children); i++) {
	if ((bsg_node *)BU_PTBL_GET(&parent->children, i) == child)
	    return;
    }

    if (child->parent && child->parent != parent)
	bsg_node_remove_child(child->parent, child);

    child->parent = parent;
    bu_ptbl_ins(&parent->children, (long *)child);
    bsg_node_touch(parent);
    bsg_node_bbox_invalidate(parent);
}


void
bsg_node_remove_child(bsg_node *parent, bsg_node *child)
{
    if (!parent || !child)
	return;

    intmax_t loc = bu_ptbl_locate(&parent->children, (const long *)child);
    if (loc < 0)
	return;

    bu_ptbl_rm(&parent->children, (const long *)child);
    if (child->parent == parent)
	child->parent = NULL;
    bsg_node_touch(parent);
    bsg_node_bbox_invalidate(parent);
}


void
bsg_node_set_visible_state(bsg_node *node, int on)
{
    if (!node)
	return;
    node->s_flag = on ? UP : DOWN;
    bsg_node_touch(node);
}


int
bsg_node_visible(const bsg_node *node)
{
    if (!node)
	return 0;
    return (node->s_flag == UP) ? 1 : 0;
}


void
bsg_node_set_transform(bsg_node *node, const mat_t mat)
{
    if (!node || !mat)
	return;
    MAT_COPY(node->s_mat, mat);
    bsg_node_touch(node);
}


void
bsg_node_transform(const bsg_node *node, mat_t mat)
{
    if (!node || !mat)
	return;
    MAT_COPY(mat, node->s_mat);
}


void
bsg_node_set_bounds(bsg_node *node, const point_t bmin, const point_t bmax, int valid)
{
    if (!node)
	return;

    node->have_bbox = valid ? 1 : 0;
    if (valid && bmin && bmax) {
	VMOVE(node->bmin, bmin);
	VMOVE(node->bmax, bmax);
    }
    node->s_bbox_cached = valid ? 1 : 0;
    bsg_node_touch(node);
}


int
bsg_node_bounds(const bsg_node *node, point_t bmin, point_t bmax)
{
    if (!node || !node->have_bbox)
	return 0;

    if (bmin)
	VMOVE(bmin, node->bmin);
    if (bmax)
	VMOVE(bmax, node->bmax);
    return 1;
}


void
bsg_node_set_user_data(bsg_node *node, void *user_data)
{
    if (!node)
	return;
    node->s_u_data = user_data;
    bsg_node_touch(node);
}


void *
bsg_node_user_data(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->s_u_data;
}


struct bsg_obj_settings *
bsg_node_settings(bsg_node *node)
{
    if (!node)
	return NULL;
    return node->s_os;
}


uint64_t
bsg_node_revision(const bsg_node *node)
{
    return bsg_node_revision_get(node);
}


void
bsg_node_touch(bsg_node *node)
{
    (void)bsg_node_revision_bump(node);
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
