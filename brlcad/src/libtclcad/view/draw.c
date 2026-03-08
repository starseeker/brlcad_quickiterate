/*                       D R A W . C
 * BRL-CAD
 *
 * Copyright (c) 2000-2025 United States Government as represented by
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
#include "dm/view.h"
#include "ged.h"
#include "tclcad.h"
#include "bsg/util.h"

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
go_draw_solid(bsg_view *gdvp, bsg_shape *sp)
{
    struct tclcad_view_data *tvd = (struct tclcad_view_data *)gdvp->u_data;
    struct ged *gedp = tvd->gedp;
    struct tclcad_ged_data *tgd = (struct tclcad_ged_data *)gedp->u_data;
    struct dm *dmp = (struct dm *)gdvp->dmp;
    struct bu_hash_entry *entry;
    struct dm_path_edit_params *params = NULL;
    mat_t save_mat, edit_model2view;
    struct path_match_data data;

    if (!sp->s_u_data)
	return;
    struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;

    data.s_fpath = &bdata->s_fullpath;
    data.dbip = gedp->dbip;
    entry = key_matches_paths(tgd->go_dmv.edited_paths, &data);

    if (entry != NULL) {
	params = (struct dm_path_edit_params *)bu_hash_value(entry, NULL);
    }
    if (params) {
	struct bsg_camera _dv;
	bsg_view_get_camera(gdvp, &_dv);
	MAT_COPY(save_mat, _dv.model2view);
	bn_mat_mul(edit_model2view, _dv.model2view, params->edit_mat);
	dm_loadmatrix(dmp, edit_model2view, 0);
    }

    if (tgd->go_dmv.dlist_on) {
	dm_draw_dlist(dmp, sp->s_dlist);
    } else {
	if (sp->s_iflag == UP)
	    (void)dm_set_fg(dmp, 255, 255, 255, 0, sp->s_os->transparency);
	else
	    (void)dm_set_fg(dmp,
			    (unsigned char)sp->s_color[0],
			    (unsigned char)sp->s_color[1],
			    (unsigned char)sp->s_color[2], 0, sp->s_os->transparency);

	if (sp->s_os->s_dmode == 4) {
	    (void)dm_draw_vlist_hidden_line(dmp, (struct bsg_vlist *)&sp->s_vlist);
	} else {
	    (void)dm_draw_vlist(dmp, (struct bsg_vlist *)&sp->s_vlist);
	}
    }
    if (params) {
	dm_loadmatrix(dmp, save_mat, 0);
    }
}

/* Draw all display lists */
static int
go_draw_dlist(bsg_view *gdvp)
{
    bsg_shape *sp;
    int line_style = -1;
    struct dm *dmp = (struct dm *)gdvp->dmp;

    bsg_shape *root = bsg_scene_root_get(gdvp);
    size_t nshapes = root ? BU_PTBL_LEN(&root->children) : 0;

    if (dm_get_transparency(dmp)) {
	/* First, draw opaque stuff */
	for (size_t si = 0; si < nshapes; si++) {
	    sp = (bsg_shape *)BU_PTBL_GET(&root->children, si);
	    if (sp->s_os->transparency < 1.0)
		continue;

	    if (line_style != sp->s_soldash) {
		line_style = sp->s_soldash;
		(void)dm_set_line_attr(dmp, dm_get_linewidth(dmp), line_style);
	    }

	    go_draw_solid(gdvp, sp);
	}

	/* disable write to depth buffer */
	(void)dm_set_depth_mask(dmp, 0);

	/* Second, draw transparent stuff */
	for (size_t si = 0; si < nshapes; si++) {
	    sp = (bsg_shape *)BU_PTBL_GET(&root->children, si);
	    /* already drawn above */
	    if (ZERO(sp->s_os->transparency - 1.0))
		continue;

	    if (line_style != sp->s_soldash) {
		line_style = sp->s_soldash;
		(void)dm_set_line_attr(dmp, dm_get_linewidth(dmp), line_style);
	    }

	    go_draw_solid(gdvp, sp);
	}

	/* re-enable write to depth buffer */
	(void)dm_set_depth_mask(dmp, 1);
    } else {
	for (size_t si = 0; si < nshapes; si++) {
	    sp = (bsg_shape *)BU_PTBL_GET(&root->children, si);
	    if (line_style != sp->s_soldash) {
		line_style = sp->s_soldash;
		(void)dm_set_line_attr(dmp, dm_get_linewidth(dmp), line_style);
	    }

	    go_draw_solid(gdvp, sp);
	}
    }

    return BRLCAD_OK;
}

