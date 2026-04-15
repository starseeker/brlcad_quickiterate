/*                   D S P _ T E S S . C P P
 * BRL-CAD
 *
 * Copyright (c) 1999-2025 United States Government as represented by
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
/** @addtogroup primitives */
/** @{ */
/** @file primitives/dsp/dsp_tess.cpp
 *
 * DSP tessellation logic
 *
 */

#include "common.h"

#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cmath>

#include "vmath.h"
#include "raytrace.h"
#include "rt/functab.h"
#include "rt/geom.h"
#include "rt/primitives/bot.h"
#include "nmg.h"

#include "TerraScape.hpp"
#include "mmesh/meshdecimation.h"

/* private header */
#include "./dsp.h"

/* ------------------------------------------------------------------ */
/* Named constants for the decimation-based DSP tessellation           */
/* ------------------------------------------------------------------ */

/* GCT decimation cost-model exponents.
 * The original GCT code uses a sixth-power cost; mmesh uses the same.
 * rt_bot_decimate_gct() adjusts for backward compatibility with the
 * fourth-power legacy model: fsize = feature^(2/3) * 2^(4/3).        */
static const double DSP_DECIMATE_STRENGTH_EXPONENT = 2.0 / 3.0;
static const double DSP_DECIMATE_STRENGTH_SCALE    = /* pow(2.0, 4.0/3.0) */ 2.5198420997897;

/* Minimum and maximum feature-size clamps (in grid units). */
static const double DSP_DECIMATE_MIN_FEATURE   = 0.5;
static const double DSP_DECIMATE_MAX_FEATURE_RATIO = 0.25; /* fraction of grid diagonal */

/* Steiner-point grid: points placed every SPACING_FACTOR * min_dist apart
 * so that at most one Steiner point falls between any two boundary edges.
 * The start/end offsets of 0.5 / 0.25 keep points off the boundary.  */
static const double DSP_STEINER_GRID_SPACING_FACTOR = 2.5;

/* Minimum spacing between Steiner points in cell units.               */
static const double DSP_STEINER_MIN_CELL_SPACING = 3.0;

/**
 * Fast NMG assembler for a manifold, CCW-oriented triangulated BOT.
 *
 * The standard rt_bot_tess() path calls nmg_cmface() per triangle,
 * which internally calls nmg_findeu() to search the vertex's vertexuse
 * list for a matching dangling edge.  For well-formed triangulated
 * meshes the per-call cost is O(local degree) ≈ O(1), but the constant
 * factor is high due to NMG_CK_* validation and heap churn.
 *
 * This assembler avoids nmg_findeu() entirely:
 *   1. Call nmg_cface() for every triangle — creates faces/edgeuses
 *      without joining shared edges.
 *   2. Collect every directed edgeuse (v_start → v_end) from every
 *      OT_SAME loop into a hash map keyed on the (vertex*, vertex*)
 *      pointer pair.
 *   3. For each entry whose key (A,B) has a matching reverse (B,A),
 *      call nmg_je() exactly once to join the pair.
 *   4. Compute face planes (nmg_calc_face_g), mark edges real, and
 *      compute bounding boxes (nmg_region_a).
 *
 * The entire assembly is O(V + F) with small constants.
 *
 * Returns 0 on success, -1 on failure.
 */
static int
dsp_build_nmg_from_bot(struct nmgregion **r_out,
		       struct model *m,
		       const struct rt_bot_internal *bot,
		       const struct bn_tol *tol,
		       struct bu_list *vlfree)
{
    struct nmgregion *r;
    struct shell *s;
    size_t i;

    if (!bot || bot->num_faces == 0 || bot->num_vertices == 0)
	return -1;

    /* Create region and shell (nmg_mrsv also creates one shell-vertex
     * placeholder; it will simply be an orphaned vertexuse).          */
    r = nmg_mrsv(m);
    s = BU_LIST_FIRST(shell, &r->s_hd);

    /* ---- Step 1: create every face with nmg_cface ---- */

    /* Track which NMG vertex belongs to each BOT vertex index.
     * Starts all-NULL; nmg_cface() fills in newly allocated vertices. */
    struct vertex **v_arr = (struct vertex **)bu_calloc(
	    bot->num_vertices, sizeof(struct vertex *), "dsp nmg v_arr");

    /* Storage for the faceuses so we can iterate them in step 3. */
    struct faceuse **fu_arr = (struct faceuse **)bu_calloc(
	    bot->num_faces, sizeof(struct faceuse *), "dsp nmg fu_arr");

    for (i = 0; i < bot->num_faces; i++) {
	int a = bot->faces[3*i + 0];
	int b = bot->faces[3*i + 1];
	int c = bot->faces[3*i + 2];

	struct vertex *corners[3];
	corners[0] = v_arr[a];
	corners[1] = v_arr[b];
	corners[2] = v_arr[c];

	struct faceuse *fu = nmg_cface(s, corners, 3);
	if (!fu) {
	    bu_log("dsp_build_nmg: nmg_cface failed for face %zu\n", i);
	    continue;
	}
	fu_arr[i] = fu;

	/* nmg_cface() fills NULL entries with freshly allocated vertices. */
	v_arr[a] = corners[0];
	v_arr[b] = corners[1];
	v_arr[c] = corners[2];
    }

    /* ---- Step 2: assign geometry to every vertex ---- */
    for (i = 0; i < bot->num_vertices; i++) {
	if (v_arr[i] && !v_arr[i]->vg_p) {
	    point_t pt;
	    VMOVE(pt, &bot->vertices[3*i]);
	    nmg_vertex_gv(v_arr[i], pt);
	}
    }

    /* ---- Step 3: join shared edges via a hash map ---- */

