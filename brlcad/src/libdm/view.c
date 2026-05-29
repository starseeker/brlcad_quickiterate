/*                          V I E W . C
 * BRL-CAD
 *
 * Copyright (c) 2007-2026 United States Government as represented by
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

#include "common.h"

#include <string.h>

#include "bu/str.h"
#include "bu/time.h"
#include "bu/units.h"
#include "bu/vls.h"
#include "bn.h"
#include "bsg/defines.h"
#include "bsg/appearance.h"
#include "bsg/hud.h"
#include "bsg/render.h"
#include "bsg/render_item.h"
#include "bsg/backend_adapter.h"
#include "bsg/material.h"
#include "bsg/node.h"
#include "bsg/lod.h"
#include "bsg/payload_typed.h"
#include "bsg/util.h"
#include "bsg/lod_ops.h"
#include "bsg/visit.h"
#include "bsg/view_scope.h"
#include "dm.h"
#include "bsg/node_private.h"

void
dm_draw_arrow(struct dm *dmp, point_t A, point_t B, fastf_t tip_length, fastf_t tip_width, fastf_t sf)
{
    point_t points[16];
    point_t BmA;
    point_t offset;
    point_t perp1, perp2;
    point_t a_base;
    point_t a_pt1, a_pt2, a_pt3, a_pt4;

    VSUB2(BmA, B, A);

    VUNITIZE(BmA);
    VSCALE(offset, BmA, -tip_length * sf);

    bn_vec_perp(perp1, BmA);
    VUNITIZE(perp1);

    VCROSS(perp2, BmA, perp1);
    VUNITIZE(perp2);

    VSCALE(perp1, perp1, tip_width * sf);
    VSCALE(perp2, perp2, tip_width * sf);

    VADD2(a_base, B, offset);
    VADD2(a_pt1, a_base, perp1);
    VADD2(a_pt2, a_base, perp2);
    VSUB2(a_pt3, a_base, perp1);
    VSUB2(a_pt4, a_base, perp2);

    VMOVE(points[0], B);
    VMOVE(points[1], a_pt1);
    VMOVE(points[2], B);
    VMOVE(points[3], a_pt2);
    VMOVE(points[4], B);
    VMOVE(points[5], a_pt3);
    VMOVE(points[6], B);
    VMOVE(points[7], a_pt4);
    VMOVE(points[8], a_pt1);
    VMOVE(points[9], a_pt2);
    VMOVE(points[10], a_pt2);
    VMOVE(points[11], a_pt3);
    VMOVE(points[12], a_pt3);
    VMOVE(points[13], a_pt4);
    VMOVE(points[14], a_pt4);
    VMOVE(points[15], a_pt1);

    (void)dm_draw_lines_3d(dmp, 16, points, 0);
}


/* Draw label payloads for BSG_SHAPE_LABELS nodes. */
void dm_draw_label(struct dm *dmp, struct bsg_node *s);

// Draw an arrow head for each MOVE+LAST_DRAW paring
void
dm_add_arrows(struct dm *dmp, struct bsg_node *s)
{
    bsg_vlist *vp = (bsg_vlist *)&s->s_vlist;
    bsg_vlist *tvp;
    point_t A = VINIT_ZERO;
    point_t B = VINIT_ZERO;
    int pcnt = 0;
    if (!s->s_arrow)
	return;
    if (NEAR_ZERO(s->s_os->s_arrow_tip_length, SMALL_FASTF) || NEAR_ZERO(s->s_os->s_arrow_tip_width, SMALL_FASTF))
       return;
    for (BU_LIST_FOR(tvp, bsg_vlist, &vp->l)) {
	int nused = tvp->nused;
	int *cmd = tvp->cmd;
	point_t *pt = tvp->pt;
	for (int i = 0; i < nused; i++, cmd++, pt++) {
	    pcnt++;
	    switch (*cmd) {
		case BSG_VLIST_LINE_MOVE:
		    if (pcnt > 1) {
			// We have a move and more than one point - add an arrow
			// to the A -> B segment at B
			dm_draw_arrow(dmp, A, B, s->s_os->s_arrow_tip_length, s->s_os->s_arrow_tip_width, 1.0);
		    }
		    VMOVE(B,*pt);
		    break;
		case BSG_VLIST_LINE_DRAW:
		    VMOVE(A,B);
		    VMOVE(B,*pt);
		    break;
		default:
		    // For these purposes, we're only interested in lines
		    break;
	    }

	}
    }
    // Get the last pairing
    if (pcnt > 1)
	dm_draw_arrow(dmp, A, B, s->s_os->s_arrow_tip_length, s->s_os->s_arrow_tip_width, 1.0);
}

