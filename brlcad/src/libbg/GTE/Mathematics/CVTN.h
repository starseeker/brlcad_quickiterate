// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// Centroidal Voronoi Tessellation for N dimensions
//
// Implements Lloyd relaxation and Newton optimization for CVT in
// arbitrary dimensions. Main use case is anisotropic remeshing with
// 6D sites (position + scaled normal).
//
// Based on:
// - geogram/src/lib/geogram/voronoi/CVT.h (reference architecture)
// - RestrictedVoronoiDiagramN (centroid computation)
// - DelaunayNN (neighborhood structure)
//
// License: Boost Software License 1.0

#pragma once

#include <Mathematics/Vector.h>
#include <Mathematics/Vector3.h>
#include <Mathematics/DelaunayNN.h>
#include <Mathematics/RestrictedVoronoiDiagramN.h>
#include <Mathematics/SurfaceRVDN.h>
#include <Mathematics/Logger.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <vector>

namespace gte
{
    // Centroidal Voronoi Tessellation for N dimensions
    //
    // Distributes sites evenly over a 3D surface mesh using N-dimensional
    // distance metric. The main algorithm is Lloyd relaxation, which
    // iteratively moves sites to centroids of their Voronoi cells.
    //
    // Use cases:
    // - Isotropic CVT: N=3, uniform distribution
    // - Anisotropic CVT: N=6, feature-aligned distribution
    //
    template <typename Real, size_t N>
    class CVTN
    {
    public:
        using PointN = Vector<N, Real>;
        using Point3 = Vector3<Real>;
        
        static_assert(
            std::is_floating_point<Real>::value,
            "Real must be float or double.");
        
        static_assert(N >= 3,
            "Dimension must be at least 3 (for 3D position).");
        
        // Constructor
        CVTN()
            : mConvergenceThreshold(static_cast<Real>(1e-6))
            , mVerbose(false)
            , mTimeLimitSeconds(0.0)
            , mIterationsCompleted(0)
        {
        }
        
        virtual ~CVTN() = default;
        
        // Initialize with surface mesh
        bool Initialize(
            std::vector<Point3> const& surfaceVertices,
            std::vector<std::array<int32_t, 3>> const& surfaceTriangles)
        {
            LogAssert(
                !surfaceVertices.empty() && !surfaceTriangles.empty(),
                "Surface mesh must not be empty.");
            
            mSurfaceVertices = surfaceVertices;
            mSurfaceTriangles = surfaceTriangles;
            
            return true;
        }
        
        // Compute initial random sampling on surface
        bool ComputeInitialSampling(size_t numSites, unsigned int seed = 12345)
        {
            if (mSurfaceVertices.empty() || mSurfaceTriangles.empty())
            {
                return false;
            }
            
            // Random number generator
            std::mt19937 rng(seed);
            std::uniform_real_distribution<Real> dist(static_cast<Real>(0), static_cast<Real>(1));
            
            mSites.clear();
            mSites.reserve(numSites);
            
            // Compute triangle areas for weighted sampling
            std::vector<Real> triangleAreas;
            Real totalArea = static_cast<Real>(0);
            
            for (auto const& tri : mSurfaceTriangles)
            {
                Point3 const& v0 = mSurfaceVertices[tri[0]];
                Point3 const& v1 = mSurfaceVertices[tri[1]];
                Point3 const& v2 = mSurfaceVertices[tri[2]];
                
                Real area = ComputeTriangleArea(v0, v1, v2);
                triangleAreas.push_back(area);
                totalArea += area;
            }
            
            // Generate random points
            for (size_t i = 0; i < numSites; ++i)
            {
                // Select triangle weighted by area
                Real r = dist(rng) * totalArea;
                Real sum = static_cast<Real>(0);
                size_t triIdx = 0;
                
                for (size_t j = 0; j < triangleAreas.size(); ++j)
                {
                    sum += triangleAreas[j];
                    if (sum >= r)
                    {
                        triIdx = j;
                        break;
                    }
                }
                
                // Random point in triangle
                auto const& tri = mSurfaceTriangles[triIdx];
                Point3 const& v0 = mSurfaceVertices[tri[0]];
                Point3 const& v1 = mSurfaceVertices[tri[1]];
                Point3 const& v2 = mSurfaceVertices[tri[2]];
                
                Real u = dist(rng);
                Real v = dist(rng);
                if (u + v > static_cast<Real>(1))
                {
                    u = static_cast<Real>(1) - u;
                    v = static_cast<Real>(1) - v;
                }
                
                Point3 p = v0 + u * (v1 - v0) + v * (v2 - v0);
                
                // Create N-dimensional site
                PointN site;
                site[0] = p[0];
                site[1] = p[1];
                site[2] = p[2];
                
                // Initialize other dimensions to zero
                // (caller can modify these for anisotropic metrics)
                for (size_t d = 3; d < N; ++d)
                {
                    site[d] = static_cast<Real>(0);
                }
                
                mSites.push_back(site);
            }
            
            if (mVerbose)
            {
                std::cout << "Generated " << mSites.size() << " initial sites\n";
            }
            
            return true;
        }

