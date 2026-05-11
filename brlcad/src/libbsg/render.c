/*                      R E N D E R . C
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
/** @file libbsg/render.c
 *
 * Phase 8 render action — BSG structural traversal with renderer ops.
 */

#include "common.h"

#include <string.h>

#include "bn.h"
#include "bu/malloc.h"
#include "bv/defines.h"

#include "bsg/appearance.h"
#include "bsg/camera.h"
#include "bsg/lod_ops.h"
#include "bsg/material.h"
#include "bsg/node.h"
#include "bsg/payload.h"
#include "bsg/render.h"
#include "bsg/selection.h"
#include "bsg/view_scope.h"

/* ---------------------------------------------------------------------- */
/* No-op renderer                                                           */
/* ---------------------------------------------------------------------- */

const struct bsg_renderer_ops bsg_renderer_noop = {
    NULL, /* begin_frame   */
    NULL, /* end_frame     */
    NULL, /* set_camera    */
    NULL, /* push_transform */
    NULL, /* pop_transform */
    NULL, /* set_material  */
    NULL, /* set_appearance */
    NULL, /* draw_payload  */
    NULL, /* draw_overlay  */
    NULL, /* draw_image_layer */
    NULL, /* set_depth_mask */
    NULL  /* query_capability */
};


/* ---------------------------------------------------------------------- */
/* Internal traversal                                                       */
/* ---------------------------------------------------------------------- */

/*
 * Recursively visit BSG structural nodes, resolving material/appearance/
 * selection for drawable leaf nodes and invoking renderer ops.
 *
 * Parameters:
 *   ra          - the render action (ops, renderer_data, view)
 *   node        - current node being visited
 *   parent_xform - accumulated world transform from the parent (model space)
 *   pass        - BSG_RENDER_PASS_* for the current traversal pass
 *   is_root_iter - non-zero when iterating the root node's direct children
 *                  (used to gate the independent-root skip check in libdm;
 *                  libbsg itself does not implement that check)
 */
static void
_bsg_render_traverse(struct bsg_render_action *ra,
		     bsg_node *node,
		     const mat_t parent_xform,
		     int pass)
{
    if (!node)
	return;

    const struct bsg_renderer_ops *ops = ra->ops;
    void *data = ra->renderer_data;
    struct bview *v = ra->view;
    unsigned long long pflags = 0;

    /* Skip non-drawable structural meta-nodes. */
    if (bsg_node_has_kind(node, BSG_NODE_SENSOR))
	return;
    if (bsg_node_has_kind(node, BSG_NODE_VIEW_BRIDGE))
	return;

    /* Phase V1 (view-scope): skip nodes scoped to a different view.
     * NULL s_v means "shared" (visible to all views).  When visible,
     * recurse into children — the scope node itself has no geometry. */
    if (bsg_node_has_kind(node, BSG_NODE_VIEW_SCOPE)) {
	if (!bsg_view_scope_visible(node, v))
	    return;
	for (size_t i = 0; i < bsg_node_child_count(node); i++) {
	    bsg_node *c = bsg_node_child(node, i);
	    _bsg_render_traverse(ra, c, parent_xform, pass);
	}
	return;
    }

    /* Phase L0 (LoD): render only the active LoD level. */
    if (bsg_node_has_kind(node, BSG_NODE_LOD)) {
	int nlevels = bsg_lod_node_level_count(node);
	if (nlevels <= 0)
	    return;
	int active = bsg_lod_node_active_level(node, v);
	if (active < 0 || active >= nlevels)
	    active = 0;
	bsg_node *child = bsg_node_child(node, (size_t)active);
	if (child)
	    _bsg_render_traverse(ra, child, parent_xform, pass);
	return;
    }

    /* Phase 8B (transform): push accumulated matrix, recurse, pop. */
    if (bsg_node_has_kind(node, BSG_NODE_TRANSFORM)) {
	mat_t new_xform;
	mat_t nmat;
	bsg_node_transform_get(node, nmat);
	bn_mat_mul(new_xform, parent_xform, nmat);

	if (ops->push_transform)
	    ops->push_transform(data, new_xform, parent_xform);

	for (size_t i = 0; i < bsg_node_child_count(node); i++) {
	    bsg_node *c = bsg_node_child(node, i);
	    _bsg_render_traverse(ra, c, new_xform, pass);
	}

	if (ops->pop_transform)
	    ops->pop_transform(data, parent_xform);
	return;
    }

    /* Phase 9A: overlay payload hook.  Overlay payloads are rendered once
     * (single pass or opaque pass) through draw_overlay when available. */
    pflags = bsg_node_get_payload_type(node);
    if (pflags & BSG_PAYLOAD_OVERLAY) {
	if (pass != BSG_RENDER_PASS_TRANSPARENT) {
	    if (ops->draw_overlay) {
		ops->draw_overlay(data, (bsg_node *)node, v);
	    } else if (ops->draw_payload) {
		ops->draw_payload(data, node, v, parent_xform, pass);
	    }
	}
	return;
    }

    /* ------------------------------------------------------------------ */
    /* Drawable node: resolve BSG material/appearance/selection (Phase 8C) */
    /* ------------------------------------------------------------------ */
    if (!bsg_node_visible(node) && !bsg_node_force_draw(node))
	return;

    struct bsg_material   mat;
    struct bsg_appearance app;
    struct bsg_appearance legacy_app;
    int have_mat = bsg_node_material_get(node, &mat);
    int have_app = bsg_node_appearance_get(node, &app);
    /* Preserve legacy transparency fallback when no explicit BSG appearance/material is set. */
    bsg_appearance_from_legacy_obj_settings(node, &legacy_app);
    /* Transparency precedence is explicit: appearance > material > legacy. */
    fastf_t obj_transparency = legacy_app.transparency;
    if (have_mat)
	obj_transparency = mat.transparency;
    if (have_app)
	obj_transparency = app.transparency;

    /* Phase 6D: BSG "active" selection first; legacy s_iflag fallback. */
    int is_highlighted =
	(v && v->bsg_root &&
	 bsg_node_is_selected((const bsg_node *)v->bsg_root, node, "active")) ||
	/* Legacy fallback while s_iflag compatibility remains in transition. */
	(((const struct bv_scene_obj *)node)->s_iflag == UP);

    /* Invoke renderer ops with resolved BSG state. */
    if (ops->set_material)
	ops->set_material(data, node,
			  &mat, have_mat, is_highlighted, obj_transparency);

    if (ops->set_appearance)
	ops->set_appearance(data, node, &app, have_app);

    if (ops->draw_payload)
	ops->draw_payload(data, node, v, parent_xform, pass);
}


