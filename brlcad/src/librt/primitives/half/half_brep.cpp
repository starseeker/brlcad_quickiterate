/*                    H A L F _ B R E P . C P P
 * BRL-CAD
 *
 * Copyright (c) 2012-2025 United States Government as represented by
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
/** @file half_brep.cpp
 *
 * Convert a halfspace primitive to b-rep form.
 *
 * The halfspace is an infinite solid bounded by a plane, so it cannot be
 * represented exactly as a finite b-rep.  Instead, we produce a large box
 * whose boundary face lies exactly on the halfspace plane and whose other
 * five faces are far behind the plane (deep inside the halfspace interior).
 * The approximation is suitable for Boolean evaluation against finite solids.
 *
 */

#include "common.h"

#include "raytrace.h"
#include "rt/geom.h"
#include "brep.h"


extern "C" void
rt_hlf_brep(ON_Brep **b, const struct rt_db_internal *ip, const struct bn_tol *)
{
    struct rt_half_internal *hip;

    RT_CK_DB_INTERNAL(ip);
    hip = (struct rt_half_internal *)ip->idb_ptr;
    RT_HALF_CK_MAGIC(hip);

    /* The halfspace is defined by the plane equation N*P = d where
     * N = (eqn[X], eqn[Y], eqn[Z]) is the outward unit normal and
     * d = eqn[W].  The interior satisfies N*P <= d.
     *
     * Approximate with a large box: one face lies on the plane N*P = d
     * (the halfspace boundary) and the opposite face is displaced S units
     * into the interior (in the -N direction).  The four side faces connect
     * them, completing the closed solid.
     */
    ON_3dVector N(hip->eqn[X], hip->eqn[Y], hip->eqn[Z]);
    double d = hip->eqn[W];

    /* Point on the plane (closest to the origin) */
    ON_3dPoint P0 = N * d;

    /* Build an orthonormal basis {X, Y} perpendicular to N */
    vect_t xbase, ybase, nvec;
    VMOVE(nvec, hip->eqn);
    bn_vec_perp(xbase, nvec);
    VCROSS(ybase, xbase, nvec);
    VUNITIZE(xbase);
    VUNITIZE(ybase);
    ON_3dVector X(xbase[0], xbase[1], xbase[2]);
    ON_3dVector Y(ybase[0], ybase[1], ybase[2]);

    /* Half-size of the approximating box.  1e5 mm (100 m) is large enough
     * for typical BRL-CAD models while remaining numerically well-behaved. */
    const double S = 1.0e5;

    /* Eight corners of the box.
     *
     * ON_BrepBox convention: corners[0..3] form one face, corners[4..7]
     * form the opposite face, with corners[i] paired with corners[i+4].
     *
     * Face 0 (corners 0..3): the halfspace boundary plane (N*P = d).
     *   Viewed from the +N direction (halfspace exterior) the winding is CCW.
     *
     * Face 1 (corners 4..7): the back face (N*P = d - S), deep inside the
     *   halfspace interior.
     */
    ON_3dPoint box_corners[8];
    box_corners[0] = P0 - S*X - S*Y;
    box_corners[1] = P0 + S*X - S*Y;
    box_corners[2] = P0 + S*X + S*Y;
    box_corners[3] = P0 - S*X + S*Y;
    box_corners[4] = P0 - S*N - S*X - S*Y;
    box_corners[5] = P0 - S*N + S*X - S*Y;
    box_corners[6] = P0 - S*N + S*X + S*Y;
    box_corners[7] = P0 - S*N - S*X + S*Y;

    ON_Brep *box_brep = ON_BrepBox(box_corners);
    if (!box_brep) {
	*b = NULL;
	return;
    }

    **b = *box_brep;
    delete box_brep;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
