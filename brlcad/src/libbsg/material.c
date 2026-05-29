/*                     M A T E R I A L . C
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
/** @file libbsg/material.c
 *
 * BSG node material accessors.
 */

#include "common.h"

#include "bsg/material.h"
#include "bsg/node_private.h"


void
bsg_material_set_rgb(bsg_node *node, unsigned char r, unsigned char g, unsigned char b)
{
    if (!node)
	return;

    node->s_color[0] = r;
    node->s_color[1] = g;
    node->s_color[2] = b;
}


void
bsg_material_get_rgb(const bsg_node *node, unsigned char *r, unsigned char *g, unsigned char *b)
{
    if (!node)
	return;

    if (r)
	*r = node->s_color[0];
    if (g)
	*g = node->s_color[1];
    if (b)
	*b = node->s_color[2];
}


void
bsg_material_set_revision(bsg_node *node, uint32_t revision)
{
    if (!node)
	return;
    node->s_color_rev = revision;
}


uint32_t
bsg_material_revision(const bsg_node *node)
{
    if (!node)
	return 0;
    return node->s_color_rev;
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
