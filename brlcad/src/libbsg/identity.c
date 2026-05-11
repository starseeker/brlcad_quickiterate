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
 * Phase 2A/2B/follow-up: ID structs, init/equality/hash helpers, and
 * side-car identity/revision storage for BSG nodes.
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
 * Global process-wide map: bsg_node * (as raw bytes) -> side-car state
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

struct _bsg_identity_sidecar {
    int have_identity;
    struct bsg_identity identity;
    uint64_t revisions[BSG_NODE_REV_COUNT];
};

static void
_bsg_id_map_ensure(void)
{
    if (!_bsg_id_map)
	_bsg_id_map = bu_hash_create(128);
}

static struct _bsg_identity_sidecar *
_bsg_sidecar_get(const bsg_node *n)
{
    if (!n || !_bsg_id_map)
	return NULL;

    return (struct _bsg_identity_sidecar *)bu_hash_get(_bsg_id_map,
	    (const uint8_t *)&n, sizeof(n));
}

static struct _bsg_identity_sidecar *
_bsg_sidecar_get_or_create(const bsg_node *n)
{
    struct _bsg_identity_sidecar *sc;

    if (!n)
	return NULL;

    _bsg_id_map_ensure();
    sc = _bsg_sidecar_get(n);
    if (sc)
	return sc;

    BU_ALLOC(sc, struct _bsg_identity_sidecar);
    sc->have_identity = 0;
    bsg_identity_init(&sc->identity);
    memset(sc->revisions, 0, sizeof(sc->revisions));
    bu_hash_set(_bsg_id_map, (const uint8_t *)&n, sizeof(n), sc);
    return sc;
}


int
bsg_node_identity_get(const bsg_node *n, struct bsg_identity *out)
{
    struct _bsg_identity_sidecar *sc;

    if (!n || !out)
	return 0;

    sc = _bsg_sidecar_get(n);
    if (!sc || !sc->have_identity)
	return 0;

    *out = sc->identity;
    return 1;
}


void
bsg_node_identity_set(bsg_node *n, const struct bsg_identity *id)
{
    struct _bsg_identity_sidecar *sc;

    if (!n)
	return;

    if (!id) {
	bsg_node_identity_clear(n);
	return;
    }

    sc = _bsg_sidecar_get_or_create(n);
    if (!sc)
	return;

    sc->identity = *id;
    sc->have_identity = 1;
}


void
bsg_node_identity_clear(bsg_node *n)
{
    struct _bsg_identity_sidecar *sc;

    if (!n || !_bsg_id_map)
	return;

    sc = _bsg_sidecar_get(n);
    if (!sc)
	return;

    bu_free(sc, "bsg_identity_sidecar");
    bu_hash_rm(_bsg_id_map, (const uint8_t *)&n, sizeof(n));
}


uint64_t
bsg_node_revision(const bsg_node *n, int rev_kind)
{
    struct _bsg_identity_sidecar *sc;

    if (!n)
	return 0;
    if (rev_kind < 0 || rev_kind >= BSG_NODE_REV_COUNT)
	return 0;

    sc = _bsg_sidecar_get(n);
    if (!sc)
	return 0;

    return sc->revisions[rev_kind];
}


uint64_t
bsg_node_bump_revision(bsg_node *n, int rev_kind)
{
    struct _bsg_identity_sidecar *sc;

    if (!n)
	return 0;
    if (rev_kind < 0 || rev_kind >= BSG_NODE_REV_COUNT)
	return 0;

    sc = _bsg_sidecar_get_or_create(n);
    if (!sc)
	return 0;

    sc->revisions[rev_kind]++;
    return sc->revisions[rev_kind];
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
