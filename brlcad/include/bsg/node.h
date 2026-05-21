/*                         N O D E . H
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
 * Generic BSG node accessors over the current bv_scene_obj backing storage.
 *
 * BSG is the preferred public scene API; libbv's bv_scene_obj layout is
 * transitional backing storage while the dedicated node storage model is being
 * introduced.
 */
/** @{ */
/* @file bsg/node.h */

#ifndef BSG_NODE_H
#define BSG_NODE_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bsg/defines.h"
#include "bsg/field.h"

struct bview;
struct bv_obj_backend;

__BEGIN_DECLS

/**
 * Allocate an unregistered BSG-owned node of the requested major kind.
 *
 * The returned node participates in the normal view-object free pool but is
 * not inserted into any legacy gv_objs table.  Intended for standalone BSG
 * node kinds such as groups, shapes, transforms, sensors, and view scopes.
 *
 * Returns NULL on failure or when @p v is NULL.
 */
BSG_EXPORT extern bsg_node *
bsg_node_create(struct bview *v, unsigned long long kind);

/**
 * Allocate an unregistered draw-tree child node of the requested major kind.
 *
 * This is the BSG lifecycle entry point for nodes that should live only by
 * virtue of a parent/child relationship (for example draw-root helper groups).
 * Returns NULL on failure or when @p v is NULL.
 */
BSG_EXPORT extern bsg_node *
bsg_node_create_child(struct bview *v, unsigned long long kind);

/**
 * Clear @p n's child table without destroying the child nodes themselves.
 *
 * Any child whose parent pointer still references @p n is detached first so no
 * stale parent link remains after the table is reset.  No-op for NULL nodes.
 */
BSG_EXPORT extern void
bsg_node_clear_children(bsg_node *n);

/**
 * Release @p n through the compatibility backing store lifecycle.
 *
 * This is the preferred BSG lifecycle exit point for standalone node handles.
 * It delegates to the current bv_scene_obj backing storage while keeping that
 * compatibility detail out of libbsg callers.  No-op for NULL nodes.
 */
BSG_EXPORT extern void
bsg_node_destroy(bsg_node *n);

BSG_EXPORT extern unsigned long long
bsg_node_kind(const bsg_node *n);

BSG_EXPORT extern int
bsg_node_has_kind(const bsg_node *n, unsigned long long kind);

BSG_EXPORT extern void
bsg_node_set_kind(bsg_node *n, unsigned long long kind);

