/*                   T E S S _ T I M I N G . C
 * BRL-CAD
 *
 * Copyright (c) 2024-2025 United States Government as represented by
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
/**
 * Tessellation timing utility - iterates over all solid primitives in
 * a .g database, tessellates each one, and reports the time taken per
 * shape.  Shapes taking longer than a configurable threshold are
 * highlighted.  BREP primitives are skipped (known issues).
 *
 * Usage: tess_timing [-t threshold_seconds] <database.g> [object ...]
 *   -t  report shapes slower than this many seconds (default: 0.5)
 *
 * If object names are provided only those objects are tessellated;
 * otherwise every solid in the database is processed.
 */

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bu/app.h"
#include "bu/getopt.h"
#include "bu/malloc.h"
#include "bu/time.h"
#include "bg/defines.h"
#include "bn/tol.h"
#include "nmg.h"
#include "raytrace.h"


/* Slow-shape threshold in seconds (overridable with -t) */
static double slow_threshold = 0.5;


/*
 * Tessellate one solid and report timing.  Returns 1 if the shape was
 * slow (took >= slow_threshold seconds), 0 otherwise, -1 on skip/error.
 */
static int
tess_one(struct db_i *dbip, struct directory *dp,
	 const struct bg_tess_tol *ttol, const struct bn_tol *tol)
{
    struct rt_db_internal intern;
    struct model *m;
    struct nmgregion *r = NULL;
    int64_t start;
    double elapsed;
    int ret = 0;

    RT_CK_DBI(dbip);
    RT_CK_DIR(dp);

    /* Only process solids */
    if (!(dp->d_flags & RT_DIR_SOLID))
	return -1;

    if (rt_db_get_internal(&intern, dp, dbip, NULL, &rt_uniresource) < 0) {
	bu_log("tess_timing: rt_db_get_internal failed for %s\n", dp->d_namep);
	return -1;
    }

    /* Skip BREP - known issues */
    if (intern.idb_minor_type == ID_BREP) {
	rt_db_free_internal(&intern);
	return -1;
    }

    /* Skip types with no tessellation function */
    if (intern.idb_minor_type < 0
	|| !OBJ[intern.idb_minor_type].ft_tessellate) {
	rt_db_free_internal(&intern);
	return -1;
    }

    m = nmg_mm();

    start = bu_gettime();
    ret = OBJ[intern.idb_minor_type].ft_tessellate(&r, m, &intern, ttol, tol);
    elapsed = (double)(bu_gettime() - start) / 1e6;

    if (ret == 0) {
	/* success */
	if (elapsed >= slow_threshold) {
	    printf("SLOW  %8.4fs  %s  (%s)\n",
		   elapsed,
		   dp->d_namep,
		   OBJ[intern.idb_minor_type].ft_name);
	    ret = 1;
	} else {
	    printf("ok    %8.4fs  %s  (%s)\n",
		   elapsed,
		   dp->d_namep,
		   OBJ[intern.idb_minor_type].ft_name);
	    ret = 0;
	}
    } else {
	printf("FAIL  %8.4fs  %s  (%s)\n",
	       elapsed,
	       dp->d_namep,
	       OBJ[intern.idb_minor_type].ft_name);
	ret = -1;
    }

    nmg_km(m);
    rt_db_free_internal(&intern);
    return ret;
}


static void
print_usage(const char *progname)
{
    bu_log("Usage: %s [-t threshold] <database.g> [object ...]\n", progname);
    bu_log("  -t threshold   seconds; shapes slower than this are flagged (default %.1f)\n",
	   slow_threshold);
    bu_log("\nTessellates every solid in the database (or only the named\n");
    bu_log("objects) and reports elapsed time per shape.  BREP primitives\n");
    bu_log("are skipped.  Shapes tagged SLOW took >= the threshold.\n");
}


int
main(int argc, char *argv[])
{
    struct db_i *dbip;
    struct bg_tess_tol ttol = BG_TESS_TOL_INIT_TOL;
    struct bn_tol tol = BN_TOL_INIT_TOL;
    int c;
    int slow_count = 0;
    int ok_count = 0;
    int fail_count = 0;
    int skip_count = 0;

    bu_setprogname(argv[0]);

    while ((c = bu_getopt(argc, argv, "t:h?")) != -1) {
	switch (c) {
	    case 't':
		slow_threshold = atof(bu_optarg);
		break;
	    case 'h':
	    case '?':
	    default:
		print_usage(argv[0]);
		return 0;
	}
    }

    argc -= bu_optind;
    argv += bu_optind;

    if (argc < 1) {
	print_usage(argv[-bu_optind]);
	return 1;
    }

    dbip = db_open(argv[0], DB_OPEN_READONLY);
    if (dbip == DBI_NULL) {
	bu_log("tess_timing: cannot open %s\n", argv[0]);
	return 1;
    }
    if (db_dirbuild(dbip) < 0) {
	bu_log("tess_timing: db_dirbuild failed for %s\n", argv[0]);
	db_close(dbip);
	return 1;
    }

    printf("Tessellation timing for: %s\n", argv[0]);
    printf("Threshold: %.4f seconds\n\n", slow_threshold);

    if (argc > 1) {
	/* Process only named objects */
	int i;
	for (i = 1; i < argc; i++) {
	    struct directory *dp = db_lookup(dbip, argv[i], LOOKUP_QUIET);
	    if (dp == RT_DIR_NULL) {
		bu_log("tess_timing: object not found: %s\n", argv[i]);
		skip_count++;
		continue;
	    }
	    c = tess_one(dbip, dp, &ttol, &tol);
	    if (c > 0) slow_count++;
	    else if (c == 0) ok_count++;
	    else if (c < 0) {
		/* Check if it was actually a solid we just skipped */
		if (dp->d_flags & RT_DIR_SOLID) {
		    skip_count++;
		} else {
		    skip_count++;
		}
	    }
	}
    } else {
	/* Process all solids in the database */
	struct directory *dp;
	FOR_ALL_DIRECTORY_START(dp, dbip)
	    if (!(dp->d_flags & RT_DIR_SOLID))
		continue;
	    c = tess_one(dbip, dp, &ttol, &tol);
	    if (c > 0) slow_count++;
	    else if (c == 0) ok_count++;
	    else skip_count++;
	FOR_ALL_DIRECTORY_END
    }

    printf("\n--- Summary ---\n");
    printf("  ok:     %d\n", ok_count);
    printf("  slow:   %d  (>= %.4fs)\n", slow_count, slow_threshold);
    printf("  fail:   %d\n", fail_count);
    printf("  skip:   %d  (brep or no tess func)\n", skip_count);

    db_close(dbip);
    return (slow_count > 0) ? 2 : 0;
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
