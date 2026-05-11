/*                    I D E N T I T Y . C
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
/** @file libbsg/identity.c
 *
 * Phase 2A: ID structs and init/equality/hash helpers.
 */

#include "common.h"

#include "bsg/identity.h"


static uint64_t
_bsg_hash_u64(uint64_t v)
{
    /* MurmurHash3 finalizer */
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33;
    v *= 0xc4ceb9fe1a85ec53ULL;
    v ^= v >> 33;
    return v;
}


void
bsg_node_id_init(struct bsg_node_id *id)
{
    if (!id)
	return;
    id->value = 0;
}


void
bsg_part_id_init(struct bsg_part_id *id)
{
    if (!id)
	return;
    id->value = 0;
}


void
bsg_instance_id_init(struct bsg_instance_id *id)
{
    if (!id)
	return;
    id->value = 0;
}


void
bsg_identity_init(struct bsg_identity *id)
{
    if (!id)
	return;

    bsg_node_id_init(&id->node_id);
    bsg_part_id_init(&id->part_id);
    bsg_instance_id_init(&id->instance_id);
    id->source_kind = BSG_SOURCE_UNKNOWN;
}


int
bsg_node_id_equal(const struct bsg_node_id *a, const struct bsg_node_id *b)
{
    if (!a && !b)
	return 1;
    if (!a || !b)
	return 0;
    return (a->value == b->value) ? 1 : 0;
}


int
bsg_part_id_equal(const struct bsg_part_id *a, const struct bsg_part_id *b)
{
    if (!a && !b)
	return 1;
    if (!a || !b)
	return 0;
    return (a->value == b->value) ? 1 : 0;
}


int
bsg_instance_id_equal(const struct bsg_instance_id *a, const struct bsg_instance_id *b)
{
    if (!a && !b)
	return 1;
    if (!a || !b)
	return 0;
    return (a->value == b->value) ? 1 : 0;
}


uint64_t
bsg_node_id_hash(const struct bsg_node_id *id)
{
    if (!id)
	return 0;
    return _bsg_hash_u64(id->value);
}


uint64_t
bsg_part_id_hash(const struct bsg_part_id *id)
{
    if (!id)
	return 0;
    return _bsg_hash_u64(id->value);
}


uint64_t
bsg_instance_id_hash(const struct bsg_instance_id *id)
{
    if (!id)
	return 0;
    return _bsg_hash_u64(id->value);
}


/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
