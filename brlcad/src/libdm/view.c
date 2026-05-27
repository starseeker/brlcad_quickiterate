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

#include "bu/str.h"
#include "bu/time.h"
#include "bu/units.h"
#include "bu/vls.h"
#include "bn.h"
#include "bsg/defines.h"
#include "bsg/appearance.h"
#include "bsg/hud.h"
#include "bsg/material.h"
#include "bsg/node.h"
#include "bsg/lod.h"
#include "bsg/payload_typed.h"
#include "bsg/util.h"
#include "bsg/lod_ops.h"
#include "bsg/visit.h"
#include "bsg/view_scope.h"
#include "dm.h"

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

static int
_independent_root_skip_child(struct bsg_node *s)
{
    if (!s)
	return 1;
    if (s->s_type_flags & BSG_NODE_VIEW_SCOPE)
	return 0;
    if (!BU_VLS_IS_INITIALIZED(&s->s_name))
	return 1;
    return BU_STR_EQUAL("_overlays", bu_vls_cstr(&s->s_name)) ? 0 : 1;
}

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

void
dm_draw_faceplate(struct bsg_view *v)
{
    struct dm *dmp = (struct dm *)v->dmp;

    /* Phase D4 (drawing_modernization): synchronize faceplate settings into
     * the BSG HUD scene tree before drawing.  This populates gv_hud_root with
     * one child node per enabled feature, ordered by render phase.  Actual
     * rasterisation still happens via the dm_* calls below; the HUD tree
     * records the *what* and *order* while libdm owns the *how*. */
    bsg_hud_sync(v);

    /* Center dot */
    if (v->gv_s->gv_center_dot.gos_draw) {
	(void)dm_set_fg(dmp,
			v->gv_s->gv_center_dot.gos_line_color[0],
			v->gv_s->gv_center_dot.gos_line_color[1],
			v->gv_s->gv_center_dot.gos_line_color[2],
			1, 1.0);
	(void)dm_draw_point_2d(dmp, 0.0, 0.0);
    }

    /* Model axes */
    if (v->gv_s->gv_model_axes.draw) {
	point_t map;
	point_t save_map;

	VMOVE(save_map, v->gv_s->gv_model_axes.axes_pos);
	VSCALE(map, v->gv_s->gv_model_axes.axes_pos, v->gv_local2base);
	MAT4X3PNT(v->gv_s->gv_model_axes.axes_pos, v->gv_model2view, map);

	dm_draw_hud_axes(dmp,
		     v->gv_size,
		     v->gv_rotation,
		     &v->gv_s->gv_model_axes);

	VMOVE(v->gv_s->gv_model_axes.axes_pos, save_map);
    }

    /* View axes */
    if (v->gv_s->gv_view_axes.draw) {
	int width, height;
	fastf_t inv_aspect;
	fastf_t save_ypos;

	save_ypos = v->gv_s->gv_view_axes.axes_pos[Y];
	width = dm_get_width(dmp);
	height = dm_get_height(dmp);
	inv_aspect = (fastf_t)height / (fastf_t)width;
	v->gv_s->gv_view_axes.axes_pos[Y] = save_ypos * inv_aspect;
	dm_draw_hud_axes(dmp,
		     v->gv_size,
		     v->gv_rotation,
		     &v->gv_s->gv_view_axes);

	v->gv_s->gv_view_axes.axes_pos[Y] = save_ypos;
    }


    /* View scale - TODO view_scale needs its own text color */
    if (v->gv_s->gv_view_scale.gos_draw)
	dm_draw_scale(dmp,
		      v->gv_size*v->gv_base2local,
		      bu_units_string(1/v->gv_base2local),
		      v->gv_s->gv_view_scale.gos_line_color,
		      v->gv_s->gv_view_params.color);


    /* Draw the angle distance cursor */
    if (v->gv_s->gv_adc.draw)
	dm_draw_adc(dmp, &(v->gv_s->gv_adc), v->gv_view2model, v->gv_model2view);

    /* Draw grid */
    if (v->gv_s->gv_grid.draw) {
	dm_draw_grid(dmp, &v->gv_s->gv_grid, v->gv_scale, v->gv_model2view, v->gv_base2local);
    }

    /* Draw rect */
    if (v->gv_s->gv_rect.draw && v->gv_s->gv_rect.line_width)
	dm_draw_rect(dmp, &v->gv_s->gv_rect);

    /* View parameters - drawn last so the FPS incorporates as much as possible
     * of the drawing work. */
    if (v->gv_s->gv_view_params.draw) {

	// Save current font size
	int ofontsize = dm_get_fontsize(dmp);
	// Set font size for params
	dm_set_fontsize(dmp, v->gv_s->gv_view_params.font_size);

	struct bu_vls vls = BU_VLS_INIT_ZERO;
	point_t center;
	char *ustr = (char *)bu_units_string(v->gv_local2base);
	MAT_DELTAS_GET_NEG(center, v->gv_center);
	VSCALE(center, center, v->gv_base2local);
	int64_t elapsed_time = bu_gettime() - (dmp)->start_time;
	/* Only use reasonable measurements */
	if (elapsed_time > 10LL && elapsed_time < 30000000LL) {
	    /* Smoothly transition to new speed */
	    v->gv_s->gv_frametime = 0.9 * v->gv_s->gv_frametime + 0.1 * elapsed_time / 1000000LL;
	}

	struct bsg_params_state *ps = &v->gv_s->gv_view_params;
	if (ps->draw_size) {
	    if (bu_vls_strlen(&vls) > 0)
		bu_vls_printf(&vls, " ");
	    bu_vls_printf(&vls, "size[%s]: %.2f", ustr, v->gv_size * v->gv_base2local);
	}
	if (ps->draw_center) {
	    if (bu_vls_strlen(&vls) > 0)
		bu_vls_printf(&vls, " ");
	    bu_vls_printf(&vls, "center[%s]: (%.2f, %.2f, %.2f)", ustr, V3ARGS(center));
	}
	if (ps->draw_az) {
	    if (bu_vls_strlen(&vls) > 0)
		bu_vls_printf(&vls, " ");
	    bu_vls_printf(&vls, "az:%.2f", v->gv_aet[0]);
	}
	if (ps->draw_el) {
	    if (bu_vls_strlen(&vls) > 0)
		bu_vls_printf(&vls, " ");
	    bu_vls_printf(&vls, "el:%.2f", v->gv_aet[1]);
	}
	if (ps->draw_tw) {
	    if (bu_vls_strlen(&vls) > 0)
		bu_vls_printf(&vls, " ");
	    bu_vls_printf(&vls, "tw:%.2f", v->gv_aet[2]);
	}
	if (ps->draw_fps) {
	    if (bu_vls_strlen(&vls) > 0)
		bu_vls_printf(&vls, " ");
	    bu_vls_printf(&vls, "FPS:%.2f", 1/v->gv_s->gv_frametime);
	}

	// TODO - really should put a rectangle behind this to ensure visibility...

	(void)dm_set_fg(dmp,
			v->gv_s->gv_view_params.color[0],
			v->gv_s->gv_view_params.color[1],
			v->gv_s->gv_view_params.color[2],
			1, 1.0);
	(void)dm_draw_string_2d(dmp, bu_vls_addr(&vls), -0.98, -0.965, 10, 0);
	bu_vls_free(&vls);

	// Restore previous font setting
	dm_set_fontsize(dmp, ofontsize);
    }
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
	if (l->anchor == BV_ANCHOR_AUTO) {
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
		case BV_ANCHOR_BOTTOM_LEFT:
		    V2SET(anchor, bmin[0], bmin[1]);
		    break;
		case BV_ANCHOR_BOTTOM_CENTER:
		    V2SET(anchor, bmid[0], bmin[1]);
		    break;
		case BV_ANCHOR_BOTTOM_RIGHT:
		    V2SET(anchor, bmax[0], bmin[1]);
		    break;
		case BV_ANCHOR_MIDDLE_LEFT:
		    V2SET(anchor, bmin[0], bmid[1]);
		    break;
		case BV_ANCHOR_MIDDLE_CENTER:
		    V2SET(anchor, bmid[0], bmid[1]);
		    break;
		case BV_ANCHOR_MIDDLE_RIGHT:
		    V2SET(anchor, bmax[0], bmid[1]);
		    break;
		case BV_ANCHOR_TOP_LEFT:
		    V2SET(anchor, bmin[0], bmax[1]);
		    break;
		case BV_ANCHOR_TOP_CENTER:
		    V2SET(anchor, bmid[0], bmax[1]);
		    break;
		case BV_ANCHOR_TOP_RIGHT:
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
    if (s->s_type_flags & BV_DB_OBJS) {
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

    if (s->s_type_flags & BV_AXES) {
	dm_draw_scene_axes(dmp, s);
    }

    if (s->s_type_flags & BV_LABELS) {
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

/* Internal traversal — supports transparency-pass filtering and the
 * accumulated transform-stack matrix.  Public bsg_view_traverse() and
 * dm_draw_objs() both delegate here. */
static void
_bsg_view_traverse_impl(struct bsg_view *v, void *root,
			int transparency_pass,
			const fastf_t *cur_mat)
{
    if (!v || !root)
	return;

    struct dm *dmp = (struct dm *)v->dmp;
    if (!dmp)
	return;

    struct bsg_node *r = (struct bsg_node *)root;
    int independent_root = 0;
    if (bsg_view_is_independent(v) && r == (struct bsg_node *)v->bsg_root) {
	independent_root = 1;
    }
    for (size_t i = 0; i < bsg_node_child_count(r); i++) {
	struct bsg_node *s = bsg_node_child_at(r, i);
	if (!s)
	    continue;

	/* Phase 6: skip sensor nodes — they are not drawable */
	if (s->s_type_flags & BSG_NODE_SENSOR)
	    continue;

	if (independent_root && _independent_root_skip_child(s))
	    continue;

	/* Phase V1 (view-scope): skip nodes scoped to a different view.
	 * A NULL owner means "shared" (visible to all views); a non-NULL
	 * owner means view-private (only visible to the owning view).
	 * When the scope is visible, recurse into children and continue — the
	 * scope node itself contributes no geometry. */
	if (s->s_type_flags & BSG_NODE_VIEW_SCOPE) {
	    if (s->s_v != NULL && s->s_v != v)
		continue; /* wrong view — skip entire subtree */
	    _bsg_view_traverse_impl(v, s, transparency_pass, cur_mat);
	    continue;
	}

	/* Phase V4: BSG_NODE_VIEW_REF and BSG_NODE_VIEW_BRIDGE were removed
	 * when the legacy ptbl bridge was retired.  Skip any stale nodes
	 * from pre-V4 trees so the traversal stays correct. */
	if (s->s_type_flags & BSG_NODE_VIEW_BRIDGE)
	    continue;

	/* Phase L0 (LoD redesign): for BSG_NODE_LOD nodes, render only
	 * the child selected by the per-view cursor.  bsg_lod_update()
	 * (called once before this traversal from dm_draw_objs) has already
	 * run select_level/activate_level, so we just need to read the
	 * cursor and recurse into the right child.  When no level has been
	 * selected yet (level == -1) we fall through to the child at index 0
	 * as a safe default. */
	if (s->s_type_flags & BSG_NODE_LOD) {
	    int active = bsg_lod_node_active_level((bsg_node *)s, v);
	    int nlevels = bsg_lod_node_level_count((bsg_node *)s);
	    if (nlevels > 0) {
		if (active < 0 || active >= nlevels)
		    active = 0;
		struct bsg_node *child = bsg_node_child_at(s, active);
		if (child)
		    _bsg_view_traverse_impl(v, child,
					    transparency_pass, cur_mat);
	    }
	    continue;
	}

	/* Phase 6 (BSG render contract): handle transform nodes — push
	 * matrix, recurse, pop.  Carry the new accumulated matrix
	 * through to dm_draw_scene_obj so that an s_iflag==UP child
	 * under this transform restores back to the transform after the
	 * gv_edit_mat swap, not to gv_model2view. */
	if (s->s_type_flags & BSG_NODE_TRANSFORM) {
	    mat_t save_mat;
	    if (cur_mat)
		MAT_COPY(save_mat, cur_mat);
	    else
		MAT_COPY(save_mat, v->gv_model2view);
	    mat_t new_mat;
	    bn_mat_mul(new_mat, save_mat, s->s_mat);
	    dm_loadmatrix(dmp, new_mat, 0);
	    _bsg_view_traverse_impl(v, s, transparency_pass, new_mat);
	    dm_loadmatrix(dmp, save_mat, 0);
	    continue;
	}

	_dm_draw_scene_obj_internal(dmp, s, v, s->s_force_draw,
				    (s->s_inherit_settings) ? s->s_os : NULL,
				    transparency_pass, cur_mat);
    }
}

void
bsg_view_traverse(struct bsg_view *v, void *root)
{
    bsg_log(3, "libdm:bsg_view_traverse");
    _bsg_view_traverse_impl(v, root, /*transparency_pass=*/0, /*cur_mat=*/NULL);
}

// To allow completely custom modes like the sketch editor to be defined by
// applications in terms of libdm, we allow an optional callback that can be
// passed in to this function.  If non-NULL, that function will be called in
// lieu of the standard logic below.
//
// Current thought is that this will allow the definition of a sketch editor
// (or, for that matter, any custom visual) in libdm terms rather than in Tk or
// even in OpenGL (although the latter may be what the custom function does
// under the hood, if it doesn't want to define itself in libdm terms - libdm
// doesn't guarantee raw OpenGL drawing is supported, but the dmp should
// provide enough information for the calling app to know if that is possible.)
void
dm_draw_objs(struct bsg_view *v, void (*dm_draw_custom)(struct bsg_view *, void *), void *u_data)
{
    bsg_log(3, "libdm:dm_draw_objs");
    if (dm_draw_custom) {
	(*dm_draw_custom)(v, u_data);
	return;
    }

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
	int zbuff_restore = dm_get_zbuffer(dmp);
	dm_set_zbuffer(dmp, 0);
	/* Phase A2 (ert reliability): clamp the fb_refresh region to the
	 * intersection of the framebuffer canvas and the dm widget so an
	 * in-flight rt-driven fb that hasn't yet matched the resized
	 * widget cannot induce out-of-bounds reads or stretched/tiled
	 * artefacts.  When dm == fb (steady state) this is a no-op. */
	struct fb *fbp = dm_get_fb(dmp);
	int rw = dm_get_width(dmp);
	int rh = dm_get_height(dmp);
	if (fbp) {
	    int fbw = fb_getwidth(fbp);
	    int fbh = fb_getheight(fbp);
	    if (fbw > 0 && fbw < rw) rw = fbw;
	    if (fbh > 0 && fbh < rh) rh = fbh;
	}
	if (rw > 0 && rh > 0) {
	    fb_refresh(fbp, 0, 0, rw, rh);
	}
	if (zbuff_restore)
	    dm_set_zbuffer(dmp, 1);
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
    // Phase V4 (drawing_stack_modernization): BV_VIEW_OBJS producers now place
    // objects natively under BSG_NODE_VIEW_SCOPE nodes.  The legacy bridge and
    // VIEW_REF proxy mechanism (Phase V2) has been removed; the BSG traversal
    // below is the sole render path for both DB and view-only objects.
    //
    // When bsg_root is NULL (view not yet associated with a GED draw tree,
    // e.g. before the first draw command) there is no renderable content and
    // the block is skipped.
    if (v->bsg_root) {
	/* Phase L2 (LoD redesign): run the LoD update pass once per frame
	 * before the render traversal.  Visits every BSG_NODE_LOD node in
	 * the tree and, for any that are stale for this view, calls
	 * select_level then activate_level.  This is a no-op on trees that
	 * contain no BSG_NODE_LOD nodes (i.e. all existing production trees
	 * until Phase L3 migrates producers). */
	bsg_lod_update((bsg_node *)v->bsg_root, v);

	/* Phase 1 (BSG render contract): two-pass transparency render.
	 * Opaque first with depth writes on, then transparent with depth
	 * writes off.  When the dm doesn't support / want transparency
	 * sorting, fall back to a single all-objects pass. */
	if (dm_get_transparency(dmp)) {
	    /* Opaque pass */
	    _bsg_view_traverse_impl(v, v->bsg_root, /*transparency_pass=*/1, NULL);
	    /* disable depth writes for the transparent pass so back-to-front
	     * blending doesn't stomp the opaque depth buffer */
	    (void)dm_set_depth_mask(dmp, 0);
	    _bsg_view_traverse_impl(v, v->bsg_root, /*transparency_pass=*/2, NULL);
	    (void)dm_set_depth_mask(dmp, 1);
	} else {
	    _bsg_view_traverse_impl(v, v->bsg_root, /*transparency_pass=*/0, NULL);
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
