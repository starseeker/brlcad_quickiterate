/*               D S P _ R A Y T R A C E _ C H E C K . C
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @file librt/tests/dsp_raytrace_check.c
 *
 * Consistency and timing comparison for the DSP raytrace paths:
 *
 *   Path A (BVH):      dsp_shot_bvh() — HLBVH over explicit triangles.
 *   Path B (DDA):      legacy HBB-pyramid + 2D Amanatides-Woo DDA.
 *
 * For each test geometry the program:
 *
 *   1. Preps an rt_i (BVH is built automatically in rt_dsp_prep).
 *   2. Runs rt_crofton_shoot — Path A (BVH).  Records SA, volume, wall time.
 *   3. Disables the BVH via dsp_bvh_root_swap(stp, NULL) on every DSP solid.
 *   4. Runs rt_crofton_shoot — Path B (DDA).  Records SA, volume, wall time.
 *   5. Restores the BVH pointers.
 *   6. For flat/ramp geometries: compares both paths against the analytical
 *      SA/volume.
 *   7. Checks that the BVH and DDA Crofton estimates agree within a tolerance.
 *   8. Prints a timing summary including prep time, Crofton time per path,
 *      effective rays/second, and the BVH/DDA speedup ratio.
 *
 * Usage:
 *   dsp_raytrace_check                          (run synthetic cases only)
 *   dsp_raytrace_check <file.g> <object-name>  (also run a real .g file)
 *
 * The real-.g-file test does NOT require analytical values; it only checks
 * that the two Crofton paths agree within DSP_AGREE_PCT percent.
 */

#include "common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bu/time.h"
#include "rt/defines.h"
#include "raytrace.h"
#include "wdb.h"

/* ------------------------------------------------------------------ */
/* Tuneable parameters                                                  */
/* ------------------------------------------------------------------ */

/* Number of Crofton rays fired for the accuracy comparison pass. */
#define CROFTON_ACCURACY_RAYS   20000u

/* Convergence threshold for the accuracy comparison pass (%). */
#define CROFTON_ACCURACY_THRESH  3.0

/* Number of Crofton rays for the single-iteration timing pass. */
#define CROFTON_TIMING_RAYS     50000u

/* Maximum acceptable relative disagreement between BVH and DDA (%). */
#define DSP_AGREE_PCT            5.0

/* Maximum acceptable relative error vs. analytical value (%). */
#define DSP_ANALYTIC_PCT         6.0


/* ------------------------------------------------------------------ */
/* Internal accessor declared in dsp.c (not in any public header).     */
/* ------------------------------------------------------------------ */
extern void *dsp_bvh_root_swap(struct soltab *stp, void *new_root);


/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static double
pct_err(double estimated, double exact)
{
    if (fabs(exact) < SMALL_FASTF)
	return (fabs(estimated) < SMALL_FASTF) ? 0.0 : 100.0;
    return fabs(estimated - exact) / fabs(exact) * 100.0;
}


/**
 * Walk all DSP solids in rtip and swap their bvh_root pointers.
 * Returns a freshly allocated array of the old pointer values (caller must
 * bu_free it).  *count_out receives the number of DSP solids found.
 */
static void **
dsp_swap_all_bvh(struct rt_i *rtip, void *new_root, size_t *count_out)
{
    size_t n = 0;
    struct soltab *stp;

    /* Count DSP solids first. */
    RT_VISIT_ALL_SOLTABS_START(stp, rtip) {
	if (stp->st_id == ID_DSP)
	    n++;
    } RT_VISIT_ALL_SOLTABS_END;

    if (!n) {
	*count_out = 0;
	return NULL;
    }

    void **saved = (void **)bu_calloc(n, sizeof(void *), "bvh saved roots");
    size_t i = 0;

    RT_VISIT_ALL_SOLTABS_START(stp, rtip) {
	if (stp->st_id == ID_DSP) {
	    saved[i++] = dsp_bvh_root_swap(stp, new_root);
	}
    } RT_VISIT_ALL_SOLTABS_END;

    *count_out = n;
    return saved;
}


/**
 * Restore bvh_root pointers saved by dsp_swap_all_bvh().
 */
static void
dsp_restore_all_bvh(struct rt_i *rtip, void **saved_roots, size_t count)
{
    size_t i = 0;
    struct soltab *stp;

    RT_VISIT_ALL_SOLTABS_START(stp, rtip) {
	if (stp->st_id == ID_DSP && i < count) {
	    dsp_bvh_root_swap(stp, saved_roots[i++]);
	}
    } RT_VISIT_ALL_SOLTABS_END;
}


