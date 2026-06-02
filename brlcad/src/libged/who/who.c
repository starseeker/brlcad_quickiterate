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

    int list_real = 1;
    int list_overlays = 0;
    if (argc == 2) {
	switch (argv[1][0]) {
	    case 'b':
		list_real = 1;
		list_overlays = 1;
		break;
	    case 'p':
		list_real = 0;
		list_overlays = 1;
		break;
	    case 'r':
		list_real = 1;
		list_overlays = 0;
		break;
	    default:
		bu_vls_printf(gedp->ged_result_str, "%s", usage);
		return BRLCAD_ERROR;
	}
    }

    struct bsg_node *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
	return BRLCAD_OK;

    struct bu_ptbl groups = BU_PTBL_INIT_ZERO;
    if (list_real && !list_overlays) {
	bsg_draw_intent_collect_for_export(root, &groups, 0ULL);
    } else {
	bsg_collect_draw_groups(root, &groups, 1 /* include overlays */);
    }

    for (size_t i = 0; i < BU_PTBL_LEN(&groups); i++) {
	struct bsg_node *group = (struct bsg_node *)BU_PTBL_GET(&groups, i);
	const struct bsg_draw_intent *di = bsg_node_get_draw_intent(group);
	if (!di)
	    continue;
	const int is_overlay = bsg_draw_intent_is_overlay(di);
	if ((is_overlay && !list_overlays) || (!is_overlay && !list_real))
	    continue;
	const char *path = bsg_draw_intent_path(di);
	if (!path || !*path)
	    continue;
	bu_vls_printf(gedp->ged_result_str, "%s ", path);
    }
    bu_ptbl_free(&groups);

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
