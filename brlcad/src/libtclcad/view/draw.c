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
#include "ged.h"
#include "tclcad.h"
#include "bsg/util.h"

/* Private headers */
#include "../tclcad_private.h"
#include "../view/view.h"


void
go_draw(bsg_view *gdvp)
{
    /* dm rendering path removed; Obol rendering is handled by obol_view widget. */
    (void)gdvp;
}

int
to_edit_redraw(struct ged *gedp,
	       int argc,
	       const char *argv[])
{
    size_t i;
    struct db_full_path subpath;
    int ret = BRLCAD_OK;

    if (argc != 2)
	return BRLCAD_ERROR;

    /* Phase 2e: gd_headDisplay is no longer populated by draw commands; use
     * scene-root children to enumerate drawn paths and re-draw any that
     * contain the given subpath.                                             */
    {
	if (db_string_to_path(&subpath, gedp->dbip, argv[1]) == 0) {
	    /* Collect unique top-level directory pointers from scene-root. */
	    bsg_view *_v = (bsg_view *)gedp->ged_gvp;
	    bsg_shape *_root = _v ? bsg_scene_root_get(_v) : NULL;
	    size_t _nshapes = _root ? BU_PTBL_LEN(&_root->children) : 0;
	    struct bu_ptbl drawn_tops;
	    bu_ptbl_init(&drawn_tops, 8, "to_edit_redraw drawn_tops");

	    for (i = 0; i < subpath.fp_len; ++i) {
		for (size_t _si = 0; _si < _nshapes; _si++) {
		    bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&_root->children, _si);
		    if (!sp || !sp->s_u_data) continue;
		    struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
		    if (!bdata->s_fullpath.fp_len) continue;
		    if (!db_full_path_search(&bdata->s_fullpath, subpath.fp_names[i]))
			continue;
		    /* Re-draw using the top-level name as the drawn path. */
		    struct directory *dp = bdata->s_fullpath.fp_names[0];
		    if (bu_ptbl_ins_unique(&drawn_tops, (long *)dp) < 0)
			continue; /* already scheduled for this cycle */
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
		    av[arg] = bu_strdup(dp->d_namep);
		    ret = ged_exec(gedp, arg + 1, (const char **)av);
		    bu_free(av[arg], "to_edit_redraw scene-root path");
		    bu_vls_free(&mflag);
		    bu_vls_free(&xflag);
		}
	    }
	    bu_ptbl_free(&drawn_tops);
	    db_free_full_path(&subpath);
	}
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
