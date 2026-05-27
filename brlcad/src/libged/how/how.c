/*                         H O W . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2026 United States Government as represented by
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
/** @file libged/how.c
 *
 * The how command.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/cmd.h"
#include "bu/str.h"
#include "dm.h"
#include "ged/bsg_ged_draw.h"
#include "../ged_private.h"

/* Callback data for how command */
struct how_data {
    struct ged *gedp;
    struct bu_vls *vls;
    struct db_full_path *dfp;
    int both;
    int found;
};

static int
how_group_cb(struct bsg_node *group, void *userdata)
{
    struct how_data *data = (struct how_data *)userdata;
    if (data->found)
	return 0; /* stop - already found */

    struct db_full_path gpath;
    db_full_path_init(&gpath);
    if (bsg_view_obj_group_dbpath(data->gedp, group, &gpath) != 0)
	return 1; /* continue */

    int match = db_full_path_match_top(&gpath, data->dfp);
    db_free_full_path(&gpath);
    if (!match)
	return 1; /* continue */

    /* found a match */
    data->found = 1;
    int dmode = bsg_view_obj_group_dmode(group);
    struct bsg_node *sp = bsg_view_obj_group_first_solid(group);
    fastf_t transparency = (sp && sp->s_os) ? sp->s_os->transparency : 0.0;
    if (dmode == _GED_HIDDEN_LINE) {
	if (data->both)
	    bu_vls_printf(data->vls, "%d 1", _GED_HIDDEN_LINE);
	else
	    bu_vls_printf(data->vls, "%d", _GED_HIDDEN_LINE);
    } else {
	if (data->both)
	    bu_vls_printf(data->vls, "%d %g", dmode, transparency);
	else
	    bu_vls_printf(data->vls, "%d", dmode);
    }

    return 0; /* stop iteration */
}


/*
 * Returns "how" an object is being displayed.
 *
 * Usage:
 * how [-b] object
 *
 */
int
ged_how_core(struct ged *gedp, int argc, const char *argv[])
{
    int both = 0;
    static const char *usage = "[-b] object";
    struct db_full_path dfp;
    int have_path = 0;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return GED_HELP;
    }

    if (3 < argc) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return BRLCAD_ERROR;
    }

    if (argc == 3 &&
	argv[1][0] == '-' &&
	argv[1][1] == 'b') {
	both = 1;
    } else {
	if (argc != 2) {
	    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	    return BRLCAD_ERROR;
	}
    }

    db_full_path_init(&dfp);
    if (db_string_to_path(&dfp, gedp->dbip, both ? argv[2] : argv[1]) != 0)
	goto good_label;
    have_path = 1;

    struct how_data data;
    data.gedp = gedp;
    data.vls = gedp->ged_result_str;
    data.dfp = &dfp;
    data.both = both;
    data.found = 0;
    bsg_view_obj_foreach_group(gedp, how_group_cb, &data);

    /* match NOT found */
    if (!data.found) bu_vls_printf(gedp->ged_result_str, "-1");

good_label:
    if (have_path)
	db_free_full_path(&dfp);

    return BRLCAD_OK;
}

#include "../include/plugin.h"

#define GED_HOW_COMMANDS(X, XID) \
    X(how, ged_how_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_HOW_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_how", 1, GED_HOW_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
