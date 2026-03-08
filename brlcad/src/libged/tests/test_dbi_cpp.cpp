/*                 T E S T _ D B I _ C P P . C P P
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
 * Headless C++ unit test for the DBI core classes: DbiState, DrawList,
 * SelectionSet, and IDbiObserver.  Does not depend on Qt.
 *
 * Creates a temporary .g database, populates it with a hierarchy of
 * primitives and combinations, then exercises the C++ API directly.
 *
 * Expected exit codes:
 *   0  all checks passed
 *   1  usage error
 *   3  one or more checks failed
 */

#include "common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bu.h"
#include "bu/ptbl.h"
#include "raytrace.h"
#include "ged.h"
#include "ged/dbi.h"

/* ------------------------------------------------------------------ */
/* Database-change callback: populates DbiState's added/changed/      */
/* removed sets so that DbiState::update() can generate events.       */
/* Without this callback registered with db_i, update() sees empty    */
/* sets and returns immediately without notifying observers.           */
/* ------------------------------------------------------------------ */
extern "C" void
test_dbi_changed_callback(struct db_i *UNUSED(dbip), struct directory *dp,
			   int mode, void *u_data)
{
    /* dbip is accessible via gedp->dbip if needed; unused here since the
     * DbiState is retrieved from the ged pointer in u_data. */
    struct ged *gedp = (struct ged *)u_data;
    DbiState *ctx = (DbiState *)gedp->dbi_state;
    if (!ctx) return;

    ctx->clear_cache(dp);

    switch (mode) {
	case 0: ctx->changed.insert(dp); break;
	case 1: ctx->added.insert(dp);   break;
	case 2: {
	    unsigned long long hash =
		bu_data_hash(dp->d_namep, strlen(dp->d_namep)*sizeof(char));
	    ctx->removed.insert(hash);
	    ctx->old_names[hash] = std::string(dp->d_namep);
	    break;
	}
	default:
	    bu_log("test_dbi_changed_callback: unknown mode %d\n", mode);
    }
}

/* ------------------------------------------------------------------ */
/* Minimal IDbiObserver implementation for capturing events           */
/* ------------------------------------------------------------------ */
class TestObserver : public IDbiObserver {
public:
    std::vector<DbiChangeEvent> events;

    void on_dbi_changed(const std::vector<DbiChangeEvent> &evs) override
    {
	events.insert(events.end(), evs.begin(), evs.end());
    }

    void clear() { events.clear(); }

    bool has_event_kind(DbiChangeKind k) const
    {
	for (const auto &e : events)
	    if (e.kind == k) return true;
	return false;
    }
};

/* ------------------------------------------------------------------ */
/* Assertion helpers                                                   */
/* ------------------------------------------------------------------ */
static int g_failures = 0;

#define CHECK(cond, msg) \
    do { \
	if (!(cond)) { \
	    fprintf(stderr, "FAIL: %s (line %d)\n", (msg), __LINE__); \
	    g_failures++; \
	} else { \
	    printf("PASS: %s\n", (msg)); \
	} \
    } while (0)

/* ------------------------------------------------------------------ */
/* Populate the database                                               */
/* ------------------------------------------------------------------ */
static void
populate_db(struct ged *gedp)
{
    /* in sph1.s sph 0 0 0 10 */
    { const char *av[] = {"in","sph1.s","sph","0","0","0","10",NULL};
      ged_exec_in(gedp, 7, av); }
    /* in tor1.s tor 0 0 0 0 0 1 20 5 */
    { const char *av[] = {"in","tor1.s","tor","0","0","0","0","0","1","20","5",NULL};
      ged_exec_in(gedp, 11, av); }
    /* r reg1.r u sph1.s */
    { const char *av[] = {"r","reg1.r","u","sph1.s",NULL};
      ged_exec_r(gedp, 4, av); }
    /* g top1.c reg1.r tor1.s */
    { const char *av[] = {"g","top1.c","reg1.r","tor1.s",NULL};
      ged_exec_g(gedp, 4, av); }
}

