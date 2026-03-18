/*                        T I T L E S . C
 * BRL-CAD
 *
 * Copyright (c) 1985-2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file mged/titles.c
 *
 */

#include "common.h"

#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bu/units.h"
#include "bn.h"
#include "ged.h"

#include "./mged.h"
#include "bsg/util.h"
#include "./sedit.h"
#include "./mged_dm.h"
#include "./menu.h"

#define USE_OLD_MENUS 0

char *state_str[] = {
    "-ZOT-",
    "VIEWING",
    "SOL PICK",
    "SOL EDIT",
    "OBJ PICK",
    "OBJ PATH",
    "OBJ EDIT",
    "VERTPICK",
    "UNKNOWN",
};


// FIXME: Global
extern mat_t perspective_mat;  /* defined in dozoom.c */

/*
 * Prepare the numerical display of the currently edited solid/object.
 */
void
create_text_overlay(struct mged_state *s, struct bu_vls *vp)
{
    struct directory *dp;

    BU_CK_VLS(vp);

    /*
     * Set up for character output.  For the best generality, we
     * don't assume that the display can process a CRLF sequence,
     * so each line is written with a separate call to dm_draw_string_2d().
     */

    /* print solid info at top of screen
     * Check if the illuminated solid still exists or it has been killed
     * before Accept was clicked.
     */
    if (MEDIT(s)->edit_flag >= 0 && illump != NULL && illump->s_u_data != NULL) {
	struct ged_bv_data *bdata = (struct ged_bv_data *)illump->s_u_data;

	dp = LAST_SOLID(bdata);

	bu_vls_strcat(vp, "** SOLID -- ");
	bu_vls_strcat(vp, dp->d_namep);
	bu_vls_strcat(vp, ": ");

	vls_solid(s, vp, &MEDIT(s)->es_int, bn_mat_identity);

	if (bdata->s_fullpath.fp_len > 1) {
	    bu_vls_strcat(vp, "\n** PATH --  ");
	    db_path_to_vls(vp, &bdata->s_fullpath);
	    bu_vls_strcat(vp, ": ");

	    /* print the evaluated (path) solid parameters */
	    vls_solid(s, vp, &MEDIT(s)->es_int, MEDIT(s)->e_mat);
	}
    }

    /* display path info for object editing also */
    if (s->global_editing_state == ST_O_EDIT && illump != NULL && illump->s_u_data != NULL) {
	struct ged_bv_data *bdata = (struct ged_bv_data *)illump->s_u_data;

	bu_vls_strcat(vp, "** PATH --  ");
	db_path_to_vls(vp, &bdata->s_fullpath);
	bu_vls_strcat(vp, ": ");

	/* print the evaluated (path) solid parameters */
	if (illump->s_old.s_Eflag == 0) {
	    mat_t new_mat;
	    /* NOT an evaluated region */
	    /* object edit option selected */
	    bn_mat_mul(new_mat, MEDIT(s)->model_changes, MEDIT(s)->e_mat);

	    vls_solid(s, vp, &MEDIT(s)->es_int, new_mat);
	}
    }

    {
	char *start;
	char *p;
	int imax = 0;
	int i = 0;
	int j;
	struct bu_vls vls = BU_VLS_INIT_ZERO;

	start = bu_vls_addr(vp);
	/*
	 * Some display managers don't handle TABs properly, so
	 * we replace any TABs with spaces. Also, look for the
	 * maximum line length.
	 */
	for (p = start; *p != '\0'; ++p) {
	    if (*p == '\t')
		*p = ' ';
	    else if (*p == '\n') {
		if (i > imax)
		    imax = i;
		i = 0;
	    } else
		++i;
	}

	if (i > imax)
	    imax = i;

	/* Prep string for use with Tcl/Tk */
	++imax;
	i = 0;
	for (p = start; *p != '\0'; ++p) {
	    if (*p == '\n') {
		for (j = 0; j < imax - i; ++j)
		    bu_vls_putc(&vls, ' ');

		bu_vls_putc(&vls, *p);
		i = 0;
	    } else {
		bu_vls_putc(&vls, *p);
		++i;
	    }
	}

	Tcl_SetVar(s->interp, "edit_info", bu_vls_addr(&vls), TCL_GLOBAL_ONLY);
	bu_vls_free(&vls);
    }
}


