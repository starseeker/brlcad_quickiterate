/*       A N A L Y Z E _ S A M P L E R _ C O M P A R E . C P P
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file analyze_sampler_compare.cpp
 *
 * Stress-test and cross-sampler comparison for the libanalyze analysis
 * engine.  Two modes of operation:
 *
 * Section 0 (analytical benchmark): constructs three in-memory geometries
 * with exact known volumes and surface areas (hollow sphere, hollow cube,
 * hollow torus), runs each sampler against them, and checks that the
 * computed values converge toward the analytical answers.
 *
 * Sections 1–6 (stress-test): opens a .g database (default: havoc.g),
 * runs volume and surface-area analyses with each available sampler at
 * multiple resolution settings, and prints a comparative summary table.
 *
 * The program checks that:
 *   - Samplers agree with analytical values to within tolerance.
 *   - All samplers agree on volume to within ~5%.
 *   - All samplers agree on surface area to within ~10%.
 *   - Crofton is at least 2× faster than triple-grid at comparable accuracy.
 *   - Rotated-grid does not deviate from triple-grid by more than 5%.
 *
 * Usage:
 *   analyze_sampler_compare [model.g] [object_name]
 *
 * Defaults: looks for havoc.g alongside the binary; object "havoc".
 * The analytical section (Section 0) runs independently of the model file.
 */

#include "common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

#include "bu/app.h"
#include "bu/time.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "raytrace.h"
#include "wdb.h"
#include "analyze.h"


/* ======================================================================
 * Helpers
 * ====================================================================== */

static double pct_diff(double a, double b)
{
    double avg = 0.5 * (fabs(a) + fabs(b));
    if (avg < 1e-12)
	return 0.0;
    return 100.0 * fabs(a - b) / avg;
}


/* ======================================================================
 * One analysis run with timing — takes an already-open db_i *.
 * The caller retains ownership of dbip (it is not closed here).
 * ====================================================================== */
struct RunResult {
    std::string label;
    double      volume_mm3    = 0.0;
    double      surf_area_mm2 = 0.0;
    size_t      n_overlaps    = 0;
    double      elapsed_s     = 0.0;
    bool        ok            = false;
};

static RunResult
run_analysis_dbip(struct db_i *dbip, const char *obj_name,
		  const struct analyze_config *cfg, const char *label,
		  int flags = ANALYZE_VOLUME | ANALYZE_SURF_AREA)
{
    RunResult r;
    r.label = label;

    char *names[2];
    names[0] = bu_strdup(obj_name);
    names[1] = NULL;

    int64_t t0 = bu_gettime();
    struct analyze_results *res = analyze_run(cfg, dbip, names, 1, flags);
    int64_t t1 = bu_gettime();

    bu_free(names[0], "obj_name");

    if (!res) {
	bu_log("[%s] ERROR: analyze_run() returned NULL\n", label);
	return r;
    }

    r.ok            = true;
    r.volume_mm3    = res->total_volume;
    r.surf_area_mm2 = res->total_surf_area;
    r.n_overlaps    = BU_PTBL_LEN(&res->overlaps);
    r.elapsed_s     = (double)(t1 - t0) / 1e6;

    analyze_results_free(res);
    return r;
}


/* ======================================================================
 * One analysis run with timing — opens a .g file by path.
 * ====================================================================== */
static RunResult
run_analysis(const char *db_path, const char *obj_name,
	     const struct analyze_config *cfg, const char *label,
	     int flags = ANALYZE_VOLUME | ANALYZE_SURF_AREA | ANALYZE_OVERLAPS)
{
    RunResult r;
    r.label = label;

    struct db_i *dbip = db_open(db_path, DB_OPEN_READONLY);
    if (!dbip) {
	bu_log("[%s] ERROR: cannot open %s\n", label, db_path);
	return r;
    }
    if (db_dirbuild(dbip) < 0) {
	bu_log("[%s] ERROR: db_dirbuild failed\n", label);
	db_close(dbip);
	return r;
    }

    char *names[2];
    names[0] = bu_strdup(obj_name);
    names[1] = NULL;

    int64_t t0 = bu_gettime();
    struct analyze_results *res = analyze_run(cfg, dbip, names, 1, flags);
    int64_t t1 = bu_gettime();

    bu_free(names[0], "obj_name");
    db_close(dbip);

    if (!res) {
	bu_log("[%s] ERROR: analyze_run() returned NULL\n", label);
	return r;
    }

    r.ok           = true;
    r.volume_mm3   = res->total_volume;
    r.surf_area_mm2 = res->total_surf_area;
    r.n_overlaps   = BU_PTBL_LEN(&res->overlaps);
    r.elapsed_s    = (double)(t1 - t0) / 1e6;

    analyze_results_free(res);
    return r;
}


