/*                   A P P E A R A N C E . C
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
/** @file libbsg/appearance.c
 *
 * BSG node appearance accessors.
 */

#include "common.h"

#include "bsg/appearance.h"


void
bsg_appearance_set_visible(bsg_node *node, int visible)
{
    if (!node)
	return;
    node->s_flag = visible ? UP : DOWN;
}


int
bsg_appearance_visible(const bsg_node *node)
{
    if (!node)
	return 0;
    return (node->s_flag == UP) ? 1 : 0;
}


void
bsg_appearance_set_force_draw(bsg_node *node, int force_draw)
{
    if (!node)
	return;
    node->s_force_draw = force_draw ? 1 : 0;
}


int
bsg_appearance_force_draw(const bsg_node *node)
{
    if (!node)
	return 0;
    return node->s_force_draw ? 1 : 0;
}


void
bsg_appearance_set_line_style(bsg_node *node, int dashed)
{
    if (!node)
	return;
    node->s_soldash = dashed ? 1 : 0;
}


int
bsg_appearance_line_style(const bsg_node *node)
{
    if (!node)
	return 0;
    return node->s_soldash ? 1 : 0;
}


void
bsg_appearance_set_line_width(bsg_node *node, int line_width)
{
    if (!node)
	return;
    if (line_width < 0)
	line_width = 0;
    struct bsg_obj_settings *os = (node->s_os) ? node->s_os : &node->s_local_os;
    os->s_line_width = line_width;
}


int
bsg_appearance_line_width(const bsg_node *node)
{
    if (!node)
	return 0;
    const struct bsg_obj_settings *os = (node->s_os) ? node->s_os : &node->s_local_os;
    return os->s_line_width;
}


void
bsg_appearance_set_highlighted(bsg_node *node, int highlighted)
{
    if (!node)
	return;
    node->s_iflag = highlighted ? UP : DOWN;
}


int
bsg_appearance_is_highlighted(const bsg_node *node)
{
    if (!node)
	return 0;
    return (node->s_iflag == UP) ? 1 : 0;
}


void
bsg_appearance_set_changed(bsg_node *node, int changed)
{
    if (!node)
	return;
    node->s_changed = changed ? 1 : 0;
}


int
bsg_appearance_get_changed(const bsg_node *node)
{
    if (!node)
	return 0;
    return node->s_changed ? 1 : 0;
}


void
bsg_appearance_set_drawn_rev(bsg_node *node, uint64_t rev)
{
    if (!node)
	return;
    node->s_drawn_rev = rev;
}


uint64_t
bsg_appearance_drawn_rev(const bsg_node *node)
{
    if (!node)
	return 0;
    return node->s_drawn_rev;
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