/* ------------------------------------------------------------------ */
/* One timed Crofton run                                                */
/* ------------------------------------------------------------------ */

struct crofton_result {
    double sa;
    double vol;
    double wall_sec;
    int    ok;        /* 0 = rt_crofton_shoot returned ok */
};

static struct crofton_result
run_crofton(struct rt_i *rtip, size_t nrays, double thresh_pct)
{
    struct crofton_result r;
    memset(&r, 0, sizeof(r));
    int64_t t0 = bu_gettime();
    int rc = rt_crofton_shoot(rtip, nrays, thresh_pct, &r.sa, &r.vol);
    r.wall_sec = (double)(bu_gettime() - t0) / 1e6;
    r.ok = rc;
    return r;
}


/* ------------------------------------------------------------------ */
/* Core comparison function                                             */
/* ------------------------------------------------------------------ */

/**
 * Run both BVH and DDA Crofton passes on @p rtip (already prepped).
 *
 * @param label          Human-readable test case label.
 * @param rtip           Prepared RT instance.
 * @param analytic_sa    Known exact SA, or -1.
 * @param analytic_vol   Known exact volume, or -1.
 * @param prep_sec       Time taken by rt_prep_parallel (caller measured).
 * @param bvh_ntris      Number of BVH triangles (0 if unknown).
 * @returns              Number of failures.
 */
