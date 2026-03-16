/*                    T C L C A D / D R A W. H
 * BRL-CAD
 *
 * Copyright (c) 2004-2025 United States Government as represented by
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
/** @file tclcad/draw.h
 *
 * @brief
 *  Drawing header file for the BRL-CAD TclCAD Library, LIBTCLCAD.
 *
 */

#ifndef TCLCAD_DRAW_H
#define TCLCAD_DRAW_H

#include "common.h"
#include "tcl.h"
/* Stage 9: dm.h replaced with dm/fbserv.h — only struct fbserv_obj is needed
 * in this header (for struct tclcad_view_data::gdv_fbs).  The full dm.h
 * is no longer pulled transitively into every mged translation unit. */
#include "dm/fbserv.h"
#include "ged.h"
#include "tclcad/defines.h"

__BEGIN_DECLS

// Data specific to an individual view rather than the geometry database
// instance.
struct tclcad_view_data {
    struct ged		*gedp;
    struct bu_vls	gdv_edit_motion_delta_callback;
    int                 gdv_edit_motion_delta_callback_cnt;
    struct bu_vls	gdv_callback;
    int			gdv_callback_cnt;
    struct fbserv_obj	gdv_fbs;

    /* Obol rendering settings — stored here so the Tcl get/set commands
     * (bg, light, zbuffer, transparency, fontsize) have stable backing
     * storage regardless of whether a dm backend is present.  The Obol
     * renderer reads these from the obol_view Tk widget's update callback
     * via obol_notify_views.  Migration note: these replace the former
     * dm_get_bg / dm_set_bg / dm_get_light / ... dm_ API calls. */
    unsigned char	gdv_bg[3];	/**< @brief background RGB [0,255] */
    int			gdv_light;	/**< @brief headlight enable flag */
    int			gdv_zbuffer;	/**< @brief zbuffer enable flag */
    int			gdv_transparency; /**< @brief transparency enable */
    int			gdv_fontsize;	/**< @brief font size (0 = default) */
};

TCLCAD_EXPORT extern void go_refresh(struct ged *gedp,
				     bsg_view *gdvp);
TCLCAD_EXPORT extern void go_refresh_draw(struct ged *gedp,
					  bsg_view *gdvp,
					  int restore_zbuffer);
TCLCAD_EXPORT extern int go_view_axes(struct ged *gedp,
				      bsg_view *gdvp,
				      int argc,
				      const char *argv[],
				      const char *usage);
TCLCAD_EXPORT extern int go_data_labels(Tcl_Interp *interp,
					struct ged *gedp,
					bsg_view *gdvp,
					int argc,
					const char *argv[],
					const char *usage);
TCLCAD_EXPORT extern int go_data_arrows(Tcl_Interp *interp,
					struct ged *gedp,
					bsg_view *gdvp,
					int argc,
					const char *argv[],
					const char *usage);
TCLCAD_EXPORT extern int go_data_pick(struct ged *gedp,
				      bsg_view *gdvp,
				      int argc,
				      const char *argv[],
				      const char *usage);
TCLCAD_EXPORT extern int go_data_axes(Tcl_Interp *interp,
				      struct ged *gedp,
				      bsg_view *gdvp,
				      int argc,
				      const char *argv[],
				      const char *usage);
TCLCAD_EXPORT extern int go_data_lines(Tcl_Interp *interp,
				       struct ged *gedp,
				       bsg_view *gdvp,
				       int argc,
				       const char *argv[],
				       const char *usage);
TCLCAD_EXPORT extern int go_data_move(Tcl_Interp *interp,
				      struct ged *gedp,
				      bsg_view *gdvp,
				      int argc,
				      const char *argv[],
				      const char *usage);
TCLCAD_EXPORT extern int go_data_move_object_mode(Tcl_Interp *interp,
						  struct ged *gedp,
						  bsg_view *gdvp,
						  int argc,
						  const char *argv[],
						  const char *usage);
TCLCAD_EXPORT extern int go_data_move_point_mode(Tcl_Interp *interp,
						 struct ged *gedp,
						 bsg_view *gdvp,
						 int argc,
						 const char *argv[],
						 const char *usage);
TCLCAD_EXPORT extern int go_data_polygons(Tcl_Interp *interp,
					  struct ged *gedp,
					  bsg_view *gdvp,
					  int argc,
					  const char *argv[],
					  const char *usage);
TCLCAD_EXPORT extern int go_mouse_poly_circ(Tcl_Interp *interp,
					    struct ged *gedp,
					    bsg_view *gdvp,
					    int argc,
					    const char *argv[],
					    const char *usage);
TCLCAD_EXPORT extern int go_mouse_poly_cont(Tcl_Interp *interp,
					    struct ged *gedp,
					    bsg_view *gdvp,
					    int argc,
					    const char *argv[],
					    const char *usage);
TCLCAD_EXPORT extern int go_mouse_poly_ell(Tcl_Interp *interp,
					   struct ged *gedp,
					   bsg_view *gdvp,
					   int argc,
					   const char *argv[],
					   const char *usage);
TCLCAD_EXPORT extern int go_mouse_poly_rect(Tcl_Interp *interp,
					    struct ged *gedp,
					    bsg_view *gdvp,
					    int argc,
					    const char *argv[],
					    const char *usage);
TCLCAD_EXPORT extern int go_poly_circ_mode(Tcl_Interp *interp,
					   struct ged *gedp,
					   bsg_view *gdvp,
					   int argc,
					   const char *argv[],
					   const char *usage);
TCLCAD_EXPORT extern int go_poly_ell_mode(Tcl_Interp *interp,
					  struct ged *gedp,
					  bsg_view *gdvp,
					  int argc,
					  const char *argv[],
					  const char *usage);
TCLCAD_EXPORT extern int go_poly_rect_mode(Tcl_Interp *interp,
					   struct ged *gedp,
					   bsg_view *gdvp,
					   int argc,
					   const char *argv[],
					   const char *usage);
TCLCAD_EXPORT extern int go_run_tclscript(Tcl_Interp *interp,
					  const char *tclscript,
					  struct bu_vls *result_str);
TCLCAD_EXPORT extern int go_poly_cont_build(Tcl_Interp *interp,
					    struct ged *gedp,
					    bsg_view *gdvp,
					    int argc,
					    const char *argv[],
					    const char *usage);
TCLCAD_EXPORT extern int go_poly_cont_build_end(Tcl_Interp *UNUSED(interp),
						struct ged *gedp,
						bsg_view *gdvp,
						int argc,
						const char *argv[],
						const char *usage);

/* dm_tcl.c */
/* The presence of Tcl_Interp as an arg prevents giving arg list */
TCLCAD_EXPORT extern void fb_tcl_setup(void);

__END_DECLS

#endif /* TCLCAD_MISC_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
