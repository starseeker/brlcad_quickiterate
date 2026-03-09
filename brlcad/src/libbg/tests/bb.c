/*                           B B . C
 * BRL-CAD
 *
 * Copyright (c) 2011-2025 United States Government as represented by
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
/** @file bb.c
 *
 * Tests for bg_pnts_aabb, bg_pnts_obb, bg_obb_pnts, bg_trimesh_aabb, and
 * bg_trimesh_obb.
 */

#include "common.h"

#include <stdio.h>

#include "bu.h"
#include "bg.h"

/* Returns 1 if the two OBBs differ in center or extent vectors. */
static int
obb_diff(
    point_t ac, vect_t av1, vect_t av2, vect_t av3,
    point_t bc, vect_t bv1, vect_t bv2, vect_t bv3
    )
{
    vect_t diff;
    vect_t nbv[3];
    VMOVE(nbv[0], bv1); VSCALE(nbv[0], nbv[0], -1);
    VMOVE(nbv[1], bv2); VSCALE(nbv[1], nbv[1], -1);
    VMOVE(nbv[2], bv3); VSCALE(nbv[2], nbv[2], -1);

    if (!VNEAR_EQUAL(ac, bc, VUNITIZE_TOL)) {
	VSUB2(diff, ac, bc);
	bu_log("Note: center (%g %g %g) vs (%g %g %g) differs: %g %g %g\n",
	       V3ARGS(ac), V3ARGS(bc), V3ARGS(diff));
	return 1;
    }

    VSUB2(diff, av1, bv1);
    if (!VNEAR_EQUAL(av1, bv1, VUNITIZE_TOL) &&
	!VNEAR_EQUAL(av1, nbv[0], VUNITIZE_TOL)) {
	bu_log("Note: v1 (%g %g %g) vs (%g %g %g) differs: %g %g %g\n",
	       V3ARGS(av1), V3ARGS(bv1), V3ARGS(diff));
	return 1;
    }

    VSUB2(diff, av2, bv2);
    if (!VNEAR_EQUAL(av2, bv2, VUNITIZE_TOL) &&
	!VNEAR_EQUAL(av2, nbv[1], VUNITIZE_TOL)) {
	bu_log("Note: v2 (%g %g %g) vs (%g %g %g) differs: %g %g %g\n",
	       V3ARGS(av2), V3ARGS(bv2), V3ARGS(diff));
	return 1;
    }

    VSUB2(diff, av3, bv3);
    if (!VNEAR_EQUAL(av3, bv3, VUNITIZE_TOL) &&
	!VNEAR_EQUAL(av3, nbv[2], VUNITIZE_TOL)) {
	bu_log("Note: v3 (%g %g %g) vs (%g %g %g) differs: %g %g %g\n",
	       V3ARGS(av3), V3ARGS(bv3), V3ARGS(diff));
	return 1;
    }

    return 0;
}

/* Returns 1 if the two arb8 vertex sets differ (order-insensitive). */
static int
arb_diff(point_t *p1, point_t *p2)
{
    for (int i = 0; i < 8; i++) {
	int have_match = 0;
	for (int j = 0; j < 8; j++) {
	    if (VNEAR_EQUAL(p1[i], p2[j], VUNITIZE_TOL)) {
		have_match = 1;
		break;
	    }
	}
	if (!have_match) {
	    bu_log("Note: p1[%d] (%g %g %g) has no match in p2\n",
		   i, V3ARGS(p1[i]));
	    return 1;
	}
    }
    return 0;
}