static int
compare_paths(const char *label,
	      struct rt_i *rtip,
	      double analytic_sa,
	      double analytic_vol,
	      double prep_sec,
	      size_t bvh_ntris)
{
    int failures = 0;

    printf("\n  %-50s\n", label);
    if (prep_sec > 0.0)
	printf("    Prep (BVH build incl.): %.3f s\n", prep_sec);
    if (bvh_ntris)
	printf("    BVH triangle count:     %zu\n", bvh_ntris);

    /* ---- Path A: BVH (bvh_root is already set from prep) ---- */
    struct crofton_result bvh = run_crofton(rtip,
					    CROFTON_ACCURACY_RAYS,
					    CROFTON_ACCURACY_THRESH);

    /* ---- Timing pass A: fixed rays, no convergence loop ---- */
    struct crofton_result bvh_t = run_crofton(rtip, CROFTON_TIMING_RAYS, 0.0);

    /* ---- Path B: DDA (disable BVH) ---- */
    size_t ndsp = 0;
    void **saved = dsp_swap_all_bvh(rtip, NULL, &ndsp);
    if (!ndsp) {
	printf("    WARNING: no DSP solids found in rt_i\n");
	bu_free(saved, "saved bvh roots");
	return 1;
    }

    struct crofton_result dda = run_crofton(rtip,
					    CROFTON_ACCURACY_RAYS,
					    CROFTON_ACCURACY_THRESH);

    /* ---- Timing pass B ---- */
    struct crofton_result dda_t = run_crofton(rtip, CROFTON_TIMING_RAYS, 0.0);

    /* ---- Restore BVH ---- */
    dsp_restore_all_bvh(rtip, saved, ndsp);
    bu_free(saved, "saved bvh roots");

    /* ---- Print SA comparison ---- */
    double sa_agree = pct_err(bvh.sa, dda.sa);
    double vol_agree = pct_err(bvh.vol, dda.vol);

    printf("\n    %-8s  %12s  %12s  %12s  %12s\n",
	   "PATH", "SA", "VOL", "SA_err%", "VOL_err%");
    printf("    %-8s  %12.4g  %12.4g\n",
	   "Analytic",
	   (analytic_sa  > 0.0) ? analytic_sa  : 0.0,
	   (analytic_vol > 0.0) ? analytic_vol : 0.0);

    double bvh_sa_err  = (analytic_sa  > 0) ? pct_err(bvh.sa,  analytic_sa)  : -1.0;
    double bvh_vol_err = (analytic_vol > 0) ? pct_err(bvh.vol, analytic_vol) : -1.0;
    double dda_sa_err  = (analytic_sa  > 0) ? pct_err(dda.sa,  analytic_sa)  : -1.0;
    double dda_vol_err = (analytic_vol > 0) ? pct_err(dda.vol, analytic_vol) : -1.0;

    if (bvh_sa_err >= 0)
	printf("    %-8s  %12.4g  %12.4g  %11.2f%%  %11.2f%%\n",
	       "BVH", bvh.sa, bvh.vol, bvh_sa_err, bvh_vol_err);
    else
	printf("    %-8s  %12.4g  %12.4g\n", "BVH", bvh.sa, bvh.vol);

    if (dda_sa_err >= 0)
	printf("    %-8s  %12.4g  %12.4g  %11.2f%%  %11.2f%%\n",
	       "DDA", dda.sa, dda.vol, dda_sa_err, dda_vol_err);
    else
	printf("    %-8s  %12.4g  %12.4g\n", "DDA", dda.sa, dda.vol);

    printf("    BVH vs DDA agreement:  SA %.2f%%  Vol %.2f%%\n",
	   sa_agree, vol_agree);

    /* ---- Timing summary ---- */
    double bvh_rps = (bvh_t.wall_sec > 1e-9)
	? CROFTON_TIMING_RAYS / bvh_t.wall_sec : 0.0;
    double dda_rps = (dda_t.wall_sec > 1e-9)
	? CROFTON_TIMING_RAYS / dda_t.wall_sec : 0.0;
    double speedup = (dda_t.wall_sec > 1e-9 && bvh_t.wall_sec > 1e-9)
	? dda_t.wall_sec / bvh_t.wall_sec : 0.0;

    printf("\n    %-8s  %8s  %12s  %10s\n",
	   "PATH", "rays", "wall_sec", "rays/sec");
    printf("    %-8s  %8u  %12.4f  %10.0f\n",
	   "BVH", CROFTON_TIMING_RAYS, bvh_t.wall_sec, bvh_rps);
    printf("    %-8s  %8u  %12.4f  %10.0f\n",
	   "DDA", CROFTON_TIMING_RAYS, dda_t.wall_sec, dda_rps);
    if (speedup > 0.0)
	printf("    BVH speedup vs DDA:  %.2fx  (%s)\n",
	       speedup,
	       (speedup > 1.0) ? "BVH faster" :
	       (speedup < 1.0) ? "DDA faster" : "same");

    /* ---- Pass/fail checks ---- */
    const char *sa_agree_tag  = (sa_agree  <= DSP_AGREE_PCT) ? "OK" : "FAIL";
    const char *vol_agree_tag = (vol_agree <= DSP_AGREE_PCT) ? "OK" : "FAIL";
    printf("    Agreement:  SA[%s]  Vol[%s]\n", sa_agree_tag, vol_agree_tag);

    if (sa_agree  > DSP_AGREE_PCT) {
	printf("    FAIL: BVH/DDA SA  disagreement %.2f%% > %.1f%%\n",
	       sa_agree, DSP_AGREE_PCT);
	failures++;
    }
    if (vol_agree > DSP_AGREE_PCT) {
	printf("    FAIL: BVH/DDA vol disagreement %.2f%% > %.1f%%\n",
	       vol_agree, DSP_AGREE_PCT);
	failures++;
    }

    if (analytic_sa > 0.0 && bvh_sa_err > DSP_ANALYTIC_PCT) {
	printf("    FAIL: BVH SA vs analytic %.2f%% > %.1f%%\n",
	       bvh_sa_err, DSP_ANALYTIC_PCT);
	failures++;
    }
    if (analytic_vol > 0.0 && bvh_vol_err > DSP_ANALYTIC_PCT) {
	printf("    FAIL: BVH vol vs analytic %.2f%% > %.1f%%\n",
	       bvh_vol_err, DSP_ANALYTIC_PCT);
	failures++;
    }
    if (analytic_sa > 0.0 && dda_sa_err > DSP_ANALYTIC_PCT) {
	printf("    FAIL: DDA SA vs analytic %.2f%% > %.1f%%\n",
	       dda_sa_err, DSP_ANALYTIC_PCT);
	failures++;
    }
    if (analytic_vol > 0.0 && dda_vol_err > DSP_ANALYTIC_PCT) {
	printf("    FAIL: DDA vol vs analytic %.2f%% > %.1f%%\n",
	       dda_vol_err, DSP_ANALYTIC_PCT);
	failures++;
    }

    return failures;
}


/* ------------------------------------------------------------------ */
/* Build an in-memory DB with a single DSP solid and run comparison    */
/* ------------------------------------------------------------------ */

struct dsp_case {
    const char *label;
    uint32_t xcnt, ycnt;
    const unsigned short *buf;
    double analytic_sa;
    double analytic_vol;
};

