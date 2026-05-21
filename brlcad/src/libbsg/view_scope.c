/*               V I E W _ S C O P E . C
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
/** @file libbsg/view_scope.c
 *
 * Phase V1 (drawing_stack_modernization):
 * BSG_NODE_VIEW_SCOPE lifecycle — create / visible / destroy.
 *
 * A view-scope node is a non-drawable container whose children are skipped
 * during render traversal for views that do not own this scope.  The owner
 * is stored in the node's s_v slot (NULL = shared, visible to all views).
 */

#include "common.h"

#include "bsg/defines.h"
#include "bsg/node.h"
#include "bsg/view_scope.h"


bsg_node *
bsg_view_scope_create(struct bview *v)
{
    struct bv_scene_obj *s = (struct bv_scene_obj *)bsg_node_create(v, BSG_NODE_VIEW_SCOPE);
    if (!s)
	return NULL;

    /* Keep ownership explicit on view-scope nodes. */
    bsg_node_view_set((bsg_node *)s, v);

    return (bsg_node *)s;
}


int
bsg_view_scope_visible(bsg_node *node, struct bview *v)
{
    if (!node)
	return 0;

    if (!bsg_node_has_kind(node, BSG_NODE_VIEW_SCOPE))
	return 0;

    /* NULL owner means "shared" — visible to every view. */
    if (bsg_node_view_get(node) == NULL)
	return 1;

    /* View-private: only visible to the owning view. */
    return (bsg_node_view_get(node) == v) ? 1 : 0;
}


void
bsg_view_scope_destroy(bsg_node *scope)
{
    bsg_node_clear_children(scope);
    bsg_node_destroy(scope);
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
