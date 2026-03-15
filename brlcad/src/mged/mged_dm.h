/*			M G E D _ D M . H
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
/** @file mged/mged_dm.h
 *
 * Header file for MGED's per-pane display state.
 *
 * MIGRATION NOTE (Stage 7 — libdm removal):
 *
 * The central struct here, `mged_dm`, currently owns both the libdm display
 * manager (`dm_dmp`) and a large collection of per-pane overlay/UI state.
 * It is the primary reason MGED still depends on libdm.
 *
 * Stage 7 (Steps 6.a-6.c) replaces `mged_dm` + `active_dm_set` with a leaner
 * `mged_pane` struct + `active_pane_set` (see RADICAL_MIGRATION.md).
 * All `active_dm_set` iteration loops are now migrated; `active_dm_set` is no
 * longer maintained.  Next step: delete `struct mged_dm`, the DMP/fbp macros,
 * and `src/mged/dm-generic.c` (Step 7).
 */

#ifndef MGED_MGED_DM_H
#define MGED_MGED_DM_H

#include "common.h"

#include "dm.h"	/* struct dm */

#include "pkg.h" /* struct pkg_conn */
#include "ged.h"

#include "mged.h"

struct scroll_item {
    char *scroll_string;
    void (*scroll_func)(struct scroll_item *, double);
    int scroll_val;
    char *scroll_cmd;
};

#ifndef COMMA
#  define COMMA ','
#endif

#define MGED_DISPLAY_VAR "mged_display"

/* +-2048 to +-1 */
#define GED2PM1(x) (((fastf_t)(x))*INV_BV)

#define LAST_SOLID(_sp)       DB_FULL_PATH_CUR_DIR( &(_sp)->s_fullpath )

#define AMM_IDLE 0
#define AMM_ROT 1
#define AMM_TRAN 2
#define AMM_SCALE 3
#define AMM_ADC_ANG1 4
#define AMM_ADC_ANG2 5
#define AMM_ADC_TRAN 6
#define AMM_ADC_DIST 7
#define AMM_CON_ROT_X 8
#define AMM_CON_ROT_Y 9
#define AMM_CON_ROT_Z 10
#define AMM_CON_TRAN_X 11
#define AMM_CON_TRAN_Y 12
#define AMM_CON_TRAN_Z 13
#define AMM_CON_SCALE_X 14
#define AMM_CON_SCALE_Y 15
#define AMM_CON_SCALE_Z 16
#define AMM_CON_XADC 17
#define AMM_CON_YADC 18
#define AMM_CON_ANG1 19
#define AMM_CON_ANG2 20
#define AMM_CON_DIST 21

struct view_ring {
    struct bu_list	l;
    mat_t			vr_rot_mat;
    mat_t			vr_tvc_mat;
    fastf_t		vr_scale;
    int			vr_id;
};


#define NUM_TRAILS 8
#define MAX_TRAIL 32
struct trail {
    int		t_cur_index;      /* index of first free entry */
    int		t_nused;          /* max index in use */
    point_t	t_pt[MAX_TRAIL];
};


#ifdef MAX_CLIENTS
#	undef MAX_CLIENTS
#	define MAX_CLIENTS 32
#else
#	define MAX_CLIENTS 32
#endif

struct client {
    int			c_fd;
#ifdef USE_TCL_CHAN
    Tcl_Channel         c_chan;
    Tcl_FileProc        *c_handler;
#endif
    struct pkg_conn	*c_pkg;
};


/* mged command variables for affecting the user environment */
struct _mged_variables {
    int		mv_rc;
    int		mv_autosize;
    int		mv_rateknobs;
    int		mv_sliders;
    int		mv_faceplate;
    int		mv_orig_gui;
    int		mv_linewidth;
    char	mv_linestyle;
    int		mv_hot_key;
    int		mv_context;
    int		mv_dlist;
    int		mv_use_air;
    int		mv_listen;			/* nonzero to listen on port */
    int		mv_port;			/* port to listen on */
    int		mv_fb;				/* toggle image on/off */
    int		mv_fb_all;			/* 0 - use part of image as defined by the rectangle
						   1 - use the entire image */
    int		mv_fb_overlay;			/* 0 - underlay    1 - interlay    2 - overlay */
    char	mv_mouse_behavior;
    char	mv_coords;
    char	mv_rotate_about;
    char	mv_transform;
    int		mv_predictor;
    double	mv_predictor_advance;
    double	mv_predictor_length;
    double	mv_perspective;			/* used to directly set the perspective angle */
    int		mv_perspective_mode;		/* used to toggle perspective viewing on/off */
    int		mv_toggle_perspective;		/* used to toggle through values in perspective_table[] */
    double	mv_nmg_eu_dist;
    double	mv_eye_sep_dist;		/* >0 implies stereo.  units = "room" mm */
    char	mv_union_lexeme[1024];
    char	mv_intersection_lexeme[1024];
    char	mv_difference_lexeme[1024];
};


