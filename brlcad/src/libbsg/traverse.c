/*                    T R A V E R S E . C
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
/** @file libbsg/traverse.c
 *
 * @brief Scene-graph traversal engine.
 *
 * Implements @c libbsg_traverse() — a pre-order depth-first walk of
 * the scene graph that accumulates transforms, tracks the active camera,
 * honours separator state isolation, and handles LoD child selection.
 *
 * ### Traversal algorithm
 *
 *   1. Call the visitor on @p root.  If it returns non-zero → prune.
 *   2. Save state if @p root has @c BSG_NODE_SEPARATOR.
 *   3. Accumulate @p root->xform into @c state->xform.
 *   4. If @p root has @c BSG_NODE_CAMERA, set @c state->active_camera.
 *   5. If @p root has @c BSG_NODE_LOD_GROUP, select one child and recurse
 *      only into that child.  Otherwise recurse into all children.
 *   6. Restore state (step 2) if separator.
 *
 * No dynamic memory is allocated during traversal; state save/restore uses
 * a local copy on the C call stack.
 */

#include "common.h"

#include <string.h>   /* memcpy */
#include <math.h>     /* sqrt */

#include "bu/ptbl.h"
#include "bu/malloc.h"
#include "bn/mat.h"
#include "vmath.h"

#include "libbsg/libbsg.h"

/* ====================================================================== *
 * Traversal state                                                         *
 * ====================================================================== */

void
libbsg_traversal_state_init(libbsg_traversal_state *state)
{
    if (!state)
	return;
    MAT_IDN(state->xform);
    state->active_camera = NULL;
    state->depth         = 0;
    state->view          = NULL;
}

/* ====================================================================== *
 * LoD child selection                                                     *
 * ====================================================================== */

/**
 * @brief Select the appropriate LoD child for @p node.
 *
 * Returns the index into @p node->children to visit, or -1 if
 * @p node has no children.  Falls back to child[0] (highest detail)
 * when the view or switch-data is unavailable.
 */
static int
lod_select_child(const bsg_node *node, const libbsg_traversal_state *state)
{
    const libbsg_lod_switch_data *sw;
    size_t nchildren;
    const libbsg_view_params *vp;
    fastf_t eye_dist;

    nchildren = BU_PTBL_LEN(&node->children);
    if (nchildren == 0)
	return -1;

    /* Default to highest detail */
    if (!node->payload)
	return 0;

    sw = (const libbsg_lod_switch_data *)node->payload;
    if (sw->num_levels == 0 || !sw->distances)
	return 0;

    /* Need view parameters to compute eye distance */
    vp = state->view;
    if (!vp)
	return 0;

    /* Compute eye-to-origin distance in model space.
     *
     * In BRL-CAD's row-major matrix layout MAT4X3PNT computes:
     *   out[X] = m[0]*in[X] + m[1]*in[Y] + m[2]*in[Z]  + m[3]
     *   out[Y] = m[4]*in[X] + m[5]*in[Y] + m[6]*in[Z]  + m[7]
     *   out[Z] = m[8]*in[X] + m[9]*in[Y] + m[10]*in[Z] + m[11]
     * For in = (0,0,0) this yields (m[3], m[7], m[11]), which is the
     * translation component of view2model — i.e. the eye position in
     * model space.  This is equivalent to reading elements [3,7,11] of
     * the matrix directly. */
    {
	point_t eye, origin;
	VSET(origin, 0.0, 0.0, 0.0);
	MAT4X3PNT(eye, vp->camera.view2model, origin);
	eye_dist = DIST_PNT_PNT(eye, origin);
    }

    /* Find the first switch distance larger than our distance — that
     * gives us the detail level to use. */
    {
	size_t i;
	size_t nlev = sw->num_levels;
	size_t nswitches = (nlev > 1) ? (nlev - 1) : 0;

	for (i = 0; i < nswitches; i++) {
	    if (eye_dist < sw->distances[i])
		return (int)i;
	}
	/* Beyond last switch → use coarsest level */
	return (int)(nchildren - 1);
    }
}

/* ====================================================================== *
 * Main traversal                                                          *
 * ====================================================================== */

void
libbsg_traverse(bsg_node               *root,
		libbsg_traversal_state *state,
		libbsg_visit_fn         visit,
		void                   *user_data)
{
    libbsg_traversal_state saved;
    mat_t accumulated;
    int is_separator;
    int prune;

    if (!root || !state || !visit)
	return;

    /* --- pre-order visit -------------------------------------------- */
    prune = visit(root, state, user_data);
    if (prune)
	return;

    /* --- save state if this is a separator -------------------------- */
    is_separator = (root->type_flags & BSG_NODE_SEPARATOR) ? 1 : 0;
    if (is_separator)
	memcpy(&saved, state, sizeof(saved));

    /* --- accumulate transform --------------------------------------- */
    bn_mat_mul(accumulated, state->xform, root->xform);
    memcpy(state->xform, accumulated, sizeof(mat_t));

    /* --- update camera ---------------------------------------------- */
    if ((root->type_flags & BSG_NODE_CAMERA) && root->payload)
	state->active_camera = (const bsg_camera *)root->payload;

    /* --- recurse into children -------------------------------------- */
    state->depth++;

    if (root->type_flags & BSG_NODE_LOD_GROUP) {
	/* Visit only the selected LoD child */
	int idx = lod_select_child(root, state);
	if (idx >= 0 && (size_t)idx < BU_PTBL_LEN(&root->children)) {
	    bsg_node *child = (bsg_node *)BU_PTBL_GET(&root->children, (size_t)idx);
	    libbsg_traverse(child, state, visit, user_data);
	}
    } else {
	/* Visit all children */
	size_t i;
	for (i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	    bsg_node *child = (bsg_node *)BU_PTBL_GET(&root->children, i);
	    libbsg_traverse(child, state, visit, user_data);
	}
    }

    state->depth--;

    /* --- restore state if this was a separator ---------------------- */
    if (is_separator)
	memcpy(state, &saved, sizeof(saved));
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
