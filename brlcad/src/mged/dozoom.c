/*                        D O Z O O M . C
 * BRL-CAD
 *
 * Copyright (c) 1985-2026 United States Government as represented by
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
/** @file mged/dozoom.c
 *
 * Phase 5 (drawing_stack_modernization): MGED scene rendering via the BSG
 * draw path.
 *
 * The non-stereo rendering path (which_eye == 0) now delegates entirely to
 * dm_draw_objs(), which uses bsg_view_traverse() when a BSG scene root is
 * present.  Edit-mode objects (s_iflag == UP) are rendered at their
 * edited position by setting view_state->vs_gvp->gv_edit_mat to point at
 * vs_model2objview before the draw call; dm_draw_scene_obj() picks this up
 * and swaps the modelview matrix for the duration of each such object.
 *
 * The stereo path (which_eye != 0) retains the legacy dm_draw_head_dl()
 * implementation: stereo uses a combined perspective + eye-offset matrix that
 * cannot be expressed cleanly through the separate dm_loadpmatrix / modelview
 * split that dm_draw_objs() expects.  It is left as a known TODO for a
 * follow-on phase.
 */

#include "common.h"

#include <math.h>
#include "vmath.h"
#include "bn.h"
#include "bsg/util.h"
#include "dm/view.h"

#include "./mged.h"
#include "./sedit.h"
#include "./mged_dm.h"

mat_t perspective_mat;
mat_t identity;

/* Assumed physical screen width in mm (stereo eye-separation calculation) */
#ifndef SCR_WIDTH_PHYS
#  define SCR_WIDTH_PHYS 330
#endif

/* This is a holding place for the current display managers default wireframe color */
unsigned char geometry_default_color[] = { 255, 0, 0 };

/*
 * Paint one eye of the scene.
 *
 * which_eye == 0  Normal (non-stereo) view — BSG path.
 * which_eye == 1  Stereo right eye — legacy dl_* path.
 * which_eye == 2  Stereo left eye  — legacy dl_* path.
 */
