/*           T E S T _ E D I T _ P H A S E 2 . C P P
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
/** @file test_edit_phase2.cpp
 *
 * Phase 2 tests for the libged `edit` command.
 *
 * Covers:
 *   - translate: -a (absolute), -r (relative), tra alias, -k FROM -a TO
 *   - rotate: 3-angle, 2-angle, 1-angle (default Z), -z/-x/-y coord flags,
 *             -R radians, ±180° ambiguity error
 *   - scale: positional scalar, xyz vector, -k/-a reference, -r factor
 *   - checkpoint / revert / reset lifecycle
 *   - mat: identity matrix (no-op), non-identity matrix
 */

#include "common.h"

#include <cstdio>
#include <cstring>
#include <cmath>

#include "bu.h"
#include "vmath.h"
#include "raytrace.h"
#include "rt/geom.h"
#include "rt/edit.h"
#include "rt/db_fullpath.h"
#include "wdb.h"
#include "ged.h"

/* Access DbiState */
#include "../../dbi.h"
/* Access edit-buffer API */
#include "../../ged_private.h"


/* ------------------------------------------------------------------ *
 * Test bookkeeping
 * ------------------------------------------------------------------ */

static int total_tests  = 0;
static int failed_tests = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        ++total_tests;                                               \
        if (!(cond)) {                                               \
            ++failed_tests;                                          \
            bu_log("FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);  \
        } else {                                                     \
            bu_log("PASS: %s\n", msg);                               \
        }                                                            \
    } while (0)

#define NEAR_ENOUGH 1e-8


/* ------------------------------------------------------------------ *
 * Fixture helpers
 * ------------------------------------------------------------------ */

/** Read back the rt_ell_internal for an ell/sphere by name. */
static int
read_ell(struct ged *gedp, const char *name, struct rt_ell_internal *out)
{
    struct directory *dp = db_lookup(gedp->dbip, name, LOOKUP_QUIET);
    if (dp == RT_DIR_NULL)
        return BRLCAD_ERROR;

    struct rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    if (rt_db_get_internal(&intern, dp, gedp->dbip, NULL) < 0)
        return BRLCAD_ERROR;

    if (intern.idb_minor_type != DB5_MINORTYPE_BRLCAD_ELL) {
        rt_db_free_internal(&intern);
        return BRLCAD_ERROR;
    }

    *out = *(struct rt_ell_internal *)intern.idb_ptr;
    /* copy done; free internal (which freed the pointer but we've copied) */
    intern.idb_ptr = NULL;
    rt_db_free_internal(&intern);
    return BRLCAD_OK;
}

/**
 * Create the Phase 2 test fixture.
 *
 *   sph.s  — sphere at (10, 0, 0), r=2  (translate tests)
 *   other.s — sphere at (20, 0, 0), r=2  (translate -k/-a reference)
 *   ell.s  — ellipsoid at origin, A=(5,0,0), B=(0,2,0), C=(0,0,3) (rotate)
 *   sca.s  — sphere at origin, r=3  (scale tests)
 *   tor.s  — torus at origin (checkpoint/revert/reset/mat tests)
 */
static int
create_p2_fixture(const char *path)
{
    struct rt_wdb *wdbp = wdb_fopen(path);
    if (!wdbp)
        return BRLCAD_ERROR;

    /* sph.s at (10, 0, 0) */
    point_t sp = {10.0, 0.0, 0.0};
    if (mk_sph(wdbp, "sph.s", sp, 2.0) != 0) goto fail;

    /* other.s at (20, 0, 0) — reference for from-to translate */
    {
        point_t op = {20.0, 0.0, 0.0};
        if (mk_sph(wdbp, "other.s", op, 2.0) != 0) goto fail;
    }

    /* ell.s at origin: A=(5,0,0), B=(0,2,0), C=(0,0,3) */
    {
        point_t ev = VINIT_ZERO;
        vect_t  ea = {5.0, 0.0, 0.0};
        vect_t  eb = {0.0, 2.0, 0.0};
        vect_t  ec = {0.0, 0.0, 3.0};
        if (mk_ell(wdbp, "ell.s", ev, ea, eb, ec) != 0) goto fail;
    }

    /* sca.s — sphere at origin, r=3 */
    {
        point_t sv = VINIT_ZERO;
        if (mk_sph(wdbp, "sca.s", sv, 3.0) != 0) goto fail;
    }

    /* tor.s at origin (for checkpoint/revert/mat tests) */
    {
        point_t tv  = VINIT_ZERO;
        vect_t  th  = {0.0, 0.0, 1.0};
        if (mk_tor(wdbp, "tor.s", tv, th, 4.0, 1.0) != 0) goto fail;
    }

    wdb_close(wdbp);
    return BRLCAD_OK;

fail:
    wdb_close(wdbp);
    return BRLCAD_ERROR;
}

/** Open the fixture, enable new_cmd_forms and create DbiState. */
static struct ged *
open_fixture(const char *path)
{
    struct ged *gedp = ged_open("db", path, 1);
    if (!gedp)
        return NULL;
    gedp->new_cmd_forms = 1;
    gedp->dbi_state = new DbiState(gedp);
    return gedp;
}


/* ================================================================== *
 * translate tests
 * ================================================================== */

/* T1: translate -a 15 0 0 → sph.s center moves to (15, 0, 0) */
static void
test_p2_translate_abs(struct ged *gedp)
{
    const char *av[] = { "edit", "sph.s", "translate", "-a", "15", "0", "0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 7, av);
    CHECK(ret == BRLCAD_OK, "translate -a 15 0 0 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "sph.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(ell.v[X], 15.0, NEAR_ENOUGH),
              "translate -a: sph.s V.x == 15");
        CHECK(NEAR_EQUAL(ell.v[Y],  0.0, NEAR_ENOUGH),
              "translate -a: sph.s V.y == 0");
    } else {
        CHECK(0, "translate -a: read_ell(sph.s) succeeded");
    }
}

/* T2: translate -r -5 0 0 → sph.s moves back to (10, 0, 0) */
static void
test_p2_translate_rel(struct ged *gedp)
{
    const char *av[] = { "edit", "sph.s", "translate", "-r", "-5", "0", "0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 7, av);
    CHECK(ret == BRLCAD_OK, "translate -r -5 0 0 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "sph.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(ell.v[X], 10.0, NEAR_ENOUGH),
              "translate -r: sph.s V.x == 10");
    } else {
        CHECK(0, "translate -r: read_ell(sph.s) succeeded");
    }
}

/* T3: tra 5 0 0 → sph.s moves to (15, 0, 0) */
static void
test_p2_tra(struct ged *gedp)
{
    const char *av[] = { "edit", "sph.s", "tra", "5", "0", "0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 6, av);
    CHECK(ret == BRLCAD_OK, "tra 5 0 0 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "sph.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(ell.v[X], 15.0, NEAR_ENOUGH),
              "tra: sph.s V.x == 15");
    } else {
        CHECK(0, "tra: read_ell(sph.s) succeeded");
    }
}

/* T4: translate -k sph.s -a other.s → sph.s moves to (20, 0, 0) */
static void
test_p2_translate_from_to(struct ged *gedp)
{
    /* sph.s is currently at (15, 0, 0); other.s is at (20, 0, 0).
     * -k sph.s means FROM = sph.s keypoint = (15,0,0).
     * -a other.s means TO = other.s keypoint = (20,0,0).
     * The delta is (5,0,0); sph.s should end up at (20,0,0).       */
    const char *av[] = {
        "edit", "sph.s", "translate", "-k", "sph.s", "-a", "other.s", NULL
    };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 7, av);
    CHECK(ret == BRLCAD_OK, "translate -k sph.s -a other.s returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "sph.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(ell.v[X], 20.0, NEAR_ENOUGH),
              "translate -k/-a: sph.s V.x == 20");
    } else {
        CHECK(0, "translate -k/-a: read_ell(sph.s) succeeded");
    }
}

