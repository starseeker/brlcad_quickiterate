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

#include "ged.h"
#include "ged/bsg_ged_draw.h"
#include "../ged_private.h"

extern int ged_who2_core(struct ged *gedp, int argc, const char **argv);
extern int ged_who_solids_core(struct ged *gedp, int argc, const char **argv);

/* Callback data for who command */
struct who_data {
    struct ged *gedp;
    int skip_real;
    int skip_overlays;
};

static int
who_group_cb(struct bv_scene_obj *group, void *userdata)
{
    struct who_data *data = (struct who_data *)userdata;

    /* Use the BSG overlay flag instead of a db_lookup + phony-addr check.
     * bsg_view_obj_group_is_phony returns 1 for the _overlays group and
     * any other synthetic overlay branch (R1 cleanup). */
    if (bsg_view_obj_group_is_phony(group)) {
	if (data->skip_overlays) return 1; /* continue: skip overlays */
    } else {
	if (data->skip_real) return 1; /* continue: skip real geometry */
    }

    const char *path = bsg_view_obj_group_path(group);
    if (!path)
	return 1; /* continue */

    bu_vls_printf(data->gedp->ged_result_str, "%s ", path);
    return 1; /* continue */
}

/*
 * List the objects currently prepped for drawing
 *
 * Usage:
 * who [r(eal)|p(hony)|b(oth)]
 * who solids [level]
 *
 */
int
ged_who_core(struct ged *gedp, int argc, const char *argv[])
{
    static const char *usage =
	"Usage:\n"
	"  who [real|phony|both]\n"
	"  who solids [level]";

    if (argc > 1 && (BU_STR_EQUAL(argv[1], "solids") || BU_STR_EQUAL(argv[1], "report")))
	return ged_who_solids_core(gedp, argc, argv);

    if (gedp->new_cmd_forms)
	return ged_who2_core(gedp, argc, argv);

    struct who_data data;

    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    if (argc == 2 && (BU_STR_EQUAL(argv[1], "-h") || BU_STR_EQUAL(argv[1], "--help") || BU_STR_EQUAL(argv[1], "-?"))) {
	bu_vls_printf(gedp->ged_result_str, "%s", usage);
	return GED_HELP;
    }

    if (2 < argc) {
	bu_vls_printf(gedp->ged_result_str, "%s", usage);
	return BRLCAD_ERROR;
    }

    data.gedp = gedp;
    data.skip_real = 0;
    data.skip_overlays = 1;
    if (argc == 2) {
	switch (argv[1][0]) {
	    case 'b':
		data.skip_real = 0;
		data.skip_overlays = 0;
		break;
	    case 'p':
		data.skip_real = 1;
		data.skip_overlays = 0;
		break;
	    case 'r':
		data.skip_real = 0;
		data.skip_overlays = 1;
		break;
	    default:
		bu_vls_printf(gedp->ged_result_str, "%s", usage);
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
