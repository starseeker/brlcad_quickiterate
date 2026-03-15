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
extern void share_dlist(struct mged_dm *dlp2);	/* defined in share.c */
int mged_default_dlist = 0;   /* This variable is available via Tcl for controlling use of display lists */

static fastf_t windowbounds[6] = { (int)BSG_VIEW_MIN, (int)BSG_VIEW_MAX, (int)BSG_VIEW_MIN, (int)BSG_VIEW_MAX, (int)BSG_VIEW_MIN, (int)BSG_VIEW_MAX };

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

    /* Step 7.18: Initialize libdm fields to safe/null defaults.
     * mp_dmp is set later by mged_dm_init() (dm attach) or stays NULL (Obol). */
    mp->mp_dmp    = NULL;
    mp->mp_fbp    = NULL;
    mp->mp_netfd  = -1;
    mp->mp_dirty  = 1;
    mp->mp_mapped = 1;
    /* mp_netchan and mp_clients zero-initialized by BU_GET. */

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
    /* dm panes use dlist if available; Obol panes never use display lists.
     * If mp_gvp is NULL at this point it's a dm-attach pane → enable dlist. */
    mp->mp_mged_variables->mv_dlist  = (mp->mp_gvp == NULL) ? mged_default_dlist : 0;
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
     * - dm-attach / sentinel (mp->mp_gvp == NULL): allocate shell; dm_var_init fills it.
     * - Obol (mp->mp_gvp != NULL): borrow GED-owned view. */
    BU_ALLOC(mp->mp_view_state, struct _view_state);
    mp->mp_view_state->vs_rc  = 1;
    mp->mp_view_state->vs_gvp = mp->mp_gvp;  /* NULL for dm/sentinel, real for Obol */
    if (mp->mp_gvp == NULL) {
	/* dm-attach or sentinel: plain init; dm_var_init will fill vs_gvp. */
	view_ring_init(mp->mp_view_state, (struct _view_state *)NULL);
	if (src && src->mp_view_state) {
	    MAT_COPY(mp->mp_view_state->vs_model2objview,
		     src->mp_view_state->vs_model2objview);
	    MAT_COPY(mp->mp_view_state->vs_objview2model,
		     src->mp_view_state->vs_objview2model);
	    MAT_COPY(mp->mp_view_state->vs_ModelDelta,
		     src->mp_view_state->vs_ModelDelta);
	}
    } else {
	/* Obol pane: copy matrix defaults from init_src if available. */
	view_ring_init(mp->mp_view_state, (struct _view_state *)NULL);
	if (src && src->mp_view_state) {
	    MAT_COPY(mp->mp_view_state->vs_model2objview,
		     src->mp_view_state->vs_model2objview);
	    MAT_COPY(mp->mp_view_state->vs_objview2model,
		     src->mp_view_state->vs_objview2model);
	    MAT_COPY(mp->mp_view_state->vs_ModelDelta,
		     src->mp_view_state->vs_ModelDelta);
	}
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
    mp->mp_owner            = 1;
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
	struct mged_state *s,
	struct mged_pane *o_pane,
	struct mged_pane *pane,
	const char *dm_type,
	int argc,
	const char *argv[])
{
    struct bu_vls vls = BU_VLS_INIT_ZERO;

    /* Step 7.18: dm_var_init takes mged_pane * directly. */
    dm_var_init(s, o_pane, pane);

    /* Step 7.14: dm_cmd_hook removed — always dm_commands; no assignment needed. */

    /* Step 7.17/7.18: view_state now in pane (dm_var_init sets pane->mp_view_state). */
    void *ctx = pane->mp_view_state->vs_gvp;
    struct dm *dmp = dm_open(ctx, (void *)s->interp, dm_type, argc-1, argv);
    if (!dmp) {
	Tcl_AppendResult(s->interp, "dm_open(", dm_type, ") failed\n", (char *)NULL);
	return TCL_ERROR;
    }
    pane->mp_dmp = dmp;

    /*XXXX this eventually needs to move into Ogl's private structure */
    dm_set_vp(dmp, &pane->mp_view_state->vs_gvp->gv_scale);
    /* Step 7.16/7.18: mged_variables now in pane. */
    dm_set_perspective(dmp, pane->mp_mged_variables->mv_perspective_mode);

#ifdef HAVE_TK
    if (dm_graphical(dmp) && !BU_STR_EQUAL(dm_get_dm_name(dmp), "swrast")) {
	Tk_DeleteGenericHandler(doEvent, (ClientData)s);
	Tk_CreateGenericHandler(doEvent, (ClientData)s);
    }
#endif
    (void)dm_configure_win(dmp, 0);

    struct bu_vls *pathname = dm_get_pathname(dmp);
    if (pathname && bu_vls_strlen(pathname)) {
	bu_vls_printf(&vls, "mged_bind_dm %s", bu_vls_cstr(pathname));
	Tcl_Eval(s->interp, bu_vls_cstr(&vls));
    }
    bu_vls_free(&vls);

    return TCL_OK;
}



