/*                    S E L E C T I O N . C
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
/** @file libbsg/selection.c
 *
 * Phase 6A/6B: BSG selection set API and scene-root named-set storage.
 *
 * Selection sets are stored in a global hash map keyed by root node
 * pointer (matching the material/identity side-car pattern).  Each root
 * entry holds a linked list of named bsg_selection_set instances.
 *
 * The "active" selection set tracks whole-object illumination (s_iflag
 * UP/DOWN) for the GED compatibility layer.  The "edit" set marks
 * objects currently under an edit operation.
 */

#include "common.h"

#include <string.h>

#include "bu/hash.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "bv/defines.h"
#include "bsg/defines.h"
#include "bsg/field.h"
#include "bsg/selection.h"
#include "bsg/visit.h"


/* ---------------------------------------------------------------------- */
/* Internal linked-list cell                                                */
/* ---------------------------------------------------------------------- */

struct _bsg_sel_entry_cell {
    struct bsg_selection_entry  entry;
    struct _bsg_sel_entry_cell *next;
};

/* ---------------------------------------------------------------------- */
/* Root-level side-car storage                                              */
/* ---------------------------------------------------------------------- */

struct _bsg_sel_set_cell {
    struct bsg_selection_set  *set;
    struct _bsg_sel_set_cell  *next;
};

struct _bsg_root_sels {
    struct _bsg_sel_set_cell *head;
};

static bu_hash_tbl *_bsg_sel_map = NULL;

static void
_sel_map_ensure(void)
{
    if (!_bsg_sel_map)
	_bsg_sel_map = bu_hash_create(64);
}

static struct _bsg_root_sels *
_bsg_root_sels_get(const bsg_node *root)
{
    if (!root || !_bsg_sel_map)
	return NULL;
    return (struct _bsg_root_sels *)bu_hash_get(
	_bsg_sel_map, (const uint8_t *)&root, sizeof(root));
}

static struct _bsg_root_sels *
_bsg_root_sels_get_or_create(const bsg_node *root)
{
    struct _bsg_root_sels *rs = NULL;
    if (!root)
	return NULL;

    _sel_map_ensure();
    rs = _bsg_root_sels_get(root);
    if (rs)
	return rs;

    BU_ALLOC(rs, struct _bsg_root_sels);
    rs->head = NULL;
    bu_hash_set(_bsg_sel_map, (const uint8_t *)&root, sizeof(root), rs);
    return rs;
}


/* ---------------------------------------------------------------------- */
/* Selection set lifecycle                                                  */
/* ---------------------------------------------------------------------- */

struct bsg_selection_set *
bsg_selection_set_create(const char *name)
{
    struct bsg_selection_set *ss;
    BU_ALLOC(ss, struct bsg_selection_set);
    ss->name  = bu_strdup(name ? name : "");
    ss->count = 0;
    ss->_priv = NULL;
    return ss;
}


static void
_entry_cell_free(struct _bsg_sel_entry_cell *c)
{
    if (!c)
	return;
    if (c->entry.src_path) {
	bu_free(c->entry.src_path, "bsg_sel entry src_path");
	c->entry.src_path = NULL;
    }
    bu_free(c, "bsg_sel entry cell");
}


void
bsg_selection_set_destroy(struct bsg_selection_set *ss)
{
    if (!ss)
	return;

    bsg_selection_clear(ss);

    if (ss->name) {
	bu_free(ss->name, "bsg_sel set name");
	ss->name = NULL;
    }
    bu_free(ss, "bsg_sel set");
}


void
bsg_selection_clear(struct bsg_selection_set *ss)
{
    if (!ss)
	return;

    struct _bsg_sel_entry_cell *c = (struct _bsg_sel_entry_cell *)ss->_priv;
    while (c) {
	struct _bsg_sel_entry_cell *next = c->next;
	_entry_cell_free(c);
	c = next;
    }
    ss->_priv  = NULL;
    ss->count  = 0;
}


/* ---------------------------------------------------------------------- */
/* Entry operations                                                         */
/* ---------------------------------------------------------------------- */

