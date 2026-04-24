/*                          L O D . C P P
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
/** @file libbsg/lod.cpp
 *
 * Phase 4: Level-of-detail helpers for the BSG scene graph.
 *
 * In Phase 4 these are thin stubs.  The full implementations will be
 * added as Phase 5 brings LoD-aware scene-graph traversal.  The stubs
 * are provided so that the public bsg/lod.h API compiles and links
 * cleanly from day one.
 */

#include "common.h"

#include "bv/defines.h"
#include "bv/lod.h"
#include "bv/util.h"

#include "bsg/defines.h"
#include "bsg/lod.h"


void
bsg_lod_update(bsg_node *root, struct bview *v)
{
    if (!root || !v)
	return;

    /* Phase 4 stub: walk the subtree and call bv_mesh_lod_view / csg
     * wireframe update for any BV_MESH_LOD / BV_CSG_LOD nodes found.
     * The traversal mirrors the LoD pass already present in
     * BViewState::redraw() — this placeholder keeps the logic
     * centralised once Phase 5 merges that pass into bsg_lod_update(). */
    struct bv_scene_obj *r = (struct bv_scene_obj *)root;
    for (size_t i = 0; i < BU_PTBL_LEN(&r->children); i++) {
	struct bv_scene_obj *child =
	    (struct bv_scene_obj *)BU_PTBL_GET(&r->children, i);
	if (!child)
	    continue;
	if (child->s_type_flags & BV_MESH_LOD)
	    bv_mesh_lod_view(child, v, 0);
	/* CSG LoD update via s_update_callback (already wired in Phase 2-B). */
    }
}


int
bsg_lod_stale(bsg_node *n, struct bview *v)
{
    if (!n || !v)
	return 0;

    /* Phase 4 stub: conservatively report "not stale" so callers don't
     * force unnecessary redraws.  Phase 5 will implement proper view-scale
     * change detection. */
    (void)n;
    (void)v;
    return 0;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
