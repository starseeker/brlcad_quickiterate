/*      T E S T _ P A Y L O A D _ T Y P E D _ L I V E _ S O U R C E . C
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
/** @file libbsg/tests/test_payload_typed_live_source.c
 *
 * Phase D6 (drawing_modernization) regression tests for BSG_PL_LIVE and the
 * generic bsg_live_source payload contract.
 *
 * Covered scenarios
 * -----------------
 *   test_live_create        — basic create / get_data / free lifecycle
 *   test_live_set_ops       — install callbacks; verify they fire
 *   test_live_revision      — revision monotonicity with revision_cb
 *   test_live_realize       — realize returns 1 on change, 0 on no-change
 *   test_live_bounds        — bounds_cb path
 *   test_live_pick          — pick_cb path
 *   test_live_snap          — snap_cb path
 *   test_live_teardown      — owns_live_ctx free chain
 *   test_live_partial_ops   — NULL update_cb is handled gracefully
 */

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "vmath.h"
#include "bsg/payload_typed.h"
#include "bsg/node_private.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


/* ---- Stub callback state ------------------------------------------------ */

struct live_stub {
    uint64_t revision;       /* value returned by revision_cb */
    int update_calls;
    int bounds_calls;
    int pick_calls;
    int snap_calls;
    int free_calls;
    int update_returns;      /* value returned by update_cb */
    point_t bmin;
    point_t bmax;
    int pick_hit;
    point_t snap_out;
};

static uint64_t
stub_revision_cb(void *live_ctx)
{
    struct live_stub *s = (struct live_stub *)live_ctx;
    return s->revision;
}

static int
stub_update_cb(void *live_ctx, struct bsg_view *UNUSED(v))
{
    struct live_stub *s = (struct live_stub *)live_ctx;
    s->update_calls++;
    s->revision++;  /* advance so revision_cb reflects the change */
    return s->update_returns;
}

static int
stub_bounds_cb(void *live_ctx, point_t *bmin, point_t *bmax)
{
    struct live_stub *s = (struct live_stub *)live_ctx;
    s->bounds_calls++;
    VMOVE(*bmin, s->bmin);
    VMOVE(*bmax, s->bmax);
    return 1;
}

static int
stub_pick_cb(void *live_ctx, struct bsg_view *UNUSED(v), int UNUSED(x), int UNUSED(y), void *pick_out)
{
    struct live_stub *s = (struct live_stub *)live_ctx;
    s->pick_calls++;
    if (pick_out)
	*(int *)pick_out = s->pick_hit;
    return (s->pick_hit != 0) ? 1 : 0;
}

static int
stub_snap_cb(void *live_ctx, struct bsg_view *UNUSED(v), const point_t UNUSED(sample_pt), point_t out_pt)
{
    struct live_stub *s = (struct live_stub *)live_ctx;
    s->snap_calls++;
    VMOVE(out_pt, s->snap_out);
    return 1;
}

static void
stub_free_cb(void *live_ctx)
{
    struct live_stub *s = (struct live_stub *)live_ctx;
    s->free_calls++;
}


/* ---- Tests --------------------------------------------------------------- */

static int
test_live_create(void)
{
    printf("=== Test 1: BSG_PL_LIVE create / get_data / free ===\n");

    int ctx_a = 1;
    int ctx_b = 2;
    struct bsg_payload *pl = bsg_payload_live_create(&ctx_a, &ctx_b);
    if (!pl) FAIL("bsg_payload_live_create returned NULL");
    if (pl->pl_type != BSG_PL_LIVE) FAIL("wrong payload type");

    struct bsg_live_source *ls = bsg_payload_live_get_data(pl);
    if (!ls) FAIL("bsg_payload_live_get_data returned NULL");
    if (ls->editor_ctx != &ctx_a) FAIL("editor_ctx mismatch");
    if (ls->aux_ctx != &ctx_b) FAIL("aux_ctx mismatch");
    if (ls->owns_live_ctx != 0) FAIL("initial owns_live_ctx should be 0");
    if (ls->last_realized_revision != 0) FAIL("initial last_realized_revision should be 0");

    bsg_payload_free(pl);
    PASS("BSG_PL_LIVE create / get_data / free");
    return 0;
}


