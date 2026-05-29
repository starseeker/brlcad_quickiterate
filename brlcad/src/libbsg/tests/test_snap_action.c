/*           T E S T _ S N A P _ A C T I O N . C
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
/** @file libbsg/tests/test_snap_action.c
 *
 * Unit tests for bsg/snap_action.h API.
 */

#include "common.h"

#include <stdio.h>

#include "bu/app.h"
#include "bsg/snap_action.h"
#include "bsg/util.h"

static int
test_null_guards(void)
{
    struct bsg_snap_result out = {0};
    point_t p = VINIT_ZERO;
    if (bsg_snap_candidates(NULL, p, 0.0, BSG_SNAP_KIND_GRID, &out) != 0) {
	printf("FAIL: NULL view guard\n");
	return 1;
    }
    bsg_snap_result_free(&out);
    return 0;
}

static int
test_grid_candidate(void)
{
    struct bsg_view v;
    bsg_view_init(&v, NULL);
    v.gv_width = 512;
    v.gv_height = 512;
    v.gv_s = &v.gv_ls;
    v.gv_s->gv_grid.res_h = 1.0;
    v.gv_s->gv_grid.res_v = 1.0;

    point_t sample = {0.1, 0.1, 0.0};
    struct bsg_snap_result out = {0};
    int cnt = bsg_snap_candidates(&v, sample, 0.0, BSG_SNAP_KIND_GRID, &out);
    bsg_snap_result_free(&out);
    bsg_view_free(&v);
    if (cnt < 0) {
	printf("FAIL: grid snap count negative\n");
	return 1;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);
    (void)argc;
    if (test_null_guards())
	return 1;
    if (test_grid_candidate())
	return 1;
    printf("PASS test_snap_action\n");
    return 0;
}