int
bsg_selection_add(struct bsg_selection_set *ss,
		  const struct bsg_selection_entry *e)
{
    if (!ss || !e)
	return 0;

    /* Duplicate-guard: skip if an entry with the same node pointer
     * (and same kind when node is non-NULL) already exists. */
    struct _bsg_sel_entry_cell *c = (struct _bsg_sel_entry_cell *)ss->_priv;
    while (c) {
	if (e->node && c->entry.node == e->node && c->entry.kind == e->kind)
	    return 0;
	c = c->next;
    }

    /* Create a new cell and copy the entry. */
    BU_ALLOC(c, struct _bsg_sel_entry_cell);
    c->entry        = *e;
    c->entry.src_path = (e->src_path) ? bu_strdup(e->src_path) : NULL;

    /* Prepend to list. */
    c->next   = (struct _bsg_sel_entry_cell *)ss->_priv;
    ss->_priv = c;
    ss->count++;

    /* Notify sensors watching this node. */
    if (e->node)
	bsg_node_field_touch(e->node, BSG_FIELD_SELECTION);

    return 1;
}


int
bsg_selection_remove(struct bsg_selection_set *ss, const bsg_node *node)
{
    if (!ss || !node)
	return 0;

    struct _bsg_sel_entry_cell **pp =
	(struct _bsg_sel_entry_cell **)&ss->_priv;
    while (*pp) {
	if ((*pp)->entry.node == node) {
	    struct _bsg_sel_entry_cell *del = *pp;
	    *pp = del->next;
	    _entry_cell_free(del);
	    ss->count--;
	    bsg_node_field_touch((bsg_node *)(uintptr_t)node,
				BSG_FIELD_SELECTION);
	    return 1;
	}
	pp = &(*pp)->next;
    }
    return 0;
}


int
bsg_selection_contains(const struct bsg_selection_set *ss,
		       const bsg_node *node)
{
    if (!ss || !node)
	return 0;

    const struct _bsg_sel_entry_cell *c =
	(const struct _bsg_sel_entry_cell *)ss->_priv;
    while (c) {
	if (c->entry.node == node)
	    return 1;
	c = c->next;
    }
    return 0;
}


size_t
bsg_selection_count(const struct bsg_selection_set *ss)
{
    if (!ss)
	return 0;
    return ss->count;
}


void
bsg_selection_visit(const struct bsg_selection_set *ss,
		    bsg_selection_visit_fn cb,
		    void *data)
{
    if (!ss || !cb)
	return;

    const struct _bsg_sel_entry_cell *c =
	(const struct _bsg_sel_entry_cell *)ss->_priv;
    while (c) {
	if (!cb(&c->entry, data))
	    break;
	c = c->next;
    }
}


void
bsg_selection_apply_policy(struct bsg_selection_set *ss,
			   const struct bsg_selection_entry *e,
			   enum bsg_selection_policy policy)
{
    if (!ss || !e)
	return;

    switch (policy) {
	case BSG_SELECTION_POLICY_SINGLE:
	case BSG_SELECTION_POLICY_REPLACE:
	    bsg_selection_clear(ss);
	    bsg_selection_add(ss, e);
	    break;

	case BSG_SELECTION_POLICY_APPEND:
	    bsg_selection_add(ss, e);
	    break;

	case BSG_SELECTION_POLICY_REMOVE:
	    bsg_selection_remove(ss, e->node);
	    break;

	case BSG_SELECTION_POLICY_TOGGLE:
	    if (bsg_selection_contains(ss, e->node))
		bsg_selection_remove(ss, e->node);
	    else
		bsg_selection_add(ss, e);
	    break;
    }
}


/* ---------------------------------------------------------------------- */
/* Scene-root named-set registry (Phase 6B)                                */
/* ---------------------------------------------------------------------- */

struct bsg_selection_set *
bsg_scene_selection_get(bsg_node *root, const char *name, int create)
{
    if (!root || !name)
	return NULL;

    struct _bsg_root_sels *rs = _bsg_root_sels_get(root);

    /* Search existing list. */
    if (rs) {
	struct _bsg_sel_set_cell *sc = rs->head;
	while (sc) {
	    if (BU_STR_EQUAL(sc->set->name, name))
		return sc->set;
	    sc = sc->next;
	}
    }

    if (!create)
	return NULL;

    /* Create the set and register it. */
    rs = _bsg_root_sels_get_or_create(root);
    if (!rs)
	return NULL;

    struct bsg_selection_set *ss = bsg_selection_set_create(name);
    if (!ss)
	return NULL;

    struct _bsg_sel_set_cell *nc;
    BU_ALLOC(nc, struct _bsg_sel_set_cell);
    nc->set  = ss;
    nc->next = rs->head;
    rs->head = nc;

    return ss;
}