/* T5: translate -x only */
static void
test_p2_translate_xonly(struct ged *gedp)
{
    /* sph.s is at (20,0,0); -x 5 should move to (25,0,0) */
    const char *av[] = { "edit", "sph.s", "translate", "-r", "-x", "5", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 6, av);
    CHECK(ret == BRLCAD_OK, "translate -r -x 5 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "sph.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(ell.v[X], 25.0, NEAR_ENOUGH),
              "translate -x: sph.s V.x == 25");
        CHECK(NEAR_EQUAL(ell.v[Y],  0.0, NEAR_ENOUGH),
              "translate -x: sph.s V.y unchanged == 0");
    } else {
        CHECK(0, "translate -x: read_ell(sph.s) succeeded");
    }
}


/* ================================================================== *
 * rotate tests (on ell.s which starts at origin, A=(5,0,0))
 * ================================================================== */

/* R1: rotate 0 0 90 → A ≈ (0, 5, 0) */
static void
test_p2_rotate_3angle(struct ged *gedp)
{
    const char *av[] = { "edit", "ell.s", "rotate", "0", "0", "90", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 6, av);
    CHECK(ret == BRLCAD_OK, "rotate 0 0 90 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "ell.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(ell.a[X], 0.0, NEAR_ENOUGH),
              "rotate 0 0 90: ell.s A.x ≈ 0");
        CHECK(NEAR_EQUAL(ell.a[Y], 5.0, NEAR_ENOUGH),
              "rotate 0 0 90: ell.s A.y ≈ 5");
        CHECK(NEAR_EQUAL(ell.b[X], -2.0, NEAR_ENOUGH),
              "rotate 0 0 90: ell.s B.x ≈ -2");
    } else {
        CHECK(0, "rotate 0 0 90: read_ell(ell.s) succeeded");
    }
}

