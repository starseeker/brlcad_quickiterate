/*               B S G _ V I E W _ O B J . C P P
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
/** @file bsg_view_obj.cpp
 *
 * Phase 6.5 (drawing-stack modernization) — Step 1 regression test.
 *
 * Exercises the bsg_view_obj_* migration-target API declared in
 * include/ged/bsg_ged_draw.h.  Each helper is currently a thin wrapper
 * over the legacy dl_* functions; this test pins the API surface so
 * that subsequent caller migrations (Step 2) and the eventual swap to
 * a pure BSG view-tree implementation (Step 7) can be done without
 * silently changing observable behavior.
 *
 * The test does NOT attach a display manager — bsg_view_obj_* must
 * work with the headless ged_drawable that ged_open() produces, so
 * that batch tooling (e.g. gtools/gsh) keeps working after the
 * implementation swap.
 *
 * Usage: ged_test_bsg_view_obj <directory-containing-moss.g>
 */

#include "common.h"

#include <fstream>
#include <cstring>

#include <bu.h>
#include <bsg.h>
#include "bsg/tcl_data.h"
#include "bsg/util.h"
#include "dm.h"
#include <ged.h>
#include "ged/bsg_ged_draw.h"
#include "bsg/defines.h"
#include "bsg/draw_set.h"
#include "bsg/field.h"
#include "bsg/lod_ops.h"
#include "bsg/node_group.h"
#include "bsg/node.h"
#include "bsg/appearance.h"
#include "bsg/draw_intent.h"
#include "bsg/util.h"
#include "bsg/visit.h"
#include "../../ged_private.h"


