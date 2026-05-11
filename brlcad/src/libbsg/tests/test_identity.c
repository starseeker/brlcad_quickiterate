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
 * Phase 2A/2B tests for ID structs, init/equality/hash helpers,
 * side-car identity storage, and path-string derived identity.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bsg/identity.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


static int
test_init_helpers(void)
{
    printf("=== Test 1: init_helpers ===\n");

    struct bsg_node_id nid = {42};
    struct bsg_part_id pid = {43};
    struct bsg_instance_id iid = {44};
    struct bsg_identity id;
    id.node_id.value = 111;
    id.part_id.value = 222;
    id.instance_id.value = 333;
    id.source_kind = BSG_SOURCE_DB_OBJECT;

    bsg_node_id_init(&nid);
    bsg_part_id_init(&pid);
    bsg_instance_id_init(&iid);
    bsg_identity_init(&id);

    if (nid.value != 0) FAIL("bsg_node_id_init");
    if (pid.value != 0) FAIL("bsg_part_id_init");
    if (iid.value != 0) FAIL("bsg_instance_id_init");
    if (id.node_id.value != 0) FAIL("bsg_identity_init node_id");
    if (id.part_id.value != 0) FAIL("bsg_identity_init part_id");
    if (id.instance_id.value != 0) FAIL("bsg_identity_init instance_id");
    if (id.source_kind != BSG_SOURCE_UNKNOWN) FAIL("bsg_identity_init source_kind");

    /* NULL guards */
    bsg_node_id_init(NULL);
    bsg_part_id_init(NULL);
    bsg_instance_id_init(NULL);
    bsg_identity_init(NULL);

    PASS("init_helpers");
    return 0;
}


static int
test_equal_helpers(void)
{
    printf("=== Test 2: equal_helpers ===\n");

    struct bsg_node_id n1 = {1001}, n2 = {1001}, n3 = {1002};
    struct bsg_part_id p1 = {2001}, p2 = {2001}, p3 = {2002};
    struct bsg_instance_id i1 = {3001}, i2 = {3001}, i3 = {3002};

    if (!bsg_node_id_equal(&n1, &n2)) FAIL("node equal true");
    if (bsg_node_id_equal(&n1, &n3)) FAIL("node equal false");
    if (!bsg_node_id_equal(NULL, NULL)) FAIL("node null/null true");
    if (bsg_node_id_equal(&n1, NULL)) FAIL("node null asym false");

    if (!bsg_part_id_equal(&p1, &p2)) FAIL("part equal true");
    if (bsg_part_id_equal(&p1, &p3)) FAIL("part equal false");
    if (!bsg_part_id_equal(NULL, NULL)) FAIL("part null/null true");
    if (bsg_part_id_equal(NULL, &p1)) FAIL("part null asym false");

    if (!bsg_instance_id_equal(&i1, &i2)) FAIL("instance equal true");
    if (bsg_instance_id_equal(&i1, &i3)) FAIL("instance equal false");
    if (!bsg_instance_id_equal(NULL, NULL)) FAIL("instance null/null true");
    if (bsg_instance_id_equal(&i1, NULL)) FAIL("instance null asym false");

    PASS("equal_helpers");
    return 0;
}


