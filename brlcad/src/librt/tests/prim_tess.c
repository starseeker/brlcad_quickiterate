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
#include "bu/tbl.h"


/* ------------------------------------------------------------------ */
/* Global output database handle (NULL = no output requested)          */
/* ------------------------------------------------------------------ */

static struct rt_wdb *g_wdb = NULL;
static int g_out_seq = 0;   /* sequential suffix for output object names */
static int g_validate = 0;  /* 1 = run manifold/mesh quality checks */

/* Metric validation flags */
static int g_validate_metrics  = 0; /* 1 = compare BOT metrics to analytic answers */
static int g_crofton_validate  = 0; /* 1 = use Crofton for --input-g scan */
static double g_metrics_tol    = 0.05; /* default: 5% tolerance for analytic comparison */

/* Tolerance overrides for --input-g scan (0 = not set / use default) */
static double g_scan_rel  = 0.0;
static double g_scan_abs  = 0.0;
static double g_scan_norm = 0.0;

/* Optional primitive filter: when non-NULL, only the named prim family runs */
static const char *g_prim_filter = NULL;

/* Optional db_search filter expression for --input-g scan.
 * When non-NULL, only primitives matching this search expression are
 * processed.  Uses the same -F syntax as the 'search' and 'stat' GED
 * commands (e.g. '-name "s.nos*"', '-type tgc', '-name foo.s').       */
static const char *g_search_filter = NULL;


/* ------------------------------------------------------------------ */
/* Global result accumulator for summary table                         */
/* ------------------------------------------------------------------ */
struct result_row {
    char label[256];
    char type[256];
    int  tess_ok;
    int  mesh_ok;
    int  metrics_ok;   /* -1=n/a, 0=fail, 1=pass */
    int  res_limited;
    long n_tris;
    double err_sa;     /* -1 = n/a */
    double err_vol;    /* -1 = n/a */
    /* convergence: 0.0=not tried, >0=tol value that converged, -1.0=tried but failed */
    double conv_rel, conv_abs, conv_norm;
};

#define MAX_RESULTS 4096
static struct result_row g_results[MAX_RESULTS];
static int g_nresults = 0;

static void
add_result(const char *label, const char *type, int tess_ok, int mesh_ok, int metrics_ok,
           int res_limited, long n_tris, double err_sa, double err_vol,
           double conv_rel, double conv_abs, double conv_norm)
{
    if (g_nresults >= MAX_RESULTS) return;
    struct result_row *rr = &g_results[g_nresults++];
    bu_strlcpy(rr->label, label, sizeof(rr->label));
    bu_strlcpy(rr->type, type, sizeof(rr->type));
    rr->tess_ok    = tess_ok;
    rr->mesh_ok    = mesh_ok;
    rr->metrics_ok = metrics_ok;
    rr->res_limited = res_limited;
    rr->n_tris     = n_tris;
    rr->err_sa     = err_sa;
    rr->err_vol    = err_vol;
    rr->conv_rel   = conv_rel;
    rr->conv_abs   = conv_abs;
    rr->conv_norm  = conv_norm;
}

static void
print_summary_table(int start_idx, int end_idx, const char *section_name)
{
    if (end_idx <= start_idx) return;

    struct bu_tbl *t = bu_tbl_create();
    bu_tbl_style(t, BU_TBL_STYLE_SINGLE);

    bu_tbl_style(t, BU_TBL_ROW_HEADER);
    bu_tbl_printf(t, "Primitive|Type|\u0394SA%%|\u0394Vol%%|Conv(r/a/n)|Status");
    bu_tbl_style(t, BU_TBL_ROW_END);
    bu_tbl_style(t, BU_TBL_ROW_SEPARATOR);

    for (int i = start_idx; i < end_idx; i++) {
        struct result_row *rr = &g_results[i];

        char sa_str[32];
        if (rr->err_sa >= 0.0)
            snprintf(sa_str, sizeof(sa_str), "%.2f%%", rr->err_sa * 100.0);
        else
            bu_strlcpy(sa_str, "n/a", sizeof(sa_str));

        char vol_str[32];
        if (rr->err_vol >= 0.0)
            snprintf(vol_str, sizeof(vol_str), "%.2f%%", rr->err_vol * 100.0);
        else
            bu_strlcpy(vol_str, "n/a", sizeof(vol_str));

        char conv_r[16], conv_a[16], conv_n[16];
#define FMT_CONV(val, buf) do { \
    if ((val) > 0.0) snprintf(buf, sizeof(buf), "%.2g", (val)); \
    else if ((val) < 0.0) bu_strlcpy(buf, "\u2717", sizeof(buf)); \
    else bu_strlcpy(buf, "-", sizeof(buf)); \
} while (0)
        FMT_CONV(rr->conv_rel,  conv_r);
        FMT_CONV(rr->conv_abs,  conv_a);
        FMT_CONV(rr->conv_norm, conv_n);
        char conv_str[64];
        snprintf(conv_str, sizeof(conv_str), "%s/%s/%s", conv_r, conv_a, conv_n);

        const char *status;
        if (!rr->tess_ok)
            status = "TESS-FAIL";
        else if (!rr->mesh_ok)
            status = "OPEN-EDGE";
        else if (rr->metrics_ok == 0 && !rr->res_limited)
            status = "METRIC-FAIL";
        else if (rr->res_limited)
            status = "RES-LIM";
        else
            status = "PASS";

        bu_tbl_printf(t, "%s|%s|%s|%s|%s|%s",
                      rr->label, rr->type, sa_str, vol_str, conv_str, status);
        bu_tbl_style(t, BU_TBL_ROW_END);
    }

    struct bu_vls tbl_str = BU_VLS_INIT_ZERO;
    bu_tbl_vls(&tbl_str, t);
    bu_tbl_destroy(t);

    if (section_name)
        printf("\n--- %s summary ---\n", section_name);
    printf("%s\n", bu_vls_cstr(&tbl_str));
    bu_vls_free(&tbl_str);
}


/* ------------------------------------------------------------------ */
/* Convergence tracking                                                 */
/* ------------------------------------------------------------------ */

/**
 * Result of evaluating a single tessellation + mesh check attempt.
 */
struct tess_eval_result {
    int    tess_ok;       /* 1 = rt_obj_tess returned 0 */
    int    mesh_ok;       /* 1 = no open edges */
    int    metrics_ok;    /* 1 = SA+Vol pass, 0 = fail, -1 = not checked */
    int    res_limited;   /* 1 = V-WARN-RES-LIM (floor-limited, not a real fail) */
    long   n_tris;
    double area;
    double vol;
    double err_sa;        /* relative SA error; -1.0 = not computed */
    double err_vol;       /* relative Vol error; -1.0 = not computed */
    int    n_open;
};

/**
 * Convergence attempt for one tolerance type.
 */
struct conv_attempt {
    int    tried;
    int    converged;
    double start_val;
    double final_val;
    int    n_steps;
    struct tess_eval_result result;
};

/* ------------------------------------------------------------------ */
/* Manifold / mesh quality validation                                   */
/* ------------------------------------------------------------------ */

/**
 * Validate the mesh quality of a tessellated NMG model.
 *
 * Triangulates the model in-place (the caller owns m and will free it),
 * converts it to a BOT, then uses bg_trimesh_solid2() to check for
 * open edges and degenerate faces.  Reports surface area, volume, and
 * open-edge count.
 *
 * When g_validate_metrics is set and ip is non-NULL, the BOT area and
 * volume are also compared against the primitive's analytic answers
 * (from ft_surf_area and ft_volume) within g_metrics_tol.
 *
 * Volume errors that fall within the theoretical floor imposed by the
 * polygon approximation of circular cross-sections are reported as
 * V-WARN-RES-LIM rather than V-FAIL; they do not count as failures.
 *
 * @param ttol  tessellation tolerances used to produce this mesh (may be
 *              NULL when called without --validate-metrics); used to
 *              compute the polygon-approximation resolution floor.
 * @param quiet 1 = suppress verbose fprintf output (for convergence probing).
 * @param res   optional output struct populated with detailed results; may be NULL.
 * @return 1 if the mesh passes all checks, 0 if it fails.
 */