    /* Key type: pair of vertex pointers encoding a directed edge. */
    struct EdgeKey {
	const struct vertex *a;
	const struct vertex *b;
	bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
    };
    struct EdgeKeyHash {
	std::size_t operator()(const EdgeKey& k) const {
	    /* Golden-ratio mixing sized for the platform's pointer width. */
	    std::size_t ha = std::hash<const void *>()(k.a);
	    std::size_t hb = std::hash<const void *>()(k.b);
#if SIZE_MAX > 0xFFFFFFFFU
	    /* 64-bit: Knuth / Fibonacci multiplier for 64-bit size_t */
	    return ha ^ (hb * (std::size_t)11400714819323198485ULL + (ha >> 16));
#else
	    /* 32-bit */
	    return ha ^ (hb * (std::size_t)2654435761U + (ha >> 16));
#endif
	}
    };

    std::unordered_map<EdgeKey, struct edgeuse *, EdgeKeyHash> eu_map;
    eu_map.reserve(bot->num_faces * 3);

    for (i = 0; i < bot->num_faces; i++) {
	struct faceuse *fu = fu_arr[i];
	if (!fu) continue;

	/* Iterate only the OT_SAME loopuse. */
	struct loopuse *lu = BU_LIST_FIRST(loopuse, &fu->lu_hd);
	if (BU_LIST_FIRST_MAGIC(&lu->down_hd) != NMG_EDGEUSE_MAGIC)
	    continue;

	struct edgeuse *eu;
	for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {
	    const struct vertex *vs = eu->vu_p->v_p;
	    const struct vertex *ve = eu->eumate_p->vu_p->v_p;
	    eu_map[{vs, ve}] = eu;
	}
    }

    /* Walk the map once: for every (A→B) entry, look for (B→A) and
     * join them.  We use the pointer-ordering trick to process each
     * unordered pair exactly once and avoid double-joining.           */
    for (auto& kv : eu_map) {
	const struct vertex *va = kv.first.a;
	const struct vertex *vb = kv.first.b;

	/* Only process each geometric edge once (the lower-addr end first). */
	if (va > vb)
	    continue;

	auto it = eu_map.find({vb, va});
	if (it == eu_map.end())
	    continue; /* boundary / open edge — leave dangling */

	struct edgeuse *eu_fwd = kv.second;
	struct edgeuse *eu_rev = it->second;

	/* Guard: skip if already joined (radial != eumate means joined). */
	if (eu_fwd->radial_p != eu_fwd->eumate_p)
	    continue;

	nmg_je(eu_fwd, eu_rev);
    }

    /* ---- Step 4: compute face planes and bounding boxes ---- */
    for (i = 0; i < bot->num_faces; i++) {
	struct faceuse *fu = fu_arr[i];
	if (!fu) continue;
	if (nmg_calc_face_g(fu, vlfree))
	    nmg_kfu(fu);
    }

    nmg_mark_edges_real(&s->l.magic, vlfree);
    nmg_region_a(r, tol);

    bu_free(v_arr,  "dsp nmg v_arr");
    bu_free(fu_arr, "dsp nmg fu_arr");

    *r_out = r;
    return 0;
}

/**
 * Generate interior Steiner points for the DSP bottom face triangulation.
 *
 * Produces a grid of interior points inside @p outer_poly (and outside any
 * @p hole_polys) spaced no closer than @p min_distance to each other or to
 * any boundary/hole vertex already in the proximity index.
 *
 * Points are placed on a uniform grid with step = 2.5 * min_distance and
 * rejected when they fall outside the outer polygon, inside a hole polygon,
 * or closer than min_distance to an already-accepted point.
 *
 * Returns a vector of (x, y) pairs in world XY coordinates (z=0 plane).
 */
static std::vector<std::pair<double, double>>
dsp_generate_steiner_pts(
	const std::vector<std::pair<double, double>>& outer_poly,
	const std::vector<std::vector<std::pair<double, double>>>& hole_polys,
	double bb_min_x, double bb_max_x,
	double bb_min_y, double bb_max_y,
	double min_distance)
{
    TerraScape::SteinerIndex idx;

    /* Seed the proximity index with boundary / hole vertices so Steiner
     * points are kept away from the constraint edges.                    */
    for (const auto& p : outer_poly)
	idx.insert(p.first, p.second);
    for (const auto& hp : hole_polys)
	for (const auto& p : hp)
	    idx.insert(p.first, p.second);

    double step = min_distance * DSP_STEINER_GRID_SPACING_FACTOR;

    std::vector<std::pair<double, double>> result;
    for (double sy = bb_min_y + step * 0.5; sy < bb_max_y - step * 0.25; sy += step) {
	for (double sx = bb_min_x + step * 0.5; sx < bb_max_x - step * 0.25; sx += step) {
	    if (!TerraScape::pointInPolygon(sx, sy, outer_poly))
		continue;
	    bool in_hole = false;
	    for (const auto& hp : hole_polys) {
		if (TerraScape::pointInPolygon(sx, sy, hp)) {
		    in_hole = true;
		    break;
		}
	    }
	    if (in_hole)
		continue;
	    if (idx.hasNear(sx, sy, min_distance))
		continue;
	    idx.insert(sx, sy);
	    result.push_back({sx, sy});
	}
    }

    return result;
}


