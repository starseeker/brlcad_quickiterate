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

## NEXT SESSION TODO

### Priority 1: Fix DelaunayNN::UpdateNeighborhoods() performance

**Problem**: Called in every Lloyd/Newton iteration, rebuilds all 32580 neighborhoods.

**Fix options** (in order of preference):
1. **Lazy neighborhood computation** (easiest, highest impact):
   - In `UpdateNeighborhoods()`, only build the NN search structure; don't precompute any neighborhoods
   - In `ComputeNeighborhood(i)`, compute on first access and cache in mNeighborhoods[i]
   - Use `mNeighborhoods[i].empty()` as "not computed yet" flag
   - But need to handle `EnlargeNeighborhood()` calls correctly
   - This avoids computing neighborhoods for seeds that are never visited during `ForEachPolygon`

2. **Cache DelaunayNN across iterations** (medium difficulty):
   - In `CVTN::AccumulateCentroids()`, accept a pre-built `DelaunayNN` as parameter
   - In `LloydIterations()`, build it once and update incrementally each iteration
   - Between iterations, only update the NN search with new seed positions (rebuild KD-tree)
   - Don't recompute neighborhoods — invalidate cache and compute lazily

3. **Match Geogram's on-demand neighborhood enlargement**:
   - Geogram's `ClipCellFacet` calls `get_neighbors(seed)` which only returns a
     fixed initial set, and `enlarge_neighborhood(seed, new_size)` queries for more
   - The key is that only the seeds actually visited in `ForEachPolygon` have their
     neighborhoods computed, not all 32580 seeds
   - Geogram doesn't call `UpdateNeighborhoods()` at all — it just has the NN search
     structure available and queries on demand

**Recommended implementation**:
In `DelaunayNN.h`, change `UpdateNeighborhoods()` to NOT precompute any neighborhoods:
```cpp
void UpdateNeighborhoods() {
    // Just ensure the NN search structure is built; neighborhoods are
    // computed lazily on first access via GetNeighbors() / EnlargeNeighborhood()
    mNeighborhoods.clear();
    mNeighborhoods.resize(this->mNumVertices);
    mComputed.assign(this->mNumVertices, false);
    // (NN search structure already built by SetVertices → BuildIndex())
}
```
Then in `GetNeighbors(v)`:
```cpp
std::vector<int32_t> GetNeighbors(int32_t v) {
    if (!mComputed[v]) {
        ComputeNeighborhood(v);
        mComputed[v] = true;
    }
    return mNeighborhoods[v];
}
```
And in `EnlargeNeighborhood(v, nb)`:
```cpp
void EnlargeNeighborhood(int32_t v, size_t nb) {
    // Query for nb neighbors (already have v's current neighborhood)
    // Just call FindKNearestNeighborsToPoint(v, nb, ...) and store
}
```

### Priority 2: Avoid rebuilding DelaunayNN every AccumulateCentroids call

Currently `AccumulateCentroids` creates a new `DelaunayNN` and `SurfaceRVDN` every call.
With lazy neighborhoods, the per-call cost is just:
- Rebuild the KD-tree for seeds: O(n log n) = fast
- The BFS walk in ForEachPolygon: O(facets × k) = main work (this is good)
- Per-accessed neighborhood: O(k log n) = only for visited seeds (lazy)

This should reduce the total overhead significantly.

### Priority 3: Parallelism
After fixing lazy neighborhoods, consider OpenMP parallelism in `ForEachPolygon`
for additional speedup, matching Geogram's parallel RVD computation.

### Priority 4: Testing
Once CVT runs to completion, test all BoT objects in Generic_Twin.g.
The timing instrumentation added to `remesh.cpp` (with `bu_log` timing calls)
can be kept for now to monitor performance during development.

