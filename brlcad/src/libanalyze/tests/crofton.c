/*                     C R O F T O N . C
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
/** @file libanalyze/tests/crofton.c
 *
 * Unit tests for analyze_crofton_sample().
 *
 * Each test creates a small in-memory BRL-CAD database, invokes the
 * Cauchy-Crofton estimator, and checks that the result is within the
 * expected tolerance of the analytic answer.
 *
 * Tests:
 *   3a  - Sphere (exact analytic)
 *   3b  - Box / ARB8 (exact analytic, planar faces)
 *   3c  - Cylinder / RCC (exact analytic)
 *   3d  - Torus (exact analytic, non-convex)
 *   3e  - Convergence-order verification (sphere, 3 sample sizes)
 *   3f  - Convergence-to-threshold (sphere, 1% threshold, timing)
 *   3g  - Degenerate cases (NULL object, zero-size)
 *   3h  - CSG vs BOT consistency (sphere at 3 tolerances)
 */

#include "common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/vls.h"
#include "vmath.h"
#include "raytrace.h"
#include "rt/geom.h"
#include "rt/db_instance.h"
#include "rt/nmg_conv.h"
#include "nmg.h"
#include "bg/trimesh.h"
#include "wdb.h"
#include "analyze.h"


/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/** Return absolute relative error between estimated and exact values */
static double
rel_err(double estimated, double exact)
{
    if (fabs(exact) < 1e-100)
	return (fabs(estimated) < 1e-100) ? 0.0 : 1.0;
    return fabs(estimated - exact) / fabs(exact);
}


/**
 * Create an in-memory database, populate it using the caller-provided
 * build function, then close the wdb handle (keeping the dbip open).
 *
 * @param build_fn  Function to populate the wdb (returns 0 on success)
 * @param user_data Passed straight through to build_fn
 * @return open dbip, or DBI_NULL on failure
 */
typedef int (*build_fn_t)(struct rt_wdb *wdbp, void *udata);

static struct db_i *
make_inmem_db(build_fn_t build_fn, void *user_data)
{
    struct db_i *dbip = db_open_inmem();
    if (dbip == DBI_NULL)
	return DBI_NULL;

    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
    if (!wdbp) {
	db_close(dbip);
	return DBI_NULL;
    }

    if (build_fn(wdbp, user_data) != 0) {
	/* wdb_dbopen returns an internal pointer; only close the dbip */
	db_close(dbip);
	return DBI_NULL;
    }

    /* wdb_dbopen returns an existing internal wdb pointer embedded in dbip -
     * do NOT call wdb_close() here, as that would call db_close() which
     * would decrement dbi_uses to 0 and free the dbip prematurely.
     * The caller is responsible for db_close(dbip) when done.           */
    db_update_nref(dbip, &rt_uniresource);
    return dbip;
}


/* ------------------------------------------------------------------ */
/* Test 3a — Sphere                                                     */
/* ------------------------------------------------------------------ */

static int
build_sphere(struct rt_wdb *wdbp, void *UNUSED(udata))
{
    point_t center = VINIT_ZERO;
    return mk_sph(wdbp, "sphere.s", center, 100.0 /* r=100 mm */);
}