/**
 * Proposed decimation-based DSP tessellation.
 *
 * Implements the five-step pipeline:
 *   1. Build a naive two-triangle-per-cell surface mesh.
 *   2. Decimate with mmesh (error-bounded, O(N log N)).
 *   3. Extract outer/hole boundary loops from the decimated half-edge set.
 *   4. Triangulate the bottom face with detria (Delaunay + Steiner points).
 *   5. Assemble surface + walls + bottom into a closed NMG.
 *
 * The boundary half-edge extraction guarantees that wall winding is always
 * consistent with the surface: for a directed half-edge ta→tb, the triangles
 * (ta, bot_a, bot_b) and (ta, bot_b, tb) produce an outward-pointing normal
 * regardless of whether the loop belongs to the outer boundary or a hole.
 * This identity holds for both CCW (positive signed area) outer loops and
 * CW (negative signed area) hole loops as verified analytically.
 *
 * Returns 0 on success; -1 if decimation or triangulation fails.
 * On success, *r_out is the newly created NMG region.
 */
static int
dsp_tess_with_decimation(
	struct nmgregion **r_out,
	struct model *m,
	const struct rt_dsp_internal *dsp_ip,
	const TerraScape::TerrainData& terrain,
	double effective_err,
	const struct bn_tol *tol,
	struct bu_list *vlfree)
{
    const int W = terrain.width;
    const int H = terrain.height;
    if (W < 2 || H < 2)
	return -1;

    /* ------------------------------------------------------------------ */
    /* Step 1: naive surface mesh — two CCW triangles per grid cell        */
    /* ------------------------------------------------------------------ */
    const size_t n_grid_verts = (size_t)W * H;
    std::vector<double> surf_verts(n_grid_verts * 3);

    for (int gy = 0; gy < H; ++gy) {
	for (int gx = 0; gx < W; ++gx) {
	    size_t i = (size_t)gy * W + gx;
	    surf_verts[3*i + 0] = terrain.origin.x + gx * terrain.cell_size;
	    surf_verts[3*i + 1] = terrain.origin.y - gy * terrain.cell_size;
	    surf_verts[3*i + 2] = terrain.getHeight(gx, gy);
	}
    }

    std::vector<int> surf_faces;
    surf_faces.reserve((size_t)(W-1) * (H-1) * 6);

    for (int gy = 0; gy < H-1; ++gy) {
	for (int gx = 0; gx < W-1; ++gx) {
	    int v00 = gy * W + gx;
	    int v10 = gy * W + (gx + 1);
	    int v01 = (gy + 1) * W + gx;
	    int v11 = (gy + 1) * W + (gx + 1);
	    /* CCW from above (+Z normal): matches TerraScape convention. */
	    surf_faces.push_back(v00); surf_faces.push_back(v01); surf_faces.push_back(v10);
	    surf_faces.push_back(v10); surf_faces.push_back(v01); surf_faces.push_back(v11);
	}
    }

    size_t n_surf_verts = n_grid_verts;
    size_t n_surf_faces  = surf_faces.size() / 3;

    bu_log("DSP decimate: naive surface %zu verts %zu faces\n",
	   n_surf_verts, n_surf_faces);

    /* ------------------------------------------------------------------ */
    /* Step 2: decimate surface with mmesh                                 */
    /* ------------------------------------------------------------------ */

    /* Compute world units per grid cell from dsp_stom to convert the
     * error tolerance (in world units) into grid units for mmesh.        */
    double grid_scale = 1.0;
    {
	point_t o_grid = {0.0, 0.0, 0.0};
	point_t x_grid = {1.0, 0.0, 0.0};
	point_t o_world, x_world;
	MAT4X3PNT(o_world, dsp_ip->dsp_stom, o_grid);
	MAT4X3PNT(x_world, dsp_ip->dsp_stom, x_grid);
	double ds = sqrt(
	    (x_world[0]-o_world[0])*(x_world[0]-o_world[0]) +
	    (x_world[1]-o_world[1])*(x_world[1]-o_world[1]) +
	    (x_world[2]-o_world[2])*(x_world[2]-o_world[2]));
	if (ds > 1e-10)
	    grid_scale = ds;
    }

    /* Feature size in grid units: error tolerance / scale factor.
     * Apply the same 2/3-power adjustment as rt_bot_decimate_gct() uses
     * for backward compatibility with the legacy GCT cost model.         */
    double raw_feature = effective_err / grid_scale;
    double grid_diag   = sqrt((double)(W-1)*(W-1) + (double)(H-1)*(H-1));
    if (raw_feature < DSP_DECIMATE_MIN_FEATURE)
	raw_feature = DSP_DECIMATE_MIN_FEATURE;
    if (raw_feature > grid_diag * DSP_DECIMATE_MAX_FEATURE_RATIO)
	raw_feature = grid_diag * DSP_DECIMATE_MAX_FEATURE_RATIO;
    double fsize = pow(raw_feature, DSP_DECIMATE_STRENGTH_EXPONENT)
		 * DSP_DECIMATE_STRENGTH_SCALE;

    {
	mdOperation mdop;
	mdOperationInit(&mdop);
	mdOperationData(&mdop,
			n_surf_verts, surf_verts.data(),
			MD_FORMAT_DOUBLE, 3 * sizeof(double),
			n_surf_faces,  surf_faces.data(),
			MD_FORMAT_INT,    3 * sizeof(int));
	mdOperationStrength(&mdop, fsize);

	/* MD_FLAGS_PLANAR_MODE: prevent face normals from flipping, which
	 * is essential for height-field surfaces.
	 * MD_FLAGS_TRIANGLE_WINDING_CCW: preserve the CCW orientation.   */
	int flags = MD_FLAGS_PLANAR_MODE | MD_FLAGS_TRIANGLE_WINDING_CCW;
	int n_threads = (int)bu_avail_cpus();
	if (n_threads < 1) n_threads = 1;

	mdMeshDecimation(&mdop, n_threads, flags);

	n_surf_verts = (size_t)mdop.vertexcount;
	n_surf_faces  = (size_t)mdop.tricount;
	surf_verts.resize(n_surf_verts * 3);
	surf_faces.resize(n_surf_faces  * 3);

	bu_log("DSP decimate: → %zu verts %zu faces (raw_feature=%g fsize=%g)\n",
	       n_surf_verts, n_surf_faces, raw_feature, fsize);
    }

