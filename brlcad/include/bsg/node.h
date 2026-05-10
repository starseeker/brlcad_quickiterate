/*                         N O D E . H
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
/** @addtogroup libbsg
 *
 * @brief
 * Generic BSG node accessors over the current bv_scene_obj backing storage.
 *
 * BSG is the preferred public scene API; libbv's bv_scene_obj layout is
 * transitional backing storage while the dedicated node storage model is being
 * introduced.
 */
/** @{ */
/* @file bsg/node.h */

#ifndef BSG_NODE_H
#define BSG_NODE_H

#include "common.h"
#include "vmath.h"
#include "bsg/defines.h"
#include "bsg/field.h"

__BEGIN_DECLS

BSG_EXPORT extern unsigned long long
bsg_node_kind(const bsg_node *n);

BSG_EXPORT extern int
bsg_node_has_kind(const bsg_node *n, unsigned long long kind);

BSG_EXPORT extern void
bsg_node_set_kind(bsg_node *n, unsigned long long kind);

BSG_EXPORT extern const char *
bsg_node_name(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_set_name(bsg_node *n, const char *name);

BSG_EXPORT extern bsg_node *
bsg_node_parent(const bsg_node *n);

BSG_EXPORT extern size_t
bsg_node_child_count(const bsg_node *n);

BSG_EXPORT extern bsg_node *
bsg_node_child(const bsg_node *n, size_t idx);

BSG_EXPORT extern void
bsg_node_add_child(bsg_node *parent, bsg_node *child);

BSG_EXPORT extern void
bsg_node_remove_child(bsg_node *parent, bsg_node *child);

BSG_EXPORT extern int
bsg_node_visible(const bsg_node *n);

BSG_EXPORT extern int
bsg_node_force_draw(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_set_force_draw(bsg_node *n, int force_draw);

BSG_EXPORT extern void
bsg_node_transform_get(const bsg_node *n, mat_t out);

BSG_EXPORT extern void
bsg_node_transform_set(bsg_node *n, const mat_t mat);

BSG_EXPORT extern void *
bsg_node_user_data_get(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_user_data_set(bsg_node *n, void *data);

BSG_EXPORT extern void
bsg_node_bounds_get(const bsg_node *n, point_t bmin, point_t bmax);

BSG_EXPORT extern void
bsg_node_bounds_set(bsg_node *n, const point_t bmin, const point_t bmax);

BSG_EXPORT extern void
bsg_node_mark_stale(bsg_node *n);

__END_DECLS

#endif /* BSG_NODE_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
