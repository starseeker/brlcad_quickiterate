/*                         S H A R E . C
 * BRL-CAD
 *
 * Copyright (c) 1998-2025 United States Government as represented by
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
/** @file mged/share.c
 *
 * Description -
 * Routines for sharing resources among display managers.
 *
 */

#include "common.h"

#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bn.h"

#include "./mged.h"
#include "./mged_dm.h"

#define RESOURCE_TYPE_ADC		0
#define RESOURCE_TYPE_AXES		1
#define RESOURCE_TYPE_COLOR_SCHEMES	2
#define RESOURCE_TYPE_GRID		3
#define RESOURCE_TYPE_MENU		4
#define RESOURCE_TYPE_MGED_VARIABLES	5
#define RESOURCE_TYPE_RUBBER_BAND	6
#define RESOURCE_TYPE_VIEW		7

/* Step 7.16: SHARE_RESOURCE now accesses resources via dm->dm_pane->mp_*
 * instead of dm->dm_* (the 8 non-view resources moved to the pane).
 * The macro parameters use the mp_* field names (e.g. mp_adc_state). */
#define SHARE_RESOURCE(uflag, str, resource, rc, dlp1, dlp2, vls, error_msg) \
    do { \
	if (uflag) { \
	    struct str *strp; \
\
	    if (dlp1->dm_pane->resource->rc > 1) {   /* must be sharing this resource */ \
		--dlp1->dm_pane->resource->rc; \
		strp = dlp1->dm_pane->resource; \
		BU_ALLOC(dlp1->dm_pane->resource, struct str); \
		*dlp1->dm_pane->resource = *strp;        /* struct copy */ \
		dlp1->dm_pane->resource->rc = 1; \
	    } \
	} else { \
	    /* must not be sharing this resource */ \
	    if (dlp1->dm_pane->resource != dlp2->dm_pane->resource) { \
		if (!--dlp2->dm_pane->resource->rc) \
		    bu_free((void *)dlp2->dm_pane->resource, error_msg); \
\
		dlp2->dm_pane->resource = dlp1->dm_pane->resource; \
		++dlp1->dm_pane->resource->rc; \
	    } \
	} \
    } while (0)

/* SHARE_RESOURCE_DM: used for dm-owned fields (dm_view_state) that are NOT
 * yet moved to the pane.  Operates on dlp->field directly (old-style). */
#define SHARE_RESOURCE_DM(uflag, str, resource, rc, dlp1, dlp2, vls, error_msg) \
    do { \
	if (uflag) { \
	    struct str *strp; \
\
	    if (dlp1->resource->rc > 1) { \
		--dlp1->resource->rc; \
		strp = dlp1->resource; \
		BU_ALLOC(dlp1->resource, struct str); \
		*dlp1->resource = *strp; \
		dlp1->resource->rc = 1; \
	    } \
	} else { \
	    if (dlp1->resource != dlp2->resource) { \
		if (!--dlp2->resource->rc) \
		    bu_free((void *)dlp2->resource, error_msg); \
\
		dlp2->resource = dlp1->resource; \
		++dlp1->resource->rc; \
	    } \
	} \
    } while (0)


// FIXME: Globals
extern struct bu_structparse axes_vparse[];
extern struct bu_structparse color_scheme_vparse[];
extern struct bu_structparse grid_vparse[];
extern struct bu_structparse rubber_band_vparse[];
extern struct bu_structparse mged_vparse[];

void free_all_resources(struct mged_dm *dlp);

/*
 * SYNOPSIS
 *	share [-u] res p1 [p2]
 *
 * DESCRIPTION
 *	Provides a mechanism to (un)share resources among display managers.
 *	Currently, there are nine different resources that can be shared.
 *	They are:
 *		ADC AXES COLOR_SCHEMES DISPLAY_LISTS GRID MENU MGED_VARIABLES RUBBER_BAND VIEW
 *
 * EXAMPLES
 *	share res_type p1 p2	--->	causes 'p1' to share its resource of type 'res_type' with 'p2'
 *	share -u res_type p	--->	causes 'p' to no longer share resource of type 'res_type'
 */