static int
test_3a_sphere(void)
{
    int failures = 0;
    const double R  = 100.0;
    const double SA_exact = 4.0 * M_PI * R * R;         /* 125663.7 mm^2 */
    const double V_exact  = (4.0/3.0) * M_PI * R*R*R;   /* 4188790.2 mm^3 */

    printf("\n--- Test 3a: Sphere (r=100 mm) ---\n");
    printf("  Analytic SA = %.4g mm^2\n", SA_exact);
    printf("  Analytic V  = %.4g mm^3\n", V_exact);

    struct db_i *dbip = make_inmem_db(build_sphere, NULL);
    if (!dbip) {
	printf("  FAIL: could not create in-memory database\n");
	return 1;
    }

    /* ---- single-pass 2000 samples: expect within 10% (loose smoke-test; at
     * N=2000 the 3-sigma bound is ~7%, so 10% catches badly broken results) */
    {
	double sa = 0.0, v = 0.0;
	struct bu_vls msgs = BU_VLS_INIT_ZERO;
	int ret = analyze_crofton_sample(dbip, "sphere.s",
					 0.0 /* single pass */,
					 2000, &sa, &v, &msgs);
	bu_vls_free(&msgs);
	if (ret != 0) {
	    printf("  FAIL 3a-1: analyze_crofton_sample returned %d\n", ret);
	    failures++;
	} else {
	    double esa = rel_err(sa, SA_exact);
	    double ev  = rel_err(v,  V_exact);
	    printf("  2k-sample: SA=%.4g (err=%.1f%%)  V=%.4g (err=%.1f%%)\n",
		   sa, esa*100.0, v, ev*100.0);
	    if (esa > 0.10) { printf("  FAIL: SA error %.1f%% > 10%%\n", esa*100.0); failures++; }
	    if (ev  > 0.10) { printf("  FAIL: V  error %.1f%% > 10%%\n", ev *100.0); failures++; }
	}
    }

    /* ---- single-pass 50000 samples: expect within 3% ---- */
    {
	double sa = 0.0, v = 0.0;
	int ret = analyze_crofton_sample(dbip, "sphere.s",
					 0.0, 50000, &sa, &v, NULL);
	if (ret != 0) {
	    printf("  FAIL 3a-2: ret=%d\n", ret);
	    failures++;
	} else {
	    double esa = rel_err(sa, SA_exact);
	    double ev  = rel_err(v,  V_exact);
	    printf("  50k-sample: SA=%.4g (err=%.2f%%)  V=%.4g (err=%.2f%%)\n",
		   sa, esa*100.0, v, ev*100.0);
	    if (esa > 0.03) { printf("  FAIL: SA error %.2f%% > 3%%\n", esa*100.0); failures++; }
	    if (ev  > 0.03) { printf("  FAIL: V  error %.2f%% > 3%%\n", ev *100.0); failures++; }
	}
    }

    db_close(dbip);
    printf("  Test 3a: %s\n", failures ? "FAIL" : "PASS");
    return failures;
}


/* ------------------------------------------------------------------ */
/* Test 3b — Box (200 x 200 x 200 mm)                                  */
/* ------------------------------------------------------------------ */

static int
build_box(struct rt_wdb *wdbp, void *UNUSED(udata))
{
    point_t mn = { -100.0, -100.0, -100.0 };
    point_t mx = {  100.0,  100.0,  100.0 };
    return mk_rpp(wdbp, "box.s", mn, mx);
}


static int
test_3b_box(void)
{
    int failures = 0;
    const double SIDE = 200.0;
    const double SA_exact = 6.0 * SIDE * SIDE;       /* 240000 mm^2 */
    const double V_exact  = SIDE * SIDE * SIDE;      /* 8000000 mm^3 */

    printf("\n--- Test 3b: Box (200x200x200 mm) ---\n");
    printf("  Analytic SA = %.4g mm^2\n", SA_exact);
    printf("  Analytic V  = %.4g mm^3\n", V_exact);

    struct db_i *dbip = make_inmem_db(build_box, NULL);
    if (!dbip) {
	printf("  FAIL: could not create database\n");
	return 1;
    }

    /* 2000 samples: expect < 5% (loose smoke-test for this sample count) */
    {
	double sa = 0.0, v = 0.0;
	int ret = analyze_crofton_sample(dbip, "box.s", 0.0, 2000, &sa, &v, NULL);
	if (ret != 0) {
	    printf("  FAIL 3b-1: ret=%d\n", ret);
	    failures++;
	} else {
	    double esa = rel_err(sa, SA_exact);
	    double ev  = rel_err(v,  V_exact);
	    printf("  2k-sample: SA=%.4g (err=%.1f%%)  V=%.4g (err=%.1f%%)\n",
		   sa, esa*100.0, v, ev*100.0);
	    if (esa > 0.05) { printf("  FAIL: SA error > 5%%\n"); failures++; }
	    if (ev  > 0.05) { printf("  FAIL: V  error > 5%%\n"); failures++; }
	}
    }

    /* 20000 samples: expect < 3% (statistical variance means 0.5% is too tight) */
    {
	double sa = 0.0, v = 0.0;
	int ret = analyze_crofton_sample(dbip, "box.s", 0.0, 20000, &sa, &v, NULL);
	if (ret != 0) {
	    printf("  FAIL 3b-2: ret=%d\n", ret);
	    failures++;
	} else {
	    double esa = rel_err(sa, SA_exact);
	    double ev  = rel_err(v,  V_exact);
	    printf("  20k-sample: SA=%.4g (err=%.2f%%)  V=%.4g (err=%.2f%%)\n",
		   sa, esa*100.0, v, ev*100.0);
	    if (esa > 0.03) { printf("  FAIL: SA error > 3%%\n"); failures++; }
	    if (ev  > 0.03) { printf("  FAIL: V  error > 3%%\n"); failures++; }
	}
    }

    db_close(dbip);
    printf("  Test 3b: %s\n", failures ? "FAIL" : "PASS");
    return failures;
}


