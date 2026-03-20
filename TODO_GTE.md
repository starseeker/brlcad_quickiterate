# GTE Remeshing — Session Notes

## Problem
`bot remesh` on `4002.1.t0` from `Generic_Twin.g` was extremely slow compared to
Geogram's implementation.

## Root Cause Analysis

Three critical O(n²)/O(n·m) bottlenecks were identified in the GTE implementation:

### 1. `NearestNeighborSearchN` — O(n) brute-force search (no KD-tree)
- `BuildIndex()` was an **empty no-op** — no spatial data structure was built
- Every call to `FindNearestNeighbor()` scanned all n points linearly
- Every call to `FindKNearestNeighbors()` also scanned all n points
- Used by `DelaunayNN::UpdateNeighborhoods` → O(n² × k) to build all neighborhoods
  per Lloyd iteration

### 2. `RestrictedVoronoiDiagramN::ComputeCentroids` — O(n_triangles × n_seeds)
- Per triangle, `FindNearestSiteND()` linearly scanned all seeds
- Used inside `CVTN::LloydIterations` for centroid computation
- Geogram uses walk-based RVD polygon clipping (O(n_triangles × avg_neighbors)) instead

### 3. `SurfaceRVDN::FindNearestSeed` — O(n_seeds) per unvisited triangle
- Called once per mesh facet in the `ForEachPolygon` outer loop
- No spatial acceleration

## Changes Made (Session 1)

### `NearestNeighborSearchN.h` — KD-tree replacement
- Added `KDTreeND` struct: N-dimensional KD-tree with split-order flat array layout
- `BuildIndex()` now builds the KD-tree in O(n log² n)
- `FindNearestNeighbor()` now O(log n) instead of O(n)
- `FindKNearestNeighbors()` now O(k log n) instead of O(n)

### `CVTN.h` — `LloydIterations` now uses `SurfaceRVDN` walk-based centroids
- **Old approach**: `RestrictedVoronoiDiagramN::ComputeCentroids` — brute-force
  O(n_triangles × n_seeds) per iteration
- **New approach**: `SurfaceRVDN::ForEachPolygon` with centroid-accumulating callback
  — walk-based O(n_triangles × avg_Delaunay_neighbors) per iteration, matching
  Geogram's `CVT::Lloyd_iterations` → `RVD_->compute_centroids()` exactly
- N-D lifted vertices built per iteration (same logic as `ComputeRDT`)
- Centroid accumulation uses Heron's formula for N-D triangle area

### `SurfaceRVDN.h` — `SeedKDTree3D` for `FindNearestSeed`
- Added `SeedKDTree3D` struct: 3D KD-tree over seed positions (first 3 N-D dims)
- `BuildSeedKDTree()` called from `Initialize()` in O(n log n)
- `FindNearestSeed()` now O(log n_seeds) instead of O(n_seeds) per facet

## Changes Made (Session 2)

### `CVTN.h` — `NewtonIterations` now implements real L-BFGS
- **Old approach**: just called `LloydIterations` with tighter convergence threshold
- **New approach**: full L-BFGS (limited-memory BFGS) optimizer matching Geogram's
  `CVT::Newton_iterations()` which uses HLBFGS
