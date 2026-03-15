/*                        A T T A C H . C
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
/** @file mged/attach.c
 *
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>		/* for struct timeval */
#endif

#include "tcl.h"
#ifdef HAVE_TK
#  include "tk.h"
#endif

#include "bnetwork.h"

/* Make sure this comes after bio.h (for Windows) */
#ifdef HAVE_GL_GL_H
#  include <GL/gl.h>
#endif

#include "vmath.h"
#include "bu/env.h"
#include "bu/ptbl.h"
#include "bsg/util.h"
#include "ged.h"
#include "tclcad.h"

#include "./mged.h"
#include "./sedit.h"
#include "./mged_dm.h"

// FIXME: Globals
/* Geometry display instances used by MGED */
/* Step 7.18: mged_dm_init_state removed; startup sentinel is s->mged_init_pane. */

/* Stage 7 (libdm removal): Obol pane set — tracks mged_pane objects for
 * all panes (Obol and legacy dm wrappers).  See RADICAL_MIGRATION.md. */
struct bu_ptbl active_pane_set = BU_PTBL_INIT_ZERO;


extern struct _color_scheme default_color_scheme;
extern struct _rubber_band default_rubber_band;
extern struct _mged_variables default_mged_variables;
extern struct bsg_grid_state default_grid_state;
extern struct _axes_state default_axes_state;
extern void share_dlist(struct mged_pane *dlp2);	/* defined in share.c */
int mged_default_dlist = 0;   /* This variable is available via Tcl for controlling use of display lists */

/* Step 7.19: windowbounds removed — dm_set_win_bounds() no longer called. */

/**
 * set_curr_pane — make a mged_pane the active view source.
 *
 * Stage 7 (libdm removal): this is the primary pane-switching function.
 * It sets s->gedp->ged_gvp to the pane's bsg_view.
 *
 * Step 7.10: mged_curr_dm removed from mged_state.  DMP is now a ternary
 * expression through mged_curr_pane->mp_dm so no redirect is needed here.
 * Lifecycle code (release, mged_attach) works directly with mged_dm
 * pointers obtained from the pane's mp_dm field.
 */
void
set_curr_pane(struct mged_state *s, struct mged_pane *mp)
{
    if (!s || !s->gedp)
	return;
    s->mged_curr_pane = mp;
    if (!mp) {
	s->gedp->ged_gvp = NULL;
	return;
    }
    /* Step 7.2: init_pane has mp_gvp == NULL (no view yet); don't clobber
     * ged_gvp when restoring to the init sentinel pane. */
    if (mp->mp_gvp)
	s->gedp->ged_gvp = mp->mp_gvp;
}

/**
 * mged_pane_find_by_name — look up a pane by its gv_name.
 *
 * Searches active_pane_set for a mged_pane whose mp_gvp->gv_name matches
 * `name`.  Returns MGED_PANE_NULL if not found.
 *
 * Step 6.a: active_pane_set now contains BOTH Obol panes (mp_dm == NULL,
 * gv_name = Tk widget path) and legacy dm wrapper panes (mp_dm != NULL,
 * gv_name = dm pathname).  This function finds both.  Used by f_winset
 * and any code that needs to resolve a pane name to a mged_pane.
 */
struct mged_pane *
mged_pane_find_by_name(const char *name)
{
    if (!name)
	return MGED_PANE_NULL;
    for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	if (mp && mp->mp_gvp &&
	    BU_STR_EQUAL(name, bu_vls_cstr(&mp->mp_gvp->gv_name)))
	    return mp;
    }
    return MGED_PANE_NULL;
}

/**
 * mged_pane_release — remove a pane from active_pane_set and free it.
 *
 * Call this when the corresponding obol_view Tk widget is destroyed.
 * The bsg_view (mp->mp_gvp) is owned by ged_free_views and must be freed
 * separately through the GED view-teardown path.
 */
void
mged_pane_release(struct mged_pane *mp)
{
    if (!mp)
	return;
    bu_ptbl_rm(&active_pane_set, (long *)mp);
    /* Step 5.15: free the predictor vlist.  The vlist entries are rt_vlfree
     * entries (same pool used by mged); BSG_FREE_VLIST uses rt_vlfree. */
    BSG_FREE_VLIST(&rt_vlfree, &mp->mp_p_vlist);
    mged_pane_free_resources(mp);
    BU_PUT(mp, struct mged_pane);
}

/*
 * mged_pane_init_resources — allocate and initialise per-pane overlay state.
 *
 * Called for both legacy dm panes (from mged_attach) and Obol panes (from
 * f_new_obol_view_ptr).  Step 7.18: single unified path; no mp_dm branching.
 * Copies initial resource values from s->mged_curr_pane if available, or from
 * s->mged_init_pane as fallback, or from hardcoded defaults.
 */