static void
_dm_draw_hud_axes_feature(struct dm *dmp, struct bsg_view *v, const struct bsg_hud_payload *payload, int model_axes)
{
    if (!payload)
	return;

    struct bsg_axes axes = payload->data.axes;
    if (model_axes) {
	point_t map;
	point_t save_map;

	VMOVE(save_map, axes.axes_pos);
	VSCALE(map, axes.axes_pos, v->gv_local2base);
	MAT4X3PNT(axes.axes_pos, v->gv_model2view, map);
	dm_draw_hud_axes(dmp, v->gv_size, v->gv_rotation, &axes);
	VMOVE(axes.axes_pos, save_map);
	return;
    }

    int width = dm_get_width(dmp);
    int height = dm_get_height(dmp);
    fastf_t inv_aspect = (width > 0) ? (fastf_t)height / (fastf_t)width : 1.0;
    axes.axes_pos[Y] = axes.axes_pos[Y] * inv_aspect;
    dm_draw_hud_axes(dmp, v->gv_size, v->gv_rotation, &axes);
}


static void
_dm_draw_hud_grid(struct dm *dmp, struct bsg_view *v, const struct bsg_hud_payload *payload)
{
    if (!payload)
	return;
    dm_draw_grid(dmp, (struct bsg_grid_state *)&payload->data.grid, v->gv_scale, v->gv_model2view, v->gv_base2local);
}


static void
_dm_draw_hud_framebuffer(struct dm *dmp)
{
    if (!dmp || !dm_get_fb(dmp))
	return;

    int zbuff_restore = dm_get_zbuffer(dmp);
    dm_set_zbuffer(dmp, 0);
    struct fb *fbp = dm_get_fb(dmp);
    int rw = dm_get_width(dmp);
    int rh = dm_get_height(dmp);
    if (fbp) {
	int fbw = fb_getwidth(fbp);
	int fbh = fb_getheight(fbp);
	if (fbw > 0 && fbw < rw) rw = fbw;
	if (fbh > 0 && fbh < rh) rh = fbh;
    }
    if (rw > 0 && rh > 0)
	fb_refresh(fbp, 0, 0, rw, rh);
    if (zbuff_restore)
	dm_set_zbuffer(dmp, 1);
}


