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

/* -----------------------------------------------------------------------
 * BSG vlist struct alias
 *
 * bsg_vlist is a layout-compatible alias for struct bv_vlist.  New BSG
 * code should use bsg_vlist; the underlying struct is unchanged.
 * ----------------------------------------------------------------------- */

/** @brief BSG alias for struct bv_vlist */
typedef struct bv_vlist bsg_vlist;

/* -----------------------------------------------------------------------
 * BSG vlist command constants (aliases for BV_VLIST_* values)
 * ----------------------------------------------------------------------- */
#define BSG_VLIST_CHUNK              BV_VLIST_CHUNK

#define BSG_VLIST_LINE_MOVE          BV_VLIST_LINE_MOVE
#define BSG_VLIST_LINE_DRAW          BV_VLIST_LINE_DRAW
#define BSG_VLIST_POLY_START         BV_VLIST_POLY_START
#define BSG_VLIST_POLY_MOVE          BV_VLIST_POLY_MOVE
#define BSG_VLIST_POLY_DRAW          BV_VLIST_POLY_DRAW
#define BSG_VLIST_POLY_END           BV_VLIST_POLY_END
#define BSG_VLIST_POLY_VERTNORM      BV_VLIST_POLY_VERTNORM
#define BSG_VLIST_TRI_START          BV_VLIST_TRI_START
#define BSG_VLIST_TRI_MOVE           BV_VLIST_TRI_MOVE
#define BSG_VLIST_TRI_DRAW           BV_VLIST_TRI_DRAW
#define BSG_VLIST_TRI_END            BV_VLIST_TRI_END
#define BSG_VLIST_TRI_VERTNORM       BV_VLIST_TRI_VERTNORM
#define BSG_VLIST_POINT_DRAW         BV_VLIST_POINT_DRAW
#define BSG_VLIST_POINT_SIZE         BV_VLIST_POINT_SIZE
#define BSG_VLIST_LINE_WIDTH         BV_VLIST_LINE_WIDTH
#define BSG_VLIST_DISPLAY_MAT        BV_VLIST_DISPLAY_MAT
#define BSG_VLIST_MODEL_MAT          BV_VLIST_MODEL_MAT
#define BSG_VLIST_CMD_MAX            BV_VLIST_CMD_MAX

/* -----------------------------------------------------------------------
 * BSG vlist operation macros (aliases for BV_* list macros)
 * ----------------------------------------------------------------------- */
#define BSG_GET_VLIST(_free_hd, p)           BV_GET_VLIST(_free_hd, p)
#define BSG_FREE_VLIST(_free_hd, hd)         BV_FREE_VLIST(_free_hd, hd)
#define BSG_ADD_VLIST(_free_hd, _dest_hd, pnt, draw) \
    BV_ADD_VLIST(_free_hd, _dest_hd, pnt, draw)
#define BSG_VLIST_SET_DISP_MAT(_fh, _dh, _rp) \
    BV_VLIST_SET_DISP_MAT(_fh, _dh, _rp)
#define BSG_VLIST_SET_MODEL_MAT(_fh, _dh)   BV_VLIST_SET_MODEL_MAT(_fh, _dh)
#define BSG_VLIST_SET_POINT_SIZE(_fh, _dh, _sz) \
    BV_VLIST_SET_POINT_SIZE(_fh, _dh, _sz)
#define BSG_VLIST_SET_LINE_WIDTH(_fh, _dh, _w) \
    BV_VLIST_SET_LINE_WIDTH(_fh, _dh, _w)

/* -----------------------------------------------------------------------
 * BSG vlist API
 * ----------------------------------------------------------------------- */

/**
 * Count the total number of vlist commands in @p vlist.
 * Identical in behavior to bv_vlist_cmd_cnt() but exported under the
 * BSG namespace for use in BSG scene-graph construction code.
 * Returns 0 when @p vlist is NULL.
 */
BSG_EXPORT extern size_t
bsg_vlist_cmd_cnt(bsg_vlist *vlist);

/**
 * Duplicate the contents of a vlist.  BSG namespace alias for
 * bv_vlist_copy().  The copy may be more densely packed than the source.
 */
BSG_EXPORT extern void
bsg_vlist_copy(struct bu_list *vlists,
               struct bu_list *dest,
               const struct bu_list *src);

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
