// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// N-dimensional nearest neighbor search
//
// Backed by nanoflann (header-only KD-tree library) for reliable O(log n)
// average query performance in arbitrary dimensions.  nanoflann uses a
// conventional KD-tree with AABB bounding-box pruning and an optimal
// leaf-size parameter, and is well-tested for 2D-6D embeddings.
//
// Public interface is identical to the previous KDTreeND implementation:
//   SetPoints() / FindNearestNeighbor() / FindKNearestNeighbors() /
//   FindKNearestNeighborsToPoint() / DistanceSquared()
//
// License: Boost Software License 1.0

#pragma once

// nanoflann is a header-only library located alongside libbg sources.
// The relative include path resolves from the GTE/Mathematics sub-directory.
#include "../../nanoflann.hpp"

#include <Mathematics/Vector.h>
#include <Mathematics/Logger.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace gte
{
    // N-dimensional nearest neighbor search backed by nanoflann.
    // Provides k-nearest neighbor queries for CVT/RVD operations.
    //
    // PointN = GTE's Vector<N, Real> (column vector, element access via []).
    // nanoflann requires a "dataset adaptor" that exposes:
    //   kdtree_get_point_count() -> size_t
    //   kdtree_get_pt(idx, dim)  -> Real
    //   kdtree_get_bbox(bb)      -> bool  (false = no pre-computed bbox)
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

        // Set the point set and build the KD-tree index.
        void SetPoints(size_t numPoints, PointN const* points)
        {
            mNumPoints = numPoints;
            mPoints    = points;
            BuildIndex();
        }

        // Find single nearest neighbor to query point.
        // Returns index of nearest point, or -1 if no points.
        int32_t FindNearestNeighbor(PointN const& query) const
        {
            if (mNumPoints == 0 || !mIndex) { return -1; }
            size_t     ret_index  = 0;
            Real       out_dist   = std::numeric_limits<Real>::max();
            nanoflann::KNNResultSet<Real> resultSet(1);
            resultSet.init(&ret_index, &out_dist);
            mIndex->findNeighbors(resultSet, &query[0]);
            return static_cast<int32_t>(ret_index);
        }

        // Find k nearest neighbors to query point.
        // Returns actual number found (may be < k if fewer points exist).
        // neighbors and distances are populated in order of increasing distance.
        size_t FindKNearestNeighbors(
            PointN const& query,
            size_t k,
            std::vector<int32_t>& neighbors,
            std::vector<Real>& distances) const
        {
            neighbors.clear();
            distances.clear();
            if (mNumPoints == 0 || k == 0 || !mIndex) { return 0; }

            k = std::min(k, mNumPoints);
            std::vector<size_t> idx(k);
            std::vector<Real>   dists(k);

            nanoflann::KNNResultSet<Real> resultSet(k);
            resultSet.init(idx.data(), dists.data());
            mIndex->findNeighbors(resultSet, &query[0]);

            size_t found = resultSet.size();
            neighbors.resize(found);
            distances.resize(found);
            for (size_t i = 0; i < found; ++i)
            {
                neighbors[i] = static_cast<int32_t>(idx[i]);
                distances[i] = std::sqrt(dists[i]);  // nanoflann returns squared dist
            }
            return found;
        }

        // Find k nearest neighbors to an existing point (by index).
        // NOTE: The returned neighbors include the query point itself (at distance 0).
        // Callers should filter out queryIndex from the result when they want
        // only distinct neighbors.  DelaunayNN::ComputeNeighborhood() does this.
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
            // k+1 to include the point itself (filtered below by caller)
            return FindKNearestNeighbors(mPoints[queryIndex], k + 1, neighbors, distances);
        }

        size_t GetNumPoints() const { return mNumPoints; }

        static Real DistanceSquared(PointN const& p0, PointN const& p1)
        {
            Real s = static_cast<Real>(0);
            for (size_t i = 0; i < N; ++i)
            {
                Real d = p1[i] - p0[i];
                s += d * d;
            }
            return s;
        }

    private:
        // ── nanoflann dataset adaptor ─────────────────────────────────────────
        // Wraps the external PointN array so nanoflann can index it.
        struct DataAdaptor
        {
            size_t       numPts  = 0;
            PointN const* pts    = nullptr;

            size_t kdtree_get_point_count() const { return numPts; }

            Real kdtree_get_pt(size_t idx, size_t dim) const
            {
                return pts[idx][static_cast<int>(dim)];
            }

            // Optional bounding box: return false to have nanoflann compute it.
            template <class BBOX>
            bool kdtree_get_bbox(BBOX&) const { return false; }
        };

        // nanoflann KD-tree type: L2, compile-time dimension N, leaf_max_size=10.
        using KDIndex = nanoflann::KDTreeSingleIndexAdaptor<
            nanoflann::L2_Adaptor<Real, DataAdaptor>,
            DataAdaptor,
            static_cast<int>(N),   // compile-time dimension
            size_t>;               // index type

        void BuildIndex()
        {
            if (mNumPoints == 0 || mPoints == nullptr)
            {
                mIndex.reset();
                return;
            }
            mAdaptor.numPts = mNumPoints;
            mAdaptor.pts    = mPoints;
            // leaf_max_size=10 matches nanoflann's default and works well for
            // both N=3 (isotropic) and N=6 (anisotropic) CVT embeddings.
            // n_thread_build=1: single-threaded build to avoid overhead for
            // typically small seed sets (hundreds to tens of thousands).
            mIndex = std::make_unique<KDIndex>(
                static_cast<int>(N), mAdaptor,
                nanoflann::KDTreeSingleIndexAdaptorParams(10 /*leaf_max_size*/));
        }

        size_t        mNumPoints;
        PointN const* mPoints;
        DataAdaptor   mAdaptor;
        std::unique_ptr<KDIndex> mIndex;
    };
}