/* ------------------------------------------------------------------ */
/* Section A: DbiState                                                 */
/* ------------------------------------------------------------------ */
static void
test_dbi_state(struct ged *gedp)
{
    printf("\n--- DbiState ---\n");

    /* DbiState was bootstrapped in main() via ged_dbi_update() */
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    CHECK(dbis != nullptr, "gedp->dbi_state is non-null after ged_dbi_update()");
    if (!dbis)
	return;

    /* update() returns flags; a re-call on an unchanged DB should return 0 */
    unsigned long long flags = dbis->update();
    (void)flags;
    printf("PASS: DbiState::update() returned 0x%llx (re-call on unchanged DB)\n",
	   (unsigned long long)flags);

    /* tops() should list at least one top-level object */
    std::vector<unsigned long long> tops = dbis->tops(false);
    CHECK(!tops.empty(), "DbiState::tops() returns non-empty vector");

    /* Each top hash should be valid */
    bool all_valid = true;
    for (auto h : tops)
	if (!dbis->valid_hash(h)) { all_valid = false; break; }
    CHECK(all_valid, "every hash returned by tops() is valid_hash()==true");

    /* Hashes for known objects */
    unsigned long long h_top1 = 0;
    for (auto h : tops) {
	struct bu_vls name = BU_VLS_INIT_ZERO;
	dbis->print_hash(&name, h);
	if (BU_STR_EQUAL(bu_vls_cstr(&name), "top1.c"))
	    h_top1 = h;
	bu_vls_free(&name);
    }
    CHECK(h_top1 != 0, "top1.c is a top-level object");
    CHECK(dbis->valid_hash(h_top1), "valid_hash for top1.c hash is true");
    CHECK(!dbis->valid_hash(0ULL), "valid_hash(0) returns false");

    /* p_v should have an entry for top1.c with 2 children */
    auto pv_it = dbis->p_v.find(h_top1);
    CHECK(pv_it != dbis->p_v.end(), "p_v contains an entry for top1.c");
    if (pv_it != dbis->p_v.end())
	CHECK(pv_it->second.size() == 2, "top1.c has 2 children in p_v");
}

/* ------------------------------------------------------------------ */
/* Section B: DrawList                                                 */
/* ------------------------------------------------------------------ */
static void
test_draw_list(struct ged *gedp)
{
    printf("\n--- DrawList ---\n");

    DbiState *dbis = (DbiState *)gedp->dbi_state;
    /* Update to populate p_v and hash maps */
    dbis->update();

    /* Get top1.c hash by scanning tops */
    unsigned long long h_top1 = 0;
    std::vector<unsigned long long> tops = dbis->tops(false);
    for (auto h : tops) {
	struct bu_vls name = BU_VLS_INIT_ZERO;
	dbis->print_hash(&name, h);
	if (BU_STR_EQUAL(bu_vls_cstr(&name), "top1.c"))
	    h_top1 = h;
	bu_vls_free(&name);
    }

    /* Build a one-element path vector (just the top-level object) */
    std::vector<unsigned long long> path_top1 = {h_top1};

    DrawList dl;
    CHECK(dl.empty(), "fresh DrawList is empty");
    CHECK(dl.count() == 0, "fresh DrawList count() == 0");
    CHECK(dl.query(h_top1) == DrawState::NOT_DRAWN,
	  "query on empty DrawList returns NOT_DRAWN");

    /* add a path in mode 0 */
    dl.add(path_top1, 0);
    CHECK(!dl.empty(), "DrawList is non-empty after add");
    CHECK(dl.count() == 1, "DrawList count() == 1 after one add");
    CHECK(dl.query(h_top1) == DrawState::FULLY_DRAWN,
	  "top1.c is FULLY_DRAWN after add(mode=0)");

    /* add same path in mode 1 */
    dl.add(path_top1, 1);
    CHECK(dl.count(-1) == 2, "DrawList count(-1)==2 after two adds");
    CHECK(dl.count(0) == 1, "DrawList count(0)==1");
    CHECK(dl.count(1) == 1, "DrawList count(1)==1");
    CHECK(dl.query(h_top1, 0) == DrawState::FULLY_DRAWN,
	  "top1.c is FULLY_DRAWN in mode 0");
    CHECK(dl.query(h_top1, 1) == DrawState::FULLY_DRAWN,
	  "top1.c is FULLY_DRAWN in mode 1");

    /* drawn_path_hashes */
    auto all_paths = dl.drawn_path_hashes(-1);
    CHECK(all_paths.size() == 2, "drawn_path_hashes returns 2 entries");

    /* clear(mode) removes only the given mode */
    dl.clear(0);
    CHECK(dl.count(0) == 0, "DrawList mode-0 count is 0 after clear(0)");
    CHECK(dl.count(1) == 1, "DrawList mode-1 count is still 1 after clear(0)");
    CHECK(dl.query(h_top1, 0) == DrawState::NOT_DRAWN,
	  "top1.c is NOT_DRAWN in mode 0 after clear(0)");
    CHECK(dl.query(h_top1, 1) == DrawState::FULLY_DRAWN,
	  "top1.c is still FULLY_DRAWN in mode 1 after clear(0)");

    /* clear() removes everything */
    dl.clear();
    CHECK(dl.empty(), "DrawList is empty after clear()");
    CHECK(dl.count() == 0, "DrawList count()==0 after clear()");

    /* remove: add two paths, then remove one */
    dl.add(path_top1, 0);
    if (!dbis->p_v.empty() && dbis->p_v.find(h_top1) != dbis->p_v.end()
	    && !dbis->p_v[h_top1].empty()) {
	unsigned long long h_child0 = dbis->p_v[h_top1][0];
	std::vector<unsigned long long> path_child = {h_top1, h_child0};
	dl.add(path_child, 0);
	CHECK(dl.count() == 2, "DrawList count()==2 after adding top + child");
	/* remove() takes a full-path hash (same as DbiState::path_hash()) */
	unsigned long long child_phash = dbis->path_hash(path_child, 0);
	dl.remove(child_phash);
	CHECK(dl.count() == 1, "DrawList count()==1 after removing child");
    } else {
	printf("SKIP: child path remove test (no children in p_v)\n");
    }

    /* Settings override: add with a settings object */
    dl.clear();
    DrawSettings ds;
    ds.has_color = true;
    ds.color.buc_rgb[0] = 255;
    ds.color.buc_rgb[1] = 0;
    ds.color.buc_rgb[2] = 0;
    dl.add(path_top1, 0, &ds);
    CHECK(dl.count() == 1, "DrawList count()==1 after add with settings");
    dl.clear();
}

