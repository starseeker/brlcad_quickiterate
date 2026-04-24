/*                          P I E C E . H
 * BRL-CAD
 *
 * Copyright (c) 1993-2026 United States Government as represented by
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
/** @file rt/piece.h
 *
 */

#ifndef RT_PIECE_H
#define RT_PIECE_H

#include "common.h"
#include "bu/list.h"
#include "bu/magic.h"
#include "bu/bitv.h"
#include "bu/ptbl.h"
#include "rt/hit.h"
#include "rt/soltab.h"
#include "rt/space_partition.h" /* cutter */

__BEGIN_DECLS

struct rt_i; /* forward declaration */

/**
 * Support for variable length arrays of "struct hit".  Patterned
 * after the libbu/ptbl.c idea.
 */
struct rt_htbl {
    struct bu_list      l;      /**< @brief  linked list for caller's use */
    size_t              end;    /**< @brief  index of first available location */
    size_t              blen;   /**< @brief  # of struct's of storage at *hits */
    struct hit *        hits;   /**< @brief  hits[blen] data storage area */
};
#define RT_CK_HTBL(_p) BU_CKMAG(_p, RT_HTBL_MAGIC, "rt_htbl")

/**
 * Holds onto memory re-used by rt_shootray() from shot to shot.  One
 * of these for each solid which uses pieces.  There is a separate
 * array of these for each cpu.  Storage for the bit vectors is
 * pre-allocated at prep time.  The array is subscripted by
 * st_piecestate_num.  The bit vector is subscripted by values found
 * in rt_piecelist pieces[].
 */
struct rt_piecestate {
    uint32_t            magic;
    long                ray_seqno;      /**< @brief  re_ray_seqno value when this entry was last updated */
    struct soltab *     stp;
    struct bu_bitv *    shot;
    fastf_t             mindist;        /**< @brief  dist ray enters solids bounding volume */
    fastf_t             maxdist;        /**< @brief  dist ray leaves solids bounding volume */
    struct rt_htbl      htab;           /**< @brief  accumulating hits here */
    const union cutter *cutp;           /**< @brief  current bounding volume */
};
#define RT_CK_PIECESTATE(_p) BU_CKMAG(_p, RT_PIECESTATE_MAGIC, "struct rt_piecestate")

/**
 * For each space partitioning cell, there is one of these for each
 * solid in that cell which uses pieces.  Storage for the array is
 * allocated at cut time, and never changes.
 *
 * It is expected that the indices allocated by any solid range from
 * 0..(npieces-1).
 *
 * The piece indices are used as a subscript into a solid-specific
 * table, and also into the 'shot' bitv of the corresponding
 * rt_piecestate.
 *
 * The values (subscripts) in pieces[] are specific to a single solid
 * (stp).
 */
struct rt_piecelist {
    uint32_t            magic;
    size_t              npieces;        /**< @brief  number of pieces in pieces[] array */
    long                *pieces;        /**< @brief  pieces[npieces], piece indices */
    struct soltab       *stp;           /**< @brief  ref back to solid */
};
#define RT_CK_PIECELIST(_p) BU_CKMAG(_p, RT_PIECELIST_MAGIC, "struct rt_piecelist")


/**
 * Per-application state for solid-piece ray shooting.
 *
 * Allocated and attached to struct application by rt_shootray() the
 * first time it finds the model contains piece-capable solids.
 * Freed by rt_ap_pieces_clean().
 *
 * The pieces[] array is indexed by stp->st_piecestate_num and is
 * sized to rti_nsolids_with_pieces.  It persists across ray calls so
 * that per-solid per-ray state can be lazily re-validated using the
 * ps_ray_seqno counter.  The pending list is reset to empty at the
 * start of each rt_shootray() call.
 *
 * When the model changes (e.g. after rt_reprep()), the stale set is
 * detected by comparing npieces against rti_nsolids_with_pieces, and
 * rt_ap_pieces_init() transparently reallocates it.
 */
struct rt_piecestate_set {
    uint32_t                magic;         /**< @brief RT_PIECESTATE_SET_MAGIC */
    struct rt_i *           rtip;          /**< @brief which rtip these pieces are for */
    size_t                  npieces;       /**< @brief size of pieces[] array */
    long                    ps_ray_seqno;  /**< @brief per-ray counter for piece-state dedup (was re_ray_seqno) */
    struct rt_piecestate *  pieces;        /**< @brief pieces[npieces], indexed by stp->st_piecestate_num */
    struct bu_ptbl          pending;       /**< @brief pieces with an odd hit pending */
};
#define RT_CK_PIECESTATE_SET(_p) BU_CKMAG(_p, RT_PIECESTATE_SET_MAGIC, "struct rt_piecestate_set")


__END_DECLS

#endif /* RT_PIECE_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