void
dozoom(struct mged_state *s, int which_eye)
{
    struct bview *v = view_state->vs_gvp;

    /*
     * The vectorThreshold stuff in libdm may turn the
     * Tcl-crank causing s->mged_curr_dm to change.
     */
    struct mged_dm *save_dm_list = s->mged_curr_dm;

    s->mged_curr_dm->dm_ndrawn = 0;

    /* Keep v->dmp in sync with the active display manager so that
     * dm_draw_objs() can find the DM.  This must be done every frame
     * because set_curr_dm() (called from refresh()) updates
     * s->mged_curr_dm without updating the view's dmp pointer. */
    v->dmp = (void *)DMP;

    /* ------------------------------------------------------------------
     * Non-stereo path: clean BSG rendering via dm_draw_objs().
     * ------------------------------------------------------------------ */
    if (which_eye == 0) {
	/* Ensure gv_pmat is current.  The GED "perspective" command normally
	 * keeps this in sync, but if the shear-perspective (gv_eye_pos[Z] !=
	 * 1.0) mode is in use we recompute it here. */
	if (v->gv_perspective >= SMALL_FASTF) {
	    if (!EQUAL(v->gv_eye_pos[Z], 1.0)) {
		point_t l, h;
		VSET(l, -1.0, -1.0, -1.0);
		VSET(h,  1.0,  1.0, 200.0);
		deering_persp_mat(v->gv_pmat, l, h, v->gv_eye_pos);
		/* Keep the file-scope copy for titles.c which reads it. */
		MAT_COPY(perspective_mat, v->gv_pmat);
	    } else {
		persp_mat(perspective_mat, v->gv_perspective,
			  (fastf_t)1.0f, (fastf_t)0.01f,
			  (fastf_t)1.0e10f, (fastf_t)1.0f);
		MAT_COPY(v->gv_pmat, perspective_mat);
	    }
	}

	/* Expose the edit-mode matrix on the view so dm_draw_scene_obj()
	 * can use it for illuminated objects without a second render pass. */
	if (s->global_editing_state != ST_VIEW)
	    v->gv_edit_mat = view_state->vs_model2objview;
	else
	    v->gv_edit_mat = NULL;

	/* dm_draw_objs() handles:
	 *   - framebuffer overlay/underlay
	 *   - dm_loadmatrix(gv_model2view)
	 *   - dm_loadpmatrix(gv_pmat) for perspective
	 *   - bsg_scene_root_sync + bsg_view_traverse (BSG path, since
	 *     setup.c has called bsg_scene_root_create for this view)
	 *   - per-object edit matrix swap for s_iflag == UP objects
	 */
	dm_draw_objs(v, NULL, NULL);

	/* draw predictor vlist */
	if (mged_variables->mv_predictor) {
	    dm_set_fg(DMP,
		      color_scheme->cs_predictor[0],
		      color_scheme->cs_predictor[1],
		      color_scheme->cs_predictor[2], 1, 1.0);
	    dm_draw_vlist(DMP, (struct bv_vlist *)&s->mged_curr_dm->dm_p_vlist);
	}

	/* Clear the edit-mat pointer now that the frame is done. */
	v->gv_edit_mat = NULL;

	/* Count drawn objects for usepen.c zone-based picking.  All objects
	 * whose s_flag is UP after the traversal were actually rendered. */
	if (v->bsg_root) {
	    struct bv_scene_obj *root = (struct bv_scene_obj *)v->bsg_root;
	    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
		struct bv_scene_obj *sp =
		    (struct bv_scene_obj *)BU_PTBL_GET(&root->children, i);
		if (sp && sp->s_flag == UP)
		    s->mged_curr_dm->dm_ndrawn++;
	    }
	}

	if (s->mged_curr_dm != save_dm_list) set_curr_dm(s, save_dm_list);
	return;
    }

    /* ------------------------------------------------------------------
     * Stereo path (which_eye == 1 or 2): legacy dm_draw_head_dl().
     *
     * Stereo uses a combined Deering perspective + eye-offset matrix that
     * cannot be separated into the dm_loadpmatrix / modelview split that
     * dm_draw_objs() expects.  TODO: revisit in a follow-on phase.
     * ------------------------------------------------------------------ */
    {
	int ndrawn = 0;
	fastf_t inv_viewsize = v->gv_isize;
	mat_t newmat = MAT_INIT_ZERO;
	matp_t mat = newmat;
	short r = -1, g = -1, b = -1;

	fastf_t to_eye_scr = 1 / tan(v->gv_perspective * DEG2RAD * 0.5);
	fastf_t eye_delta_scr = mged_variables->mv_eye_sep_dist * 0.5 / SCR_WIDTH_PHYS;
	point_t l, h, eye;
	VSET(l, -1.0, -1.0, -1.0);
	VSET(h,  1.0,  1.0, 200.0);
	VSET(eye, 0.0, 0.0, to_eye_scr);

	if (which_eye == 1) {
	    eye[X] = eye_delta_scr;
	    printf("d=%gscr, d=%gmm, delta=%gscr\n",
		   to_eye_scr, to_eye_scr * SCR_WIDTH_PHYS, eye_delta_scr);
	    VPRINT("l", l); VPRINT("h", h);
	} else {
	    eye[X] = -eye_delta_scr;
	}
	deering_persp_mat(perspective_mat, l, h, eye);
	mat = v->gv_model2view;
	bn_mat_mul(newmat, perspective_mat, mat);
	mat = newmat;

	dm_loadmatrix(DMP, mat, which_eye);

	if (dm_get_transparency(DMP)) {
	    ndrawn = dm_draw_head_dl(DMP, (struct bu_list *)ged_dl(s->gedp),
				    1.0, inv_viewsize, r, g, b,
				    mged_variables->mv_linewidth,
				    mged_variables->mv_dlist, 0,
				    geometry_default_color, 1,
				    mged_variables->mv_dlist);
	    if (s->mged_curr_dm != save_dm_list) set_curr_dm(s, save_dm_list);
	    s->mged_curr_dm->dm_ndrawn += ndrawn;
	    dm_set_depth_mask(DMP, 0);
	    ndrawn = dm_draw_head_dl(DMP, (struct bu_list *)ged_dl(s->gedp),
				    0.0, inv_viewsize, r, g, b,
				    mged_variables->mv_linewidth,
				    mged_variables->mv_dlist, 0,
				    geometry_default_color, 0,
				    mged_variables->mv_dlist);
	    dm_set_depth_mask(DMP, 1);
	} else {
	    ndrawn = dm_draw_head_dl(DMP, (struct bu_list *)ged_dl(s->gedp),
				    1.0, inv_viewsize, r, g, b,
				    mged_variables->mv_linewidth,
				    mged_variables->mv_dlist, 0,
				    geometry_default_color, 1,
				    mged_variables->mv_dlist);
	}
	if (s->mged_curr_dm != save_dm_list) set_curr_dm(s, save_dm_list);
	s->mged_curr_dm->dm_ndrawn += ndrawn;

	if (mged_variables->mv_predictor) {
	    dm_set_fg(DMP,
		      color_scheme->cs_predictor[0],
		      color_scheme->cs_predictor[1],
		      color_scheme->cs_predictor[2], 1, 1.0);
	    dm_draw_vlist(DMP, (struct bv_vlist *)&s->mged_curr_dm->dm_p_vlist);
	}

	if (s->global_editing_state == ST_VIEW)
	    return;

	if (v->gv_perspective <= 0) {
	    mat = view_state->vs_model2objview;
	} else {
	    bn_mat_mul(newmat, perspective_mat, view_state->vs_model2objview);
	    mat = newmat;
	}
	dm_loadmatrix(DMP, mat, which_eye);
	inv_viewsize /= MEDIT(s)->model_changes[15];
	dm_set_fg(DMP,
		  color_scheme->cs_geo_hl[0],
		  color_scheme->cs_geo_hl[1],
		  color_scheme->cs_geo_hl[2], 1, 1.0);
	ndrawn = dm_draw_head_dl(DMP, (struct bu_list *)ged_dl(s->gedp),
				 1.0, inv_viewsize, r, g, b,
				 mged_variables->mv_linewidth,
				 mged_variables->mv_dlist, 1,
				 geometry_default_color, 0,
				 mged_variables->mv_dlist);
	s->mged_curr_dm->dm_ndrawn += ndrawn;
	if (s->mged_curr_dm != save_dm_list) set_curr_dm(s, save_dm_list);
    }
}