void
mged_pane_init_resources(struct mged_state *s, struct mged_pane *mp)
{
    if (!mp)
	return;

    /* Source for copying initial resource values. */
    struct mged_pane *src = (s && s->mged_curr_pane) ? s->mged_curr_pane : NULL;
    /* Fallback: the sentinel init_pane (created before any dm/obol attaches). */
    struct mged_pane *init_src = (s && s->mged_init_pane && s->mged_init_pane != mp)
				 ? s->mged_init_pane : NULL;
    if (!src && init_src) src = init_src;

    /* Step 7.20: libdm fields (mp_dmp/mp_fbp/mp_netfd/mp_dirty/mp_mapped)
     * removed from mged_pane — no init needed. */

    BU_ALLOC(mp->mp_adc_state, struct _adc_state);
    if (src && src->mp_adc_state)
	*mp->mp_adc_state = *src->mp_adc_state;
    else { mp->mp_adc_state->adc_a1 = mp->mp_adc_state->adc_a2 = 45.0; }
    mp->mp_adc_state->adc_rc = 1;

    BU_ALLOC(mp->mp_menu_state, struct _menu_state);
    if (src && src->mp_menu_state)
	*mp->mp_menu_state = *src->mp_menu_state;
    mp->mp_menu_state->ms_rc = 1;

    BU_ALLOC(mp->mp_rubber_band, struct _rubber_band);
    if (src && src->mp_rubber_band)
	*mp->mp_rubber_band = *src->mp_rubber_band;
    else
	*mp->mp_rubber_band = default_rubber_band;
    mp->mp_rubber_band->rb_rc = 1;

    BU_ALLOC(mp->mp_mged_variables, struct _mged_variables);
    if (src && src->mp_mged_variables)
	*mp->mp_mged_variables = *src->mp_mged_variables;
    else
	*mp->mp_mged_variables = default_mged_variables;
    mp->mp_mged_variables->mv_rc     = 1;
    /* Step 7.19: libdm display-list path removed; mv_dlist is always 0. */
    mp->mp_mged_variables->mv_dlist  = 0;
    mp->mp_mged_variables->mv_listen = 0;
    mp->mp_mged_variables->mv_port   = 0;
    mp->mp_mged_variables->mv_fb     = 0;

    BU_ALLOC(mp->mp_color_scheme, struct _color_scheme);
    if (src && src->mp_color_scheme)
	*mp->mp_color_scheme = *src->mp_color_scheme;
    else
	*mp->mp_color_scheme = default_color_scheme;
    mp->mp_color_scheme->cs_rc = 1;

    BU_ALLOC(mp->mp_grid_state, struct bsg_grid_state);
    if (src && src->mp_grid_state)
	*mp->mp_grid_state = *src->mp_grid_state;
    else
	*mp->mp_grid_state = default_grid_state;
    mp->mp_grid_state->rc = 1;

    BU_ALLOC(mp->mp_axes_state, struct _axes_state);
    if (src && src->mp_axes_state)
	*mp->mp_axes_state = *src->mp_axes_state;
    else
	*mp->mp_axes_state = default_axes_state;
    mp->mp_axes_state->ax_rc = 1;

    BU_ALLOC(mp->mp_dlist_state, struct _dlist_state);
    mp->mp_dlist_state->dl_rc = 1;

    /* view_state:
     * - sentinel (mp->mp_gvp == NULL): allocate shell with NULL vs_gvp.
     * - Obol pane (mp->mp_gvp != NULL): link to the live bsg_view.
     * Step 7.19: dm-attach path removed; the sentinel case is the only case
     * where mp_gvp == NULL here. */
    BU_ALLOC(mp->mp_view_state, struct _view_state);
    mp->mp_view_state->vs_rc  = 1;
    mp->mp_view_state->vs_gvp = mp->mp_gvp;
    view_ring_init(mp->mp_view_state, (struct _view_state *)NULL);
    if (src && src->mp_view_state) {
	MAT_COPY(mp->mp_view_state->vs_model2objview,
		 src->mp_view_state->vs_model2objview);
	MAT_COPY(mp->mp_view_state->vs_objview2model,
		 src->mp_view_state->vs_objview2model);
	MAT_COPY(mp->mp_view_state->vs_ModelDelta,
		 src->mp_view_state->vs_ModelDelta);
    }

    /* HUD Tcl variable name storage. */
    bu_vls_init(&mp->mp_fps_name);
    bu_vls_init(&mp->mp_aet_name);
    bu_vls_init(&mp->mp_ang_name);
    bu_vls_init(&mp->mp_center_name);
    bu_vls_init(&mp->mp_size_name);
    bu_vls_init(&mp->mp_adc_name);

    /* Predictor vlist and trails. */
    BU_LIST_INIT(&mp->mp_p_vlist);
    predictor_init_pane(mp);
    mp->mp_ndrawn = 0;

    /* Step 7.15 scalar state. */
    mp->mp_am_mode          = AMM_IDLE;
    mp->mp_perspective_angle = 0;
    mp->mp_adc_auto         = 1;
    mp->mp_grid_auto_size   = 1;
    /* mouse/knob/scroll fields zero-init via BU_GET. */
}