struct _axes_state {
    int		ax_rc;
    int		ax_model_draw;			/* model axes */
    int		ax_model_size;
    int		ax_model_linewidth;
    fastf_t	ax_model_pos[3];
    int		ax_view_draw;			/* view axes */
    int		ax_view_size;
    int		ax_view_linewidth;
    int		ax_view_pos[2];
    int		ax_edit_draw;			/* edit axes */
    int		ax_edit_size1;
    int		ax_edit_size2;
    int		ax_edit_linewidth1;
    int		ax_edit_linewidth2;
};


struct _dlist_state {
    int		dl_rc;
    int		dl_active;	/* 1 - actively using display lists */
    int		dl_flag;
};

struct _adc_state {
    int		adc_rc;
    int		adc_draw;
    int		adc_dv_x;
    int		adc_dv_y;
    int		adc_dv_a1;
    int		adc_dv_a2;
    int		adc_dv_dist;
    fastf_t	adc_pos_model[3];
    fastf_t	adc_pos_view[3];
    fastf_t	adc_pos_grid[3];
    fastf_t	adc_a1;
    fastf_t	adc_a2;
    fastf_t	adc_dst;
    int		adc_anchor_pos;
    int		adc_anchor_a1;
    int		adc_anchor_a2;
    int		adc_anchor_dst;
    fastf_t	adc_anchor_pt_a1[3];
    fastf_t	adc_anchor_pt_a2[3];
    fastf_t	adc_anchor_pt_dst[3];
};


struct _rubber_band {
    int		rb_rc;
    int		rb_active;	/* 1 - actively drawing a rubber band */
    int		rb_draw;	/* draw rubber band rectangle */
    int		rb_linewidth;
    char	rb_linestyle;
    int		rb_pos[2];	/* Position in image coordinates */
    int		rb_dim[2];	/* Rectangle dimension in image coordinates */
    fastf_t	rb_x;		/* Corner of rectangle in normalized     */
    fastf_t	rb_y;		/* ------ view coordinates (i.e. +-1.0). */
    fastf_t	rb_width;	/* Width and height of rectangle in      */
    fastf_t	rb_height;	/* ------ normalized view coordinates.   */
};


struct _view_state {
    int		vs_rc;
    int		vs_flag;

    bsg_view *vs_gvp;
    mat_t	vs_model2objview;
    mat_t	vs_objview2model;
    mat_t	vs_ModelDelta;		/* changes to Viewrot this frame */

    struct view_ring	vs_headView;
    struct view_ring	*vs_current_view;
    struct view_ring	*vs_last_view;

    /* Rate stuff */
    bsg_knobs k;

    /* Virtual trackball stuff */
    point_t	vs_orig_pos;
};


