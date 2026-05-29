/*                    B S G _ P R I V A T E . H
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
/** @file libbsg/bsg_private.h
 *
 * Internal libbsg helpers shared between multiple translation units.
 * This header is NOT installed; it is for libbsg source use only.
 */

#ifndef LIBBSG_BSG_PRIVATE_H
#define LIBBSG_BSG_PRIVATE_H

#include "common.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bsg/defines.h"
#include "bsg/node_private.h"
#include "bsg/draw_ctx.h"

/* Internal view-set implementation data */
struct bsg_view_set_internal {
    struct bu_ptbl views;
    struct bu_ptbl shared_db_objs;

    struct bsg_node  *free_scene_obj;
    struct bu_list vlfree;
};

#include "bsg/overlay.h"

/* Internal scene-node implementation data (reserved for future use) */
struct bsg_node_internal {
    struct bsg_overlay_info *overlay;
};

/*
 * Walk node @p n up to the draw root and return the bsg_draw_ctx stored
 * in root->s_i_data.  Returns NULL if the root has no context.
 */
static inline struct bsg_draw_ctx *
_ctx_of_node(struct bsg_node *n)
{
    if (!n)
	return NULL;
    while (n->parent)
	n = (struct bsg_node *)n->parent;
    return (struct bsg_draw_ctx *)n->s_i_data;
}

#endif /* LIBBSG_BSG_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
