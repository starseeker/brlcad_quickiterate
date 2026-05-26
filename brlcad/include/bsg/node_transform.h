/*               N O D E _ T R A N S F O R M . H
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
 * Transform node API (BSG_NODE_TRANSFORM).
 *
 * A transform node carries a 4x4 matrix (stored in s_mat) that is pushed on
 * to the display manager's modelview stack before its children are rendered
 * and popped afterwards.  Analogous to OpenInventor's SoTransform.
 */
/** @{ */
/* @file bsg/node_transform.h */

#ifndef BSG_NODE_TRANSFORM_H
#define BSG_NODE_TRANSFORM_H

#include "common.h"
#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Allocate a BSG_NODE_TRANSFORM node associated with view @p v.
 * The node is NOT inserted into any view table.
 * The initial matrix is the identity matrix.
 * Returns NULL on failure.
 */
BSG_EXPORT extern bsg_node *
bsg_transform_create(struct bsg_view *v);

/**
 * Copy @p mat into @p transform's s_mat.
 * No-op if either argument is NULL.
 */
BSG_EXPORT extern void
bsg_transform_set_matrix(bsg_node *transform, const mat_t mat);

/**
 * Copy @p transform's s_mat into @p mat.
 * No-op if either argument is NULL.
 */
BSG_EXPORT extern void
bsg_transform_get_matrix(const bsg_node *transform, mat_t mat);

/**
 * Release the transform node back to the libbv free pool.
 * Children are NOT freed.
 * No-op if @p transform is NULL.
 */
BSG_EXPORT extern void
bsg_transform_destroy(bsg_node *transform);

__END_DECLS

#endif /* BSG_NODE_TRANSFORM_H */

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
