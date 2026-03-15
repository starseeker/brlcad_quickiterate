/*                           S E T . C
 * BRL-CAD
 *
 * Copyright (c) 1990-2025 United States Government as represented by
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
/** @file mged/set.c
 *
 */

#include "common.h"


#include "vmath.h"
#include "bsg/util.h"

#include "./sedit.h"
#include "./mged.h"
#include "./mged_dm.h"

#include "tcl.h"

/* external sp_hook functions */
extern void predictor_hook(const struct bu_structparse *, const char *, void *, const char *, void *);

/* exported sp_hook functions */
void set_perspective(const struct bu_structparse *, const char *, void *, const char *, void *);
void set_scroll_private(const struct bu_structparse *, const char *, void *, const char *, void *);

/* local sp_hook functions */
static void establish_perspective(const struct bu_structparse *, const char *, void *, const char *, void *);
static void nmg_eu_dist_set(const struct bu_structparse *, const char *, void *, const char *, void *);
static void set_coords(const struct bu_structparse *, const char *, void *, const char *, void *);
static void set_dirty_flag(const struct bu_structparse *, const char *, void *, const char *, void *);
static void set_dlist(const struct bu_structparse *, const char *, void *, const char *, void *);
static void set_rotate_about(const struct bu_structparse *, const char *, void *, const char *, void *);
static void toggle_perspective(const struct bu_structparse *, const char *, void *, const char *, void *);

static char *read_var(ClientData clientData, Tcl_Interp *interp, const char *name1, const char *name2, int flags);
static char *write_var(ClientData clientData, Tcl_Interp *interp, const char *name1, const char *name2, int flags);
static char *unset_var(ClientData clientData, Tcl_Interp *interp, const char *name1, const char *name2, int flags);

static int perspective_table[] = { 90, 30, 45, 60 };

struct _mged_variables default_mged_variables = {
    /* mv_rc */			1,
    /* mv_autosize */		1,
    /* mv_rateknobs */		0,
    /* mv_sliders */		0,
    /* mv_faceplate */		1,
    /* mv_orig_gui */		1,
    /* mv_linewidth */		1,
    /* mv_linestyle */		's',
    /* mv_hot_key */		0,
    /* mv_context */		1,
    /* mv_dlist */		0,
    /* mv_use_air */		0,
    /* mv_listen */		0,
    /* mv_port */		0,
    /* mv_struct fb */			0,
    /* mv_fb_all */		1,
    /* mv_fb_overlay */		0,
    /* mv_mouse_behavior */	'd',
    /* mv_coords */		'v',
    /* mv_rotate_about */	'v',
    /* mv_transform */		'v',
    /* mv_predictor */		0,
    /* mv_predictor_advance */	1.0,
    /* mv_predictor_length */	2.0,
    /* mv_perspective */	-1,
    /* mv_perspective_mode */	0,
    /* mv_toggle_perspective */	1,
    /* mv_nmg_eu_dist */	0.05,
    /* mv_eye_sep_dist */	0.0,
    /* mv_union lexeme */	"u",
    /* mv_intersection lexeme */"n",
    /* mv_difference lexeme */	"-",
};


