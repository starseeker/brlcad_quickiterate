/*                         U T I L . H
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
 * Scene-graph lifecycle and query utilities.
 */
/** @{ */
/* @file bsg/util.h */

#ifndef BSG_UTIL_H
#define BSG_UTIL_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Allocate and initialise a scene-graph root node for view @p v.
 * Stores the result in @p v->bsg_root and returns it.  Returns NULL
 * if @p v is NULL or allocation fails.
 */
BSG_EXPORT extern bsg_node *
bsg_scene_root_create(struct bview *v);

/**
 * Synchronise @p root with view @p v.
 *
 * In the current migration stage @p root aliases @p v->gv_draw_root and
 * draw-tree mutations keep children live, so this function is a compatibility
 * no-op.
 */
BSG_EXPORT extern void
bsg_scene_root_sync(bsg_node *root, struct bview *v);

/**
 * Destroy a scene root previously created by bsg_scene_root_create().
 * The root's children are NOT freed (they are borrowed references
 * owned by the view).  Only the root node itself is released.
 * Also clears @p v->bsg_root if @p root matches it.
 */
BSG_EXPORT extern void
bsg_scene_root_destroy(bsg_node *root);

/**
 * Return the first child of @p root whose s_type_flags field has all
 * bits in @p flags set.  Returns NULL if no match is found or if either
 * argument is NULL.  Searches one level deep (direct children only).
 */
BSG_EXPORT extern bsg_node *
bsg_view_find_by_type(bsg_node *root, unsigned long long flags);

/**
 * Fire sensor callbacks on all nodes in the subtree rooted at @p root
 * whose type includes BSG_NODE_SENSOR.  @p v is passed to each
 * callback as context.  No-op when @p root is NULL.
 */
BSG_EXPORT extern void
bsg_sensor_fire(bsg_node *root, struct bview *v);

__END_DECLS

#endif /* BSG_UTIL_H */

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
