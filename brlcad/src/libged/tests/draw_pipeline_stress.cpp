/*      D R A W _ P I P E L I N E _ S T R E S S . C P P
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
/** @file draw_pipeline_stress.cpp
 *
 * DrawPipeline concurrent stress test (Phase 3.5).
 *
 * The DrawPipeline uses a concurrentqueue-backed 5-stage worker design (Phase
 * 3.5).  The existing Test 9 in test_dbi_cpp verifies that the pipeline runs
 * once and settles.  This test pushes the pipeline harder:
 *
 *   Round 1 — Full batch: queue all solid objects at once, wait to settle,
 *     verify all bboxes are populated.
 *
 *   Round 2 — Rapid re-push: while the pipeline may still be draining from
 *     one push, push the same batch again.  This tests that repeated pushes
 *     of duplicate hashes don't corrupt or deadlock the pipeline.
 *
 *   Round 3 — Staggered push: split the object list into two halves, push
 *     the first half, immediately push the second half without waiting,
 *     then wait_for_pipeline.  All objects must still be in bboxes.
 *
 *   Round 4 — Reset + replay: delete and recreate DbiState; push all objects
 *     again; verify the pipeline rebuilds cleanly from scratch.
 *
 * Because all pushes come from the main thread (as required by the pipeline
 * design), this is not a multi-threaded GED test but a stress test of the
 * lock-free queue under load from a rapidly-firing caller.
 *
 * Usage: ged_test_draw_pipeline_stress <path-to-moss.g>
 */

#include "common.h"

#include <vector>
#include <unordered_set>

#include <bu.h>
#include "bu/opt.h"
#include <ged.h>

#include "../dbi.h"

