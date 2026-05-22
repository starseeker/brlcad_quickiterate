/*                        V L I S T . H
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
 * Vlist helpers for the BSG scene graph.
 *
 * These functions wrap or extend the libbv vlist API (bv/vlist.h) for
 * use in BSG scene-graph construction.
 */
/** @{ */
/* @file bsg/vlist.h */

#ifndef BSG_VLIST_H
#define BSG_VLIST_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bv/vlist.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Count the total number of vlist commands in @p vlist.
 * Identical in behaviour to bv_vlist_cmd_cnt() but exported under the
 * BSG namespace for use in BSG scene-graph construction code.
 * Returns 0 when @p vlist is NULL.
 */
BSG_EXPORT extern size_t
bsg_vlist_cmd_cnt(struct bv_vlist *vlist);

/**
 * Emit wireframe vlist commands for an ARB8 defined by the eight
 * points @p pts into @p vhead, using @p vlfree as the free-list
 * allocator.
 *
 * Drawing strategy (18 commands):
 *   bottom loop  MOVE(0) DRAW(1) DRAW(2) DRAW(3) DRAW(0)   — 5 cmds
 *   top loop     MOVE(4) DRAW(5) DRAW(6) DRAW(7) DRAW(4)   — 5 cmds
 *   4 verticals  MOVE(0)/DRAW(4)  MOVE(1)/DRAW(5)
 *                MOVE(2)/DRAW(6)  MOVE(3)/DRAW(7)           — 8 cmds
 *
 * Total: 18 vlist commands, each unique edge drawn exactly once.
 */
BSG_EXPORT extern void
bsg_vlist_arb8(struct bu_list *vhead, struct bu_list *vlfree, point_t pts[8]);


/**
 * Slice 6 (bv_scene_obj_migrate): BSG-namespaced 3-D vector-font string.
 *
 * Converts @p string to stroked vlist commands in 3-D, appending them
 * to @p vhead using @p free_hd as the chunk free-list.
 *
 * Parameters mirror bv_vlist_3string() exactly:
 *   @p vhead    destination vlist head
 *   @p free_hd  chunk free-list (must be an initialized bu_list head)
 *   @p string   NUL-terminated text to stroke
 *   @p origin   lower-left corner of the first character (model coords)
 *   @p rot      4×4 transform matrix applied to each character position
 *               (WARNING: may translate as well as rotate)
 *   @p scale    character width in model-space units (mm)
 *
 * Wraps bv_vlist_3string() so callers do not need to include bv/vlist.h.
 */
BSG_EXPORT extern void
bsg_vlist_3string(struct bu_list *vhead,
		  struct bu_list *free_hd,
		  const char *string,
		  const point_t origin,
		  const mat_t rot,
		  double scale);


/**
 * Slice 6 (bv_scene_obj_migrate): BSG-namespaced 2-D vector-font string.
 *
 * Converts @p string to stroked vlist commands in the X-Y plane,
 * appending them to @p vhead using @p free_hd as the chunk free-list.
 *
 * Parameters mirror bv_vlist_2string() exactly:
 *   @p vhead    destination vlist head
 *   @p free_hd  chunk free-list (must be an initialized bu_list head)
 *   @p string   NUL-terminated text to stroke
 *   @p x        X coordinate of the lower-left of the first character
 *   @p y        Y coordinate of the lower-left of the first character
 *   @p scale    character width in model-space units (mm)
 *   @p theta    counter-clockwise rotation in degrees from the +X axis
 *
 * Wraps bv_vlist_2string() so callers do not need to include bv/vlist.h.
 */
BSG_EXPORT extern void
bsg_vlist_2string(struct bu_list *vhead,
		  struct bu_list *free_hd,
		  const char *string,
		  double x,
		  double y,
		  double scale,
		  double theta);

__END_DECLS

#endif /* BSG_VLIST_H */

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
