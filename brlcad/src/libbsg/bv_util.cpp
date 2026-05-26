/*                      U T I L . C P P
 * BRL-CAD
 *
 * Copyright (c) 2020-2026 United States Government as represented by
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
/** @file util.cpp
 *
 * Utility functions for operating on BRL-CAD views
 *
 */

#include "common.h"
#include <queue>
#include <string.h>
#include "vmath.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/path.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bn/mat.h"
#include "bg/plane.h"
#include "bsg/appearance.h"
#include "bsg/defines.h"
#include "bsg/material.h"
#include "bsg/snap.h"
#include "bsg/util.h"
#include "bsg/view_sets.h"
#include "bsg/vlist.h"
#include "bsg/defines.h"
#include "bsg/lod_ops.h"
#include "./bsg_private.h"

#define VIEW_NAME_MAXTRIES 100000
#define DM_DEFAULT_FONT_SIZE 20
#define BV_INDEPENDENT_SCOPE_NAME "_independent_db_scope"

static const char *
_bv_vname(struct bsg_view *v)
{
    if (!v)
	return "NULL";
    if (!BU_VLS_IS_INITIALIZED(&v->gv_name))
	return "<unnamed>";
    return bu_vls_cstr(&v->gv_name);
}

static int
_bv_is_independent_scope(struct bsg_node *s, const struct bsg_view *owner)
{
    if (!s)
	return 0;
    if (!(s->s_type_flags & BSG_NODE_VIEW_SCOPE))
	return 0;
    if (s->s_v != owner)
	return 0;
    if (!BU_VLS_IS_INITIALIZED(&s->s_name))
	return 0;
    return BU_STR_EQUAL(bu_vls_cstr(&s->s_name), BV_INDEPENDENT_SCOPE_NAME);
}

static struct bsg_node *
_bv_independent_scope_find(struct bsg_node *root, const struct bsg_view *owner)
{
    if (!root || !owner)
	return NULL;

    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	struct bsg_node *c = (struct bsg_node *)BU_PTBL_GET(&root->children, i);
	if (_bv_is_independent_scope(c, owner))
	    return c;
    }

    return NULL;
}

static void
_bv_scope_free_recursive(struct bsg_node *node)
{
    if (!node)
	return;

    size_t i = BU_PTBL_LEN(&node->children);
    while (i > 0) {
	i--;
	struct bsg_node *child = (struct bsg_node *)BU_PTBL_GET(&node->children, i);
	if (!child)
	    continue;
	bu_ptbl_rm(&node->children, (long *)child);
	_bv_scope_free_recursive(child);
    }

    bsg_obj_put(node);
}

static int
_bv_independent_root_skip_child(struct bsg_view *v, struct bsg_node *parent, struct bsg_node *child)
{
    if (!v || !parent || !child)
	return 0;
    if (!bsg_view_is_independent(v))
	return 0;
    if (parent != (struct bsg_node *)v->gv_draw_root)
	return 0;
    if (child->s_type_flags & BSG_NODE_VIEW_SCOPE)
	return 0;
    if (!BU_VLS_IS_INITIALIZED(&child->s_name))
	return 1;
    return BU_STR_EQUAL("_overlays", bu_vls_cstr(&child->s_name)) ? 0 : 1;
}

int
bsg_view_is_independent(const struct bsg_view *v)
{
    if (!v)
	return 0;

    if (v->gv_draw_root) {
	struct bsg_node *root = (struct bsg_node *)v->gv_draw_root;
	return (_bv_independent_scope_find(root, v) != NULL) ? 1 : 0;
    }

    /* Phase D: no draw root → view is not yet registered, treat as shared. */
    return 0;
}

struct bsg_node *
bsg_view_independent_scope(struct bsg_view *v, int create)
{
    if (!v || !v->gv_draw_root)
	return NULL;

    struct bsg_node *root = (struct bsg_node *)v->gv_draw_root;
    struct bsg_node *scope = _bv_independent_scope_find(root, v);
    if (scope || !create)
	return scope;

    scope = bsg_obj_get_unregistered(v, BV_CHILD_OBJS | BV_LOCAL_OBJS);
    if (!scope)
	return NULL;

    scope->s_type_flags = BSG_NODE_VIEW_SCOPE | BV_LOCAL_OBJS;
    scope->s_flag = UP;
    scope->s_v = v;
    scope->parent = root;
    bu_vls_sprintf(&scope->s_name, "%s", BV_INDEPENDENT_SCOPE_NAME);
    bu_ptbl_ins(&root->children, (long *)scope);

    return scope;
}

void
bsg_view_independent_scope_destroy(struct bsg_view *v)
{
    if (!v || !v->gv_draw_root)
	return;

    struct bsg_node *root = (struct bsg_node *)v->gv_draw_root;
    struct bsg_node *scope = _bv_independent_scope_find(root, v);
    if (!scope)
	return;

    bu_ptbl_rm(&root->children, (long *)scope);
    _bv_scope_free_recursive(scope);
}

void
bsg_data_tclcad_init(struct bsg_data_tclcad *d)
{
    d->gv_polygon_mode = 0;
    d->gv_hide = 0;

    d->gv_data_arrows.gdas_draw = 0;
    d->gv_data_arrows.gdas_color[0] = 0;
    d->gv_data_arrows.gdas_color[1] = 0;
    d->gv_data_arrows.gdas_color[2] = 0;
    d->gv_data_arrows.gdas_line_width = 0;
    d->gv_data_arrows.gdas_tip_length = 0;
    d->gv_data_arrows.gdas_tip_width = 0;
    d->gv_data_arrows.gdas_num_points = 0;
    d->gv_data_arrows.gdas_points = NULL;

    d->gv_data_axes.draw = 0;
    d->gv_data_axes.color[0] = 0;
    d->gv_data_axes.color[1] = 0;
    d->gv_data_axes.color[2] = 0;
    d->gv_data_axes.line_width = 0;
    d->gv_data_axes.size = 0;
    d->gv_data_axes.num_points = 0;
    d->gv_data_axes.points = NULL;

    d->gv_data_labels.gdls_draw = 0;
    d->gv_data_labels.gdls_color[0] = 0;
    d->gv_data_labels.gdls_color[1] = 0;
    d->gv_data_labels.gdls_color[2] = 0;
    d->gv_data_labels.gdls_num_labels = 0;
    d->gv_data_labels.gdls_size = 0;
    d->gv_data_labels.gdls_labels = NULL;
    d->gv_data_labels.gdls_points = NULL;

    d->gv_data_lines.gdls_draw = 0;
    d->gv_data_lines.gdls_color[0] = 0;
    d->gv_data_lines.gdls_color[1] = 0;
    d->gv_data_lines.gdls_color[2] = 0;
    d->gv_data_lines.gdls_line_width = 0;
    d->gv_data_lines.gdls_num_points = 0;
    d->gv_data_lines.gdls_points = NULL;

    d->gv_data_polygons.gdps_draw = 0;
    d->gv_data_polygons.gdps_moveAll = 0;
    d->gv_data_polygons.gdps_color[0] = 0;
    d->gv_data_polygons.gdps_color[1] = 0;
    d->gv_data_polygons.gdps_color[2] = 0;
    d->gv_data_polygons.gdps_line_width = 0;
    d->gv_data_polygons.gdps_line_style = 0;
    d->gv_data_polygons.gdps_cflag = 0;
    d->gv_data_polygons.gdps_target_polygon_i = 0;
    d->gv_data_polygons.gdps_curr_polygon_i = 0;
    d->gv_data_polygons.gdps_curr_point_i = 0;
    d->gv_data_polygons.gdps_prev_point[0] = 0;
    d->gv_data_polygons.gdps_prev_point[1] = 0;
    d->gv_data_polygons.gdps_prev_point[2] = 0;
    d->gv_data_polygons.gdps_clip_type = bg_Union;
    d->gv_data_polygons.gdps_scale = 0;
    d->gv_data_polygons.gdps_origin[0] = 0;
    d->gv_data_polygons.gdps_origin[1] = 0;
    d->gv_data_polygons.gdps_origin[2] = 0;
    MAT_ZERO(d->gv_data_polygons.gdps_rotation);
    MAT_ZERO(d->gv_data_polygons.gdps_view2model);
    MAT_ZERO(d->gv_data_polygons.gdps_model2view);
    d->gv_data_polygons.gdps_polygons.num_polygons = 0;
    d->gv_data_polygons.gdps_polygons.polygon = NULL;
    d->gv_data_polygons.gdps_data_vZ = 0;

    d->gv_sdata_arrows.gdas_draw = 0;
    d->gv_sdata_arrows.gdas_color[0] = 0;
    d->gv_sdata_arrows.gdas_color[1] = 0;
    d->gv_sdata_arrows.gdas_color[2] = 0;
    d->gv_sdata_arrows.gdas_line_width = 0;
    d->gv_sdata_arrows.gdas_tip_length = 0;
    d->gv_sdata_arrows.gdas_tip_width = 0;
    d->gv_sdata_arrows.gdas_num_points = 0;
    d->gv_sdata_arrows.gdas_points = NULL;

    d->gv_sdata_axes.draw = 0;
    d->gv_sdata_axes.color[0] = 0;
    d->gv_sdata_axes.color[1] = 0;
    d->gv_sdata_axes.color[2] = 0;
    d->gv_sdata_axes.line_width = 0;
    d->gv_sdata_axes.size = 0;
    d->gv_sdata_axes.num_points = 0;
    d->gv_sdata_axes.points = NULL;

    d->gv_sdata_labels.gdls_draw = 0;
    d->gv_sdata_labels.gdls_color[0] = 0;
    d->gv_sdata_labels.gdls_color[1] = 0;
    d->gv_sdata_labels.gdls_color[2] = 0;
    d->gv_sdata_labels.gdls_num_labels = 0;
    d->gv_sdata_labels.gdls_size = 0;
    d->gv_sdata_labels.gdls_labels = NULL;
    d->gv_sdata_labels.gdls_points = NULL;

    d->gv_sdata_lines.gdls_draw = 0;
    d->gv_sdata_lines.gdls_color[0] = 0;
    d->gv_sdata_lines.gdls_color[1] = 0;
    d->gv_sdata_lines.gdls_color[2] = 0;
    d->gv_sdata_lines.gdls_line_width = 0;
    d->gv_sdata_lines.gdls_num_points = 0;
    d->gv_sdata_lines.gdls_points = NULL;

    d->gv_sdata_polygons.gdps_draw = 0;
    d->gv_sdata_polygons.gdps_moveAll = 0;
    d->gv_sdata_polygons.gdps_color[0] = 0;
    d->gv_sdata_polygons.gdps_color[1] = 0;
    d->gv_sdata_polygons.gdps_color[2] = 0;
    d->gv_sdata_polygons.gdps_line_width = 0;
    d->gv_sdata_polygons.gdps_line_style = 0;
    d->gv_sdata_polygons.gdps_cflag = 0;
    d->gv_sdata_polygons.gdps_target_polygon_i = 0;
    d->gv_sdata_polygons.gdps_curr_polygon_i = 0;
    d->gv_sdata_polygons.gdps_curr_point_i = 0;
    d->gv_sdata_polygons.gdps_prev_point[0] = 0;
    d->gv_sdata_polygons.gdps_prev_point[1] = 0;
    d->gv_sdata_polygons.gdps_prev_point[2] = 0;
    d->gv_sdata_polygons.gdps_clip_type = bg_Union;
    d->gv_sdata_polygons.gdps_scale = 0;
    d->gv_sdata_polygons.gdps_origin[0] = 0;
    d->gv_sdata_polygons.gdps_origin[1] = 0;
    d->gv_sdata_polygons.gdps_origin[2] = 0;
    MAT_ZERO(d->gv_sdata_polygons.gdps_rotation);
    MAT_ZERO(d->gv_sdata_polygons.gdps_view2model);
    MAT_ZERO(d->gv_sdata_polygons.gdps_model2view);
    d->gv_sdata_polygons.gdps_polygons.num_polygons = 0;
    d->gv_sdata_polygons.gdps_polygons.polygon = NULL;
    d->gv_sdata_polygons.gdps_data_vZ = 0;

    d->gv_prim_labels.gos_draw = 0;
    d->gv_prim_labels.gos_font_size = DM_DEFAULT_FONT_SIZE;
    d->gv_prim_labels.gos_line_color[0] = 0;
    d->gv_prim_labels.gos_line_color[1] = 0;
    d->gv_prim_labels.gos_line_color[2] = 0;
    d->gv_prim_labels.gos_text_color[0] = 0;
    d->gv_prim_labels.gos_text_color[1] = 0;
    d->gv_prim_labels.gos_text_color[2] = 0;
}

