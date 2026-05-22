/*                         S E T _ T R A N S P A R E N C Y . C
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
/** @file libged/set_transparency.c
 *
 * The set_transparency command.
 *
 */

#include "common.h"

#include "bsg/material.h"
#include "bsg/node.h"
#include "ged/bsg_ged_draw.h"
#include "../ged_private.h"

/* Callback data for set_transparency */
struct set_transparency_data {
    struct ged *gedp;
    struct directory **dpp;
    double transparency;
};

static int
set_transparency_cb(struct bv_scene_obj *sp, void *userdata)
{
    struct set_transparency_data *data = (struct set_transparency_data *)userdata;
    size_t i;
    struct directory **tmp_dpp;

    if (!bsg_node_u3_get((const bsg_node *)sp))
	return 1; /* continue */

    struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_u3_get((const bsg_node *)sp);

    for (i = 0, tmp_dpp = data->dpp;
	 i < bdata->s_fullpath.fp_len && *tmp_dpp != RT_DIR_NULL;
	 ++i, ++tmp_dpp) {
	if (bdata->s_fullpath.fp_names[i] != *tmp_dpp)
	    break;
    }

    if (*tmp_dpp != RT_DIR_NULL)
	return 1; /* continue */

    /* found a match */
    struct bsg_material mat;
    bsg_material_init(&mat);
    (void)bsg_node_material_get((const bsg_node *)sp, &mat);
    mat.transparency = (fastf_t)data->transparency;
    mat.rgba[3] = bsg_material_alpha_from_transparency(mat.transparency);
    bsg_node_material_set((bsg_node *)sp, &mat);

    return 1; /* continue */
}

void
dl_set_transparency(struct ged *gedp, struct directory **dpp, double transparency)
{
    struct set_transparency_data data;
    data.gedp = gedp;
    data.dpp = dpp;
    data.transparency = transparency;
    bsg_view_obj_foreach_solid(gedp, set_transparency_cb, &data);
}


/*
 * Set the transparency of the specified object
 *
 * Usage:
 * set_transparency obj tr
 *
 */
int
ged_set_transparency_core(struct ged *gedp, int argc, const char *argv[])
{
    struct directory **dpp;

    /* intentionally double for scan */
    double transparency;

    static const char *usage = "node tval";

    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return BRLCAD_ERROR;
    }


    if (argc != 3) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return BRLCAD_ERROR;
    }

    if (sscanf(argv[2], "%lf", &transparency) != 1) {
	bu_vls_printf(gedp->ged_result_str, "dgo_set_transparency: bad transparency - %s\n", argv[2]);
	return BRLCAD_ERROR;
    }

    if ((dpp = _ged_build_dpp(gedp, argv[1])) == NULL) {
	return BRLCAD_OK;
    }

    dl_set_transparency(gedp, dpp, transparency);

    if (dpp != (struct directory **)NULL)
	bu_free((void *)dpp, "ged_set_transparency_core: directory pointers");

    return BRLCAD_OK;
}


#include "../include/plugin.h"

#define GED_SET_TRANSPARENCY_COMMANDS(X, XID) \
    X(set_transparency, ged_set_transparency_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_SET_TRANSPARENCY_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_set_transparency", 1, GED_SET_TRANSPARENCY_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
