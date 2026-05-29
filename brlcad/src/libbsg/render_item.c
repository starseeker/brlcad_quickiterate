/*                  R E N D E R _ I T E M . C
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
/** @file libbsg/render_item.c
 *
 * Phase D5: render-item lifecycle.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "vmath.h"

#include "bsg/render_item.h"


struct bsg_render_item *
bsg_render_item_create(void)
{
    struct bsg_render_item *item;
    BU_ALLOC(item, struct bsg_render_item);
    memset(item, 0, sizeof(struct bsg_render_item));
    /* Default: identity transform, fully opaque, phase opaque */
    MAT_IDN(item->model_mat);
    item->appearance.transparency = 1.0;
    item->phase        = BSG_RENDER_PHASE_OPAQUE;
    return item;
}


void
bsg_render_item_free(struct bsg_render_item *item)
{
    if (!item)
	return;
    bu_free(item, "bsg_render_item");
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