#define MV_O(_m) bu_offsetof(struct _mged_variables, _m)
#define LINE RT_MAXLINE
struct bu_structparse mged_vparse[] = {
    {"%d", 1, "rc",			MV_O(mv_rc),			BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"%d", 1, "autosize",		MV_O(mv_autosize),		BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"%d", 1, "rateknobs",		MV_O(mv_rateknobs),		BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"%d", 1, "sliders",		MV_O(mv_sliders),		set_scroll_private, NULL, NULL },
    {"%d", 1, "faceplate",		MV_O(mv_faceplate),		set_dirty_flag, NULL, NULL },
    {"%d", 1, "orig_gui",		MV_O(mv_orig_gui),	        set_dirty_flag, NULL, NULL },
    {"%d", 1, "linewidth",		MV_O(mv_linewidth),		set_dirty_flag, NULL, NULL },
    {"%c", 1, "linestyle",		MV_O(mv_linestyle),		set_dirty_flag, NULL, NULL },
    {"%d", 1, "hot_key",		MV_O(mv_hot_key),		BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"%d", 1, "context",		MV_O(mv_context),		BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"%d", 1, "dlist",			MV_O(mv_dlist),			set_dlist, NULL, NULL },
    {"%d", 1, "use_air",		MV_O(mv_use_air),		BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"%d", 1, "listen",			MV_O(mv_listen),		fbserv_set_port, NULL, NULL },
    {"%d", 1, "port",			MV_O(mv_port),			fbserv_set_port, NULL, NULL },
    {"%d", 1, "fb",			MV_O(mv_fb),			set_dirty_flag, NULL, NULL },
    {"%d", 1, "fb_all",			MV_O(mv_fb_all),		set_dirty_flag, NULL, NULL },
    {"%d", 1, "fb_overlay",		MV_O(mv_fb_overlay),		set_dirty_flag, NULL, NULL },
    {"%c", 1, "mouse_behavior",		MV_O(mv_mouse_behavior),	BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"%c", 1, "coords",			MV_O(mv_coords),		set_coords, NULL, NULL },
    {"%c", 1, "rotate_about",		MV_O(mv_rotate_about),		set_rotate_about, NULL, NULL },
    {"%c", 1, "transform",		MV_O(mv_transform),		BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"%d", 1, "predictor",		MV_O(mv_predictor),		predictor_hook, NULL, NULL },
    {"%g", 1, "predictor_advance",	MV_O(mv_predictor_advance),	predictor_hook, NULL, NULL },
    {"%g", 1, "predictor_length",	MV_O(mv_predictor_length),	predictor_hook, NULL, NULL },
    {"%g", 1, "perspective",		MV_O(mv_perspective),		set_perspective, NULL, NULL },
    {"%d", 1, "perspective_mode",	MV_O(mv_perspective_mode),	establish_perspective, NULL, NULL },
    {"%d", 1, "toggle_perspective",	MV_O(mv_toggle_perspective),	toggle_perspective, NULL, NULL },
    {"%g", 1, "nmg_eu_dist",		MV_O(mv_nmg_eu_dist),		nmg_eu_dist_set, NULL, NULL },
    {"%g", 1, "eye_sep_dist",		MV_O(mv_eye_sep_dist),		set_dirty_flag, NULL, NULL },
    {"%s", LINE, "union_op",		MV_O(mv_union_lexeme),	        BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"%s", LINE, "intersection_op",	MV_O(mv_intersection_lexeme),   BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"%s", LINE, "difference_op",	MV_O(mv_difference_lexeme),	BU_STRUCTPARSE_FUNC_NULL, NULL, NULL },
    {"",   0, NULL,			0,				BU_STRUCTPARSE_FUNC_NULL, NULL, NULL }
};

static void
set_dirty_flag(const struct bu_structparse *UNUSED(sdp),
	       const char *UNUSED(name),
	       void *UNUSED(base),
	       const char *UNUSED(value),
	       void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    /* Stage 7: notify the Obol path via update_views. */
    s->update_views = 1;
    /* Step 6.b: use active_pane_set. */
    for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	if (!mp->mp_dmp) continue;  /* skip Obol panes */
	if (mp->mp_mged_variables == mged_variables) {
	    mp->mp_dirty = 1;
	    dm_set_dirty(mp->mp_dmp, 1);
	}
    }
}


static void
nmg_eu_dist_set(const struct bu_structparse *UNUSED(sdp),
		const char *UNUSED(name),
		void *UNUSED(base),
		const char *UNUSED(value),
		void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    struct bu_vls tmp_vls = BU_VLS_INIT_ZERO;

    nmg_eue_dist = mged_variables->mv_nmg_eu_dist;

    bu_vls_printf(&tmp_vls, "New nmg_eue_dist = %g\n", nmg_eue_dist);
    Tcl_AppendResult(s->interp, bu_vls_addr(&tmp_vls), (char *)NULL);
    bu_vls_free(&tmp_vls);
}


/**
 **
 **
 ** Callback used when an MGED variable is read with either the Tcl "set"
 ** command or the Tcl dereference operator '$'.
 **
 **/
static char *
read_var(ClientData clientData, Tcl_Interp *interp, const char *UNUSED(name1), const char *UNUSED(name2), int flags)
    /* Contains pointer to bu_struct_parse entry */


{
    struct mged_state *s = MGED_STATE;
    struct bu_structparse *sp = (struct bu_structparse *)clientData;
    struct bu_vls str = BU_VLS_INIT_ZERO;

    /* Ask the libbu structparser for the value of the variable */

    bu_vls_struct_item(&str, sp, (const char *)mged_variables, ' ');

    /* Next, set the Tcl variable to this value */
    (void)Tcl_SetVar(interp, sp->sp_name, bu_vls_addr(&str),
		     (flags&TCL_GLOBAL_ONLY)|TCL_LEAVE_ERR_MSG);

    bu_vls_free(&str);

    return NULL;
}


