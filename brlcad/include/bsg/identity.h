/*                    I D E N T I T Y . H
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
/** @addtogroup libbsg
 *
 * @brief
 * BSG identity primitives (Phase 2A/2B/follow-up): ID structs,
 * init/equality/hash helpers, and side-car storage APIs for per-node identity
 * and revisions.
 */
/** @{ */
/* @file bsg/identity.h */

#ifndef BSG_IDENTITY_H
#define BSG_IDENTITY_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

enum bsg_source_kind {
    BSG_SOURCE_UNKNOWN = 0,
    BSG_SOURCE_DB_OBJECT,
    BSG_SOURCE_VIEW_OBJECT,
    BSG_SOURCE_OVERLAY,
    BSG_SOURCE_IMAGE,
    BSG_SOURCE_GENERATED,
    BSG_SOURCE_ANONYMOUS
};

struct bsg_node_id {
    uint64_t value;
};

struct bsg_part_id {
    uint64_t value;
};

struct bsg_instance_id {
    uint64_t value;
};

struct bsg_identity {
    struct bsg_node_id node_id;
    struct bsg_part_id part_id;
    struct bsg_instance_id instance_id;
    enum bsg_source_kind source_kind;
};

enum bsg_node_revision_kind {
    BSG_NODE_REV_MATERIAL = 0,
    BSG_NODE_REV_PAYLOAD,
    BSG_NODE_REV_TRANSFORM,
    BSG_NODE_REV_BOUNDS,
    BSG_NODE_REV_COUNT
};

BSG_EXPORT extern void
bsg_node_id_init(struct bsg_node_id *id);

BSG_EXPORT extern void
bsg_part_id_init(struct bsg_part_id *id);

BSG_EXPORT extern void
bsg_instance_id_init(struct bsg_instance_id *id);

BSG_EXPORT extern void
bsg_identity_init(struct bsg_identity *id);

BSG_EXPORT extern int
bsg_node_id_equal(const struct bsg_node_id *a, const struct bsg_node_id *b);

BSG_EXPORT extern int
bsg_part_id_equal(const struct bsg_part_id *a, const struct bsg_part_id *b);

BSG_EXPORT extern int
bsg_instance_id_equal(const struct bsg_instance_id *a, const struct bsg_instance_id *b);

BSG_EXPORT extern uint64_t
bsg_node_id_hash(const struct bsg_node_id *id);

BSG_EXPORT extern uint64_t
bsg_part_id_hash(const struct bsg_part_id *id);

BSG_EXPORT extern uint64_t
bsg_instance_id_hash(const struct bsg_instance_id *id);

/**
 * Phase 2B: side-car identity storage.
 *
 * These functions maintain a process-global map keyed by node pointer.
 * Storage is allocated on first use.  Note: the map is not thread-safe in
 * this initial implementation; external serialization is required when
 * multiple threads mutate the same node concurrently.
 *
 * Long-term plan: identity will move into real bsg_node storage once an
 * ABI-compatible slot is available.
 */

/**
 * Retrieve the identity stored for @p n.  Writes into @p out and returns 1
 * if an entry exists.  Returns 0 and leaves @p out unchanged if no identity
 * has been stored for @p n.  Either @p n or @p out may be NULL (returns 0).
 */
BSG_EXPORT extern int
bsg_node_identity_get(const bsg_node *n, struct bsg_identity *out);

/**
 * Store or replace the identity for @p n.  If @p id is NULL the stored entry
 * is removed (same as calling bsg_node_identity_clear).
 */
BSG_EXPORT extern void
bsg_node_identity_set(bsg_node *n, const struct bsg_identity *id);

/**
 * Remove any stored identity for @p n and free associated memory.
 * Silently ignores NULL or nodes with no stored identity.
 */
BSG_EXPORT extern void
bsg_node_identity_clear(bsg_node *n);

/**
 * Initialise @p id for a node whose draw-tree position is described by the
 * path string @p path_str.  The node_id.value is derived by hashing @p
 * path_str so that the same string always produces the same ID within a
 * session.  @p kind is stored directly as source_kind.  @p path_str may be
 * NULL (a zero node_id is produced; source_kind is still set).  Other ID
 * fields are zeroed.
 */
BSG_EXPORT extern void
bsg_identity_from_path_str(struct bsg_identity *id,
			   const char *path_str,
			   enum bsg_source_kind kind);

/**
 * Phase 2 follow-up: revision counters in the identity side-car.
 *
 * Return the current revision counter for @p n and @p rev_kind.  Returns 0
 * for NULL node, invalid revision kind, or nodes with no side-car state.
 */
BSG_EXPORT extern uint64_t
bsg_node_revision(const bsg_node *n, int rev_kind);

/**
 * Increment and return the revision counter for @p n and @p rev_kind.
 * Returns 0 for NULL node or invalid revision kind.
 */
BSG_EXPORT extern uint64_t
bsg_node_bump_revision(bsg_node *n, int rev_kind);

__END_DECLS

#endif /* BSG_IDENTITY_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