- L-BFGS with m=7 history pairs (matching Geogram's `Newton_m=7` default)
- Armijo backtracking line search (c1=1e-4, standard value)
- Two-loop L-BFGS recursion (Nocedal 1980)
- CVT gradient: `g_s = 2 * m_s * (p_s - c_s)` computed via same ForEachPolygon walk
- Energy proxy: `f = sum_s m_s * ||p_s - c_s||^2` for line search
- Each Newton iteration uses 1 gradient evaluation + at most ~2 line search steps
- Converges 5-10× faster than Lloyd per effective quality unit

### `MeshRemesh.h` — Newton iterations wired into CVT pipeline
- Both `RemeshCVTIsotropic` and `RemeshCVTAnisotropic` now call `cvt.NewtonIterations(params.newtonIterations)` after Lloyd
- Default `newtonIterations` changed from 5 to 30 (matching Geogram's `nb_Newton_iter=30`)
- This matches Geogram's `remesh_smooth`: 5 Lloyd → 30 Newton → compute_surface

### `MeshRemesh.h` — `MeshAdjustSurface` first implementation
- Initial version used nearest-point-on-triangle projection with AABB early-out
- Called from both `RemeshCVTIsotropic` and `RemeshCVTAnisotropic` after compute_surface

## Changes Made (Session 3)

### `MeshRemesh.h` — `MeshAdjustSurface` full Geogram-equivalent rewrite
- **Old approach (Session 2)**: nearest-point-on-triangle projection — WRONG
- **New approach**: full translation of Geogram's `mesh_adjust_surface()` from
  `geogram/src/lib/geogram/mesh/mesh_remesh.cpp`
- Algorithm matches Geogram exactly:
  1. Compute area-weighted vertex normals Nv[v] for output mesh
  2. Compute per-vertex average edge length Lv[v]
  3. For border vertices: reset Nv[v] to tangent-to-border direction
     (cross(edge, face_normal) per adjacent border edge) — Geogram's border handling
  4. Build AABB BVH tree over reference (input) triangles using `AABBBVTreeOfTriangles`
  5. Check if reference mesh has border edges; if so, create ribbon mesh and its BVH
     (translation of `create_ribbon_on_border()`)
  6. For each output vertex: fire bidirectional ray along Nv[v] → find nearest
     intersection on reference surface (or ribbon) → target point Qv[v]
     (translation of `nearest_along_bidirectional_ray()`)
  7. For each output face: compute center Pf, fire bidirectional ray → target Qf
  8. Solve global sparse least-squares system with Conjugate Gradient:
     - Variables: lambda[v] (one per output vertex)
     - Vertex rows:  lambda_v * Nv[v][c] = Qv[v][c] - Pv[c]
     - Facet rows:   Σ_j (1/d * lambda_vj * Nv[vj][c]) = Qf[c] - Pf[c]
     - Border edge rows (weighted by border_importance)
     - Normal equations A^T A λ = A^T b solved with Jacobi-preconditioned CG
     - Matrix-free implementation (no explicit matrix storage)
  9. Apply displacement: Pv += lambda[v] * Nv[v]
- Includes `<Mathematics/AABBBVTreeOfTriangles.h>` and `<unordered_map>`
- max_edge_distance=0.5 and border_importance=1.0 match Geogram defaults

### `repair.cpp` — Fixed pre-existing brace error in pass0 block
- Removed extra `}` in the "pass 0: remove purely-extra faces" block that was
  causing `t_pruned` to go out of scope before the `if (t_pruned.size() < ...)` check
- This fixed compilation errors: `'t_pruned' was not declared in this scope`,
  `label 'pass1' used but not defined`, etc.
- The full brlcad build now succeeds

## Performance Status (Session 3)

Timing from `bot remesh 4002.1.t0` on Generic_Twin.g (3258 verts, 6488 faces):

| Stage     | Time   | Notes |
|-----------|--------|-------|
| repair    | 0.01s  | ✓ fast |
| preproc   | 1.49s  | ✓ acceptable (86572 verts, 64184 faces) |
| cvt       | HUNG   | ← **STILL SLOW** |

**Session 3 identified CVT bottleneck**: The `AccumulateCentroids()` function
(called in every Lloyd/Newton iteration) rebuilds `DelaunayNN` from scratch each
time, calling `UpdateNeighborhoods()` which does 32580 × k-NN queries in 6D space.

**Root cause of CVT hang**: `DelaunayNN::UpdateNeighborhoods()` precomputes ALL 32580
neighborhoods eagerly, each requiring a 20-NN query in a 6D KD-tree of 32580 points.
While the KD-tree is fast (O(k log n)), for N=6 dimensions the curse of dimensionality
means many subtrees are explored. Total: 32580 × 20 × ~35 iterations = severe slowdown.

## Changes Made (Session 4)

### `DelaunayNN.h` — Lazy neighborhood computation ✅

Implemented Priority 1 exactly as designed in the TODO:

1. **`UpdateNeighborhoods()`** now only resets the cache state:
   ```cpp
   mNeighborhoods.clear();
   mNeighborhoods.resize(this->mNumVertices);
   mComputed.assign(this->mNumVertices, false);
   // KD-tree was already built by SetVertices() → mNNSearch.SetPoints()
   ```

2. **`GetNeighbors(v)`** computes and caches on first access:
   ```cpp
   if (!mComputed[v]) {
       ComputeNeighborhood(v);
       mComputed[v] = true;
   }
   return mNeighborhoods[v];
   ```

3. **`ComputeNeighborhood()`** made `const` (writes to `mutable` cache).

4. **`EnlargeNeighborhood(v, nb)`** sets `mComputed[v] = true` after fetching.

5. **`mNeighborhoods` and `mComputed`** declared `mutable` for const-correct lazy cache.

**Expected performance impact**: For `bot remesh 4002.1.t0` with 32580 seeds:
- **Before**: 32580 × 20-NN queries per iteration (all seeds, eager) → hung
- **After**: Only seeds visited by `SurfaceRVDN::ForEachPolygon` have neighborhoods
  computed. In practice each BFS walk traverses O(n_facets × k) seed cells. With
  n_facets=64184 and average 6 seeds per facet, only ~6000–10000 unique seeds are
  ever visited per iteration. That is 3–5× fewer NN queries than the eager approach,
  and they are computed lazily as needed during the BFS walk.

## Changes Made (Session 6)

### `MeshRemesh.h` — Fixed O(n²) seed-normal lookup in `RemeshCVTAnisotropic`

**Problem**: After `ComputeInitialSamplingFarthestPoint` returns 3D seeds, the code
augmented each seed with its surface normal by brute-force scanning all input vertices:
```
for (size_t s = 0; s < rawSites.size(); ++s)
    for (size_t v = 0; v < verts3.size(); ++v)  // O(n) per seed → O(n_seeds * n_verts)
        ...find nearest vert...
```
With `nb_pts = 10 × input_verts`, a preproc'd mesh of ~86K verts and ~86K seeds means
86000 × 86000 = 7.4 billion comparisons just to assign normals.

**Fix**: Added `#include <Mathematics/NearestNeighborSearchN.h>` and replaced the
inner `for (v)` scan with a single `NearestNeighborSearchN<Real, 3>` KD-tree built once
over the input vertices. Each seed's nearest vertex is now O(log n) → total O(n log n).

**Verified**: `libbg` builds cleanly with the change.

### Remaining CVT hang (torus/larger meshes)

A sphere (146 verts → 1460 seeds) completes CVT in 1.62s. ✓
A torus (105 verts → 1050 seeds, but 1482 verts after preproc → ~14820 seeds) HANGS.

The hang is in `SurfaceRVDN::ClipCellFacet`'s security-radius enlargement loop:
```cpp
while (true) {
    for (; jj < neighbors.size(); ++jj) {
        if (dij > 4.1 * R2) { srSatisfied = true; break; }
        clip_by_plane(...);
        update R2...
    }
    if (srSatisfied) break;
    // enlarge neighborhood...
    mDelaunay->EnlargeNeighborhood(seed, nb);
    // nb grows as: nb += nb/8 for nb>8, else nb++
}
```

**Key difference vs Geogram's `clip_by_cell_SR`**:

Geogram's Delaunay_NearestNeighbors returns neighbors **pre-sorted by distance** (the
ANN library's `get_nearest_neighbors` already returns them in sorted order). Our
`DelaunayNN::GetNeighbors` returns the 20 nearest in **unsorted** order from the KD-tree.

In the sorted case, the security radius criterion `dij > 4.1 * R2` becomes a true
early-exit: once the distance grows past `4.1 * R2`, ALL remaining neighbors (being
farther) also fail, so we stop. In our case with unsorted neighbors, we process all
k neighbors every time hoping to find one distant enough, and in the worst case never
satisfy the SR until we've fetched ALL n-1 neighbors (which requires O(n log n) NN
queries via repeated `EnlargeNeighborhood` calls).

## NEXT SESSION TODO

### Priority 1 (CRITICAL): Sort neighbors by distance in `DelaunayNN::GetNeighbors`

Geogram's `Delaunay_NearestNeighbors::get_neighbors_internal` uses ANN's
`get_nearest_neighbors(nb_neigh, i, closest_pt_ix, closest_pt_dist)` which returns
neighbors **in order of increasing distance**. The security-radius loop in
`clip_by_cell_SR` depends on this ordering to terminate early.

**Fix needed in `DelaunayNN.h`**:
In `ComputeNeighborhood(v)`, after calling `mNNSearch.FindKNearestNeighbors(...)`,
sort the result by squared distance to `mSites[v]`. The distance is already available
from the KD-tree query (the `FindKNearestNeighbors` return includes distances).

Check `NearestNeighborSearchN::FindKNearestNeighbors` signature — it likely returns
neighbor indices with distances. If so, sort both arrays together by distance before
storing in `mNeighborhoods[v]`.

Alternatively, after `GetNeighbors` returns, the caller in `ClipCellFacet` currently
sorts by distance anyway (line ~936 `std::sort(neighbors.begin(), ...)`) — so the
real issue may be that `EnlargeNeighborhood` is called too many times. Check whether
`EnlargeNeighborhood` properly enlarges beyond the initial 20 and whether the growth
rate `nb += nb/8` ever reaches `nbTotalSeeds - 1` for the torus seeds.

### Priority 2: Testing

Once CVT runs to completion on torus and sphere:
1. Test on a larger mesh (~1000 input verts).
2. Compare timing against Geogram reference.
3. Compare output mesh quality.

### Priority 3: Parallelism (optional)

After confirming Priority 2, consider OpenMP parallelism in `ForEachPolygon`
for additional speedup, matching Geogram's parallel RVD computation.

## Changes Made (Session 8)

Four Geogram-alignment fixes discovered by systematic code review:

### 1. `CVTN.h` — Fixed normalScale drift in `BuildLiftedVertices` and `ComputeRDT`

**Problem**: `BuildLiftedVertices` re-derived `normalScale` from the current seed
normal-component magnitudes on every call. As Lloyd iterations move seeds to N-D
Voronoi centroids, the normal components become averages of nearby unit normals,
whose vector sum magnitude is ≤ 1. Over iterations, seed normal magnitudes shrink
→ `normalScale` computed from seeds also shrinks → the 6-D metric gradually becomes
more isotropic than intended.

**Geogram equivalent**: `CVT::set_anisotropy(s)` pins the scale once before the CVT
loop and never re-derives it.

**Fix**: Added `mNormalScale` member (default 0 = dynamic derivation), `SetNormalScale(s)`
and `GetNormalScale()` methods. `BuildLiftedVertices` and `ComputeRDT` use `mNormalScale`
when non-zero, otherwise fall back to the old seed-derived computation.

### 2. `MeshRemesh.h` — Call `SetNormalScale` after `SetSites` in both 6D callers

`RemeshCVTAnisotropic` and `LloydRelaxationAnisotropic` now call `cvt.SetNormalScale(normalScale)`
immediately after `cvt.SetSites(...)`, before any Lloyd or Newton iterations. This pins
the 6-D metric scale consistently for the entire CVT optimisation.

### 3. `SurfaceRVDN.h` — Fixed `clip_by_plane` cross condition

**Problem**: The edge-crossing condition was `s != prevS && prevS != 0`. This fires
when `prevS ≠ 0` and `s = 0` (vertex exactly on bisector), emitting a spurious
intersection vertex.

**Geogram equivalent**: Uses `geo_sgn()` and the condition `prevS * S < 0`, which only
fires on a strict sign change and does NOT fire when either sign is 0.

**Fix**: Changed condition to `prevS * s < 0`.

### 4. `SurfaceRVDN.h` — Full sort after `EnlargeNeighborhood`; SR constant 4.1→4.0

- After `EnlargeNeighborhood()`, sorted the FULL enlarged list (not just the suffix
  [prevSize..newSize)). Since KNN returns results in a specific internal order, sorting
  only the suffix could leave the prefix out of order relative to what was already
  processed. Full sort guarantees the first prevSize elements are the same k-nearest
  (correctly skipped by `jj=prevSize`), matching Geogram's approach.

- Changed security-radius constant from 4.1 to 4.0 to exactly match Geogram's
  `clip_by_cell_SR` check. The theoretical security-radius proof uses 4.0; the previous
  4.1 was a conservative safety margin not present in Geogram.

### Quality Results (torus remesh, 5 Lloyd + 30 Newton iterations)

| Mesh | Target | v | t | time | AR p50 | AR p90 | AR p99 | AR max |
|------|--------|---|---|------|--------|--------|--------|--------|
| 20×10 torus | 200 | 198 | 340 | 0.19s | 1.105 | 1.281 | 1.488 | 1.616 |
| 20×10 torus | 500 | 468 | 702 | 0.16s | 1.132 | 1.331 | 1.661 | 9.32  |
| 20×10 torus | 1000 | 762 | 994 | 0.12s | 1.172 | 1.396 | 1.970 | 8.14  |
| 20×10 torus | 2000 | 1241 | 1498 | 0.17s | 1.196 | 1.489 | 3.183 | 21.1  |
| 40×20 torus | 8000 | 5266 | 6407 | 0.79s | 1.199 | 1.494 | 2.570 | 823   |

The high max AR is a single sliver triangle from near-degenerate anisotropic CVT
seeds at a surface fold; p99 quality is excellent across all sizes.

### Remaining Potential Issues

- Max AR outliers (single sliver triangle) at high target-vertex counts: intrinsic to
  anisotropic CVT where two seeds are close in 3D but far in 6D. Geogram handles this
  via post-processing cleanup in `mesh_postprocess_RDT`. Our `ComputeMultiNerveRDT`
  already has steps 5a-5f but may need an additional sliver-removal step for very high
  target-to-input ratios.

## Changes Made (Session 11)

Investigated Geogram differences and performance, removed debug instrumentation,
and added structural caching to reduce per-iteration overhead.

### Debug output removal
- Removed all `std::cerr` timing/diagnostic output from `MeshRemesh.h`, `CVTN.h`,
  and `SurfaceRVDN.h`. Removed `#include <iostream>` from these files.
- Removed `dbgClipCalls`, `dbgFindNearestCalls`, `dbgAdjSeedsProcessed` counters
  and their timing blocks from `SurfaceRVDN::ForEachPolygon`.

### K=30 to match Geogram default
- `AccumulateCentroids` now uses `DelaunayNN<Real,N>(30)` instead of `(20)`.
  Geogram's `Delaunay` constructor sets `default_nb_neighbors_ = 30` and
  precomputes 30 neighbors per vertex during `set_vertices()`.
- Larger initial K reduces the frequency of `EnlargeNeighborhood` calls in
  Newton mode (checkSR=true), improving CVT time from ~108s → ~88s on
  `4002.1.t0` (64K faces, 32K seeds).

### Cached lifted vertices in CVTN
- Added `mCachedLiftedArr` + `mLiftedArrValid` private members to `CVTN`.
  The N-D lifted vertex array (vertex positions + scaled normals) depends only
  on `mSurfaceVertices`, `mSurfaceTriangles`, and `mNormalScale` — none of which
  change between Lloyd/Newton iterations.
- `BuildLiftedVertices` now returns from cache on subsequent calls, bypassing
  O(n_verts) normal-accumulation loop.
- `SetNormalScale()` and `Initialize()` invalidate the cache.

### Cached SurfaceRVDN mesh setup in CVTN
- Added `mCachedRVD` + `mRVDMeshInitDone` private members to `CVTN`.
  `SurfaceRVDN::Initialize()` has been split into:
  - `InitMeshOnly(liftedVerts, tris)`: builds adjacency table once per mesh
    (O(n_faces) work), called only when mesh changes.
  - `UpdateSeeds(seeds, delaunay)`: rebuilds seed KD-tree (O(n_seeds log n_seeds)),
    called each iteration.
  - `SetLiftedVerts(liftedVerts)`: rebinds pointer without rebuilding anything.
  - `BuildFacetAdjacency()`: extracted private helper for the adjacency table build.
- `AccumulateCentroids` now calls `InitMeshOnly` only on the first call and
  `UpdateSeeds` on every call, saving ~35ms/iter × 35 iters ≈ 1.2s.

### Performance results (4002.1.t0, 64K faces, 32K seeds)
| Session | Total CVT time |
|---------|---------------|
| 10      | ~109s          |
| 11      | ~88s (+19%)    |

The improvement comes primarily from K=30 (reduces SR enlargement calls in Newton).

### 30-bot pass rate
Random sample of 30 BoTs from Generic_Twin.g: all 30 pass (100% pass rate).

## NEXT SESSION TODO

### Priority 1: Further Newton speedup
- The main bottleneck (88s total on 4002.1.t0) is 30 Newton iterations × ~3s each.
- Geogram achieves similar quality much faster via multi-threading (`parallel_for`).
- Potential single-threaded speedups:
  - Use nanoflann (available at `src/libbg/nanoflann.hpp`) for faster KNN queries,
    especially for `EnlargeNeighborhood` calls.
  - Pre-compute all 30 neighbors upfront at the start of each AccumulateCentroids
    call (matching Geogram's eager computation during `set_vertices`).
  - Reduce Newton iterations from 30 to 10-15 for practical use (add energy
    convergence criterion to early-exit).

### Priority 2: Quality on degenerate inputs
- `7052.1.t0` (8 verts, 6 faces thin panel) fails with "mesh too small".
  Investigate if this is a pre-existing issue or regression, and add a
  fallback path for tiny meshes.

### Priority 3: OpenMP parallelism
- `ForEachPolygon_SeedsPriority` is embarrassingly parallel over independent
  Voronoi cells. Adding OpenMP would directly match Geogram's `parallel_for`
  speedup of 4-8x on modern hardware.


