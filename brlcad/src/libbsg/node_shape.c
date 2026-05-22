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
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/vlist.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/node_shape.h"


bsg_node *
bsg_shape_create(struct bview *v)
{
    if (!v)
	return NULL;

    struct bv_scene_obj *s = bsg_obj_create(v, BV_VIEW_OBJS | BV_LOCAL_OBJS);
    if (!s)
	return NULL;

    s->s_type_flags = BSG_NODE_SHAPE;
    s->s_flag = UP;
    return (bsg_node *)s;
}


void
bsg_shape_set_vlist(bsg_node *shape, struct bu_list *vhead)
{
    if (!shape || !vhead)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)shape;

    /* Free any existing vlist */
    if (BU_LIST_IS_INITIALIZED(&s->s_vlist))
	BV_FREE_VLIST(s->vlfree, &s->s_vlist);
    BU_LIST_INIT(&s->s_vlist);

    /* Copy in the new vlist */
    bv_vlist_copy(s->vlfree, &s->s_vlist, vhead);
}


void
bsg_shape_destroy(bsg_node *shape)
{
    if (!shape)
	return;

    bsg_obj_put((struct bv_scene_obj *)shape);
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