/*
 * mged_pane_free_resources — free per-pane overlay state.
 *
 * Safe to call even if mged_pane_init_resources was never called (all
 * pointers default to NULL from BU_GET).
 * Step 7.18: unified path (no mp_dm branching).
 * Note: mp_dmp is NOT freed here — caller must call dm_close().
 *       mp_fbp is NOT freed here — caller must call fb_close_existing().
 */
void
mged_pane_free_resources(struct mged_pane *mp)
{
    if (!mp)
	return;

    if (mp->mp_adc_state && !--mp->mp_adc_state->adc_rc)
	bu_free(mp->mp_adc_state, "mp_adc_state");
    mp->mp_adc_state = NULL;

    if (mp->mp_menu_state && !--mp->mp_menu_state->ms_rc)
	bu_free(mp->mp_menu_state, "mp_menu_state");
    mp->mp_menu_state = NULL;

    if (mp->mp_rubber_band && !--mp->mp_rubber_band->rb_rc)
	bu_free(mp->mp_rubber_band, "mp_rubber_band");
    mp->mp_rubber_band = NULL;

    if (mp->mp_mged_variables && !--mp->mp_mged_variables->mv_rc)
	bu_free(mp->mp_mged_variables, "mp_mged_variables");
    mp->mp_mged_variables = NULL;

    if (mp->mp_color_scheme && !--mp->mp_color_scheme->cs_rc)
	bu_free(mp->mp_color_scheme, "mp_color_scheme");
    mp->mp_color_scheme = NULL;

    if (mp->mp_grid_state && !--mp->mp_grid_state->rc)
	bu_free(mp->mp_grid_state, "mp_grid_state");
    mp->mp_grid_state = NULL;

    if (mp->mp_axes_state && !--mp->mp_axes_state->ax_rc)
	bu_free(mp->mp_axes_state, "mp_axes_state");
    mp->mp_axes_state = NULL;

    if (mp->mp_dlist_state && !--mp->mp_dlist_state->dl_rc)
	bu_free(mp->mp_dlist_state, "mp_dlist_state");
    mp->mp_dlist_state = NULL;

    if (mp->mp_view_state) {
	if (!--mp->mp_view_state->vs_rc) {
	    view_ring_destroy(mp->mp_view_state);
	    /* vs_gvp for dm panes: registered in ged_views, freed separately.
	     * vs_gvp for Obol panes: also GED-owned, do NOT free here. */
	    mp->mp_view_state->vs_gvp = NULL;
	    bu_free(mp->mp_view_state, "mp_view_state");
	}
	mp->mp_view_state = NULL;
    }

    /* Free Tcl HUD variable name storage. */
    bu_vls_free(&mp->mp_fps_name);
    bu_vls_free(&mp->mp_aet_name);
    bu_vls_free(&mp->mp_ang_name);
    bu_vls_free(&mp->mp_center_name);
    bu_vls_free(&mp->mp_size_name);
    bu_vls_free(&mp->mp_adc_name);

    BSG_FREE_VLIST(&rt_vlfree, &mp->mp_p_vlist);
}

int
mged_dm_init(
	struct mged_state *UNUSED(s),
	struct mged_pane *UNUSED(o_pane),
	struct mged_pane *UNUSED(pane),
	const char *UNUSED(dm_type),
	int UNUSED(argc),
	const char *UNUSED(argv[]))
{
    /* Step 7.19: mged_dm_init() removed — libdm plugin creation eliminated.
     * mged_attach() now creates Obol panes directly (no dm_open call).
     * This stub is retained so that any out-of-tree callers get a clean
     * compile error rather than a link error. */
    return TCL_ERROR;
}



/* Step 7.19: mged_fb_open() removed — framebuffer-over-dm path eliminated.
 * Obol panes use their own fb overlay mechanism; no libdm fb handle needed. */


/* Step 7.13: mged_slider_init_vls, mged_slider_free_vls, mged_link_vars removed.
 * Pane HUD VLS names are populated exclusively by mged_pane_link_vars(). */


