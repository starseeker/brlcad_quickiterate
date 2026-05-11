/*                       V I S I T . C
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
/** @file libbsg/visit.c
 *
 * BSG depth-first visitor.
 *
 * Replaces the legacy foreach_solid / foreach_group / BU_LIST_FOR
 * iteration patterns with a single parameterized DFS walk.  Each
 * caller supplies a type-flag predicate and an action callback.
 * Returning 0 from the callback stops traversal early.
 *
 * Phase 7 / Layer A (drawing-stack modernization, plan §B9).
 */

#include "common.h"

#include "bu/ptbl.h"
#include "bv/defines.h"
#include "bsg/defines.h"
#include "bsg/node.h"
#include "bsg/visit.h"


/* Internal DFS helper.  Returns 0 to request early stop. */
static int
_bsg_visit_dfs(bsg_node *node,
	       unsigned long long type_mask,
	       int (*cb)(bsg_node *, void *),
	       void *userdata)
{
    /* Invoke callback if this node matches the predicate */
    if (type_mask == 0 || (bsg_node_kind(node) & type_mask)) {
	if (!(*cb)(node, userdata))
	    return 0;
    }

    /* Recurse into children (ptbl, insertion order) */
    for (size_t i = 0; i < bsg_node_child_count(node); i++) {
	bsg_node *child = bsg_node_child(node, i);
	if (!_bsg_visit_dfs(child, type_mask, cb, userdata))
	    return 0;
    }

    return 1;
}


void
bsg_visit(bsg_node *root,
	  unsigned long long type_mask,
	  int (*cb)(bsg_node *node, void *userdata),
	  void *userdata)
{
    if (!root || !cb)
	return;
    _bsg_visit_dfs(root, type_mask, cb, userdata);
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