int
main(int UNUSED(argc), const char *argv[])
{
    bu_setprogname(argv[0]);

    int faces[12] = {
	1, 3, 2,
	1, 2, 4,
	1, 4, 3,
	3, 4, 2};
    point_t verts[5];
    VSET(verts[0], -1, -1, -1);
    VSET(verts[1], 5000, 4000, 3000);
    VSET(verts[2], 5000, 6000, 3000);
    VSET(verts[3], 5000, 6000, 5000);
    VSET(verts[4], 3000, 6000, 3000);

    for (int i = 0; i < 10; i++) {
	verts[1][X] += i;
	verts[1][Y] -= i;
	verts[2][Y] += i;
	verts[3][Z] -= i;

	bu_log("\nTest %d\n", i+1);

	/* --- bg_pnts_aabb --- */
	{
	    point_t paabmin, paabmax;
	    if (bg_pnts_aabb(&paabmin, &paabmax, (const point_t *)verts, 5) != BRLCAD_OK)
		bu_exit(-1, "Error calculating pnts aabb\n");
	    bu_log("pnts_aabb: %g %g %g -> %g %g %g\n",
		   V3ARGS(paabmin), V3ARGS(paabmax));
	}

	/* --- bg_pnts_obb + bg_obb_pnts --- */
	{
	    point_t pobbc;
	    vect_t pv1, pv2, pv3;
	    if (bg_pnts_obb(&pobbc, &pv1, &pv2, &pv3, (const point_t *)verts, 5) != BRLCAD_OK)
		bu_exit(-1, "Error calculating pnts obb\n");
	    bu_log("pnts_obb: %g %g %g: %g %g %g, %g %g %g, %g %g %g\n",
		   V3ARGS(pobbc), V3ARGS(pv1), V3ARGS(pv2), V3ARGS(pv3));

	    point_t *pbverts = (point_t *)bu_calloc(8, sizeof(point_t), "p out");
	    if (bg_obb_pnts(pbverts, (const point_t *)&pobbc,
			   (const vect_t *)&pv1, (const vect_t *)&pv2,
			   (const vect_t *)&pv3) != BRLCAD_OK)
		bu_exit(-1, "Error computing obb pnts from pnts_obb\n");
	    bu_log("pnts_obb_verts: %g %g %g, %g %g %g, %g %g %g, %g %g %g\n",
		   V3ARGS(pbverts[0]), V3ARGS(pbverts[1]), V3ARGS(pbverts[2]), V3ARGS(pbverts[3]));
	    bu_log("               %g %g %g, %g %g %g, %g %g %g, %g %g %g\n",
		   V3ARGS(pbverts[4]), V3ARGS(pbverts[5]), V3ARGS(pbverts[6]), V3ARGS(pbverts[7]));
	    bu_free(pbverts, "p out");
	}

	/* --- bg_trimesh_aabb --- */
	{
	    point_t taabmin, taabmax;
	    if (bg_trimesh_aabb(&taabmin, &taabmax, faces, 4,
			       (const point_t *)verts, 5) != 0)
		bu_exit(-1, "Error calculating trimesh aabb\n");
	    bu_log("trimesh_aabb: %g %g %g -> %g %g %g\n",
		   V3ARGS(taabmin), V3ARGS(taabmax));
	}

	/* --- bg_trimesh_obb + consistency check --- */
	{
	    point_t tobbc;
	    vect_t tv1, tv2, tv3;
	    if (bg_trimesh_obb(&tobbc, &tv1, &tv2, &tv3, faces, 4,
			      (const point_t *)verts, 5) != BRLCAD_OK)
		bu_exit(-1, "Error calculating trimesh obb\n");
	    bu_log("trimesh_obb: %g %g %g: %g %g %g, %g %g %g, %g %g %g\n",
		   V3ARGS(tobbc), V3ARGS(tv1), V3ARGS(tv2), V3ARGS(tv3));

	    point_t tbverts[8];
	    if (bg_obb_pnts(tbverts, (const point_t *)&tobbc,
			   (const vect_t *)&tv1, (const vect_t *)&tv2,
			   (const vect_t *)&tv3) != BRLCAD_OK)
		bu_exit(-1, "Error computing obb pnts from trimesh_obb\n");
	    bu_log("trimesh_obb_verts: %g %g %g, %g %g %g, %g %g %g, %g %g %g\n",
		   V3ARGS(tbverts[0]), V3ARGS(tbverts[1]), V3ARGS(tbverts[2]), V3ARGS(tbverts[3]));
	    bu_log("                   %g %g %g, %g %g %g, %g %g %g, %g %g %g\n",
		   V3ARGS(tbverts[4]), V3ARGS(tbverts[5]), V3ARGS(tbverts[6]), V3ARGS(tbverts[7]));

	    /* Verify: compute OBB from the 8 corner verts and compare */
	    point_t btobbc;
	    vect_t btv1, btv2, btv3;
	    if (bg_pnts_obb(&btobbc, &btv1, &btv2, &btv3,
			   (const point_t *)tbverts, 8) != BRLCAD_OK)
		bu_exit(-1, "Error computing pnts_obb from trimesh_obb verts\n");

	    if (obb_diff(tobbc, tv1, tv2, tv3, btobbc, btv1, btv2, btv3)) {
		/* The OBB recomputed from the 8 corner points may differ
		 * slightly; verify that all original corners are still present
		 * in the new arb. */
		point_t tbpntverts[8];
		if (bg_obb_pnts(tbpntverts, (const point_t *)&btobbc,
				(const vect_t *)&btv1, (const vect_t *)&btv2,
				(const vect_t *)&btv3) != BRLCAD_OK)
		    bu_exit(-1, "Error computing obb pnts from back-computed obb\n");

		if (arb_diff(tbverts, tbpntverts)) {
		    bu_log("ERROR: arb8 corner sets differ after round-trip!\n");
		    return 1;
		}
	    }
	}
    }

    return 0;
}


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