void
mged_fb_open(struct mged_state *s)
{
    /* Step 7.18: DMP goes directly through mged_curr_pane->mp_dmp.
     * Guard against Obol panes (DMP==NULL) for safety. */
    if (!DMP)
	return;
    fbp = dm_get_fb(DMP);
}


/* Step 7.13: mged_slider_init_vls, mged_slider_free_vls, mged_link_vars removed.
 * Pane HUD VLS names are populated exclusively by mged_pane_link_vars(). */


static int
release(struct mged_state *s, char *name, int need_close, struct mged_pane *bad_pane)
{
    /* Step 7.18: cdm removed; cpane is the pane being released. */
    struct mged_pane *cpane = MGED_PANE_NULL;
    struct mged_pane *save_pane = MGED_PANE_NULL; /* pane to restore to after release */
    struct bu_vls *pathname = NULL;

    if (name != NULL) {
	struct mged_pane *found_mp = MGED_PANE_NULL;

	if (BU_STR_EQUAL("nu", name))
	    return TCL_OK;  /* Ignore */

	/* Step 6.c: search active_pane_set for the named dm wrapper. */
	for (size_t i = 0; i < BU_PTBL_LEN(&active_pane_set); i++) {
	    struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, i);
	    if (!mp || !mp->mp_dmp)
		continue;

	    pathname = dm_get_pathname(mp->mp_dmp);
	    if (!BU_STR_EQUAL(name, bu_vls_cstr(pathname)))
		continue;

	    /* found it */
	    if (mp != s->mged_curr_pane) {
		save_pane = s->mged_curr_pane;
		set_curr_pane(s, mp);
	    }
	    found_mp = mp;
	    break;
	}

	if (found_mp == MGED_PANE_NULL) {
	    /* "release .mged0.ul" called from releasemv in mview.tcl
	     * for Obol panes — look them up by name in active_pane_set. */
	    struct mged_pane *mp = mged_pane_find_by_name(name);
	    if (mp) {
		bsg_view *gvp = mp->mp_gvp;

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
		    bu_free((void *)gvp, "release obol pane: bsg_view");
		}
		return TCL_OK;
	    }

	    Tcl_AppendResult(s->interp, "release: ", name, " not found\n", (char *)NULL);
	    return TCL_ERROR;
	}
	cpane = found_mp; /* now the current pane after set_curr_pane() */
    } else {
	/* name == NULL: Bad: path from mged_attach. bad_pane is the newly
	 * allocated (but failed to initialize) pane. */
	cpane = bad_pane;
	if (!cpane || !cpane->mp_dmp)
	    /* Nothing to release for headless/null dm. */
	    return TCL_OK;
    }

    if (cpane->mp_fbp) {
	if (mged_variables->mv_listen) {
	    /* drop all clients */
	    mged_variables->mv_listen = 0;
	    fbserv_set_port(NULL, NULL, NULL, NULL, s);
	}

	/* release framebuffer resources */
	fb_close_existing(cpane->mp_fbp);
	cpane->mp_fbp = (struct fb *)NULL;
    }

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

    if (need_close && cpane->mp_dmp)
	dm_close(cpane->mp_dmp);
    cpane->mp_dmp = NULL;

    BU_PUT(cpane, struct mged_pane);

    /* Restore pane context. */
    if (save_pane != MGED_PANE_NULL) {
	set_curr_pane(s, save_pane);
    } else {
	/* Current pane was released; find next available dm wrapper pane. */
	struct mged_pane *next_pane = NULL;
	for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	    struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	    if (mp && mp->mp_dmp) { next_pane = mp; break; }
	}
	if (next_pane) {
	    set_curr_pane(s, next_pane);
	} else {
	    /* No more dm panes; clear mged_curr_pane. */
	    s->mged_curr_pane = MGED_PANE_NULL;
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


static void
print_valid_dm(Tcl_Interp *interpreter)
{
    Tcl_AppendResult(interpreter, "\tThe following display manager types are valid: ", (char *)NULL);
    struct bu_vls dm_types = BU_VLS_INIT_ZERO;
    dm_list_types(&dm_types, " ");

    if (bu_vls_strlen(&dm_types)) {
	Tcl_AppendResult(interpreter, bu_vls_cstr(&dm_types), (char *)NULL);
    } else {
	Tcl_AppendResult(interpreter, "NONE AVAILABLE", (char *)NULL);
    }
    bu_vls_free(&dm_types);
    Tcl_AppendResult(interpreter, "\n", (char *)NULL);
}


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
	print_valid_dm(interpreter);

	return TCL_ERROR;
    }

    if (BU_STR_EQUAL(argv[argc-1], "nu")) {
	/* nothing to do */
	return TCL_OK;
    }

    if (!dm_valid_type(argv[argc-1], NULL)) {
	Tcl_AppendResult(interpreter, "attach(", argv[argc - 1], "): BAD\n", (char *)NULL);
	print_valid_dm(interpreter);
	return TCL_ERROR;
    }

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
    struct mged_pane *o_pane;   /* old/previous pane (save before alloc) */

    if (!wp_name) {
	return TCL_ERROR;
    }

    /* Step 7.18: o_pane = current pane (or sentinel if no pane yet). */
    o_pane = s->mged_curr_pane ? s->mged_curr_pane : s->mged_init_pane;

    /* Only need to do this once */
    if (tkwin == NULL && BU_STR_EQUIV(dm_graphics_system(wp_name), "Tk")) {
	/* Stage 7 (step 5.14): Parse the optional "-d display_string" from argv
	 * without opening a temporary "nu" dm.  Previously dm_open("nu") was
	 * used as a throwaway option-parser; we now scan argv directly.
	 * This removes the second dm_open("nu") call from the attach path. */
	const char *dname = NULL;
	for (int i = 1; i < argc - 1; i++) {
	    /* argv[argc-1] is the dm type (wp_name); the value for -d must
	     * appear before it, so i+1 < argc-1 ensures we never read the dm
	     * type name as the display string value. */
	    if (BU_STR_EQUAL(argv[i], "-d") && i + 1 < argc - 1) {
		dname = argv[i + 1];
		break;
	    }
	}

	if (dname && strlen(dname) > 0) {
	    if (gui_setup(s, dname) == TCL_ERROR) {
		s->mged_curr_pane = o_pane;
		return TCL_ERROR;
	    }
	} else if (gui_setup(s, (char *)NULL) == TCL_ERROR) {
	    s->mged_curr_pane = o_pane;
	    return TCL_ERROR;
	}
    }

    /* Step 7.18: Create the pane and allocate its resources BEFORE
     * mged_dm_init() so that pane->mp_mged_variables is available
     * for dm_set_perspective() inside mged_dm_init().
     * Pane is added to active_pane_set here so that release() can find and
     * clean it up on the Bad: error path. */
    struct mged_pane *pane;
    BU_GET(pane, struct mged_pane);
    pane->mp_gvp     = NULL;      /* set after mged_dm_init creates the view */
    pane->mp_cmd_tie = NULL;
    mged_pane_init_resources(s, pane);   /* allocates all 9 resources + libdm fields */
    bu_ptbl_ins(&active_pane_set, (long *)pane);

    if (mged_dm_init(s, o_pane, pane, wp_name, argc, argv) == TCL_ERROR) {
	goto Bad;
    }

    /* Step 7.17/7.18: dm_var_init sets pane->mp_view_state directly.
     * Sync gvp from pane's view_state (which was just set by dm_var_init). */
    pane->mp_gvp = pane->mp_view_state->vs_gvp;

    /* initialize the background color */
    {
	/* need dummy values for func signature--they are unused in the func */
	const struct bu_structparse *sdp = 0;
	const char name[] = "name";
	void *base = 0;
	const char value[] = "value";
	cs_set_bg(sdp, name, base, value, s);
    }

    /* Step 7.9/7.10/7.18: Use pane->mp_dmp directly throughout. */
    struct dm *ndmp = pane->mp_dmp;

    Tcl_ResetResult(s->interp);
    const char *dm_name = dm_get_dm_name(ndmp);
    const char *dm_lname = dm_get_dm_lname(ndmp);
    if (dm_name && dm_lname) {
	Tcl_AppendResult(s->interp, "ATTACHING ", dm_name, " (", dm_lname,	")\n", (char *)NULL);
    }

    share_dlist(pane);

    if (dm_get_displaylist(ndmp) && mged_variables->mv_dlist && !dlist_state->dl_active) {
	createDListAll(s, NULL);
	dlist_state->dl_active = 1;
    }

    (void)dm_make_current(ndmp);
    (void)dm_set_win_bounds(ndmp, windowbounds);

    s->gedp->ged_gvp = pane->mp_view_state->vs_gvp;
    s->gedp->ged_gvp->gv_s->gv_grid = *pane->mp_grid_state; /* struct copy */

    /* Step 6.c: pane already in active_pane_set (added before mged_dm_init above).
     * Finalize: set gv_name from dm pathname, link vars, make current. */
    {
	/* Set gv_name from dm pathname so mged_pane_find_by_name finds it. */
	struct bu_vls *dm_path = dm_get_pathname(ndmp);
	if (dm_path && bu_vls_strlen(dm_path)) {
	    if (!BU_VLS_IS_INITIALIZED(&pane->mp_view_state->vs_gvp->gv_name))
		bu_vls_init(&pane->mp_view_state->vs_gvp->gv_name);
	    bu_vls_sprintf(&pane->mp_view_state->vs_gvp->gv_name,
			  "%s", bu_vls_cstr(dm_path));
	}
	mged_pane_link_vars(pane);           /* populate HUD var names */
	/* Step 7.1a / 7.10/7.18: set_curr_pane makes DMP = pane->mp_dmp
	 * (the new dm) so mged_fb_open() below correctly opens on the new dm. */
	set_curr_pane(s, pane);
    }

    /* Step 7.9: mged_fb_open is called AFTER set_curr_pane so that DMP
     * (and fbp) correctly refer to the new pane's dm. */
    mged_fb_open(s);

    return TCL_OK;

 Bad:
    Tcl_AppendResult(s->interp, "attach(", argv[argc - 1], "): BAD\n", (char *)NULL);

    /* Step 7.18: Pass pane (the failed new pane) explicitly to release().
     * mged_curr_pane is still the OLD pane so the Bad: path must not
     * try to release via the old pane's dm.
     * The pane was already added to active_pane_set before dm_init,
     * so release() will find and free it. */
    release(s, (char *)NULL, (pane->mp_dmp != NULL) ? 1 : 0, pane);

    return TCL_ERROR;
}