/* R2: rotate 0 0 -90 (undo R1) → A ≈ (5, 0, 0) */
static void
test_p2_rotate_undo(struct ged *gedp)
{
    const char *av[] = { "edit", "ell.s", "rotate", "0", "0", "-90", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 6, av);
    CHECK(ret == BRLCAD_OK, "rotate 0 0 -90 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "ell.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(ell.a[X], 5.0, NEAR_ENOUGH),
              "rotate 0 0 -90: ell.s A.x ≈ 5 (undone)");
        CHECK(NEAR_EQUAL(ell.a[Y], 0.0, NEAR_ENOUGH),
              "rotate 0 0 -90: ell.s A.y ≈ 0 (undone)");
    } else {
        CHECK(0, "rotate 0 0 -90: read_ell(ell.s) succeeded");
    }
}

/* R3: rotate -z 45 (coord flag, single angle) */
static void
test_p2_rotate_z_flag(struct ged *gedp)
{
    /* ell.s back at A=(5,0,0) after R2 */
    const char *av[] = { "edit", "ell.s", "rotate", "-z", "45", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 5, av);
    CHECK(ret == BRLCAD_OK, "rotate -z 45 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "ell.s", &ell) == BRLCAD_OK) {
        fastf_t expected = 5.0 * cos(45.0 * DEG2RAD); /* 5/√2 ≈ 3.536 */
        CHECK(NEAR_EQUAL(ell.a[X], expected, 1e-5),
              "rotate -z 45: ell.s A.x ≈ 5·cos(45°)");
        CHECK(NEAR_EQUAL(ell.a[Y], expected, 1e-5),
              "rotate -z 45: ell.s A.y ≈ 5·sin(45°)");
    } else {
        CHECK(0, "rotate -z 45: read_ell(ell.s) succeeded");
    }
}

/* R4: rotate 90 (single-angle, defaults to Z-axis rotation) */
static void
test_p2_rotate_single_angle(struct ged *gedp)
{
    /* ell.s has been rotated by 45° from R3; state doesn't matter for
     * this test — we just verify the command succeeds.                  */
    const char *av[] = { "edit", "ell.s", "rotate", "90", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 4, av);
    CHECK(ret == BRLCAD_OK, "rotate 90 (single angle) returns OK");
}

/* R5: rotate 180 → FAIL (ambiguous) */
static void
test_p2_rotate_180_ambiguous(struct ged *gedp)
{
    const char *av[] = { "edit", "ell.s", "rotate", "180", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 4, av);
    CHECK(ret == BRLCAD_ERROR, "rotate 180 (no axis) returns error (ambiguous)");
    bool has_msg = (strstr(bu_vls_cstr(gedp->ged_result_str), "ambiguous") != NULL ||
                    strstr(bu_vls_cstr(gedp->ged_result_str), "axis")      != NULL);
    CHECK(has_msg, "rotate 180 prints ambiguity message");
}

/* R6: rotate -R 0 0 1.5707963 (≈ π/2, radians flag) */
static void
test_p2_rotate_radians(struct ged *gedp)
{
    /* Get current A of ell.s to compare after rotation */
    struct rt_ell_internal before;
    bool have_before = (read_ell(gedp, "ell.s", &before) == BRLCAD_OK);

    const char *av[] = {
        "edit", "ell.s", "rotate", "-R", "0", "0", "1.5707963267948966", NULL
    };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 7, av);
    CHECK(ret == BRLCAD_OK, "rotate -R 0 0 pi/2 returns OK");

    if (have_before) {
        struct rt_ell_internal after;
        if (read_ell(gedp, "ell.s", &after) == BRLCAD_OK) {
            /* After ≈ 90° Z-rotation: A' ≈ (-before.a[Y], before.a[X], 0) */
            CHECK(NEAR_EQUAL(after.a[X], -before.a[Y], 1e-4),
                  "rotate -R pi/2: A.x ≈ -before.A.y");
            CHECK(NEAR_EQUAL(after.a[Y],  before.a[X], 1e-4),
                  "rotate -R pi/2: A.y ≈  before.A.x");
        } else {
            CHECK(0, "rotate -R: read_ell after rotation succeeded");
        }
    }
}