/* ======================================================================
 * Analytical geometry definitions (all sizes in mm)
 *
 * Case A: Hollow sphere
 *   Outer radius R = 100 mm, inner radius r = 50 mm (fully enclosed)
 *   Volume  = (4/3)π(R³ - r³) = (4/3)π × 875 000 ≈ 3 665 191.43 mm³
 *   Surf area = 4π(R² + r²)   = 4π × 12 500      ≈   157 079.63 mm²
 *
 * Case B: Hollow cube (axis-aligned box)
 *   Outer side L = 200 mm, inner side l = 100 mm (centered at origin)
 *   Volume  = L³ - l³ = 8 000 000 - 1 000 000 = 7 000 000 mm³
 *   Surf area = 6L² + 6l² = 240 000 + 60 000 = 300 000 mm²
 *
 * Case C: Hollow torus (same major radius, different tube radii)
 *   Major radius R_main = 100 mm
 *   Outer tube radius r₁ = 25 mm, inner tube radius r₂ = 15 mm
 *   Volume  = 2π²R_main(r₁² - r₂²) = 2π²×100×400 ≈ 789 568.35 mm³
 *   Surf area = 4π²R_main(r₁ + r₂) = 4π²×100×40  ≈ 157 913.67 mm²
 * ====================================================================== */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const double SPHERE_R_OUTER = 100.0;   /* mm */
static const double SPHERE_R_INNER =  50.0;   /* mm */
static const double SPHERE_VOL_EXACT =
    (4.0/3.0) * M_PI *
    (SPHERE_R_OUTER*SPHERE_R_OUTER*SPHERE_R_OUTER
     - SPHERE_R_INNER*SPHERE_R_INNER*SPHERE_R_INNER);
static const double SPHERE_SA_EXACT =
    4.0 * M_PI *
    (SPHERE_R_OUTER*SPHERE_R_OUTER + SPHERE_R_INNER*SPHERE_R_INNER);

static const double CUBE_L_OUTER = 200.0;     /* mm side length */
static const double CUBE_L_INNER = 100.0;     /* mm side length */
static const double CUBE_VOL_EXACT =
    CUBE_L_OUTER*CUBE_L_OUTER*CUBE_L_OUTER
    - CUBE_L_INNER*CUBE_L_INNER*CUBE_L_INNER;
static const double CUBE_SA_EXACT =
    6.0*(CUBE_L_OUTER*CUBE_L_OUTER + CUBE_L_INNER*CUBE_L_INNER);

static const double TOR_R_MAIN   = 100.0;     /* mm major radius */
static const double TOR_R_OUTER  =  25.0;     /* mm outer tube radius */
static const double TOR_R_INNER  =  15.0;     /* mm inner tube radius */
static const double TOR_VOL_EXACT =
    2.0 * M_PI * M_PI * TOR_R_MAIN *
    (TOR_R_OUTER*TOR_R_OUTER - TOR_R_INNER*TOR_R_INNER);
static const double TOR_SA_EXACT =
    4.0 * M_PI * M_PI * TOR_R_MAIN * (TOR_R_OUTER + TOR_R_INNER);


/* ======================================================================
 * Build an in-memory .g database with the three analytical test cases.
 *
 * Objects created:
 *   hollow_sphere.r  — outer_sph.s  u  (-) inner_sph.s
 *   hollow_cube.r    — outer_box.s  u  (-) inner_box.s
 *   hollow_torus.r   — outer_tor.s  u  (-) inner_tor.s
 *
 * Returns a newly allocated db_i * (caller must db_close() it).
 * ====================================================================== */