    if (n_surf_faces == 0) {
	bu_log("DSP decimate: all faces removed by decimation\n");
	return -1;
    }

    /* ------------------------------------------------------------------ */
    /* Step 3: extract boundary loops via directed half-edges              */
    /*                                                                     */
    /* A half-edge (va → vb) is a boundary edge when its reverse           */
    /* (vb → va) has no corresponding triangle.  We encode each directed  */
    /* edge as a uint64_t key = (uint32_t(va) << 32) | uint32_t(vb).      */
    /* ------------------------------------------------------------------ */
    std::unordered_map<uint64_t, bool> fwd_edges;
    fwd_edges.reserve(n_surf_faces * 3 * 2);

    for (size_t f = 0; f < n_surf_faces; ++f) {
	uint32_t va = (uint32_t)surf_faces[3*f + 0];
	uint32_t vb = (uint32_t)surf_faces[3*f + 1];
	uint32_t vc = (uint32_t)surf_faces[3*f + 2];
	fwd_edges[((uint64_t)va << 32) | vb] = true;
	fwd_edges[((uint64_t)vb << 32) | vc] = true;
	fwd_edges[((uint64_t)vc << 32) | va] = true;
    }

    /* bdry_next[va] = vb means the directed boundary half-edge va→vb exists. */
    std::unordered_map<int, int> bdry_next;
    for (const auto& kv : fwd_edges) {
	int va = (int)(kv.first >> 32);
	int vb = (int)(kv.first & 0xFFFFFFFFu);
	uint64_t rev = ((uint64_t)(uint32_t)vb << 32) | (uint32_t)va;
	if (!fwd_edges.count(rev))
	    bdry_next[va] = vb;
    }

    if (bdry_next.empty()) {
	bu_log("DSP decimate: no boundary edges found\n");
	return -1;
    }

    /* Chain boundary half-edges into closed loops. */
    std::vector<std::vector<int>> boundary_loops;
    {
	std::unordered_set<int> visited;
	for (const auto& kv : bdry_next) {
	    int start = kv.first;
	    if (visited.count(start))
		continue;
	    std::vector<int> loop;
	    int cur = start;
	    for (;;) {
		if (visited.count(cur))
		    break;
		visited.insert(cur);
		loop.push_back(cur);
		auto it = bdry_next.find(cur);
		if (it == bdry_next.end())
		    break;
		cur = it->second;
	    }
	    if (loop.size() >= 3)
		boundary_loops.push_back(std::move(loop));
	}
    }

    bu_log("DSP decimate: %zu boundary loop(s)\n", boundary_loops.size());
    if (boundary_loops.empty()) {
	bu_log("DSP decimate: no valid boundary loops found\n");
	return -1;
    }

    /* Compute signed area of each loop (shoelace formula).
     * Positive area (CCW in world XY) = outer boundary.
     * Negative area (CW) = interior hole.
     * The outer boundary is identified as the loop with the largest |area|. */
    int outer_loop_idx = -1;
    double max_abs_area = 0.0;
    std::vector<double> loop_area(boundary_loops.size(), 0.0);

    for (size_t li = 0; li < boundary_loops.size(); ++li) {
	const auto& loop = boundary_loops[li];
	double sa = 0.0;
	size_t n = loop.size();
	for (size_t i = 0; i < n; ++i) {
	    int va = loop[i], vb = loop[(i + 1) % n];
	    double xa = surf_verts[3*va], ya = surf_verts[3*va + 1];
	    double xb = surf_verts[3*vb], yb = surf_verts[3*vb + 1];
	    sa += xa * yb - xb * ya;
	}
	loop_area[li] = 0.5 * sa;
	if (std::abs(sa) > max_abs_area) {
	    max_abs_area = std::abs(sa);
	    outer_loop_idx = (int)li;
	}
    }

    if (outer_loop_idx < 0) {
	bu_log("DSP decimate: could not identify outer boundary loop\n");
	return -1;
    }

    /* ------------------------------------------------------------------ */
    /* Step 4: build walls + prepare bottom face data                      */
    /*                                                                     */
    /* For each directed boundary half-edge ta→tb (regardless of loop     */
    /* orientation), the triangles:                                        */
    /*   (ta, bot_a, bot_b)  and  (ta, bot_b, tb)                         */
    /* always produce an outward-pointing normal.  This is verified        */
    /* analytically: for CCW outer loops the normal points away from the   */
    /* terrain body; for CW hole loops it points toward the hole interior  */
    /* (away from the terrain body).                                       */
    /* ------------------------------------------------------------------ */
    std::vector<double> all_verts(
	surf_verts.begin(), surf_verts.begin() + n_surf_verts * 3);
    std::vector<int>    all_faces(
	surf_faces.begin(), surf_faces.begin() + n_surf_faces * 3);

    /* Map surface vertex index → bottom vertex index (z = 0 plane).
     *
     * Special case: when a surface vertex already sits at z ≈ 0 (terrain
     * height = 0), the surface vertex IS the bottom vertex — they are the
     * same geometric point.  Return the surface index itself so that the
     * NMG vertex pointer is shared.  This avoids degenerate wall triangles
     * (and the resulting open edges they leave behind when removed).       */
    std::unordered_map<int, int> surf_to_bot;