int
f_share(ClientData clientData, Tcl_Interp *interpreter, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    int uflag = 0;		/* unshare flag */
    struct mged_dm *dlp1 = MGED_DM_NULL;
    struct mged_dm *dlp2 = MGED_DM_NULL;
    struct bu_vls vls = BU_VLS_INIT_ZERO;

    if (argc != 4) {
	bu_vls_printf(&vls, "helpdevel share");
	Tcl_Eval(interpreter, bu_vls_addr(&vls));

	bu_vls_free(&vls);
	return TCL_ERROR;
    }

    if (argv[1][0] == '-' && argv[1][1] == 'u') {
	uflag = 1;
	--argc;
	++argv;
    }

    /* Step 6.b: search active_pane_set for legacy dm wrapper by pathname. */
    for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	if (!mp->mp_dm) continue;
	struct bu_vls *pname = dm_get_pathname(mp->mp_dm->dm_dmp);
	if (BU_STR_EQUAL(argv[2], bu_vls_cstr(pname))) {
	    dlp1 = mp->mp_dm;
	    break;
	}
    }

    if (dlp1 == MGED_DM_NULL) {
	Tcl_AppendResult(interpreter, "share: unrecognized path name - ", argv[2], "\n", (char *)NULL);
	bu_vls_free(&vls);
	return TCL_ERROR;
    }

    if (!uflag) {
	/* Step 6.b: search active_pane_set for legacy dm wrapper by pathname. */
	for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	    struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	    if (!mp->mp_dm) continue;
	    struct bu_vls *pname = dm_get_pathname(mp->mp_dm->dm_dmp);
	    if (BU_STR_EQUAL(argv[3], bu_vls_cstr(pname))) {
		dlp2 = mp->mp_dm;
		break;
	    }
	}

	if (dlp2 == MGED_DM_NULL) {
	    Tcl_AppendResult(interpreter, "share: unrecognized path name - ", argv[3], "\n", (char *)NULL);
	    bu_vls_free(&vls);
	    return TCL_ERROR;
	}

	/* same display manager */
	if (dlp1 == dlp2) {
	    bu_vls_free(&vls);
	    return TCL_OK;
	}
    }

    switch (argv[1][0]) {
	case 'a':
	case 'A':
	    if (argv[1][1] == 'd' || argv[1][1] == 'D')
		SHARE_RESOURCE(uflag, _adc_state, mp_adc_state, adc_rc, dlp1, dlp2, vls, "share: adc_state");
	    else if (argv[1][1] == 'x' || argv[1][1] == 'X')
		SHARE_RESOURCE(uflag, _axes_state, mp_axes_state, ax_rc, dlp1, dlp2, vls, "share: axes_state");
	    else {
		bu_vls_printf(&vls, "share: resource type '%s' unknown\n", argv[1]);
		Tcl_AppendResult(interpreter, bu_vls_addr(&vls), (char *)NULL);

		bu_vls_free(&vls);
		return TCL_ERROR;
	    }
	    break;
	case 'c':
	case 'C':
	    SHARE_RESOURCE(uflag, _color_scheme, mp_color_scheme, cs_rc, dlp1, dlp2, vls, "share: color_scheme");
	    break;
	case 'd':
	case 'D':
	    {
		struct dm *dmp1;
		struct dm *dmp2 = (struct dm *)NULL;

		dmp1 = dlp1->dm_dmp;
		if (dlp2 != (struct mged_dm *)NULL)
		    dmp2 = dlp2->dm_dmp;

		if (dm_share_dlist(dmp1, dmp2) == TCL_OK) {
		    SHARE_RESOURCE(uflag, _dlist_state, mp_dlist_state, dl_rc, dlp1, dlp2, vls, "share: dlist_state");
		    if (uflag) {
			dlp1->dm_pane->mp_dlist_state->dl_active = dlp1->dm_pane->mp_mged_variables->mv_dlist;

			if (dlp1->dm_pane->mp_mged_variables->mv_dlist) {
			    /* Step 7.5: use pane-based save/restore. */
			    struct mged_pane *save_p = s->mged_curr_pane;
			    struct mged_pane *dlp1_pane = MGED_PANE_NULL;
			    for (size_t _pi = 0; _pi < BU_PTBL_LEN(&active_pane_set); _pi++) {
				struct mged_pane *_mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, _pi);
				if (_mp->mp_dm == dlp1) { dlp1_pane = _mp; break; }
			    }
			    if (dlp1_pane) set_curr_pane(s, dlp1_pane);

			    createDListAll(s, NULL);

			    /* restore */
			    set_curr_pane(s, save_p);
			}

			dlp1->dm_dirty = 1;
			if (dlp1->dm_dmp) dm_set_dirty(dlp1->dm_dmp, 1);
		    } else {
			dlp1->dm_dirty = dlp2->dm_dirty = 1;
			if (dlp1->dm_dmp) dm_set_dirty(dlp1->dm_dmp, 1);
			if (dlp2->dm_dmp) dm_set_dirty(dlp2->dm_dmp, 1);
		    }
		}
	    }
	    break;
	case 'g':
	case 'G':
	    SHARE_RESOURCE(uflag, bsg_grid_state, mp_grid_state, rc, dlp1, dlp2, vls, "share: grid_state");
	    break;
	case 'm':
	case 'M':
	    SHARE_RESOURCE(uflag, _menu_state, mp_menu_state, ms_rc, dlp1, dlp2, vls, "share: menu_state");
	    break;
	case 'r':
	case 'R':
	    SHARE_RESOURCE(uflag, _rubber_band, mp_rubber_band, rb_rc, dlp1, dlp2, vls, "share: rubber_band");
	    break;
	case 'v':
	case 'V':
	    if ((argv[1][1] == 'a' || argv[1][1] == 'A') &&
		(argv[1][2] == 'r' || argv[1][2] == 'R'))
		SHARE_RESOURCE(uflag, _mged_variables, mp_mged_variables, mv_rc, dlp1, dlp2, vls, "share: mged_variables");
	    else if (argv[1][1] == 'i' || argv[1][1] == 'I') {
		if (!uflag) {
		    /* free dlp2's view_state resources if currently not sharing */
		    if (dlp2->dm_pane && dlp2->dm_pane->mp_view_state->vs_rc == 1)
			view_ring_destroy(dlp2->dm_pane->mp_view_state);
		}

		/* Step 7.17: view_state now in pane (mp_view_state); use SHARE_RESOURCE. */
		SHARE_RESOURCE(uflag, _view_state, mp_view_state, vs_rc, dlp1, dlp2, vls, "share: view_state");

		if (uflag) {
		    struct _view_state *ovsp;
		    ovsp = dlp1->dm_pane->mp_view_state;

		    /* initialize dlp1's view_state */
		    if (ovsp != dlp1->dm_pane->mp_view_state)
			view_ring_init(dlp1->dm_pane->mp_view_state, ovsp);
		}
	    } else {
		bu_vls_printf(&vls, "share: resource type '%s' unknown\n", argv[1]);
		Tcl_AppendResult(interpreter, bu_vls_addr(&vls), (char *)NULL);

		bu_vls_free(&vls);
		return TCL_ERROR;
	    }

	    break;
	default:
	    bu_vls_printf(&vls, "share: resource type '%s' unknown\n", argv[1]);
	    Tcl_AppendResult(interpreter, bu_vls_addr(&vls), (char *)NULL);

	    bu_vls_free(&vls);
	    return TCL_ERROR;
    }

    if (!uflag) {
	s->update_views = 1;  /* Stage 7: notify Obol path on share change */
	dlp2->dm_dirty = 1;	/* need to redraw this guy */
	if (dlp2->dm_dmp) dm_set_dirty(dlp2->dm_dmp, 1);
    }

    bu_vls_free(&vls);
    return TCL_OK;
}


