/*                 V I E W _ S C O P E . H
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
 * Phase V1 (drawing_stack_modernization):
 * View-scope node API (BSG_NODE_VIEW_SCOPE).
 *
 * A view-scope node is a container whose children are skipped during BSG
 * render traversal for any view that does not match the node's owner (s_v).
 * When the owner is NULL the scope is "shared" and its children are visible
 * to all views.
 *
 * Design:
 *   - Per-view scope:  bsg_view_scope_create(v)  with v != NULL.
 *                      Only the view v will see the children.
 *   - Shared scope:    s_v == NULL semantics are supported by the traversal
 *                      predicate; the NULL-owner allocation path is reserved
 *                      for a future phase.
 *
 * No producers move to this new node kind in Phase V1.  The node kind is
 * introduced here so that the render traversal is ready before any migration
 * begins in Phase V2/V3.
 */
/** @{ */
/* @file bsg/view_scope.h */

#ifndef BSG_VIEW_SCOPE_H
#define BSG_VIEW_SCOPE_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Allocate a BSG_NODE_VIEW_SCOPE node scoped to view @p v.
 *
 * The returned node's s_v field is set to @p v.  During BSG render traversal,
 * only the view whose pointer equals @p v will descend into the node's
 * children.
 *
 * The node is NOT inserted into any parent; the caller must attach it
 * (e.g. via bsg_group_add_child) after creation.
 *
 * @p v must be non-NULL.  Returns NULL on failure.
 */
BSG_EXPORT extern bsg_node *
bsg_view_scope_create(struct bsg_view *v);

/**
 * Return non-zero if @p node is a BSG_NODE_VIEW_SCOPE that should be
 * traversed for view @p v.
 *
 * Visibility rules:
 *   - node->s_v == NULL  →  shared scope, visible to all views  (returns 1).
 *   - node->s_v == v     →  view-private scope, visible to v    (returns 1).
 *   - node->s_v != v     →  wrong view, skip                    (returns 0).
 *   - node is NULL or not BSG_NODE_VIEW_SCOPE                   (returns 0).
 */
BSG_EXPORT extern int
bsg_view_scope_visible(bsg_node *node, struct bsg_view *v);

/**
 * Release the view-scope node back to the libbv free pool.
 *
 * Children are NOT freed — they remain owned by their original contexts.
 * No-op if @p scope is NULL.
 */
BSG_EXPORT extern void
bsg_view_scope_destroy(bsg_node *scope);

__END_DECLS

#endif /* BSG_VIEW_SCOPE_H */

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
