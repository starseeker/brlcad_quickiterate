/*                    T E S T _ D B I _ C . C
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
/** @file test_dbi_c.c
 *
 * Exercise the C surface of the DBI layer (ged_dbi_* and
 * ged_selection_* functions declared in ged/dbi.h).
 *
 * Creates a temporary .g database, populates it with a few primitives
 * and a combination, then exercises all C-surface entry points.
 *
 * Expected exit codes:
 *   0  all checks passed
 *   1  usage error
 *   3  one or more checks failed
 */

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bu.h"
#include "bu/ptbl.h"
#include "ged.h"
#include "ged/dbi.h"

#define CHECK(cond, msg) \
    do { \
	if (!(cond)) { \
	    fprintf(stderr, "FAIL: %s (line %d)\n", (msg), __LINE__); \
	    failures++; \
	} else { \
	    printf("PASS: %s\n", (msg)); \
	} \
    } while (0)

int
main(int argc, char *argv[])
{
    int failures = 0;
    struct ged *gedp;
    struct bu_ptbl tops = BU_PTBL_INIT_ZERO;
    struct bu_ptbl paths_out = BU_PTBL_INIT_ZERO;
    unsigned long long h;
    int ret;
    char tmpfile[MAXPATHLEN] = {0};

    bu_setprogname(argv[0]);

    if (argc != 2) {
	fprintf(stderr, "Usage: %s output.g\n", argv[0]);
	return 1;
    }

    /* Use the supplied path as our temporary database file.
     * Remove it first so ged_open creates a fresh file. */
    bu_strlcpy(tmpfile, argv[1], sizeof(tmpfile));
    bu_file_delete(tmpfile);

    gedp = ged_open("db", tmpfile, 0 /* 0 = create if missing */);
    if (!gedp) {
	fprintf(stderr, "ERROR: ged_open failed for %s\n", tmpfile);
	return 3;
    }

    /* ------------------------------------------------------------------ */
    /* Populate the database with a sphere and a comb                     */
    /* ------------------------------------------------------------------ */
    {
	/* in sph1.s sph 0 0 0 10 */
	const char *av[] = {"in", "sph1.s", "sph", "0", "0", "0", "10", NULL};
	ged_exec_in(gedp, 7, av);
    }
    {
	/* in tor1.s tor 0 0 0 0 0 1 20 5 */
	const char *av[] = {"in", "tor1.s", "tor", "0","0","0","0","0","1","20","5", NULL};
	ged_exec_in(gedp, 11, av);
    }
    {
	/* r reg1.r u sph1.s */
	const char *av[] = {"r", "reg1.r", "u", "sph1.s", NULL};
	ged_exec_r(gedp, 4, av);
    }
    {
	/* g top1.c reg1.r tor1.s */
	const char *av[] = {"g", "top1.c", "reg1.r", "tor1.s", NULL};
	ged_exec_g(gedp, 4, av);
    }

    /* ------------------------------------------------------------------ */
    /* Database-state helpers                                              */
    /* ------------------------------------------------------------------ */

    /* ged_dbi_update */
    {
	unsigned long long flags = ged_dbi_update(gedp);
	(void)flags;
	printf("PASS: ged_dbi_update returned 0x%llx\n", (unsigned long long)flags);
    }

    /* ged_dbi_tops */
    bu_ptbl_init(&tops, 8, "tops");
    ret = ged_dbi_tops(gedp, &tops);
    CHECK(ret > 0, "ged_dbi_tops returns >0 for a non-empty database");
    printf("      tops count = %d\n", ret);
    bu_ptbl_free(&tops);

    /* ged_dbi_hash_of / ged_dbi_valid_hash */
    h = ged_dbi_hash_of(gedp, "top1.c");
    CHECK(h != 0, "ged_dbi_hash_of('top1.c') is non-zero");

    ret = ged_dbi_valid_hash(gedp, h);
    CHECK(ret != 0, "ged_dbi_valid_hash for 'top1.c' hash is non-zero");

    h = ged_dbi_hash_of(gedp, "__no_such_object__");
    CHECK(h == 0, "ged_dbi_hash_of for non-existent name returns 0");

    /* ged_dbi_valid_hash(0) == false */
    ret = ged_dbi_valid_hash(gedp, 0ULL);
    CHECK(ret == 0, "ged_dbi_valid_hash(0) returns 0");

    /* NULL gedp should not crash */
    h = ged_dbi_hash_of(NULL, "top1.c");
    CHECK(h == 0, "ged_dbi_hash_of(NULL gedp) returns 0 safely");

    /* ------------------------------------------------------------------ */
    /* GObj helpers                                                        */
    /* ------------------------------------------------------------------ */

    /* top1.c should be a comb */
    ret = ged_dbi_gobj_is_comb(gedp, "top1.c");
    CHECK(ret == 1, "ged_dbi_gobj_is_comb('top1.c') returns 1");

    /* sph1.s is a primitive */
    ret = ged_dbi_gobj_is_comb(gedp, "sph1.s");
    CHECK(ret == 0, "ged_dbi_gobj_is_comb('sph1.s') returns 0");

    /* non-existent object */
    ret = ged_dbi_gobj_is_comb(gedp, "__no_such_object__");
    CHECK(ret == -1, "ged_dbi_gobj_is_comb for non-existent name returns -1");

    /* region_id: reg1.r was created with `r` which sets region_id -1 by default */
    ret = ged_dbi_gobj_region_id(gedp, "reg1.r");
    printf("      ged_dbi_gobj_region_id('reg1.r') = %d\n", ret);
    /* Just check it doesn't return an error */
    ret = ged_dbi_gobj_region_id(gedp, "__none__");
    CHECK(ret == -1, "ged_dbi_gobj_region_id for non-existent name returns -1");

    /* child count */
    ret = ged_dbi_gobj_child_count(gedp, "top1.c");
    CHECK(ret == 2, "ged_dbi_gobj_child_count('top1.c') returns 2");

    ret = ged_dbi_gobj_child_count(gedp, "sph1.s");
    CHECK(ret == -1, "ged_dbi_gobj_child_count on primitive returns -1");

    ret = ged_dbi_gobj_child_count(gedp, "__none__");
    CHECK(ret == -1, "ged_dbi_gobj_child_count for non-existent name returns -1");

    /* color: no color set on freshly created objects → returns 0 */
    {
	unsigned char r = 0, g = 0, b = 0;
	ret = ged_dbi_gobj_color(gedp, "top1.c", &r, &g, &b);
	CHECK(ret == 0 || ret == 1, "ged_dbi_gobj_color returns 0 or 1 for known object");
    }
    ret = ged_dbi_gobj_color(gedp, "__none__", NULL, NULL, NULL);
    CHECK(ret == -1, "ged_dbi_gobj_color for non-existent name returns -1");

    /* ------------------------------------------------------------------ */
    /* SelectionSet helpers                                                */
    /* ------------------------------------------------------------------ */

    /* Initially nothing selected */
    ret = ged_selection_count(gedp, NULL);
    CHECK(ret == 0, "ged_selection_count is 0 on fresh open");

    /* Select a path */
    ret = ged_selection_select(gedp, NULL, "top1.c/reg1.r/sph1.s");
    CHECK(ret == 1, "ged_selection_select returns 1 on new selection");

    /* Selecting again returns 0 (no change) */
    ret = ged_selection_select(gedp, NULL, "top1.c/reg1.r/sph1.s");
    CHECK(ret == 0, "ged_selection_select returns 0 when already selected");

    /* Count should now be 1 */
    ret = ged_selection_count(gedp, NULL);
    CHECK(ret == 1, "ged_selection_count is 1 after one select");

    /* is_selected for that path */
    ret = ged_selection_is_selected(gedp, NULL, "top1.c/reg1.r/sph1.s");
    CHECK(ret != 0, "ged_selection_is_selected returns non-zero for selected path");

    /* is_selected for a different path */
    ret = ged_selection_is_selected(gedp, NULL, "top1.c");
    CHECK(ret == 0, "ged_selection_is_selected returns 0 for non-selected path");

    /* list_paths */
    bu_ptbl_init(&paths_out, 8, "paths_out");
    ret = ged_selection_list_paths(gedp, NULL, &paths_out);
    CHECK(ret == 1, "ged_selection_list_paths returns 1 path");
    if (ret > 0) {
	const char *p = (const char *)BU_PTBL_GET(&paths_out, 0);
	printf("      selected path[0] = %s\n", p ? p : "(null)");
	CHECK(p != NULL, "selected path[0] is non-NULL");
    }
    /* free the bu_strdup'd strings */
    {
	size_t i;
	for (i = 0; i < BU_PTBL_LEN(&paths_out); i++) {
	    char *s = (char *)BU_PTBL_GET(&paths_out, i);
	    bu_free(s, "path_str");
	}
    }
    bu_ptbl_free(&paths_out);

    /* Deselect */
    ret = ged_selection_deselect(gedp, NULL, "top1.c/reg1.r/sph1.s");
    CHECK(ret == 1, "ged_selection_deselect returns 1 on successful deselect");

    ret = ged_selection_count(gedp, NULL);
    CHECK(ret == 0, "ged_selection_count is 0 after deselect");

    /* Deselect non-selected returns 0 */
    ret = ged_selection_deselect(gedp, NULL, "top1.c/reg1.r/sph1.s");
    CHECK(ret == 0, "ged_selection_deselect returns 0 for non-selected path");

    /* select two paths then clear */
    ged_selection_select(gedp, NULL, "top1.c");
    ged_selection_select(gedp, NULL, "reg1.r");
    ret = ged_selection_count(gedp, NULL);
    CHECK(ret == 2, "ged_selection_count is 2 after two selects");

    ged_selection_clear(gedp, NULL);
    ret = ged_selection_count(gedp, NULL);
    CHECK(ret == 0, "ged_selection_count is 0 after clear");

    /* Named selection set is independent of default set */
    ret = ged_selection_select(gedp, "my_set", "tor1.s");
    CHECK(ret == 1, "ged_selection_select with named set");

    ret = ged_selection_count(gedp, "my_set");
    CHECK(ret == 1, "ged_selection_count on named set is 1");

    ret = ged_selection_count(gedp, NULL);
    CHECK(ret == 0, "default selection set still empty after named-set select");

    ged_selection_clear(gedp, "my_set");
    ret = ged_selection_count(gedp, "my_set");
    CHECK(ret == 0, "ged_selection_count on named set is 0 after clear");

    /* NULL/edge-case safety */
    ret = ged_selection_select(gedp, NULL, NULL);
    CHECK(ret == -1, "ged_selection_select(NULL path) returns -1 safely");

    ret = ged_selection_count(NULL, NULL);
    CHECK(ret == -1, "ged_selection_count(NULL gedp) returns -1 safely");

    /* ------------------------------------------------------------------ */
    ged_close(gedp);
    bu_file_delete(tmpfile);

    if (failures) {
	fprintf(stderr, "\n%d check(s) FAILED\n", failures);
	return 3;
    }
    printf("\nAll checks passed.\n");
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
