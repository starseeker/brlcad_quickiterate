// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
//
// Nearest-neighbor based Delaunay for N dimensions — Geogram architecture.
//
// Direct port of Geogram's Delaunay_NearestNeighbors:
//   • SetVertices builds the KD-tree then eagerly precomputes ALL K=30
//     neighborhoods in parallel (matching Geogram's update_neighbors()).
//   • GetNeighbors is a pure read after SetVertices — no lazy computation.
//   • EnlargeNeighborhood uses per-vertex std::atomic spinlocks matching
//     Geogram's PackedArrays::lock_array/unlock_array — safe for concurrent
//     calls from multiple threads on the same DelaunayNN instance.
//   • FindNearestVertex delegates to the shared KD-tree (thread-safe reads).
//
// This lets CVTN::AccumulateCentroids build ONE DelaunayNN per iteration and
// share it across all worker threads without per-thread KD-tree rebuilds.

#pragma once

#include <Mathematics/DelaunayN.h>
#include <Mathematics/NearestNeighborSearchN.h>
#include <Mathematics/Logger.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace gte
{
    template <typename Real, size_t N>
    class DelaunayNN : public DelaunayN<Real, N>
    {
    public:
        using PointN  = typename DelaunayN<Real, N>::PointN;
        using Simplex = typename DelaunayN<Real, N>::Simplex;

        explicit DelaunayNN(size_t defaultNbNeighbors = 30)
            : DelaunayN<Real, N>()
            , mDefaultNbNeighbors(defaultNbNeighbors)
        {
        }

        virtual ~DelaunayNN() = default;

        // Build KD-tree and eagerly precompute ALL K=defaultNbNeighbors
        // neighborhoods in parallel — matching Geogram's set_vertices() which
        // calls NN_->set_points() then update_neighbors() with parallel_for.
        //
        // After this call the object is immutable (all GetNeighbors calls are
        // pure reads) until the next SetVertices.
        bool SetVertices(size_t numVertices, PointN const* vertices) override
        {
            LogAssert(numVertices > 0 && vertices != nullptr,
                      "Invalid arguments.");

            this->mNumVertices = numVertices;
            this->mVertices    = vertices;

            // Build the KD-tree (once, in the calling thread).
            mNNSearch.SetPoints(numVertices, vertices);

            // Eagerly compute ALL K=mDefaultNbNeighbors neighborhoods in
            // parallel, matching Geogram's parallel_for in update_neighbors().
            UpdateNeighborhoods();

            return true;
        }

        // Thread-safe read under per-vertex spinlock.
        // Matches Geogram's PackedArrays::get_array (thread-safe variant):
        // reads are serialized with concurrent EnlargeNeighborhood writes.
        std::vector<int32_t> GetNeighbors(int32_t v) const override
        {
            if (v < 0 || static_cast<size_t>(v) >= this->mNumVertices)
                return {};
            AcquireLock(static_cast<size_t>(v));
            std::vector<int32_t> result = mNeighborhoods[v];
            ReleaseLock(static_cast<size_t>(v));
            return result;
        }

        size_t GetNumNeighbors(int32_t v) const
        {
            if (v < 0 || static_cast<size_t>(v) >= this->mNumVertices)
                return 0;
            return mNeighborhoods[v].size();
        }

        // Thread-safe enlargement: per-vertex spinlock ensures each
        // neighbourhood is expanded at most once across concurrent threads,
        // matching Geogram's enlarge_neighborhood with lock_array/unlock_array.
        void EnlargeNeighborhood(int32_t v, size_t newSize)
        {
            if (v < 0 || static_cast<size_t>(v) >= this->mNumVertices)
                return;

            // Per-vertex spinlock (Geogram: neighbors_.lock_array(v)).
            AcquireLock(static_cast<size_t>(v));

            if (newSize > mNeighborhoods[v].size())
            {
                // Re-query for newSize neighbors.
                size_t k = std::min(newSize, this->mNumVertices - 1);
                std::vector<size_t> raw(k + 1);
                std::vector<Real>   sqd(k + 1);
                size_t found = mNNSearch.FindKNearestNeighborsToPoint(
                    v, k, raw.data(), sqd.data());

                mNeighborhoods[v].clear();
                mNeighborhoods[v].reserve(found);
                for (size_t j = 0; j < found; ++j)
                {
                    int32_t nb = static_cast<int32_t>(raw[j]);
                    if (nb == v) continue;
                    // Geogram: skip if duplicate (dist==0) and nb < v.
                    if (sqd[j] == static_cast<Real>(0) && nb < v) continue;
                    mNeighborhoods[v].push_back(nb);
                }
            }

            ReleaseLock(static_cast<size_t>(v));
        }

        // KD-tree nearest vertex (thread-safe: read-only after SetVertices).
        int32_t FindNearestVertex(PointN const& query) const override
        {
            return mNNSearch.FindNearestNeighbor(query);
        }

        void SetDefaultNbNeighbors(size_t nb) { mDefaultNbNeighbors = nb; }
        size_t GetDefaultNbNeighbors() const   { return mDefaultNbNeighbors; }
        size_t GetNumVertices()        const   { return this->mNumVertices;  }

    private:
        // ── Eager parallel neighbourhood precomputation ───────────────────────
        // Geogram: parallel_for(0, nb_vertices, store_neighbors_CB, grain=1).
        // Each vertex independently queries the shared (read-only) KD-tree.
        void UpdateNeighborhoods()
        {
            size_t n = this->mNumVertices;
            mNeighborhoods.assign(n, {});

            // Allocate per-vertex spinlocks, all in "unlocked" (0) state.
            mLocks = std::make_unique<std::atomic<uint8_t>[]>(n);
            for (size_t i = 0; i < n; ++i)
                mLocks[i].store(0, std::memory_order_relaxed);

            // Partition vertices across hardware threads.
            unsigned int hw = std::thread::hardware_concurrency();
            if (hw == 0) hw = 1;
            size_t nT  = std::min(static_cast<size_t>(hw), n);
            size_t per = (n + nT - 1) / nT;

            std::vector<std::thread> threads;
            threads.reserve(nT);
            for (size_t t = 0; t < nT; ++t)
            {
                size_t b = t * per;
                size_t e = std::min(b + per, n);
                threads.emplace_back([this, b, e]()
                {
                    size_t k = std::min(mDefaultNbNeighbors,
                                        this->mNumVertices - 1);
                    std::vector<size_t> raw(k + 1);
                    std::vector<Real>   sqd(k + 1);
                    for (size_t i = b; i < e; ++i)
                    {
                        size_t found = mNNSearch.FindKNearestNeighborsToPoint(
                            static_cast<int32_t>(i), k,
                            raw.data(), sqd.data());
                        auto& nb = mNeighborhoods[i];
                        nb.clear();
                        nb.reserve(found);
                        for (size_t j = 0; j < found; ++j)
                        {
                            int32_t idx = static_cast<int32_t>(raw[j]);
                            if (idx == static_cast<int32_t>(i)) continue;
                            if (sqd[j] == static_cast<Real>(0)
                                && idx < static_cast<int32_t>(i)) continue;
                            nb.push_back(idx);
                        }
                    }
                });
            }
            for (auto& th : threads) th.join();
        }

        // ── Per-vertex spinlock (Geogram: PackedArrays::lock/unlock_array) ────
        void AcquireLock(size_t v) const
        {
            uint8_t expected = 0;
            while (!mLocks[v].compare_exchange_weak(
                       expected, 1,
                       std::memory_order_acquire,
                       std::memory_order_relaxed))
            {
                expected = 0;
            }
        }

        void ReleaseLock(size_t v) const
        {
            mLocks[v].store(0, std::memory_order_release);
        }

        size_t mDefaultNbNeighbors;
        NearestNeighborSearchN<Real, N>           mNNSearch;
        std::vector<std::vector<int32_t>>         mNeighborhoods;
        mutable std::unique_ptr<std::atomic<uint8_t>[]> mLocks;
    };

    template <typename Real, size_t N>
    std::unique_ptr<DelaunayN<Real, N>> CreateDelaunayN(
        std::string const& method)
    {
        if (method == "NN" || method == "default")
            return std::make_unique<DelaunayNN<Real, N>>();
        LogError("Unknown Delaunay method: " + method);
        return nullptr;
    }
}