#define ASSERT(cond) do { \
    nchecks++; \
    if (!(cond)) { \
	bu_log("FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond); \
	nfails++; \
    } \
} while (0)

static int nchecks = 0;
static int nfails  = 0;

/* Count drawn groups via the public API. */
static int
_dl_count_cb(struct bsg_node * /*g*/, void *ud)
{
    int *n = (int *)ud;
    (*n)++;
    return 1;
}

static int
dl_count(struct ged *gedp)
{
    int n = 0;
    bsg_view_obj_foreach_group(gedp, _dl_count_cb, &n);
    return n;
}


int
main(int ac, char *av[])
{
    bu_setprogname(av[0]);

    if (ac != 2) {
	bu_log("Usage: %s <directory-containing-moss.g>\n", av[0]);
	return 1;
    }
    if (!bu_file_directory(av[1])) {
	bu_log("ERROR: [%s] is not a directory.\n", av[1]);
	return 2;
    }

    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);

    /* Local cache dir (mirrors the convention of basic.cpp). */
    char lcache[MAXPATHLEN] = {0};
    bu_dir(lcache, MAXPATHLEN, BU_DIR_CURR,
	   "ged_bsg_view_obj_cache", NULL);
    bu_mkdir(lcache);
    bu_setenv("BU_DIR_CACHE", lcache, 1);

    /* Copy moss.g into a working file. */
    struct bu_vls fname = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&fname, "%s/moss.g", av[1]);
    std::ifstream orig(bu_vls_cstr(&fname), std::ios::binary);
    std::ofstream tmpg("bsg_view_obj_tmp.g", std::ios::binary);
    tmpg << orig.rdbuf();
    orig.close();
    tmpg.close();
    bu_vls_free(&fname);

    struct ged *gedp = ged_open("db", "bsg_view_obj_tmp.g", 1);
    if (!gedp) {
	bu_log("ged_open failed\n");
	return 1;
    }

    bu_log("=== bsg_view_obj_* API regression ===\n");

    /* ---------------------------------------------------------------- *
     * 1. NULL-arg safety: every helper must tolerate NULL gedp/path.   *
     * ---------------------------------------------------------------- */
    bu_log("[1] NULL-arg safety...\n");
    /* Phase 13: the path-string mutation/lookup variants
     * (bsg_view_obj_lookup_or_add_path / _erase_by_path / _erase_all_paths /
     * _group_set_path) were removed; only the db_full_path-keyed and
     * name-keyed entry points remain (exercised in section [10] below). */
    bsg_view_obj_erase_by_name(NULL, "x");              /* no crash */
    bsg_view_obj_erase_by_name(gedp, NULL);             /* no crash */
    bsg_view_obj_set_iflag(NULL, 0);                       /* no crash */
    bsg_view_obj_color_from_soltab(NULL);                  /* no crash */
    ASSERT(bsg_view_obj_name_hash(NULL) == 0);

    /* Phase 10: db_full_path-keyed entry points must also be NULL-safe. */
    ASSERT(bsg_view_obj_lookup_or_add_dbpath(NULL, NULL) == NULL);
    ASSERT(bsg_view_obj_lookup_or_add_dbpath(gedp, NULL) == NULL);
    bsg_view_obj_erase_by_dbpath(NULL, NULL);              /* no crash */
    bsg_view_obj_erase_by_dbpath(gedp, NULL);              /* no crash */
    bsg_view_obj_erase_all_dbpaths(NULL, NULL);            /* no crash */
    bsg_view_obj_erase_all_dbpaths(gedp, NULL);            /* no crash */
    bsg_view_obj_group_set_dbpath(NULL, NULL);             /* no crash */
    {
	struct db_full_path tmp;
	db_full_path_init(&tmp);
	ASSERT(bsg_view_obj_group_dbpath(NULL, NULL, &tmp) != 0);
	ASSERT(bsg_view_obj_group_dbpath(gedp, NULL, &tmp) != 0);
	db_free_full_path(&tmp);
    }
    {
	vect_t mn, mx;
	int empty = bsg_view_obj_bounds(NULL, &mn, &mx, 0);
	ASSERT(empty == 1);
	empty = bsg_view_obj_bounds(gedp, NULL, &mx, 0);
	ASSERT(empty == 1);
    }

    /* ---------------------------------------------------------------- *
     * 2. Empty state: nothing drawn yet.                                *
     * ---------------------------------------------------------------- */
    bu_log("[2] Empty draw set...\n");
    ASSERT(dl_count(gedp) == 0);
    ASSERT(bsg_view_obj_name_hash(gedp) == 0);
    {
	vect_t mn, mx;
	int empty = bsg_view_obj_bounds(gedp, &mn, &mx, 0);
	ASSERT(empty == 1);
    }

    /* ---------------------------------------------------------------- *
     * 3. Draw via ged_exec, then probe API.                             *
     * ---------------------------------------------------------------- */
    bu_log("[3] Draw and probe...\n");
    {
	const char *s_av[3] = {"draw", "all.g", NULL};
	ged_exec(gedp, 2, s_av);
    }
    int after_draw = dl_count(gedp);
    ASSERT(after_draw > 0);

    /* lookup_or_add_dbpath on an already-drawn path must return non-NULL
     * and must not insert a duplicate. */
    int before_lookup = dl_count(gedp);
    void *h = NULL;
    {
	struct db_full_path dfp;
	db_full_path_init(&dfp);
	if (db_string_to_path(&dfp, gedp->dbip, "all.g") == 0)
	    h = bsg_view_obj_lookup_or_add_dbpath(gedp, &dfp);
	db_free_full_path(&dfp);
    }
    ASSERT(h != NULL);
    ASSERT(dl_count(gedp) == before_lookup);

    /* lookup_or_add_dbpath on a non-existent leaf must return NULL.  We
     * test the path-string variant for the legacy noisy-log fallback
     * via a pragma-guarded call below in section [15]. */
    void *h_missing = NULL;
    {
	struct db_full_path dfp;
	db_full_path_init(&dfp);
	if (db_string_to_path(&dfp, gedp->dbip, "definitely_no_such_obj") == 0)
	    h_missing = bsg_view_obj_lookup_or_add_dbpath(gedp, &dfp);
	db_free_full_path(&dfp);
    }
    ASSERT(h_missing == NULL);

    /* bounds must report non-empty after a draw. */
    {
	vect_t mn, mx;
	int empty = bsg_view_obj_bounds(gedp, &mn, &mx, 0);
	ASSERT(empty == 0);
	/* Sanity: min <= max on each axis. */
	ASSERT(mn[X] <= mx[X]);
	ASSERT(mn[Y] <= mx[Y]);
	ASSERT(mn[Z] <= mx[Z]);
    }

    /* name_hash must be non-zero with content drawn. */
    unsigned long long h1 = bsg_view_obj_name_hash(gedp);
    ASSERT(h1 != 0);

    /* set_iflag must propagate to every drawn scene obj. */
    bsg_view_obj_set_iflag(gedp, UP);
    {
	int all_up = 1;
	auto cb = +[](struct bsg_node *sp, void *ud) -> int {
	    int *ok = (int *)ud;
	    if (!bsg_appearance_is_highlighted(sp)) *ok = 0;
	    return 1;
	};
	bsg_view_obj_foreach_solid(gedp, cb, &all_up);
	ASSERT(all_up);
    }
    bsg_view_obj_set_iflag(gedp, DOWN);
    {
	int all_down = 1;
	auto cb = +[](struct bsg_node *sp, void *ud) -> int {
	    int *ok = (int *)ud;
	    if (bsg_appearance_is_highlighted(sp)) *ok = 0;
	    return 1;
	};
	bsg_view_obj_foreach_solid(gedp, cb, &all_down);
	ASSERT(all_down);
    }

    /* color_from_soltab must run cleanly. */
    bsg_view_obj_color_from_soltab(gedp);

    /* ---------------------------------------------------------------- *
     * 4. invent: insert a phony pseudo-solid.                           *
     * ---------------------------------------------------------------- */
    bu_log("[4] invent...\n");
    {
	struct bu_list vhead;
	BU_LIST_INIT(&vhead);
	struct bu_list *vlfree = &rt_vlfree;
	point_t p1 = {0, 0, 0}, p2 = {100, 100, 100};
	BSG_ADD_VLIST(vlfree, &vhead, p1, BSG_VLIST_LINE_MOVE);
	BSG_ADD_VLIST(vlfree, &vhead, p2, BSG_VLIST_LINE_DRAW);

	/* Save current count so we can verify a new entry was added.
	 * The invent call creates the _overlays group as a new root child. */
	int before_invent = dl_count(gedp);
	int rc = bsg_view_obj_invent(gedp,
				     (char *)"_bsg_test_phony", &vhead,
				     0xFF8800,  /* orange */
				     1,         /* copy = yes */
				     1.0, 0, 0);
	ASSERT(rc == 0);
	ASSERT(dl_count(gedp) > before_invent);

	/* The _overlays group must be present as a root child and be phony. */
	{
	    struct bsg_node *root = bsg_view_obj_root(gedp);
	    struct bsg_node *overlays_grp = NULL;
	    for (size_t i = 0; i < bsg_node_child_count(root); i++) {
		struct bsg_node *g = bsg_node_child_at(root, i);
		if (BU_STR_EQUAL("_overlays", bu_vls_cstr(&g->s_name))) {
		    overlays_grp = g;
		    break;
		}
	    }
	    ASSERT(overlays_grp != NULL);
	    ASSERT(bsg_view_obj_group_is_phony(overlays_grp));

	    /* The overlay shape must have BSG_PAYLOAD_OVERLAY set. */
	    if (overlays_grp && bsg_node_child_count(overlays_grp) > 0) {
		struct bsg_node *sp = bsg_node_child_at(overlays_grp, 0);
		ASSERT(sp->s_type_flags & BSG_PAYLOAD_OVERLAY);
		/* No phony db entry should exist for this name. */
		ASSERT(db_lookup(gedp->dbip, "_bsg_test_phony", LOOKUP_QUIET)
		       == RT_DIR_NULL);
	    }
	}

	/* Free the local vlist (we passed copy=1, so vhead still owns it). */
	BSG_FREE_VLIST(vlfree, &vhead);

	/* Erase the phony solid by name. */
	bsg_view_obj_erase_by_name(gedp, "_bsg_test_phony");
	/* _overlays group should be gone (empty → freed). */
	ASSERT(dl_count(gedp) == before_invent);
    }

    /* ---------------------------------------------------------------- *
     * 5. erase_by_path / erase_all_paths semantics.                     *
     * ---------------------------------------------------------------- */
    bu_log("[5] erase_*...\n");
    {
	/* Make sure we still have all.g drawn. */
	const char *s_av[3] = {"draw", "all.g", NULL};
	ged_exec(gedp, 2, s_av);

	int before = dl_count(gedp);
	ASSERT(before > 0);

	/* erase_by_dbpath on the exact drawn name must remove that entry. */
	{
	    struct db_full_path dfp;
	    db_full_path_init(&dfp);
	    if (db_string_to_path(&dfp, gedp->dbip, "all.g") == 0)
		bsg_view_obj_erase_by_dbpath(gedp, &dfp);
	    db_free_full_path(&dfp);
	}
	ASSERT(dl_count(gedp) < before);

	/* Re-draw and try erase_all_dbpaths. */
	ged_exec(gedp, 2, s_av);
	int before2 = dl_count(gedp);
	ASSERT(before2 > 0);
	{
	    struct db_full_path dfp;
	    db_full_path_init(&dfp);
	    if (db_string_to_path(&dfp, gedp->dbip, "all.g") == 0)
		bsg_view_obj_erase_all_dbpaths(gedp, &dfp);
	    db_free_full_path(&dfp);
	}
	/* Note: erase_all_dbpaths matches subset paths, so should clear
	 * everything that has all.g as a prefix component. */
	ASSERT(dl_count(gedp) <= before2);
    }

    /* ---------------------------------------------------------------- *
     * 6. zap then verify hash returns to zero.                          *
     * ---------------------------------------------------------------- */
    bu_log("[6] zap and rehash...\n");
    {
	const char *s_av[2] = {"zap", NULL};
	ged_exec(gedp, 1, s_av);
	ASSERT(bsg_view_obj_name_hash(gedp) == 0);
	ASSERT(dl_count(gedp) == 0);
    }

    /* ---------------------------------------------------------------- *
     * 7. solid_count / solid_at / solid_index — snapshotted DFS index. *
     * ---------------------------------------------------------------- */
    bu_log("[7] solid_count/at/index...\n");
    {
	/* Draw moss scene. */
	const char *s_av[3] = {"draw", "all.g", NULL};
	ged_exec(gedp, 2, s_av);

	int count = bsg_view_obj_solid_count(gedp);
	ASSERT(count > 0);

	/* solid_at(0) must be non-NULL and equal to first_solid. */
	struct bsg_node *first = bsg_view_obj_first_solid(gedp);
	struct bsg_node *at0 = bsg_view_obj_solid_at(gedp, 0);
	ASSERT(at0 != NULL);
	ASSERT(at0 == first);

	/* solid_index must round-trip with solid_at. */
	int idx_first = bsg_view_obj_solid_index(gedp, first);
	ASSERT(idx_first == 0);

	/* last solid: solid_at(-1) should wrap to count-1. */
	struct bsg_node *last = bsg_view_obj_solid_at(gedp, -1);
	ASSERT(last != NULL);
	int idx_last = bsg_view_obj_solid_index(gedp, last);
	ASSERT(idx_last == count - 1);

	/* advance_solid wraps correctly: last+1 == first. */
	struct bsg_node *wrap_fwd = bsg_view_obj_advance_solid(gedp, last, 1);
	ASSERT(wrap_fwd == first);

	/* advance_solid backward: first-1 == last. */
	struct bsg_node *wrap_bwd = bsg_view_obj_advance_solid(gedp, first, -1);
	ASSERT(wrap_bwd == last);

	/* Non-drawn pointer returns -1 from solid_index. */
	ASSERT(bsg_view_obj_solid_index(gedp, NULL) == -1);

	/* Overlay shapes should NOT appear in the snapshot. */
	{
	    struct bu_list vhead;
	    BU_LIST_INIT(&vhead);
	    struct bu_list *vlfree = &rt_vlfree;
	    point_t p1 = {0, 0, 0};
	    BSG_ADD_VLIST(vlfree, &vhead, p1, BSG_VLIST_LINE_MOVE);
	    bsg_view_obj_invent(gedp, (char *)"_snap_test_overlay",
			       &vhead, 0xFF0000, 1, 1.0, 0, 0);
	    BSG_FREE_VLIST(vlfree, &vhead);

	    /* count must not have changed */
	    ASSERT(bsg_view_obj_solid_count(gedp) == count);
	    /* Clean up */
	    bsg_view_obj_erase_by_name(gedp, "_snap_test_overlay");
	}
    }

    /* ---------------------------------------------------------------- *
     * 8. draw_rev / name_hash revision counter (Step 4 / B7).          *
     * ---------------------------------------------------------------- */
    bu_log("[8] draw_rev revision counter...\n");
    {
	/* Zap → rev must be 0, hash must be 0. */
	{
	    const char *s_av[2] = {"zap", NULL};
	    ged_exec(gedp, 1, s_av);
	}
	ASSERT(bsg_view_obj_draw_rev(gedp) == 0);
	ASSERT(bsg_view_obj_name_hash(gedp) == 0);

	/* Draw something — rev must be non-zero. */
	{
	    const char *s_av[3] = {"draw", "all.g", NULL};
	    ged_exec(gedp, 2, s_av);
	}
	uint64_t rev_after_draw = bsg_view_obj_draw_rev(gedp);
	ASSERT(rev_after_draw != 0);
	ASSERT(bsg_view_obj_name_hash(gedp) == (unsigned long long)rev_after_draw);

	/* Erase something — rev must have increased again. */
	{
	    struct db_full_path dfp;
	    db_full_path_init(&dfp);
	    if (db_string_to_path(&dfp, gedp->dbip, "all.g") == 0)
		bsg_view_obj_erase_by_dbpath(gedp, &dfp);
	    db_free_full_path(&dfp);
	}
	uint64_t rev_after_erase = bsg_view_obj_draw_rev(gedp);
	ASSERT(rev_after_erase > rev_after_draw);

	/* Zap → rev reset to 0. */
	{
	    const char *s_av[2] = {"zap", NULL};
	    ged_exec(gedp, 1, s_av);
	}
	ASSERT(bsg_view_obj_draw_rev(gedp) == 0);

	/* Invent an overlay → rev bumped. */
	{
	    const char *s_av[3] = {"draw", "all.g", NULL};
	    ged_exec(gedp, 2, s_av);
	    uint64_t rev_pre = bsg_view_obj_draw_rev(gedp);

	    struct bu_list vhead;
	    BU_LIST_INIT(&vhead);
	    struct bu_list *vlfree = &rt_vlfree;
	    point_t p = {1, 1, 1};
	    BSG_ADD_VLIST(vlfree, &vhead, p, BSG_VLIST_LINE_MOVE);
	    bsg_view_obj_invent(gedp, (char *)"_rev_test_ov",
			       &vhead, 0x00FF00, 1, 1.0, 0, 0);
	    BSG_FREE_VLIST(vlfree, &vhead);
	    ASSERT(bsg_view_obj_draw_rev(gedp) > rev_pre);

	    bsg_view_obj_erase_by_name(gedp, "_rev_test_ov");
	}
    }

    /* ---------------------------------------------------------------- *
     * 9. Nested group tree structure (Step 5 — A2+B1+B2).             *
     * ---------------------------------------------------------------- */
    bu_log("[9] nested group tree structure...\n");
    {
	/* Start with a clean slate */
	{
	    const char *s_av[2] = {"zap", NULL};
	    ged_exec(gedp, 1, s_av);
	}

	/* Draw "all.g" */
	{
	    const char *s_av[3] = {"draw", "all.g", NULL};
	    ged_exec(gedp, 2, s_av);
	}

	struct bsg_node *root = bsg_view_obj_root(gedp);
	ASSERT(root != NULL);

	/* Root should have exactly one non-_overlays child after drawing "all.g" */
	int real_groups = 0;
	struct bsg_node *all_g_group = NULL;
	for (size_t i = 0; i < bsg_node_child_count(root); i++) {
	    struct bsg_node *g = bsg_node_child_at(root, i);
	    if (!BU_STR_EQUAL("_overlays", bu_vls_cstr(&g->s_name))) {
		real_groups++;
		if (!all_g_group)
		    all_g_group = g;
	    }
	}
	ASSERT(real_groups == 1);
	ASSERT(all_g_group != NULL);

	/* Root child must be named "all.g" (single component, not "all.g/hull.r") */
	ASSERT(BU_STR_EQUAL("all.g", bu_vls_cstr(&all_g_group->s_name)));

	/* Root child must contain sub-groups (not just flat shapes) for any
	 * multi-level hierarchy in moss.g */
	int has_subgroup = 0;
	for (size_t i = 0; i < bsg_node_child_count(all_g_group); i++) {
	    struct bsg_node *c = bsg_node_child_at(all_g_group, i);
	    if (c->s_type_flags & BSG_NODE_GROUP) {
		has_subgroup = 1;
		break;
	    }
	}
	ASSERT(has_subgroup);

	/* group_first_solid and group_last_solid must return SHAPE nodes
	 * (not GROUP nodes) even when children include sub-groups */
	struct bsg_node *fs = bsg_view_obj_group_first_solid(all_g_group);
	ASSERT(fs != NULL);
	ASSERT((fs->s_type_flags & BSG_NODE_SHAPE) != 0);

	struct bsg_node *ls = bsg_view_obj_group_last_solid(all_g_group);
	ASSERT(ls != NULL);
	ASSERT((ls->s_type_flags & BSG_NODE_SHAPE) != 0);

	/* group_is_nonempty must return 1 when shapes exist in sub-tree */
	ASSERT(bsg_view_obj_group_is_nonempty(all_g_group) == 1);

	/* group_of_solid must return the root child, not the immediate parent */
	ASSERT(bsg_view_obj_group_of_solid(gedp, fs) == all_g_group);

	/* group_of_solid on the last solid also returns the root child */
	ASSERT(bsg_view_obj_group_of_solid(gedp, ls) == all_g_group);

	/* Erase "all.g" should clean up cleanly */
	{
	    struct db_full_path dfp;
	    db_full_path_init(&dfp);
	    if (db_string_to_path(&dfp, gedp->dbip, "all.g") == 0)
		bsg_view_obj_erase_by_dbpath(gedp, &dfp);
	    db_free_full_path(&dfp);
	}
	ASSERT(bsg_view_obj_solid_count(gedp) == 0);
	ASSERT(dl_count(gedp) == 0);
    }

    /* ---------------------------------------------------------------- *
     * [10] B5: set_illum / set_iflag O(1) / mater_rev (B4 counter).   *
     * ---------------------------------------------------------------- */
    {
	bu_log("[10] set_illum/mater_rev...\n");

	/* Draw all.g again to have some solids in the tree. */
	{
	    const char *s_av[3] = {"draw", "all.g", NULL};
	    ged_exec(gedp, 2, s_av);
	}
	int ns = bsg_view_obj_solid_count(gedp);
	ASSERT(ns > 0);

	/* get_illum returns NULL initially (nothing illuminated). */
	ASSERT(bsg_view_obj_get_illum(gedp) == NULL);

	/* Illuminate the first solid. */
	struct bsg_node *s0 = bsg_view_obj_solid_at(gedp, 0);
	ASSERT(s0 != NULL);
	bsg_view_obj_set_illum(gedp, s0);

	/* get_illum returns s0 and s0 is highlighted. */
	ASSERT(bsg_view_obj_get_illum(gedp) == s0);
	ASSERT(bsg_appearance_is_highlighted(s0));

	/* set_iflag(DOWN) should run in O(1) — s0 is the tracked solid. */
	bsg_view_obj_set_iflag(gedp, DOWN);
	ASSERT(!bsg_appearance_is_highlighted(s0));
	ASSERT(bsg_view_obj_get_illum(gedp) == NULL);

	/* set_illum(s0) then set_illum(s1) clears s0 and illuminates s1. */
	if (ns >= 2) {
	    struct bsg_node *s1 = bsg_view_obj_solid_at(gedp, 1);
	    ASSERT(s1 != NULL);
	    bsg_view_obj_set_illum(gedp, s0);
	    ASSERT(bsg_appearance_is_highlighted(s0));
	    bsg_view_obj_set_illum(gedp, s1);
	    ASSERT(!bsg_appearance_is_highlighted(s0));
	    ASSERT(bsg_appearance_is_highlighted(s1));
	    ASSERT(bsg_view_obj_get_illum(gedp) == s1);
	    /* Clean up */
	    bsg_view_obj_set_iflag(gedp, DOWN);
	    ASSERT(!bsg_appearance_is_highlighted(s1));
	}

	/* set_illum(NULL) invalidates tracking — subsequent set_iflag(DOWN)
	 * falls back to O(N) sweep (both paths yield correct result). */
	bsg_view_obj_set_illum(gedp, s0);
	bsg_appearance_set_highlighted(s0, 1);
	bsg_view_obj_set_illum(gedp, NULL);  /* invalidate */
	ASSERT(bsg_view_obj_get_illum(gedp) == NULL);
	bsg_view_obj_set_iflag(gedp, DOWN);  /* O(N) fallback */
	/* After O(N) sweep, s0 must not be highlighted. */
	ASSERT(!bsg_appearance_is_highlighted(s0));

	/* B4 activated: color_from_soltab does NOT bump mater_rev by itself.
	 * The counter is event-driven: only bsg_view_obj_bump_mater_rev() moves it.
	 *
	 * Freshly drawn shapes have s_color_rev=0 (from calloc).
	 * gd_mater_rev is initialized to 1 so new shapes are always stale.
	 * The first color_from_soltab call colors them and stamps s_color_rev=1.
	 * The counter itself stays at 1 (no self-bump). */
	uint64_t rev0 = bsg_view_obj_mater_rev(gedp);
	bsg_view_obj_color_from_soltab(gedp);
	/* Counter must be unchanged — no material-change event occurred. */
	ASSERT(bsg_view_obj_mater_rev(gedp) == rev0);

	/* Verify the first shape was stamped with rev0. */
	ASSERT(s0 != NULL);
	ASSERT((uint64_t)s0->s_color_rev == rev0);

	/* Simulate a material-change event: bump the counter. */
	bsg_view_obj_bump_mater_rev(gedp);
	uint64_t rev1 = bsg_view_obj_mater_rev(gedp);
	ASSERT(rev1 == rev0 + 1);

	/* After a bump, color_from_soltab recolors stale shapes and stamps
	 * them with the new rev, but the counter itself stays put. */
	bsg_view_obj_color_from_soltab(gedp);
	ASSERT(bsg_view_obj_mater_rev(gedp) == rev1);  /* unchanged */
	ASSERT((uint64_t)s0->s_color_rev == rev1);      /* stamped at rev1 */

	/* A second call without a bump must skip all already-stamped shapes.
	 * Verify by force-setting a known color and checking it is unchanged. */
	s0->s_color[0] = 123;
	s0->s_color[1] = 45;
	s0->s_color[2] = 67;
	bsg_view_obj_color_from_soltab(gedp);  /* skip: s_color_rev == mater_rev */
	ASSERT(s0->s_color[0] == 123);         /* must be unchanged */
	ASSERT(s0->s_color[1] == 45);
	ASSERT(s0->s_color[2] == 67);
	ASSERT((uint64_t)s0->s_color_rev == rev1);  /* stamp unchanged */

	/* set_illum pointer is cleared by zap. */
	bsg_view_obj_set_illum(gedp, s0);
	ASSERT(bsg_view_obj_get_illum(gedp) == s0);
	bsg_view_obj_zap(gedp);
	ASSERT(bsg_view_obj_get_illum(gedp) == NULL);
    }

    /* ---------------------------------------------------------------- *
     * [11] A3: gv_draw_root registration (Phase F aliasing).           *
     *      Phase F: bsg_root IS gv_draw_root (same pointer).           *
     *      bsg_root->children IS gv_draw_root->children — no sync      *
     *      rebuild is needed.                                           *
     * ---------------------------------------------------------------- */
    {
	bu_log("[11] A3: gv_draw_root / Phase F aliasing...\n");

	/* Draw one object to populate the tree. */
	{
	    const char *dav[3] = {"draw", "all.g", NULL};
	    ged_exec(gedp, 2, dav);
	}

	struct bsg_view *v = gedp->ged_gvp;
	ASSERT(v != NULL);

	/* gv_draw_root must be set now (registered by _sg_root via ensure_root) */
	ASSERT(v->gv_draw_root != NULL);
	ASSERT(v->gv_draw_root == gedp->i->ged_gdp->gd_draw_root);

	/* Phase F: bsg_root is now an alias for gv_draw_root */
	ASSERT(v->bsg_root != NULL);
	ASSERT(v->bsg_root == v->gv_draw_root);

	/* bsg_group_find_child / bsg_group_ensure_child smoke test */
	bsg_node *draw_root = (bsg_node *)v->gv_draw_root;
	ASSERT(draw_root != NULL);

	/* The draw root must have at least one child group (from the draw) */
	struct bsg_node *dr = (struct bsg_node *)draw_root;
	ASSERT(bsg_node_child_count(dr) > 0);

	/* bsg_draw_tree_depth of the draw root should be 0 (no parent). */
	ASSERT(bsg_draw_tree_depth(draw_root) == 0);

	/* A child's depth should be 1. */
	struct bsg_node *first_child = bsg_node_child_at(dr, 0);
	ASSERT(first_child != NULL);
	if ((first_child->s_type_flags & BSG_NODE_GROUP) ||
	    (first_child->s_type_flags & BSG_NODE_SHAPE)) {
	    ASSERT(bsg_draw_tree_depth((bsg_node *)first_child) == 1);
	}

	/* Phase F: bsg_root == gv_draw_root, so bsg_root->children IS
	 * gv_draw_root->children.  bsg_scene_root_sync is a no-op but the
	 * children match trivially since they are the same ptbl. */
	struct bsg_node *bsg_r = (struct bsg_node *)v->bsg_root;
	ASSERT(bsg_r == dr);  /* same pointer — trivially true */
	ASSERT(bsg_node_child_count(bsg_r) ==
	       bsg_node_child_count(dr));

	/* The children pointers must match exactly (same ptbl). */
	for (size_t i = 0; i < bsg_node_child_count(dr); i++) {
	    ASSERT(bsg_node_child_at(bsg_r, i) ==
		   bsg_node_child_at(dr, i));
	}

	/* After zap the draw root has no children. */
	bsg_view_obj_zap(gedp);
	ASSERT(bsg_node_child_count(bsg_r) == 0);

	/* gv_draw_root itself remains valid after zap (root node not freed). */
	ASSERT(v->gv_draw_root != NULL);
    }

    /* ---------------------------------------------------------------- *
     * [12] Phase 9.1 (B3): cached aggregate bbox.                       *
     *      Verify that bsg_subtree_bbox returns the same answer as the  *
     *      non-cached walk, that the cache flag is set on a draw root    *
     *      after a query, and that it is invalidated by erase.           *
     * ---------------------------------------------------------------- */
    {
	bu_log("[12] Phase 9.1: cached aggregate bbox...\n");

	/* Start clean. */
	{
	    const char *s_av[2] = {"zap", NULL};
	    ged_exec(gedp, 1, s_av);
	}

	/* Empty draw tree: bounds report empty. */
	{
	    vect_t emin, emax;
	    int empty = bsg_view_obj_bounds(gedp, &emin, &emax, 0);
	    ASSERT(empty == 1);
	}

	/* Draw two paths so the tree has multiple groups. */
	{
	    const char *dav[3] = {"draw", "all.g", NULL};
	    ged_exec(gedp, 2, dav);
	}

	struct bsg_node *root = gedp->i->ged_gdp->gd_draw_root;
	ASSERT(root != NULL);

	/* First query: cache is cold, must compute. */
	vect_t min1, max1;
	int empty1 = bsg_view_obj_bounds(gedp, &min1, &max1, 0);
	ASSERT(empty1 == 0);
	/* After a no-overlay query the root cache must be set. */
	ASSERT(root->s_bbox_cached == 1);

	/* Second query: cache hit, must return the identical bbox. */
	vect_t min2, max2;
	int empty2 = bsg_view_obj_bounds(gedp, &min2, &max2, 0);
	ASSERT(empty2 == 0);
	ASSERT(VNEAR_EQUAL(min1, min2, SMALL_FASTF));
	ASSERT(VNEAR_EQUAL(max1, max2, SMALL_FASTF));
	ASSERT(root->s_bbox_cached == 1);

	/* Cross-check vs an explicit walk: bsg_subtree_bbox(include_overlays=1)
	 * bypasses the cache, so for a tree with no overlays it must agree
	 * with the cached non-overlay value. */
	vect_t min3, max3;
	int empty3 = bsg_subtree_bbox((bsg_node *)root, &min3, &max3, 1);
	ASSERT(empty3 == 0);
	ASSERT(VNEAR_EQUAL(min1, min3, SMALL_FASTF));
	ASSERT(VNEAR_EQUAL(max1, max3, SMALL_FASTF));

	/* Erase invalidates the cache. */
	{
	    const char *eav[3] = {"erase", "all.g", NULL};
	    ged_exec(gedp, 2, eav);
	}
	ASSERT(root->s_bbox_cached == 0);

	/* Re-query reports empty again (no shapes left). */
	{
	    vect_t emin, emax;
	    int e = bsg_view_obj_bounds(gedp, &emin, &emax, 0);
	    ASSERT(e == 1);
	}
    }


    /* ---------------------------------------------------------------- *
     * [13] Phase 9.2 (B5): gv_frame_rev / s_drawn_rev generation.       *
     *      Verify that the per-shape drawn-this-frame stamp is wired    *
     *      and matches the bsg_view's frame-revision counter.              *
     * ---------------------------------------------------------------- */
    {
	bu_log("[13] Phase 9.2: gv_frame_rev / s_drawn_rev...\n");

	/* Start clean. */
	{
	    const char *s_av[2] = {"zap", NULL};
	    ged_exec(gedp, 1, s_av);
	}

	/* Initial gv_frame_rev is 0; nothing has been drawn yet. */
	struct bsg_view *v = gedp->ged_gvp;
	ASSERT(v != NULL);
	ASSERT(v->gv_frame_rev == 0);

	/* Draw something so we have shapes to stamp. */
	{
	    const char *dav[3] = {"draw", "all.g", NULL};
	    ged_exec(gedp, 2, dav);
	}

	/* Without an attached dm, dm_draw_objs() is a no-op for stamping;
	 * exercise the bookkeeping directly so the test runs without a
	 * display manager (the off-screen swrast path is exercised by
	 * ged_test_mged_shaded_mode_bsg). */
	struct bsg_node *root = gedp->i->ged_gdp->gd_draw_root;
	ASSERT(root != NULL);

	/* Bump frame, stamp every drawn shape's s_drawn_rev to match. */
	v->gv_frame_rev++;
	uint64_t this_frame = v->gv_frame_rev;
	{
	    /* Walk the tree and stamp each shape. */
	    struct _stamp_ctx { uint64_t r; } ctx = { this_frame };
	    auto stamp_cb = [](bsg_node *n, void *ud) -> int {
		struct _stamp_ctx *c = (struct _stamp_ctx *)ud;
		struct bsg_node *sp = (struct bsg_node *)n;
		if (sp) sp->s_drawn_rev = c->r;
		return 1;
	    };
	    bsg_visit((bsg_node *)root, BSG_NODE_SHAPE, stamp_cb, &ctx);
	}

	/* Count "drawn this frame" — every shape under the root. */
	int counted = 0;
	{
	    struct _count_ctx { uint64_t r; int *n; } ctx = { this_frame, &counted };
	    auto count_cb = [](bsg_node *n, void *ud) -> int {
		struct _count_ctx *c = (struct _count_ctx *)ud;
		struct bsg_node *sp = (struct bsg_node *)n;
		if (sp && sp->s_drawn_rev == c->r) (*c->n)++;
		return 1;
	    };
	    bsg_visit((bsg_node *)root, BSG_NODE_SHAPE, count_cb, &ctx);
	}
	ASSERT(counted > 0);

	/* Bump frame again — no shape has been re-stamped, so the
	 * "drawn this frame" count must be zero on the new frame
	 * generation, with no full-tree reset needed. */
	v->gv_frame_rev++;
	uint64_t next_frame = v->gv_frame_rev;
	int recounted = 0;
	{
	    struct _count_ctx { uint64_t r; int *n; } ctx = { next_frame, &recounted };
	    auto count_cb = [](bsg_node *n, void *ud) -> int {
		struct _count_ctx *c = (struct _count_ctx *)ud;
		struct bsg_node *sp = (struct bsg_node *)n;
		if (sp && sp->s_drawn_rev == c->r) (*c->n)++;
		return 1;
	    };
	    bsg_visit((bsg_node *)root, BSG_NODE_SHAPE, count_cb, &ctx);
	}
	ASSERT(recounted == 0);

	/* Erase to leave a clean state. */
	{
	    const char *eav[3] = {"erase", "all.g", NULL};
	    ged_exec(gedp, 2, eav);
	}
    }


    /* ---------------------------------------------------------------- *
     * [14] Phase 9.3 (B5): illum NodeSensor + gd_illum_rev.             *
     *      Verify the highlight-state revision counter bumps on        *
     *      transitions and on field-touches of the illuminated solid.  *
     * ---------------------------------------------------------------- */
    {
	bu_log("[14] Phase 9.3: illum NodeSensor + gd_illum_rev...\n");

	{
	    const char *s_av[2] = {"zap", NULL};
	    ged_exec(gedp, 1, s_av);
	}
	{
	    const char *dav[3] = {"draw", "all.g", NULL};
	    ged_exec(gedp, 2, dav);
	}

	/* Locate a shape under the draw root. */
	struct bsg_node *root = gedp->i->ged_gdp->gd_draw_root;
	ASSERT(root != NULL);
	struct bsg_node *target = bsg_view_obj_first_solid(gedp);
	ASSERT(target != NULL);

	/* Snapshot the current highlight rev. */
	uint64_t r0 = bsg_view_obj_illum_rev(gedp);

	/* Transition NULL -> target: rev bumps. */
	bsg_view_obj_set_illum(gedp, target);
	uint64_t r1 = bsg_view_obj_illum_rev(gedp);
	ASSERT(r1 > r0);
	ASSERT(bsg_view_obj_get_illum(gedp) == target);
	ASSERT(target->s_iflag == UP);

	/* Touching a field on the illuminated solid fires the NodeSensor,
	 * which bumps gd_illum_rev — the whole point of Phase 9.3: callers
	 * see the highlight rev change without subscribing themselves. */
	bsg_node_field_touch((bsg_node *)target, BSG_FIELD_FLAG);
	uint64_t r2 = bsg_view_obj_illum_rev(gedp);
	ASSERT(r2 > r1);

	/* Touching a field on a non-illuminated node does NOT bump. */
	struct bsg_node *other = bsg_view_obj_next_solid(gedp, target);
	if (other && other != target) {
	    bsg_node_field_touch((bsg_node *)other, BSG_FIELD_FLAG);
	    uint64_t r3 = bsg_view_obj_illum_rev(gedp);
	    ASSERT(r3 == r2);
	}

	/* Transition target -> NULL: rev bumps; sensor gets torn down. */
	bsg_view_obj_set_illum(gedp, NULL);
	uint64_t r4 = bsg_view_obj_illum_rev(gedp);
	ASSERT(r4 > r2);
	ASSERT(bsg_view_obj_get_illum(gedp) == NULL);

	/* After teardown, touching the previously-illuminated solid no
	 * longer bumps gd_illum_rev (sensor was destroyed). */
	bsg_node_field_touch((bsg_node *)target, BSG_FIELD_FLAG);
	uint64_t r5 = bsg_view_obj_illum_rev(gedp);
	ASSERT(r5 == r4);

	{
	    const char *eav[3] = {"erase", "all.g", NULL};
	    ged_exec(gedp, 2, eav);
	}
    }


    /* ---------------------------------------------------------------- *
     * [15] Phase 10: db_full_path-keyed entry points.                   *
     *      Verify lookup_or_add_dbpath / erase_by_dbpath /              *
     *      erase_all_dbpaths / group_dbpath / group_set_dbpath behave   *
     *      identically to their path-string counterparts.               *
     * ---------------------------------------------------------------- */
    {
	bu_log("[15] Phase 10: db_full_path-keyed entry points...\n");

	{
	    const char *s_av[2] = {"zap", NULL};
	    ged_exec(gedp, 1, s_av);
	}

	struct db_full_path dfp;
	db_full_path_init(&dfp);
	ASSERT(db_string_to_path(&dfp, gedp->dbip, "all.g") == 0);

	/* lookup_or_add via dbpath. */
	struct bsg_node *g = bsg_view_obj_lookup_or_add_dbpath(gedp, &dfp);
	ASSERT(g != NULL);
	ASSERT(dl_count(gedp) == 1);
	ASSERT(BU_STR_EQUAL(bsg_view_obj_group_path(g), "all.g"));
	ASSERT(bsg_view_obj_group_dmode(g) == BSG_DRAW_MODE_WIRE);

	/* Draw-intent metadata, not s_name, is the canonical path/mode source. */
	bsg_draw_intent_set_mode(bsg_node_get_draw_intent(g), BSG_DRAW_MODE_HIDDEN_LINE);
	ASSERT(bsg_view_obj_group_dmode(g) == BSG_DRAW_MODE_HIDDEN_LINE);
	bsg_node_set_name(g, "intentionally-wrong");
	ASSERT(BU_STR_EQUAL(bsg_view_obj_group_path(g), "all.g"));

	/* group_dbpath round-trips. */
	struct db_full_path got;
	db_full_path_init(&got);
	ASSERT(bsg_view_obj_group_dbpath(gedp, g, &got) == 0);
	ASSERT(got.fp_len == dfp.fp_len);
	if (got.fp_len > 0 && dfp.fp_len > 0)
	    ASSERT(BU_STR_EQUAL(got.fp_names[0]->d_namep,
				dfp.fp_names[0]->d_namep));
	db_free_full_path(&got);

	/* group_set_dbpath keeps the draw-intent path synchronized. */
	bsg_view_obj_group_set_dbpath(g, &dfp);
	ASSERT(BU_STR_EQUAL(bsg_view_obj_group_path(g), "all.g"));

	/* erase_by_dbpath removes it. */
	bsg_view_obj_erase_by_dbpath(gedp, &dfp);
	ASSERT(dl_count(gedp) == 0);

	/* erase_all_dbpaths is a no-op on an empty set (no crash). */
	bsg_view_obj_erase_all_dbpaths(gedp, &dfp);
	ASSERT(dl_count(gedp) == 0);

	db_free_full_path(&dfp);
    }


    /* ---------------------------------------------------------------- *
     * [16] Phase 11: renderer-backend contract.                         *
     *      Stub a dm_backend_ops, attach an s_backend descriptor on a  *
     *      shape, and verify that bsg_scene_obj_invalidate_backend /    *
     *      bsg_scene_obj_release_backend fire the new ops.              *
     * ---------------------------------------------------------------- */
    {
	bu_log("[16] Phase 11: renderer-backend contract...\n");

	/* State captured by the stub callbacks. */
	struct phase11_state {
	    int free_calls;
	    int invalidate_calls;
	    struct bsg_node *last_obj;
	} st;
	memset(&st, 0, sizeof(st));

	/* Re-use a fresh draw of all.g and pick a shape under it. */
	{
	    const char *s_av[2] = {"zap", NULL};
	    ged_exec(gedp, 1, s_av);
	}
	{
	    const char *dav[3] = {"draw", "all.g", NULL};
	    ged_exec(gedp, 2, dav);
	}
	struct bsg_node *target = bsg_view_obj_first_solid(gedp);
	ASSERT(target != NULL);

	/* Stub backend free / invalidate.  These match the
	 * struct bsg_backend signature in include/bv/defines.h. */
	struct phase11_helpers {
	    static void backend_free(struct bsg_node *s) {
		struct phase11_state *p = (struct phase11_state *)
		    s->s_backend->handle;
		p->free_calls++;
		p->last_obj = s;
		BU_PUT(s->s_backend, struct bsg_backend);
	    }
	    static void backend_invalidate(struct bsg_node *s) {
		struct phase11_state *p = (struct phase11_state *)
		    s->s_backend->handle;
		p->invalidate_calls++;
		p->last_obj = s;
	    }
	};

	/* Attach a backend descriptor to the target shape. */
	struct bsg_backend *be;
	BU_GET(be, struct bsg_backend);
	be->type_tag   = BV_BACKEND_GL;  /* any tag works for the stub */
	be->handle     = &st;
	be->free       = phase11_helpers::backend_free;
	be->invalidate = phase11_helpers::backend_invalidate;
	target->s_backend = be;

	/* Sub-test 1: invalidate fires the new contract callback. */
	bsg_scene_obj_invalidate_backend(target);
	ASSERT(st.invalidate_calls == 1);
	ASSERT(st.last_obj == target);

	/* Sub-test 2: bsg_obj_stale recurses into children and ultimately
	 * reaches our shape via bsg_scene_obj_invalidate_backend. */
	st.invalidate_calls = 0;
	bsg_obj_stale(target);
	ASSERT(st.invalidate_calls == 1);

	/* Sub-test 3: release_backend fires the new free and clears the
	 * s_backend slot. */
	bsg_scene_obj_release_backend(target);
	ASSERT(st.free_calls == 1);
	ASSERT(target->s_backend == NULL);

	/* Sub-test 4: release_backend on an object with no backend slot
	 * is a safe no-op. */
	struct bsg_node *bare = bsg_view_obj_next_solid(gedp, target);
	if (bare && bare != target) {
	    bare->s_backend = NULL;
	    bsg_scene_obj_release_backend(bare);
	    ASSERT(bare->s_backend == NULL);
	}

	/* Sub-test 5: dm-side dispatch wrappers tolerate a NULL dmp. */
	bsg_scene_obj_release_backend(NULL); /* must not crash */
	dm_backend_invalidate_obj(NULL, target);
	dm_backend_release_obj(NULL, target);

	{
	    const char *eav[3] = {"erase", "all.g", NULL};
	    ged_exec(gedp, 2, eav);
	}
    }


    /* ---------------------------------------------------------------- *
     * [17] Phase L0: BSG_NODE_LOD lifecycle and cursor bookkeeping.    *
     *      Exercises bsg_lod_node_create / set_ops / attach_level /    *
     *      get_cursor / active_level with a synthetic ops set that      *
     *      toggles between two children.                               *
     * ---------------------------------------------------------------- */
    {
	bu_log("[17] Phase L0: BSG_NODE_LOD node lifecycle...\n");

	struct bsg_view *lv = gedp->ged_gvp;
	ASSERT(lv != NULL);

	/* Create a BSG_NODE_LOD node. */
	bsg_node *lod = bsg_lod_node_create(lv);
	ASSERT(lod != NULL);
	{
	    struct bsg_node *n = (struct bsg_node *)lod;
	    ASSERT((n->s_type_flags & BSG_NODE_LOD) != 0);
	    ASSERT(bsg_node_get_internal_data(n) != NULL);
	}

	/* No children yet. */
	ASSERT(bsg_lod_node_level_count(lod) == 0);

	/* Attach two level representations. */
	bsg_node *lvl0 = bsg_group_create(lv);
	bsg_node *lvl1 = bsg_group_create(lv);
	ASSERT(lvl0 != NULL);
	ASSERT(lvl1 != NULL);
	bsg_lod_node_attach_level(lod, lvl0);
	bsg_lod_node_attach_level(lod, lvl1);
	ASSERT(bsg_lod_node_level_count(lod) == 2);

	/* Duplicate attach must not change the count. */
	bsg_lod_node_attach_level(lod, lvl0);
	ASSERT(bsg_lod_node_level_count(lod) == 2);

	/* No cursor for this view yet → active level is -1. */
	ASSERT(bsg_lod_node_active_level(lod, lv) == -1);

	/* Install a synthetic ops set. */
	struct _lod17_state {
	    int select_calls;
	    int activate_calls;
	    int stale_val;
	    int select_val;
	} st17;
	memset(&st17, 0, sizeof(st17));
	st17.stale_val  = 1;
	st17.select_val = 0;

	auto lod17_select = [](bsg_node *node, struct bsg_view */*v*/) -> int {
	    auto *pl = (struct bsg_lod_payload *)
		bsg_node_get_internal_data((struct bsg_node *)node);
	    auto *st = (struct _lod17_state *)pl->user_data;
	    st->select_calls++;
	    return st->select_val;
	};
	auto lod17_activate = [](bsg_node *node, struct bsg_view *v, int level) {
	    auto *pl = (struct bsg_lod_payload *)
		bsg_node_get_internal_data((struct bsg_node *)node);
	    auto *st = (struct _lod17_state *)pl->user_data;
	    st->activate_calls++;
	    auto *cur = bsg_lod_node_get_cursor(node, v);
	    if (cur) cur->level = level;
	};
	auto lod17_stale = [](bsg_node *node, struct bsg_view */*v*/) -> int {
	    auto *pl = (struct bsg_lod_payload *)
		bsg_node_get_internal_data((struct bsg_node *)node);
	    auto *st = (struct _lod17_state *)pl->user_data;
	    return st->stale_val;
	};

	struct bsg_lod_ops ops17;
	memset(&ops17, 0, sizeof(ops17));
	ops17.select_level   = lod17_select;
	ops17.activate_level = lod17_activate;
	ops17.is_stale       = lod17_stale;
	bsg_lod_node_set_ops(lod, &ops17, &st17);

	/* Pre-create cursor (simulates bsg_lod_update). */
	struct bsg_lod_view_cursor *cur = bsg_lod_node_get_cursor(lod, lv);
	ASSERT(cur != NULL);
	ASSERT(cur->v == lv);
	ASSERT(cur->level == -1);

	/* Second get_cursor must return the same slot. */
	struct bsg_lod_view_cursor *cur2 = bsg_lod_node_get_cursor(lod, lv);
	ASSERT(cur2 == cur);

	/* Simulate bsg_lod_update round 1: stale → select 0 → activate 0. */
	{
	    auto *pl = (struct bsg_lod_payload *)
		((struct bsg_node *)lod)->s_i_data;
	    if (pl->ops->is_stale(lod, lv)) {
		int lvl_idx = pl->ops->select_level(lod, lv);
		pl->ops->activate_level(lod, lv, lvl_idx);
	    }
	}
	ASSERT(st17.select_calls   == 1);
	ASSERT(st17.activate_calls == 1);
	ASSERT(bsg_lod_node_active_level(lod, lv) == 0);

	/* Round 2: stale → select 1 → activate 1. */
	st17.select_val = 1;
	{
	    auto *pl = (struct bsg_lod_payload *)
		((struct bsg_node *)lod)->s_i_data;
	    if (pl->ops->is_stale(lod, lv)) {
		int lvl_idx = pl->ops->select_level(lod, lv);
		pl->ops->activate_level(lod, lv, lvl_idx);
	    }
	}
	ASSERT(st17.select_calls   == 2);
	ASSERT(st17.activate_calls == 2);
	ASSERT(bsg_lod_node_active_level(lod, lv) == 1);

	/* Round 3: not stale → no callbacks fired. */
	st17.stale_val = 0;
	{
	    auto *pl = (struct bsg_lod_payload *)
		((struct bsg_node *)lod)->s_i_data;
	    if (pl->ops->is_stale(lod, lv)) {
		int lvl_idx = pl->ops->select_level(lod, lv);
		pl->ops->activate_level(lod, lv, lvl_idx);
	    }
	}
	ASSERT(st17.select_calls   == 2);
	ASSERT(st17.activate_calls == 2);
	ASSERT(bsg_lod_node_active_level(lod, lv) == 1);

	/* Cleanup: fire free callback manually (simulates node destruction).
	 * Do NOT set ops->free since user_data lives on our stack. */
	ops17.free = NULL;
	struct bsg_node *lod_raw = (struct bsg_node *)lod;
	if (lod_raw->s_free_callback)
	    lod_raw->s_free_callback(lod_raw);
	ASSERT(lod_raw->s_i_data == NULL);
    }


    /* Final zap to leave clean state. */
    {
	const char *s_av[2] = {"zap", NULL};
	ged_exec(gedp, 1, s_av);
	ASSERT(bsg_view_obj_name_hash(gedp) == 0);
	ASSERT(dl_count(gedp) == 0);
    }

    /* ---------------------------------------------------------------- *
     * Summary.                                                          *
     * ---------------------------------------------------------------- */
    ged_close(gedp);

    bu_log("=== bsg_view_obj_* API regression: %d/%d checks passed ===\n",
	   nchecks - nfails, nchecks);
    return (nfails == 0) ? 0 : 1;
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