void
bsg_init(struct bsg_view *gvp, struct bsg_view_set *s)
{
    if (!gvp)
	return;

    gvp->magic = BV_MAGIC;
    gvp->vset = s;

    if (!BU_VLS_IS_INITIALIZED(&gvp->gv_name)) {
	bu_vls_init(&gvp->gv_name);
    }

    // TODO - Archer doesn't seem to like it when we set initial
    // view names
#if 0
    // If we have a non-null set, go ahead and generate a unique
    // view name to start out with.  App may override, but make
    // sure we at least start out with a unique name
    bu_vls_sprintf(&gvp->gv_name, "V0");
    bool name_collide = false;
    int view_try_cnt = 0;
    struct bu_ptbl *views = bsg_set_views(s);
    for (size_t i = 0; i < BU_PTBL_LEN(views); i++) {
	struct bsg_view *nv = (struct bsg_view *)BU_PTBL_GET(views, i);
	if (!bu_vls_strcmp(&nv->gv_name, &gvp->gv_name)) {
	    name_collide = true;
	    break;
	}
    }
    while (name_collide && view_try_cnt < VIEW_NAME_MAXTRIES) {
	bu_vls_incr(&gvp->gv_name, NULL, "0:0:0:0", NULL, NULL);
	name_collide = false;
	for (size_t i = 0; i < BU_PTBL_LEN(views); i++) {
	    struct bsg_view *nv = (struct bsg_view *)BU_PTBL_GET(views, i);
	    if (!bu_vls_strcmp(&nv->gv_name, &gvp->gv_name)) {
		name_collide = true;
		break;
	    }
	    view_try_cnt++;
	}
    }
    if (view_try_cnt >= VIEW_NAME_MAXTRIES) {
	bu_log("Warning - unable to generate view name unique to view set\n");
    }
#endif

    /* Phase D: independent state is now tracked solely via the BSG
     * independent scope node; no per-view flag is needed. */
    gvp->gv_scale = 500.0;
    gvp->gv_i_scale = gvp->gv_scale;
    gvp->gv_a_scale = 1.0 - gvp->gv_scale / gvp->gv_i_scale;
    gvp->gv_size = 2.0 * gvp->gv_scale;
    gvp->gv_isize = 1.0 / gvp->gv_size;
    VSET(gvp->gv_aet, 35.0, 25.0, 0.0);
    VSET(gvp->gv_eye_pos, 0.0, 0.0, 1.0);
    MAT_IDN(gvp->gv_rotation);
    MAT_IDN(gvp->gv_center);
    MAT_IDN(gvp->gv_view2model);
    MAT_IDN(gvp->gv_model2view);
    VSETALL(gvp->gv_keypoint, 0.0);
    gvp->gv_coord = 'v';
    gvp->gv_rotate_about = 'v';
    gvp->gv_minMouseDelta = -20;
    gvp->gv_maxMouseDelta = 20;
    gvp->gv_rscale = 0.4;
    gvp->gv_sscale = 2.0;
    gvp->gv_perspective = 0.0;

    gvp->gv_prevMouseX = 0;
    gvp->gv_prevMouseY = 0;
    gvp->gv_mouse_x = 0;
    gvp->gv_mouse_y = 0;
    VSETALL(gvp->gv_prev_point, 0.0);
    VSETALL(gvp->gv_point, 0.0);

    /* Initialize local settings */
    bsg_settings_init(&gvp->gv_ls);

    // TODO - unimplemented
    gvp->gv_ls.gv_selected = NULL;

    /* Out of the gate we don't have any shared settings */
    gvp->gv_s = &gvp->gv_ls;

    /* FIXME: this causes the shaders.sh regression to fail */
    /* bsg_mat_aet(gvp); */


    // gv_objs.db_objs is local to this view and thus is controlled
    // by the bv init and free routines.
    BU_GET(gvp->gv_objs.db_objs, struct bu_ptbl);
    bu_ptbl_init(gvp->gv_objs.db_objs, 8, "view_objs init");

    // Until the app tells us differently, we need to use our local
    // containers
    BU_GET(gvp->gv_objs.free_scene_obj, struct bsg_node);
    BU_LIST_INIT(&gvp->gv_objs.free_scene_obj->l);
    BU_LIST_INIT(&gvp->gv_objs.gv_vlfree);

    // Out of the gate we don't have callbacks
    gvp->callbacks = NULL;
    gvp->gv_callback = NULL;
    gvp->gv_bounds_update= NULL;

    // Also don't have a display manager
    // TODO - What the heck Archer??? Initializing this to NULL causes
    // problems even without the gv_name setting logic above?
    //gvp->dmp = NULL;

    // Initial scaling factors are 1
    gvp->gv_base2local = 1.0;
    gvp->gv_local2base = 1.0;

    // Initialize knob vars
    bsg_knobs_reset(&gvp->k, BV_KNOBS_ALL);
    gvp->k.origin_m = '\0';
    gvp->k.origin_o = '\0';
    gvp->k.origin_v = '\0';
    gvp->k.rot_m_udata = NULL;
    gvp->k.rot_o_udata = NULL;
    gvp->k.rot_v_udata = NULL;
    gvp->k.sca_udata = NULL;
    gvp->k.tra_m_udata = NULL;
    gvp->k.tra_v_udata = NULL;

    // Initialize trackball pos
    MAT_DELTAS_GET_NEG(gvp->orig_pos, gvp->gv_center);

    // Phase T3 (drawing_stack_modernization): gv_tcl is no longer embedded;
    // ownership has moved to libtclcad's tclcad_view_data.  For non-Tcl views
    // the pointer stays NULL.  libtclcad sets gv_tcl = &tvd->tcl_data after
    // calling bsg_data_tclcad_init().
    gvp->gv_tcl = NULL;

    // No BSG scene root until bsg_scene_root_create() is called
    gvp->bsg_root = NULL;

    // No edit-mode matrix override until explicitly set by the renderer
    gvp->gv_edit_mat = NULL;

    bsg_update(gvp);
}

void
bsg_free(struct bsg_view *gvp)
{
    if (!gvp)
	return;

    bu_vls_free(&gvp->gv_name);
    bu_ptbl_free(gvp->gv_objs.db_objs);
    BU_PUT(gvp->gv_objs.db_objs, struct bu_ptbl);

    // TODO - clean up local vlfree list contents
    struct bsg_node *sp, *nsp;
    sp = BU_LIST_NEXT(bsg_node, &gvp->gv_objs.free_scene_obj->l);
    while (BU_LIST_NOT_HEAD(sp, &gvp->gv_objs.free_scene_obj->l)) {
	nsp = BU_LIST_PNEXT(bsg_node, sp);
	BU_LIST_DEQUEUE(&((sp)->l));
	if (sp->s_free_callback)
	    (*sp->s_free_callback)(sp);
	/* Phase 11: release backend state via the generic contract. */
	bsg_scene_obj_release_backend(sp);
	bu_ptbl_free(&sp->children);
	BU_PUT(sp, struct bsg_node);
	sp = nsp;
    }
    BU_PUT(gvp->gv_objs.free_scene_obj, struct bsg_node);
    if (gvp->gv_s)
	bu_ptbl_free(&gvp->gv_s->gv_snap_objs);
    if (gvp->gv_s != &gvp->gv_ls)
	bu_ptbl_free(&gvp->gv_ls.gv_snap_objs);

    if (gvp->gv_ls.gv_selected) {
	bu_ptbl_free(gvp->gv_ls.gv_selected);
	BU_PUT(gvp->gv_ls.gv_selected, struct bu_ptbl);
	gvp->gv_ls.gv_selected = NULL;
    }

    if (gvp->callbacks) {
	bu_ptbl_free(gvp->callbacks);
	BU_PUT(gvp->callbacks, struct bu_ptbl);
    }
}

static void
_bound_objs(int *is_empty, int *have_geom_objs, vect_t min, vect_t max, struct bu_ptbl *so, struct bsg_view *v)
{
    vect_t minus, plus;
    for (size_t i = 0; i < BU_PTBL_LEN(so); i++) {
	struct bsg_scene_group *g = (struct bsg_scene_group *)BU_PTBL_GET(so, i);
	_bound_objs(is_empty, have_geom_objs, min, max, &g->children, v);
	if (g->have_bbox || bsg_scene_obj_bound(g, v)) {
	    (*is_empty) = 0;
	    (*have_geom_objs) = 1;
	    minus[X] = g->s_center[X] - g->s_size;
	    minus[Y] = g->s_center[Y] - g->s_size;
	    minus[Z] = g->s_center[Z] - g->s_size;
	    VMIN(min, minus);
	    plus[X] = g->s_center[X] + g->s_size;
	    plus[Y] = g->s_center[Y] + g->s_size;
	    plus[Z] = g->s_center[Z] + g->s_size;
	    VMAX(max, plus);
	}
    }
}

static void
_find_view_geom(int *have_geom_objs, struct bu_ptbl *so)
{
    if (*have_geom_objs)
	return;

    for (size_t i = 0; i < BU_PTBL_LEN(so); i++) {
	struct bsg_node *s = (struct bsg_node *)BU_PTBL_GET(so, i);
	_find_view_geom(have_geom_objs, &s->children);
	if ((s->s_type_flags & BV_DBOBJ_BASED) ||
		(s->s_type_flags & BV_POLYGONS) ||
		(s->s_type_flags & BV_LABELS)) {
	    (*have_geom_objs) = 1;
	    break;
	}
    }
}

static void
_bound_objs_view(int *is_empty, vect_t min, vect_t max, struct bu_ptbl *so, struct bsg_view *v, int have_geom_objs, int all_view_objs)
{
    vect_t minus, plus;
    for (size_t i = 0; i < BU_PTBL_LEN(so); i++) {
	struct bsg_node *s = (struct bsg_node *)BU_PTBL_GET(so, i);
	_bound_objs_view(is_empty, min, max, &s->children, v, have_geom_objs, all_view_objs);
	if (have_geom_objs && !all_view_objs) {
	    if (!(s->s_type_flags & BV_DBOBJ_BASED) &&
		!(s->s_type_flags & BV_POLYGONS) &&
		!(s->s_type_flags & BV_LABELS))
		continue;
	}
	if (bsg_scene_obj_bound(s, v)) {
	    (*is_empty) = 0;
	    minus[X] = s->s_center[X] - s->s_size;
	    minus[Y] = s->s_center[Y] - s->s_size;
	    minus[Z] = s->s_center[Z] - s->s_size;
	    VMIN(min, minus);
	    plus[X] = s->s_center[X] + s->s_size;
	    plus[Y] = s->s_center[Y] + s->s_size;
	    plus[Z] = s->s_center[Z] + s->s_size;
	    VMAX(max, plus);
	}
    }
}


/* Phase B: context for bsg_autoview's bsg_view_objs_visit_db callback. */
struct _bv_autoview_db_ctx {
    int *is_empty;
    int *have_geom_objs;
    vect_t min;
    vect_t max;
    struct bsg_view *v;
};

