/*                S E A R C H _ S T R E S S . C
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
/** @file search_stress.c
 *
 * Stress and correctness tests for search.cpp / search_old.cpp.
 *
 * Builds test geometry in-memory using librt's API and verifies:
 *   - db_search results match independently-calculated ground truth
 *   - db_search (new optimised implementation) and db_search_old
 *     (reference implementation) return identical results for all
 *     tested filters
 *
 * Tree used in correctness tests
 * ================================
 *
 * All objects are searched starting from the single top-level object
 * (top1), since all other objects in the tree are referenced children.
 *
 * Primitives (spheres):
 *   prim_target.s   <- the "target" that -above searches look for
 *   prim_special.s  <- a second named target
 *   prim_plain.s    <- uninteresting filler
 *
 * Leaf combos (depth-1, referenced by mid-level combos):
 *   leaf_t   = { prim_target.s }
 *   leaf_s   = { prim_special.s }
 *   leaf_ts  = { prim_target.s, prim_special.s }  (two children)
 *   leaf_p   = { prim_plain.s }
 *
 * Mid combos (depth-2):
 *   mid_a    = { leaf_t,  leaf_p  }   <- has prim_target.s
 *   mid_b    = { leaf_s,  leaf_p  }   <- has prim_special.s only
 *   mid_c    = { leaf_ts, leaf_p  }   <- has both prim_target.s/prim_special.s
 *
 * Upper combos (depth-3):
 *   upper_a  = { mid_a, mid_b }   <- has prim_target.s via mid_a
 *   upper_b  = { mid_b, mid_c }   <- has prim_target.s via mid_c; mid_b is reused
 *
 * Top combo (depth-4) -- the only top-level (unreferenced) object:
 *   top1     = { upper_a, upper_b }
 *
 * Note: all nodes are reached only through top1, so all path counts below
 * are relative to the subtree rooted at top1.
 *
 * Ground-truth path counts (DB_SEARCH_TREE, no explicit path => searches top1):
 *
 * Query: -name prim_target.s
 *   /top1/upper_a/mid_a/leaf_t/prim_target.s
 *   /top1/upper_b/mid_c/leaf_ts/prim_target.s
 *   Total = 2
 *
 * Query: -type shape
 *   9 paths (prim_target.s x2, prim_special.s x3, prim_plain.s x4)
 *   Total = 9
 *
 * Query: -type comb   (all comb paths in tree)
 *   /top1
 *   /top1/upper_a                      /top1/upper_b
 *   /top1/upper_a/mid_a                /top1/upper_b/mid_b
 *   /top1/upper_a/mid_a/leaf_t         /top1/upper_b/mid_b/leaf_s
 *   /top1/upper_a/mid_a/leaf_p         /top1/upper_b/mid_b/leaf_p
 *   /top1/upper_a/mid_b                /top1/upper_b/mid_c
 *   /top1/upper_a/mid_b/leaf_s         /top1/upper_b/mid_c/leaf_ts
 *   /top1/upper_a/mid_b/leaf_p         /top1/upper_b/mid_c/leaf_p
 *   Total = 15
 *
 * Query: -above -name prim_target.s
 *   Objects in whose subtree prim_target.s appears:
 *   /top1
 *   /top1/upper_a                      /top1/upper_b
 *   /top1/upper_a/mid_a                /top1/upper_b/mid_c
 *   /top1/upper_a/mid_a/leaf_t         /top1/upper_b/mid_c/leaf_ts
 *   Total = 7
 *
 * Query: -above -name prim_special.s
 *   /top1
 *   /top1/upper_a                      /top1/upper_b
 *   /top1/upper_a/mid_b                /top1/upper_b/mid_b
 *   /top1/upper_a/mid_b/leaf_s         /top1/upper_b/mid_b/leaf_s
 *   /top1/upper_b/mid_c                /top1/upper_b/mid_c/leaf_ts
 *   Total = 9
 *
 * Query: -above -name prim_plain.s
 *   All comb paths except leaves that do not contain leaf_p as descendant.
 *   (Every path in the tree is above some leaf_p.)
 *   Total = 11
 *
 * Query: -below -name mid_a  (searched from top1)
 *   Objects that are direct or indirect children of mid_a:
 *   /top1/upper_a/mid_a/leaf_t
 *   /top1/upper_a/mid_a/leaf_p
 *   /top1/upper_a/mid_a/leaf_t/prim_target.s
 *   /top1/upper_a/mid_a/leaf_p/prim_plain.s
 *   Total = 4
 *
 * Query: -type comb  (RETURN_UNIQ_DP)  -> 10 unique comb dps
 * Query: -type shape (RETURN_UNIQ_DP)  ->  3 unique shape dps
 * Note: db_search return value is always the PATH count, not the unique
 *       dp count.  Use BU_PTBL_LEN(&results) for the unique-dp count.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu.h"
#include "vmath.h"
#include "raytrace.h"
#include "rt/search.h"
#include "wdb.h"


/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            bu_log("FAIL: %s\n", (msg)); \
            failures++; \
        } \
    } while (0)

#define CROSS_CHECK(new_cnt, old_cnt, filter_str) \
    do { \
        if ((new_cnt) != (old_cnt)) { \
            bu_log("CROSS-VALIDATION FAIL: '%s': new=%d old=%d\n", \
                   (filter_str), (new_cnt), (old_cnt)); \
            failures++; \
        } \
    } while (0)


/*
 * Run a tree search using the new (db_search) and old (db_search_old)
 * implementations and return both counts.  Returns 0 on success, 1 if
 * either function signals an error.
 */
