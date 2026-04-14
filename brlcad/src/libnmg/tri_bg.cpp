/*                     T R I _ B G . C P P
 * BRL-CAD
 *
 * Copyright (c) 1994-2025 United States Government as represented by
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
/** @addtogroup nmg */
/** @{ */
/** @file libnmg/tri_bg.cpp
 *
 * Triangulate a single NMG faceuse using libbg's constrained Delaunay
 * triangulator (bg_nested_poly_triangulate / TRI_CONSTRAINED_DELAUNAY).
 *
 * This file is intentionally separate from tri.c so that it can use C++17
 * containers (std::unordered_map) for O(1) edge-gluing lookups, avoiding the
 * O(n²) behaviour that a naive vertex-list walk would introduce.
 *
 * Entry point (C linkage):
 *   int nmg_tri_fu_bg(struct faceuse *fu,
 *                     struct bu_list *vlfree,
 *                     const struct bn_tol *tol)
 *
 * Returns 0 on success (fu has been killed, replaced by triangle faceuses
 * that are glued back into the surrounding shell).
 * Returns 1 on failure (fu is completely unchanged; caller should fall back
 * to the ear-clip path).
 */
/** @} */

#include "common.h"

#include <unordered_map>
#include <utility>
#include <vector>

#include "vmath.h"
#include "bu/malloc.h"
#include "bn/mat.h"
#include "bg/polygon.h"
#include "nmg.h"


/* ------------------------------------------------------------------ */
/* Hash map for directed edges (struct vertex* src → struct vertex* dst) */
/* ------------------------------------------------------------------ */

using VPair = std::pair<struct vertex *, struct vertex *>;

struct VPairHash {
    std::size_t operator()(const VPair &p) const noexcept {
	/* Boost-style hash combine using std::size_t throughout so the
	 * arithmetic is correct on both 32- and 64-bit platforms.        */
	std::size_t h1 = std::hash<struct vertex *>{}(p.first);
	std::size_t h2 = std::hash<struct vertex *>{}(p.second);
	/* Mix h2 into h1 with a Fibonacci-style multiplier. */
	h1 ^= h2 + (std::size_t)0x9e3779b9u + (h1 << 6) + (h1 >> 2);
	return h1;
    }
};

using EdgeMap = std::unordered_map<VPair, struct edgeuse *, VPairHash>;


/* ------------------------------------------------------------------ */
/* Helper: return the start/end vertex of an edgeuse                  */
/* ------------------------------------------------------------------ */

static inline struct vertex *
eu_src(const struct edgeuse *eu)
{
    return eu->vu_p->v_p;
}

static inline struct vertex *
eu_dst(const struct edgeuse *eu)
{
    return eu->eumate_p->vu_p->v_p;
}

static inline bool
eu_is_free(const struct edgeuse *eu)
{
    return eu->radial_p == eu->eumate_p;
}


/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */

/*
 * Attempt to triangulate faceuse fu using bg_nested_poly_triangulate with
 * TRI_CONSTRAINED_DELAUNAY.
 *
 * Design notes:
 *
 * 1. bg_detria (used by bg_nested_poly_triangulate) returns CW triangles
 *    (cwTriangles = true).  In NMG, a face's OT_SAME loop must be CCW from
 *    the face's outward normal so that nmg_fu_planeeqn produces the correct
 *    outward-pointing normal.  We therefore REVERSE the winding by swapping
 *    indices i1 and i2 when calling nmg_cface.
 *
 * 2. For a manifold mesh, adjacent OT_SAME faceuses share an edge with their
 *    OT_SAME edgeuses going in OPPOSITE directions.  After the winding
 *    reversal the new cap triangle's outer boundary edges go in the SAME
 *    direction as the original cap's outer loop (CCW), which is the OPPOSITE
 *    of the adjacent side-wall face's OT_SAME edgeuse → Case 1 of nmg_je.
 *
 *    For inner (hole) boundaries the situation is reversed: the new cap
 *    triangle edge at the hole boundary goes in the SAME direction as the
 *    adjacent inner face's OT_SAME edgeuse → Case 2 of nmg_je.
 *
 *    We handle both cases by building a hash map of the side-wall edgeuses
 *    (keyed by their own directed vertex pair) BEFORE killing fu.  During
 *    the glue pass we first try the reverse direction (Case 1) and then the
 *    same direction (Case 2).
 *
 * 3. All edge lookups use std::unordered_map for O(1) expected time,
 *    turning the glue step from O(n * valence) to O(n).
 */