    auto add_bottom_vert = [&](int sv) -> int {
	/* Terrain heights are non-negative integers; z ≤ 0 means ground. */
	if (surf_verts[3*sv + 2] <= 0.0)
	    return sv;  /* surface vertex IS the bottom vertex */
	auto it = surf_to_bot.find(sv);
	if (it != surf_to_bot.end()) return it->second;
	int bv = (int)(all_verts.size() / 3);
	all_verts.push_back(surf_verts[3*sv + 0]);
	all_verts.push_back(surf_verts[3*sv + 1]);
	all_verts.push_back(0.0);
	surf_to_bot[sv] = bv;
	return bv;
    };

    /* Pre-allocate bottom vertices for all boundary loops so that
     * surf_to_bot is complete before we emit wall triangles.              */
    std::vector<std::vector<size_t>> loop_bot_indices(boundary_loops.size());
    for (size_t li = 0; li < boundary_loops.size(); ++li) {
	for (int sv : boundary_loops[li])
	    loop_bot_indices[li].push_back((size_t)add_bottom_vert(sv));
    }

    /* Emit wall triangles for every boundary loop.
     *
     * For each directed boundary half-edge (va → vb), the wall quad
     * decomposes into two triangles:
     *   T1: (va, bot_a, bot_b)   — lower-left + lower-right + top-left
     *   T2: (va, bot_b, vb)      — lower-left + lower-right + top-right
     *
     * When a surface vertex is at height 0 (i.e., bot_x == vx), one of the
     * triangles degenerates to a zero-area sliver.  Skip degenerate
     * triangles (any two indices equal) to prevent nmg_kfu removing them
     * and leaving open edges in the assembled mesh.                        */
    for (size_t li = 0; li < boundary_loops.size(); ++li) {
	const auto& loop = boundary_loops[li];
	size_t n = loop.size();
	for (size_t i = 0; i < n; ++i) {
	    int va    = loop[i];
	    int vb    = loop[(i + 1) % n];
	    int bot_a = (int)loop_bot_indices[li][i];
	    int bot_b = (int)loop_bot_indices[li][(i + 1) % n];
	    /* T1: (va, bot_a, bot_b) — skip if degenerate */
	    if (va != bot_a && va != bot_b && bot_a != bot_b) {
		all_faces.push_back(va);    all_faces.push_back(bot_a); all_faces.push_back(bot_b);
	    }
	    /* T2: (va, bot_b, vb) — skip if degenerate */
	    if (va != bot_b && va != vb && bot_b != vb) {
		all_faces.push_back(va);    all_faces.push_back(bot_b); all_faces.push_back(vb);
	    }
	}
    }

    /* ------------------------------------------------------------------ */
    /* Step 5: triangulate bottom face with detria + Steiner points        */
    /* ------------------------------------------------------------------ */

    /* Bounding box (used for Steiner point grid).                         */
    double bb_min_x = surf_verts[0], bb_max_x = surf_verts[0];
    double bb_min_y = surf_verts[1], bb_max_y = surf_verts[1];
    for (size_t i = 0; i < n_surf_verts; ++i) {
	double x = surf_verts[3*i], y = surf_verts[3*i + 1];
	if (x < bb_min_x) bb_min_x = x;
	if (x > bb_max_x) bb_max_x = x;
	if (y < bb_min_y) bb_min_y = y;
	if (y > bb_max_y) bb_max_y = y;
    }

    /* Build 2D outer boundary polygon and hole polygons for detria.
     * The outer loop index was computed above (largest |area|).           */
    const auto& outer_loop = boundary_loops[outer_loop_idx];
    std::vector<std::pair<double, double>> outer_poly_2d;
    std::vector<size_t> outer_bot_idx;
    for (size_t i = 0; i < outer_loop.size(); ++i) {
	int sv = outer_loop[i];
	outer_poly_2d.push_back({surf_verts[3*sv], surf_verts[3*sv + 1]});
	outer_bot_idx.push_back(loop_bot_indices[outer_loop_idx][i]);
    }

    std::vector<std::vector<std::pair<double, double>>> hole_polys_2d;
    std::vector<std::vector<size_t>> hole_bot_idx;
    for (size_t li = 0; li < boundary_loops.size(); ++li) {
	if ((int)li == outer_loop_idx)
	    continue;
	const auto& hloop = boundary_loops[li];
	std::vector<std::pair<double, double>> hpoly;
	for (int sv : hloop)
	    hpoly.push_back({surf_verts[3*sv], surf_verts[3*sv + 1]});
	hole_polys_2d.push_back(std::move(hpoly));
	hole_bot_idx.push_back(loop_bot_indices[li]);
    }

    /* Steiner points: use 3 grid cells as minimum spacing. */
    double min_dist = terrain.cell_size * DSP_STEINER_MIN_CELL_SPACING;
    auto steiner_pts = dsp_generate_steiner_pts(
	outer_poly_2d, hole_polys_2d,
	bb_min_x, bb_max_x, bb_min_y, bb_max_y,
	min_dist);

    bu_log("DSP decimate: %zu Steiner points for bottom face\n",
	   steiner_pts.size());

    /* Assemble the detria point list and index mapping
     * (detria index → index into all_verts).                              */
    std::vector<detria::PointD> dtri_pts;
    std::vector<size_t>         dtri_to_mesh;

    /* Outer boundary. */
    size_t outer_dtri_count = outer_poly_2d.size();
    for (size_t i = 0; i < outer_dtri_count; ++i) {
	dtri_pts.push_back({outer_poly_2d[i].first, outer_poly_2d[i].second});
	dtri_to_mesh.push_back(outer_bot_idx[i]);
    }

