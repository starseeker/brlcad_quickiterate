/*                       T O R U S . C P P
 * BRL-CAD
 *
 * Copyright (c) 2020-2025 United States Government as represented by
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
/** @file torus.cpp
 *
 * Torus shape recognition and CSG conversion routines.
 *
 */

#include "common.h"

#include <set>
#include <map>

#include "bu/log.h"
#include "bu/str.h"
#include "bu/malloc.h"
#include "shape_recognition.h"


int
tor_validate_face(const ON_BrepFace *forig, const ON_BrepFace *fcand)
{
    ON_Torus torig, tcand;
    ON_Surface *ssorig = forig->SurfaceOf()->Duplicate();
    ssorig->IsTorus(&torig, BREP_TOROIDAL_TOL);
    delete ssorig;
    ON_Surface *sscand = fcand->SurfaceOf()->Duplicate();
    sscand->IsTorus(&tcand, BREP_TOROIDAL_TOL);
    delete sscand;

    // Same center?
    if (torig.Center().DistanceTo(tcand.Center()) > BREP_TOROIDAL_TOL) return 0;

    // Same axis direction?
    if (torig.plane.Normal().IsParallelTo(tcand.plane.Normal(), VUNITIZE_TOL) == 0) return 0;

    // Same major and minor radii?
    if (!NEAR_ZERO(torig.major_radius - tcand.major_radius, BREP_TOROIDAL_TOL)) return 0;
    if (!NEAR_ZERO(torig.minor_radius - tcand.minor_radius, BREP_TOROIDAL_TOL)) return 0;

    return 1;
}


/* Return -1 if the torus tube surface normals are pointing toward the tube
 * center (negative/inner surface), 1 if they are pointing outward (positive/
 * outer surface), and 0 if there is some other problem. */
int
negative_torus(const ON_Brep *brep, int face_index, double torus_tol)
{
    const ON_Surface *surf = brep->m_F[face_index].SurfaceOf();
    ON_Torus torus;
    ON_Surface *cs = surf->Duplicate();
    cs->IsTorus(&torus, torus_tol);
    delete cs;

    ON_3dPoint pnt;
    ON_3dVector normal;
    double u = surf->Domain(0).Mid();
    double v = surf->Domain(1).Mid();
    if (!surf->EvNormal(u, v, pnt, normal)) return 0;

    /* Find the nearest point on the torus major circle (ring centerline).
     * Project pnt onto the torus plane, normalize, then scale by major radius. */
    ON_3dVector radial = pnt - torus.Center();
    ON_3dVector axis = torus.plane.Normal();
    radial = radial - ON_DotProduct(radial, axis) * axis;
    if (radial.Length() < ON_ZERO_TOLERANCE) return 0;
    radial.Unitize();
    ON_3dPoint ring_pt = torus.Center() + torus.major_radius * radial;

    /* Vector from the ring centerline point to the surface point is the
     * "outward" direction for the tube cross-section. */
    ON_3dVector outward = pnt - ring_pt;
    double dotp = ON_DotProduct(outward, normal);

    if (NEAR_ZERO(dotp, VUNITIZE_TOL)) return 0;
    int ret = (dotp < 0) ? -1 : 1;
    if (brep->m_F[face_index].m_bRev) ret = -1 * ret;
    return ret;
}


/* Populate CSG primitive data for a torus shoal.
 *
 * For a full torus (no bounding planes), returns 0 (no arbn needed).
 * For a partial torus section bounded by edge planes, returns 1 (arbn needed).
 * The bounding arbn planes are already collected in tor_planes by shoal_csg()
 * from the edge geometry. */
int
torus_implicit_params(struct subbrep_shoal_data *data, ON_SimpleArray<ON_Plane> *tor_planes, int shoal_nonplanar_face)
{
    ON_Torus torus;
    ON_Surface *cs = data->i->brep->m_L[data->shoal_loops[0]].Face()->SurfaceOf()->Duplicate();
    cs->IsTorus(&torus, BREP_TOROIDAL_TOL);
    delete cs;

    int need_arbn = (tor_planes->Count() == 0) ? 0 : 1;

    data->params->csg_type = TORUS;
    data->params->csg_id = (*(data->i->obj_cnt))++;
    data->params->negative = negative_torus(data->i->brep, shoal_nonplanar_face, BREP_TOROIDAL_TOL);
    data->params->bool_op = (data->params->negative == -1) ? '-' : 'u';

    ON_3dPoint origin = torus.Center();
    BN_VMOVE(data->params->origin, origin);

    ON_3dVector norm = torus.plane.Normal();
    BN_VMOVE(data->params->hv, norm);

    data->params->radius = torus.major_radius;
    data->params->r2 = torus.minor_radius;

    return need_arbn;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