static int
test_live_set_ops(void)
{
    printf("=== Test 2: bsg_payload_live_set_ops ===\n");

    struct live_stub stub;
    memset(&stub, 0, sizeof(stub));
    stub.revision = 5;
    stub.update_returns = 1;
    VSET(stub.bmin, -1.0, -2.0, -3.0);
    VSET(stub.bmax, 4.0, 5.0, 6.0);
    stub.pick_hit = 99;
    VSET(stub.snap_out, 0.1, 0.2, 0.3);

    struct bsg_payload *pl = bsg_payload_live_create(NULL, NULL);
    if (!pl) FAIL("create failed");

    /* set_ops with explicit live_ctx = &stub */
    int rc = bsg_payload_live_set_ops(pl,
	    &stub, 0,                /* live_ctx, !owns */
	    stub_revision_cb,
	    stub_update_cb,
	    stub_bounds_cb,
	    stub_pick_cb,
	    stub_snap_cb,
	    stub_free_cb);
    if (!rc) FAIL("bsg_payload_live_set_ops returned 0 (expected 1)");

    struct bsg_live_source *ls = bsg_payload_live_get_data(pl);
    if (!ls) FAIL("get_data after set_ops returned NULL");
    if (ls->live_ctx != &stub) FAIL("live_ctx not stored");
    if (ls->revision_cb != stub_revision_cb) FAIL("revision_cb not stored");
    if (ls->update_cb   != stub_update_cb)   FAIL("update_cb not stored");
    if (ls->bounds_cb   != stub_bounds_cb)   FAIL("bounds_cb not stored");
    if (ls->pick_cb     != stub_pick_cb)     FAIL("pick_cb not stored");
    if (ls->snap_cb     != stub_snap_cb)     FAIL("snap_cb not stored");
    if (ls->free_cb     != stub_free_cb)     FAIL("free_cb not stored");

    bsg_payload_free(pl);
    /* owns_live_ctx == 0 so free_cb should NOT have been called */
    if (stub.free_calls != 0) FAIL("free_cb called despite !owns_live_ctx");

    PASS("bsg_payload_live_set_ops");
    return 0;
}


static int
test_live_revision(void)
{
    printf("=== Test 3: revision monotonicity ===\n");

    struct live_stub stub;
    memset(&stub, 0, sizeof(stub));
    stub.revision = 10;
    stub.update_returns = 1;

    struct bsg_payload *pl = bsg_payload_live_create(NULL, NULL);
    if (!pl) FAIL("create failed");
    bsg_payload_live_set_ops(pl, &stub, 0,
	    stub_revision_cb, stub_update_cb,
	    NULL, NULL, NULL, NULL);

    uint64_t rev = bsg_payload_live_revision(pl);
    if (rev != 10) FAIL("initial revision from revision_cb");

    /* Each realize call advances stub.revision by 1 (inside update_cb) */
    int changed = bsg_payload_live_realize(pl, NULL);
    if (!changed) FAIL("realize should report change (update_cb returns 1)");
    uint64_t rev2 = bsg_payload_live_revision(pl);
    if (rev2 <= 10) FAIL("revision should advance after realize");

    /* A second realize: stub.revision == 12 after first realize, now 13 */
    bsg_payload_live_realize(pl, NULL);
    uint64_t rev3 = bsg_payload_live_revision(pl);
    if (rev3 <= rev2) FAIL("revision not monotonic across second realize");

    bsg_payload_free(pl);
    PASS("revision monotonicity");
    return 0;
}


