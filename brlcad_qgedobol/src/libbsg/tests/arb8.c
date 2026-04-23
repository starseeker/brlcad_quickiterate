/*                              A R B 8 . C
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/**
 * Test bsg_vlist_arb8: verify it generates exactly 18 vlist commands
 * (5 for each of the 2 quad faces + 8 for the 4 lateral edges).
 */

#include "common.h"

#include "bu.h"
#include "bsg.h"

int
arb8_main(int argc, char *argv[])
{
    struct bu_list head;
    struct bu_list vlfree;
    point_t pts[8];
    size_t cmd_cnt;

    (void)argc;
    (void)argv;

    /* Build a unit cube as the test arb8 */
    VSET(pts[0], 0.0, 0.0, 0.0);
    VSET(pts[1], 1.0, 0.0, 0.0);
    VSET(pts[2], 1.0, 1.0, 0.0);
    VSET(pts[3], 0.0, 1.0, 0.0);
    VSET(pts[4], 0.0, 0.0, 1.0);
    VSET(pts[5], 1.0, 0.0, 1.0);
    VSET(pts[6], 1.0, 1.0, 1.0);
    VSET(pts[7], 0.0, 1.0, 1.0);

    BU_LIST_INIT(&head);
    BU_LIST_INIT(&vlfree);

    bsg_vlist_arb8(&vlfree, &head, (const point_t *)pts);

    /* Expected: 18 commands
     *   face 0: MOVE pts[0] + DRAW pts[1,2,3,0] = 5 cmds
     *   face 1: MOVE pts[4] + DRAW pts[5,6,7,4] = 5 cmds
     *   laterals: 4 x (MOVE + DRAW) = 8 cmds
     *   total: 18
     */
    cmd_cnt = bsg_vlist_cmd_cnt((struct bsg_vlist *)&head);

    BSG_FREE_VLIST(&vlfree, &head);

    return (cmd_cnt == 18) ? 0 : 1;
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
