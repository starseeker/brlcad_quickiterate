/*                   N O D E _ G R O U P . H
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
 * Group node API (BSG_NODE_GROUP).
 *
 * A group aggregates child nodes without contributing any drawable geometry.
 * Analogous to OpenInventor's SoGroup.
 */
/** @{ */
/* @file bsg/node_group.h */

#ifndef BSG_NODE_GROUP_H
#define BSG_NODE_GROUP_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Allocate a BSG_NODE_GROUP node associated with view @p v.
 * The node is NOT inserted into any view table; the caller must attach it to
 * a parent (e.g. via bsg_group_add_child).
 * Returns NULL on failure.
 */
BSG_EXPORT extern bsg_node *
bsg_group_create(struct bsg_view *v);

/**
 * Append @p child to @p group's children list.
 * No-op if either argument is NULL or @p child is already present.
 */
BSG_EXPORT extern void
bsg_group_add_child(bsg_node *group, bsg_node *child);

/**
 * Remove @p child from @p group's children list.
 * The child is NOT freed; ownership stays with the caller.
 * No-op if @p child is not found or either argument is NULL.
 */
BSG_EXPORT extern void
bsg_group_remove_child(bsg_node *group, bsg_node *child);

/**
 * Release the group node back to the libbv free pool.
 * Children are NOT freed — they remain owned by their original contexts.
 * No-op if @p group is NULL.
 */
BSG_EXPORT extern void
bsg_group_destroy(bsg_node *group);

__END_DECLS

#endif /* BSG_NODE_GROUP_H */

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
