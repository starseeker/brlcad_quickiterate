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
 *
 * Phase D (bv_scene_obj_migrate.txt): bsg_node_create/create_child/destroy now
 * own allocation and teardown in libbsg rather than delegating to bv_obj_*.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/vlist.h"
#include "vmath.h"
#include "bsg/field.h"
#include "bsg/node.h"

#include "./bsg_private.h"

static struct bv_scene_obj *
_bsg_node_alloc(struct bview *v, unsigned long long kind, int as_draw_child)
{
    if (!v)
	return NULL;

    struct bv_scene_obj *s = NULL;
    BU_ALLOC(s, struct bv_scene_obj);

    BU_LIST_INIT(&s->bsg.l);
    BU_PTBL_INIT(&s->bsg.bsg_children);
    BU_LIST_INIT(&s->s_vlist);
    BU_VLS_INIT(&s->bsg.bsg_name);

    s->i = NULL;
    s->s_v = v;
    s->vlfree = &v->gv_objs.gv_vlfree;
    s->free_scene_obj = NULL;
    s->otbl = NULL;
    s->s_backend = NULL;
    s->s_update_callback = NULL;
    s->s_free_callback = NULL;
    s->bsg.bsg_parent = NULL;

    bv_scene_obj_settings_reset(s);
    s->s_inherit_settings = 0;

    MAT_IDN(s->s_mat);
    VSET(s->s_color, 255, 0, 0);
    VSETALL(s->bmax, -INFINITY);
    VSETALL(s->bmin, INFINITY);
    VSETALL(s->s_center, 0);

    s->adaptive_wireframe = 0;
    s->bot_threshold = 0;
    s->csg_obj = 0;
    s->current = 0;
    s->curve_scale = 0;
    s->dp = NULL;
    s->draw_data = NULL;
    s->have_bbox = 0;
    s->mesh_obj = 0;
    s->point_scale = 0;
    s->s_arrow = 0;
    s->s_csize = 0;
    s->s_color_rev = 0;
    s->bsg.bsg_flag = UP;
    s->bsg.bsg_force_draw = 0;
    s->s_i_data = NULL;
    s->bsg.bsg_iflag = DOWN;
    s->s_path = NULL;
    s->s_size = 0;
    s->s_soldash = 0;
    s->view_scale = 0;
    s->s_vlen = 0;
    s->s_displayobj = 0;
    s->s_bbox_cached = 0;
    s->s_drawn_rev = 0;
    s->bsg.bsg_magic = 0;

    if (as_draw_child)
	s->bsg.bsg_kind = BV_CHILD_OBJS;
    else
	s->bsg.bsg_kind = BV_VIEW_OBJS | BV_LOCAL_OBJS;
    bsg_node_set_kind((bsg_node *)s, kind | s->bsg.bsg_kind);
    bsg_node_set_visible((bsg_node *)s, 1);
    return s;
}


bsg_node *
bsg_node_create(struct bview *v, unsigned long long kind)
{
    return (bsg_node *)_bsg_node_alloc(v, kind, 0);
}


bsg_node *
bsg_node_create_child(struct bview *v, unsigned long long kind)
{
    return (bsg_node *)_bsg_node_alloc(v, kind, 1);
}


void
bsg_node_clear_children(bsg_node *n)
{
    struct bv_scene_obj *s;

    if (!n)
	return;

    s = (struct bv_scene_obj *)n;
    for (size_t i = 0; i < BU_PTBL_LEN(&s->bsg.bsg_children); i++) {
	bsg_node *child = (bsg_node *)BU_PTBL_GET(&s->bsg.bsg_children, i);
	if (child && ((struct bv_scene_obj *)child)->bsg.bsg_parent == n)
	    ((struct bv_scene_obj *)child)->bsg.bsg_parent = NULL;
    }
    bu_ptbl_reset(&s->bsg.bsg_children);
    bsg_node_field_touch(n, BSG_FIELD_CHILDREN);
}


void
bsg_node_destroy(bsg_node *n)
{
    if (!n)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;

    if (s->bsg.bsg_parent)
	bsg_node_remove_child(s->bsg.bsg_parent, n);

    while (BU_PTBL_IS_INITIALIZED(&s->bsg.bsg_children) &&
	   BU_PTBL_LEN(&s->bsg.bsg_children) > 0) {
	struct bv_scene_obj *child =
	    (struct bv_scene_obj *)BU_PTBL_GET(&s->bsg.bsg_children, BU_PTBL_LEN(&s->bsg.bsg_children) - 1);
	if (!child)
	    break;
	bsg_node_remove_child(n, (bsg_node *)child);
	bsg_node_destroy((bsg_node *)child);
    }
    if (BU_PTBL_IS_INITIALIZED(&s->bsg.bsg_children))
	bu_ptbl_reset(&s->bsg.bsg_children);

    bsg_node_invoke_free_callback(n);
    bsg_node_set_free_callback(n, NULL);
    bv_scene_obj_release_backend(s);

    if ((s->bsg.bsg_kind & BV_LABELS) && bsg_node_user_data_get((const bsg_node *)s)) {
	struct bv_label *la = (struct bv_label *)bsg_node_user_data_get((const bsg_node *)s);
	bu_vls_free(&la->label);
	BU_PUT(la, struct bv_label);
    }

    if (BU_LIST_IS_INITIALIZED(&s->s_vlist) && s->vlfree)
	BV_FREE_VLIST(s->vlfree, &s->s_vlist);
    BU_LIST_INIT(&s->s_vlist);

    if (s->s_v)
	bu_ptbl_rm(&s->s_v->gv_s->gv_snap_objs, (long *)s);

    if (BU_VLS_IS_INITIALIZED(&s->bsg.bsg_name))
	bu_vls_free(&s->bsg.bsg_name);

    if (s->bsg.bsg_magic == BSG_NODE_CORE_MAGIC && s->bsg.bsg_core_free_fn)
	s->bsg.bsg_core_free_fn(&s->bsg);
    if (s->bsg.settings_local)
	bu_free(s->bsg.settings_local, "bsg_node settings_local");
    if (s->bsg.settings_effective)
	bu_free(s->bsg.settings_effective, "bsg_node settings_effective");

    if (BU_PTBL_IS_INITIALIZED(&s->bsg.bsg_children))
	bu_ptbl_free(&s->bsg.bsg_children);

    BU_PUT(s, struct bv_scene_obj);
}


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


