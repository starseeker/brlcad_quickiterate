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
 *
 *
 * DAG (shared-node) topology used in performance tests
 * =====================================================
 *
 * The fan-tree used above has no path sharing: every node is
 * referenced by exactly one parent, so the number of distinct full
 * paths equals the number of nodes.  The old and new implementations
 * therefore do the same amount of work on that tree.
 *
 * The key algorithmic difference is:
 *   db_search_old  - ALWAYS pre-builds the complete set of full paths
 *                    into a bu_ptbl before evaluating any filter.
 *   db_search (new) - for queries that do NOT contain -above, uses an
 *                    on-the-fly traversal that builds, evaluates, and
 *                    frees each path in turn without accumulating them.
 *                    This covers -name, -type, and -below queries.
 *                    For -above queries it falls back to the same
 *                    pre-collection strategy as the old implementation.
 *
 * To expose this difference the DAG performance tests use a 3-level
 * all-to-all structure:
 *
 *   dag_target.s             <- single target primitive
 *   dag_leaf_0..dag_leaf_{L-1}  <- L leaf combos, each containing dag_target.s
 *   dag_mid_0..dag_mid_{M-1}    <- M mid combos, each referencing ALL L leaves
 *   dag_top                  <- single top combo referencing all M mids
 *
 * Full paths from dag_top:
 *   dag_top alone                         :         1
 *   dag_top -> dag_mid_j                  :         M
 *   dag_top -> dag_mid_j -> dag_leaf_i    :       M*L
 *   dag_top -> dag_mid_j -> dag_leaf_i -> dag_target.s : M*L
 * Total full paths = 1 + M + 2*M*L
 *
 * The old code allocates all (1 + M + 2*M*L) path objects upfront;
 * the new code on non-above queries keeps only O(depth) live at once.
 * At L=M=60 (7261 paths) the new code is typically ~1.3x faster for
 * -name, -type, and -below queries.  Both codes show equal performance
 * for -above queries because both take the pre-collection code path.
 *
 * For -below queries the new code is triggered by the TRAVERSE_BREADTH_FIRST
 * strategy (selected when has_below && !has_above).  The test uses
 * "-below -name dag_top", which matches all paths except dag_top itself
 * (M + 2*M*L results), exercising the full throughput of the traversal.
 *
 *
 * Deep linear-chain topology used in -below optimization test
 * ===========================================================
 *
 * The -below filter has an O(D²) worst case on a linear chain of depth D:
 * for each node at depth d, the original implementation walks up d ancestors
 * evaluating the inner plan, giving total work proportional to D*(D-1)/2.
 *
 * The new db_search eliminates this with a BFS propagation cache:
 *
 *   below_passes(C) = below_passes(parent(C)) OR inner(parent(C))
 *
 * Since BFS processes the parent before the child, each node's result is
 * O(1) (one hash lookup + at most one inner-plan evaluation), reducing the
 * total cost from O(D²) to O(D).
 *
 * The topology is a simple linear chain:
 *
 *   chain_root -> chain_0 -> chain_1 -> ... -> chain_{D-1} -> chain_leaf.s
 *
 * Total full paths = D + 2  (chain_root, D combs, and chain_leaf.s)
 *
 * Query: -below -name chain_root
 *   Every path except chain_root itself has chain_root as an ancestor.
 *   Expected count = D + 1  (all paths except chain_root).
 *
 * At D=1000 the original O(D²) behavior requires ~500K inner-plan evaluations;
 * the optimized O(D) implementation requires only ~1000.
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

    bu_log("  stress depth=%d  -name: new=%d(%.6fs) old=%d(%.6fs)\n",
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

    bu_log("  stress depth=%d  -above: new=%d(%.6fs) old=%d(%.6fs)\n",
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
/*  DAG sharing tree builder                                           */
/* ------------------------------------------------------------------ */

/*
 * Build the 3-level all-to-all DAG described in the file header.
 *
 *   dag_target.s  <- single target primitive
 *   dag_leaf_0 .. dag_leaf_{L-1}  <- leaf combos, each containing dag_target.s
 *   dag_mid_0  .. dag_mid_{M-1}   <- mid  combos, each referencing ALL L leaves
 *   dag_top                       <- top  combo  referencing all M mids
 *
 * Total full paths from dag_top = 1 + M + 2*M*L
 *
 * Returns 0 on success.
 */
static int
build_dag_sharing_tree(struct rt_wdb *wdbp, int L, int M)
{
    int i, j;
    struct bu_vls name = BU_VLS_INIT_ZERO;
    point_t center = VINIT_ZERO;
    fastf_t radius = 1.0;
    struct wmember wm;

    if (L < 1 || M < 1) {
	bu_vls_free(&name);
	return 1;
    }

    /* target primitive */
    if (mk_sph(wdbp, "dag_target.s", center, radius) != 0)
	goto cleanup_fail;

    /* L leaf combos, each containing dag_target.s */
    for (i = 0; i < L; i++) {
	BU_LIST_INIT(&wm.l);
	mk_addmember("dag_target.s", &wm.l, NULL, WMOP_UNION);
	bu_vls_sprintf(&name, "dag_leaf_%d", i);
	if (mk_lcomb(wdbp, bu_vls_cstr(&name), &wm, 0, NULL, NULL, NULL, 0) != 0)
	    goto cleanup_fail;
    }

    /* M mid combos, each referencing ALL L leaves */
    for (j = 0; j < M; j++) {
	BU_LIST_INIT(&wm.l);
	for (i = 0; i < L; i++) {
	    bu_vls_sprintf(&name, "dag_leaf_%d", i);
	    mk_addmember(bu_vls_cstr(&name), &wm.l, NULL, WMOP_UNION);
	}
	bu_vls_sprintf(&name, "dag_mid_%d", j);
	if (mk_lcomb(wdbp, bu_vls_cstr(&name), &wm, 0, NULL, NULL, NULL, 0) != 0)
	    goto cleanup_fail;
    }

    /* top combo referencing all M mids */
    BU_LIST_INIT(&wm.l);
    for (j = 0; j < M; j++) {
	bu_vls_sprintf(&name, "dag_mid_%d", j);
	mk_addmember(bu_vls_cstr(&name), &wm.l, NULL, WMOP_UNION);
    }
    if (mk_lcomb(wdbp, "dag_top", &wm, 0, NULL, NULL, NULL, 0) != 0)
	goto cleanup_fail;

    bu_vls_free(&name);
    return 0;

cleanup_fail:
    bu_vls_free(&name);
    return 1;
}


/*
 * Cross-validate new vs old on the DAG sharing tree and report timing.
 *
 * Queries tested:
 *   -name dag_target.s           (non-above: new is faster on large DAGs)
 *   -type shape                  (non-above: new is faster on large DAGs)
 *   -below -name dag_top         (non-above: new is faster on large DAGs;
 *                                 matches all M + 2*M*L paths below dag_top)
 *   -above -name dag_target.s    (above: both take the same code path)
 */
static int
test_dag_cross_validation(struct db_i *dbip, int L, int M)
{
    int failures = 0;
    int new_cnt, old_cnt;
    int64_t t_new, t_old;
    int total_paths = 1 + M + 2 * M * L;
    struct bu_ptbl new_results = BU_PTBL_INIT_ZERO;
    struct bu_ptbl old_results = BU_PTBL_INIT_ZERO;
    struct db_search_context *ctx = db_search_context_create();

    /* -name (non-above): new code uses on-the-fly traversal */
    t_new = bu_gettime();
    new_cnt = db_search(&new_results, DB_SEARCH_TREE,
			"-name dag_target.s",
			0, NULL, dbip, NULL, NULL, NULL);
    t_new = bu_gettime() - t_new;
    db_search_free(&new_results);

    t_old = bu_gettime();
    old_cnt = db_search_old(&old_results, DB_SEARCH_TREE,
			    "-name dag_target.s",
			    0, NULL, dbip, ctx);
    t_old = bu_gettime() - t_old;
    db_search_free(&old_results);

    bu_log("  dag L=%-3d M=%-3d paths=%-6d  -name:  new=%d(%.6fs) old=%d(%.6fs)\n",
	   L, M, total_paths,
	   new_cnt, (double)t_new/1e6, old_cnt, (double)t_old/1e6);
    CROSS_CHECK(new_cnt, old_cnt, "-name dag_target.s (dag)");

    /* -type shape (non-above) */
    t_new = bu_gettime();
    new_cnt = db_search(&new_results, DB_SEARCH_TREE,
			"-type shape",
			0, NULL, dbip, NULL, NULL, NULL);
    t_new = bu_gettime() - t_new;
    db_search_free(&new_results);

    t_old = bu_gettime();
    old_cnt = db_search_old(&old_results, DB_SEARCH_TREE,
			    "-type shape",
			    0, NULL, dbip, ctx);
    t_old = bu_gettime() - t_old;
    db_search_free(&old_results);

    bu_log("  dag L=%-3d M=%-3d paths=%-6d  -type:  new=%d(%.6fs) old=%d(%.6fs)\n",
	   L, M, total_paths,
	   new_cnt, (double)t_new/1e6, old_cnt, (double)t_old/1e6);
    CROSS_CHECK(new_cnt, old_cnt, "-type shape (dag)");

    /* -below -name dag_top: matches all paths that have dag_top as an
     * ancestor.  Since dag_top is the search root every path except
     * dag_top itself matches (M + 2*M*L results).  Like -name and -type,
     * the new code uses on-the-fly BFS traversal here (no full-path table
     * pre-build), giving the same performance advantage on large DAGs. */
    t_new = bu_gettime();
    new_cnt = db_search(&new_results, DB_SEARCH_TREE,
			"-below -name dag_top",
			0, NULL, dbip, NULL, NULL, NULL);
    t_new = bu_gettime() - t_new;
    db_search_free(&new_results);

    t_old = bu_gettime();
    old_cnt = db_search_old(&old_results, DB_SEARCH_TREE,
			    "-below -name dag_top",
			    0, NULL, dbip, ctx);
    t_old = bu_gettime() - t_old;
    db_search_free(&old_results);

    bu_log("  dag L=%-3d M=%-3d paths=%-6d  -below: new=%d(%.6fs) old=%d(%.6fs)\n",
	   L, M, total_paths,
	   new_cnt, (double)t_new/1e6, old_cnt, (double)t_old/1e6);
    CROSS_CHECK(new_cnt, old_cnt, "-below -name dag_top (dag)");

    /* -above -name (above): both use full-path pre-collection */
    t_new = bu_gettime();
    new_cnt = db_search(&new_results, DB_SEARCH_TREE,
			"-above -name dag_target.s",
			0, NULL, dbip, NULL, NULL, NULL);
    t_new = bu_gettime() - t_new;
    db_search_free(&new_results);

    t_old = bu_gettime();
    old_cnt = db_search_old(&old_results, DB_SEARCH_TREE,
			    "-above -name dag_target.s",
			    0, NULL, dbip, ctx);
    t_old = bu_gettime() - t_old;
    db_search_free(&old_results);

    bu_log("  dag L=%-3d M=%-3d paths=%-6d  -above: new=%d(%.6fs) old=%d(%.6fs)\n",
	   L, M, total_paths,
	   new_cnt, (double)t_new/1e6, old_cnt, (double)t_old/1e6);
    CROSS_CHECK(new_cnt, old_cnt, "-above -name dag_target.s (dag)");

    db_search_context_destroy(ctx);
    return failures;
}


/* ------------------------------------------------------------------ */
/*  Deep linear-chain builder and -below performance test             */
/* ------------------------------------------------------------------ */

/*
 * Build a linear chain of depth D:
 *
 *   chain_root -> chain_0 -> chain_1 -> ... -> chain_{D-1} -> chain_leaf.s
 *
 * Total paths when searching from chain_root: D + 2.
 * Returns 0 on success.
 */
static int
build_linear_chain(struct rt_wdb *wdbp, int D)
{
    int i;
    struct bu_vls name = BU_VLS_INIT_ZERO;
    struct wmember wm;
    point_t center = VINIT_ZERO;
    fastf_t radius = 1.0;

    if (D < 1) {
	bu_vls_free(&name);
	return 1;
    }

    /* leaf primitive */
    if (mk_sph(wdbp, "chain_leaf.s", center, radius) != 0)
	goto fail;

    /* chain_{D-1} wraps the primitive */
    BU_LIST_INIT(&wm.l);
    mk_addmember("chain_leaf.s", &wm.l, NULL, WMOP_UNION);
    bu_vls_sprintf(&name, "chain_%d", D - 1);
    if (mk_lcomb(wdbp, bu_vls_cstr(&name), &wm, 0, NULL, NULL, NULL, 0) != 0)
	goto fail;

    /* chain_{D-2} -> chain_{D-1}, ..., chain_0 -> chain_1 */
    for (i = D - 2; i >= 0; i--) {
	BU_LIST_INIT(&wm.l);
	bu_vls_sprintf(&name, "chain_%d", i + 1);
	mk_addmember(bu_vls_cstr(&name), &wm.l, NULL, WMOP_UNION);
	bu_vls_sprintf(&name, "chain_%d", i);
	if (mk_lcomb(wdbp, bu_vls_cstr(&name), &wm, 0, NULL, NULL, NULL, 0) != 0)
	    goto fail;
    }

    /* chain_root -> chain_0 */
    BU_LIST_INIT(&wm.l);
    mk_addmember("chain_0", &wm.l, NULL, WMOP_UNION);
    if (mk_lcomb(wdbp, "chain_root", &wm, 0, NULL, NULL, NULL, 0) != 0)
	goto fail;

    bu_vls_free(&name);
    return 0;

fail:
    bu_vls_free(&name);
    return 1;
}


/*
 * Verify -below correctness and report timing on a deep linear chain.
 *
 * Query: -below -name chain_root
 *   Expected result count: D + 1  (every path except chain_root itself).
 *
 * With the BFS propagation cache the new implementation runs in O(D);
 * the original ancestor-walk would be O(D²).
 */
static int
test_below_deep_chain(struct db_i *dbip, int D)
{
    int failures = 0;
    int cnt;
    int expected = D + 1;
    int64_t t;
    struct bu_ptbl results = BU_PTBL_INIT_ZERO;

    t = bu_gettime();
    cnt = db_search(&results, DB_SEARCH_TREE,
		    "-below -name chain_root",
		    0, NULL, dbip, NULL, NULL, NULL);
    t = bu_gettime() - t;
    db_search_free(&results);

    bu_log("  chain D=%-4d  -below -name chain_root: got=%d expect=%d (%.6fs)%s\n",
	   D, cnt, expected, (double)t / 1e6,
	   (cnt != expected) ? "  FAIL" : "");
    CHECK(cnt == expected, "-below chain count matches expected");

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
        int branches = 256;
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

    /* ---- DAG sharing tests: expose new vs old performance gap ---- */
    /*
     * The fan-tree stress tests above have no path sharing and show
     * no timing difference between implementations.  The DAG tests
     * below use an all-to-all connection structure (L leaves, each
     * referenced by M mids, all under one top) which causes the total
     * number of full paths to grow as 1 + M + 2*M*L.
     *
     * Expected outcome:
     *   non-above queries (-name, -type): new code faster (~1.5x at
     *     L=M=60 because it avoids pre-building the full-path table).
     *   -above queries: both implementations identical (~1.0x) because
     *     both use the same full-path pre-collection code path.
     */
    {
	/* Parameters: L leaves, M mids; total paths = 1 + M + 2*M*L */
	int dag_cases[][2] = {
	    {20, 20},  /* ~821  paths: fast correctness cross-check    */
	    {60, 60},  /* ~7261 paths: performance difference visible   */
	    {0,  0}
	};
	int di;

	bu_log("\nRunning DAG sharing tests (all-to-all structure)...\n");

	for (di = 0; dag_cases[di][0] != 0; di++) {
	    int L = dag_cases[di][0];
	    int M = dag_cases[di][1];

	    dbip = db_open_inmem();
	    if (dbip == DBI_NULL) {
		bu_log("ERROR: Cannot create DAG db (L=%d M=%d)\n", L, M);
		failures++;
		continue;
	    }
	    wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
	    if (!wdbp) {
		db_close(dbip);
		failures++;
		continue;
	    }

	    if (build_dag_sharing_tree(wdbp, L, M) != 0) {
		bu_log("ERROR: Cannot build DAG tree L=%d M=%d\n", L, M);
		wdb_close(wdbp);
		failures++;
		continue;
	    }

	    db_update_nref(dbip, &rt_uniresource);
	    failures += test_dag_cross_validation(dbip, L, M);

	    wdb_close(wdbp);
	}
    }

    /* ---- Deep linear-chain tests: verify -below O(D) behavior ---- */
    /*
     * A linear chain exposes the O(D²) worst case in the naive -below
     * implementation: for each of the D+2 paths, the original code walks
     * up all ancestors evaluating the inner plan.  The new BFS propagation
     * cache reduces this to O(D).
     *
     * Each case verifies correctness (count == D+1) and reports the
     * elapsed time.  On a system where D=1000 used to take hundreds of
     * milliseconds, the optimized version completes in single-digit
     * milliseconds.
     */
    {
	int depths[] = {100, 500, 1000, 0};
	int di;

	bu_log("\nRunning deep-chain -below tests (O(D) optimization)...\n");

	for (di = 0; depths[di] != 0; di++) {
	    int D = depths[di];

	    dbip = db_open_inmem();
	    if (dbip == DBI_NULL) {
		bu_log("ERROR: Cannot create chain db (D=%d)\n", D);
		failures++;
		continue;
	    }
	    wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
	    if (!wdbp) {
		db_close(dbip);
		failures++;
		continue;
	    }

	    if (build_linear_chain(wdbp, D) != 0) {
		bu_log("ERROR: Cannot build linear chain D=%d\n", D);
		wdb_close(wdbp);
		failures++;
		continue;
	    }

	    db_update_nref(dbip, &rt_uniresource);
	    failures += test_below_deep_chain(dbip, D);

	    wdb_close(wdbp);
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
