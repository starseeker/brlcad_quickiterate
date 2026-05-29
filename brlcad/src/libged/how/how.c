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
#include "bsg/draw_intent.h"
#include "dm.h"
#include "ged/bsg_ged_draw.h"
#include "../ged_private.h"

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
    const char *obj_arg = NULL;

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

    obj_arg = both ? argv[2] : argv[1];
    struct bsg_node *root = (gedp->i && gedp->i->ged_gdp) ? gedp->i->ged_gdp->gd_draw_root : NULL;
    if (!root)
	goto not_found;

    struct bu_ptbl groups = BU_PTBL_INIT_ZERO;
    bsg_draw_intent_match(root, obj_arg, &groups);
    if (BU_PTBL_LEN(&groups) == 0) {
	struct bu_vls prefix = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&prefix, "%s/*", obj_arg);
	bsg_draw_intent_match(root, bu_vls_cstr(&prefix), &groups);
	bu_vls_free(&prefix);
    }

    struct bsg_node *group = (BU_PTBL_LEN(&groups) > 0) ? (struct bsg_node *)BU_PTBL_GET(&groups, 0) : NULL;
    bu_ptbl_free(&groups);
    if (!group)
	goto not_found;

    int dmode = bsg_view_obj_group_dmode(group);
    struct bsg_node *sp = bsg_view_obj_group_first_solid(group);
    fastf_t transparency = (sp && sp->s_os) ? sp->s_os->transparency : 0.0;
    if (dmode == _GED_HIDDEN_LINE) {
	if (both)
	    bu_vls_printf(gedp->ged_result_str, "%d 1", _GED_HIDDEN_LINE);
	else
	    bu_vls_printf(gedp->ged_result_str, "%d", _GED_HIDDEN_LINE);
    } else {
	if (both)
	    bu_vls_printf(gedp->ged_result_str, "%d %g", dmode, transparency);
	else
	    bu_vls_printf(gedp->ged_result_str, "%d", dmode);
    }
    return BRLCAD_OK;

not_found:
    bu_vls_printf(gedp->ged_result_str, "-1");

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