static int
test_live_realize(void)
{
    printf("=== Test 4: realize semantics ===\n");

    /* Case A: no update_cb — realize returns 0 */
    {
	struct bsg_payload *pl = bsg_payload_live_create(NULL, NULL);
	if (!pl) FAIL("create failed (case A)");
	int rc = bsg_payload_live_realize(pl, NULL);
	if (rc != 0) FAIL("realize with no update_cb should return 0");
	bsg_payload_free(pl);
    }

    /* Case B: update_cb returns 0 and stub.revision stays the same
     * (simulate: nothing changed, revision_cb still returns the same value).
     * In this case realize should return 0. */
    {
	struct live_stub stub;
	memset(&stub, 0, sizeof(stub));
	stub.revision = 3;
	stub.update_returns = 0;

	/* Use a revision_cb that always returns the same value so that
	 * _live_payload_update sees no advancement and leaves pl_revision
	 * unchanged. */
	struct bsg_payload *pl = bsg_payload_live_create(NULL, NULL);
	if (!pl) FAIL("create failed (case B)");

	/* Only set the update_cb (no revision_cb): if update_cb returns 0
	 * and there is no revision_cb, _live_payload_update should NOT
	 * bump pl_revision and realize should return 0. */
	bsg_payload_live_set_ops(pl, &stub, 0,
		NULL,             /* no revision_cb */
		stub_update_cb,   /* returns 0 */
		NULL, NULL, NULL, NULL);
	/* update_cb will increment stub.revision inside but that is not
	 * visible to _live_payload_update because there is no revision_cb. */
	stub.update_returns = 0;
	int rc = bsg_payload_live_realize(pl, NULL);
	/* update_cb returns 0, no revision_cb → live_rev stays at
	 * last_realized_revision → pl_revision unchanged → rc == 0. */
	if (rc != 0) FAIL("realize with no-change update should return 0");
	bsg_payload_free(pl);
    }

    /* Case C: NULL payload — realize returns -1 */
    {
	int rc = bsg_payload_live_realize(NULL, NULL);
	if (rc != -1) FAIL("realize(NULL) should return -1");
    }

    PASS("realize semantics");
    return 0;
}


static int
test_live_bounds(void)
{
    printf("=== Test 5: bounds_cb ===\n");

    struct live_stub stub;
    memset(&stub, 0, sizeof(stub));
    VSET(stub.bmin, -5.0, -6.0, -7.0);
    VSET(stub.bmax,  8.0,  9.0, 10.0);

    struct bsg_payload *pl = bsg_payload_live_create(NULL, NULL);
    if (!pl) FAIL("create failed");
    bsg_payload_live_set_ops(pl, &stub, 0,
	    NULL, NULL, stub_bounds_cb, NULL, NULL, NULL);

    point_t bmin = VINIT_ZERO, bmax = VINIT_ZERO;
    int rc = bsg_payload_live_bounds(pl, &bmin, &bmax);
    if (!rc) FAIL("bounds_cb should return non-zero");
    if (stub.bounds_calls != 1) FAIL("bounds_cb not called exactly once");
    if (!NEAR_EQUAL(bmin[0], -5.0, SMALL_FASTF)) FAIL("bmin[0] wrong");
    if (!NEAR_EQUAL(bmax[2], 10.0, SMALL_FASTF)) FAIL("bmax[2] wrong");

    /* NULL bounds_cb — should return 0 gracefully */
    bsg_payload_live_set_ops(pl, &stub, 0, NULL, NULL, NULL, NULL, NULL, NULL);
    rc = bsg_payload_live_bounds(pl, &bmin, &bmax);
    if (rc != 0) FAIL("bounds with no bounds_cb should return 0");

    bsg_payload_free(pl);
    PASS("bounds_cb");
    return 0;
}


static int
test_live_pick(void)
{
    printf("=== Test 6: pick_cb ===\n");

    struct live_stub stub;
    memset(&stub, 0, sizeof(stub));
    stub.pick_hit = 42;

    struct bsg_payload *pl = bsg_payload_live_create(NULL, NULL);
    if (!pl) FAIL("create failed");
    bsg_payload_live_set_ops(pl, &stub, 0,
	    NULL, NULL, NULL, stub_pick_cb, NULL, NULL);

    int pick_out = -1;
    int rc = bsg_payload_live_pick(pl, NULL, 10, 20, &pick_out);
    if (!rc) FAIL("pick_cb hit should return non-zero");
    if (stub.pick_calls != 1) FAIL("pick_cb not called");
    if (pick_out != 42) FAIL("pick_out not set by callback");

    /* NULL pick_cb — should return 0 gracefully */
    bsg_payload_live_set_ops(pl, &stub, 0, NULL, NULL, NULL, NULL, NULL, NULL);
    rc = bsg_payload_live_pick(pl, NULL, 0, 0, NULL);
    if (rc != 0) FAIL("pick with no pick_cb should return 0");

    bsg_payload_free(pl);
    PASS("pick_cb");
    return 0;
}