static void
_dm_hud_draw_item(void *dmp_ptr, const struct bsg_render_item *item)
{
    struct dm *dmp = (struct dm *)dmp_ptr;
    if (!dmp || !item || !item->node || !item->node->s_v)
	return;

    struct bsg_view *v = item->node->s_v;
    const struct bsg_hud_node_meta *meta = bsg_hud_node_get_meta(item->node);
    struct bsg_payload *pl = bsg_node_get_payload(item->node);
    if (!meta || !pl)
	return;

    switch (pl->pl_type) {
	case BSG_PL_LINE_SET: {
	    struct bsg_payload_line_set *ls = bsg_payload_line_set_get(pl);
	    if (!ls || ls->point_cnt < 2)
		break;
	    int save_lw = dm_get_linewidth(dmp);
	    int save_ls = dm_get_linestyle(dmp);
	    dm_set_line_attr(dmp, item->appearance.line_width, item->node->s_soldash);
	    dm_set_fg(dmp, item->appearance.color[0], item->appearance.color[1], item->appearance.color[2], 1, 1.0);
	    point_t prev = VINIT_ZERO;
	    int have_prev = 0;
	    for (size_t i = 0; i < ls->point_cnt; i++) {
		if (ls->cmds[i] == BSG_VLIST_LINE_MOVE) {
		    VMOVE(prev, ls->points[i]);
		    have_prev = 1;
		    continue;
		}
		if (ls->cmds[i] == BSG_VLIST_LINE_DRAW && have_prev) {
		    dm_draw_line_2d(dmp, prev[X], prev[Y], ls->points[i][X], ls->points[i][Y]);
		    VMOVE(prev, ls->points[i]);
		}
	    }
	    dm_set_line_attr(dmp, save_lw, save_ls);
	    break;
	}
	case BSG_PL_HUD_TEXT: {
	    struct bsg_label *label = bsg_payload_hud_text_get(pl);
	    if (!label)
		break;
	    int ofontsize = dm_get_fontsize(dmp);
	    dm_set_fg(dmp, item->appearance.color[0], item->appearance.color[1], item->appearance.color[2], 1, 1.0);
	    dm_set_fontsize(dmp, label->size);
	    dm_draw_string_2d(dmp, bu_vls_cstr(&label->label), label->p[X], label->p[Y], label->size, 0);
	    dm_set_fontsize(dmp, ofontsize);
	    break;
	}
	case BSG_PL_AXES: {
	    struct bsg_axes *axes = bsg_payload_axes_get(pl);
	    if (!axes)
		break;
	    struct bsg_hud_payload payload;
	    memset(&payload, 0, sizeof(payload));
	    payload.feature_type = meta->feature_type;
	    payload.data.axes = *axes;
	    _dm_draw_hud_axes_feature(dmp, v, &payload, meta->feature_type == BSG_HUD_FEATURE_MODEL_AXES);
	    break;
	}
	case BSG_PL_GRID: {
	    struct bsg_grid_state *grid = bsg_payload_grid_get(pl);
	    if (!grid)
		break;
	    struct bsg_hud_payload payload;
	    memset(&payload, 0, sizeof(payload));
	    payload.feature_type = meta->feature_type;
	    payload.data.grid = *grid;
	    _dm_draw_hud_grid(dmp, v, &payload);
	    break;
	}
	default:
	    break;
    }
}


static void
_dm_framebuffer_draw_item(void *dmp_ptr, const struct bsg_render_item *item)
{
    struct dm *dmp = (struct dm *)dmp_ptr;
    if (!dmp || !item || !item->node || !item->node->s_v)
	return;

    const struct bsg_hud_node_meta *meta = bsg_hud_node_get_meta(item->node);
    struct bsg_payload *pl = bsg_node_get_payload(item->node);
    if (!meta || !pl || pl->pl_type != BSG_PL_FRAMEBUFFER || meta->feature_type != BSG_HUD_FEATURE_FRAMEBUFFER)
	return;

    _dm_draw_hud_framebuffer(dmp);
}


static int
_dm_hud_render_request(struct bsg_view *v, struct bsg_backend_adapter *adapter)
{
    if (!v || !v->dmp)
	return 0;
    if (bsg_hud_sync(v) != 0)
	return 0;

    bsg_node *root = bsg_hud_root_get(v);
    if (!root)
	return 0;

    struct bsg_render_request *req = bsg_render_request_create(v, root, v->dmp);
    if (!req)
	return 0;
    req->flags = BSG_RENDER_FLAG_VISIBLE_ONLY | BSG_RENDER_FLAG_HUD_PASS;
    req->adapter = adapter;
    int ret = bsg_render_request_execute(req);
    bsg_render_request_destroy(req);
    return ret;
}

static int
_dm_scene_prepare_item(void *dmp_ptr, const struct bsg_render_item *item)
{
    (void)dmp_ptr;
    return item && item->node ? 1 : 0;
}

