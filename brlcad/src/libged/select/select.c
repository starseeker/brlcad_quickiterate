/*                         S E L E C T . C
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
/** @file libged/select.c
 *
 * The select command.
 *
 */

#include "common.h"

#include <string.h>


#include "bsg/payload.h"
#include "bsg/node.h"
#include "bu/getopt.h"
#include "ged/bsg_ged_draw.h"
#include "../ged_private.h"

static int
_ged_select_botpts(struct ged *gedp, struct rt_bot_internal *botip, double vx, double vy, double vwidth, double vheight, double vminz, int rflag)
{
    size_t i;
    fastf_t vr = 0.0;
    fastf_t vmin_x = 0.0;
    fastf_t vmin_y = 0.0;
    fastf_t vmax_x = 0.0;
    fastf_t vmax_y = 0.0;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_VIEW(gedp, BRLCAD_ERROR);

    if (rflag) {
	vr = vwidth;
    } else {
	vmin_x = vx;
	vmin_y = vy;

	if (vwidth > 0)
	    vmax_x = vx + vwidth;
	else {
	    vmin_x = vx + vwidth;
	    vmax_x = vx;
	}

	if (vheight > 0)
	    vmax_y = vy + vheight;
	else {
	    vmin_y = vy + vheight;
	    vmax_y = vy;
	}
    }

    if (rflag) {
	for (i = 0; i < botip->num_vertices; i++) {
	    point_t vloc;
	    point_t vpt;
	    vect_t diff;
	    fastf_t mag;

	    MAT4X3PNT(vpt, gedp->ged_gvp->gv_model2view, &botip->vertices[i*3]);

	    if (vpt[Z] < vminz)
		continue;

	    VSET(vloc, vx, vy, vpt[Z]);
	    VSUB2(diff, vpt, vloc);
	    mag = MAGNITUDE(diff);

	    if (mag > vr)
		continue;

	    bu_vls_printf(gedp->ged_result_str, "%zu ", i);
	}
    } else {
	for (i = 0; i < botip->num_vertices; i++) {
	    point_t vpt;

	    MAT4X3PNT(vpt, gedp->ged_gvp->gv_model2view, &botip->vertices[i*3]);

	    if (vpt[Z] < vminz)
		continue;

	    if (vmin_x <= vpt[X] && vpt[X] <= vmax_x &&
		vmin_y <= vpt[Y] && vpt[Y] <= vmax_y) {
		bu_vls_printf(gedp->ged_result_str, "%zu ", i);
	    }
	}
    }

    return BRLCAD_OK;
}

/* Callback data for dl_select */
struct select_data {
    mat_t model2view;
    struct bu_vls *vls;
    double vx;
    double vy;
    double vwidth;
    double vheight;
    int rflag;
    fastf_t vr;
    fastf_t vmin_x;
    fastf_t vmin_y;
    fastf_t vmax_x;
    fastf_t vmax_y;
};