/**
 **
 **
 ** Callback used when an MGED variable is set with the Tcl "set" command.
 **
 **/

static char *
write_var(ClientData clientData, Tcl_Interp *interp, const char *name1, const char *name2, int flags)
{
    struct mged_state *s = MGED_STATE;
    struct bu_structparse *sp = (struct bu_structparse *)clientData;
    struct bu_vls str = BU_VLS_INIT_ZERO;
    const char *newvalue;

    newvalue = Tcl_GetVar(interp, sp->sp_name,
			  (flags&TCL_GLOBAL_ONLY)|TCL_LEAVE_ERR_MSG);
    bu_vls_printf(&str, "%s=\"%s\"", name1, newvalue);
    if (bu_struct_parse(&str, mged_vparse, (char *)mged_variables, MGED_STATE) < 0) {
	Tcl_AppendResult(interp, "ERROR OCCURRED WHEN SETTING ", name1,
			 " TO ", newvalue, "\n", (char *)NULL);
    }

    bu_vls_free(&str);
    return read_var(clientData, interp, name1, name2,
		    (flags&(~TCL_TRACE_WRITES))|TCL_TRACE_READS);
}


/**
 **
 **
 ** Callback used when an MGED variable is unset.  This function undoes that.
 **
 **/

static char *
unset_var(ClientData clientData, Tcl_Interp *interp, const char *name1, const char *name2, int flags)
{
    struct bu_structparse *sp = (struct bu_structparse *)clientData;

    if (flags & TCL_INTERP_DESTROYED)
	return NULL;

    Tcl_AppendResult(interp, "mged variables cannot be unset\n", (char *)NULL);
    Tcl_TraceVar(interp, sp->sp_name, TCL_TRACE_READS,
		 (Tcl_VarTraceProc *)read_var,
		 (ClientData)sp);
    Tcl_TraceVar(interp, sp->sp_name, TCL_TRACE_WRITES,
		 (Tcl_VarTraceProc *)write_var,
		 (ClientData)sp);
    Tcl_TraceVar(interp, sp->sp_name, TCL_TRACE_UNSETS,
		 (Tcl_VarTraceProc *)unset_var,
		 (ClientData)sp);
    read_var(clientData, interp, name1, name2,
	     (flags&(~TCL_TRACE_UNSETS))|TCL_TRACE_READS);
    return NULL;
}


/**
 **
 **
 ** Sets the variable traces for each of the MGED variables so they can be
 ** accessed with the Tcl "set" and "$" operators.
 **
 **/

void
mged_variable_setup(struct mged_state *s)
{
    Tcl_Interp *interp = s->interp;
    struct bu_structparse *sp;

    for (sp = &mged_vparse[0]; sp->sp_name != NULL; sp++) {
	read_var((ClientData)sp, interp, sp->sp_name, (char *)NULL, 0);
	Tcl_TraceVar(interp, sp->sp_name, TCL_TRACE_READS|TCL_GLOBAL_ONLY,
		     (Tcl_VarTraceProc *)read_var, (ClientData)sp);
	Tcl_TraceVar(interp, sp->sp_name, TCL_TRACE_WRITES|TCL_GLOBAL_ONLY,
		     (Tcl_VarTraceProc *)write_var, (ClientData)sp);
	Tcl_TraceVar(interp, sp->sp_name, TCL_TRACE_UNSETS|TCL_GLOBAL_ONLY,
		     (Tcl_VarTraceProc *)unset_var, (ClientData)sp);
    }
}


int
f_set(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    struct bu_vls vls = BU_VLS_INIT_ZERO;

    if (argc < 1 || 2 < argc) {
	bu_vls_printf(&vls, "help vars");
	Tcl_Eval(interp, bu_vls_addr(&vls));
	bu_vls_free(&vls);

	return TCL_ERROR;
    }

    mged_vls_struct_parse_old(s, &vls, "mged variables", mged_vparse,
			      (char *)mged_variables, argc, argv);
    Tcl_AppendResult(interp, bu_vls_addr(&vls), (char *)NULL);
    bu_vls_free(&vls);

    return TCL_OK;
}