/*
 * SYNOPSIS
 *	rset [res_type [res [vals]]]
 *
 * DESCRIPTION
 *	Provides a mechanism to set resource values for some resource.
 *
 * EXAMPLES
 *	rset c bg 0 0 50	--->	sets the background color to dark blue
 */
int
f_rset (ClientData clientData, Tcl_Interp *interpreter, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    struct bu_vls vls = BU_VLS_INIT_ZERO;

    /* print values for all resources */
    if (argc == 1) {
	mged_vls_struct_parse(s, &vls, "Axes, res_type - ax", axes_vparse,
			      (const char *)axes_state, argc, argv);
	bu_vls_printf(&vls, "\n");
	mged_vls_struct_parse(s, &vls, "Color Schemes, res_type - c", color_scheme_vparse,
			      (const char *)color_scheme, argc, argv);
	bu_vls_printf(&vls, "\n");
	mged_vls_struct_parse(s, &vls, "Grid, res_type - g", grid_vparse,
			      (const char *)grid_state, argc, argv);
	bu_vls_printf(&vls, "\n");
	mged_vls_struct_parse(s, &vls, "Rubber Band, res_type - r", rubber_band_vparse,
			      (const char *)rubber_band, argc, argv);
	bu_vls_printf(&vls, "\n");
	mged_vls_struct_parse(s, &vls, "MGED Variables, res_type - var", mged_vparse,
			      (const char *)mged_variables, argc, argv);

	Tcl_AppendResult(interpreter, bu_vls_addr(&vls), (char *)NULL);
	bu_vls_free(&vls);

	return TCL_OK;
    }

    switch (argv[1][0]) {
	case 'a':
	case 'A':
	    if (argv[1][1] == 'd' || argv[1][1] == 'D')
		bu_vls_printf(&vls, "rset: use the adc command for the 'adc' resource");
	    else if (argv[1][1] == 'x' || argv[1][1] == 'X')
		mged_vls_struct_parse(s, &vls, "Axes", axes_vparse,
				      (const char *)axes_state, argc-1, argv+1);
	    else {
		bu_vls_printf(&vls, "rset: resource type '%s' unknown\n", argv[1]);
		Tcl_AppendResult(interpreter, bu_vls_addr(&vls), (char *)NULL);

		bu_vls_free(&vls);
		return TCL_ERROR;
	    }
	    break;
	case 'c':
	case 'C':
	    mged_vls_struct_parse(s, &vls, "Color Schemes", color_scheme_vparse,
				  (const char *)color_scheme, argc-1, argv+1);
	    break;
	case 'g':
	case 'G':
	    mged_vls_struct_parse(s, &vls, "Grid", grid_vparse,
				  (const char *)grid_state, argc-1, argv+1);
	    break;
	case 'r':
	case 'R':
	    mged_vls_struct_parse(s, &vls, "Rubber Band", rubber_band_vparse,
				  (const char *)rubber_band, argc-1, argv+1);
	    break;
	case 'v':
	case 'V':
	    if ((argv[1][1] == 'a' || argv[1][1] == 'A') &&
		(argv[1][2] == 'r' || argv[1][2] == 'R'))
		mged_vls_struct_parse(s, &vls, "mged variables", mged_vparse,
				      (const char *)mged_variables, argc-1, argv+1);
	    else if (argv[1][1] == 'i' || argv[1][1] == 'I')
		bu_vls_printf(&vls, "rset: no support available for the 'view' resource");
	    else {
		bu_vls_printf(&vls, "rset: resource type '%s' unknown\n", argv[1]);
		Tcl_AppendResult(interpreter, bu_vls_addr(&vls), (char *)NULL);

		bu_vls_free(&vls);
		return TCL_ERROR;
	    }

	    break;
	default:
	    bu_vls_printf(&vls, "rset: resource type '%s' unknown\n", argv[1]);
	    Tcl_AppendResult(interpreter, bu_vls_addr(&vls), (char *)NULL);

	    bu_vls_free(&vls);
	    return TCL_ERROR;
    }

    Tcl_AppendResult(interpreter, bu_vls_addr(&vls), (char *)NULL);
    bu_vls_free(&vls);

    return TCL_OK;
}