static int
_bv_autoview_db_cb(struct bsg_node *s, void *data)
{
    struct _bv_autoview_db_ctx *ctx = (struct _bv_autoview_db_ctx *)data;
    vect_t minus, plus;
    /* For non-BSG top-level groups, recurse into their children first */
    _bound_objs(ctx->is_empty, ctx->have_geom_objs, ctx->min, ctx->max,
		&s->children, ctx->v);
    /* Check this object's own bounds */
    if (s->have_bbox || bsg_scene_obj_bound(s, ctx->v)) {
	(*ctx->is_empty) = 0;
	(*ctx->have_geom_objs) = 1;
	minus[X] = s->s_center[X] - s->s_size;
	minus[Y] = s->s_center[Y] - s->s_size;
	minus[Z] = s->s_center[Z] - s->s_size;
	VMIN(ctx->min, minus);
	plus[X] = s->s_center[X] + s->s_size;
	plus[Y] = s->s_center[Y] + s->s_size;
	plus[Z] = s->s_center[Z] + s->s_size;
	VMAX(ctx->max, plus);
    }
    return 1;
}

/* Phase D (drawing_stack_modernization): context for bsg_autoview's
 * bsg_view_obj_visit pass, replacing the bsg_view_objs(BV_VIEW_OBJS) calls.
 * All output fields are pointers so the callback updates the caller's storage
 * in-place (consistent with is_empty and v, both of which are also pointers). */
struct _bv_autoview_view_ctx {
    int *is_empty;
    fastf_t *min;   /* vect_t — fastf_t[3] */
    fastf_t *max;   /* vect_t — fastf_t[3] */
    struct bsg_view *v;
    int have_geom_objs;
    int all_view_objs;
};

/* Pass 1 callback: set have_geom_objs if any view object has a geometric type. */
static int
_bv_find_view_geom_visit_cb(struct bsg_node *s, void *data)
{
    int *have_geom_objs = (int *)data;
    _find_view_geom(have_geom_objs, &s->children);
    if (!(*have_geom_objs)) {
	if ((s->s_type_flags & BV_DBOBJ_BASED) ||
	    (s->s_type_flags & BV_POLYGONS) ||
	    (s->s_type_flags & BV_LABELS))
	    (*have_geom_objs) = 1;
    }
    return 1;
}

/* Pass 2 callback: bound each view object and its children. */
static int
_bv_bound_view_obj_cb(struct bsg_node *s, void *data)
{
    struct _bv_autoview_view_ctx *ctx = (struct _bv_autoview_view_ctx *)data;
    vect_t minus, plus;
    _bound_objs_view(ctx->is_empty, ctx->min, ctx->max, &s->children,
		     ctx->v, ctx->have_geom_objs, ctx->all_view_objs);
    if (ctx->have_geom_objs && !ctx->all_view_objs) {
	if (!(s->s_type_flags & BV_DBOBJ_BASED) &&
	    !(s->s_type_flags & BV_POLYGONS) &&
	    !(s->s_type_flags & BV_LABELS))
	    return 1;
    }
    if (bsg_scene_obj_bound(s, ctx->v)) {
	(*ctx->is_empty) = 0;
	minus[X] = s->s_center[X] - s->s_size;
	minus[Y] = s->s_center[Y] - s->s_size;
	minus[Z] = s->s_center[Z] - s->s_size;
	VMIN(ctx->min, minus);
	plus[X] = s->s_center[X] + s->s_size;
	plus[Y] = s->s_center[Y] + s->s_size;
	plus[Z] = s->s_center[Z] + s->s_size;
	VMAX(ctx->max, plus);
    }
    return 1;
}

void
bsg_autoview(struct bsg_view *v, double factor, int all_view_objs)
{
    vect_t min, max;
    vect_t center = VINIT_ZERO;
    vect_t radial;
    vect_t sqrt_small;
    int is_empty = 1;
    int have_geom_objs = 0;

    /* set the default if unset or insane */
    if (factor < SQRT_SMALL_FASTF) {
	factor = 2.0; /* 2 is half the view */
    }

    VSETALL(sqrt_small, SQRT_SMALL_FASTF);

    /* calculate the bounding for all solids and polygons being displayed */
    VSETALL(min,  INFINITY);
    VSETALL(max, -INFINITY);

    /* Phase B: use bsg_view_objs_visit_db so that GED consumers with the BSG
     * draw tree are handled correctly even after BV_DB_OBJS ptbls are empty. */
    struct _bv_autoview_db_ctx bav_ctx;
    bav_ctx.is_empty = &is_empty;
    bav_ctx.have_geom_objs = &have_geom_objs;
    VSETALL(bav_ctx.min,  INFINITY);
    VSETALL(bav_ctx.max, -INFINITY);
    bav_ctx.v = v;
    bsg_view_objs_visit_db(v, _bv_autoview_db_cb, &bav_ctx);
    if (!is_empty) {
	VMOVE(min, bav_ctx.min);
	VMOVE(max, bav_ctx.max);
    }

    // When it comes to view-only objects, normally we will only include those
    // that are db object based, polygons or labels, unless the flag to
    // consider all objects is set.   However, there is an exception - if there
    // are NO such objects in the scene (have_geom_objs == 0) and we do have
    // view objs (for example, when overlaying a plot file on an empty view)
    // then basing autoview on the view-only objs is more intuitive than just
    // using the default view settings.

    /* Phase D: use bsg_view_obj_visit instead of bsg_view_objs(BV_VIEW_OBJS).
     * Two passes: collect have_geom_objs across all view objects first, then
     * bound them so the geom-filter logic has complete information. */
    bsg_view_obj_visit(v, BV_VIEW_OBJ_SCOPE_ALL, _bv_find_view_geom_visit_cb, &have_geom_objs);
    {
	struct _bv_autoview_view_ctx vctx;
	vctx.is_empty = &is_empty;
	vctx.min = min;
	vctx.max = max;
	vctx.v = v;
	vctx.have_geom_objs = have_geom_objs;
	vctx.all_view_objs = all_view_objs;
	bsg_view_obj_visit(v, BV_VIEW_OBJ_SCOPE_ALL, _bv_bound_view_obj_cb, &vctx);
    }

    if (is_empty) {
	/* Nothing is in view */
	VSETALL(radial, 1000.0);
    } else {
	VADD2SCALE(center, max, min, 0.5);
	VSUB2(radial, max, center);
    }

    /* make sure it's not inverted */
    VMAX(radial, sqrt_small);

    /* make sure it's not too small */
    if (VNEAR_ZERO(radial, SQRT_SMALL_FASTF))
	VSETALL(radial, 1.0);

    MAT_IDN(v->gv_center);
    MAT_DELTAS_VEC_NEG(v->gv_center, center);
    v->gv_scale = radial[X];
    V_MAX(v->gv_scale, radial[Y]);
    V_MAX(v->gv_scale, radial[Z]);

    v->gv_size = factor * v->gv_scale;
    v->gv_isize = 1.0 / v->gv_size;
    bsg_update(v);
}

/**
 * FIXME: this routine is suspect and needs investigating.  if run
 * during view initialization, the shaders regression test fails.
 */
void
bsg_mat_aet(struct bsg_view *v)
{
    mat_t tmat;
    fastf_t twist;
    fastf_t c_twist;
    fastf_t s_twist;

    bn_mat_angles(v->gv_rotation,
		  270.0 + v->gv_aet[1],
		  0.0,
		  270.0 - v->gv_aet[0]);

    twist = -v->gv_aet[2] * DEG2RAD;
    c_twist = cos(twist);
    s_twist = sin(twist);
    bn_mat_zrot(tmat, s_twist, c_twist);
    bn_mat_mul2(tmat, v->gv_rotation);
}

/* --- Camera accessor implementations --- */

fastf_t
bsg_view_get_scale(const struct bsg_view *v)
{
    if (!v) return 0.0;
    return v->gv_scale;
}

void
bsg_view_set_scale(struct bsg_view *v, fastf_t scale)
{
    if (!v) return;
    v->gv_scale = scale;
    v->gv_size  = scale * 2.0;
    v->gv_isize = (v->gv_size > 0.0) ? 1.0 / v->gv_size : 0.0;
}

fastf_t
bsg_view_get_size(const struct bsg_view *v)
{
    if (!v) return 0.0;
    return v->gv_size;
}

void
bsg_view_set_size(struct bsg_view *v, fastf_t size)
{
    if (!v) return;
    v->gv_size  = size;
    v->gv_scale = size * 0.5;
    v->gv_isize = (size > 0.0) ? 1.0 / size : 0.0;
}

fastf_t
bsg_view_get_perspective(const struct bsg_view *v)
{
    if (!v) return 0.0;
    return v->gv_perspective;
}

void
bsg_view_set_perspective(struct bsg_view *v, fastf_t perspective)
{
    if (!v) return;
    v->gv_perspective = perspective;
}

void
bsg_view_get_aet(const struct bsg_view *v, vect_t aet)
{
    if (!v) { VSETALL(aet, 0.0); return; }
    VMOVE(aet, v->gv_aet);
}

void
bsg_view_set_aet(struct bsg_view *v, const vect_t aet)
{
    if (!v) return;
    VMOVE(v->gv_aet, aet);
    bsg_mat_aet(v);
}

void
bsg_view_get_rotation(const struct bsg_view *v, mat_t rot)
{
    if (!v) { MAT_IDN(rot); return; }
    MAT_COPY(rot, v->gv_rotation);
}

void
bsg_view_set_rotation(struct bsg_view *v, const mat_t rot)
{
    if (!v) return;
    MAT_COPY(v->gv_rotation, rot);
}

void
bsg_view_get_center_vec(const struct bsg_view *v, point_t center)
{
    if (!v) { VSETALL(center, 0.0); return; }
    MAT_DELTAS_GET_NEG(center, v->gv_center);
}

void
bsg_view_set_center_vec(struct bsg_view *v, const point_t center)
{
    if (!v) return;
    MAT_DELTAS_VEC_NEG(v->gv_center, center);
}

/* --- end camera accessors --- */

void
bsg_settings_init(struct bsg_view_settings *s)
{
    s->gv_cleared = 1;

    s->gv_adc.draw = 0;
    s->gv_adc.a1 = 45.0;
    s->gv_adc.a2 = 45.0;
    VSET(s->gv_adc.line_color, 255, 255, 0);
    VSET(s->gv_adc.tick_color, 255, 255, 255);

    s->gv_grid.draw = 0;
    s->gv_grid.adaptive = 0;
    s->gv_grid.snap = 0;
    VSET(s->gv_grid.anchor, 0.0, 0.0, 0.0);
    s->gv_grid.res_h = 1.0;
    s->gv_grid.res_v = 1.0;
    s->gv_grid.res_major_h = 5;
    s->gv_grid.res_major_v = 5;
    VSET(s->gv_grid.color, 255, 255, 255);

    s->gv_rect.draw = 0;
    s->gv_rect.pos[0] = 128;
    s->gv_rect.pos[1] = 128;
    s->gv_rect.dim[0] = 256;
    s->gv_rect.dim[1] = 256;
    VSET(s->gv_rect.color, 255, 255, 255);

    s->gv_view_axes.draw = 0;
    VSET(s->gv_view_axes.axes_pos, 0.80, -0.80, 0.0);
    s->gv_view_axes.axes_size = 0.2;
    s->gv_view_axes.line_width = 0;
    s->gv_view_axes.pos_only = 1;
    VSET(s->gv_view_axes.axes_color, 255, 255, 255);
    s->gv_view_axes.label_flag = 1;
    VSET(s->gv_view_axes.label_color, 255, 255, 0);
    s->gv_view_axes.triple_color = 1;

    s->gv_model_axes.draw = 0;
    VSET(s->gv_model_axes.axes_pos, 0.0, 0.0, 0.0);
    s->gv_model_axes.axes_size = 2.0;
    s->gv_model_axes.line_width = 0;
    s->gv_model_axes.pos_only = 0;
    VSET(s->gv_model_axes.axes_color, 255, 255, 255);
    s->gv_model_axes.label_flag = 1;
    VSET(s->gv_model_axes.label_color, 255, 255, 0);
    s->gv_model_axes.triple_color = 0;
    s->gv_model_axes.tick_enabled = 1;
    s->gv_model_axes.tick_length = 4;
    s->gv_model_axes.tick_major_length = 8;
    s->gv_model_axes.tick_interval = 100;
    s->gv_model_axes.ticks_per_major = 10;
    s->gv_model_axes.tick_threshold = 8;
    VSET(s->gv_model_axes.tick_color, 255, 255, 0);
    VSET(s->gv_model_axes.tick_major_color, 255, 0, 0);

    s->gv_center_dot.gos_draw = 0;
    s->gv_center_dot.gos_font_size = DM_DEFAULT_FONT_SIZE;
    VSET(s->gv_center_dot.gos_line_color, 255, 255, 0);

    s->gv_view_params.draw = 0;
    s->gv_view_params.draw_size = 1;
    s->gv_view_params.draw_center = 1;
    s->gv_view_params.draw_az = 1;
    s->gv_view_params.draw_el = 1;
    s->gv_view_params.draw_tw = 1;
    s->gv_view_params.draw_fps = 0;
    VSET(s->gv_view_params.color, 255, 255, 0);
    s->gv_view_params.font_size = DM_DEFAULT_FONT_SIZE;

    s->gv_view_scale.gos_draw = 0;
    s->gv_view_scale.gos_font_size = DM_DEFAULT_FONT_SIZE;
    VSET(s->gv_view_scale.gos_line_color, 255, 255, 0);
    VSET(s->gv_view_scale.gos_text_color, 255, 255, 255);

    s->gv_frametime = 1;
    s->gv_fb_mode = 0;

    s->gv_autoview = 1;

    s->adaptive_plot_mesh = 0;
    s->adaptive_plot_csg = 0;
    s->redraw_on_zoom = 0;
    s->point_scale = 1;
    s->curve_scale = 1;
    s->bot_threshold = 0;
    s->lod_scale = 1.0;

    // Higher values indicate more aggressive behavior (i.e. points further away will be snapped).
    s->gv_snap_tol_factor = 10;
    s->gv_snap_lines = 0;
    BU_PTBL_INIT(&s->gv_snap_objs);
    s->gv_snap_flags = 0;
}

