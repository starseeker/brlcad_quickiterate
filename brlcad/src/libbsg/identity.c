/*                     I D E N T I T Y . C
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
/** @file libbsg/identity.c
 *
 * BSG node identity and revision model accessors.
 */

#include "common.h"

#include <limits.h>

#include "bsg/defines.h"
#include "bsg/draw_set.h"
#include "bsg/identity.h"


void
bsg_node_identity_set_name(bsg_node *node, const char *name)
{
    if (!node)
	return;

    if (!name) {
	bu_vls_trunc(&node->s_name, 0);
	return;
    }
    bu_vls_sprintf(&node->s_name, "%s", name);
}


const char *
bsg_node_identity_name(const bsg_node *node)
{
    if (!node)
	return NULL;
    return bu_vls_cstr(&node->s_name);
}


void
bsg_node_identity_set_path(bsg_node *node, void *path_token)
{
    if (!node)
	return;
    node->s_path = path_token;
}


void *
bsg_node_identity_path(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->s_path;
}


void
bsg_node_identity_set_source(bsg_node *node, void *source_data)
{
    if (!node)
	return;
    node->dp = source_data;
}


void *
bsg_node_identity_source(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->dp;
}


uint64_t
bsg_node_revision_get(const bsg_node *node)
{
    if (!node || node->s_changed < 0)
	return 0;
    return (uint64_t)node->s_changed;
}


void
bsg_node_revision_set(bsg_node *node, uint64_t revision)
{
    if (!node)
	return;
    node->s_changed = (revision > INT_MAX) ? INT_MAX : (int)revision;
}


uint64_t
bsg_node_revision_bump(bsg_node *node)
{
    if (!node)
	return 0;

    if (node->s_changed < INT_MAX)
	node->s_changed++;
    bsg_bump_rev_node(node);
    return (uint64_t)node->s_changed;
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