BSG_EXPORT extern const char *
bsg_node_name(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_set_name(bsg_node *n, const char *name);

BSG_EXPORT extern bsg_node *
bsg_node_parent(const bsg_node *n);

BSG_EXPORT extern size_t
bsg_node_child_count(const bsg_node *n);

BSG_EXPORT extern bsg_node *
bsg_node_child(const bsg_node *n, size_t idx);

BSG_EXPORT extern void
bsg_node_add_child(bsg_node *parent, bsg_node *child);

BSG_EXPORT extern void
bsg_node_remove_child(bsg_node *parent, bsg_node *child);

BSG_EXPORT extern int
bsg_node_visible(const bsg_node *n);

BSG_EXPORT extern int
bsg_node_force_draw(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_set_force_draw(bsg_node *n, int force_draw);

BSG_EXPORT extern void
bsg_node_transform_get(const bsg_node *n, mat_t out);

BSG_EXPORT extern void
bsg_node_transform_set(bsg_node *n, const mat_t mat);

BSG_EXPORT extern void *
bsg_node_user_data_get(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_user_data_set(bsg_node *n, void *data);

BSG_EXPORT extern void *
bsg_node_source_path_get(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_source_path_set(bsg_node *n, void *path);

BSG_EXPORT extern void *
bsg_node_app_data_get(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_app_data_set(bsg_node *n, void *data);

BSG_EXPORT extern struct bview *
bsg_node_view_get(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_view_set(bsg_node *n, struct bview *v);

BSG_EXPORT extern struct bv_obj_backend *
bsg_node_backend_get(const bsg_node *n);

BSG_EXPORT extern void
bsg_node_backend_set(bsg_node *n, struct bv_obj_backend *backend);

BSG_EXPORT extern void
bsg_node_bounds_get(const bsg_node *n, point_t bmin, point_t bmax);

BSG_EXPORT extern void
bsg_node_bounds_set(bsg_node *n, const point_t bmin, const point_t bmax);

/**
 * Return the bounding-sphere radius for @p n (bv_scene_obj::s_size).
 *
 * This is the half-extent of the node's geometry in model space, used for
 * level-of-detail selection and bound-flag culling.  Returns 0.0 for NULL @p n.
 */
BSG_EXPORT extern fastf_t
bsg_node_size_get(const bsg_node *n);

/**
 * Set the bounding-sphere radius for @p n (bv_scene_obj::s_size).
 *
 * Stores @p size into the backing s_size field.  No-op for NULL @p n.
 */
BSG_EXPORT extern void
bsg_node_size_set(bsg_node *n, fastf_t size);

BSG_EXPORT extern void
bsg_node_mark_stale(bsg_node *n);

/**
 * Legacy compatibility accessor for illumination/highlight state (s_iflag).
 *
 * Returns non-zero when illuminated/highlighted (UP), zero otherwise.
 */
BSG_EXPORT extern int
bsg_node_legacy_illum(const bsg_node *n);

/**
 * Legacy compatibility setter for illumination/highlight state (s_iflag).
 *
 * Stores UP when @p illuminated is non-zero, DOWN otherwise.
 */
BSG_EXPORT extern void
bsg_node_set_legacy_illum(bsg_node *n, int illuminated);

/**
 * Return non-zero when @p n is a display-space-coordinate object.
 *
 * Maps to the legacy bv_scene_obj::s_displayobj flag.  Objects with this flag
 * set contain vertices already expressed in display/screen coordinates rather
 * than model space; bounds-based culling is suppressed for them.
 */
BSG_EXPORT extern int
bsg_node_is_display_obj(const bsg_node *n);

/**
 * Return the per-frame draw revision stamp for @p n.
 *
 * This value is set to the view's gv_frame_rev each time the node is
 * successfully rendered.  Callers can compare it with gv_frame_rev to
 * determine whether the node was drawn in the current frame, replacing
 * the legacy full-tree s_flag = DOWN/UP reset sweep.
 *
 * Returns 0 if @p n is NULL.
 */
BSG_EXPORT extern uint64_t
bsg_node_drawn_rev(const bsg_node *n);

/**
 * Set the per-frame draw revision stamp for @p n to @p rev.
 *
 * Typically called by renderers after successfully drawing the node,
 * passing gv_frame_rev as @p rev.  No-op if @p n is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_drawn_rev(bsg_node *n, uint64_t rev);

/**
 * Return the GED-private data pointer stored on @p n (bv_scene_obj::s_u_data).
 *
 * This field carries a `struct ged_bv_data *` for shapes created by libged
 * draw paths.  View-only shapes that were not created by GED return NULL.
 * Callers should cast the result to the appropriate type before use.
 *
 * Returns NULL if @p n is NULL.
 */
BSG_EXPORT extern void *
bsg_node_ged_data_get(const bsg_node *n);

/**
 * Set the GED-private data pointer on @p n (bv_scene_obj::s_u_data).
 *
 * Stores an opaque pointer that libged draw helpers use to associate a
 * `struct ged_bv_data *` with each drawn shape.  No-op if @p n is NULL.
 */
BSG_EXPORT extern void
bsg_node_ged_data_set(bsg_node *n, void *data);

/**
 * Function pointer type for node lifecycle callbacks.
 *
 * The callback receives the node that is about to be freed.  Used with
 * bsg_node_set_free_callback() to register a cleanup hook.
 */
typedef void (*bsg_node_free_fn)(bsg_node *);

/**
 * Function pointer type for node update/generator callbacks.
 *
 * This is the typed BSG wrapper for the legacy bv_scene_obj::s_update_callback
 * signature.
 */
typedef int (*bsg_node_update_fn)(bsg_node *, struct bview *, int);

/**
 * Register a lifecycle callback to be invoked just before @p n is freed.
 *
 * Maps to bv_scene_obj::s_free_callback.  Pass NULL to clear any existing
 * callback.  No-op if @p n is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_free_callback(bsg_node *n, bsg_node_free_fn cb);

/**
 * Invoke the free callback registered on @p n, if any.
 *
 * If no callback has been registered (or @p n is NULL) this is a no-op.
 * Callers that need to trigger cleanup before manually recycling a node
 * should call this instead of accessing s_free_callback directly.
 */
BSG_EXPORT extern void
bsg_node_invoke_free_callback(bsg_node *n);

/**
 * Register a payload/update callback on @p n.
 *
 * Maps to bv_scene_obj::s_update_callback.  Pass NULL to clear any existing
 * callback.  No-op if @p n is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_update_callback(bsg_node *n, bsg_node_update_fn cb);

/**
 * Invoke the update callback registered on @p n, if any.
 *
 * Returns the callback result when one is installed, otherwise 0.  NULL nodes
 * also return 0.
 */
BSG_EXPORT extern int
bsg_node_invoke_update_callback(bsg_node *n, struct bview *v, int flags);

/**
 * Return the legacy "E-flag" for @p n (bv_scene_obj::s_old.s_Eflag).
 *
 * The E-flag is set to 1 for overlays/pseudo-solids generated by the `e`
 * (MGED E-command) operator rather than solid geometry shapes, and is
 * checked by MGED edit code to decide which paths are accessible.  Returns
 * 0 for NULL @p n or when the flag is not set.
 */
BSG_EXPORT extern int
bsg_node_legacy_eflag(const bsg_node *n);

/**
 * Set the legacy E-flag on @p n (bv_scene_obj::s_old.s_Eflag).
 *
 * Pass non-zero to mark @p n as an E-operator overlay; zero to clear.
 * No-op if @p n is NULL.
 */
BSG_EXPORT extern void
bsg_node_set_legacy_eflag(bsg_node *n, int eflag);

/**
 * Return the vlist free-list pointer for @p n (bv_scene_obj::vlfree).
 *
 * This is the allocator free-list that should be passed as the first
 * argument to BV_ADD_VLIST / BV_FREE_VLIST when directly building vlist
 * geometry for @p n.  Typically set to the owning view's free-list at
 * object creation time.
 *
 * Returns NULL if @p n is NULL.
 */
BSG_EXPORT extern struct bu_list *
bsg_node_vlfree(const bsg_node *n);

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
