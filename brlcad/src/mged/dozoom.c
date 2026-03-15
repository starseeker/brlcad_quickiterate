/*                        D O Z O O M . C
 * BRL-CAD
 *
 * Copyright (c) 1985-2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file mged/dozoom.c
 *
 */

#include "common.h"

#include <math.h>
#include "vmath.h"
#include "bn.h"
#include "bsg/util.h"

#include "./mged.h"
#include "./sedit.h"
#include "./mged_dm.h"

mat_t perspective_mat;
mat_t identity;


/* This is a holding place for the current display managers default wireframe color */
unsigned char geometry_default_color[] = { 255, 0, 0 };

/*
 * This routine reviews all of the solids in the solids table,
 * to see if they are visible within the current viewing
 * window.  If they are, the routine computes the scale and appropriate
 * screen position for the object.
 */
void
dozoom(struct mged_state *UNUSED(s), int UNUSED(which_eye))
{
    /* Step 7.20: libdm removed — no-op (rendering loop removed in Step 7.19). */
}


/*
 * Create a display list for "sp" for every display manager
 * manager that:
 * 1 - supports display lists
 * 2 - is actively using display lists
 * 3 - has not already been created (i.e. sharing with a
 * display manager that has already created the display list)
 */
void
createDListSolid(void *vlist_ctx, bsg_shape *UNUSED(sp))
{
    /* Step 7.20: libdm display lists removed — no-op. */
    struct mged_state *s = (struct mged_state *)vlist_ctx;
    MGED_CK_STATE(s);
}

/*
 * Create a display list for "sp" for every display manager
 * manager that:
 * 1 - supports display lists
 * 2 - is actively using display lists
 * 3 - has not already been created (i.e. sharing with a
 * display manager that has already created the display list)
 */
void
createDListAll(void *vlist_ctx, struct display_list *gdlp)
{
    struct mged_state *s = (struct mged_state *)vlist_ctx;
    MGED_CK_STATE(s);
    (void)gdlp;

    /* Phase 2e: shapes exclusively in scene-root children */
    bsg_view *v = (bsg_view *)s->gedp->ged_gvp;
    bsg_shape *root = v ? bsg_scene_root_get(v) : NULL;
    if (!root) return;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	createDListSolid(s, (bsg_shape *)BU_PTBL_GET(&root->children, i));
    }
}


/*
 * Free the range of display lists for all display managers
 * that support display lists and have them activated.
 */
void
freeDListsAll(void *data, unsigned int UNUSED(dlist), int UNUSED(range))
{
    /* Step 7.20: libdm display lists removed — no-op. */
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
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