static int
test_live_snap(void)
{
    printf("=== Test 7: snap_cb ===\n");

    struct live_stub stub;
    memset(&stub, 0, sizeof(stub));
    VSET(stub.snap_out, 1.5, 2.5, 3.5);

    struct bsg_payload *pl = bsg_payload_live_create(NULL, NULL);
    if (!pl) FAIL("create failed");
    bsg_payload_live_set_ops(pl, &stub, 0,
	    NULL, NULL, NULL, NULL, stub_snap_cb, NULL);

    point_t sample = VINIT_ZERO;
    point_t snapped = VINIT_ZERO;
    int rc = bsg_payload_live_snap(pl, NULL, sample, snapped);
    if (!rc) FAIL("snap_cb should return non-zero");
    if (stub.snap_calls != 1) FAIL("snap_cb not called");
    if (!NEAR_EQUAL(snapped[1], 2.5, SMALL_FASTF)) FAIL("snapped[1] wrong");

    /* NULL snap_cb — should return 0 gracefully */
    bsg_payload_live_set_ops(pl, &stub, 0, NULL, NULL, NULL, NULL, NULL, NULL);
    rc = bsg_payload_live_snap(pl, NULL, sample, snapped);
    if (rc != 0) FAIL("snap with no snap_cb should return 0");

    bsg_payload_free(pl);
    PASS("snap_cb");
    return 0;
}


static int
test_live_teardown(void)
{
    printf("=== Test 8: owns_live_ctx teardown ===\n");

    struct live_stub *stub = (struct live_stub *)bu_calloc(1, sizeof(struct live_stub), "teardown stub");
    stub->revision = 1;

    struct bsg_payload *pl = bsg_payload_live_create(NULL, NULL);
    if (!pl) {
	bu_free(stub, "teardown stub");
	FAIL("create failed");
    }

    /* owns_live_ctx = 1 means free_cb is called on teardown */
    bsg_payload_live_set_ops(pl, stub, 1,
	    stub_revision_cb, NULL, NULL, NULL, NULL,
	    stub_free_cb);

    /* free_cb is not called yet */
    if (stub->free_calls != 0) FAIL("free_cb called too early");

    bsg_payload_free(pl);
    if (stub->free_calls != 1) FAIL("free_cb not called on teardown");

    /* stub was freed by the callback — don't double-free */
    PASS("owns_live_ctx teardown");
    return 0;
}


static int
test_live_partial_ops(void)
{
    printf("=== Test 9: NULL update_cb is handled gracefully ===\n");

    struct live_stub stub;
    memset(&stub, 0, sizeof(stub));
    stub.revision = 99;

    struct bsg_payload *pl = bsg_payload_live_create(NULL, NULL);
    if (!pl) FAIL("create failed");

    /* Only revision_cb is set — update is not available */
    bsg_payload_live_set_ops(pl, &stub, 0,
	    stub_revision_cb, NULL, NULL, NULL, NULL, NULL);

    /* realize without update_cb should return 0 */
    int rc = bsg_payload_live_realize(pl, NULL);
    if (rc != 0) FAIL("realize with NULL update_cb should return 0");

    /* revision_cb should still be readable */
    uint64_t rev = bsg_payload_live_revision(pl);
    if (rev != 99) FAIL("revision_cb should still work without update_cb");

    bsg_payload_free(pl);
    PASS("NULL update_cb handled gracefully");
    return 0;
}


int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);
    if (argc > 1)
	fprintf(stderr, "Unexpected arguments\n");

    int ret = 0;
    ret |= test_live_create();
    ret |= test_live_set_ops();
    ret |= test_live_revision();
    ret |= test_live_realize();
    ret |= test_live_bounds();
    ret |= test_live_pick();
    ret |= test_live_snap();
    ret |= test_live_teardown();
    ret |= test_live_partial_ops();

    return ret;
}
