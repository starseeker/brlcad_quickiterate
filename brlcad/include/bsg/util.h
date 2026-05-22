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
 * Allocate and initialize a scene-graph root node for view @p v.
 * Stores the result in @p v->bsg_root and returns it.  Returns NULL
 * if @p v is NULL or allocation fails.
 */
BSG_EXPORT extern bsg_node *
bsg_scene_root_create(struct bview *v);

/**
 * Synchronize the children list of @p root from the current draw state
 * of @p v.  The root's children table is cleared (pointers only, the
 * actual scene objects are owned by the view) and refilled from all
 * BV_DB_OBJS and BV_VIEW_OBJS tables accessible through @p v.
 *
 * This is the "shim" that mirrors the existing display-list contents
 * into the BSG tree (Phase 4-D).
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

/**
 * Allocate and initialize a scene-graph object using the BSG lifecycle API.
 *
 * The @p type flags currently use the existing BV_* storage flags while the
 * bview storage model is being migrated into libbsg.
 */
BSG_EXPORT extern bsg_node *
bsg_obj_create(struct bview *v, int type);

/**
 * Allocate a scene-graph object without registering it in the view's legacy
 * flat object tables.
 */
BSG_EXPORT extern bsg_node *
bsg_obj_get_unregistered(struct bview *v, int type);

/**
 * Recycle a scene-graph object allocated by bsg_obj_create() or
 * bsg_obj_get_unregistered().
 */
BSG_EXPORT extern void
bsg_obj_put(bsg_node *obj);

/**
 * Release backend-owned renderer state associated with @p obj.
 */
BSG_EXPORT extern void
bsg_scene_obj_release_backend(bsg_node *obj);

/**
 * Mark backend-owned renderer state associated with @p obj stale.
 */
BSG_EXPORT extern void
bsg_scene_obj_invalidate_backend(bsg_node *obj);

/**
 * Initialize a view object using the BSG namespace.  BSG wrapper around
 * bv_init().  @p v must point to allocated but uninitialized storage.
 * @p s is the optional view-set the view belongs to; pass NULL when unused.
 */
BSG_EXPORT extern void
bsg_view_init(struct bview *v, struct bview_set *s);

/**
 * Free resources owned by a view object.  BSG wrapper around bv_free().
 * Does not free the memory for @p v itself.
 */
BSG_EXPORT extern void
bsg_view_free(struct bview *v);

/**
 * Duplicate the contents of a vlist.  BSG-namespaced wrapper around
 * bv_vlist_copy().  @p vlists is the free-list pool; @p dest is cleared
 * and filled from @p src.
 */
BSG_EXPORT extern void
bsg_vlist_copy(struct bu_list *vlists,
               struct bu_list *dest,
               const struct bu_list *src);

/**
 * Return the table of views registered in the view-set @p s.
 * BSG-namespaced wrapper around bv_set_views().
 * Returns NULL when @p s is NULL.
 */
BSG_EXPORT extern struct bu_ptbl *
bsg_set_views(struct bview_set *s);

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