static void
_dm_scene_draw_item(void *dmp_ptr, const struct bsg_render_item *item)
{
    struct dm *dmp = (struct dm *)dmp_ptr;
    if (!dmp || !item || !item->node)
	return;

    struct bsg_node *s = item->node;
    struct bsg_view *v = s->s_v ? s->s_v : item->view;
    if (!v)
	return;

    if (dm_get_transparency(dmp))
	(void)dm_set_depth_mask(dmp, (item->phase == BSG_RENDER_PHASE_TRANSPARENT) ? 0 : 1);

    if (dm_get_bound_flag(dmp)
	&& !s->s_displayobj
	&& (s->s_type_flags & BSG_NODE_SHAPE)
	&& v->gv_isize > 0
	&& s->s_size > SMALL_FASTF
	&& (s->s_size * v->gv_isize) < 0.001) {
	return;
    }

    mat_t model2view;
    bn_mat_mul(model2view, v->gv_model2view, item->model_mat);
    dm_loadmatrix(dmp, model2view, 0);
    if (item->appearance.highlighted && v->gv_edit_mat)
	dm_loadmatrix(dmp, v->gv_edit_mat, 0);

    if (item->appearance.highlighted) {
	(void)dm_set_fg(dmp, 255, 255, 255, 0, item->appearance.transparency);
    } else {
	/* Phase D5/G7: command override, geometry-default color (s_cflag),
	 * and the base material color are all resolved into
	 * item->appearance.color by bsg_appearance_resolve, so the backend
	 * reads the final color directly instead of re-deriving it. */
	(void)dm_set_fg(dmp, item->appearance.color[0], item->appearance.color[1], item->appearance.color[2], 0, item->appearance.transparency);
    }

    int lw = item->appearance.line_width;
    if (lw <= 0)
	lw = dm_get_linewidth(dmp);
    (void)dm_set_line_attr(dmp, lw, item->appearance.line_style);

    (void)dm_backend_draw_obj(dmp, s);
    s->s_drawn_rev = v->gv_frame_rev;

    dm_add_arrows(dmp, s);
    if (s->s_type_flags & BSG_SHAPE_AXES)
	dm_draw_scene_axes(dmp, s);
    if (s->s_type_flags & BSG_SHAPE_LABELS)
	dm_draw_label(dmp, s);
}

static void
_dm_scene_invalidate_item(void *dmp_ptr, const struct bsg_render_item *item,
			   unsigned int UNUSED(reason_mask))
{
    struct dm *dmp = (struct dm *)dmp_ptr;
    if (!dmp || !item || !item->node)
	return;
    dm_backend_invalidate_obj(dmp, item->node);
}

static void
_dm_scene_free_item(void *dmp_ptr, const struct bsg_render_item *item)
{
    struct dm *dmp = (struct dm *)dmp_ptr;
    if (!dmp || !item || !item->node)
	return;
    dm_backend_release_obj(dmp, item->node);
}

static unsigned int
_dm_scene_capabilities(void *UNUSED(dmp_ptr))
{
    return BSG_ADAPTER_CAP_TRANSPARENCY |
	   BSG_ADAPTER_CAP_WIREFRAME |
	   BSG_ADAPTER_CAP_SHADED |
	   BSG_ADAPTER_CAP_HUD |
	   BSG_ADAPTER_CAP_SORTED_ALPHA;
}


void
dm_draw_faceplate(struct bsg_view *v)
{
    static struct bsg_backend_adapter hud_adapter = {NULL, _dm_hud_draw_item, NULL, NULL, NULL};
    (void)_dm_hud_render_request(v, &hud_adapter);
}