#define MAX_ATTACH_RETRIES 100

void
get_attached(struct mged_state *s)
{
    char *tok;
    int inflimit = MAX_ATTACH_RETRIES;
    int ret;
    struct bu_vls avail_types = BU_VLS_INIT_ZERO;
    struct bu_vls wanted_type = BU_VLS_INIT_ZERO;
    struct bu_vls prompt = BU_VLS_INIT_ZERO;

    const char *DELIM = " ";

    dm_list_types(&avail_types, DELIM);

    bu_vls_sprintf(&prompt, "attach (nu");
    for (tok = strtok(bu_vls_addr(&avail_types), " "); tok; tok = strtok(NULL, " ")) {
	if (BU_STR_EQUAL(tok, "nu"))
	    continue;
	if (BU_STR_EQUAL(tok, "plot"))
	    continue;
	if (BU_STR_EQUAL(tok, "postscript"))
	    continue;
	bu_vls_printf(&prompt, " %s", tok);
    }
    bu_vls_printf(&prompt, ")[nu]? ");

    bu_vls_free(&avail_types);

    while (inflimit > 0) {
	bu_log("%s", bu_vls_cstr(&prompt));

	ret = bu_vls_gets(&wanted_type, stdin);
	if (ret < 0) {
	    /* handle EOF */
	    bu_log("\n");
	    bu_vls_free(&wanted_type);
	    bu_vls_free(&prompt);
	    return;
	}

	if (bu_vls_strlen(&wanted_type) == 0 || BU_STR_EQUAL(bu_vls_addr(&wanted_type), "nu")) {
	    /* Nothing more to do. */
	    bu_vls_free(&wanted_type);
	    bu_vls_free(&prompt);
	    return;
	}

	/* trim whitespace before comparisons (but not before checking empty) */
	bu_vls_trimspace(&wanted_type);

	if (dm_valid_type(bu_vls_cstr(&wanted_type), NULL)) {
	    break;
	}

	/* Not a valid choice, loop. */
	inflimit--;
    }

    bu_vls_free(&prompt);

    if (inflimit <= 0) {
	bu_log("\nInfinite loop protection, attach aborted!\n");
	bu_vls_free(&wanted_type);
	return;
    }

    bu_log("Starting an %s display manager\n", bu_vls_cstr(&wanted_type));

    int argc = 1;
    const char *argv[3];
    argv[0] = "";
    argv[1] = "";
    argv[2] = (char *)NULL;
    (void)mged_attach(s, bu_vls_cstr(&wanted_type), argc, argv);
    bu_vls_free(&wanted_type);
}


