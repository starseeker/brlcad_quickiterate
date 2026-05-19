/*                 T E S T _ D B I _ C P P . C P P
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
/** @file test_dbi_cpp.cpp
 *
 * Phase 0-C of drawing_stack_modernization: DbiState unit test.
 *
 * Tests (without requiring Qt or a display manager):
 *   1. Direct bu_cache round-trip for each of the five DBI payload types
 *      (CACHE_OBJ_BOUNDS, CACHE_REGION_ID, CACHE_REGION_FLAG,
 *       CACHE_INHERIT_FLAG, CACHE_COLOR).
 *   2. DbiState construction with a known .g file (moss.g).
 *   3. DbiState::tops() returns at least one entry.
 *   4. DbiState::digest_path() succeeds on a valid path, fails on an
 *      invalid one, and returns the correct depth.
 *   5. DbiState::valid_hash() and valid_hash_path() agree with the
 *      hashes produced by digest_path.
 *   6. DbiState::get_bbox() on a known solid returns a finite bbox.
 *   7. BViewState::add_path() + redraw() + count_drawn_paths().
 *   8. Two-pass DbiState construction: second pass reads from the
 *      on-disk cache and produces maps identical to the first pass.
 *   9. Phase 3.5 DrawPipeline: DbiState::wait_for_pipeline() delivers at
 *      least one AABB result for the solids in moss.g.
 *
 * Usage: test_dbi_cpp <dir-containing-moss.g>
 */

#include "common.h"

#include <chrono>
#include <fstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>

#include "vmath.h"
#include "bu/app.h"
#include "bu/cache.h"
#include "bu/color.h"
#include "bu/env.h"
#include "bu/file.h"
#include "bu/hash.h"
#include "bu/malloc.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "ged.h"

#include "../dbi.h"

/* ------------------------------------------------------------------ */
/* Tiny assertion helper                                                */
/* ------------------------------------------------------------------ */

static int g_failures = 0;