void *
bsg_node_source_path_get(const bsg_node *n)
{
    if (!n)
	return NULL;

    return ((const struct bv_scene_obj *)n)->s_path;
}


void
bsg_node_source_path_set(bsg_node *n, void *path)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_path = path;
}


void *
bsg_node_app_data_get(const bsg_node *n)
{
    if (!n)
	return NULL;

    return ((const struct bv_scene_obj *)n)->dp;
}


void
bsg_node_app_data_set(bsg_node *n, void *data)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->dp = data;
}


struct bview *
bsg_node_view_get(const bsg_node *n)
{
    if (!n)
	return NULL;

    return ((const struct bv_scene_obj *)n)->s_v;
}


void
bsg_node_view_set(bsg_node *n, struct bview *v)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_v = v;
}


struct bv_obj_backend *
bsg_node_backend_get(const bsg_node *n)
{
    if (!n)
	return NULL;

    return ((const struct bv_scene_obj *)n)->s_backend;
}


void
bsg_node_backend_set(bsg_node *n, struct bv_obj_backend *backend)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_backend = backend;
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


fastf_t
bsg_node_size_get(const bsg_node *n)
{
    if (!n)
	return 0.0;

    return ((const struct bv_scene_obj *)n)->s_size;
}


void
bsg_node_size_set(bsg_node *n, fastf_t size)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_size = size;
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


void
bsg_node_set_update_callback(bsg_node *n, bsg_node_update_fn cb)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_update_callback =
	(int (*)(struct bv_scene_obj *, struct bview *, int))cb;
}


int
bsg_node_invoke_update_callback(bsg_node *n, struct bview *v, int flags)
{
    struct bv_scene_obj *s;

    if (!n)
	return 0;

    s = (struct bv_scene_obj *)n;
    if (!s->s_update_callback)
	return 0;

    return s->s_update_callback(s, v, flags);
}


void
bsg_node_center_get(const bsg_node *n, vect_t out)
{
    if (!n || !out)
	return;

    VMOVE(out, ((const struct bv_scene_obj *)n)->s_center);
}


void
bsg_node_center_set(bsg_node *n, const vect_t center)
{
    if (!n || !center)
	return;

    VMOVE(((struct bv_scene_obj *)n)->s_center, center);
}


int
bsg_node_bbox_valid(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->have_bbox ? 1 : 0;
}


void
bsg_node_set_bbox_valid(bsg_node *n, int valid)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->have_bbox = valid ? 1 : 0;
}


int
bsg_node_bbox_cached(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_bbox_cached ? 1 : 0;
}


void
bsg_node_set_bbox_cached(bsg_node *n, int cached)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_bbox_cached = cached ? 1 : 0;
}


void
bsg_node_set_display_obj(bsg_node *n, int is_display)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_displayobj = is_display ? 1 : 0;
}


int
bsg_node_legacy_uflag(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_old.s_uflag ? 1 : 0;
}


void
bsg_node_set_legacy_uflag(bsg_node *n, int uflag)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_old.s_uflag = uflag ? 1 : 0;
}


int
bsg_node_legacy_dflag(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_old.s_dflag ? 1 : 0;
}


void
bsg_node_set_legacy_dflag(bsg_node *n, int dflag)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_old.s_dflag = dflag ? 1 : 0;
}


int
bsg_node_legacy_cflag(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_old.s_cflag ? 1 : 0;
}


void
bsg_node_set_legacy_cflag(bsg_node *n, int cflag)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_old.s_cflag = cflag ? 1 : 0;
}


void
bsg_node_legacy_basecolor_get(const bsg_node *n,
			      unsigned char *r,
			      unsigned char *g,
			      unsigned char *b)
{
    if (!n)
	return;

    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    if (r) *r = s->s_old.s_basecolor[0];
    if (g) *g = s->s_old.s_basecolor[1];
    if (b) *b = s->s_old.s_basecolor[2];
}


void
bsg_node_legacy_basecolor_set(bsg_node *n,
			      unsigned char r,
			      unsigned char g,
			      unsigned char b)
{
    if (!n)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    s->s_old.s_basecolor[0] = r;
    s->s_old.s_basecolor[1] = g;
    s->s_old.s_basecolor[2] = b;
}


short
bsg_node_legacy_regionid(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_old.s_regionid;
}


void
bsg_node_set_legacy_regionid(bsg_node *n, short regionid)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_old.s_regionid = regionid;
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


int
bsg_node_legacy_wflag(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_old.s_wflag ? 1 : 0;
}


void
bsg_node_set_legacy_wflag(bsg_node *n, int wflag)
{
    if (!n)
	return;

    ((struct bv_scene_obj *)n)->s_old.s_wflag = wflag ? 1 : 0;
}


struct bu_list *
bsg_node_vlfree(const bsg_node *n)
{
    if (!n)
	return NULL;

    return ((const struct bv_scene_obj *)n)->vlfree;
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