        // Compute initial sampling using Mitchell's best-candidate algorithm.
        //
        // For each new site, generates 'numCandidates' random candidates on the
        // surface and picks the one farthest (in 3D) from all existing sites.
        // This matches Geogram's farthest-point / Poisson-disk style initialisation
        // and produces more evenly spaced seeds than pure random sampling.
        //
        // numCandidates: number of random candidates per site (default 10 for speed;
        // higher values (e.g. 20) give slightly better distribution at higher cost).
        //
        // Performance: uses a 3-D k-d tree with periodic rebuilds to reduce the
        // per-candidate nearest-neighbour query from O(n) to O(log n), giving
        // O(numSites * numCandidates * log(numSites)) overall instead of O(n^2).
        bool ComputeInitialSamplingFarthestPoint(
            size_t numSites,
            unsigned int seed = 12345,
            size_t numCandidates = 10)
        {
            if (mSurfaceVertices.empty() || mSurfaceTriangles.empty() || numSites == 0)
            {
                return false;
            }

            std::mt19937 rng(seed);
            std::uniform_real_distribution<Real> uni(static_cast<Real>(0), static_cast<Real>(1));

            // Build cumulative area table for O(log n) triangle selection
            std::vector<Real> cumArea;
            cumArea.reserve(mSurfaceTriangles.size());
            Real totalArea = static_cast<Real>(0);
            for (auto const& tri : mSurfaceTriangles)
            {
                totalArea += ComputeTriangleArea(
                    mSurfaceVertices[tri[0]],
                    mSurfaceVertices[tri[1]],
                    mSurfaceVertices[tri[2]]);
                cumArea.push_back(totalArea);
            }

            // Helper: sample one random point on the surface
            auto samplePoint = [&]() -> Point3
            {
                Real r = uni(rng) * totalArea;
                auto it = std::lower_bound(cumArea.begin(), cumArea.end(), r);
                size_t triIdx = static_cast<size_t>(it - cumArea.begin());
                if (triIdx >= mSurfaceTriangles.size())
                {
                    triIdx = mSurfaceTriangles.size() - 1;
                }
                auto const& tri = mSurfaceTriangles[triIdx];
                Point3 const& v0 = mSurfaceVertices[tri[0]];
                Point3 const& v1 = mSurfaceVertices[tri[1]];
                Point3 const& v2 = mSurfaceVertices[tri[2]];
                Real u = uni(rng);
                Real v = uni(rng);
                if (u + v > static_cast<Real>(1))
                {
                    u = static_cast<Real>(1) - u;
                    v = static_cast<Real>(1) - v;
                }
                return v0 + u * (v1 - v0) + v * (v2 - v0);
            };

            // K-D tree accelerated minimum-distance query.
            //
            // The k-d tree covers mSites[0..treeSize).  Sites added since the
            // last rebuild are in the linear tail mSites[treeSize..end).  This
            // gives O(log n) average query cost instead of O(n), while keeping
            // the rebuild cost amortised to O(sqrt(n)*log(n)) per site.
            //
            // Rebuild interval: floor(sqrt(numSites)), clamped to [64, 512].
            // Lower bound (64): avoids rebuilding too frequently when numSites is small,
            // keeping per-site rebuild cost reasonable.
            // Upper bound (512): caps the linear buffer scan cost for very large inputs,
            // ensuring the buffer never dominates the O(log n) tree query.
            KDTree3D kdTree;
            size_t treeSize = 0;
            size_t rebuildEvery = static_cast<size_t>(
                std::max(64.0, std::min(512.0, std::sqrt(static_cast<double>(numSites)))));

            // Returns min squared 3D distance from p to any current site.
            // If the distance drops below 'cap' the function returns early —
            // the caller uses this to skip candidates that cannot beat the
            // current best.
            auto minDistSqFast = [&](Point3 const& p, Real cap) -> Real
            {
                Real best = kdTree.nearestDistSqCapped(p, cap);
                for (size_t j = treeSize; j < mSites.size(); ++j)
                {
                    Real dx = p[0] - mSites[j][0];
                    Real dy = p[1] - mSites[j][1];
                    Real dz = p[2] - mSites[j][2];
                    Real d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < best)
                    {
                        best = d2;
                        if (best < cap)
                        {
                            return best;  // closer than cap → this candidate is disqualified
                        }
                    }
                }
                return best;
            };

            // Rebuild the k-d tree from all current sites when the linear
            // buffer has grown to rebuildEvery entries.
            auto maybeRebuild = [&]()
            {
                if (mSites.size() - treeSize >= rebuildEvery)
                {
                    std::vector<Point3> pts3;
                    pts3.reserve(mSites.size());
                    for (auto const& s : mSites)
                    {
                        pts3.push_back(Point3{s[0], s[1], s[2]});
                    }
                    kdTree.build(std::move(pts3));
                    treeSize = mSites.size();
                }
            };

            mSites.clear();
            mSites.reserve(numSites);