// TODO - investigate saveview/loadview logic, see if anything
// makes sense to move here
void
bsg_sync(struct bsg_view *dest, struct bsg_view *src)
{
    if (!src || !dest)
	return;

    /* Size info */
    dest->gv_i_scale = src->gv_i_scale;
    dest->gv_a_scale = src->gv_a_scale;
    dest->gv_scale = src->gv_scale;
    dest->gv_size = src->gv_size;
    dest->gv_isize = src->gv_isize;
    dest->gv_width = src->gv_width;
    dest->gv_height = src->gv_height;
    dest->gv_base2local = src->gv_base2local;
    dest->gv_rscale = src->gv_rscale;
    dest->gv_sscale = src->gv_sscale;

    /* Camera info */
    dest->gv_perspective = src->gv_perspective;
    VMOVE(dest->gv_aet, src->gv_aet);
    VMOVE(dest->gv_eye_pos, src->gv_eye_pos);
    VMOVE(dest->gv_keypoint, src->gv_keypoint);
    dest->gv_coord = src->gv_coord;
    dest->gv_rotate_about = src->gv_rotate_about;
    MAT_COPY(dest->gv_rotation, src->gv_rotation);
    MAT_COPY(dest->gv_center, src->gv_center);
    MAT_COPY(dest->gv_model2view, src->gv_model2view);
    MAT_COPY(dest->gv_pmodel2view, src->gv_pmodel2view);
    MAT_COPY(dest->gv_view2model, src->gv_view2model);
    MAT_COPY(dest->gv_pmat, src->gv_pmat);
}

void
bsg_update(struct bsg_view *gvp)
{
    vect_t work, work1;
    vect_t temp, temp1;

    if (!gvp)
	return;

    bn_mat_mul(gvp->gv_model2view,
	       gvp->gv_rotation,
	       gvp->gv_center);
    gvp->gv_model2view[15] = gvp->gv_scale;
    bn_mat_inv(gvp->gv_view2model, gvp->gv_model2view);

    /* Find current azimuth, elevation, and twist angles */
    VSET(work, 0.0, 0.0, 1.0);       /* view z-direction */
    MAT4X3VEC(temp, gvp->gv_view2model, work);
    VSET(work1, 1.0, 0.0, 0.0);      /* view x-direction */
    MAT4X3VEC(temp1, gvp->gv_view2model, work1);

    /* calculate angles using accuracy of 0.005, since display
     * shows 2 digits right of decimal point */
    bn_aet_vec(&gvp->gv_aet[0],
	       &gvp->gv_aet[1],
	       &gvp->gv_aet[2],
	       temp, temp1, (fastf_t)0.005);

    /* Force azimuth range to be [0, 360] */
    if ((NEAR_EQUAL(gvp->gv_aet[1], 90.0, (fastf_t)0.005) ||
	 NEAR_EQUAL(gvp->gv_aet[1], -90.0, (fastf_t)0.005)) &&
	gvp->gv_aet[0] < 0 &&
	!NEAR_ZERO(gvp->gv_aet[0], (fastf_t)0.005))
	gvp->gv_aet[0] += 360.0;
    else if (NEAR_ZERO(gvp->gv_aet[0], (fastf_t)0.005))
	gvp->gv_aet[0] = 0.0;

    /* apply the perspective angle to model2view */
    bn_mat_mul(gvp->gv_pmodel2view, gvp->gv_pmat, gvp->gv_model2view);

    /* Update obb, if the caller has told us how to */
    if (gvp->gv_bounds_update) {
	(*gvp->gv_bounds_update)(gvp);
    }

    if (gvp->gv_callback) {

	if (gvp->callbacks) {
	    if (bu_ptbl_locate(gvp->callbacks, (long *)(uintptr_t)gvp->gv_callback) != -1) {
		bu_log("Recursive callback (bsg_update and gvp->gv_callback)");
	    }
	    bu_ptbl_ins_unique(gvp->callbacks, (long *)(uintptr_t)gvp->gv_callback);
	}

	(*gvp->gv_callback)(gvp, gvp->gv_clientData);

	if (gvp->callbacks) {
	    bu_ptbl_rm(gvp->callbacks, (long *)(uintptr_t)gvp->gv_callback);
	}

    }
}

int
bsg_obj_settings_sync(struct bsg_obj_settings *dest, struct bsg_obj_settings *src)
{
    int ret = 0;
    if (!dest || !src)
	return ret;

    if (dest->s_line_width != src->s_line_width) {
	dest->s_line_width = src->s_line_width;
	ret = 1;
    }
    if (!NEAR_EQUAL(dest->s_arrow_tip_length, src->s_arrow_tip_length, SMALL_FASTF)) {
	dest->s_arrow_tip_length = src->s_arrow_tip_length;
	ret = 1;
    }
    if (!NEAR_EQUAL(dest->s_arrow_tip_width, src->s_arrow_tip_width, SMALL_FASTF)) {
	dest->s_arrow_tip_width = src->s_arrow_tip_width;
	ret = 1;
    }
    if (!NEAR_EQUAL(dest->transparency, src->transparency, SMALL_FASTF)) {
	dest->transparency = src->transparency;
	ret = 1;
    }
    if (dest->s_dmode != src->s_dmode) {
	dest->s_dmode = src->s_dmode;
	ret = 1;
    }
    if (dest->color_override != src->color_override) {
	dest->color_override = src->color_override;
	ret = 1;
    }
    if (!VNEAR_EQUAL(dest->color, src->color, SMALL_FASTF)) {
	VMOVE(dest->color, src->color);
	ret = 1;
    }
    if (dest->draw_solid_lines_only != src->draw_solid_lines_only) {
	dest->draw_solid_lines_only = src->draw_solid_lines_only;
	ret = 1;
    }
    if (dest->draw_non_subtract_only != src->draw_non_subtract_only) {
	dest->draw_non_subtract_only = src->draw_non_subtract_only;
	ret = 1;
    }

    return ret;
}

int
bsg_update_selected(struct bsg_view *gvp)
{
    int ret = 0;
    if (!gvp)
	return 0;
    return (ret > 0) ? 1 : 0;
}

// TODO - support constraints
int
_bv_rot(struct bsg_view *v, int dx, int dy, point_t keypoint, unsigned long long UNUSED(flags))
{
    if (!v)
	return 0;

    point_t rot_pt;
    point_t new_origin;
    mat_t viewchg, viewchginv;
    point_t new_cent_view;
    point_t new_cent_model;

    fastf_t rdx = (fastf_t)dx * 0.25;
    fastf_t rdy = (fastf_t)dy * 0.25;
    mat_t newrot, newinv;
    bn_mat_angles(newrot, rdx, rdy, 0);
    bn_mat_inv(newinv, newrot);
    MAT4X3PNT(rot_pt, v->gv_model2view, keypoint);  /* point to rotate around */

    bn_mat_xform_about_pnt(viewchg, newrot, rot_pt);
    bn_mat_inv(viewchginv, viewchg);
    VSET(new_origin, 0.0, 0.0, 0.0);
    MAT4X3PNT(new_cent_view, viewchginv, new_origin);
    MAT4X3PNT(new_cent_model, v->gv_view2model, new_cent_view);
    MAT_DELTAS_VEC_NEG(v->gv_center, new_cent_model);

    /* Update the rotation component of the model2view matrix */
    bn_mat_mul2(newrot, v->gv_rotation); /* pure rotation */

    /* gv_rotation is updated, now sync other bv values */
    bsg_update(v);

    return 1;
}

int
_bv_trans(struct bsg_view *v, int dx, int dy, point_t UNUSED(keypoint), unsigned long long UNUSED(flags))
{
    if (!v)
	return 0;

    fastf_t aspect = (fastf_t)v->gv_width / (fastf_t)v->gv_height;
    fastf_t fx = (fastf_t)dx / (fastf_t)v->gv_width * 2.0;
    fastf_t fy = -dy / (fastf_t)v->gv_height / aspect * 2.0;

    vect_t tt;
    point_t delta;
    point_t work;
    point_t vc, nvc;

    VSET(tt, fx, fy, 0);
    MAT4X3PNT(work, v->gv_view2model, tt);
    MAT_DELTAS_GET_NEG(vc, v->gv_center);
    VSUB2(delta, work, vc);
    VSUB2(nvc, vc, delta);
    MAT_DELTAS_VEC_NEG(v->gv_center, nvc);
    bsg_update(v);

    return 1;
}

int
_bv_scale(struct bsg_view *v, int sensitivity, int factor, point_t UNUSED(keypoint), unsigned long long UNUSED(flags))
{
    if (!v)
	return 0;

    double f = (double)factor/(double)sensitivity;
    v->gv_scale /= f;
    if (v->gv_scale < BV_MINVIEWSCALE)
	v->gv_scale = BV_MINVIEWSCALE;
    v->gv_size = 2.0 * v->gv_scale;
    v->gv_isize = 1.0 / v->gv_size;

    /* scale factors are set, now sync other bv values */
    bsg_update(v);

    return 1;
}

int
_bv_center(struct bsg_view *v, int vx, int vy, point_t UNUSED(keypoint), unsigned long long UNUSED(flags))
{
    if (!v)
	return 0;

    point_t vpt, center;
    fastf_t fx = 0.0;
    fastf_t fy = 0.0;
    bsg_screen_to_view(v, &fx, &fy, (fastf_t)vx, (fastf_t)vy);
    VSET(vpt, fx, fy, 0);
    MAT4X3PNT(center, v->gv_view2model, vpt);
    MAT_DELTAS_VEC_NEG(v->gv_center, center);
    bsg_update(v);
    return 1;
}

int
bsg_adjust(struct bsg_view *v, int dx, int dy, point_t keypoint, int UNUSED(mode), unsigned long long flags)
{
    if (flags == BV_IDLE)
	return 0;

    // TODO - figure out why these need to be flipped for qdm to do the right thing...
    if (flags & BV_ROT)
	return _bv_rot(v, dy, dx, keypoint, flags);

    if (flags & BV_TRANS)
	return _bv_trans(v, dx, dy, keypoint, flags);

    if (flags & BV_SCALE)
	return _bv_scale(v, dx, dy, keypoint, flags);

    if (flags & BV_CENTER)
	return _bv_center(v, dx, dy, keypoint, flags);


    return 0;
}


