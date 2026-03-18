/*                     P R E D I C T O R . C
 * BRL-CAD
 *
 * Copyright (c) 1992-2025 United States Government as represented by
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
/** @file mged/predictor.c
 *
 * Put a predictor frame into view, as an aid to velocity-based
 * navigation through an MGED model.
 *
 * Inspired by the paper "Manipulating the Future: Predictor Based
 * Feedback for Velocity Control in Virtual Environment Navigation" by
 * Dale Chapman and Colin Ware, <cware@unb.ca>, in ACM SIGGRAPH
 * Computer Graphics Special Issue on 1992 Symposium on Interactive 3D
 * Graphics.
 *
 */

#include "common.h"

#include <string.h>
#include <math.h>

#include "vmath.h"
#include "bn.h"
#include "ged.h"

#include "./mged.h"
#include "bsg/util.h"
#include "./cmd.h"
#include "./mged_dm.h"

static void
init_trail(struct trail *tp)
{
    tp->t_cur_index = 0;
    tp->t_nused = 0;
}


void
predictor_init(struct mged_state *s)
{
    int i;

    for (i = 0; i < NUM_TRAILS; ++i)
	init_trail(&pane_trails[i]);
}


/* Step 5.15: initialize predictor trails for an Obol mged_pane.
 * Called from mged_pane_init_resources() (attach.c) when a new Obol pane
 * is registered.  Initializes the eight trail-history arrays embedded in
 * mp->mp_trails so predictor_frame() can safely push trail points from
 * the first frame onward.  The companion mp_p_vlist is initialized via
 * BU_LIST_INIT in mged_pane_init_resources() and freed in mged_pane_release().
 */
void
predictor_init_pane(struct mged_pane *mp)
{
    for (int i = 0; i < NUM_TRAILS; ++i)
	init_trail(&mp->mp_trails[i]);
}


void
predictor_kill(struct mged_state *s)
{
    BSG_FREE_VLIST(s->vlfree, pv_head);
    predictor_init(s);
}


#define TF_BORD 0.01
#define TF_X 0.14
#define TF_Y 0.07
#define TF_Z (1.0-0.15)	/* To prevent Z clipping of TF_X */

#define TF_VL(_m, _v) \
	{ vect_t edgevect_m; \
	MAT4X3VEC(edgevect_m, predictorXv2m, _v); \
	VADD2(_m, framecenter_m, edgevect_m); }

/*
 * Draw the frame itself as four polygons:
 * ABFE, HGCD, EILH, and JFGK.
 * The streamers will attach at edges AE, BF, GC, and HD.
 *
 *		D --------------- C
 *		|                 |
 *		H -L-----------K- G
 *		|  |           |  |
 *		|  |           |  |
 *		|  |           |  |
 *		E -I-----------J- F
 *		|                 |
 *		A --------------- B
 */
void
predictor_frame(struct mged_state *UNUSED(s))
{
    /* Step 7.20: libdm removed — no-op. */
}


/*
 * Called from set.c when the predictor variables are modified.
 */
void
predictor_hook(const struct bu_structparse *UNUSED(sp), const char *UNUSED(c1), void *UNUSED(v1), const char *UNUSED(c2), void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);

    if (mged_variables->mv_predictor > 0)
	predictor_init(s);
    else
	predictor_kill(s);
    /* Step 7.20: DMP_dirty removed. */
    s->update_views = 1;
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
