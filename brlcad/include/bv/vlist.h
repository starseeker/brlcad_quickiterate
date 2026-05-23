/*                        V L I S T . H
 * BRL-CAD
 *
 * Copyright (c) 2004-2026 United States Government as represented by
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
/** @addtogroup bv_vlist
 *
 * Backward-compatibility bridge. All definitions now live in bsg/vlist.h.
 *
 * TODO: migrate all callers to bsg/vlist.h then delete this file.
 */
/** @{ */
/** @file bv/vlist.h */

#ifndef BV_VLIST_H
#define BV_VLIST_H

#include "bsg/vlist.h"

/* Compat aliases for callers not yet migrated to bsg/vlist.h */
#define bv_vlist bsg_vlist

#define BV_VLIST_CHUNK              BSG_VLIST_CHUNK
#define BV_VLIST_NULL               BSG_VLIST_NULL
#define BV_CK_VLIST(_p)             BSG_CK_VLIST(_p)

#define BV_VLIST_LINE_MOVE          BSG_VLIST_LINE_MOVE
#define BV_VLIST_LINE_DRAW          BSG_VLIST_LINE_DRAW
#define BV_VLIST_POLY_START         BSG_VLIST_POLY_START
#define BV_VLIST_POLY_MOVE          BSG_VLIST_POLY_MOVE
#define BV_VLIST_POLY_DRAW          BSG_VLIST_POLY_DRAW
#define BV_VLIST_POLY_END           BSG_VLIST_POLY_END
#define BV_VLIST_POLY_VERTNORM      BSG_VLIST_POLY_VERTNORM
#define BV_VLIST_TRI_START          BSG_VLIST_TRI_START
#define BV_VLIST_TRI_MOVE           BSG_VLIST_TRI_MOVE
#define BV_VLIST_TRI_DRAW           BSG_VLIST_TRI_DRAW
#define BV_VLIST_TRI_END            BSG_VLIST_TRI_END
#define BV_VLIST_TRI_VERTNORM       BSG_VLIST_TRI_VERTNORM
#define BV_VLIST_POINT_DRAW         BSG_VLIST_POINT_DRAW
#define BV_VLIST_POINT_SIZE         BSG_VLIST_POINT_SIZE
#define BV_VLIST_LINE_WIDTH         BSG_VLIST_LINE_WIDTH
#define BV_VLIST_DISPLAY_MAT        BSG_VLIST_DISPLAY_MAT
#define BV_VLIST_MODEL_MAT          BSG_VLIST_MODEL_MAT
#define BV_VLIST_CMD_MAX            BSG_VLIST_CMD_MAX

#define BV_GET_VLIST(_fh, p)                    BSG_GET_VLIST(_fh, p)
#define BV_FREE_VLIST(_fh, hd)                  BSG_FREE_VLIST(_fh, hd)
#define BV_ADD_VLIST(_fh, _dh, pnt, draw)       BSG_ADD_VLIST(_fh, _dh, pnt, draw)
#define BV_VLIST_SET_DISP_MAT(_fh, _dh, _rp)   BSG_VLIST_SET_DISP_MAT(_fh, _dh, _rp)
#define BV_VLIST_SET_MODEL_MAT(_fh, _dh)        BSG_VLIST_SET_MODEL_MAT(_fh, _dh)
#define BV_VLIST_SET_POINT_SIZE(_fh, _dh, _sz)  BSG_VLIST_SET_POINT_SIZE(_fh, _dh, _sz)
#define BV_VLIST_SET_LINE_WIDTH(_fh, _dh, _w)   BSG_VLIST_SET_LINE_WIDTH(_fh, _dh, _w)

#endif  /* BV_VLIST_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
