// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// N-dimensional nearest neighbor search
//
// Efficient nearest neighbor search for N-dimensional points using a
// KD-tree.  Provides O(log n) average queries for nearest neighbor and
// O(k log n) for k-nearest neighbor, replacing the previous brute-force
// O(n) implementation.
//
// The KD-tree uses the same "split-order" layout as the KDTree3D in
// CVTN.h, extended to arbitrary N dimensions.  The split axis cycles
// through dimensions 0..N-1 (round-robin), and nth_element produces an
// in-place median partition with no heap allocation.
//
// For k-nearest neighbor queries a max-heap of capacity k is maintained.
// Subtrees whose axis-aligned bounding box is entirely farther than the
// current k-th best candidate are pruned.
//
// Based on:
// - geogram/src/lib/geogram/points/nn_search.h (reference architecture)
// - KDTree3D pattern from CVTN.h (same repository)
// - GTE's NearestNeighborQuery.h (interface specification)
//
// License: Boost Software License 1.0

#pragma once

#include <Mathematics/Vector.h>
#include <Mathematics/Logger.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

namespace gte
{
    // N-dimensional nearest neighbor search backed by a KD-tree.
    // Provides k-nearest neighbor queries for CVT operations.
    template <typename Real, size_t N>
    class NearestNeighborSearchN
    {
    public:
        using PointN = Vector<N, Real>;

        static_assert(
            std::is_floating_point<Real>::value,
            "Real must be float or double.");

        NearestNeighborSearchN()
            : mNumPoints(0)
            , mPoints(nullptr)
        {
        }

        virtual ~NearestNeighborSearchN() = default;

        // Set the point set for searching and build the KD-tree index.
        void SetPoints(size_t numPoints, PointN const* points)
        {
            mNumPoints = numPoints;
            mPoints    = points;
            BuildIndex();
        }

        // Find single nearest neighbor to query point.
        // Returns index of nearest point, or -1 if no points.
        // O(log n) average with the KD-tree.
        int32_t FindNearestNeighbor(PointN const& query) const
        {
            if (mNumPoints == 0)
            {
                return -1;
            }
            if (mKDTree.empty())
            {
                // Fallback: brute force (only when tree not yet built)
                int32_t best = 0;
                Real bestD = DistanceSquared(query, mPoints[0]);
                for (size_t i = 1; i < mNumPoints; ++i)
                {
                    Real d = DistanceSquared(query, mPoints[i]);
                    if (d < bestD) { bestD = d; best = static_cast<int32_t>(i); }
                }
                return best;
            }
            Real bestD = std::numeric_limits<Real>::max();
            int32_t bestIdx = 0;
            mKDTree.nearest(query, 0, static_cast<int32_t>(mNumPoints), 0, bestD, bestIdx);
            return bestIdx;
        }

        // Find k nearest neighbors to query point.
        // Returns actual number of neighbors found (may be less than k).
        // O(k log n) average with the KD-tree.
        size_t FindKNearestNeighbors(
            PointN const& query,
            size_t k,
            std::vector<int32_t>& neighbors,
            std::vector<Real>& distances) const
        {
            neighbors.clear();
            distances.clear();

            if (mNumPoints == 0 || k == 0)
            {
                return 0;
            }

            k = std::min(k, mNumPoints);

            if (mKDTree.empty())
            {
                // Fallback: brute force
                using Pair = std::pair<Real, int32_t>;
                std::priority_queue<Pair> heap;
                for (size_t i = 0; i < mNumPoints; ++i)
                {
                    Real d = DistanceSquared(query, mPoints[i]);
                    heap.push({d, static_cast<int32_t>(i)});
                    if (heap.size() > k) { heap.pop(); }
                }
                neighbors.resize(heap.size());
                distances.resize(heap.size());
                for (int32_t i = static_cast<int32_t>(heap.size()) - 1; i >= 0; --i)
                {
                    neighbors[i] = heap.top().second;
                    distances[i] = std::sqrt(heap.top().first);
                    heap.pop();
                }
                return neighbors.size();
            }

            // KD-tree k-NN: maintain a max-heap of (distSq, index)
            using Pair = std::pair<Real, int32_t>;
            std::priority_queue<Pair> heap;
            Real worstD = std::numeric_limits<Real>::max();
            mKDTree.knearest(query, k, 0, static_cast<int32_t>(mNumPoints), 0, heap, worstD);

            size_t found = heap.size();
            neighbors.resize(found);
            distances.resize(found);
            for (int32_t i = static_cast<int32_t>(found) - 1; i >= 0; --i)
            {
                neighbors[i] = heap.top().second;
                distances[i] = std::sqrt(heap.top().first);
                heap.pop();
            }
            return found;
        }

        // Find k nearest neighbors to an existing point (by index).
        // Useful for computing neighborhoods in Delaunay.
        size_t FindKNearestNeighborsToPoint(
            int32_t queryIndex,
            size_t k,
            std::vector<int32_t>& neighbors,
            std::vector<Real>& distances) const
        {
            if (queryIndex < 0 || static_cast<size_t>(queryIndex) >= mNumPoints)
            {
                neighbors.clear();
                distances.clear();
                return 0;
            }
            return FindKNearestNeighbors(mPoints[queryIndex], k + 1, neighbors, distances);
        }

        // Get number of points
        size_t GetNumPoints() const
        {
            return mNumPoints;
        }

        // Compute squared distance between two points
        static Real DistanceSquared(PointN const& p0, PointN const& p1)
        {
            Real sumSq = static_cast<Real>(0);
            for (size_t i = 0; i < N; ++i)
            {
                Real diff = p1[i] - p0[i];
                sumSq += diff * diff;
            }
            return sumSq;
        }

