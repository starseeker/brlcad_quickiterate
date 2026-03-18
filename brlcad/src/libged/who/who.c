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
#include "ged.h"
#include "bsg/util.h"

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

    /* Phase 2e: enumerate unique top-level objects from scene-root */
    bsg_shape *_root = gedp->ged_gvp ? bsg_scene_root_get(gedp->ged_gvp) : NULL;
    if (_root) {
	struct bu_ptbl _tops;
	bu_ptbl_init(&_tops, 8, "who_tops");
	for (size_t _i = 0; _i < BU_PTBL_LEN(&_root->children); _i++) {
	    bsg_shape *_sp = (bsg_shape *)BU_PTBL_GET(&_root->children, _i);
	    if (!_sp || !_sp->s_u_data) continue;
	    struct ged_bv_data *_bd = (struct ged_bv_data *)_sp->s_u_data;
	    if (!_bd->s_fullpath.fp_len) continue;
	    struct directory *_dp = _bd->s_fullpath.fp_names[0];
	    if (_dp->d_addr == RT_DIR_PHONY_ADDR) {
		if (skip_phony) continue;
	    } else {
		if (skip_real) continue;
	    }
	    bu_ptbl_ins_unique(&_tops, (long *)_dp);
	}
	for (size_t _ti = 0; _ti < BU_PTBL_LEN(&_tops); _ti++) {
	    struct directory *_dp = (struct directory *)BU_PTBL_GET(&_tops, _ti);
	    bu_vls_printf(gedp->ged_result_str, "%s ", _dp->d_namep);
	}
	bu_ptbl_free(&_tops);
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
