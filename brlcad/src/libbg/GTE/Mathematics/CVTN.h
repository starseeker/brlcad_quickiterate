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
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <thread>
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
            , mNormalScale(static_cast<Real>(0))
            , mLiftedArrValid(false)
            , mRVDMeshInitDone(false)
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
            mLiftedArrValid = false;   // invalidate cached lifted vertices
            mRVDMeshInitDone = false;  // invalidate cached RVD mesh setup
            
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
                // ── Steps 1–4: compute area-weighted N-D centroids via walk ───
                // Uses BuildLiftedVertices + AccumulateCentroids helpers to
                // eliminate duplication with NewtonIterations.
                // checkSR=false: Lloyd uses initial neighborhood only, no SR
                // enlargement — matching Geogram's set_check_SR(false) call
                // before Lloyd_iterations().
                std::vector<std::array<Real, N>> mg;
                std::vector<Real> m;
                if (!AccumulateCentroids(mg, m, false))
                {
                    return false;
                }

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
        
        // Newton iterations using L-BFGS optimization.
        //
        // Direct translation of Geogram's CVT::Newton_iterations() which calls
        //   HLBFGS optimizer with funcgrad_CB = RVD_->compute_CVT_func_grad.
        //
        // The CVT energy and gradient are:
        //   E       = sum_i integral_{V_i} ||x - p_i||^2 dA
        //   ∇_{p_i}E = 2 * m_i * (p_i - c_i)       (m_i = area(V_i), c_i = centroid)
        //
        // Both are computed via SurfaceRVDN::ForEachPolygon (same walk as Lloyd).
        // The energy proxy uses f = sum_i m_i * ||p_i - c_i||^2 which has the
        // same gradient as E and is zero at the optimum.
        //
        // m  = number of L-BFGS history pairs (7 matches Geogram's Newton_m default)
        bool NewtonIterations(size_t numIterations, size_t histSize = 7)
        {
            if (mSites.empty())
            {
                return false;
            }

            size_t numSeeds = mSites.size();
            size_t totalVars = numSeeds * N;

            // ── Helpers to flatten / unflatten site positions ──────────────────
            std::vector<Real> x(totalVars);
            auto flattenSites = [&]()
            {
                for (size_t i = 0; i < numSeeds; ++i)
                    for (size_t d = 0; d < N; ++d)
                        x[i * N + d] = mSites[i][d];
            };
            auto unflattenX = [&](std::vector<Real> const& xv)
            {
                for (size_t i = 0; i < numSeeds; ++i)
                    for (size_t d = 0; d < N; ++d)
                        mSites[i][d] = xv[i * N + d];
            };

            // ── Compute CVT gradient and energy proxy via AccumulateCentroids ──
            // gradient g[i*N+d] = 2 * m_i * (p_i^d - c_i^d)
            // energy   f        = sum_i m_i * ||p_i - c_i||^2
            // Both are zero at the CVT optimum (seed positions = centroids).
            // checkSR=true: Newton uses full SR-based neighborhood enlargement,
            // matching Geogram's set_check_SR(true) call before Newton_iterations().
            auto computeGradient = [&](std::vector<Real>& gOut, Real& fOut) -> bool
            {
                std::vector<std::array<Real, N>> mg;
                std::vector<Real> m_area;
                if (!AccumulateCentroids(mg, m_area, true))
                    return false;

                gOut.assign(totalVars, static_cast<Real>(0));
                fOut = static_cast<Real>(0);
                for (size_t s = 0; s < numSeeds; ++s)
                {
                    if (m_area[s] > static_cast<Real>(1e-30))
                    {
                        Real inv_m = static_cast<Real>(1) / m_area[s];
                        for (size_t d = 0; d < N; ++d)
                        {
                            Real c_d    = mg[s][d] * inv_m;
                            Real p_d    = mSites[s][d];
                            Real diff   = p_d - c_d;
                            gOut[s*N+d] = Real(2) * m_area[s] * diff;
                            fOut       += m_area[s] * diff * diff;
                        }
                    }
                }
                return true;
            };

            // ── L-BFGS main loop ───────────────────────────────────────────────
            // Direct translation of the HLBFGS algorithm used by Geogram's
            // Optimizer::create("HLBFGS") backend.
            //
            // Two-loop recursion (Nocedal 1980):
            //   q = g
            //   for i = k-1 downto k-m:  α_i = ρ_i*(s_i·q); q -= α_i*y_i
            //   r = γ_k * q   (γ_k = (s_{k-1}·y_{k-1})/(y_{k-1}·y_{k-1}))
            //   for i = k-m upto k-1:  β_i = ρ_i*(y_i·r); r += s_i*(α_i-β_i)
            //   direction d = -r
            //
            // Line search: backtracking Armijo (sufficient decrease condition).
            //   c1 = 1e-4 (standard value matching Geogram's HLBFGS)
            flattenSites();

            // L-BFGS history
            using VecN = std::vector<Real>;
            std::vector<VecN> s_hist, y_hist;
            std::vector<Real> rho_hist;

            VecN g(totalVars);
            Real f;
            if (!computeGradient(g, f))
            {
                return false;
            }

            for (size_t iter = 0; iter < numIterations; ++iter)
            {
                // Check gradient convergence
                Real gNorm = static_cast<Real>(0);
                for (Real gi : g) gNorm += gi * gi;
                gNorm = std::sqrt(gNorm);
                if (gNorm < mConvergenceThreshold * static_cast<Real>(totalVars))
                {
                    if (mVerbose)
                        std::cout << "Newton converged after " << iter << " iters\n";
                    break;
                }

                // ── Two-loop L-BFGS direction ──────────────────────────────
                VecN q = g;
                size_t hs = s_hist.size();
                VecN alpha_v(hs, Real(0));

                for (int i = static_cast<int>(hs) - 1; i >= 0; --i)
                {
                    Real si = Real(0);
                    for (size_t j = 0; j < totalVars; ++j) si += s_hist[i][j] * q[j];
                    alpha_v[i] = rho_hist[i] * si;
                    for (size_t j = 0; j < totalVars; ++j) q[j] -= alpha_v[i] * y_hist[i][j];
                }

                VecN r = q;
                if (hs > 0)
                {
                    Real sy = Real(0), yy = Real(0);
                    for (size_t j = 0; j < totalVars; ++j)
                    {
                        sy += s_hist.back()[j] * y_hist.back()[j];
                        yy += y_hist.back()[j] * y_hist.back()[j];
                    }
                    Real gamma = (yy > Real(1e-30)) ? sy / yy : Real(1);
                    for (size_t j = 0; j < totalVars; ++j) r[j] *= gamma;
                }

                for (size_t i = 0; i < hs; ++i)
                {
                    Real beta = Real(0);
                    for (size_t j = 0; j < totalVars; ++j) beta += rho_hist[i] * y_hist[i][j] * r[j];
                    for (size_t j = 0; j < totalVars; ++j) r[j] += s_hist[i][j] * (alpha_v[i] - beta);
                }

                VecN d(totalVars);
                for (size_t j = 0; j < totalVars; ++j) d[j] = -r[j];

                // Check d is a descent direction
                Real dotGD = Real(0);
                for (size_t j = 0; j < totalVars; ++j) dotGD += g[j] * d[j];
                if (dotGD >= Real(0))
                {
                    // Reset to gradient descent
                    s_hist.clear(); y_hist.clear(); rho_hist.clear();
                    for (size_t j = 0; j < totalVars; ++j) d[j] = -g[j];
                    dotGD = -gNorm * gNorm;
                }

                // ── Armijo backtracking line search ────────────────────────
                static constexpr Real c1 = static_cast<Real>(1e-4);
                Real alpha = Real(1);
                VecN x_new(totalVars), g_new(totalVars);
                Real f_new;
                bool lineOk = false;

                for (int ls = 0; ls < 16; ++ls)
                {
                    for (size_t j = 0; j < totalVars; ++j) x_new[j] = x[j] + alpha * d[j];
                    unflattenX(x_new);
                    if (computeGradient(g_new, f_new) && f_new <= f + c1 * alpha * dotGD)
                    {
                        lineOk = true;
                        break;
                    }
                    alpha *= Real(0.5);
                }

                if (!lineOk)
                {
                    unflattenX(x); // restore
                    break;
                }

                // ── Update L-BFGS history ──────────────────────────────────
                VecN s_k(totalVars), y_k(totalVars);
                for (size_t j = 0; j < totalVars; ++j)
                {
                    s_k[j] = x_new[j] - x[j];
                    y_k[j] = g_new[j] - g[j];
                }
                Real sy = Real(0);
                for (size_t j = 0; j < totalVars; ++j) sy += s_k[j] * y_k[j];

                if (sy > Real(1e-30))
                {
                    s_hist.push_back(std::move(s_k));
                    y_hist.push_back(std::move(y_k));
                    rho_hist.push_back(Real(1) / sy);
                    if (s_hist.size() > histSize)
                    {
                        s_hist.erase(s_hist.begin());
                        y_hist.erase(y_hist.begin());
                        rho_hist.erase(rho_hist.begin());
                    }
                }

                x = std::move(x_new);
                g = std::move(g_new);

                // Energy-change convergence: stop when the improvement is tiny
                // compared to the current energy value.  This prevents spending
                // many L-BFGS iterations once the CVT has effectively converged,
                // matching the spirit of Geogram's HLBFGS convergence criterion.
                // Threshold 1e-6 relative to f matches Geogram's default tolerance.
                static constexpr Real ENERGY_REL_TOL = static_cast<Real>(1e-6);
                Real fChange = std::abs(f_new - f);
                Real fBase   = std::abs(f) + static_cast<Real>(1e-30);
                f = f_new;
                if (fChange / fBase < ENERGY_REL_TOL)
                {
                    if (mVerbose)
                        std::cout << "Newton energy-converged after " << (iter + 1) << " iters\n";
                    break;
                }

                if (mVerbose)
                {
                    std::cout << "Newton iter " << (iter + 1)
                              << ": f=" << f << " |g|=" << gNorm << "\n";
                }
            }

            return true;
        }
        
        // Set the fixed normal-component scale to use in BuildLiftedVertices
        // and ComputeRDT.  Call this after SetSites() in the anisotropic
        // (N>3) path so that the 6-D metric remains consistent across all
        // Lloyd and Newton iterations.  A value of 0 (default) causes
        // BuildLiftedVertices to re-derive the scale from the current seed
        // normal magnitudes — correct for the initial call but may drift
        // as seeds move during optimisation.  Matching Geogram's fixed
        // set_anisotropy() scale.
        //
        // Passing 0 resets to the dynamic-derivation behaviour (the default
        // when CVTN is first constructed), which may be useful in custom
        // workflows that compute the scale incrementally.
        void SetNormalScale(Real s)
        {
            if (s != mNormalScale)
            {
                mNormalScale = s;
                mLiftedArrValid = false;  // invalidate cached lifted vertices
            }
        }

        Real GetNormalScale() const { return mNormalScale; }

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

                // Normal scale: use fixed mNormalScale if set (set once before
                // the CVT loop by SetNormalScale()), otherwise derive from seeds.
                // Matching Geogram's set_anisotropy() which pins the scale once.
                Real normalScale = static_cast<Real>(0);
                if constexpr (N > 3)
                {
                    if (mNormalScale > static_cast<Real>(0))
                    {
                        normalScale = mNormalScale;
                    }
                    else
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

        // ── Shared helper: build lifted N-D vertices + seed array ─────────────
        //
        // Used by LloydIterations and NewtonIterations::computeGradient.
        // For N=3 (isotropic): identity — first 3 dims = 3D position.
        // For N=6 (anisotropic): appends scaled vertex normals to produce the
        //   N-D metric embedding matching Geogram's set_anisotropy().
        //
        // The liftedArr result is cached in mCachedLiftedArr because the mesh
        // vertices and normalScale never change between Lloyd/Newton iterations.
        // Only seedsArr changes (seeds move each iteration).
        void BuildLiftedVertices(
            std::vector<std::array<Real, N>>& liftedArr,
            std::vector<std::array<Real, N>>& seedsArr,
            Real& normalScale) const
        {
            size_t numSeeds = mSites.size();

            seedsArr.resize(numSeeds);
            for (size_t s = 0; s < numSeeds; ++s)
                for (size_t d = 0; d < N; ++d) seedsArr[s][d] = mSites[s][d];

            // Normal scale: use the fixed mNormalScale if set (preferred),
            // otherwise derive from current seed normal-component magnitudes.
            // Geogram pins this scale once via set_anisotropy() so that the
            // 6-D metric remains consistent across all Lloyd/Newton iterations.
            normalScale = static_cast<Real>(0);
            if constexpr (N > 3)
            {
                if (mNormalScale > static_cast<Real>(0))
                {
                    normalScale = mNormalScale;
                }
                else
                {
                    for (size_t s = 0; s < numSeeds; ++s)
                    {
                        Real ns = static_cast<Real>(0);
                        for (size_t d = 3; d < N; ++d) ns += mSites[s][d] * mSites[s][d];
                        normalScale += std::sqrt(ns);
                    }
                    if (numSeeds > 0) normalScale /= static_cast<Real>(numSeeds);
                }
            }

            // Use the cached liftedArr when valid (normalScale and mesh haven't changed).
            // Computing lifted vertices from scratch is O(n_verts) but for large meshes
            // (86K verts, N=6) it takes ~12ms per call and never changes between iterations.
            if (mLiftedArrValid)
            {
                liftedArr = mCachedLiftedArr;
                return;
            }

            // Build the lifted array from scratch.
            mCachedLiftedArr.resize(mSurfaceVertices.size());

            if constexpr (N > 3)
            {
                // Area-weighted vertex normals (same as Geogram's compute_normals)
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
                    mCachedLiftedArr[v][0] = mSurfaceVertices[v][0];
                    mCachedLiftedArr[v][1] = mSurfaceVertices[v][1];
                    mCachedLiftedArr[v][2] = mSurfaceVertices[v][2];
                    Real nx = vertNorm[v][0], ny = vertNorm[v][1], nz = vertNorm[v][2];
                    Real len = std::sqrt(nx*nx + ny*ny + nz*nz);
                    if (len > static_cast<Real>(1e-10))
                    { nx /= len; ny /= len; nz /= len; }
                    if constexpr (N >= 6)
                    {
                        mCachedLiftedArr[v][3] = nx * normalScale;
                        mCachedLiftedArr[v][4] = ny * normalScale;
                        mCachedLiftedArr[v][5] = nz * normalScale;
                    }
                    for (size_t d = 6; d < N; ++d) mCachedLiftedArr[v][d] = static_cast<Real>(0);
                }
            }
            else
            {
                // N=3: copy 3D positions directly
                for (size_t v = 0; v < mSurfaceVertices.size(); ++v)
                {
                    mCachedLiftedArr[v][0] = mSurfaceVertices[v][0];
                    mCachedLiftedArr[v][1] = mSurfaceVertices[v][1];
                    mCachedLiftedArr[v][2] = mSurfaceVertices[v][2];
                }
            }

            mLiftedArrValid = true;
            liftedArr = mCachedLiftedArr;
        }

        // ── Shared helper: accumulate N-D centroids via SurfaceRVDN walk ──────
        //
        // Uses C++17 std::thread to parallelize across hardware threads.
        // The facet range [0, numFacets) is split into nThreads contiguous
        // sub-ranges.  Each thread runs its own ForEachPolygon_SeedsPriority
        // on its sub-range with its own local state (facet_is_marked, seed_stamp)
        // and its own partial mg/m_area accumulator.  Partial results are merged
        // after all threads complete.
        //
        // Correctness: every facet in [0, numFacets) is processed by exactly
        // the thread that owns it.  Within-range BFS stays in the range;
        // facets outside the range are handled by their respective thread's
        // outer loop via FindNearestSeed().  This matches Geogram's parallel
        // for_each_triangle pattern (each thread owns a contiguous tile).
        //
        // checkSR: when false (Lloyd mode), ClipCellFacet uses initial
        //   neighborhood only (no enlargement) — matching Geogram's
        //   RVD_->set_check_SR(false) call before Lloyd_iterations().
        //   When true (Newton / compute_surface), full SR enlargement.
        //
        // Fills mg[s][d] = sum(area * centroid[d]) and m_area[s] = sum(area)
        // for each seed s.  Returns false if the walk produces no polygons.
        bool AccumulateCentroids(
            std::vector<std::array<Real, N>>& mg,
            std::vector<Real>&               m_area,
            bool                             checkSR = true) const
        {
            size_t numSeeds  = mSites.size();
            size_t numFacets = mSurfaceTriangles.size();

            // Build seedsArr from current site positions.
            // Rebuilt each iteration as seeds move.
            std::vector<std::array<Real, N>> seedsArr(numSeeds);
            for (size_t s = 0; s < numSeeds; ++s)
                for (size_t d = 0; d < N; ++d) seedsArr[s][d] = mSites[s][d];

            // Populate lifted-vertex cache if needed.
            // mCachedLiftedArr is stable (member of CVTN), valid for lifetime
            // of this CVTN object.
            if (!mLiftedArrValid)
            {
                std::vector<std::array<Real, N>> dummy, dummySeeds;
                Real dummyNS;
                BuildLiftedVertices(dummy, dummySeeds, dummyNS);
            }

            // Build ONE shared DelaunayNN in the main thread — matches
            // Geogram's Delaunay_NearestNeighbors::set_vertices() which calls
            // NN_->set_points() then update_neighbors() (parallel_for over all
            // vertices).  K=30 matches Geogram's default_nb_neighbors_=30.
            //
            // SetVertices eagerly precomputes ALL K=30 neighbourhoods in
            // parallel, so worker threads only do read-only GetNeighbors() and
            // thread-safe (per-vertex spinlock) EnlargeNeighborhood() calls.
            DelaunayNN<Real, N> delaunay(30);
            delaunay.SetVertices(numSeeds, mSites.data());

            // Set up the cached SurfaceRVDN (adjacency built once per mesh).
            mCachedRVD.SetCheckSR(checkSR);
            if (!mRVDMeshInitDone)
            {
                mCachedRVD.InitMeshOnly(mCachedLiftedArr, mSurfaceTriangles);
                mRVDMeshInitDone = true;
            }
            else
            {
                mCachedRVD.SetLiftedVerts(mCachedLiftedArr);
            }
            // mCachedRVD.UpdateSeeds is intentionally omitted: threads call
            // rvd_t.UpdateSeeds() themselves with the shared delaunay below.

            // Determine thread count: use hardware concurrency, capped at
            // numFacets (no point in more threads than facets).
            unsigned int hwThreads = std::thread::hardware_concurrency();
            // hardware_concurrency() returns 0 when the value is not computable.
            if (hwThreads == 0) hwThreads = 1;
            size_t nThreads = static_cast<size_t>(hwThreads);
            if (nThreads > numFacets) nThreads = numFacets;
            if (nThreads < 1) nThreads = 1;

            // Lambda for computing the area-weighted centroid contribution from
            // a single RVD polygon — shared by all per-thread callbacks.
            auto accumPoly = [&](
                int32_t seed,
                RVDPolygon<Real, N> const& P,
                std::vector<std::array<Real, N>>& mg_local,
                std::vector<Real>& m_area_local)
            {
                const size_t nv = P.nb_vertices();
                for (size_t i = 1; i + 1 < nv; ++i)
                {
                    Real ea = Real(0), eb = Real(0), ec = Real(0);
                    for (size_t d = 0; d < N; ++d)
                    {
                        Real e0 = P.V[0].pos[d] - P.V[i].pos[d];
                        Real e1 = P.V[i].pos[d] - P.V[i+1].pos[d];
                        Real e2 = P.V[i+1].pos[d] - P.V[0].pos[d];
                        ea += e0*e0; eb += e1*e1; ec += e2*e2;
                    }
                    ea = std::sqrt(ea); eb = std::sqrt(eb); ec = std::sqrt(ec);
                    Real hs   = Real(0.5)*(ea+eb+ec);
                    Real A2   = hs*(hs-ea)*(hs-eb)*(hs-ec);
                    Real area = std::sqrt(std::max(A2, Real(0)));
                    Real inv3 = area / Real(3);
                    for (size_t d = 0; d < N; ++d)
                        mg_local[seed][d] += inv3*(P.V[0].pos[d]+P.V[i].pos[d]+P.V[i+1].pos[d]);
                    m_area_local[seed] += area;
                }
            };

            // Per-thread partial accumulators and thread objects.
            // Each thread has its own SurfaceRVDN (per-thread mSeedKDTree and
            // mFacetAdj copy) but shares the single pre-built delaunay — no
            // per-thread KD-tree rebuild, matching Geogram's architecture.
            std::vector<std::vector<std::array<Real, N>>> mg_parts(nThreads);
            std::vector<std::vector<Real>>                 ma_parts(nThreads);
            std::vector<std::thread>                       threads;
            threads.reserve(nThreads);

            size_t facetsPerThread = (numFacets + nThreads - 1) / nThreads;

            for (size_t t = 0; t < nThreads; ++t)
            {
                int32_t fBegin = static_cast<int32_t>(t * facetsPerThread);
                int32_t fEnd   = static_cast<int32_t>(
                    std::min((t + 1) * facetsPerThread, numFacets));

                mg_parts[t].assign(numSeeds, {});
                for (auto& a : mg_parts[t]) a.fill(static_cast<Real>(0));
                ma_parts[t].assign(numSeeds, static_cast<Real>(0));

                threads.emplace_back([&, t, fBegin, fEnd]()
                {
                    // Each thread gets its own SurfaceRVDN for its per-thread
                    // mSeedKDTree and local BFS state, but shares the single
                    // main-thread delaunay — neighbourhoods are pre-built
                    // (GetNeighbors is a pure read) and EnlargeNeighborhood
                    // uses per-vertex spinlocks, so concurrent access is safe.
                    SurfaceRVDN<Real, N> rvd_t;
                    rvd_t.SetCheckSR(checkSR);
                    rvd_t.ShareMeshFrom(mCachedRVD);
                    rvd_t.UpdateSeeds(seedsArr, delaunay);

                    auto& mg_t  = mg_parts[t];
                    auto& ma_t  = ma_parts[t];

                    rvd_t.ForEachPolygon_SeedsPriority(
                        [&](int32_t seed, int32_t /*facet*/,
                            RVDPolygon<Real, N> const& P)
                        {
                            accumPoly(seed, P, mg_t, ma_t);
                        },
                        fBegin, fEnd);
                });
            }

            for (auto& th : threads) th.join();

            // Merge partial accumulators.
            mg.assign(numSeeds, {});
            for (auto& a : mg) a.fill(static_cast<Real>(0));
            m_area.assign(numSeeds, static_cast<Real>(0));
            for (size_t t = 0; t < nThreads; ++t)
            {
                for (size_t s = 0; s < numSeeds; ++s)
                {
                    for (size_t d = 0; d < N; ++d)
                        mg[s][d] += mg_parts[t][s][d];
                    m_area[s] += ma_parts[t][s];
                }
            }

            return true;
        }


        std::vector<Point3> mSurfaceVertices;                    // 3D mesh vertices
        std::vector<std::array<int32_t, 3>> mSurfaceTriangles;   // Triangle indices
        std::vector<PointN> mSites;                              // N-dimensional sites
        Real mConvergenceThreshold;                               // Convergence criterion
        bool mVerbose;                                            // Output progress
        double mTimeLimitSeconds;                                 // 0 = no limit
        size_t mIterationsCompleted;                              // Iters completed in last LloydIterations() call
        Real mNormalScale;  // Fixed normal-component scale (0 = derive from seeds each call)

        // Cached lifted mesh vertices (position + scaled normals in N-D).
        // Computed once from mSurfaceVertices + mSurfaceTriangles + mNormalScale
        // and reused across all AccumulateCentroids calls.  Invalidated when
        // mNormalScale changes (via SetNormalScale) or when Initialize is called.
        mutable std::vector<std::array<Real, N>> mCachedLiftedArr;
        mutable bool mLiftedArrValid = false;

        // Cached SurfaceRVDN with the mesh adjacency table built once.
        // The mesh adjacency (mFacetAdj) depends only on mSurfaceTriangles and
        // never changes between iterations.  The seed KD-tree is updated via
        // UpdateSeeds() on each AccumulateCentroids call (O(n_seeds log n_seeds)
        // instead of O(n_faces) for full Initialize).
        mutable SurfaceRVDN<Real, N> mCachedRVD;
        mutable bool mRVDMeshInitDone = false;
    };
}
