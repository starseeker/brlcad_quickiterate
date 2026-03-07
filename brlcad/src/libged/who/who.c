/*                         W H O . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2025 United States Government as represented by
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

#include "common.h"

#include "bu/ptbl.h"
#include "bsg/util.h"
#include "ged.h"
#include "../ged_private.h"

extern int ged_who2_core(struct ged *gedp, int argc, const char **argv);

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

    struct display_list *gdlp;
    int skip_real, skip_phony;
    static const char *usage = "[r(eal)|p(hony)|b(oth)]";

    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    if (2 < argc) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return BRLCAD_ERROR;
    }

    skip_real = 0;
    skip_phony = 1;
    if (argc == 2) {
	switch (argv[1][0]) {
	    case 'b':
		skip_real = 0;
		skip_phony = 0;
		break;
	    case 'p':
		skip_real = 1;
		skip_phony = 0;
		break;
	    case 'r':
		skip_real = 0;
		skip_phony = 1;
		break;
	    default:
		bu_vls_printf(gedp->ged_result_str, "ged_who_core: argument not understood\n");
		return BRLCAD_ERROR;
	}
    }

    /* Phase 2e: use scene-root children as the canonical drawn-objects source.
     * If gd_headDisplay has entries, use them (legacy tracking is still maintained
     * by draw.c, so this gives the correct multi-level-path output).  If it is
     * empty, fall back to enumerating unique top-level directory entries from the
     * scene-root children across all views. */
    if (BU_LIST_IS_EMPTY((struct bu_list *)ged_dl(gedp))) {
	/* Scene-root fallback: collect unique top-level directory entries */
	struct bu_ptbl unique_dirs;
	bu_ptbl_init(&unique_dirs, 8, "who unique_dirs");

	struct bu_ptbl *views = bsg_scene_views(&gedp->ged_views);
	if (views) {
	    for (size_t vi = 0; vi < BU_PTBL_LEN(views); vi++) {
		bsg_view *v = (bsg_view *)BU_PTBL_GET(views, vi);
		bsg_shape *root = bsg_scene_root_get(v);
		if (!root) continue;
		for (size_t si = 0; si < BU_PTBL_LEN(&root->children); si++) {
		    bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, si);
		    if (!sp || !sp->s_u_data) continue;
		    struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
		    if (!bdata->s_fullpath.fp_len) continue;
		    struct directory *dp = bdata->s_fullpath.fp_names[0];
		    if (dp->d_addr == RT_DIR_PHONY_ADDR) {
			if (skip_phony) continue;
		    } else {
			if (skip_real) continue;
		    }
		    bu_ptbl_ins_unique(&unique_dirs, (long *)dp);
		}
	    }
	}

	for (size_t i = 0; i < BU_PTBL_LEN(&unique_dirs); i++) {
	    struct directory *dp = (struct directory *)BU_PTBL_GET(&unique_dirs, i);
	    bu_vls_printf(gedp->ged_result_str, "%s ", dp->d_namep);
	}
	bu_ptbl_free(&unique_dirs);
    } else {
	for (BU_LIST_FOR(gdlp, display_list, (struct bu_list *)ged_dl(gedp))) {
	    if (((struct directory *)gdlp->dl_dp)->d_addr == RT_DIR_PHONY_ADDR) {
		if (skip_phony) continue;
	    } else {
		if (skip_real) continue;
	    }

	    bu_vls_printf(gedp->ged_result_str, "%s ", bu_vls_addr(&gdlp->dl_path));
	}
    }

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
