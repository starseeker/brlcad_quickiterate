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
 * Tests for NMG tessellation of BRL-CAD primitives (TOR, ETO, TGC).
 *
 * Exercises:
 *  - Normal tessellation at various tolerance settings
 *  - Degenerate / edge-case primitives (zero radius, near-self-intersecting)
 *  - Chess-model-derived parameters that historically caused infinite loops
 *
 * Each test calls rt_obj_tess() and checks:
 *  - Whether the function returns without hanging
 *  - The return code (0 = success, negative = failure)
 *  - Optionally prints face/vertex counts
 */

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "bu.h"
#include "vmath.h"
#include "bg/defines.h"
#include "nmg.h"
#include "raytrace.h"
#include "rt/geom.h"


/* ------------------------------------------------------------------ */
/* Helper: run tess and report results                                  */
/* ------------------------------------------------------------------ */

/**
 * Run rt_obj_tess() with a given db_internal and tolerance set.
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
    struct model *m = nmg_mm();
    struct nmgregion *r = NULL;

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
	printf("  %-48s ret=%-3d faces=%-6d [%s]\n",
	       label, ret, nfaces, passed ? "PASS" : "FAIL");
    } else {
	printf("  %-48s ret=%-3d             [%s]\n",
	       label, ret, passed ? "PASS" : "FAIL");
    }

    nmg_km(m);
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

    return failures;
}




int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);

    if (argc > 1 && BU_STR_EQUAL(argv[1], "-h")) {
	printf("Usage: %s\n", argv[0]);
	printf("  Runs NMG tessellation tests for TOR, ETO, TGC, ELL,\n");
	printf("  EPA, EHY, RPC, RHC, HYP, and PART primitives.\n");
	printf("  Tests normal cases and degenerate/edge cases.\n");
	printf("  Returns 0 on all-pass, 1 on any failure.\n");
	return 0;
    }

    int total_failures = 0;

    total_failures += test_tor();
    total_failures += test_eto();
    total_failures += test_tgc();
    total_failures += test_ell();
    total_failures += test_epa();
    total_failures += test_ehy();
    total_failures += test_rpc();
    total_failures += test_rhc();
    total_failures += test_hyp();
    total_failures += test_part();

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
