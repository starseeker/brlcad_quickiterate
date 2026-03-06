/*               D M _ A D A P T E R . C
 * BRL-CAD
 *
 * Copyright (c) 2024-2025 United States Government as represented by
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
/**
 * @file libbsg_dm/dm_adapter.c
 *
 * @brief Display-manager adapter for the libbsg scene graph.
 *
 * This file is the ONLY place in libbsg_dm that includes the legacy
 * display-manager headers alongside the libbsg headers.  It implements
 * the visitor callback that drives scene rendering via dm_* calls.
 *
 * ### Bridging bv_vlist and libbsg_shape.vlist
 *
 * The display manager's @c dm_draw_vlist() accepts @c struct @c bv_vlist *,
 * which starts with @c struct @c bu_list l.  The @c libbsg_shape.vlist field
 * is also a @c struct @c bu_list head.  The cast
 * @c (struct @c bv_vlist *)&s->vlist is therefore safe: @c dm_draw_vlist
 * walks the list via @c BU_LIST_FOR, and the first element of a
 * @c bv_vlist chain is itself a @c bu_list header, so the chain pointer
 * is valid.  This is the same cast used throughout @c dm-generic.c.
 */

#include "common.h"

#include <string.h>   /* memset */

/* Include libbsg first to get libbsg_shape, bsg_node, etc. */
#include "libbsg/libbsg.h"
#include "libbsg_dm/libbsg_dm.h"

/* Now include the dm and bv headers — this is allowed in this file
 * because libbsg/libbsg.h no longer defines conflicting struct names. */
#include "dm.h"
#include "bv/vlist.h"
#include "bu/list.h"
#include "bn/mat.h"

/* ====================================================================== *
 * Internal visitor state                                                  *
 * ====================================================================== */

/**
 * @brief Context passed to the draw visitor through user_data.
 */
struct _dm_draw_ctx {
    struct dm                   *dmp;     /**< display manager             */
    const libbsg_dm_draw_params *params;  /**< draw params (colour, width) */
    int                          ndrawn;  /**< running count of shapes drawn */
};

/* Default draw parameters used when the caller passes NULL. */
static const libbsg_dm_draw_params _default_params = {
    255, 255, 255,   /* r, g, b — white */
    1                /* line_width */
};

/* ====================================================================== *
 * Visitor callback                                                        *
 * ====================================================================== */

/**
 * @brief Pre-order visitor called for every node by libbsg_traverse().
 *
 * - **Camera node**: loads the camera matrices into the DM.
 * - **Shape node**: sets colour, loads the accumulated model transform,
 *   then draws each vlist segment via dm_draw_vlist().
 * - **Other nodes** (separator, transform, LoD group): no-op — return 0
 *   so that the traversal engine recurses into children.
 */
static int
_dm_draw_visitor(bsg_node                     *node,
		 const libbsg_traversal_state *state,
		 void                         *user_data)
{
    struct _dm_draw_ctx *ctx = (struct _dm_draw_ctx *)user_data;

    /* Camera node: push camera matrices into the DM. */
    if (node->type_flags & LIBBSG_NODE_CAMERA) {
	if (node->payload)
	    libbsg_dm_load_camera(ctx->dmp, (const libbsg_camera *)node->payload);
	return 0; /* recurse into children */
    }

    /* Shape node: draw if it has geometry. */
    if (node->type_flags & LIBBSG_NODE_SHAPE) {
	libbsg_shape *s = (libbsg_shape *)node;

	if (BU_LIST_NON_EMPTY(&s->vlist)) {
	    const libbsg_dm_draw_params *p = ctx->params;

	    /* Colour: use shape override if set, else use params default. */
	    unsigned char r = (unsigned char)(s->s_cflag ? s->s_color[0] : (unsigned char)p->r);
	    unsigned char g = (unsigned char)(s->s_cflag ? s->s_color[1] : (unsigned char)p->g);
	    unsigned char b = (unsigned char)(s->s_cflag ? s->s_color[2] : (unsigned char)p->b);

	    dm_set_fg(ctx->dmp, r, g, b, 1, 1.0);
	    dm_set_line_attr(ctx->dmp, p->line_width, 0);

	    /* Load the accumulated model-to-view transform for this shape. */
	    dm_loadmatrix(ctx->dmp, (fastf_t *)state->xform, 0);

	    /* dm_draw_vlist takes struct bv_vlist * whose first field is
	     * bu_list l.  libbsg_shape.vlist is also a bu_list head with
	     * bv_vlist segments on it, so this cast is safe. */
	    if (dm_draw_vlist(ctx->dmp,
			     (struct bv_vlist *)(&s->vlist)) == BRLCAD_OK)
		ctx->ndrawn++;
	}
	return 1; /* shape leaves have no geometry children to recurse into */
    }

    /* All other node types: just recurse. */
    return 0;
}

/* ====================================================================== *
 * Public API                                                              *
 * ====================================================================== */

int
libbsg_dm_load_camera(struct dm *dmp, const libbsg_camera *cam)
{
    if (!dmp || !cam)
	return -1;

    /* Push perspective matrix (column-major). */
    dm_loadpmatrix(dmp, cam->pmat);

    /* Push model-to-view matrix. */
    dm_loadmatrix(dmp, (fastf_t *)cam->model2view, 0);

    return 0;
}

int
libbsg_dm_draw_scene(struct dm                   *dmp,
		     bsg_node                    *root,
		     const libbsg_view_params    *view,
		     const libbsg_dm_draw_params *params)
{
    struct _dm_draw_ctx ctx;
    libbsg_traversal_state state;

    if (!dmp || !root)
	return -1;

    ctx.dmp    = dmp;
    ctx.params = params ? params : &_default_params;
    ctx.ndrawn = 0;

    libbsg_traversal_state_init(&state);
    state.view = view;

    /* If we have explicit view params with a camera, pre-load it. */
    if (view)
	libbsg_dm_load_camera(dmp, &view->camera);

    libbsg_traverse(root, &state, _dm_draw_visitor, &ctx);

    return ctx.ndrawn;
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
