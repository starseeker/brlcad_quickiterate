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

#include "bu/malloc.h"
#include "bu/vls.h"
#include "bv/util.h"
#include "bsg/identity.h"

#include "./bsg_private.h"


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
/* Phase 10D: identity and revision storage via bsg_node_core           */
/*                                                                      */
/* The global _bsg_id_map hash table (Phase 2B) has been replaced by   */
/* inline fields in struct bsg_node_core, which is embedded directly in */
/* bv_scene_obj.  This eliminates the per-call hash-map lookup while   */
/* providing deterministic lifetime: the core is zeroed by              */
/* bv_obj_reset() so identity data is automatically released when the   */
/* node is recycled.                                                    */
/* ------------------------------------------------------------------ */

/*
 * Compile-time guard: if new revision kinds are added to bsg_identity.h,
 * BSG_NODE_REV_MAX in bv/defines.h must be incremented too.
 */
typedef char _bsg_rev_max_check[
    (BSG_NODE_REV_COUNT <= BSG_NODE_REV_MAX) ? 1 : -1];


int
bsg_node_identity_get(const bsg_node *n, struct bsg_identity *out)
{
    const struct bv_scene_obj *s;
    const struct bsg_node_core *core;

    if (!n || !out)
	return 0;

    s = (const struct bv_scene_obj *)n;
    core = &s->bsg_core;

    if (core->bsg_magic != BSG_NODE_CORE_MAGIC || !core->have_identity)
	return 0;

    out->node_id.value      = core->identity_node_id;
    out->part_id.value      = core->identity_part_id;
    out->instance_id.value  = core->identity_instance_id;
    out->source_kind        = (enum bsg_source_kind)core->identity_source_kind;
    return 1;
}


void
bsg_node_identity_set(bsg_node *n, const struct bsg_identity *id)
{
    struct bsg_node_core *core;

    if (!n)
	return;

    if (!id) {
	bsg_node_identity_clear(n);
	return;
    }

    core = _bsg_core_ensure(n);
    if (!core)
	return;

    core->identity_node_id      = id->node_id.value;
    core->identity_part_id      = id->part_id.value;
    core->identity_instance_id  = id->instance_id.value;
    core->identity_source_kind  = (int)id->source_kind;
    core->have_identity         = 1;
}


void
bsg_node_identity_clear(bsg_node *n)
{
    struct bv_scene_obj *s;

    if (!n)
	return;

    s = (struct bv_scene_obj *)n;
    if (s->bsg_core.bsg_magic != BSG_NODE_CORE_MAGIC)
	return;

    s->bsg_core.have_identity         = 0;
    s->bsg_core.identity_node_id      = 0;
    s->bsg_core.identity_part_id      = 0;
    s->bsg_core.identity_instance_id  = 0;
    s->bsg_core.identity_source_kind  = 0;
}


uint64_t
bsg_node_revision(const bsg_node *n, int rev_kind)
{
    const struct bv_scene_obj *s;

    if (!n)
	return 0;
    if (rev_kind < 0 || rev_kind >= BSG_NODE_REV_COUNT)
	return 0;

    s = (const struct bv_scene_obj *)n;
    if (s->bsg_core.bsg_magic != BSG_NODE_CORE_MAGIC)
	return 0;

    return s->bsg_core.revisions[rev_kind];
}


uint64_t
bsg_node_bump_revision(bsg_node *n, int rev_kind)
{
    struct bsg_node_core *core;

    if (!n)
	return 0;
    if (rev_kind < 0 || rev_kind >= BSG_NODE_REV_COUNT)
	return 0;

    core = _bsg_core_ensure(n);
    if (!core)
	return 0;

    core->revisions[rev_kind]++;
    return core->revisions[rev_kind];
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

static int _bsg_view_obj_derivation_enabled = 0;

static void
_bsg_view_obj_identity_hook(struct bv_scene_obj *obj,
			    struct bview *v,
			    struct bv_scene_obj *scope,
			    const char *name,
			    int local,
			    int name_ordinal)
{
    struct bsg_identity id;
    struct bu_vls path = BU_VLS_INIT_ZERO;
    const char *scope_name = "_view_obj_scope";
    const char *view_name = "_view";
    const char *obj_name = (name && *name) ? name : "_view_obj";

    if (!obj || !scope)
	return;

    if (BU_VLS_IS_INITIALIZED(&scope->s_name) && bu_vls_strlen(&scope->s_name))
	scope_name = bu_vls_cstr(&scope->s_name);
    if (local && v && BU_VLS_IS_INITIALIZED(&v->gv_name) && bu_vls_strlen(&v->gv_name))
	view_name = bu_vls_cstr(&v->gv_name);

    if (local) {
	bu_vls_sprintf(&path, "%s/%s/%s#%d",
		       view_name, scope_name, obj_name, name_ordinal);
    } else {
	bu_vls_sprintf(&path, "%s/%s#%d",
		       scope_name, obj_name, name_ordinal);
    }

    bsg_identity_from_path_str(&id, bu_vls_cstr(&path), BSG_SOURCE_VIEW_OBJECT);
    bsg_node_identity_set((bsg_node *)obj, &id);
    bu_vls_free(&path);
}

void
bsg_identity_enable_view_obj_derivation(void)
{
    if (_bsg_view_obj_derivation_enabled)
	return;

    bv_view_obj_identity_hook_set(_bsg_view_obj_identity_hook);
    _bsg_view_obj_derivation_enabled = 1;
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
