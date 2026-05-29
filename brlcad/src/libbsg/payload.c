/*                    P A Y L O A D . C
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
/** @file libbsg/payload.c
 *
 * Phase 6-D: Payload type accessors and pre-render dispatch.
 *
 * bsg_payload_dispatch() is a pre-render hook that calls s_update_callback
 * (when set) for payload types requiring a geometry update before the node
 * is handed off to the libdm renderer.  For BSG_PAYLOAD_VLIST the vlist is
 * assumed to be already populated and no callback is invoked.
 *
 * The actual rendering is still performed by libdm (bsg_view_traverse /
 * dm_draw_objs) to avoid a libbsg → libdm circular dependency.
 */

#include "common.h"

#include "bsg/defines.h"
#include "bsg/payload.h"
#include "bsg/node_private.h"


void
bsg_node_set_payload_type(bsg_node *node, unsigned long long payload_flags)
{
    if (!node)
	return;

    bsg_node *s = (bsg_node *)node;

    /* Replace only the payload bits — preserve all other type flags */
    s->s_type_flags = (s->s_type_flags & ~BSG_PAYLOAD_MASK) |
		      (payload_flags & BSG_PAYLOAD_MASK);
}


unsigned long long
bsg_node_get_payload_type(const bsg_node *node)
{
    if (!node)
	return 0;

    return ((const bsg_node *)node)->s_type_flags & BSG_PAYLOAD_MASK;
}


void
bsg_payload_dispatch(void *dmp, bsg_node *node, struct bsg_view *v)
{
    if (!node)
	return;

    unsigned long long ptype = bsg_node_get_payload_type(node);
    if (!ptype)
	return;

    bsg_node *s = (bsg_node *)node;

    /* For VLIST payloads the vlist is already populated; skip callback. */
    if (ptype & BSG_PAYLOAD_VLIST)
	return;

    /* For all other payload types, invoke the update callback if set.
     * The callback is responsible for refreshing s_vlist or any other
     * geometry data before the renderer consumes it. */
    if (s->s_update_callback) {
	/* dmp is not directly accessible through the generic update_callback
	 * signature (int(*)(bsg_node*, bsg_view*, int)) so we pass the
	 * mode as 0.  Callbacks that need the dmp pointer should retrieve
	 * it via v->dmp. */
	(void)dmp;  /* consumed by caller context if needed */
	(*s->s_update_callback)(s, v, 0);
    }
}

void
bsg_node_set_internal_data(bsg_node *node, void *data)
{
    if (!node)
	return;
    node->s_i_data = data;
}


void *
bsg_node_get_internal_data(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->s_i_data;
}


void
bsg_node_set_draw_data(bsg_node *node, void *data)
{
    if (!node)
	return;
    node->draw_data = data;
}


void *
bsg_node_get_draw_data(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->draw_data;
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
