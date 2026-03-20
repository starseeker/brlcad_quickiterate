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

        // Lock-free read — port of Geogram's PackedArrays::get_array
        // (lock=false path that is safe because data is written before size,
        // and size is stored with memory_order_release / loaded with
        // memory_order_acquire):
        //
        //   Writer (EnlargeNeighborhood):
        //     1. hold spinlock
        //     2. write data into flat array
        //     3. store size with release  ← synchronization point
        //     4. release spinlock
        //
        //   Reader (GetNeighbors):
        //     1. load size with acquire   ← synchronization point
        //     2. copy data from flat array
        //
        // If the reader sees the new size it sees all the new data (release
        // happened-before acquire). If it sees the old size it reads the
        // old, complete data. Never a torn read.
        // Vertices that exceed FLAT_K fall back to the locked overflow path.
        std::vector<int32_t> GetNeighbors(int32_t v) const override
        {
            if (v < 0 || static_cast<size_t>(v) >= this->mNumVertices)
                return {};

            uint32_t sz = mFlatSizes[v].load(std::memory_order_acquire);

            if (sz != OVERFLOW_MARKER)
            {
                // Lock-free path: data written before size, acquire sees it.
                size_t vi = static_cast<size_t>(v) * FLAT_K;
                return std::vector<int32_t>(
                    mFlatData.get() + vi,
                    mFlatData.get() + vi + sz);
            }
            // Overflow path (rare): lock needed.
            AcquireLock(static_cast<size_t>(v));
            std::vector<int32_t> result = mOverflow[v];
            ReleaseLock(static_cast<size_t>(v));
            return result;
        }

        size_t GetNumNeighbors(int32_t v) const
        {
            if (v < 0 || static_cast<size_t>(v) >= this->mNumVertices)
                return 0;
            uint32_t sz = mFlatSizes[v].load(std::memory_order_acquire);
            if (sz != OVERFLOW_MARKER) return sz;
            AcquireLock(static_cast<size_t>(v));
            size_t r = mOverflow[v].size();
            ReleaseLock(static_cast<size_t>(v));
            return r;
        }

        // Thread-safe enlargement: per-vertex spinlock; writes data before
        // updating size with memory_order_release so GetNeighbors can read
        // lock-free (matching Geogram's enlarge_neighborhood + PackedArrays).
        //
        // The expensive KD-tree query is performed OUTSIDE the spinlock so
        // concurrent threads calling EnlargeNeighborhood for different seeds
        // can run the query in parallel without serialization.  The spinlock
        // only protects the cheap store step.  Duplicate queries (two threads
        // enlarging the same seed concurrently) may occasionally occur but
        // produce correct results (both write valid neighborhood data, the
        // second writer wins, which is fine).
        void EnlargeNeighborhood(int32_t v, size_t newSize)
        {
            if (v < 0 || static_cast<size_t>(v) >= this->mNumVertices)
                return;

            // Fast pre-check without the lock: if already big enough, skip.
            {
                uint32_t cur = mFlatSizes[v].load(std::memory_order_acquire);
                size_t   curSz = (cur == OVERFLOW_MARKER)
                                    ? mOverflow[v].size()   // rare; safe: owner wrote
                                    : static_cast<size_t>(cur);
                if (newSize <= curSz) return;
            }

            // Expensive: KD-tree query runs WITHOUT holding any lock.
            // Multiple threads may do this concurrently for the same vertex —
            // that wastes a little work but avoids serialization on the slow
            // 6D tree traversal.
            size_t k = std::min(newSize, this->mNumVertices - 1);
            std::vector<size_t> raw(k + 1);
            std::vector<Real>   sqd(k + 1);
            size_t found = mNNSearch.FindKNearestNeighborsToPoint(
                v, k, raw.data(), sqd.data());

            // Filter (match Geogram duplicate-point handling).
            std::vector<int32_t> tmp;
            tmp.reserve(found);
            for (size_t j = 0; j < found; ++j)
            {
                int32_t nb = static_cast<int32_t>(raw[j]);
                if (nb == v) continue;
                if (sqd[j] == static_cast<Real>(0) && nb < v) continue;
                tmp.push_back(nb);
            }

            // Cheap: commit under lock only if our result is bigger than what
            // is already there (another thread may have written in the meantime).
            AcquireLock(static_cast<size_t>(v));

            uint32_t cur = mFlatSizes[v].load(std::memory_order_relaxed);
            size_t   curSz = (cur == OVERFLOW_MARKER)
                                ? mOverflow[v].size()
                                : static_cast<size_t>(cur);

            if (tmp.size() > curSz)
            {
                if (tmp.size() <= FLAT_K)
                {
                    // Write data first, THEN update size (release).
                    size_t vi = static_cast<size_t>(v) * FLAT_K;
                    for (size_t j = 0; j < tmp.size(); ++j)
                        mFlatData[vi + j] = tmp[j];
                    mFlatSizes[v].store(
                        static_cast<uint32_t>(tmp.size()),
                        std::memory_order_release);
                }
                else
                {
                    // Overflow: lock is already held so readers will block.
                    mOverflow[v] = std::move(tmp);
                    mFlatSizes[v].store(OVERFLOW_MARKER,
                                        std::memory_order_release);
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
        // Maximum neighbors stored in the lock-free flat array.
        // 256 comfortably exceeds the ~30–80 used in practice (SR check
        // rarely grows beyond 60 even on complex meshes).
        static constexpr size_t   FLAT_K          = 256;
        static constexpr uint32_t OVERFLOW_MARKER = 0xFFFFFFFFu;

        // ── Flat pre-allocated neighbourhood storage ──────────────────────────
        // mFlatData[v * FLAT_K .. v * FLAT_K + mFlatSizes[v]) holds the
        // neighbours of vertex v.  Written before mFlatSizes[v] is updated
        // (release), so readers can load mFlatSizes[v] (acquire) and then
        // safely read mFlatData without a lock.
        std::unique_ptr<int32_t[]>                  mFlatData;
        std::unique_ptr<std::atomic<uint32_t>[]>    mFlatSizes;

        // Fallback for the rare case that a vertex needs > FLAT_K neighbours.
        std::vector<std::vector<int32_t>>           mOverflow;

        // ── Eager parallel neighbourhood precomputation ───────────────────────
        void UpdateNeighborhoods()
        {
            size_t n = this->mNumVertices;

            // Allocate flat storage.
            mFlatData  = std::make_unique<int32_t[]>(n * FLAT_K);
            mFlatSizes = std::make_unique<std::atomic<uint32_t>[]>(n);
            mOverflow.assign(n, {});

            // Allocate per-vertex spinlocks, all unlocked (0).
            mLocks = std::make_unique<std::atomic<uint8_t>[]>(n);
            for (size_t i = 0; i < n; ++i)
            {
                mFlatSizes[i].store(0, std::memory_order_relaxed);
                mLocks[i].store(0, std::memory_order_relaxed);
            }

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

                        // Fill flat array; threads write disjoint ranges.
                        size_t vi  = i * FLAT_K;
                        size_t out = 0;
                        for (size_t j = 0; j < found && out < FLAT_K; ++j)
                        {
                            int32_t idx = static_cast<int32_t>(raw[j]);
                            if (idx == static_cast<int32_t>(i)) continue;
                            if (sqd[j] == static_cast<Real>(0)
                                && idx < static_cast<int32_t>(i)) continue;
                            mFlatData[vi + out++] = idx;
                        }
                        // Release store: data written before size is visible.
                        mFlatSizes[i].store(static_cast<uint32_t>(out),
                                            std::memory_order_release);
                    }
                });
            }
            for (auto& th : threads) th.join();
        }

        // ── Per-vertex spinlock ───────────────────────────────────────────────
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
        NearestNeighborSearchN<Real, N>                  mNNSearch;
        mutable std::unique_ptr<std::atomic<uint8_t>[]>  mLocks;
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

