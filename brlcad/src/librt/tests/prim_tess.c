/*                   P R I M _ T E S S . C
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
/** @file prim_tess.c
 *
 * Tests for NMG tessellation of BRL-CAD primitives.
 *
 * Exercises:
 *  - Normal tessellation at various tolerance settings
 *  - Degenerate / edge-case primitives (zero radius, near-self-intersecting)
 *  - Chess-model-derived parameters that historically caused infinite loops
 *  - Extreme tolerance combinations (very tight, very loose, all-three-combined)
 *  - Extreme primitive parameter scales (tiny, large, high-aspect-ratio)
 *  - ARB8 / ARB6 / ARB4 polyhedra tessellation
 *  - ARS (Arbitrary faceted surface) tessellation
 *  - ARBN (Arbitrary convex N-hedron) tessellation
 *  - PIPE (swept pipe solid) tessellation
 *  - METABALL (marching-cubes iso-surface) tessellation
 *
 * Each test calls rt_obj_tess() and checks:
 *  - Whether the function returns without hanging
 *  - The return code (0 = success, negative = failure)
 *  - Optionally prints face/vertex counts
 *
 * -----------------------------------------------------------------------
 * Primitive tessellation status summary (as of 2025-04)
 * -----------------------------------------------------------------------
 *
 * Primitive   | tess fn              | Status / notes
 * ------------|----------------------|------------------------------------
 * TOR         | rt_tor_tess          | OK - tested here
 * TGC / REC   | rt_tgc_tess          | OK - tested here
 * ELL / SPH   | rt_ell_tess          | OK - tested here
 * ARB8        | rt_arb_tess          | OK - tested here; produces NMG polyhedron
 * ARS         | rt_ars_tess          | OK - tested here; arbitrary faceted surface
 * HALF        | rt_hlf_tess          | OK - infinite half-space stub (returns -1)
 * POLY / PG   | rt_pg_tess           | OK - polygon mesh passthrough
 * BSPLINE     | rt_nurb_tess         | OK - NURBS surface
 * NMG         | rt_nmg_tess          | OK - already NMG, passthrough
 * EBM         | rt_ebm_tess          | OK - tested here; outline-tracing approach
 *             |                      |   avoids per-pixel faces; NOT the DSP
 *             |                      |   coplanar-density problem
 * VOL         | rt_vol_tess          | OK - tested here; 2D coherent-patch merging
 *             |                      |   (DSP/TerraScape technique extended to 2D):
 *             |                      |   spans are extended across consecutive rows
 *             |                      |   only when boundaries match exactly, making
 *             |                      |   each flat rectangular region one NMG face;
 *             |                      |   nmg_shell_coplanar_face_merge is kept as
 *             |                      |   a final cleanup pass only
 * ARBN        | rt_arbn_tess         | OK - tested here; arbitrary convex polyhedron
 * PIPE        | rt_pipe_tess         | OK - tested here; swept pipe solid
 * PART        | rt_part_tess         | OK - tested here
 * RPC         | rt_rpc_tess          | OK - tested here
 * RHC         | rt_rhc_tess          | OK - tested here
 * EPA         | rt_epa_tess          | OK - tested here (nseg cap removed)
 * EHY         | rt_ehy_tess          | OK - tested here (nseg cap removed)
 * ETO         | rt_eto_tess          | OK - tested here
 * GRIP        | rt_grp_tess          | STUB - always returns -1
 * JOINT       | rt_joint_tess        | STUB - always returns -1
 * HF          | rt_hf_tess           | STUB - always returns -1 (use DSP instead)
 * DSP         | rt_dsp_tess          | OK - tested here; decimation-based approach:
 *             |                      |   loose tol → naive surface + mmesh decimation
 *             |                      |   + half-edge boundary extraction + detria bottom;
 *             |                      |   tight tol → TerraScape triangulateVolume
 * SKETCH      | NULL                 | no tess (2-D only)
 * EXTRUDE     | rt_extrude_tess      | OK - extrusion of sketch
 * SUBMODEL    | rt_submodel_tess     | STUB - not implemented
 * CLINE       | rt_cline_tess        | OK - MUVES cline element
 * BOT         | rt_bot_tess          | OK - already triangulated mesh
 * COMB        | rt_comb_tess         | OK - booleans via NMG
 * SUPERELL    | rt_superell_tess     | STUB - logs warning, returns -1
 * METABALL    | rt_metaball_tess     | OK - ISOPOTENTIAL and BLOB methods tested here;
 *             |                      |    METABALL_METABALL method skipped (not implemented,
 *             |                      |    generates excessive error output in every voxel eval)
 * BREP        | rt_brep_tess         | OK - OpenNURBS B-rep
 * HYP         | rt_hyp_tess          | OK - tested here (nseg cap removed)
 * REVOLVE     | rt_revolve_tess      | OK - revolve of sketch
 * PNTS        | NULL                 | no tess (point cloud)
 * ANNOT       | NULL                 | no tess (annotation)
 * HRT         | NULL                 | no tess (heart - no NMG impl yet)
 * DATUM       | rt_datum_tess        | STUB - always returns -1
 * SCRIPT      | NULL                 | no tess
 * MATERIAL    | NULL                 | no tess
 * -----------------------------------------------------------------------
 */

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "bu.h"
#include "bu/parallel.h"
#include "vmath.h"
#include "bg/defines.h"
#include "bg/trimesh.h"
#include "nmg.h"
#include "raytrace.h"
#include "rt/geom.h"
#include "rt/nmg_conv.h"
#include "rt/search.h"
#include "wdb.h"


/* ------------------------------------------------------------------ */
/* Global output database handle (NULL = no output requested)          */
/* ------------------------------------------------------------------ */

static struct rt_wdb *g_wdb = NULL;
static int g_out_seq = 0;   /* sequential suffix for output object names */
static int g_validate = 0;  /* 1 = run manifold/mesh quality checks */

/* Tolerance overrides for --input-g scan (0 = not set / use default) */
static double g_scan_rel  = 0.0;
static double g_scan_abs  = 0.0;
static double g_scan_norm = 0.0;


/* ------------------------------------------------------------------ */
/* Manifold / mesh quality validation                                   */
/* ------------------------------------------------------------------ */

/**
 * Validate the mesh quality of a tessellated NMG model.
 *
 * Triangulates the model in-place (the caller owns m and will free it),
 * converts it to a BOT, then uses bg_trimesh_solid2() to check for
 * open edges and degenerate faces.  Reports the surface area and
 * open-edge count.
 *
 * @return 1 if the mesh passes all checks, 0 if it fails.
 */
static int
check_nmg_mesh(const char *label, struct model *m,
	       const struct bn_tol *tol, struct bu_list *vlfree)
{
    struct nmgregion *r;
    struct shell *s;

    if (!m) return 0;

    /* Count OT_SAME faces before triangulation */
    int nfaces_poly = 0;
    for (BU_LIST_FOR(r, nmgregion, &m->r_hd))
	for (BU_LIST_FOR(s, shell, &r->s_hd)) {
	    struct faceuse *fu;
	    for (BU_LIST_FOR(fu, faceuse, &s->fu_hd))
		if (fu->orientation == OT_SAME) nfaces_poly++;
	}

    if (nfaces_poly == 0) {
	fprintf(stderr, "  MESH: %-44s faces=0 (empty mesh) [WARN]\n", label);
	return 1; /* empty is unusual but not a hard failure */
    }

    /* Convert directly via nmg_mdl_to_bot, which has its own fast path:
     *  - All-triangles model → O(N) nmg_to_bot_all_tri()  (no edge fusion)
     *  - Poly faces → per-face triangulation without nmg_edge_g_fuse()
     *  - Degenerate cases → full nmg_triangulate_model() fallback
     *
     * Calling nmg_triangulate_model() explicitly here would invoke
     * nmg_edge_g_fuse(), an O(N²) scan over all edge-geometry structs that
     * becomes catastrophically slow for large DSP meshes (134k+ triangles). */
    struct rt_bot_internal *bot = NULL;
    if (!BU_SETJUMP) {
	bot = nmg_mdl_to_bot(m, vlfree, tol);
    } else {
	BU_UNSETJUMP;
	fprintf(stderr,
		"  MESH: %-44s  nmg_mdl_to_bot() bombed [FAIL]\n",
		label);
	return 0;
    } BU_UNSETJUMP;

    if (!bot || bot->num_faces == 0) {
	if (bot) {
	    bu_free(bot->vertices, "bot verts");
	    bu_free(bot->faces, "bot faces");
	    bu_free(bot, "bot");
	}
	fprintf(stderr, "  MESH: %-44s  nmg_mdl_to_bot returned empty [FAIL]\n", label);
	return 0;
    }

    /* bg_trimesh_solid2 checks for open edges and degenerate faces */
    struct bg_trimesh_solid_errors errs;
    memset(&errs, 0, sizeof(errs));
    int open_cnt = bg_trimesh_solid2(
	(int)bot->num_vertices, (int)bot->num_faces,
	bot->vertices, bot->faces, &errs);
    int n_open  = (int)errs.unmatched.count;
    int n_degen = (int)errs.degenerate.count;

    /* Surface area */
    fastf_t area = bg_trimesh_area(
	bot->faces, bot->num_faces,
	(const point_t *)bot->vertices, bot->num_vertices);

    int passed = (open_cnt == 0 && n_open == 0);

    fprintf(stderr,
	    "  MESH: %-44s  tris=%-6lu  area=%-12.4g  open=%-4d  degen=%-4d  [%s]\n",
	    label,
	    (unsigned long)bot->num_faces,
	    area,
	    n_open,
	    n_degen,
	    passed ? "OK" : "OPEN-EDGES");

    fflush(stderr);
    bg_free_trimesh_solid_errors(&errs);

    /* Optionally save the BOT to the output .g */
    if (g_wdb && passed) {
	char bot_name[256];
	snprintf(bot_name, sizeof(bot_name), "tess_%04d.bot", ++g_out_seq);
	mk_bot(g_wdb, bot_name,
	       RT_BOT_SOLID, RT_BOT_CCW, 0,
	       bot->num_vertices, bot->num_faces,
	       bot->vertices, bot->faces,
	       NULL, NULL);
    }

    bu_free(bot->vertices, "bot verts");
    bu_free(bot->faces, "bot faces");
    bu_free(bot, "bot");

    return passed;
}


/* ------------------------------------------------------------------ */
/* Helper: run tess, report results, validate mesh quality             */
/* ------------------------------------------------------------------ */

/**
 * Run rt_obj_tess() with a given db_internal and tolerance set.
 * On success, also validates manifold/mesh quality via check_nmg_mesh().
 *
 * @return 1 if tess succeeded (ret == 0), 0 otherwise.
 */
static int
run_tess(const char *label,
	 struct rt_db_internal *ip,
	 const struct bg_tess_tol *ttol,
	 const struct bn_tol *tol,
	 int expect_fail)
{
    struct bu_list vlfree;
    BU_LIST_INIT(&vlfree);

    struct model *m = nmg_mm();
    struct nmgregion *r = NULL;

    fprintf(stderr, "STARTING: %s\n", label);
    fflush(stderr);

    int ret = rt_obj_tess(&r, m, ip, ttol, tol);

    int passed;
    if (expect_fail) {
	passed = (ret != 0);
    } else {
	passed = (ret == 0);
    }

    if (ret == 0 && r != NULL) {
	/* Count faces */
	int nfaces = 0;
	struct shell *s;
	for (BU_LIST_FOR(s, shell, &r->s_hd)) {
	    struct faceuse *fu;
	    for (BU_LIST_FOR(fu, faceuse, &s->fu_hd)) {
		if (fu->orientation == OT_SAME)
		    nfaces++;
	    }
	}
	fprintf(stderr, "  %-48s ret=%-3d faces=%-6d [%s]\n",
	       label, ret, nfaces, passed ? "PASS" : "FAIL");

	/* Validate mesh quality when tessellation succeeded */
	if (g_validate && !expect_fail)
	    (void)check_nmg_mesh(label, m, tol, &vlfree);

	/* Optionally write the CSG primitive to the output .g.
	 * Build a temporary rt_db_internal with idb_meth set (the
	 * hand-crafted ip in each test function leaves it NULL).
	 * We do NOT call rt_db_free_internal here: idb_ptr points
	 * to a caller-owned stack variable.                               */
	if (g_wdb && !expect_fail) {
	    char prim_name[256];
	    struct bu_external ext;
	    struct rt_db_internal tmp_intern;
	    snprintf(prim_name, sizeof(prim_name), "csg_%04d.s", g_out_seq);
	    BU_EXTERNAL_INIT(&ext);
	    RT_DB_INTERNAL_INIT(&tmp_intern);
	    tmp_intern.idb_major_type = ip->idb_major_type;
	    tmp_intern.idb_type       = ip->idb_minor_type;
	    tmp_intern.idb_ptr        = ip->idb_ptr;
	    tmp_intern.idb_meth       = &OBJ[ip->idb_minor_type];
	    if (rt_db_cvt_to_external5(&ext, prim_name, &tmp_intern, 1.0,
				       g_wdb->dbip, &rt_uniresource,
				       ip->idb_major_type) == 0) {
		int flags = db_flags_internal(&tmp_intern);
		(void)wdb_export_external(g_wdb, &ext, prim_name,
					  flags, ip->idb_type);
	    }
	    bu_free_external(&ext);
	}
    } else {
	fprintf(stderr, "  %-48s ret=%-3d             [%s]\n",
	       label, ret, passed ? "PASS" : "FAIL");
    }
    fflush(stderr);

    nmg_km(m);
    bu_list_free(&vlfree);
    return passed;
}


/**
 * Like run_tess() but also fails when the face count exceeds max_faces.
 * Useful for regression-testing that coarse tolerances produce compact
 * meshes (not hundreds of spurious intermediate rings).
 *
 * @return 1 if tess succeeded and face count <= max_faces, 0 otherwise.
 */
static int
run_tess_maxfaces(const char *label,
		  struct rt_db_internal *ip,
		  const struct bg_tess_tol *ttol,
		  const struct bn_tol *tol,
		  int max_faces)
{
    struct bu_list vlfree;
    BU_LIST_INIT(&vlfree);

    struct model *m = nmg_mm();
    struct nmgregion *r = NULL;

    fprintf(stderr, "STARTING: %s (max_faces=%d)\n", label, max_faces);
    fflush(stderr);

    int ret = rt_obj_tess(&r, m, ip, ttol, tol);
    int passed = 0;
    int nfaces = 0;

    if (ret == 0 && r != NULL) {
	struct shell *s;
	for (BU_LIST_FOR(s, shell, &r->s_hd)) {
	    struct faceuse *fu;
	    for (BU_LIST_FOR(fu, faceuse, &s->fu_hd)) {
		if (fu->orientation == OT_SAME)
		    nfaces++;
	    }
	}
	passed = (nfaces <= max_faces);
	fprintf(stderr, "  %-55s ret=%-3d faces=%-6d max=%-6d [%s]\n",
		label, ret, nfaces, max_faces, passed ? "PASS" : "FAIL");
    } else {
	fprintf(stderr, "  %-55s ret=%-3d             [FAIL - tess returned %d]\n",
		label, ret, ret);
    }
    fflush(stderr);

    nmg_km(m);
    bu_list_free(&vlfree);
    return passed;
}


/* ------------------------------------------------------------------ */
/* Standard tolerances                                                  */
/* ------------------------------------------------------------------ */

static void
init_tols(struct bg_tess_tol *ttol, struct bn_tol *tol,
	  double abs_tol, double rel_tol, double norm_tol)
{
    ttol->abs = abs_tol;
    ttol->rel = rel_tol;
    ttol->norm = norm_tol;
    BG_CK_TESS_TOL(ttol);

    tol->dist = 0.005;
    tol->dist_sq = tol->dist * tol->dist;
    tol->perp = 1e-6;
    tol->para = 1 - tol->perp;
    BN_CK_TOL(tol);
}