            // First site: purely random
            {
                Point3 p = samplePoint();
                PointN site;
                site[0] = p[0];
                site[1] = p[1];
                site[2] = p[2];
                for (size_t d = 3; d < N; ++d)
                {
                    site[d] = static_cast<Real>(0);
                }
                mSites.push_back(site);
            }

            // Subsequent sites: best of numCandidates random candidates.
            // The first candidate uses an uncapped query to establish bestD.
            // All subsequent candidates use the capped query (early-exit) so
            // that candidates which cannot beat the current best are rejected
            // as soon as a nearby existing site is encountered.
            for (size_t i = 1; i < numSites; ++i)
            {
                maybeRebuild();

                Point3 bestPt = samplePoint();
                Real bestD = minDistSqFast(bestPt, std::numeric_limits<Real>::max());

                for (size_t k = 1; k < numCandidates; ++k)
                {
                    Point3 candidate = samplePoint();
                    Real d = minDistSqFast(candidate, bestD);
                    if (d > bestD)
                    {
                        bestD  = d;
                        bestPt = candidate;
                    }
                }

                PointN site;
                site[0] = bestPt[0];
                site[1] = bestPt[1];
                site[2] = bestPt[2];
                for (size_t d = 3; d < N; ++d)
                {
                    site[d] = static_cast<Real>(0);
                }
                mSites.push_back(site);
            }

            if (mVerbose)
            {
                std::cout << "Generated " << mSites.size()
                          << " initial sites (farthest-point)\n";
            }

            return true;
        }
        
        // Set sites explicitly
        void SetSites(std::vector<PointN> const& sites)
        {
            mSites = sites;
        }
        
        // Get current sites
        std::vector<PointN> const& GetSites() const
        {
            return mSites;
        }
        
        // Get number of sites
        size_t GetNumSites() const
        {
            return mSites.size();
        }
        
        // Lloyd iterations - move sites to centroids of their Voronoi cells.
        //
        // This is a direct translation of Geogram's CVT::Lloyd_iterations():
        //   for each iteration:
        //     delaunay->set_vertices(sites)        [O(n log n) with KD-tree NN]
        //     RVD->compute_centroids(mg, m)         [walk-based O(n_tri) per iter]
        //     sites = mg / m
        //
        // The centroid computation uses SurfaceRVDN::ForEachPolygon() which
        // implements Geogram's compute_surfacic_with_cnx_priority walk — the
        // correct polygon-clipping approach that gives EXACT Voronoi cell
        // centroids, unlike the old brute-force triangle-assignment approach
        // which was O(n_triangles * n_seeds) per iteration.
        //
        // With the KD-tree-backed DelaunayNN:
        //   - BuildDelaunay:       O(n log n)   was O(n²)
        //   - ComputeCentroids:    O(n_tri · k) was O(n_tri · n_seeds)
        // where k is the average number of Delaunay neighbors per seed (~20).
        //
        // If a time limit has been set via SetTimeLimit(), the loop stops
        // after the current iteration completes once the wall-clock deadline
        // is reached.  The number of iterations actually completed is stored
        // in mIterationsCompleted and can be queried via GetIterationsCompleted().
        bool LloydIterations(size_t numIterations)
        {
            if (mSites.empty())
            {
                return false;
            }

            mIterationsCompleted = 0;

            using Clock = std::chrono::steady_clock;
            Clock::time_point deadline{};
            bool hasDeadline = (mTimeLimitSeconds > 0.0);
            if (hasDeadline)
            {
                deadline = Clock::now() +
                    std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(mTimeLimitSeconds));
            }

            size_t numSeeds = mSites.size();