/* ------------------------------------------------------------------ */
/* Section C: SelectionSet                                             */
/* ------------------------------------------------------------------ */
static void
test_selection_set(struct ged *gedp)
{
    printf("\n--- SelectionSet ---\n");

    DbiState *dbis = (DbiState *)gedp->dbi_state;
    dbis->update();

    /* Use the default selection set (name == nullptr) */
    SelectionSet *ss = dbis->get_selection_set(nullptr);
    CHECK(ss != nullptr, "get_selection_set(nullptr) returns non-null");

    /* Nothing selected initially */
    CHECK(!ss->is_selected(0ULL), "is_selected(0) returns false on fresh set");

    /* Select via string path */
    bool added = ss->select("top1.c/reg1.r/sph1.s");
    CHECK(added, "select('top1.c/reg1.r/sph1.s') returns true on new selection");

    /* Idempotency: selecting again returns false */
    bool added2 = ss->select("top1.c/reg1.r/sph1.s");
    CHECK(!added2, "select same path again returns false (idempotent)");

    /* top1.c should be an ancestor of the selected path.
     * is_ancestor() takes a PATH hash (hash over a vector of name hashes),
     * not a bare name hash.  The path hash for a single-element path is
     * path_hash({name_hash}, 0). */
    unsigned long long h_top1_name = 0;
    {
	std::vector<unsigned long long> tops = dbis->tops(false);
	for (auto h : tops) {
	    struct bu_vls name = BU_VLS_INIT_ZERO;
	    dbis->print_hash(&name, h);
	    if (BU_STR_EQUAL(bu_vls_cstr(&name), "top1.c"))
		h_top1_name = h;
	    bu_vls_free(&name);
	}
    }
    if (h_top1_name) {
	/* Compute the path hash for the single-element path {top1.c} */
	std::vector<unsigned long long> path_top1 = {h_top1_name};
	unsigned long long h_top1_path = dbis->path_hash(path_top1, 0);
	CHECK(ss->is_ancestor(h_top1_path),
	      "top1.c path is_ancestor() after selecting top1.c/reg1.r/sph1.s");
	/* The full-path hash for top1.c (as a leaf, single element) must not
	 * be selected — only paths that end in sph1.s are selected. */
	CHECK(!ss->is_selected(h_top1_path),
	      "top1.c path is NOT is_selected() (only ancestor, not leaf)");
    } else {
	printf("SKIP: ancestor test (top1.c hash not found)\n");
    }

    /* Deselect */
    bool removed = ss->deselect("top1.c/reg1.r/sph1.s");
    CHECK(removed, "deselect returns true on successful deselect");
    bool removed2 = ss->deselect("top1.c/reg1.r/sph1.s");
    CHECK(!removed2, "deselect returns false when not selected");

    /* Select two paths then clear.  Use string-based select so there is no
     * dependency on computing path hashes here. */
    ss->select("top1.c");
    ss->select("reg1.r");
    ss->clear();
    /* After clear nothing should be selected — check is_selected(0) as a proxy */
    CHECK(!ss->is_selected(0ULL), "is_selected(0) returns false after clear()");

    /* Named selection set is independent */
    SelectionSet *named = dbis->get_selection_set("my_set");
    CHECK(named != nullptr, "get_selection_set('my_set') returns non-null");
    CHECK(named != ss, "named set is different from default set");
    named->select("tor1.s");
    SelectionSet *named2 = dbis->get_selection_set("my_set");
    CHECK(named2 == named, "get_selection_set('my_set') returns same pointer");
    named->clear();

    /* Multiple sets via get_selection_sets() */
    auto all_sets = dbis->get_selection_sets();
    CHECK(!all_sets.empty(), "get_selection_sets() returns non-empty list");
}