/**
 * Analytical SA and volume for a flat DSP in solid (grid) space with
 * identity stom (1 unit per cell, 1 unit per height step):
 *
 *   nx = xcnt-1 cells wide,  ny = ycnt-1 cells tall,  h = height value
 *
 *   Top  = nx*ny
 *   Bot  = nx*ny
 *   X-walls: each 1 unit wide, h units tall → 2 * ny * h
 *   Y-walls: each 1 unit wide, h units tall → 2 * nx * h
 *
 *   SA  = 2*nx*ny + 2*h*(nx+ny)
 *   Vol = nx*ny*h
 */
static void
flat_dsp_analytic(uint32_t xcnt, uint32_t ycnt, unsigned short h,
		  double *sa_out, double *vol_out)
{
    double nx = xcnt - 1;
    double ny = ycnt - 1;
    double z  = (double)h;
    *sa_out  = 2.0*nx*ny + 2.0*z*(nx + ny);
    *vol_out = nx * ny * z;
}


static int
run_inmem_case(const struct dsp_case *tc)
{
    /* Build in-memory DB */
    struct db_i *dbip = db_open_inmem();
    if (!dbip) {
	printf("  FAIL: db_open_inmem\n");
	return 1;
    }

    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
    if (!wdbp) {
	db_close(dbip);
	return 1;
    }

    /* Create the binary data object */
    const char *data_name = "dsp_test.data";
    const char *dsp_name  = "dsp_test.s";

    if (mk_binunif(wdbp, data_name,
		   (const void *)tc->buf,
		   WDB_BINUNIF_UINT16,
		   (long)(tc->xcnt * tc->ycnt)) < 0) {
	db_close(dbip);
	return 1;
    }

    /* Build the DSP primitive with identity stom */
    struct rt_dsp_internal *dsp;
    BU_ALLOC(dsp, struct rt_dsp_internal);
    dsp->magic       = RT_DSP_INTERNAL_MAGIC;
    dsp->dsp_xcnt    = tc->xcnt;
    dsp->dsp_ycnt    = tc->ycnt;
    dsp->dsp_smooth  = 0;
    dsp->dsp_cuttype = DSP_CUT_DIR_llUR;
    dsp->dsp_datasrc = RT_DSP_SRC_OBJ;
    bu_vls_init(&dsp->dsp_name);
    bu_vls_strcpy(&dsp->dsp_name, data_name);
    MAT_IDN(dsp->dsp_stom);
    MAT_IDN(dsp->dsp_mtos);

    /* dsp_bip is filled in by rt_dsp_prep when datasrc==RT_DSP_SRC_OBJ */
    dsp->dsp_bip = NULL;
    dsp->dsp_mp  = NULL;

    if (wdb_export(wdbp, dsp_name, (void *)dsp, ID_DSP, 1.0) < 0) {
	bu_vls_free(&dsp->dsp_name);
	bu_free(dsp, "dsp");
	db_close(dbip);
	return 1;
    }

    db_update_nref(dbip, &rt_uniresource);

    /* Prep */
    struct rt_i *rtip = rt_new_rti(dbip);
    if (!rtip) {
	db_close(dbip);
	return 1;
    }

    if (rt_gettree(rtip, dsp_name) != 0) {
	rt_free_rti(rtip);
	db_close(dbip);
	return 1;
    }

    int64_t t0 = bu_gettime();
    rt_prep_parallel(rtip, 1);
    double prep_sec = (double)(bu_gettime() - t0) / 1e6;

    int failures = compare_paths(tc->label, rtip,
				 tc->analytic_sa, tc->analytic_vol,
				 prep_sec, 0);

    rt_free_rti(rtip);
    db_close(dbip);
    return failures;
}


/* ------------------------------------------------------------------ */
/* Run a real .g file                                                   */
/* ------------------------------------------------------------------ */

static int
run_file_case(const char *gfile, const char *objname)
{
    char label[256];
    snprintf(label, sizeof(label), "real terrain: %s / %s", gfile, objname);

    struct db_i *dbip = db_open(gfile, DB_OPEN_READONLY);
    if (!dbip) {
	printf("  FAIL: cannot open %s\n", gfile);
	return 1;
    }
    db_dirbuild(dbip);

    struct rt_i *rtip = rt_new_rti(dbip);
    if (!rtip) {
	db_close(dbip);
	return 1;
    }

    if (rt_gettree(rtip, objname) != 0) {
	printf("  FAIL: rt_gettree('%s') failed in %s\n", objname, gfile);
	rt_free_rti(rtip);
	db_close(dbip);
	return 1;
    }

    int64_t t0 = bu_gettime();
    rt_prep_parallel(rtip, 1);
    double prep_sec = (double)(bu_gettime() - t0) / 1e6;

    int failures = compare_paths(label, rtip,
				 -1.0 /* no analytic */,
				 -1.0 /* no analytic */,
				 prep_sec, 0);

    rt_free_rti(rtip);
    db_close(dbip);
    return failures;
}


