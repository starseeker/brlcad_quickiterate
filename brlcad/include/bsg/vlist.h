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
 * Self-contained vlist utility functions for BSG scene-graph
 * construction.  These functions operate on the bv_vlist type (which
 * will eventually migrate from libbv into libbsg) but do NOT call any
 * compiled libbv symbol — they are implemented directly in libbsg so
 * that the libbsg build does not acquire a runtime dependency on libbv.
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
 * Returns 0 when @p vlist is NULL.
 */
BSG_EXPORT extern size_t
bsg_vlist_cmd_cnt(struct bv_vlist *vlist);

/**
 * Compute the axis-aligned bounding box of all point-bearing commands
 * in the vlist rooted at @p vlistp, storing the min/max corners in
 * @p bmin and @p bmax.
 *
 * Returns the last unrecognised command code encountered (non-zero), or
 * 0 if all commands were processed successfully.  Mirrors the behaviour
 * of bv_vlist_bbox().
 */
BSG_EXPORT extern int
bsg_vlist_bbox(struct bu_list *vlistp, point_t *bmin, point_t *bmax);

/**
 * Copy all vlist commands from @p src into @p dest, allocating new
 * chunks from @p vlists as needed.
 */
BSG_EXPORT extern void
bsg_vlist_copy(struct bu_list *vlists,
	       struct bu_list *dest,
	       const struct bu_list *src);

/**
 * Return all bv_vlist chunks in @p hd to the heap.
 *
 * After this call @p hd is empty and all its chunks have been freed.
 * Equivalent to bv_vlist_cleanup() but implemented directly in libbsg.
 */
BSG_EXPORT extern void
bsg_vlist_cleanup(struct bu_list *hd);

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
 * Slice 6 (bv_scene_obj_migrate): BSG-native 3-D vector-font string.
 *
 * Converts @p string to stroked vlist commands in 3-D, appending them
 * to @p vhead using @p free_hd as the chunk free-list.
 *
 * Parameters:
 *   @p vhead    destination vlist head
 *   @p free_hd  chunk free-list (must be an initialized bu_list head)
 *   @p string   NUL-terminated text to stroke
 *   @p origin   lower-left corner of the first character (model coords)
 *   @p rot      4×4 transform matrix applied to each character position
 *               (WARNING: may translate as well as rotate)
 *   @p scale    character width in model-space units
 *
 * Implemented directly in libbsg; does not call bv_vlist_3string().
 */
BSG_EXPORT extern void
bsg_vlist_3string(struct bu_list *vhead,
		  struct bu_list *free_hd,
		  const char *string,
		  const point_t origin,
		  const mat_t rot,
		  double scale);


/**
 * Slice 6 (bv_scene_obj_migrate): BSG-native 2-D vector-font string.
 *
 * Converts @p string to stroked vlist commands in the X-Y plane,
 * appending them to @p vhead using @p free_hd as the chunk free-list.
 *
 * Parameters:
 *   @p vhead    destination vlist head
 *   @p free_hd  chunk free-list (must be an initialized bu_list head)
 *   @p string   NUL-terminated text to stroke
 *   @p x        X coordinate of the lower-left of the first character
 *   @p y        Y coordinate of the lower-left of the first character
 *   @p scale    character width in model-space units
 *   @p theta    counter-clockwise rotation in degrees from the +X axis
 *
 * Implemented directly in libbsg; does not call bv_vlist_2string().
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