static int
release(struct mged_state *s, char *name, int UNUSED(need_close), struct mged_pane *bad_pane)
{
    /* Step 7.18: cpane is the pane being released. */
    struct mged_pane *cpane = MGED_PANE_NULL;
    struct mged_pane *save_pane = MGED_PANE_NULL; /* pane to restore to after release */

    if (name != NULL) {
	if (BU_STR_EQUAL("nu", name))
	    return TCL_OK;  /* Ignore */

	/* Step 7.19: libdm path removed; search all panes by gv_name. */
	struct mged_pane *mp = mged_pane_find_by_name(name);
	if (mp) {
	    bsg_view *gvp = mp->mp_gvp;
	    int is_curr = (mp == s->mged_curr_pane);

	    /* Save settings to sentinel before releasing. */
	    usurp_all_resources(s->mged_init_pane, mp);

	    /* Clear cmd_tie before freeing the pane. */
	    if (mp->mp_cmd_tie) {
		mp->mp_cmd_tie->cl_tie = MGED_PANE_NULL;
		mp->mp_cmd_tie = CMD_LIST_NULL;
	    }

	    /* Remove the pane from active_pane_set and free the struct. */
	    mged_pane_release(mp);

	    /* Teardown the bsg_view: remove from scene, free tclcad user
	     * data, remove from ged_free_views, then free the view. */
	    if (gvp && s->gedp) {
		bsg_scene_rm_view(&s->gedp->ged_views, gvp);
		bu_ptbl_rm(&s->gedp->ged_free_views, (long *)gvp);

		struct tclcad_view_data *tvd =
		    (struct tclcad_view_data *)gvp->u_data;
		if (tvd) {
		    bu_vls_free(&tvd->gdv_edit_motion_delta_callback);
		    bu_vls_free(&tvd->gdv_callback);
		    BU_PUT(tvd, struct tclcad_view_data);
		    gvp->u_data = NULL;
		}

		bsg_view_free(gvp);
		bu_free((void *)gvp, "release pane: bsg_view");
	    }

	    /* If we released the current pane, restore to init sentinel. */
	    if (is_curr)
		set_curr_pane(s, s->mged_init_pane);

	    return TCL_OK;
	}

	Tcl_AppendResult(s->interp, "release: ", name, " not found\n", (char *)NULL);
	return TCL_ERROR;
    } else {
	/* name == NULL: release the specified bad_pane (or current pane via
	 * f_release).  Step 7.19: mp_dmp is always NULL in Obol-only mode,
	 * so do NOT skip cleanup based on mp_dmp. */
	cpane = bad_pane;
	if (!cpane)
	    return TCL_OK;
    }

    /* Step 7.20: mp_fbp/mp_clients removed — no framebuffer teardown needed. */

    /*
     * Save the state of the resources to the "nu" sentinel init_pane,
     * which is beneficial only if closing the last display manager.
     * So when another display manager is opened, it looks like the last one.
     * Step 7.18: usurp_all_resources now takes mged_pane * directly.
     */
    usurp_all_resources(s->mged_init_pane, cpane);

    /* Clear cmd_tie before freeing the pane. */
    if (cpane->mp_cmd_tie) {
	cpane->mp_cmd_tie->cl_tie = MGED_PANE_NULL;
	cpane->mp_cmd_tie = CMD_LIST_NULL;
    }

    /* Remove the pane from active_pane_set. */
    bu_ptbl_rm(&active_pane_set, (long *)cpane);

    /* mged_pane_free_resources: resources already moved to sentinel by usurp. */
    mged_pane_free_resources(cpane);

    /* Step 7.19: mp_dmp is always NULL (no libdm attach). */

    BU_PUT(cpane, struct mged_pane);

    /* Restore pane context. */
    if (save_pane != MGED_PANE_NULL) {
	set_curr_pane(s, save_pane);
    } else {
	/* Current pane was released; find next available pane. */
	struct mged_pane *next_pane = NULL;
	for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	    struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	    if (mp) { next_pane = mp; break; }
	}
	if (next_pane) {
	    set_curr_pane(s, next_pane);
	} else {
	    /* No more panes; fall back to sentinel. */
	    set_curr_pane(s, s->mged_init_pane);
	}
    }
    return TCL_OK;
}


int
f_release(ClientData clientData, Tcl_Interp *interpreter, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    struct bu_vls vls = BU_VLS_INIT_ZERO;

    if (argc < 1 || 2 < argc) {
	bu_vls_printf(&vls, "help release");
	Tcl_Eval(interpreter, bu_vls_addr(&vls));
	bu_vls_free(&vls);

	return TCL_ERROR;
    }

    if (argc == 2) {
	int status;

	if (*argv[1] != '.')
	    bu_vls_printf(&vls, ".%s", argv[1]);
	else
	    bu_vls_strcpy(&vls, argv[1]);

	status = release(s, bu_vls_addr(&vls), 1, NULL);

	bu_vls_free(&vls);
	return status;
    } else
	return release(s, (char *)NULL, 1, s->mged_curr_pane);
}


/* Step 7.19: print_valid_dm() removed — dm type list no longer exposed.
 * libdm attach path eliminated; attach always creates Obol panes. */


int
f_attach(ClientData clientData, Tcl_Interp *interpreter, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    if (argc < 2) {
	struct bu_vls vls = BU_VLS_INIT_ZERO;

	bu_vls_printf(&vls, "help attach");
	Tcl_Eval(interpreter, bu_vls_addr(&vls));
	bu_vls_free(&vls);
	Tcl_AppendResult(interpreter,
		"\tStep 7.19: use new_obol_view_ptr + obol_view instead.\n",
		(char *)NULL);
	return TCL_ERROR;
    }

    /* Step 7.19: "nu" is still accepted as a no-op for backward compat. */
    if (BU_STR_EQUAL(argv[argc-1], "nu"))
	return TCL_OK;

    /* Step 7.19: libdm attach path removed.  All types now create Obol panes.
     * Legacy callers (scripts that do "attach ogl") get an Obol pane silently. */
    return mged_attach(s, argv[argc - 1], argc, argv);
}