static int
test_hash_helpers(void)
{
    printf("=== Test 3: hash_helpers ===\n");

    struct bsg_node_id n1 = {0x1234}, n2 = {0x1234}, n3 = {0x1235};
    struct bsg_part_id p1 = {0x2234}, p2 = {0x2235};
    struct bsg_instance_id i1 = {0x3234}, i2 = {0x3235};

    if (bsg_node_id_hash(&n1) != bsg_node_id_hash(&n2))
	FAIL("node hash stable");
    if (bsg_node_id_hash(&n1) == bsg_node_id_hash(&n3))
	FAIL("node hash differs for different IDs");
    if (bsg_node_id_hash(NULL) != 0)
	FAIL("node hash null");

    if (bsg_part_id_hash(&p1) == bsg_part_id_hash(&p2))
	FAIL("part hash differs for different IDs");
    if (bsg_part_id_hash(NULL) != 0)
	FAIL("part hash null");

    if (bsg_instance_id_hash(&i1) == bsg_instance_id_hash(&i2))
	FAIL("instance hash differs for different IDs");
    if (bsg_instance_id_hash(NULL) != 0)
	FAIL("instance hash null");

    PASS("hash_helpers");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 4: Phase 2B side-car identity storage                           */
/* ------------------------------------------------------------------ */

static int
test_node_identity_sidecar(void)
{
    printf("=== Test 4: node_identity_sidecar ===\n");

    /* Use a dummy pointer — identity storage is keyed by address only. */
    int dummy_a = 0, dummy_b = 0;
    bsg_node *na = (bsg_node *)&dummy_a;
    bsg_node *nb = (bsg_node *)&dummy_b;

    /* Before any set, get should return 0 */
    struct bsg_identity got;
    memset(&got, 0xFF, sizeof(got));
    if (bsg_node_identity_get(na, &got) != 0)
	FAIL("get on unknown node should return 0");

    /* set then get round-trip */
    struct bsg_identity id_a;
    bsg_identity_init(&id_a);
    id_a.node_id.value = 0xDEAD;
    id_a.part_id.value = 0xBEEF;
    id_a.source_kind = BSG_SOURCE_DB_OBJECT;
    bsg_node_identity_set(na, &id_a);

    struct bsg_identity got_a;
    if (!bsg_node_identity_get(na, &got_a))
	FAIL("get after set should return 1");
    if (got_a.node_id.value != 0xDEAD)
	FAIL("node_id round-trip");
    if (got_a.part_id.value != 0xBEEF)
	FAIL("part_id round-trip");
    if (got_a.source_kind != BSG_SOURCE_DB_OBJECT)
	FAIL("source_kind round-trip");

    /* Different node pointer is independent */
    if (bsg_node_identity_get(nb, &got) != 0)
	FAIL("get on different node should return 0");

    /* Overwrite with different values */
    struct bsg_identity id_a2;
    bsg_identity_init(&id_a2);
    id_a2.node_id.value = 0xCAFE;
    id_a2.source_kind = BSG_SOURCE_VIEW_OBJECT;
    bsg_node_identity_set(na, &id_a2);

    struct bsg_identity got_a2;
    if (!bsg_node_identity_get(na, &got_a2))
	FAIL("get after overwrite should return 1");
    if (got_a2.node_id.value != 0xCAFE)
	FAIL("overwrite node_id round-trip");
    if (got_a2.source_kind != BSG_SOURCE_VIEW_OBJECT)
	FAIL("overwrite source_kind round-trip");

    /* Clear removes the entry */
    bsg_node_identity_clear(na);
    if (bsg_node_identity_get(na, &got) != 0)
	FAIL("get after clear should return 0");

    /* Double-clear is a no-op */
    bsg_node_identity_clear(na);

    /* NULL safety */
    bsg_node_identity_set(NULL, &id_a);
    bsg_node_identity_set(na, NULL); /* should also be a no-op / clear */
    bsg_node_identity_clear(NULL);
    if (bsg_node_identity_get(NULL, &got) != 0)
	FAIL("get(NULL) should return 0");

    PASS("node_identity_sidecar");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test 5: Phase 2B path-string derived identity                        */
/* ------------------------------------------------------------------ */

static int
test_identity_from_path_str(void)
{
    printf("=== Test 5: identity_from_path_str ===\n");

    struct bsg_identity id;

    /* NULL path should produce zero node_id but still set source_kind */
    bsg_identity_from_path_str(&id, NULL, BSG_SOURCE_DB_OBJECT);
    if (id.node_id.value != 0)
	FAIL("NULL path -> zero node_id");
    if (id.source_kind != BSG_SOURCE_DB_OBJECT)
	FAIL("NULL path -> source_kind set");

    /* Non-empty path should produce a non-zero node_id */
    bsg_identity_from_path_str(&id, "/tank/hull", BSG_SOURCE_DB_OBJECT);
    if (id.node_id.value == 0)
	FAIL("non-empty path -> non-zero node_id");
    if (id.source_kind != BSG_SOURCE_DB_OBJECT)
	FAIL("non-empty path -> source_kind set");

    /* Same path always produces the same ID */
    uint64_t id1 = id.node_id.value;
    bsg_identity_from_path_str(&id, "/tank/hull", BSG_SOURCE_DB_OBJECT);
    if (id.node_id.value != id1)
	FAIL("same path -> stable node_id");

    /* Different paths produce different IDs (no collision for these values) */
    uint64_t id_a = id.node_id.value;
    bsg_identity_from_path_str(&id, "/tank/turret", BSG_SOURCE_DB_OBJECT);
    uint64_t id_b = id.node_id.value;
    if (id_a == id_b)
	FAIL("different paths -> different node_id");

    /* source_kind is preserved independently */
    bsg_identity_from_path_str(&id, "/obj", BSG_SOURCE_VIEW_OBJECT);
    if (id.source_kind != BSG_SOURCE_VIEW_OBJECT)
	FAIL("view object source_kind");

    /* NULL out pointer is a no-op, must not crash */
    bsg_identity_from_path_str(NULL, "/path", BSG_SOURCE_DB_OBJECT);

    PASS("identity_from_path_str");
    return 0;
}


int
main(int UNUSED(argc), const char **argv)
{
    bu_setprogname(argv[0]);

    int failures = 0;
    failures += test_init_helpers();
    failures += test_equal_helpers();
    failures += test_hash_helpers();
    failures += test_node_identity_sidecar();
    failures += test_identity_from_path_str();

    if (failures) {
	printf("FAIL: %d test group(s) failed\n", failures);
	return 1;
    }

    printf("PASS: all identity tests passed\n");    return 0;
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
