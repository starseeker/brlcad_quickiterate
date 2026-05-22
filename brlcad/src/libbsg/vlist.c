/*                        V L I S T . C
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
/** @file libbsg/vlist.c
 *
 * Phase 4: vlist helpers for BSG scene-graph construction.
 *
 * bsg_vlist_cmd_cnt — BSG-namespaced command counter (wraps libbv).
 * bsg_vlist_arb8    — emit 18-command ARB8 wireframe into a vlist.
 */

#include "common.h"

#include "vmath.h"
#include "bu/list.h"
#include "bsg/defines.h"
#include "bsg/vlist.h"


size_t
bsg_vlist_cmd_cnt(struct bv_vlist *vlist)
{
    return bv_vlist_cmd_cnt(vlist);
}


void
bsg_vlist_arb8(struct bu_list *vhead, struct bu_list *vlfree, point_t pts[8])
{
    if (!vhead || !vlfree || !pts)
	return;

    /* Bottom face loop: MOVE(0) DRAW(1) DRAW(2) DRAW(3) DRAW(0)  — 5 cmds */
    BSG_ADD_VLIST(vlfree, vhead, pts[0], BSG_VLIST_LINE_MOVE);
    BSG_ADD_VLIST(vlfree, vhead, pts[1], BSG_VLIST_LINE_DRAW);
    BSG_ADD_VLIST(vlfree, vhead, pts[2], BSG_VLIST_LINE_DRAW);
    BSG_ADD_VLIST(vlfree, vhead, pts[3], BSG_VLIST_LINE_DRAW);
    BSG_ADD_VLIST(vlfree, vhead, pts[0], BSG_VLIST_LINE_DRAW); /* close */

    /* Top face loop: MOVE(4) DRAW(5) DRAW(6) DRAW(7) DRAW(4)     — 5 cmds */
    BSG_ADD_VLIST(vlfree, vhead, pts[4], BSG_VLIST_LINE_MOVE);
    BSG_ADD_VLIST(vlfree, vhead, pts[5], BSG_VLIST_LINE_DRAW);
    BSG_ADD_VLIST(vlfree, vhead, pts[6], BSG_VLIST_LINE_DRAW);
    BSG_ADD_VLIST(vlfree, vhead, pts[7], BSG_VLIST_LINE_DRAW);
    BSG_ADD_VLIST(vlfree, vhead, pts[4], BSG_VLIST_LINE_DRAW); /* close */

    /* Four vertical edges: 4 × (MOVE + DRAW)                     — 8 cmds */
    BSG_ADD_VLIST(vlfree, vhead, pts[0], BSG_VLIST_LINE_MOVE);
    BSG_ADD_VLIST(vlfree, vhead, pts[4], BSG_VLIST_LINE_DRAW);

    BSG_ADD_VLIST(vlfree, vhead, pts[1], BSG_VLIST_LINE_MOVE);
    BSG_ADD_VLIST(vlfree, vhead, pts[5], BSG_VLIST_LINE_DRAW);

    BSG_ADD_VLIST(vlfree, vhead, pts[2], BSG_VLIST_LINE_MOVE);
    BSG_ADD_VLIST(vlfree, vhead, pts[6], BSG_VLIST_LINE_DRAW);

    BSG_ADD_VLIST(vlfree, vhead, pts[3], BSG_VLIST_LINE_MOVE);
    BSG_ADD_VLIST(vlfree, vhead, pts[7], BSG_VLIST_LINE_DRAW);

    /* Total: 5 + 5 + 8 = 18 vlist commands */
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