/*
 * Run a display manager specific command(s).
 */
int
f_dm(ClientData clientData, Tcl_Interp *interpreter, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;
    struct bu_vls vls = BU_VLS_INIT_ZERO;

    if (argc < 2) {
	bu_vls_printf(&vls, "help dm");
	Tcl_Eval(interpreter, bu_vls_addr(&vls));
	bu_vls_free(&vls);
	return TCL_ERROR;
    }

    if (BU_STR_EQUAL(argv[1], "valid")) {
	if (argc < 3) {
	    bu_vls_printf(&vls, "help dm");
	    Tcl_Eval(interpreter, bu_vls_addr(&vls));
	    bu_vls_free(&vls);
	    return TCL_ERROR;
	}
	if (dm_valid_type(argv[argc-1], NULL)) {
	    Tcl_AppendResult(interpreter, argv[argc-1], (char *)NULL);
	}
	return TCL_OK;
    }

    /* Stage 7 (step 5.14): DMP is NULL when no display manager has been
     * attached yet (the initial "nu" mged_dm has dm_dmp==NULL).  Return
     * sensible errors for the informational subcommands. */
    if (BU_STR_EQUAL(argv[1], "type")) {
	if (argc != 2) {
	    bu_vls_printf(&vls, "help dm");
	    Tcl_Eval(interpreter, bu_vls_addr(&vls));
	    bu_vls_free(&vls);
	    return TCL_ERROR;
	}
	if (!DMP) {
	    Tcl_AppendResult(interpreter, "nu", (char *)NULL);
	    return TCL_OK;
	}
	Tcl_AppendResult(interpreter, dm_get_type(DMP), (char *)NULL);
	return TCL_OK;
    }

    if (!DMP) {
	Tcl_AppendResult(interpreter,
		"dm: no display manager attached\n", (char *)NULL);
	return TCL_ERROR;
    }

    /* Step 7.14: dm_cmd_hook removed; dm_commands is the universal handler. */
    return dm_commands(argc-1, argv+1, (void *)s);
}

