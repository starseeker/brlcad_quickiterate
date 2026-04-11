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

#include "vmath.h"
#include "raytrace.h"
#include "rt/functab.h"
#include "rt/geom.h"
#include "rt/primitives/bot.h"
#include "nmg.h"

#include "TerraScape.hpp"

/* private header */
#include "./dsp.h"

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

    // Step 4.  Make the TerraScape mesh (includes walls + bottom)
    if (use_simplified) {
	mesh.triangulateVolumeSimplified(terrain, simp);
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
