/*                 T R I M E S H _ S O L I D . C
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @file trimesh_solid.c
 *
 * Tests for bg_trimesh_solid2 and bg_trimesh_non_manifold_vertices,
 * specifically the non-manifold vertex detection capability.
 */

#include "common.h"

#include <stdio.h>

#include "bu.h"
#include "bg.h"

/* ---- Simple closed tetrahedron (manifold, solid) -------------------- */

/* 4 vertices */
static double tet_verts[] = {
    0.0, 0.0, 0.0,
    1.0, 0.0, 0.0,
    0.5, 1.0, 0.0,
    0.5, 0.5, 1.0
};

/* 4 faces with consistent CCW winding */
static int tet_faces[] = {
    0, 2, 1,
    0, 1, 3,
    1, 2, 3,
    0, 3, 2
};

/* ---- Mesh with a non-manifold vertex --------------------------------
 *
 * Two separate tetrahedra (each internally manifold) whose distinct
 * vertex buffers are merged so that vertex index 0 is shared between
 * both.  After merging:
 *   - All edges within each tet are internally matched (no unmatched
 *     edges from edge-pair checks alone).
 *   - Vertex 0 has two disconnected triangle fans → non-manifold.
 *
 * Construction: take two separate closed tetrahedra, keep the second
 * tet's vertex indices offset by 3 EXCEPT that the second tet also
 * references vertex 0 (its "apex" in the merged array).
 *
 * Tet A  (vertices 0,1,2,3)  — same as tet_faces above
 * Tet B  (vertices 0,4,5,6)  — vertex 0 is the shared NMV
 */

static double nmv_verts[] = {
    /* vertex 0 — shared by both tets (non-manifold vertex) */
     0.0,  0.0,  0.0,
    /* tet A */
     1.0,  0.0,  0.0,
     0.5,  1.0,  0.0,
     0.5,  0.5,  1.0,
    /* tet B — offset in space so it doesn't degenerate */
     3.0,  0.0,  0.0,
     3.5,  1.0,  0.0,
     3.5,  0.5,  1.0
};

static int nmv_faces[] = {
    /* tet A faces (vertex 0,1,2,3) */
    0, 2, 1,
    0, 1, 3,
    1, 2, 3,
    0, 3, 2,
    /* tet B faces (vertex 0,4,5,6) */
    0, 5, 4,
    0, 4, 6,
    4, 5, 6,
    0, 6, 5
};

/* ---- Mesh with an unmatched (boundary) edge but no NMV -------------- */

/* Tetrahedron with one face removed → open mesh */
static double open_verts[] = {
    0.0, 0.0, 0.0,
    1.0, 0.0, 0.0,
    0.5, 1.0, 0.0,
    0.5, 0.5, 1.0
};

static int open_faces[] = {
    /* Only 3 of the 4 tet faces */
    0, 1, 3,
    1, 2, 3,
    0, 3, 2
};

/* ===================================================================== */

static int
test_solid_tetrahedron(void)
{
    int result;
    struct bg_trimesh_solid_errors errors = BG_TRIMESH_SOLID_ERRORS_INIT_NULL;

    /* Simple tetrahedron must be solid (return 0) */
    result = bg_trimesh_solid2(4, 4, tet_verts, tet_faces, NULL);
    if (result != 0) {
	bu_log("FAIL: solid tetrahedron reported as not-solid (fast path)\n");
	return 1;
    }

    result = bg_trimesh_solid2(4, 4, tet_verts, tet_faces, &errors);
    if (result != 0) {
	bu_log("FAIL: solid tetrahedron reported as not-solid (error-collection path)\n");
	bg_free_trimesh_solid_errors(&errors);
	return 1;
    }
    if (errors.degenerate.count != 0 ||
	errors.unmatched.count  != 0 ||
	errors.excess.count     != 0 ||
	errors.misoriented.count!= 0 ||
	errors.non_manifold_verts.count != 0)
    {
	bu_log("FAIL: solid tetrahedron has unexpected error counts\n");
	bg_free_trimesh_solid_errors(&errors);
	return 1;
    }
    bg_free_trimesh_solid_errors(&errors);

    bu_log("PASS: solid tetrahedron correctly identified as solid\n");
    return 0;
}

static int
test_nmv_detection_fast(void)
{
    int result;

    /* Fast-exit path: should detect NMV and return non-zero */
    result = bg_trimesh_solid2(7, 8, nmv_verts, nmv_faces, NULL);
    if (result == 0) {
	bu_log("FAIL: NMV mesh incorrectly reported as solid (fast path)\n");
	return 1;
    }

    bu_log("PASS: NMV mesh correctly identified as not-solid (fast path)\n");
    return 0;
}