void
set_scroll_private(const struct bu_structparse *UNUSED(sdp),
		   const char *UNUSED(name),
		   void *UNUSED(base),
		   const char *UNUSED(value),
		   void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    struct mged_pane *save_pane = s->mged_curr_pane;
    /* Step 7.7: compare via pane's mp_mged_variables (works for both Obol and
     * legacy dm wrapper panes; mged_curr_pane->mp_mged_variables is always valid). */
    struct _mged_variables *save_mv = save_pane->mp_mged_variables;

    /* Step 6.b: use active_pane_set. */
    for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	if (!mp->mp_dmp) continue;  /* skip Obol panes */
	if (mp->mp_mged_variables == save_mv) {
	    set_curr_pane(s, mp);

	    if (mged_variables->mv_faceplate && mged_variables->mv_orig_gui) {
		if (mged_variables->mv_sliders)	/* zero slider variables */
		    mged_svbase(s);

		set_scroll(s);		/* set scroll_array for drawing the scroll bars */
		DMP_dirty = 1;
		dm_set_dirty(DMP, 1);
	    }
	}
    }

    set_curr_pane(s, save_pane);
}


void
set_absolute_tran(struct mged_state *s)
{
    /* calculate absolute_tran */
    set_absolute_view_tran(s);

    /* calculate absolute_model_tran */
    set_absolute_model_tran(s);
}


void
set_absolute_view_tran(struct mged_state *s)
{
    struct bsg_camera _cam;
    bsg_view_get_camera(view_state->vs_gvp, &_cam);
    /* calculate absolute_tran */
    MAT4X3PNT(view_state->k.tra_v_abs, _cam.model2view, view_state->vs_orig_pos);
    /* This is used in f_knob()  ---- needed in case absolute_tran is set from Tcl */
    VMOVE(view_state->k.tra_v_abs_last, view_state->k.tra_v_abs);
}


void
set_absolute_model_tran(struct mged_state *s)
{
    point_t new_pos;
    point_t diff;
    struct bsg_camera _cam;

    bsg_view_get_camera(view_state->vs_gvp, &_cam);
    /* calculate absolute_model_tran */
    MAT_DELTAS_GET_NEG(new_pos, _cam.center);
    VSUB2(diff, view_state->vs_orig_pos, new_pos);
    VSCALE(view_state->k.tra_m_abs, diff, 1/view_state->vs_gvp->gv_scale);
    /* This is used in f_knob()  ---- needed in case absolute_model_tran is set from Tcl */
    VMOVE(view_state->k.tra_m_abs_last, view_state->k.tra_m_abs);
}


static void
set_dlist(const struct bu_structparse *UNUSED(sdp),
	  const char *UNUSED(name),
	  void *UNUSED(base),
	  const char *UNUSED(value),
	  void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    struct mged_pane *save_pane = s->mged_curr_pane;
    /* Step 7.7: compare via pane's mp_mged_variables instead of mged_curr_dm. */
    struct _mged_variables *save_mv = save_pane->mp_mged_variables;

    if (mged_variables->mv_dlist) {
	/* create display lists */

	/* Step 6.b: for each wrapper pane that shares mged_variables with save_pane */
	for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	    struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	    if (!mp->mp_dmp) continue;  /* skip Obol panes */

	    if (mp->mp_mged_variables != save_mv)
		continue;

	    if (dm_get_displaylist(mp->mp_dmp) &&
		mp->mp_dlist_state->dl_active == 0) {
		set_curr_pane(s, mp);
		createDListAll((void *)s, NULL);
		mp->mp_dlist_state->dl_active = 1;
		mp->mp_dirty = 1;
		dm_set_dirty(mp->mp_dmp, 1);
	    }
	}
    } else {
	/*
	 * Free display lists if not being used by another display manager
	 */

	/* Step 6.b: for each wrapper pane that shares mged_variables with save_pane */
	for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	    struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);

	    if (!mp->mp_dmp) continue;  /* skip Obol panes */
	    if (mp->mp_mged_variables != save_mv)
		continue;

	    if (mp->mp_dlist_state->dl_active) {
		/* for each wrapper pane mp2 that is sharing display lists with mp */
		struct mged_pane *mp2 = MGED_PANE_NULL;
		for (size_t pj = 0; pj < BU_PTBL_LEN(&active_pane_set); pj++) {
		    struct mged_pane *m2 = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pj);

		    if (!m2->mp_dmp) continue;  /* skip Obol panes */
		    if (m2->mp_dlist_state != mp->mp_dlist_state)
			continue;

		    /* found mp2 that is actively using mp's display lists */
		    if (mp2 && mp2->mp_mged_variables->mv_dlist) {
			mp2 = m2;
			break;
		    }
		}

		/* these display lists are not being used, so free them */
		if (mp2 == MGED_PANE_NULL) {
		    mp->mp_dlist_state->dl_active = 0;

		    /* Free each shape's display list individually via scene-root children */
		    bsg_shape *root = bsg_scene_root_get(view_state->vs_gvp);
		    size_t nshapes = root ? BU_PTBL_LEN(&root->children) : 0;
		    (void)dm_make_current(mp->mp_dmp);
		    for (size_t si = 0; si < nshapes; si++) {
			bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, si);
			if (sp->s_dlist) {
			    (void)dm_free_dlists(mp->mp_dmp, sp->s_dlist, 1);
			    sp->s_dlist = 0;
			}
		    }
		}
	    }
	}
    }

    set_curr_pane(s, save_pane);
}