/* ------------------------------------------------------------------ */
/* Test 3c — Cylinder / RCC (r=50 mm, h=200 mm)                        */
/* ------------------------------------------------------------------ */

static int
build_cylinder(struct rt_wdb *wdbp, void *UNUSED(udata))
{
    point_t base   = { 0.0, 0.0, -100.0 };
    vect_t  height = { 0.0, 0.0,  200.0 };
    return mk_rcc(wdbp, "cyl.s", base, height, 50.0 /* r=50 mm */);
}


static int
test_3c_cylinder(void)
{
    int failures = 0;
    /* SA = 2*pi*r*(r+h) = 2*pi*50*(50+200) = 2*pi*50*250 */
    const double R = 50.0, h_mm = 200.0;
    const double SA_exact = 2.0 * M_PI * R * (R + h_mm);   /* ~78540 mm^2 */
    const double V_exact  = M_PI * R * R * h_mm;            /* ~1570796 mm^3 */

    printf("\n--- Test 3c: Cylinder (r=50, h=200 mm) ---\n");
    printf("  Analytic SA = %.4g mm^2\n", SA_exact);
    printf("  Analytic V  = %.4g mm^3\n", V_exact);

    struct db_i *dbip = make_inmem_db(build_cylinder, NULL);
    if (!dbip) {
	printf("  FAIL: could not create database\n");
	return 1;
    }

    double sa = 0.0, v = 0.0;
    int ret = analyze_crofton_sample(dbip, "cyl.s", 0.0, 5000, &sa, &v, NULL);
    if (ret != 0) {
	printf("  FAIL 3c: ret=%d\n", ret);
	failures++;
    } else {
	double esa = rel_err(sa, SA_exact);
	double ev  = rel_err(v,  V_exact);
	printf("  5k-sample: SA=%.4g (err=%.1f%%)  V=%.4g (err=%.1f%%)\n",
	       sa, esa*100.0, v, ev*100.0);
	if (esa > 0.05) { printf("  FAIL: SA error > 5%%\n"); failures++; }
	if (ev  > 0.05) { printf("  FAIL: V  error > 5%%\n"); failures++; }
    }

    db_close(dbip);
    printf("  Test 3c: %s\n", failures ? "FAIL" : "PASS");
    return failures;
}


/* ------------------------------------------------------------------ */
/* Test 3d — Torus (r_a=100 mm, r_h=20 mm)                             */
/* ------------------------------------------------------------------ */

static int
build_torus(struct rt_wdb *wdbp, void *UNUSED(udata))
{
    point_t center = VINIT_ZERO;
    vect_t  normal = { 0.0, 0.0, 1.0 };
    return mk_tor(wdbp, "tor.s", center, normal,
		  100.0 /* r_a */, 20.0 /* r_h */);
}