int
bsg_screen_to_view(struct bsg_view *v, fastf_t *fx, fastf_t *fy, fastf_t x, fastf_t y)
{
    if (!v)
	return -1;

    if (!v->gv_width || !v->gv_height)
	return -1;

    if (fx) {
	fastf_t tx = x / (fastf_t)v->gv_width * 2.0 - 1.0;
	(*fx) = tx;
    }

    if (fy) {
	fastf_t aspect = (fastf_t)v->gv_width / (fastf_t)v->gv_height;
	fastf_t ty = (y / (fastf_t)v->gv_height * -2.0 + 1.0) / aspect;
	(*fy) = ty;
    }

    // If snapping is enabled, apply it
    int snapped = 0;
    if (v->gv_s) {
	if (v->gv_s->gv_snap_lines) {
	    snapped = bsg_snap_lines_2d(v, fx, fy);
	}
	if (!snapped && v->gv_s->gv_grid.snap) {
	    bsg_snap_grid_2d(v, fx, fy);
	}
    }

    return 0;
}

int
bsg_screen_pt(point_t *p, fastf_t x, fastf_t y, struct bsg_view *v)
{
    if (!p || !v)
	return -1;

    if (!v->gv_width || !v->gv_height)
	return -1;

    fastf_t tx, ty;
    if (bsg_screen_to_view(v, &tx, &ty, x, y))
	return -1;

    point_t vpt;
    VSET(vpt, tx, ty, 0);
    MAT4X3PNT(*p, v->gv_view2model, vpt);
    return 0;
}

int
bsg_view_plane(plane_t *p, struct bsg_view *v)
{
    if (!p || !v)
	return -1;

    point_t cpt = VINIT_ZERO;
    vect_t vnrml = VINIT_ZERO;

    MAT_DELTAS_GET_NEG(cpt, v->gv_center);
    VMOVEN(vnrml, v->gv_rotation + 8, 3);
    VUNITIZE(vnrml);
    VSCALE(vnrml, vnrml, -1.0);

    return bg_plane_pt_nrml(p, cpt, vnrml);
}

/* Phase D: count callback for bsg_view_obj_visit used in bsg_clear. */
static int
_bv_count_view_obj_cb(struct bsg_node *UNUSED(obj), void *data)
{
    size_t *count = (size_t *)data;
    (*count)++;
    return 1;
}

size_t
bsg_clear(struct bsg_view *v, int flags)
{
    if (!flags || flags & BV_DB_OBJS) {
	struct bu_ptbl *sg = bsg_view_objs(v, BV_DB_OBJS | (flags & ~BV_VIEW_OBJS));
	if (sg) {
	    for (size_t i = 0; i < BU_PTBL_LEN(sg); i++) {
		struct bsg_node *cg = (struct bsg_scene_group *)BU_PTBL_GET(sg, i);
		bsg_obj_put(cg);
	    }
	    bu_ptbl_reset(sg);
	}
    }

    /* Phase V4: view-only objects live in BSG VIEW_SCOPE nodes.
     * bsg_view_obj_remove_all handles the tree cleanup directly. */
    if (!flags || flags & BV_VIEW_OBJS) {
	int scope_mask = 0;
	if (!flags) {
	    scope_mask = BV_VIEW_OBJ_SCOPE_ALL;
	} else if (flags & BV_LOCAL_OBJS) {
	    scope_mask = BV_VIEW_OBJ_SCOPE_LOCAL;
	} else {
	    scope_mask = BV_VIEW_OBJ_SCOPE_SHARED;
	}
	bsg_view_obj_remove_all(v, scope_mask);
    }

    if (!flags || flags & BV_LOCAL_OBJS || bsg_view_is_independent(v)) {
	if (!flags || flags & BV_DB_OBJS) {
	    struct bu_ptbl *sg = bsg_view_objs(v, BV_DB_OBJS | (flags & ~BV_VIEW_OBJS) | BV_LOCAL_OBJS);
	    if (sg) {
		for (size_t i = 0; i < BU_PTBL_LEN(sg); i++) {
		    struct bsg_scene_group *cg = (struct bsg_scene_group *)BU_PTBL_GET(sg, i);
		    bsg_obj_put(cg);
		}
		bu_ptbl_reset(sg);
	    }
	}
	/* VIEW_OBJS local clear already handled above via scope_mask */
    }

    struct bu_ptbl *sg = bsg_view_objs(v, BV_DB_OBJS);
    struct bu_ptbl *sgl = bsg_view_objs(v, BV_DB_OBJS | BV_LOCAL_OBJS);

    /* Phase D: count view-only objects via bsg_view_obj_visit. */
    size_t vo_count = 0;
    bsg_view_obj_visit(v, BV_VIEW_OBJ_SCOPE_ALL, _bv_count_view_obj_cb, &vo_count);

    size_t ocnt = 0;
    ocnt += (sg) ? BU_PTBL_LEN(sg) : 0;
    ocnt += (sgl && sgl != sg) ? BU_PTBL_LEN(sgl) : 0;
    ocnt += vo_count;
    return ocnt;
}

void
bsg_scene_obj_release_backend(struct bsg_node *s)
{
    if (UNLIKELY(!s))
	return;

    /* Phase 11 contract: fire the backend-owned free callback (if any) and
     * clear the slot.  Backends are responsible for releasing whatever is
     * stored in s_backend->handle and freeing the descriptor itself in
     * their free() implementation. */
    if (s->s_backend && s->s_backend->free) {
	(*s->s_backend->free)(s);
    }
    s->s_backend = NULL;
}

void
bsg_scene_obj_invalidate_backend(struct bsg_node *s)
{
    if (UNLIKELY(!s))
	return;

    /* Phase 11 contract: fire the backend-owned invalidate callback (if
     * any).  Optional: backends without a separately-cacheable resource
     * can leave invalidate==NULL. */
    if (s->s_backend && s->s_backend->invalidate) {
	(*s->s_backend->invalidate)(s);
    }
}

void
bsg_obj_stale(struct bsg_node *s)
{
    bsg_scene_obj_invalidate_backend(s);

    if (BU_PTBL_IS_INITIALIZED(&s->children)) {
	for (size_t i = 0; i < BU_PTBL_LEN(&s->children); i++) {
	    struct bsg_node *s_c = (struct bsg_node *)BU_PTBL_GET(&s->children, i);
	    bsg_obj_stale(s_c);
	}
    }
}

struct bsg_node *
bsg_obj_create(struct bsg_view *v, int type)
{
    if (!v)
	return NULL;

    bsg_log(1, "bsg_obj_create (%s)", _bv_vname(v));

    struct bsg_node *s = NULL;

    // What we get and from where is based on the requested obj type and the
    // view type.  If the caller is not asking for a local object, we will try
    // to get a shared object.  If the view has no associated set then the only
    // available storage is the local storage, and that will be used instead.
    // If a local object is requested, then the local storage is used
    // regardless of whether or not a shared repository is available.
    struct bsg_node *free_scene_obj = NULL;
    struct bu_list *vlfree = NULL;
    if (type & BV_LOCAL_OBJS || type & BV_CHILD_OBJS || bsg_view_is_independent(v) || !v->vset)  {
	free_scene_obj = v->gv_objs.free_scene_obj;
	vlfree = &v->gv_objs.gv_vlfree;
    } else {
	free_scene_obj = v->vset->i->free_scene_obj;
	vlfree = &v->vset->i->vlfree;
    }
    if (!free_scene_obj)
	return NULL;

    // The table has an additional complication - we don't want child objects
    // to be stored in it, because they are part of the scene only by virtue
    // of their parent object
    struct bu_ptbl *otbl = NULL;
    if (type & BV_LOCAL_OBJS || type & BV_CHILD_OBJS || bsg_view_is_independent(v) || !v->vset)  {
	if (!(type & BV_CHILD_OBJS)) {
	    if (type & BV_DB_OBJS) {
		otbl = v->gv_objs.db_objs;
	    }
	    /* BV_VIEW_OBJS objects are tracked in BSG VIEW_SCOPE nodes; otbl = NULL. */
	}
    } else {
	if (type & BV_DB_OBJS) {
	    otbl = &v->vset->i->shared_db_objs;
	}
	/* BV_VIEW_OBJS objects live in BSG VIEW_SCOPE; otbl = NULL. */
    }
    if (!free_scene_obj)
	return NULL;


    // We know where we're going to get the object from - get it
    if (BU_LIST_IS_EMPTY(&free_scene_obj->l)) {
	BU_ALLOC(s, struct bsg_node);
	s->i = new bsg_node_internal;
    } else {
	s = BU_LIST_NEXT(bsg_node, &free_scene_obj->l);
	BU_LIST_DEQUEUE(&((s)->l));
    }

    // Zero out callback pointers
    s->s_type_flags = 0;
    s->s_free_callback = NULL;
    /* Phase 11: zero the backend slot so any prior owner state is dropped. */
    s->s_backend = NULL;

    // Use reset to do most of the initialization
    bsg_obj_reset(s);

    // Set view
    s->s_v = v;

    // Set the type flag(s) on the object itself
    s->s_type_flags = type;

    // Set this object's containers
    s->free_scene_obj = free_scene_obj;
    s->vlfree = vlfree;
    s->otbl = otbl;

    return s;
}

/* Forward declaration: defined below after bsg_view_scope helpers. */
static struct bsg_node *
_bv_view_obj_create(struct bsg_view *v, const char *name, int local, unsigned long long type_flags);

struct bsg_node *
bsg_obj_get(struct bsg_view *v, int type)
{
    if (!v)
	return NULL;

    bsg_log(1, "bsg_obj_get %d(%s)", type, _bv_vname(v));

   int ltype = type;
   if (bsg_view_is_independent(v))
	ltype |= BV_LOCAL_OBJS;

    struct bsg_node *s = bsg_obj_create(v, ltype);
    if (!s)
	return NULL;

    if (s->otbl)
	bu_ptbl_ins(s->otbl, (long *)s);

    return s;
}

struct bsg_node *
bsg_obj_get_unregistered(struct bsg_view *v, int type)
{
    /* Allocates a scene object with s_type_flags set but does NOT insert it
     * into any gv_objs ptbl.  Used by BViewState for leaves that are owned
     * exclusively by the BSG draw tree (gd_draw_root) rather than the legacy
     * flat ptbl.  The caller is responsible for freeing via bsg_obj_put. */
    if (!v)
	return NULL;

    bsg_log(1, "bsg_obj_get_unregistered %d(%s)", type, _bv_vname(v));

    int ltype = type;
    if (bsg_view_is_independent(v))
	ltype |= BV_LOCAL_OBJS;

    struct bsg_node *s = bsg_obj_create(v, ltype);
    if (!s)
	return NULL;

    /* Intentionally do NOT call bu_ptbl_ins: this object will be owned and
     * indexed by the BSG draw tree rather than a gv_objs ptbl.  Clear otbl
     * so that bsg_obj_put later does not attempt a ptbl removal. */
    s->otbl = NULL;

    return s;
}

static struct bsg_node *
_bv_view_scope_find(struct bsg_node *root, struct bsg_view *owner)
{
    if (!root)
	return NULL;

    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	struct bsg_node *c = (struct bsg_node *)BU_PTBL_GET(&root->children, i);
	if (!c)
	    continue;
	if (!(c->s_type_flags & BSG_NODE_VIEW_SCOPE))
	    continue;
	if (_bv_is_independent_scope(c, owner))
	    continue;
	if (c->s_v != owner)
	    continue;
	return c;
    }

    return NULL;
}

static struct bsg_node *
_bv_view_scope_ensure(struct bsg_view *v, int local)
{
    if (!v || !v->gv_draw_root)
	return NULL;

    struct bsg_node *root = (struct bsg_node *)v->gv_draw_root;
    struct bsg_view *owner = local ? v : NULL;
    struct bsg_node *scope = _bv_view_scope_find(root, owner);
    if (scope)
	return scope;

    scope = bsg_obj_get_unregistered(v, BV_CHILD_OBJS | (local ? BV_LOCAL_OBJS : 0));
    if (!scope)
	return NULL;

    scope->s_type_flags = BSG_NODE_VIEW_SCOPE | (local ? BV_LOCAL_OBJS : 0);
    scope->s_v = owner;
    bu_vls_sprintf(&scope->s_name, local ? "_view_obj_scope_local" : "_view_obj_scope_shared");
    scope->parent = root;
    bu_ptbl_ins(&root->children, (long *)scope);

    return scope;
}

