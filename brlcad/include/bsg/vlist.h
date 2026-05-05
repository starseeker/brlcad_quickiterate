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