struct _color_scheme {
    int	cs_rc;
    int	cs_mode;
    int	cs_bg[3];
    int	cs_bg_a[3];
    int	cs_bg_ia[3];
    int	cs_adc_line[3];
    int	cs_adc_line_a[3];
    int	cs_adc_line_ia[3];
    int	cs_adc_tick[3];
    int	cs_adc_tick_a[3];
    int	cs_adc_tick_ia[3];
    int	cs_geo_def[3];
    int	cs_geo_def_a[3];
    int	cs_geo_def_ia[3];
    int	cs_geo_hl[3];
    int	cs_geo_hl_a[3];
    int	cs_geo_hl_ia[3];
    int	cs_geo_label[3];
    int	cs_geo_label_a[3];
    int	cs_geo_label_ia[3];
    int	cs_model_axes[3];
    int	cs_model_axes_a[3];
    int	cs_model_axes_ia[3];
    int	cs_model_axes_label[3];
    int	cs_model_axes_label_a[3];
    int	cs_model_axes_label_ia[3];
    int	cs_view_axes[3];
    int	cs_view_axes_a[3];
    int	cs_view_axes_ia[3];
    int	cs_view_axes_label[3];
    int	cs_view_axes_label_a[3];
    int	cs_view_axes_label_ia[3];
    int	cs_edit_axes1[3];
    int	cs_edit_axes1_a[3];
    int	cs_edit_axes1_ia[3];
    int	cs_edit_axes_label1[3];
    int	cs_edit_axes_label1_a[3];
    int	cs_edit_axes_label1_ia[3];
    int	cs_edit_axes2[3];
    int	cs_edit_axes2_a[3];
    int	cs_edit_axes2_ia[3];
    int	cs_edit_axes_label2[3];
    int	cs_edit_axes_label2_a[3];
    int	cs_edit_axes_label2_ia[3];
    int	cs_rubber_band[3];
    int	cs_rubber_band_a[3];
    int	cs_rubber_band_ia[3];
    int	cs_grid[3];
    int	cs_grid_a[3];
    int	cs_grid_ia[3];
    int	cs_predictor[3];
    int	cs_predictor_a[3];
    int	cs_predictor_ia[3];
    int	cs_menu_line[3];
    int	cs_menu_line_a[3];
    int	cs_menu_line_ia[3];
    int	cs_slider_line[3];
    int	cs_slider_line_a[3];
    int	cs_slider_line_ia[3];
    int	cs_other_line[3];
    int	cs_other_line_a[3];
    int	cs_other_line_ia[3];
    int	cs_status_text1[3];
    int	cs_status_text1_a[3];
    int	cs_status_text1_ia[3];
    int	cs_status_text2[3];
    int	cs_status_text2_a[3];
    int	cs_status_text2_ia[3];
    int	cs_slider_text1[3];
    int	cs_slider_text1_a[3];
    int	cs_slider_text1_ia[3];
    int	cs_slider_text2[3];
    int	cs_slider_text2_a[3];
    int	cs_slider_text2_ia[3];
    int	cs_menu_text1[3];
    int	cs_menu_text1_a[3];
    int	cs_menu_text1_ia[3];
    int	cs_menu_text2[3];
    int	cs_menu_text2_a[3];
    int	cs_menu_text2_ia[3];
    int	cs_menu_title[3];
    int	cs_menu_title_a[3];
    int	cs_menu_title_ia[3];
    int	cs_menu_arrow[3];
    int	cs_menu_arrow_a[3];
    int	cs_menu_arrow_ia[3];
    int	cs_state_text1[3];
    int	cs_state_text1_a[3];
    int	cs_state_text1_ia[3];
    int	cs_state_text2[3];
    int	cs_state_text2_a[3];
    int	cs_state_text2_ia[3];
    int	cs_edit_info[3];
    int	cs_edit_info_a[3];
    int	cs_edit_info_ia[3];
    int	cs_center_dot[3];
    int	cs_center_dot_a[3];
    int	cs_center_dot_ia[3];
};


struct _menu_state {
    int	ms_rc;
    int	ms_flag;
    int	ms_top;
    int	ms_cur_menu;
    int	ms_cur_item;
    struct menu_item	*ms_menus[NMENU];    /* base of menu items array */
};


struct mged_dm {
    struct dm		*dm_dmp;
    struct fb		*dm_fbp;
    int			dm_netfd;			/* socket used to listen for connections */
#ifdef USE_TCL_CHAN
    Tcl_Channel		dm_netchan;
#endif
    struct client	dm_clients[MAX_CLIENTS];
    int			dm_dirty;			/* true if received an expose or configuration event */
    int			dm_mapped;
    int			dm_owner;			/* true if owner of the view info */
    int			dm_am_mode;			/* alternate mouse mode */
    int			dm_ndrawn;
    int			dm_perspective_angle;
    int			*dm_zclip_ptr;
    struct bu_list	dm_p_vlist;			/* predictor vlist */
    struct trail	dm_trails[NUM_TRAILS];
    struct cmd_list	*dm_tie;