int
gui_setup(struct mged_state *s, const char *dstr)
{
    if (!s)
	return TCL_ERROR;

#ifdef HAVE_TK
    Tk_GenericProc *handler = doEvent;
#endif
    /* initialize only once */
    if (tkwin != NULL)
	return TCL_OK;

    Tcl_ResetResult(s->interp);

    /* set DISPLAY to dstr */
    if (dstr != (char *)NULL) {
	Tcl_SetVar(s->interp, "env(DISPLAY)", dstr, TCL_GLOBAL_ONLY);
    }

#ifdef HAVE_TK
    /* This runs the tk.tcl script */
    if (Tk_Init(s->interp) == TCL_ERROR) {
	const char *result = Tcl_GetStringResult(s->interp);
	/* hack to avoid a stupid Tk error */
	if (bu_strncmp(result, "this isn't a Tk applicationcouldn't", 35) == 0) {
	    result = (result + 27);
	    Tcl_ResetResult(s->interp);
	    Tcl_AppendResult(s->interp, result, (char *)NULL);
	}
	return TCL_ERROR;
    }

    /* Initialize [incr Tk] */
    if (Tcl_Eval(s->interp, "package require Itk") != TCL_OK) {
      return TCL_ERROR;
    }

    /* Import [incr Tk] commands into the global namespace */
    if (Tcl_Import(s->interp, Tcl_GetGlobalNamespace(s->interp),
		   "::itk::*", /* allowOverwrite */ 1) != TCL_OK) {
	return TCL_ERROR;
    }
#endif

    /* Initialize the Iwidgets package */
    if (Tcl_Eval(s->interp, "package require Iwidgets") != TCL_OK) {
	return TCL_ERROR;
    }

    /* Import iwidgets into the global namespace */
    if (Tcl_Import(s->interp, Tcl_GetGlobalNamespace(s->interp),
		   "::iwidgets::*", /* allowOverwrite */ 1) != TCL_OK) {
	return TCL_ERROR;
    }

    /* Initialize libtclcad */
#ifdef HAVE_TK
    (void)tclcad_init(s->interp, 1, NULL);
#else
    (void)tclcad_init(s->interp, 0, NULL);
#endif

#ifdef HAVE_TK
    if ((tkwin = Tk_MainWindow(s->interp)) == NULL) {
	return TCL_ERROR;
    }

    /* Initialize Obol scene-graph renderer if the libtclcad obol_view
     * command was registered.  This is a no-op when BRLCAD_ENABLE_OBOL
     * was not set at compile time (obol_init won't exist as a command). */
    if (Tcl_GetCommandInfo(s->interp, "obol_init", NULL)) {
	if (Tcl_Eval(s->interp, "obol_init") != TCL_OK)
	    bu_log("mged: Obol init warning: %s\n",
		   Tcl_GetStringResult(s->interp));
    }

    /* create the event handler */
    Tk_CreateGenericHandler(handler, (ClientData)s);

    Tcl_Eval(s->interp, "wm withdraw .");
    Tcl_Eval(s->interp, "tk appname mged");
#endif

    return TCL_OK;
}