static int
run_both(struct db_i *dbip, int flags, const char *filter,
         int *new_cnt, int *old_cnt)
{
    struct bu_ptbl new_results = BU_PTBL_INIT_ZERO;
    struct bu_ptbl old_results = BU_PTBL_INIT_ZERO;
    struct db_search_context *ctx = db_search_context_create();
    int ret = 0;

    *new_cnt = db_search(&new_results, flags, filter, 0, NULL, dbip,
                         NULL, NULL, NULL);
    *old_cnt = db_search_old(&old_results, flags, filter, 0, NULL, dbip,
                             ctx);

    if (*new_cnt < 0 || *old_cnt < 0) {
        bu_log("ERROR: search error for filter '%s' (new=%d old=%d)\n",
               filter, *new_cnt, *old_cnt);
        ret = 1;
    }

    db_search_free(&new_results);
    db_search_free(&old_results);
    db_search_context_destroy(ctx);
    return ret;
}


/* ------------------------------------------------------------------ */
/*  Build the correctness-test tree                                    */
/* ------------------------------------------------------------------ */

/*
 * Create the tree documented in the file-level comment using the
 * in-memory database pointed to by wdbp.
 *
 * Returns 0 on success.
 */
static int
build_correctness_tree(struct rt_wdb *wdbp)
{
    point_t center = VINIT_ZERO;
    fastf_t radius = 1.0;
    struct wmember wm;

    /* --- Primitives ------------------------------------------------ */
    if (mk_sph(wdbp, "prim_target.s", center, radius) != 0) return 1;
    if (mk_sph(wdbp, "prim_special.s", center, radius) != 0) return 1;
    if (mk_sph(wdbp, "prim_plain.s", center, radius) != 0) return 1;

    /* --- Leaf combos (depth 1) ------------------------------------- */
    BU_LIST_INIT(&wm.l);
    mk_addmember("prim_target.s", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "leaf_t", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    BU_LIST_INIT(&wm.l);
    mk_addmember("prim_special.s", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "leaf_s", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    BU_LIST_INIT(&wm.l);
    mk_addmember("prim_target.s", &wm.l, NULL, WMOP_UNION);
    mk_addmember("prim_special.s", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "leaf_ts", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    BU_LIST_INIT(&wm.l);
    mk_addmember("prim_plain.s", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "leaf_p", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    /* --- Mid combos (depth 2) -------------------------------------- */
    BU_LIST_INIT(&wm.l);
    mk_addmember("leaf_t", &wm.l, NULL, WMOP_UNION);
    mk_addmember("leaf_p", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "mid_a", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    BU_LIST_INIT(&wm.l);
    mk_addmember("leaf_s", &wm.l, NULL, WMOP_UNION);
    mk_addmember("leaf_p", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "mid_b", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    BU_LIST_INIT(&wm.l);
    mk_addmember("leaf_ts", &wm.l, NULL, WMOP_UNION);
    mk_addmember("leaf_p", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "mid_c", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    /* --- Upper combos (depth 3) ------------------------------------ */
    BU_LIST_INIT(&wm.l);
    mk_addmember("mid_a", &wm.l, NULL, WMOP_UNION);
    mk_addmember("mid_b", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "upper_a", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    BU_LIST_INIT(&wm.l);
    mk_addmember("mid_b", &wm.l, NULL, WMOP_UNION);  /* mid_b is reused */
    mk_addmember("mid_c", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "upper_b", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    /* --- Top combo (depth 4) --------------------------------------- */
    BU_LIST_INIT(&wm.l);
    mk_addmember("upper_a", &wm.l, NULL, WMOP_UNION);
    mk_addmember("upper_b", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "top1", &wm, 0, NULL, NULL, NULL, 0) != 0) return 1;

    return 0;
}


/* ------------------------------------------------------------------ */
/*  Correctness tests against ground truth                            */
/* ------------------------------------------------------------------ */

static int
test_name_search(struct db_i *dbip)
{
    int failures = 0;
    struct bu_ptbl results = BU_PTBL_INIT_ZERO;
    int cnt;

    /*
     * Ground truth: 2 paths end in prim_target.s (both under top1).
     * Note: db_search returns the PATH count; RETURN_UNIQ_DP only
     * affects the contents of the ptbl, not the return value.
     */
    cnt = db_search(&results, DB_SEARCH_TREE, "-name prim_target.s",
                    0, NULL, dbip, NULL, NULL, NULL);
    CHECK(cnt == 2, "-name prim_target.s path count == 2");
    db_search_free(&results);

    /* Unique dp check: 1 unique directory pointer for prim_target.s */
    cnt = db_search(&results,
                    DB_SEARCH_TREE | DB_SEARCH_RETURN_UNIQ_DP,
                    "-name prim_target.s",
                    0, NULL, dbip, NULL, NULL, NULL);
    CHECK(BU_PTBL_LEN(&results) == 1,
          "-name prim_target.s unique-dp ptbl len == 1");
    db_search_free(&results);

    /* Ground truth: 9 paths lead to primitive shapes */
    cnt = db_search(&results, DB_SEARCH_TREE, "-type shape",
                    0, NULL, dbip, NULL, NULL, NULL);
    CHECK(cnt == 9, "-type shape path count == 9");
    db_search_free(&results);

    /* Unique dp check: 3 unique shape objects */
    cnt = db_search(&results,
                    DB_SEARCH_TREE | DB_SEARCH_RETURN_UNIQ_DP,
                    "-type shape",
                    0, NULL, dbip, NULL, NULL, NULL);
    CHECK(BU_PTBL_LEN(&results) == 3,
          "-type shape unique-dp ptbl len == 3");
    db_search_free(&results);

    /* Ground truth: 15 comb paths */
    cnt = db_search(&results, DB_SEARCH_TREE, "-type comb",
                    0, NULL, dbip, NULL, NULL, NULL);
    CHECK(cnt == 15, "-type comb path count == 15");
    db_search_free(&results);

    /* Unique dp check: 10 unique comb objects */
    cnt = db_search(&results,
                    DB_SEARCH_TREE | DB_SEARCH_RETURN_UNIQ_DP,
                    "-type comb",
                    0, NULL, dbip, NULL, NULL, NULL);
    CHECK(BU_PTBL_LEN(&results) == 10,
          "-type comb unique-dp ptbl len == 10");
    db_search_free(&results);

    (void)cnt;
    return failures;
}


static int
test_above_search(struct db_i *dbip)
{
    int failures = 0;
    struct bu_ptbl results = BU_PTBL_INIT_ZERO;
    int cnt;

    /* Ground truth: 7 paths are ancestors of prim_target.s */
    cnt = db_search(&results, DB_SEARCH_TREE,
                    "-above -name prim_target.s",
                    0, NULL, dbip, NULL, NULL, NULL);
    CHECK(cnt == 7, "-above -name prim_target.s count == 7");
    db_search_free(&results);

    /* Ground truth: 9 paths are ancestors of prim_special.s */
    cnt = db_search(&results, DB_SEARCH_TREE,
                    "-above -name prim_special.s",
                    0, NULL, dbip, NULL, NULL, NULL);
    CHECK(cnt == 9, "-above -name prim_special.s count == 9");
    db_search_free(&results);

    /* Ground truth: 11 paths are ancestors of prim_plain.s */
    cnt = db_search(&results, DB_SEARCH_TREE,
                    "-above -name prim_plain.s",
                    0, NULL, dbip, NULL, NULL, NULL);
    CHECK(cnt == 11, "-above -name prim_plain.s count == 11");
    db_search_free(&results);

    (void)cnt;
    return failures;
}


static int
test_below_search(struct db_i *dbip)
{
    int failures = 0;
    struct bu_ptbl results = BU_PTBL_INIT_ZERO;
    int cnt;
    struct directory *top_dp;

    /*
     * -below -name mid_a searched from top1:
     * Finds all paths (within top1's subtree) where an ancestor is
     * named mid_a.  Result: 4 paths (leaf_t, leaf_p, and their
     * primitive children, all under top1/upper_a/mid_a/).
     */
    top_dp = db_lookup(dbip, "top1", LOOKUP_QUIET);
    if (top_dp) {
        cnt = db_search(&results, DB_SEARCH_TREE,
                        "-below -name mid_a",
                        1, &top_dp, dbip, NULL, NULL, NULL);
        CHECK(cnt == 4, "-below -name mid_a count from top1 == 4");
        db_search_free(&results);
    }

    (void)cnt;
    return failures;
}


/* ------------------------------------------------------------------ */
/*  Cross-validation: new vs old implementation                       */
/* ------------------------------------------------------------------ */

static int
test_cross_validation(struct db_i *dbip)
{
    int failures = 0;
    int new_cnt, old_cnt;

    static const char * const filters[] = {
        "-name prim_target.s",
        "-name prim_special.s",
        "-name prim_plain.s",
        "-above -name prim_target.s",
        "-above -name prim_special.s",
        "-above -name prim_plain.s",
        "-type shape",
        "-type comb",
        "-type region",
        "-name leaf*",
        "-name mid*",
        "-name upper*",
        NULL
    };
    const char * const *f;

    for (f = filters; *f; f++) {
        if (run_both(dbip, DB_SEARCH_TREE, *f, &new_cnt, &old_cnt)) {
            failures++;
            continue;
        }
        CROSS_CHECK(new_cnt, old_cnt, *f);
    }

    /* Also test DB_SEARCH_RETURN_UNIQ_DP */
    for (f = filters; *f; f++) {
        if (run_both(dbip, DB_SEARCH_TREE | DB_SEARCH_RETURN_UNIQ_DP,
                     *f, &new_cnt, &old_cnt)) {
            failures++;
            continue;
        }
        CROSS_CHECK(new_cnt, old_cnt, *f);
    }

    return failures;
}


/* ------------------------------------------------------------------ */
/*  Stress tree builder                                                */
/* ------------------------------------------------------------------ */

/*
 * Build a stress-test tree of the requested depth.
 *
 * The tree is a binary branching hierarchy:
 *   - A small pool of primitive spheres is shared across all leaf combos.
 *   - Each leaf combo references two primitives from the pool.
 *   - Two named "target" leaf combos (stress_target_a, stress_target_b)
 *     give the -above filter something specific to find.
 *   - Fan-out combos at each level above the leaves each reference
 *     two combos from the level below.
 *
 * depth:    number of comb levels above the primitives (>= 1)
 * branches: number of leaf combos at depth 1 (>= 2)
 *
 * Returns 0 on success.
 */
static int
build_stress_tree(struct rt_wdb *wdbp, int depth, int branches)
{
    int i, d;
    const int pool_sz = 4;
    struct bu_vls name = BU_VLS_INIT_ZERO;
    struct bu_vls lname = BU_VLS_INIT_ZERO;
    struct bu_vls rname = BU_VLS_INIT_ZERO;
    point_t center = VINIT_ZERO;
    fastf_t radius = 1.0;
    struct wmember wm;
    int cur_level_count;

    if (depth < 1 || branches < 2) {
        bu_vls_free(&name);
        bu_vls_free(&lname);
        bu_vls_free(&rname);
        return 1;
    }

    /* Primitives */
    for (i = 0; i < pool_sz; i++) {
        bu_vls_sprintf(&name, "stress_prim_%d.s", i);
        if (mk_sph(wdbp, bu_vls_cstr(&name), center, radius) != 0)
            goto cleanup_fail;
    }

    /* Leaf combos (depth 1) */
    for (i = 0; i < branches; i++) {
        BU_LIST_INIT(&wm.l);
        bu_vls_sprintf(&name, "stress_prim_%d.s", i % pool_sz);
        mk_addmember(bu_vls_cstr(&name), &wm.l, NULL, WMOP_UNION);
        bu_vls_sprintf(&name, "stress_prim_%d.s", (i + 1) % pool_sz);
        mk_addmember(bu_vls_cstr(&name), &wm.l, NULL, WMOP_UNION);

        if (i == 0) {
            if (mk_lcomb(wdbp, "stress_target_a", &wm, 0, NULL, NULL, NULL, 0) != 0)
                goto cleanup_fail;
        } else if (i == 1) {
            if (mk_lcomb(wdbp, "stress_target_b", &wm, 0, NULL, NULL, NULL, 0) != 0)
                goto cleanup_fail;
        } else {
            bu_vls_sprintf(&name, "stress_leaf_%d", i);
            if (mk_lcomb(wdbp, bu_vls_cstr(&name), &wm, 0, NULL, NULL, NULL, 0) != 0)
                goto cleanup_fail;
        }
    }

    /* Fan levels: d=2..depth */
    cur_level_count = branches;
    for (d = 2; d <= depth; d++) {
        int next_count = (cur_level_count + 1) / 2;
        for (i = 0; i < next_count; i++) {
            int left_idx  = 2 * i;
            int right_idx = 2 * i + 1;

            BU_LIST_INIT(&wm.l);

            /* Name the left child */
            if (d == 2) {
                if (left_idx == 0)
                    bu_vls_sprintf(&lname, "stress_target_a");
                else if (left_idx == 1)
                    bu_vls_sprintf(&lname, "stress_target_b");
                else
                    bu_vls_sprintf(&lname, "stress_leaf_%d", left_idx);
            } else {
                bu_vls_sprintf(&lname, "stress_L%d_%d", d - 1, left_idx);
            }

            mk_addmember(bu_vls_cstr(&lname), &wm.l, NULL, WMOP_UNION);

            /* Name the right child (only if it exists) */
            if (right_idx < cur_level_count) {
                if (d == 2) {
                    if (right_idx == 0)
                        bu_vls_sprintf(&rname, "stress_target_a");
                    else if (right_idx == 1)
                        bu_vls_sprintf(&rname, "stress_target_b");
                    else
                        bu_vls_sprintf(&rname, "stress_leaf_%d", right_idx);
                } else {
                    bu_vls_sprintf(&rname, "stress_L%d_%d", d - 1, right_idx);
                }
                if (db_lookup(wdbp->dbip, bu_vls_cstr(&rname), LOOKUP_QUIET))
                    mk_addmember(bu_vls_cstr(&rname), &wm.l, NULL, WMOP_UNION);
            }

            bu_vls_sprintf(&name, "stress_L%d_%d", d, i);
            if (mk_lcomb(wdbp, bu_vls_cstr(&name), &wm, 0, NULL, NULL, NULL, 0) != 0)
                goto cleanup_fail;
        }
        cur_level_count = next_count;
    }

    bu_vls_free(&name);
    bu_vls_free(&lname);
    bu_vls_free(&rname);
    return 0;

cleanup_fail:
    bu_vls_free(&name);
    bu_vls_free(&lname);
    bu_vls_free(&rname);
    return 1;
}


/*
 * Run cross-validation searches on the stress tree.  No hard-coded
 * ground truth; just ensure new == old for each filter.
 */
static int
test_stress_cross_validation(struct db_i *dbip, int depth)
{
    int failures = 0;
    int new_cnt, old_cnt;
    int64_t t_new, t_old;
    struct bu_ptbl new_results = BU_PTBL_INIT_ZERO;
    struct bu_ptbl old_results = BU_PTBL_INIT_ZERO;
    struct db_search_context *ctx = db_search_context_create();

    /* -name search (fast) */
    t_new = bu_gettime();
    new_cnt = db_search(&new_results, DB_SEARCH_TREE,
                        "-name stress_target_a",
                        0, NULL, dbip, NULL, NULL, NULL);
    t_new = bu_gettime() - t_new;
    db_search_free(&new_results);

    t_old = bu_gettime();
    old_cnt = db_search_old(&old_results, DB_SEARCH_TREE,
                            "-name stress_target_a",
                            0, NULL, dbip, ctx);
    t_old = bu_gettime() - t_old;
    db_search_free(&old_results);

    bu_log("  stress depth=%d  -name: new=%d(%.3fs) old=%d(%.3fs)\n",
           depth, new_cnt, (double)t_new/1e6, old_cnt, (double)t_old/1e6);
    CROSS_CHECK(new_cnt, old_cnt, "-name stress_target_a (stress)");

    /* -above search (expensive) */
    t_new = bu_gettime();
    new_cnt = db_search(&new_results, DB_SEARCH_TREE,
                        "-above -name stress_target_a",
                        0, NULL, dbip, NULL, NULL, NULL);
    t_new = bu_gettime() - t_new;
    db_search_free(&new_results);

    t_old = bu_gettime();
    old_cnt = db_search_old(&old_results, DB_SEARCH_TREE,
                            "-above -name stress_target_a",
                            0, NULL, dbip, ctx);
    t_old = bu_gettime() - t_old;
    db_search_free(&old_results);

    bu_log("  stress depth=%d  -above: new=%d(%.3fs) old=%d(%.3fs)\n",
           depth, new_cnt, (double)t_new/1e6, old_cnt, (double)t_old/1e6);
    CROSS_CHECK(new_cnt, old_cnt, "-above -name stress_target_a (stress)");

    /* -type shape (unique dps) */
    new_cnt = db_search(&new_results,
                        DB_SEARCH_TREE | DB_SEARCH_RETURN_UNIQ_DP,
                        "-type shape",
                        0, NULL, dbip, NULL, NULL, NULL);
    db_search_free(&new_results);
    old_cnt = db_search_old(&old_results,
                            DB_SEARCH_TREE | DB_SEARCH_RETURN_UNIQ_DP,
                            "-type shape",
                            0, NULL, dbip, ctx);
    db_search_free(&old_results);

    bu_log("  stress depth=%d  -type shape: new=%d old=%d\n",
           depth, new_cnt, old_cnt);
    CROSS_CHECK(new_cnt, old_cnt, "-type shape (stress)");

    db_search_context_destroy(ctx);
    return failures;
}


/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    int failures = 0;
    struct db_i *dbip;
    struct rt_wdb *wdbp;

    bu_setprogname(argv[0]);
    (void)argc;

    /* ---- Correctness test database ---- */
    dbip = db_open_inmem();
    if (dbip == DBI_NULL) {
        bu_exit(1, "ERROR: Unable to create in-memory database\n");
    }
    wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
    if (!wdbp) {
        db_close(dbip);
        bu_exit(1, "ERROR: Unable to open in-memory database for writing\n");
    }

    bu_log("Building correctness-test tree...\n");
    if (build_correctness_tree(wdbp) != 0) {
        wdb_close(wdbp);  /* also closes dbip */
        bu_exit(1, "ERROR: Failed to build correctness test tree\n");
    }

    db_update_nref(dbip, &rt_uniresource);

    bu_log("Running name-search ground-truth tests...\n");
    failures += test_name_search(dbip);

    bu_log("Running -above ground-truth tests...\n");
    failures += test_above_search(dbip);

    bu_log("Running -below ground-truth tests...\n");
    failures += test_below_search(dbip);

    bu_log("Running cross-validation (new vs old)...\n");
    failures += test_cross_validation(dbip);

    /* wdb_close also closes dbip via db_close */
    wdb_close(wdbp);

    /* ---- Stress tests at increasing depths ---- */
    {
        int depths[] = {3, 5, 7, 0};
        int branches = 8;
        int di;

        bu_log("\nRunning stress tests (branches=%d)...\n", branches);

        for (di = 0; depths[di] != 0; di++) {
            int depth = depths[di];

            dbip = db_open_inmem();
            if (dbip == DBI_NULL) {
                bu_log("ERROR: Cannot create stress db (depth=%d)\n", depth);
                failures++;
                continue;
            }
            wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
            if (!wdbp) {
                db_close(dbip);
                failures++;
                continue;
            }

            if (build_stress_tree(wdbp, depth, branches) != 0) {
                bu_log("ERROR: Cannot build stress tree depth=%d\n", depth);
                wdb_close(wdbp);  /* also closes dbip */
                failures++;
                continue;
            }

            db_update_nref(dbip, &rt_uniresource);
            failures += test_stress_cross_validation(dbip, depth);

            wdb_close(wdbp);  /* also closes dbip */
        }
    }

    if (failures) {
        bu_log("\n%d test(s) FAILED\n", failures);
        return 1;
    }
    bu_log("\nAll tests PASSED\n");
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
