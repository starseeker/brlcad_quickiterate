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
#include "bsg/defines.h"
#include "bsg/field.h"

__BEGIN_DECLS

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

BSG_EXPORT extern void
bsg_node_bounds_get(const bsg_node *n, point_t bmin, point_t bmax);

BSG_EXPORT extern void
bsg_node_bounds_set(bsg_node *n, const point_t bmin, const point_t bmax);

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