static int
dl_select_cb(struct bv_scene_obj *sp, void *userdata)
{
    struct select_data *data = (struct select_data *)userdata;

    if (!bsg_node_ged_data_get((const bsg_node *)sp))
	return 1; /* continue */
    struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_ged_data_get((const bsg_node *)sp);

    point_t vmin, vmax;
    struct bv_vlist *vp;
    struct bu_list *vhead = bsg_node_vlist_head((bsg_node *)sp);

    vmax[X] = vmax[Y] = vmax[Z] = -INFINITY;
    vmin[X] = vmin[Y] = vmin[Z] =  INFINITY;

    for (BU_LIST_FOR(vp, bv_vlist, vhead)) {
	size_t j;
	size_t nused = vp->nused;
	int *cmd = vp->cmd;
	point_t *pt = vp->pt;
	point_t vpt;
	for (j = 0; j < nused; j++, cmd++, pt++) {
	    switch (*cmd) {
		case BV_VLIST_POLY_START:
		case BV_VLIST_POLY_VERTNORM:
		case BV_VLIST_TRI_START:
		case BV_VLIST_TRI_VERTNORM:
		case BV_VLIST_POINT_SIZE:
		case BV_VLIST_LINE_WIDTH:
		    /* attribute, not location */
		    break;
		case BV_VLIST_LINE_MOVE:
		case BV_VLIST_LINE_DRAW:
		case BV_VLIST_POLY_MOVE:
		case BV_VLIST_POLY_DRAW:
		case BV_VLIST_POLY_END:
		case BV_VLIST_TRI_MOVE:
		case BV_VLIST_TRI_DRAW:
		case BV_VLIST_TRI_END:
		    MAT4X3PNT(vpt, data->model2view, *pt);
		    V_MIN(vmin[X], vpt[X]);
		    V_MAX(vmax[X], vpt[X]);
		    V_MIN(vmin[Y], vpt[Y]);
		    V_MAX(vmax[Y], vpt[Y]);
		    V_MIN(vmin[Z], vpt[Z]);
		    V_MAX(vmax[Z], vpt[Z]);
		    break;
		default: {
		    bu_vls_printf(data->vls, "unknown vlist op %d\n", *cmd);
		}
	    }
	}
    }

    if (data->rflag) {
	point_t vloc;
	vect_t diff;
	fastf_t mag;

	VSET(vloc, data->vx, data->vy, vmin[Z]);
	VSUB2(diff, vmin, vloc);
	mag = MAGNITUDE(diff);

	if (mag > data->vr)
	    return 1; /* continue */

	VSET(vloc, data->vx, data->vy, vmax[Z]);
	VSUB2(diff, vmax, vloc);
	mag = MAGNITUDE(diff);

	if (mag > data->vr)
	    return 1; /* continue */

	db_path_to_vls(data->vls, &bdata->s_fullpath);
	bu_vls_printf(data->vls, "\n");
    } else {
	if (data->vmin_x <= vmin[X] && vmax[X] <= data->vmax_x &&
	    data->vmin_y <= vmin[Y] && vmax[Y] <= data->vmax_y) {
	    db_path_to_vls(data->vls, &bdata->s_fullpath);
	    bu_vls_printf(data->vls, "\n");
	}
    }

    return 1; /* continue */
}

int
dl_select(struct ged *gedp, mat_t model2view, struct bu_vls *vls, double vx, double vy, double vwidth, double vheight, int rflag)
{
    struct select_data data;

    MAT_COPY(data.model2view, model2view);
    data.vls = vls;
    data.vx = vx;
    data.vy = vy;
    data.vwidth = vwidth;
    data.vheight = vheight;
    data.rflag = rflag;
    data.vr = 0.0;
    data.vmin_x = 0.0;
    data.vmin_y = 0.0;
    data.vmax_x = 0.0;
    data.vmax_y = 0.0;

    if (rflag) {
        data.vr = vwidth;
    } else {
        data.vmin_x = vx;
        data.vmin_y = vy;

        if (vwidth > 0)
            data.vmax_x = vx + vwidth;
        else {
            data.vmin_x = vx + vwidth;
            data.vmax_x = vx;
        }

        if (vheight > 0)
            data.vmax_y = vy + vheight;
        else {
            data.vmin_y = vy + vheight;
            data.vmax_y = vy;
        }
    }

    bsg_view_obj_foreach_solid(gedp, dl_select_cb, &data);

    return BRLCAD_OK;
}

