/*                     L O D _ O P S . H
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
 * Phase L0 (drawing_stack_modernization):
 * LoD selector node — vtable, per-view cursor, and payload for BSG_NODE_LOD.
 *
 * A BSG_NODE_LOD node sits between the path-group node and its level
 * representation children.  It carries:
 *
 *   - a bsg_lod_ops vtable that supplies the level-selection policy
 *     (implemented by libbv for mesh LoD and by libged for CSG wireframe LoD);
 *   - an opaque user_data pointer for the policy implementation;
 *   - a small per-view cursor map (view * → bsg_lod_view_cursor) that
 *     tracks the currently selected level and the view metrics that were
 *     current at the time of selection.
 *
 * The cursor map replaces the per-view duplicate bsg_node subtrees
 * (bsg_node::i->vobjs) for LoD-managed paths.  Levels are ordinary
 * BSG_NODE_SHAPE children of the LoD node; the LoD node selects which
 * child the render traversal should visit.
 *
 * This header is used by libbsg, libbv, and libged.  libbsg owns the
 * node lifecycle; libbv and libged supply the bsg_lod_ops vtable and
 * the user_data.
 */
/** @{ */
/* @file bsg/lod_ops.h */

#ifndef BSG_LOD_OPS_H
#define BSG_LOD_OPS_H

#include "common.h"
#include <stddef.h>
#include <stdint.h>
#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

struct bsg_view;   /* forward declaration */

/* ------------------------------------------------------------------ */
/* Per-view LoD cursor                                                  */
/* ------------------------------------------------------------------ */

/**
 * Snapshot of the view metrics that were current the last time
 * select_level() was called for this (node, view) pair.
 *
 * Fields:
 *   v                - the associated view pointer (used as map key).
 *   level            - level index selected (-1 = none yet).
 *   view_scale       - gv_scale at last select_level.
 *   curve_scale      - gv_s->curve_scale at last select_level.
 *   point_scale      - gv_s->point_scale at last select_level.
 *   perspective_flag - non-zero if perspective was on.
 *   last_frame_rev   - gv_frame_rev at the time of the last select.
 */
struct bsg_lod_view_cursor {
    struct bsg_view *v;
    int           level;
    fastf_t       view_scale;
    fastf_t       curve_scale;
    fastf_t       point_scale;
    int           perspective_flag;
    uint64_t      last_frame_rev;
};


/* ------------------------------------------------------------------ */
/* Level-of-detail ops vtable                                           */
/* ------------------------------------------------------------------ */

/**
 * Policy vtable installed on a BSG_NODE_LOD node.
 *
 * Implementations are supplied by:
 *   - libbv (mesh pop-buffer LoD): wraps bsg_mesh_lod_view.
 *   - libged (CSG adaptive wireframe): wraps csg_wireframe_update /
 *     ft_adaptive_plot.
 *
 * All callbacks receive the LoD node pointer.  The policy state is
 * accessible from the payload's user_data field.
 */
struct bsg_lod_ops {
    /**
     * Choose which level index (0-based) to use for view @p v.
     *
     * The return value is an index into the LoD node's children ptbl.
     * Must return 0 when uncertain (level 0 = best quality or fallback).
     * The cursor for view @p v must already exist when this is called;
     * the ops implementation may consult it via bsg_lod_node_get_cursor.
     */
    int  (*select_level)(bsg_node *node, struct bsg_view *v);

    /**
     * Make level @p level the active representation for view @p v.
     *
     * Called after select_level() if is_stale() reported that a change
     * is needed.  The ops implementation must ensure the level child
     * exists (lazily creating it if necessary) and update the cursor.
     *
     * @p level is guaranteed to be in [0, number_of_children).
     */
    void (*activate_level)(bsg_node *node, struct bsg_view *v, int level);

    /**
     * Return non-zero if the cached level for view @p v is no longer
     * appropriate given the current view metrics.
     *
     * Returning 0 is always safe (suppresses work); returning 1 triggers
     * a select_level → activate_level cycle.
     */
    int  (*is_stale)(bsg_node *node, struct bsg_view *v);