    int			dm_adc_auto;
    int			dm_grid_auto_size;
    int			_dm_mouse_dx;
    int			_dm_mouse_dy;
    int			_dm_omx;
    int			_dm_omy;
    int			_dm_knobs[8];
    point_t		_dm_work_pt;

    /* Tcl variable names for display info */
    struct bu_vls	dm_fps_name;
    struct bu_vls	dm_aet_name;
    struct bu_vls	dm_ang_name;
    struct bu_vls	dm_center_name;
    struct bu_vls	dm_size_name;
    struct bu_vls	dm_adc_name;

    /* Slider stuff */
    int			dm_scroll_top;
    int			dm_scroll_active;
    int			dm_scroll_y;
    struct scroll_item	*dm_scroll_array[6];

    /* Shareable Resources */
    struct _view_state	*dm_view_state;
    struct _adc_state	*dm_adc_state;
    struct _menu_state	*dm_menu_state;
    struct _rubber_band	*dm_rubber_band;
    struct _mged_variables *dm_mged_variables;
    struct _color_scheme	*dm_color_scheme;
    struct bsg_grid_state *dm_grid_state;
    struct _axes_state	*dm_axes_state;
    struct _dlist_state	*dm_dlist_state;

    /* Hooks */
    int			(*dm_cmd_hook)(int, const char **, void *);
    void			(*dm_viewpoint_hook)(void);
    int			(*dm_eventHandler)(void);
};

#define MGED_DM_NULL ((struct mged_dm *)NULL)

/* -----------------------------------------------------------------------
 * Stage 7 — MGED libdm removal: mged_pane + active_pane_set
 *
 * `mged_pane` is the lightweight per-pane struct that replaces `mged_dm`
 * once libdm is fully removed from MGED.  It carries only the non-dm
 * per-pane state.  `active_pane_set` (a bu_ptbl of mged_pane*) replaces
 * `active_dm_set`.  Step 6.c done: `active_dm_set` is no longer maintained.
 *
 *  - Legacy dm panes are tracked via their `mged_pane` wrapper (mp_dm != NULL).
 *  - Obol panes created by `f_new_obol_view_ptr` have mp_dm == NULL and are
 *    registered in both `active_pane_set` AND `ged_views`.
 *
 * `set_curr_pane()` sets `s->gedp->ged_gvp` from `mp->mp_gvp`, sets
 * `s->mged_curr_pane = mp`, AND redirects `s->mged_curr_dm` to the headless
 * "nu" init dm (`mged_dm_init_state`) so that `DMP == NULL` and all legacy
 * libdm drawing guards fire cleanly.  The ternary macros prefer `mp->mp_*`
 * because `mged_curr_pane` is now non-NULL.  Next: remove `struct mged_dm`
 * and the DMP macros (Step 7).
 *
 * See RADICAL_MIGRATION.md, "MGED refactoring for libdm removal", steps 2-6.
 * ----------------------------------------------------------------------- */

struct mged_pane {
    bsg_view          *mp_gvp;       /* the view this pane displays (dmp == NULL for Obol) */
    struct cmd_list   *mp_cmd_tie;   /* Tcl command-history link (mirrors dm_tie) */
    struct bu_list     mp_p_vlist;   /* predictor vlist (mirrors dm_p_vlist) */
    struct trail       mp_trails[NUM_TRAILS]; /* predictor trails (mirrors dm_trails) */
    int                mp_ndrawn;    /* count of objects drawn (mirrors dm_ndrawn) */

    /* Stage 7 Step 6.a: back-pointer to the legacy mged_dm for "thin wrapper"
     * panes created by mged_attach() when the user runs "attach ogl" etc.
     * NULL for Obol panes created by f_new_obol_view_ptr().
     *
     * When mp_dm != NULL this mged_pane is a thin wrapper: the mp_* resource
     * pointers below are SHARED with the mged_dm (not separately allocated).
     * set_curr_pane() redirects s->mged_curr_dm to mp_dm for these panes so
     * that DMP is non-NULL and legacy GL drawing still works.
     * mged_pane_free_resources() skips freeing the shared pointers. */
    struct mged_dm    *mp_dm;