void
dm_draw_label(struct dm *dmp, struct bsg_node *s)
{
    struct bsg_label *l = bsg_payload_text_get(bsg_node_get_payload(s));
    if (!l)
	return;

    /* set color */
    unsigned char r, g, b;
    bsg_material_get_rgb(s, &r, &g, &b);
    (void)dm_set_fg(dmp, r, g, b, 1, 1.0);

    point_t vpoint;
    MAT4X3PNT(vpoint, s->s_v->gv_model2view, l->p);

    // Check that we can calculate the bbox before drawing text
    vect2d_t bmin = V2INIT_ZERO;
    vect2d_t bmax = V2INIT_ZERO;
    (void)dm_hud_begin(dmp);
    int txt_ok = dm_string_bbox_2d(dmp, &bmin, &bmax, bu_vls_cstr(&l->label), vpoint[X], vpoint[Y], 1, 1);
    (void)dm_hud_end(dmp);

    // Have bbox - go ahead and write the label
    if (txt_ok == BRLCAD_OK) {
	(void)dm_hud_begin(dmp);
	(void)dm_draw_string_2d(dmp, bu_vls_cstr(&l->label), vpoint[X], vpoint[Y], 0, 1);
	(void)dm_hud_end(dmp);
    }

    if (!l->line_flag)
	return;

    point_t l3d = VINIT_ZERO;
    point_t mpt = VINIT_ZERO;

    if (txt_ok == BRLCAD_OK) {
	vect2d_t bmid;
	bmid[0] = (bmax[0] - bmin[0]) * 0.5 + bmin[0];
	bmid[1] = (bmax[1] - bmin[1]) * 0.5 + bmin[1];

	vect2d_t anchor = V2INIT_ZERO;
	if (l->anchor == BSG_ANCHOR_AUTO) {
	    fastf_t xvals[3];
	    fastf_t yvals[3];
	    xvals[0] = bmin[0];
	    xvals[1] = bmid[0];
	    xvals[2] = bmax[0];
	    yvals[0] = bmin[1];
	    yvals[1] = bmid[1];
	    yvals[2] = bmax[1];
	    fastf_t closest_dist = MAX_FASTF;
	    for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
		    point_t t3d, tpt;
		    if (bsg_screen_to_view(s->s_v, &t3d[0], &t3d[1], (int)xvals[i], (int)yvals[j]) < 0) {
			return;
		    }
		    t3d[2] = 0;
		    MAT4X3PNT(tpt, s->s_v->gv_view2model, t3d);
		    double dsq = DIST_PNT_PNT_SQ(tpt, l->target);
		    if (dsq < closest_dist) {
			V2SET(anchor, xvals[i], yvals[j]);
			closest_dist = dsq;
		    }
		}
	    }
	} else {
	    switch (l->anchor) {
		case BSG_ANCHOR_BOTTOM_LEFT:
		    V2SET(anchor, bmin[0], bmin[1]);
		    break;
		case BSG_ANCHOR_BOTTOM_CENTER:
		    V2SET(anchor, bmid[0], bmin[1]);
		    break;
		case BSG_ANCHOR_BOTTOM_RIGHT:
		    V2SET(anchor, bmax[0], bmin[1]);
		    break;
		case BSG_ANCHOR_MIDDLE_LEFT:
		    V2SET(anchor, bmin[0], bmid[1]);
		    break;
		case BSG_ANCHOR_MIDDLE_CENTER:
		    V2SET(anchor, bmid[0], bmid[1]);
		    break;
		case BSG_ANCHOR_MIDDLE_RIGHT:
		    V2SET(anchor, bmax[0], bmid[1]);
		    break;
		case BSG_ANCHOR_TOP_LEFT:
		    V2SET(anchor, bmin[0], bmax[1]);
		    break;
		case BSG_ANCHOR_TOP_CENTER:
		    V2SET(anchor, bmid[0], bmax[1]);
		    break;
		case BSG_ANCHOR_TOP_RIGHT:
		    V2SET(anchor, bmax[0], bmax[1]);
		    break;
		default:
		    bu_log("Unhandled anchor case: %d\n", l->anchor);
		    return;
	    }
	}
	bsg_screen_to_view(s->s_v, &l3d[0], &l3d[1], (int)anchor[0], (int)anchor[1]);
	MAT4X3PNT(mpt, s->s_v->gv_view2model, l3d);
    } else {
	VMOVE(mpt, l->p);
    }

    if (l->arrow) {
	dm_draw_arrow(dmp, mpt, l->target, s->s_os->s_arrow_tip_length, s->s_os->s_arrow_tip_width, 1.0);
    } else {
	dm_draw_line_3d(dmp, mpt, l->target);
    }
}

/* Phase 8 BSG render contract:
 *
 *   transparency_pass values for the BSG traversal helpers below:
 *     0 - draw all objects regardless of transparency (single-pass)
 *     1 - draw only opaque objects (s_os->transparency >= 1.0)
 *     2 - draw only transparent objects
 */