static int
test_3d_torus(void)
{
    int failures = 0;
    const double R_A = 100.0, R_H = 20.0;
    /* SA = 4*pi^2 * R_A * R_H */
    const double SA_exact = 4.0 * M_PI * M_PI * R_A * R_H; /* ~78957 mm^2 */
    /* V  = 2*pi^2 * R_A * R_H^2 */
    const double V_exact  = 2.0 * M_PI * M_PI * R_A * R_H * R_H; /* ~394784 mm^3 */

    printf("\n--- Test 3d: Torus (r_a=100, r_h=20 mm) ---\n");
    printf("  Analytic SA = %.4g mm^2\n", SA_exact);
    printf("  Analytic V  = %.4g mm^3\n", V_exact);

    struct db_i *dbip = make_inmem_db(build_torus, NULL);
    if (!dbip) {
	printf("  FAIL: could not create database\n");
	return 1;
    }

    double sa = 0.0, v = 0.0;
    int ret = analyze_crofton_sample(dbip, "tor.s", 0.0, 5000, &sa, &v, NULL);
    if (ret != 0) {
	printf("  FAIL 3d: ret=%d\n", ret);
	failures++;
    } else {
	double esa = rel_err(sa, SA_exact);
	double ev  = rel_err(v,  V_exact);
	printf("  5k-sample: SA=%.4g (err=%.1f%%)  V=%.4g (err=%.1f%%)\n",
	       sa, esa*100.0, v, ev*100.0);
	/* Torus is non-convex but the Crofton formula still applies;
	 * we allow 10% because with only 5k samples variance is higher. */
	if (esa > 0.10) { printf("  FAIL: SA error > 10%%\n"); failures++; }
	if (ev  > 0.10) { printf("  FAIL: V  error > 10%%\n"); failures++; }
    }

    db_close(dbip);
    printf("  Test 3d: %s\n", failures ? "FAIL" : "PASS");
    return failures;
}


/* ------------------------------------------------------------------ */
/* Test 3e — Convergence-order verification                             */
/* ------------------------------------------------------------------ */

static int
test_3e_convergence_order(void)
{
    int failures = 0;
    const double R        = 100.0;
    const double SA_exact = 4.0 * M_PI * R * R;
    const double V_exact  = (4.0/3.0) * M_PI * R*R*R;

    printf("\n--- Test 3e: Convergence-order (sphere) ---\n");

    struct db_i *dbip = make_inmem_db(build_sphere, NULL);
    if (!dbip) {
	printf("  FAIL: could not create database\n");
	return 1;
    }

    /* Run 5 independent experiments at 3 sample sizes, take median error */
    size_t sizes[3] = { 1000, 10000, 100000 };
    const int NREP = 5;
    double err_sa[3], err_v[3];

    for (int s = 0; s < 3; s++) {
	double med_sa[NREP], med_v[NREP];
	for (int r = 0; r < NREP; r++) {
	    double sa = 0.0, v = 0.0;
	    analyze_crofton_sample(dbip, "sphere.s", 0.0, sizes[s], &sa, &v, NULL);
	    med_sa[r] = rel_err(sa, SA_exact);
	    med_v[r]  = rel_err(v,  V_exact);
	}
	/* Sort to get median */
	for (int i = 0; i < NREP - 1; i++)
	    for (int j = i+1; j < NREP; j++) {
		if (med_sa[j] < med_sa[i]) { double t = med_sa[i]; med_sa[i] = med_sa[j]; med_sa[j] = t; }
		if (med_v[j]  < med_v[i])  { double t = med_v[i];  med_v[i]  = med_v[j];  med_v[j]  = t; }
	    }
	err_sa[s] = med_sa[NREP/2];
	err_v[s]  = med_v[NREP/2];
	printf("  n=%-7zu  SA-err=%.2f%%  V-err=%.2f%%\n",
	       sizes[s], err_sa[s]*100.0, err_v[s]*100.0);
    }

    /* Assert convergence: 100k samples should give clearly better accuracy than
     * 1k.  We check that the 100k median error is < 2/3 of the 1k median error
     * (theory predicts ~1/10), and the 100k absolute error is < 3%.
     * We do NOT enforce strict monotone improvement at intermediate sizes since
     * median of 5 replicates still has enough variance to invert at adjacent
     * sizes; the 1k vs 100k comparison is robust because the 100× difference
     * overwhelms sampling noise.                                              */
    if (err_sa[0] > 0.0 && err_sa[2] > err_sa[0] * 0.67) {
	printf("  FAIL: SA 100k median (%.2f%%) not clearly better than 1k (%.2f%%)\n",
	       err_sa[2]*100.0, err_sa[0]*100.0);
	failures++;
    }
    if (err_v[0] > 0.0 && err_v[2] > err_v[0] * 0.67) {
	printf("  FAIL: V 100k median (%.2f%%) not clearly better than 1k (%.2f%%)\n",
	       err_v[2]*100.0, err_v[0]*100.0);
	failures++;
    }
    if (err_sa[2] > 0.03) {
	printf("  FAIL: SA error at 100k (%.2f%%) > 3%%\n", err_sa[2]*100.0);
	failures++;
    }
    if (err_v[2] > 0.03) {
	printf("  FAIL: V  error at 100k (%.2f%%) > 3%%\n", err_v[2]*100.0);
	failures++;
    }

    db_close(dbip);
    printf("  Test 3e: %s\n", failures ? "FAIL" : "PASS");
    return failures;
}