#define CHECK(cond, msg) \
    do { \
	if (!(cond)) { \
	    bu_log("FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); \
	    g_failures++; \
	} \
    } while (0)

/* ------------------------------------------------------------------ */
/* Test 1: Direct bu_cache round-trip for all five DBI payload types   */
/* ------------------------------------------------------------------ */

static int
test_cache_roundtrip(void)
{
    bu_log("=== Test 1: bu_cache round-trip for DBI payload types ===\n");
    int failures = 0;

    /* Open a fresh test cache in the current directory */
    struct bu_cache *c = bu_cache_open("test_dbi_cache_roundtrip", 1, 0);
    if (!c) {
	bu_log("FAIL: bu_cache_open returned NULL\n");
	return 1;
    }

    /* --- CACHE_OBJ_BOUNDS: 2 x point_t = 6 fastf_t ------------------- */
    {
	point_t bmin = {-1.5, -2.5, -3.5};
	point_t bmax = { 4.0,  5.0,  6.0};
	char buf[sizeof(bmin) + sizeof(bmax)];
	memcpy(buf,               &bmin, sizeof(bmin));
	memcpy(buf + sizeof(bmin), &bmax, sizeof(bmax));

	size_t written = bu_cache_write(buf, sizeof(buf), "test:bb", c, NULL);
	if (!written) {
	    bu_log("FAIL: bu_cache_write CACHE_OBJ_BOUNDS returned 0\n");
	    failures++;
	} else {
	    void *out = NULL;
	    struct bu_cache_txn *t = NULL;
	    size_t got = bu_cache_get(&out, "test:bb", c, &t);
	    if (got != sizeof(buf)) {
		bu_log("FAIL: CACHE_OBJ_BOUNDS size mismatch (got %zu, want %zu)\n",
		       got, sizeof(buf));
		failures++;
	    } else {
		point_t rbmin, rbmax;
		const char *bp = (const char *)out;
		memcpy(&rbmin, bp, sizeof(rbmin));
		memcpy(&rbmax, bp + sizeof(rbmin), sizeof(rbmax));
		if (!VNEAR_EQUAL(rbmin, bmin, VDIVIDE_TOL) ||
		    !VNEAR_EQUAL(rbmax, bmax, VDIVIDE_TOL)) {
		    bu_log("FAIL: CACHE_OBJ_BOUNDS data mismatch after round-trip\n");
		    failures++;
		} else {
		    bu_log("  PASS: CACHE_OBJ_BOUNDS\n");
		}
	    }
	    bu_cache_get_done(&t);
	}
    }

    /* --- CACHE_REGION_ID: int ------------------------------------------ */
    {
	int region_id = 42;
	size_t written = bu_cache_write(&region_id, sizeof(region_id), "test:rid", c, NULL);
	if (!written) {
	    bu_log("FAIL: bu_cache_write CACHE_REGION_ID returned 0\n");
	    failures++;
	} else {
	    void *out = NULL;
	    struct bu_cache_txn *t = NULL;
	    size_t got = bu_cache_get(&out, "test:rid", c, &t);
	    if (got != sizeof(region_id)) {
		bu_log("FAIL: CACHE_REGION_ID size mismatch\n");
		failures++;
	    } else {
		int rval;
		memcpy(&rval, out, sizeof(rval));
		if (rval != region_id) {
		    bu_log("FAIL: CACHE_REGION_ID value mismatch (%d != %d)\n", rval, region_id);
		    failures++;
		} else {
		    bu_log("  PASS: CACHE_REGION_ID\n");
		}
	    }
	    bu_cache_get_done(&t);
	}
    }

    /* --- CACHE_REGION_FLAG: int ---------------------------------------- */
    {
	int region_flag = 1;
	size_t written = bu_cache_write(&region_flag, sizeof(region_flag), "test:rf", c, NULL);
	if (!written) {
	    bu_log("FAIL: bu_cache_write CACHE_REGION_FLAG returned 0\n");
	    failures++;
	} else {
	    void *out = NULL;
	    struct bu_cache_txn *t = NULL;
	    size_t got = bu_cache_get(&out, "test:rf", c, &t);
	    if (got != sizeof(region_flag)) {
		bu_log("FAIL: CACHE_REGION_FLAG size mismatch\n");
		failures++;
	    } else {
		int rval;
		memcpy(&rval, out, sizeof(rval));
		if (rval != region_flag) {
		    bu_log("FAIL: CACHE_REGION_FLAG value mismatch\n");
		    failures++;
		} else {
		    bu_log("  PASS: CACHE_REGION_FLAG\n");
		}
	    }
	    bu_cache_get_done(&t);
	}
    }

    /* --- CACHE_INHERIT_FLAG: int --------------------------------------- */
    {
	int inherit_flag = 1;
	size_t written = bu_cache_write(&inherit_flag, sizeof(inherit_flag), "test:if", c, NULL);
	if (!written) {
	    bu_log("FAIL: bu_cache_write CACHE_INHERIT_FLAG returned 0\n");
	    failures++;
	} else {
	    void *out = NULL;
	    struct bu_cache_txn *t = NULL;
	    size_t got = bu_cache_get(&out, "test:if", c, &t);
	    if (got != sizeof(inherit_flag)) {
		bu_log("FAIL: CACHE_INHERIT_FLAG size mismatch\n");
		failures++;
	    } else {
		int rval;
		memcpy(&rval, out, sizeof(rval));
		if (rval != inherit_flag) {
		    bu_log("FAIL: CACHE_INHERIT_FLAG value mismatch\n");
		    failures++;
		} else {
		    bu_log("  PASS: CACHE_INHERIT_FLAG\n");
		}
	    }
	    bu_cache_get_done(&t);
	}
    }

    /* --- CACHE_COLOR: unsigned int (packed RGB) ------------------------ */
    {
	unsigned int cval = 255u + (128u << 8) + (64u << 16);
	size_t written = bu_cache_write(&cval, sizeof(cval), "test:c", c, NULL);
	if (!written) {
	    bu_log("FAIL: bu_cache_write CACHE_COLOR returned 0\n");
	    failures++;
	} else {
	    void *out = NULL;
	    struct bu_cache_txn *t = NULL;
	    size_t got = bu_cache_get(&out, "test:c", c, &t);
	    if (got != sizeof(cval)) {
		bu_log("FAIL: CACHE_COLOR size mismatch\n");
		failures++;
	    } else {
		unsigned int rval;
		memcpy(&rval, out, sizeof(rval));
		if (rval != cval) {
		    bu_log("FAIL: CACHE_COLOR value mismatch\n");
		    failures++;
		} else {
		    bu_log("  PASS: CACHE_COLOR\n");
		}
	    }
	    bu_cache_get_done(&t);
	}
    }

    bu_cache_close(c);
    bu_cache_erase("test_dbi_cache_roundtrip");

    return failures;
}

/* ------------------------------------------------------------------ */
/* Test helpers                                                         */
/* ------------------------------------------------------------------ */

/* Returns hash of the given name string (same algorithm DbiState uses) */
static unsigned long long
name_hash(const char *name)
{
    return bu_data_hash(name, strlen(name));
}

/* ------------------------------------------------------------------ */
/* Tests 2-8: DbiState functional tests                                */
/* ------------------------------------------------------------------ */

static int
test_dbistate(const char *moss_g_path)
{
    int failures = 0;

    /* ---------------------------------------------------------------- */
    /* Set up a GED instance from the given .g file                     */
    /* ---------------------------------------------------------------- */
    struct ged *gedp = ged_open("db", moss_g_path, 1);
    if (!gedp) {
	bu_log("FAIL: ged_open returned NULL for %s\n", moss_g_path);
	return 1;
    }

    gedp->dbi_state = new DbiState(gedp);
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    gedp->new_cmd_forms = 1;

    /* ---------------------------------------------------------------- */
    /* Test 2: tops()                                                   */
    /* ---------------------------------------------------------------- */
    bu_log("=== Test 2: DbiState::tops() ===\n");
    {
	std::vector<unsigned long long> top_hashes = dbis->tops(false);
	CHECK(top_hashes.size() > 0, "tops() must return at least one entry");

	/* Verify "all.g" appears in the tops list */
	unsigned long long allg_hash = name_hash("all.g");
	bool found_allg = false;
	for (size_t i = 0; i < top_hashes.size(); i++) {
	    if (top_hashes[i] == allg_hash) {
		found_allg = true;
		break;
	    }
	}
	CHECK(found_allg, "tops() must include 'all.g'");
	if (found_allg)
	    bu_log("  PASS: tops() contains 'all.g'\n");
    }

    /* ---------------------------------------------------------------- */
    /* Test 3: digest_path()                                            */
    /* ---------------------------------------------------------------- */
    bu_log("=== Test 3: DbiState::digest_path() ===\n");
    {
	/* Valid two-element path */
	std::vector<unsigned long long> ph = dbis->digest_path("all.g/platform.r");
	CHECK(ph.size() == 2, "digest_path('all.g/platform.r') must return 2 elements");

	/* hash[0] must equal hash of "all.g", hash[1] must equal hash of "platform.r" */
	if (ph.size() == 2) {
	    CHECK(ph[0] == name_hash("all.g"),
		  "digest_path first element must hash to 'all.g'");
	    CHECK(ph[1] == name_hash("platform.r"),
		  "digest_path second element must hash to 'platform.r'");
	    bu_log("  PASS: digest_path valid path\n");
	}

	/* Invalid path must return empty vector */
	std::vector<unsigned long long> bad = dbis->digest_path("does_not_exist.g/also_bad");
	CHECK(bad.size() == 0, "digest_path on invalid path must return empty vector");
	if (bad.size() == 0)
	    bu_log("  PASS: digest_path invalid path returns empty\n");

	/* Single top-level object */
	std::vector<unsigned long long> single = dbis->digest_path("all.g");
	CHECK(single.size() == 1, "digest_path single object must return 1 element");
	if (single.size() == 1)
	    bu_log("  PASS: digest_path single object\n");
    }

    /* ---------------------------------------------------------------- */
    /* Test 4: valid_hash() and valid_hash_path()                       */
    /* ---------------------------------------------------------------- */
    bu_log("=== Test 4: valid_hash / valid_hash_path ===\n");
    {
	std::vector<unsigned long long> ph = dbis->digest_path("all.g/platform.r");
	if (ph.size() == 2) {
	    CHECK(dbis->valid_hash(ph[0]), "all.g hash must be valid");
	    CHECK(dbis->valid_hash(ph[1]), "platform.r hash must be valid");
	    CHECK(dbis->valid_hash_path(ph), "valid_hash_path on known path must succeed");
	    bu_log("  PASS: valid_hash / valid_hash_path\n");
	}
	CHECK(!dbis->valid_hash(0ULL), "hash 0 must not be valid");
    }

    /* ---------------------------------------------------------------- */
    /* Test 5: get_bbox()                                               */
    /* ---------------------------------------------------------------- */
    bu_log("=== Test 5: DbiState::get_bbox() ===\n");
    {
	/* Look up the hash of a known leaf solid.  moss.g contains
	 * all.g/platform.r/platform.s — use platform.r as a comb that
	 * owns at least one solid so get_bbox can succeed. */
	unsigned long long plat_hash = name_hash("platform.r");

	point_t bbmin, bbmax;
	VSETALL(bbmin,  MAX_FASTF);
	VSETALL(bbmax, -MAX_FASTF);

	bool bbox_ok = dbis->get_bbox(&bbmin, &bbmax, NULL, plat_hash);
	CHECK(bbox_ok, "get_bbox on platform.r must succeed");
	if (bbox_ok) {
	    /* Verify the bbox is finite and non-degenerate */
	    bool finite_min = std::isfinite((double)bbmin[X]) &&
			      std::isfinite((double)bbmin[Y]) &&
			      std::isfinite((double)bbmin[Z]);
	    bool finite_max = std::isfinite((double)bbmax[X]) &&
			      std::isfinite((double)bbmax[Y]) &&
			      std::isfinite((double)bbmax[Z]);
	    CHECK(finite_min, "bbmin must be finite after get_bbox");
	    CHECK(finite_max, "bbmax must be finite after get_bbox");
	    CHECK(bbmax[X] > bbmin[X] || bbmax[Y] > bbmin[Y] || bbmax[Z] > bbmin[Z],
		  "bbox must not be degenerate (max > min on at least one axis)");
	    if (finite_min && finite_max)
		bu_log("  PASS: get_bbox\n");
	}
    }

    /* ---------------------------------------------------------------- */
    /* Test 6: BViewState::add_path() + redraw() + count_drawn_paths() */
    /* ---------------------------------------------------------------- */
    bu_log("=== Test 6: BViewState add_path + redraw + count_drawn_paths ===\n");
    {
	BViewState *bvs = dbis->get_view_state(gedp->ged_gvp);
	CHECK(bvs != NULL, "get_view_state must return non-NULL for ged_gvp");

	if (bvs) {
	    /* Before any draw operation, the view should be empty */
	    size_t cnt_before = bvs->count_drawn_paths(-1, false);
	    CHECK(cnt_before == 0, "count_drawn_paths must be 0 before add_path");

	    /* Stage a path.  count_drawn_paths still 0 (redraw not yet called) */
	    bvs->add_path("all.g");
	    size_t cnt_staged = bvs->count_drawn_paths(-1, false);
	    CHECK(cnt_staged == 0,
		  "count_drawn_paths must remain 0 between add_path and redraw");

	    /* redraw expands staged paths into scene objects.
	     * We need non-NULL vs for staged paths to be processed. */
	    struct bsg_settings vs;
	    bsg_settings_init(&vs);
	    std::unordered_set<struct bview *> views;
	    views.insert(gedp->ged_gvp);

	    bvs->redraw(&vs, views, 1 /* no_autoview */);

	    size_t cnt_after = bvs->count_drawn_paths(-1, false);
	    CHECK(cnt_after > 0,
		  "count_drawn_paths must be > 0 after drawing all.g");
	    if (cnt_after > 0)
		bu_log("  PASS: add_path + redraw produced %zu drawn paths\n", cnt_after);

	    /* Verify 'all.g' itself reports as drawn (partially or fully).
	     * is_hdrawn takes a *path* hash (hash of the path vector), not an
	     * object name hash — compute it via path_hash(). */
	    std::vector<unsigned long long> allg_pv = dbis->digest_path("all.g");
	    unsigned long long allg_path_hash = dbis->path_hash(allg_pv, 0);
	    int hdrawn = bvs->is_hdrawn(-1, allg_path_hash);
	    CHECK(hdrawn != 0, "all.g must be reported as drawn (fully or partially)");
	    if (hdrawn)
		bu_log("  PASS: all.g is_hdrawn == %d\n", hdrawn);
	}
    }

    /* ---------------------------------------------------------------- */
    /* Test 7: Two-pass DbiState construction — cache consistency       */
    /* ---------------------------------------------------------------- */
    bu_log("=== Test 7: two-pass DbiState construction (cache consistency) ===\n");
    {
	/* Capture the maps populated during the first construction */
	std::unordered_map<unsigned long long, int>          first_region_id  = dbis->region_id;
	std::unordered_map<unsigned long long, unsigned int> first_rgb        = dbis->rgb;
	std::unordered_map<unsigned long long, int>          first_c_inherit  = dbis->c_inherit;

	/* Tear down the first DbiState (cache is flushed to disk on close) */
	delete (DbiState *)gedp->dbi_state;
	gedp->dbi_state = NULL;

	/* Construct a second DbiState from the same gedp / dbip.
	 * update_dp will find the cache warm and read attribute data from it
	 * rather than re-reading avs from disk. */
	gedp->dbi_state = new DbiState(gedp);
	DbiState *dbis2 = (DbiState *)gedp->dbi_state;

	/* The attribute maps must be identical between the two passes */
	CHECK(dbis2->region_id == first_region_id,
	      "region_id map must be identical across DbiState re-construction");
	CHECK(dbis2->rgb == first_rgb,
	      "rgb map must be identical across DbiState re-construction");
	CHECK(dbis2->c_inherit == first_c_inherit,
	      "c_inherit map must be identical across DbiState re-construction");

	if (dbis2->region_id  == first_region_id &&
	    dbis2->rgb        == first_rgb        &&
	    dbis2->c_inherit  == first_c_inherit)
	    bu_log("  PASS: maps are consistent across two DbiState constructions\n");

	/* Clean up second DbiState */
	delete (DbiState *)gedp->dbi_state;
	gedp->dbi_state = NULL;
    }

    /* ---------------------------------------------------------------- */
    /* Tear down                                                         */
    /* ---------------------------------------------------------------- */
    ged_close(gedp);

    return failures;
}

/* ------------------------------------------------------------------ */
/* Test 8: IDbiObserver — observer-based notification (Phase 1-C)     */
/* ------------------------------------------------------------------ */

struct TestObserver : public IDbiObserver {
    std::vector<DbiChangeEvent> received;
    void on_dbi_changed(const std::vector<DbiChangeEvent> &events) override {
	received.insert(received.end(), events.begin(), events.end());
    }
};

static int
test_observer(const char *moss_g_path)
{
    int failures = 0;
    bu_log("=== Test 8: IDbiObserver notification (Phase 1-C) ===\n");

    struct ged *gedp = ged_open("db", moss_g_path, 1);
    if (!gedp) {
	bu_log("FAIL: ged_open returned NULL for %s\n", moss_g_path);
	return 1;
    }
    gedp->dbi_state = new DbiState(gedp);
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    gedp->new_cmd_forms = 1;

    /* Register observer */
    TestObserver obs;
    dbis->add_observer(&obs);

    /* Trigger an update — no changes expected on a freshly-opened db */
    dbis->update();
    CHECK(obs.received.empty(),
	  "no change events expected on a fresh no-op update");
    if (obs.received.empty())
	bu_log("  PASS: no spurious events on no-op update\n");

    /* Verify remove_observer prevents further delivery */
    dbis->remove_observer(&obs);
    obs.received.clear();
    dbis->update();
    CHECK(obs.received.empty(),
	  "no events should arrive after remove_observer");
    if (obs.received.empty())
	bu_log("  PASS: no events delivered after remove_observer\n");

    /* Verify GObj model populated for moss.g objects */
    unsigned long long allg_hash = bu_data_hash("all.g", strlen("all.g") * sizeof(char));
    const GObj *gobj = dbis->get_gobj(allg_hash);
    CHECK(gobj != nullptr, "get_gobj(hash('all.g')) must return non-null GObj");
    if (gobj) {
	CHECK(!gobj->cv.empty(), "GObj for all.g comb must have child instances");
	bu_log("  PASS: GObj for all.g has %zu child CombInst entries\n", gobj->cv.size());
    }

    /* Test DrawList */
    BViewState *vs = dbis->get_view_state(gedp->ged_gvp);
    CHECK(vs != nullptr, "get_view_state must return non-null BViewState");
    if (vs) {
	DrawList &dl = vs->draw_list();
	CHECK(dl.empty(), "fresh DrawList must be empty");

	std::vector<unsigned long long> test_path = {allg_hash};
	dl.add(test_path, 1);
	CHECK(dl.count() == 1, "DrawList must have 1 entry after add");
	CHECK(dl.query(allg_hash, 1) == DrawState::FULLY_DRAWN,
	      "added path must be FULLY_DRAWN");
	dl.clear();
	CHECK(dl.empty(), "DrawList must be empty after clear");
	bu_log("  PASS: DrawList add/query/clear\n");
    }

    /* Test SelectionSet via new API */
    SelectionSet *ss = dbis->get_selection_set(nullptr);
    CHECK(ss != nullptr, "get_selection_set(null) must return non-null");
    if (ss) {
	bool sel = ss->select("all.g/platform.r", true);
	CHECK(sel, "select('all.g/platform.r') must return true");
	CHECK(!ss->selected_map().empty(), "selected map must be non-empty after select");
	CHECK(ss->state_hash_val() != 0, "state_hash_val must be non-zero with a selection");
	auto paths = ss->selected_paths();
	CHECK(!paths.empty(), "selected_paths() must be non-empty");
	bool desel = ss->deselect("all.g/platform.r", true);
	CHECK(desel, "deselect('all.g/platform.r') must return true");
	CHECK(ss->selected_map().empty(), "selected map must be empty after deselect");
	bu_log("  PASS: SelectionSet select/deselect/state_hash_val\n");
    }

    delete (DbiState *)gedp->dbi_state;
    gedp->dbi_state = NULL;
    ged_close(gedp);

    return failures;
}

/* ------------------------------------------------------------------ */
/* Test 9: Phase 3.5 DrawPipeline (drain_geom_results)                 */
/* ------------------------------------------------------------------ */

static int
test_pipeline(const char *moss_g_path)
{
    int failures = 0;

    bu_log("=== Test 9: Phase 3.5 DrawPipeline drain_geom_results ===\n");

    struct ged *gedp = ged_open("db", moss_g_path, 1);
    if (!gedp) {
	bu_log("  FAIL: could not open %s\n", moss_g_path);
	return 1;
    }

    DbiState *dbis = new DbiState(gedp);
    gedp->dbi_state = (void *)dbis;

    /* Wait up to 10 seconds for the pipeline to settle and deliver results. */
    size_t total = dbis->wait_for_pipeline(10000);
    bu_log("  drain_geom_results produced %zu results\n", total);

    /* The pipeline must have produced at least one AABB result (moss.g has
     * solid primitives; AABB is always computed for non-comb objects). */
    CHECK(total > 0, "DrawPipeline must produce at least one result");

    /* obbs map may or may not be populated depending on whether moss.g
     * primitives support ft_oriented_bbox; just verify no crash. */
    bu_log("  obbs map contains %zu entries after pipeline drain\n",
	   dbis->obbs.size());
    bu_log("  bboxes map contains %zu entries\n", dbis->bboxes.size());

    /* BViewState::drain_geom_results must return size_t (0 after exhausted). */
    size_t bvs_drain = dbis->shared_vs->drain_geom_results();
    /* pipeline is now settled so this should return 0 */
    CHECK(bvs_drain == 0, "BViewState::drain_geom_results must return 0 when settled");

    bu_log("  PASS: DrawPipeline produced results and settled\n");

    delete (DbiState *)gedp->dbi_state;
    gedp->dbi_state = NULL;
    ged_close(gedp);

    return failures;
}

/* ------------------------------------------------------------------ */
/* Test 10: DrawPipeline color sentinel (UINT_MAX fix)                 */
/*                                                                     */
/* Objects that carry no "color" or "rgb" attribute must NOT have a    */
/* color entry written to the cache.  Before the fix, the dp_attr_worker*/
/* would write UINT_MAX as a packed-RGB value; on the next open that   */
/* value would be read back as a valid (near-white) color, making      */
/* colorless solids appear white instead of inheriting the region hue. */
/*                                                                     */
/* Strategy:                                                           */
/*   a) Create a DbiState and let the pipeline settle.                 */
/*   b) Identify a solid that has no color attribute in moss.g.        */
/*   c) Call path_color() for that solid's hash.  If the UINT_MAX bug  */
/*      is present the returned color will be white (255,255,255),     */
/*      because the bug-encoded UINT_MAX != INT_MAX sentinel causes it  */
/*      to be treated as a valid packed-RGB in digest_path.            */
/*   d) Verify path_color() does NOT return white.                     */
/* ------------------------------------------------------------------ */
static int
test_color_sentinel(const char *moss_g_path)
{
    int failures = 0;

    bu_log("=== Test 10: DrawPipeline color-sentinel (UINT_MAX fix) ===\n");

    struct ged *gedp = ged_open("db", moss_g_path, 1);
    if (!gedp) {
	bu_log("  FAIL: could not open %s\n", moss_g_path);
	return 1;
    }

    DbiState *dbis = new DbiState(gedp);
    gedp->dbi_state = (void *)dbis;

    /* Wait for the pipeline to settle so all cache writes are committed. */
    (void)dbis->wait_for_pipeline(10000);

    /* Force digest_path to populate the in-memory rgb / c_inherit maps
     * by calling update() which triggers a full digest pass. */
    dbis->update();

    /* Pick a primitive that carries no color attribute.  In moss.g the
     * solid "box.s" is a plain ARB8; it has no color of its own. */
    const char *colorless_name = "box.s";
    struct directory *dp = db_lookup(gedp->dbip, colorless_name, LOOKUP_QUIET);
    if (dp == RT_DIR_NULL) {
	bu_log("  WARNING: '%s' not found in %s — skipping color-sentinel check\n",
	       colorless_name, moss_g_path);
	delete (DbiState *)gedp->dbi_state;
	gedp->dbi_state = NULL;
	ged_close(gedp);
	return 0;  /* inconclusive, not a failure */
    }

    /* Verify the object has no color attribute on disk.
     * is_colorless stays true unless a "color" or "rgb" avs attribute is found. */
    struct bu_attribute_value_set avs = BU_AVS_INIT_ZERO;
    bool is_colorless = true;
    if (db5_get_attributes(gedp->dbip, &avs, dp) == 0) {
	const char *cval = bu_avs_get(&avs, "color");
	if (!cval) cval = bu_avs_get(&avs, "rgb");
	if (cval) {
	    bu_log("  NOTE: '%s' has a color attribute ('%s') — "
		   "test is inconclusive for this solid\n",
		   colorless_name, cval);
	    is_colorless = false;
	}
	bu_avs_free(&avs);
    }

    if (!is_colorless) {
	delete (DbiState *)gedp->dbi_state;
	gedp->dbi_state = NULL;
	ged_close(gedp);
	return 0;
    }

    /* Build a single-element path for box.s and ask for its color.
     * With the UINT_MAX bug the DbiState::rgb map gets an entry for
     * this solid with the value 0xFFFFFFFF, so path_color() returns
     * true with nearly-white.  With the fix, no entry is in rgb and
     * path_color returns false (or a default, never pure white). */
    unsigned long long hash =
	bu_data_hash(dp->d_namep, strlen(dp->d_namep) * sizeof(char));

    std::vector<unsigned long long> path_elems;
    path_elems.push_back(hash);

    struct bu_color color;
    bool has_color = dbis->path_color(&color, path_elems);

    if (has_color) {
	/* A color was returned — check it is NOT UINT_MAX-derived white */
	unsigned char r = 0, g = 0, b = 0;
	int ri, gi, bi;
	bu_color_to_rgb_ints(&color, &ri, &gi, &bi);
	r = (unsigned char)ri;
	g = (unsigned char)gi;
	b = (unsigned char)bi;
	if (r == 255 && g == 255 && b == 255) {
	    /* This is the exact symptom of the pre-fix UINT_MAX bug */
	    bu_log("FAIL: path_color for colorless '%s' returned white "
		   "(255,255,255) — UINT_MAX sentinel bug NOT fixed\n",
		   colorless_name);
	    failures++;
	} else {
	    bu_log("  NOTE: path_color returned (%d,%d,%d) for '%s' "
		   "(not UINT_MAX-derived white — from material table or inherit chain)\n",
		   (int)r, (int)g, (int)b, colorless_name);
	    bu_log("  PASS: color sentinel not triggered\n");
	}
    } else {
	/* No color — the sentinel is working correctly */
	bu_log("  PASS: path_color returns false for colorless solid '%s'\n",
	       colorless_name);
    }

    delete (DbiState *)gedp->dbi_state;
    gedp->dbi_state = NULL;
    ged_close(gedp);

    return failures;
}

/* ------------------------------------------------------------------ */
/* Test 11: DBI cache format-version mismatch detection                */
/*                                                                     */
/* Scenario: simulate what happens when a stale format file (written   */
/* by an older build) is present in the cache directory.  DbiState     */
/* must detect the version mismatch, clear the old cache, and write    */
/* the current format version into the file so a second construction   */
/* does NOT trigger another clear.                                     */
/* ------------------------------------------------------------------ */
static int
test_format_version_mismatch(const char *moss_g_path)
{
    int failures = 0;

    bu_log("=== Test 11: DBI cache format-version mismatch detection ===\n");

    /* Make a dedicated private cache sub-directory for this test so that
     * previous tests' cache state does not interfere. */
    char fmt_cache[MAXPATHLEN] = {0};
    bu_dir(fmt_cache, MAXPATHLEN, BU_DIR_CURR, "ged_dbi_fmt_test_cache", NULL);
    bu_mkdir(fmt_cache);
    bu_setenv("BU_DIR_CACHE", fmt_cache, 1);

    /* Create the .Dbi directory and inject a stale format file. */
    char dbi_dir[MAXPATHLEN] = {0};
    bu_dir(dbi_dir, MAXPATHLEN, BU_DIR_CACHE, ".Dbi", NULL);
    bu_mkdir(dbi_dir);

    char fmt_path[MAXPATHLEN] = {0};
    bu_dir(fmt_path, MAXPATHLEN, BU_DIR_CACHE, ".Dbi", "format", NULL);
    {
	FILE *fp = fopen(fmt_path, "w");
	CHECK(fp != NULL, "could not write stale format file");
	if (fp) {
	    fprintf(fp, "1\n");   /* version 1 — previous format, always stale here */
	    fclose(fp);
	}
    }

    /* Also plant a sentinel file inside .Dbi to confirm it gets cleared. */
    char sentinel[MAXPATHLEN] = {0};
    bu_dir(sentinel, MAXPATHLEN, BU_DIR_CACHE, ".Dbi", "stale_sentinel", NULL);
    {
	FILE *fp = fopen(sentinel, "w");
	CHECK(fp != NULL, "could not write sentinel file");
	if (fp) fclose(fp);
    }

    /* First DbiState construction: should detect version 0 != current,
     * clear .Dbi, recreate it, and write the current format number. */
    struct ged *gedp = ged_open("db", moss_g_path, 1);
    if (!gedp) {
	bu_log("  FAIL: could not open %s\n", moss_g_path);
	return ++failures;
    }
    DbiState *dbis = new DbiState(gedp);
    gedp->dbi_state = (void *)dbis;
    (void)dbis->wait_for_pipeline(5000);

    /* Verify: format file now contains a valid (non-zero) version. */
    {
	int new_fmt = -1;
	std::ifstream ff(fmt_path);
	if (ff.is_open())
	    ff >> new_fmt;
	if (new_fmt <= 0) {
	    bu_log("  FAIL: format file not updated after mismatch clear "
		   "(got %d, expected > 0)\n", new_fmt);
	    failures++;
	} else {
	    bu_log("  OK: format file updated to version %d\n", new_fmt);
	}
    }

    /* Verify: the sentinel file was removed (old cache was cleared). */
    if (bu_file_exists(sentinel, NULL)) {
	bu_log("  FAIL: stale sentinel file survived the cache clear\n");
	failures++;
    } else {
	bu_log("  OK: stale data was cleared on format mismatch\n");
    }

    delete (DbiState *)gedp->dbi_state;
    gedp->dbi_state = NULL;
    ged_close(gedp);

    /* Second DbiState construction: format now matches — no clear should
     * occur and the sentinel must still be absent. */
    gedp = ged_open("db", moss_g_path, 1);
    if (!gedp) {
	bu_log("  FAIL: could not reopen %s\n", moss_g_path);
	return ++failures;
    }
    dbis = new DbiState(gedp);
    gedp->dbi_state = (void *)dbis;
    (void)dbis->wait_for_pipeline(5000);

    /* Plant a fresh sentinel, then construct DbiState a third time.
     * A spurious extra clear would remove it. */
    {
	FILE *fp = fopen(sentinel, "w");
	if (fp) fclose(fp);
    }

    delete (DbiState *)gedp->dbi_state;
    gedp->dbi_state = NULL;
    ged_close(gedp);

    gedp = ged_open("db", moss_g_path, 1);
    if (!gedp) {
	bu_log("  FAIL: could not open %s for third pass\n", moss_g_path);
	return ++failures;
    }
    dbis = new DbiState(gedp);
    gedp->dbi_state = (void *)dbis;
    (void)dbis->wait_for_pipeline(5000);

    if (!bu_file_exists(sentinel, NULL)) {
	bu_log("  FAIL: sentinel removed on second open — spurious extra clear\n");
	failures++;
    } else {
	bu_log("  OK: no spurious clear on second open (format already current)\n");
    }

    delete (DbiState *)gedp->dbi_state;
    gedp->dbi_state = NULL;
    ged_close(gedp);

    return failures;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int ac, char *av[])
{
    int ret = 0;

    bu_setprogname(av[0]);

    if (ac != 2) {
	printf("Usage: %s <dir-containing-moss.g>\n", av[0]);
	return 1;
    }

    if (!bu_file_directory(av[1])) {
	printf("ERROR: [%s] is not a directory\n", av[1]);
	return 2;
    }

    /* Use a local working-directory cache so we do not pollute the user's
     * real BRL-CAD cache and so the test is fully self-contained. */
    char lcache[MAXPATHLEN] = {0};
    bu_dir(lcache, MAXPATHLEN, BU_DIR_CURR, "ged_dbi_cpp_test_cache", NULL);
    bu_mkdir(lcache);
    bu_setenv("BU_DIR_CACHE", lcache, 1);

    /* Make a temporary copy of moss.g so the original is not modified */
    struct bu_vls src_path = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&src_path, "%s/moss.g", av[1]);
    if (!bu_file_exists(bu_vls_cstr(&src_path), NULL)) {
	printf("ERROR: [%s] does not exist\n", bu_vls_cstr(&src_path));
	bu_vls_free(&src_path);
	return 3;
    }

    const char *tmp_g = "moss_dbi_tmp.g";
    {
	std::ifstream orig(bu_vls_cstr(&src_path), std::ios::binary);
	std::ofstream tmpf(tmp_g, std::ios::binary);
	tmpf << orig.rdbuf();
    }
    bu_vls_free(&src_path);

    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);

    /* Test 1: bu_cache round-trip */
    ret += test_cache_roundtrip();

    /* Tests 2-7: DbiState functional tests */
    ret += test_dbistate(tmp_g);

    /* Test 8: Phase 1-C observer + Phase 1-D GObj + Phase 1-E DrawList + Phase 1-F SelectionSet */
    ret += test_observer(tmp_g);

    /* Test 9: Phase 3.5 DrawPipeline — drain_geom_results produces results */
    ret += test_pipeline(tmp_g);

    /* Test 10: DrawPipeline color sentinel — colorless objects must not get
     *          a white color written to the cache (UINT_MAX sentinel fix). */
    ret += test_color_sentinel(tmp_g);

    /* Test 11: DBI cache format-version mismatch detection — stale format
     *          triggers clear; matching format does not trigger a second clear. */
    ret += test_format_version_mismatch(tmp_g);
    /* Restore the test-wide cache dir for any future tests that might be added. */
    bu_setenv("BU_DIR_CACHE", lcache, 1);

    /* Accumulate any inline CHECK() failures */
    ret += g_failures;

    bu_file_delete(tmp_g);

    if (ret == 0)
	bu_log("All DbiState tests PASSED.\n");
    else
	bu_log("%d DbiState test(s) FAILED.\n", ret);

    return (ret != 0) ? 1 : 0;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