#define CHECK(cond, msg) \
    do { \
	if (!(cond)) { \
	    bu_log("FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); \
	    failures++; \
	} \
    } while (0)

/* ------------------------------------------------------------------ */
/* Collect all solid (non-comb) directory entries                      */
/* ------------------------------------------------------------------ */
static std::vector<DrawPipeline::WorkItem>
collect_solid_items(struct ged *gedp, DbiState *dbis)
{
    std::vector<DrawPipeline::WorkItem> items;
    struct db_i *dbip = gedp->dbip;
    struct directory *dp;
    FOR_ALL_DIRECTORY_START(dp, dbip) {
	if (!(dp->d_flags & RT_DIR_SOLID))
	    continue;
	unsigned long long h = bu_data_hash(dp->d_namep,
					    strlen(dp->d_namep) * sizeof(char));
	if (!h)
	    continue;
	DrawPipeline::WorkItem wi;
	wi.hash = h;
	wi.dp   = dp;
	items.push_back(wi);
    } FOR_ALL_DIRECTORY_END
    (void)dbis;
    return items;
}

/* ================================================================== */
int
main(int ac, char *av[])
{
    bu_setprogname(av[0]);

    int soft_fail = 0;
    struct bu_opt_desc d[2];
    BU_OPT(d[0], "c", "continue", "", NULL, &soft_fail, "Continue on failure.");
    BU_OPT_NULL(d[1]);

    int uac = bu_opt_parse(NULL, ac, (const char **)av, d);
    if (uac != 2)
	bu_exit(EXIT_FAILURE,
		"Usage: %s [-c] <path-to-moss.g>\n", av[0]);
    const char *moss_path = av[1];
    if (!bu_file_exists(moss_path, NULL))
	bu_exit(EXIT_FAILURE, "ERROR: [%s] does not exist\n", moss_path);

    /* Private cache for this test */
    char lcache[MAXPATHLEN];
    bu_dir(lcache, MAXPATHLEN, BU_DIR_CURR, "dp_stress_cache", NULL);
    bu_mkdir(lcache);
    bu_setenv("BU_DIR_CACHE", lcache, 1);
    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);

    int failures = 0;

    /* ---- Round 1: full batch push, wait, verify bboxes ------------ */
    bu_log("=== DrawPipeline stress Round 1: full batch ===\n");
    {
	struct ged *gedp = ged_open("db", moss_path, 1);
	CHECK(gedp != NULL, "ged_open must succeed");
	if (!gedp)
	    goto done;

	DbiState *dbis = new DbiState(gedp);
	gedp->dbi_state = (void *)dbis;

	std::vector<DrawPipeline::WorkItem> all_items = collect_solid_items(gedp, dbis);
	bu_log("  %zu solid objects found\n", all_items.size());
	CHECK(!all_items.empty(), "moss.g must have at least one solid");

	/* Push ALL items (pipeline may already have processed some during
	 * DbiState construction; this ensures they are re-queued). */
	dbis->start_geom_load(all_items);

	size_t total = dbis->wait_for_pipeline(10000);
	bu_log("  Round 1: %zu results after wait\n", total);

	/* Every solid must have an AABB entry after settling */
	size_t missing = 0;
	for (auto &wi : all_items) {
	    if (dbis->bboxes.find(wi.hash) == dbis->bboxes.end())
		missing++;
	}
	bu_log("  %zu solids missing from bboxes map\n", missing);
	CHECK(missing == 0, "all solids must have AABB after Round 1");

	delete dbis;
	gedp->dbi_state = NULL;
	ged_close(gedp);
    }

    /* ---- Round 2: rapid re-push (duplicate hashes) ---------------- */
    bu_log("=== DrawPipeline stress Round 2: rapid re-push ===\n");
    {
	struct ged *gedp = ged_open("db", moss_path, 1);
	CHECK(gedp != NULL, "ged_open Round 2 must succeed");
	if (!gedp)
	    goto done;

	DbiState *dbis = new DbiState(gedp);
	gedp->dbi_state = (void *)dbis;

	std::vector<DrawPipeline::WorkItem> all_items = collect_solid_items(gedp, dbis);

	/* Push the same batch twice without waiting between pushes */
	dbis->start_geom_load(all_items);
	dbis->start_geom_load(all_items);   /* second push while first is live */

	size_t total = dbis->wait_for_pipeline(10000);
	bu_log("  Round 2: %zu results after double push\n", total);
	/* After wait_for_pipeline returns the pipeline is settled.
	 * A second call with a short timeout should produce nothing. */
	CHECK(dbis->wait_for_pipeline(500) == 0, "pipeline must be idle after settling");

	/* Bboxes must still be complete */
	size_t missing = 0;
	for (auto &wi : all_items) {
	    if (dbis->bboxes.find(wi.hash) == dbis->bboxes.end())
		missing++;
	}
	CHECK(missing == 0, "all solids must have AABB after Round 2 double push");

	delete dbis;
	gedp->dbi_state = NULL;
	ged_close(gedp);
    }

    /* ---- Round 3: staggered push (split batch) -------------------- */
    bu_log("=== DrawPipeline stress Round 3: staggered push ===\n");
    {
	struct ged *gedp = ged_open("db", moss_path, 1);
	CHECK(gedp != NULL, "ged_open Round 3 must succeed");
	if (!gedp)
	    goto done;

	DbiState *dbis = new DbiState(gedp);
	gedp->dbi_state = (void *)dbis;

	std::vector<DrawPipeline::WorkItem> all_items = collect_solid_items(gedp, dbis);

	/* Split into first half and second half */
	size_t mid = all_items.size() / 2;
	std::vector<DrawPipeline::WorkItem> first_half(all_items.begin(),
						       all_items.begin() + (std::ptrdiff_t)mid);
	std::vector<DrawPipeline::WorkItem> second_half(all_items.begin() + (std::ptrdiff_t)mid,
							all_items.end());

	dbis->start_geom_load(first_half);
	/* Immediately push second half without draining first half */
	dbis->start_geom_load(second_half);

	size_t total = dbis->wait_for_pipeline(10000);
	bu_log("  Round 3: %zu results after staggered push\n", total);
	CHECK(dbis->wait_for_pipeline(500) == 0, "pipeline must be idle after staggered push");

	size_t missing = 0;
	for (auto &wi : all_items) {
	    if (dbis->bboxes.find(wi.hash) == dbis->bboxes.end())
		missing++;
	}
	CHECK(missing == 0, "all solids must have AABB after Round 3 staggered push");

	delete dbis;
	gedp->dbi_state = NULL;
	ged_close(gedp);
    }

    /* ---- Round 4: destroy + recreate DbiState, full push ---------- */
    bu_log("=== DrawPipeline stress Round 4: reset + replay ===\n");
    {
	struct ged *gedp = ged_open("db", moss_path, 1);
	CHECK(gedp != NULL, "ged_open Round 4 must succeed");
	if (!gedp)
	    goto done;

	/* First DbiState — let it settle */
	DbiState *dbis1 = new DbiState(gedp);
	gedp->dbi_state = (void *)dbis1;
	dbis1->wait_for_pipeline(10000);
	size_t bboxes_first = dbis1->bboxes.size();
	bu_log("  Round 4 first DbiState: %zu bboxes\n", bboxes_first);
	delete dbis1;
	gedp->dbi_state = NULL;

	/* Second DbiState on the same open db — must rebuild cleanly */
	DbiState *dbis2 = new DbiState(gedp);
	gedp->dbi_state = (void *)dbis2;

	std::vector<DrawPipeline::WorkItem> all_items = collect_solid_items(gedp, dbis2);
	dbis2->start_geom_load(all_items);
	dbis2->wait_for_pipeline(10000);
	size_t bboxes_second = dbis2->bboxes.size();
	bu_log("  Round 4 second DbiState: %zu bboxes\n", bboxes_second);

	/* Both states must have the same number of bboxes */
	CHECK(bboxes_second == bboxes_first,
	      "reset + replay must produce same AABB count as initial run");

	delete dbis2;
	gedp->dbi_state = NULL;
	ged_close(gedp);
    }

done:
    bu_dirclear(lcache);

    if (failures)
	bu_log("RESULT: %d failure(s)\n", failures);
    else
	bu_log("RESULT: all DrawPipeline stress tests PASSED\n");

    return failures;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
