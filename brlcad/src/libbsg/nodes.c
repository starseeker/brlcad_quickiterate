/*                      N O D E S . C
 * BRL-CAD
 *
 * Copyright (c) 2024-2025 United States Government as represented by
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
/** @file libbsg/nodes.c
 *
 * @brief Node allocation, deallocation, and parent-child management.
 *
 * All memory used by libbsg nodes is managed through the BRL-CAD
 * bu_malloc / bu_free allocator so that the standard BRL-CAD memory
 * debugging tools continue to work.
 */

#include "common.h"

#include <string.h>   /* memset */

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/list.h"
#include "bu/vls.h"
#include "bn/mat.h"
#include "vmath.h"

/* Pull in only the libbsg public header — no bv.h / bsg/defines.h */
#include "libbsg/libbsg.h"

/* ====================================================================== *
 * Internal helpers                                                        *
 * ====================================================================== */

/**
 * @brief Initialise a bsg_node to its default (empty) state.
 *
 * Called after the raw memory for a node has been zeroed.  Sets the
 * xform to identity and initialises the children ptbl.
 */
static void
node_init(bsg_node *n, unsigned long long type_flags)
{
    /* The caller has already zeroed the memory; just set non-zero fields. */
    n->type_flags = type_flags;
    bu_ptbl_init(&n->children, 8, "bsg_node children");
    MAT_IDN(n->xform);
}

/**
 * @brief Release all resources owned by a node WITHOUT freeing the node
 *        itself.
 *
 * Used by both libbsg_node_free (standalone) and libbsg_shape_free.
 */
static void
node_destroy(bsg_node *n)
{
    if (!n)
	return;
    if (n->free_payload && n->payload)
	n->free_payload(n->payload);
    bu_ptbl_free(&n->children);
}

/* ====================================================================== *
 * Node lifecycle                                                          *
 * ====================================================================== */

bsg_node *
libbsg_node_alloc(unsigned long long type_flags)
{
    bsg_node *n;
    BU_GET(n, bsg_node);
    memset(n, 0, sizeof(*n));
    node_init(n, type_flags);
    return n;
}

void
libbsg_node_free(bsg_node *node, int recurse)
{
    if (!node)
	return;

    if (recurse) {
	size_t i;
	/* Free children depth-first.
	 * Shapes are embedded structs but also start with bsg_node, so
	 * we call libbsg_node_free recursively; shapes will detect that
	 * their BSG_NODE_SHAPE flag is set and delegate to
	 * libbsg_shape_free. */
	for (i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	    bsg_node *child = (bsg_node *)BU_PTBL_GET(&node->children, i);
	    if (child->type_flags & BSG_NODE_SHAPE)
		libbsg_shape_free((bsg_shape *)child);
	    else
		libbsg_node_free(child, 1);
	}
    }

    node_destroy(node);
    BU_PUT(node, bsg_node);
}

/* ====================================================================== *
 * Shape lifecycle                                                         *
 * ====================================================================== */

bsg_shape *
libbsg_shape_alloc(void)
{
    bsg_shape *s;
    BU_GET(s, bsg_shape);
    memset(s, 0, sizeof(*s));
    node_init(&s->node, BSG_NODE_SHAPE);
    BU_LIST_INIT(&s->vlist);
    bu_vls_init(&s->s_name);
    return s;
}

void
libbsg_shape_free(bsg_shape *s)
{
    if (!s)
	return;

    /* Free any vlists */
    if (BU_LIST_NON_EMPTY(&s->vlist)) {
	/* Walk the list and free each segment using BU_LIST_WHILE pattern */
	struct bu_list *lp;
	while (BU_LIST_WHILE(lp, bu_list, &s->vlist)) {
	    BU_LIST_DEQUEUE(lp);
	    bu_free(lp, "libbsg vlist segment");
	}
    }

    bu_vls_free(&s->s_name);
    node_destroy(&s->node);
    BU_PUT(s, bsg_shape);
}

/* ====================================================================== *
 * Parent–child management                                                 *
 * ====================================================================== */

void
libbsg_node_add_child(bsg_node *parent, bsg_node *child)
{
    if (!parent || !child)
	return;
    /* Avoid duplicates */
    if (bu_ptbl_locate(&parent->children, (long *)child) >= 0)
	return;
    bu_ptbl_ins(&parent->children, (long *)child);
    child->parent = parent;
}

void
libbsg_node_rm_child(bsg_node *parent, bsg_node *child)
{
    size_t removed;
    if (!parent || !child)
	return;
    removed = bu_ptbl_rm(&parent->children, (long *)child);
    if (removed > 0) {
	if (child->parent == parent)
	    child->parent = NULL;
    }
}

/* ====================================================================== *
 * Typed query                                                             *
 * ====================================================================== */

void
libbsg_find_by_type(bsg_node *root, unsigned long long mask,
		    struct bu_ptbl *result)
{
    size_t i;
    if (!root || !result)
	return;

    if (root->type_flags & mask)
	bu_ptbl_ins(result, (long *)root);

    for (i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	bsg_node *child = (bsg_node *)BU_PTBL_GET(&root->children, i);
	libbsg_find_by_type(child, mask, result);
    }
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