    /* Hole boundaries. */
    std::vector<std::pair<size_t, size_t>> hole_ranges; /* [start, count) */
    for (size_t hi = 0; hi < hole_polys_2d.size(); ++hi) {
	size_t hstart = dtri_pts.size();
	for (size_t i = 0; i < hole_polys_2d[hi].size(); ++i) {
	    dtri_pts.push_back({hole_polys_2d[hi][i].first,
				hole_polys_2d[hi][i].second});
	    dtri_to_mesh.push_back(hole_bot_idx[hi][i]);
	}
	hole_ranges.push_back({hstart, dtri_pts.size() - hstart});
    }

    /* Steiner points become extra bottom-face vertices. */
    for (const auto& sp : steiner_pts) {
	int mesh_idx = (int)(all_verts.size() / 3);
	all_verts.push_back(sp.first);
	all_verts.push_back(sp.second);
	all_verts.push_back(0.0);
	dtri_pts.push_back({sp.first, sp.second});
	dtri_to_mesh.push_back((size_t)mesh_idx);
    }

    /* Run Delaunay triangulation.
     * IMPORTANT: ReadonlySpan stores a raw pointer into the vector data.
     * All index vectors must remain alive until after triangulate() returns. */
    detria::Triangulation dtri;
    dtri.setPoints(dtri_pts);

    std::vector<uint32_t> oidx(outer_dtri_count);
    for (uint32_t i = 0; i < (uint32_t)outer_dtri_count; ++i)
	oidx[i] = i;
    dtri.addOutline(oidx);

    /* Keep hole index vectors alive alongside the Triangulation object. */
    std::vector<std::vector<uint32_t>> hidx_storage;
    for (const auto& hr : hole_ranges) {
	hidx_storage.emplace_back(hr.second);
	for (uint32_t i = 0; i < (uint32_t)hr.second; ++i)
	    hidx_storage.back()[i] = (uint32_t)(hr.first + i);
	dtri.addHole(hidx_storage.back());
    }

    bool dtri_ok = false;
    try {
	dtri_ok = dtri.triangulate(true); /* true = Delaunay */
    } catch (const std::exception& e) {
	bu_log("DSP decimate: detria threw exception: %s\n", e.what());
    }

    if (!dtri_ok) {
	bu_log("DSP decimate: detria failed: %s\n",
	       dtri.getErrorMessage().c_str());
    }

    if (dtri_ok) {
	/* Add bottom triangles with reversed winding (normal points -Z). */
	dtri.forEachTriangle([&](detria::Triangle<uint32_t> t) {
	    size_t v0 = dtri_to_mesh[t.x];
	    size_t v1 = dtri_to_mesh[t.y];
	    size_t v2 = dtri_to_mesh[t.z];
	    /* Reversed: addTriangle(v0, v2, v1) pattern from TerraScape. */
	    all_faces.push_back((int)v0);
	    all_faces.push_back((int)v2);
	    all_faces.push_back((int)v1);
	}, false); /* false = CCW triangles from detria */
    } else {
	bu_log("DSP decimate: detria failed, using fan fallback for bottom face\n");
	/* Fan triangulation from first boundary vertex (works for convex). */
	if (outer_bot_idx.size() >= 3) {
	    for (size_t i = 1; i + 1 < outer_bot_idx.size(); ++i) {
		all_faces.push_back((int)outer_bot_idx[0]);
		all_faces.push_back((int)outer_bot_idx[i + 1]);
		all_faces.push_back((int)outer_bot_idx[i]);
	    }
	}
    }

    /* ------------------------------------------------------------------ */
    /* Step 6: apply dsp_stom transform and build NMG                     */
    /* ------------------------------------------------------------------ */
    size_t n_all_verts = all_verts.size() / 3;
    size_t n_all_faces = all_faces.size() / 3;

    bu_log("DSP decimate: final mesh %zu verts %zu faces\n",
	   n_all_verts, n_all_faces);

    if (n_all_faces == 0)
	return -1;

    struct rt_bot_internal bot_ip;
    memset(&bot_ip, 0, sizeof(bot_ip));
    bot_ip.magic        = RT_BOT_INTERNAL_MAGIC;
    bot_ip.num_vertices = (int)n_all_verts;
    bot_ip.num_faces    = (int)n_all_faces;
    bot_ip.mode         = RT_BOT_SOLID;
    bot_ip.orientation  = RT_BOT_CCW;

    bot_ip.vertices = (fastf_t *)bu_malloc(
	3 * n_all_verts * sizeof(fastf_t), "dsp decimate bot verts");
    bot_ip.faces    = (int *)    bu_malloc(
	3 * n_all_faces * sizeof(int),     "dsp decimate bot faces");

    for (size_t i = 0; i < n_all_verts; ++i) {
	point_t in_pt  = { all_verts[3*i], all_verts[3*i+1], all_verts[3*i+2] };
	point_t out_pt;
	MAT4X3PNT(out_pt, dsp_ip->dsp_stom, in_pt);
	VMOVE(&bot_ip.vertices[3*i], out_pt);
    }
    for (size_t f = 0; f < n_all_faces; ++f) {
	bot_ip.faces[3*f + 0] = all_faces[3*f + 0];
	bot_ip.faces[3*f + 1] = all_faces[3*f + 1];
	bot_ip.faces[3*f + 2] = all_faces[3*f + 2];
    }

    int ret = dsp_build_nmg_from_bot(r_out, m, &bot_ip, tol, vlfree);

    bu_free(bot_ip.vertices, "dsp decimate bot verts");
    bu_free(bot_ip.faces,    "dsp decimate bot faces");

    return ret;
}


/**
 * Returns -
 * -1 failure
 * 0 OK.  *r points to nmgregion that holds this tessellation.
 */