    /* Per-pane state (mirrors the shareable resources in mged_dm).  For Obol
     * panes (mp_dm == NULL) these are allocated and initialized by
     * mged_pane_init_resources() and freed by mged_pane_free_resources().
     * For legacy dm wrappers (mp_dm != NULL) these point directly to the
     * corresponding dm_* fields of mp_dm and must NOT be freed here. */
    struct _view_state      *mp_view_state;
    struct _adc_state       *mp_adc_state;
    struct _menu_state      *mp_menu_state;
    struct _rubber_band     *mp_rubber_band;
    struct _mged_variables  *mp_mged_variables;
    struct _color_scheme    *mp_color_scheme;
    struct bsg_grid_state   *mp_grid_state;
    struct _axes_state      *mp_axes_state;
    struct _dlist_state     *mp_dlist_state;

    /* Tcl display variable names (mirrors dm_fps_name, dm_aet_name, etc.).
     * Initialized by mged_pane_link_vars() when the pane is registered.
     * Used by dotitles() (Stage 7: currently dotitles() skips Obol panes
     * via the !DMP guard; these fields prepare for a future obol_dotitles). */
    struct bu_vls   mp_fps_name;    /* "$::mged_display(%path,fps)" */
    struct bu_vls   mp_aet_name;    /* "$::mged_display(%path,aet)" */
    struct bu_vls   mp_ang_name;    /* "$::mged_display(%path,ang)" */
    struct bu_vls   mp_center_name; /* "$::mged_display(%path,center)" */
    struct bu_vls   mp_size_name;   /* "$::mged_display(%path,size)" */
    struct bu_vls   mp_adc_name;    /* "$::mged_display(%path,adc)" */
};

#define MGED_PANE_NULL ((struct mged_pane *)NULL)

extern struct bu_ptbl active_pane_set;       /* defined in attach.c */
extern void set_curr_pane(struct mged_state *s, struct mged_pane *mp);
extern void mged_pane_link_vars(struct mged_pane *mp);  /* in attach.c */
extern void obol_update_title_vars(struct mged_state *s, struct mged_pane *mp);  /* in titles.c */

/**
 * Find the mged_pane in active_pane_set whose gv_name matches `name`.
 * Step 6.a: active_pane_set covers both Obol panes and legacy dm wrappers.
 * Returns MGED_PANE_NULL if not found.
 */
extern struct mged_pane *mged_pane_find_by_name(const char *name);

/**
 * Release an Obol pane: remove it from active_pane_set and free it.
 * Call this when the corresponding obol_view Tk widget is destroyed.
 * The caller is responsible for destroying the bsg_view separately
 * (it is tracked in ged_free_views).
 */
extern void mged_pane_release(struct mged_pane *mp);

/**
 * Allocate and initialize the per-pane overlay state (mp_view_state,
 * mp_color_scheme, etc.) for an Obol mged_pane.  Copies initial values
 * from mged_dm_init_state so the new pane starts with the same defaults
 * as a legacy dm pane.  Must be called after mged_dm_init_state has been
 * populated (i.e. after the nu-dm is set up in mged main()).
 */
extern void mged_pane_init_resources(struct mged_state *s, struct mged_pane *mp);

/**
 * Free the per-pane overlay state allocated by mged_pane_init_resources().
 * Safe to call even if init_resources was never called (all pointers are NULL).
 */
extern void mged_pane_free_resources(struct mged_pane *mp);

/* Step 7.9: DMP is now a conditional expression through mged_curr_pane.
 * For Obol panes (mp_dm == NULL), DMP evaluates to NULL so all legacy
 * "if (!DMP) return;" guards fire cleanly without NULL pointer dereference.
 * For legacy dm wrapper panes (mp_dm != NULL), DMP gives the real dm pointer.
 * The single lvalue use of DMP (dm_open assignment in mged_dm_init) was
 * replaced with explicit s->mged_curr_dm->dm_dmp access (Step 7.9).
 * DMP is still used as an lvalue inside release() and other lifecycle
 * functions where mged_curr_pane is correctly set. */
#define DMP (s->mged_curr_pane->mp_dm ? s->mged_curr_pane->mp_dm->dm_dmp : (struct dm *)NULL)
#define DMP_dirty s->mged_curr_pane->mp_dm->dm_dirty
#define fbp s->mged_curr_pane->mp_dm->dm_fbp
#define clients s->mged_curr_pane->mp_dm->dm_clients
#define mapped s->mged_curr_pane->mp_dm->dm_mapped
#define owner s->mged_curr_pane->mp_dm->dm_owner
#define am_mode s->mged_curr_pane->mp_dm->dm_am_mode
#define perspective_angle s->mged_curr_pane->mp_dm->dm_perspective_angle
#define zclip_ptr s->mged_curr_pane->mp_dm->dm_zclip_ptr