static void
_dm_draw_scene_obj_internal(struct dm *dmp,
			    struct bsg_node *s,
			    struct bsg_view *v,
			    int force_draw,
			    struct bsg_obj_settings *obj_settings,
			    int transparency_pass,
			    const fastf_t *cur_mat)
{
    if (!s || !v || (s->s_flag == DOWN && !force_draw))
	return;

    int do_force_draw = (force_draw || s->s_force_draw) ? 1 : 0;

    /* Phase 1 (BSG render contract): transparency-pass filter.  Note we
     * *do* still recurse into children — a non-leaf scene-obj may have
     * children with different transparency than the parent. */
    int pass_skip = 0;
    if (transparency_pass == 1 && s->s_os->transparency < 1.0)
	pass_skip = 1;
    if (transparency_pass == 2 && ZERO(s->s_os->transparency - 1.0))
	pass_skip = 1;

    /* Phase 5 (BSG render contract): bound-flag size-based culling — skip
     * very small geometry on a panning frame when the dm has set bound. */
    if (!pass_skip
	&& dm_get_bound_flag(dmp)
	&& !s->s_displayobj
	&& (s->s_type_flags & BSG_NODE_SHAPE)
	&& v->gv_isize > 0
	&& (s->s_size * v->gv_isize) < 0.001) {
	pass_skip = 1;
    }

    // Draw children. TODO - drawing children first may not
    // always be the desired behavior - might need interior and exterior
    // children tables to provide some control
    for (size_t i = 0; i < bsg_node_child_count(s); i++) {
	struct bsg_node *s_c = bsg_node_child_at(s, i);
	_dm_draw_scene_obj_internal(dmp, s_c, v, do_force_draw, obj_settings,
				    transparency_pass, cur_mat);
    }

    if (pass_skip)
	return;

    // Assign color attributes
    if (obj_settings) {
	dm_set_fg(dmp, obj_settings->color[0], obj_settings->color[1], obj_settings->color[2], 0, obj_settings->transparency);
    } else {
	if (bsg_appearance_is_highlighted(s)) {
	    dm_set_fg(dmp, 255, 255, 255, 0, s->s_os->transparency);
	} else if (s->s_os->color_override) {
	    dm_set_fg(dmp, s->s_os->color[0], s->s_os->color[1], s->s_os->color[2], 0, s->s_os->transparency);
	} else if (s->s_old.s_cflag) {
	    /* Phase 4 (BSG render contract): legacy "use the dm's geometry
	     * default colour" behaviour — drives objects that asked for it
	     * via dl_add_path/solid_set_color_info. */
	    unsigned char *gdc = dm_get_geometry_default_color(dmp);
	    dm_set_fg(dmp, gdc[0], gdc[1], gdc[2], 0, s->s_os->transparency);
	} else {
	    unsigned char sr, sg, sb;
	    bsg_material_get_rgb(s, &sr, &sg, &sb);
	    dm_set_fg(dmp, sr, sg, sb, 0, s->s_os->transparency);
	}
    }

    /* Phase 4 (BSG render contract): line-width fallback.  When the
     * per-object override is zero (or negative), use the dm's current
     * global linewidth so `set linewidth` propagates through. */
    int lw = s->s_os->s_line_width;
    if (lw <= 0)
	lw = dm_get_linewidth(dmp);
    dm_set_line_attr(dmp, lw, s->s_soldash);

    /* Phase 6 (BSG render contract): if this object is illuminated (edit
     * mode) and the view carries an edit-mode matrix override,
     * temporarily swap the modelview matrix so the object is drawn at
     * its edit-transformed position.  Restore the *current* accumulated
     * matrix afterwards (cur_mat) — falling back to gv_model2view when
     * we are not under a transform node. */
    int edit_mat_swapped = 0;
    if (bsg_appearance_is_highlighted(s) && v->gv_edit_mat) {
	dm_loadmatrix(dmp, v->gv_edit_mat, 0);
	edit_mat_swapped = 1;
    }

    // Primary object drawing.
    if (s->s_type_flags & BSG_OBJ_DB) {
	struct bsg_node *vo = s;
	bsg_log(1, "dm_draw_scene_obj - drawing %s[%s]", bu_vls_cstr(&vo->s_name), bu_vls_cstr(&v->gv_name));

	/* Phase 11 (drawing_stack_modernization): renderer-backend contract.
	 * dm_backend_draw_obj() routes through the dm's registered
	 * struct dm_backend_ops::draw_obj (e.g. gl_backend_ops::gl_draw_obj
	 * for the GL family) when present, and falls back to the legacy
	 * dm_impl::dm_draw_obj path otherwise.  The fallback keeps backends
	 * that have not (yet) registered Phase 11 ops rendering correctly. */
	dm_backend_draw_obj(dmp, vo);
    } else {
	dm_backend_draw_obj(dmp, s);
    }

    /* Phase 9.2 (BSG render contract): per-frame generation stamp.
     * Mirror the legacy dm_drawSolid behaviour and mark this object as
     * successfully rendered in the current frame, but via the v->gv_frame_rev
     * generation counter rather than s_flag = UP.  Callers (e.g. mged
     * dozoom counting drawn objects) test
     *   sp->s_drawn_rev == v->gv_frame_rev
     * to identify "drawn this frame" without needing a per-frame full-tree
     * reset of s_flag.  s_flag remains the persistent visibility bit
     * (DOWN means hidden); it is no longer toggled UP by the renderer. */
    s->s_drawn_rev = v->gv_frame_rev;

    if (edit_mat_swapped) {
	/* Phase 6 (BSG render contract): restore the accumulated matrix
	 * if we are under a transform node, otherwise restore the standard
	 * view matrix so subsequent objects are drawn in the right
	 * coordinate frame. */
	if (cur_mat)
	    dm_loadmatrix(dmp, (fastf_t *)cur_mat, 0);
	else
	    dm_loadmatrix(dmp, v->gv_model2view, 0);
    }

    dm_add_arrows(dmp, s);

    if (s->s_type_flags & BSG_SHAPE_AXES) {
	dm_draw_scene_axes(dmp, s);
    }

    if (s->s_type_flags & BSG_SHAPE_LABELS) {
	dm_draw_label(dmp, s);
    }
}

