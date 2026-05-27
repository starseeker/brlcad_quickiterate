/*                  D R A W _ S O U R C E . H
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
 * Draw-source accessors: database directory pointer, path token, view,
 * vlist storage, vlfree pool, and update/free callbacks.
 *
 * These accessors replace direct manipulation of the raw bsg_node fields
 * dp, s_path, s_v, s_vlist, s_vlen, vlfree, s_update_callback, and
 * s_free_callback by drawing-code consumers (libged, libdm, MGED, qged).
 *
 * Phase 8A (bsg_modernize): stabilize user-facing BSG accessors so that
 * libged/MGED/qged/libqtcad can describe their intent in BSG/scene-graph
 * terms instead of renamed legacy view/draw "god struct" field writes.
 */
/** @{ */
/* @file bsg/draw_source.h */

#ifndef BSG_DRAW_SOURCE_H
#define BSG_DRAW_SOURCE_H

#include "common.h"
#include "bu/list.h"
#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/* -----------------------------------------------------------------------
 * Database directory pointer (dp)
 *
 * dp holds the application's opaque reference to the database directory
 * entry (struct directory *) that this node was drawn from.  It is NULL
 * for overlay/invented shapes that have no corresponding database object.
 * ----------------------------------------------------------------------- */

/**
 * Set the database directory pointer for @p node.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_draw_dp(bsg_node *node, void *dp);

/**
 * Return the database directory pointer stored in @p node, or NULL.
 */
BSG_EXPORT extern void *
bsg_node_get_draw_dp(const bsg_node *node);

/* -----------------------------------------------------------------------
 * Path token (s_path)
 *
 * s_path holds an application-specific alternative encoding of the node's
 * draw path (e.g. a db_full_path pointer or a string token).  Ownership
 * is caller-managed; the bsg_node does not free it.
 * ----------------------------------------------------------------------- */

/**
 * Set the path token for @p node.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_draw_path(bsg_node *node, void *path_token);

/**
 * Return the path token stored in @p node, or NULL.
 */
BSG_EXPORT extern void *
bsg_node_get_draw_path(const bsg_node *node);

/* -----------------------------------------------------------------------
 * Associated view (s_v)
 *
 * The node carries a back-pointer to its creating/editing bsg_view.  In a
 * multi-view scenario the pointer may be updated as the node is edited from
 * different views.  Use the accessor rather than reading s_v directly.
 * ----------------------------------------------------------------------- */

/**
 * Set the associated view for @p node.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_view(bsg_node *node, struct bsg_view *v);

/**
 * Return the associated view for @p node, or NULL.
 */
BSG_EXPORT extern struct bsg_view *
bsg_node_get_view(const bsg_node *node);

/* -----------------------------------------------------------------------
 * Vlist storage (s_vlist / s_vlen)
 *
 * Shape nodes carry their 3-D wireframe geometry as a bu_list of
 * bsg_vlist chunks in s_vlist.  Use these accessors rather than
 * touching s_vlist / s_vlen directly.
 * ----------------------------------------------------------------------- */

/**
 * Return a pointer to the vlist head of @p node (the s_vlist field).
 * The caller may append to or read from this list, but must not free it
 * via free() — use BSG_FREE_VLIST with the associated vlfree pool.
 * Returns NULL if @p node is NULL.
 */
BSG_EXPORT extern struct bu_list *
bsg_node_vlist_head(bsg_node *node);

/**
 * Return the number of vlist commands currently stored in @p node.
 * Returns 0 if @p node is NULL.
 */
BSG_EXPORT extern size_t
bsg_node_vlen(const bsg_node *node);

/**
 * Set the vlist command count for @p node to @p vlen.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_vlen(bsg_node *node, size_t vlen);

/* -----------------------------------------------------------------------
 * Vlist free-list pool (vlfree)
 *
 * vlfree points to the pool of recycled bsg_vlist chunks shared by all
 * nodes in the same view.  Use bsg_node_get_vlfree() rather than reading
 * the field directly to ensure NULL-safety.
 * ----------------------------------------------------------------------- */

/**
 * Return the vlfree free-list pool pointer for @p node, or NULL.
 */
BSG_EXPORT extern struct bu_list *
bsg_node_get_vlfree(const bsg_node *node);

/* -----------------------------------------------------------------------
 * Draw matrix (s_mat)
 *
 * s_mat is the 4x4 matrix used for internal lookup and mesh LoD drawing.
 * It is distinct from the transform node matrix (see bsg/node_transform.h).
 * ----------------------------------------------------------------------- */

/**
 * Copy @p mat into the draw matrix of @p node.
 * No-op if either argument is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_draw_mat(bsg_node *node, const mat_t mat);

/**
 * Copy the draw matrix of @p node into @p mat.
 * No-op if either argument is NULL.
 */
BSG_EXPORT extern void
bsg_node_get_draw_mat(const bsg_node *node, mat_t mat);

/* -----------------------------------------------------------------------
 * Bounding sphere (s_size / s_center)
 *
 * s_size is the distance across the solid in model space; s_center is its
 * center in model space.  Updated by bsg_scene_obj_bound().
 * ----------------------------------------------------------------------- */

/**
 * Return the bounding sphere radius of @p node (s_size), or 0.0.
 */
BSG_EXPORT extern fastf_t
bsg_node_draw_size(const bsg_node *node);

/**
 * Set the bounding sphere radius of @p node to @p size.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_draw_size(bsg_node *node, fastf_t size);

/**
 * Copy the bounding sphere center of @p node into @p center.
 * No-op if either argument is NULL.
 */
BSG_EXPORT extern void
bsg_node_get_draw_center(const bsg_node *node, vect_t center);

/**
 * Copy @p center into the bounding sphere center of @p node.
 * No-op if either argument is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_draw_center(bsg_node *node, const vect_t center);

/* -----------------------------------------------------------------------
 * Update and free callbacks (s_update_callback / s_free_callback)
 *
 * s_update_callback is invoked by the pre-render dispatch path to regenerate
 * the vlist or other geometry data before drawing.  s_free_callback is
 * invoked when the node is destroyed to release s_i_data and any other
 * node-private resources.
 *
 * Use these accessors rather than assigning the function pointers directly.
 * ----------------------------------------------------------------------- */

/**
 * Callback type for s_update_callback: receives the node, the current view,
 * and an integer flag; returns non-zero if the vlist was regenerated.
 */
typedef int (*bsg_update_cb_t)(struct bsg_node *node, struct bsg_view *v, int flag);

/**
 * Callback type for s_free_callback: receives the node and should release
 * any resources stored in s_i_data or draw_data.
 */
typedef void (*bsg_free_cb_t)(struct bsg_node *node);

/**
 * Set the update callback for @p node.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_update_cb(bsg_node *node, bsg_update_cb_t cb);

/**
 * Return the update callback stored in @p node, or NULL.
 */
BSG_EXPORT extern bsg_update_cb_t
bsg_node_get_update_cb(const bsg_node *node);

/**
 * Set the free callback for @p node.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_free_cb(bsg_node *node, bsg_free_cb_t cb);

/**
 * Invoke the free callback of @p node (if set) and clear it.
 * No-op if @p node is NULL or has no free callback.
 */
BSG_EXPORT extern void
bsg_node_invoke_free_cb(bsg_node *node);

__END_DECLS

#endif /* BSG_DRAW_SOURCE_H */

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
