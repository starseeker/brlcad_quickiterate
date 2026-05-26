/*                   T E S T _ N O D E . C
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
/** @file libbsg/tests/test_node.c
 *
 * Phase 1 unit tests for generic bsg/node.h API.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bsg/node.h"
#include "bsg/util.h"

#define CHECK(_expr, _msg) \
    do { \
	if (!(_expr)) { \
	    printf("FAIL: %s\n", (_msg)); \
	    return 1; \
	} \
    } while (0)

static struct bsg_view *
make_view(void)
{
    struct bsg_view *v = NULL;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_node_view");
    return v;
}

static void
free_view(struct bsg_view *v)
{
    if (!v)
	return;
    bsg_view_free(v);
    bu_free(v, "test node view");
}

static int
test_lifecycle_and_kind(void)
{
    struct bsg_view *v = make_view();
    bsg_node *n = bsg_node_create(v, BSG_NODE_TRANSFORM);
    CHECK(n != NULL, "bsg_node_create");
    CHECK(bsg_node_kind(n) == BSG_NODE_TRANSFORM, "kind");
    CHECK(bsg_node_is_kind(n, BSG_NODE_TRANSFORM), "is_kind");
    CHECK(bsg_node_visible(n), "visible by default");

    mat_t got, ident;
    MAT_IDN(ident);
    bsg_node_transform(n, got);
    CHECK(memcmp(got, ident, sizeof(mat_t)) == 0, "default transform is identity");

    bsg_node_set_visible_state(n, 0);
    CHECK(!bsg_node_visible(n), "set invisible");
    bsg_node_destroy(n);
    free_view(v);
    return 0;
}

static int
test_name_user_bounds(void)
{
    struct bsg_view *v = make_view();
    bsg_node *n = bsg_node_create(v, BSG_NODE_SHAPE);
    CHECK(n != NULL, "create shape");

    bsg_node_set_name(n, "nodeA");
    CHECK(!strcmp(bsg_node_name(n), "nodeA"), "name round trip");

    int payload = 42;
    bsg_node_set_user_data(n, &payload);
    CHECK(bsg_node_user_data(n) == &payload, "user data round trip");

    point_t bmin, bmax, gmin, gmax;
    VSET(bmin, -1.0, -2.0, -3.0);
    VSET(bmax, 4.0, 5.0, 6.0);
    bsg_node_set_bounds(n, bmin, bmax, 1);
    CHECK(bsg_node_bounds(n, gmin, gmax) == 1, "bounds valid");
    CHECK(VNEAR_EQUAL(gmin, bmin, SMALL_FASTF), "bounds min");
    CHECK(VNEAR_EQUAL(gmax, bmax, SMALL_FASTF), "bounds max");

    bsg_node_set_bounds(n, bmin, bmax, 0);
    CHECK(bsg_node_bounds(n, gmin, gmax) == 0, "bounds invalid");

    bsg_node_destroy(n);
    free_view(v);
    return 0;
}

static int
test_parent_child_and_revision(void)
{
    struct bsg_view *v = make_view();
    bsg_node *p1 = bsg_node_create(v, BSG_NODE_GROUP);
    bsg_node *p2 = bsg_node_create(v, BSG_NODE_GROUP);
    bsg_node *c = bsg_node_create(v, BSG_NODE_SHAPE);
    CHECK(p1 && p2 && c, "create parent/child");

    uint64_t p1_rev0 = bsg_node_revision(p1);
    bsg_node_add_child(p1, c);
    CHECK(bsg_node_parent(c) == p1, "parent assigned");
    CHECK(bsg_node_child_count(p1) == 1, "child count");
    CHECK(bsg_node_child_at(p1, 0) == c, "child index");
    CHECK(bsg_node_revision(p1) > p1_rev0, "parent revision bumped on add");

    bsg_node_add_child(p2, c);
    CHECK(bsg_node_parent(c) == p2, "reparented");
    CHECK(bsg_node_child_count(p1) == 0, "old parent updated");
    CHECK(bsg_node_child_count(p2) == 1, "new parent count");

    uint64_t c_rev = bsg_node_revision(c);
    bsg_node_touch(c);
    CHECK(bsg_node_revision(c) > c_rev, "touch increments revision");

    bsg_node_remove_child(p2, c);
    CHECK(bsg_node_parent(c) == NULL, "parent cleared on remove");
    CHECK(bsg_node_child_count(p2) == 0, "remove child");

    bsg_node_destroy(c);
    bsg_node_destroy(p1);
    bsg_node_destroy(p2);
    free_view(v);
    return 0;
}

int
main(int UNUSED(argc), const char **argv)
{
    bu_setprogname(argv[0]);
    int failures = 0;

    failures += test_lifecycle_and_kind();
    failures += test_name_user_bounds();
    failures += test_parent_child_and_revision();

    if (!failures)
	printf("PASS: test_node\n");
    return failures;
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