void
dm_draw_scene_obj(struct dm *dmp, struct bsg_node *s, struct bsg_view *v, int force_draw, struct bsg_obj_settings *obj_settings)
{
    /* Public single-pass API — preserves legacy behaviour: draw any
     * object regardless of transparency, restore gv_model2view after
     * any edit-matrix swap. */
    _dm_draw_scene_obj_internal(dmp, s, v, force_draw, obj_settings,
				/*transparency_pass=*/0, /*cur_mat=*/NULL);
}

// Phase 4-D (drawing_stack_modernization): BSG render traversal.
// Declared in dm/view.h as DM_EXPORT.  Defined here (in libdm) rather
// than in libbsg because the traversal calls dm_draw_scene_obj() which
// requires dm_* rendering functions — putting it here avoids a
// libbsg → libdm circular dependency.
//
// bsg_view_traverse syncs the scene root from the view's current draw
// state and then draws each child node using dm_draw_scene_obj, producing
// the same output as the legacy dl_* walk.

/* Phase D5: bsg_view_traverse is now a thin wrapper around
 * bsg_render_request_execute.  The old _bsg_view_traverse_impl traversal
 * has been removed; all traversal logic now lives in libbsg/render.c. */
void
bsg_view_traverse(struct bsg_view *v, void *root)
{
    bsg_log(3, "libdm:bsg_view_traverse");
    if (!v || !root)
	return;
    struct dm *dmp = (struct dm *)v->dmp;
    if (!dmp)
	return;

    static struct bsg_backend_adapter traverse_adapter = {
	_dm_scene_prepare_item,
	_dm_scene_draw_item,
	_dm_scene_invalidate_item,
	_dm_scene_free_item,
	_dm_scene_capabilities
    };

    struct bsg_render_request *req =
	bsg_render_request_create(v, (bsg_node *)root, dmp);
    if (!req)
	return;
    req->flags = BSG_RENDER_FLAG_VISIBLE_ONLY | BSG_RENDER_FLAG_PAYLOAD_DISPATCH;
    if (dm_get_transparency(dmp))
	req->flags |= BSG_RENDER_FLAG_SORTED_ALPHA;
    /* Phase D5/G7: hand the backend's geometry-default color to the render
     * settings so bsg_appearance_resolve can model the default-color layer. */
    if (req->settings) {
	unsigned char *gdc = dm_get_geometry_default_color(dmp);
	req->settings->geometry_default_color[0] = gdc[0];
	req->settings->geometry_default_color[1] = gdc[1];
	req->settings->geometry_default_color[2] = gdc[2];
    }
    req->adapter = &traverse_adapter;
    (void)bsg_render_request_execute(req);
    bsg_render_request_destroy(req);
}