    /**
     * Release any resources allocated by the ops implementation
     * (e.g. the mesh-LoD context handle or the CSG driver state).
     * Called when the LoD node is destroyed.  May be NULL.
     */
    void (*free)(bsg_node *node);
};


/* ------------------------------------------------------------------ */
/* LoD node payload (stored in bsg_node::s_i_data)                 */
/* ------------------------------------------------------------------ */

/**
 * Payload allocated for every BSG_NODE_LOD node.  Stored in the node's
 * s_i_data field; freed by the node's s_free_callback.
 */
struct bsg_lod_payload {
    struct bsg_lod_ops *ops;        /**< @brief level-selection vtable */
    void               *user_data;  /**< @brief opaque policy context  */

    /* Per-view cursor array (small, linear — typically 1-4 views). */
    struct bsg_lod_view_cursor *cursors; /**< @brief malloc'd cursor array */
    size_t cursor_alloc;                 /**< @brief allocated slots       */
    size_t cursor_count;                 /**< @brief used slots            */
};


/* ------------------------------------------------------------------ */
/* BSG_NODE_LOD lifecycle API                                           */
/* ------------------------------------------------------------------ */

/**
 * Allocate a BSG_NODE_LOD node associated with view @p v.
 *
 * The node is NOT linked into any parent; the caller must attach it
 * (e.g. via bsg_group_add_child) after configuring the ops vtable.
 *
 * Returns NULL on failure.
 */
BSG_EXPORT extern bsg_node *
bsg_lod_node_create(struct bsg_view *v);

/**
 * Install the level-selection ops vtable @p ops and opaque policy
 * context @p user_data on an existing BSG_NODE_LOD node @p node.
 *
 * Safe to call more than once; replaces any previous ops.
 * No-op if @p node is NULL or is not a BSG_NODE_LOD node.
 */
BSG_EXPORT extern void
bsg_lod_node_set_ops(bsg_node *node,
		     struct bsg_lod_ops *ops,
		     void *user_data);

/**
 * Append @p level_node as the next level representation child of the
 * BSG_NODE_LOD node @p lod_node.
 *
 * The level index for the appended child is BU_PTBL_LEN(children) - 1
 * after insertion.  Level 0 is the highest quality (or the only level
 * if only one is present).
 *
 * No-op if either argument is NULL, or @p lod_node is not a
 * BSG_NODE_LOD node.
 */
BSG_EXPORT extern void
bsg_lod_node_attach_level(bsg_node *lod_node, bsg_node *level_node);

/**
 * Return a pointer to the per-view cursor for view @p v in the LoD
 * node @p node, creating a new cursor entry if one does not yet exist.
 *
 * The returned pointer is valid until the next bsg_lod_node_get_cursor
 * call that triggers a reallocation of the cursor array.  In practice
 * callers use the cursor immediately (e.g. to read level and metrics)
 * and do not cache the pointer.
 *
 * Returns NULL if @p node is NULL, not a BSG_NODE_LOD node, or on
 * allocation failure.
 */
BSG_EXPORT extern struct bsg_lod_view_cursor *
bsg_lod_node_get_cursor(bsg_node *node, struct bsg_view *v);

/**
 * Return the currently active level index for view @p v in LoD node
 * @p node.  Returns -1 if no level has been selected yet or if the
 * cursor does not exist for this view.
 */
BSG_EXPORT extern int
bsg_lod_node_active_level(bsg_node *node, struct bsg_view *v);

/**
 * Return the number of level children currently attached to LoD node
 * @p node.  Returns 0 if @p node is NULL or not a BSG_NODE_LOD node.
 */
BSG_EXPORT extern int
bsg_lod_node_level_count(bsg_node *node);

/**
 * Insert a new BSG_NODE_LOD node between @p leaf and its current parent.
 *
 * The new LoD node is inserted in the same parent-child slot formerly
 * occupied by @p leaf, and @p leaf is attached as level-0 child of the
 * new LoD node.
 *
 * Returns the new LoD node, or NULL on failure.
 */
BSG_EXPORT extern bsg_node *
bsg_lod_node_insert_above(bsg_node *leaf, struct bsg_view *v);

__END_DECLS

#endif /* BSG_LOD_OPS_H */

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
