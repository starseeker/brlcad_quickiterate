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
 * Dependencies: libbsg lifecycle helpers, bv/defines.h, bu (bu_ptbl, bu_str).
 * No librt, no libged.
 */

#include "common.h"

#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/vlist.h"

#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/draw_ctx.h"
#include "bsg/draw_set.h"
#include "bsg/overlay.h"
#include "bsg_private.h"


/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * FREE_BV_SCENE_OBJ: recycle a bv_scene_obj back into the free-pool
 * list @p fp and free its vlist data using the vlist pool @p vlf.
 *
 * Mirrors the identical macro in libbsg/draw_set.c.
 */
#define FREE_BV_SCENE_OBJ(p, fp, vlf) { \
    BU_LIST_APPEND(fp, &((p)->l)); \
    BV_FREE_VLIST(vlf, &((p)->s_vlist)); }


/* ------------------------------------------------------------------ */

bsg_node *
bsg_find_overlay_group(bsg_node *draw_root)
{
    struct bv_scene_obj *root = (struct bv_scene_obj *)draw_root;
    if (!root)
	return NULL;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	struct bv_scene_obj *g =
	    (struct bv_scene_obj *)BU_PTBL_GET(&root->children, i);
	if (BU_STR_EQUAL("_overlays", bu_vls_cstr(&g->s_name)))
	    return (bsg_node *)g;
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

    struct bv_scene_obj *root = (struct bv_scene_obj *)draw_root;

    struct bv_scene_obj *ov = bsg_obj_create(v, BV_CHILD_OBJS);
    if (!ov)
	return NULL;

    ov->s_type_flags = BSG_NODE_GROUP;
    ov->s_flag       = UP;
    ov->dp           = NULL;
    ov->parent       = draw_root;
    bu_vls_sprintf(&ov->s_name, "_overlays");
    bu_ptbl_ins(&root->children, (long *)ov);

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

    struct bsg_draw_ctx *ctx = _ctx_of_node(root);
    struct bv_scene_obj *fso = (ctx && ctx->fso) ? ctx->fso : NULL;

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&ov->children); i++)
	bu_ptbl_ins(&snap, BU_PTBL_GET(&ov->children, i));

    for (size_t i = 0; i < BU_PTBL_LEN(&snap); i++) {
	struct bv_scene_obj *sp =
	    (struct bv_scene_obj *)BU_PTBL_GET(&snap, i);
	if (!BU_STR_EQUAL(name, bu_vls_cstr(&sp->s_name)))
	    continue;

	/* Phase 11: release backend resources via the generic contract. */
	bsg_scene_obj_release_backend(sp);
	bu_ptbl_rm(&ov->children, (const long *)sp);
	/* bump rev via root (sp->parent now being cleared) */
	bsg_bump_rev_node(draw_root);
	sp->parent = NULL;
	struct bv_scene_obj *sfso = fso ? fso : sp->free_scene_obj;
	if (sfso)
	    FREE_BV_SCENE_OBJ(sp, &sfso->l, sp->vlfree);
    }
    bu_ptbl_free(&snap);

    /* Remove empty _overlays group from root */
    if (BU_PTBL_LEN(&ov->children) == 0) {
	bu_ptbl_rm(&root->children, (const long *)ov);
	ov->parent = NULL;
	struct bv_scene_obj *ofso = ov->free_scene_obj;
	if (ofso)
	    FREE_BV_SCENE_OBJ(ov, &ofso->l, ov->vlfree);
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