static struct bsg_node *
_bv_view_obj_create(struct bsg_view *v, const char *name, int local, unsigned long long type_flags)
{
    if (!v)
	return NULL;

    /* Phase V4: objects must always go into the BSG view scope.  If no draw
     * root exists yet (non-GED consumer), there is nowhere to place the object
     * and we return NULL rather than silently using the legacy ptbl path. */
    struct bsg_node *scope = _bv_view_scope_ensure(v, local);
    if (!scope)
	return NULL;

    struct bsg_node *s = bsg_obj_get_unregistered(v, BV_VIEW_OBJS | (local ? BV_LOCAL_OBJS : 0));
    if (!s)
	return NULL;
    s->parent = scope;
    bu_ptbl_ins(&scope->children, (long *)s);

    s->s_type_flags |= BV_VIEWONLY;
    s->s_v = v;
    if (name && strlen(name)) {
	bu_vls_sprintf(&s->s_name, "%s", name);
    }
    s->s_flag = UP;
    s->s_changed++;

    if (type_flags)
	s->s_type_flags |= type_flags;

    return s;
}

struct bsg_node *
bsg_view_obj_create(struct bsg_view *v, const char *name, unsigned long long type_flags, const struct bsg_view_obj_opts *opts)
{
    int local = 0;
    if (opts)
	local = opts->local ? 1 : 0;

    struct bsg_node *s = _bv_view_obj_create(v, name, local, type_flags);
    if (!s)
	return NULL;

    if (opts && opts->arrow)
	s->s_arrow = 1;

    return s;
}

struct bsg_node *
bsg_view_obj_axes_create(struct bsg_view *v, const char *name, int local)
{
    struct bsg_view_obj_opts opts = BV_VIEW_OBJ_OPTS_INIT;
    opts.local = local;
    return bsg_view_obj_create(v, name, BV_AXES, &opts);
}

struct bsg_node *
bsg_view_obj_lines_create(struct bsg_view *v, const char *name, int local)
{
    struct bsg_view_obj_opts opts = BV_VIEW_OBJ_OPTS_INIT;
    opts.local = local;
    return bsg_view_obj_create(v, name, 0, &opts);
}

struct bsg_node *
bsg_view_obj_label_create(struct bsg_view *v, const char *name, int local)
{
    struct bsg_view_obj_opts opts = BV_VIEW_OBJ_OPTS_INIT;
    opts.local = local;
    return bsg_view_obj_create(v, name, BV_LABELS, &opts);
}

struct bsg_node *
bsg_view_obj_arrow_create(struct bsg_view *v, const char *name, int local)
{
    struct bsg_view_obj_opts opts = BV_VIEW_OBJ_OPTS_INIT;
    opts.local = local;
    opts.arrow = 1;
    return bsg_view_obj_create(v, name, 0, &opts);
}

struct bsg_node *
bsg_view_obj_overlay_create(struct bsg_view *v, const char *name, int local)
{
    struct bsg_view_obj_opts opts = BV_VIEW_OBJ_OPTS_INIT;
    opts.local = local;
    return bsg_view_obj_create(v, name, 0, &opts);
}

struct bsg_node *
bsg_view_obj_polygon_create(struct bsg_view *v, const char *name, int local)
{
    struct bsg_view_obj_opts opts = BV_VIEW_OBJ_OPTS_INIT;
    opts.local = local;
    return bsg_view_obj_create(v, name, BV_VIEWONLY, &opts);
}

void
bsg_view_obj_labels_sync(struct bsg_view *v,
                        struct bsg_data_label_state *gdlsp,
                        const char *bsg_name)
{
    /* Phase T3 (drawing_stack_modernization): BSG-backed label-scope sync.
     * This is the replacement for the deprecated dm_draw_labels() path.
     * Remove any previous BSG object for this slot, then rebuild from the
     * supplied gdlsp if drawing is enabled and there are labels to show. */
    if (!v || !gdlsp || !bsg_name)
	return;

    bsg_view_obj_remove(v, bsg_name);

    if (!gdlsp->gdls_draw || gdlsp->gdls_num_labels < 1)
	return;

    /* Create a container object (no geometry of its own) to hold per-label
     * BV_LABELS child objects, so the whole group is removed as one unit. */
    struct bsg_node *parent = bsg_view_obj_lines_create(v, bsg_name, 1 /* local */);
    if (!parent)
	return;

    for (int i = 0; i < gdlsp->gdls_num_labels; ++i) {
	struct bsg_node *child = bsg_obj_get_child(parent);
	if (!child)
	    continue;

	child->s_type_flags |= BV_LABELS;
	VSET(child->s_color, gdlsp->gdls_color[0], gdlsp->gdls_color[1], gdlsp->gdls_color[2]);
	child->s_flag = UP;

	struct bsg_label *l;
	BU_GET(l, struct bsg_label);
	BU_VLS_INIT(&l->label);
	bu_vls_sprintf(&l->label, "%s", gdlsp->gdls_labels[i]);
	VMOVE(l->p, gdlsp->gdls_points[i]);
	l->line_flag = 0;
	l->anchor    = BV_ANCHOR_AUTO;
	l->arrow     = 0;
	child->s_i_data = (void *)l;
    }
}

static int
_bv_view_scope_visible(struct bsg_node *scope, struct bsg_view *v)
{
    if (!scope || !(scope->s_type_flags & BSG_NODE_VIEW_SCOPE))
	return 0;
    if (!scope->s_v)
	return 1;
    return (scope->s_v == v) ? 1 : 0;
}

int
bsg_view_obj_remove(struct bsg_view *v, const char *name)
{
    if (!v || !name || !strlen(name))
	return 0;
    if (!v->gv_draw_root)
	return 0;

    struct bsg_node *root = (struct bsg_node *)v->gv_draw_root;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	struct bsg_node *scope = (struct bsg_node *)BU_PTBL_GET(&root->children, i);
	if (!scope || !(scope->s_type_flags & BSG_NODE_VIEW_SCOPE))
	    continue;
	if (_bv_is_independent_scope(scope, v))
	    continue;
	if (!_bv_view_scope_visible(scope, v))
	    continue;
	size_t j = BU_PTBL_LEN(&scope->children);
	while (j > 0) {
	    j--;
	    struct bsg_node *obj = (struct bsg_node *)BU_PTBL_GET(&scope->children, j);
	    if (!obj)
		continue;
	    if (!BU_STR_EQUAL(name, bu_vls_cstr(&obj->s_name)))
		continue;
	    bu_ptbl_rm(&scope->children, (long *)obj);
	    bsg_obj_put(obj);
	    if (!BU_PTBL_LEN(&scope->children)) {
		bu_ptbl_rm(&root->children, (long *)scope);
		bsg_obj_put(scope);
	    }
	    return 1;
	}
    }

    return 0;
}

size_t
bsg_view_obj_remove_all(struct bsg_view *v, int scope_mask)
{
    if (!v || !v->gv_draw_root)
	return 0;

    if (!scope_mask)
	scope_mask = BV_VIEW_OBJ_SCOPE_ALL;

    struct bsg_node *root = (struct bsg_node *)v->gv_draw_root;
    size_t removed = 0;
    size_t i = BU_PTBL_LEN(&root->children);
    while (i > 0) {
	i--;
	struct bsg_node *scope = (struct bsg_node *)BU_PTBL_GET(&root->children, i);
	if (!scope || !(scope->s_type_flags & BSG_NODE_VIEW_SCOPE))
	    continue;
	if (_bv_is_independent_scope(scope, v))
	    continue;
	int is_local = scope->s_v ? 1 : 0;
	if (is_local && !(scope_mask & BV_VIEW_OBJ_SCOPE_LOCAL))
	    continue;
	if (!is_local && !(scope_mask & BV_VIEW_OBJ_SCOPE_SHARED))
	    continue;
	if (!_bv_view_scope_visible(scope, v))
	    continue;
	size_t j = BU_PTBL_LEN(&scope->children);
	while (j > 0) {
	    j--;
	    struct bsg_node *obj = (struct bsg_node *)BU_PTBL_GET(&scope->children, j);
	    if (!obj)
		continue;
	    bu_ptbl_rm(&scope->children, (long *)obj);
	    bsg_obj_put(obj);
	    removed++;
	}
	if (!BU_PTBL_LEN(&scope->children)) {
	    bu_ptbl_rm(&root->children, (long *)scope);
	    bsg_obj_put(scope);
	}
    }

    return removed;
}

struct bsg_node *
bsg_view_obj_find(struct bsg_view *v, const char *name)
{
    if (!v || !name || !strlen(name) || !v->gv_draw_root)
	return NULL;

    struct bsg_node *root = (struct bsg_node *)v->gv_draw_root;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	struct bsg_node *scope = (struct bsg_node *)BU_PTBL_GET(&root->children, i);
	if (!scope || !(scope->s_type_flags & BSG_NODE_VIEW_SCOPE))
	    continue;
	if (_bv_is_independent_scope(scope, v))
	    continue;
	if (!_bv_view_scope_visible(scope, v))
	    continue;
	for (size_t j = 0; j < BU_PTBL_LEN(&scope->children); j++) {
	    struct bsg_node *obj = (struct bsg_node *)BU_PTBL_GET(&scope->children, j);
	    if (!obj)
		continue;
	    if (!BU_STR_EQUAL(name, bu_vls_cstr(&obj->s_name)))
		continue;
	    return obj;
	}
    }

    return NULL;
}

void
bsg_view_obj_visit(struct bsg_view *v,
		  int scope_mask,
		  int (*cb)(struct bsg_node *obj, void *data),
		  void *data)
{
    if (!v || !cb || !v->gv_draw_root)
	return;

    if (!scope_mask)
	scope_mask = BV_VIEW_OBJ_SCOPE_ALL;

    struct bsg_node *root = (struct bsg_node *)v->gv_draw_root;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	struct bsg_node *scope = (struct bsg_node *)BU_PTBL_GET(&root->children, i);
	if (!scope || !(scope->s_type_flags & BSG_NODE_VIEW_SCOPE))
	    continue;
	if (_bv_is_independent_scope(scope, v))
	    continue;
	int is_local = scope->s_v ? 1 : 0;
	if (is_local && !(scope_mask & BV_VIEW_OBJ_SCOPE_LOCAL))
	    continue;
	if (!is_local && !(scope_mask & BV_VIEW_OBJ_SCOPE_SHARED))
	    continue;
	if (!_bv_view_scope_visible(scope, v))
	    continue;
	for (size_t j = 0; j < BU_PTBL_LEN(&scope->children); j++) {
	    struct bsg_node *obj = (struct bsg_node *)BU_PTBL_GET(&scope->children, j);
	    if (!obj)
		continue;
	    /* API contract: callback returns 0/false to stop iteration early. */
	    if (!cb(obj, data))
		return;
	}
    }
}

/* Phase A3 (drawing_stack_modernization): typed setters for view-only object
 * properties.  Each mutates the matching bsg_node field and bumps the
 * stale flag so the renderer's backend cache is invalidated for the next
 * frame.  Callers must not write s_color / s_line_width / s_force_draw
 * directly. */
static int
_bv_clamp_byte(int v)
{
    if (v < 0)
	return 0;
    if (v > 255)
	return 255;
    return v;
}

void
bsg_view_obj_set_color(struct bsg_node *s, int r, int g, int b)
{
    if (!s)
	return;
    bsg_material_set_rgb(s,
			 (unsigned char)_bv_clamp_byte(r),
			 (unsigned char)_bv_clamp_byte(g),
			 (unsigned char)_bv_clamp_byte(b));
    s->s_changed++;
    bsg_obj_stale(s);
}

