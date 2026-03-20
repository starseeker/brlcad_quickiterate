// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
//
// N-dimensional nearest neighbor search.
//
// Direct port of Geogram's BalancedKdTree algorithm, adapted for GTE's
// compile-time-dimension Vector<N, Real>.
//
// Key properties matching Geogram:
//   - Sorted point_index[] array: O(1) range endpoints per node (no node ptrs)
//   - Max-spread axis selection for balanced splits (std::nth_element)
//   - Parallel top-level build: splits top 3 levels (8 subtrees) in parallel
//   - Leaf size 16 (matching Geogram's MAX_LEAF_SIZE)
//   - Bounding-box distance pruning during traversal (same as Geogram)
//   - Thread-safe after Build(): all queries are read-only, safe for
//     concurrent calls from multiple threads

#pragma once

#include <Mathematics/Vector.h>
#include <Mathematics/Logger.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

namespace gte
{
    // BalancedKdTree over GTE's Vector<N, Real> points.
    //
    // Public interface used by DelaunayNN:
    //   SetPoints(n, pts)
    //   FindNearestNeighbor(query) -> int32_t
    //   FindKNearestNeighbors(query, k, nbrs, sqDists)   // sqDists, not dists
    //   FindKNearestNeighborsToPoint(idx, k, nbrs, sqDists)
    //   GetNumPoints() -> size_t
    //   DistanceSquared(p0, p1) -> Real
    template <typename Real, size_t N>
    class NearestNeighborSearchN
    {
    public:
        using PointN = Vector<N, Real>;

        static_assert(
            std::is_floating_point<Real>::value,
            "Real must be float or double.");

        NearestNeighborSearchN() = default;
        ~NearestNeighborSearchN() = default;

        // Set the point set and build the balanced KD-tree.
        // The pointer must remain valid for the lifetime of this object.
        void SetPoints(size_t numPoints, PointN const* points)
        {
            mNumPoints = numPoints;
            mPoints    = points;
            if (numPoints > 0)
                Build();
        }

        // Find single nearest neighbor.  Returns -1 if empty.
        int32_t FindNearestNeighbor(PointN const& query) const
        {
            if (mNumPoints == 0) return -1;
            size_t nbr;
            Real   sqd;
            FindKNearestNeighbors(query, 1, &nbr, &sqd);
            return static_cast<int32_t>(nbr);
        }

        // Find k nearest neighbors to a query point.
        // Outputs: nbrs[0..k-1] = point indices (ascending distance),
        //          sqDists[0..k-1] = squared distances.
        // Returns actual count found (min(k, mNumPoints)).
        size_t FindKNearestNeighbors(
            PointN const& query,
            size_t        k,
            size_t*       nbrs,
            Real*         sqDists) const
        {
            if (mNumPoints == 0 || k == 0) return 0;
            k = std::min(k, mNumPoints);

            // Work arrays: size k+1 (one extra slot for the sorted-insert
            // algorithm, matching Geogram's alloca(sizeof*k+1) pattern).
            std::vector<size_t> workNbr(k + 1, NO_IDX);
            std::vector<Real>   workSq (k + 1, std::numeric_limits<Real>::max());

            NearestNeighbors nn(k, nbrs, sqDists,
                                workNbr.data(), workSq.data());

            // Compute initial bbox distance for the root.
            Real bboxDist = static_cast<Real>(0);
            Real bboxMin[N], bboxMax[N];
            for (size_t d = 0; d < N; ++d)
            {
                bboxMin[d] = mBBoxMin[d];
                bboxMax[d] = mBBoxMax[d];
                Real q = query[static_cast<int>(d)];
                if      (q < mBBoxMin[d]) bboxDist += Sq(mBBoxMin[d] - q);
                else if (q > mBBoxMax[d]) bboxDist += Sq(mBBoxMax[d] - q);
            }

            QueryRecursive(/*node*/1, 0, mNumPoints,
                           bboxMin, bboxMax, bboxDist,
                           &query[0], nn);
            nn.CopyToUser();
            return k;
        }

        // Find k nearest neighbors to point[queryIndex] (excludes self).
        // Outputs: nbrs, sqDists (ascending), returns count.
        size_t FindKNearestNeighborsToPoint(
            int32_t queryIndex,
            size_t  k,
            size_t* nbrs,
            Real*   sqDists) const
        {
            if (queryIndex < 0
                || static_cast<size_t>(queryIndex) >= mNumPoints)
            {
                return 0;
            }
            // Ask for k+1 to include the point itself, then strip it below.
            size_t kp1 = std::min(k + 1, mNumPoints);
            std::vector<size_t> tmp(kp1);
            std::vector<Real>   td(kp1);
            FindKNearestNeighbors(mPoints[queryIndex], kp1,
                                  tmp.data(), td.data());

            // Copy to output, skipping queryIndex.
            size_t out = 0;
            for (size_t i = 0; i < kp1 && out < k; ++i)
            {
                if (tmp[i] != static_cast<size_t>(queryIndex))
                {
                    nbrs[out]    = tmp[i];
                    sqDists[out] = td[i];
                    ++out;
                }
            }
            return out;
        }

