/*         A P P E A R A N C E _ A C T I O N . C
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
/** @file libbsg/appearance_action.c
 *
 * Phase D5: bsg_appearance_resolve — resolve the final appearance for a
 * shape node by accumulating material, command overrides, transparency,
 * display mode, line attributes, and selection/highlight state.
 */

#include "common.h"

#include <string.h>

#include "bsg/defines.h"
#include "bsg/material.h"
#include "bsg/appearance.h"
#include "bsg/appearance_action.h"
#include "bsg/node_private.h"


int
bsg_appearance_resolve(const struct bsg_view *UNUSED(v),
		       const bsg_node *node,
		       const struct bsg_obj_settings *inherited_os,
		       struct bsg_resolved_appearance *out)
{
    if (!node || !out)
	return 0;

    memset(out, 0, sizeof(struct bsg_resolved_appearance));

    /* ------------------------------------------------------------------ */
    /* 1. Start from the node's base material color                        */
    /* ------------------------------------------------------------------ */
    unsigned char r = 0, g = 0, b = 0;
    bsg_material_get_rgb(node, &r, &g, &b);
    out->color[0] = r;
    out->color[1] = g;
    out->color[2] = b;
    out->active_layers |= BSG_ALAY_BASE;

    /* ------------------------------------------------------------------ */
    /* 2. Inherited ancestor group settings                                */
    /* ------------------------------------------------------------------ */
    if (inherited_os) {
	out->transparency = inherited_os->transparency;
	out->dmode        = inherited_os->s_dmode;
	out->line_width   = inherited_os->s_line_width;
	if (inherited_os->color_override) {
	    out->color[0] = inherited_os->color[0];
	    out->color[1] = inherited_os->color[1];
	    out->color[2] = inherited_os->color[2];
	    out->active_layers |= BSG_ALAY_INHERITED;
	}
    } else {
	out->transparency = 1.0;
	out->dmode        = 0;
	out->line_width   = 1;
    }

    /* ------------------------------------------------------------------ */
    /* 3. Per-node command override (s_os)                                 */
    /* ------------------------------------------------------------------ */
    const struct bsg_obj_settings *os =
	((const bsg_node *)node)->s_os;
    if (os) {
	out->transparency = os->transparency;
	out->dmode        = os->s_dmode;
	out->line_width   = os->s_line_width;
	if (os->color_override) {
	    out->color[0] = os->color[0];
	    out->color[1] = os->color[1];
	    out->color[2] = os->color[2];
	    out->active_layers |= BSG_ALAY_COMMAND;
	}
    }

    /* ------------------------------------------------------------------ */
    /* 4. Line style from the shape node                                   */
    /* ------------------------------------------------------------------ */
    out->line_style = ((const bsg_node *)node)->s_soldash;

    /* ------------------------------------------------------------------ */
    /* 5. Transparency layer flag                                           */
    /* ------------------------------------------------------------------ */
    if (out->transparency < 1.0)
	out->active_layers |= BSG_ALAY_TRANSPARENCY;

    /* ------------------------------------------------------------------ */
    /* 6. Hidden-line display mode                                          */
    /* ------------------------------------------------------------------ */
    if (out->dmode == 4)
	out->active_layers |= BSG_ALAY_HIDDEN_LINE;

    /* ------------------------------------------------------------------ */
    /* 7. Highlight state (s_iflag == UP)                                   */
    /* ------------------------------------------------------------------ */
    out->highlighted = bsg_appearance_is_highlighted(node);
    if (out->highlighted)
	out->active_layers |= BSG_ALAY_HIGHLIGHT;

    /* ------------------------------------------------------------------ */
    /* 8. Revision stamps for backend cache invalidation                   */
    /* ------------------------------------------------------------------ */
    out->material_rev   = ((const bsg_node *)node)->s_color_rev;
    out->appearance_rev = (uint32_t)((const bsg_node *)node)->s_drawn_rev & 0xFFFFFFFFu;

    return 1;
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