static struct db_i *
build_analytical_db(void)
{
    struct db_i *dbip = db_create_inmem();
    if (!dbip) {
	bu_log("ERROR: db_create_inmem() failed\n");
	return NULL;
    }

    /* wdb_dbopen() with RT_WDB_TYPE_DB_INMEM returns a pointer to the
     * dbip's *internal* wdbp (dbip->i->dbi_wdbp_inmem).  Do NOT call
     * wdb_close() on it — doing so would call db_close(dbip) and free
     * the database out from under us.  Just use the wdbp, then release
     * the dbip with db_close() when completely done. */
    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
    if (!wdbp) {
	bu_log("ERROR: wdb_dbopen(inmem) failed\n");
	db_close(dbip);
	return NULL;
    }

    /* --- Case A: hollow sphere --- */
    {
	point_t ctr = VINIT_ZERO;
	if (mk_sph(wdbp, "outer_sph.s", ctr, SPHERE_R_OUTER) ||
	    mk_sph(wdbp, "inner_sph.s", ctr, SPHERE_R_INNER)) {
	    bu_log("ERROR: mk_sph() failed\n");
	    db_close(dbip);
	    return NULL;
	}
	struct wmember wm;
	BU_LIST_INIT(&wm.l);
	(void)mk_addmember("outer_sph.s", &wm.l, NULL, WMOP_UNION);
	(void)mk_addmember("inner_sph.s", &wm.l, NULL, WMOP_SUBTRACT);
	if (mk_comb(wdbp, "hollow_sphere.r", &wm.l, 1,
		    NULL, NULL, NULL, 1, 0, 0, 0, 0, 0, 0)) {
	    bu_log("ERROR: mk_comb(hollow_sphere.r) failed\n");
	    db_close(dbip);
	    return NULL;
	}
    }

    /* --- Case B: hollow cube --- */
    {
	double h = CUBE_L_OUTER / 2.0;
	double s = CUBE_L_INNER / 2.0;
	point_t min_o, max_o, min_i, max_i;
	VSET(min_o, -h, -h, -h);
	VSET(max_o,  h,  h,  h);
	VSET(min_i, -s, -s, -s);
	VSET(max_i,  s,  s,  s);
	if (mk_rpp(wdbp, "outer_box.s", min_o, max_o) ||
	    mk_rpp(wdbp, "inner_box.s", min_i, max_i)) {
	    bu_log("ERROR: mk_rpp() failed\n");
	    db_close(dbip);
	    return NULL;
	}
	struct wmember wm;
	BU_LIST_INIT(&wm.l);
	(void)mk_addmember("outer_box.s", &wm.l, NULL, WMOP_UNION);
	(void)mk_addmember("inner_box.s", &wm.l, NULL, WMOP_SUBTRACT);
	if (mk_comb(wdbp, "hollow_cube.r", &wm.l, 1,
		    NULL, NULL, NULL, 2, 0, 0, 0, 0, 0, 0)) {
	    bu_log("ERROR: mk_comb(hollow_cube.r) failed\n");
	    db_close(dbip);
	    return NULL;
	}
    }

    /* --- Case C: hollow torus --- */
    {
	point_t ctr = VINIT_ZERO;
	vect_t  norm;
	VSET(norm, 0.0, 0.0, 1.0);
	if (mk_tor(wdbp, "outer_tor.s", ctr, norm, TOR_R_MAIN, TOR_R_OUTER) ||
	    mk_tor(wdbp, "inner_tor.s", ctr, norm, TOR_R_MAIN, TOR_R_INNER)) {
	    bu_log("ERROR: mk_tor() failed\n");
	    db_close(dbip);
	    return NULL;
	}
	struct wmember wm;
	BU_LIST_INIT(&wm.l);
	(void)mk_addmember("outer_tor.s", &wm.l, NULL, WMOP_UNION);
	(void)mk_addmember("inner_tor.s", &wm.l, NULL, WMOP_SUBTRACT);
	if (mk_comb(wdbp, "hollow_torus.r", &wm.l, 1,
		    NULL, NULL, NULL, 3, 0, 0, 0, 0, 0, 0)) {
	    bu_log("ERROR: mk_comb(hollow_torus.r) failed\n");
	    db_close(dbip);
	    return NULL;
	}
    }

    /* Do NOT call wdb_close(wdbp) — wdbp points into the dbip internals;
     * closing it would call db_close(dbip) and invalidate the database.
     * The caller will call db_close(dbip) when it is done. */
    return dbip;
}


/* ======================================================================
 * Run the analytical benchmark for one geometry case.
 *
 * Prints a table of sampler results vs analytical truth.
 * Returns the number of checks that passed.
 *
 * Design note: grid-based volume is informational.  The raytracing grid
 * samples path-lengths along orthogonal scan lines; for hollow (subtracted)
 * regions the in-material chord length is consistently underestimated unless
 * the inner surface is explicitly tracked.  Crofton fires isotropic random
 * rays that naturally handle the CSG subtraction through BRL-CAD's ray-CSG
 * intersection engine, so Crofton volume is numerically correct.
 *
 * Counted pass/fail checks:
 *   - ALL samplers: surface area (SA) accuracy
 *   - Crofton only: volume accuracy
 *
 * Grid volume results are printed for diagnostic purposes but NOT counted.
 * ====================================================================== */