void
bsg_view_obj_set_line_width(struct bsg_node *s, int line_width)
{
    if (!s)
	return;
    if (line_width < 0)
	line_width = 0;
    /* By convention bsg_obj_reset() sets s_os = &s->s_local_os, but other
     * code paths in this file defensively fall back to s_local_os when
     * s_os is unset; do the same here. */
    bsg_appearance_set_line_width(s, line_width);
    s->s_changed++;
    bsg_obj_stale(s);
}

void
bsg_view_obj_set_visible(struct bsg_node *s, int visible)
{
    if (!s)
	return;
    bsg_appearance_set_force_draw(s, visible);
    s->s_changed++;
    bsg_obj_stale(s);
}

struct bsg_node *
bsg_obj_get_child(struct bsg_node *sp)
{
    if (!sp)
	return NULL;

    bsg_log(1, "bsg_obj_get_child %s(%s)", bu_vls_cstr(&sp->s_name), _bv_vname(sp->s_v));

    struct bsg_node *s = NULL;

    // Children use their parent's info
    if (BU_LIST_IS_EMPTY(&sp->free_scene_obj->l)) {
	BU_ALLOC((s), struct bsg_node);
	s->i = new bsg_node_internal;
    } else {
	s = BU_LIST_NEXT(bsg_node, &sp->free_scene_obj->l);
	if (!s) {
	    BU_ALLOC((s), struct bsg_node);
	    s->i = new bsg_node_internal;
	} else {
	    BU_LIST_DEQUEUE(&((s)->l));
	}
    }

    // Use reset to do most of the initialization
    bsg_obj_reset(s);

    bu_vls_sprintf(&s->s_name, "child:%s:%zd", bu_vls_cstr(&sp->s_name), BU_PTBL_LEN(&sp->children));

    s->s_v = sp->s_v;
    s->dp = sp->dp;
    s->free_scene_obj = sp->free_scene_obj;
    s->vlfree = sp->vlfree;

    bu_ptbl_ins(&sp->children, (long *)s);

    return s;
}

void
bsg_obj_reset(struct bsg_node *s)
{
    // handle children
    if (BU_PTBL_IS_INITIALIZED(&s->children)) {
	for (size_t i = 0; i < BU_PTBL_LEN(&s->children); i++) {
	    struct bsg_node *s_c = (struct bsg_node *)BU_PTBL_GET(&s->children, i);
	    bsg_obj_put(s_c);
	}
    } else {
	BU_PTBL_INIT(&s->children);
    }
    bu_ptbl_reset(&s->children);

    // If we have a callback for the internal data, use it
    if (s->s_free_callback)
	(*s->s_free_callback)(s);
    s->s_free_callback = NULL;

    // Phase 11: release any backend-owned per-shape state via the generic
    // contract.
    bsg_scene_obj_release_backend(s);

    // If we have a label, do the label freeing steps
    // TODO - this should be using the free callback rather
    // than special casing...
    if ((s->s_type_flags & BV_LABELS) && s->s_i_data) {
	struct bsg_label *la = (struct bsg_label *)s->s_i_data;
	bu_vls_free(&la->label);
	BU_PUT(la, struct bsg_label);
    }

    // free vlist
    if (BU_LIST_IS_INITIALIZED(&s->s_vlist)) {
	BSG_FREE_VLIST(s->vlfree, &s->s_vlist);
    }
    BU_LIST_INIT(&(s->s_vlist));

    if (!BU_VLS_IS_INITIALIZED(&s->s_name))
	BU_VLS_INIT(&s->s_name);
    bu_vls_trunc(&s->s_name, 0);

    struct bsg_obj_settings defaults = BV_OBJ_SETTINGS_INIT;
    bsg_obj_settings_sync(&s->s_local_os, &defaults);
    s->s_os = &s->s_local_os;
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
    s->s_flag = UP;
    s->s_force_draw = 0;
    s->s_i_data = NULL;
    s->s_iflag = DOWN;
    s->s_path = NULL;
    s->s_size = 0;
    s->s_soldash = 0;
    s->s_update_callback = NULL;
    s->s_v = NULL;
    s->view_scale = 0;
}

#define FREE_BV_SCENE_OBJ(p, fp) { \
    BU_LIST_APPEND(fp, &((p)->l)); }

void
bsg_obj_put(struct bsg_node *s)
{
    bsg_log(1, "bsg_obj_put %s[%s]", bu_vls_cstr(&s->s_name), _bv_vname(s->s_v));
    for (size_t i = 0; i < BU_PTBL_LEN(&s->children); i++) {
	struct bsg_scene_group *cg = (struct bsg_scene_group *)BU_PTBL_GET(&s->children, i);
	bsg_obj_put(cg);
    }

    // If this object was selected for snapping, it is no longer a valid candidate
    if (s->s_v)
	bu_ptbl_rm(&s->s_v->gv_s->gv_snap_objs, (long *)s);

    bsg_obj_reset(s);

    // Clear names
    bu_vls_trunc(&s->s_name, 0);
    s->s_path = NULL;

    if (s->otbl)
	bu_ptbl_rm(s->otbl, (long *)s);

    /* Phase V4: for BSG-placed view objects (otbl==NULL, parent set), remove
     * from the parent scope's children so no stale pointer remains. */
    if (!s->otbl && s->parent) {
	bu_ptbl_rm(&s->parent->children, (long *)s);
	s->parent = NULL;
    }

    s->otbl = NULL;

    struct bsg_node *fs = s->free_scene_obj;
    s->free_scene_obj = NULL;
    if (fs)
	FREE_BV_SCENE_OBJ(s, &fs->l);
}

struct bsg_node *
bsg_find_obj(struct bsg_view *v, const char *name)
{
    if (!v || !name)
	return NULL;

    // First look for matches in shared sets, if any are defined
    if (!bsg_view_is_independent(v) && v->vset) {
	for (size_t i = 0; i < BU_PTBL_LEN(&v->vset->i->shared_db_objs); i++) {
	    struct bsg_node *s_c = (struct bsg_node *)BU_PTBL_GET(&v->vset->i->shared_db_objs, i);
	    if (!bu_path_match(name, bu_vls_cstr(&s_c->s_name), 0))
		return s_c;
	}
    }

    // Next look locally in DB objects
    for (size_t i = 0; i < BU_PTBL_LEN(v->gv_objs.db_objs); i++) {
	struct bsg_node *s_c = (struct bsg_node *)BU_PTBL_GET(v->gv_objs.db_objs, i);
	if (!bu_path_match(name, bu_vls_cstr(&s_c->s_name), 0))
	    return s_c;
    }

    if (!v->gv_draw_root)
	return NULL;

    std::queue<struct bsg_node *> nqueue;
    nqueue.push((struct bsg_node *)v->gv_draw_root);
    while (!nqueue.empty()) {
	struct bsg_node *n = nqueue.front();
	nqueue.pop();
	for (size_t i = 0; i < BU_PTBL_LEN(&n->children); i++) {
	    struct bsg_node *c = (struct bsg_node *)BU_PTBL_GET(&n->children, i);
	    if (!c)
		continue;
	    if (_bv_independent_root_skip_child(v, n, c))
		continue;
	    if ((c->s_type_flags & BSG_NODE_VIEW_SCOPE) && !_bv_view_scope_visible(c, v))
		continue;
	    if (BU_VLS_IS_INITIALIZED(&c->s_name) && !bu_path_match(name, bu_vls_cstr(&c->s_name), 0))
		return c;
	    nqueue.push(c);
	}
    }

    return NULL;
}

static bool
_uniq_name(const char *name, struct bsg_view *v)
{
    if (bsg_find_obj(v, name))
	return false;

    if (v->vset) {
	for (size_t i = 0; i < BU_PTBL_LEN(&v->vset->i->shared_db_objs); i++) {
	    struct bsg_node *s_c = (struct bsg_node *)BU_PTBL_GET(&v->vset->i->shared_db_objs, i);
	    if (BU_STR_EQUAL(name, bu_vls_cstr(&s_c->s_name)))
		return false;
	}
    }

    // Next look locally
    for (size_t i = 0; i < BU_PTBL_LEN(v->gv_objs.db_objs); i++) {
	struct bsg_node *s_c = (struct bsg_node *)BU_PTBL_GET(v->gv_objs.db_objs, i);
	if (BU_STR_EQUAL(name, bu_vls_cstr(&s_c->s_name)))
	    return false;
    }

    /* Phase V4: view-only objects are found via bsg_find_obj's BSG walk above. */

    return true;
}

void
bsg_uniq_obj_name(struct bu_vls *oname, const char *seed, struct bsg_view *v)
{
    if (!oname || !v)
	return;

    struct bu_vls vseed = BU_VLS_INIT_ZERO;
    if (seed) {
	bu_vls_sprintf(&vseed, "%s", seed);
    } else {
	bu_vls_sprintf(&vseed, "%s:obj_0", bu_vls_cstr(&v->gv_name));
    }


    const char *npattern = "([-_:]*[0-9]+[-_:]*)[^0-9]*$";
    long int loop_guard = 0;
    bool is_uniq = _uniq_name(bu_vls_cstr(&vseed), v);
    while (!is_uniq && loop_guard < LONG_MAX) {
	(void)bu_vls_incr(&vseed, npattern, NULL, NULL, NULL);
	is_uniq = _uniq_name(bu_vls_cstr(&vseed), v);
	loop_guard++;
    }

    bu_vls_sprintf(oname, "%s", bu_vls_cstr(&vseed));
    bu_vls_free(&vseed);
}

struct bsg_node *
bsg_find_child(struct bsg_node *s, const char *vname)
{
    if (!s || !vname || !BU_PTBL_IS_INITIALIZED(&s->children))
	return NULL;
    for (size_t i = 0; i < BU_PTBL_LEN(&s->children); i++) {
	struct bsg_node *s_c = (struct bsg_node *)BU_PTBL_GET(&s->children, i);
	if (!bu_path_match(vname, bu_vls_cstr(&s_c->s_name), 0))
	    return s_c;
    }

    return NULL;
}

static int
_bv_lod_node_active_level(struct bsg_node *lod, struct bsg_view *v)
{
    if (!lod || !(lod->s_type_flags & BSG_NODE_LOD) || !v)
	return -1;

    struct bsg_lod_payload *pl = (struct bsg_lod_payload *)lod->s_i_data;
    if (!pl)
	return -1;

    for (size_t i = 0; i < pl->cursor_count; i++) {
	if (pl->cursors[i].v == v)
	    return pl->cursors[i].level;
    }

    return -1;
}