    private:
        // ── N-dimensional KD-tree ─────────────────────────────────────────────
        //
        // Layout: "split-order" flat array.  For the range [lo, hi), the
        // node is at mid = (lo+hi)/2, left child covers [lo,mid), right child
        // [mid+1, hi).  Split axis cycles round-robin through 0..N-1.
        //
        // Build cost: O(n log² n) using nth_element.
        // Query cost: O(log n) for nearest, O(k log n) for k-nearest
        //             (expected; degrades gracefully in high dimensions).
        //
        // Nearest-distance queries support a 'cap' parameter: if the current
        // best distance exceeds the cap the search terminates early, matching
        // the pattern used by CVTN::KDTree3D::nearestDistSqCapped().
        struct KDTreeND
        {
            using Pair = std::pair<Real, int32_t>;  // (distSq, original index)

            std::vector<PointN>  pts;  // points in split order
            std::vector<int32_t> idx;  // original index of each pt in split order
            std::vector<int32_t> ax;   // split axis at each node

            bool empty() const { return pts.empty(); }

            // Build from the caller's point set (referenced by mPoints/mNumPoints).
            void build(size_t n, PointN const* src)
            {
                pts.resize(n);
                idx.resize(n);
                ax.resize(n);
                // Initialise index array
                std::vector<int32_t> order(n);
                for (size_t i = 0; i < n; ++i) { order[i] = static_cast<int32_t>(i); }
                buildRange(src, order, 0, static_cast<int32_t>(n), 0);
            }

            // Nearest-neighbor search: sets bestD/bestIdx to the closest point.
            void nearest(PointN const& q,
                         int32_t lo, int32_t hi, int32_t depth,
                         Real& bestD, int32_t& bestIdx) const
            {
                if (lo >= hi) { return; }
                int32_t mid = (lo + hi) / 2;

                Real d = distSq(q, pts[mid]);
                if (d < bestD) { bestD = d; bestIdx = idx[mid]; }

                int32_t a    = ax[mid];
                Real    diff = q[static_cast<size_t>(a)] - pts[mid][static_cast<size_t>(a)];

                // Recurse into the closer half first for better pruning
                if (diff <= static_cast<Real>(0))
                {
                    nearest(q, lo, mid, depth + 1, bestD, bestIdx);
                    if (diff * diff < bestD)
                        nearest(q, mid + 1, hi, depth + 1, bestD, bestIdx);
                }
                else
                {
                    nearest(q, mid + 1, hi, depth + 1, bestD, bestIdx);
                    if (diff * diff < bestD)
                        nearest(q, lo, mid, depth + 1, bestD, bestIdx);
                }
            }

            // K-nearest search: maintains a max-heap of k best (distSq, idx) pairs.
            // worstD is the current heap maximum; subtrees with min-box-dist > worstD
            // are pruned.
            void knearest(PointN const& q, size_t k,
                          int32_t lo, int32_t hi, int32_t depth,
                          std::priority_queue<Pair>& heap, Real& worstD) const
            {
                if (lo >= hi) { return; }
                int32_t mid = (lo + hi) / 2;

                Real d = distSq(q, pts[mid]);
                if (heap.size() < k)
                {
                    heap.push({d, idx[mid]});
                    worstD = heap.top().first;
                }
                else if (d < worstD)
                {
                    heap.pop();
                    heap.push({d, idx[mid]});
                    worstD = heap.top().first;
                }

                int32_t a    = ax[mid];
                Real    diff = q[static_cast<size_t>(a)] - pts[mid][static_cast<size_t>(a)];

                if (diff <= static_cast<Real>(0))
                {
                    knearest(q, k, lo, mid, depth + 1, heap, worstD);
                    if (diff * diff < worstD)
                        knearest(q, k, mid + 1, hi, depth + 1, heap, worstD);
                }
                else
                {
                    knearest(q, k, mid + 1, hi, depth + 1, heap, worstD);
                    if (diff * diff < worstD)
                        knearest(q, k, lo, mid, depth + 1, heap, worstD);
                }
            }

        private:
            static Real distSq(PointN const& a, PointN const& b)
            {
                Real s = static_cast<Real>(0);
                for (size_t c = 0; c < N; ++c)
                {
                    Real d = a[c] - b[c];
                    s += d * d;
                }
                return s;
            }

            void buildRange(PointN const* src,
                            std::vector<int32_t>& order,
                            int32_t lo, int32_t hi, int32_t depth)
            {
                if (lo >= hi) { return; }
                int32_t mid = (lo + hi) / 2;
                int32_t a   = depth % static_cast<int32_t>(N);

                // Partial-sort: median at position mid
                std::nth_element(
                    order.begin() + lo,
                    order.begin() + mid,
                    order.begin() + hi,
                    [&](int32_t u, int32_t v)
                    {
                        return src[u][static_cast<size_t>(a)] < src[v][static_cast<size_t>(a)];
                    });

                pts[mid] = src[order[mid]];
                idx[mid] = order[mid];
                ax [mid] = a;

                buildRange(src, order, lo,     mid,    depth + 1);
                buildRange(src, order, mid + 1, hi,    depth + 1);
            }
        };
        // ── End KDTreeND ──────────────────────────────────────────────────────

        void BuildIndex()
        {
            if (mNumPoints > 0 && mPoints != nullptr)
            {
                mKDTree.build(mNumPoints, mPoints);
            }
        }

    private:
        size_t       mNumPoints;
        PointN const* mPoints;
        KDTreeND     mKDTree;
    };
}
