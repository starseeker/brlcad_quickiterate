/*                    S O L I D _ R E P O R T . C
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
/** @file libged/solid_report.c
 *
 * The solid_report command.
 *
 */

#include "common.h"

#include <stdlib.h>

#include "bsg/node.h"
#include "bsg/payload.h"
#include "bu/opt.h"
#include "ged.h"
#include "ged/bsg_ged_draw.h"
#include "../ged_private.h"

#define LAST_SOLID(_sp) DB_FULL_PATH_CUR_DIR(&(_sp)->s_fullpath)

/* Callback data for solid report */
struct solid_report_data {
    struct db_i *dbip;
    int lvl;
    int vlcmds;
    struct bu_vls *vls;
};

static int
solid_report_cb(struct bv_scene_obj *sp, void *userdata)
{
    struct solid_report_data *data = (struct solid_report_data *)userdata;
    struct bv_vlist *vp;
    struct bu_list *vhead = bsg_node_vlist_head((bsg_node *)sp);

    if (!data->vlcmds) {
	size_t nvlist;
	size_t npts;

	if (!bsg_node_u3_get((bsg_node *)sp))
	    return 1; /* continue */
	struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_u3_get((bsg_node *)sp);

	if (data->lvl <= -2) {
	    /* print only leaves */
	    if (bdata && LAST_SOLID(bdata))
		bu_vls_printf(data->vls, "%s ", LAST_SOLID(bdata)->d_namep);
	    return 1; /* continue */
	}

	db_path_to_vls(data->vls, &bdata->s_fullpath);

	if ((data->lvl != -1) && (sp->bsg.bsg_iflag == UP))
	    bu_vls_printf(data->vls, " ILLUM");

	bu_vls_printf(data->vls, "\n");

	if (data->lvl <= 0)
	    return 1; /* continue */

	/* convert to the local unit for printing */
	{
	    vect_t center;
	    bsg_node_center_get((const bsg_node *)sp, center);
	    bu_vls_printf(data->vls, "  cent=(%.3f, %.3f, %.3f) sz=%g ",
			  center[X]*data->dbip->dbi_base2local,
			  center[Y]*data->dbip->dbi_base2local,
			  center[Z]*data->dbip->dbi_base2local,
			  bsg_node_size_get((const bsg_node *)sp)*data->dbip->dbi_base2local);
	}
	bu_vls_printf(data->vls, "reg=%d\n", bsg_node_legacy_regionid((const bsg_node *)sp));
	{
	    unsigned char br, bg, bb;
	    unsigned char cr, cg, cb;
	    bsg_node_legacy_basecolor_get((const bsg_node *)sp, &br, &bg, &bb);
	    bsg_node_get_color((const bsg_node *)sp, &cr, &cg, &cb);
	    bu_vls_printf(data->vls, "  basecolor=(%d, %d, %d) color=(%d, %d, %d)%s%s%s\n",
			  br, bg, bb, cr, cg, cb,
			  bsg_node_legacy_uflag((const bsg_node *)sp)?" U":"",
			  bsg_node_legacy_dflag((const bsg_node *)sp)?" D":"",
			  bsg_node_legacy_cflag((const bsg_node *)sp)?" C":"");
	}

	if (data->lvl <= 1)
	    return 1; /* continue */

	/* Print the actual vector list */
	nvlist = 0;
	npts = 0;
	for (BU_LIST_FOR(vp, bv_vlist, vhead)) {
	    size_t i;
	    size_t nused = vp->nused;
	    int *cmd = vp->cmd;
	    point_t *pt = vp->pt;

	    BV_CK_VLIST(vp);
	    nvlist++;
	    npts += nused;

	    if (data->lvl <= 2)
		continue;

	    for (i = 0; i < nused; i++, cmd++, pt++) {
		bu_vls_printf(data->vls, "  %s (%g, %g, %g)\n",
			      bv_vlist_get_cmd_description(*cmd),
			      V3ARGS(*pt));
	    }
	}

	bu_vls_printf(data->vls, "  %zu vlist structures, %zu pts\n", nvlist, npts);
	bu_vls_printf(data->vls, "  %zu pts (via bv_ck_vlist)\n", bv_ck_vlist(vhead));
    } else {
	/* Print vlist cmds */
	{
	    unsigned char cr, cg, cb;
	    bsg_node_get_color((const bsg_node *)sp, &cr, &cg, &cb);
	    bu_vls_printf(data->vls, "-1 %d %d %d\n", cr, cg, cb);
	}

	/* Print the actual vector list */
	for (BU_LIST_FOR(vp, bv_vlist, vhead)) {
	    size_t i;
	    size_t nused = vp->nused;
	    int *cmd = vp->cmd;
	    point_t *pt = vp->pt;

	    BV_CK_VLIST(vp);

	    for (i = 0; i < nused; i++, cmd++, pt++)
		bu_vls_printf(data->vls, "%d %g %g %g\n", *cmd, V3ARGS(*pt));
	}
    }

    return 1; /* continue */
}

static void
dl_print_schain(struct ged *gedp, struct db_i *dbip, int lvl, int vlcmds, struct bu_vls *vls)
{
    struct solid_report_data data;

    if (dbip == DBI_NULL) return;

    data.dbip = dbip;
    data.lvl = lvl;
    data.vlcmds = vlcmds;
    data.vls = vls;
    bsg_view_obj_foreach_solid(gedp, solid_report_cb, &data);
}


/*
 * Returns the list of displayed solids and/or vector list information
 * based on the provided level:
 *
 *   <= -2 print primitive names (path leaves)
 *   == -1 print paths
 *   == 0 print paths + ILLUM on illuminated
 *   == 1 print paths + ILLUM on illuminated + center/region/color info
 *   >= 2 print paths + ILLUM on illuminated + center/region/color info + vector lists
 *
 * Usage:
 * solid_report [lvl]
 *
 */
int
ged_solid_report_core(struct ged *gedp, int argc, const char *argv[])
{
    int print_help = 0;
    int lvl = 0;
    static const char *usage = "[-2|-1|0|1|2|3]";
    const char *argv0 = argv[0];
    struct bu_opt_desc d[3];
    BU_OPT(d[0], "h", "help",      "",         NULL,  &print_help,  "Print help and exit");
    BU_OPT(d[1], "?",     "",      "",         NULL,  &print_help,  "");
    BU_OPT_NULL(d[2]);

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    int opt_ret = bu_opt_parse(NULL, argc, argv, d);

    if (opt_ret < 1 || opt_ret > 2 || print_help) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv0, usage);
	return BRLCAD_ERROR;
    }

    if (opt_ret == 2)
	lvl = atoi(argv[1]);

    if (lvl > 3)
	lvl = 3;

    dl_print_schain(gedp, gedp->dbip, lvl, 0, gedp->ged_result_str);

    return BRLCAD_OK;
}


#include "../include/plugin.h"

#define GED_SOLID_REPORT_COMMANDS(X, XID) \
    X(solid_report, ged_solid_report_core, GED_CMD_DEFAULT) \
    X(x, ged_solid_report_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_SOLID_REPORT_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_solid_report", 1, GED_SOLID_REPORT_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