extern "C" int
nmg_tri_fu_bg(struct faceuse *fu, struct bu_list *UNUSED(vlfree),
	      const struct bn_tol *tol)
{
    NMG_CK_FACEUSE(fu);
    BN_CK_TOL(tol);

    struct loopuse *lu;
    struct edgeuse *eu;

    /* ---- 1. Count loops; require exactly one OT_SAME outer loop ---- */
    int n_outer       = 0;   /* vertex count of the OT_SAME loop       */
    int nholes        = 0;   /* number of OT_OPPOSITE (hole) loops      */
    int n_inner_total = 0;   /* total vertex count across all holes     */
    int n_same_loops  = 0;   /* number of OT_SAME loops in faceuse      */
    struct loopuse *outer_lu = NULL;

    for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
	if (BU_LIST_FIRST_MAGIC(&lu->down_hd) != NMG_EDGEUSE_MAGIC)
	    continue;
	if (lu->orientation == OT_SAME) {
	    int vc = 0;
	    for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd))
		vc++;
	    n_same_loops++;
	    if (vc > n_outer) {
		n_outer  = vc;
		outer_lu = lu;
	    }
	} else if (lu->orientation == OT_OPPOSITE) {
	    int vc = 0;
	    for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd))
		vc++;
	    n_inner_total += vc;
	    nholes++;
	}
    }

    /* Require exactly one loop (OT_SAME or OT_OPPOSITE-only when there is no
     * OT_SAME loop); multi-outer-loop faces are unusual (boolean artifacts)
     * and the ear-clip path handles them better.
     *
     * nmg_lu_reorient() (called by nmg_triangulate_fu before us) labels a
     * loop as OT_OPPOSITE when its 2-D winding appears CW relative to the
     * stored face normal.  For pipe end-caps the stored face normal (set by
     * nmg_rebound / Newell) is inward, so the outward-facing cap loop is
     * correctly wound in 3-D but gets flagged OT_OPPOSITE by reorient.  We
     * handle this by accepting a single OT_OPPOSITE loop as the outer polygon
     * when there are no OT_SAME loops at all.  The signed-area check in
     * section 5 will flip the 2-D polygon to CCW regardless of the label.  */
    bu_log("  nmg_tri_fu_bg: n_same=%d n_outer=%d nholes=%d\n", n_same_loops, n_outer, nholes);
    if (n_same_loops == 0 && nholes == 1 && n_inner_total >= 3) {
	/* Promote the sole OT_OPPOSITE loop to be the outer polygon. */
	n_outer = n_inner_total;
	nholes  = 0;
	n_inner_total = 0;
	for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
	    if (BU_LIST_FIRST_MAGIC(&lu->down_hd) == NMG_EDGEUSE_MAGIC &&
		lu->orientation == OT_OPPOSITE) {
		outer_lu = lu;
		break;
	    }
	}
    } else if (n_same_loops != 1 || !outer_lu || n_outer < 3) {
	return 1;
    }

    /* A triangle without holes is already triangulated.                    */
    if (n_outer == 3 && nholes == 0)
	return 1;

    /* ---- 2. Projection axes: (u_ax × v_ax) = fu_normal ---- */
    /* Inline NMG_GET_FU_NORMAL without 'register' keyword (C++17 drops it).
     * The XOR of (orientation != OT_SAME) and (flip != 0) captures the rule:
     * flip the stored normal when exactly one of {OT_OPPOSITE, face->flip}
     * is true, and keep it when both or neither are true.                   */
    vect_t fu_normal;
    {
	const struct face_g_plane *_fg = fu->f_p->g.plane_p;
	bool is_opposite = (fu->orientation != OT_SAME);
	bool is_flipped  = (fu->f_p->flip != 0);
	if (is_opposite != is_flipped)
	    VREVERSE(fu_normal, _fg->N);
	else
	    VMOVE(fu_normal, _fg->N);
    }
    vect_t u_ax, v_ax;
    bn_vec_ortho(u_ax, fu_normal);
    VCROSS(v_ax, fu_normal, u_ax); /* so CCW in 2-D = outward in 3-D */

    eu = BU_LIST_FIRST(edgeuse, &outer_lu->down_hd);
    point_t base_pt;
    VMOVE(base_pt, eu->vu_p->v_p->vg_p->coord);

    /* ---- 3. Build combined 2-D point array ---- */
    int npts = n_outer + n_inner_total;
    std::vector<point2d_t>      pts(npts);
    std::vector<struct vertex *> idx_to_vert(npts, nullptr);
    std::vector<int>             poly(n_outer);

    int idx = 0;
    for (BU_LIST_FOR(eu, edgeuse, &outer_lu->down_hd)) {
	vect_t d;
	VSUB2(d, eu->vu_p->v_p->vg_p->coord, base_pt);
	pts[idx][X]      = VDOT(d, u_ax);
	pts[idx][Y]      = VDOT(d, v_ax);
	poly[idx]        = idx;
	idx_to_vert[idx] = eu->vu_p->v_p;
	idx++;
    }

    std::vector<std::vector<int>> hole_vecs(nholes);
    {
	int h = 0;
	for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
	    if (BU_LIST_FIRST_MAGIC(&lu->down_hd) != NMG_EDGEUSE_MAGIC)
		continue;
	    if (lu->orientation != OT_OPPOSITE)
		continue;
	    int hole_start = idx;
	    for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {
		vect_t d;
		VSUB2(d, eu->vu_p->v_p->vg_p->coord, base_pt);
		pts[idx][X]      = VDOT(d, u_ax);
		pts[idx][Y]      = VDOT(d, v_ax);
		idx_to_vert[idx] = eu->vu_p->v_p;
		idx++;
	    }
	    int n_hole = idx - hole_start;
	    hole_vecs[h].resize(n_hole);
	    for (int j = 0; j < n_hole; j++)
		hole_vecs[h][j] = hole_start + j;
	    h++;
	}
    }

    /* Build C-style arrays for holes (bg_nested_poly_triangulate interface) */
    std::vector<const int *> holes_arr_ptrs(nholes, nullptr);
    std::vector<size_t>      holes_npts_vec(nholes, 0);
    for (int h = 0; h < nholes; h++) {
	holes_arr_ptrs[h]  = hole_vecs[h].data();
	holes_npts_vec[h]  = (size_t)hole_vecs[h].size();
    }

    /* ---- 4. Pre-collect side-wall edgeuses (BEFORE killing fu) ----
     *
     * For each boundary edgeuse `eu` in fu's loops, eu->radial_p is the
     * adjacent side-wall face's edgeuse.  We record it keyed by its own
     * directed vertex pair so the glue pass can locate it in O(1).
     *
     * After nmg_kfu(fu) these side-wall edgeuses become self-radial
     * (eu->radial_p == eu->eumate_p).  We verify they are still free
     * before calling nmg_je in the glue pass.
     */
    EdgeMap swmap;
    swmap.reserve((size_t)(n_outer + n_inner_total) * 2);

    for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
	if (BU_LIST_FIRST_MAGIC(&lu->down_hd) != NMG_EDGEUSE_MAGIC)
	    continue;
	for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {
	    if (eu->radial_p == eu->eumate_p)
		continue; /* no adjacent face on this edge */
	    struct edgeuse *sw = eu->radial_p;
	    /* Key: the side-wall edgeuse's own direction. */
	    swmap.emplace(VPair{eu_src(sw), eu_dst(sw)}, sw);
	}
    }

    /* ---- 5. Ensure CCW winding before handing off to bg_detria ----
     *
     * bg_nested_poly_triangulate (via bg_detria) expects the outer polygon to
     * be CCW in the 2-D projection plane.  For most tessellated primitives the
     * OT_SAME loop is already CCW when projected with fu_normal; however, some
     * primitives (notably pipe end caps) produce a loop that is CW from the
     * outward face normal direction.  Detect this and reverse the outer polygon
     * so that CDT always receives CCW input.
     *
     * A CW polygon has a negative signed area (shoelace formula).
     *
     * IMPORTANT: only poly[] (the index sequence) is reversed.  pts[] and
     * idx_to_vert[] are keyed by the SAME integer space as the CDT output
     * indices.  Reversing pts[]+idx_to_vert[] together with poly[] is a
     * no-op for the polygon shape (it produces the same traversal after
     * substitution) and breaks the idx_to_vert mapping used in the glue
     * pass.  Reversing only poly[] correctly swaps CCW ↔ CW while keeping
     * all index mappings intact. */
    {
	double signed_area = 0.0;
	for (int j = 0; j < n_outer; j++) {
	    int k = (j + 1) % n_outer;
	    signed_area += pts[poly[j]][X] * pts[poly[k]][Y]
			- pts[poly[k]][X] * pts[poly[j]][Y];
	}
	if (signed_area < 0.0) {
	    /* Reverse the traversal order: flip poly[] in-place. */
	    for (int j = 0, end = n_outer - 1; j < end; j++, end--)
		std::swap(poly[j], poly[end]);
	}
    }

    /* ---- 6. Triangulate ---- */
    int *tri_faces = NULL;
    int  num_tri   = 0;
    bu_log("  bg CDT: n_outer=%d nholes=%d signed_area check\n", n_outer, nholes);
    {double sa=0;for(int j=0;j<n_outer;j++){int k=(j+1)%n_outer;sa+=pts[poly[j]][X]*pts[poly[k]][Y]-pts[poly[k]][X]*pts[poly[j]][Y];}bu_log("  bg CDT: signed_area=%g, poly[0..2]=%d %d %d\n",sa,poly[0],poly[1],n_outer>2?poly[2]:-1);}
    int bg_ret = bg_nested_poly_triangulate(
	&tri_faces, &num_tri, NULL, NULL,
	poly.data(), (size_t)n_outer,
	nholes > 0 ? holes_arr_ptrs.data() : NULL,
	nholes > 0 ? holes_npts_vec.data() : NULL,
	(size_t)nholes,
	NULL, 0,
	pts.data(), (size_t)npts,
	TRI_CONSTRAINED_DELAUNAY);

    if (bg_ret != 0 || num_tri <= 0 || !tri_faces) {
	bu_log("  bg CDT: FAILED bg_ret=%d num_tri=%d\n", bg_ret, num_tri);
	/* bg_nested_poly_triangulate may have partially allocated tri_faces
	 * even on failure (e.g. if it allocated but returned 0 triangles). */
	if (tri_faces)
	    bu_free(tri_faces, "nmg_tri_fu_bg tri_faces");
	return 1; /* fu unchanged; use ear-clip fallback */
    }
    bu_log("  bg CDT: success num_tri=%d\n", num_tri);

    /* ---- 7. Kill original face, create new triangle faceuses ---- */
    struct shell *s = fu->s_p;
    (void)nmg_kfu(fu); /* fu is now invalid – do not use */

    /* Map of NEW triangle OT_SAME edgeuses: (src,dst) → edgeuse.
     * Populated in one pass, then consumed in the glue pass.           */
    EdgeMap new_tri_map;
    new_tri_map.reserve((size_t)num_tri * 6); /* 3 edges × 2 directions */

    for (int k = 0; k < num_tri; k++) {
	int i0 = tri_faces[3*k];
	int i1 = tri_faces[3*k+1];
	int i2 = tri_faces[3*k+2];

	if (i0 < 0 || i0 >= npts ||
	    i1 < 0 || i1 >= npts ||
	    i2 < 0 || i2 >= npts)
	    continue;

	struct vertex *tv[3];
	tv[0] = idx_to_vert[i0];
	/* SWAP i1 ↔ i2 to reverse the winding from CW to CCW.
	 * bg_detria returns CW triangles; NMG requires the OT_SAME loop
	 * to be CCW from the outward face normal.                        */
	tv[1] = idx_to_vert[i2];
	tv[2] = idx_to_vert[i1];

	if (!tv[0] || !tv[1] || !tv[2])
	    continue;
	if (tv[0] == tv[1] || tv[1] == tv[2] || tv[0] == tv[2])
	    continue;

	struct faceuse *nfu = nmg_cface(s, tv, 3);
	if (!nfu)
	    continue;

	/* Compute face plane directly from the 3 vertex coordinates.
	 *
	 * nmg_fu_planeeqn() rejects triangles whose vertices are within
	 * tol->dist of each other ("Cannot find three distinct vertices").
	 * For freshly-created CDT triangles this is too aggressive: a cap
	 * polygon on a small-radius cylinder (e.g. r=0.1 mm) produces many
	 * valid but tiny triangles that all share the same geometric centre
	 * – they ARE distinct 3-D points but fall within the NMG tolerance.
	 * Silently dropping them leaves gaps → open edges in the final BOT.
	 *
	 * Instead we call bg_make_plane_3pnts() directly with a very small
	 * absolute tolerance (SMALL_FASTF) so only truly degenerate
	 * (zero-area) triangles are discarded.  nmg_face_g() assigns the
	 * resulting plane to the new faceuse without any vertex-distance
	 * checks.                                                           */
	{
	    plane_t tri_plane;
	    const fastf_t *p0 = tv[0]->vg_p->coord;
	    const fastf_t *p1 = tv[1]->vg_p->coord;
	    const fastf_t *p2 = tv[2]->vg_p->coord;
	    struct bn_tol geom_tol = *tol;
	    geom_tol.dist    = SMALL_FASTF;
	    geom_tol.dist_sq = SMALL_FASTF * SMALL_FASTF;
	    if (bg_make_plane_3pnts(tri_plane, p0, p1, p2, &geom_tol) < 0) {
		/* Truly zero-area triangle – skip it. */
		(void)nmg_kfu(nfu);
		continue;
	    }
	    nmg_face_g(nfu, tri_plane);
	}

	/* Collect OT_SAME edgeuses into new_tri_map. */
	struct loopuse *nlu = BU_LIST_FIRST(loopuse, &nfu->lu_hd);
	if (BU_LIST_FIRST_MAGIC(&nlu->down_hd) != NMG_EDGEUSE_MAGIC)
	    continue;
	for (BU_LIST_FOR(eu, edgeuse, &nlu->down_hd)) {
	    new_tri_map.emplace(VPair{eu_src(eu), eu_dst(eu)}, eu);
	}
    }

    bu_free(tri_faces, "nmg_tri_fu_bg tri_faces");

    /* ---- 8. Glue pass (all O(1) lookups via hash maps) ----
     *
     * For each directed edge (v1→v2) in new_tri_map:
     *
     *   Case 1 – outer-boundary side-wall: swmap has (v2,v1) = sw going
     *            v2→v1 (opposite direction).  Call nmg_je(neu, sw).
     *
     *   Case 2 – inner-boundary (hole) side-wall: swmap has (v1,v2) = sw
     *            going v1→v2 (same direction).  Call nmg_je(neu, sw).
     *
     *   Inter-triangle: new_tri_map has (v2,v1) = other new triangle edge
     *            going in the opposite direction.  Call nmg_je(neu, other).
     */
    for (auto &[pair, neu] : new_tri_map) {
	/* Skip if already glued by a previous iteration. */
	if (!eu_is_free(neu))
	    continue;

	struct vertex *v1 = pair.first;   /* neu: v1 → v2 */
	struct vertex *v2 = pair.second;

	/* Case 1: outer boundary – look for sw going v2→v1 */
	{
	    auto it = swmap.find({v2, v1});
	    if (it != swmap.end() && eu_is_free(it->second)) {
		nmg_je(neu, it->second);
		continue;
	    }
	}

	/* Case 2: inner boundary – look for sw going v1→v2 (same dir) */
	{
	    auto it = swmap.find({v1, v2});
	    if (it != swmap.end() && it->second != neu &&
		eu_is_free(it->second)) {
		nmg_je(neu, it->second);
		continue;
	    }
	}

	/* Inter-triangle: look for a mate going v2→v1 */
	{
	    auto it = new_tri_map.find({v2, v1});
	    if (it != new_tri_map.end() && it->second != neu &&
		eu_is_free(it->second)) {
		nmg_je(neu, it->second);
	    }
	}
    }

    return 0; /* success */
}


/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
