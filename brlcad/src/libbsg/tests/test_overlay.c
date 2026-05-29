/*               T E S T _ O V E R L A Y . C
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
/** @file libbsg/tests/test_overlay.c
 *
 * Unit tests for bsg overlay node API.
 */

#include "common.h"

#include <stdio.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/vls.h"
#include "bsg/defines.h"
#include "bsg/hud.h"
#include "bsg/overlay.h"
#include "bsg/util.h"

static struct bsg_view *
_make_view(void)
{
    struct bsg_view *v;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_overlay_view");
    (void)bsg_scene_root_create(v);
    return v;
}

static void
_free_view(struct bsg_view *v)
{
    if (!v)
return;
    bsg_hud_root_destroy(v);
    bsg_view_free(v);
    bu_free(v, "test_overlay view");
}

#define CHECK(_cond, _msg) do { if (!(_cond)) { printf("FAIL: %s\n", (_msg)); return 1; } } while (0)

static int
test_owner_replace_clear(void)
{
    struct bsg_view *v = _make_view();
    int owner = 7;
    bsg_node *n1 = bsg_view_obj_lines_create(v, "overlay_a", 0);
    CHECK(n1 != NULL, "create overlay a");
    CHECK(bsg_overlay_register_owner(n1, &owner, BSG_OVERLAY_ROLE_SCREEN,
BSG_OVERLAY_CLASS_MEASURE, BSG_OVERLAY_LC_PER_TOOL,
BSG_OVERLAY_ORDER_POST_TRANSPARENT, NULL, 0), "register overlay a");
    CHECK(bsg_overlay_replace(v, &owner, n1) == n1, "replace overlay a");

    bsg_node *n2 = bsg_view_obj_lines_create(v, "overlay_b", 0);
    CHECK(n2 != NULL, "create overlay b");
    CHECK(bsg_overlay_register_owner(n2, &owner, BSG_OVERLAY_ROLE_SCREEN,
BSG_OVERLAY_CLASS_MEASURE, BSG_OVERLAY_LC_PER_TOOL,
BSG_OVERLAY_ORDER_POST_TRANSPARENT, NULL, 1), "register overlay b");
    (void)bsg_overlay_replace(v, &owner, n2);

    struct bu_ptbl matches = BU_PTBL_INIT_ZERO;
    CHECK(bsg_overlay_query_by_role((bsg_node *)v->gv_draw_root, BSG_OVERLAY_ROLE_SCREEN, &matches) >= 1,
"query overlays by role");
    bu_ptbl_free(&matches);

    CHECK(bsg_overlay_clear_owned(v, &owner) >= 1, "clear owned overlays");
    _free_view(v);
    return 0;
}

static int
test_auto_remove(void)
{
    struct bsg_view *v = _make_view();
    bsg_node *n = bsg_view_obj_lines_create(v, "overlay_source", 0);
    CHECK(n != NULL, "create source overlay");
    CHECK(bsg_overlay_register_owner(n, v, BSG_OVERLAY_ROLE_MODEL,
BSG_OVERLAY_CLASS_COMMAND_RESULT, BSG_OVERLAY_LC_AUTO_REMOVE_ON_SOURCE,
BSG_OVERLAY_ORDER_MODEL, "db/path", 0), "register source overlay");
    CHECK(bsg_overlay_auto_remove((bsg_node *)v->gv_draw_root, "db/path") == 1,
"auto remove overlay by source");
    _free_view(v);
    return 0;
}

int
main(int argc, char **argv)
{
    int ret = 0;
    bu_setprogname(argv[0]);
    (void)argc;
    ret += test_owner_replace_clear();
    ret += test_auto_remove();
    if (!ret)
printf("ALL TESTS PASSED\n");
    return ret;
}