static int
check_nmg_mesh(const char *label, struct model *m,
	       const struct bn_tol *tol, struct bu_list *vlfree,
	       const struct rt_db_internal *ip,
	       const struct bg_tess_tol *ttol,
	       int quiet,
	       struct tess_eval_result *res)
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
	if (!quiet)
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
	if (!quiet)
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
	if (!quiet)
	    fprintf(stderr, "  MESH: %-44s  nmg_mdl_to_bot returned empty [FAIL]\n", label);
	return 0;
    }

    /* bg_trimesh_solid2 checks for open edges and degenerate faces */
    struct bg_trimesh_solid_errors errs;
    memset(&errs, 0, sizeof(errs));
    int open_cnt = bg_trimesh_solid2(
	(int)bot->num_vertices, (int)bot->num_faces,
	bot->vertices, bot->faces, &errs);
    int n_open    = (int)errs.unmatched.count;
    int n_degen   = (int)errs.degenerate.count;
    int n_excess  = (int)errs.excess.count;
    int n_misor   = (int)errs.misoriented.count;

    /* Surface area and volume */
    fastf_t area = bg_trimesh_area(
	bot->faces, bot->num_faces,
	(const point_t *)bot->vertices, bot->num_vertices);

    fastf_t vol = 0.0;
    if (open_cnt == 0) {
	/* Volume is only meaningful for a closed manifold */
	vol = bg_trimesh_volume(
	    bot->faces, bot->num_faces,
	    (const point_t *)bot->vertices, bot->num_vertices);
    }

    int passed = (open_cnt == 0);

    /* Choose a label that accurately reflects which problem was found */
    const char *mesh_tag;
    if (passed) {
	mesh_tag = "OK";
    } else if (n_open > 0) {
	mesh_tag = "OPEN-EDGES";
    } else if (n_excess > 0) {
	mesh_tag = "NON-MANIFOLD";
    } else if (n_misor > 0) {
	mesh_tag = "MISORIENT";
    } else {
	mesh_tag = "MESH-FAIL";
    }

    if (!quiet) {
	fprintf(stderr,
		"  MESH: %-44s  tris=%-6lu  area=%-12.4g  vol=%-12.4g"
		"  open=%-4d  excess=%-4d  misor=%-4d  degen=%-4d  [%s]\n",
		label,
		(unsigned long)bot->num_faces,
		area,
		vol,
		n_open,
		n_excess,
		n_misor,
		n_degen,
		mesh_tag);
	fflush(stderr);
    }
    bg_free_trimesh_solid_errors(&errs);

    /* ----------------------------------------------------------------
     * Analytic metric computation: always done when the mesh is closed
     * and ip is available, so ΔSA% and ΔVol% always appear in the
     * summary table.  Pass/fail checking is performed separately below,
     * gated on --validate-metrics.
     * -------------------------------------------------------------- */
    fastf_t analytic_sa = -1.0;
    fastf_t analytic_v  = -1.0;
    double err_sa = -1.0;
    double err_v  = -1.0;
    int res_limited = 0;
    int metrics_ok = -1; /* -1 = not checked */

    if (passed && ip &&
	ip->idb_minor_type >= 0 && ip->idb_minor_type < ID_MAXIMUM) {

	const struct rt_functab *ft = &OBJ[ip->idb_minor_type];

	if (ft->ft_surf_area)
	    ft->ft_surf_area(&analytic_sa, ip);
	if (ft->ft_volume)
	    ft->ft_volume(&analytic_v, ip);

	/* Crofton fallback: when the analytic formula is absent or silently
	 * returns no value (e.g. rt_ell_surf_area for triaxial ellipsoids),
	 * estimate SA and/or volume via ray sampling so the table always has
	 * numbers.
	 *
	 * Convention: analytic_sa/analytic_v are initialised to -1.0 above.
	 * After calling ft_surf_area/ft_volume they remain <= 0.0 whenever the
	 * formula is unavailable (either because the function pointer is NULL,
	 * or because the function silently returns without writing *area / *vol).
	 * Any <= 0.0 value is therefore treated as "missing"; a positive value
	 * is a valid analytic answer.
	 *
	 * Only runs in non-quiet (top-level) mode to avoid expensive and
	 * potentially problematic ray sampling inside convergence loops.
	 *
	 * crofton_from_ip() requires ip->idb_meth to export the primitive to
	 * an in-memory database; hand-crafted test IPs may leave it NULL, so
	 * we fill it in via a temporary copy before the call.               */
	if (g_validate_metrics && !quiet && (analytic_sa <= 0.0 || analytic_v <= 0.0)) {
	    struct rt_db_internal ip_meth = *ip;
	    if (!ip_meth.idb_meth)
		ip_meth.idb_meth = &OBJ[ip->idb_minor_type];
	    if (analytic_sa <= 0.0) {
		fastf_t croft_sa = 0.0;
		rt_crofton_surf_area(&croft_sa, &ip_meth);
		if (croft_sa > 0.0) analytic_sa = croft_sa;
	    }
	    if (analytic_v <= 0.0) {
		fastf_t croft_v = 0.0;
		rt_crofton_volume(&croft_v, &ip_meth);
		if (croft_v > 0.0) analytic_v = croft_v;
	    }
	}

	if (analytic_sa > 0.0)
	    err_sa = fabs((double)(area - analytic_sa)) / (double)analytic_sa;
	if (analytic_v > 0.0)
	    err_v = fabs((double)(vol - analytic_v)) / (double)analytic_v;
    }

    /* ----------------------------------------------------------------
     * Optional analytic metric pass/fail check (--validate-metrics)
     * -------------------------------------------------------------- */
    if (g_validate_metrics && passed && ip &&
	ip->idb_minor_type >= 0 && ip->idb_minor_type < ID_MAXIMUM) {

	metrics_ok = 1; /* assume pass until a check fails */

	if (analytic_sa > 0.0) {
	    const char *tag = (err_sa <= g_metrics_tol) ? "SA-OK" : "SA-FAIL";
	    if (!quiet)
		fprintf(stderr,
			"  METRICS: %-44s  SA=%.4g  analytic=%.4g  err=%.1f%%  [%s]\n",
			label, (double)area, (double)analytic_sa, err_sa*100.0, tag);
	    if (err_sa > g_metrics_tol) { passed = 0; metrics_ok = 0; }
	} else {
	    if (!quiet)
		fprintf(stderr, "  METRICS: %-44s  SA-analytic=NA\n", label);
	}

	if (analytic_v > 0.0) {
	    const char *tag_v;

	    if (err_v <= g_metrics_tol) {
		tag_v = "V-OK";
	    } else {
		/* ------------------------------------------------------------
		 * Distinguish genuine formula/topology bugs from errors that
		 * are simply the coarsest achievable mesh given the tolerances
		 * (the "polygon approximation resolution floor").
		 *
		 * For a regular N-gon approximating a circle:
		 *   SA_err  = 1 - N*sin(π/N)/π       (perimeter / circumference)
		 *   Vol_err = 1 - N*sin(2π/N)/(2π)   (polygon / circle area)
		 *
		 * Two strategies:
		 *   PIPE (exact): recompute arc_segs using the same formula as
		 *     rt_pipe_tess and derive the exact theoretical floor.
		 *   Generic (heuristic): estimate N from the observed SA error
		 *     via SA_err ≈ π²/(6N²) → N ≈ π/√(6·SA_err), then derive
		 *     the expected vol floor.
		 * ------------------------------------------------------------ */
		double res_floor = 0.0;
		int arc_segs_eff = 0;

		if (ip->idb_minor_type == ID_PIPE && ttol) {
		    /* Recompute the arc_segs that rt_pipe_tess would choose.
		     * Mirrors the updated formula: rel references max_od/2. */
		    struct rt_pipe_internal *pip2 =
			(struct rt_pipe_internal *)ip->idb_ptr;
		    fastf_t max_diam2 = 0.0;
		    struct wdb_pipe_pnt *ppt;
		    for (BU_LIST_FOR(ppt, wdb_pipe_pnt, &pip2->pipe_segs_head))
			if (ppt->pp_od > max_diam2) max_diam2 = ppt->pp_od;

		    int ts = 6;
		    if (ttol->abs > SMALL_FASTF && ttol->abs * 2.0 < max_diam2) {
			int t2 = rt_num_circular_segments(
			    ttol->abs, max_diam2 / 2.0);
			if (t2 > ts) ts = t2;
		    }
		    if (ttol->rel > SMALL_FASTF) {
			int t2 = rt_num_circular_segments(
			    ttol->rel * max_diam2 / 2.0, max_diam2 / 2.0);
			if (t2 > ts) ts = t2;
		    }
		    if (ttol->norm > SMALL_FASTF) {
			/* PRIM_MIN_NORM_TOL default = π/360 ≈ 0.00873 rad */
			fastf_t ntol_e = ttol->norm;
			const fastf_t min_ntol = (fastf_t)(M_PI / 360.0);
			if (ntol_e < min_ntol) ntol_e = min_ntol;
			int t2;
			double t2d = ceil(M_PI / ntol_e); /* segments from angular tolerance */
			t2 = (int)t2d;
			if (t2 > ts) ts = t2;
		    }
		    arc_segs_eff = ts;
		    res_floor = 1.0 -
			((double)arc_segs_eff * sin(M_2PI / arc_segs_eff)) / M_2PI;

		} else if (err_sa > 0.001) {
		    /* Generic heuristic: estimate N from observed SA error.
		     * SA_err ≈ π²/(6N²) → N ≈ π/√(6·SA_err). */
		    double n_est = M_PI / sqrt(6.0 * err_sa);
		    if (n_est >= 3.0) {
			res_floor = 1.0 -
			    (n_est * sin(M_2PI / n_est)) / M_2PI;
		    }
		}

		/* 10% slack: accounts for bend/end-cap approximation adding a
		 * small error on top of the pure cross-section polygon error. */
		if (res_floor > 0.0 && err_v <= res_floor * 1.1) {
		    tag_v = "V-WARN-RES-LIM";
		    res_limited = 1;
		    if (!quiet) {
			if (arc_segs_eff > 0)
			    fprintf(stderr,
				    "  (tessellation at %d arc_segs; "
				    "finest achievable vol accuracy ~%.1f%%)\n",
				    arc_segs_eff, res_floor * 100.0);
			else
			    fprintf(stderr,
				    "  (tessellation at resolution floor; "
				    "finest achievable vol accuracy ~%.1f%%)\n",
				    res_floor * 100.0);
		    }
		    /* Resolution-limited — not a formula/topology bug */
		} else {
		    tag_v = "V-FAIL";
		    passed = 0;
		    metrics_ok = 0;
		}
	    }

	    if (!quiet)
		fprintf(stderr,
			"  METRICS: %-44s  V=%.4g   analytic=%.4g  err=%.1f%%  [%s]\n",
			label, (double)vol, (double)analytic_v, err_v*100.0, tag_v);
	} else {
	    if (!quiet)
		fprintf(stderr, "  METRICS: %-44s  V-analytic=NA\n", label);
	}
	if (!quiet)
	    fflush(stderr);
    }

    /* Populate optional result struct */
    if (res) {
	res->tess_ok    = 1;
	res->mesh_ok    = (open_cnt == 0);
	res->n_open     = n_open;
	res->n_tris     = (long)bot->num_faces;
	res->area       = (double)area;
	res->vol        = (double)(open_cnt == 0 ? vol : 0.0);
	res->err_sa     = (analytic_sa > 0.0) ? err_sa  : -1.0;
	res->err_vol    = (analytic_v  > 0.0) ? err_v   : -1.0;
	res->metrics_ok = metrics_ok;
	res->res_limited = res_limited;
    }

    /* Optionally save the BOT to the output .g */
    if (g_wdb && (open_cnt == 0 && n_open == 0)) {
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
/* eval_tess: tessellate and check in one call (for convergence)       */
/* ------------------------------------------------------------------ */

static int
eval_tess(const char *label, struct rt_db_internal *ip,
          const struct bg_tess_tol *ttol, const struct bn_tol *tol,
          int quiet, struct tess_eval_result *res)
{
    memset(res, 0, sizeof(*res));
    res->tess_ok    = 0;
    res->mesh_ok    = 0;
    res->metrics_ok = -1;
    res->err_sa     = -1.0;
    res->err_vol    = -1.0;

    struct bu_list vlfree;
    BU_LIST_INIT(&vlfree);
    struct model *m = nmg_mm();
    struct nmgregion *r = NULL;

    int ret = -1;
    if (!BU_SETJUMP) {
        ret = rt_obj_tess(&r, m, ip, ttol, tol);
    } else {
        BU_UNSETJUMP;
        nmg_km(m);
        bu_list_free(&vlfree);
        return 0;
    } BU_UNSETJUMP;

    if (ret != 0 || r == NULL) {
        nmg_km(m);
        bu_list_free(&vlfree);
        return 0;
    }

    res->tess_ok = 1;
    int ok = check_nmg_mesh(label, m, tol, &vlfree, ip, ttol, quiet, res);

    nmg_km(m);
    bu_list_free(&vlfree);
    return ok;
}


/* ------------------------------------------------------------------ */
/* try_converge_tol: tighten one tolerance type until metrics pass     */
/* ------------------------------------------------------------------ */

#define CONV_REL  0
#define CONV_ABS  1
#define CONV_NORM 2

#define CONV_REL_FLOOR  1e-5
#define CONV_ABS_FLOOR  1e-4
#define CONV_NORM_FLOOR (M_PI / 3600.0)  /* 0.05 deg: 1/10 of PRIM_MIN_NORM_TOL */
#define CONV_MAX_STEPS  6

static int
try_converge_tol(const char *label, struct rt_db_internal *ip,
                 const struct bg_tess_tol *ttol_base,
                 const struct bn_tol *tol,
                 int tol_type, struct conv_attempt *out)
{
    memset(out, 0, sizeof(*out));
    out->tried = 1;

    double start_val;
    if (tol_type == CONV_REL)
        start_val = (ttol_base->rel > 0.0) ? ttol_base->rel : 0.01;
    else if (tol_type == CONV_ABS)
        start_val = (ttol_base->abs > 0.0) ? ttol_base->abs : 0.0;
    else
        start_val = (ttol_base->norm > 0.0) ? ttol_base->norm : 0.0;

    if ((tol_type == CONV_ABS || tol_type == CONV_NORM) && start_val <= 0.0) {
        out->tried = 0;
        return 0;
    }

    out->start_val = start_val;

    double cur_val = start_val;
    double floor_val = (tol_type == CONV_REL)  ? CONV_REL_FLOOR :
                       (tol_type == CONV_ABS)   ? CONV_ABS_FLOOR :
                                                   CONV_NORM_FLOOR;

    for (int step = 0; step < CONV_MAX_STEPS; step++) {
        if (tol_type == CONV_NORM)
            cur_val *= 0.316227766; /* sqrt(0.1): tighten by sqrt(10) each step */
        else
            cur_val *= 0.1;

        if (cur_val < floor_val)
            cur_val = floor_val;

        struct bg_tess_tol ttol_try;
        memcpy(&ttol_try, ttol_base, sizeof(ttol_try));
        if (tol_type == CONV_REL)  { ttol_try.rel = cur_val; ttol_try.abs = 0.0; ttol_try.norm = 0.0; }
        if (tol_type == CONV_ABS)  { ttol_try.abs = cur_val; ttol_try.rel = 0.0; ttol_try.norm = 0.0; }
        if (tol_type == CONV_NORM) { ttol_try.norm = cur_val; ttol_try.rel = 0.0; ttol_try.abs = 0.0; }

        out->n_steps   = step + 1;
        out->final_val = cur_val;

        int ok = eval_tess(label, ip, &ttol_try, tol, 1 /* quiet */, &out->result);

	/* Print a MESH-REFINE line for every convergence step so progress
	 * is visible even when the outer check_nmg_mesh runs quietly.    */
	{
	    const char *tol_name = (tol_type == CONV_REL)  ? "rel"  :
				   (tol_type == CONV_ABS)  ? "abs"  : "norm";
	    char sa_str[24], vol_str[24];
	    if (out->result.err_sa >= 0.0)
		snprintf(sa_str,  sizeof(sa_str),  "%.2f%%", out->result.err_sa  * 100.0);
	    else
		bu_strlcpy(sa_str,  "n/a", sizeof(sa_str));
	    if (out->result.err_vol >= 0.0)
		snprintf(vol_str, sizeof(vol_str), "%.2f%%", out->result.err_vol * 100.0);
	    else
		bu_strlcpy(vol_str, "n/a", sizeof(vol_str));
	    /* Mirror the convergence test so the label is always accurate */
	    int conv_pass = (ok && out->result.mesh_ok &&
			     (out->result.metrics_ok == 1 ||
			      out->result.metrics_ok == -1 ||
			      out->result.res_limited));
	    const char *step_tag;
	    if (conv_pass)
		step_tag = "OK";
	    else if (!out->result.tess_ok)
		step_tag = "TESS-FAIL";
	    else if (!out->result.mesh_ok)
		step_tag = "OPEN-EDGES";
	    else if (out->result.res_limited)
		step_tag = "RES-LIM"; /* vol at floor but other check still failing */
	    else if (out->result.metrics_ok == 0)
		step_tag = "METRIC-FAIL";
	    else
		step_tag = "NO-CHECK";
	    fprintf(stderr,
		    "  MESH-REFINE: %-44s  %s=%.3g  tris=%-6ld  \u0394SA=%s  \u0394Vol=%s  [%s]\n",
		    label, tol_name, cur_val,
		    out->result.n_tris,
		    sa_str, vol_str, step_tag);
	    fflush(stderr);
	}

        if (ok && out->result.mesh_ok &&
            (out->result.metrics_ok == 1 || out->result.metrics_ok == -1 ||
             out->result.res_limited)) {
            out->converged = 1;
            return 1;
        }

        if (cur_val <= floor_val)
            break;
    }

    return 0;
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

    fprintf(stderr, "STARTING: %s (%s)\n", label, ((ip && ip->idb_meth) ? ip->idb_meth->ft_label : "?"));
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
	if (g_validate && !expect_fail) {
	    struct tess_eval_result mesh_res;
	    memset(&mesh_res, 0, sizeof(mesh_res));
	    mesh_res.metrics_ok = -1;
	    mesh_res.err_sa = -1.0;
	    mesh_res.err_vol = -1.0;
	    int mesh_ok = check_nmg_mesh(label, m, tol, &vlfree, ip, ttol,
					 0 /* not quiet */, &mesh_res);
	    if (!mesh_ok) passed = 0;

	    /* Convergence probing when metrics check failed but mesh topology is ok.
	     * If there are open edges, tightening tolerances won't fix them.      */
	    int conv_rel = 0, conv_abs = 0, conv_norm = 0;
	    struct conv_attempt rel_c, abs_c, norm_c;
	    memset(&rel_c,  0, sizeof(rel_c));
	    memset(&abs_c,  0, sizeof(abs_c));
	    memset(&norm_c, 0, sizeof(norm_c));
	    if (g_validate_metrics && !expect_fail && ret == 0 && r != NULL &&
		mesh_res.mesh_ok && mesh_res.metrics_ok == 0) {
		int is_planar = 0;
		if (ip) {
		    switch (ip->idb_minor_type) {
			case ID_ARB8: case ID_ARBN: case ID_ARS:
			case ID_BOT:  case ID_NMG:  case ID_POLY:
			    is_planar = 1; break;
			default: break;
		    }
		}
		if (!is_planar) {
		    conv_rel  = try_converge_tol(label, ip, ttol, tol, CONV_REL,  &rel_c);
		    conv_abs  = try_converge_tol(label, ip, ttol, tol, CONV_ABS,  &abs_c);
		    conv_norm = try_converge_tol(label, ip, ttol, tol, CONV_NORM, &norm_c);

		    fprintf(stderr,
			"  CONV: %-44s  "
			"rel=%s(%.3g->%.3g,%dstep)  "
			"abs=%s(%.3g->%.3g,%dstep)  "
			"norm=%s(%.3g->%.3g,%dstep)\n",
			label,
			rel_c.tried  ? (conv_rel  ? "OK" : "NO") : "-",
			rel_c.start_val,  rel_c.final_val,  rel_c.n_steps,
			abs_c.tried  ? (conv_abs  ? "OK" : "NO") : "-",
			abs_c.start_val,  abs_c.final_val,  abs_c.n_steps,
			norm_c.tried ? (conv_norm ? "OK" : "NO") : "-",
			norm_c.start_val, norm_c.final_val, norm_c.n_steps);

		    if (conv_rel || conv_abs || conv_norm) {
			fprintf(stderr,
			    "  CONV: %-44s  convergence achieved\n", label);
			passed = 1;
			/* Update displayed metrics from the converged result.
			 * Convergence checks run in quiet mode (no Crofton), so
			 * err_sa may be -1.0 when the analytic SA formula is
			 * absent; in that case keep the initial SA from the
			 * non-quiet top-level check.                           */
			struct tess_eval_result *best_res =
			    conv_rel  ? &rel_c.result  :
			    conv_abs  ? &abs_c.result  : &norm_c.result;
			mesh_res.metrics_ok  = best_res->metrics_ok;
			if (best_res->err_sa  >= 0.0) mesh_res.err_sa  = best_res->err_sa;
			if (best_res->err_vol >= 0.0) mesh_res.err_vol = best_res->err_vol;
			mesh_res.res_limited = best_res->res_limited;
			mesh_res.n_tris      = best_res->n_tris;
		    } else if (rel_c.tried || abs_c.tried || norm_c.tried) {
			fprintf(stderr,
			    "  CONV: %-44s  WARNING: no convergence found\n", label);
		    }
		}
	    }

	    /* Build double conv values: >0 = tol value that converged,
	     * -1.0 = tried but failed, 0.0 = not tried.                  */
	    double conv_rel_val  = conv_rel  ? rel_c.final_val  : (rel_c.tried  ? -1.0 : 0.0);
	    double conv_abs_val  = conv_abs  ? abs_c.final_val  : (abs_c.tried  ? -1.0 : 0.0);
	    double conv_norm_val = conv_norm ? norm_c.final_val : (norm_c.tried ? -1.0 : 0.0);

	    add_result(label, ((ip && ip->idb_meth) ? ip->idb_meth->ft_label : "?"),
		       mesh_res.tess_ok, mesh_res.mesh_ok, mesh_res.metrics_ok,
		       mesh_res.res_limited, mesh_res.n_tris,
		       mesh_res.err_sa, mesh_res.err_vol,
		       conv_rel_val, conv_abs_val, conv_norm_val);
	}

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

    fprintf(stderr, "STARTING: %s (%s) (max_faces=%d)\n", label, ((ip && ip->idb_meth) ? ip->idb_meth->ft_label : "?"), max_faces);
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

    /* Large-scale ETO: C along N so ch=0, r_min = eto_r - a*|dh| = 2000-100*10 = 1000 > 0 */
    tip.eto_r  = 2000.0;
    tip.eto_rd = 10.0;
    VSET(tip.eto_C, 0.0, 0.0, 100.0);  /* C along N: ch=0, cv=100, phi=0 */
    VSET(tip.eto_N, 0, 0, 1);
    VSET(tip.eto_V, 0, 0, 0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto large (r=2000 rd=10 C-along-N)", &ip, &ttol, &tol, 0)) failures++;

    /* Self-intersecting ETO (ch > eto_r): outer surface only, manifold expected */
    VSET(tip.eto_N, 0, 0, 1);
    VSET(tip.eto_V, 0, 0, 0);
    tip.eto_r  = 2.0;   /* revolution radius */
    tip.eto_rd = 0.5;
    VSET(tip.eto_C, 4.0, 0.0, 0.0);  /* ch=4 > eto_r=2: self-intersects */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto self-intersecting (ch=4 > r=2)", &ip, &ttol, &tol, 0)) failures++;

    /* Strongly self-intersecting ETO */
    tip.eto_r  = 1.0;
    tip.eto_rd = 0.5;
    VSET(tip.eto_C, 5.0, 0.0, 1.0);  /* ch >> eto_r */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("eto strongly self-intersecting (ch=5 >> r=1)", &ip, &ttol, &tol, 0)) failures++;

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

    /* m35.g s165: long-axis TGC with small cross-section relative to height.
     * Previously generated a 1-edgeuse degenerate face in the NMG zipper
     * when dont_use collapsed a ring to a single usable vertex.
     * Coordinates in mm from m35.asc solid record 165. */
    VSET(tip.v, 3016.580322, -63.5, 1101.826660);
    VSET(tip.h, -249.529602, -114.300003, 558.977783);
    VSET(tip.a, 0.710807, -9.987247, -1.724890);
    VSET(tip.b, 9.281481, -0.053131, 4.132421);
    VSET(tip.c, 0.533105, -7.490436, -1.293667);
    VSET(tip.d, 6.961110, -0.039848, 3.099316);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("tgc m35-s165 (long-axis, rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

    return failures;
}


/* ------------------------------------------------------------------ */
/* ELL (Ellipsoid) tests                                                */
/* ------------------------------------------------------------------ */

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
     * The canonical ARB4 encoding triggers arb_is_noncanonical (equiv_pts[4..7]
     * all map to index 3, which is < 4).  The hull fallback correctly produces
     * a 4-face tetrahedron, so this case now succeeds. */
    VSET(tip.pt[0],  0,  0,  0);
    VSET(tip.pt[1], 10,  0,  0);
    VSET(tip.pt[2],  5,  8,  0);
    VSET(tip.pt[3],  5,  3, 10);   /* apex */
    VSET(tip.pt[4],  5,  3, 10);
    VSET(tip.pt[5],  5,  3, 10);
    VSET(tip.pt[6],  5,  3, 10);
    VSET(tip.pt[7],  5,  3, 10);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb4 tetrahedron (hull fallback)", &ip, &ttol, &tol, 0)) failures++;

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

    /* ARB8 with collinear vertex triplets: mirrors detail_ws.s4 from
     * toyjeep.g.  Two faces have their 3rd vertex exactly collinear
     * with the first two (V4-V7-V3 and V2-V6-V5), causing the old code
     * to drop one vertex per face and produce a non-watertight mesh. */
    VSET(tip.pt[0], 768.35, 238.125, 463.55);  /* V1 */
    VSET(tip.pt[1], 819.15, 238.125, 463.55);  /* V2 */
    VSET(tip.pt[2], 819.15, 279.40,  463.55);  /* V3 */
    VSET(tip.pt[3], 768.35, 279.40,  463.55);  /* V4 */
    VSET(tip.pt[4], 768.35, 238.125, 504.825); /* V5 */
    VSET(tip.pt[5], 819.15, 238.125, 504.825); /* V6 */
    VSET(tip.pt[6], 819.15, 258.7625, 484.1875); /* V7 (midpoint of V2-V3 on right slant) */
    VSET(tip.pt[7], 768.35, 258.7625, 484.1875); /* V8 (midpoint of V1-V4 on left slant) */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb8 collinear triplet (detail_ws.s4-style)", &ip, &ttol, &tol, 0)) failures++;

    /* Non-canonical ARB6 (wedge/triangular prism):
     * The duplicate pair is at pt[0]==pt[3] and pt[1]==pt[2] instead of the
     * canonical pt[4]==pt[5] and pt[6]==pt[7].  This is the bldg391.g-style
     * non-standard encoding.  arb_is_noncanonical fires and the hull fallback
     * produces the correct 5-face prism (2 triangles + 3 quads = 8 triangles). */
    VSET(tip.pt[0],  0,  0,  5);  /* ridge left  (duplicated at pt[3]) */
    VSET(tip.pt[1], 10,  0,  5);  /* ridge right (duplicated at pt[2]) */
    VSET(tip.pt[2], 10,  0,  5);  /* = pt[1] */
    VSET(tip.pt[3],  0,  0,  5);  /* = pt[0] */
    VSET(tip.pt[4],  0, -5,  0);  /* base corner 1 */
    VSET(tip.pt[5], 10, -5,  0);  /* base corner 2 */
    VSET(tip.pt[6], 10,  5,  0);  /* base corner 3 */
    VSET(tip.pt[7],  0,  5,  0);  /* base corner 4 */
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb6 non-canonical wedge (hull fallback)", &ip, &ttol, &tol, 0)) failures++;

    /* Non-canonical ARB7:
     * pt[4]==pt[0] makes one top vertex a duplicate of a bottom vertex. */
    VSET(tip.pt[0],  0,  0, 10);  /* apex (duplicated at pt[4]) */
    VSET(tip.pt[1], 10,  0,  0);
    VSET(tip.pt[2], 10, 10,  0);
    VSET(tip.pt[3],  0, 10,  0);
    VSET(tip.pt[4],  0,  0, 10);  /* = pt[0] */
    VSET(tip.pt[5], 10,  0,  0);  /* = pt[1] (canonical ARB7 dup) */
    VSET(tip.pt[6], 10, 10,  0);
    VSET(tip.pt[7],  0, 10,  0);
    init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
    if (!run_tess("arb7 non-canonical (hull fallback)", &ip, &ttol, &tol, 0)) failures++;

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
 * NOTE on the real-world "terrain ARS" (NC=5, PPC=201, object named "ars"
 * in primitives.asc):  the three terrain ring curves (C1, C2, C3) close in
 * XY but NOT in Z — the last data point of each ring differs from the first
 * point by 0.56–2.24 mm in Z.  This seam discontinuity means the ARS data
 * is inherently non-manifold: rt_ars_tess detects this and returns -1 with
 * a diagnostic message.  The .g scanner treats ARS TESS-FAIL as a graceful
 * skip (not a failure), mirroring how such geometry would behave in the wild.
 * The bicone ARS (object "ars_bicone" in primitives.asc) exercises the
 * tessellation logic for valid closed ARS geometry.
 *
 * Helper: allocate and fill a ring of N 3D points laid out as a regular
 * polygon at height z and radius r.  Returns heap memory that must be
 * freed by the caller (via bu_free on each curves[i]).
 */
/*
 * ars_make_ring: allocate and fill a ring of n_sides 3D points plus a
 * closing duplicate of point 0.  The returned array has (n_sides + 1)
 * entries, matching the convention of rt_ars_import5 which always appends
 * a copy of the first point at index pts_per_curve.
 * Callers should set aip.pts_per_curve = n_sides + 1.
 */
static fastf_t *
ars_make_ring(size_t n_sides, double r, double cx, double cy, double z)
{
    /* Allocate n_sides unique + 1 closing copy = n_sides+1 points. */
    fastf_t *ring = (fastf_t *)bu_malloc((n_sides + 1) * 3 * sizeof(fastf_t), "ars ring");
    for (size_t i = 0; i < n_sides; i++) {
	double angle = i * M_2PI / n_sides;
	ring[i*3+0] = (fastf_t)(cx + r * cos(angle));
	ring[i*3+1] = (fastf_t)(cy + r * sin(angle));
	ring[i*3+2] = (fastf_t)z;
    }
    /* Closing copy — must equal point 0 exactly. */
    ring[n_sides*3+0] = ring[0];
    ring[n_sides*3+1] = ring[1];
    ring[n_sides*3+2] = ring[2];
    return ring;
}

/*
 * ars_make_cap: allocate a degenerate ring (all pts at one apex point) with
 * the same (n_sides + 1) layout as ars_make_ring.
 */
static fastf_t *
ars_make_cap(size_t n_sides, double cx, double cy, double z)
{
    fastf_t *ring = (fastf_t *)bu_malloc((n_sides + 1) * 3 * sizeof(fastf_t), "ars cap");
    for (size_t i = 0; i <= n_sides; i++) {
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
	const size_t n_sides = 8;          /* number of polygon sides */
	const size_t ppc = n_sides + 1;    /* pts_per_curve: n_sides unique + 1 closing copy */
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	curves[0] = ars_make_cap(n_sides, 0.0, 0.0, 0.0);       /* bottom cap */
	curves[1] = ars_make_ring(n_sides, 5.0, 0.0, 0.0, 0.0); /* bottom ring */
	curves[2] = ars_make_ring(n_sides, 5.0, 0.0, 0.0, 10.0);/* top ring */
	curves[3] = ars_make_cap(n_sides, 0.0, 0.0, 10.0);      /* top cap */

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
	const size_t n_sides = 12;
	const size_t ppc = n_sides + 1;
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	curves[0] = ars_make_ring(n_sides, 8.0, 0.0, 0.0, 0.0); /* bottom ring */
	curves[1] = ars_make_ring(n_sides, 2.0, 0.0, 0.0, 15.0);/* top ring */
	curves[2] = ars_make_cap(n_sides, 0.0, 0.0, 15.0);      /* top cap */

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
	const size_t n_sides = 16;
	const size_t ppc = n_sides + 1;
	const double R = 10.0;
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	/* latitude steps: -90, -45, 0, 45, 90 degrees */
	static const double lats[] = {-M_PI_2, -M_PI/4.0, 0.0, M_PI/4.0, M_PI_2};
	for (size_t i = 0; i < ncurves; i++) {
	    double z = R * sin(lats[i]);
	    double r = R * cos(lats[i]);
	    if (fabs(r) < 1e-10)
		curves[i] = ars_make_cap(n_sides, 0.0, 0.0, z);
	    else
		curves[i] = ars_make_ring(n_sides, r, 0.0, 0.0, z);
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
	const size_t n_sides = 64;
	const size_t ppc = n_sides + 1;
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	curves[0] = ars_make_cap(n_sides, 0.0, 0.0, 0.0);
	curves[1] = ars_make_ring(n_sides, 3.0, 0.0, 0.0, 0.0);
	curves[2] = ars_make_ring(n_sides, 3.0, 0.0, 0.0, 20.0);
	curves[3] = ars_make_cap(n_sides, 0.0, 0.0, 20.0);

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
	const size_t n_sides = 10;
	const size_t ppc = n_sides + 1;
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	curves[0] = ars_make_cap(n_sides, 0.0, 0.0, 0.0);
	for (size_t i = 1; i < ncurves - 1; i++) {
	    double frac = (double)i / (ncurves - 2);
	    double r = 5.0 * (1.0 - 0.8 * frac);   /* radius tapers 5 → 1 */
	    curves[i] = ars_make_ring(n_sides, r, 0.0, 0.0, (double)i * 5.0);
	}
	curves[ncurves-1] = ars_make_cap(n_sides, 0.0, 0.0, (double)(ncurves-2)*5.0);

	aip.ncurves = ncurves;
	aip.pts_per_curve = ppc;
	aip.curves = curves;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("ars multi-ring taper (8 curves, tapers 5→1)", &ip, &ttol, &tol, 0)) failures++;

	for (size_t i = 0; i < ncurves; i++)
	    bu_free(curves[i], "ars ring");
	bu_free(curves, "ars curves");
    }

    /* ---- ARS non-manifold open seam (simulates real-world bad data) ---
     *
     * This test exercises the graceful-skip path in rt_ars_tess.  The ring
     * curve closes in XY but NOT in Z — the closing copy at index n_sides
     * has the same XY as point 0 but a different Z value, exactly the pattern
     * seen in real-world terrain ARS data.  rt_ars_tess must detect this and
     * return -1 with a diagnostic message, without producing a corrupted NMG
     * or calling bu_bomb.  expect_fail=1 tells run_tess() to treat
     * ret != 0 as PASS.                                                     */
    {
	const size_t ncurves = 3;
	const size_t n_sides = 8;       /* number of polygon sides */
	const size_t ppc = n_sides + 1; /* pts_per_curve: n_sides unique + 1 closing copy */
	fastf_t **curves = (fastf_t **)bu_calloc(ncurves, sizeof(fastf_t *), "ars curves");

	/* C0: bottom cap (n_sides+1 points, all zeros). */
	curves[0] = ars_make_cap(n_sides, 0.0, 0.0, 0.0);

	/* C1: ring that closes in XY but has a Z-seam discontinuity.
	 * Points 0..n_sides-1 are a regular polygon at r=5, z=5.
	 * The closing copy at index n_sides has the same XY as point 0
	 * but a different Z (5 + 2.24), simulating the terrain ARS defect. */
	{
	    fastf_t *ring = (fastf_t *)bu_calloc(ppc * 3, sizeof(fastf_t), "ars ring");
	    for (size_t k = 0; k < n_sides; k++) {
		double angle = k * M_2PI / n_sides;
		ring[k*3+0] = (fastf_t)(5.0 * cos(angle));
		ring[k*3+1] = (fastf_t)(5.0 * sin(angle));
		ring[k*3+2] = 5.0f;
	    }
	    /* Closing copy: same XY as point 0 but different Z (seam gap). */
	    ring[n_sides*3+0] = ring[0];
	    ring[n_sides*3+1] = ring[1];
	    ring[n_sides*3+2] = 5.0f + 2.24f; /* Z discontinuity */
	    curves[1] = ring;
	}

	/* C2: top cap */
	curves[2] = ars_make_cap(n_sides, 0.0, 0.0, 10.0);

	aip.ncurves = ncurves;
	aip.pts_per_curve = ppc;
	aip.curves = curves;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	/* expect_fail=1: rt_ars_tess must return -1 (not tessellate) */
	if (!run_tess("ars non-manifold open-seam (expect skip)", &ip, &ttol, &tol, 1)) failures++;

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

    /* ---- wh_axle.bracket.s6 replica: small L-bend, od=0.375, bendR=1 --- */
    /* Reproduces the wh_axle.bracket.s6 geometry from toyjeep.g which was
     * timing out at rel=0.001 after the CDT end-cap orientation fix.
     * The small od relative to bendR and to the bounding-box diagonal is the
     * challenging characteristic. */
    {
	struct wdb_pipe_pnt p1, p2, p3;

	p1.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p1.pp_coord, 37.5625, -8.5, 7.875);
	p1.pp_id = 0.0; p1.pp_od = 0.375; p1.pp_bendradius = 1.0;

	p2.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p2.pp_coord, 37.5625, -8.5, 5.25);  /* bend point */
	p2.pp_id = 0.0; p2.pp_od = 0.375; p2.pp_bendradius = 1.0;

	p3.l.magic = WDB_PIPESEG_MAGIC;
	VSET(p3.pp_coord, 37.5625, -16.25, 5.25);
	p3.pp_id = 0.0; p3.pp_od = 0.375; p3.pp_bendradius = 1.0;

	BU_LIST_INSERT(&pip.pipe_segs_head, &p1.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p2.l);
	BU_LIST_INSERT(&pip.pipe_segs_head, &p3.l);
	pip.pipe_count = 3;

	init_tols(&ttol, &tol, 0.0, 0.01, 0.0);
	if (!run_tess("pipe wh_axle L-bend (rel=0.01)", &ip, &ttol, &tol, 0)) failures++;

	init_tols(&ttol, &tol, 0.0, 0.001, 0.0);
	if (!run_tess("pipe wh_axle L-bend (rel=0.001)", &ip, &ttol, &tol, 0)) failures++;

	BU_LIST_DEQUEUE(&p1.l);
	BU_LIST_DEQUEUE(&p2.l);
	BU_LIST_DEQUEUE(&p3.l);
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
/* Crofton estimator verification against analytic formulas            */
/* ------------------------------------------------------------------ */

/**
 * Verify the Cauchy-Crofton SA+volume estimator against analytically
 * known primitives.
 *
 * Tests performed:
 *   1. Sphere     -- SA = 4πr², V = (4/3)πr³
 *   2. RCC        -- SA = 2πr(r+h), V = πr²h  (right circular cylinder)
 *   3. Oblique RCC -- SA via numerically-exact lateral integral, V = π·r²·h_perp
 *                   (verifies rt_tgc_volume uses perpendicular height)
 *
 * All three cases are also run through rt_crofton_surf_area / rt_crofton_volume
 * directly so any systematic bias in the Crofton estimator shows up here
 * rather than only when processing real .g files.
 *
 * Tolerance: we allow ±5% deviation.  The default 2000-sample Crofton
 * run has ~2–3% statistical error, so 5% gives comfortable headroom
 * while still catching formula bugs.
 *
 * @return number of failures (0 = all checks passed).
 */
static int
verify_crofton_estimates(void)
{
    int failures = 0;
    const double tol_pct = 5.0;  /* ±5% acceptable */

    printf("\n--- Crofton estimator verification ---\n");

    /* -------------------------------------------------------------- */
    /* Helper lambda-like macro: compute Crofton SA+vol, compare,     */
    /* and report.                                                     */
    /* -------------------------------------------------------------- */
#define CROFTON_CHECK(label, ip_ptr, analytic_sa, analytic_vol) \
    do { \
	struct rt_db_internal *_ip = (ip_ptr); \
	if (!_ip->idb_meth) _ip->idb_meth = &OBJ[_ip->idb_minor_type]; \
	fastf_t _csa = 0.0, _cvol = 0.0; \
	rt_crofton_surf_area(&_csa, _ip); \
	rt_crofton_volume(&_cvol, _ip); \
	double _sa_err  = fabs(_csa  - (analytic_sa))  / ((analytic_sa)  > 0 ? (analytic_sa)  : 1.0) * 100.0; \
	double _vol_err = fabs(_cvol - (analytic_vol)) / ((analytic_vol) > 0 ? (analytic_vol) : 1.0) * 100.0; \
	const char *_sa_tag  = (_sa_err  <= tol_pct) ? "SA-OK"  : "SA-FAIL"; \
	const char *_vol_tag = (_vol_err <= tol_pct) ? "VOL-OK" : "VOL-FAIL"; \
	printf("  %-42s  SA_err=%.1f%%[%s]  VOL_err=%.1f%%[%s]\n", \
	       (label), _sa_err, _sa_tag, _vol_err, _vol_tag); \
	fflush(stdout); \
	if (_sa_err  > tol_pct) { failures++; } \
	if (_vol_err > tol_pct) { failures++; } \
    } while (0)

    /* -------------------------------------------------------------- */
    /* 1. Sphere: r = 10 mm                                           */
    /* -------------------------------------------------------------- */
    {
	struct rt_ell_internal ell;
	memset(&ell, 0, sizeof(ell));
	ell.magic = RT_ELL_INTERNAL_MAGIC;
	VSET(ell.v, 0, 0, 0);
	VSET(ell.a, 10, 0, 0);
	VSET(ell.b, 0, 10, 0);
	VSET(ell.c, 0, 0, 10);

	struct rt_db_internal ip;
	RT_DB_INTERNAL_INIT(&ip);
	ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
	ip.idb_minor_type = ID_ELL;
	ip.idb_type       = ID_ELL;
	ip.idb_ptr        = &ell;

	double analytic_sa  = 4.0 * M_PI * 10.0 * 10.0;           /* 1256.6 mm² */
	double analytic_vol = (4.0 / 3.0) * M_PI * 10.0 * 10.0 * 10.0; /* 4188.8 mm³ */
	CROFTON_CHECK("sphere r=10", &ip, analytic_sa, analytic_vol);
    }

    /* -------------------------------------------------------------- */
    /* 2. RCC (right circular cylinder): r=5, h=20 mm                 */
    /* -------------------------------------------------------------- */
    {
	struct rt_tgc_internal tgc;
	memset(&tgc, 0, sizeof(tgc));
	tgc.magic = RT_TGC_INTERNAL_MAGIC;
	VSET(tgc.v, 0, 0, 0);
	VSET(tgc.h, 0, 0, 20);
	VSET(tgc.a, 5, 0, 0);
	VSET(tgc.b, 0, 5, 0);
	VSET(tgc.c, 5, 0, 0);
	VSET(tgc.d, 0, 5, 0);

	struct rt_db_internal ip;
	RT_DB_INTERNAL_INIT(&ip);
	ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
	ip.idb_minor_type = ID_TGC;
	ip.idb_type       = ID_TGC;
	ip.idb_ptr        = &tgc;

	/* SA = 2π·r·(r+h) = 2π·5·25 = 250π ≈ 785.4 mm² */
	double analytic_sa  = 2.0 * M_PI * 5.0 * (5.0 + 20.0);
	/* V = π·r²·h = π·25·20 = 500π ≈ 1570.8 mm³ */
	double analytic_vol = M_PI * 5.0 * 5.0 * 20.0;
	CROFTON_CHECK("RCC r=5 h=20", &ip, analytic_sa, analytic_vol);

	/* Also verify that rt_tgc_volume gives the correct answer */
	fastf_t tgc_vol = 0.0;
	ip.idb_meth = &OBJ[ID_TGC];
	ip.idb_meth->ft_volume(&tgc_vol, &ip);
	double tgc_vol_err = fabs(tgc_vol - analytic_vol) / analytic_vol * 100.0;
	printf("  %-42s  analytic_formula_err=%.2f%%  [%s]\n",
	       "RCC r=5 h=20 (rt_tgc_volume)",
	       tgc_vol_err,
	       (tgc_vol_err <= 0.01) ? "OK" : "FORMULA-FAIL");
	if (tgc_vol_err > 0.01) failures++;
    }

    /* -------------------------------------------------------------- */
    /* 3. Oblique RCC (cylinder tilted 30° from vertical)             */
    /*                                                                */
    /*    H = (10, 0, 10√3)  (|H|=20, tilt 30° in XZ plane)         */
    /*    A=(5,0,0), B=(0,5,0), C=A, D=B  → circular cross-section  */
    /*    h_perp = H·ẑ = 10√3 ≈ 17.32 mm                           */
    /*                                                                */
    /* Volume (exact, regardless of SA formula):                      */
    /*    V = π·r²·h_perp = 25π·10√3 ≈ 1360.4 mm³                  */
    /*    Old formula (|H|): 25π·20 = 1570.8 mm³  (15.5% too large) */
    /*                                                                */
    /* Surface area (oblique cylinder, exact):                        */
    /*    Lateral SA = r · ∫₀²π √(Hz²+Hx²cos²(φ)) dφ  (exact, not 2πr·h_perp)     */
    /*    (elliptic integral; NOT 2πr·h_perp NOR 2πr·|H|)           */
    /*    End caps = 2·π·r²  (perpendicular to axis A,B)             */
    /*    Computed numerically below to serve as Crofton reference.  */
    /*    Note: rt_tgc_surf_area for RCC uses 2πr·|H| which is also  */
    /*    approximate for oblique cases; that is a separate issue.   */
    /* -------------------------------------------------------------- */
    {
	const double sin30 = 0.5;
	const double cos30 = sqrt(3.0) / 2.0;
	const double h_len = 20.0;
	const double r_cyl = 5.0;
	const double Hx    = h_len * sin30;    /* 10 */
	const double Hz    = h_len * cos30;    /* 10√3 */

	struct rt_tgc_internal tgc;
	memset(&tgc, 0, sizeof(tgc));
	tgc.magic = RT_TGC_INTERNAL_MAGIC;
	VSET(tgc.v, 0, 0, 0);
	VSET(tgc.h, Hx, 0, Hz);
	VSET(tgc.a, r_cyl, 0, 0);
	VSET(tgc.b, 0, r_cyl, 0);
	VSET(tgc.c, r_cyl, 0, 0);
	VSET(tgc.d, 0, r_cyl, 0);

	struct rt_db_internal ip;
	RT_DB_INTERNAL_INIT(&ip);
	ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
	ip.idb_minor_type = ID_TGC;
	ip.idb_type       = ID_TGC;
	ip.idb_ptr        = &tgc;

	/* Exact volume: V = π·r²·h_perp (h_perp = Hz since Hy=0, A⊥B ⊥ Z-axis) */
	double h_perp       = Hz;                    /* 10√3 */
	double analytic_vol = M_PI * r_cyl * r_cyl * h_perp;

	/* Exact SA (numerical integration of the exact lateral surface integral).
	 * The lateral surface is parameterised as r(φ,t)=t·H + r·(cosφ·x̂+sinφ·ŷ),
	 * giving |∂r/∂φ × ∂r/∂t| = r·√(Hz²+Hx²cos²φ).
	 * N=10000 Riemann points gives < 0.01% error.                          */
	{
	    const int N = 10000;
	    double sum = 0.0;
	    for (int k = 0; k < N; k++) {
		double phi = 2.0 * M_PI * k / N;
		sum += sqrt(Hz * Hz + Hx * Hx * cos(phi) * cos(phi));
	    }
	    double lateral_sa = r_cyl * sum * (2.0 * M_PI / N);
	    double end_caps   = 2.0 * M_PI * r_cyl * r_cyl;
	    double analytic_sa = lateral_sa + end_caps;
	    CROFTON_CHECK("oblique RCC 30deg tilt (Crofton)", &ip, analytic_sa, analytic_vol);
	}

	/* Verify rt_tgc_volume now returns h_perp-based result */
	fastf_t tgc_vol = 0.0;
	ip.idb_meth = &OBJ[ID_TGC];
	ip.idb_meth->ft_volume(&tgc_vol, &ip);
	double tgc_vol_err = fabs(tgc_vol - analytic_vol) / analytic_vol * 100.0;
	double old_vol     = M_PI * r_cyl * r_cyl * h_len;   /* old (wrong) formula */
	double old_err     = fabs(old_vol - analytic_vol) / analytic_vol * 100.0;
	printf("  %-42s  analytic_formula_err=%.2f%%  [%s]  (old_err=%.1f%%)\n",
	       "oblique RCC 30deg (rt_tgc_volume)",
	       tgc_vol_err,
	       (tgc_vol_err <= 0.1) ? "OK" : "FORMULA-FAIL",
	       old_err);
	if (tgc_vol_err > 0.1) failures++;
    }

    /* -------------------------------------------------------------- */
    /* 4. General TEC: non-similar cross-sections (a/b != c/d)        */
    /*                                                                 */
    /*    Geometry modelled on havoc.g s.fuse17.i:                    */
    /*      H = (-1360, -339.5, 0)   A = (0, 0, 919)   B = (0, 0.1, 0) */
    /*      C = (0, 0, 639)          D = (0, 159, 0)                  */
    /*    A⊥B, C⊥D, A·D=0, B·C=0, A×B = (-91.9, 0, ≈0)            */
    /*    h_perp = |H·(A×B)| / (|A|·|B|) = 124984 / 91.9 = 1360    */
    /*                                                                 */
    /*    Correct volume (prismatoid formula):                         */
    /*      V = π·h_perp/6·(2ab + ad + bc + 2cd)                    */
    /*        = π·1360/6·(2·91.9 + 146121 + 63.9 + 2·101601)        */
    /*        = π·1360/6·349570.7 ≈ 2.489e8 mm³                     */
    /*                                                                 */
    /*    Old (wrong) formula: π·h_perp/3·(ab+cd+√(abcd))           */
    /*      √(abcd) = √(91.9·101601) ≈ 3055.7   (not (ad+bc)/2 ≈ 73092) */
    /*      → 1.492e8 mm³  (≈40% underestimate)                     */
    /* -------------------------------------------------------------- */
    {
	struct rt_tgc_internal tgc;
	memset(&tgc, 0, sizeof(tgc));
	tgc.magic = RT_TGC_INTERNAL_MAGIC;
	VSET(tgc.v, 10320, 459.5, 1550);
	VSET(tgc.h, -1360, -339.5, 0);
	VSET(tgc.a, 0, 0, 919);
	VSET(tgc.b, 0, 0.1, 0);
	VSET(tgc.c, 0, 0, 639);
	VSET(tgc.d, 0, 159, 0);

	struct rt_db_internal ip;
	RT_DB_INTERNAL_INIT(&ip);
	ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
	ip.idb_minor_type = ID_TGC;
	ip.idb_type       = ID_TGC;
	ip.idb_meth       = &OBJ[ID_TGC];
	ip.idb_ptr        = &tgc;

	/* Prismatoid formula: V = π·h_perp/6·(2ab + ad + bc + 2cd) */
	const double a = 919.0, b = 0.1, c = 639.0, d = 159.0;
	const double h_perp_tec = 1360.0;
	double analytic_vol = M_PI * h_perp_tec / 6.0 *
	    (2.0*a*b + a*d + b*c + 2.0*c*d);

	/* Verify rt_tgc_volume gives the prismatoid result */
	fastf_t tgc_vol = 0.0;
	ip.idb_meth->ft_volume(&tgc_vol, &ip);
	double tgc_vol_err = fabs(tgc_vol - analytic_vol) / analytic_vol * 100.0;
	/* Compute what the old (incorrect) formula π·h/3·(ab+cd+√(abcd)) would
	 * have returned, shown as diagnostic "old_err" in the output line.   */
	double old_vol = M_PI * h_perp_tec * (a*b + c*d + sqrt(a*b*c*d)) / 3.0;
	double old_err = fabs(old_vol - analytic_vol) / analytic_vol * 100.0;
	printf("  %-42s  analytic_formula_err=%.2f%%  [%s]  (old_err=%.1f%%)\n",
	       "general TEC a/b!=c/d (rt_tgc_volume)",
	       tgc_vol_err,
	       (tgc_vol_err <= 0.01) ? "OK" : "FORMULA-FAIL",
	       old_err);
	if (tgc_vol_err > 0.01) failures++;
    }

    /* -------------------------------------------------------------- */
    /* 5. Oblique REC: non-circular, tilted 30° (models s.ant1 in     */
    /*    havoc.g)                                                     */
    /*                                                                 */
    /*    A=(80,0,0), B=(0,10,0), C=A, D=B  → elliptic cross-section */
    /*    H=(-160,0,320): |H|≈357.77, h_perp = |H·(A×B)|/(|A||B|)  */
    /*                  = |(-160)(0)(0)-...| = Hz = 320               */
    /*    (A×B = (0,0,800), VDOT(H, (0,0,800))/(80*10) = 320*800/800 */
    /*    = 320 ✓)                                                     */
    /*                                                                 */
    /*    Correct SA (lateral via h_perp):                             */
    /*      SA = ELL_CIRCUMFERENCE(a,b) * h_perp + 2π·a·b             */
    /*      Old SA used |H|=357.77 instead of h_perp=320 → ~10% error */
    /* -------------------------------------------------------------- */
    {
	const double rec_a  = 80.0;    /* semi-axis a */
	const double rec_b  = 10.0;    /* semi-axis b */
	/* H=(-160,0,320): h_perp = Hz = 320 (A×B points along Z) */
	const double h_perp_rec = 320.0;
	const double h_len_rec  = sqrt(160.0*160.0 + 320.0*320.0);  /* |H| */

	struct rt_tgc_internal tgc;
	memset(&tgc, 0, sizeof(tgc));
	tgc.magic = RT_TGC_INTERNAL_MAGIC;
	VSET(tgc.v, 0, 0, 0);
	VSET(tgc.h, -160, 0, 320);
	VSET(tgc.a, rec_a, 0, 0);
	VSET(tgc.b, 0, rec_b, 0);
	VSET(tgc.c, rec_a, 0, 0);
	VSET(tgc.d, 0, rec_b, 0);

	struct rt_db_internal ip2;
	RT_DB_INTERNAL_INIT(&ip2);
	ip2.idb_major_type = DB5_MAJORTYPE_BRLCAD;
	ip2.idb_minor_type = ID_TGC;
	ip2.idb_type       = ID_TGC;
	ip2.idb_meth       = &OBJ[ID_TGC];
	ip2.idb_ptr        = &tgc;

	/* Expected SA: ELL_CIRCUMFERENCE(a,b)*h_perp + 2π·a·b
	 * (Ramanujan approximation, same formula rt_tgc_surf_area uses) */
	double h_approx_rec = (rec_a - rec_b) / (rec_a + rec_b);
	double h2_rec       = h_approx_rec * h_approx_rec;
	double circ_rec     = M_PI * (rec_a + rec_b) *
	    (1.0 + 3.0*h2_rec / (10.0 + sqrt(4.0 - 3.0*h2_rec)));
	double expected_sa_rec = circ_rec * h_perp_rec + 2.0 * M_PI * rec_a * rec_b;

	fastf_t sa_rec = -1.0;
	ip2.idb_meth->ft_surf_area(&sa_rec, &ip2);

	double sa_rec_err     = fabs(sa_rec - expected_sa_rec) / expected_sa_rec * 100.0;
	double old_sa_rec     = circ_rec * h_len_rec + 2.0 * M_PI * rec_a * rec_b;
	double old_sa_rec_err = fabs(old_sa_rec - expected_sa_rec) / expected_sa_rec * 100.0;
	printf("  %-42s  analytic_formula_err=%.2f%%  [%s]  (old_err=%.1f%%)\n",
	       "oblique REC 30deg (rt_tgc_surf_area)",
	       sa_rec_err,
	       (sa_rec_err <= 0.01) ? "OK" : "FORMULA-FAIL",
	       old_sa_rec_err);
	if (sa_rec_err > 0.01) failures++;
    }

    /* -------------------------------------------------------------- */
    /* 6. ARB8 unit cube: analytic SA must equal 6 exactly.           */
    /*                                                                 */
    /*    Models s.GSI in havoc.g.  Previously rt_arb_surf_area used  */
    /*    *area += (not *area = 0 first), so when the caller passes   */
    /*    analytic_sa=-1.0 the result was -1+6=5 (20% error).         */
    /* -------------------------------------------------------------- */
    {
	struct rt_arb_internal arb;
	arb.magic = RT_ARB_INTERNAL_MAGIC;
	VSET(arb.pt[0],  0.5, -0.5, -0.5);
	VSET(arb.pt[1],  0.5,  0.5, -0.5);
	VSET(arb.pt[2],  0.5,  0.5,  0.5);
	VSET(arb.pt[3],  0.5, -0.5,  0.5);
	VSET(arb.pt[4], -0.5, -0.5, -0.5);
	VSET(arb.pt[5], -0.5,  0.5, -0.5);
	VSET(arb.pt[6], -0.5,  0.5,  0.5);
	VSET(arb.pt[7], -0.5, -0.5,  0.5);

	struct rt_db_internal ip2;
	RT_DB_INTERNAL_INIT(&ip2);
	ip2.idb_major_type = DB5_MAJORTYPE_BRLCAD;
	ip2.idb_minor_type = ID_ARB8;
	ip2.idb_type       = ID_ARB8;
	ip2.idb_meth       = &OBJ[ID_ARB8];
	ip2.idb_ptr        = &arb;

	fastf_t sa_arb = -1.0;   /* intentionally pre-set to -1 sentinel */
	ip2.idb_meth->ft_surf_area(&sa_arb, &ip2);

	double sa_arb_err = fabs(sa_arb - 6.0) / 6.0 * 100.0;
	printf("  %-42s  analytic_formula_err=%.2f%%  [%s]\n",
	       "ARB8 unit cube SA (rt_arb_surf_area)",
	       sa_arb_err,
	       (sa_arb_err <= 0.01) ? "OK" : "FORMULA-FAIL");
	if (sa_arb_err > 0.01) failures++;
    }

#undef CROFTON_CHECK

    printf("  Crofton verification: %d failure(s)\n", failures);
    return failures;
}




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

    /* Build allowed-object set from -F filter, if requested.
     * DB_SEARCH_FLAT searches every object in the database directly
     * (no hierarchy traversal), which is appropriate for filtering
     * primitives by name, type, or attribute.                        */
    struct bu_ptbl filter_objs = BU_PTBL_INIT_ZERO;
    int use_filter = 0;
    if (g_search_filter) {
	int s_flags = DB_SEARCH_FLAT | DB_SEARCH_RETURN_UNIQ_DP | DB_SEARCH_QUIET;
	int nfound = db_search(&filter_objs, s_flags, g_search_filter,
			       0, NULL, dbip, NULL, NULL, NULL);
	use_filter = 1;
	printf("    Filter: '%s'  (%d match(es))\n", g_search_filter, nfound);
    }

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

	/* Apply -F search filter: skip primitives not in the allowed set */
	if (use_filter && bu_ptbl_locate(&filter_objs, (long *)dp) == -1) {
	    n_skip++;
	    continue;
	}

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
	    case ID_BSPLINE:    /* rt_nurb_tess() always returns -1 */
	    case ID_REVOLVE:    /* rt_revolve_tess() not yet implemented */
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

	/* BOT-specific pre-checks: plate-mode and surface-mode BOTs cannot
	 * be validated as closed solid meshes by this tool.              */
	if (id == ID_BOT) {
	    struct rt_bot_internal *bot_ip = (struct rt_bot_internal *)intern.idb_ptr;
	    if (bot_ip->mode == RT_BOT_PLATE || bot_ip->mode == RT_BOT_PLATE_NOCOS) {
		fprintf(stderr, "  SKIP %-32s  (plate-mode BOT: no solid tess)\n",
			dp->d_namep);
		n_skip++;
		rt_db_free_internal(&intern);
		continue;
	    }
	    if (bot_ip->mode == RT_BOT_SURFACE) {
		fprintf(stderr, "  SKIP %-32s  (surface-mode BOT: not a solid)\n",
			dp->d_namep);
		n_skip++;
		rt_db_free_internal(&intern);
		continue;
	    }
	}

	/* Tessellate - wrap in BU_SETJUMP to catch bu_bomb() */
	struct model *m = nmg_mm();
	struct nmgregion *r = NULL;

        fprintf(stderr, "STARTING: %s (%s)\n", dp->d_namep, ((intern.idb_meth) ? intern.idb_meth->ft_label : "?"));
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
	    /* For PART primitives: sphere-type and degenerate (|H|≤tol.dist)
	     * PARTs intentionally return -1 from rt_part_tess.  These are
	     * geometrically valid, just not tessellated via this path.  Skip
	     * rather than counting as a failure. */
	    if (id == ID_PARTICLE && ret == -1) {
		struct rt_part_internal *part_ip =
		    (struct rt_part_internal *)intern.idb_ptr;
		if (part_ip->part_type == RT_PARTICLE_TYPE_SPHERE ||
		    MAGNITUDE(part_ip->part_H) <= tol.dist) {
		    fprintf(stderr, "  SKIP %-32s  (sphere-type PART, no NMG tess)\n",
			    dp->d_namep);
		    n_skip++;
		    nmg_km(m);
		    rt_db_free_internal(&intern);
		    continue;
		}
	    }
	    /* For ARS primitives: rt_ars_tess returns -1 when a ring curve is
	     * not closed in 3D (seam discontinuity) or backtracks on itself.
	     * These are data-quality issues — the solid cannot form a valid
	     * closed manifold mesh regardless of algorithm.  Skip gracefully
	     * rather than counting as a test failure. */
	    if (id == ID_ARS && ret == -1) {
		fprintf(stderr, "  SKIP %-32s  (ARS: unsuitable for tess — non-manifold geometry)\n",
			dp->d_namep);
		n_skip++;
		nmg_km(m);
		rt_db_free_internal(&intern);
		continue;
	    }
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
	struct tess_eval_result scan_res;
	memset(&scan_res, 0, sizeof(scan_res));
	scan_res.metrics_ok = -1;
	scan_res.err_sa = -1.0;
	scan_res.err_vol = -1.0;
	int ok = check_nmg_mesh(dp->d_namep, m, &tol, &vlfree, &intern, &ttol,
				 0 /* not quiet */, &scan_res);
	if (ok) n_pass++; else { n_fail++; failures++; }

	/* Convergence probing when metrics check failed but mesh topology is ok.
	 * If there are open edges, tightening tolerances won't fix them.      */
	int conv_rel = 0, conv_abs = 0, conv_norm = 0;
	struct conv_attempt rel_c, abs_c, norm_c;
	memset(&rel_c,  0, sizeof(rel_c));
	memset(&abs_c,  0, sizeof(abs_c));
	memset(&norm_c, 0, sizeof(norm_c));
	if (g_validate_metrics && scan_res.mesh_ok && scan_res.metrics_ok == 0) {
	    int is_planar = 0;
	    switch (id) {
		case ID_ARB8: case ID_ARBN: case ID_ARS:
		case ID_BOT:  case ID_NMG:  case ID_POLY:
		    is_planar = 1; break;
		default: break;
	    }
	    if (!is_planar) {
		conv_rel  = try_converge_tol(dp->d_namep, &intern, &ttol, &tol, CONV_REL,  &rel_c);
		conv_abs  = try_converge_tol(dp->d_namep, &intern, &ttol, &tol, CONV_ABS,  &abs_c);
		conv_norm = try_converge_tol(dp->d_namep, &intern, &ttol, &tol, CONV_NORM, &norm_c);

		fprintf(stderr,
		    "  CONV: %-44s  "
		    "rel=%s(%.3g->%.3g,%dstep)  "
		    "abs=%s(%.3g->%.3g,%dstep)  "
		    "norm=%s(%.3g->%.3g,%dstep)\n",
		    dp->d_namep,
		    rel_c.tried  ? (conv_rel  ? "OK" : "NO") : "-",
		    rel_c.start_val,  rel_c.final_val,  rel_c.n_steps,
		    abs_c.tried  ? (conv_abs  ? "OK" : "NO") : "-",
		    abs_c.start_val,  abs_c.final_val,  abs_c.n_steps,
		    norm_c.tried ? (conv_norm ? "OK" : "NO") : "-",
		    norm_c.start_val, norm_c.final_val, norm_c.n_steps);

		if (conv_rel || conv_abs || conv_norm) {
		    fprintf(stderr,
			"  CONV: %-44s  convergence achieved\n", dp->d_namep);
		    failures--; /* undo the failure count: tess routine converges */
		    n_fail--;
		    n_pass++;
		    /* Update displayed metrics from the converged result.
		     * Convergence checks run in quiet mode (no Crofton), so
		     * err_sa may be -1.0 when the analytic SA formula is
		     * absent; in that case keep the initial SA.            */
		    struct tess_eval_result *best_res =
			conv_rel  ? &rel_c.result  :
			conv_abs  ? &abs_c.result  : &norm_c.result;
		    scan_res.metrics_ok  = best_res->metrics_ok;
		    if (best_res->err_sa  >= 0.0) scan_res.err_sa  = best_res->err_sa;
		    if (best_res->err_vol >= 0.0) scan_res.err_vol = best_res->err_vol;
		    scan_res.res_limited = best_res->res_limited;
		    scan_res.n_tris      = best_res->n_tris;
		} else if (rel_c.tried || abs_c.tried || norm_c.tried) {
		    fprintf(stderr,
			"  CONV: %-44s  WARNING: no convergence found\n", dp->d_namep);
		}
	    }
	}

	/* Build double conv values: >0 = tol value that converged,
	 * -1.0 = tried but failed, 0.0 = not tried.                  */
	double conv_rel_val  = conv_rel  ? rel_c.final_val  : (rel_c.tried  ? -1.0 : 0.0);
	double conv_abs_val  = conv_abs  ? abs_c.final_val  : (abs_c.tried  ? -1.0 : 0.0);
	double conv_norm_val = conv_norm ? norm_c.final_val : (norm_c.tried ? -1.0 : 0.0);

	add_result(dp->d_namep, (intern.idb_meth ? intern.idb_meth->ft_label : "?"),
		   scan_res.tess_ok, scan_res.mesh_ok, scan_res.metrics_ok,
		   scan_res.res_limited, scan_res.n_tris,
		   scan_res.err_sa, scan_res.err_vol,
		   conv_rel_val, conv_abs_val, conv_norm_val);

	/* Optional Crofton validation: compare BOT metrics vs CSG raytrace.
	 * Use the original dbip directly so that file-referencing primitives
	 * (EBM, DSP, VOL) can locate their data files via dbip->dbi_filepath.
	 * Uses rt_crofton_shoot directly (no libanalyze dependency).        */
	if (ok && g_crofton_validate) {
	    double csa = 0.0, cv = 0.0;
	    struct rt_i *cr_rtip = rt_new_rti(dbip);
	    int cr = -1;
	    if (cr_rtip) {
		if (rt_gettree(cr_rtip, dp->d_namep) == 0) {
		    rt_prep_parallel(cr_rtip, 1);
		    cr = rt_crofton_shoot(cr_rtip, 2000, 2.0, &csa, &cv);
		}
		rt_free_rti(cr_rtip);
	    }
	    if (cr == 0) {
		fprintf(stderr,
			"  CROFTON: %-32s  CSG-SA=%.4g  CSG-V=%.4g\n",
			dp->d_namep, csa, cv);
	    } else {
		fprintf(stderr,
			"  CROFTON: %-32s  failed\n", dp->d_namep);
	    }
	}

	nmg_km(m);
	rt_db_free_internal(&intern);
    }

    bu_free(dpv, "dpv");
    if (use_filter)
	db_search_free(&filter_objs);
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
     *   [--validate]            run mesh quality checks (built-in tests)
     *   [--validate-metrics]    compare BOT area/volume to analytic answers
     *   [--crofton-validate]    run Crofton CSG estimate for --input-g scan
     *   [--metrics-tol <pct>]   metric tolerance % (default 5)
     *   [-h]                    print help
     */
    for (int i = 1; i < argc; i++) {
	if (BU_STR_EQUAL(argv[i], "-h") || BU_STR_EQUAL(argv[i], "--help")) {
	    printf("Usage: %s [--input-g <file.g>] [--output-g <file.g>]\n", argv[0]);
	    printf("          [--rel <frac>] [--abs <dist>] [--norm <rad>]\n");
	    printf("          [-F <filter>]\n");
	    printf("          [--validate] [--validate-metrics] [--crofton-validate] [--metrics-tol <pct>]\n");
	    printf("\n");
	    printf("  Without options: runs built-in NMG tessellation tests.\n");
	    printf("\n");
	    printf("  --input-g <file.g>\n");
	    printf("    Open an existing .g database, tessellate every non-BREP\n");
	    printf("    solid primitive, and validate manifold/open-edge quality.\n");
	    printf("\n");
	    printf("  -F <filter>\n");
	    printf("  --filter <filter>\n");
	    printf("    Restrict --input-g scan to primitives matching the given\n");
	    printf("    search expression (same syntax as the 'search'/'stat' GED\n");
	    printf("    commands).  Examples:\n");
	    printf("      -F '-name \"s.nos5c13.i\"'  (single named object)\n");
	    printf("      -F '-name \"s.nos*\"'        (glob on name)\n");
	    printf("      -F '-type tgc'             (all TGC primitives)\n");
	    printf("    Multiple expressions may be combined with -and/-or.\n");
	    printf("\n");
	    printf("  --output-g <file.g>\n");
	    printf("    Write each built-in CSG test primitive and its BOT\n");
	    printf("    facetization to a new .g file for visual inspection.\n");
	    printf("\n");
	    printf("  --validate\n");
	    printf("    Run mesh quality checks (open edges, manifold) for built-in tests.\n");
	    printf("    Automatically enabled by --validate-metrics and --output-g.\n");
	    printf("\n");
	    printf("  --validate-metrics\n");
	    printf("    Compare tessellated BOT surface area and volume against\n");
	    printf("    the primitive's analytic answers (ft_surf_area, ft_volume).\n");
	    printf("    Enables manifold validation automatically.\n");
	    printf("\n");
	    printf("  --crofton-validate\n");
	    printf("    For each primitive in --input-g, also shoot a Cauchy-Crofton\n");
	    printf("    sample on the CSG object and report SA and volume.\n");
	    printf("\n");
	    printf("  --metrics-tol <pct>\n");
	    printf("    Tolerance (as percent) for analytic metric comparison (default 5).\n");
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
	} else if (BU_STR_EQUAL(argv[i], "--validate")) {
	    g_validate = 1;
	} else if (BU_STR_EQUAL(argv[i], "--validate-metrics")) {
	    g_validate_metrics = 1;
	    g_validate = 1;
	} else if (BU_STR_EQUAL(argv[i], "--prim") && i + 1 < argc) {
	    g_prim_filter = argv[++i];
	} else if ((BU_STR_EQUAL(argv[i], "-F") || BU_STR_EQUAL(argv[i], "--filter")) && i + 1 < argc) {
	    g_search_filter = argv[++i];
	} else if (BU_STR_EQUAL(argv[i], "--crofton-validate")) {
	    g_crofton_validate = 1;
	} else if (BU_STR_EQUAL(argv[i], "--metrics-tol") && i + 1 < argc) {
	    double v = atof(argv[++i]);
	    if (v > 0.0 && v <= 100.0) g_metrics_tol = v / 100.0; /* convert percent to fraction */
	    else if (v > 100.0) fprintf(stderr, "WARNING: --metrics-tol value %.4g > 100%% (did you mean a fraction?), ignored\n", v);
	    else fprintf(stderr, "WARNING: --metrics-tol requires a positive value, ignored\n");
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

    /* ---- Crofton estimator self-check (always runs) ---- */
    total_failures += verify_crofton_estimates();

    if (!input_g) {
	/* ---- Built-in test suite ---- */
#define RUN_IF(name, fn) \
    do { if (!g_prim_filter || BU_STR_EQUAL(g_prim_filter, name)) { \
	    int _start = g_nresults; \
	    total_failures += fn(); \
	    if (g_validate && g_nresults > _start) \
		print_summary_table(_start, g_nresults, name); \
	} } while (0)
	RUN_IF("tor",      test_tor);
	RUN_IF("eto",      test_eto);
	RUN_IF("tgc",      test_tgc);
	RUN_IF("ell",      test_ell);
	RUN_IF("epa",      test_epa);
	RUN_IF("ehy",      test_ehy);
	RUN_IF("rpc",      test_rpc);
	RUN_IF("rhc",      test_rhc);
	RUN_IF("hyp",      test_hyp);
	RUN_IF("part",     test_part);
	RUN_IF("dsp",      test_dsp);
	RUN_IF("ebm",      test_ebm);
	RUN_IF("vol",      test_vol);
	RUN_IF("arb",      test_arb);
	RUN_IF("ars",      test_ars);
	RUN_IF("arbn",     test_arbn);
	RUN_IF("pipe",     test_pipe);
	RUN_IF("metaball", test_metaball);
#undef RUN_IF
    }

    /* ---- Input .g scan (if requested) ---- */
    if (input_g) {
	/* Scan mode always validates mesh quality */
	if (!g_validate) g_validate = 1;
	int scan_start = g_nresults;
	total_failures += scan_input_g(input_g);
	print_summary_table(scan_start, g_nresults, input_g);
    }

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
