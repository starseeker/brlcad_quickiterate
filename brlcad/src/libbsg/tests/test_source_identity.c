/*         T E S T _ S O U R C E _ I D E N T I T Y . C
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
/** @file libbsg/tests/test_source_identity.c
 *
 * Slice 5 (bv_scene_obj_migrate.txt) tests for the source-identity
 * accessor migration:
 *
 *   bsg_node_u1_get / bsg_node_u1_set
 *   bsg_node_u2_get / bsg_node_u2_set
 *   bsg_node_u3_get / bsg_node_u3_set
 *
 * All three fields are now stored inline in bsg_node (bsg_db_dir,
 * bsg_source_path, bsg_ged_data) rather than in bv_scene_obj.dp /
 * s_path / s_u_data.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/defines.h"   /* struct bv_scene_obj, bsg_node inline layout */
#include "bsg/node.h"     /* bsg_node_u1_get/set, bsg_node_source_path_* */

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


/* Allocate a minimal bv_scene_obj on the stack for testing.
 * Zeroing the memory is sufficient — BSG fields are valid at all-zero. */
static void
node_init(struct bv_scene_obj *s)
{
    memset(s, 0, sizeof(*s));
}


/* ------------------------------------------------------------------ */
/* Test 1: bsg_node_u1_get / bsg_node_u1_set                  */
/* ------------------------------------------------------------------ */

static int
test_db_dir(void)
{
    printf("=== Test 1: db_dir get/set ===\n");

    struct bv_scene_obj s;
    node_init(&s);
    bsg_node *n = (bsg_node *)&s;

    /* Fresh node: no db_dir set */
    if (bsg_node_u1_get(n) != NULL)
	FAIL("fresh node: db_dir should be NULL");

    /* Use a stack address as a fake struct directory * */
    struct directory *fake_dp = (struct directory *)0xDEADBEEF;
    bsg_node_u1_set(n, fake_dp);

    if (bsg_node_u1_get(n) != fake_dp)
	FAIL("db_dir round-trip");

    /* Clear to NULL */
    bsg_node_u1_set(n, NULL);
    if (bsg_node_u1_get(n) != NULL)
	FAIL("db_dir clear to NULL");

    /* NULL node safety */
    bsg_node_u1_set(NULL, fake_dp); /* no-op */
    if (bsg_node_u1_get(NULL) != NULL)
	FAIL("db_dir_get(NULL) should return NULL");

    /* Verify storage is in bsg_node inline field, NOT in bv_scene_obj.dp */
    bsg_node_u1_set(n, fake_dp);
    if (n->bsg_db_dir != (void *)fake_dp)
	FAIL("db_dir stored in bsg.bsg_db_dir");
    /* The legacy dp field must remain independent (not aliased) */
    if (s.dp == (void *)fake_dp)
	FAIL("db_dir must NOT alias bv_scene_obj.dp");

    PASS("db_dir");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 2: bsg_node_u2_get / bsg_node_u2_set        */
/* ------------------------------------------------------------------ */

static int
test_source_path(void)
{
    printf("=== Test 2: source_path get/set ===\n");

    struct bv_scene_obj s;
    node_init(&s);
    bsg_node *n = (bsg_node *)&s;

    /* Fresh node: no source_path set */
    if (bsg_node_u2_get(n) != NULL)
	FAIL("fresh node: source_path should be NULL");

    void *fake_path = (void *)0xCAFEBABE;
    bsg_node_u2_set(n, fake_path);
    if (bsg_node_u2_get(n) != fake_path)
	FAIL("source_path round-trip");

    /* Clear to NULL */
    bsg_node_u2_set(n, NULL);
    if (bsg_node_u2_get(n) != NULL)
	FAIL("source_path clear to NULL");

    /* NULL node safety */
    bsg_node_u2_set(NULL, fake_path); /* no-op */
    if (bsg_node_u2_get(NULL) != NULL)
	FAIL("source_path_get(NULL) should return NULL");

    /* Verify storage in bsg_node inline field */
    bsg_node_u2_set(n, fake_path);
    if (n->bsg_source_path != fake_path)
	FAIL("source_path stored in bsg.bsg_source_path");
    /* Legacy s_path must remain independent */
    if (s.s_path == fake_path)
	FAIL("source_path must NOT alias bv_scene_obj.s_path");

    PASS("source_path");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 3: bsg_node_u3_get / bsg_node_u3_set              */
/* ------------------------------------------------------------------ */

static int
test_ged_data(void)
{
    printf("=== Test 3: ged_data get/set ===\n");

    struct bv_scene_obj s;
    node_init(&s);
    bsg_node *n = (bsg_node *)&s;

    /* Fresh node: no ged_data set */
    if (bsg_node_u3_get(n) != NULL)
	FAIL("fresh node: ged_data should be NULL");

    void *fake_ged = (void *)0xFEEDFACE;
    bsg_node_u3_set(n, fake_ged);
    if (bsg_node_u3_get(n) != fake_ged)
	FAIL("ged_data round-trip");

    /* Clear to NULL */
    bsg_node_u3_set(n, NULL);
    if (bsg_node_u3_get(n) != NULL)
	FAIL("ged_data clear to NULL");

    /* NULL node safety */
    bsg_node_u3_set(NULL, fake_ged); /* no-op */
    if (bsg_node_u3_get(NULL) != NULL)
	FAIL("ged_data_get(NULL) should return NULL");

    /* Verify storage in bsg_node inline field */
    bsg_node_u3_set(n, fake_ged);
    if (n->bsg_ged_data != fake_ged)
	FAIL("ged_data stored in bsg.bsg_ged_data");
    /* Legacy s_u_data must remain independent */
    if (s.s_u_data == fake_ged)
	FAIL("ged_data must NOT alias bv_scene_obj.s_u_data");

    PASS("ged_data");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 4: all three fields are independent (no aliasing)              */
/* ------------------------------------------------------------------ */

static int
test_field_independence(void)
{
    printf("=== Test 4: field independence ===\n");

    struct bv_scene_obj s;
    node_init(&s);
    bsg_node *n = (bsg_node *)&s;

    struct directory *fake_dp   = (struct directory *)0x1111;
    void             *fake_path = (void *)0x2222;
    void             *fake_ged  = (void *)0x3333;

    bsg_node_u1_set(n, fake_dp);
    bsg_node_u2_set(n, fake_path);
    bsg_node_u3_set(n, fake_ged);

    if (bsg_node_u1_get(n)      != fake_dp)   FAIL("db_dir preserved after other sets");
    if (bsg_node_u2_get(n) != fake_path) FAIL("source_path preserved after other sets");
    if (bsg_node_u3_get(n)    != fake_ged)  FAIL("ged_data preserved after other sets");

    /* Overwrite one does not affect others */
    bsg_node_u1_set(n, NULL);
    if (bsg_node_u2_get(n) != fake_path) FAIL("source_path unchanged after db_dir clear");
    if (bsg_node_u3_get(n)    != fake_ged)  FAIL("ged_data unchanged after db_dir clear");

    PASS("field_independence");
    return 0;
}


int
main(int UNUSED(argc), const char **argv)
{
    bu_setprogname(argv[0]);

    int failures = 0;
    failures += test_db_dir();
    failures += test_source_path();
    failures += test_ged_data();
    failures += test_field_independence();

    if (failures) {
	printf("FAIL: %d test group(s) failed\n", failures);
	return 1;
    }

    printf("PASS: all source-identity tests passed\n");
    return 0;
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