static int
test_nmv_detection_errors(void)
{
    int result;
    struct bg_trimesh_solid_errors errors = BG_TRIMESH_SOLID_ERRORS_INIT_NULL;

    result = bg_trimesh_solid2(7, 8, nmv_verts, nmv_faces, &errors);
    if (result == 0) {
	bu_log("FAIL: NMV mesh incorrectly reported as solid (error-collection path)\n");
	bg_free_trimesh_solid_errors(&errors);
	return 1;
    }
    if (errors.non_manifold_verts.count != 1) {
	bu_log("FAIL: expected 1 non-manifold vertex, got %d\n",
	       errors.non_manifold_verts.count);
	bg_free_trimesh_solid_errors(&errors);
	return 1;
    }
    if (!errors.non_manifold_verts.verts) {
	bu_log("FAIL: non_manifold_verts.verts is NULL\n");
	bg_free_trimesh_solid_errors(&errors);
	return 1;
    }
    if (errors.non_manifold_verts.verts[0] != 0) {
	bu_log("FAIL: expected NMV at index 0, got index %d\n",
	       errors.non_manifold_verts.verts[0]);
	bg_free_trimesh_solid_errors(&errors);
	return 1;
    }

    bg_free_trimesh_solid_errors(&errors);
    bu_log("PASS: NMV mesh: correct NMV count and index reported\n");
    return 0;
}

static int
test_nmv_standalone_function(void)
{
    int count;

    /* Solid tet should have 0 NMVs */
    count = bg_trimesh_non_manifold_vertices(4, 4, tet_faces,
					     bg_trimesh_vert_continue, NULL);
    if (count != 0) {
	bu_log("FAIL: solid tet has %d NMVs (expected 0)\n", count);
	return 1;
    }

    /* NMV mesh should have exactly 1 NMV (vertex 0) */
    count = bg_trimesh_non_manifold_vertices(7, 8, nmv_faces,
					     bg_trimesh_vert_continue, NULL);
    if (count != 1) {
	bu_log("FAIL: NMV mesh has %d NMVs (expected 1)\n", count);
	return 1;
    }

    /* Fast-exit should still return >= 1 */
    count = bg_trimesh_non_manifold_vertices(7, 8, nmv_faces,
					     bg_trimesh_vert_exit, NULL);
    if (count < 1) {
	bu_log("FAIL: NMV fast-exit returned %d (expected >= 1)\n", count);
	return 1;
    }

    /* Gather test */
    {
	struct bg_trimesh_verts verts = BG_TRIMESH_VERTS_INIT_NULL;
	verts.verts = (int *)bu_calloc(1, sizeof(int), "nmv test");
	verts.count = 0;
	count = bg_trimesh_non_manifold_vertices(7, 8, nmv_faces,
						 bg_trimesh_vert_gather, &verts);
	if (count != 1 || verts.count != 1 || verts.verts[0] != 0) {
	    bu_log("FAIL: NMV gather: count=%d, verts.count=%d, verts[0]=%d (expected 1,1,0)\n",
		   count, verts.count, verts.verts[0]);
	    bu_free(verts.verts, "nmv test");
	    return 1;
	}
	bu_free(verts.verts, "nmv test");
    }

    bu_log("PASS: bg_trimesh_non_manifold_vertices standalone tests\n");
    return 0;
}

static int
test_open_mesh_no_nmv(void)
{
    int nmv_count;
    int solid2_ret;

    /* Open mesh (missing a face) should have unmatched edges but NO NMVs */
    nmv_count = bg_trimesh_non_manifold_vertices(4, 3, open_faces,
						 bg_trimesh_vert_continue, NULL);
    if (nmv_count != 0) {
	bu_log("FAIL: open mesh has %d NMVs (expected 0)\n", nmv_count);
	return 1;
    }

    /* solid2 should still fail (unmatched boundary edges) */
    solid2_ret = bg_trimesh_solid2(4, 3, open_verts, open_faces, NULL);
    if (solid2_ret == 0) {
	bu_log("FAIL: open mesh incorrectly reported as solid\n");
	return 1;
    }

    bu_log("PASS: open mesh: no NMVs, correctly reported as not-solid\n");
    return 0;
}

int
main(int UNUSED(argc), const char **UNUSED(argv))
{
    int failures = 0;

    failures += test_solid_tetrahedron();
    failures += test_nmv_detection_fast();
    failures += test_nmv_detection_errors();
    failures += test_nmv_standalone_function();
    failures += test_open_mesh_no_nmv();

    if (failures == 0) {
	bu_log("All bg_trimesh_solid2 / NMV tests PASSED\n");
    } else {
	bu_log("%d test(s) FAILED\n", failures);
    }

    return failures;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