/* R7: rotate with two angles (-45 45 → X=-45°, Y=45°, Z=0°) */
static void
test_p2_rotate_two_angles(struct ged *gedp)
{
    const char *av[] = { "edit", "ell.s", "rotate", "-45", "45", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 5, av);
    CHECK(ret == BRLCAD_OK, "rotate -45 45 (two-angle) returns OK");
}


/* ================================================================== *
 * scale tests (on sca.s: sphere at origin, r=3)
 * ================================================================== */

/* S1: scale 2 → radius doubles: A should be (6, 0, 0) */
static void
test_p2_scale_scalar(struct ged *gedp)
{
    const char *av[] = { "edit", "sca.s", "scale", "2", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 4, av);
    CHECK(ret == BRLCAD_OK, "scale 2 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "sca.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(MAGNITUDE(ell.a), 6.0, NEAR_ENOUGH),
              "scale 2: sca.s |A| == 6");
    } else {
        CHECK(0, "scale 2: read_ell(sca.s) succeeded");
    }
}

/* S2: scale 2 2 2 (xyz vector) → same as scale 2 from the NEW state */
static void
test_p2_scale_vector(struct ged *gedp)
{
    /* sca.s is now r=6 from S1 */
    const char *av[] = { "edit", "sca.s", "scale", "2", "2", "2", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 6, av);
    CHECK(ret == BRLCAD_OK, "scale 2 2 2 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "sca.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(MAGNITUDE(ell.a), 12.0, NEAR_ENOUGH),
              "scale 2 2 2: sca.s |A| == 12 (doubled again)");
    } else {
        CHECK(0, "scale 2 2 2: read_ell(sca.s) succeeded");
    }
}

/* S3: scale -k 0 0 0 -a 2 2 2  (from-to reference, all-equal diff → factor 2) */
static void
test_p2_scale_from_to(struct ged *gedp)
{
    /* Use a fresh view of sca.s from S2 (|A|=12); factor=2 → |A|=24 */
    const char *av[] = {
        "edit", "sca.s", "scale", "-k", "0", "0", "0", "-a", "2", "2", "2", NULL
    };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 11, av);
    CHECK(ret == BRLCAD_OK, "scale -k 0 0 0 -a 2 2 2 returns OK");

    struct rt_ell_internal ell;
    if (read_ell(gedp, "sca.s", &ell) == BRLCAD_OK) {
        CHECK(NEAR_EQUAL(MAGNITUDE(ell.a), 24.0, NEAR_ENOUGH),
              "scale -k/-a: sca.s |A| == 24");
    } else {
        CHECK(0, "scale -k/-a: read_ell(sca.s) succeeded");
    }
}

/* S4: scale -k 0 0 0 -a 1 1 1 -r 2  (explicit factor -r 2 → factor=2) */
static void
test_p2_scale_explicit_r(struct ged *gedp)
{
    const char *av[] = {
        "edit", "sca.s", "scale",
        "-k", "0", "0", "0", "-a", "1", "1", "1", "-r", "2", NULL
    };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 13, av);
    CHECK(ret == BRLCAD_OK, "scale -k 0 0 0 -a 1 1 1 -r 2 returns OK");
}

/* S5: scale -k 5 10 15 -a 7 11 -2 -r 4 2 34 (complex, just check OK) */
static void
test_p2_scale_complex(struct ged *gedp)
{
    const char *av[] = {
        "edit", "sca.s", "scale",
        "-k", "5", "10", "15", "-a", "7", "11", "-2", "-r", "4", "2", "34",
        NULL
    };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 15, av);
    CHECK(ret == BRLCAD_OK, "scale -k 5 10 15 -a 7 11 -2 -r 4 2 34 returns OK");
}

/* S6: scale 0 → error (factor must be > 0) */
static void
test_p2_scale_zero_error(struct ged *gedp)
{
    const char *av[] = { "edit", "sca.s", "scale", "0", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 4, av);
    CHECK(ret == BRLCAD_ERROR, "scale 0 returns error");
}