            for (size_t iter = 0; iter < numIterations; ++iter)
            {
                // ── Step 1: Build Delaunay over current N-D sites ──────────────
                // With the KD-tree-backed NearestNeighborSearchN this is O(n log n)
                // instead of the previous O(n²).
                DelaunayNN<Real, N> delaunay(20);
                delaunay.SetVertices(numSeeds, mSites.data());

                // ── Step 2: Build N-D lifted mesh vertices ─────────────────────
                // For N=3 (isotropic): identity — dims 0–2 are 3D position.
                // For N=6 (anisotropic): append scaled vertex normals so that the
                // Sutherland-Hodgman polygon clipping uses the correct N-D metric.
                // This matches the lifting done in ComputeRDT's multinerve path and
                // in RemeshCVTAnisotropic (MeshRemesh.h).
                std::vector<std::array<Real, N>> liftedArr(mSurfaceVertices.size());

                // Estimate normal scale from current sites' dims 3-N-1 magnitudes
                // (same formula as RestrictedVoronoiDiagramN::ComputeCentroids).
                Real normalScale = static_cast<Real>(0);
                if constexpr (N > 3)
                {
                    for (size_t s = 0; s < numSeeds; ++s)
                    {
                        Real ns = static_cast<Real>(0);
                        for (size_t d = 3; d < N; ++d) ns += mSites[s][d] * mSites[s][d];
                        normalScale += std::sqrt(ns);
                    }
                    if (numSeeds > 0)
                        normalScale /= static_cast<Real>(numSeeds);
                }

                if constexpr (N > 3)
                {
                    // Accumulate area-weighted face normals per vertex
                    std::vector<std::array<Real, 3>> vertNorm(
                        mSurfaceVertices.size(), {Real(0), Real(0), Real(0)});
                    for (auto const& tri : mSurfaceTriangles)
                    {
                        Point3 const& v0 = mSurfaceVertices[tri[0]];
                        Point3 const& v1 = mSurfaceVertices[tri[1]];
                        Point3 const& v2 = mSurfaceVertices[tri[2]];
                        Point3 fn = Cross(v1 - v0, v2 - v0);
                        for (int lv = 0; lv < 3; ++lv)
                        {
                            vertNorm[tri[lv]][0] += fn[0];
                            vertNorm[tri[lv]][1] += fn[1];
                            vertNorm[tri[lv]][2] += fn[2];
                        }
                    }
                    for (size_t v = 0; v < mSurfaceVertices.size(); ++v)
                    {
                        liftedArr[v][0] = mSurfaceVertices[v][0];
                        liftedArr[v][1] = mSurfaceVertices[v][1];
                        liftedArr[v][2] = mSurfaceVertices[v][2];
                        Real nx = vertNorm[v][0], ny = vertNorm[v][1], nz = vertNorm[v][2];
                        Real len = std::sqrt(nx*nx + ny*ny + nz*nz);
                        if (len > static_cast<Real>(1e-10))
                        {
                            nx /= len; ny /= len; nz /= len;
                        }
                        if constexpr (N >= 6)
                        {
                            liftedArr[v][3] = nx * normalScale;
                            liftedArr[v][4] = ny * normalScale;
                            liftedArr[v][5] = nz * normalScale;
                        }
                        for (size_t d = 6; d < N; ++d)
                            liftedArr[v][d] = static_cast<Real>(0);
                    }
                }
                else
                {
                    // N=3: copy 3D positions directly (no lifting needed)
                    for (size_t v = 0; v < mSurfaceVertices.size(); ++v)
                    {
                        liftedArr[v][0] = mSurfaceVertices[v][0];
                        liftedArr[v][1] = mSurfaceVertices[v][1];
                        liftedArr[v][2] = mSurfaceVertices[v][2];
                    }
                }

                // ── Step 3: Convert N-D sites to std::array for SurfaceRVDN ──
                std::vector<std::array<Real, N>> seedsArr(numSeeds);
                for (size_t s = 0; s < numSeeds; ++s)
                    for (size_t d = 0; d < N; ++d) seedsArr[s][d] = mSites[s][d];

                // ── Step 4: Walk the surface and accumulate N-D centroids ──────
                // Uses SurfaceRVDN::ForEachPolygon — a direct translation of
                // Geogram's compute_surfacic_with_cnx_priority walk.  For each
                // (seed, facet) restricted polygon, we fan-triangulate and
                // accumulate area-weighted N-D centroids exactly as Geogram does
                // in its CVT::Lloyd_iterations / compute_centroids callback.
                SurfaceRVDN<Real, N> rvd;
                rvd.Initialize(liftedArr, mSurfaceTriangles, seedsArr, delaunay);

                std::vector<std::array<Real, N>> mg(numSeeds);
                std::vector<Real>               m(numSeeds, static_cast<Real>(0));
                for (auto& a : mg) a.fill(static_cast<Real>(0));

                rvd.ForEachPolygon([&](
                    int32_t seed, int32_t /*facet*/,
                    RVDPolygon<Real, N> const& P,
                    bool /*compChanged*/, int32_t /*compID*/)
                {
                    const size_t nv = P.nb_vertices();
                    for (size_t i = 1; i + 1 < nv; ++i)
                    {
                        // N-D triangle area (Heron's formula on N-D edge lengths).
                        // Direct translation of Geom::triangle_area<DIM>() from
                        // geogram/src/lib/geogram/basic/geometry_nd.h.
                        Real ea = Real(0), eb = Real(0), ec = Real(0);
                        for (size_t d = 0; d < N; ++d)
                        {
                            Real e0 = P.V[0].pos[d] - P.V[i].pos[d];
                            Real e1 = P.V[i].pos[d] - P.V[i+1].pos[d];
                            Real e2 = P.V[i+1].pos[d] - P.V[0].pos[d];
                            ea += e0 * e0;
                            eb += e1 * e1;
                            ec += e2 * e2;
                        }
                        ea = std::sqrt(ea);
                        eb = std::sqrt(eb);
                        ec = std::sqrt(ec);
                        Real hs = Real(0.5) * (ea + eb + ec);
                        Real A2 = hs * (hs - ea) * (hs - eb) * (hs - ec);
                        Real area = std::sqrt(std::max(A2, Real(0)));

                        Real inv3 = area / Real(3);
                        for (size_t d = 0; d < N; ++d)
                        {
                            mg[seed][d] += inv3 * (P.V[0].pos[d]
                                                 + P.V[i].pos[d]
                                                 + P.V[i+1].pos[d]);
                        }
                        m[seed] += area;
                    }
                });

                // ── Step 5: Update sites = mg / m  ─────────────────────────────
                // Matches Geogram's normalization:
                //   if(m[j] > 1e-30) points_[j] = mg[j] / m[j];
                Real maxMovement = static_cast<Real>(0);
                for (size_t i = 0; i < numSeeds; ++i)
                {
                    if (m[i] > static_cast<Real>(1e-30))
                    {
                        Real inv_m = static_cast<Real>(1) / m[i];
                        Real dSq   = static_cast<Real>(0);
                        for (size_t d = 0; d < N; ++d)
                        {
                            Real newCoord = mg[i][d] * inv_m;
                            Real diff     = mSites[i][d] - newCoord;
                            dSq          += diff * diff;
                            mSites[i][d]  = newCoord;
                        }
                        maxMovement = std::max(maxMovement, std::sqrt(dSq));
                    }
                }

                ++mIterationsCompleted;

                if (mVerbose)
                {
                    std::cout << "Lloyd iteration " << (iter + 1)
                              << ": max movement = " << maxMovement << "\n";
                }

                // Check convergence
                if (maxMovement < mConvergenceThreshold)
                {
                    if (mVerbose)
                    {
                        std::cout << "Converged after " << (iter + 1)
                                  << " iterations\n";
                    }
                    break;
                }

                // Check time limit after completing the iteration
                if (hasDeadline && Clock::now() >= deadline)
                {
                    if (mVerbose)
                    {
                        std::cout << "Lloyd time limit reached after " << (iter + 1)
                                  << " iterations\n";
                    }
                    break;
                }
            }

