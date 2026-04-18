/*                   C H E C K _ B B O X . C
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
/**
 * @file check_bbox.c
 *
 * "check bbox" subcommand – reports the axis-aligned bounding box of the
 * specified objects without firing any rays.
 *
 * This closes the one feature gap between 'check' and 'gqa': gqa has
 * always supported '-A b' (bounding box), which check lacked.  Unlike
 * the other check subcommands, this one does not call perform_raytracing;
 * it delegates to analyze_bbox() which only runs rt_prep.
 */

#include "common.h"

#include "vmath.h"
#include "ged.h"
#include "analyze.h"

#include "../ged_private.h"
#include "./check_private.h"


int
check_bbox(struct ged *gedp, struct current_state *state,
	   struct db_i *dbip,
	   char **tobjtab,
	   int tnobjs,
	   struct check_parameters *options)
{
    int i;
    point_t bbox_min, bbox_max;
    vect_t span;
    double lv = options->units[LINE]->val;
    double lv2 = lv * lv;

    /* state is not used for bbox (no raytracing needed), but keep the
     * function signature consistent with all other check subcommands */
    (void)state;

    if (analyze_bbox(dbip, tobjtab, tnobjs, bbox_min, bbox_max) < 0) {
	bu_vls_printf(gedp->ged_result_str,
		      "check bbox: error computing bounding box\n");
	return BRLCAD_ERROR;
    }

    VSUB2(span, bbox_max, bbox_min);

    bu_vls_printf(gedp->ged_result_str, "Bounding Box:\n");
    bu_vls_printf(gedp->ged_result_str,
		  "\t  min: (%g, %g, %g) %s\n",
		  bbox_min[X] / lv, bbox_min[Y] / lv, bbox_min[Z] / lv,
		  options->units[LINE]->name);
    bu_vls_printf(gedp->ged_result_str,
		  "\t  max: (%g, %g, %g) %s\n",
		  bbox_max[X] / lv, bbox_max[Y] / lv, bbox_max[Z] / lv,
		  options->units[LINE]->name);
    bu_vls_printf(gedp->ged_result_str,
		  "\t span: (%g, %g, %g) %s\n",
		  span[X] / lv, span[Y] / lv, span[Z] / lv,
		  options->units[LINE]->name);
    bu_vls_printf(gedp->ged_result_str,
		  "\t face areas (YZ, XZ, XY): %g, %g, %g %s^2\n",
		  span[Y] * span[Z] / lv2,
		  span[X] * span[Z] / lv2,
		  span[X] * span[Y] / lv2,
		  options->units[LINE]->name);

    if (options->print_per_region_stats) {
	bu_vls_printf(gedp->ged_result_str, "\nPer-object bounding boxes:\n");
	for (i = 0; i < tnobjs; i++) {
	    point_t omin, omax;
	    vect_t ospan;
	    char *single[2];
	    single[0] = tobjtab[i];
	    single[1] = NULL;
	    if (analyze_bbox(dbip, single, 1, omin, omax) == 0) {
		VSUB2(ospan, omax, omin);
		bu_vls_printf(gedp->ged_result_str,
			      "\t%s: min(%g,%g,%g) max(%g,%g,%g) span(%g,%g,%g) %s\n",
			      tobjtab[i],
			      omin[X]/lv, omin[Y]/lv, omin[Z]/lv,
			      omax[X]/lv, omax[Y]/lv, omax[Z]/lv,
			      ospan[X]/lv, ospan[Y]/lv, ospan[Z]/lv,
			      options->units[LINE]->name);
	    }
	}
    }

    return BRLCAD_OK;
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
