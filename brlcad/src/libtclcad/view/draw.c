/*                       D R A W . C
 * BRL-CAD
 *
 * Copyright (c) 2000-2026 United States Government as represented by
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
/** @addtogroup libtclcad */
/** @{ */
/** @file libtclcad/view/draw.c
 *
 */
/** @} */

#include "common.h"
#include "dm.h"
#include "dm/view.h"
#include "bsg/appearance.h"
#include "bsg/material.h"
#include "bsg/node.h"
#include "bsg/util.h"
#include "bsg/visit.h"
#include "ged.h"
#include "ged/bsg_ged_draw.h"
#include "tclcad.h"

/* Private headers */
#include "../tclcad_private.h"
#include "../view/view.h"



struct path_match_data {
    struct db_full_path *s_fpath;
    struct db_i *dbip;
};

static struct bu_hash_entry *
key_matches_paths(struct bu_hash_tbl *t, void *udata)
{
    struct path_match_data *data = (struct path_match_data *)udata;
    struct db_full_path entry_fpath;
    uint8_t *key;
    char *path_string;
    struct bu_hash_entry *entry = bu_hash_next(t, NULL);

    while (entry) {
	(void)bu_hash_key(entry, &key, NULL);
	path_string = (char *)key;
	if (db_string_to_path(&entry_fpath, data->dbip, path_string) < 0) {
	    continue;
	}

	if (db_full_path_match_top(&entry_fpath, data->s_fpath)) {
	    db_free_full_path(&entry_fpath);
	    return entry;
	}

	db_free_full_path(&entry_fpath);
	entry = bu_hash_next(t, entry);
    }

    return NULL;
}

static void
go_draw_solid(struct bview *gdvp, struct bv_scene_obj *sp)
{
    struct tclcad_view_data *tvd = (struct tclcad_view_data *)gdvp->u_data;
    struct ged *gedp = tvd->gedp;
    struct tclcad_ged_data *tgd = (struct tclcad_ged_data *)gedp->u_data;
    struct dm *dmp = (struct dm *)gdvp->dmp;
    struct bu_hash_entry *entry;
    struct dm_path_edit_params *params = NULL;
    mat_t save_mat, edit_model2view;
    struct path_match_data data;

    if (!bsg_node_ged_data_get((const bsg_node *)sp))
	return;
    struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_ged_data_get((const bsg_node *)sp);

    data.s_fpath = &bdata->s_fullpath;
    data.dbip = gedp->dbip;
    entry = key_matches_paths(tgd->go_dmv.edited_paths, &data);

    if (entry != NULL) {
	params = (struct dm_path_edit_params *)bu_hash_value(entry, NULL);
    }
    if (params) {
	MAT_COPY(save_mat, gdvp->gv_model2view);
	bn_mat_mul(edit_model2view, gdvp->gv_model2view, params->edit_mat);
	dm_loadmatrix(dmp, edit_model2view, 0);
    }

    /* Phase 11D: resolve highlight/color from BSG accessors. */
    struct bsg_material _mat;
    bsg_node_material_get((const bsg_node *)sp, &_mat);
    if (bsg_node_legacy_illum((const bsg_node *)sp)) {
	(void)dm_set_fg(dmp, 255, 255, 255, 0, _mat.transparency);
    } else {
	if (_mat.use_override_color)
	    (void)dm_set_fg(dmp, _mat.override_rgb[0], _mat.override_rgb[1], _mat.override_rgb[2],
			    0, _mat.transparency);
	else if (_mat.use_geometry_default_color) {
	    unsigned char *gdc = dm_get_geometry_default_color(dmp);
	    (void)dm_set_fg(dmp, gdc[0], gdc[1], gdc[2], 0, _mat.transparency);
	} else
	    (void)dm_set_fg(dmp, _mat.rgba[0], _mat.rgba[1], _mat.rgba[2], 0, _mat.transparency);
    }

    /* Phase 13 (drawing_stack_modernization): the GL backend now lazily
     * compiles per-shape display lists on first draw (matching the qged
     * model).  Route through dm_draw_obj so gl_draw_obj handles dlist
     * generation/replay, hidden-line mode, and the standard vlist walk
     * itself.  The legacy `dlist_on` Tcl toggle is retained as a
     * no-op for backward compatibility. */
    (void)dm_draw_obj(dmp, sp);

    if (params) {
	dm_loadmatrix(dmp, save_mat, 0);
    }
}