// Phase D6 (drawing_modernization): all interactive visuals are expected to
// arrive as scene-graph payloads and overlay nodes before libdm traversal.
void
dm_draw_objs(struct bsg_view *v)
{
    bsg_log(3, "libdm:dm_draw_objs");

    struct dm *dmp = (struct dm *)v->dmp;
    if (!dmp) {
	bu_log("Warning - dm_draw_objs called when view has no associated display manager\n");
	return;
    }

    /* Phase 9.2 (drawing_stack_modernization B5): bump the frame generation
     * counter.  Every shape rendered below stamps s->s_drawn_rev := this
     * value, so callers can detect "drawn this frame" by simple equality
     * test against v->gv_frame_rev — replacing the legacy per-frame
     * full-tree "reset every s_flag to DOWN" sweep. */
    v->gv_frame_rev++;

    // This is the start of a draw cycle - start the stopwatch to time the
    // frame.  If the faceplate fps display is enabled, the faceplate draw at
    // the end of the cycle will need this start time.
    dmp->start_time = bu_gettime();

    // If we're drawing the framebuffer, that's the first order of business.
    // The rest of the drawing layers manipulate the OpenGL view and projection
    // matrices, but the framebuffer is always aligned to the view.  We also
    // can't have the zbuffer enabled or the fb image won't draw correctly.
    if (v->gv_s->gv_fb_mode && dm_get_fb(dmp)) {
	static struct bsg_backend_adapter framebuffer_adapter = {NULL, _dm_framebuffer_draw_item, NULL, NULL, NULL};
	(void)_dm_hud_render_request(v, &framebuffer_adapter);
	if (v->gv_s->gv_fb_mode == 1) {
	    // In overlay mode, it's just the fb - skip all the rest
	    return;
	}
    }

    // On to the scene objects - for drawing those we need the view matrix
    matp_t mat = v->gv_model2view;
    dm_loadmatrix(dmp, mat, 0);


    // Set up to render using current perspective settings
    if (SMALL_FASTF < v->gv_perspective)
	(void)dm_loadpmatrix(dmp, v->gv_pmat);
    else {
	(void)dm_loadpmatrix(dmp, NULL);
    }


    // Phase F (drawing_stack_modernization): bsg_root is now an alias for
    // gv_draw_root — no per-frame bsg_scene_root_sync rebuild is needed.
    // bsg_root->children IS gv_draw_root->children, maintained live by
    // draw/erase mutations.
    //
    // Phase V4 (drawing_stack_modernization): BSG_OBJ_VIEW producers now place
    // objects natively under BSG_NODE_VIEW_SCOPE nodes.  The legacy bridge and
    // VIEW_REF proxy mechanism (Phase V2) has been removed; the BSG traversal
    // below is the sole render path for both DB and view-only objects.
    //
    // When bsg_root is NULL (view not yet associated with a GED draw tree,
    // e.g. before the first draw command) there is no renderable content and
    // the block is skipped.
    if (v->bsg_root) {
	static struct bsg_backend_adapter scene_adapter = {
	    _dm_scene_prepare_item,
	    _dm_scene_draw_item,
	    _dm_scene_invalidate_item,
	    _dm_scene_free_item,
	    _dm_scene_capabilities
	};
	struct bsg_render_request *req = bsg_render_request_create(v, (bsg_node *)v->bsg_root, dmp);
	if (req) {
	    req->flags = BSG_RENDER_FLAG_VISIBLE_ONLY | BSG_RENDER_FLAG_PAYLOAD_DISPATCH;
	    if (dm_get_transparency(dmp))
		req->flags |= BSG_RENDER_FLAG_SORTED_ALPHA;
	    /* Phase D5/G7: hand the backend's geometry-default color to the
	     * render settings so bsg_appearance_resolve models the
	     * default-color layer and the scene adapter can read the resolved
	     * color directly from item->appearance.color. */
	    if (req->settings) {
		unsigned char *gdc = dm_get_geometry_default_color(dmp);
		req->settings->geometry_default_color[0] = gdc[0];
		req->settings->geometry_default_color[1] = gdc[1];
		req->settings->geometry_default_color[2] = gdc[2];
	    }
	    req->adapter = &scene_adapter;
	    (void)bsg_render_request_execute(req);
	    bsg_render_request_destroy(req);
	    (void)dm_set_depth_mask(dmp, 1);
	}

    }

    // Done with perspective/orthogonal drawing
    dm_pop_pmatrix(dmp);

    /* And finally, faceplate.  Set up matrices for HUD drawing, rather than 3D
     * scene drawing. */
    (void)dm_hud_begin(dmp);

    /* Draw faceplate elements based on their current enable/disable settings */
    dm_draw_faceplate(v);

    /* Restore non-HUD settings. */
    (void)dm_hud_end(dmp);
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
