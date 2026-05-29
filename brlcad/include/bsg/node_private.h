/*                  N O D E _ P R I V A T E . H
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
/** @file bsg/node_private.h
 *
 * Internal bsg_node layout for in-tree implementation code.
 * New code should prefer bsg/node.h accessors when possible.
 */

#ifndef BSG_NODE_PRIVATE_H
#define BSG_NODE_PRIVATE_H

#include "bsg/defines.h"

struct bsg_node  {
    struct bu_list l;

    struct bsg_node_internal *i;

    unsigned long long s_type_flags;
    struct bu_vls s_name;
    void *s_path;
    void *dp;
    mat_t s_mat;
    struct bsg_view *s_v;
    void *s_i_data;
    int (*s_update_callback)(struct bsg_node *, struct bsg_view *, int);
    void (*s_free_callback)(struct bsg_node *);
    struct bu_list s_vlist;
    size_t s_vlen;
    struct bsg_payload *pl;
    struct bsg_draw_intent *di;
    struct bsg_backend *s_backend;
    fastf_t s_size;
    fastf_t s_csize;
    vect_t s_center;
    int s_displayobj;
    point_t bmin;
    point_t bmax;
    int have_bbox;
    int s_bbox_cached;
    char s_flag;
    char s_iflag;
    int s_force_draw;
    unsigned char s_color[3];
    uint32_t s_color_rev;
    uint64_t s_drawn_rev;
    int s_soldash;
    int s_arrow;
    int s_changed;
    int current;
    int adaptive_wireframe;
    int csg_obj;
    int mesh_obj;
    fastf_t view_scale;
    size_t bot_threshold;
    fastf_t curve_scale;
    fastf_t point_scale;
    struct bsg_obj_settings *s_os;
    struct bsg_obj_settings s_local_os;
    int s_inherit_settings;
    struct bsg_node_old_settings s_old;
    struct bu_ptbl children;
    struct bsg_node *parent;
    struct bu_list *vlfree;
    struct bsg_node *free_scene_obj;
    struct bu_ptbl *otbl;
    void *draw_data;
    void *s_u_data;
};

#endif /* BSG_NODE_PRIVATE_H */