int
bsg_scene_obj_bound(struct bsg_node *sp, struct bsg_view *v)
{
    int cmd;
    VSET(sp->bmin, INFINITY, INFINITY, INFINITY);
    VSET(sp->bmax, -INFINITY, -INFINITY, -INFINITY);
    int calc = 0;
    struct bsg_node *s = sp;
    struct bsg_node *lod = NULL;
    if (s->s_type_flags & BSG_NODE_LOD) {
	lod = s;
    } else {
	struct bsg_node *p = (struct bsg_node *)s->parent;
	if (p && (p->s_type_flags & BSG_NODE_LOD))
	    lod = p;
    }
    if (lod) {
	int active = _bv_lod_node_active_level(lod, v);
	int nlevels = (int)BU_PTBL_LEN(&lod->children);
	if (nlevels > 0) {
	    if (active < 0 || active >= nlevels)
		active = 0;
	    struct bsg_node *ls = (struct bsg_node *)BU_PTBL_GET(&lod->children, active);
	    if (ls) {
		s = ls;
		if (isfinite(s->bmin[X]) && isfinite(s->bmin[Y]) && isfinite(s->bmin[Z]) &&
		    isfinite(s->bmax[X]) && isfinite(s->bmax[Y]) && isfinite(s->bmax[Z])) {
		    calc = 1;
		}
	    }
	}
    }
    if (!calc && s->mesh_obj && s->draw_data) {
	struct bsg_mesh_lod *i = (struct bsg_mesh_lod *)s->draw_data;
	if (i) {
	    point_t obmin, obmax;
	    VMOVE(obmin, i->bmin);
	    VMOVE(obmax, i->bmax);
	    // Apply the scene matrix to the bounding box values to bound this
	    // instance, since the mesh LoD data is based on the
	    // non-instanced mesh.
	    MAT4X3PNT(s->bmin, s->s_mat, obmin);
	    MAT4X3PNT(s->bmax, s->s_mat, obmax);
	    calc = 1;
	}
    } else if (!calc && bu_list_len(&s->s_vlist)) {
	int dismode;
	cmd = bsg_vlist_bbox(&s->s_vlist, &s->bmin, &s->bmax, NULL, &dismode);
	if (cmd) {
	    bu_log("unknown vlist op %d\n", cmd);
	}
	s->s_displayobj = dismode;
	calc = 1;
    }
    if (calc) {
	s->s_center[X] = (s->bmin[X] + s->bmax[X]) * 0.5;
	s->s_center[Y] = (s->bmin[Y] + s->bmax[Y]) * 0.5;
	s->s_center[Z] = (s->bmin[Z] + s->bmax[Z]) * 0.5;

	s->s_size = s->bmax[X] - s->bmin[X];
	V_MAX(s->s_size, s->bmax[Y] - s->bmin[Y]);
	V_MAX(s->s_size, s->bmax[Z] - s->bmin[Z]);

	// sp may not be the same as s - propagate up
	VMOVE(sp->s_center, s->s_center);
	VMOVE(sp->bmin, s->bmin);
	VMOVE(sp->bmax, s->bmax);
	sp->s_size = s->s_size;

	return 1;
    }

    return 0;
}

fastf_t
bsg_vZ_calc(struct bsg_node *s, struct bsg_view *v, int mode)
{
    fastf_t vZ = 0.0;
    int calc_mode = mode;
    if (!s)
	return vZ;

    if (mode < 0)
	calc_mode = 0;
    if (mode > 1)
	calc_mode = 1;

    double calc_val = (calc_mode) ? -DBL_MAX : DBL_MAX;
    int have_val = 0;
    struct bsg_vlist *tvp;
    for (BU_LIST_FOR(tvp, bsg_vlist, &((struct bsg_vlist *)(&s->s_vlist))->l)) {
	size_t nused = tvp->nused;
	point_t *lpt = tvp->pt;
	for (size_t l = 0; l < nused; l++, lpt++) {
	    vect_t vpt;
	    MAT4X3PNT(vpt, v->gv_model2view, *lpt);
	    if (calc_mode) {
		if (vpt[Z] > calc_val) {
		    calc_val = vpt[Z];
		    have_val = 1;
		}
	    } else {
		if (vpt[Z] < calc_val) {
		    calc_val = vpt[Z];
		    have_val = 1;
		}
	    }
	}
    }
    if (have_val) {
	vZ = calc_val;
    }
    return vZ;
}


struct bu_ptbl *
bsg_view_objs(struct bsg_view *v, int type)
{
    if (!v)
	return NULL;

    if (type & BV_DB_OBJS) {
	if (type & BV_LOCAL_OBJS || bsg_view_is_independent(v)) {
	    return v->gv_objs.db_objs;
	} else {
	    if (v->vset)
		return &v->vset->i->shared_db_objs;
	}
    }

    /* BV_VIEW_OBJS queries are no longer supported (Phase D,
     * drawing_stack_modernization); use bsg_view_obj_visit instead. */

    return NULL;
}


/* Internal DFS helper for bsg_view_objs_visit_db.
 * Traverses the BSG tree rooted at @p node (which is just a struct bsg_node
 * since bsg_node is a layout-compatible alias).  The callback is invoked for
 * every node whose s_type_flags has BV_DB_OBJS set; returning 0 from the
 * callback stops traversal early. */
static int
_bv_visit_db_internal(struct bsg_node *node,
		      int (*cb)(struct bsg_node *, void *),
		      void *data)
{
    if (!node)
	return 1;

    /* Call back for DB-derived shape leaves */
    if ((node->s_type_flags & BV_DB_OBJS) && !(node->s_type_flags & BSG_NODE_VIEW_SCOPE)) {
	if (!cb(node, data))
	    return 0;
    }

    /* Recurse into children (groups and any other sub-nodes) */
    for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	struct bsg_node *child =
	    (struct bsg_node *)BU_PTBL_GET(&node->children, i);
	if (!_bv_visit_db_internal(child, cb, data))
	    return 0;
    }

    return 1;
}


void
bsg_view_objs_visit_db(struct bsg_view *v,
		      int (*cb)(struct bsg_node *obj, void *data),
		      void *data)
{
    /* Iterate all DB-derived scene objects visible from @p v.  When the view
     * has a BSG draw root (GED consumers after Phase B), the BSG tree is
     * traversed depth-first and the callback fires for every node with
     * BV_DB_OBJS set in s_type_flags.  For non-GED consumers (no gv_draw_root)
     * the legacy BV_DB_OBJS and BV_DB_OBJS|BV_LOCAL_OBJS ptbls are iterated.
     * Returning 0 from the callback stops traversal early.
     * See include/bv/util.h for the full API contract. */
    if (!v || !cb)
	return;

    if (v->gv_draw_root) {
	/* Phase B: GED consumers with the BSG draw tree.  The tree is
	 * depth-first traversed; the callback fires for every node whose
	 * s_type_flags has BV_DB_OBJS set (i.e. BViewState-owned leaves). */
	struct bsg_node *root = (struct bsg_node *)v->gv_draw_root;
	if (bsg_view_is_independent(v)) {
	    struct bsg_node *scope = _bv_independent_scope_find(root, v);
	    if (scope)
		_bv_visit_db_internal(scope, cb, data);
	} else {
	    _bv_visit_db_internal(root, cb, data);
	}
	return;
    }

    /* Fallback: non-GED / legacy consumers that store top-level objects
     * directly in the gv_objs ptbls. */
    struct bu_ptbl *so = bsg_view_objs(v, BV_DB_OBJS);
    if (so) {
	for (size_t i = 0; i < BU_PTBL_LEN(so); i++) {
	    struct bsg_node *s =
		(struct bsg_node *)BU_PTBL_GET(so, i);
	    if (!cb(s, data))
		return;
	}
    }
    struct bu_ptbl *sol = bsg_view_objs(v, BV_DB_OBJS | BV_LOCAL_OBJS);
    if (sol && sol != so) {
	for (size_t i = 0; i < BU_PTBL_LEN(sol); i++) {
	    struct bsg_node *s =
		(struct bsg_node *)BU_PTBL_GET(sol, i);
	    if (!cb(s, data))
		return;
	}
    }
}


void
bsg_obj_sync(struct bsg_node *dest, struct bsg_node *src)
{
    bsg_obj_settings_sync(dest->s_os, src->s_os);
    VMOVE(dest->s_center, src->s_center);
    VMOVE(dest->s_color, src->s_color);
    VMOVE(dest->bmin, src->bmin);
    VMOVE(dest->bmax, src->bmax);
    MAT_COPY(dest->s_mat, src->s_mat);
    dest->s_size = src->s_size;
    dest->s_soldash = src->s_soldash;
    dest->s_arrow = src->s_arrow;
    dest->adaptive_wireframe = src->adaptive_wireframe;
    dest->view_scale = src->view_scale;
    dest->bot_threshold = src->bot_threshold;
    dest->curve_scale = src->curve_scale;
    dest->point_scale = src->point_scale;
}

int
bsg_illum_obj(struct bsg_node *s, char ill_state)
{
    bool changed = 0;
    for (size_t i = 0; i < BU_PTBL_LEN(&s->children); i++) {
	struct bsg_node *s_c = (struct bsg_node *)BU_PTBL_GET(&s->children, i);
	int cchanged = bsg_illum_obj(s_c, ill_state);
	if (cchanged)
	    changed = 1;
    }
    if (ill_state != s->s_iflag) {
	changed = 1;
	s->s_iflag = ill_state;
	//bsg_obj_stale(s);
    }
    return changed;
}

//#define USE_BV_LOG
void
#ifdef USE_BV_LOG
bsg_log(int level, const char *fmt, ...)
#else
bsg_log(int UNUSED(level), const char *UNUSED(fmt), ...)
#endif
{
#ifdef USE_BV_LOG
    if (level < 0 || !fmt)
	return;
    const char *brsig = getenv("BV_LOG");
    if (!brsig)
	return;
    if (brsig) {
	int blev = atoi(brsig);
	if (blev < level)
	    return;
    }

    va_list ap;
    struct bu_vls msg = BU_VLS_INIT_ZERO;
    va_start(ap, fmt);
    bu_vls_vprintf(&msg, fmt, ap);
    bu_log("%s\n", bu_vls_cstr(&msg));
    bu_vls_free(&msg);
    va_end(ap);
#endif
}

void
bsg_view_print(const char *title, struct bsg_view *v, int UNUSED(verbosity))
{
    if (!v)
	return;

    struct bu_vls vtitle = BU_VLS_INIT_ZERO;
    if (title) {
	bu_vls_sprintf(&vtitle, "%s", title);
    } else {
	bu_vls_sprintf(&vtitle, "%s", bu_vls_cstr(&v->gv_name));
    }

    bu_log("%s\n", bu_vls_cstr(&vtitle));
    bu_vls_free(&vtitle);

    bu_log("Size info:\n");
    bu_log("  i_scale:      %f\n", v->gv_i_scale);
    bu_log("  a_scale:      %f\n", v->gv_a_scale);
    bu_log("  scale:        %f\n", v->gv_scale);
    bu_log("  size:         %f\n", v->gv_size);
    bu_log("  isize:        %f\n", v->gv_isize);
    bu_log("  base2local:   %f\n", v->gv_base2local);
    bu_log("  local2base:   %f\n", v->gv_local2base);
    bu_log("  rscale:       %f\n", v->gv_rscale);
    bu_log("  sscale:       %f\n", v->gv_sscale);

    bu_log("Window info:");
    bu_log("  width:        %d\n", v->gv_width);
    bu_log("  height:       %d\n", v->gv_height);
    bu_log("  wmin:         %f\n, %f", v->gv_wmin[0], v->gv_wmin[1]);
    bu_log("  wmax:         %f\n, %f", v->gv_wmax[0], v->gv_wmax[1]);

    bu_log("Camera info:");
    bu_log("  perspective:  %f\n", v->gv_perspective);
    bu_log("  aet:          %f %f %f\n", V3ARGS(v->gv_aet));
    bu_log("  eye_pos:      %f %f %f\n", V3ARGS(v->gv_eye_pos));
    bu_log("  keypoint:     %f %f %f\n", V3ARGS(v->gv_keypoint));
    bu_log("  coord:        %c\n", v->gv_coord);
    bu_log("  rotate_about: %c\n", v->gv_rotate_about);
    bn_mat_print("rotation", v->gv_rotation);
    bn_mat_print("center", v->gv_center);
    bn_mat_print("model2view", v->gv_model2view);
    bn_mat_print("pmodel2view", v->gv_pmodel2view);
    bn_mat_print("view2model", v->gv_view2model);
    bn_mat_print("perspective", v->gv_pmat);

    bu_log("Keyboard/mouse info:");
    bu_log("  prevMouseX:   %f\n", v->gv_prevMouseX);
    bu_log("  prevMouseY:   %f\n", v->gv_prevMouseY);
    bu_log("  mouse_x:      %d\n", v->gv_mouse_x);
    bu_log("  mouse_y:      %d\n", v->gv_mouse_y);
    bu_log("  gv_prev_point:%f %f %f\n", V3ARGS(v->gv_prev_point));
    bu_log("  gv_point:     %f %f %f\n", V3ARGS(v->gv_point));
    bu_log("  key:          %c\n", v->gv_key);
    bu_log("  mod_flags:    %ld\n", v->gv_mod_flags);
    bu_log("  minMousedelta:%f\n", v->gv_minMouseDelta);
    bu_log("  maxMousedelta:%f\n", v->gv_maxMouseDelta);
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