/* ------------------------------------------------------------------ */
/* bsg_visit callbacks for go_draw_dlist transparency passes           */
/* ------------------------------------------------------------------ */

struct _go_draw_data {
    struct bview *gdvp;
    int line_style;
    int transparency_pass; /* 0=all, 1=opaque, 2=transparent */
};

static int
_go_draw_solid_cb(bsg_node *n, void *ud)
{
    struct bv_scene_obj *sp = (struct bv_scene_obj *)n;
    struct _go_draw_data *d = (struct _go_draw_data *)ud;
    struct dm *dmp = (struct dm *)d->gdvp->dmp;

    /* Phase 11D: resolve transparency and line style from BSG appearance. */
    struct bsg_appearance _app;
    struct bsg_material _mat;
    bsg_node_appearance_get((const bsg_node *)sp, &_app);
    bsg_node_material_get((const bsg_node *)sp, &_mat);
    if (d->transparency_pass == 1 && _mat.transparency < 1.0) return 1;
    if (d->transparency_pass == 2 && ZERO(_mat.transparency - 1.0)) return 1;

    int _soldash = (_app.line_style == BSG_APPEARANCE_LINE_DASHED) ? 1 : 0;
    if (d->line_style != _soldash) {
	d->line_style = _soldash;
	(void)dm_set_line_attr(dmp, dm_get_linewidth(dmp), d->line_style);
    }
    go_draw_solid(d->gdvp, sp);
    return 1;
}

/* ------------------------------------------------------------------ */

/* Draw all display lists */
static int
go_draw_dlist(struct bview *gdvp)
{
    struct dm *dmp = (struct dm *)gdvp->dmp;
    struct tclcad_view_data *tvd = (struct tclcad_view_data *)gdvp->u_data;
    struct ged *lgedp = tvd->gedp;
    struct _go_draw_data d;
    d.gdvp = gdvp;
    d.line_style = -1;

    if (dm_get_transparency(dmp)) {
	/* First, draw opaque stuff */
	d.transparency_pass = 1;
	bsg_visit((bsg_node *)bsg_view_obj_root(lgedp), BSG_NODE_SHAPE, _go_draw_solid_cb, &d);

	/* disable write to depth buffer */
	(void)dm_set_depth_mask(dmp, 0);

	/* Second, draw transparent stuff */
	d.transparency_pass = 2;
	bsg_visit((bsg_node *)bsg_view_obj_root(lgedp), BSG_NODE_SHAPE, _go_draw_solid_cb, &d);

	/* re-enable write to depth buffer */
	(void)dm_set_depth_mask(dmp, 1);
    } else {
	d.transparency_pass = 0;
	bsg_visit((bsg_node *)bsg_view_obj_root(lgedp), BSG_NODE_SHAPE, _go_draw_solid_cb, &d);
    }

    return BRLCAD_OK;
}

void
go_draw(struct bview *gdvp)
{
    struct dm *dmp = (struct dm *)gdvp->dmp;

    (void)dm_loadmatrix(dmp, gdvp->gv_model2view, 0);

    if (SMALL_FASTF < gdvp->gv_perspective)
	(void)dm_loadpmatrix(dmp, gdvp->gv_pmat);
    else
	(void)dm_loadpmatrix(dmp, (fastf_t *)NULL);

    /* Phase F (drawing_stack_modernization): bsg_root is now an alias for
     * gv_draw_root; bsg_root->children IS the live draw-tree children list.
     * The former BSG block (which iterated bsg_root->children via
     * go_draw_solid) was silently broken: BSG_NODE_GROUP nodes — the direct
     * children of the draw root — have no s_u_data, so go_draw_solid skipped
     * them all.  go_draw_dlist() uses bsg_visit(draw_root, BSG_NODE_SHAPE)
     * which descends to the leaf shape nodes correctly and is the right path
     * for all libtclcad/Archer views. */
    go_draw_dlist(gdvp);
}