/*
 * Output a vls string to the display manager,
 * as a text overlay on the graphics area (ugh).
 *
 * Set up for character output.  For the best generality, we
 * don't assume that the display can process a CRLF sequence,
 * so each line is written with a separate call to dm_draw_string_2d().
 */
void
screen_vls(
	struct mged_state *UNUSED(s),
	int UNUSED(xbase),
	int UNUSED(ybase),
	struct bu_vls *UNUSED(vp))
{
    /* Step 7.20: libdm removed — no-op. */
}


/*
 * Produce titles, etc., on the screen.
 * NOTE that this routine depends on being called AFTER dozoom();
 */
void
dotitles(struct mged_state *UNUSED(s), struct bu_vls *UNUSED(overlay_vls))
{
    /* Step 7.20: libdm removed — no-op. */
}


/* Stage 7: Update Tcl HUD display variables for an Obol pane.
 * This is the Obol equivalent of the Tcl-var-update portion of dotitles().
 * Called from refresh() when active_pane_set is non-empty and
 * obol_needs_refresh || do_time, using the mged_pane mp_*_name fields.
 * Physical drawing is handled by obol_notify_views instead. */
void
obol_update_title_vars(struct mged_state *s, struct mged_pane *mp)
{
    if (!mp || !mp->mp_gvp || !s->interp) return;
    /* Only update if the variable names have been populated. */
    if (!bu_vls_strlen(&mp->mp_aet_name)) return;

    struct bu_vls vls = BU_VLS_INIT_ZERO;
    struct bsg_camera _tc;
    bsg_view_get_camera(mp->mp_gvp, &_tc);

    bu_vls_printf(&vls, "az=%3.2f  el=%3.2f  tw=%3.2f", V3ARGS(_tc.aet));
    Tcl_SetVar(s->interp, bu_vls_cstr(&mp->mp_aet_name),
	       bu_vls_cstr(&vls), TCL_GLOBAL_ONLY);

    if (s->dbip != DBI_NULL) {
	char cent_x[32], cent_y[32], cent_z[32], size[32];
	float tmp;

	tmp = -_tc.center[MDX]*s->dbip->dbi_base2local;
	snprintf(cent_x, sizeof(cent_x), fabs(tmp) < 10e70 ? "%.3f" : "%.3g", tmp);
	tmp = -_tc.center[MDY]*s->dbip->dbi_base2local;
	snprintf(cent_y, sizeof(cent_y), fabs(tmp) < 10e70 ? "%.3f" : "%.3g", tmp);
	tmp = -_tc.center[MDZ]*s->dbip->dbi_base2local;
	snprintf(cent_z, sizeof(cent_z), fabs(tmp) < 10e70 ? "%.3f" : "%.3g", tmp);
	bu_vls_trunc(&vls, 0);
	bu_vls_printf(&vls, "cent=(%s %s %s)", cent_x, cent_y, cent_z);
	Tcl_SetVar(s->interp, bu_vls_cstr(&mp->mp_center_name),
		   bu_vls_cstr(&vls), TCL_GLOBAL_ONLY);

	tmp = mp->mp_gvp->gv_size*s->dbip->dbi_base2local;
	snprintf(size, sizeof(size), fabs(tmp) < 10e70 ? "sz=%.3f" : "sz=%.3g", tmp);
	Tcl_SetVar(s->interp, bu_vls_cstr(&mp->mp_size_name), size, TCL_GLOBAL_ONLY);
    }

    bu_vls_free(&vls);
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