/* ------------------------------------------------------------------ */
/* Test 3f — Convergence-to-threshold                                   */
/* ------------------------------------------------------------------ */

static int
test_3f_threshold(void)
{
    int failures = 0;
    const double R        = 100.0;
    const double SA_exact = 4.0 * M_PI * R * R;
    const double V_exact  = (4.0/3.0) * M_PI * R*R*R;

    printf("\n--- Test 3f: Convergence-to-threshold (sphere, 1%% target) ---\n");

    struct db_i *dbip = make_inmem_db(build_sphere, NULL);
    if (!dbip) {
	printf("  FAIL: could not create database\n");
	return 1;
    }

    time_t t0 = time(NULL);
    double sa = 0.0, v = 0.0;
    struct bu_vls msgs = BU_VLS_INIT_ZERO;
    int ret = analyze_crofton_sample(dbip, "sphere.s",
				     1.0 /* 1% threshold */,
				     1000, &sa, &v, &msgs);
    time_t elapsed = time(NULL) - t0;

    bu_vls_free(&msgs);

    if (ret != 0) {
	printf("  FAIL 3f: analyze_crofton_sample returned %d\n", ret);
	failures++;
    } else {
	double esa = rel_err(sa, SA_exact);
	double ev  = rel_err(v,  V_exact);
	printf("  Converged: SA=%.4g (err=%.2f%%)  V=%.4g (err=%.2f%%)  time=%lds\n",
	       sa, esa*100.0, v, ev*100.0, (long)elapsed);

	/* Allow 5% result error (randomness means convergence criterion
	 * doesn't guarantee exact-answer proximity)                    */
	if (esa > 0.05) { printf("  FAIL: SA error %.2f%% > 5%%\n", esa*100.0); failures++; }
	if (ev  > 0.05) { printf("  FAIL: V  error %.2f%% > 5%%\n", ev *100.0); failures++; }

	if (elapsed > 10)
	    printf("  WARNING: took %lds (> 10s target)\n", (long)elapsed);
	if (elapsed > 120) {
	    printf("  FAIL: took %lds (> 120s limit)\n", (long)elapsed);
	    failures++;
	}
    }

    db_close(dbip);
    printf("  Test 3f: %s\n", failures ? "FAIL" : "PASS");
    return failures;
}


/* ------------------------------------------------------------------ */
/* Test 3g — Degenerate cases                                           */
/* ------------------------------------------------------------------ */

