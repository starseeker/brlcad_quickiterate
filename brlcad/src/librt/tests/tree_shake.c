/*                    T R E E _ S H A K E . C
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
/** @file librt/tests/tree_shake.c
 *
 * Correctness tests for the prep-time CSG tree shaker
 * (rt_tree_shake_subs / rt_tree_prune_subtractor) added to
 * rt_gettrees_and_attrs().
 *
 * Three scenarios are exercised:
 *
 *  1. Disjoint SUBTRACT (should prune):
 *     region = sphA - sphB, where sphB is far from sphA.
 *     After rt_gettrees the shaker should eliminate sphB and leave
 *     only sphA in rti_headsolid.
 *
 *  2. Overlapping SUBTRACT (must NOT prune):
 *     region = sphA - sphC, where sphC overlaps sphA.
 *     Both solids must remain after rt_gettrees.
 *
 *  3. Union-of-subtractors, partially disjoint:
 *     sub_grp (non-region comb) = sphD (far) ∪ sphE (overlapping)
 *     region = sphA - sub_grp
 *     The shaker should prune sphD but keep sphE, leaving 2 soltabs
 *     (sphA + sphE).
 *
 * Each test creates a fresh in-memory .g, calls rt_gettrees (which
 * triggers the shaker), then counts the surviving soltabs in
 * rti_headsolid via RT_VISIT_ALL_SOLTABS_START.
 */

#include "common.h"

#include <stdio.h>

#include "bu/app.h"
#include "vmath.h"
#include "raytrace.h"
#include "wdb.h"


/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/*
 * Count the number of soltab entries currently alive in rtip->rti_headsolid.
 */
static int
count_soltabs(struct rt_i *rtip)
{
    struct soltab *stp;
    int n = 0;
    RT_VISIT_ALL_SOLTABS_START(stp, rtip) {
	(void)stp;
	n++;
    } RT_VISIT_ALL_SOLTABS_END;
    return n;
}


/*
 * Build a fresh in-memory .g, create the named primitives and
 * combination, run rt_gettrees, then return the rt_i.
 *
 * The caller owns the returned rt_i and the wdb pointer (close with
 * rt_free_rti followed by wdb_close).  Returns NULL on failure.
 */
typedef int (*build_fn)(struct rt_wdb *wdbp);

static struct rt_i *
build_and_load(build_fn builder, const char *top_obj, struct rt_wdb **wdbp_out)
{
    struct db_i *dbip;
    struct rt_wdb *wdbp;
    struct rt_i *rtip;
    const char *argv[2];

    dbip = db_open_inmem();
    if (!dbip) {
	bu_log("ERROR: db_open_inmem failed\n");
	return NULL;
    }

    wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp) {
	bu_log("ERROR: wdb_dbopen failed\n");
	db_close(dbip);
	return NULL;
    }

    if (builder(wdbp) != 0) {
	bu_log("ERROR: geometry builder failed\n");
	wdb_close(wdbp);
	return NULL;
    }

    db_update_nref(dbip, &rt_uniresource);

    rtip = rt_new_rti(dbip);
    if (!rtip) {
	bu_log("ERROR: rt_new_rti failed\n");
	wdb_close(wdbp);
	return NULL;
    }

    argv[0] = top_obj;
    argv[1] = NULL;
    if (rt_gettrees(rtip, 1, argv, 1) < 0) {
	bu_log("ERROR: rt_gettrees failed\n");
	rt_free_rti(rtip);
	wdb_close(wdbp);
	return NULL;
    }

    *wdbp_out = wdbp;
    return rtip;
}


/* ------------------------------------------------------------------ */
/* Test 1: Disjoint SUBTRACT – subtractor should be pruned            */
/* ------------------------------------------------------------------ */

/*
 * region1.r = sphA (origin, r=10) - sphB ((1000,0,0), r=5)
 * sphB is completely outside sphA.  After the tree shaker, sphB's
 * soltab should be freed and only sphA should remain.
 */
static int
build_disjoint(struct rt_wdb *wdbp)
{
    point_t center;
    struct wmember wm;

    VSET(center, 0.0, 0.0, 0.0);
    if (mk_sph(wdbp, "sphA.s", center, 10.0) != 0) return 1;

    VSET(center, 1000.0, 0.0, 0.0);
    if (mk_sph(wdbp, "sphB.s", center, 5.0) != 0) return 1;

    BU_LIST_INIT(&wm.l);
    mk_addmember("sphA.s", &wm.l, NULL, WMOP_UNION);
    mk_addmember("sphB.s", &wm.l, NULL, WMOP_SUBTRACT);
    if (mk_lcomb(wdbp, "region1.r", &wm, 1, NULL, NULL, NULL, 0) != 0) return 1;

    return 0;
}

static int
test_disjoint_subtract(void)
{
    struct rt_wdb *wdbp = NULL;
    struct rt_i *rtip = build_and_load(build_disjoint, "region1.r", &wdbp);
    int nsols, failures = 0;

    if (!rtip) return 1;

    nsols = count_soltabs(rtip);
    if (nsols != 1) {
	bu_log("FAIL test_disjoint_subtract: expected 1 soltab after shake, got %d\n",
	       nsols);
	failures++;
    } else {
	printf("PASS: test_disjoint_subtract (1 soltab after pruning disjoint subtractor)\n");
    }

    rt_free_rti(rtip);
    wdb_close(wdbp);		/* also closes the dbip */
    return failures;
}