/* ------------------------------------------------------------------ */
/* Synthetic test cases                                                 */
/* ------------------------------------------------------------------ */

static int
test_synthetic(void)
{
    int failures = 0;

    printf("\n=== Synthetic DSP cases ===\n");

    /* --- 1. Flat 5×5 grid (4×4 cells), height = 100 --- */
    {
	const uint32_t GW = 5, GH = 5;
	unsigned short buf[25];
	for (int i = 0; i < 25; i++) buf[i] = 100;

	double asa, avol;
	flat_dsp_analytic(GW, GH, 100, &asa, &avol);

	struct dsp_case tc = {
	    "flat 5x5 h=100 (4x4 cells)", GW, GH, buf, asa, avol
	};
	failures += run_inmem_case(&tc);
    }

    /* --- 2. Flat 10×10 grid (9×9 cells), height = 200 --- */
    {
	const uint32_t GW = 10, GH = 10;
	unsigned short buf[100];
	for (int i = 0; i < 100; i++) buf[i] = 200;

	double asa, avol;
	flat_dsp_analytic(GW, GH, 200, &asa, &avol);

	struct dsp_case tc = {
	    "flat 10x10 h=200 (9x9 cells)", GW, GH, buf, asa, avol
	};
	failures += run_inmem_case(&tc);
    }

    /* --- 3. Linear ramp 9×9 grid --- */
    {
	const uint32_t GW = 9, GH = 9;
	unsigned short buf[81];
	for (uint32_t y = 0; y < GH; y++)
	    for (uint32_t x = 0; x < GW; x++)
		buf[y * GW + x] = (unsigned short)(100 + 20 * x + 10 * y);

	struct dsp_case tc = {
	    "ramp 9x9 (no analytic)", GW, GH, buf, -1.0, -1.0
	};
	failures += run_inmem_case(&tc);
    }

    /* --- 4. 16×16 sinusoidal terrain --- */
    {
	const uint32_t GW = 16, GH = 16;
	unsigned short buf[256];
	for (uint32_t y = 0; y < GH; y++)
	    for (uint32_t x = 0; x < GW; x++) {
		double fx = (double)x / (GW - 1) * M_PI;
		double fy = (double)y / (GH - 1) * M_PI;
		buf[y * GW + x] = (unsigned short)(int)(300.0 + 150.0 * sin(fx) * sin(fy));
	    }

	struct dsp_case tc = {
	    "sinusoidal 16x16 (no analytic)", GW, GH, buf, -1.0, -1.0
	};
	failures += run_inmem_case(&tc);
    }

    /* --- 5. Larger 33×33 complex terrain --- */
    {
	const uint32_t GW = 33, GH = 33;
	unsigned short *buf = (unsigned short *)bu_calloc(GW * H,
							  sizeof(unsigned short),
							  "33x33 buf");
	for (uint32_t y = 0; y < GH; y++)
	    for (uint32_t x = 0; x < GW; x++) {
		double fx = (double)x / (GW - 1) * 6.2832;
		double fy = (double)y / (GH - 1) * 6.2832;
		buf[y * GW + x] = (unsigned short)(int)(
		    500.0 + 200.0 * sin(fx) + 150.0 * cos(2.0 * fy));
	    }

	struct dsp_case tc = {
	    "complex 33x33 wave (no analytic)", GW, GH, buf, -1.0, -1.0
	};
	failures += run_inmem_case(&tc);

	bu_free(buf, "33x33 buf");
    }

    printf("\n=== Synthetic: %d failure(s) ===\n", failures);
    return failures;
}


/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);

    int failures = 0;

    /* Synthetic cases always run */
    failures += test_synthetic();

    /* Optional real .g file */
    if (argc == 3) {
	printf("\n=== Real terrain file: %s  object: %s ===\n",
	       argv[1], argv[2]);
	failures += run_file_case(argv[1], argv[2]);
    } else if (argc != 1) {
	bu_exit(1, "Usage: %s [<file.g> <object-name>]\n", argv[0]);
    }

    printf("\n=== Overall: %d failure(s) ===\n", failures);
    return (failures > 0) ? 1 : 0;
}