int
mged_attach(struct mged_state *s, const char *wp_name, int argc, const char *argv[])
{
    /* Step 7.19: Obol-only attach path.  The legacy libdm plugin creation
     * (dm_open → mged_dm_init) is removed.  mged_attach now creates a pure
     * Obol pane (bsg_view with NULL dmp) regardless of the requested dm type.
     * gui_setup() is still called to initialise Tk for obol_view widget support.
     * Callers that previously expected a live libdm handle should use the Tcl
     * new_obol_view_ptr / obol_view path from mview.tcl instead. */
    if (!wp_name)
	return TCL_ERROR;

    /* Initialise Tk once, scanning argv for an optional "-d display" override. */
    if (tkwin == NULL) {
	const char *dname = NULL;
	for (int i = 1; i < argc - 1; i++) {
	    if (BU_STR_EQUAL(argv[i], "-d") && i + 1 < argc - 1) {
		dname = argv[i + 1];
		break;
	    }
	}
	if (gui_setup(s, (dname && strlen(dname)) ? dname : (char *)NULL) == TCL_ERROR)
	    return TCL_ERROR;
    }

    /* Allocate a new bsg_view (no libdm handle — Obol renderer). */
    bsg_view *gvp;
    BU_ALLOC(gvp, bsg_view);

    struct bu_ptbl *callbacks;
    BU_GET(callbacks, struct bu_ptbl);
    bu_ptbl_init(callbacks, 8, "bv callbacks");

    bu_vls_init(&gvp->gv_name);
    bu_vls_sprintf(&gvp->gv_name, "%s", wp_name);

    /* Allocate tclcad_view_data (expected by libtclcad go_refresh / go_draw_solid). */
    struct tclcad_view_data *tvd;
    BU_GET(tvd, struct tclcad_view_data);
    bu_vls_init(&tvd->gdv_edit_motion_delta_callback);
    tvd->gdv_edit_motion_delta_callback_cnt = 0;
    bu_vls_init(&tvd->gdv_callback);
    tvd->gdv_callback_cnt = 0;
    tvd->gedp = s->gedp;
    gvp->u_data = (void *)tvd;
    gvp->dmp = NULL;  /* no libdm handle */

    bsg_view_init(gvp, &s->gedp->ged_views);
    bsg_scene_root_create(gvp);
    gvp->callbacks = callbacks;
    bsg_scene_add_view(&s->gedp->ged_views, gvp);
    bu_ptbl_ins(&s->gedp->ged_free_views, (long *)gvp);

    gvp->gv_s->point_scale = 1.0;
    gvp->gv_s->curve_scale = 1.0;

    /* Framebuffer: not connected for Obol panes (fb overlay is follow-on work). */
    tvd->gdv_fbs.fbs_listener.fbsl_fbsp = &tvd->gdv_fbs;
    tvd->gdv_fbs.fbs_listener.fbsl_fd   = -1;
    tvd->gdv_fbs.fbs_listener.fbsl_port = -1;
    tvd->gdv_fbs.fbs_fbp                = NULL; /* Stage 9: FB_NULL (dm.h) → NULL */
    tvd->gdv_fbs.fbs_callback           = NULL;
    tvd->gdv_fbs.fbs_clientData         = gvp;
    tvd->gdv_fbs.fbs_interp             = s->interp;

    /* Create and register the mged_pane (mp_gvp set before init_resources so
     * the Obol code path runs, not the dm-shell path). */
    struct mged_pane *pane;
    BU_GET(pane, struct mged_pane);
    pane->mp_gvp     = gvp;
    pane->mp_cmd_tie = NULL;
    BU_LIST_INIT(&pane->mp_p_vlist);
    mged_pane_init_resources(s, pane);
    mged_pane_link_vars(pane);
    bu_ptbl_ins(&active_pane_set, (long *)pane);

    /* Sync ged_gvp and gv_grid from the new pane. */
    s->gedp->ged_gvp = gvp;
    s->gedp->ged_gvp->gv_s->gv_grid = *pane->mp_grid_state; /* struct copy */

    set_curr_pane(s, pane);

    Tcl_ResetResult(s->interp);
    Tcl_AppendResult(s->interp,
	    "ATTACHING obol (Obol scene-graph renderer)\n", (char *)NULL);
    return TCL_OK;
}


#define MAX_ATTACH_RETRIES 100

void
get_attached(struct mged_state *UNUSED(s))
{
    /* Step 7.19: libdm attach prompt removed.  In the Obol-only world, display
     * panes are created via the Tcl new_obol_view_ptr / obol_view commands
     * from mview.tcl rather than via a terminal dm-type prompt. */
}


/*
 * Run a display manager specific command(s).
 * Step 7.19: libdm removed; all subcommands now return "nu" or an error.
 */
int
f_dm(ClientData clientData, Tcl_Interp *interpreter, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct bu_vls vls = BU_VLS_INIT_ZERO;

    if (argc < 2) {
	bu_vls_printf(&vls, "help dm");
	Tcl_Eval(interpreter, bu_vls_addr(&vls));
	bu_vls_free(&vls);
	return TCL_ERROR;
    }

    /* "dm valid <type>": no libdm types are valid in Obol-only mode. */
    if (BU_STR_EQUAL(argv[1], "valid"))
	return TCL_OK;

    /* "dm type": always "nu" (no libdm attached). */
    if (BU_STR_EQUAL(argv[1], "type")) {
	if (argc != 2) {
	    bu_vls_printf(&vls, "help dm");
	    Tcl_Eval(interpreter, bu_vls_addr(&vls));
	    bu_vls_free(&vls);
	    return TCL_ERROR;
	}
	Tcl_AppendResult(interpreter, "nu", (char *)NULL);
	return TCL_OK;
    }

    /* All other subcommands: no display manager available. */
    Tcl_AppendResult(interpreter,
	    "dm: no display manager attached (Obol-only mode)\n", (char *)NULL);
    return TCL_ERROR;
}

/* Step 7.19: dm_var_init() removed — it was the libdm view-initialisation
 * helper that created a bsg_view inside a _view_state for a legacy dm pane.
 * mged_attach() now creates views directly via bsg_view_init (Obol path).
 * mged_pane_init_resources() handles the Obol view_state for new panes. */


/* Step 7.13: mged_link_vars() removed.  The dm's dm_fps_name etc. VLS fields
 * were the only output and they were never read after being set.  Pane HUD
 * variable names are populated by mged_pane_link_vars() instead. */


