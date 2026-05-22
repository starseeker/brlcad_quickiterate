/*                    O V E R L A Y . H
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
 * Phase 7 Step 13 (drawing_stack_modernization):
 * Pure-BSG overlay group helpers — no dependency on GED or librt types.
 *
 * These functions manage the special @c _overlays BSG_NODE_GROUP that lives
 * as a direct child of the draw root and collects all pseudo-solid / invented
 * overlay shapes.  They carry no dependency on @c struct @c ged,
 * @c struct @c db_i, or @c struct @c rt_wdb, making them safe to implement in
 * libbsg.  GED-specific logic (db_lookup guard, callback registration,
 * vlist generation) remains in @c src/libged/bsg_ged_draw.c.
 */
/** @{ */
/* @file bsg/overlay.h */

#ifndef BSG_OVERLAY_H
#define BSG_OVERLAY_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

struct bview;          /* forward-declare to avoid circular includes */


/**
 * Return the @c _overlays BSG_NODE_GROUP child of @p draw_root, or NULL if
 * it has not been created yet.
 *
 * Does NOT create the group.  Use bsg_ensure_overlay_group() for that.
 */
BSG_EXPORT extern bsg_node *
bsg_find_overlay_group(bsg_node *draw_root);


/**
 * Return the @c _overlays BSG_NODE_GROUP child of @p draw_root, creating it
 * on first call.
 *
 * @p v  The view used to allocate the new group node via bsg_obj_create().
 *       If NULL and the group does not yet exist, the function returns NULL.
 *
 * Returns the (possibly newly created) @c _overlays group, or NULL on
 * allocation failure.
 */
BSG_EXPORT extern bsg_node *
bsg_ensure_overlay_group(bsg_node *draw_root, struct bview *v);


/**
 * Erase all overlay shapes named @p name from the @c _overlays group.
 *
 * Releases each matching shape's backend state via
 * bsg_scene_obj_release_backend() and fires its @c s_free_callback before
 * recycling the node.  Bumps the draw-tree revision counter.  Removes the
 * @c _overlays group itself if it becomes empty after the erasure.
 *
 * Does nothing if @p draw_root is NULL or has no @c _overlays child.
 */
BSG_EXPORT extern void
bsg_erase_overlay_by_name(bsg_node *draw_root, const char *name);


__END_DECLS

#endif /* BSG_OVERLAY_H */

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
