/*                  S E L E C T I O N . C
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
 * Phase 6: First-class selection model for BSG scene nodes.
 */

#include "common.h"

#include "bu/malloc.h"
#include "bu/ptbl.h"

#include "bsg/appearance.h"
#include "bsg/defines.h"
#include "bsg/selection.h"


struct bsg_selection {
    struct bu_ptbl nodes;
};


struct bsg_selection *
bsg_selection_create(void)
{
    struct bsg_selection *sel;
    BU_ALLOC(sel, struct bsg_selection);
    bu_ptbl_init(&sel->nodes, 8, "bsg_selection nodes");
    return sel;
}


void
bsg_selection_destroy(struct bsg_selection *sel)
{
    if (!sel)
	return;
    bu_ptbl_free(&sel->nodes);
    bu_free(sel, "bsg_selection");
}


void
bsg_selection_add(struct bsg_selection *sel, bsg_node *node)
{
    if (!sel || !node)
	return;
    bu_ptbl_ins_unique(&sel->nodes, (long *)node);
}


void
bsg_selection_remove(struct bsg_selection *sel, bsg_node *node)
{
    if (!sel || !node)
	return;
    bu_ptbl_rm(&sel->nodes, (long *)node);
}


void
bsg_selection_clear(struct bsg_selection *sel)
{
    if (!sel)
	return;
    bu_ptbl_reset(&sel->nodes);
}


int
bsg_selection_contains(const struct bsg_selection *sel,
		       const bsg_node              *node)
{
    if (!sel || !node)
	return 0;
    return (bu_ptbl_locate((struct bu_ptbl *)&sel->nodes,
			   (long *)node) >= 0);
}


size_t
bsg_selection_count(const struct bsg_selection *sel)
{
    if (!sel)
	return 0;
    return BU_PTBL_LEN(&sel->nodes);
}


const struct bu_ptbl *
bsg_selection_nodes(const struct bsg_selection *sel)
{
    if (!sel)
	return NULL;
    return &sel->nodes;
}


void
bsg_selection_highlight(struct bsg_selection *sel)
{
    if (!sel)
	return;
    for (size_t i = 0; i < BU_PTBL_LEN(&sel->nodes); i++) {
	bsg_node *n = (bsg_node *)BU_PTBL_GET(&sel->nodes, i);
	if (n) {
	    bsg_appearance_set_highlighted(n, 1);
	    n->s_iflag = UP;
	}
    }
}


void
bsg_selection_unhighlight(struct bsg_selection *sel)
{
    if (!sel)
	return;
    for (size_t i = 0; i < BU_PTBL_LEN(&sel->nodes); i++) {
	bsg_node *n = (bsg_node *)BU_PTBL_GET(&sel->nodes, i);
	if (n) {
	    bsg_appearance_set_highlighted(n, 0);
	    n->s_iflag = DOWN;
	}
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