extern void
set_perspective(const struct bu_structparse *sdp,
		const char *name,
		void *base,
		const char *value,
		void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    /* if perspective is set to something greater than 0, turn perspective mode on */
    if (mged_variables->mv_perspective > 0)
	mged_variables->mv_perspective_mode = 1;
    else
	mged_variables->mv_perspective_mode = 0;

    /* keep view object in sync */
    {
	struct bsg_camera _sp; bsg_view_get_camera(view_state->vs_gvp, &_sp);
	_sp.perspective = mged_variables->mv_perspective;
	bsg_view_set_camera(view_state->vs_gvp, &_sp);
    }

    /* keep display manager in sync */
    if (DMP) dm_set_perspective(DMP, mged_variables->mv_perspective_mode);

    set_dirty_flag(sdp, name, base, value, data);
}


static void
establish_perspective(const struct bu_structparse *sdp,
		      const char *name,
		      void *base,
		      const char *value,
		      void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    mged_variables->mv_perspective = mged_variables->mv_perspective_mode ?
	perspective_table[perspective_angle] : -1;

    /* keep view object in sync */
    {
	struct bsg_camera _sp; bsg_view_get_camera(view_state->vs_gvp, &_sp);
	_sp.perspective = mged_variables->mv_perspective;
	bsg_view_set_camera(view_state->vs_gvp, &_sp);
    }

    /* keep display manager in sync */
    if (DMP) dm_set_perspective(DMP, mged_variables->mv_perspective_mode);

    set_dirty_flag(sdp, name, base, value, data);
}


/*
  This routine toggles the perspective_angle if the
  toggle_perspective value is 0 or less. Otherwise, the
  perspective_angle is set to the value of (toggle_perspective - 1).
*/
static void
toggle_perspective(const struct bu_structparse *sdp,
		   const char *name,
		   void *base,
		   const char *value,
		   void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
     /* set perspective matrix */
    if (mged_variables->mv_toggle_perspective > 0)
	perspective_angle = mged_variables->mv_toggle_perspective <= 4 ?
	    mged_variables->mv_toggle_perspective - 1: 3;
    else if (--perspective_angle < 0) /* toggle perspective matrix */
	perspective_angle = 3;

    /*
      Just in case the "!" is used with the set command. This
      allows us to toggle through more than two values.
    */
    mged_variables->mv_toggle_perspective = 1;

    if (!mged_variables->mv_perspective_mode)
	return;

    mged_variables->mv_perspective = perspective_table[perspective_angle];

    /* keep view object in sync */
    {
	struct bsg_camera _sp; bsg_view_get_camera(view_state->vs_gvp, &_sp);
	_sp.perspective = mged_variables->mv_perspective;
	bsg_view_set_camera(view_state->vs_gvp, &_sp);
    }

    /* keep display manager in sync */
    if (DMP) dm_set_perspective(DMP, mged_variables->mv_perspective_mode);

    set_dirty_flag(sdp, name, base, value, data);
}


static void
set_coords(const struct bu_structparse *UNUSED(sdp),
	   const char *UNUSED(name),
	   void *UNUSED(base),
	   const char *UNUSED(value),
	   void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    {
	struct bsg_camera _sc; bsg_view_get_camera(view_state->vs_gvp, &_sc);
	_sc.coord = mged_variables->mv_coords;
	bsg_view_set_camera(view_state->vs_gvp, &_sc);
    }
}


static void
set_rotate_about(const struct bu_structparse *UNUSED(sdp),
		 const char *UNUSED(name),
		 void *UNUSED(base),
		 const char *UNUSED(value),
		 void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    {
	struct bsg_camera _sr; bsg_view_get_camera(view_state->vs_gvp, &_sr);
	_sr.rotate_about = mged_variables->mv_rotate_about;
	bsg_view_set_camera(view_state->vs_gvp, &_sr);
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
