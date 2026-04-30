/*                         W H O . C
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
/** @file libged/who.c
 *
 * The who command.
 *
 */

#include <string.h>
#include "ged.h"
#include "ged/bsg_view_obj.h"
#include "../ged_private.h"

extern int ged_who2_core(struct ged *gedp, int argc, const char **argv);

/* Callback data for who command */
struct who_data {
    struct ged *gedp;
    int skip_real;
    int skip_phony;
};

static int
who_group_cb(void *group_handle, void *userdata)
{
    struct who_data *data = (struct who_data *)userdata;
    const char *path = bsg_view_obj_group_path(group_handle);
    if (!path)
	return 1; /* continue */

    /* Get the directory entry for this group */
    /* We need to check if it's a phony or real entry */
    /* The dl_dp field would have this info, but we need to look it up */
    char *name = strrchr(path, '/');
    if (!name)
	name = (char *)path;
    else
	name++;

    struct directory *dp = db_lookup(data->gedp->dbip, name, LOOKUP_QUIET);
    if (dp != RT_DIR_NULL) {
	if (dp->d_addr == RT_DIR_PHONY_ADDR) {
	    if (data->skip_phony) return 1; /* continue */
	} else {
	    if (data->skip_real) return 1; /* continue */
	}
    }

    bu_vls_printf(data->gedp->ged_result_str, "%s ", path);
    return 1; /* continue */
}

/*
 * List the objects currently prepped for drawing
 *
 * Usage:
 * who [r(eal)|p(hony)|b(oth)]
 *
 */
int
ged_who_core(struct ged *gedp, int argc, const char *argv[])
{
    if (gedp->new_cmd_forms)
	return ged_who2_core(gedp, argc, argv);

    struct who_data data;
    static const char *usage = "[r(eal)|p(hony)|b(oth)]";

    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    if (2 < argc) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return BRLCAD_ERROR;
    }

    data.gedp = gedp;
    data.skip_real = 0;
    data.skip_phony = 1;
    if (argc == 2) {
	switch (argv[1][0]) {
	    case 'b':
		data.skip_real = 0;
		data.skip_phony = 0;
		break;
	    case 'p':
		data.skip_real = 1;
		data.skip_phony = 0;
		break;
	    case 'r':
		data.skip_real = 0;
		data.skip_phony = 1;
		break;
	    default:
		bu_vls_printf(gedp->ged_result_str, "ged_who_core: argument not understood\n");
		return BRLCAD_ERROR;
	}
    }

    bsg_view_obj_foreach_group(gedp, who_group_cb, &data);

    return BRLCAD_OK;
}


#include "../include/plugin.h"

#define GED_WHO_COMMANDS(X, XID) \
    X(who, ged_who_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_WHO_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_who", 1, GED_WHO_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