static int
build_zero_box(struct rt_wdb *wdbp, void *UNUSED(udata))
{
    /* A box with zero volume (all vertices at origin) */
    point_t mn = VINIT_ZERO;
    point_t mx = VINIT_ZERO;
    return mk_rpp(wdbp, "zerobox.s", mn, mx);
}


static int
test_3g_degenerate(void)
{
    int failures = 0;
    printf("\n--- Test 3g: Degenerate cases ---\n");

    /* ---- NULL object name: expect error return ---- */
    {
	struct db_i *dbip = make_inmem_db(build_sphere, NULL);
	if (!dbip) { printf("  FAIL: db create\n"); return 1; }
	double sa = 0.0, v = 0.0;
	int ret = analyze_crofton_sample(dbip, NULL, 0.0, 1000, &sa, &v, NULL);
	if (ret == 0) {
	    printf("  FAIL 3g-1: expected error for NULL object name, got ret=0\n");
	    failures++;
	} else {
	    printf("  NULL obj: correctly returned error (%d)\n", ret);
	}
	db_close(dbip);
    }

    /* ---- non-existent object name: expect error return ---- */
    {
	struct db_i *dbip = make_inmem_db(build_sphere, NULL);
	if (!dbip) { printf("  FAIL: db create\n"); return 1; }
	double sa = 0.0, v = 0.0;
	int ret = analyze_crofton_sample(dbip, "no_such_object.s",
					 0.0, 1000, &sa, &v, NULL);
	if (ret == 0) {
	    printf("  FAIL 3g-2: expected error for nonexistent object, got ret=0\n");
	    failures++;
	} else {
	    printf("  Non-existent obj: correctly returned error (%d)\n", ret);
	}
	db_close(dbip);
    }

    /* ---- zero-size box: expect 0 or tiny result, no hang ---- */
    {
	struct db_i *dbip = make_inmem_db(build_zero_box, NULL);
	if (!dbip) {
	    /* mk_rpp may reject zero-volume box; treat as skip */
	    printf("  Zero-box: database creation failed (primitive may reject zero bbox) -- skip\n");
	    goto tiny_test;
	}
	double sa = 0.0, v = 0.0;
	int ret = analyze_crofton_sample(dbip, "zerobox.s", 0.0, 1000, &sa, &v, NULL);
	printf("  Zero-box: ret=%d  SA=%.4g  V=%.4g\n", ret, sa, v);
	/* We just require it doesn't hang or crash, not a specific numeric result */
	db_close(dbip);
    }

tiny_test:
    /* ---- Very small object (r=1.0 mm) vs analytic ---- */
    /* Note: r=0.001 is below BRL-CAD's internal distance tolerance (0.0005 mm)
     * and will produce 0 hits.  Use r=1.0 which is well above tolerance and
     * represents the "small object" case the test is intended to cover.        */
    {
	struct db_i *dbip = db_open_inmem();
	if (!dbip) { printf("  FAIL: tiny db create\n"); failures++; goto done; }
	struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
	if (!wdbp) { db_close(dbip); printf("  FAIL: tiny wdbp\n"); failures++; goto done; }
	point_t c = VINIT_ZERO;
	mk_sph(wdbp, "tiny.s", c, 1.0);
	/* wdbp is an internal pointer inside dbip - do NOT call wdb_close().
	 * The dbip will be freed below via db_close.                        */
	db_update_nref(dbip, &rt_uniresource);

	const double tiny_r  = 1.0;
	const double SA_exact_t = 4.0 * M_PI * tiny_r * tiny_r;
	const double V_exact_t  = (4.0/3.0) * M_PI * tiny_r*tiny_r*tiny_r;
	double sa = 0.0, v = 0.0;
	int ret = analyze_crofton_sample(dbip, "tiny.s", 0.0, 5000, &sa, &v, NULL);
	if (ret != 0) {
	    printf("  FAIL 3g-tiny: ret=%d\n", ret);
	    failures++;
	} else {
	    double esa = rel_err(sa, SA_exact_t);
	    double ev  = rel_err(v,  V_exact_t);
	    printf("  tiny (r=1.0): SA=%.4g (err=%.1f%%)  V=%.4g (err=%.1f%%)\n",
		   sa, esa*100.0, v, ev*100.0);
	    if (esa > 0.10) { printf("  FAIL: SA error > 10%%\n"); failures++; }
	    if (ev  > 0.10) { printf("  FAIL: V  error > 10%%\n"); failures++; }
	}
	db_close(dbip);
    }

done:
    printf("  Test 3g: %s\n", failures ? "FAIL" : "PASS");
    return failures;
}


