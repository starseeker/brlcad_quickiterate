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
 * Equality comparison for bu_h128_t.
 * Required by std::unordered_map / std::unordered_set key operations.
 */
inline bool operator==(const bu_h128_t &a, const bu_h128_t &b)
{
    return a.w[0] == b.w[0] && a.w[1] == b.w[1];
}

namespace std {
    /**
     * std::hash specialization for bu_h128_t.
     *
     * Reduces the 128-bit fingerprint to a single std::size_t bucket index.
     * Both halves already have excellent distribution from XXH3-128; XOR-
     * folding them gives a compact result that distinguishes keys differing
     * in either half.
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