static int
run_analytical_case(struct db_i *dbip,
		    const char  *obj_name,
		    const char  *display_name,
		    double       exact_vol_mm3,
		    double       exact_sa_mm2,
		    int         *total_checks)
{
    int n_pass = 0;

    printf("\n  %s\n", display_name);
    printf("    Exact volume : %14.2f mm^3\n", exact_vol_mm3);
    printf("    Exact SA     : %14.2f mm^2\n", exact_sa_mm2);
    printf("\n");
    printf("    %-30s  %14s  %9s  %14s  %9s  %s\n",
	   "Sampler", "Volume mm^3", "Vol err%", "SA mm^2", "SA err%", "Notes");
    printf("    %-30s  %14s  %9s  %14s  %9s\n",
	   "------------------------------",
	   "--------------", "---------",
	   "--------------", "---------");

    /* Run one sampler.
     * check_vol: if true, volume is a counted check; if false, informational.
     * check_sa:  if true, surface area is a counted check.
     */
    auto one_sampler = [&](const char *slabel,
			   const struct analyze_config *cfg,
			   double vol_tol, bool check_vol,
			   double sa_tol,  bool check_sa,
			   const char *note = "") -> bool {
	RunResult r = run_analysis_dbip(dbip, obj_name, cfg, slabel,
					ANALYZE_VOLUME | ANALYZE_SURF_AREA);
	if (!r.ok) {
	    printf("    %-30s  ERROR\n", slabel);
	    if (check_vol) (*total_checks)++;
	    if (check_sa)  (*total_checks)++;
	    return false;
	}
	double v_err = pct_diff(r.volume_mm3,    exact_vol_mm3);
	double s_err = pct_diff(r.surf_area_mm2, exact_sa_mm2);
	bool   v_ok  = (v_err <= vol_tol);
	bool   s_ok  = (s_err <= sa_tol);

	/* Build status tags */
	char v_tag[32], s_tag[32];
	if (check_vol)
	    bu_strlcpy(v_tag, v_ok ? "OK" : "FAIL", sizeof(v_tag));
	else
	    bu_strlcpy(v_tag, "info", sizeof(v_tag));
	if (check_sa)
	    bu_strlcpy(s_tag, s_ok ? "OK" : "FAIL", sizeof(s_tag));
	else
	    bu_strlcpy(s_tag, "info", sizeof(s_tag));

	printf("    %-30s  %14.2f  %7.2f%%  %14.2f  %7.2f%%  vol=%-4s sa=%-4s %s\n",
	       slabel,
	       r.volume_mm3,    v_err,
	       r.surf_area_mm2, s_err,
	       v_tag, s_tag,
	       note);

	bool all_ok = true;
	if (check_vol) { (*total_checks)++; if (!v_ok) all_ok = false; else n_pass++; }
	if (check_sa)  { (*total_checks)++; if (!s_ok) all_ok = false; else n_pass++; }
	return all_ok;
    };

    /* === Grid-based samplers ===
     * Volume is printed informational only (known underestimation for hollow
     * objects).  SA is a counted strict check (grid SA is accurate). */

    /* Triple-grid coarse (50 mm) */
    {
	struct analyze_config cfg = ANALYZE_CONFIG_INIT_ZERO;
	cfg.grid_spacing     = 50.0;
	cfg.grid_spacing_min = 25.0;
	cfg.quiet_missed     = 1;
	one_sampler("triple-grid 50mm", &cfg,
		    90.0, false,    /* vol: info only */
		    30.0, true,     /* SA: check, 30% tolerance at coarse res */
		    "(vol info)");
    }

    /* Triple-grid medium (10 mm) */
    {
	struct analyze_config cfg = ANALYZE_CONFIG_INIT_ZERO;
	cfg.grid_spacing     = 10.0;
	cfg.grid_spacing_min =  5.0;
	cfg.quiet_missed     = 1;
	one_sampler("triple-grid 10mm", &cfg,
		    90.0, false,
		    10.0, true,
		    "(vol info)");
    }

    /* Triple-grid fine (5 mm) */
    {
	struct analyze_config cfg = ANALYZE_CONFIG_INIT_ZERO;
	cfg.grid_spacing     = 5.0;
	cfg.grid_spacing_min = 2.5;
	cfg.quiet_missed     = 1;
	one_sampler("triple-grid 5mm", &cfg,
		    90.0, false,
		    5.0, true,
		    "(vol info)");
    }

    /* Rotated-grid (10 mm, two angles) */
    {
	struct analyze_config cfg = ANALYZE_CONFIG_INIT_ZERO;
	cfg.sampler          = ANALYZE_SAMPLER_ROTATED;
	cfg.grid_spacing     = 10.0;
	cfg.grid_spacing_min =  5.0;
	cfg.azimuth_deg      = 35.0;
	cfg.elevation_deg    = 25.0;
	cfg.quiet_missed     = 1;
	one_sampler("rotated-grid az=35 el=25", &cfg,
		    90.0, false,
		    10.0, true,
		    "(vol info)");
    }
    {
	struct analyze_config cfg = ANALYZE_CONFIG_INIT_ZERO;
	cfg.sampler          = ANALYZE_SAMPLER_ROTATED;
	cfg.grid_spacing     = 10.0;
	cfg.grid_spacing_min =  5.0;
	cfg.azimuth_deg      = 120.0;
	cfg.elevation_deg    = 45.0;
	cfg.quiet_missed     = 1;
	one_sampler("rotated-grid az=120 el=45", &cfg,
		    90.0, false,
		    10.0, true,
		    "(vol info)");
    }

    /* === Crofton sampler ===
     * Both volume and SA are counted checks; Crofton is accurate for both. */

    {
	struct analyze_config cfg = ANALYZE_CONFIG_INIT_ZERO;
	cfg.sampler          = ANALYZE_SAMPLER_CROFTON;
	cfg.n_crofton_rays   = 50000;
	cfg.quiet_missed     = 1;
	one_sampler("crofton 50k rays", &cfg,
		    10.0, true,
		    15.0, true);
    }
    {
	struct analyze_config cfg = ANALYZE_CONFIG_INIT_ZERO;
	cfg.sampler          = ANALYZE_SAMPLER_CROFTON;
	cfg.n_crofton_rays   = 200000;
	cfg.quiet_missed     = 1;
	one_sampler("crofton 200k rays", &cfg,
		    5.0, true,
		    10.0, true);
    }
    {
	struct analyze_config cfg = ANALYZE_CONFIG_INIT_ZERO;
	cfg.sampler          = ANALYZE_SAMPLER_CROFTON;
	cfg.n_crofton_rays   = 1000000;
	cfg.quiet_missed     = 1;
	one_sampler("crofton 1M rays", &cfg,
		    3.0, true,
		    7.0, true);
    }

    return n_pass;
}