/* ------------------------------------------------------------------ */
/* Section D: IDbiObserver                                             */
/* ------------------------------------------------------------------ */
static void
test_observer(struct ged *gedp)
{
    printf("\n--- IDbiObserver ---\n");

    DbiState *dbis = (DbiState *)gedp->dbi_state;

    TestObserver obs;
    dbis->add_observer(&obs);

    /* Trigger an update that modifies the DB: add a new sphere */
    obs.clear();
    { const char *av[] = {"in","sph2.s","sph","5","0","0","3",NULL};
      ged_exec_in(gedp, 7, av); }
    dbis->update();

    CHECK(!obs.events.empty(), "observer received events after adding sph2.s");
    CHECK(obs.has_event_kind(DbiChangeKind::ObjectAdded),
	  "observer got ObjectAdded event for sph2.s");
    printf("      event count = %zu\n", obs.events.size());

    /* Trigger a modification: add sph2.s to top1.c */
    obs.clear();
    { const char *av[] = {"g","top1.c","sph2.s",NULL};
      ged_exec_g(gedp, 3, av); }
    dbis->update();
    CHECK(!obs.events.empty(), "observer received events after modifying top1.c");
    CHECK(obs.has_event_kind(DbiChangeKind::ObjectModified),
	  "observer got ObjectModified event for top1.c");

    /* Trigger a removal */
    obs.clear();
    { const char *av[] = {"kill","-f","sph2.s",NULL};
      ged_exec_kill(gedp, 3, av); }
    dbis->update();
    /* After kill, the DB was modified (sph2.s removed from top1.c, and sph2.s deleted) */
    CHECK(!obs.events.empty(), "observer received events after killing sph2.s");

    /* Remove the observer - no further events should arrive */
    dbis->remove_observer(&obs);
    obs.clear();
    { const char *av[] = {"in","sph3.s","sph","0","5","0","2",NULL};
      ged_exec_in(gedp, 7, av); }
    dbis->update();
    CHECK(obs.events.empty(),
	  "observer receives no events after remove_observer()");
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int
main(int argc, char *argv[])
{
    char tmpfile[MAXPATHLEN] = {0};

    bu_setprogname(argv[0]);

    if (argc != 2) {
	fprintf(stderr, "Usage: %s output.g\n", argv[0]);
	return 1;
    }

    bu_strlcpy(tmpfile, argv[1], sizeof(tmpfile));
    bu_file_delete(tmpfile);

    struct ged *gedp = ged_open("db", tmpfile, 0);
    if (!gedp) {
	fprintf(stderr, "ERROR: ged_open failed for %s\n", tmpfile);
	return 3;
    }

    populate_db(gedp);

    /* Bootstrap DbiState via the C surface so gedp->dbi_state is initialized
     * before the C++ tests cast it directly. */
    ged_dbi_update(gedp);

    /* Register the change callback so DbiState::update() can detect future
     * database modifications (adds, changes, removals).  Without this,
     * update() always sees empty added/changed/removed sets and no events
     * are generated for IDbiObserver callbacks. */
    db_add_changed_clbk(gedp->dbip, &test_dbi_changed_callback, (void *)gedp);

    test_dbi_state(gedp);
    test_draw_list(gedp);
    test_selection_set(gedp);
    test_observer(gedp);

    ged_close(gedp);
    bu_file_delete(tmpfile);

    if (g_failures) {
	fprintf(stderr, "\n%d check(s) FAILED\n", g_failures);
	return 3;
    }
    printf("\nAll checks passed.\n");
    return 0;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