extern "C" int
rt_dsp_tess(struct nmgregion **r, struct model *m, struct rt_db_internal *ip, const struct bg_tess_tol *ttol, const struct bn_tol *tol)
{
    struct rt_dsp_internal *dsp_ip;

    if (RT_G_DEBUG & RT_DEBUG_HF)
	bu_log("rt_dsp_tess()\n");

    RT_CK_DB_INTERNAL(ip);
    dsp_ip = (struct rt_dsp_internal *)ip->idb_ptr;
    RT_DSP_CK_MAGIC(dsp_ip);

    switch (dsp_ip->dsp_datasrc) {
	case RT_DSP_SRC_V4_FILE:
	case RT_DSP_SRC_FILE:
	    if (!dsp_ip->dsp_mp) {
		bu_log("WARNING: Cannot find data file for displacement map (DSP)\n");
		if (bu_vls_addr(&dsp_ip->dsp_name)) {
		    bu_log("         DSP data file [%s] not found or empty\n", bu_vls_cstr(&dsp_ip->dsp_name));
		} else {
		    bu_log("         DSP data file not found or not specified\n");
		}
		return -1;
	    }
	    break;
	case RT_DSP_SRC_OBJ:
	    if (!dsp_ip->dsp_bip) {
		bu_log("WARNING: Cannot find data object for displacement map (DSP)\n");
		if (bu_vls_addr(&dsp_ip->dsp_name)) {
		    bu_log("         DSP data object [%s] not found or empty\n", bu_vls_cstr(&dsp_ip->dsp_name));
		} else {
		    bu_log("         DSP data object not found or not specified\n");
		}
		return -1;
	    }
	    RT_CK_DB_INTERNAL(dsp_ip->dsp_bip);
	    RT_CK_BINUNIF(dsp_ip->dsp_bip->idb_ptr);
	    break;
    }

    // Step 1: Create TerraScape DSPData from rt_dsp_internal
    TerraScape::DSPData dsp;
    dsp.dsp_buf = dsp_ip->dsp_buf;           // Point to existing buffer (owned by BRL-CAD)
    dsp.dsp_xcnt = dsp_ip->dsp_xcnt;         // Copy dimensions
    dsp.dsp_ycnt = dsp_ip->dsp_ycnt;
    dsp.cell_size = 1.0;                     // Will be scaled by transformation matrix
    dsp.origin = TerraScape::Point3D(0, 0, 0);
    dsp.owns_buffer = false;                 // Don't delete BRL-CAD's buffer

    // Step 2. Convert to TerrainData
    TerraScape::TerrainData terrain;
    if (!dsp.toTerrain(terrain)) {
	bu_log("Failed to convert DSP buffer to TerrainData\n");
	return -1;
    }

    // Step 3.  Decide triangulation strategy based on tolerances -
    // we need to translate BRL-CAD's tolerances into those used by
    // the terrain algorithms.
    TerraScape::TerrainMesh mesh;

    point_t dsp_bb_min, dsp_bb_max;
    if (rt_dsp_bbox(ip, &dsp_bb_min, &dsp_bb_max, tol)) {
	/* Fallback if bbox computation fails */
	VSETALL(dsp_bb_min, 0.0);
	VSETALL(dsp_bb_max, 0.0);
    }
    double dx = dsp_bb_max[0] - dsp_bb_min[0];
    double dy = dsp_bb_max[1] - dsp_bb_min[1];
    double dz = dsp_bb_max[2] - dsp_bb_min[2];
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    if (dz < 0) dz = 0;
    double diag = sqrt(dx*dx + dy*dy + dz*dz);
    double height_range = dz;

    /* Extract tessellation tolerances */
    double abs_tol = (ttol && ttol->abs > 0.0) ? ttol->abs : INFINITY;
    double rel_tol = (ttol && ttol->rel > 0.0) ? (ttol->rel * (diag > 0.0 ? diag : 1.0)) : INFINITY;

    double effective_err = INFINITY;
    if (abs_tol < INFINITY && rel_tol < INFINITY)
	effective_err = (abs_tol < rel_tol) ? abs_tol : rel_tol;
    else if (abs_tol < INFINITY)
	effective_err = abs_tol;
    else if (rel_tol < INFINITY)
	effective_err = rel_tol;

    /* Provide a fallback if neither tolerance is set */
    double base_cell = 1.0; /* original grid spacing pre-transform */
    if (!isfinite(effective_err))
	effective_err = base_cell * 0.25;

    /* Respect modeling tolerance floor */
    if (tol && tol->dist > 0.0 && effective_err < tol->dist * 0.5)
	effective_err = tol->dist * 0.5;

    /* Normal tolerance to slope threshold */
    double slope_threshold = 0.2; /* default fallback */
    if (ttol && ttol->norm > 0.0) {
	double angle_rad = 0.0;
	if (ttol->norm < 1.0) {
	    /* treat as cosine of angle */
	    if (ttol->norm > 0.0) {
		double c = ttol->norm;
		if (c > 1.0) c = 1.0;
		if (c < -1.0) c = -1.0;
		angle_rad = acos(c);
	    }
	} else {
	    /* treat as degrees */
	    angle_rad = ttol->norm * (M_PI / 180.0);
	}
	if (angle_rad > 0.0) {
	    double t = tan(angle_rad);
	    if (t < 0.0) t = -t;
	    /* clamp to avoid runaway */
	    if (t > 10.0) t = 10.0;
	    slope_threshold = t;
	}
    }

    /* Derive a heuristic reduction target */
    int min_reduction = 0;
    if (height_range > 1e-9) {
	double hscale = effective_err / height_range;
	if (hscale < 0.0) hscale = 0.0;
	if (hscale > 1.0) hscale = 1.0;
	min_reduction = (int)(hscale * 80.0); /* up to 80% if very loose */
    }
    if (min_reduction < 0) min_reduction = 0;
    if (min_reduction > 90) min_reduction = 90;

    TerraScape::SimplificationParams simp;
    simp.setErrorTol(effective_err);
    simp.setSlopeTol(slope_threshold);
    simp.setMinReduction(min_reduction);
    simp.setPreserveBounds(true);

    int use_simplified = 0;
    /* Decide whether to simplify:
       - If effective error significantly larger than base cell
       - Or slope tolerance generous
       */
    if (effective_err > base_cell * 0.6 || slope_threshold > 0.6) {
	use_simplified = 1;
    }

    if (RT_G_DEBUG & RT_DEBUG_HF) {
	bu_log("DSP tess tol mapping:\n");
	bu_log("  bbox: min=(%g %g %g) max=(%g %g %g)\n",
		V3ARGS(dsp_bb_min), V3ARGS(dsp_bb_max));
	bu_log("  abs_tol=%g rel_tol=%g -> eff=%g\n", abs_tol, rel_tol, effective_err);
	bu_log("  slope_threshold=%g min_triangle_reduction=%d (height_range=%g diag=%g)\n",
		slope_threshold, min_reduction, height_range, diag);
	bu_log("  using %s path\n", use_simplified ? "simplified" : "full");
    }

    bu_log("DSP tess: bbox diag=%g eff_err=%g use_simplified=%d min_reduction=%d step=%d terrain=%dx%d\n",
	    diag, effective_err, use_simplified, min_reduction,
	    std::max(1, (int)std::sqrt(100.0 / (100.0 - std::max(0, std::min(90, min_reduction))))),
	    terrain.width, terrain.height);

    // Step 4.  Make the tessellation.
    //
    // For the simplified path we use the proposed decimation-based approach:
    //   1. Naive two-triangle-per-cell surface mesh
    //   2. mmesh decimation (error-bounded, O(N log N))
    //   3. Boundary loop extraction from the half-edge set
    //   4. detria Delaunay bottom face with Steiner points
    //   5. Assemble into NMG
    //
    // The full path retains the original TerraScape triangulateVolume.
    if (use_simplified) {
	struct bu_list vlfree;
	BU_LIST_INIT(&vlfree);
	int ret = dsp_tess_with_decimation(r, m, dsp_ip, terrain,
					   effective_err, tol, &vlfree);
	bu_list_free(&vlfree);
	if (ret == 0)
	    return 0;
	/* Fall through to the full TerraScape path on failure. */
	bu_log("DSP decimate path failed; falling back to triangulateVolume\n");
	mesh.triangulateVolume(terrain);
    } else {
	mesh.triangulateVolume(terrain);
    }
    bu_log("DSP tess: mesh has %zu vertices, %zu triangles\n",
	    mesh.vertices.size(), mesh.triangles.size());
    if (mesh.vertices.empty() || mesh.triangles.empty()) {
	bu_log("TerraScape produced empty mesh\n");
	return -1;
    }

    // Step 5.  Translate to BoT
    /* Allocate BOT internal */
    struct rt_bot_internal *bot_ip = (struct rt_bot_internal *)bu_calloc(1, sizeof(struct rt_bot_internal), "dsp bot_ip");
    bot_ip->magic = RT_BOT_INTERNAL_MAGIC;
    bot_ip->num_vertices = (int)mesh.vertices.size();
    bot_ip->num_faces = (int)mesh.triangles.size();
    bot_ip->vertices = (fastf_t *)bu_calloc(3 * bot_ip->num_vertices, sizeof(fastf_t), "bot verts");
    bot_ip->faces = (int *)bu_calloc(3 * bot_ip->num_faces, sizeof(int), "bot faces");
    bot_ip->thickness = NULL;
    bot_ip->face_mode = NULL;
    bot_ip->mode = RT_BOT_SOLID;
    bot_ip->orientation = RT_BOT_CCW;
    bot_ip->bot_flags = 0;
    bot_ip->face_normals = NULL;
    bot_ip->num_normals = 0;
    bot_ip->normals = NULL;
    bot_ip->num_face_normals = 0;
    bot_ip->face_normals = NULL;

    /* Populate vertices (apply dsp_stom) */
    for (size_t i = 0; i < bot_ip->num_vertices; ++i) {
	const TerraScape::Point3D &p = mesh.vertices[(size_t)i];
	point_t in = { p.x, p.y, p.z };
	point_t out;
	MAT4X3PNT(out, dsp_ip->dsp_stom, in);
	bot_ip->vertices[3*i+0] = out[0];
	bot_ip->vertices[3*i+1] = out[1];
	bot_ip->vertices[3*i+2] = out[2];
    }

    /* Populate faces */
    for (size_t f = 0; f < bot_ip->num_faces; ++f) {
	const TerraScape::Triangle &tri = mesh.triangles[(size_t)f];
	bot_ip->faces[3*f+0] = (int)tri.vertices[0];
	bot_ip->faces[3*f+1] = (int)tri.vertices[1];
	bot_ip->faces[3*f+2] = (int)tri.vertices[2];
    }

    /* Wrap in rt_db_internal and invoke fast manifold NMG assembler.
     * The assembler bypasses rt_bot_tess / nmg_cmface / nmg_findeu
     * and builds the NMG in O(V+F) using a hash-map edge-pairing step. */
    struct bu_list vlfree;
    BU_LIST_INIT(&vlfree);

    int ret = dsp_build_nmg_from_bot(r, m, bot_ip, tol, &vlfree);

    bu_list_free(&vlfree);

    /* Free our temporary BOT arrays */
    bu_free(bot_ip->vertices, "dsp bot verts");
    bu_free(bot_ip->faces,    "dsp bot faces");
    bu_free(bot_ip,           "dsp bot_ip");

    return ret;
}

/** @} */


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