int
to_edit_redraw(struct ged *gedp,
	       int argc,
	       const char *argv[])
{
    if (argc != 2)
	return BRLCAD_ERROR;

    struct db_full_path subpath;
    if (db_string_to_path(&subpath, gedp->dbip, argv[1]) != 0)
	return BRLCAD_OK;  /* path not found — nothing to do */

    /* Phase 6: iterate the BSG view tree (BV_DB_OBJS) instead of walking
     * the legacy ged_dl / dl_head_scene_obj display-list chain. */
    struct bu_ptbl *views = bv_set_views(&gedp->ged_views);
    size_t vi;
    for (vi = 0; vi < BU_PTBL_LEN(views); vi++) {
	struct bview *v = (struct bview *)BU_PTBL_GET(views, vi);
	struct bu_ptbl *db_objs = bv_view_objs(v, BV_DB_OBJS);
	if (!db_objs)
	    continue;

	size_t oi;
	for (oi = 0; oi < BU_PTBL_LEN(db_objs); oi++) {
	    struct bv_scene_obj *sp =
		(struct bv_scene_obj *)BU_PTBL_GET(db_objs, oi);
	    if (!sp || !bsg_node_ged_data_get((const bsg_node *)sp))
		continue;

	    struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_ged_data_get((const bsg_node *)sp);
	    size_t pi;
	    for (pi = 0; pi < subpath.fp_len; pi++) {
		if (!db_full_path_search(&bdata->s_fullpath,
					 subpath.fp_names[pi]))
		    continue;

		/* Match found — re-execute draw for this path */
		struct bu_vls mflag = BU_VLS_INIT_ZERO;
		struct bu_vls xflag = BU_VLS_INIT_ZERO;
		char *av[5] = {0};
		int arg = 0;

		av[arg++] = (char *)argv[0];
		/* Phase 11D: read draw mode from appearance and transparency from material. */
		struct bsg_appearance _sp_app;
		struct bsg_material _sp_mat;
		bsg_node_appearance_get((const bsg_node *)sp, &_sp_app);
		bsg_node_material_get((const bsg_node *)sp, &_sp_mat);
		if (_sp_app.draw_mode == 4) {
		    av[arg++] = "-h";
		} else {
		    bu_vls_printf(&mflag, "-m%d", _sp_app.draw_mode);
		    bu_vls_printf(&xflag, "-x%f", _sp_mat.transparency);
		    av[arg++] = bu_vls_addr(&mflag);
		    av[arg++] = bu_vls_addr(&xflag);
		}
		av[arg] = bu_vls_strdup(&sp->bsg.bsg_name);

		ged_exec(gedp, arg + 1, (const char **)av);

		bu_free(av[arg], "to_edit_redraw");
		bu_vls_free(&mflag);
		bu_vls_free(&xflag);
		break;
	    }
	}
    }

    db_free_full_path(&subpath);
    to_refresh_all_views(current_top);
    return BRLCAD_OK;
}

int
to_redraw(struct ged *gedp,
	  int argc,
	  const char *argv[],
	  ged_func_ptr UNUSED(func),
	  const char *usage,
	  int UNUSED(maxargs))
{
    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return GED_HELP;
    }

    if (argc != 2) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return BRLCAD_ERROR;
    }

    return to_edit_redraw(gedp, argc, argv);
}

int
to_blast(struct ged *gedp,
	 int argc,
	 const char *argv[],
	 ged_func_ptr UNUSED(func),
	 const char *UNUSED(usage),
	 int UNUSED(maxargs))
{
    int ret;

    ret = ged_exec(gedp, argc, argv);

    if (ret != BRLCAD_OK)
	return ret;

    to_autoview_all_views(current_top);

    return ret;
}



/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
