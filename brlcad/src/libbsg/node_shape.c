/*                   N O D E _ S H A P E . C
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
/** @file libbsg/node_shape.c
 *
 * Phase 6-C: BSG_NODE_SHAPE lifecycle (create / set_vlist / destroy).
 * A shape node is a drawable leaf that carries a vlist or a
 * s_update_callback-based payload.
 */

#include "common.h"

#include "bu/list.h"
#include "bsg/defines.h"
#include "bsg/node.h"
#include "bsg/util.h"
#include "bsg/vlist.h"
#include "bsg/node_shape.h"


bsg_node *
bsg_shape_create(struct bsg_view *v)
{
    if (!v)
	return NULL;

    return bsg_node_create(v, BSG_NODE_SHAPE);
}


void
bsg_shape_set_vlist(bsg_node *shape, struct bu_list *vhead)
{
    if (!shape || !vhead)
	return;

    bsg_node *s = (bsg_node *)shape;

    /* Free any existing vlist */
    if (BU_LIST_IS_INITIALIZED(&s->s_vlist))
	BSG_FREE_VLIST(s->vlfree, &s->s_vlist);
    BU_LIST_INIT(&s->s_vlist);

    /* Copy in the new vlist */
    bsg_vlist_copy(s->vlfree, &s->s_vlist, vhead);
    if (BU_LIST_IS_EMPTY(&s->s_vlist)) {
	s->s_vlen = 0;
    } else {
	s->s_vlen = bsg_vlist_cmd_cnt((bsg_vlist *)BU_LIST_FIRST(bsg_vlist, &s->s_vlist));
    }
}


void
bsg_shape_destroy(bsg_node *shape)
{
    if (!shape)
	return;

    bsg_node_destroy(shape);
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