/* Stage 7: Set up Tcl HUD display variable names for an Obol mged_pane.
 * The variable names mirror the dm_*_name fields in mged_link_vars() but
 * use the view's gv_name (set when f_new_obol_view_ptr registers the
 * bsg_view with GED) instead of the dm pathname.
 * The bu_vls fields are initialized (zero-length) by mged_pane_init_resources;
 * this function populates them from mp->mp_gvp->gv_name.
 */
void
mged_pane_link_vars(struct mged_pane *mp)
{
    if (!mp || !mp->mp_gvp || !bu_vls_strlen(&mp->mp_gvp->gv_name))
	return;
    const char *pname = bu_vls_cstr(&mp->mp_gvp->gv_name);
    bu_vls_printf(&mp->mp_fps_name,    "%s(%s,fps)",    MGED_DISPLAY_VAR, pname);
    bu_vls_printf(&mp->mp_aet_name,    "%s(%s,aet)",    MGED_DISPLAY_VAR, pname);
    bu_vls_printf(&mp->mp_ang_name,    "%s(%s,ang)",    MGED_DISPLAY_VAR, pname);
    bu_vls_printf(&mp->mp_center_name, "%s(%s,center)", MGED_DISPLAY_VAR, pname);
    bu_vls_printf(&mp->mp_size_name,   "%s(%s,size)",   MGED_DISPLAY_VAR, pname);
    bu_vls_printf(&mp->mp_adc_name,    "%s(%s,adc)",    MGED_DISPLAY_VAR, pname);
}


int
f_get_dm_list(ClientData UNUSED(clientData), Tcl_Interp *interpreter, int argc, const char *argv[])
{
    if (argc != 1 || !argv) {
	struct bu_vls vls = BU_VLS_INIT_ZERO;

	bu_vls_printf(&vls, "helpdevel get_dm_list");
	Tcl_Eval(interpreter, bu_vls_addr(&vls));
	bu_vls_free(&vls);

	return TCL_ERROR;
    }

    /* Step 7.19: libdm removed; enumerate Obol panes by gv_name instead. */
    for (size_t i = 0; i < BU_PTBL_LEN(&active_pane_set); i++) {
	struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, i);
	if (!mp || !mp->mp_gvp)
	    continue;
	if (bu_vls_strlen(&mp->mp_gvp->gv_name))
	    Tcl_AppendElement(interpreter, bu_vls_cstr(&mp->mp_gvp->gv_name));
    }
    return TCL_OK;
}


/**
 * f_gvp_ptr — return the current bsg_view pointer as a hex string.
 *
 * This exposes the live view-state pointer to Tcl so that the obol_view
 * widget can be attached to it:
 *
 *   .v attach [mged_gvp_ptr]
 *
 * The returned string is a C pointer formatted as "%p" (e.g. "0x7f3a1c00").
 * It is valid for the lifetime of the mged session.
 */
int
f_gvp_ptr(ClientData clientData, Tcl_Interp *interpreter,
	  int UNUSED(argc), const char **UNUSED(argv))
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    if (!s || !s->gedp || !s->gedp->ged_gvp) {
	Tcl_SetResult(interpreter, (char *)"0", TCL_STATIC);
	return TCL_OK;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%p", (void *)s->gedp->ged_gvp);
    Tcl_SetResult(interpreter, buf, TCL_VOLATILE);
    return TCL_OK;
}


/**
 * f_new_obol_view_ptr — create a new independent bsg_view for an Obol pane.
 *
 * Usage: new_obol_view_ptr <pane_path>
 *
 * Creates a new bsg_view registered in the mged GED view set with a NULL dmp
 * (Obol-rendered; no libdm plugin).  Returns the pointer as a hex string so
 * that mview.tcl can pass it to "obol_view <path> attach <ptr>".
 *
 * This enables per-pane independent cameras in mged's 4-pane Obol layout:
 * each pane gets its own bsg_view so that view commands (ae, press, zoom, …)
 * affect only the focused pane.  mview.tcl stores the pane→ptr mapping in
 * the Tcl array ::obol_pane_gvp so that winset can switch ged_gvp.
 *
 * MIGRATION NOTE (Stage 7 — MGED libdm removal):
 *
 * Views created here intentionally bypass the legacy `mged_dm` / `active_dm_set`
 * infrastructure.  They are the prototype for the future `mged_pane` struct that
 * will replace `mged_dm` once libdm is fully removed from MGED.  See the "MGED
 * refactoring for libdm removal" section of RADICAL_MIGRATION.md for the full
 * plan.  In particular:
 *
 *  - `dmp == NULL` is load-bearing: `go_refresh()`, `go_draw_solid()`, and
 *    `mged.c`'s `refresh()` all already guard against NULL dmp.
 *  - The `tclcad_view_data` `u_data` is what libtclcad helpers (go_refresh,
 *    go_draw_solid, to_open_fbs) expect; it must be present.
 *  - The framebuffer (gdv_fbs) is left in its sentinel state because rt output
 *    compositing for mged Obol panes is a follow-on task (requires wiring up
 *    an fbs_open_client_handler analogous to the qged Obol path in fbserv.cpp).
 */