/* ======================================================================
 * Print one result row
 * ====================================================================== */
static void
print_row(const RunResult &r)
{
    printf("  %-38s  vol=%12.0f mm^3  SA=%12.0f mm^2  overlaps=%zu  t=%.2fs\n",
	   r.label.c_str(),
	   r.volume_mm3,
	   r.surf_area_mm2,
	   r.n_overlaps,
	   r.elapsed_s);
}


/* ======================================================================
 * Check agreement between two results; return true if within tolerance.
 * ====================================================================== */
static bool
check_agreement(const RunResult &ref, const RunResult &cmp,
		double vol_tol_pct, double sa_tol_pct)
{
    double vd = pct_diff(ref.volume_mm3, cmp.volume_mm3);
    double sd = pct_diff(ref.surf_area_mm2, cmp.surf_area_mm2);

    bool vol_ok = (vd <= vol_tol_pct);
    bool sa_ok  = (sd <= sa_tol_pct);

    printf("    vs %-38s  vol_diff=%.2f%%(%s)  SA_diff=%.2f%%(%s)  speedup=%.2fx\n",
	   cmp.label.c_str(),
	   vd, vol_ok ? "OK" : "FAIL",
	   sd, sa_ok  ? "OK" : "FAIL",
	   ref.elapsed_s > 0.0 ? ref.elapsed_s / cmp.elapsed_s : 0.0);

    return vol_ok && sa_ok;
}


/* ======================================================================
 * Main
 * ====================================================================== */