void
bsg_node_set_selected(bsg_node *root, bsg_node *node,
		      const char *set_name, int selected)
{
    if (!root || !node)
	return;

    struct bsg_selection_set *ss =
	bsg_scene_selection_get(root, set_name ? set_name : "active",
				selected ? 1 : 0);

    if (!ss)
	return;  /* non-create path: nothing to remove from a missing set */

    if (selected) {
	struct bsg_selection_entry e;
	memset(&e, 0, sizeof(e));
	e.node = node;
	e.kind = BSG_SELECTION_NODE;
	bsg_selection_add(ss, &e);
    } else {
	bsg_selection_remove(ss, node);
    }
}


int
bsg_node_is_selected(const bsg_node *root, const bsg_node *node,
		     const char *set_name)
{
    if (!root || !node)
	return 0;

    /* Cast away const for the get call — no creation, so it is safe. */
    struct bsg_selection_set *ss =
	bsg_scene_selection_get((bsg_node *)(uintptr_t)root,
				set_name ? set_name : "active", 0);
    if (!ss)
	return 0;

    return bsg_selection_contains(ss, node);
}


/* ---------------------------------------------------------------------- */
/* Compatibility sync helpers                                               */
/* ---------------------------------------------------------------------- */

/*
 * Callback for bsg_selection_sync_illum_flags: sets s_iflag based on
 * membership in the "active" selection set.
 */
struct _sync_illum_ctx {
    struct bsg_selection_set *active_ss;
};

static int
_sync_illum_cb(bsg_node *n, void *data)
{
    struct _sync_illum_ctx *ctx = (struct _sync_illum_ctx *)data;
    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    if (!s)
	return 1;

    int in_active = bsg_selection_contains(ctx->active_ss, n);
    s->s_iflag = in_active ? UP : DOWN;
    return 1; /* continue */
}

void
bsg_selection_sync_illum_flags(bsg_node *root)
{
    if (!root)
	return;

    struct bsg_selection_set *ss =
	bsg_scene_selection_get(root, "active", 0);
    if (!ss) {
	/* No "active" set: clear all flags. */
	struct bv_scene_obj *r = (struct bv_scene_obj *)root;
	for (size_t i = 0; i < BU_PTBL_LEN(&r->children); i++) {
	    struct bv_scene_obj *c =
		(struct bv_scene_obj *)BU_PTBL_GET(&r->children, i);
	    if (c)
		c->s_iflag = DOWN;
	}
	return;
    }

    struct _sync_illum_ctx ctx;
    ctx.active_ss = ss;
    bsg_visit(root, 0, _sync_illum_cb, &ctx);
}


/*
 * Callback for bsg_selection_from_illum_flags: collects nodes with
 * s_iflag == UP into the "active" selection set.
 */
struct _from_illum_ctx {
    bsg_node                 *root;
    struct bsg_selection_set *active_ss;
};

static int
_from_illum_cb(bsg_node *n, void *data)
{
    struct _from_illum_ctx *ctx = (struct _from_illum_ctx *)data;
    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    if (!s)
	return 1;

    if (s->s_iflag == UP) {
	struct bsg_selection_entry e;
	memset(&e, 0, sizeof(e));
	e.node = n;
	e.kind = BSG_SELECTION_NODE;
	bsg_selection_add(ctx->active_ss, &e);
    }
    return 1; /* continue */
}

void
bsg_selection_from_illum_flags(bsg_node *root)
{
    if (!root)
	return;

    struct bsg_selection_set *ss =
	bsg_scene_selection_get(root, "active", 1);
    if (!ss)
	return;

    bsg_selection_clear(ss);

    struct _from_illum_ctx ctx;
    ctx.root      = root;
    ctx.active_ss = ss;
    bsg_visit(root, 0, _from_illum_cb, &ctx);
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
