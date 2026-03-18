/*                         P N T S . C P P
 * BRL-CAD
 *
 * Copyright (c) 2016-2025 United States Government as represented by
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
/** @file pnts.cpp
 *
 * Axis-aligned and oriented bounding box routines for point sets.
 *
 * bg_pnts_aabb  — axis-aligned bbox for a flat array of points
 * bg_pnts_obb   — oriented bbox for a flat array of points (GTE)
 * bg_obb_pnts   — reconstruct the 8 corner vertices from OBB center+extents
 */

#include "common.h"

#include <array>
#include <cstring>

#include "Mathematics/ContOrientedBox3.h"

#include "vmath.h"
#include "bu/malloc.h"
#include "bg/pnts.h"


int
bg_pnts_aabb(point_t *min, point_t *max, const point_t *p, size_t num_pnts)
{
    if (!min || !max)
	return BRLCAD_ERROR;

    /* Sentinel values: if anything goes wrong callers see "obviously wrong" */
    VSETALL((*min),  INFINITY);
    VSETALL((*max), -INFINITY);

    if (!p || num_pnts == 0)
	return BRLCAD_ERROR;

    for (size_t i = 0; i < num_pnts; i++)
	VMINMAX((*min), (*max), p[i]);

    /* Guarantee non-zero volume */
    if (NEAR_EQUAL((*min)[X], (*max)[X], SMALL_FASTF)) {
	(*min)[X] -= SMALL_FASTF;
	(*max)[X] += SMALL_FASTF;
    }
    if (NEAR_EQUAL((*min)[Y], (*max)[Y], SMALL_FASTF)) {
	(*min)[Y] -= SMALL_FASTF;
	(*max)[Y] += SMALL_FASTF;
    }
    if (NEAR_EQUAL((*min)[Z], (*max)[Z], SMALL_FASTF)) {
	(*min)[Z] -= SMALL_FASTF;
	(*max)[Z] += SMALL_FASTF;
    }

    return BRLCAD_OK;
}


int
bg_pnts_obb(point_t *c, vect_t *v1, vect_t *v2, vect_t *v3,
	    const point_t *p, size_t num_pnts)
{
    if (!c || !v1 || !v2 || !v3)
	return BRLCAD_ERROR;

    /* Sentinel values */
    VSETALL((*c), 0);
    VSET((*v1), INFINITY, 0, 0);
    VSET((*v2), 0, INFINITY, 0);
    VSET((*v3), 0, 0, INFINITY);

    if (!p || num_pnts == 0)
	return BRLCAD_ERROR;

    gte::OrientedBox3<fastf_t> gte_bb;
    if (!gte::GetContainer((int)num_pnts, (const gte::Vector3<fastf_t> *)p, gte_bb))
	return BRLCAD_ERROR;

    VSET(*c, gte_bb.center[0], gte_bb.center[1], gte_bb.center[2]);

    VSET(*v1, gte_bb.axis[0][0], gte_bb.axis[0][1], gte_bb.axis[0][2]);
    if (!NEAR_ZERO(gte_bb.extent[0], VUNITIZE_TOL))
	VSCALE(*v1, *v1, gte_bb.extent[0]);

    VSET(*v2, gte_bb.axis[1][0], gte_bb.axis[1][1], gte_bb.axis[1][2]);
    if (!NEAR_ZERO(gte_bb.extent[1], VUNITIZE_TOL))
	VSCALE(*v2, *v2, gte_bb.extent[1]);

    VSET(*v3, gte_bb.axis[2][0], gte_bb.axis[2][1], gte_bb.axis[2][2]);
    if (!NEAR_ZERO(gte_bb.extent[2], VUNITIZE_TOL))
	VSCALE(*v3, *v3, gte_bb.extent[2]);

    return BRLCAD_OK;
}


int
bg_obb_pnts(point_t *verts,
	    const point_t *c, const vect_t *v1, const vect_t *v2, const vect_t *v3)
{
    if (!c || !v1 || !v2 || !v3 || !verts)
	return BRLCAD_ERROR;

    /* Build a GTE OrientedBox3 from center + unit-axis + extent */
    vect_t evects[3];
    VMOVE(evects[0], *v1);
    VMOVE(evects[1], *v2);
    VMOVE(evects[2], *v3);

    fastf_t emags[3];
    for (int i = 0; i < 3; i++)
	emags[i] = MAGNITUDE(evects[i]);

    for (int i = 0; i < 3; i++)
	VUNITIZE(evects[i]);

    gte::OrientedBox3<fastf_t> gte_bb;
    gte_bb.center[0] = (*c)[X];
    gte_bb.center[1] = (*c)[Y];
    gte_bb.center[2] = (*c)[Z];
    for (int i = 0; i < 3; i++) {
	gte_bb.extent[i] = emags[i];
	for (int j = 0; j < 3; j++)
	    gte_bb.axis[i][j] = evects[i][j];
    }

    std::array<gte::Vector3<fastf_t>, 8> cv;
    gte_bb.GetVertices(cv);

    /* Reorder GTE vertices to the librt arb8 convention used by
     * rt_bot_oriented_bbox / dbi2:
     *   V0: 0,0,0   V1: 0,1,0   V2: 0,1,1   V3: 0,0,1
     *   V4: 1,0,0   V5: 1,1,0   V6: 1,1,1   V7: 1,0,1
     */
    VSET(verts[0], cv[0][0], cv[0][1], cv[0][2]);
    VSET(verts[1], cv[1][0], cv[1][1], cv[1][2]);
    VSET(verts[2], cv[3][0], cv[3][1], cv[3][2]);
    VSET(verts[3], cv[2][0], cv[2][1], cv[2][2]);
    VSET(verts[4], cv[4][0], cv[4][1], cv[4][2]);
    VSET(verts[5], cv[5][0], cv[5][1], cv[5][2]);
    VSET(verts[6], cv[7][0], cv[7][1], cv[7][2]);
    VSET(verts[7], cv[6][0], cv[6][1], cv[6][2]);

    return BRLCAD_OK;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