/*
 * Create Display Lists
 */
void
createDLists(void *data, struct bu_list *hdlp)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    struct display_list *gdlp;
    struct display_list *next_gdlp;

    gdlp = BU_LIST_NEXT(display_list, hdlp);
    while (BU_LIST_NOT_HEAD(gdlp, hdlp)) {
	next_gdlp = BU_LIST_PNEXT(display_list, gdlp);

	dm_set_dirty(DMP, 1);
	dm_draw_display_list(DMP, gdlp);

	gdlp = next_gdlp;
    }
}

/*
 * Create a display list for "sp" for every display manager
 * manager that:
 * 1 - supports display lists
 * 2 - is actively using display lists
 * 3 - has not already been created (i.e. sharing with a
 * display manager that has already created the display list)
 */
void
createDListSolid(void *vlist_ctx, struct bv_scene_obj *sp)
{
    struct mged_state *s = (struct mged_state *)vlist_ctx;
    MGED_CK_STATE(s);
    struct mged_dm *save_dlp;

    save_dlp = s->mged_curr_dm;

    for (size_t di = 0; di < BU_PTBL_LEN(&active_dm_set); di++) {
	struct mged_dm *dlp = (struct mged_dm *)BU_PTBL_GET(&active_dm_set, di);
	if (dlp->dm_mapped &&
		dm_get_displaylist(dlp->dm_dmp) &&
		dlp->dm_mged_variables->mv_dlist) {
	    if (sp->s_dlist == 0)
		sp->s_dlist = dm_gen_dlists(DMP, 1);

	    dm_set_dirty(DMP, 1);
	    (void)dm_make_current(DMP);
	    (void)dm_begin_dlist(DMP, sp->s_dlist);
	    if (sp->s_iflag == UP)
		(void)dm_set_fg(DMP, 255, 255, 255, 0, sp->s_os->transparency);
	    else
		(void)dm_set_fg(DMP,
			(unsigned char)sp->s_color[0],
			(unsigned char)sp->s_color[1],
			(unsigned char)sp->s_color[2], 0, sp->s_os->transparency);
	    (void)dm_draw_vlist(DMP, (struct bv_vlist *)&sp->s_vlist);
	    (void)dm_end_dlist(DMP);
	}

	dlp->dm_dirty = 1;
	dm_set_dirty(DMP, 1);
    }

    set_curr_dm(s, save_dlp);
}

/*
 * Create a display list for "sp" for every display manager
 * manager that:
 * 1 - supports display lists
 * 2 - is actively using display lists
 * 3 - has not already been created (i.e. sharing with a
 * display manager that has already created the display list)
 */
void
createDListAll(void *vlist_ctx, struct display_list *gdlp)
{
    struct mged_state *s = (struct mged_state *)vlist_ctx;
    MGED_CK_STATE(s);
    struct bv_scene_obj *sp;
    for (BU_LIST_FOR(sp, bv_scene_obj, &gdlp->dl_head_scene_obj)) {
	createDListSolid(s, sp);
    }
}


/*
 * Free the range of display lists for all display managers
 * that support display lists and have them activated.
 */
void
freeDListsAll(void *data, unsigned int dlist, int range)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    for (size_t di = 0; di < BU_PTBL_LEN(&active_dm_set); di++) {
	struct mged_dm *dlp = (struct mged_dm *)BU_PTBL_GET(&active_dm_set, di);
	if (dm_get_displaylist(dlp->dm_dmp) &&
	    dlp->dm_mged_variables->mv_dlist) {
	    (void)dm_make_current(DMP);
	    (void)dm_free_dlists(dlp->dm_dmp, dlist, range);
	}

	dlp->dm_dirty = 1;
	dm_set_dirty(DMP, 1);
    }
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

