/*                  S N A P _ A C T I O N . H
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @file bsg/snap_action.h */

#ifndef BSG_SNAP_ACTION_H
#define BSG_SNAP_ACTION_H

#include "common.h"
#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

typedef enum {
    BSG_SNAP_KIND_GRID            = 0x01ULL,
    BSG_SNAP_KIND_ENDPOINT        = 0x02ULL,
    BSG_SNAP_KIND_MIDPOINT        = 0x04ULL,
    BSG_SNAP_KIND_INTERSECTION    = 0x08ULL,
    BSG_SNAP_KIND_PERPENDICULAR   = 0x10ULL,
    BSG_SNAP_KIND_TANGENT         = 0x20ULL,
    BSG_SNAP_KIND_OVERLAY_HANDLE  = 0x40ULL
} bsg_snap_kind;

typedef unsigned long long bsg_snap_kind_mask;

struct bsg_snap_candidate {
    point_t sc_point;
    bsg_snap_kind sc_kind;
    fastf_t sc_distance;
    struct bsg_node *sc_node;
};

struct bsg_snap_result {
    struct bsg_snap_candidate *sr_candidates;
    size_t sr_cnt;
};

BSG_EXPORT extern void
bsg_snap_result_init(struct bsg_snap_result *out);

BSG_EXPORT extern void
bsg_snap_result_free(struct bsg_snap_result *out);

BSG_EXPORT extern int
bsg_snap_candidates(struct bsg_view *v, point_t sample, double tol,
		    bsg_snap_kind_mask kinds, struct bsg_snap_result *out);

__END_DECLS

#endif /* BSG_SNAP_ACTION_H */