            return true;
        }
        
        // Newton iterations (simplified version focusing on Lloyd)
        // Full Newton requires Hessian computation which is complex
        // For now, this is an alias for Lloyd with tighter convergence
        bool NewtonIterations(size_t numIterations)
        {
            // For a full Newton implementation, we would need:
            // 1. Compute energy gradient
            // 2. Approximate Hessian (BFGS)
            // 3. Solve linear system
            // 4. Line search
            //
            // This is complex and Lloyd works well for our use case.
            // We'll use Lloyd with tighter convergence as a practical alternative.
            
            Real savedThreshold = mConvergenceThreshold;
            mConvergenceThreshold *= static_cast<Real>(0.1);  // Tighter convergence
            
            bool result = LloydIterations(numIterations);
            
            mConvergenceThreshold = savedThreshold;
            return result;
        }
        
        // Set convergence threshold
        void SetConvergenceThreshold(Real threshold)
        {
            mConvergenceThreshold = threshold;
        }
        
        // Get convergence threshold
        Real GetConvergenceThreshold() const
        {
            return mConvergenceThreshold;
        }

        // Set an optional wall-clock time limit (in seconds) for LloydIterations.
        // When the limit is reached after completing an iteration, the loop stops
        // early and the current (partially converged) sites are kept.
        // A value of 0.0 or less disables the limit (default).
        void SetTimeLimit(double seconds)
        {
            mTimeLimitSeconds = seconds;
        }

        // Returns the number of Lloyd iterations that actually completed in the
        // most recent LloydIterations() call.  Useful for diagnosing whether the
        // time limit caused early exit.
        size_t GetIterationsCompleted() const
        {
            return mIterationsCompleted;
        }

        // Enable/disable verbose output
        void SetVerbose(bool verbose)
        {
            mVerbose = verbose;
        }
        
        // Compute total CVT energy (for analysis)
        Real ComputeEnergy() const
        {
            if (mSites.empty())
            {
                return static_cast<Real>(0);
            }
            
            Real energy = static_cast<Real>(0);
            
            // For each triangle, find nearest site and accumulate distance
            for (auto const& tri : mSurfaceTriangles)
            {
                Point3 const& v0 = mSurfaceVertices[tri[0]];
                Point3 const& v1 = mSurfaceVertices[tri[1]];
                Point3 const& v2 = mSurfaceVertices[tri[2]];
                
                Point3 triCentroid = (v0 + v1 + v2) / static_cast<Real>(3);
                Real area = ComputeTriangleArea(v0, v1, v2);
                
                // Find nearest site
                int32_t nearestIdx = FindNearestSite(triCentroid);
                if (nearestIdx >= 0)
                {
                    PointN query;
                    query[0] = triCentroid[0];
                    query[1] = triCentroid[1];
                    query[2] = triCentroid[2];
                    for (size_t d = 3; d < N; ++d)
                    {
                        query[d] = static_cast<Real>(0);
                    }
                    
                    Real dist = Distance(query, mSites[nearestIdx]);
                    energy += dist * dist * area;
                }
            }
            
            return energy;
        }

        // Compute Restricted Delaunay Triangulation from current sites.
        //
        // This is the equivalent of Geogram's CVT::compute_surface().
        //
        // multinerve=true (default): Full geometric RVD via SurfaceRVDN —
        //   a direct translation of Geogram's
        //   GenRestrictedVoronoiDiagram<DIM>::compute_surfacic_with_cnx_priority()
        //   + GetConnectedComponentsPrimalTriangles callback.
        //   Each connected component of a seed's Restricted Voronoi Cell (RVC)
        //   becomes one output vertex, matching Geogram's RDT_MULTINERVE mode.
        //   The surface mesh is lifted to N-D (vertex normals scaled by the
        //   same factor used during Lloyd iterations) so the clipping is done
        //   in the correct N-D metric for both isotropic (N=3) and anisotropic
        //   (N=6) cases.
        //
        // multinerve=false: simplified path — one RVC centroid vertex per seed,
        //   connectivity from vertex-to-seed Voronoi assignment.
        //
        // Returns true if a non-empty triangulation was produced.
        bool ComputeRDT(
            std::vector<Point3>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            bool multinerve = true) const
        {
            if (mSites.empty() || mSurfaceVertices.empty() || mSurfaceTriangles.empty())
            {
                return false;
            }

            size_t numSeeds = mSites.size();

            if (multinerve)
            {
                // ------------------------------------------------------------
                // Full geometric multi-nerve RDT via SurfaceRVDN.
                //
                // Step 1: Lift mesh vertices to N-D.
                //   For N=3 (isotropic): identity — dims 0-2 are 3D position.
                //   For N=6 (anisotropic): append scaled vertex normals so
                //     the clipping metric matches the N-D Lloyd iterations.
                //   Vertex normals are area-weighted averages of adjacent face
                //   normals, scaled by the same normalScale used for the sites.
                // ------------------------------------------------------------
                std::vector<PointN> liftedVerts(mSurfaceVertices.size());

                // Estimate normal scale from the sites' dims 3-N-1 magnitudes
                // (same formula as RestrictedVoronoiDiagramN::ComputeCentroids)
                Real normalScale = static_cast<Real>(0);
                if constexpr (N > 3)
                {
                    for (size_t s = 0; s < numSeeds; ++s)
                    {
                        Real normSq = static_cast<Real>(0);
                        for (size_t d = 3; d < N; ++d)
                        {
                            normSq += mSites[s][d] * mSites[s][d];
                        }
                        normalScale += std::sqrt(normSq);
                    }
                    if (numSeeds > 0)
                    {
                        normalScale /= static_cast<Real>(numSeeds);
                    }
                }

                if constexpr (N > 3)
                {
                    // Accumulate area-weighted face normals per vertex
                    std::vector<std::array<Real, 3>> vertNorm(
                        mSurfaceVertices.size(), {Real(0), Real(0), Real(0)});

                    for (auto const& tri : mSurfaceTriangles)
                    {
                        Point3 const& v0 = mSurfaceVertices[tri[0]];
                        Point3 const& v1 = mSurfaceVertices[tri[1]];
                        Point3 const& v2 = mSurfaceVertices[tri[2]];
                        Point3 faceN = Cross(v1 - v0, v2 - v0);  // area * 2 * unit_normal
                        for (int i = 0; i < 3; ++i)
                        {
                            vertNorm[tri[i]][0] += faceN[0];
                            vertNorm[tri[i]][1] += faceN[1];
                            vertNorm[tri[i]][2] += faceN[2];
                        }
                    }

                    // Build 6-D lifted vertices
                    for (size_t v = 0; v < mSurfaceVertices.size(); ++v)
                    {
                        liftedVerts[v][0] = mSurfaceVertices[v][0];
                        liftedVerts[v][1] = mSurfaceVertices[v][1];
                        liftedVerts[v][2] = mSurfaceVertices[v][2];
                        // Normalize and scale
                        Real nx = vertNorm[v][0], ny = vertNorm[v][1], nz = vertNorm[v][2];
                        Real len = std::sqrt(nx*nx + ny*ny + nz*nz);
                        if (len > static_cast<Real>(1e-10))
                        {
                            nx /= len; ny /= len; nz /= len;
                        }
                        if constexpr (N >= 6)
                        {
                            liftedVerts[v][3] = nx * normalScale;
                            liftedVerts[v][4] = ny * normalScale;
                            liftedVerts[v][5] = nz * normalScale;
                        }
                        for (size_t d = 6; d < N; ++d)
                        {
                            liftedVerts[v][d] = static_cast<Real>(0);
                        }
                    }
                }
                else
                {
                    // N=3: copy 3D positions directly
                    for (size_t v = 0; v < mSurfaceVertices.size(); ++v)
                    {
                        liftedVerts[v][0] = mSurfaceVertices[v][0];
                        liftedVerts[v][1] = mSurfaceVertices[v][1];
                        liftedVerts[v][2] = mSurfaceVertices[v][2];
                    }
                }

                // Step 2: Build Delaunay/NN over current seeds (same as LloydIterations)
                DelaunayNN<Real, N> delaunay(32);
                delaunay.SetVertices(numSeeds, mSites.data());

                // Convert mSites (std::vector<PointN>) to std::vector<std::array<Real,N>>
                // (PointN = Vector<N,Real>; std::array is needed by SurfaceRVDN)
                std::vector<std::array<Real, N>> seedsArr(numSeeds);
                std::vector<std::array<Real, N>> liftedArr(mSurfaceVertices.size());

                for (size_t s = 0; s < numSeeds; ++s)
                {
                    for (size_t d = 0; d < N; ++d)
                    {
                        seedsArr[s][d] = mSites[s][d];
                    }
                }
                for (size_t v = 0; v < mSurfaceVertices.size(); ++v)
                {
                    for (size_t d = 0; d < N; ++d)
                    {
                        liftedArr[v][d] = liftedVerts[v][d];
                    }
                }

                // Step 3: Run the full geometric multi-nerve RDT
                return ComputeMultiNerveRDT<Real, N>(
                    seedsArr, liftedArr, mSurfaceTriangles, delaunay,
                    outVertices, outTriangles);
            }
            else
            {
                // ------------------------------------------------------------
                // Simplified non-multinerve path: one vertex per seed placed
                // at the RVC area-weighted 3D centroid (on-surface position).
                // Connectivity from vertex-to-seed Voronoi assignment.
                // ------------------------------------------------------------

                // Vertex to nearest seed (3D distance)
                std::vector<int32_t> vertToSeed(mSurfaceVertices.size(), 0);
                for (size_t v = 0; v < mSurfaceVertices.size(); ++v)
                {
                    Real const px = mSurfaceVertices[v][0];
                    Real const py = mSurfaceVertices[v][1];
                    Real const pz = mSurfaceVertices[v][2];
                    Real minD = std::numeric_limits<Real>::max();
                    for (size_t s = 0; s < numSeeds; ++s)
                    {
                        Real dx = px - mSites[s][0];
                        Real dy = py - mSites[s][1];
                        Real dz = pz - mSites[s][2];
                        Real d2 = dx*dx + dy*dy + dz*dz;
                        if (d2 < minD) { minD = d2; vertToSeed[v] = static_cast<int32_t>(s); }
                    }
                }

                // RVC centroids and RDT triangle candidates
                std::vector<Point3> rvcPos(numSeeds, Point3{});
                std::vector<Real>   rvcArea(numSeeds, static_cast<Real>(0));

                using TriKey = std::array<int32_t, 3>;
                struct NormalAccum { Point3 normal{}; };
                std::map<TriKey, NormalAccum> candidates;

                for (auto const& tri : mSurfaceTriangles)
                {
                    int32_t s0 = vertToSeed[tri[0]];
                    int32_t s1 = vertToSeed[tri[1]];
                    int32_t s2 = vertToSeed[tri[2]];
                    Point3 const& va = mSurfaceVertices[tri[0]];
                    Point3 const& vb = mSurfaceVertices[tri[1]];
                    Point3 const& vc = mSurfaceVertices[tri[2]];
                    Real area = ComputeTriangleArea(va, vb, vc);
                    Point3 cen = (va + vb + vc) / static_cast<Real>(3);

                    // Assign triangle to centroid's nearest seed for RVC centroid
                    {
                        Real minD = std::numeric_limits<Real>::max(); int32_t sC = 0;
                        for (size_t s = 0; s < numSeeds; ++s)
                        {
                            Real dx = cen[0]-mSites[s][0], dy = cen[1]-mSites[s][1], dz = cen[2]-mSites[s][2];
                            Real d2 = dx*dx+dy*dy+dz*dz;
                            if (d2 < minD) { minD = d2; sC = static_cast<int32_t>(s); }
                        }
                        rvcPos[sC]  += cen * area;
                        rvcArea[sC] += area;
                    }

                    if (s0 == s1 || s1 == s2 || s0 == s2) { continue; }
                    TriKey key = {s0, s1, s2};
                    std::sort(key.begin(), key.end());
                    candidates[key].normal += Cross(vb - va, vc - va);
                }

                if (candidates.empty()) { return false; }

                outVertices.resize(numSeeds);
                for (size_t s = 0; s < numSeeds; ++s)
                {
                    if (rvcArea[s] > static_cast<Real>(1e-10))
                    {
                        outVertices[s] = rvcPos[s] / rvcArea[s];
                    }
                    else
                    {
                        outVertices[s] = {mSites[s][0], mSites[s][1], mSites[s][2]};
                    }
                }

                outTriangles.clear();
                outTriangles.reserve(candidates.size());
                for (auto const& kv : candidates)
                {
                    int32_t a = kv.first[0], b = kv.first[1], c = kv.first[2];
                    Point3 outN = Cross(outVertices[b]-outVertices[a], outVertices[c]-outVertices[a]);
                    if (Dot(outN, kv.second.normal) >= static_cast<Real>(0))
                    {
                        outTriangles.push_back({a, b, c});
                    }
                    else
                    {
                        outTriangles.push_back({a, c, b});
                    }
                }
                return !outTriangles.empty();
            }
        }

    private:
        // ── Simple static 3-D k-d tree (nearest-distance-squared only) ──────────
        //
        // Points are stored in "split order": for the sub-range [lo, hi) the
        // median element (mid = (lo+hi)/2) is the current node's split plane;
        // the left subtree covers [lo, mid) and the right subtree [mid+1, hi).
        // The same flat vectors are used for both building and searching — no
        // heap pointers are needed, so cache behaviour is good.
        struct KDTree3D
        {
            std::vector<Point3>  pts;  // points in split order
            std::vector<int32_t> ax;   // split axis (0/1/2) for each node

            bool empty() const { return pts.empty(); }

            // Build from a point set (src is taken by value; sorted in-place).
            void build(std::vector<Point3> src)
            {
                int32_t n = static_cast<int32_t>(src.size());
                pts.resize(n);
                ax.resize(n);
                buildRange(src, 0, n, 0);
            }

            // Nearest squared distance to q, capped at 'bound'.
            // Returns the true minimum if it is ≤ bound; may return any value
            // ≤ bound otherwise (early exit).  This is sufficient for the
            // "is this candidate beaten?" test in ComputeInitialSamplingFarthestPoint.
            Real nearestDistSqCapped(Point3 const& q, Real bound) const
            {
                Real best = bound;
                if (!pts.empty())
                {
                    searchRange(0, static_cast<int32_t>(pts.size()), 0, q, best);
                }
                return best;
            }

        private:
            void buildRange(std::vector<Point3>& src,
                            int32_t lo, int32_t hi, int32_t depth)
            {
                if (lo >= hi) return;
                int32_t mid = (lo + hi) / 2;
                int32_t a   = depth % 3;
                std::nth_element(
                    src.begin() + lo,
                    src.begin() + mid,
                    src.begin() + hi,
                    [a](Point3 const& u, Point3 const& v)
                    { return u[a] < v[a]; });
                pts[mid] = src[mid];
                ax [mid] = a;
                buildRange(src, lo,     mid,    depth + 1);
                buildRange(src, mid+1,  hi,     depth + 1);
            }

            void searchRange(int32_t lo, int32_t hi, int32_t depth,
                             Point3 const& q, Real& best) const
            {
                if (lo >= hi) return;
                int32_t mid = (lo + hi) / 2;
                Real dx = q[0] - pts[mid][0];
                Real dy = q[1] - pts[mid][1];
                Real dz = q[2] - pts[mid][2];
                Real d2 = dx*dx + dy*dy + dz*dz;
                if (d2 < best) best = d2;

                int32_t a    = ax[mid];
                Real    diff = q[a] - pts[mid][a];
                if (diff <= static_cast<Real>(0))
                {
                    searchRange(lo,    mid,    depth+1, q, best);
                    if (diff*diff < best)
                        searchRange(mid+1, hi, depth+1, q, best);
                }
                else
                {
                    searchRange(mid+1, hi,     depth+1, q, best);
                    if (diff*diff < best)
                        searchRange(lo, mid,   depth+1, q, best);
                }
            }
        };
        // ── End KDTree3D ─────────────────────────────────────────────────────────

        // Find nearest site to a 3D point
        int32_t FindNearestSite(Point3 const& point3D) const
        {
            if (mSites.empty())
            {
                return -1;
            }
            
            PointN queryN;
            queryN[0] = point3D[0];
            queryN[1] = point3D[1];
            queryN[2] = point3D[2];
            for (size_t d = 3; d < N; ++d)
            {
                queryN[d] = static_cast<Real>(0);
            }
            
            int32_t nearest = 0;
            Real minDist = Distance(queryN, mSites[0]);
            
            for (size_t i = 1; i < mSites.size(); ++i)
            {
                Real dist = Distance(queryN, mSites[i]);
                if (dist < minDist)
                {
                    minDist = dist;
                    nearest = static_cast<int32_t>(i);
                }
            }
            
            return nearest;
        }
        
        // Compute N-dimensional distance
        static Real Distance(PointN const& p0, PointN const& p1)
        {
            Real sumSq = static_cast<Real>(0);
            for (size_t i = 0; i < N; ++i)
            {
                Real diff = p1[i] - p0[i];
                sumSq += diff * diff;
            }
            return std::sqrt(sumSq);
        }
        
        // Compute area of 3D triangle
        static Real ComputeTriangleArea(
            Point3 const& v0,
            Point3 const& v1,
            Point3 const& v2)
        {
            Point3 edge1 = v1 - v0;
            Point3 edge2 = v2 - v0;
            Point3 cross = Cross(edge1, edge2);
            return Length(cross) * static_cast<Real>(0.5);
        }
        
    private:
        std::vector<Point3> mSurfaceVertices;                    // 3D mesh vertices
        std::vector<std::array<int32_t, 3>> mSurfaceTriangles;   // Triangle indices
        std::vector<PointN> mSites;                              // N-dimensional sites
        Real mConvergenceThreshold;                               // Convergence criterion
        bool mVerbose;                                            // Output progress
        double mTimeLimitSeconds;                                 // 0 = no limit
        size_t mIterationsCompleted;                              // Iters completed in last LloydIterations() call
    };
}
