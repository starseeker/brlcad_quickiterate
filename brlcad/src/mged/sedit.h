/*                         S E D I T . H
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
/** @file mged/sedit.h
 *
 * This header file contains the esolid structure definition,
 * which holds all the information necessary for solid editing.
 * Storage is actually allocated in edsol.c
 *
 */

#ifndef MGED_SEDIT_H
#define MGED_SEDIT_H

#include "rt/edit.h"
#include "rt/rt_ecmds.h"
#include "rt/primitives/arb8.h"
#include "rt/primitives/ars.h"
#include "rt/primitives/bot.h"
#include "rt/primitives/cline.h"
#include "rt/primitives/dsp.h"
#include "rt/primitives/ell.h"
#include "rt/primitives/epa.h"
#include "rt/primitives/metaball.h"
#include "rt/primitives/nmg.h"
#include "rt/primitives/pipe.h"
#include "rt/primitives/rhc.h"
#include "rt/primitives/rpc.h"
#include "rt/primitives/tgc.h"
#include "rt/primitives/tor.h"
#include "mged.h"

#define MGED_SMALL_SCALE 1.0e-10

/* These EDIT_CLASS_ values go in es_edclass. */
#define EDIT_CLASS_NULL 0
#define EDIT_CLASS_TRAN 1
#define EDIT_CLASS_ROTATE 2
#define EDIT_CLASS_SCALE 3

/* ECMD_* primitive edit command codes are defined in rt/primitives/ headers */
/* EARB, PTARB, ECMD_ARB_* are defined in rt/primitives/arb8.h */
/* ECMD_VTRANS is defined in rt/geom.h */

/* SEDIT_ROTATE: any solid-edit rotation mode.
 * edit_mode is set to RT_PARAMS_EDIT_ROT for all primitive rotate operations
 * (librt ft_edit_set_edit_mode), and RT_MATRIX_EDIT_ROT for matrix rotation. */
#define SEDIT_ROTATE (s->global_editing_state == ST_S_EDIT && \
		      (MEDIT(s)->edit_mode == RT_PARAMS_EDIT_ROT || \
		       MEDIT(s)->edit_mode == RT_MATRIX_EDIT_ROT))
#define OEDIT_ROTATE (s->global_editing_state == ST_O_EDIT && \
		      edobj == BE_O_ROTATE)
#define EDIT_ROTATE (SEDIT_ROTATE || OEDIT_ROTATE)

/* SEDIT_SCALE: any solid-edit scaling mode.
 * edit_mode is set to RT_PARAMS_EDIT_SCALE for all primitive scale operations
 * (librt ft_edit_set_edit_mode), and RT_MATRIX_EDIT_SCALE* for matrix scaling. */
#define SEDIT_SCALE (s->global_editing_state == ST_S_EDIT && \
		     (MEDIT(s)->edit_mode == RT_PARAMS_EDIT_SCALE || \
		      MEDIT(s)->edit_mode == RT_MATRIX_EDIT_SCALE || \
		      MEDIT(s)->edit_mode == RT_MATRIX_EDIT_SCALE_X || \
		      MEDIT(s)->edit_mode == RT_MATRIX_EDIT_SCALE_Y || \
		      MEDIT(s)->edit_mode == RT_MATRIX_EDIT_SCALE_Z))
#define OEDIT_SCALE (s->global_editing_state == ST_O_EDIT && \
		     (edobj == BE_O_XSCALE || \
		      edobj == BE_O_YSCALE || \
		      edobj == BE_O_ZSCALE || \
		      edobj == BE_O_SCALE))
#define EDIT_SCALE (SEDIT_SCALE || OEDIT_SCALE)

/* SEDIT_TRAN: any solid-edit translation mode.
 * edit_mode is set to RT_PARAMS_EDIT_TRANS for all primitive translate operations
 * (librt ft_edit_set_edit_mode), and RT_MATRIX_EDIT_TRANS_* for matrix translation. */
#define SEDIT_TRAN (s->global_editing_state == ST_S_EDIT && \
		    (MEDIT(s)->edit_mode == RT_PARAMS_EDIT_TRANS || \
		     MEDIT(s)->edit_mode == RT_MATRIX_EDIT_TRANS_VIEW_XY || \
		     MEDIT(s)->edit_mode == RT_MATRIX_EDIT_TRANS_VIEW_X || \
		     MEDIT(s)->edit_mode == RT_MATRIX_EDIT_TRANS_VIEW_Y))
#define OEDIT_TRAN (s->global_editing_state == ST_O_EDIT && \
		    (edobj == BE_O_X || \
		     edobj == BE_O_Y || \
		     edobj == BE_O_XY))
#define EDIT_TRAN (SEDIT_TRAN || OEDIT_TRAN)

#define SEDIT_PICK (s->global_editing_state == ST_S_EDIT && \
		    MEDIT(s)->edit_mode == RT_PARAMS_EDIT_PICK)



// NMG editing vars




extern void
get_solid_keypoint(struct mged_state *s, fastf_t *pt, const char **strp, struct rt_db_internal *ip, fastf_t *mat);

extern void set_e_axes_pos(struct mged_state *s, int both);


#endif /* MGED_SEDIT_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