/*
 * dlp1 takes control of dlp2's pane resources. dlp2 is
 * probably on its way out (i.e. being destroyed).
 * Step 7.16: resources are owned by pane (dm_pane->mp_*); transfer them
 * from dlp2's pane to dlp1's pane via the dm_pane back-pointer.
 */
void
usurp_all_resources(struct mged_dm *dlp1, struct mged_dm *dlp2)
{
    struct mged_pane *p1 = dlp1->dm_pane;
    struct mged_pane *p2 = dlp2->dm_pane;

    if (!p1 || !p2) return;

    /* Free p1's current 8 non-view resources (ref-counted). */
    if (p1->mp_adc_state      && !--p1->mp_adc_state->adc_rc)
	bu_free(p1->mp_adc_state,      "usurp: adc_state");
    if (p1->mp_menu_state     && !--p1->mp_menu_state->ms_rc)
	bu_free(p1->mp_menu_state,     "usurp: menu_state");
    if (p1->mp_rubber_band    && !--p1->mp_rubber_band->rb_rc)
	bu_free(p1->mp_rubber_band,    "usurp: rubber_band");
    if (p1->mp_mged_variables && !--p1->mp_mged_variables->mv_rc)
	bu_free(p1->mp_mged_variables, "usurp: mged_variables");
    if (p1->mp_color_scheme   && !--p1->mp_color_scheme->cs_rc)
	bu_free(p1->mp_color_scheme,   "usurp: color_scheme");
    if (p1->mp_grid_state     && !--p1->mp_grid_state->rc)
	bu_free(p1->mp_grid_state,     "usurp: grid_state");
    if (p1->mp_axes_state     && !--p1->mp_axes_state->ax_rc)
	bu_free(p1->mp_axes_state,     "usurp: axes_state");
    /* dlist_state: it doesn't make sense to save display list info */
    if (p1->mp_dlist_state && !--p1->mp_dlist_state->dl_rc)
	bu_free(p1->mp_dlist_state,    "usurp: p1 dlist_state");

    /* Transfer p2's 8 non-view resource pointers to p1. */
    p1->mp_adc_state      = p2->mp_adc_state;
    p1->mp_menu_state     = p2->mp_menu_state;
    p1->mp_rubber_band    = p2->mp_rubber_band;
    p1->mp_mged_variables = p2->mp_mged_variables;
    p1->mp_color_scheme   = p2->mp_color_scheme;
    p1->mp_grid_state     = p2->mp_grid_state;
    p1->mp_axes_state     = p2->mp_axes_state;

    /* dlist_state: free p2's (not saved) */
    if (p2->mp_dlist_state && !--p2->mp_dlist_state->dl_rc)
	bu_free(p2->mp_dlist_state,    "usurp: p2 dlist_state");

    /* Step 7.17: view_state is now pane-owned too; usurp it like the others. */
    if (p1->mp_view_state && !--p1->mp_view_state->vs_rc) {
	view_ring_destroy(p1->mp_view_state);
	bu_free((void *)p1->mp_view_state, "usurp: p1 view_state");
    }
    p1->mp_view_state = p2->mp_view_state;

    /* Null out p2's pointers (they now belong to p1 or were freed). */
    p2->mp_adc_state      = NULL;
    p2->mp_menu_state     = NULL;
    p2->mp_rubber_band    = NULL;
    p2->mp_mged_variables = NULL;
    p2->mp_color_scheme   = NULL;
    p2->mp_grid_state     = NULL;
    p2->mp_axes_state     = NULL;
    p2->mp_dlist_state    = NULL;
    p2->mp_view_state     = NULL;
}


