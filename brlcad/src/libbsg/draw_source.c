/*               D R A W _ S O U R C E . C
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
/** @file libbsg/draw_source.c
 *
 * Phase 8A: draw-source accessors for dp, s_path, s_v, vlfree,
 * s_vlist/s_vlen, and update/free callbacks.
 */

#include "common.h"

#include "vmath.h"
#include "bsg/draw_source.h"


void
bsg_node_set_draw_dp(bsg_node *node, void *dp)
{
    if (!node)
	return;
    node->dp = dp;
}


void *
bsg_node_get_draw_dp(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->dp;
}


void
bsg_node_set_draw_path(bsg_node *node, void *path_token)
{
    if (!node)
	return;
    node->s_path = path_token;
}


void *
bsg_node_get_draw_path(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->s_path;
}


void
bsg_node_set_view(bsg_node *node, struct bsg_view *v)
{
    if (!node)
	return;
    node->s_v = v;
}


struct bsg_view *
bsg_node_get_view(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->s_v;
}


struct bu_list *
bsg_node_vlist_head(bsg_node *node)
{
    if (!node)
	return NULL;
    return &node->s_vlist;
}


size_t
bsg_node_vlen(const bsg_node *node)
{
    if (!node)
	return 0;
    return node->s_vlen;
}


void
bsg_node_set_vlen(bsg_node *node, size_t vlen)
{
    if (!node)
	return;
    node->s_vlen = vlen;
}


struct bu_list *
bsg_node_get_vlfree(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->vlfree;
}


void
bsg_node_set_update_cb(bsg_node *node, bsg_update_cb_t cb)
{
    if (!node)
	return;
    node->s_update_callback = cb;
}


bsg_update_cb_t
bsg_node_get_update_cb(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->s_update_callback;
}


void
bsg_node_set_free_cb(bsg_node *node, bsg_free_cb_t cb)
{
    if (!node)
	return;
    node->s_free_callback = cb;
}


void
bsg_node_invoke_free_cb(bsg_node *node)
{
    if (!node || !node->s_free_callback)
	return;
    node->s_free_callback(node);
    node->s_free_callback = NULL;
}


void
bsg_node_set_draw_mat(bsg_node *node, const mat_t mat)
{
    if (!node || !mat)
	return;
    MAT_COPY(node->s_mat, mat);
}


void
bsg_node_get_draw_mat(const bsg_node *node, mat_t mat)
{
    if (!node || !mat)
	return;
    MAT_COPY(mat, node->s_mat);
}


fastf_t
bsg_node_draw_size(const bsg_node *node)
{
    if (!node)
	return 0.0;
    return node->s_size;
}


void
bsg_node_set_draw_size(bsg_node *node, fastf_t size)
{
    if (!node)
	return;
    node->s_size = size;
}


void
bsg_node_get_draw_center(const bsg_node *node, vect_t center)
{
    if (!node || !center)
	return;
    VMOVE(center, node->s_center);
}


void
bsg_node_set_draw_center(bsg_node *node, const vect_t center)
{
    if (!node || !center)
	return;
    VMOVE(node->s_center, center);
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