/* ------------------------------------------------------------------ */
/* Test 2: Overlapping SUBTRACT – subtractor must be kept             */
/* ------------------------------------------------------------------ */

/*
 * region2.r = sphA (origin, r=10) - sphC ((5,0,0), r=5)
 * sphC overlaps sphA.  The shaker must not prune sphC.
 */
static int
build_overlapping(struct rt_wdb *wdbp)
{
    point_t center;
    struct wmember wm;

    VSET(center, 0.0, 0.0, 0.0);
    if (mk_sph(wdbp, "sphA2.s", center, 10.0) != 0) return 1;

    VSET(center, 5.0, 0.0, 0.0);
    if (mk_sph(wdbp, "sphC.s", center, 5.0) != 0) return 1;

    BU_LIST_INIT(&wm.l);
    mk_addmember("sphA2.s", &wm.l, NULL, WMOP_UNION);
    mk_addmember("sphC.s",  &wm.l, NULL, WMOP_SUBTRACT);
    if (mk_lcomb(wdbp, "region2.r", &wm, 1, NULL, NULL, NULL, 0) != 0) return 1;

    return 0;
}

static int
test_overlapping_subtract(void)
{
    struct rt_wdb *wdbp = NULL;
    struct rt_i *rtip = build_and_load(build_overlapping, "region2.r", &wdbp);
    int nsols, failures = 0;

    if (!rtip) return 1;

    nsols = count_soltabs(rtip);
    if (nsols != 2) {
	bu_log("FAIL test_overlapping_subtract: expected 2 soltabs (no pruning), got %d\n",
	       nsols);
	failures++;
    } else {
	printf("PASS: test_overlapping_subtract (2 soltabs kept for overlapping subtractor)\n");
    }

    rt_free_rti(rtip);
    wdb_close(wdbp);
    return failures;
}


/* ------------------------------------------------------------------ */
/* Test 3: UNION subtractor, one branch disjoint, one overlapping     */
/* ------------------------------------------------------------------ */

/*
 * sub_grp3  (non-region) = sphD ((100,0,0), r=5) ∪ sphE ((3,0,0), r=2)
 * region3.r = sphA3 (origin, r=10) - sub_grp3
 *
 * sphD is entirely outside sphA3; sphE overlaps sphA3.
 * The shaker should prune sphD but keep sphE.
 * Expected soltabs after rt_gettrees: 2 (sphA3 + sphE).
 */
static int
build_union_subtractor(struct rt_wdb *wdbp)
{
    point_t center;
    struct wmember wm;

    VSET(center, 0.0, 0.0, 0.0);
    if (mk_sph(wdbp, "sphA3.s", center, 10.0) != 0) return 1;

    VSET(center, 100.0, 0.0, 0.0);
    if (mk_sph(wdbp, "sphD.s", center, 5.0) != 0) return 1;

    VSET(center, 3.0, 0.0, 0.0);
    if (mk_sph(wdbp, "sphE.s", center, 2.0) != 0) return 1;

    /* non-region combination: sphD ∪ sphE */
    BU_LIST_INIT(&wm.l);
    mk_addmember("sphD.s", &wm.l, NULL, WMOP_UNION);
    mk_addmember("sphE.s", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "sub_grp3.g", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    /* region: sphA3 - sub_grp3 */
    BU_LIST_INIT(&wm.l);
    mk_addmember("sphA3.s",  &wm.l, NULL, WMOP_UNION);
    mk_addmember("sub_grp3.g", &wm.l, NULL, WMOP_SUBTRACT);
    if (mk_lcomb(wdbp, "region3.r", &wm, 1, NULL, NULL, NULL, 0) != 0) return 1;

    return 0;
}

static int
test_union_subtractor_partial_prune(void)
{
    struct rt_wdb *wdbp = NULL;
    struct rt_i *rtip = build_and_load(build_union_subtractor, "region3.r", &wdbp);
    int nsols, failures = 0;

    if (!rtip) return 1;

    nsols = count_soltabs(rtip);
    if (nsols != 2) {
	bu_log("FAIL test_union_subtractor_partial_prune: "
	       "expected 2 soltabs (sphA3 + sphE), got %d\n", nsols);
	failures++;
    } else {
	printf("PASS: test_union_subtractor_partial_prune "
	       "(disjoint sphD pruned, overlapping sphE kept)\n");
    }

    rt_free_rti(rtip);
    wdb_close(wdbp);
    return failures;
}


/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int
main(int UNUSED(argc), char *argv[])
{
    int failures = 0;

    bu_setprogname(argv[0]);

    failures += test_disjoint_subtract();
    failures += test_overlapping_subtract();
    failures += test_union_subtractor_partial_prune();

    if (failures) {
	bu_log("tree_shake: %d test(s) FAILED\n", failures);
	return 1;
    }

    printf("tree_shake: all tests PASSED\n");
    return 0;
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