int
f_new_obol_view_ptr(ClientData clientData, Tcl_Interp *interpreter,
		    int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    if (argc < 2) {
	Tcl_SetResult(interpreter,
		      (char *)"Usage: new_obol_view_ptr <pane_path>",
		      TCL_STATIC);
	return TCL_ERROR;
    }

    if (!s || !s->gedp) {
	Tcl_SetResult(interpreter, (char *)"0", TCL_STATIC);
	return TCL_OK;
    }

    /* Allocate and initialise a new bsg_view (Obol path — no dmp). */
    bsg_view *gvp;
    BU_ALLOC(gvp, bsg_view);

    struct bu_ptbl *callbacks;
    BU_GET(callbacks, struct bu_ptbl);
    bu_ptbl_init(callbacks, 8, "bv callbacks");

    bu_vls_init(&gvp->gv_name);
    bu_vls_sprintf(&gvp->gv_name, "%s", argv[1]);

    /* Allocate tclcad_view_data (u_data expected by libtclcad helpers). */
    struct tclcad_view_data *tvd;
    BU_GET(tvd, struct tclcad_view_data);
    bu_vls_init(&tvd->gdv_edit_motion_delta_callback);
    tvd->gdv_edit_motion_delta_callback_cnt = 0;
    bu_vls_init(&tvd->gdv_callback);
    tvd->gdv_callback_cnt = 0;
    tvd->gedp = s->gedp;
    gvp->u_data = (void *)tvd;

    /* dmp is intentionally left NULL — the obol_view Tk widget renders this
     * view; go_refresh() / go_draw_solid() guard against null dmp already. */
    gvp->dmp = NULL;

    bsg_view_init(gvp, &s->gedp->ged_views);
    bsg_scene_root_create(gvp);
    gvp->callbacks = callbacks;
    bsg_scene_add_view(&s->gedp->ged_views, gvp);
    bu_ptbl_ins(&s->gedp->ged_free_views, (long *)gvp);

    gvp->gv_s->point_scale = 1.0;
    gvp->gv_s->curve_scale = 1.0;

    /* Framebuffer: not yet connected for mged Obol panes — to_open_fbs()
     * already returns immediately when dmp is NULL, so leave fbs uninit'd
     * except for the sentinel values that prevent use-before-init crashes. */
    tvd->gdv_fbs.fbs_listener.fbsl_fbsp = &tvd->gdv_fbs;
    tvd->gdv_fbs.fbs_listener.fbsl_fd   = -1;
    tvd->gdv_fbs.fbs_listener.fbsl_port = -1;
    tvd->gdv_fbs.fbs_fbp                = NULL; /* Stage 9: FB_NULL (dm.h) → NULL */
    tvd->gdv_fbs.fbs_callback           = NULL;
    tvd->gdv_fbs.fbs_clientData         = gvp;
    tvd->gdv_fbs.fbs_interp             = interpreter;

    /* Stage 7 (libdm removal): Register this Obol pane in active_pane_set so
     * that f_winset can find it without the ::obol_pane_gvp Tcl-variable
     * bridge.  The mged_pane carries only the view pointer and command-history
     * link; all other per-pane state remains on the legacy mged_dm for now. */
    struct mged_pane *pane;
    BU_GET(pane, struct mged_pane);
    pane->mp_gvp     = gvp;
    pane->mp_cmd_tie = NULL;  /* not yet attached to a cmd_list */
    BU_LIST_INIT(&pane->mp_p_vlist);
    /* Stage 7 Step 6 prep: allocate per-pane overlay state so Obol panes
     * can eventually have their own color scheme, variables, etc. independent
     * of the legacy mged_curr_dm state.  These are not yet wired into the
     * global macros (view_state, color_scheme, etc.) — that happens in Step 6
     * when the macros are changed to prefer mged_curr_pane. */
    mged_pane_init_resources(s, pane);
    mged_pane_link_vars(pane);  /* populate mp_fps_name, mp_aet_name etc. */
    bu_ptbl_ins(&active_pane_set, (long *)pane);

    /* Return the pointer as a hex string.  The caller (mview.tcl) passes this
     * to "obol_view <path> attach <ptr>" — the obol_view Tk widget's attach
     * subcommand expects a C pointer in this format (same convention as the
     * existing gvp_ptr / to_new_view paths).  f_winset validates the parsed
     * pointer against active_pane_set before using it, preventing a crafted
     * Tcl value from redirecting ged_gvp to an unregistered address. */
    char buf[64];
    snprintf(buf, sizeof(buf), "%p", (void *)gvp);
    Tcl_SetResult(interpreter, buf, TCL_VOLATILE);
    return TCL_OK;
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
