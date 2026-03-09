/*                         P N T S . H
 * BRL-CAD
 *
 * Copyright (c) 2004-2025 United States Government as represented by
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

/*----------------------------------------------------------------------*/
/* @file pnts.h */
/** @addtogroup bg_pnts */
/** @{ */

/**
 *  @brief Routines for computing axis-aligned and oriented bounding boxes
 *  from point sets.
 */

#ifndef BG_PNTS_H
#define BG_PNTS_H

#include "common.h"
#include "vmath.h"
#include "bg/defines.h"

__BEGIN_DECLS

/**
 * @brief
 * Calculate the axis-aligned bounding box for a point set.
 *
 * Returns BRLCAD_OK if successful, else BRLCAD_ERROR.
 *
 * @param[out] min XYZ coordinate defining the minimum bbox point
 * @param[out] max XYZ coordinate defining the maximum bbox point
 * @param[in]  p   array of points
 * @param[in]  num_pnts number of points in p
 */
BG_EXPORT extern int
bg_pnts_aabb(point_t *min, point_t *max, const point_t *p, size_t num_pnts);

/**
 * @brief
 * Calculate an oriented bounding box for a point set.
 *
 * The OBB is returned as a center point and three half-extent vectors.
 * Each extent vector points along one axis of the box; its magnitude is
 * the half-length along that axis.  To recover the 8 corner points use
 * bg_obb_pnts().
 *
 * Returns BRLCAD_OK if successful, else BRLCAD_ERROR.
 *
 * @param[out] c  XYZ coordinate of the bbox center
 * @param[out] v1 first  half-extent vector of the bbox
 * @param[out] v2 second half-extent vector of the bbox
 * @param[out] v3 third  half-extent vector of the bbox
 * @param[in]  p  array of points
 * @param[in]  num_pnts number of points in p
 */
BG_EXPORT extern int
bg_pnts_obb(point_t *c, vect_t *v1, vect_t *v2, vect_t *v3,
	    const point_t *p, size_t num_pnts);

/**
 * @brief
 * Compute the 8 corner vertices of an oriented bounding box.
 *
 * The OBB is described by its center point @p c and three half-extent vectors
 * @p v1, @p v2, @p v3 (as returned by bg_pnts_obb() or bg_trimesh_obb()).
 * The caller must supply @p verts with storage for at least 8 point_t values.
 *
 * Output vertices follow the librt arb8 convention:
 * @verbatim
 *   verts[0]: c - v1 - v2 - v3  (000)
 *   verts[1]: c - v1 + v2 - v3  (010)
 *   verts[2]: c - v1 + v2 + v3  (011)
 *   verts[3]: c - v1 - v2 + v3  (001)
 *   verts[4]: c + v1 - v2 - v3  (100)
 *   verts[5]: c + v1 + v2 - v3  (110)
 *   verts[6]: c + v1 + v2 + v3  (111)
 *   verts[7]: c + v1 - v2 + v3  (101)
 * @endverbatim
 *
 * Returns BRLCAD_OK if successful, else BRLCAD_ERROR.
 *
 * @param[out] verts array of 8 XYZ coordinates (caller-supplied)
 * @param[in]  c     center point of the OBB
 * @param[in]  v1    first  half-extent vector
 * @param[in]  v2    second half-extent vector
 * @param[in]  v3    third  half-extent vector
 */
BG_EXPORT extern int
bg_obb_pnts(point_t *verts,
	    const point_t *c, const vect_t *v1, const vect_t *v2, const vect_t *v3);

__END_DECLS

#endif  /* BG_PNTS_H */
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
