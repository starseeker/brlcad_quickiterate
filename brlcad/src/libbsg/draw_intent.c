/*                  D R A W _ I N T E N T . C
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
/** @file libbsg/draw_intent.c
 *
 * Phase D2 (drawing_modernization):
 * Explicit draw-intent metadata attached to BSG scene groups.
 *
 * Implementation of bsg/draw_intent.h — lifecycle, node binding,
 * accessors, and scene-level actions.
 */

#include "common.h"

#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bsg/defines.h"
#include "bsg/draw_intent.h"


/* -----------------------------------------------------------------------
 * Lifecycle
 * ----------------------------------------------------------------------- */

struct bsg_draw_intent *
bsg_draw_intent_create(const char *path, bsg_draw_mode mode)
{
    struct bsg_draw_intent *di;
    BU_GET(di, struct bsg_draw_intent);
    bu_vls_init(&di->di_path);
    if (path)
	bu_vls_sprintf(&di->di_path, "%s", path);
    di->di_mode    = mode;
    di->di_lod     = BSG_LOD_AUTO;
    di->di_mixed   = 0;
    di->di_overlay = 0;
    return di;
}


struct bsg_draw_intent *
bsg_draw_intent_create_overlay(const char *name)
{
    struct bsg_draw_intent *di;
    BU_GET(di, struct bsg_draw_intent);
    bu_vls_init(&di->di_path);
    if (name)
	bu_vls_sprintf(&di->di_path, "%s", name);
    di->di_mode    = BSG_DRAW_MODE_WIRE;
    di->di_lod     = BSG_LOD_OFF;
    di->di_mixed   = 0;
    di->di_overlay = 1;
    return di;
}


void
bsg_draw_intent_free(struct bsg_draw_intent *di)
{
    if (!di)
	return;
    bu_vls_free(&di->di_path);
    BU_PUT(di, struct bsg_draw_intent);
}


/* -----------------------------------------------------------------------
 * Node binding
 * ----------------------------------------------------------------------- */

void
bsg_node_set_draw_intent(bsg_node *node, struct bsg_draw_intent *di)
{
    if (!node)
	return;
    if (node->di)
	bsg_draw_intent_free(node->di);
    node->di = di;
}


struct bsg_draw_intent *
bsg_node_get_draw_intent(const bsg_node *node)
{
    if (!node)
	return NULL;
    return node->di;
}


/* -----------------------------------------------------------------------
 * Accessors
 * ----------------------------------------------------------------------- */

const char *
bsg_draw_intent_path(const struct bsg_draw_intent *di)
{
    if (!di)
	return NULL;
    return bu_vls_cstr(&di->di_path);
}


void
bsg_draw_intent_set_path(struct bsg_draw_intent *di, const char *path)
{
    if (!di)
	return;
    bu_vls_trunc(&di->di_path, 0);
    if (path)
	bu_vls_sprintf(&di->di_path, "%s", path);
}


bsg_draw_mode
bsg_draw_intent_mode(const struct bsg_draw_intent *di)
{
    if (!di)
	return BSG_DRAW_MODE_WIRE;
    return di->di_mode;
}


void
bsg_draw_intent_set_mode(struct bsg_draw_intent *di, bsg_draw_mode mode)
{
    if (!di)
	return;
    di->di_mode = mode;
}


bsg_lod_policy
bsg_draw_intent_lod(const struct bsg_draw_intent *di)
{
    if (!di)
	return BSG_LOD_AUTO;
    return di->di_lod;
}


int
bsg_draw_intent_is_overlay(const struct bsg_draw_intent *di)
{
    if (!di)
	return 0;
    return di->di_overlay;
}


/* -----------------------------------------------------------------------
 * Scene-level actions
 * ----------------------------------------------------------------------- */

/*
 * Strip a single leading '/' from @p s if present.  Used to normalize
 * paths before comparison (db_path_to_string prepends '/').
 */
static const char *
_strip_lead_slash(const char *s)
{
    if (s && *s == '/')
	return s + 1;
    return s;
}


/*
 * DFS helper for bsg_draw_intent_find.  Returns the first intent-bearing
 * node under @p node (including @p node itself) whose di_path equals
 * @p norm_path, or NULL.
 */
static bsg_node *
_find_intent_dfs(bsg_node *node, const char *norm_path)
{
    if (!node)
	return NULL;

    const struct bsg_draw_intent *di = bsg_node_get_draw_intent(node);
    if (di) {
	const char *gpath = _strip_lead_slash(bu_vls_cstr(&di->di_path));
	if (BU_STR_EQUAL(gpath, norm_path))
	    return node;
	/* Intent-bearing node with a non-matching path: do NOT recurse. */
	return NULL;
    }

    /* No intent yet — this is an intermediate path-component group.
     * Search its children. */
    for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	bsg_node *child = (bsg_node *)BU_PTBL_GET(&node->children, i);
	bsg_node *found = _find_intent_dfs(child, norm_path);
	if (found)
	    return found;
    }
    return NULL;
}


bsg_node *
bsg_draw_intent_find(bsg_node *root, const char *path)
{
    if (!root || !path)
	return NULL;

    const char *norm = _strip_lead_slash(path);

    /* Search each direct child of root. */
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	bsg_node *g = (bsg_node *)BU_PTBL_GET(&root->children, i);
	bsg_node *found = _find_intent_dfs(g, norm);
	if (found)
	    return found;
    }
    return NULL;
}


/*
 * DFS helper for bsg_collect_draw_groups.  Collects all intent-bearing
 * leaf groups under @p node (including @p node itself).  When
 * @p include_overlays is zero, overlay intents are skipped.
 */
static void
_collect_intent_dfs(bsg_node *node, struct bu_ptbl *groups,
		    int include_overlays)
{
    if (!node)
	return;

    const struct bsg_draw_intent *di = bsg_node_get_draw_intent(node);
    if (di) {
	if (include_overlays || !di->di_overlay)
	    bu_ptbl_ins(groups, (long *)node);
	/* Intent-bearing node: do not recurse into its children. */
	return;
    }

    /* No intent — intermediate path component; recurse. */
    for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	bsg_node *child = (bsg_node *)BU_PTBL_GET(&node->children, i);
	_collect_intent_dfs(child, groups, include_overlays);
    }
}


void
bsg_collect_draw_groups(bsg_node *root, struct bu_ptbl *groups,
			int include_overlays)
{
    if (!root || !groups)
	return;

    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	bsg_node *g = (bsg_node *)BU_PTBL_GET(&root->children, i);
	_collect_intent_dfs(g, groups, include_overlays);
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