/* Step 7.2: mged_curr_pane is always non-NULL after startup (init_pane created
 * in mged_main before any attach).  Direct mp_* access — no ternary fallback
 * to mged_curr_dm->dm_* needed.  dm_var_init and the mged.c startup block
 * both use explicit s->mged_curr_dm->dm_<field> access, so they are unaffected
 * by these macros becoming non-ternary. */
#define view_state s->mged_curr_pane->mp_view_state
#define adc_state s->mged_curr_pane->mp_adc_state
#define menu_state s->mged_curr_pane->mp_menu_state
#define rubber_band s->mged_curr_pane->mp_rubber_band
#define mged_variables s->mged_curr_pane->mp_mged_variables
#define color_scheme s->mged_curr_pane->mp_color_scheme
#define grid_state s->mged_curr_pane->mp_grid_state
#define axes_state s->mged_curr_pane->mp_axes_state
#define dlist_state s->mged_curr_pane->mp_dlist_state

/* Step 7.8: pv_head / pane_trails simplified from ternary to always use pane
 * fields.  mged_pane_init_resources() initialises mp_p_vlist and mp_trails
 * for BOTH Obol panes and legacy dm wrapper panes (via predictor_init_pane),
 * so the pane's fields are always valid.  mged_dm_init() / dm_var_init()
 * no longer call predictor_init(s) before the wrapper pane is registered;
 * predictor_init_pane(pane) is called inside mged_pane_init_resources()
 * after the wrapper pane is fully constructed.
 *
 * dm_p_vlist is kept BU_LIST_INIT'd in mged_attach() so that the legacy
 * BSG_FREE_VLIST call in release() / mged_finish() remains a safe no-op. */
#define pv_head (&s->mged_curr_pane->mp_p_vlist)
#define pane_trails (s->mged_curr_pane->mp_trails)

/* Step 7.8: cmd_hook / viewpoint_hook / eventHandler through pane. */
#define cmd_hook s->mged_curr_pane->mp_dm->dm_cmd_hook
#define viewpoint_hook s->mged_curr_pane->mp_dm->dm_viewpoint_hook
#define eventHandler s->mged_curr_pane->mp_dm->dm_eventHandler

#define adc_auto s->mged_curr_pane->mp_dm->dm_adc_auto
#define grid_auto_size s->mged_curr_pane->mp_dm->dm_grid_auto_size

/* Names of macros must be different than actual struct element */
#define dm_mouse_dx s->mged_curr_pane->mp_dm->_dm_mouse_dx
#define dm_mouse_dy s->mged_curr_pane->mp_dm->_dm_mouse_dy
#define dm_omx s->mged_curr_pane->mp_dm->_dm_omx
#define dm_omy s->mged_curr_pane->mp_dm->_dm_omy
#define dm_knobs s->mged_curr_pane->mp_dm->_dm_knobs
#define dm_work_pt s->mged_curr_pane->mp_dm->_dm_work_pt

#define scroll_top s->mged_curr_pane->mp_dm->dm_scroll_top
#define scroll_active s->mged_curr_pane->mp_dm->dm_scroll_active
#define scroll_y s->mged_curr_pane->mp_dm->dm_scroll_y
#define scroll_array s->mged_curr_pane->mp_dm->dm_scroll_array

#define VIEWSIZE	(view_state->vs_gvp->gv_size)	/* Width of viewing cube */
#define VIEWFACTOR	(1/view_state->vs_gvp->gv_scale)

#define RATE_ROT_FACTOR 6.0
#define ABS_ROT_FACTOR 180.0
#define ADC_ANGLE_FACTOR 45.0

/*
 * Definitions for dealing with the buttons and lights.
 * BV are for viewing, and BE are for editing functions.
 */
#define LIGHT_OFF	0
#define LIGHT_ON	1
#define LIGHT_RESET	2		/* all lights out */