void
dm_var_init(struct mged_state *s, struct mged_pane *target_pane, struct mged_pane *npane)
{
    /* Step 7.18: dm_var_init now takes mged_pane * directly.
     * npane's mp_view_state was initialized to a placeholder shell by
     * mged_pane_init_resources(); here we replace it with a proper live
     * view_state that contains a new bsg_view. */
    struct mged_pane *opane = target_pane;

    if (npane->mp_view_state) {
	/* Free the placeholder shell allocated by mged_pane_init_resources(). */
	if (!--npane->mp_view_state->vs_rc) {
	    view_ring_destroy(npane->mp_view_state);
	    bu_free(npane->mp_view_state, "dm_var_init: placeholder view_state");
	}
	npane->mp_view_state = NULL;
    }

    BU_ALLOC(npane->mp_view_state, struct _view_state);
    if (opane && opane->mp_view_state)
	*npane->mp_view_state = *opane->mp_view_state;	/* struct copy */
    /* Allocate a fresh vs_gvp for the new pane. */
    bsg_view *new_vs_gvp;
    BU_ALLOC(new_vs_gvp, bsg_view);
    BU_GET(new_vs_gvp->callbacks, struct bu_ptbl);
    bu_ptbl_init(new_vs_gvp->callbacks, 8, "bv callbacks");

    if (opane && opane->mp_view_state && opane->mp_view_state->vs_gvp)
	*new_vs_gvp = *opane->mp_view_state->vs_gvp;	/* struct copy */

    BU_GET(new_vs_gvp->gv_objs.db_objs, struct bu_ptbl);
    bu_ptbl_init(new_vs_gvp->gv_objs.db_objs, 8, "view_objs init");

    BU_GET(new_vs_gvp->gv_objs.view_objs, struct bu_ptbl);
    bu_ptbl_init(new_vs_gvp->gv_objs.view_objs, 8, "view_objs init");

    bsg_scene_root_create(new_vs_gvp);

    new_vs_gvp->vset = &s->gedp->ged_views;
    new_vs_gvp->independent = 0;

    new_vs_gvp->gv_clientData = (void *)npane->mp_view_state;
    new_vs_gvp->gv_s->adaptive_plot_csg = 0;
    new_vs_gvp->gv_s->redraw_on_zoom = 0;
    new_vs_gvp->gv_s->point_scale = 1.0;
    new_vs_gvp->gv_s->curve_scale = 1.0;
    npane->mp_view_state->vs_gvp = new_vs_gvp;
    npane->mp_view_state->vs_rc = 1;
    view_ring_init(npane->mp_view_state, (struct _view_state *)NULL);

    /* Step 7.18: mp_dirty, mp_mapped, mp_netfd are now pane fields.
     * mged_pane_init_resources() set mp_netfd=-1, mp_dirty=1, mp_mapped=1. */
    npane->mp_dirty  = 1;
    npane->mp_mapped = 1;
    npane->mp_netfd  = -1;
    if (opane && opane->mp_dmp) {
	dm_set_dirty(opane->mp_dmp, 1);
    }
}


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

    /* Step 6.c/7.18: enumerate dm wrappers via active_pane_set. */
    for (size_t i = 0; i < BU_PTBL_LEN(&active_pane_set); i++) {
	struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, i);
	if (!mp || !mp->mp_dmp)
	    continue;
	struct bu_vls *pn = dm_get_pathname(mp->mp_dmp);
	if (pn && bu_vls_strlen(pn))
	    Tcl_AppendElement(interpreter, bu_vls_cstr(pn));
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
    tvd->gdv_fbs.fbs_fbp                = FB_NULL;
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