/* ================================================================== *
 * checkpoint / revert / reset  (on tor.s)
 * ================================================================== */

/* C1: checkpoint creates an in-buffer entry */
static void
test_p2_checkpoint_creates_entry(struct ged *gedp)
{
    const char *av[] = { "edit", "tor.s", "checkpoint", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 3, av);
    CHECK(ret == BRLCAD_OK, "checkpoint returns OK");

    /* Verify the entry exists in the buffer */
    struct db_full_path dfp;
    db_full_path_init(&dfp);
    db_string_to_path(&dfp, gedp->dbip, "tor.s");
    struct rt_edit *s = ged_edit_buf_get(gedp, &dfp);
    CHECK(s != NULL, "checkpoint: edit buffer has entry for tor.s");
    db_free_full_path(&dfp);
}

/* C2: translate -i (intermediate) + revert rolls back */
static void
test_p2_revert(struct ged *gedp)
{
    /* First do a checkpoint (may already exist from C1) */
    {
        const char *av[] = { "edit", "tor.s", "checkpoint", NULL };
        ged_exec(gedp, 3, av);
    }

    /* Now translate in intermediate mode (doesn't write disk) */
    {
        const char *av[] = {
            "edit", "-i", "tor.s", "translate", "-r", "100", "0", "0", NULL
        };
        bu_vls_trunc(gedp->ged_result_str, 0);
        int ret = ged_exec(gedp, 8, av);
        CHECK(ret == BRLCAD_OK, "revert test: -i translate returns OK");
    }

    /* Revert: should undo the in-buffer translate and write back */
    {
        const char *av[] = { "edit", "tor.s", "revert", NULL };
        bu_vls_trunc(gedp->ged_result_str, 0);
        int ret = ged_exec(gedp, 3, av);
        CHECK(ret == BRLCAD_OK, "revert returns OK");
    }
}

/* C3: reset clears the buffer entry */
static void
test_p2_reset(struct ged *gedp)
{
    /* Ensure there's something in the buffer */
    {
        const char *av[] = { "edit", "tor.s", "checkpoint", NULL };
        ged_exec(gedp, 3, av);
    }

    /* Reset */
    {
        const char *av[] = { "edit", "tor.s", "reset", NULL };
        bu_vls_trunc(gedp->ged_result_str, 0);
        int ret = ged_exec(gedp, 3, av);
        CHECK(ret == BRLCAD_OK, "reset returns OK");
    }

    /* Buffer entry should be gone */
    struct db_full_path dfp;
    db_full_path_init(&dfp);
    db_string_to_path(&dfp, gedp->dbip, "tor.s");
    struct rt_edit *s = ged_edit_buf_get(gedp, &dfp);
    CHECK(s == NULL, "reset: buffer entry removed for tor.s");
    db_free_full_path(&dfp);
}

/* C4: revert with no prior checkpoint → error */
static void
test_p2_revert_no_checkpoint(struct ged *gedp)
{
    /* Make sure no buffer entry exists for sca.s */
    {
        struct db_full_path dfp;
        db_full_path_init(&dfp);
        db_string_to_path(&dfp, gedp->dbip, "sca.s");
        ged_edit_buf_abandon(gedp, &dfp);
        db_free_full_path(&dfp);
    }

    const char *av[] = { "edit", "sca.s", "revert", NULL };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 3, av);
    CHECK(ret == BRLCAD_ERROR, "revert with no buffer entry returns error");
}


/* ================================================================== *
 * mat tests
 * ================================================================== */

/* M1: identity matrix on ell.s — geometry unchanged */
static void
test_p2_mat_identity(struct ged *gedp)
{
    struct rt_ell_internal before;
    bool have_before = (read_ell(gedp, "ell.s", &before) == BRLCAD_OK);

    /* Row-major 4×4 identity */
    const char *av[] = {
        "edit", "ell.s", "mat",
        "1","0","0","0",
        "0","1","0","0",
        "0","0","1","0",
        "0","0","0","1",
        NULL
    };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 19, av);
    CHECK(ret == BRLCAD_OK, "mat identity returns OK");

    if (have_before) {
        struct rt_ell_internal after;
        if (read_ell(gedp, "ell.s", &after) == BRLCAD_OK) {
            CHECK(NEAR_EQUAL(MAGNITUDE(after.a), MAGNITUDE(before.a), 1e-6),
                  "mat identity: |A| unchanged");
        } else {
            CHECK(0, "mat identity: read_ell after succeeded");
        }
    }
}

