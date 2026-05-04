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
 * include/ged/bsg_view_obj.h.  Each helper is currently a thin wrapper
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
#include <bv.h>
#include "bv/tcl_data.h"
#include <ged.h>
#include "ged/bsg_view_obj.h"
#include "bsg/defines.h"
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
_dl_count_cb(struct bv_scene_obj * /*g*/, void *ud)
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
    ASSERT(bsg_view_obj_lookup_or_add_path(NULL, "any") == NULL);
    ASSERT(bsg_view_obj_lookup_or_add_path(gedp, NULL) == NULL);
    bsg_view_obj_erase_by_path(NULL, "x", 0);              /* no crash */
    bsg_view_obj_erase_by_path(gedp, NULL, 0);             /* no crash */
    bsg_view_obj_erase_by_name(NULL, "x", 0);              /* no crash */
    bsg_view_obj_erase_by_name(gedp, NULL, 0);             /* no crash */
    bsg_view_obj_erase_all_paths(NULL, "x", 0);            /* no crash */
    bsg_view_obj_erase_all_paths(gedp, NULL, 0);           /* no crash */
    bsg_view_obj_set_iflag(NULL, 0);                       /* no crash */
    bsg_view_obj_color_from_soltab(NULL);                  /* no crash */
    ASSERT(bsg_view_obj_name_hash(NULL) == 0);
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

    /* lookup_or_add_path on an already-drawn path must return non-NULL
     * and must not insert a duplicate. */
    int before_lookup = dl_count(gedp);
    void *h = bsg_view_obj_lookup_or_add_path(gedp, "all.g");
    ASSERT(h != NULL);
    ASSERT(dl_count(gedp) == before_lookup);

    /* lookup_or_add_path on a non-existent leaf must return NULL.  The
     * legacy dl_addToDisplay() emits a noisy LOOKUP_NOISY log message
     * for missing leaves; that's expected behavior we preserve. */
    void *h_missing =
	bsg_view_obj_lookup_or_add_path(gedp, "definitely_no_such_obj");
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
	struct iflag_check { int *ok; int target; } cu = { &all_up, UP };
	auto cb = +[](struct bv_scene_obj *sp, void *ud) -> int {
	    struct iflag_check *c = (struct iflag_check *)ud;
	    if (sp->s_iflag != c->target) *c->ok = 0;
	    return 1;
	};
	bsg_view_obj_foreach_solid(gedp, cb, &cu);
	ASSERT(all_up);
    }
    bsg_view_obj_set_iflag(gedp, DOWN);
    {
	int all_down = 1;
	struct iflag_check { int *ok; int target; } cd = { &all_down, DOWN };
	auto cb = +[](struct bv_scene_obj *sp, void *ud) -> int {
	    struct iflag_check *c = (struct iflag_check *)ud;
	    if (sp->s_iflag != c->target) *c->ok = 0;
	    return 1;
	};
	bsg_view_obj_foreach_solid(gedp, cb, &cd);
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
	BV_ADD_VLIST(vlfree, &vhead, p1, BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(vlfree, &vhead, p2, BV_VLIST_LINE_DRAW);

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
	    struct bv_scene_obj *root = bsg_view_obj_root(gedp);
	    struct bv_scene_obj *overlays_grp = NULL;
	    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
		struct bv_scene_obj *g =
		    (struct bv_scene_obj *)BU_PTBL_GET(&root->children, i);
		if (BU_STR_EQUAL("_overlays", bu_vls_cstr(&g->s_name))) {
		    overlays_grp = g;
		    break;
		}
	    }
	    ASSERT(overlays_grp != NULL);
	    ASSERT(bsg_view_obj_group_is_phony(overlays_grp));

	    /* The overlay shape must have BSG_PAYLOAD_OVERLAY set. */
	    if (overlays_grp && BU_PTBL_LEN(&overlays_grp->children) > 0) {
		struct bv_scene_obj *sp =
		    (struct bv_scene_obj *)BU_PTBL_GET(&overlays_grp->children, 0);
		ASSERT(sp->s_type_flags & BSG_PAYLOAD_OVERLAY);
		/* No phony db entry should exist for this name. */
		ASSERT(db_lookup(gedp->dbip, "_bsg_test_phony", LOOKUP_QUIET)
		       == RT_DIR_NULL);
	    }
	}

	/* Free the local vlist (we passed copy=1, so vhead still owns it). */
	BV_FREE_VLIST(vlfree, &vhead);

	/* Erase the phony solid by name. */
	bsg_view_obj_erase_by_name(gedp, "_bsg_test_phony", 0);
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

	/* erase_by_path on the exact drawn name must remove that entry. */
	bsg_view_obj_erase_by_path(gedp, "all.g", 0);
	ASSERT(dl_count(gedp) < before);

	/* Re-draw and try erase_all_paths with skip_first=0. */
	ged_exec(gedp, 2, s_av);
	int before2 = dl_count(gedp);
	ASSERT(before2 > 0);
	bsg_view_obj_erase_all_paths(gedp, "all.g", 0);
	/* Note: erase_all_paths matches subset paths, so should clear
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
	struct bv_scene_obj *first = bsg_view_obj_first_solid(gedp);
	struct bv_scene_obj *at0 = bsg_view_obj_solid_at(gedp, 0);
	ASSERT(at0 != NULL);
	ASSERT(at0 == first);

	/* solid_index must round-trip with solid_at. */
	int idx_first = bsg_view_obj_solid_index(gedp, first);
	ASSERT(idx_first == 0);

	/* last solid: solid_at(-1) should wrap to count-1. */
	struct bv_scene_obj *last = bsg_view_obj_solid_at(gedp, -1);
	ASSERT(last != NULL);
	int idx_last = bsg_view_obj_solid_index(gedp, last);
	ASSERT(idx_last == count - 1);

	/* advance_solid wraps correctly: last+1 == first. */
	struct bv_scene_obj *wrap_fwd = bsg_view_obj_advance_solid(gedp, last, 1);
	ASSERT(wrap_fwd == first);

	/* advance_solid backward: first-1 == last. */
	struct bv_scene_obj *wrap_bwd = bsg_view_obj_advance_solid(gedp, first, -1);
	ASSERT(wrap_bwd == last);

	/* Non-drawn pointer returns -1 from solid_index. */
	ASSERT(bsg_view_obj_solid_index(gedp, NULL) == -1);

	/* Overlay shapes should NOT appear in the snapshot. */
	{
	    struct bu_list vhead;
	    BU_LIST_INIT(&vhead);
	    struct bu_list *vlfree = &rt_vlfree;
	    point_t p1 = {0, 0, 0};
	    BV_ADD_VLIST(vlfree, &vhead, p1, BV_VLIST_LINE_MOVE);
	    bsg_view_obj_invent(gedp, (char *)"_snap_test_overlay",
			       &vhead, 0xFF0000, 1, 1.0, 0, 0);
	    BV_FREE_VLIST(vlfree, &vhead);

	    /* count must not have changed */
	    ASSERT(bsg_view_obj_solid_count(gedp) == count);
	    /* Clean up */
	    bsg_view_obj_erase_by_name(gedp, "_snap_test_overlay", 0);
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
	bsg_view_obj_erase_by_path(gedp, "all.g", 0);
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
	    BV_ADD_VLIST(vlfree, &vhead, p, BV_VLIST_LINE_MOVE);
	    bsg_view_obj_invent(gedp, (char *)"_rev_test_ov",
			       &vhead, 0x00FF00, 1, 1.0, 0, 0);
	    BV_FREE_VLIST(vlfree, &vhead);
	    ASSERT(bsg_view_obj_draw_rev(gedp) > rev_pre);

	    bsg_view_obj_erase_by_name(gedp, "_rev_test_ov", 0);
	}
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
