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

#include "./bsg_private.h"


unsigned long long
bsg_node_kind(const bsg_node *n)
{
    if (!n)
	return 0;

    return n->bsg_kind;
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

    _bsg_core_ensure(n);
    n->bsg_kind = kind;
    bsg_node_field_touch(n, BSG_FIELD_KIND);
}


const char *
bsg_node_name(const bsg_node *n)
{
    if (!n)
	return NULL;

    return bu_vls_cstr(&n->bsg_name);
}


void
bsg_node_set_name(bsg_node *n, const char *name)
{
    if (!n)
	return;

    if (!name) {
	bu_vls_trunc(&n->bsg_name, 0);
    } else {
	bu_vls_sprintf(&n->bsg_name, "%s", name);
    }
    bsg_node_field_touch(n, BSG_FIELD_NAME);
}


bsg_node *
bsg_node_parent(const bsg_node *n)
{
    if (!n)
	return NULL;

    return n->bsg_parent;
}


size_t
bsg_node_child_count(const bsg_node *n)
{
    if (!n)
	return 0;

    return BU_PTBL_LEN(&n->bsg_children);
}


bsg_node *
bsg_node_child(const bsg_node *n, size_t idx)
{
    if (!n || idx >= bsg_node_child_count(n))
	return NULL;

    return (bsg_node *)BU_PTBL_GET(&n->bsg_children, idx);
}


void
bsg_node_add_child(bsg_node *parent, bsg_node *child)
{
    if (!parent || !child || parent == child)
	return;

    for (size_t i = 0; i < BU_PTBL_LEN(&parent->bsg_children); i++) {
	if ((bsg_node *)BU_PTBL_GET(&parent->bsg_children, i) == child)
	    return;
    }

    bu_ptbl_ins(&parent->bsg_children, (long *)child);
    child->bsg_parent = parent;
    bsg_node_field_touch(parent, BSG_FIELD_CHILDREN);
}


void
bsg_node_remove_child(bsg_node *parent, bsg_node *child)
{
    if (!parent || !child)
	return;

    int found = 0;
    for (size_t i = 0; i < BU_PTBL_LEN(&parent->bsg_children); i++) {
	if ((bsg_node *)BU_PTBL_GET(&parent->bsg_children, i) == child) {
	    found = 1;
	    break;
	}
    }

    if (!found)
	return;

    bu_ptbl_rm(&parent->bsg_children, (const long *)child);
    if (child->bsg_parent == parent)
	child->bsg_parent = NULL;
    bsg_node_field_touch(parent, BSG_FIELD_CHILDREN);
}


int
bsg_node_visible(const bsg_node *n)
{
    if (!n)
	return 0;

    return (n->bsg_flag == UP) ? 1 : 0;
}


int
bsg_node_force_draw(const bsg_node *n)
{
    if (!n)
	return 0;

    return n->bsg_force_draw ? 1 : 0;
}


/**
 * @brief Set whether a node should draw even when inherited visibility rules
 * would otherwise suppress it.
 *
 * Stores 0/1 in bsg_force_draw and fires BSG_FIELD_FORCE_DRAW notifications.
 */
void
bsg_node_set_force_draw(bsg_node *n, int force_draw)
{
    if (!n)
	return;

    n->bsg_force_draw = force_draw ? 1 : 0;
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


int
bsg_node_legacy_illum(const bsg_node *n)
{
    if (!n)
	return 0;

    return (n->bsg_iflag == UP) ? 1 : 0;
}


void
bsg_node_set_legacy_illum(bsg_node *n, int illuminated)
{
    if (!n)
	return;

    n->bsg_iflag = illuminated ? UP : DOWN;
}


int
bsg_node_is_display_obj(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_displayobj ? 1 : 0;
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


void *
bsg_node_ged_data_get(const bsg_node *n)
{
    if (!n)
	return NULL;

    return ((const struct bv_scene_obj *)n)->s_u_data;
}


void
bsg_node_ged_data_set(bsg_node *n, void *data)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_u_data = data;
}


void
bsg_node_set_free_callback(bsg_node *n, bsg_node_free_fn cb)
{
    if (!n)
	return;

    /* bsg_node and bv_scene_obj are related via first-member embedding;
     * function pointer cast is safe for the current storage model. */
    ((struct bv_scene_obj *)n)->s_free_callback =
	(void (*)(struct bv_scene_obj *))cb;
}


void
bsg_node_invoke_free_callback(bsg_node *n)
{
    if (!n)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    if (s->s_free_callback)
	s->s_free_callback(s);
}


int
bsg_node_legacy_eflag(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_old.s_Eflag ? 1 : 0;
}


void
bsg_node_set_legacy_eflag(bsg_node *n, int eflag)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_old.s_Eflag = eflag ? 1 : 0;
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