/* ---------------------------------------------------------------------- */
/* Public API                                                               */
/* ---------------------------------------------------------------------- */

void
bsg_render_action_init(struct bsg_render_action *ra,
		       const struct bsg_renderer_ops *ops,
		       void *renderer_data)
{
    if (!ra)
	return;
    /* Always zero-initialize so callers can rely on unset fields being NULL/0. */
    memset(ra, 0, sizeof(*ra));
    if (!ops)
	return;
    ra->ops           = ops;
    ra->renderer_data = renderer_data;
    ra->view          = NULL;
}

void
bsg_render_action_set_view(struct bsg_render_action *ra, struct bview *v)
{
    if (!ra)
	return;
    ra->view = v;
}

int
bsg_render_action_apply(struct bsg_render_action *ra, bsg_node *root)
{
    if (!ra || !root)
	return 0;

    const struct bsg_renderer_ops *ops = ra->ops;
    void *data = ra->renderer_data;
    struct bview *v = ra->view;

    /* Camera snapshot — derived from the active view when available. */
    if (v && ops->set_camera) {
	struct bsg_camera_snapshot cam;
	bsg_camera_snapshot_from_bview(&cam, v);
	ops->set_camera(data, &cam);
    }

    if (ops->begin_frame)
	ops->begin_frame(data, v);

    /* Phase 9D: framebuffer/image-layer hook before scene traversal.
     * Renderers may return 0 to skip scene drawing (overlay-only mode). */
    int do_scene = 1;
    if (ops->draw_image_layer)
	do_scene = ops->draw_image_layer(data, root, v);
    if (!do_scene) {
	if (ops->end_frame)
	    ops->end_frame(data, v);
	return 1;
    }

    /* Use gv_model2view as the initial accumulated transform so that
     * transform-node matrix computations start from the correct base,
     * matching the behaviour of _bsg_view_traverse_impl in libdm. */
    mat_t initial_xform;
    if (v)
	MAT_COPY(initial_xform, v->gv_model2view);
    else
	MAT_IDN(initial_xform);

    /* Determine whether to use two-pass transparency rendering. */
    int has_transparency =
	(ops->query_capability)
	? ops->query_capability(data, BSG_RENDERER_CAP_TRANSPARENCY)
	: 0;

    if (has_transparency) {
	/* --- Opaque pass: draw objects with transparency == 1.0 --- */
	for (size_t i = 0; i < bsg_node_child_count(root); i++) {
	    bsg_node *c = bsg_node_child(root, i);
	    _bsg_render_traverse(ra, c, initial_xform, BSG_RENDER_PASS_OPAQUE);
	}

	/* Disable depth writes for the transparent pass. */
	if (ops->set_depth_mask)
	    ops->set_depth_mask(data, 0);

	/* --- Transparent pass: draw objects with transparency < 1.0 --- */
	for (size_t i = 0; i < bsg_node_child_count(root); i++) {
	    bsg_node *c = bsg_node_child(root, i);
	    _bsg_render_traverse(ra, c, initial_xform, BSG_RENDER_PASS_TRANSPARENT);
	}

	/* Restore depth writes. */
	if (ops->set_depth_mask)
	    ops->set_depth_mask(data, 1);

    } else {
	/* Single-pass: all objects regardless of transparency. */
	for (size_t i = 0; i < bsg_node_child_count(root); i++) {
	    bsg_node *c = bsg_node_child(root, i);
	    _bsg_render_traverse(ra, c, initial_xform, BSG_RENDER_PASS_ALL);
	}
    }

    if (ops->end_frame)
	ops->end_frame(data, v);

    return 1;
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
