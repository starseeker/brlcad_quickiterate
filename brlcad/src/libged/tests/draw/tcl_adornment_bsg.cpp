/*        T C L _ A D O R N M E N T _ B S G . C P P
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
/** @file tcl_adornment_bsg.cpp
 *
 * Phase T1/T4 (drawing_stack_modernization) — structural regression test.
 *
 * Verifies that the BSG view-scope object lifecycle used by the T1
 * adornment-sync helpers (arrows, lines, labels, axes, polygons) behaves
 * correctly:
 *
 *   • bsg_view_obj_lines_create  creates a named local-scope object
 *   • bsg_view_obj_find          locates it by name
 *   • bsg_view_obj_visit         visits it via the scope callback
 *   • bsg_view_obj_remove        deletes it; subsequent find returns NULL
 *   • dm_draw_objs              can be called headlessly (NULL dmp) without
 *                               crashing — the T2-final call site in
 *                               go_refresh_draw must be no-op safe.
 *
 * This test does NOT attach a display manager.  It is intentionally headless
 * so that the BSG adornment object contract is enforced independently of
 * any rendering backend.
 *
 * Usage: ged_test_tcl_adornment_bsg <directory-with-moss.g>
 */

#include "common.h"

#include <cstdio>
#include <cstring>
#include <fstream>

#include <bu.h>
#include <bsg.h>
#include "bsg/tcl_data.h"
#include "bsg/util.h"
#include "bsg/vlist.h"
#include <dm.h>
#include <ged.h>

