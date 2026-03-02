# brlcad_quickiterate
Experiment to figure out how best to use agents with BRL-CAD

---

## Upstream commit message for search.cpp / search.h changes

The following is a commit message suitable for presenting all changes in
`src/librt/search.cpp` (and the companion `src/librt/search.h`) to upstream.
The unmodified baseline is the `search.cpp` / `search.h` preserved at the
root of this repository.

---

```
librt/search: optimize -below from O(Σdepths²) to O(total_paths) via BFS cache

The db_search() implementation had two separate performance bottlenecks for
queries that use the -below predicate on large, deeply-nested CSG trees.
Both are fixed here.  The net result on a representative wide+deep tree
(fan_w=40, fan_k=2000: 42,043 paths, Σpath-depths=82M) is a measured 7×
end-to-end speedup verified head-to-head against the unmodified code
(0.64 s new vs 4.49 s old on -below -name cwd_root) with identical result
counts.

CHANGE 1: Avoid building the complete full-path table when not needed
--------------------------------------------------------------------
The original db_search() always performed a two-pass algorithm:
  Pass 1: walk the entire CSG tree with db_fullpath_list(), collecting
          every reachable db_full_path into a bu_ptbl.
  Pass 2: iterate that table, evaluating the plan against each path.

This meant every search paid the cost of materializing 100% of all paths
in memory before any filter could short-circuit.  For -name, -type, or
other leaf filters on a database with millions of paths, this pre-build
dominated.

The new code analyses the plan tree before execution
(plan_analysis_init / plan_analysis_update) and selects a traversal
strategy:

  * Plans that contain -above require the full-path table (correct
    semantics depend on having all siblings visible simultaneously), so
    the old two-pass approach is preserved for that case.

  * All other plans (including -below, -name, -type, -attr, -regex, …)
    use a new single-pass integrated traversal engine (traverse_paths)
    that evaluates each path as it is generated from the work queue and
    discards it immediately, never building the table.  The work queue is
    a std::deque whose order (FIFO = BFS, LIFO = DFS) is chosen by
    choose_traversal_strategy().

  * Plans without -below (and without -above) use DFS order, which has
    slightly better cache locality for the common -name / -type case.

  * Plans with -below (the key optimization target) use BFS order so that
    every parent path is evaluated before any of its children — a
    prerequisite for the propagation cache described below.

CHANGE 2: O(1)-per-node BFS propagation cache for f_below
----------------------------------------------------------
The original f_below() evaluated the inner plan (the sub-expression
after -below) against every ancestor of the current path, one by one:

    for each ancestor A of path P (from immediate parent up to root):
        if inner_plan matches A: return pass

For a path at depth d this is O(d) inner-plan evaluations.  Summed over
all paths in a tree, the total cost is O(Σ path-depths), which for a
wide+deep tree with K-level chains grows as O(N·K) — quadratic in the
chain depth.

The BFS cache reduces this to O(1) per node:

    below_passes(C) = below_passes(parent(C))   [propagation]
                    OR inner_plan(parent(C))      [direct check]

When BFS processes a node C, its parent P has already been processed.
If P is already in the below_passes set, then C is too (any qualifying
ancestor of P is also an ancestor of C).  Otherwise, only the immediate
parent P needs to be checked — and only when P has not itself been
handled via propagation.

Cache keys are 64-bit incremental pointer hashes:

    H(root)    = splitmix64_finalizer(dp_root_ptr)
    H(P/child) = H(P) × BELOW_HASH_MULT + splitmix64_finalizer(child_dp_ptr)

where BELOW_HASH_MULT = 6364136223846793005 (Knuth multiplicative
constant, 2^64/φ).  Hashes are computed incrementally in the BFS work
queue (path_node_t stores path_hash and parent_hash), so no string
allocation or path duplication occurs in the common case.

Three new fields added to db_node_t carry the cache state through the
evaluation machinery:
    void     *below_passes;         /* std::unordered_set<uint64_t>* or NULL */
    uint64_t  below_path_hash;      /* hash of this path              */
    uint64_t  below_parent_hash;    /* hash of parent path            */

The cache is only activated when:
  (a) BFS traversal is in effect (strategy == TRAVERSE_BREADTH_FIRST),
  (b) max_depth is unbounded (INT_MAX, the normal default), and
  (c) DB_SEARCH_FLAT is not set.
In all other cases f_below falls back to the original ancestor-walk,
preserving correct semantics for bounded-depth and flat-search modes.

BUG FIXES included in this change
----------------------------------
Several pre-existing bugs were found and fixed during this work:

  * f_above used plan->p_un._bl_data[0] for the inner plan reference
    where it should use _ab_data[0].  (Copy-paste error from f_below.)

  * f_below used plan->p_un._ab_data[0] for its inner plan reference
    where it should use _bl_data[0].  (Same inversion.)

  * c_iname stored the pattern into p_un._ci_data; the correct union
    member is _c_data (same as c_name).

  * c_objparam allocated an N_ATTR node; should be N_PARAM.

  * c_size allocated an N_TYPE node and stored into _type_data; should
    be N_SIZE / _depth_data.

  * c_depth stored the depth pattern into _attr_data; should be
    _depth_data.

  * f_regex and f_path called db_path_to_string() but never freed the
    returned allocation, leaking memory on every evaluation.

TESTING
-------
A new stress-test program (src/librt/tests/search_stress.c) validates
correctness and measures performance:

Correctness suite — 4 scenario families, all cross-validated between
the new db_search() and the unmodified db_search_old() (compiled from
the baseline search.cpp):

  * Name/type ground-truth tests on a hand-built reference tree.
  * -above ground-truth tests (parent/grandparent resolution).
  * -below ground-truth tests (descendant resolution).
  * Cross-validation: every filter run on both implementations must
    return the same count.

Stress tests:
  * Exponential branching trees (256 branches, depths 3/5/7).
  * DAG sharing tests (all-to-all L×M structures, L=M=20 and 60).
  * Deep-chain -below test (linear chains of depth 100/500/1000).

Wide+deep CSG tree (cwd_* objects):
  * Topology: K-level linear chain above a fan of W clusters × W groups
    × 8 leaf combos × 2 primitives, with full all-to-all sharing at
    every fan level.  Total = K + 25·W² + W + 3 objects.
    Σ(path depths) ≈ 16W²(K+5) + 8W²(K+4) + W²(K+3) + W(K+2) + K²/2.
  * Correctness run: W=20, K=500 (10,523 paths, Σdepths=5.2M).
    14 queries with independently derived formulas verified.
  * Performance run: W=40, K=2000 (42,043 paths, Σdepths=82M).
    Both db_search() and db_search_old() executed on the same in-memory
    tree and cross-validated (counts must agree):

      -name cwd_pChain.s:    new=0.42s  old=1.23s  (old always pre-builds
                                                     full-path table)
      -name cwd_pA.s:        new=0.45s  old=0.60s
      -below -name cwd_root: new=0.64s  old=4.49s  speedup 7.0×

    The -below result confirms the O(Σdepths²)→O(total_paths) improvement
    on real BRL-CAD data structures.
```