/*
 * - decrement the reference count of all resources (all 9, now pane-owned)
 * - free all resources that are not being used
 * Step 7.17: view_state also pane-owned; accessed via dlp->dm_pane->mp_view_state
 */
void
free_all_resources(struct mged_dm *dlp)
{
    struct mged_pane *pane = dlp->dm_pane;

    if (!pane) return;

    if (pane->mp_view_state && !--pane->mp_view_state->vs_rc) {
	view_ring_destroy(pane->mp_view_state);
	bu_free((void *)pane->mp_view_state, "free_all_resources: view_state");
    }
    pane->mp_view_state = NULL;

    if (pane->mp_adc_state      && !--pane->mp_adc_state->adc_rc)
	bu_free(pane->mp_adc_state,      "free_all_resources: adc_state");
    pane->mp_adc_state = NULL;

    if (pane->mp_menu_state     && !--pane->mp_menu_state->ms_rc)
	bu_free(pane->mp_menu_state,     "free_all_resources: menu_state");
    pane->mp_menu_state = NULL;

    if (pane->mp_rubber_band    && !--pane->mp_rubber_band->rb_rc)
	bu_free(pane->mp_rubber_band,    "free_all_resources: rubber_band");
    pane->mp_rubber_band = NULL;

    if (pane->mp_mged_variables && !--pane->mp_mged_variables->mv_rc)
	bu_free(pane->mp_mged_variables, "free_all_resources: mged_variables");
    pane->mp_mged_variables = NULL;

    if (pane->mp_color_scheme   && !--pane->mp_color_scheme->cs_rc)
	bu_free(pane->mp_color_scheme,   "free_all_resources: color_scheme");
    pane->mp_color_scheme = NULL;

    if (pane->mp_grid_state     && !--pane->mp_grid_state->rc)
	bu_free(pane->mp_grid_state,     "free_all_resources: grid_state");
    pane->mp_grid_state = NULL;

    if (pane->mp_axes_state     && !--pane->mp_axes_state->ax_rc)
	bu_free(pane->mp_axes_state,     "free_all_resources: axes_state");
    pane->mp_axes_state = NULL;
}