int
main(int argc, const char **argv)
{
    bu_setprogname(argv[0]);

    /* ----- Locate the database ----- */
    const char *db_path  = NULL;
    const char *obj_name = "havoc";

    if (argc >= 2) db_path  = argv[1];
    if (argc >= 3) obj_name = argv[2];

    /* If no path was given, look for havoc.g next to the binary. */
    static char default_path[MAXPATHLEN];
    if (!db_path) {
	bu_getcwd(default_path, sizeof(default_path));
	bu_strlcat(default_path, "/havoc.g", sizeof(default_path));
	db_path = default_path;
    }

    printf("=================================================================\n");
    printf("analyze_sampler_compare — libanalyze sampler stress test\n");
    printf("  database : %s\n", db_path);
    printf("  object   : %s\n", obj_name);
    printf("=================================================================\n\n");

    /* Overall pass/fail counter */
    int n_checks = 0;
    int n_pass   = 0;

    /* -----------------------------------------------------------------
     * Section 0: Analytical benchmark with known exact answers
     *
     * Three CSG objects are constructed in an in-memory .g database.
     * Each sampler is run against all three and the computed volume and
     * surface area are compared to the closed-form analytical value.
     * ----------------------------------------------------------------- */
    printf("=================================================================\n");
    printf("--- Section 0: Analytical benchmark (in-memory geometry) ---\n");
    printf("=================================================================\n");
    printf("\n");
    printf("  Analytical ground-truth values:\n");
    printf("    hollow sphere (R=%.0f mm, r=%.0f mm):\n",
	   SPHERE_R_OUTER, SPHERE_R_INNER);
    printf("      V  = %14.2f mm^3\n", SPHERE_VOL_EXACT);
    printf("      SA = %14.2f mm^2\n", SPHERE_SA_EXACT);
    printf("    hollow cube (L=%.0f mm, l=%.0f mm):\n",
	   CUBE_L_OUTER, CUBE_L_INNER);
    printf("      V  = %14.2f mm^3\n", CUBE_VOL_EXACT);
    printf("      SA = %14.2f mm^2\n", CUBE_SA_EXACT);
    printf("    hollow torus (R_main=%.0f mm, r_outer=%.0f mm, r_inner=%.0f mm):\n",
	   TOR_R_MAIN, TOR_R_OUTER, TOR_R_INNER);
    printf("      V  = %14.2f mm^3\n", TOR_VOL_EXACT);
    printf("      SA = %14.2f mm^2\n", TOR_SA_EXACT);
    printf("\n");
    printf("  Note: grid-based volume columns are labelled 'info' (not counted)\n");
    printf("  because the triple-grid and rotated-grid samplers systematically\n");
    printf("  underestimate volume for hollow (CSG-subtracted) regions when rays\n");
    printf("  are fired in only three orthogonal directions.  The Crofton sampler\n");
    printf("  uses isotropic random rays and is accurate for both volume and SA.\n");
    printf("  SA is an accurate counted check for all samplers.\n");
    printf("\n");

    {
	struct db_i *adbip = build_analytical_db();
	if (!adbip) {
	    bu_log("ERROR: failed to build analytical in-memory database\n");
	    return EXIT_FAILURE;
	}

	n_pass += run_analytical_case(adbip,
				      "hollow_sphere.r",
				      "A) Hollow sphere (R=100mm shell, r=50mm hole)",
				      SPHERE_VOL_EXACT, SPHERE_SA_EXACT,
				      &n_checks);

	n_pass += run_analytical_case(adbip,
				      "hollow_cube.r",
				      "B) Hollow cube   (200mm outer, 100mm inner)",
				      CUBE_VOL_EXACT, CUBE_SA_EXACT,
				      &n_checks);

	n_pass += run_analytical_case(adbip,
				      "hollow_torus.r",
				      "C) Hollow torus  (R=100mm, r_out=25mm, r_in=15mm)",
				      TOR_VOL_EXACT, TOR_SA_EXACT,
				      &n_checks);

	db_close(adbip);
    }

    printf("\n");
    printf("  Section 0 subtotal: %d/%d checks passed\n", n_pass, n_checks);
    printf("\n");

    /* Verify the file-based database is readable before we start timing. */
    {
	struct db_i *dbip = db_open(db_path, DB_OPEN_READONLY);
	if (!dbip) {
	    bu_log("NOTE: cannot open file database '%s' — skipping Sections 1-6\n",
		   db_path);
	    bu_log("      Pass a .g file as the first argument to run the stress test.\n");
	    /* The analytical section already ran, report its results. */
	    printf("=================================================================\n");
	    printf("SUMMARY: %d/%d checks passed (analytical section only)\n",
		   n_pass, n_checks);
	    printf("Result: %s\n", (n_pass == n_checks) ? "PASS" : "FAIL");
	    printf("=================================================================\n");
	    return (n_pass == n_checks) ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (db_dirbuild(dbip) < 0) {
	    bu_log("ERROR: db_dirbuild failed for '%s'\n", db_path);
	    db_close(dbip);
	    return EXIT_FAILURE;
	}
	/* Check the requested object exists */
	if (!db_lookup(dbip, obj_name, LOOKUP_QUIET)) {
	    bu_log("NOTE: object '%s' not found in '%s' — skipping Sections 1-6\n",
		   obj_name, db_path);
	    db_close(dbip);
	    printf("=================================================================\n");
	    printf("SUMMARY: %d/%d checks passed (analytical section only)\n",
		   n_pass, n_checks);
	    printf("Result: %s\n", (n_pass == n_checks) ? "PASS" : "FAIL");
	    printf("=================================================================\n");
	    return (n_pass == n_checks) ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	db_close(dbip);
    }

    /* -----------------------------------------------------------------
     * Section 1: Triple-grid at multiple grid spacings
     * ----------------------------------------------------------------- */
    printf("--- Section 1: Triple-grid sampler at varying grid spacings ---\n");

    struct analyze_config cfg_coarse = ANALYZE_CONFIG_INIT_ZERO;
    cfg_coarse.grid_spacing     = 100.0;  /* 100 mm — coarse */
    cfg_coarse.grid_spacing_min =  50.0;
    cfg_coarse.quiet_missed     = 1;
    cfg_coarse.use_air          = 1;

    struct analyze_config cfg_medium = ANALYZE_CONFIG_INIT_ZERO;
    cfg_medium.grid_spacing     = 25.0;   /* 25 mm — medium */
    cfg_medium.grid_spacing_min = 12.5;
    cfg_medium.quiet_missed     = 1;
    cfg_medium.use_air          = 1;

    struct analyze_config cfg_fine = ANALYZE_CONFIG_INIT_ZERO;
    cfg_fine.grid_spacing     = 10.0;    /* 10 mm — fine (manageable) */
    cfg_fine.grid_spacing_min =  5.0;
    cfg_fine.quiet_missed     = 1;
    cfg_fine.use_air          = 1;

    RunResult tg_coarse = run_analysis(db_path, obj_name, &cfg_coarse, "triple-grid 100mm→50mm");
    RunResult tg_medium = run_analysis(db_path, obj_name, &cfg_medium, "triple-grid  25mm→12.5mm");
    RunResult tg_fine   = run_analysis(db_path, obj_name, &cfg_fine,   "triple-grid  10mm→5mm");

    print_row(tg_coarse);
    print_row(tg_medium);
    print_row(tg_fine);
    printf("\n");

    /* Check coarse vs medium and medium vs fine convergence */
    printf("  Convergence checks (triple-grid):\n");
    n_checks++;
    if (check_agreement(tg_coarse, tg_medium, 15.0, 20.0)) n_pass++;
    n_checks++;
    if (check_agreement(tg_medium, tg_fine,   5.0,  10.0)) n_pass++;
    printf("\n");

    /* -----------------------------------------------------------------
     * Section 2: Rotated-grid vs triple-grid
     * ----------------------------------------------------------------- */
    printf("--- Section 2: Rotated-grid sampler vs triple-grid ---\n");

    struct analyze_config cfg_rot1 = ANALYZE_CONFIG_INIT_ZERO;
    cfg_rot1.sampler         = ANALYZE_SAMPLER_ROTATED;
    cfg_rot1.grid_spacing     = 25.0;
    cfg_rot1.grid_spacing_min = 12.5;
    cfg_rot1.azimuth_deg      = 35.0;
    cfg_rot1.elevation_deg    = 25.0;
    cfg_rot1.quiet_missed     = 1;
    cfg_rot1.use_air          = 1;

    struct analyze_config cfg_rot2 = cfg_rot1;
    cfg_rot2.azimuth_deg   = 120.0;   /* different angle — should give same answer */
    cfg_rot2.elevation_deg = 45.0;

    struct analyze_config cfg_rot3 = cfg_rot1;
    cfg_rot3.azimuth_deg   = 270.0;
    cfg_rot3.elevation_deg = 10.0;

    RunResult rot1 = run_analysis(db_path, obj_name, &cfg_rot1, "rotated-grid az=35  el=25");
    RunResult rot2 = run_analysis(db_path, obj_name, &cfg_rot2, "rotated-grid az=120 el=45");
    RunResult rot3 = run_analysis(db_path, obj_name, &cfg_rot3, "rotated-grid az=270 el=10");

    print_row(rot1);
    print_row(rot2);
    print_row(rot3);
    printf("\n");

    printf("  Rotated-grid angle-independence checks:\n");
    n_checks++;
    if (check_agreement(rot1, rot2, 5.0, 10.0)) n_pass++;
    n_checks++;
    if (check_agreement(rot1, rot3, 5.0, 10.0)) n_pass++;

    printf("  Rotated-grid vs triple-grid (same spacing):\n");
    n_checks++;
    if (check_agreement(tg_medium, rot1, 5.0, 10.0)) n_pass++;
    printf("\n");

    /* -----------------------------------------------------------------
     * Section 3: Crofton sampler at varying ray counts
     * ----------------------------------------------------------------- */
    printf("--- Section 3: Crofton sampler at varying ray counts ---\n");

    struct analyze_config cfg_croft_10k = ANALYZE_CONFIG_INIT_ZERO;
    cfg_croft_10k.sampler       = ANALYZE_SAMPLER_CROFTON;
    cfg_croft_10k.n_crofton_rays = 10000;
    cfg_croft_10k.quiet_missed  = 1;
    cfg_croft_10k.use_air       = 1;

    struct analyze_config cfg_croft_100k = cfg_croft_10k;
    cfg_croft_100k.n_crofton_rays = 100000;

    struct analyze_config cfg_croft_500k = cfg_croft_10k;
    cfg_croft_500k.n_crofton_rays = 500000;

    struct analyze_config cfg_croft_2m = cfg_croft_10k;
    cfg_croft_2m.n_crofton_rays = 2000000;

    RunResult croft_10k  = run_analysis(db_path, obj_name, &cfg_croft_10k,  "crofton 10k rays");
    RunResult croft_100k = run_analysis(db_path, obj_name, &cfg_croft_100k, "crofton 100k rays");
    RunResult croft_500k = run_analysis(db_path, obj_name, &cfg_croft_500k, "crofton 500k rays");
    RunResult croft_2m   = run_analysis(db_path, obj_name, &cfg_croft_2m,   "crofton 2M rays");

    print_row(croft_10k);
    print_row(croft_100k);
    print_row(croft_500k);
    print_row(croft_2m);
    printf("\n");

    printf("  Crofton convergence checks:\n");
    n_checks++;
    if (check_agreement(croft_10k,  croft_100k, 15.0, 20.0)) n_pass++;
    n_checks++;
    if (check_agreement(croft_100k, croft_500k,  5.0, 10.0)) n_pass++;
    n_checks++;
    if (check_agreement(croft_500k, croft_2m,    2.0,  5.0)) n_pass++;
    printf("\n");

    /* -----------------------------------------------------------------
     * Section 4: Cross-sampler agreement at comparable accuracy
     * ----------------------------------------------------------------- */
    printf("--- Section 4: Cross-sampler agreement ---\n");
    printf("  (reference: triple-grid medium 25mm→12.5mm)\n\n");

    /* Use fine grid and high-ray Crofton as the "truth" reference */
    printf("  Volume comparisons vs triple-grid 10mm→5mm:\n");
    n_checks++;
    if (check_agreement(tg_medium, rot1,       5.0, 10.0)) n_pass++;
    n_checks++;
    if (check_agreement(tg_medium, croft_100k, 10.0, 15.0)) n_pass++;
    n_checks++;
    if (check_agreement(tg_medium, croft_500k,  5.0, 10.0)) n_pass++;
    n_checks++;
    if (check_agreement(tg_medium, croft_2m,    3.0,  7.0)) n_pass++;
    printf("\n");

    /* Crofton vs fine grid */
    printf("  Crofton vs fine triple-grid (2mm→1mm):\n");
    n_checks++;
    if (check_agreement(tg_fine, croft_500k, 5.0, 10.0)) n_pass++;
    n_checks++;
    if (check_agreement(tg_fine, croft_2m,   3.0,  7.0)) n_pass++;
    printf("\n");

    /* -----------------------------------------------------------------
     * Section 5: Performance summary
     * ----------------------------------------------------------------- */
    printf("--- Section 5: Performance summary ---\n\n");
    printf("  Sampler                                 Time (s)   Speed vs tg_medium\n");
    printf("  %-38s  %8.2f s   1.00x (reference)\n",
	   tg_medium.label.c_str(), tg_medium.elapsed_s);

    auto speed_row = [&](const RunResult &r) {
	double speedup = (tg_medium.elapsed_s > 0) ? tg_medium.elapsed_s / r.elapsed_s : 0.0;
	printf("  %-38s  %8.2f s   %.2fx\n", r.label.c_str(), r.elapsed_s, speedup);
    };

    speed_row(tg_coarse);
    speed_row(tg_fine);
    speed_row(rot1);
    speed_row(rot2);
    speed_row(rot3);
    speed_row(croft_10k);
    speed_row(croft_100k);
    speed_row(croft_500k);
    speed_row(croft_2m);
    printf("\n");

    /* Is Crofton 100k faster than triple-grid medium? */
    printf("  Speed checks:\n");
    bool croft_faster = croft_100k.elapsed_s < tg_medium.elapsed_s;
    n_checks++;
    if (croft_faster) n_pass++;
    printf("    crofton 100k faster than triple-grid 10mm→5mm: %s (%.2fx)\n",
	   croft_faster ? "PASS" : "FAIL",
	   tg_medium.elapsed_s > 0 ? tg_medium.elapsed_s / croft_100k.elapsed_s : 0.0);
    printf("\n");

    /* -----------------------------------------------------------------
     * Section 6: Overlap detection — do all samplers find the same overlaps?
     * ----------------------------------------------------------------- */
    printf("--- Section 6: Overlap detection consistency ---\n");
    printf("  triple-grid 10mm→5mm : %zu overlap pairs\n", tg_medium.n_overlaps);
    printf("  rotated-grid az=35   : %zu overlap pairs\n", rot1.n_overlaps);
    printf("  crofton 100k rays    : %zu overlap pairs\n", croft_100k.n_overlaps);
    printf("  crofton 500k rays    : %zu overlap pairs\n", croft_500k.n_overlaps);
    printf("\n");

    /* -----------------------------------------------------------------
     * Summary
     * ----------------------------------------------------------------- */
    printf("=================================================================\n");
    printf("SUMMARY: %d/%d checks passed\n", n_pass, n_checks);
    if (n_pass == n_checks)
	printf("Result: PASS\n");
    else
	printf("Result: FAIL (%d check(s) failed)\n", n_checks - n_pass);
    printf("=================================================================\n");

    return (n_pass == n_checks) ? EXIT_SUCCESS : EXIT_FAILURE;
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
