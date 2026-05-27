/*                      A C T I O N . C
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
/** @file libbsg/action.c
 *
 * Phase 5: BSG action framework — typed visitor-based tree traversals.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "vmath.h"

#include "bsg/defines.h"
#include "bsg/visit.h"
#include "bsg/vlist.h"
#include "bsg/action.h"

/* ------------------------------------------------------------------ */
/* BBOX action                                                          */
/* ------------------------------------------------------------------ */

struct bbox_state {
    point_t bmin;
    point_t bmax;
    int     valid;
};

static int
bbox_cb(bsg_node *node, void *userdata)
{
    struct bbox_state *st = (struct bbox_state *)userdata;

    /* Only accumulate leaf shape nodes that have a valid bounding box */
    if (!(node->s_type_flags & BSG_NODE_SHAPE))
	return 1;   /* continue traversal */

    if (!node->have_bbox)
	return 1;

    if (!st->valid) {
	VMOVE(st->bmin, node->bmin);
	VMOVE(st->bmax, node->bmax);
	st->valid = 1;
    } else {
	VMIN(st->bmin, node->bmin);
	VMAX(st->bmax, node->bmax);
    }
    return 1;
}


/* ------------------------------------------------------------------ */
/* COLLECT action                                                       */
/* ------------------------------------------------------------------ */

struct collect_state {
    unsigned long long mask;
    struct bu_ptbl    *nodes;
};

static int
collect_cb(bsg_node *node, void *userdata)
{
    struct collect_state *st = (struct collect_state *)userdata;

    if (st->mask == 0 || (node->s_type_flags & st->mask))
	bu_ptbl_ins_unique(st->nodes, (long *)node);

    return 1;   /* always continue */
}


/* ------------------------------------------------------------------ */
/* EXPORT action                                                        */
/* ------------------------------------------------------------------ */

struct export_state {
    struct bu_list *vhead;   /* destination vlist */
    struct bu_list *vlfree;
};

static int
export_cb(bsg_node *node, void *userdata)
{
    struct export_state *st = (struct export_state *)userdata;

    if (!(node->s_type_flags & BSG_NODE_SHAPE))
	return 1;

    if (BU_LIST_IS_EMPTY(&node->s_vlist))
	return 1;

    /* Copy all vlist entries from node->s_vlist into *vhead.
     * bsg_vlist_copy(vlists, dest, src) — first arg is the free-list pool */
    bsg_vlist_copy(st->vlfree, st->vhead, &node->s_vlist);

    return 1;
}


/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

struct bsg_action *
bsg_action_create(int kind)
{
    struct bsg_action *a;
    BU_ALLOC(a, struct bsg_action);
    memset(a, 0, sizeof(struct bsg_action));
    a->kind = kind;

    switch (kind) {
	case BSG_ACTION_COLLECT:
	    bu_ptbl_init(&a->result.nodes, 8, "bsg_action collect nodes");
	    break;
	case BSG_ACTION_EXPORT:
	    BU_LIST_INIT(&a->result.vlist);
	    break;
	default:
	    break;
    }
    return a;
}


void
bsg_action_destroy(struct bsg_action *action, struct bu_list *vlfree)
{
    if (!action)
	return;

    switch (action->kind) {
	case BSG_ACTION_COLLECT:
	    bu_ptbl_free(&action->result.nodes);
	    break;
	case BSG_ACTION_EXPORT: {
	    /* BSG_FREE_VLIST appends the chain to the free-list head.
	     * If caller has no pool, use a local head and then
	     * individually free each entry. */
	    if (vlfree) {
		BSG_FREE_VLIST(vlfree, &action->result.vlist);
	    } else {
		struct bu_list local_free;
		struct bsg_vlist *vlp;
		BU_LIST_INIT(&local_free);
		BSG_FREE_VLIST(&local_free, &action->result.vlist);
		while (BU_LIST_WHILE(vlp, bsg_vlist, &local_free)) {
		    BU_LIST_DEQUEUE(&vlp->l);
		    bu_free(vlp, "bsg_vlist");
		}
	    }
	    BU_LIST_INIT(&action->result.vlist);
	    break;
	}
	default:
	    break;
    }
    bu_free(action, "bsg_action");
}


void
bsg_action_execute(struct bsg_action *action,
		   bsg_node          *root,
		   struct bu_list    *vlfree)
{
    if (!action || !root)
	return;

    switch (action->kind) {
	case BSG_ACTION_BBOX: {
	    struct bbox_state st;
	    VSETALL(st.bmin, 0.0);
	    VSETALL(st.bmax, 0.0);
	    st.valid = 0;
	    bsg_visit(root, BSG_NODE_SHAPE, bbox_cb, &st);
	    if (st.valid) {
		VMOVE(action->result.bbox.bmin, st.bmin);
		VMOVE(action->result.bbox.bmax, st.bmax);
		action->result.bbox.valid = 1;
	    }
	    break;
	}
	case BSG_ACTION_COLLECT: {
	    struct collect_state st;
	    st.mask  = action->params.collect_mask;
	    st.nodes = &action->result.nodes;
	    bsg_visit(root, 0, collect_cb, &st);
	    break;
	}
	case BSG_ACTION_EXPORT: {
	    struct export_state st;
	    st.vhead  = &action->result.vlist;
	    st.vlfree = vlfree;
	    bsg_visit(root, BSG_NODE_SHAPE, export_cb, &st);
	    break;
	}
	default:
	    break;
    }
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
