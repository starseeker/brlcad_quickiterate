/*                    H A S H _ C X X . H
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file bu/hash_cxx.h
 *
 * C++ helpers for bu_h128_t.
 *
 * This header is intentionally separate from bu/hash.h so that C translation
 * units are never exposed to C++ system headers.  C++ callers that need to
 * use bu_h128_t as a key in std::unordered_map or std::unordered_set should
 * include this header (directly or via a C++ wrapper).
 *
 * **Two-level collision model**
 *
 * An unordered container uses its hasher in two completely separate steps:
 *
 *  1. Bucket placement (std::hash<bu_h128_t>) – maps a key to one of N
 *     buckets.  Multiple distinct keys can land in the same bucket; the
 *     container resolves them with a linear scan using operator==.  A
 *     bucket collision costs a little extra time but never produces a wrong
 *     answer.
 *
 *  2. Identity / equality test (operator==) – determines whether two keys
 *     are the same object.  This is where a false match would be a
 *     correctness bug (e.g., the -below cache treating two different paths
 *     as identical).
 *
 * std::hash<bu_h128_t> folds the 128-bit fingerprint down to std::size_t
 * (typically 64 bits), so bucket-placement collision probability is back up
 * near N^2 / 2^65 – that is the expected answer to "doesn't folding down
 * bump our chances back up?": yes, for bucket placement.  But operator==
 * still compares all 128 bits, so identity collision probability remains
 * N^2 / 2^129.  The worst a bucket collision can do is slow a single lookup
 * from O(1) amortised to O(chain-length); it cannot produce a wrong result.
 *
 * This is also why a plain folded 64-bit value cannot replace bu_h128_t as
 * the fingerprint: if the fingerprint itself were only 64 bits, two
 * different paths with the same 64-bit hash would be treated as identical by
 * operator== (correctness bug at N^2 / 2^65).  The whole point of the
 * 128-bit type is that identity comparison uses the full width, while the
 * std::hash bucket function is free to compress to whatever size the
 * container needs.
 *
 * Example:
 * @code
 *   #include "bu/hash_cxx.h"
 *   std::unordered_set<bu_h128_t> seen;
 *   bu_h128_t fp = bu_data_hash128(data, len);
 *   seen.insert(fp);
 * @endcode
 */

#ifndef BU_HASH_CXX_H
#define BU_HASH_CXX_H

/* This header is C++ only. */
#ifndef __cplusplus
#  error "bu/hash_cxx.h is a C++-only header"
#endif

#include "bu/hash.h"
#include <cstddef>
#include <functional>

/**
 * Identity equality for bu_h128_t.
 *
 * Compares all 128 bits.  This is the correctness gate used by
 * std::unordered_map / std::unordered_set after a bucket lookup: two keys
 * are the same if and only if both 64-bit words match.  The collision
 * probability at this level is N^2 / 2^129 (see file-level comment).
 */
inline bool operator==(const bu_h128_t &a, const bu_h128_t &b)
{
    return a.w[0] == b.w[0] && a.w[1] == b.w[1];
}

namespace std {
    /**
     * Bucket-placement hasher for bu_h128_t (performance only, not identity).
     *
     * This function maps a 128-bit fingerprint to a single std::size_t bucket
     * index.  It is used exclusively for bucket selection; the container
     * resolves actual equality with operator== (above), which inspects all
     * 128 bits.
     *
     * Folding to std::size_t increases *bucket* collision probability back to
     * roughly N^2 / 2^65 on 64-bit platforms – that is intentional and
     * harmless: a bucket collision only lengthens one chain by one node, it
     * never causes a false identity match.  See the file-level comment for
     * a full explanation of the two-level collision model.
     *
     * Implementation: XOR the two 64-bit halves (both have XXH3's excellent
     * avalanche properties), then fold to size_t width for 32-bit platforms.
     */
    template<>
    struct hash<bu_h128_t> {
	size_t operator()(const bu_h128_t &h) const noexcept {
	    /* XOR the two 64-bit halves, then fold to size_t width.
	     * On 64-bit platforms this is a no-op truncation.
	     * On 32-bit platforms the extra shift folds the upper 32
	     * bits of the XOR result into the lower 32 before the cast. */
	    uint64_t v = h.w[0] ^ h.w[1];
	    if (sizeof(size_t) < sizeof(uint64_t))
		v ^= (v >> 32);
	    return (size_t)v;
	}
    };
} /* namespace std */

#endif  /* BU_HASH_CXX_H */

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
