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

#include "bu/ptbl.h"
#include "bsg/defines.h"
#include "bsg/node.h"
#include "bsg/util.h"
#include "bsg/view_scope.h"
#include "bsg/node_private.h"


bsg_node *
bsg_view_scope_create(struct bsg_view *v)
{
    if (!v)
	return NULL;

    bsg_node *s = bsg_node_create(v, BSG_NODE_VIEW_SCOPE);
    if (!s)
	return NULL;

    /* s_v is already set by bsg_obj_create to v; make the ownership explicit. */
    s->s_v          = v;

    return (bsg_node *)s;
}


int
bsg_view_scope_visible(bsg_node *node, struct bsg_view *v)
{
    if (!node)
	return 0;

    bsg_node *s = (bsg_node *)node;
    if (!(s->s_type_flags & BSG_NODE_VIEW_SCOPE))
	return 0;

    /* NULL owner means "shared" — visible to every view. */
    if (s->s_v == NULL)
	return 1;

    /* View-private: only visible to the owning view. */
    return (s->s_v == v) ? 1 : 0;
}


void
bsg_view_scope_destroy(bsg_node *scope)
{
    if (!scope)
	return;

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
