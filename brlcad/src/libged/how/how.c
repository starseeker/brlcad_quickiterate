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
    struct bu_vls *vls;
    struct directory **dpp;
    int both;
    int found;
};

static int
how_solid_cb(struct bsg_node *sp, void *userdata)
{
    struct how_data *data = (struct how_data *)userdata;
    if (data->found)
	return 0; /* stop - already found */

    if (!sp->s_u_data)
	return 1; /* continue */
    struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;

    size_t i;
    struct directory **tmp_dpp;
    for (i = 0, tmp_dpp = data->dpp;
	 i < bdata->s_fullpath.fp_len && *tmp_dpp != RT_DIR_NULL;
	 ++i, ++tmp_dpp) {
	if (bdata->s_fullpath.fp_names[i] != *tmp_dpp)
	    break;
    }

    if (*tmp_dpp != RT_DIR_NULL)
	return 1; /* continue */

    /* found a match */
    data->found = 1;
    if (sp->s_os->s_dmode == 4) {
	if (data->both)
	    bu_vls_printf(data->vls, "%d 1", _GED_HIDDEN_LINE);
	else
	    bu_vls_printf(data->vls, "%d", _GED_HIDDEN_LINE);
    } else {
	if (data->both)
	    bu_vls_printf(data->vls, "%d %g", sp->s_os->s_dmode, sp->s_os->transparency);
	else
	    bu_vls_printf(data->vls, "%d", sp->s_os->s_dmode);
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
    struct directory **dpp;
    int both = 0;
    static const char *usage = "[-b] object";

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

	if ((dpp = _ged_build_dpp(gedp, argv[2])) == NULL)
	    goto good_label;
    } else {
	if ((dpp = _ged_build_dpp(gedp, argv[1])) == NULL)
	    goto good_label;
    }

    struct how_data data;
    data.vls = gedp->ged_result_str;
    data.dpp = dpp;
    data.both = both;
    data.found = 0;
    bsg_view_obj_foreach_solid(gedp, how_solid_cb, &data);

    /* match NOT found */
    if (!data.found) bu_vls_printf(gedp->ged_result_str, "-1");

good_label:
    if (dpp != (struct directory **)NULL)
	bu_free((void *)dpp, "ged_how_core: directory pointers");

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
