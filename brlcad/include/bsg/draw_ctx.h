/*                   D R A W _ C T X . H
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
 * Phase 7 Step 10 (drawing_stack_modernization):
 * Draw-tree context stored in the root node's s_i_data.
 *
 * bsg_draw_ctx is a thin wrapper that lets code reach the draw-revision
 * counter without carrying a struct ged * pointer.  The owner (libged)
 * sets draw_rev to point at its own storage (ged_drawable::gd_draw_rev)
 * at root-creation time.  Freeing helpers in bsg_view_obj.c walk the
 * parent chain to the root and bump *draw_rev through this pointer,
 * removing the last struct ged * dependency from the BSG freeing helpers.
 */
/** @{ */
/* @file bsg/draw_ctx.h */

#ifndef BSG_DRAW_CTX_H
#define BSG_DRAW_CTX_H

#include <stdint.h>

struct bsg_node; /* forward-declare to avoid circular includes */

/**
 * Per-root draw-tree context.  Stored in the draw root's s_i_data by
 * the owner that creates the root.  Accessed (read-only pointer) by
 * libbsg helpers that need to bump the revision counter or free objects
 * without access to the owning application's private state.
 *
 * Fields:
 *   draw_rev  - pointer to the owner's structural revision counter
 *               (set by libged to &ged_drawable::gd_draw_rev at root-
 *               creation time, Phase 7 Step 10).
 *   fso       - pointer to the draw-tree's free-object pool node whose
 *               bu_list chain is used for FREE_BV_SCENE_OBJ recycling
 *               (set by libged via bv_set_fsos at root-creation time,
 *               Phase 7 Step 11).  If NULL, individual node->free_scene_obj
 *               pointers are used as fallback.
 */
struct bsg_draw_ctx {
    uint64_t          *draw_rev;  /**< @brief pointer to the owner's revision counter */
    struct bsg_node *fso;     /**< @brief free-object pool for this draw tree */
};

#endif /* BSG_DRAW_CTX_H */

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
