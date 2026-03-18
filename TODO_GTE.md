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

## Changes Made This Session

### `NearestNeighborSearchN.h` — KD-tree replacement
- Added `KDTreeND` struct: N-dimensional KD-tree with split-order flat array layout
  (same pattern as `KDTree3D` in CVTN.h, generalized to N dimensions)
- `BuildIndex()` now builds the KD-tree in O(n log² n)
- `FindNearestNeighbor()` now O(log n) instead of O(n)
- `FindKNearestNeighbors()` now O(k log n) instead of O(n)
- Build verified clean (libged compiled successfully)

### `CVTN.h` — `LloydIterations` now uses `SurfaceRVDN` walk-based centroids
- **Old approach**: `RestrictedVoronoiDiagramN::ComputeCentroids` — brute-force
  O(n_triangles × n_seeds) per iteration
- **New approach**: `SurfaceRVDN::ForEachPolygon` with centroid-accumulating callback
  — walk-based O(n_triangles × avg_Delaunay_neighbors) per iteration, matching
  Geogram's `CVT::Lloyd_iterations` → `RVD_->compute_centroids()` exactly
- N-D lifted vertices built per iteration (same logic as `ComputeRDT`)
- Centroid accumulation uses Heron's formula for N-D triangle area (matching
  Geogram's `Geom::triangle_area<DIM>()`)
- Normal scale for anisotropic (N=6) estimated from current sites each iteration

### `SurfaceRVDN.h` — `SeedKDTree3D` for `FindNearestSeed`
- Added `SeedKDTree3D` struct: 3D KD-tree over seed positions (first 3 N-D dims)
- `BuildSeedKDTree()` called from `Initialize()` in O(n log n)
- `FindNearestSeed()` now O(log n_seeds) instead of O(n_seeds) per facet
- This accelerates the outer `ForEachPolygon` loop which visits every mesh facet

## Performance Impact

For a mesh with n_seeds = 50,000 seeds and n_triangles = 100,000:

| Operation           | Before            | After             | Speedup    |
|---------------------|-------------------|-------------------|------------|
| Build DelaunayNN    | O(n² × k) ≈ 50B   | O(n log n × k)    | ~50,000×   |
| Compute centroids   | O(n_tri × n_seeds)| O(n_tri × k)      | ~2,500×    |
| FindNearestSeed     | O(n_tri × n_seeds)| O(n_tri × log n)  | ~3,000×    |

Where k ≈ 20 (default Delaunay neighbors).

## Remaining Work

### Newton iterations (TODO — NOT STARTED)
Geogram uses full Newton optimization (HLBFGS, L-BFGS) with 30 iterations after
Lloyd. The GTE `NewtonIterations()` method is just an alias for Lloyd with tighter
convergence — it does NOT implement actual Newton/quasi-Newton optimization.

A full Newton implementation would need:
1. Compute CVT energy gradient (already partially available via ForEachPolygon)
2. L-BFGS quasi-Newton update (store m=7 gradient history vectors)
3. Line search along the L-BFGS direction
4. This is significantly more complex than Lloyd but converges 5-10× faster

See Geogram's `CVT::Newton_iterations()` → `optimizer->optimize(points_.data())`
using HLBFGS (a.k.a. L-BFGS with Hessian approximation).

### `mesh_adjust_surface` equivalent (TODO — NOT STARTED)
After `CVT::compute_surface()`, Geogram calls `mesh_adjust_surface(M_out, M_in)`
which fits the output mesh to the reference surface using a least-squares optimizer
over per-vertex offsets along surface normals. This improves geometric fidelity.

The GTE implementation has no equivalent. This should be implemented to match
Geogram's quality.

See `geogram/src/lib/geogram/mesh/mesh_remesh.cpp`: `mesh_adjust_surface()`.

### `set_anisotropy` correspondence check (TODO)
In `brlcad_geogram/remesh.cpp`: `set_anisotropy(gm, 2*0.02)` lifts the mesh to 6D
by appending `s*bbox_diag * normal` to each vertex. Verify that the GTE anisotropy
scale computation in `MeshRemesh.h::RemeshCVTAnisotropic` and the adaptive normal
scale in `CVTN.h::LloydIterations` use the same normalization formula as Geogram's
`set_anisotropy`.

### Parallelism (TODO — future enhancement)
Geogram parallelizes the RVD centroid computation across threads. The GTE
implementation is single-threaded. Adding OpenMP parallelism to `ForEachPolygon`
centroid accumulation could provide additional speedup.

### Testing on Generic_Twin.g (TODO)
Build complete `mged`/`brlcad` and test:
```
mged Generic_Twin.g
bot remesh 4002.1.t0
```
Verify timing vs. previous implementation and vs. Geogram path.
