/*                         N O D E . C
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
 * Generic BSG node accessors over the current bv_scene_obj storage.
 */

#include "common.h"

#include <string.h>

#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "vmath.h"
#include "bsg/field.h"
#include "bsg/node.h"


unsigned long long
bsg_node_kind(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_type_flags;
}


int
bsg_node_has_kind(const bsg_node *n, unsigned long long kind)
{
    if (!n || !kind)
	return 0;

    return ((bsg_node_kind(n) & kind) == kind) ? 1 : 0;
}


void
bsg_node_set_kind(bsg_node *n, unsigned long long kind)
{
    if (!n)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    s->s_type_flags = kind;
    bsg_node_field_touch(n, BSG_FIELD_KIND);
}


const char *
bsg_node_name(const bsg_node *n)
{
    if (!n)
	return NULL;

    return bu_vls_cstr(&((const struct bv_scene_obj *)n)->s_name);
}


void
bsg_node_set_name(bsg_node *n, const char *name)
{
    if (!n)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    if (!name) {
	bu_vls_trunc(&s->s_name, 0);
    } else {
	bu_vls_sprintf(&s->s_name, "%s", name);
    }
    bsg_node_field_touch(n, BSG_FIELD_NAME);
}


bsg_node *
bsg_node_parent(const bsg_node *n)
{
    if (!n)
	return NULL;

    return (bsg_node *)((const struct bv_scene_obj *)n)->parent;
}


size_t
bsg_node_child_count(const bsg_node *n)
{
    if (!n)
	return 0;

    return BU_PTBL_LEN(&((const struct bv_scene_obj *)n)->children);
}


bsg_node *
bsg_node_child(const bsg_node *n, size_t idx)
{
    if (!n || idx >= bsg_node_child_count(n))
	return NULL;

    return (bsg_node *)BU_PTBL_GET(&((const struct bv_scene_obj *)n)->children, idx);
}


void
bsg_node_add_child(bsg_node *parent, bsg_node *child)
{
    if (!parent || !child || parent == child)
	return;

    struct bv_scene_obj *p = (struct bv_scene_obj *)parent;
    struct bv_scene_obj *c = (struct bv_scene_obj *)child;

    for (size_t i = 0; i < BU_PTBL_LEN(&p->children); i++) {
	if ((struct bv_scene_obj *)BU_PTBL_GET(&p->children, i) == c) {
	    return;
	}
    }

    bu_ptbl_ins(&p->children, (long *)c);
    c->parent = p;
    bsg_node_field_touch(parent, BSG_FIELD_CHILDREN);
}


void
bsg_node_remove_child(bsg_node *parent, bsg_node *child)
{
    if (!parent || !child)
	return;

    struct bv_scene_obj *p = (struct bv_scene_obj *)parent;
    struct bv_scene_obj *c = (struct bv_scene_obj *)child;
    int found = 0;

    for (size_t i = 0; i < BU_PTBL_LEN(&p->children); i++) {
	if ((struct bv_scene_obj *)BU_PTBL_GET(&p->children, i) == c) {
	    found = 1;
	    break;
	}
    }

    if (!found)
	return;

    bu_ptbl_rm(&p->children, (const long *)c);
    if (c->parent == p)
	c->parent = NULL;
    bsg_node_field_touch(parent, BSG_FIELD_CHILDREN);
}


int
bsg_node_visible(const bsg_node *n)
{
    if (!n)
	return 0;

    return (((const struct bv_scene_obj *)n)->s_flag == UP) ? 1 : 0;
}


int
bsg_node_force_draw(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_force_draw ? 1 : 0;
}


/**
 * @brief Set whether a node should draw even when inherited visibility rules
 * would otherwise suppress it.
 *
 * Stores 0/1 in s_force_draw and fires BSG_FIELD_FORCE_DRAW notifications.
 */
void
bsg_node_set_force_draw(bsg_node *n, int force_draw)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_force_draw = force_draw ? 1 : 0;
    bsg_node_field_touch(n, BSG_FIELD_FORCE_DRAW);
}


void
bsg_node_transform_get(const bsg_node *n, mat_t out)
{
    if (!out)
	return;

    MAT_IDN(out);
    if (!n)
	return;

    MAT_COPY(out, ((const struct bv_scene_obj *)n)->s_mat);
}


void
bsg_node_transform_set(bsg_node *n, const mat_t mat)
{
    if (!n || !mat)
	return;

    MAT_COPY(((struct bv_scene_obj *)n)->s_mat, mat);
    bsg_node_field_touch(n, BSG_FIELD_TRANSFORM);
}


void *
bsg_node_user_data_get(const bsg_node *n)
{
    if (!n)
	return NULL;

    return ((const struct bv_scene_obj *)n)->s_i_data;
}


void
bsg_node_user_data_set(bsg_node *n, void *data)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_i_data = data;
    bsg_node_field_touch(n, BSG_FIELD_USER_DATA);
}


void
bsg_node_bounds_get(const bsg_node *n, point_t bmin, point_t bmax)
{
    if (!n)
	return;

    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    if (bmin) {
	if (s->have_bbox) {
	    VMOVE(bmin, s->bmin);
	} else {
	    VSETALL(bmin, 0.0);
	}
    }
    if (bmax) {
	if (s->have_bbox) {
	    VMOVE(bmax, s->bmax);
	} else {
	    VSETALL(bmax, 0.0);
	}
    }
}


void
bsg_node_bounds_set(bsg_node *n, const point_t bmin, const point_t bmax)
{
    if (!n || !bmin || !bmax)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    VMOVE(s->bmin, bmin);
    VMOVE(s->bmax, bmax);
    s->have_bbox = 1;
    bsg_node_field_touch(n, BSG_FIELD_BOUNDS);
}


void
bsg_node_mark_stale(bsg_node *n)
{
    if (!n)
	return;

    bv_obj_stale((struct bv_scene_obj *)n);
}


uint64_t
bsg_node_drawn_rev(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_drawn_rev;
}


void
bsg_node_set_drawn_rev(bsg_node *n, uint64_t rev)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_drawn_rev = rev;
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
