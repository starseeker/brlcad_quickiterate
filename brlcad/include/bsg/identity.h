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
 * BSG identity primitives (Phase 2A): ID structs and init/equality/hash helpers.
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
