/*                          A X E S . C
 * BRL-CAD
 *
 * Copyright (c) 1998-2025 United States Government as represented by
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
/** @file mged/axes.c
 *
 */

#include "common.h"

#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bn.h"
#include "bsg/util.h"
#include "ged.h"

#include "./mged.h"
#include "./mged_dm.h"

/* local sp_hook function */
static void ax_set_dirty_flag(const struct bu_structparse *, const char *, void *, const char *, void *);

struct _axes_state default_axes_state = {
    /* ax_rc */			1,
    /* ax_model_draw */    	0,
    /* ax_model_size */		500,
    /* ax_model_linewidth */	1,
    /* ax_model_pos */		VINIT_ZERO,
    /* ax_view_draw */    	0,
    /* ax_view_size */		500,
    /* ax_view_linewidth */	1,
    /* ax_view_pos */		{ 0, 0 },
    /* ax_edit_draw */		0,
    /* ax_edit_size1 */		500,
    /* ax_edit_size2 */		500,
    /* ax_edit_linewidth1 */	1,
    /* ax_edit_linewidth2 */	1
};


#define AX_O(_m) bu_offsetof(struct _axes_state, _m)
struct bu_structparse axes_vparse[] = {
    {"%d", 1, "model_draw",	AX_O(ax_model_draw),		ax_set_dirty_flag, NULL, NULL },
    {"%d", 1, "model_size",	AX_O(ax_model_size),		ax_set_dirty_flag, NULL, NULL },
    {"%d", 1, "model_linewidth",AX_O(ax_model_linewidth),	ax_set_dirty_flag, NULL, NULL },
    {"%f", 3, "model_pos",	AX_O(ax_model_pos),		ax_set_dirty_flag, NULL, NULL },
    {"%d", 1, "view_draw",	AX_O(ax_view_draw),		ax_set_dirty_flag, NULL, NULL },
    {"%d", 1, "view_size",	AX_O(ax_view_size),		ax_set_dirty_flag, NULL, NULL },
    {"%d", 1, "view_linewidth",	AX_O(ax_view_linewidth),	ax_set_dirty_flag, NULL, NULL },
    {"%d", 2, "view_pos",	AX_O(ax_view_pos),		ax_set_dirty_flag, NULL, NULL },
    {"%d", 1, "edit_draw",	AX_O(ax_edit_draw),		ax_set_dirty_flag, NULL, NULL },
    {"%d", 1, "edit_size1",	AX_O(ax_edit_size1),		ax_set_dirty_flag, NULL, NULL },
    {"%d", 1, "edit_size2",	AX_O(ax_edit_size2),		ax_set_dirty_flag, NULL, NULL },
    {"%d", 1, "edit_linewidth1",AX_O(ax_edit_linewidth1),	ax_set_dirty_flag, NULL, NULL },
    {"%d", 1, "edit_linewidth2",AX_O(ax_edit_linewidth2),	ax_set_dirty_flag, NULL, NULL },
    {"",   0, (char *)0,	0,			 BU_STRUCTPARSE_FUNC_NULL, NULL, NULL }
};


static void
ax_set_dirty_flag(const struct bu_structparse *UNUSED(sdp),
		  const char *UNUSED(name),
		  void *UNUSED(base),
		  const char *UNUSED(value),
		  void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    /* Step 7.20: mp_dmp removed; update_views triggers Obol refresh. */
    s->update_views = 1;
}


void
draw_e_axes(struct mged_state *UNUSED(s))
{
    /* Step 7.20: libdm removed — no-op (was only called from dm rendering loop). */
}


void
draw_m_axes(struct mged_state *UNUSED(s))
{
    /* Step 7.20: libdm removed — no-op. */
}


void
draw_v_axes(struct mged_state *UNUSED(s))
{
    /* Step 7.20: libdm removed — no-op. */
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