/* Callback for partial select - checks each vertex */
static int
dl_select_partial_cb(struct bv_scene_obj *sp, void *userdata)
{
    struct select_data *data = (struct select_data *)userdata;

    if (!bsg_node_ged_data_get((const bsg_node *)sp))
	return 1; /* continue */
    struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_ged_data_get((const bsg_node *)sp);

    struct bv_vlist *vp;
    struct bu_list *vhead = bsg_node_vlist_head((bsg_node *)sp);

    for (BU_LIST_FOR(vp, bv_vlist, vhead)) {
	size_t j;
	size_t nused = vp->nused;
	int *cmd = vp->cmd;
	point_t *pt = vp->pt;
	point_t vpt;
	for (j = 0; j < nused; j++, cmd++, pt++) {
	    switch (*cmd) {
		case BV_VLIST_POLY_START:
		case BV_VLIST_POLY_VERTNORM:
		case BV_VLIST_TRI_START:
		case BV_VLIST_TRI_VERTNORM:
		    /* Has normal vector, not location */
		    break;
		case BV_VLIST_LINE_MOVE:
		case BV_VLIST_LINE_DRAW:
		case BV_VLIST_POLY_MOVE:
		case BV_VLIST_POLY_DRAW:
		case BV_VLIST_POLY_END:
		case BV_VLIST_TRI_MOVE:
		case BV_VLIST_TRI_DRAW:
		case BV_VLIST_TRI_END:
		    MAT4X3PNT(vpt, data->model2view, *pt);

		    if (data->rflag) {
			point_t vloc;
			vect_t diff;
			fastf_t mag;

			VSET(vloc, data->vx, data->vy, vpt[Z]);
			VSUB2(diff, vpt, vloc);
			mag = MAGNITUDE(diff);

			if (mag > data->vr)
			    continue;

			db_path_to_vls(data->vls, &bdata->s_fullpath);
			bu_vls_printf(data->vls, "\n");

			return 1; /* solid done, continue to next */
		    } else {
			if (data->vmin_x <= vpt[X] && vpt[X] <= data->vmax_x &&
			    data->vmin_y <= vpt[Y] && vpt[Y] <= data->vmax_y) {
			    db_path_to_vls(data->vls, &bdata->s_fullpath);
			    bu_vls_printf(data->vls, "\n");

			    return 1; /* solid done, continue to next */
			}
		    }

		    break;
		default: {
		    bu_vls_printf(data->vls, "unknown vlist op %d\n", *cmd);
		}
	    }
	}
    }

    return 1; /* continue */
}

int
dl_select_partial(struct ged *gedp, mat_t model2view, struct bu_vls *vls, double vx, double vy, double vwidth, double vheight, int rflag)
{
    struct select_data data;

    MAT_COPY(data.model2view, model2view);
    data.vls = vls;
    data.vx = vx;
    data.vy = vy;
    data.vwidth = vwidth;
    data.vheight = vheight;
    data.rflag = rflag;
    data.vr = 0.0;
    data.vmin_x = 0.0;
    data.vmin_y = 0.0;
    data.vmax_x = 0.0;
    data.vmax_y = 0.0;

    if (rflag) {
        data.vr = vwidth;
    } else {
        data.vmin_x = vx;
        data.vmin_y = vy;

        if (vwidth > 0)
            data.vmax_x = vx + vwidth;
        else {
            data.vmin_x = vx + vwidth;
            data.vmax_x = vx;
        }

        if (vheight > 0)
            data.vmax_y = vy + vheight;
        else {
            data.vmin_y = vy + vheight;
            data.vmax_y = vy;
        }
    }

    bsg_view_obj_foreach_solid(gedp, dl_select_partial_cb, &data);

    return BRLCAD_OK;
}


/*
 * Returns a list of items within the specified rectangle or circle.
 * If bot is specified, the bot points within the specified area are returned.
 *
 * Usage:
 * select [-b bot] [-p] [-z vminz] vx vy {vr | vw vh}
 *
 */