        size_t GetNumPoints() const { return mNumPoints; }

        static Real DistanceSquared(PointN const& p0, PointN const& p1)
        {
            Real s = static_cast<Real>(0);
            for (size_t i = 0; i < N; ++i)
            {
                Real d = p1[static_cast<int>(i)]
                       - p0[static_cast<int>(i)];
                s += d * d;
            }
            return s;
        }

    private:
        // ─── Constants ────────────────────────────────────────────────────────
        static constexpr size_t MAX_LEAF = 16;   // Geogram MAX_LEAF_SIZE = 16
        static constexpr size_t NO_IDX   = ~size_t(0);

        static Real Sq(Real x) { return x * x; }

        // ─── Tree storage (Geogram BalancedKdTree layout) ─────────────────────
        // mPointIndex[i]: original point index at sorted position i.
        //   For internal node n covering [b,e), midpoint m = b+(e-b)/2.
        //   Left subtree  = node 2*n,   covering [b, m).
        //   Right subtree = node 2*n+1, covering [m, e).
        // Root = node 1 (not 0, so that 2n/2n+1 works without off-by-one).
        std::vector<size_t>  mPointIndex;   // sorted index remapping
        std::vector<size_t>  mSplitCoord;   // per node: split dimension
        std::vector<Real>    mSplitVal;     // per node: split value
        Real                 mBBoxMin[N]{};
        Real                 mBBoxMax[N]{};
        size_t               mNumPoints = 0;
        PointN const*        mPoints    = nullptr;

        // ─── Nearest-neighbor priority queue (Geogram NearestNeighbors) ───────
        // Maintains a sorted list of (index, sq_dist) pairs of size ≤ K,
        // plus one extra work slot.  Work arrays are owned by the caller
        // (allocated on heap before calling FindKNearestNeighbors).
        struct NearestNeighbors
        {
            size_t   nbMax;       // K
            size_t   nb;          // current fill
            size_t*  wNbr;        // work array, size K+1
            Real*    wSq;         // work array, size K+1
            size_t*  uNbr;        // user output, size K
            Real*    uSq;         // user output, size K

            NearestNeighbors(size_t k,
                             size_t* un, Real* us,
                             size_t* wn, Real* ws)
                : nbMax(k), nb(0), wNbr(wn), wSq(ws), uNbr(un), uSq(us)
            {
                // Initialise work arrays to "nothing found yet".
                for (size_t i = 0; i <= k; ++i)
                {
                    wNbr[i] = NO_IDX;
                    wSq[i]  = std::numeric_limits<Real>::max();
                }
            }

            // Furthest known neighbour squared distance (or +inf when < K found).
            Real FurthestSq() const
            {
                return (nb == nbMax)
                    ? wSq[nb - 1]
                    : std::numeric_limits<Real>::max();
            }

            // Sorted insert: keep the K nearest in ascending sq-dist order.
            void Insert(size_t idx, Real sq)
            {
                int i;
                for (i = static_cast<int>(nb); i > 0; --i)
                {
                    if (wSq[i - 1] < sq) break;
                    wNbr[i] = wNbr[i - 1];
                    wSq[i]  = wSq[i - 1];
                }
                wNbr[i] = idx;
                wSq[i]  = sq;
                if (nb < nbMax) ++nb;
            }

            void CopyToUser() const
            {
                for (size_t i = 0; i < nbMax; ++i)
                {
                    uNbr[i] = wNbr[i];
                    uSq[i]  = wSq[i];
                }
            }
        };

        // ─── Build ────────────────────────────────────────────────────────────

        // Pre-compute the storage size needed for node-index arrays:
        // root=1; left(n)=2n, right(n)=2n+1.
        static size_t MaxNodeIndex(size_t node, size_t b, size_t e)
        {
            if (e - b <= MAX_LEAF) return node;
            size_t m = b + (e - b) / 2;
            return std::max(MaxNodeIndex(2 * node,     b, m),
                            MaxNodeIndex(2 * node + 1, m, e));
        }