void
go_draw(bsg_view *gdvp)
{
    struct bsg_camera _gdvc;
    bsg_view_get_camera(gdvp, &_gdvc);
    (void)dm_loadmatrix((struct dm *)gdvp->dmp, _gdvc.model2view, 0);

    if (SMALL_FASTF < _gdvc.perspective)
	(void)dm_loadpmatrix((struct dm *)gdvp->dmp, _gdvc.pmat);
    else
	(void)dm_loadpmatrix((struct dm *)gdvp->dmp, (fastf_t *)NULL);

    go_draw_dlist(gdvp);
}

int
to_edit_redraw(struct ged *gedp,
	       int argc,
	       const char *argv[])
{
    size_t i;
    register struct display_list *gdlp;
    register struct display_list *next_gdlp;
    struct db_full_path subpath;
    int ret = BRLCAD_OK;

    if (argc != 2)
	return BRLCAD_ERROR;

    gdlp = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
    while (BU_LIST_NOT_HEAD(gdlp, ged_dl(gedp))) {
	gdlp->dl_wflag = 0;
	gdlp = BU_LIST_PNEXT(display_list, gdlp);
    }

    if (db_string_to_path(&subpath, gedp->dbip, argv[1]) == 0) {
	for (i = 0; i < subpath.fp_len; ++i) {
	    gdlp = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
	    while (BU_LIST_NOT_HEAD(gdlp, (struct bu_list *)ged_dl(gedp))) {
		register bsg_shape *curr_sp;

		next_gdlp = BU_LIST_PNEXT(display_list, gdlp);

		if (gdlp->dl_wflag) {
		    gdlp = next_gdlp;
		    continue;
		}

		/* Phase 2e: search scene-root children instead of dl_head_scene_obj */
		bsg_view *_v = (bsg_view *)gedp->ged_gvp;
		bsg_shape *_root = _v ? bsg_scene_root_get(_v) : NULL;
		size_t _nshapes = _root ? BU_PTBL_LEN(&_root->children) : 0;
		for (size_t _si = 0; _si < _nshapes; _si++) {
		    curr_sp = (bsg_shape *)BU_PTBL_GET(&_root->children, _si);
		    if (!curr_sp->s_u_data) continue;
		    struct ged_bv_data *bdata = (struct ged_bv_data *)curr_sp->s_u_data;
		    /* Only consider shapes belonging to this gdlp */
		    struct db_full_path _gdlp_fp;
		    db_full_path_init(&_gdlp_fp);
		    if (db_string_to_path(&_gdlp_fp, gedp->dbip, bu_vls_cstr(&gdlp->dl_path)) != 0)
			{ db_free_full_path(&_gdlp_fp); continue; }
		    int _belongs = db_full_path_match_top(&_gdlp_fp, &bdata->s_fullpath);
		    db_free_full_path(&_gdlp_fp);
		    if (!_belongs) continue;

		    if (db_full_path_search(&bdata->s_fullpath, subpath.fp_names[i])) {
			struct display_list *last_gdlp;
			/* Use the matched shape's draw mode as representative for this group */
			bsg_shape *sp = curr_sp;
			struct bu_vls mflag = BU_VLS_INIT_ZERO;
			struct bu_vls xflag = BU_VLS_INIT_ZERO;
			char *av[5] = {0};
			int arg = 0;

			av[arg++] = (char *)argv[0];
			if (sp->s_os->s_dmode == 4) {
			    av[arg++] = "-h";
			} else {
			    bu_vls_printf(&mflag, "-m%d", sp->s_os->s_dmode);
			    bu_vls_printf(&xflag, "-x%f", sp->s_os->transparency);
			    av[arg++] = bu_vls_addr(&mflag);
			    av[arg++] = bu_vls_addr(&xflag);
			}
			av[arg] = bu_vls_strdup(&gdlp->dl_path);

			ret = ged_exec(gedp, arg + 1, (const char **)av);

			bu_free(av[arg], "to_edit_redraw");
			bu_vls_free(&mflag);
			bu_vls_free(&xflag);

			/* The function call above causes gdlp to be
			 * removed from the display list. A new one is
			 * then created and appended to the end.  Here
			 * we put it back where it belongs (i.e. as
			 * specified by the user).  This also prevents
			 * an infinite loop where the last and the
			 * second to last list items play leap frog
			 * with the end of list.
			 */
			last_gdlp = BU_LIST_PREV(display_list, (struct bu_list *)ged_dl(gedp));
			BU_LIST_DEQUEUE(&last_gdlp->l);
			BU_LIST_INSERT(&next_gdlp->l, &last_gdlp->l);
			last_gdlp->dl_wflag = 1;

			goto end;
		    }
		}

	    end:
		gdlp = next_gdlp;
	    }
	}

	db_free_full_path(&subpath);
    }

    to_refresh_all_views(current_top);

    return ret;
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
