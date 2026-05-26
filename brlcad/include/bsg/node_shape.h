/*                   N O D E _ S H A P E . H
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
 * Shape node API (BSG_NODE_SHAPE).
 *
 * A shape node is a leaf node that carries drawable geometry (a vlist or an
 * update-callback-populated payload).  Analogous to OpenInventor's SoShape.
 */
/** @{ */
/* @file bsg/node_shape.h */

#ifndef BSG_NODE_SHAPE_H
#define BSG_NODE_SHAPE_H

#include "common.h"
#include "bu/list.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Allocate a BSG_NODE_SHAPE node associated with view @p v.
 * The node is NOT inserted into any view table.
 * Returns NULL on failure.
 */
BSG_EXPORT extern bsg_node *
bsg_shape_create(struct bsg_view *v);

/**
 * Copy the vlist starting at @p vhead into @p shape's s_vlist.
 * Any existing vlist on @p shape is freed first.
 * No-op if either argument is NULL.
 */
BSG_EXPORT extern void
bsg_shape_set_vlist(bsg_node *shape, struct bu_list *vhead);

/**
 * Release the shape node back to the libbv free pool (frees s_vlist).
 * No-op if @p shape is NULL.
 */
BSG_EXPORT extern void
bsg_shape_destroy(bsg_node *shape);

__END_DECLS

#endif /* BSG_NODE_SHAPE_H */

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