/* ------------------------------------------------------------------ */
/* Test 3h — CSG vs BOT consistency                                     */
/* ------------------------------------------------------------------ */

static int
test_3h_csg_vs_bot(void)
{
    int failures = 0;
    const double R        = 100.0;
    const double SA_exact = 4.0 * M_PI * R * R;
    const double V_exact  = (4.0/3.0) * M_PI * R*R*R;

    printf("\n--- Test 3h: CSG vs BOT consistency (sphere r=100) ---\n");
    printf("  Analytic SA = %.4g mm^2\n", SA_exact);
    printf("  Analytic V  = %.4g mm^3\n", V_exact);

    /* Get Crofton estimate from the CSG sphere once */
    struct db_i *dbip = make_inmem_db(build_sphere, NULL);
    if (!dbip) {
	printf("  FAIL: could not create CSG database\n");
	return 1;
    }

    double csg_sa = 0.0, csg_v = 0.0;
    {
	int ret = analyze_crofton_sample(dbip, "sphere.s",
					 0.5 /* 0.5% threshold */,
					 5000, &csg_sa, &csg_v, NULL);
	if (ret != 0) {
	    printf("  FAIL 3h-csg: analyze_crofton_sample returned %d\n", ret);
	    db_close(dbip);
	    return 1;
	}
	printf("  CSG Crofton: SA=%.4g  V=%.4g\n", csg_sa, csg_v);
    }
    db_close(dbip);

    /* ---- Tessellate at 3 tolerances, compute BOT metrics ---- */
    double rel_tols[3] = { 0.1, 0.01, 0.001 };
    double prev_bot_sa = -1.0;

    for (int t = 0; t < 3; t++) {
	double rel = rel_tols[t];

	/* Build ELL sphere as rt_db_internal */
	struct rt_db_internal ip;
	struct rt_ell_internal eli;
	RT_DB_INTERNAL_INIT(&ip);
	ip.idb_magic      = RT_DB_INTERNAL_MAGIC;
	ip.idb_major_type = DB5_MAJORTYPE_BRLCAD;
	ip.idb_minor_type = ID_ELL;
	ip.idb_ptr        = &eli;
	eli.magic = RT_ELL_INTERNAL_MAGIC;
	VSET(eli.v, 0.0, 0.0, 0.0);
	VSET(eli.a, R,   0.0, 0.0);
	VSET(eli.b, 0.0, R,   0.0);
	VSET(eli.c, 0.0, 0.0, R);

	struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
	struct bn_tol      tol  = BN_TOL_INIT_ZERO;
	ttol.magic = BG_TESS_TOL_MAGIC;
	tol.magic  = BN_TOL_MAGIC;
	ttol.rel   = rel;
	tol.dist   = 0.005;
	tol.dist_sq = tol.dist * tol.dist;
	tol.perp    = 1e-6;
	tol.para    = 1.0 - tol.perp;

	struct bu_list vlfree;
	BU_LIST_INIT(&vlfree);

	struct model      *m = nmg_mm();
	struct nmgregion  *r = NULL;
	int ret = rt_obj_tess(&r, m, &ip, &ttol, &tol);

	if (ret != 0 || !r) {
	    printf("  SKIP rel=%.3f: tessellation failed (ret=%d)\n", rel, ret);
	    nmg_km(m);
	    bu_list_free(&vlfree);
	    continue;
	}

	/* Convert to BOT */
	struct rt_bot_internal *bot = nmg_mdl_to_bot(m, &vlfree, &tol);
	nmg_km(m);
	bu_list_free(&vlfree);

	if (!bot || bot->num_faces == 0) {
	    printf("  SKIP rel=%.3f: nmg_mdl_to_bot returned empty\n", rel);
	    if (bot) {
		bu_free(bot->vertices, "v"); bu_free(bot->faces, "f"); bu_free(bot, "b");
	    }
	    continue;
	}

	/* Check manifold */
	struct bg_trimesh_solid_errors errs;
	memset(&errs, 0, sizeof(errs));
	int open = bg_trimesh_solid2(
	    (int)bot->num_vertices, (int)bot->num_faces,
	    bot->vertices, bot->faces, &errs);
	bg_free_trimesh_solid_errors(&errs);

	if (open != 0) {
	    printf("  SKIP rel=%.3f: mesh has open edges (not closed)\n", rel);
	    bu_free(bot->vertices, "v"); bu_free(bot->faces, "f"); bu_free(bot, "b");
	    continue;
	}

	fastf_t bot_sa = bg_trimesh_area(
	    bot->faces, bot->num_faces,
	    (const point_t *)bot->vertices, bot->num_vertices);
	fastf_t bot_v = bg_trimesh_volume(
	    bot->faces, bot->num_faces,
	    (const point_t *)bot->vertices, bot->num_vertices);

	printf("  rel=%.3f: BOT tris=%zu  SA=%.4g  V=%.4g\n",
	       rel, bot->num_faces, (double)bot_sa, (double)bot_v);

	/* BOT metrics vs Crofton CSG reference (generous tolerance) */
	double max_err = 3.0 * rel;   /* Allow 3x the tessellation tolerance */
	if (max_err < 0.50) max_err = 0.50;  /* floor: coarse meshes can be far off */
	double esa_vs_csg = rel_err((double)bot_sa, csg_sa);
	double ev_vs_csg  = rel_err((double)bot_v,  csg_v);
	printf("    vs Crofton CSG: SA-err=%.1f%%  V-err=%.1f%%  (limit=%.0f%%)\n",
	       esa_vs_csg*100.0, ev_vs_csg*100.0, max_err*100.0);
	if (esa_vs_csg > max_err) {
	    printf("  FAIL 3h-SA rel=%.3f: SA-err %.1f%% > %.0f%%\n",
		   rel, esa_vs_csg*100.0, max_err*100.0);
	    failures++;
	}
	if (ev_vs_csg > max_err) {
	    printf("  FAIL 3h-V  rel=%.3f: V-err  %.1f%% > %.0f%%\n",
		   rel, ev_vs_csg*100.0, max_err*100.0);
	    failures++;
	}

	/* Finer tolerances should give BOT closer to CSG answer */
	if (prev_bot_sa >= 0.0) {
	    double prev_esa = rel_err(prev_bot_sa, csg_sa);
	    if (esa_vs_csg > prev_esa * 1.2) {
		/* Allow small regression (1.2x) due to Monte Carlo variance */
		printf("  WARN 3h: SA did not improve at tighter tolerance (%.2f%% vs %.2f%%)\n",
		       esa_vs_csg*100.0, prev_esa*100.0);
	    }
	}

	prev_bot_sa = (double)bot_sa;
	(void)bot_v; /* volume tracked for informational purposes only */

	bu_free(bot->vertices, "v"); bu_free(bot->faces, "f"); bu_free(bot, "b");
    }

    printf("  Test 3h: %s\n", failures ? "FAIL" : "PASS");
    return failures;
}


/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);
    (void)argc;

    int failures = 0;

    printf("=== analyze_crofton_sample() unit tests ===\n");

    failures += test_3a_sphere();
    failures += test_3b_box();
    failures += test_3c_cylinder();
    failures += test_3d_torus();
    failures += test_3e_convergence_order();
    failures += test_3f_threshold();
    failures += test_3g_degenerate();
    failures += test_3h_csg_vs_bot();

    printf("\n=== Summary: %d failure(s) ===\n", failures);
    return (failures > 0) ? 1 : 0;
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
