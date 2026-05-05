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

struct bview;          /* forward-declare to avoid circular includes */
struct bv_scene_obj;   /* forward-declare to avoid circular includes */

/**
 * Signature for a caller-supplied shape path-match predicate.
 *
 * Used by bsg_erase_nested_subpath() in case (b) — the leaf-primitive case
 * where a BSG_NODE_SHAPE child should be erased only when its s_u_data
 * satisfies the match condition.
 *
 * @p shape_u_data  The @c s_u_data pointer of the candidate shape node.
 *                  May be NULL; the callback should return 0 in that case.
 * @p match_ctx     Opaque context supplied by the caller (e.g. a pointer to
 *                  a @c struct @c db_full_path in the GED layer).
 *
 * Returns non-zero if the shape should be erased, zero otherwise.
 */
typedef int (*bsg_path_match_fn)(void *shape_u_data, void *match_ctx);

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


/**
 * Walk from node @p n to the draw root and bump the structural revision
 * counter stored in the root's bsg_draw_ctx.
 *
 * Call on every add or remove of a group or shape node in the draw tree.
 * Does nothing if @p n is NULL or if the root has no bsg_draw_ctx.
 *
 * Replaces the file-private @c _sg_bump_rev_node() helper in
 * src/libged/bsg_view_obj.c (Phase 7 Step 11).
 */
BSG_EXPORT extern void
bsg_bump_rev_node(bsg_node *n);


/**
 * Recursively free all descendant nodes of @p g (shapes and nested
 * sub-groups) without freeing @p g itself.
 *
 * @p fso must be the free-object pool pointer for this draw tree.
 * Obtain it from the bsg_draw_ctx that the root's s_i_data points to
 * (field bsg_draw_ctx::fso), or fall back to the individual node's
 * free_scene_obj field.
 *
 * Each freed shape node has its s_dlist_free_callback and s_free_callback
 * fired before recycling.  Group nodes are freed recursively.
 *
 * This function does NOT bump the draw-tree revision counter; callers
 * must call bsg_bump_rev_node() at the appropriate ancestor.
 *
 * Replaces the file-private @c _sg_free_children_recursive() helper in
 * src/libged/bsg_view_obj.c (Phase 7 Step 11).
 */
BSG_EXPORT extern void
bsg_free_children_recursive(bsg_node *g, struct bv_scene_obj *fso);


/**
 * Free all descendant nodes of @p g without freeing @p g itself.
 *
 * Obtains the free-object pool pointer from the bsg_draw_ctx stored in
 * the draw root's s_i_data (field bsg_draw_ctx::fso).  Falls back to
 * the individual node's free_scene_obj field when the context is absent.
 *
 * This function does NOT bump the draw-tree revision counter; callers
 * must call bsg_bump_rev_node() at the appropriate ancestor after
 * structural changes.
 *
 * Replaces the file-private @c _sg_free_group_contents() helper in
 * src/libged/bsg_view_obj.c (Phase 7 Step 11).
 */
BSG_EXPORT extern void
bsg_free_group_contents(bsg_node *g);


/**
 * Free the entire subtree rooted at @p g, including @p g itself.
 *
 * Removes @p g from its parent's children table, bumps the draw-tree
 * revision counter, then frees all descendants followed by @p g itself.
 *
 * It is safe to call this function on a group that has already been
 * unlinked from its parent (parent == NULL), but in that case the
 * revision bump is a no-op.
 *
 * Replaces the file-private @c _sg_free_group() helper in
 * src/libged/bsg_view_obj.c (Phase 7 Step 11).
 */
BSG_EXPORT extern void
bsg_free_group(bsg_node *g);


/**
 * Navigate the draw sub-tree below @p parent and erase the matching
 * sub-group or leaf shape(s) at the deepest path component.
 *
 * @p comp_names  Array of @p comp_count path-component name strings,
 *                indexing from 0.  These correspond to successive BSG
 *                group names below @p parent.
 * @p comp_count  Total number of components in @p comp_names.
 * @p depth_start Index into @p comp_names where navigation should begin.
 *                Pass 0 to start from the first component; pass n to skip
 *                the first n components (use when @p parent already
 *                represents those n components).
 * @p match_fn    Path-match predicate called in case (b) — the leaf
 *                primitive case — to decide whether a BSG_NODE_SHAPE child
 *                should be erased.  The first argument is the shape's
 *                @c s_u_data pointer; the second is @p match_ctx.
 *                If NULL, every BSG_NODE_SHAPE child at the leaf level is
 *                erased.
 * @p match_ctx   Opaque context forwarded verbatim to @p match_fn.
 *
 * Two cases at the final component:
 *   a) A BSG_NODE_GROUP child with that name exists — its entire sub-tree
 *      is freed and the group node is removed from its parent.
 *   b) No matching group — the component is treated as a leaf primitive;
 *      all BSG_NODE_SHAPE children that satisfy @p match_fn are freed.
 *
 * The structural revision counter is bumped for each erasure.
 *
 * Extracted from the file-private @c _sg_erase_nested_subpath() helper in
 * src/libged/bsg_ged_draw.c (Phase 7 Step 12).
 */
BSG_EXPORT extern void
bsg_erase_nested_subpath(bsg_node *parent,
			 const char * const *comp_names, size_t comp_count,
			 size_t depth_start,
			 bsg_path_match_fn match_fn, void *match_ctx);


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