        // Spread of coordinate d over [b,e).
        Real Spread(size_t b, size_t e, size_t d) const
        {
            Real mn =  std::numeric_limits<Real>::max();
            Real mx = -std::numeric_limits<Real>::max();
            for (size_t i = b; i < e; ++i)
            {
                Real v = mPoints[mPointIndex[i]][static_cast<int>(d)];
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
            return mx - mn;
        }

        // Choose split coordinate = dimension with maximum spread over [b,e).
        size_t BestSplitCoord(size_t b, size_t e) const
        {
            size_t best = 0;
            Real   maxS = Spread(b, e, 0);
            for (size_t d = 1; d < N; ++d)
            {
                Real s = Spread(b, e, d);
                if (s > maxS) { maxS = s; best = d; }
            }
            return best;
        }

        // Partition [b,e) at median m = b+(e-b)/2 along the best-spread axis.
        // Stores split coord/val in node arrays.  Returns m.
        size_t SplitNode(size_t nodeIdx, size_t b, size_t e)
        {
            if (b + 1 == e) return b;
            size_t splitD = BestSplitCoord(b, e);
            size_t m      = b + (e - b) / 2;

            // Partial sort so mPointIndex[m] is the median, left < median,
            // right >= median — matching Geogram's nth_element call exactly.
            std::nth_element(
                mPointIndex.begin() + static_cast<std::ptrdiff_t>(b),
                mPointIndex.begin() + static_cast<std::ptrdiff_t>(m),
                mPointIndex.begin() + static_cast<std::ptrdiff_t>(e),
                [&](size_t a, size_t bb2) {
                    return mPoints[a][static_cast<int>(splitD)]
                         < mPoints[bb2][static_cast<int>(splitD)];
                });

            mSplitCoord[nodeIdx] = splitD;
            mSplitVal  [nodeIdx] = mPoints[mPointIndex[m]][static_cast<int>(splitD)];
            return m;
        }

        // Recursively build nodes [b,e) under nodeIdx.
        void BuildRecursive(size_t nodeIdx, size_t b, size_t e)
        {
            if (e - b <= MAX_LEAF) return;
            size_t m = SplitNode(nodeIdx, b, e);
            BuildRecursive(2 * nodeIdx,     b, m);
            BuildRecursive(2 * nodeIdx + 1, m, e);
        }

        void Build()
        {
            // Initialise identity permutation.
            mPointIndex.resize(mNumPoints);
            for (size_t i = 0; i < mNumPoints; ++i)
                mPointIndex[i] = i;

            // Global bounding box.
            for (size_t d = 0; d < N; ++d)
            {
                mBBoxMin[d] =  std::numeric_limits<Real>::max();
                mBBoxMax[d] = -std::numeric_limits<Real>::max();
            }
            for (size_t i = 0; i < mNumPoints; ++i)
            {
                for (size_t d = 0; d < N; ++d)
                {
                    Real v = mPoints[i][static_cast<int>(d)];
                    if (v < mBBoxMin[d]) mBBoxMin[d] = v;
                    if (v > mBBoxMax[d]) mBBoxMax[d] = v;
                }
            }

            // Size the node arrays.
            size_t sz = MaxNodeIndex(1, 0, mNumPoints) + 1;
            mSplitCoord.assign(sz, 0);
            mSplitVal  .assign(sz, static_cast<Real>(0));

            // Parallel top-level build matching Geogram's 3-level strategy:
            //   Level 0: root (single)
            //   Level 1: 2 nodes  (parallel pair)
            //   Level 2: 4 nodes  (parallel quad)
            //   Level 3+: 8 subtrees in parallel, each single-threaded
            //
            // Threshold: ≥ 16*MAX_LEAF = 256 points (Geogram: 16*MAX_LEAF_SIZE).
            if (mNumPoints >= 16 * MAX_LEAF)
            {
                // Level 0 — root
                size_t m0 = 0, m8 = mNumPoints;
                size_t m4 = SplitNode(1, m0, m8);

                // Level 1 — two subtrees in parallel
                size_t m2, m6;
                {
                    std::thread t0([&]{ m2 = SplitNode(2, m0, m4); });
                    std::thread t1([&]{ m6 = SplitNode(3, m4, m8); });
                    t0.join(); t1.join();
                }

                // Level 2 — four subtrees in parallel
                size_t m1, m3, m5, m7;
                {
                    std::thread t0([&]{ m1 = SplitNode(4,  m0, m2); });
                    std::thread t1([&]{ m3 = SplitNode(5,  m2, m4); });
                    std::thread t2([&]{ m5 = SplitNode(6,  m4, m6); });
                    std::thread t3([&]{ m7 = SplitNode(7,  m6, m8); });
                    t0.join(); t1.join(); t2.join(); t3.join();
                }

                // Level 3 — eight subtrees, each built recursively
                {
                    std::thread t0([&]{ BuildRecursive(8,  m0, m1); });
                    std::thread t1([&]{ BuildRecursive(9,  m1, m2); });
                    std::thread t2([&]{ BuildRecursive(10, m2, m3); });
                    std::thread t3([&]{ BuildRecursive(11, m3, m4); });
                    std::thread t4([&]{ BuildRecursive(12, m4, m5); });
                    std::thread t5([&]{ BuildRecursive(13, m5, m6); });
                    std::thread t6([&]{ BuildRecursive(14, m6, m7); });
                    std::thread t7([&]{ BuildRecursive(15, m7, m8); });
                    t0.join(); t1.join(); t2.join(); t3.join();
                    t4.join(); t5.join(); t6.join(); t7.join();
                }
            }
            else
            {
                BuildRecursive(1, 0, mNumPoints);
            }
        }

        // ─── Query ────────────────────────────────────────────────────────────

        // Leaf: brute-force distance to all points in [b,e).
        void QueryLeaf(size_t b, size_t e,
                       Real const* query,
                       NearestNeighbors& nn) const
        {
            Real R = nn.FurthestSq();
            size_t nb = e - b;
            // Cache local indices for cache-friendly access (Geogram pattern).
            size_t   local_idx[MAX_LEAF];
            Real     local_sq [MAX_LEAF];
            for (size_t ii = 0; ii < nb; ++ii)
            {
                size_t pi = mPointIndex[b + ii];
                Real sq = static_cast<Real>(0);
                for (size_t d = 0; d < N; ++d)
                {
                    Real diff = query[d] - mPoints[pi][static_cast<int>(d)];
                    sq += diff * diff;
                }
                local_idx[ii] = pi;
                local_sq [ii] = sq;
            }
            for (size_t ii = 0; ii < nb; ++ii)
            {
                if (local_sq[ii] <= R)
                {
                    nn.Insert(local_idx[ii], local_sq[ii]);
                    R = nn.FurthestSq();
                }
            }
        }

        // Recursive traversal with bbox-distance pruning (Geogram algorithm).
        void QueryRecursive(size_t nodeIdx,
                            size_t b, size_t e,
                            Real*  bboxMin, Real* bboxMax,
                            Real   boxDist,
                            Real const* query,
                            NearestNeighbors& nn) const
        {
            if (e - b <= MAX_LEAF)
            {
                QueryLeaf(b, e, query, nn);
                return;
            }

            size_t m     = b + (e - b) / 2;
            size_t coord = mSplitCoord[nodeIdx];
            Real   val   = mSplitVal  [nodeIdx];
            Real   cutD  = query[coord] - val;

            if (cutD < static_cast<Real>(0))
            {
                // Query is left of split — go left first.
                {
                    Real save    = bboxMax[coord];
                    bboxMax[coord] = val;
                    QueryRecursive(2 * nodeIdx, b, m,
                                   bboxMin, bboxMax, boxDist, query, nn);
                    bboxMax[coord] = save;
                }
                // Update box distance for right subtree.
                Real boxD2 = bboxMin[coord] - query[coord];
                if (boxD2 > static_cast<Real>(0)) boxDist -= Sq(boxD2);
                boxDist += Sq(cutD);
                if (boxDist <= nn.FurthestSq())
                {
                    Real save    = bboxMin[coord];
                    bboxMin[coord] = val;
                    QueryRecursive(2 * nodeIdx + 1, m, e,
                                   bboxMin, bboxMax, boxDist, query, nn);
                    bboxMin[coord] = save;
                }
            }
            else
            {
                // Query is right of split — go right first.
                {
                    Real save    = bboxMin[coord];
                    bboxMin[coord] = val;
                    QueryRecursive(2 * nodeIdx + 1, m, e,
                                   bboxMin, bboxMax, boxDist, query, nn);
                    bboxMin[coord] = save;
                }
                // Update box distance for left subtree.
                Real boxD2 = query[coord] - bboxMax[coord];
                if (boxD2 > static_cast<Real>(0)) boxDist -= Sq(boxD2);
                boxDist += Sq(cutD);
                if (boxDist <= nn.FurthestSq())
                {
                    Real save    = bboxMax[coord];
                    bboxMax[coord] = val;
                    QueryRecursive(2 * nodeIdx, b, m,
                                   bboxMin, bboxMax, boxDist, query, nn);
                    bboxMax[coord] = save;
                }
            }
        }
    };
}