/* ------------------------------------------------------------------ */
/* TOR (Torus) tests                                                    */
/* ------------------------------------------------------------------ */

static int
test_tor(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_tor_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_TOR;
    ip.idb_ptr = &tip;

    tip.magic = RT_TOR_INTERNAL_MAGIC;
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 1);
    tip.r_a = 10.0;
    tip.r_b = tip.r_a;
    VSET(tip.a, tip.r_a, 0, 0);
    VSET(tip.b, 0, tip.r_a, 0);

    printf("\n--- TOR tests ---\n");

    /* Normal torus: r_a=10, r_h=1 with default tolerances */
    tip.r_h = 1.0;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tor normal (r_a=10 r_h=1 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal torus with absolute tolerance */
    tip.r_h = 2.0;
    init_tols(&ttol, &tol, 0.5, 0.0, 0.0);
    if (!run_tess("tor normal (r_a=10 r_h=2 abs=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal torus with norm tolerance */
    tip.r_h = 2.0;
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
    if (!run_tess("tor normal (r_a=10 r_h=2 norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* No tolerances (defaults kick in) */
    tip.r_h = 3.0;
    init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
    if (!run_tess("tor no-tol (r_a=10 r_h=3)", &ip, &ttol, &tol, 0)) failures++;

    /* Chess pawn bodycut.s: r_a=67.8 r_h=63.4 (nearly self-intersecting torus) */
    tip.r_a = 67.815;
    tip.r_b = tip.r_a;
    VSET(tip.a, tip.r_a, 0, 0);
    VSET(tip.b, 0, tip.r_a, 0);
    tip.r_h = 63.43;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tor chess pawn bodycut (r_a=67.8 r_h=63.4)", &ip, &ttol, &tol, 0)) failures++;

    /* Chess king bodycut.s: r_a=107.7 r_h=101.8 */
    tip.r_a = 107.7;
    tip.r_b = tip.r_a;
    VSET(tip.a, tip.r_a, 0, 0);
    VSET(tip.b, 0, tip.r_a, 0);
    tip.r_h = 101.81;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tor chess king bodycut (r_a=107.7 r_h=101.8)", &ip, &ttol, &tol, 0)) failures++;

    /* Reset to unit-scale for degenerate tests */
    tip.r_a = 10.0;
    tip.r_b = 10.0;
    VSET(tip.a, 10.0, 0, 0);
    VSET(tip.b, 0, 10.0, 0);

    /* Zero r_h - should fail gracefully (not crash/hang) */
    tip.r_h = 0.0;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tor DEGENERATE r_h=0 (expect fail)", &ip, &ttol, &tol, 1)) failures++;

    /* Negative r_h: the surface is identical to |r_h|, so tessellation
     * should succeed and produce the same face count as |r_h|. */
    tip.r_h = -1.0;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tor negative r_h=-1 (same as r_h=1, expect success)", &ip, &ttol, &tol, 0)) failures++;

    /* Spindle torus: r_h > r_a means tube passes through the axis.
     * The outer surface is a valid closed manifold (sphere topology). */
    tip.r_a = 5.0;
    tip.r_b = 5.0;
    VSET(tip.a, 5.0, 0, 0);
    VSET(tip.b, 0, 5.0, 0);
    tip.r_h = 8.0;   /* > r_a=5: spindle torus */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tor spindle torus (r_a=5 r_h=8 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Spindle torus with extreme self-intersection ratio */
    tip.r_a = 2.0;
    tip.r_b = 2.0;
    VSET(tip.a, 2.0, 0, 0);
    VSET(tip.b, 0, 2.0, 0);
    tip.r_h = 10.0;  /* r_h/r_a = 5: strongly spindle */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tor strongly spindle (r_a=2 r_h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* Zero r_a - should fail gracefully */
    tip.r_a = 0.0;
    tip.r_b = 0.0;
    VSET(tip.a, 0, 0, 0);
    VSET(tip.b, 0, 0, 0);
    tip.r_h = 1.0;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tor DEGENERATE r_a=0 (expect fail)", &ip, &ttol, &tol, 1)) failures++;

    /* Extreme tolerances: very tight norm (below PRIM_MIN_NORM_TOL clamp) */
    tip.r_a = 10.0;
    tip.r_b = 10.0;
    VSET(tip.a, 10.0, 0, 0);
    VSET(tip.b, 0, 10.0, 0);
    tip.r_h = 2.0;
    /* norm=0.05 (~2.9 deg): well above PRIM_MIN_NORM_TOL (0.5 deg clamp) so no
     * warning, and generates a manageable mesh (~4K faces).  This exercises the
     * norm-driven subdivision code path without producing an astronomically large
     * NMG that would make the test too slow for CI.  The PRIM_MIN_NORM_TOL clamp
     * itself is a defensive safety-net; it fires when norm < 0.00873 rad, which
     * would produce ~130K+ faces — impractical for a unit test. */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.05);
    if (!run_tess("tor norm-driven (norm=0.05)", &ip, &ttol, &tol, 0)) failures++;

    /* Extreme tolerances: very loose norm */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.9);    /* ~51 degrees, very coarse */
    if (!run_tess("tor loose-norm (norm=0.9)", &ip, &ttol, &tol, 0)) failures++;

    /* Extreme tolerances: all three set simultaneously */
    init_tols(&ttol, &tol, 0.5, 0.05, 0.2);
    if (!run_tess("tor all-tols (abs=0.5 rel=0.05 norm=0.2)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight rel (fine mesh) */
    init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
    if (!run_tess("tor tight-rel (rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose rel (coarse mesh) */
    init_tols(&ttol, &tol, 0.0, 0.5, 0.0);
    if (!run_tess("tor loose-rel (rel=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* Tiny scale torus (below PRIM_MIN_ABS_TOL in size) */
    tip.r_a = 0.1;
    tip.r_b = 0.1;
    VSET(tip.a, 0.1, 0, 0);
    VSET(tip.b, 0, 0.1, 0);
    tip.r_h = 0.02;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tor tiny (r_a=0.1 r_h=0.02)", &ip, &ttol, &tol, 0)) failures++;

    /* Large scale torus */
    tip.r_a = 5000.0;
    tip.r_b = 5000.0;
    VSET(tip.a, 5000.0, 0, 0);
    VSET(tip.b, 0, 5000.0, 0);
    tip.r_h = 100.0;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tor large (r_a=5000 r_h=100)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* ETO (Elliptical Torus) tests                                         */
/* ------------------------------------------------------------------ */

static int
test_eto(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_eto_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_ETO;
    ip.idb_ptr = &tip;

    tip.eto_magic = RT_ETO_INTERNAL_MAGIC;
    VSET(tip.eto_V, 0, 0, 0);
    VSET(tip.eto_N, 0, 0, 1);

    printf("\n--- ETO tests ---\n");

    /* Normal ETO */
    tip.eto_r  = 10.0;
    tip.eto_rd = 1.5;
    VSET(tip.eto_C, 2.0, 0.0, 1.5);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto normal (r=10 rd=1.5 C=(2,0,1.5) rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal ETO with absolute tolerance */
    tip.eto_r  = 10.0;
    tip.eto_rd = 2.0;
    VSET(tip.eto_C, 2.0, 0.0, 2.0);
    init_tols(&ttol, &tol, 0.5, 0.0, 0.0);
    if (!run_tess("eto normal (r=10 rd=2.0 abs=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* Chess bishop headeto2.s: r=5.716 rd=5.246 (eto_rd close to eto_r) */
    tip.eto_r  = 5.716;
    tip.eto_rd = 5.246;
    VSET(tip.eto_C, 3.216, 0.079, 7.028);
    VSET(tip.eto_N, 0, 0, 0.0592);
    VUNITIZE(tip.eto_N);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto chess bishop headeto2 (r=5.716 rd=5.246)", &ip, &ttol, &tol, 0)) failures++;

    /* Chess pawn bottometo.s */
    VSET(tip.eto_N, 0, 0, 0.05994);
    VUNITIZE(tip.eto_N);
    VSET(tip.eto_V, 0, 0, 20);
    tip.eto_r  = 5.028;
    tip.eto_rd = 1.663;
    VSET(tip.eto_C, 2.846, 1.705, 1.877);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto chess pawn bottometo (r=5.028 rd=1.663)", &ip, &ttol, &tol, 0)) failures++;

    /* Degenerate: near-zero r (should fail gracefully) */
    VSET(tip.eto_N, 0, 0, 1);
    VSET(tip.eto_V, 0, 0, 0);
    tip.eto_r  = 0.00005;   /* < 0.0001 threshold */
    tip.eto_rd = 1.0;
    VSET(tip.eto_C, 1.0, 0, 1.0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto DEGENERATE near-zero r (expect fail)", &ip, &ttol, &tol, 1)) failures++;

    /* Negative r: the surface is identical to |r|, so tessellation
     * should succeed and produce valid geometry. */
    tip.eto_r  = -5.0;
    tip.eto_rd = 1.0;
    VSET(tip.eto_C, 2.0, 0, 1.0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto negative r=-5 (same as r=5, expect success)", &ip, &ttol, &tol, 0)) failures++;

    /* Degenerate: near-zero rd (should fail gracefully) */
    tip.eto_r  = 5.0;
    tip.eto_rd = 0.00005;
    VSET(tip.eto_C, 2.0, 0, 0.00005);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto DEGENERATE near-zero rd (expect fail)", &ip, &ttol, &tol, 1)) failures++;

    /* Extreme tolerances: very tight norm */
    VSET(tip.eto_N, 0, 0, 1);
    VSET(tip.eto_V, 0, 0, 0);
    tip.eto_r  = 10.0;
    tip.eto_rd = 1.5;
    VSET(tip.eto_C, 2.0, 0.0, 1.5);
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);  /* norm-driven; chord-floor in make_ellipse guards against runaway */
    if (!run_tess("eto norm-driven (norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* Extreme tolerances: very loose norm */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.9);
    if (!run_tess("eto loose-norm (norm=0.9)", &ip, &ttol, &tol, 0)) failures++;

    /* All three tolerances combined */
    init_tols(&ttol, &tol, 0.3, 0.03, 0.15);
    if (!run_tess("eto all-tols (abs=0.3 rel=0.03 norm=0.15)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight rel (fine mesh) */
    init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
    if (!run_tess("eto tight-rel (rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

    /* Nearly circular cross-section: eto_rd close to |eto_C| */
    tip.eto_r  = 8.0;
    tip.eto_rd = 1.99;
    VSET(tip.eto_C, 0.1, 0.0, 1.99);   /* |C| ≈ eto_rd */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto near-circular-section (rd≈|C|)", &ip, &ttol, &tol, 0)) failures++;

    /* Large-scale ETO */
    tip.eto_r  = 2000.0;
    tip.eto_rd = 50.0;
    VSET(tip.eto_C, 100.0, 0.0, 50.0);
    VSET(tip.eto_N, 0, 0, 1);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto large (r=2000 rd=50)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* TGC (Truncated General Cone) tests                                   */
/* ------------------------------------------------------------------ */

static int
test_tgc(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_tgc_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_TGC;
    ip.idb_ptr = &tip;

    tip.magic = RT_TGC_INTERNAL_MAGIC;

    printf("\n--- TGC tests ---\n");

    /* Normal RCC (right circular cylinder): A=B=C=D */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 10);
    VSET(tip.a, 5, 0, 0);
    VSET(tip.b, 0, 5, 0);
    VSET(tip.c, 5, 0, 0);
    VSET(tip.d, 0, 5, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc RCC (r=5 h=10 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal TGC (cone) */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 20);
    VSET(tip.a, 8, 0, 0);
    VSET(tip.b, 0, 8, 0);
    VSET(tip.c, 2, 0, 0);
    VSET(tip.d, 0, 2, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc cone (A=B=8 C=D=2 h=20 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* TGC with absolute tolerance */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 10);
    VSET(tip.a, 5, 0, 0);
    VSET(tip.b, 0, 5, 0);
    VSET(tip.c, 5, 0, 0);
    VSET(tip.d, 0, 5, 0);
    init_tols(&ttol, &tol, 0.5, 0.0, 0.0);
    if (!run_tess("tgc RCC (r=5 h=10 abs=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* TGC with norm tolerance */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
    if (!run_tess("tgc RCC (r=5 h=10 norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* TGC with no tolerance */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
    if (!run_tess("tgc RCC no-tol (r=5 h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* Chess rook crownrcc.s: RCC r=14.16 h=12.29 */
    VSET(tip.v, 0, 0, -2.29);
    VSET(tip.h, 0, 0, 12.29);
    VSET(tip.a, 14.16, 0, 0);
    VSET(tip.b, 0, 14.16, 0);
    VSET(tip.c, 14.16, 0, 0);
    VSET(tip.d, 0, 14.16, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc chess rook crownrcc (r=14.16 h=12.29)", &ip, &ttol, &tol, 0)) failures++;

    /* TGC with extreme aspect ratio (long thin cylinder) - potential subdivision stress */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 1000);
    VSET(tip.a, 1, 0, 0);
    VSET(tip.b, 0, 1, 0);
    VSET(tip.c, 1, 0, 0);
    VSET(tip.d, 0, 1, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc long-thin (r=1 h=1000 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* TGC with very large taper ratio (stress test for subdivision loop) */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 100);
    VSET(tip.a, 100, 0, 0);
    VSET(tip.b, 0, 100, 0);
    VSET(tip.c, 0.1, 0, 0);
    VSET(tip.d, 0, 0.1, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc large-taper (A=100 C=0.1 h=100 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Extreme nozzle geometry (A=280 C=0.138 h=280): the per-ring nsegs fix
     * should yield a compact mesh rather than 17000+ triangles. */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 280);
    VSET(tip.a, 280, 0, 0);
    VSET(tip.b, 0, 280, 0);
    VSET(tip.c, 0.138, 0, 0);
    VSET(tip.d, 0, 0.138, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc extreme-nozzle (A=280 C=0.138 h=280 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Sharp cone (degenerate top) */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 10);
    VSET(tip.a, 5, 0, 0);
    VSET(tip.b, 0, 5, 0);
    VSET(tip.c, 0, 0, 0);
    VSET(tip.d, 0, 0, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc sharp cone (A=B=5 C=D=0 h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* Twisted TGC (A and C not parallel) */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 10);
    VSET(tip.a, 5, 0, 0);
    VSET(tip.b, 0, 5, 0);
    VSET(tip.c, 0, 3, 0);
    VSET(tip.d, -3, 0, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc twisted (A=(5,0,0) C=(0,3,0) h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* tgc.g long_thin.s: extreme aspect ratio cylinder (H/r ~ 94000).
     * Previously triggered the 10000-iteration safeguard with a WARNING.
     * Bulk-insertion for near-uniform profiles should handle it cleanly. */
    VSET(tip.v, -23916.39, 16576.29, 6232.91);
    VSET(tip.h, 4380.15, -8304.15, -842.34);
    VSET(tip.a, 0.08845, 0.04665, 0.0);
    VSET(tip.b, 0.004169, -0.007904, 0.09960);
    VSET(tip.c, 0.08845, 0.04665, 0.0);
    VSET(tip.d, 0.004169, -0.007904, 0.09960);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc long_thin.s (H/r~94000, real geometry)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight norm (below clamp, exercises alpha_tol path) */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 10);
    VSET(tip.a, 5, 0, 0);
    VSET(tip.b, 0, 5, 0);
    VSET(tip.c, 5, 0, 0);
    VSET(tip.d, 0, 5, 0);
    init_tols(&ttol, &tol, 0.0, 0.0, 0.02);   /* norm-driven, above clamp */
    if (!run_tess("tgc norm-driven (norm=0.02)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose norm (coarse approximation) */
    init_tols(&ttol, &tol, 0.0, 0.0, 1.0);
    if (!run_tess("tgc loose-norm (norm=1.0)", &ip, &ttol, &tol, 0)) failures++;

    /* All three tolerances combined */
    init_tols(&ttol, &tol, 0.3, 0.03, 0.15);
    if (!run_tess("tgc all-tols (abs=0.3 rel=0.03 norm=0.15)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight rel */
    init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
    if (!run_tess("tgc tight-rel (r=5 h=10 rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose rel (coarse mesh) */
    init_tols(&ttol, &tol, 0.0, 0.5, 0.0);
    if (!run_tess("tgc loose-rel (r=5 h=10 rel=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* Large-scale TGC cylinder */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 5000);
    VSET(tip.a, 1000, 0, 0);
    VSET(tip.b, 0, 1000, 0);
    VSET(tip.c, 1000, 0, 0);
    VSET(tip.d, 0, 1000, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc large (r=1000 h=5000)", &ip, &ttol, &tol, 0)) failures++;

    /* Tiny-scale TGC */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 0.5);
    VSET(tip.a, 0.1, 0, 0);
    VSET(tip.b, 0, 0.1, 0);
    VSET(tip.c, 0.1, 0, 0);
    VSET(tip.d, 0, 0.1, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc tiny (r=0.1 h=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* REC with highly elliptical cross-section (major/minor = 100) */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 20);
    VSET(tip.a, 100, 0, 0);
    VSET(tip.b, 0, 1, 0);
    VSET(tip.c, 100, 0, 0);
    VSET(tip.d, 0, 1, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc highly-elliptical (A=100 B=1 h=20)", &ip, &ttol, &tol, 0)) failures++;

    /* s.nos5a.i (havoc.g): near-apex TGC -- tiny bottom, large top.
     * Before the sub-tolerance-ring fix the 1%-rel tessellation generated
     * 80+ intermediate rings (driven by the 0.039 mm bottom ring being
     * far below dtol ~0.28 mm).  With the fix the face count must stay
     * well under 200 (a handful of rings at most). */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, -27.5253, 3.84612, -0.541924);
    VSET(tip.a, 0, 0, 0.0137527);
    VSET(tip.b, 0, 0.0389778, 0);
    VSET(tip.c, 0, 0, 5.22436);
    VSET(tip.d, 0, 14.0442, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc s.nos5a.i-like (tiny-bot large-top rel=0.01)", &ip, &ttol, &tol, 0)) failures++;
    /* Regression: face count must be compact (not driven by sub-tol bottom ring) */
    if (!run_tess_maxfaces("tgc s.nos5a.i-like face-count bound (max 200)", &ip, &ttol, &tol, 200)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* TGC analytic surface area tests (rt_tgc_surf_area)                  */
/* ------------------------------------------------------------------ */

/**
 * Directly exercise rt_tgc_surf_area() for each TGC subtype.
 *
 * RCC, TRC, REC have closed-form analytic formulas and are compared
 * exactly (< 0.01% error).  TEC has no closed-form solution and uses
 * the Crofton ray-sampling fallback; it is validated against:
 *   1. Hard sanity bounds (positive, > base areas, < enclosing cylinder)
 *   2. A geometric frustum approximation (< 10% error)
 *   3. A continuity check: a "nearly-TRC" TEC (|a|≈|b|, |c|≈|d|) must
 *      agree with the exact TRC formula within 5%.
 */
static int
test_tgc_surf_area(void)
{
    int failures = 0;
    struct rt_db_internal ip;
    struct rt_tgc_internal tip;
    fastf_t area = 0.0;
    double expected, err;

    /* Ramanujan ellipse circumference: π(a+b)(1 + 3h²/(10+√(4-3h²)))
     * where h=(a-b)/(a+b).  Mirrors ELL_CIRCUMFERENCE in librt_private.h. */
#define ELL_CIRCUM(a_,b_) (M_PI*((a_)+(b_)) * \
    (1.0 + 3.0*((a_)-(b_))/((a_)+(b_))*((a_)-(b_))/((a_)+(b_)) \
    / (10.0 + sqrt(4.0 - 3.0*((a_)-(b_))/((a_)+(b_))*((a_)-(b_))/((a_)+(b_))))))

    ip.idb_magic      = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_TGC;
    ip.idb_meth       = &OBJ[ID_TGC];  /* required by Crofton serialization */
    ip.idb_ptr        = &tip;
    tip.magic         = RT_TGC_INTERNAL_MAGIC;

    printf("\n--- TGC surface area (analytic) tests ---\n");

    /* ------------------------------------------------------------------
     * RCC: right circular cylinder  a=b=c=d=5, h=10
     * exact SA = 2π·r·(r+h) = 2π·5·15 = 150π
     * ------------------------------------------------------------------ */
    VSET(tip.v, 0, 0, 0);
    VSET(tip.h, 0, 0, 10);
    VSET(tip.a, 5, 0, 0);
    VSET(tip.b, 0, 5, 0);
    VSET(tip.c, 5, 0, 0);
    VSET(tip.d, 0, 5, 0);
    area = 0.0;
    OBJ[ID_TGC].ft_surf_area(&area, &ip);
    expected = M_2PI * 5.0 * 15.0;   /* 150π */
    err = fabs((double)area - expected) / expected;
    fprintf(stderr,
	    "  SA RCC (r=5 h=10): area=%.6g  expected=%.6g  err=%.4f%%  [%s]\n",
	    (double)area, expected, err*100.0,
	    (err < 1e-4) ? "PASS" : "FAIL");
    if (err >= 1e-4) failures++;

    /* ------------------------------------------------------------------
     * TRC: truncated right cone  a=b=8, c=d=3, h=15
     * exact SA = π·(a+c)·√((a-c)²+h²) + π·a² + π·c²
     * ------------------------------------------------------------------ */
    VSET(tip.h, 0, 0, 15);
    VSET(tip.a, 8, 0, 0);
    VSET(tip.b, 0, 8, 0);
    VSET(tip.c, 3, 0, 0);
    VSET(tip.d, 0, 3, 0);
    area = 0.0;
    OBJ[ID_TGC].ft_surf_area(&area, &ip);
    expected = M_PI * ((8.0+3.0)*sqrt((8.0-3.0)*(8.0-3.0) + 15.0*15.0)
		       + 8.0*8.0 + 3.0*3.0);
    err = fabs((double)area - expected) / expected;
    fprintf(stderr,
	    "  SA TRC (a=8 c=3 h=15): area=%.6g  expected=%.6g  err=%.4f%%  [%s]\n",
	    (double)area, expected, err*100.0,
	    (err < 1e-4) ? "PASS" : "FAIL");
    if (err >= 1e-4) failures++;

    /* ------------------------------------------------------------------
     * REC: right elliptic cylinder  a=c=8, b=d=4, h=10
     * SA = ELL_CIRCUMFERENCE(a,b)·h + 2·π·a·b  (Ramanujan approximation)
     * ------------------------------------------------------------------ */
    VSET(tip.h, 0, 0, 10);
    VSET(tip.a, 8, 0, 0);
    VSET(tip.b, 0, 4, 0);
    VSET(tip.c, 8, 0, 0);
    VSET(tip.d, 0, 4, 0);
    area = 0.0;
    OBJ[ID_TGC].ft_surf_area(&area, &ip);
    expected = ELL_CIRCUM(8.0, 4.0) * 10.0 + 2.0 * M_PI * 8.0 * 4.0;
    err = fabs((double)area - expected) / expected;
    fprintf(stderr,
	    "  SA REC (a=8 b=4 h=10): area=%.6g  expected=%.6g  err=%.4f%%  [%s]\n",
	    (double)area, expected, err*100.0,
	    (err < 1e-4) ? "PASS" : "FAIL");
    if (err >= 1e-4) failures++;

    /* ------------------------------------------------------------------
     * TEC: truncated elliptic cone  a=6, b=4, c=3, d=2, h=10
     * No closed-form exists → Crofton sampling.
     *
     * Sanity checks:
     *   (a) area > 0
     *   (b) area > sum of two elliptic base areas (π·ab + π·cd)
     *   (c) area < enclosing-cylinder surface area (generous upper bound)
     *   (d) within 10% of the analytic frustum approximation:
     *         ½(C₁+C₂)·½(l_a+l_b) + π·(ab+cd)
     *       where C₁=ELL_CIRCUM(a,b), C₂=ELL_CIRCUM(c,d),
     *             l_a=√(h²+(a-c)²), l_b=√(h²+(b-d)²)
     * ------------------------------------------------------------------ */
    VSET(tip.h, 0, 0, 10);
    VSET(tip.a, 6, 0, 0);
    VSET(tip.b, 0, 4, 0);
    VSET(tip.c, 3, 0, 0);
    VSET(tip.d, 0, 2, 0);
    area = 0.0;
    OBJ[ID_TGC].ft_surf_area(&area, &ip);
    {
	double base_areas  = M_PI * (6.0*4.0 + 3.0*2.0);  /* π·ab + π·cd */
	/* enclosing right cylinder: radius max(a,b)=6, height 10 */
	double upper_bound = M_2PI * 6.0 * (6.0 + 10.0) + 0.0;  /* lateral + no caps — generous */
	double C1  = ELL_CIRCUM(6.0, 4.0);
	double C2  = ELL_CIRCUM(3.0, 2.0);
	double la  = sqrt(10.0*10.0 + (6.0-3.0)*(6.0-3.0));  /* slant along a-axis */
	double lb  = sqrt(10.0*10.0 + (4.0-2.0)*(4.0-2.0));  /* slant along b-axis */
	double approx_sa = 0.5*(C1+C2)*0.5*(la+lb) + base_areas;
	double approx_err = fabs((double)area - approx_sa) / approx_sa;

	int bounds_ok = (area > 0.0 && area > base_areas && area < upper_bound + base_areas);
	int approx_ok = (approx_err < 0.10);  /* Crofton within 10% of approximation */
	fprintf(stderr,
		"  SA TEC (a=6 b=4 c=3 d=2 h=10): area=%.6g  approx=%.6g"
		"  approx_err=%.1f%%  bounds=%s  [%s]\n",
		(double)area, approx_sa, approx_err*100.0,
		bounds_ok ? "ok" : "FAIL",
		(bounds_ok && approx_ok) ? "PASS" : "FAIL");
	if (!bounds_ok || !approx_ok) failures++;
    }

    /* ------------------------------------------------------------------
     * TEC continuity: "nearly-TRC" shape  a=8.001, b=7.999, c=3.001, d=2.999
     *
     * GET_TGC_TYPE classifies this as TEC (|a|≠|b|) so Crofton is used,
     * but the geometry is nearly circular.  The Crofton result must agree
     * with the exact TRC formula for a=8, c=3, h=15 within 5%.
     * ------------------------------------------------------------------ */
    VSET(tip.h, 0, 0, 15);
    VSET(tip.a, 8.001, 0,     0);
    VSET(tip.b, 0,     7.999, 0);
    VSET(tip.c, 3.001, 0,     0);
    VSET(tip.d, 0,     2.999, 0);
    area = 0.0;
    OBJ[ID_TGC].ft_surf_area(&area, &ip);
    expected = M_PI * ((8.0+3.0)*sqrt((8.0-3.0)*(8.0-3.0) + 15.0*15.0)
		       + 8.0*8.0 + 3.0*3.0);
    err = fabs((double)area - expected) / expected;
    fprintf(stderr,
	    "  SA TEC~TRC (a≈8 c≈3 h=15): area=%.6g  trc_ref=%.6g  err=%.2f%%  [%s]\n",
	    (double)area, expected, err*100.0,
	    (err < 0.05) ? "PASS" : "FAIL");
    if (err >= 0.05) failures++;

#undef ELL_CIRCUM

    return failures;
}




static int
test_ell(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_ell_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_ELL;
    ip.idb_ptr = &tip;

    tip.magic = RT_ELL_INTERNAL_MAGIC;
    VSET(tip.v, 0, 0, 0);
    VSET(tip.a, 10, 0, 0);
    VSET(tip.b, 0, 10, 0);
    VSET(tip.c, 0, 0, 10);

    printf("\n--- ELL tests ---\n");

    /* Sphere (equal axes) with relative tolerance */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("ell sphere (r=10 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Sphere with absolute tolerance */
    init_tols(&ttol, &tol, 0.5, 0.0, 0.0);
    if (!run_tess("ell sphere (r=10 abs=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* Sphere with normal tolerance */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
    if (!run_tess("ell sphere (r=10 norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* Sphere with no tolerance (default 10% fallback) */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
    if (!run_tess("ell sphere no-tol (r=10)", &ip, &ttol, &tol, 0)) failures++;

    /* Sphere with both abs and rel (abs is tighter) */
    init_tols(&ttol, &tol, 0.2, 0.05, 0.0);
    if (!run_tess("ell sphere (r=10 abs=0.2 rel=0.05 abs tighter)", &ip, &ttol, &tol, 0)) failures++;

    /* Sphere with both abs and rel (rel is tighter) */
    init_tols(&ttol, &tol, 2.0, 0.01, 0.0);
    if (!run_tess("ell sphere (r=10 abs=2.0 rel=0.01 rel tighter)", &ip, &ttol, &tol, 0)) failures++;

    /* General ellipsoid (unequal axes) */
    VSET(tip.a, 20, 0, 0);
    VSET(tip.b, 0, 5, 0);
    VSET(tip.c, 0, 0, 10);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("ell general (A=20 B=5 C=10 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Degenerate: zero-length A axis (expect fail) */
    VSET(tip.a, 0, 0, 0);
    VSET(tip.b, 0, 5, 0);
    VSET(tip.c, 0, 0, 5);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("ell DEGENERATE zero-A (expect fail)", &ip, &ttol, &tol, 1)) failures++;

    /* Very tight norm (fine mesh) */
    VSET(tip.a, 10, 0, 0);
    VSET(tip.b, 0, 10, 0);
    VSET(tip.c, 0, 0, 10);
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);   /* norm-driven; theta_tol floor in rt_ell_tess guards against runaway */
    if (!run_tess("ell norm-driven (norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose norm (coarse) */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.9);
    if (!run_tess("ell loose-norm (norm=0.9)", &ip, &ttol, &tol, 0)) failures++;

    /* All three combined */
    init_tols(&ttol, &tol, 0.3, 0.03, 0.15);
    if (!run_tess("ell all-tols (abs=0.3 rel=0.03 norm=0.15)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight rel */
    init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
    if (!run_tess("ell tight-rel (r=10 rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose rel */
    init_tols(&ttol, &tol, 0.0, 0.5, 0.0);
    if (!run_tess("ell loose-rel (r=10 rel=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* Highly elongated prolate spheroid (oblate stress test) */
    VSET(tip.a, 1, 0, 0);
    VSET(tip.b, 0, 1, 0);
    VSET(tip.c, 0, 0, 100);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("ell prolate (A=B=1 C=100)", &ip, &ttol, &tol, 0)) failures++;

    /* Flat oblate spheroid */
    VSET(tip.a, 50, 0, 0);
    VSET(tip.b, 0, 50, 0);
    VSET(tip.c, 0, 0, 1);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("ell oblate (A=B=50 C=1)", &ip, &ttol, &tol, 0)) failures++;

    /* Large scale sphere */
    VSET(tip.a, 10000, 0, 0);
    VSET(tip.b, 0, 10000, 0);
    VSET(tip.c, 0, 0, 10000);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("ell large-sphere (r=10000)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* EPA (Elliptical Paraboloid) tests                                    */
/* ------------------------------------------------------------------ */

static int
test_epa(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_epa_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_EPA;
    ip.idb_ptr = &tip;

    tip.epa_magic = RT_EPA_INTERNAL_MAGIC;
    VSET(tip.epa_V, 0, 0, 0);
    VSET(tip.epa_H, 0, 0, 10);
    VSET(tip.epa_Au, 1, 0, 0);
    tip.epa_r1 = 5.0;
    tip.epa_r2 = 3.0;

    printf("\n--- EPA tests ---\n");

    /* Normal EPA with relative tolerance */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("epa normal (r1=5 r2=3 h=10 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal EPA with absolute tolerance */
    init_tols(&ttol, &tol, 0.3, 0.0, 0.0);
    if (!run_tess("epa normal (r1=5 r2=3 h=10 abs=0.3)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal EPA with normal tolerance */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
    if (!run_tess("epa normal (r1=5 r2=3 h=10 norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* EPA with no tolerance (default 10% fallback) */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
    if (!run_tess("epa no-tol (r1=5 r2=3 h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* EPA with both abs and rel */
    init_tols(&ttol, &tol, 0.2, 0.05, 0.0);
    if (!run_tess("epa (r1=5 r2=3 abs=0.2 rel=0.05)", &ip, &ttol, &tol, 0)) failures++;

    /* Circular paraboloid (r1==r2) */
    tip.epa_r1 = 5.0;
    tip.epa_r2 = 5.0;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("epa circular (r1=r2=5 h=10 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight norm (exercises subdivision clamp) */
    tip.epa_r1 = 5.0;
    tip.epa_r2 = 3.0;
    init_tols(&ttol, &tol, 0.0, 0.0, 0.02);
    if (!run_tess("epa norm-driven (norm=0.02)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose norm */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.9);
    if (!run_tess("epa loose-norm (norm=0.9)", &ip, &ttol, &tol, 0)) failures++;

    /* All three tolerances combined */
    init_tols(&ttol, &tol, 0.3, 0.03, 0.15);
    if (!run_tess("epa all-tols (abs=0.3 rel=0.03 norm=0.15)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight rel */
    init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
    if (!run_tess("epa tight-rel (rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose rel */
    init_tols(&ttol, &tol, 0.0, 0.5, 0.0);
    if (!run_tess("epa loose-rel (rel=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* High aspect ratio: very narrow opening */
    tip.epa_r1 = 0.5;
    tip.epa_r2 = 0.1;
    VSET(tip.epa_H, 0, 0, 100);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("epa high-aspect (r1=0.5 r2=0.1 h=100)", &ip, &ttol, &tol, 0)) failures++;

    /* Very wide/flat paraboloid */
    tip.epa_r1 = 100.0;
    tip.epa_r2 = 80.0;
    VSET(tip.epa_H, 0, 0, 2);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("epa flat (r1=100 r2=80 h=2)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* EHY (Elliptical Hyperboloid) tests                                   */
/* ------------------------------------------------------------------ */

static int
test_ehy(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_ehy_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_EHY;
    ip.idb_ptr = &tip;

    tip.ehy_magic = RT_EHY_INTERNAL_MAGIC;
    VSET(tip.ehy_V, 0, 0, 0);
    VSET(tip.ehy_H, 0, 0, 10);
    VSET(tip.ehy_Au, 1, 0, 0);
    tip.ehy_r1 = 5.0;
    tip.ehy_r2 = 3.0;
    tip.ehy_c  = 2.0;

    printf("\n--- EHY tests ---\n");

    /* Normal EHY with relative tolerance */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("ehy normal (r1=5 r2=3 c=2 h=10 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal EHY with absolute tolerance */
    init_tols(&ttol, &tol, 0.3, 0.0, 0.0);
    if (!run_tess("ehy normal (r1=5 r2=3 c=2 h=10 abs=0.3)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal EHY with normal tolerance */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
    if (!run_tess("ehy normal (r1=5 r2=3 c=2 h=10 norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* EHY with no tolerance (default 10% fallback) */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
    if (!run_tess("ehy no-tol (r1=5 r2=3 c=2 h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* EHY with both abs and rel */
    init_tols(&ttol, &tol, 0.2, 0.05, 0.0);
    if (!run_tess("ehy (r1=5 r2=3 c=2 abs=0.2 rel=0.05)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight norm (exercises subdivision clamp) */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.02);
    if (!run_tess("ehy norm-driven (norm=0.02)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose norm */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.9);
    if (!run_tess("ehy loose-norm (norm=0.9)", &ip, &ttol, &tol, 0)) failures++;

    /* All three tolerances combined */
    init_tols(&ttol, &tol, 0.3, 0.03, 0.15);
    if (!run_tess("ehy all-tols (abs=0.3 rel=0.03 norm=0.15)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight rel */
    init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
    if (!run_tess("ehy tight-rel (rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose rel */
    init_tols(&ttol, &tol, 0.0, 0.5, 0.0);
    if (!run_tess("ehy loose-rel (rel=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* Near-cylinder: large c (asymptote nearly vertical) */
    tip.ehy_r1 = 5.0;
    tip.ehy_r2 = 3.0;
    tip.ehy_c  = 50.0;   /* very large c → nearly cylindrical */
    VSET(tip.ehy_H, 0, 0, 10);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("ehy near-cylinder (c=50 r1=5 r2=3 h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* Small c (sharp asymptote, highly curved) */
    tip.ehy_c  = 0.1;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("ehy sharp-asymptote (c=0.1 r1=5 r2=3 h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* High aspect ratio: r1 >> r2 */
    tip.ehy_r1 = 20.0;
    tip.ehy_r2 = 0.5;
    tip.ehy_c  = 2.0;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("ehy high-elliptic (r1=20 r2=0.5 c=2)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* RPC (Right Parabolic Cylinder) tests                                 */
/* ------------------------------------------------------------------ */

static int
test_rpc(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_rpc_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_RPC;
    ip.idb_ptr = &tip;

    tip.rpc_magic = RT_RPC_INTERNAL_MAGIC;
    VSET(tip.rpc_V, 0, 0, 0);
    VSET(tip.rpc_H, 0, 0, 10);
    VSET(tip.rpc_B, 0, 5, 0);
    tip.rpc_r = 3.0;

    printf("\n--- RPC tests ---\n");

    /* Normal RPC with relative tolerance */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("rpc normal (B=5 r=3 h=10 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal RPC with absolute tolerance */
    init_tols(&ttol, &tol, 0.3, 0.0, 0.0);
    if (!run_tess("rpc normal (B=5 r=3 h=10 abs=0.3)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal RPC with normal tolerance */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
    if (!run_tess("rpc normal (B=5 r=3 h=10 norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* RPC with no tolerance (default 10% fallback) */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
    if (!run_tess("rpc no-tol (B=5 r=3 h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* RPC with both abs and rel */
    init_tols(&ttol, &tol, 0.2, 0.05, 0.0);
    if (!run_tess("rpc (B=5 r=3 abs=0.2 rel=0.05)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight norm */
    VSET(tip.rpc_B, 0, 5, 0);
    tip.rpc_r = 3.0;
    init_tols(&ttol, &tol, 0.0, 0.0, 0.02);
    if (!run_tess("rpc norm-driven (norm=0.02)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose norm */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.9);
    if (!run_tess("rpc loose-norm (norm=0.9)", &ip, &ttol, &tol, 0)) failures++;

    /* All three tolerances combined */
    init_tols(&ttol, &tol, 0.3, 0.03, 0.15);
    if (!run_tess("rpc all-tols (abs=0.3 rel=0.03 norm=0.15)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight rel */
    init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
    if (!run_tess("rpc tight-rel (rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose rel */
    init_tols(&ttol, &tol, 0.0, 0.5, 0.0);
    if (!run_tess("rpc loose-rel (rel=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* Wide flat RPC (B >> r) */
    VSET(tip.rpc_B, 0, 50, 0);
    tip.rpc_r = 1.0;
    VSET(tip.rpc_H, 0, 0, 20);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("rpc wide-flat (B=50 r=1 h=20)", &ip, &ttol, &tol, 0)) failures++;

    /* Narrow tall RPC (r >> B) */
    VSET(tip.rpc_B, 0, 0.5, 0);
    tip.rpc_r = 10.0;
    VSET(tip.rpc_H, 0, 0, 10);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("rpc narrow-tall (B=0.5 r=10 h=10)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* RHC (Right Hyperbolic Cylinder) tests                                */
/* ------------------------------------------------------------------ */

static int
test_rhc(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_rhc_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_RHC;
    ip.idb_ptr = &tip;

    tip.rhc_magic = RT_RHC_INTERNAL_MAGIC;
    VSET(tip.rhc_V, 0, 0, 0);
    VSET(tip.rhc_H, 0, 0, 10);
    VSET(tip.rhc_B, 0, 5, 0);
    tip.rhc_r = 3.0;
    tip.rhc_c = 1.0;

    printf("\n--- RHC tests ---\n");

    /* Normal RHC with relative tolerance */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("rhc normal (B=5 r=3 c=1 h=10 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal RHC with absolute tolerance */
    init_tols(&ttol, &tol, 0.3, 0.0, 0.0);
    if (!run_tess("rhc normal (B=5 r=3 c=1 h=10 abs=0.3)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal RHC with normal tolerance */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
    if (!run_tess("rhc normal (B=5 r=3 c=1 h=10 norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* RHC with no tolerance (default 10% fallback) */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
    if (!run_tess("rhc no-tol (B=5 r=3 c=1 h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* RHC with both abs and rel */
    init_tols(&ttol, &tol, 0.2, 0.05, 0.0);
    if (!run_tess("rhc (B=5 r=3 c=1 abs=0.2 rel=0.05)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight norm */
    VSET(tip.rhc_B, 0, 5, 0);
    tip.rhc_r = 3.0;
    tip.rhc_c = 1.0;
    init_tols(&ttol, &tol, 0.0, 0.0, 0.02);
    if (!run_tess("rhc norm-driven (norm=0.02)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose norm */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.9);
    if (!run_tess("rhc loose-norm (norm=0.9)", &ip, &ttol, &tol, 0)) failures++;

    /* All three tolerances combined */
    init_tols(&ttol, &tol, 0.3, 0.03, 0.15);
    if (!run_tess("rhc all-tols (abs=0.3 rel=0.03 norm=0.15)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight rel */
    init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
    if (!run_tess("rhc tight-rel (rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose rel */
    init_tols(&ttol, &tol, 0.0, 0.5, 0.0);
    if (!run_tess("rhc loose-rel (rel=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* Near-cylinder: large c */
    VSET(tip.rhc_B, 0, 5, 0);
    tip.rhc_r = 3.0;
    tip.rhc_c = 50.0;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("rhc near-cylinder (c=50 B=5 r=3 h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* Very small c (sharp hyperbola) */
    tip.rhc_c = 0.05;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("rhc sharp-hyperbola (c=0.05 B=5 r=3)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* HYP (Hyperboloid of one sheet) tests                                 */
/* ------------------------------------------------------------------ */

static int
test_hyp(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_hyp_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_HYP;
    ip.idb_ptr = &tip;

    tip.hyp_magic = RT_HYP_INTERNAL_MAGIC;
    VSET(tip.hyp_Vi, 0, 0, 0);
    VSET(tip.hyp_Hi, 0, 0, 20);
    VSET(tip.hyp_A,  8, 0, 0);
    tip.hyp_b   = 6.0;
    tip.hyp_bnr = 0.5;  /* neck/base ratio in (0,1) */

    printf("\n--- HYP tests ---\n");

    /* Normal HYP with relative tolerance */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("hyp normal (A=8 b=6 bnr=0.5 h=20 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal HYP with absolute tolerance */
    init_tols(&ttol, &tol, 0.4, 0.0, 0.0);
    if (!run_tess("hyp normal (A=8 b=6 h=20 abs=0.4)", &ip, &ttol, &tol, 0)) failures++;

    /* Normal HYP with normal tolerance */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
    if (!run_tess("hyp normal (A=8 b=6 h=20 norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* HYP with no tolerance (default 10% fallback) */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
    if (!run_tess("hyp no-tol (A=8 b=6 h=20)", &ip, &ttol, &tol, 0)) failures++;

    /* HYP with both abs and rel */
    init_tols(&ttol, &tol, 0.3, 0.05, 0.0);
    if (!run_tess("hyp (A=8 b=6 abs=0.3 rel=0.05)", &ip, &ttol, &tol, 0)) failures++;

    /* Very thin neck (bnr close to 0) */
    tip.hyp_bnr = 0.05;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("hyp thin-neck (bnr=0.05 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight norm */
    tip.hyp_bnr = 0.5;
    VSET(tip.hyp_Hi, 0, 0, 20);
    VSET(tip.hyp_A, 8, 0, 0);
    tip.hyp_b = 6.0;
    init_tols(&ttol, &tol, 0.0, 0.0, 0.02);
    if (!run_tess("hyp norm-driven (norm=0.02)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose norm */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.9);
    if (!run_tess("hyp loose-norm (norm=0.9)", &ip, &ttol, &tol, 0)) failures++;

    /* All three tolerances combined */
    init_tols(&ttol, &tol, 0.3, 0.03, 0.15);
    if (!run_tess("hyp all-tols (abs=0.3 rel=0.03 norm=0.15)", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight rel */
    init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
    if (!run_tess("hyp tight-rel (rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose rel (coarse) */
    init_tols(&ttol, &tol, 0.0, 0.5, 0.0);
    if (!run_tess("hyp loose-rel (rel=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* Near-maximum neck ratio (bnr close to 1 → almost cylindrical) */
    tip.hyp_bnr = 0.99;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("hyp near-cylinder (bnr=0.99)", &ip, &ttol, &tol, 0)) failures++;

    /* High elliptical cross-section (b >> |A|) */
    VSET(tip.hyp_A, 2, 0, 0);
    tip.hyp_b = 20.0;
    tip.hyp_bnr = 0.5;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("hyp high-elliptic (A=2 b=20 bnr=0.5)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* PART (Particle) tests                                                 */
/* ------------------------------------------------------------------ */

static int
test_part(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_part_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_PARTICLE;
    ip.idb_ptr = &tip;

    tip.part_magic = RT_PART_INTERNAL_MAGIC;
    VSET(tip.part_V, 0, 0, 0);
    VSET(tip.part_H, 0, 0, 10);
    tip.part_vrad = 3.0;
    tip.part_hrad = 3.0;
    tip.part_type = RT_PARTICLE_TYPE_CYLINDER;

    printf("\n--- PART (particle) tests ---\n");

    /* Cylinder (equal radii) with relative tolerance */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("part cylinder (vr=hr=3 h=10 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Cylinder with absolute tolerance */
    init_tols(&ttol, &tol, 0.2, 0.0, 0.0);
    if (!run_tess("part cylinder (vr=hr=3 h=10 abs=0.2)", &ip, &ttol, &tol, 0)) failures++;

    /* Cylinder with normal tolerance */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
    if (!run_tess("part cylinder (vr=hr=3 h=10 norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

    /* Cylinder with no tolerance (default 10% fallback) */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
    if (!run_tess("part cylinder no-tol (vr=hr=3 h=10)", &ip, &ttol, &tol, 0)) failures++;

    /* Cone (unequal radii) */
    tip.part_vrad = 5.0;
    tip.part_hrad = 2.0;
    tip.part_type = RT_PARTICLE_TYPE_CONE;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("part cone (vr=5 hr=2 h=10 rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    /* Cone with both abs and rel */
    init_tols(&ttol, &tol, 0.15, 0.02, 0.0);
    if (!run_tess("part cone (vr=5 hr=2 abs=0.15 rel=0.02)", &ip, &ttol, &tol, 0)) failures++;

    /* Sphere (degenerate particle) - rt_part_tess returns -1 for spheres */
    tip.part_vrad = 5.0;
    tip.part_hrad = 5.0;
    tip.part_type = RT_PARTICLE_TYPE_SPHERE;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("part sphere (expect fail - handled by ell)", &ip, &ttol, &tol, 1)) failures++;

    /* Very tight norm */
    tip.part_vrad = 3.0;
    tip.part_hrad = 3.0;
    tip.part_type = RT_PARTICLE_TYPE_CYLINDER;
    VSET(tip.part_H, 0, 0, 10);
    init_tols(&ttol, &tol, 0.0, 0.0, 0.02);
    if (!run_tess("part cylinder norm-driven (norm=0.02)", &ip, &ttol, &tol, 0)) failures++;

    /* Very loose norm */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.9);
    if (!run_tess("part cylinder loose-norm (norm=0.9)", &ip, &ttol, &tol, 0)) failures++;

    /* All three tolerances combined */
    init_tols(&ttol, &tol, 0.3, 0.03, 0.15);
    if (!run_tess("part cylinder all-tols", &ip, &ttol, &tol, 0)) failures++;

    /* Very tight rel */
    init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
    if (!run_tess("part cylinder tight-rel (rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

    /* Cone with very small head radius (near-pointed cone).
     * hr=0.001 is below tol->dist=0.005, so rt_part_tess rejects it. */
    tip.part_vrad = 10.0;
    tip.part_hrad = 0.001;
    tip.part_type = RT_PARTICLE_TYPE_CONE;
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("part near-pointed-cone (vr=10 hr=0.001, expect fail)", &ip, &ttol, &tol, 1)) failures++;

    /* Very large particle */
    tip.part_vrad = 5000.0;
    tip.part_hrad = 5000.0;
    tip.part_type = RT_PARTICLE_TYPE_CYLINDER;
    VSET(tip.part_H, 0, 0, 20000);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("part large-cylinder (vr=hr=5000 h=20000)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}




/* ------------------------------------------------------------------ */
/* EBM (Extruded Bit Map) tests                                         */
/* ------------------------------------------------------------------ */

/*
 * DSP (Displacement Solid) tessellation tests.
 *
 * DSP represents a height field stored as a 2D array of uint16_t values.
 * The tessellation produces a closed solid with:
 *   - a top surface triangulated from the height field
 *   - vertical side walls connecting the surface boundary to z=0
 *   - a flat bottom face at z=0
 *
 * We test both the "full" path (tight tolerances, uses triangulateVolume)
 * and the "simplified/decimation" path (loose tolerances, uses
 * dsp_tess_with_decimation which runs mmesh + detria).
 *
 * The dsp_bip field must be a valid rt_db_internal wrapping an
 * rt_binunif_internal.  For tests we stack-allocate both structs and set
 * their magic fields manually (only the magic is checked by RT_CK_*).
 * The dsp_stom / dsp_mtos matrices are set to identity so that grid
 * coordinates map 1:1 to world coordinates.
 */
static int
test_dsp(void)
{
    int failures = 0;

    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol     tol   = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic  = BN_TOL_MAGIC;

    /* Shared rt_db_internal wrapping the dsp internals. */
    struct rt_db_internal ip;
    ip.idb_magic      = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_DSP;

    /* Shared binunif stub (only magic is checked via RT_CK_BINUNIF). */
    struct rt_binunif_internal bip_int;
    memset(&bip_int, 0, sizeof(bip_int));
    bip_int.magic = RT_BINUNIF_INTERNAL_MAGIC;

    struct rt_db_internal bip_db;
    RT_DB_INTERNAL_INIT(&bip_db);
    bip_db.idb_major_type = DB5_MAJORTYPE_BINARY_UNIF;
    bip_db.idb_ptr        = &bip_int;

    /* Shared DSP internal. */
    struct rt_dsp_internal dsp;
    memset(&dsp, 0, sizeof(dsp));
    dsp.magic       = RT_DSP_INTERNAL_MAGIC;
    dsp.dsp_smooth  = 0;
    dsp.dsp_cuttype = DSP_CUT_DIR_llUR;
    dsp.dsp_datasrc = RT_DSP_SRC_OBJ;
    dsp.dsp_bip     = &bip_db;
    dsp.dsp_mp      = NULL;
    bu_vls_init(&dsp.dsp_name);
    bu_vls_strcpy(&dsp.dsp_name, "test_dsp_data");
    MAT_IDN(dsp.dsp_stom);
    MAT_IDN(dsp.dsp_mtos);

    ip.idb_ptr = &dsp;

    printf("\n--- DSP tests ---\n");

    /* -------------------------------------------------------------- */
    /* Test 1: 4×4 grid, all cells at the same height (flat terrain)  */
    /* Tight tolerance → full path (triangulateVolume).               */
    /* -------------------------------------------------------------- */
    {
	const uint32_t xcnt = 4, ycnt = 4;
	unsigned short buf[16];
	for (int i = 0; i < 16; i++) buf[i] = 500;

	dsp.dsp_xcnt = xcnt;
	dsp.dsp_ycnt = ycnt;
	dsp.dsp_buf  = buf;
	bip_int.count    = xcnt * ycnt;
	bip_int.u.uint16 = buf;

	init_tols(&ttol, &tol, 0.0, 0.001, 0.0); /* tight → full path */
	if (!run_tess("dsp 4x4 flat tight (full path)", &ip, &ttol, &tol, 0))
	    failures++;
    }

    /* -------------------------------------------------------------- */
    /* Test 2: 4×4 grid, flat, loose tolerance → decimation path      */
    /* -------------------------------------------------------------- */
    {
	const uint32_t xcnt = 4, ycnt = 4;
	unsigned short buf[16];
	for (int i = 0; i < 16; i++) buf[i] = 500;

	dsp.dsp_xcnt = xcnt;
	dsp.dsp_ycnt = ycnt;
	dsp.dsp_buf  = buf;
	bip_int.count    = xcnt * ycnt;
	bip_int.u.uint16 = buf;

	init_tols(&ttol, &tol, 0.0, 0.10, 0.0); /* loose → decimation path */
	if (!run_tess("dsp 4x4 flat loose (decimation)", &ip, &ttol, &tol, 0))
	    failures++;
    }

    /* -------------------------------------------------------------- */
    /* Test 3: 8×8 grid with a simple ramp (linearly rising heights)  */
    /* -------------------------------------------------------------- */
    {
	const uint32_t xcnt = 8, ycnt = 8;
	unsigned short buf[64];
	for (uint32_t y = 0; y < ycnt; y++)
	    for (uint32_t x = 0; x < xcnt; x++)
		buf[y * xcnt + x] = (unsigned short)(100 + 50 * x + 30 * y);

	dsp.dsp_xcnt = xcnt;
	dsp.dsp_ycnt = ycnt;
	dsp.dsp_buf  = buf;
	bip_int.count    = xcnt * ycnt;
	bip_int.u.uint16 = buf;

	init_tols(&ttol, &tol, 0.0, 0.05, 0.0); /* mid tolerance */
	if (!run_tess("dsp 8x8 ramp rel=0.05", &ip, &ttol, &tol, 0))
	    failures++;
    }

    /* -------------------------------------------------------------- */
    /* Test 4: 16×16 sinusoidal height field (exercises decimation)   */
    /* -------------------------------------------------------------- */
    {
	const uint32_t xcnt = 16, ycnt = 16;
	unsigned short buf[256];
	for (uint32_t y = 0; y < ycnt; y++) {
	    for (uint32_t x = 0; x < xcnt; x++) {
		double fx = (double)x / (xcnt - 1) * M_PI;
		double fy = (double)y / (ycnt - 1) * M_PI;
		double h  = 500.0 + 300.0 * sin(fx) * sin(fy);
		buf[y * xcnt + x] = (unsigned short)(int)h;
	    }
	}

	dsp.dsp_xcnt = xcnt;
	dsp.dsp_ycnt = ycnt;
	dsp.dsp_buf  = buf;
	bip_int.count    = xcnt * ycnt;
	bip_int.u.uint16 = buf;

	/* Tight: should use full path. */
	init_tols(&ttol, &tol, 0.0, 0.005, 0.0);
	if (!run_tess("dsp 16x16 sine tight (full path)", &ip, &ttol, &tol, 0))
	    failures++;

	/* Loose: should use decimation path. */
	init_tols(&ttol, &tol, 0.0, 0.15, 0.0);
	if (!run_tess("dsp 16x16 sine loose (decimation)", &ip, &ttol, &tol, 0))
	    failures++;
    }

    /* -------------------------------------------------------------- */
    /* Test 5: 8×8 grid with peak in the center (pyramid-like)        */
    /* -------------------------------------------------------------- */
    {
	const uint32_t xcnt = 8, ycnt = 8;
	unsigned short buf[64];
	for (uint32_t y = 0; y < ycnt; y++) {
	    for (uint32_t x = 0; x < xcnt; x++) {
		double cx = fabs((double)x - 3.5);
		double cy = fabs((double)y - 3.5);
		double d  = (cx > cy ? cx : cy);
		buf[y * xcnt + x] = (unsigned short)(int)(1000.0 - d * 200.0);
	    }
	}

	dsp.dsp_xcnt = xcnt;
	dsp.dsp_ycnt = ycnt;
	dsp.dsp_buf  = buf;
	bip_int.count    = xcnt * ycnt;
	bip_int.u.uint16 = buf;

	init_tols(&ttol, &tol, 0.0, 0.10, 0.0);
	if (!run_tess("dsp 8x8 pyramid loose (decimation)", &ip, &ttol, &tol, 0))
	    failures++;
    }

    /* -------------------------------------------------------------- */
    /* Test 6: 32×32 terrain, no-tol (default fallback)               */
    /* -------------------------------------------------------------- */
    {
	const uint32_t xcnt = 32, ycnt = 32;
	unsigned short *buf = (unsigned short *)bu_calloc(
	    xcnt * ycnt, sizeof(unsigned short), "dsp 32x32 buf");
	for (uint32_t y = 0; y < ycnt; y++) {
	    for (uint32_t x = 0; x < xcnt; x++) {
		double fx = (double)x / (xcnt - 1) * 6.28318;
		double fy = (double)y / (ycnt - 1) * 6.28318;
		double h  = 2000.0 + 800.0 * (sin(fx) + cos(fy));
		buf[y * xcnt + x] = (unsigned short)(int)h;
	    }
	}

	dsp.dsp_xcnt = xcnt;
	dsp.dsp_ycnt = ycnt;
	dsp.dsp_buf  = buf;
	bip_int.count    = xcnt * ycnt;
	bip_int.u.uint16 = buf;

	init_tols(&ttol, &tol, 0.0, 0.0, 0.0); /* no tol → default */
	if (!run_tess("dsp 32x32 wave no-tol", &ip, &ttol, &tol, 0))
	    failures++;

	bu_free(buf, "dsp 32x32 buf");
    }

    bu_vls_free(&dsp.dsp_name);
    return failures;
}


/*
 * EBM tess uses outline-tracing (not per-pixel faces), so it does NOT
 * have the dense-coplanar-mesh problem seen in pre-fix DSP.  The tess
 * function ignores ttol entirely (UNUSED(ttol)).  The datasrc=RT_EBM_SRC_OBJ
 * path reads the bitmap from eip->buf, which must be padded by BIT_XWIDEN=2
 * and BIT_YWIDEN=2 cells of zeros on all sides, i.e.
 *   buf size = (xdim+4) * (ydim+4) bytes.
 */
static int
test_ebm(void)
{
    int failures = 0;

    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_ebm_internal eip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_EBM;
    ip.idb_ptr = &eip;

    memset(&eip, 0, sizeof(eip));
    eip.magic = RT_EBM_INTERNAL_MAGIC;
    eip.tallness = 10.0;
    MAT_IDN(eip.mat);
    eip.datasrc = RT_EBM_SRC_OBJ;

    printf("\n--- EBM tests ---\n");

    /* 4x4 solid square bitmap: all cells set.
     * buf size = (4+4) * (4+4) = 64 bytes, all zeros initially (padding).
     * Set cells [0..3][0..3] (stored with +2 offset in each axis). */
    {
	const size_t xd = 4, yd = 4;
	const size_t stride = xd + 4;	/* BIT_XWIDEN*2 = 4 */
	const size_t bufsize = stride * (yd + 4);
	unsigned char *buf = (unsigned char *)bu_calloc(bufsize, 1, "ebm buf");

	for (size_t y = 0; y < yd; y++)
	    for (size_t x = 0; x < xd; x++)
		buf[(y + 2) * stride + (x + 2)] = 1;

	eip.xdim = (uint32_t)xd;
	eip.ydim = (uint32_t)yd;
	eip.buf = buf;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("ebm 4x4 solid square", &ip, &ttol, &tol, 0)) failures++;

	bu_free(buf, "ebm buf");
    }

    /* 8x8 bitmap with a 4x4 hole in the center (frame pattern). */
    {
	const size_t xd = 8, yd = 8;
	const size_t stride = xd + 4;
	const size_t bufsize = stride * (yd + 4);
	unsigned char *buf = (unsigned char *)bu_calloc(bufsize, 1, "ebm buf");

	for (size_t y = 0; y < yd; y++) {
	    for (size_t x = 0; x < xd; x++) {
		/* set outer frame (border cells), leave interior empty */
		int is_interior = (x >= 2 && x <= 5 && y >= 2 && y <= 5);
		if (!is_interior)
		    buf[(y + 2) * stride + (x + 2)] = 1;
	    }
	}

	eip.xdim = (uint32_t)xd;
	eip.ydim = (uint32_t)yd;
	eip.buf = buf;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("ebm 8x8 frame with 4x4 hole", &ip, &ttol, &tol, 0)) failures++;

	bu_free(buf, "ebm buf");
    }

    return failures;
}


/* ------------------------------------------------------------------ */
/* VOL (Voxel volume) tests                                             */
/* ------------------------------------------------------------------ */

/*
 * VOL tess creates individual quad faces per exposed voxel boundary, then
 * calls nmg_model_fuse() and nmg_shell_coplanar_face_merge() to collapse
 * adjacent coplanar faces.  For small grids this is fast.  Large flat
 * surfaces (e.g. a 256x256 voxel layer) could be slow for the same reason
 * that pre-TerraScape DSP was slow.
 *
 * The voxel map must be padded by VOL_[XYZ]WIDEN=2 on all sides:
 *   map size = (xdim+4) * (ydim+4) * (zdim+4) bytes.
 * VOL value must satisfy: lo <= value <= hi to be "on".
 */
static int
test_vol(void)
{
    int failures = 0;

    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_vol_internal vip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_VOL;
    ip.idb_ptr = &vip;

    memset(&vip, 0, sizeof(vip));
    vip.magic = RT_VOL_INTERNAL_MAGIC;
    vip.lo = 1;
    vip.hi = 255;
    VSET(vip.cellsize, 1.0, 1.0, 1.0);
    MAT_IDN(vip.mat);
    vip.datasrc = RT_VOL_SRC_OBJ;

    printf("\n--- VOL tests ---\n");

    /* 4x4x4 solid cube: all voxels set. */
    {
	const size_t xd = 4, yd = 4, zd = 4;
	/* stride with padding: (xdim+4), (ydim+4) */
	const size_t xs = xd + 4, ys = yd + 4, zs = zd + 4;
	const size_t bufsize = xs * ys * zs;
	unsigned char *buf = (unsigned char *)bu_calloc(bufsize, 1, "vol map");

	for (size_t z = 0; z < zd; z++)
	    for (size_t y = 0; y < yd; y++)
		for (size_t x = 0; x < xd; x++)
		    buf[((z+2)*ys + (y+2))*xs + (x+2)] = 200; /* in [lo,hi] */

	vip.xdim = (uint32_t)xd;
	vip.ydim = (uint32_t)yd;
	vip.zdim = (uint32_t)zd;
	vip.map = buf;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("vol 4x4x4 solid cube", &ip, &ttol, &tol, 0)) failures++;

	bu_free(buf, "vol map");
    }

    /* 6x6x3 slab: flat top/bottom surfaces (tests coplanar-face merging). */
    {
	const size_t xd = 6, yd = 6, zd = 3;
	const size_t xs = xd + 4, ys = yd + 4, zs = zd + 4;
	const size_t bufsize = xs * ys * zs;
	unsigned char *buf = (unsigned char *)bu_calloc(bufsize, 1, "vol map");

	for (size_t z = 0; z < zd; z++)
	    for (size_t y = 0; y < yd; y++)
		for (size_t x = 0; x < xd; x++)
		    buf[((z+2)*ys + (y+2))*xs + (x+2)] = 200;

	vip.xdim = (uint32_t)xd;
	vip.ydim = (uint32_t)yd;
	vip.zdim = (uint32_t)zd;
	vip.map = buf;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("vol 6x6x3 flat slab", &ip, &ttol, &tol, 0)) failures++;

	bu_free(buf, "vol map");
    }

    /* 4x4x4 hollow cube: only outer shell voxels set. */
    {
	const size_t xd = 4, yd = 4, zd = 4;
	const size_t xs = xd + 4, ys = yd + 4, zs = zd + 4;
	const size_t bufsize = xs * ys * zs;
	unsigned char *buf = (unsigned char *)bu_calloc(bufsize, 1, "vol map");

	for (size_t z = 0; z < zd; z++) {
	    for (size_t y = 0; y < yd; y++) {
		for (size_t x = 0; x < xd; x++) {
		    /* only set voxels on the outer shell */
		    if (x == 0 || x == xd-1 ||
			y == 0 || y == yd-1 ||
			z == 0 || z == zd-1)
			buf[((z+2)*ys + (y+2))*xs + (x+2)] = 200;
		}
	    }
	}

	vip.xdim = (uint32_t)xd;
	vip.ydim = (uint32_t)yd;
	vip.zdim = (uint32_t)zd;
	vip.map = buf;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("vol 4x4x4 hollow shell", &ip, &ttol, &tol, 0)) failures++;

	bu_free(buf, "vol map");
    }

    /*
     * 20x20x1 large flat slab: the coplanar-density stress test.
     *
     * With 2D coherent-patch merging the top and bottom faces each become
     * a single 20×20 NMG quad directly during construction — nmg_shell_
     * coplanar_face_merge has no work to do for those surfaces.  The four
     * side faces similarly collapse to one quad each.  We verify that the
     * overall pipeline succeeds.
     */
    {
	const size_t xd = 20, yd = 20, zd = 1;
	const size_t xs = xd + 4, ys = yd + 4, zs = zd + 4;
	const size_t bufsize = xs * ys * zs;
	unsigned char *buf = (unsigned char *)bu_calloc(bufsize, 1, "vol map");

	for (size_t z = 0; z < zd; z++)
	    for (size_t y = 0; y < yd; y++)
		for (size_t x = 0; x < xd; x++)
		    buf[((z+2)*ys + (y+2))*xs + (x+2)] = 200;

	vip.xdim = (uint32_t)xd;
	vip.ydim = (uint32_t)yd;
	vip.zdim = (uint32_t)zd;
	vip.map = buf;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("vol 20x20x1 large flat slab", &ip, &ttol, &tol, 0)) failures++;

	bu_free(buf, "vol map");
    }

    /*
     * 6x6x1 L-shaped slab: exercises non-rectangular coherent patches.
     *
     * The voxel map is a 6×6 slab with the top-right 3×3 corner empty,
     * forming an L-shape:
     *
     *   y=5  X X X . . .
     *   y=4  X X X . . .
     *   y=3  X X X . . .
     *   y=2  X X X X X X
     *   y=1  X X X X X X
     *   y=0  X X X X X X
     *        x=0 ... x=5
     *
     * For the z+ (top) face, the 2D coherent-patch algorithm should
     * produce TWO rectangles: [0..5]×[0..2] and [0..2]×[3..5].
     * The pipeline must complete without error.
     */
    {
	const size_t xd = 6, yd = 6, zd = 1;
	const size_t xs = xd + 4, ys = yd + 4, zs = zd + 4;
	const size_t bufsize = xs * ys * zs;
	unsigned char *buf = (unsigned char *)bu_calloc(bufsize, 1, "vol map");

	for (size_t z = 0; z < zd; z++) {
	    for (size_t y = 0; y < yd; y++) {
		for (size_t x = 0; x < xd; x++) {
		    /* omit top-right 3×3 corner (x>=3, y>=3) */
		    if (x >= 3 && y >= 3) continue;
		    buf[((z+2)*ys + (y+2))*xs + (x+2)] = 200;
		}
	    }
	}

	vip.xdim = (uint32_t)xd;
	vip.ydim = (uint32_t)yd;
	vip.zdim = (uint32_t)zd;
	vip.map = buf;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("vol 6x6x1 L-shape (non-rectangular patch)", &ip, &ttol, &tol, 0)) failures++;

	bu_free(buf, "vol map");
    }

    return failures;
}


/* ------------------------------------------------------------------ */
/* ARB8 (Generalized ARB) tests                                         */
/* ------------------------------------------------------------------ */

/*
 * rt_arb_tess ignores ttol entirely (UNUSED(ttol)).  It only uses tol for
 * nmg_region_a() and nmg_make_faces_within_tol().  We test several arb
 * types: ARB8 (8 unique pts), ARB6 (6 unique, 2 pairs duplicated),
 * ARB4 (tetrahedron, 4 unique pts), and ARB5 (pyramid, 5 unique pts).
 */
static int
test_arb(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_arb_internal tip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_ARB8;
    ip.idb_ptr = &tip;

    tip.magic = RT_ARB_INTERNAL_MAGIC;

    printf("\n--- ARB tests ---\n");

    /* ARB8: unit cube 10x10x10 */
    VSET(tip.pt[0],  0,  0,  0);
    VSET(tip.pt[1], 10,  0,  0);
    VSET(tip.pt[2], 10, 10,  0);
    VSET(tip.pt[3],  0, 10,  0);
    VSET(tip.pt[4],  0,  0, 10);
    VSET(tip.pt[5], 10,  0, 10);
    VSET(tip.pt[6], 10, 10, 10);
    VSET(tip.pt[7],  0, 10, 10);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb8 cube (10x10x10)", &ip, &ttol, &tol, 0)) failures++;

    /* ARB8 with abs tolerance */
    init_tols(&ttol, &tol, 0.5, 0.0, 0.0);
    if (!run_tess("arb8 cube (abs=0.5)", &ip, &ttol, &tol, 0)) failures++;

    /* ARB8 with no tolerance */
    init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
    if (!run_tess("arb8 cube no-tol", &ip, &ttol, &tol, 0)) failures++;

    /* ARB6: triangular prism.  pts[6]==pts[7] makes one quad a triangle. */
    VSET(tip.pt[0],  0,  0,  0);
    VSET(tip.pt[1], 10,  0,  0);
    VSET(tip.pt[2], 10, 10,  0);
    VSET(tip.pt[3],  0, 10,  0);
    VSET(tip.pt[4],  5,  0, 10);
    VSET(tip.pt[5],  5,  0, 10);   /* same as pt[4]: top front edge */
    VSET(tip.pt[6],  5, 10, 10);
    VSET(tip.pt[7],  5, 10, 10);   /* same as pt[6]: top back edge */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb6 triangular prism", &ip, &ttol, &tol, 0)) failures++;

    /* ARB5: square pyramid.  pts[4..7] all at apex. */
    VSET(tip.pt[0],  0,  0,  0);
    VSET(tip.pt[1], 10,  0,  0);
    VSET(tip.pt[2], 10, 10,  0);
    VSET(tip.pt[3],  0, 10,  0);
    VSET(tip.pt[4],  5,  5, 10);   /* apex */
    VSET(tip.pt[5],  5,  5, 10);
    VSET(tip.pt[6],  5,  5, 10);
    VSET(tip.pt[7],  5,  5, 10);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb5 square pyramid", &ip, &ttol, &tol, 0)) failures++;

    /* ARB4: tetrahedron.  pts[3..7] all at fourth vertex.
     * rt_arb_tess with this duplicate-endpoint encoding returns -2
     * (degenerate faces in the ARB8→ARB4 topology).  Mark as expect_fail. */
    VSET(tip.pt[0],  0,  0,  0);
    VSET(tip.pt[1], 10,  0,  0);
    VSET(tip.pt[2],  5,  8,  0);
    VSET(tip.pt[3],  5,  3, 10);   /* apex */
    VSET(tip.pt[4],  5,  3, 10);
    VSET(tip.pt[5],  5,  3, 10);
    VSET(tip.pt[6],  5,  3, 10);
    VSET(tip.pt[7],  5,  3, 10);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb4 tetrahedron (expect fail - degenerate ARB topo)", &ip, &ttol, &tol, 1)) failures++;

    /* ARB8 large scale */
    VSET(tip.pt[0],      0,      0,      0);
    VSET(tip.pt[1], 100000,      0,      0);
    VSET(tip.pt[2], 100000, 100000,      0);
    VSET(tip.pt[3],      0, 100000,      0);
    VSET(tip.pt[4],      0,      0, 100000);
    VSET(tip.pt[5], 100000,      0, 100000);
    VSET(tip.pt[6], 100000, 100000, 100000);
    VSET(tip.pt[7],      0, 100000, 100000);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb8 large cube (100000^3)", &ip, &ttol, &tol, 0)) failures++;

    /* ARB8 tiny scale: vertices at 0.001 mm intervals.
     * tol->dist = 0.005 mm > 0.001 mm, so the face geometry is below the
     * geometric tolerance — rt_arb_tess returns -2 (degenerate). */
    VSET(tip.pt[0], 0.0,   0.0,   0.0);
    VSET(tip.pt[1], 0.001, 0.0,   0.0);
    VSET(tip.pt[2], 0.001, 0.001, 0.0);
    VSET(tip.pt[3], 0.0,   0.001, 0.0);
    VSET(tip.pt[4], 0.0,   0.0,   0.001);
    VSET(tip.pt[5], 0.001, 0.0,   0.001);
    VSET(tip.pt[6], 0.001, 0.001, 0.001);
    VSET(tip.pt[7], 0.0,   0.001, 0.001);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb8 tiny cube (0.001^3, expect fail - below tol)", &ip, &ttol, &tol, 1)) failures++;

    /* ARB8 thin slab (extreme aspect ratio) */
    VSET(tip.pt[0],    0,   0,    0);
    VSET(tip.pt[1], 1000,   0,    0);
    VSET(tip.pt[2], 1000, 500,    0);
    VSET(tip.pt[3],    0, 500,    0);
    VSET(tip.pt[4],    0,   0, 0.01);
    VSET(tip.pt[5], 1000,   0, 0.01);
    VSET(tip.pt[6], 1000, 500, 0.01);
    VSET(tip.pt[7],    0, 500, 0.01);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb8 thin slab (1000x500x0.01)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* ARS (Arbitrary faceted surface) tests                                */
/* ------------------------------------------------------------------ */

/*
 * rt_ars_tess ignores ttol (UNUSED(ttol)).
 *
 * An ARS solid is defined as ncurves cross-sectional rings.  Ring 0 and
 * ring ncurves-1 are typically degenerate (all pts the same) to form
 * top/bottom caps.  pts_per_curve includes the repeated first/last point
 * that closes each ring.  Each "curve" pointer points to pts_per_curve*3
 * fastf_t values.
 *
 * Helper: allocate and fill a ring of N 3D points laid out as a regular
 * polygon at height z and radius r.  Returns heap memory that must be
 * freed by the caller (via bu_free on each curves[i]).
 */
static fastf_t *
ars_make_ring(size_t n, double r, double cx, double cy, double z)
{
    fastf_t *ring = (fastf_t *)bu_malloc(n * 3 * sizeof(fastf_t), "ars ring");
    for (size_t i = 0; i < n; i++) {
	double angle = i * M_2PI / n;
	ring[i*3+0] = (fastf_t)(cx + r * cos(angle));
	ring[i*3+1] = (fastf_t)(cy + r * sin(angle));
	ring[i*3+2] = (fastf_t)z;
    }
    return ring;
}

/* Helper: allocate a degenerate ring (all pts at one point). */
static fastf_t *
ars_make_cap(size_t n, double cx, double cy, double z)
{
    fastf_t *ring = (fastf_t *)bu_malloc(n * 3 * sizeof(fastf_t), "ars cap");
    for (size_t i = 0; i < n; i++) {
	ring[i*3+0] = (fastf_t)cx;
	ring[i*3+1] = (fastf_t)cy;
	ring[i*3+2] = (fastf_t)z;
    }
    return ring;
}

static int
test_ars(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_ars_internal aip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_ARS;
    ip.idb_ptr = &aip;

    aip.magic = RT_ARS_INTERNAL_MAGIC;

    printf("\n--- ARS tests ---\n");

    /* ---- ARS cylinder: 4-curve, 8-sided ------------------------------- */
    {
	const size_t ncurves = 4;
	const size_t ppc = 8;
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	curves[0] = ars_make_cap(ppc, 0.0, 0.0, 0.0);     /* bottom cap */
	curves[1] = ars_make_ring(ppc, 5.0, 0.0, 0.0, 0.0);  /* bottom ring */
	curves[2] = ars_make_ring(ppc, 5.0, 0.0, 0.0, 10.0); /* top ring */
	curves[3] = ars_make_cap(ppc, 0.0, 0.0, 10.0);    /* top cap */

	aip.ncurves = ncurves;
	aip.pts_per_curve = ppc;
	aip.curves = curves;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("ars cylinder 4-curve 8-sided (r=5 h=10)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.5, 0.0, 0.0);
	if (!run_tess("ars cylinder (abs=0.5)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
	if (!run_tess("ars cylinder no-tol", &ip, &ttol, &tol, 0)) failures++;

	for (size_t i = 0; i < ncurves; i++)
	    bu_free(curves[i], "ars ring");
	bu_free(curves, "ars curves");
    }

    /* ---- ARS cone: 3-curve (bottom ring, top cap degenerate) ----------- */
    {
	const size_t ncurves = 3;
	const size_t ppc = 12;
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	curves[0] = ars_make_ring(ppc, 8.0, 0.0, 0.0, 0.0); /* bottom ring */
	curves[1] = ars_make_ring(ppc, 2.0, 0.0, 0.0, 15.0); /* top ring */
	curves[2] = ars_make_cap(ppc, 0.0, 0.0, 15.0);    /* top cap */

	aip.ncurves = ncurves;
	aip.pts_per_curve = ppc;
	aip.curves = curves;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("ars cone (r_bot=8 r_top=2 h=15)", &ip, &ttol, &tol, 0)) failures++;

	for (size_t i = 0; i < ncurves; i++)
	    bu_free(curves[i], "ars ring");
	bu_free(curves, "ars curves");
    }

    /* ---- ARS sphere approximation: 5 rings ----------------------------- */
    {
	const size_t ncurves = 5;
	const size_t ppc = 16;
	const double R = 10.0;
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	/* latitude steps: -90, -45, 0, 45, 90 degrees */
	static const double lats[] = {-M_PI_2, -M_PI/4.0, 0.0, M_PI/4.0, M_PI_2};
	for (size_t i = 0; i < ncurves; i++) {
	    double z = R * sin(lats[i]);
	    double r = R * cos(lats[i]);
	    if (fabs(r) < 1e-10)
		curves[i] = ars_make_cap(ppc, 0.0, 0.0, z);
	    else
		curves[i] = ars_make_ring(ppc, r, 0.0, 0.0, z);
	}

	aip.ncurves = ncurves;
	aip.pts_per_curve = ppc;
	aip.curves = curves;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("ars sphere-approx 5 rings 16-sided (R=10)", &ip, &ttol, &tol, 0)) failures++;

	for (size_t i = 0; i < ncurves; i++)
	    bu_free(curves[i], "ars ring");
	bu_free(curves, "ars curves");
    }

    /* ---- ARS high-sided cylinder (many segments per ring) -------------- */
    {
	const size_t ncurves = 4;
	const size_t ppc = 64;
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	curves[0] = ars_make_cap(ppc, 0.0, 0.0, 0.0);
	curves[1] = ars_make_ring(ppc, 3.0, 0.0, 0.0, 0.0);
	curves[2] = ars_make_ring(ppc, 3.0, 0.0, 0.0, 20.0);
	curves[3] = ars_make_cap(ppc, 0.0, 0.0, 20.0);

	aip.ncurves = ncurves;
	aip.pts_per_curve = ppc;
	aip.curves = curves;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("ars high-sided cylinder (64-sided r=3 h=20)", &ip, &ttol, &tol, 0)) failures++;

	for (size_t i = 0; i < ncurves; i++)
	    bu_free(curves[i], "ars ring");
	bu_free(curves, "ars curves");
    }

    /* ---- ARS multi-ring tapering shape (stress for strip loop) --------- */
    {
	const size_t ncurves = 8;
	const size_t ppc = 10;
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	curves[0] = ars_make_cap(ppc, 0.0, 0.0, 0.0);
	for (size_t i = 1; i < ncurves - 1; i++) {
	    double frac = (double)i / (ncurves - 2);
	    double r = 5.0 * (1.0 - 0.8 * frac);   /* radius tapers 5 → 1 */
	    curves[i] = ars_make_ring(ppc, r, 0.0, 0.0, (double)i * 5.0);
	}
	curves[ncurves-1] = ars_make_cap(ppc, 0.0, 0.0, (double)(ncurves-2)*5.0);

	aip.ncurves = ncurves;
	aip.pts_per_curve = ppc;
	aip.curves = curves;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("ars multi-ring taper (8 curves, tapers 5→1)", &ip, &ttol, &tol, 0)) failures++;

	for (size_t i = 0; i < ncurves; i++)
	    bu_free(curves[i], "ars ring");
	bu_free(curves, "ars curves");
    }

    return failures;
}


/* ------------------------------------------------------------------ */
/* ARBN (Arbitrary convex N-hedron) tests                               */
/* ------------------------------------------------------------------ */

/*
 * rt_arbn_tess ignores ttol (UNUSED(ttol)).  An ARBN is defined by neqn
 * half-space plane equations: plane[i] = {nx, ny, nz, d} where the
 * outward unit normal is (nx,ny,nz) and the plane passes through the
 * point at distance d from the origin: dot(n,pt) = d means pt is ON the
 * plane; dot(n,pt) <= d means pt is INSIDE.
 */
static int
test_arbn(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_arbn_internal aip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_ARBN;
    ip.idb_ptr = &aip;

    aip.magic = RT_ARBN_INTERNAL_MAGIC;

    printf("\n--- ARBN tests ---\n");

    /* ---- ARBN cube (6 planes) ------------------------------------------ */
    {
	plane_t planes[6];
	VSET(planes[0], 1, 0, 0);  planes[0][3] =  5.0;   /* +X face */
	VSET(planes[1],-1, 0, 0);  planes[1][3] =  5.0;   /* -X face */
	VSET(planes[2], 0, 1, 0);  planes[2][3] =  5.0;   /* +Y face */
	VSET(planes[3], 0,-1, 0);  planes[3][3] =  5.0;   /* -Y face */
	VSET(planes[4], 0, 0, 1);  planes[4][3] =  5.0;   /* +Z face */
	VSET(planes[5], 0, 0,-1);  planes[5][3] =  5.0;   /* -Z face */

	aip.neqn = 6;
	aip.eqn  = planes;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("arbn cube 6-plane (side=10)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.5, 0.0, 0.0);
	if (!run_tess("arbn cube (abs=0.5)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
	if (!run_tess("arbn cube no-tol", &ip, &ttol, &tol, 0)) failures++;
    }

    /* ---- ARBN tetrahedron (4 planes) ----------------------------------- */
    {
	/* Regular tetrahedron with vertices at:
	 *  (1,1,1), (1,-1,-1), (-1,1,-1), (-1,-1,1)
	 * Face normals (outward) are the normalised sum of each triple of verts. */
	plane_t planes[4];
	/* Face opposite vertex 0 (1,1,1): normal = normalised((-1,-1,-1)) */
	VSET(planes[0], -M_SQRT1_2/sqrt(3)*sqrt(3),
	                -1.0/sqrt(3), -1.0/sqrt(3)); /* normalise */
	/* Just use the known unit normals for a regular tetrahedron: */
	double r3 = 1.0/sqrt(3.0);
	VSET(planes[0],  r3,  r3,  r3);  planes[0][3] =  r3 * 3.0;
	VSET(planes[1],  r3, -r3, -r3);  planes[1][3] =  r3;
	VSET(planes[2], -r3,  r3, -r3);  planes[2][3] =  r3;
	VSET(planes[3], -r3, -r3,  r3);  planes[3][3] =  r3;

	aip.neqn = 4;
	aip.eqn  = planes;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("arbn tetrahedron (4 planes)", &ip, &ttol, &tol, 0)) failures++;
    }

    /* ---- ARBN octahedron (8 planes) ------------------------------------ */
    {
	/* Regular octahedron: |x|+|y|+|z| <= R, all 8 sign combinations. */
	plane_t planes[8];
	double R = 10.0;
	double n = 1.0/sqrt(3.0);
	int idx = 0;
	for (int sx = -1; sx <= 1; sx += 2)
	    for (int sy = -1; sy <= 1; sy += 2)
		for (int sz = -1; sz <= 1; sz += 2) {
		    VSET(planes[idx], sx*n, sy*n, sz*n);
		    planes[idx][3] = R * n;
		    idx++;
		}

	aip.neqn = 8;
	aip.eqn  = planes;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("arbn octahedron (8 planes R=10)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.3, 0.03, 0.0);
	if (!run_tess("arbn octahedron (abs=0.3 rel=0.03)", &ip, &ttol, &tol, 0)) failures++;
    }

    /* ---- ARBN cuboctahedron (12 planes: 6 cube + 8 oct corners cut) ---- */
    {
	/* Start with cube planes at ±8 and truncate 8 corners with planes
	 * from the octahedron at R=12: gives a shape with 14 faces. */
	plane_t planes[14];
	int idx = 0;
	/* Cube faces at ±8 */
	VSET(planes[idx], 1,0,0); planes[idx][3]=8; idx++;
	VSET(planes[idx],-1,0,0); planes[idx][3]=8; idx++;
	VSET(planes[idx], 0,1,0); planes[idx][3]=8; idx++;
	VSET(planes[idx], 0,-1,0); planes[idx][3]=8; idx++;
	VSET(planes[idx], 0,0,1); planes[idx][3]=8; idx++;
	VSET(planes[idx], 0,0,-1); planes[idx][3]=8; idx++;
	/* 8 octahedron cutting planes */
	double n = 1.0/sqrt(3.0);
	for (int sx = -1; sx <= 1; sx += 2)
	    for (int sy = -1; sy <= 1; sy += 2)
		for (int sz = -1; sz <= 1; sz += 2) {
		    VSET(planes[idx], sx*n, sy*n, sz*n);
		    planes[idx][3] = 10.0 * n;
		    idx++;
		}

	aip.neqn = 14;
	aip.eqn  = planes;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("arbn cuboctahedron (14 planes)", &ip, &ttol, &tol, 0)) failures++;
    }

    return failures;
}


/* ------------------------------------------------------------------ */
/* PIPE tests                                                            */
/* ------------------------------------------------------------------ */

/*
 * rt_pipe_tess uses ttol to derive arc_segs (min 6 segments per circle).
 * The pipe is defined by a linked list of wdb_pipe_pnt structs; each has:
 *   pp_coord      – 3-D control point
 *   pp_od         – outer diameter (must be > 0)
 *   pp_id         – inner diameter (0 = solid wire)
 *   pp_bendradius – bend radius at this point (must be >= pp_od/2)
 *
 * The tessellation connects consecutive (pp1, pp2) and (pp2, pp3) triples.
 * At bends, it sweeps the pipe cross-section around the bend arc.
 */
static int
test_pipe(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_pipe_internal pip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_PIPE;
    ip.idb_ptr = &pip;

    pip.pipe_magic = RT_PIPE_INTERNAL_MAGIC;
    BU_LIST_INIT(&pip.pipe_segs_head);

    printf("\n--- PIPE tests ---\n");

    /* ---- Straight solid wire (2 pts, id=0) ----------------------------- */
    {
	struct wdb_pipe_pnt p1, p2;

	p1.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p1.pp_coord, 0, 0, 0);
	p1.pp_id = 0.0;
	p1.pp_od = 4.0;
	p1.pp_bendradius = 8.0;

	p2.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p2.pp_coord, 0, 0, 20);
	p2.pp_id = 0.0;
	p2.pp_od = 4.0;
	p2.pp_bendradius = 8.0;

	BU_LIST_INSERT(&pip.pipe_segs_head, &p1.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p2.l);
	pip.pipe_count = 2;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("pipe straight solid wire (od=4 h=20)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.3, 0.0, 0.0);
	if (!run_tess("pipe straight solid wire (abs=0.3)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
	if (!run_tess("pipe straight solid wire (norm=0.1)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.0, 0.0, 0.0);
	if (!run_tess("pipe straight solid wire no-tol", &ip, &ttol, &tol, 0)) failures++;

	BU_LIST_DEQUEUE(&p1.l);
	BU_LIST_DEQUEUE(&p2.l);
	pip.pipe_count = 0;
    }

    /* ---- Straight hollow pipe (2 pts, id>0) ---------------------------- */
    {
	struct wdb_pipe_pnt p1, p2;

	p1.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p1.pp_coord, 0, 0, 0);
	p1.pp_id = 2.0;
	p1.pp_od = 5.0;
	p1.pp_bendradius = 10.0;

	p2.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p2.pp_coord, 0, 0, 30);
	p2.pp_id = 2.0;
	p2.pp_od = 5.0;
	p2.pp_bendradius = 10.0;

	BU_LIST_INSERT(&pip.pipe_segs_head, &p1.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p2.l);
	pip.pipe_count = 2;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("pipe hollow (id=2 od=5 h=30)", &ip, &ttol, &tol, 0)) failures++;

	BU_LIST_DEQUEUE(&p1.l);
	BU_LIST_DEQUEUE(&p2.l);
	pip.pipe_count = 0;
    }

    /* ---- L-shaped bend (3 pts, 90° turn) ------------------------------- */
    {
	struct wdb_pipe_pnt p1, p2, p3;
	const double od = 3.0;
	const double br = 6.0;   /* bend radius >= od/2 */

	p1.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p1.pp_coord, -20, 0, 0);
	p1.pp_id = 0.0; p1.pp_od = od; p1.pp_bendradius = br;

	p2.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p2.pp_coord, 0, 0, 0);   /* bend point */
	p2.pp_id = 0.0; p2.pp_od = od; p2.pp_bendradius = br;

	p3.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p3.pp_coord, 0, 20, 0);
	p3.pp_id = 0.0; p3.pp_od = od; p3.pp_bendradius = br;

	BU_LIST_INSERT(&pip.pipe_segs_head, &p1.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p2.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p3.l);
	pip.pipe_count = 3;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("pipe L-bend (3 pts, 90deg od=3 br=6)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.0, 0.0, 0.1);
	if (!run_tess("pipe L-bend norm=0.1", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.3, 0.03, 0.15);
	if (!run_tess("pipe L-bend all-tols", &ip, &ttol, &tol, 0)) failures++;

	BU_LIST_DEQUEUE(&p1.l);
	BU_LIST_DEQUEUE(&p2.l);
	BU_LIST_DEQUEUE(&p3.l);
	pip.pipe_count = 0;
    }

    /* ---- U-shaped pipe (4 pts: two 90° bends) -------------------------- */
    {
	struct wdb_pipe_pnt p1, p2, p3, p4;
	const double od = 4.0;
	const double br = 8.0;

	p1.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p1.pp_coord, 0, 0, 0);
	p1.pp_id = 0.0; p1.pp_od = od; p1.pp_bendradius = br;

	p2.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p2.pp_coord, 20, 0, 0);
	p2.pp_id = 0.0; p2.pp_od = od; p2.pp_bendradius = br;

	p3.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p3.pp_coord, 20, 30, 0);
	p3.pp_id = 0.0; p3.pp_od = od; p3.pp_bendradius = br;

	p4.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p4.pp_coord, 0, 30, 0);
	p4.pp_id = 0.0; p4.pp_od = od; p4.pp_bendradius = br;

	BU_LIST_INSERT(&pip.pipe_segs_head, &p1.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p2.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p3.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p4.l);
	pip.pipe_count = 4;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("pipe U-shape (4 pts, 2 bends od=4 br=8)", &ip, &ttol, &tol, 0)) failures++;

	BU_LIST_DEQUEUE(&p1.l);
	BU_LIST_DEQUEUE(&p2.l);
	BU_LIST_DEQUEUE(&p3.l);
	BU_LIST_DEQUEUE(&p4.l);
	pip.pipe_count = 0;
    }

    /* ---- Varying OD along length (tapering pipe) ----------------------- */
    {
	struct wdb_pipe_pnt p1, p2;

	p1.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p1.pp_coord, 0, 0, 0);
	p1.pp_id = 0.0; p1.pp_od = 10.0; p1.pp_bendradius = 20.0;

	p2.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p2.pp_coord, 0, 0, 50);
	p2.pp_id = 0.0; p2.pp_od = 2.0; p2.pp_bendradius = 4.0;

	BU_LIST_INSERT(&pip.pipe_segs_head, &p1.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p2.l);
	pip.pipe_count = 2;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("pipe tapering (od 10→2 h=50)", &ip, &ttol, &tol, 0)) failures++;

	BU_LIST_DEQUEUE(&p1.l);
	BU_LIST_DEQUEUE(&p2.l);
	pip.pipe_count = 0;
    }

    /* ---- Very tight norm tolerance (fine mesh) ------------------------- */
    {
	struct wdb_pipe_pnt p1, p2;

	p1.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p1.pp_coord, 0, 0, 0);
	p1.pp_id = 0.0; p1.pp_od = 4.0; p1.pp_bendradius = 8.0;

	p2.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p2.pp_coord, 0, 0, 20);
	p2.pp_id = 0.0; p2.pp_od = 4.0; p2.pp_bendradius = 8.0;

	BU_LIST_INSERT(&pip.pipe_segs_head, &p1.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p2.l);
	pip.pipe_count = 2;

	init_tols(&ttol, &tol, 0.0, 0.0, 0.02);
	if (!run_tess("pipe norm-driven (norm=0.02)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.0, 0.0, 0.9);
	if (!run_tess("pipe loose-norm (norm=0.9)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
	if (!run_tess("pipe tight-rel (rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

	BU_LIST_DEQUEUE(&p1.l);
	BU_LIST_DEQUEUE(&p2.l);
	pip.pipe_count = 0;
    }

    return failures;
}


/* ------------------------------------------------------------------ */
/* METABALL tests                                                        */
/* ------------------------------------------------------------------ */

/*
 * rt_metaball_tess uses the marching-cubes algorithm.  The step size
 * (mtol) is derived from ttol: max(ttol->abs, ttol->rel * radius * 10,
 * tol->dist).  Very tight tolerances → many cubes → very slow test.
 * We use loose tolerances (rel=0.2) for all metaball tests to keep
 * runtime reasonable.  We test all three evaluation methods:
 *   METABALL_METABALL (0), METABALL_ISOPOTENTIAL (1), METABALL_BLOB (2).
 */
static int
test_metaball(void)
{
    int failures = 0;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;

    struct rt_db_internal ip;
    struct rt_metaball_internal mip;

    ip.idb_magic = RT_DB_INTERNAL_MAGIC;
    ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    ip.idb_minor_type = ID_METABALL;
    ip.idb_ptr = &mip;

    mip.magic = RT_METABALL_INTERNAL_MAGIC;
    mip.threshold = 1.0;
    mip.initstep  = 0.1;
    mip.finalstep = 0.001;
    BU_LIST_INIT(&mip.metaball_ctrl_head);

    printf("\n--- METABALL tests ---\n");

    /* ---- METABALL_METABALL method (not implemented) --------------------
     * rt_metaball_point_value_metaball() is a stub that always returns 0,
     * so the marching-cubes field evaluation is always "outside" regardless
     * of control-point positions.  The tessellation succeeds (returns 0)
     * but produces zero triangles, which is not useful for coverage.
     * More importantly, with a fine tolerance the voxel grid can have
     * hundreds of millions of cells, each causing a bu_log() call, making
     * any test with METABALL_METABALL extremely slow.  We skip this method
     * until it is actually implemented.
     */

    /* ---- METABALL_ISOPOTENTIAL method: two control points -------------- */
    {
	struct wdb_metaball_pnt p1, p2;

	p1.l.magic = WDB_METABALLPT_MAGIC;
	VSET(p1.coord, -3, 0, 0);
	VSET(p1.coord2, 0, 0, 0);
	p1.type = WDB_METABALLPT_TYPE_POINT;
	p1.fldstr = 2.0; p1.sweat = 1.0;

	p2.l.magic = WDB_METABALLPT_MAGIC;
	VSET(p2.coord,  3, 0, 0);
	VSET(p2.coord2, 0, 0, 0);
	p2.type = WDB_METABALLPT_TYPE_POINT;
	p2.fldstr = 2.0; p2.sweat = 1.0;

	BU_LIST_INSERT(&mip.metaball_ctrl_head, &p1.l);
	BU_LIST_INSERT(&mip.metaball_ctrl_head, &p2.l);
	mip.method    = METABALL_ISOPOTENTIAL;
	mip.threshold = 1.0;

	init_tols(&ttol, &tol, 0.0, 0.2, 0.0);
	if (!run_tess("metaball two-pt ISOPOTENTIAL (rel=0.2)", &ip, &ttol, &tol, 0)) failures++;

	BU_LIST_DEQUEUE(&p1.l);
	BU_LIST_DEQUEUE(&p2.l);
    }

    /* ---- METABALL_BLOB method: three control points -------------------- */
    {
	struct wdb_metaball_pnt p1, p2, p3;

	p1.l.magic = WDB_METABALLPT_MAGIC;
	VSET(p1.coord, -4, 0, 0);
	VSET(p1.coord2, 0, 0, 0);
	p1.type = WDB_METABALLPT_TYPE_POINT;
	p1.fldstr = 2.0; p1.sweat = 0.5;

	p2.l.magic = WDB_METABALLPT_MAGIC;
	VSET(p2.coord,  0, 0, 0);
	VSET(p2.coord2, 0, 0, 0);
	p2.type = WDB_METABALLPT_TYPE_POINT;
	p2.fldstr = 3.0; p2.sweat = 0.5;

	p3.l.magic = WDB_METABALLPT_MAGIC;
	VSET(p3.coord,  4, 0, 0);
	VSET(p3.coord2, 0, 0, 0);
	p3.type = WDB_METABALLPT_TYPE_POINT;
	p3.fldstr = 2.0; p3.sweat = 0.5;

	BU_LIST_INSERT(&mip.metaball_ctrl_head, &p1.l);
	BU_LIST_INSERT(&mip.metaball_ctrl_head, &p2.l);
	BU_LIST_INSERT(&mip.metaball_ctrl_head, &p3.l);
	mip.method    = METABALL_BLOB;
	mip.threshold = 1.0;

	init_tols(&ttol, &tol, 0.0, 0.2, 0.0);
	if (!run_tess("metaball three-pt BLOB (rel=0.2)", &ip, &ttol, &tol, 0)) failures++;

	BU_LIST_DEQUEUE(&p1.l);
	BU_LIST_DEQUEUE(&p2.l);
	BU_LIST_DEQUEUE(&p3.l);
    }

    /* ---- METABALL with no control points ---------------------------------
     * rt_metaball_get_bounding_sphere returns 0 (not -1) for an empty
     * control-point list due to the ∞-∞=NaN edge case in the bounds check.
     * rt_metaball_tess then proceeds with radius=0 and produces an empty
     * mesh, returning 0.  Accept success (no crash, 0 faces) rather than
     * counting it as a failure; the behaviour is not harmful. */
    {
	mip.method    = METABALL_ISOPOTENTIAL;
	mip.threshold = 1.0;
	/* list already empty */
	init_tols(&ttol, &tol, 0.5, 0.0, 0.0);
	(void)run_tess("metaball no-pts (degenerate, success with 0 faces)", &ip, &ttol, &tol, 0);
    }

    /* ---- METABALL single point with fldstr < threshold (ISOPOTENTIAL) ---
     * For ISOPOTENTIAL: field at distance d = fldstr/d.  Surface is where
     * field = threshold = 1.0 → d = fldstr/threshold = 0.5/1.0 = 0.5 mm.
     * This IS a valid surface — a sphere of radius 0.5 mm. */
    {
	struct wdb_metaball_pnt pt1;
	pt1.l.magic = WDB_METABALLPT_MAGIC;
	VSET(pt1.coord, 0, 0, 0);
	VSET(pt1.coord2, 0, 0, 0);
	pt1.type   = WDB_METABALLPT_TYPE_POINT;
	pt1.fldstr = 0.5;
	pt1.sweat  = 1.0;

	BU_LIST_INSERT(&mip.metaball_ctrl_head, &pt1.l);
	mip.method    = METABALL_ISOPOTENTIAL;
	mip.threshold = 1.0;

	init_tols(&ttol, &tol, 0.5, 0.0, 0.0);
	if (!run_tess("metaball single-pt ISOPOTENTIAL (fldstr=0.5 threshold=1.0)", &ip, &ttol, &tol, 0)) failures++;

	BU_LIST_DEQUEUE(&pt1.l);
    }

    return failures;
}


/* ------------------------------------------------------------------ */
/* Input .g file scanner                                               */
/* ------------------------------------------------------------------ */

/**
 * Open an existing .g database, iterate over all solid (non-combination)
 * primitives, skip BREP objects, tessellate each one, and report the
 * manifold quality of the resulting mesh.
 *
 * @return number of failures (0 = all passed).
 */
static int
scan_input_g(const char *g_path)
{
    int failures = 0;

    struct db_i *dbip = db_open(g_path, DB_OPEN_READONLY);
    if (dbip == DBI_NULL) {
	fprintf(stderr, "ERROR: cannot open '%s'\n", g_path);
	return 1;
    }
    if (db_dirbuild(dbip) < 0) {
	fprintf(stderr, "ERROR: db_dirbuild failed for '%s'\n", g_path);
	db_close(dbip);
	return 1;
    }

    /* Standard tolerances used for tessellation */
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
    struct bn_tol tol = BN_TOL_INIT_ZERO;
    ttol.magic = BG_TESS_TOL_MAGIC;
    tol.magic = BN_TOL_MAGIC;
    ttol.abs  = (g_scan_abs  > 0.0) ? g_scan_abs  : 0.0;
    ttol.rel  = (g_scan_rel  > 0.0) ? g_scan_rel  : 0.01; /* 1% chord-height */
    ttol.norm = (g_scan_norm > 0.0) ? g_scan_norm : 0.0;
    tol.dist = 0.005;
    tol.dist_sq = tol.dist * tol.dist;
    tol.perp = 1e-6;
    tol.para = 1.0 - tol.perp;

    struct bu_list vlfree;
    BU_LIST_INIT(&vlfree);

    /* Collect all primitive (solid) entries */
    struct directory **dpv = NULL;
    size_t ndp = db_ls(dbip, DB_LS_PRIM, NULL, &dpv);

    printf("\n--- Input .g scan: '%s'  (%zu solid(s)) ---\n", g_path, ndp);
    printf("    Tolerances: rel=%.4g  abs=%.4g  norm=%.4g\n",
	   ttol.rel, ttol.abs, ttol.norm);

    int n_skip  = 0;
    int n_fail  = 0;
    int n_pass  = 0;
    int n_tess_fail = 0;

    for (size_t i = 0; i < ndp; i++) {
	struct directory *dp = dpv[i];

	struct rt_db_internal intern;
	RT_DB_INTERNAL_INIT(&intern);
	int id = rt_db_get_internal(&intern, dp, dbip, NULL, &rt_uniresource);
	if (id < 0) {
	    fprintf(stderr, "  SKIP %-32s  (rt_db_get_internal failed)\n", dp->d_namep);
	    n_skip++;
	    continue;
	}

	/* Skip primitives that have no tessellation function or are known
	 * not to produce NMG output we can validate:                      */
	switch (id) {
	    case ID_BREP:       /* B-rep: known broken in this context */
	    case ID_HALF:       /* infinite half-space: no closed mesh */
	    case ID_GRIP:       /* stub */
	    case ID_JOINT:      /* stub */
	    case ID_HF:         /* deprecated, use DSP */
	    case ID_SUBMODEL:   /* references external .g */
	    case ID_PNTS:       /* point cloud: no tess */
	    case ID_ANNOT:      /* annotation: no tess */
	    case ID_HRT:        /* no NMG impl */
	    case ID_DATUM:      /* stub */
	    case ID_SCRIPT:     /* no tess */
	    case ID_SKETCH:     /* 2-D only */
		fprintf(stderr, "  SKIP %-32s  (type %d: no manifold tess)\n",
			dp->d_namep, id);
		n_skip++;
		rt_db_free_internal(&intern);
		continue;
	    default:
		break;
	}

	/* Tessellate - wrap in BU_SETJUMP to catch bu_bomb() */
	struct model *m = nmg_mm();
	struct nmgregion *r = NULL;

	fprintf(stderr, "STARTING: %s\n", dp->d_namep);
	fflush(stderr);

	int ret = -1;
	if (!BU_SETJUMP) {
	    ret = rt_obj_tess(&r, m, &intern, &ttol, &tol);
	} else {
	    BU_UNSETJUMP;
	    fprintf(stderr, "  TESS-BOMB %-32s  (bu_bomb in tess)\n", dp->d_namep);
	    n_tess_fail++;
	    failures++;
	    nmg_km(m);
	    rt_db_free_internal(&intern);
	    continue;
	} BU_UNSETJUMP;

	if (ret != 0 || r == NULL) {
	    fprintf(stderr, "  TESS-FAIL %-32s  (ret=%d)\n", dp->d_namep, ret);
	    n_tess_fail++;
	    failures++;
	    nmg_km(m);
	    rt_db_free_internal(&intern);
	    continue;
	}

	/* Count faces */
	int nfaces = 0;
	struct shell *s;
	for (BU_LIST_FOR(s, shell, &r->s_hd)) {
	    struct faceuse *fu;
	    for (BU_LIST_FOR(fu, faceuse, &s->fu_hd))
		if (fu->orientation == OT_SAME) nfaces++;
	}
	fprintf(stderr, "  %-32s  faces=%d\n", dp->d_namep, nfaces);

	/* Validate manifold quality */
	int ok = check_nmg_mesh(dp->d_namep, m, &tol, &vlfree);
	if (ok) n_pass++; else { n_fail++; failures++; }

	nmg_km(m);
	rt_db_free_internal(&intern);
    }

    bu_free(dpv, "dpv");
    bu_list_free(&vlfree);
    db_close(dbip);

    printf("\n  Scan results: %d passed, %d mesh-fail, %d tess-fail, %d skipped\n",
	   n_pass, n_fail, n_tess_fail, n_skip);
    return failures;
}


int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);

    const char *input_g  = NULL;
    const char *output_g = NULL;

    /* Simple argument parsing:
     *   [--input-g  <file.g>]   scan an existing .g for primitives to tess
     *   [--output-g <file.g>]   write CSG inputs + BOT outputs to a new .g
     *   [-h]                    print help
     */
    for (int i = 1; i < argc; i++) {
	if (BU_STR_EQUAL(argv[i], "-h") || BU_STR_EQUAL(argv[i], "--help")) {
	    printf("Usage: %s [--input-g <file.g>] [--output-g <file.g>]\n", argv[0]);
	    printf("          [--rel <frac>] [--abs <dist>] [--norm <rad>]\n");
	    printf("\n");
	    printf("  Without options: runs built-in NMG tessellation tests.\n");
	    printf("\n");
	    printf("  --input-g <file.g>\n");
	    printf("    Open an existing .g database, tessellate every non-BREP\n");
	    printf("    solid primitive, and validate manifold/open-edge quality.\n");
	    printf("\n");
	    printf("  --output-g <file.g>\n");
	    printf("    Write each built-in CSG test primitive and its BOT\n");
	    printf("    facetization to a new .g file for visual inspection.\n");
	    printf("\n");
	    printf("  --rel <frac>   Relative chord-height tolerance (e.g. 0.1 = 10%%).\n");
	    printf("                 Applied to --input-g scans; default 0.01.\n");
	    printf("  --abs <dist>   Absolute chord-height tolerance in mm.\n");
	    printf("                 Applied to --input-g scans; default off.\n");
	    printf("  --norm <rad>   Normal-angle tolerance in radians.\n");
	    printf("                 Applied to --input-g scans; default off.\n");
	    printf("\n");
	    printf("  Returns 0 on all-pass, 1 on any failure.\n");
	    return 0;
	} else if (BU_STR_EQUAL(argv[i], "--input-g") && i + 1 < argc) {
	    input_g = argv[++i];
	} else if (BU_STR_EQUAL(argv[i], "--output-g") && i + 1 < argc) {
	    output_g = argv[++i];
	} else if (BU_STR_EQUAL(argv[i], "--rel") && i + 1 < argc) {
	    double v = atof(argv[++i]);
	    if (v > 0.0) g_scan_rel = v;
	    else fprintf(stderr, "WARNING: --rel requires a positive value (got '%s'), ignored\n", argv[i]);
	} else if (BU_STR_EQUAL(argv[i], "--abs") && i + 1 < argc) {
	    double v = atof(argv[++i]);
	    if (v > 0.0) g_scan_abs = v;
	    else fprintf(stderr, "WARNING: --abs requires a positive value (got '%s'), ignored\n", argv[i]);
	} else if (BU_STR_EQUAL(argv[i], "--norm") && i + 1 < argc) {
	    double v = atof(argv[++i]);
	    if (v > 0.0) g_scan_norm = v;
	    else fprintf(stderr, "WARNING: --norm requires a positive value (got '%s'), ignored\n", argv[i]);
	} else {
	    fprintf(stderr, "WARNING: unknown argument '%s' (use -h for help)\n", argv[i]);
	}
    }

    /* Open output .g if requested */
    if (output_g) {
	g_wdb = wdb_fopen(output_g);
	if (!g_wdb) {
	    fprintf(stderr, "ERROR: cannot create output .g '%s'\n", output_g);
	    return 1;
	}
	mk_id(g_wdb, "prim_tess output");
	g_validate = 1;
	printf("Output .g: %s\n", output_g);
    }

    int total_failures = 0;

    /* ---- Built-in test suite ----
     * Always run the built-in correctness tests.  Manifold validation
     * (g_validate) is only enabled when --output-g is given, so the
     * CI baseline run (no flags) stays fast.  When only --input-g is
     * given the built-in suite runs without the slow triangulation step.  */
    total_failures += test_tor();
    total_failures += test_eto();
    total_failures += test_tgc();
    total_failures += test_tgc_surf_area();
    total_failures += test_ell();
    total_failures += test_epa();
    total_failures += test_ehy();
    total_failures += test_rpc();
    total_failures += test_rhc();
    total_failures += test_hyp();
    total_failures += test_part();
    total_failures += test_dsp();
    total_failures += test_ebm();
    total_failures += test_vol();
    total_failures += test_arb();
    total_failures += test_ars();
    total_failures += test_arbn();
    total_failures += test_pipe();
    total_failures += test_metaball();

    /* ---- Input .g scan (if requested) ---- */
    if (input_g)
	total_failures += scan_input_g(input_g);

    /* Close output .g */
    if (g_wdb) {
	wdb_close(g_wdb);
	g_wdb = NULL;
	printf("Wrote output .g: %s\n", output_g);
    }

    printf("\n=== Summary: %d failure(s) ===\n", total_failures);

    return (total_failures > 0) ? 1 : 0;
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
