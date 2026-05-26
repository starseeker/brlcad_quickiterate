/*                          N O D E . H
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
 * Generic node lifecycle and accessors for BSG scene nodes.
 */
/** @{ */
/* @file bsg/node.h */

#ifndef BSG_NODE_H
#define BSG_NODE_H

#include "common.h"
#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

BSG_EXPORT extern bsg_node *
bsg_node_create(struct bsg_view *v, unsigned long long kind);

BSG_EXPORT extern void
bsg_node_destroy(bsg_node *node);

BSG_EXPORT extern unsigned long long
bsg_node_kind(const bsg_node *node);

BSG_EXPORT extern int
bsg_node_is_kind(const bsg_node *node, unsigned long long kind);

BSG_EXPORT extern void
bsg_node_set_name(bsg_node *node, const char *name);

BSG_EXPORT extern const char *
bsg_node_name(const bsg_node *node);

BSG_EXPORT extern bsg_node *
bsg_node_parent(const bsg_node *node);

BSG_EXPORT extern size_t
bsg_node_child_count(const bsg_node *node);

BSG_EXPORT extern bsg_node *
bsg_node_child_at(const bsg_node *node, size_t idx);

BSG_EXPORT extern void
bsg_node_add_child(bsg_node *parent, bsg_node *child);

BSG_EXPORT extern void
bsg_node_remove_child(bsg_node *parent, bsg_node *child);

BSG_EXPORT extern void
bsg_node_set_visible_state(bsg_node *node, int on);

BSG_EXPORT extern int
bsg_node_visible(const bsg_node *node);

BSG_EXPORT extern void
bsg_node_set_transform(bsg_node *node, const mat_t mat);

BSG_EXPORT extern void
bsg_node_transform(const bsg_node *node, mat_t mat);

BSG_EXPORT extern void
bsg_node_set_bounds(bsg_node *node, const point_t bmin, const point_t bmax, int valid);

BSG_EXPORT extern int
bsg_node_bounds(const bsg_node *node, point_t bmin, point_t bmax);

BSG_EXPORT extern void
bsg_node_set_user_data(bsg_node *node, void *user_data);

BSG_EXPORT extern void *
bsg_node_user_data(const bsg_node *node);

BSG_EXPORT extern uint64_t
bsg_node_revision(const bsg_node *node);

BSG_EXPORT extern void
bsg_node_touch(bsg_node *node);

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
