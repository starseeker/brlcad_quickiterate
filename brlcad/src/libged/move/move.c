/*                         M O V E . C
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
/** @file libged/move.c
 *
 * The move command.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/cmd.h"
#include "bu/str.h"
#include "ged/bsg_ged_draw.h"

#include "../ged_private.h"


/* Callback data for renaming group paths */
struct move_rename_data {
    struct ged *gedp;
    const char *old_name;
    const char *new_name;
};

static int
move_rename_group_cb(struct bsg_node *group, void *userdata)
{
    struct move_rename_data *data = (struct move_rename_data *)userdata;
    const char *path = bsg_view_obj_group_path(group);
    if (!path)
	return 1; /* continue */

    int first = 1;
    int found = 0;
    struct bu_vls new_path = BU_VLS_INIT_ZERO;
    char *dupstr = bu_strdup(path);
    char *tok = strtok(dupstr, "/");

    while (tok) {
	if (first) {
	    first = 0;
	    if (BU_STR_EQUAL(tok, data->old_name)) {
		found = 1;
		bu_vls_printf(&new_path, "%s", data->new_name);
	    } else {
		/* no match on first element - no need to go further */
		break;
	    }
	} else {
	    bu_vls_printf(&new_path, "/%s", tok);
	}
	tok = strtok((char *)NULL, "/");
    }

    if (found) {
	struct db_full_path dfp;
	db_full_path_init(&dfp);
	if (db_string_to_path(&dfp, data->gedp->dbip, bu_vls_cstr(&new_path)) == 0)
	    bsg_view_obj_group_set_dbpath(group, &dfp);
	db_free_full_path(&dfp);
    }

    free((void *)dupstr);
    bu_vls_free(&new_path);
    return 1; /* continue */
}


int
ged_move_core(struct ged *gedp, int argc, const char *argv[])
{
    struct directory *dp;
    struct rt_db_internal intern;
    static const char *usage = "from to";

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return GED_HELP;
    }

    if (argc != 3) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return BRLCAD_ERROR;
    }

    if ((dp = db_lookup(gedp->dbip,  argv[1], LOOKUP_NOISY)) == RT_DIR_NULL)
	return BRLCAD_ERROR;

    if (db_lookup(gedp->dbip, argv[2], LOOKUP_QUIET) != RT_DIR_NULL) {
	bu_vls_printf(gedp->ged_result_str, "%s: already exists", argv[2]);
	return BRLCAD_ERROR;
    }

    if (rt_db_get_internal(&intern, dp, gedp->dbip, (fastf_t *)NULL) < 0) {
	bu_vls_printf(gedp->ged_result_str, "Database read error, aborting");
	return BRLCAD_ERROR;
    }

    /* Change object name in the in-memory directory. */
    if (db_rename(gedp->dbip, dp, argv[2]) < 0) {
	rt_db_free_internal(&intern);
	bu_vls_printf(gedp->ged_result_str, "error in db_rename to %s, aborting", argv[2]);
	return BRLCAD_ERROR;
    }

    /* Re-write to the database.  New name is applied on the way out. */
    if (rt_db_put_internal(dp, gedp->dbip, &intern) < 0) {
	bu_vls_printf(gedp->ged_result_str, "Database write error, aborting");
	return BRLCAD_ERROR;
    }

    /* Change object name if it matches the first element in the display list path. */
    struct move_rename_data data = { gedp, argv[1], argv[2] };
    bsg_view_obj_foreach_group(gedp, move_rename_group_cb, &data);

    return BRLCAD_OK;
}


#include "../include/plugin.h"

#define GED_MOVE_COMMANDS(X, XID) \
    X(move, ged_move_core, GED_CMD_DEFAULT) \
    X(mv, ged_move_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_MOVE_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_move", 1, GED_MOVE_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