void
share_dlist(struct mged_dm *dlp2)
{
    /* Stage 7 (step 5.14): guard for NULL dm_dmp (initial "nu" mged_dm). */
    if (!dlp2->dm_dmp || !dm_get_displaylist(dlp2->dm_dmp))
	return;

    /* Step 6.b: search active_pane_set for matching legacy dm wrapper. */
    for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	if (!mp->mp_dm) continue;
	struct mged_dm *dlp1 = mp->mp_dm;
	if (dlp1 != dlp2 &&
	    dm_get_type(dlp1->dm_dmp) == dm_get_type(dlp2->dm_dmp) && dm_get_dname(dlp1->dm_dmp) && dm_get_dname(dlp2->dm_dmp) &&
	    !bu_vls_strcmp(dm_get_dname(dlp1->dm_dmp), dm_get_dname(dlp2->dm_dmp))) {
	    if (dm_share_dlist(dlp1->dm_dmp, dlp2->dm_dmp) == TCL_OK) {
		struct bu_vls vls = BU_VLS_INIT_ZERO;

		SHARE_RESOURCE(0, _dlist_state, mp_dlist_state, dl_rc, dlp1, dlp2, vls, "share: dlist_state");
		dlp1->dm_dirty = dlp2->dm_dirty = 1;
		dm_set_dirty(dlp1->dm_dmp, 1);
		dm_set_dirty(dlp2->dm_dmp, 1);
		bu_vls_free(&vls);
	    }

	    break;
	}
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
