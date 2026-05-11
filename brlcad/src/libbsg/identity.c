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
 * Phase 2A/2B: ID structs, init/equality/hash helpers, and side-car
 * identity storage for BSG nodes.
 */

#include "common.h"

#include <string.h>

#include "bu/hash.h"
#include "bu/malloc.h"
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


/* ------------------------------------------------------------------ */
/* Phase 2B: side-car identity storage                                  */
/* ------------------------------------------------------------------ */

/*
 * Global process-wide map: bsg_node * (as raw bytes) -> struct bsg_identity *
 *
 * Lifetime: entries are explicitly removed by bsg_node_identity_clear().
 * If a node is destroyed without calling bsg_node_identity_clear() first
 * the entry will remain in the map but the pointer key becomes dangling.
 * This is acceptable for Phase 2B; future phases will hook into node
 * destruction to call bsg_node_identity_clear() automatically.
 *
 * Thread safety: the map is NOT thread-safe.  External serialisation is
 * required when the caller accesses the same node from multiple threads.
 */
static bu_hash_tbl *_bsg_id_map = NULL;

static void
_bsg_id_map_ensure(void)
{
    if (!_bsg_id_map)
	_bsg_id_map = bu_hash_create(128);
}


int
bsg_node_identity_get(const bsg_node *n, struct bsg_identity *out)
{
    if (!n || !out || !_bsg_id_map)
	return 0;

    void *val = bu_hash_get(_bsg_id_map,
			    (const uint8_t *)&n, sizeof(n));
    if (!val)
	return 0;

    *out = *(const struct bsg_identity *)val;
    return 1;
}


void
bsg_node_identity_set(bsg_node *n, const struct bsg_identity *id)
{
    if (!n)
	return;

    if (!id) {
	bsg_node_identity_clear(n);
	return;
    }

    _bsg_id_map_ensure();

    /* Check whether we already have an entry to update in-place */
    void *existing = bu_hash_get(_bsg_id_map,
				 (const uint8_t *)&n, sizeof(n));
    if (existing) {
	*(struct bsg_identity *)existing = *id;
    } else {
	struct bsg_identity *copy;
	BU_ALLOC(copy, struct bsg_identity);
	*copy = *id;
	bu_hash_set(_bsg_id_map, (const uint8_t *)&n, sizeof(n), copy);
    }
}


void
bsg_node_identity_clear(bsg_node *n)
{
    if (!n || !_bsg_id_map)
	return;

    void *val = bu_hash_get(_bsg_id_map,
			    (const uint8_t *)&n, sizeof(n));
    if (!val)
	return;

    bu_free(val, "bsg_identity");
    bu_hash_rm(_bsg_id_map, (const uint8_t *)&n, sizeof(n));
}


/* ------------------------------------------------------------------ */
/* Phase 2B: path-string derived identity                               */
/* ------------------------------------------------------------------ */

/*
 * FNV-1a 64-bit hash of a NUL-terminated string.  Chosen because it is
 * simple, deterministic across platforms, and fast for short path strings.
 */
static uint64_t
_fnv1a_64(const char *s)
{
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
	h ^= (uint8_t)(*s++);
	h *= 1099511628211ULL;
    }
    return h;
}


void
bsg_identity_from_path_str(struct bsg_identity *id,
			   const char *path_str,
			   enum bsg_source_kind kind)
{
    if (!id)
	return;

    bsg_identity_init(id);
    id->source_kind = kind;
    if (path_str && *path_str)
	id->node_id.value = _fnv1a_64(path_str);
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
