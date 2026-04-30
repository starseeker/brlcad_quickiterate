/*                B S G _ V I E W _ O B J . C
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
/** @file libged/bsg_view_obj.c
 *
 * Phase 6.5 (drawing-stack modernization) — Step 1.
 *
 * Implementation of the BSG view-object query API declared in
 * include/ged/bsg_view_obj.h.  These nine helpers form the migration
 * target for libged callers (and out-of-tree consumers) that currently
 * walk the legacy gedp->i->ged_gdp->gd_headDisplay chain via the
 * dl_* / _dl_* / invent_solid functions in display_list.c.
 *
 * The current implementation deliberately delegates to the legacy
 * dl_* routines.  This lets caller migration (Step 2 of the deletion
 * plan) proceed independently against a stable signature surface,
 * without any behavior change.  Once every caller has been migrated,
 * Step 7 of the plan will replace the bodies below with a pure BSG
 * view-tree walk and delete display_list.c.
 *
 * See doc/notes/drawing_stack_modernization.txt Phase 6.5 for the
 * staged plan and see brlcad/include/ged/bsg_view_obj.h for the API
 * contract each helper presents to callers.
 */

#include "common.h"

#include "ged.h"
#include "ged/bsg_view_obj.h"
#include "./ged_private.h"


void *
bsg_view_obj_lookup_or_add_path(struct ged *gedp, const char *path)
{
    if (!gedp || !path)
	return NULL;
    return (void *)dl_addToDisplay(gedp->i->ged_gdp->gd_headDisplay,
				   gedp->dbip, path);
}


void
bsg_view_obj_erase_by_path(struct ged *gedp, const char *path,
			   int allow_split)
{
    if (!gedp || !path)
	return;
    dl_erasePathFromDisplay(gedp, path, allow_split);
}


void
bsg_view_obj_erase_by_name(struct ged *gedp, const char *name,
			   int skip_first)
{
    if (!gedp || !name)
	return;
    _dl_eraseAllNamesFromDisplay(gedp, name, skip_first);
}


void
bsg_view_obj_erase_all_paths(struct ged *gedp, const char *path,
			     int skip_first)
{
    if (!gedp || !path)
	return;
    _dl_eraseAllPathsFromDisplay(gedp, path, skip_first);
}


int
bsg_view_obj_bounds(struct ged *gedp, vect_t *min, vect_t *max,
		    int pflag)
{
    if (!gedp || !min || !max)
	return 1;
    return dl_bounding_sph(gedp->i->ged_gdp->gd_headDisplay,
			   min, max, pflag);
}


void
bsg_view_obj_set_iflag(struct ged *gedp, int iflag)
{
    if (!gedp)
	return;
    dl_set_iflag(gedp->i->ged_gdp->gd_headDisplay, iflag);
}


void
bsg_view_obj_color_from_soltab(struct ged *gedp)
{
    if (!gedp)
	return;
    dl_color_soltab((struct bu_list *)ged_dl(gedp), gedp->dbip);
}


int
bsg_view_obj_invent(struct ged *gedp, char *name, struct bu_list *vhead,
		    long int rgb, int copy, fastf_t transparency,
		    int dmode, int csoltab)
{
    if (!gedp || !name || !vhead)
	return -1;
    return invent_solid(gedp, name, vhead, rgb, copy, transparency,
			dmode, csoltab);
}


unsigned long long
bsg_view_obj_name_hash(struct ged *gedp)
{
    if (!gedp)
	return 0;
    return dl_name_hash(gedp);
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