#define ASSERT(cond) do { \
    nchecks++; \
    if (!(cond)) { \
	bu_log("FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond); \
	nfails++; \
    } \
} while (0)

static int nchecks = 0;
static int nfails  = 0;

/* Visitor that counts objects reached via bsg_view_obj_visit. */
static int
_count_visit_cb(struct bsg_node * /*s*/, void *ud)
{
    int *cnt = (int *)ud;
    (*cnt)++;
    return 1;
}

int
main(int ac, char *av[])
{
    bu_setprogname(av[0]);

    if (ac != 2) {
	bu_exit(EXIT_FAILURE, "Usage: %s <directory-containing-moss.g>\n", av[0]);
    }

    /* ------------------------------------------------------------------ *
     * Open a headless GED session.                                        *
     * ------------------------------------------------------------------ */
    struct bu_vls fname = BU_VLS_INIT_ZERO;
    struct bu_vls moss = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&moss, "%s/moss.g", av[1]);
    char tmpname[MAXPATHLEN] = {0};
    FILE *fp = bu_temp_file(tmpname, MAXPATHLEN);
    if (!fp) {
	bu_log("failed to create temp db path\n");
	bu_vls_free(&moss);
	bu_vls_free(&fname);
	return 1;
    }
    fclose(fp);
    bu_vls_sprintf(&fname, "%s", tmpname);
    {
	/* This test is headless, but ged_open still requires a valid .g file. */
	std::ifstream orig(bu_vls_cstr(&moss), std::ios::binary);
	std::ofstream tmpg(bu_vls_cstr(&fname), std::ios::binary);
	if (!orig.good() || !tmpg.good()) {
	    bu_log("failed to prepare tmp db: %s\n", bu_vls_cstr(&fname));
	    bu_vls_free(&moss);
	    bu_vls_free(&fname);
	    return 1;
	}
	tmpg << orig.rdbuf();
	orig.close();
	tmpg.close();
    }
    struct ged *gedp = ged_open("db", bu_vls_cstr(&fname), 1);
    bu_vls_free(&moss);
    if (!gedp) {
	bu_log("ged_open failed\n");
	bu_file_delete(bu_vls_cstr(&fname));
	bu_vls_free(&fname);
	return 1;
    }

    bu_log("=== TCL adornment BSG lifecycle ===\n");

    struct bsg_view *v = gedp->ged_gvp;
    ASSERT(v != NULL);
    ASSERT(v->gv_draw_root != NULL);

    /* ------------------------------------------------------------------ *
     * [1] create: bsg_view_obj_lines_create must return a non-NULL scene  *
     *     object and register it in the local scope.                     *
     * ------------------------------------------------------------------ */
    bu_log("[1] bsg_view_obj_lines_create...\n");
    const char *tname = "_tcl_test_adornment";
    struct bsg_node *obj = bsg_view_obj_lines_create(v, tname, 1 /*local*/);
    ASSERT(obj != NULL);
    if (!obj) goto done;

    /* ------------------------------------------------------------------ *
     * [2] find: bsg_view_obj_find must locate the object by name.         *
     * ------------------------------------------------------------------ */
    bu_log("[2] bsg_view_obj_find...\n");
    {
	struct bsg_node *found = bsg_view_obj_find(v, tname);
	ASSERT(found != NULL);
	ASSERT(found == obj);
    }

    /* ------------------------------------------------------------------ *
     * [3] vlist: add vlist data and verify non-empty.                    *
     * ------------------------------------------------------------------ */
    bu_log("[3] BSG_ADD_VLIST...\n");
    {
	point_t p0 = {0, 0, 0};
	point_t p1 = {1, 0, 0};
	BSG_ADD_VLIST(obj->vlfree, &obj->s_vlist, p0, BSG_VLIST_LINE_MOVE);
	BSG_ADD_VLIST(obj->vlfree, &obj->s_vlist, p1, BSG_VLIST_LINE_DRAW);
	ASSERT(!BU_LIST_IS_EMPTY(&obj->s_vlist));
    }

    /* ------------------------------------------------------------------ *
     * [4] set_color / set_line_width / set_visible typed setters.        *
     * ------------------------------------------------------------------ */
    bu_log("[4] typed setters...\n");
    bsg_view_obj_set_color(obj, 255, 128, 0);
    bsg_view_obj_set_line_width(obj, 2);
    bsg_view_obj_set_visible(obj, 1);
    ASSERT(obj->s_color[0] == 255 && obj->s_color[1] == 128 && obj->s_color[2] == 0);
    ASSERT(obj->s_os->s_line_width == 2);
    ASSERT(obj->s_force_draw == 1);

    /* ------------------------------------------------------------------ *
     * [5] visit: bsg_view_obj_visit with BV_VIEW_OBJ_SCOPE_LOCAL must    *
     *     reach at least the one object we created.                      *
     * ------------------------------------------------------------------ */
    bu_log("[5] bsg_view_obj_visit (local scope)...\n");
    {
	int cnt = 0;
	bsg_view_obj_visit(v, BV_VIEW_OBJ_SCOPE_LOCAL, _count_visit_cb, &cnt);
	ASSERT(cnt >= 1);
    }

    /* ------------------------------------------------------------------ *
     * [6] dm_draw_objs headless: must not crash when dmp is NULL.        *
     *     This mirrors the T2-final call in go_refresh_draw.             *
     * ------------------------------------------------------------------ */
    bu_log("[6] dm_draw_objs headless (NULL dmp)...\n");
    {
	struct dm *saved_dmp = (struct dm *)v->dmp;
	v->dmp = NULL;
	dm_draw_objs(v, NULL, NULL);   /* must be a no-op, not a crash */
	v->dmp = saved_dmp;
    }

    /* ------------------------------------------------------------------ *
     * [7] remove: bsg_view_obj_remove must delete the named object;       *
     *     a subsequent find must return NULL.                            *
     * ------------------------------------------------------------------ */
    bu_log("[7] bsg_view_obj_remove...\n");
    {
	int r = bsg_view_obj_remove(v, tname);
	ASSERT(r == 1);
	struct bsg_node *gone = bsg_view_obj_find(v, tname);
	ASSERT(gone == NULL);
    }

    /* ------------------------------------------------------------------ *
     * [8] remove idempotency: removing a non-existent name is safe.      *
     * ------------------------------------------------------------------ */
    bu_log("[8] remove idempotency...\n");
    {
	int r = bsg_view_obj_remove(v, "_tcl_test_nonexistent");
	(void)r; /* return value may differ by impl; the call must not crash */
    }

    /* ------------------------------------------------------------------ *
     * [9] multi-object: create data/sdata objects for all four adornment *
     *     slots (arrows, lines, labels, polygons) and verify they are    *
     *     independently addressable and removable.                       *
     * ------------------------------------------------------------------ */
    bu_log("[9] multi-object adornment slots...\n");
    {
	const char *slots[] = {
	    "_tcl_data_arrows",    "_tcl_sdata_arrows",
	    "_tcl_data_lines",     "_tcl_sdata_lines",
	    "_tcl_data_labels",    "_tcl_sdata_labels",
	    "_tcl_data_axes",      "_tcl_sdata_axes",
	    "_tcl_data_polygons",  "_tcl_sdata_polygons",
	    NULL
	};
	/* Create all slots */
	for (int k = 0; slots[k]; k++) {
	    struct bsg_node *s = bsg_view_obj_lines_create(v, slots[k], 1);
	    ASSERT(s != NULL);
	}
	/* Verify all are findable */
	for (int k = 0; slots[k]; k++) {
	    struct bsg_node *s = bsg_view_obj_find(v, slots[k]);
	    ASSERT(s != NULL);
	}
	/* Remove all slots */
	for (int k = 0; slots[k]; k++) {
	    bsg_view_obj_remove(v, slots[k]);
	    struct bsg_node *gone = bsg_view_obj_find(v, slots[k]);
	    ASSERT(gone == NULL);
	}
    }

done:
    ged_close(gedp);
    bu_file_delete(bu_vls_cstr(&fname));
    bu_vls_free(&fname);

    bu_log("Result: %d checks, %d failures\n", nchecks, nfails);
    return (nfails > 0) ? 1 : 0;
}

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