extern int ged_select2_core(struct ged *gedp, int argc, const char *argv[]);
int
ged_select_core(struct ged *gedp, int argc, const char *argv[])
{
    if (gedp->dbi_state)
	return ged_select2_core(gedp, argc, argv);

    int c;
    double vx, vy, vw, vh, vr;
    static const char *usage = "[-b bot] [-p] [-z vminz] vx vy {vr | vw vh}";
    const char *cmd = argv[0];
    struct rt_db_internal intern;
    struct rt_bot_internal *botip = NULL;
    int pflag = 0;
    double vminz = -1000.0;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_VIEW(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    /* Get command line options. */
    bu_optind = 1;
    while ((c = bu_getopt(argc, (char * const *)argv, "b:pz:")) != -1) {
	switch (c) {
	case 'b':
	{
	    mat_t mat;

	    /* skip subsequent bot specifications */
	    if (botip != (struct rt_bot_internal *)NULL)
		break;

	    if (wdb_import_from_path2(gedp->ged_result_str, &intern, bu_optarg, wdbp, mat) & BRLCAD_ERROR) {
		bu_vls_printf(gedp->ged_result_str, "%s: failed to find %s", cmd, bu_optarg);
		return BRLCAD_ERROR;
	    }

	    if (intern.idb_major_type != DB5_MAJORTYPE_BRLCAD ||
		intern.idb_minor_type != DB5_MINORTYPE_BRLCAD_BOT) {
		bu_vls_printf(gedp->ged_result_str, "%s: %s is not a BOT", cmd, bu_optarg);
		rt_db_free_internal(&intern);

		return BRLCAD_ERROR;
	    }

	    botip = (struct rt_bot_internal *)intern.idb_ptr;
	}

	break;
	case 'p':
	    pflag = 1;
	    break;
	case 'z':
	    if (sscanf(bu_optarg, "%lf", &vminz) != 1) {
		if (botip != (struct rt_bot_internal *)NULL)
		    rt_db_free_internal(&intern);

		bu_vls_printf(gedp->ged_result_str, "%s: bad vminz - %s", cmd, bu_optarg);
		bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", cmd, usage);
	    }

	    break;
	default:
	    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", cmd, usage);
	    return BRLCAD_ERROR;
	}
    }

    argc -= (bu_optind - 1);
    argv += (bu_optind - 1);

    if (argc < 4 || 5 < argc) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", cmd, usage);
	return BRLCAD_ERROR;
    }

    if (argc == 4) {
	if (sscanf(argv[1], "%lf", &vx) != 1 ||
	    sscanf(argv[2], "%lf", &vy) != 1 ||
	    sscanf(argv[3], "%lf", &vr) != 1) {
	    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", cmd, usage);
	    return BRLCAD_ERROR;
	}

	if (botip != (struct rt_bot_internal *)NULL) {
	    int ret;

	    ret = _ged_select_botpts(gedp, botip, vx, vy, vr, vr, vminz, 1);
	    rt_db_free_internal(&intern);

	    return ret;
	} else {
	    if (pflag)
		return dl_select_partial(gedp, gedp->ged_gvp->gv_model2view, gedp->ged_result_str, vx, vy, vr, vr, 1);
	    else
		return dl_select(gedp, gedp->ged_gvp->gv_model2view, gedp->ged_result_str, vx, vy, vr, vr, 1);
	}
    } else {
	if (sscanf(argv[1], "%lf", &vx) != 1 ||
	    sscanf(argv[2], "%lf", &vy) != 1 ||
	    sscanf(argv[3], "%lf", &vw) != 1 ||
	    sscanf(argv[4], "%lf", &vh) != 1) {
	    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", cmd, usage);
	    return BRLCAD_ERROR;
	}

	if (botip != (struct rt_bot_internal *)NULL) {
	    int ret;

	    ret = _ged_select_botpts(gedp, botip, vx, vy, vw, vh, vminz, 0);
	    rt_db_free_internal(&intern);

	    return ret;
	} else {
	    if (pflag)
		return dl_select_partial(gedp, gedp->ged_gvp->gv_model2view, gedp->ged_result_str, vx, vy, vw, vh, 0);
	    else
		return dl_select(gedp, gedp->ged_gvp->gv_model2view, gedp->ged_result_str, vx, vy, vw, vh, 0);
	}
    }
}