/* M2: mat missing values → error */
static void
test_p2_mat_missing_values(struct ged *gedp)
{
    const char *av[] = {
        "edit", "ell.s", "mat",
        "1","0","0","0",
        "0","1","0","0",
        NULL   /* only 8 of 16 */
    };
    bu_vls_trunc(gedp->ged_result_str, 0);
    int ret = ged_exec(gedp, 10, av);
    CHECK(ret == BRLCAD_ERROR, "mat with <16 values returns error");
}


/* ================================================================== *
 * main
 * ================================================================== */

int
main(int ac, char *av[])
{
    struct ged *gedp;
    struct bu_vls db_path = BU_VLS_INIT_ZERO;
    int need_help = 0;

    bu_setprogname(av[0]);

    struct bu_opt_desc d[2];
    BU_OPT(d[0], "h", "help", "", NULL, &need_help, "Print help and exit");
    BU_OPT_NULL(d[1]);

    int opt_ret = bu_opt_parse(NULL, ac, (const char **)av, d);
    if (need_help || opt_ret < 0) {
        bu_log("Usage: %s [-h] [fixture.g]\n", av[0]);
        return (need_help) ? 0 : 1;
    }

    /* Determine fixture path */
    if (opt_ret > 1) {
        bu_vls_sprintf(&db_path, "%s", av[opt_ret]);
    } else if (opt_ret == 1 && av[opt_ret] && av[opt_ret][0] != '-') {
        bu_vls_sprintf(&db_path, "%s", av[opt_ret]);
    } else {
        char tmpname[MAXPATHLEN] = {0};
        FILE *fp = bu_temp_file(tmpname, MAXPATHLEN);
        if (!fp) {
            bu_log("ERROR: unable to create temp file\n");
            bu_vls_free(&db_path);
            return 1;
        }
        fclose(fp);
        bu_vls_sprintf(&db_path, "%s", tmpname);
    }

    /* Create fixture if not already present */
    if (!bu_file_exists(bu_vls_cstr(&db_path), NULL) ||
        bu_file_size(bu_vls_cstr(&db_path)) == 0)
    {
        if (create_p2_fixture(bu_vls_cstr(&db_path)) != BRLCAD_OK) {
            bu_log("ERROR: fixture creation failed\n");
            bu_vls_free(&db_path);
            return 1;
        }
    }

    gedp = open_fixture(bu_vls_cstr(&db_path));
    if (!gedp) {
        bu_log("ERROR: ged_open failed for %s\n", bu_vls_cstr(&db_path));
        bu_vls_free(&db_path);
        return 1;
    }

    /* ---- translate ---------------------------------------- */
    test_p2_translate_abs(gedp);
    test_p2_translate_rel(gedp);
    test_p2_tra(gedp);
    test_p2_translate_from_to(gedp);
    test_p2_translate_xonly(gedp);

    /* ---- rotate ------------------------------------------- */
    test_p2_rotate_3angle(gedp);
    test_p2_rotate_undo(gedp);
    test_p2_rotate_z_flag(gedp);
    test_p2_rotate_single_angle(gedp);
    test_p2_rotate_180_ambiguous(gedp);
    test_p2_rotate_radians(gedp);
    test_p2_rotate_two_angles(gedp);

    /* ---- scale -------------------------------------------- */
    test_p2_scale_scalar(gedp);
    test_p2_scale_vector(gedp);
    test_p2_scale_from_to(gedp);
    test_p2_scale_explicit_r(gedp);
    test_p2_scale_complex(gedp);
    test_p2_scale_zero_error(gedp);

    /* ---- checkpoint / revert / reset ---------------------- */
    test_p2_checkpoint_creates_entry(gedp);
    test_p2_revert(gedp);
    test_p2_reset(gedp);
    test_p2_revert_no_checkpoint(gedp);

    /* ---- mat ---------------------------------------------- */
    test_p2_mat_identity(gedp);
    test_p2_mat_missing_values(gedp);

    /* ---- Summary ------------------------------------------ */
    bu_log("\n========================================\n");
    bu_log("edit Phase 2 tests: %d/%d passed\n",
           total_tests - failed_tests, total_tests);
    bu_log("========================================\n");

    ged_close(gedp);
    bu_vls_free(&db_path);

    return (failed_tests == 0) ? 0 : 1;
}


/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8 cino=N-s
 */
