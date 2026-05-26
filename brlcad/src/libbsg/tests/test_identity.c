/*               T E S T _ I D E N T I T Y . C
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
/** @file libbsg/tests/test_identity.c
 *
 * Phase 2 unit tests for bsg/identity.h API.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bsg/identity.h"
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
    bu_vls_sprintf(&v->gv_name, "test_identity_view");
    return v;
}

static void
free_view(struct bsg_view *v)
{
    if (!v)
	return;
    bsg_view_free(v);
    bu_free(v, "test identity view");
}

static int
test_identity_fields(void)
{
    struct bsg_view *v = make_view();
    bsg_node *n = bsg_node_create(v, BSG_NODE_SHAPE);
    CHECK(n != NULL, "bsg_node_create");

    bsg_node_identity_set_name(n, "shape:foo");
    CHECK(!strcmp(bsg_node_identity_name(n), "shape:foo"), "name");

    int path_token = 5;
    bsg_node_identity_set_path(n, &path_token);
    CHECK(bsg_node_identity_path(n) == &path_token, "path token");

    int source_data = 9;
    bsg_node_identity_set_source(n, &source_data);
    CHECK(bsg_node_identity_source(n) == &source_data, "source data");

    bsg_node_destroy(n);
    free_view(v);
    return 0;
}

static int
test_revision_fields(void)
{
    struct bsg_view *v = make_view();
    bsg_node *n = bsg_node_create(v, BSG_NODE_GROUP);
    CHECK(n != NULL, "bsg_node_create");

    CHECK(bsg_node_revision_get(n) == 0, "initial revision");
    bsg_node_revision_set(n, 41);
    CHECK(bsg_node_revision_get(n) == 41, "set revision");
    CHECK(bsg_node_revision_bump(n) == 42, "bump revision return value");
    CHECK(bsg_node_revision_get(n) == 42, "bump revision state");

    bsg_node_destroy(n);
    free_view(v);
    return 0;
}

int
main(int UNUSED(argc), const char **argv)
{
    bu_setprogname(argv[0]);
    int failures = 0;

    failures += test_identity_fields();
    failures += test_revision_fields();

    if (!failures)
	printf("PASS: test_identity\n");
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
