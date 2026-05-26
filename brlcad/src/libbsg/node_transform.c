/*               N O D E _ T R A N S F O R M . C
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
/** @file libbsg/node_transform.c
 *
 * Phase 6-C: BSG_NODE_TRANSFORM lifecycle (create / set_matrix /
 * get_matrix / destroy).  The node stores a 4x4 modelview matrix in s_mat
 * that bsg_view_traverse pushes before rendering child nodes and pops after.
 */

#include "common.h"

#include "vmath.h"
#include "bsg/defines.h"
#include "bsg/node.h"
#include "bsg/util.h"
#include "bsg/node_transform.h"


bsg_node *
bsg_transform_create(struct bsg_view *v)
{
    if (!v)
	return NULL;

    return bsg_node_create(v, BSG_NODE_TRANSFORM);
}


void
bsg_transform_set_matrix(bsg_node *transform, const mat_t mat)
{
    if (!transform || !mat)
	return;

    bsg_node_set_transform(transform, mat);
}


void
bsg_transform_get_matrix(const bsg_node *transform, mat_t mat)
{
    if (!transform || !mat)
	return;

    bsg_node_transform(transform, mat);
}


void
bsg_transform_destroy(bsg_node *transform)
{
    if (!transform)
	return;

    bsg_node_destroy(transform);
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
