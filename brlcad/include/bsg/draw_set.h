/*                    D R A W _ S E T . H
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
 * Phase 7 step 7 (A3 + C1/C3) of drawing_stack_modernization:
 * Pure-BSG draw-tree helpers that are independent of GED types.
 *
 * These functions operate on @c bsg_node (= struct bv_scene_obj) trees
 * and @c struct @c bview pointers.  They carry no dependency on
 * @c struct @c ged or @c ged_private.h, making them safe to implement in
 * libbsg.  GED-specific functionality (coloring, display-list callbacks,
 * database lookups) remains in libged/bsg_view_obj.c.
 *
 * Together with bsg/visit.h these form the "tree navigation" layer that
 * covers the C1/C3 split goal from the modernisation notes.
 */
/** @{ */
/* @file bsg/draw_set.h */

#ifndef BSG_DRAW_SET_H
#define BSG_DRAW_SET_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

struct bview;   /* forward-declare to avoid circular includes */

/**
 * Return the depth of node @p g in its BSG tree.
 *
 * The root node (no parent) has depth 0; immediate children of the root have
 * depth 1; and so on.  The walk follows the @c parent pointer chain, so only
 * nodes that are actually linked into a tree give a meaningful result.
 *
 * Replaces the file-private @c _sg_tree_depth() helper in
 * src/libged/bsg_view_obj.c.
 */
BSG_EXPORT extern int
bsg_draw_tree_depth(const bsg_node *g);


/**
 * Find a BSG_NODE_GROUP child of @p parent named exactly @p name.
 *
 * Returns the matching child, or NULL when no such child exists.
 * Does NOT create a new child if the name is absent — use
 * bsg_group_ensure_child() for that behaviour.
 *
 * Replaces the linear search extracted from @c _sg_find_or_create_child_group
 * in src/libged/bsg_view_obj.c.
 */
BSG_EXPORT extern bsg_node *
bsg_group_find_child(bsg_node *parent, const char *name);


/**
 * Find or create a BSG_NODE_GROUP child of @p parent named @p name.
 *
 * When a child with the given name already exists it is returned directly.
 * Otherwise a new BSG_NODE_GROUP node is allocated via @c bv_obj_create()
 * on @p v, linked into the tree, and returned.
 *
 * @p dp_hint is an opaque pointer stored verbatim in the new child's
 * @c dp field (type @c void*).  Pass @c NULL if the caller has no
 * corresponding database directory pointer.  For GED draw-trees the
 * caller should pass @c (void*)dp from a prior @c db_lookup() call so
 * that path-matching logic has a fast dp handle.
 *
 * Returns NULL on allocation failure.
 *
 * Replaces the @c _sg_find_or_create_child_group() helper in
 * src/libged/bsg_view_obj.c (minus the db_lookup call which belongs in
 * the libged wrapper).
 */
BSG_EXPORT extern bsg_node *
bsg_group_ensure_child(bsg_node *parent, struct bview *v,
		       const char *name, void *dp_hint);

__END_DECLS

#endif /* BSG_DRAW_SET_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