/*
 * Returns a list of items within the previously defined rectangle.
 *
 * Usage:
 * rselect [-b bot] [-p] [-z vminz]
 *
 */
int
ged_rselect_core(struct ged *gedp, int argc, const char *argv[])
{
    int c;
    static const char *usage = "[-b bot] [-p] [-z vminz]";
    const char *cmd = argv[0];
    struct rt_db_internal intern;
    struct rt_bot_internal *botip = (struct rt_bot_internal *)NULL;
    int pflag = 0;
    double vminz = -1000.0;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_VIEW(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);

    /* Get command line options. */
    bu_optind = 1;
    while ((c = bu_getopt(argc, (char * const *)argv, "b:pz:")) != -1) {
	switch (c) {
	case 'b':
	{
	    mat_t mat;

	    /* skip subsequent bot specifications */
	    if (botip != (struct rt_bot_internal *)NULL)
		break;

	    if (wdb_import_from_path2(gedp->ged_result_str, &intern, bu_optarg, wdbp, mat) == BRLCAD_ERROR) {
		bu_vls_printf(gedp->ged_result_str, "%s: failed to find %s", cmd, bu_optarg);
		return BRLCAD_ERROR;
	    }

	    if (intern.idb_major_type != DB5_MAJORTYPE_BRLCAD ||
		intern.idb_minor_type != DB5_MINORTYPE_BRLCAD_BOT) {
		bu_vls_printf(gedp->ged_result_str, "%s: %s is not a BOT", cmd, bu_optarg);
		rt_db_free_internal(&intern);

		return BRLCAD_ERROR;
	    }

	    botip = (struct rt_bot_internal *)intern.idb_ptr;
	}

	break;
	case 'p':
	    pflag = 1;
	    break;
	case 'z':
	    if (sscanf(bu_optarg, "%lf", &vminz) != 1) {
		if (botip != (struct rt_bot_internal *)NULL)
		    rt_db_free_internal(&intern);

		bu_vls_printf(gedp->ged_result_str, "%s: bad vminz - %s", cmd, bu_optarg);
		bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", cmd, usage);
	    }

	    break;
	default:
	    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", cmd, usage);
	    return BRLCAD_ERROR;
	}
    }

    argc -= (bu_optind - 1);

    if (argc != 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", cmd, usage);
	return BRLCAD_ERROR;
    }

    if (botip != (struct rt_bot_internal *)NULL) {
	int ret;

	ret = _ged_select_botpts(gedp, botip,
				  gedp->ged_gvp->gv_s->gv_rect.x,
				  gedp->ged_gvp->gv_s->gv_rect.y,
				  gedp->ged_gvp->gv_s->gv_rect.width,
				  gedp->ged_gvp->gv_s->gv_rect.height,
				  vminz,
				  0);

	rt_db_free_internal(&intern);
	return ret;
    } else {
	if (pflag)
	    return dl_select_partial(gedp, gedp->ged_gvp->gv_model2view, gedp->ged_result_str,
				     gedp->ged_gvp->gv_s->gv_rect.x,
				     gedp->ged_gvp->gv_s->gv_rect.y,
				     gedp->ged_gvp->gv_s->gv_rect.width,
				     gedp->ged_gvp->gv_s->gv_rect.height,
				     0);
	else
	    return dl_select(gedp, gedp->ged_gvp->gv_model2view, gedp->ged_result_str,
			     gedp->ged_gvp->gv_s->gv_rect.x,
			     gedp->ged_gvp->gv_s->gv_rect.y,
			     gedp->ged_gvp->gv_s->gv_rect.width,
			     gedp->ged_gvp->gv_s->gv_rect.height,
			     0);
    }
}


#include "../include/plugin.h"

#define GED_SELECT_COMMANDS(X, XID) \
    X(select, ged_select_core, GED_CMD_DEFAULT) \
    X(rselect, ged_rselect_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_SELECT_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_select", 1, GED_SELECT_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
