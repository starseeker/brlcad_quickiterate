/*                    O V E R L A Y . C
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
/** @file libbsg/overlay.c
 *
 * Phase 7 Step 13 (drawing_stack_modernization):
 * Pure-BSG overlay group helpers — no dependency on GED types.
 *
 * Extracted from the file-private helpers _sg_find_overlay_group,
 * _sg_overlay_root, and _sg_erase_overlay_by_name in
 * src/libged/bsg_view_obj.c (now bsg_ged_draw.c).  Moving them here
 * removes the last overlay-management code from the GED layer that carries
 * no GED-specific logic.
 *
 * Dependencies: libbv (bv/defines.h backing storage), bu (bu_ptbl, bu_str).
 * No librt, no libged.
 */

#include "common.h"

#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bv/defines.h"

#include "bsg/defines.h"
#include "bsg/draw_set.h"
#include "bsg/node.h"
#include "bsg/overlay.h"

bsg_node *
bsg_find_overlay_group(bsg_node *draw_root)
{
    if (!draw_root)
	return NULL;

    size_t n = bsg_node_child_count(draw_root);
    for (size_t i = 0; i < n; i++) {
	bsg_node *g = bsg_node_child(draw_root, i);
	const char *name = bsg_node_name(g);
	if (name && BU_STR_EQUAL("_overlays", name))
	    return g;
    }
    return NULL;
}


bsg_node *
bsg_ensure_overlay_group(bsg_node *draw_root, struct bview *v)
{
    bsg_node *existing = bsg_find_overlay_group(draw_root);
    if (existing)
	return existing;

    if (!v)
	return NULL;

    struct bv_scene_obj *ov = (struct bv_scene_obj *)bsg_node_create_child(v, BSG_NODE_GROUP);
    if (!ov)
	return NULL;

    bsg_node_uptr_set((bsg_node *)ov, 0, NULL);
    bsg_node_set_name((bsg_node *)ov, "_overlays");
    bsg_node_add_child(draw_root, (bsg_node *)ov);

    return (bsg_node *)ov;
}


void
bsg_erase_overlay_by_name(bsg_node *draw_root, const char *name)
{
    struct bv_scene_obj *root = (struct bv_scene_obj *)draw_root;
    if (!root)
	return;

    struct bv_scene_obj *ov =
	(struct bv_scene_obj *)bsg_find_overlay_group(draw_root);
    if (!ov)
	return;

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&ov->bsg.bsg_children); i++)
	bu_ptbl_ins(&snap, BU_PTBL_GET(&ov->bsg.bsg_children, i));

    for (size_t i = 0; i < BU_PTBL_LEN(&snap); i++) {
	struct bv_scene_obj *sp =
	    (struct bv_scene_obj *)BU_PTBL_GET(&snap, i);
	if (!BU_STR_EQUAL(name, bu_vls_cstr(&sp->bsg.bsg_name)))
	    continue;

	bsg_node_remove_child((bsg_node *)ov, (bsg_node *)sp);
	/* bump rev via root (sp->bsg.bsg_parent now being cleared) */
	bsg_bump_rev_node(draw_root);
	bsg_node_destroy((bsg_node *)sp);
    }
    bu_ptbl_free(&snap);

    /* Remove empty _overlays group from root */
    if (BU_PTBL_LEN(&ov->bsg.bsg_children) == 0) {
	bsg_node_remove_child((bsg_node *)root, (bsg_node *)ov);
	bsg_node_destroy((bsg_node *)ov);
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