/* Function button/light codes.  Note that code 0 is reserved */
#define BV_TOP		15+16
#define BV_BOTTOM	14+16
#define BV_RIGHT	13+16
#define BV_LEFT		12+16
#define BV_FRONT	11+16
#define BV_REAR		10+16
#define BV_VRESTORE	9+16
#define BV_VSAVE	8+16
#define BE_O_ILLUMINATE	7+16
#define BE_O_SCALE	6+16
#define BE_O_X		5+16
#define BE_O_Y		4+16
#define BE_O_XY		3+16
#define BE_O_ROTATE	2+16
#define BE_ACCEPT	1+16
#define BE_REJECT	0+16

#define BE_S_EDIT	14
#define BE_S_ROTATE	13
#define BE_S_TRANS	12
#define BE_S_SCALE	11
#define BE_MENU		10
#define BV_ADCURSOR	9
#define BV_RESET	8
#define BE_S_ILLUMINATE	7
#define BE_O_XSCALE	6
#define BE_O_YSCALE	5
#define BE_O_ZSCALE	4
#define BV_ZOOM_IN	3
#define BV_ZOOM_OUT	2
#define BV_45_45	1
#define BV_35_25	0+32

#define BV_RATE_TOGGLE	1+32
#define BV_EDIT_TOGGLE  2+32
#define BV_EYEROT_TOGGLE 3+32
#define BE_S_CONTEXT    4+32

#define BV_MAXFUNC	64	/* largest code used */

#define GET_MGED_DM(p, id) { \
    \
    (p) = MGED_DM_NULL; \
    for (size_t dm_ind = 0; dm_ind < BU_PTBL_LEN(&active_pane_set); dm_ind++) { \
	struct mged_pane *_mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, dm_ind); \
	if (_mp->mp_dm && (id) == dm_get_id(_mp->mp_dm->dm_dmp)) { \
	    (p) = _mp->mp_dm; \
	    break; \
	} \
    } \
    \
}

/* Step 7.5: pane-based variant of GET_MGED_DM for doevent.c migration.
 * Finds the mged_pane whose legacy dm window-id matches `id`. */
#define GET_MGED_PANE(p, id) { \
    \
    (p) = MGED_PANE_NULL; \
    for (size_t dm_ind = 0; dm_ind < BU_PTBL_LEN(&active_pane_set); dm_ind++) { \
	struct mged_pane *_mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, dm_ind); \
	if (_mp->mp_dm && _mp->mp_dm->dm_dmp && \
	    (id) == dm_get_id(_mp->mp_dm->dm_dmp)) { \
	    (p) = _mp; \
	    break; \
	} \
    } \
    \
}

extern double frametime;		/* defined in mged.c */
extern int dm_pipe[];			/* defined in mged.c */
extern int update_views;		/* defined in mged.c */
/* active_dm_set removed (Step 6.c); use active_pane_set instead */
extern struct mged_dm *mged_dm_init_state;

/* defined in doevent.c */
#ifdef HAVE_X11_TYPES
extern int doEvent(ClientData, XEvent *);
#else
extern int doEvent(ClientData, void *);
#endif

/* defined in attach.c */
extern void dm_var_init(struct mged_state *s, struct mged_dm *target_dm, struct mged_dm *ndm);

/* defined in dm-generic.c */
extern int common_dm(struct mged_state *s, int argc, const char *argv[]);
extern void view_state_flag_hook(const struct bu_structparse *, const char *, void *,const char *, void *);
extern void dirty_hook(const struct bu_structparse *, const char *, void *,const char *, void *);
extern void zclip_hook(const struct bu_structparse *, const char *, void *,const char *, void *);

/* external sp_hook functions */
extern void cs_set_bg(const struct bu_structparse *, const char *, void *, const char *, void *); /* defined in color_scheme.c */

/* defined in setup.c */
extern void mged_rtCmdNotify(int);

/* indices into which_dm[] */
#define DM_PLOT_INDEX 0
#define DM_PS_INDEX 1

struct mged_view_hook_state {
    struct mged_state    *hs_s;          /* back-pointer to session state (Step 6.a) */
    struct dm *hs_dmp;
    struct _view_state *vs;
    int *dirty_global;
};
extern void *set_hook_data(struct mged_state *s, struct mged_view_hook_state *hs);

int dm_commands(int argc, const char *argv[], void *data);


#endif /* MGED_MGED_DM_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
