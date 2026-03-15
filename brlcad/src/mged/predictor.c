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


/*
 * Add a new point to the end of the trail.
 */
static void
push_trail(struct trail *tp, fastf_t *pt)
{
    VMOVE(tp->t_pt[tp->t_cur_index], pt);
    if (tp->t_cur_index >= tp->t_nused) tp->t_nused++;
    tp->t_cur_index++;
    if (tp->t_cur_index >= MAX_TRAIL) tp->t_cur_index = 0;
}


/*
 * Draw from the most recently added points in two trails, as polygons.
 * Proceeds backwards.
 * t1 should be below (lower screen Y) t2.
 */
static void
poly_trail(struct mged_state *s, struct bu_list *vhead, struct trail *t1, struct trail *t2)
{
    int i1, i2;
    int todo = t1->t_nused;
    fastf_t *s1, *s2;
    vect_t right, up;
    vect_t norm;

    if (t2->t_nused < todo) todo = t2->t_nused;

    BU_LIST_INIT(vhead);
    if (t1->t_nused <= 0 || t1->t_nused <= 0) return;

    if ((i1 = t1->t_cur_index-1) < 0) i1 = t1->t_nused-1;
    if ((i2 = t2->t_cur_index-1) < 0) i2 = t2->t_nused-1;

    /* Get starting points, next to frame. */
    s1 = t1->t_pt[i1];
    s2 = t2->t_pt[i2];
    if ((--i1) < 0) i1 = t1->t_nused-1;
    if ((--i2) < 0) i2 = t2->t_nused-1;
    todo--;

    for (; todo > 0; todo--) {
	/* Go from s1 to s2 to t2->t_pt[i2] to t1->t_pt[i1] */
	VSUB2(up, s1, s2);
	VSUB2(right, t1->t_pt[i1], s1);
	VCROSS(norm, right, up);

	BSG_ADD_VLIST(s->vlfree, vhead, norm, BSG_VLIST_POLY_START);
	BSG_ADD_VLIST(s->vlfree, vhead, s1, BSG_VLIST_POLY_MOVE);
	BSG_ADD_VLIST(s->vlfree, vhead, s2, BSG_VLIST_POLY_DRAW);
	BSG_ADD_VLIST(s->vlfree, vhead, t2->t_pt[i2], BSG_VLIST_POLY_DRAW);
	BSG_ADD_VLIST(s->vlfree, vhead, t1->t_pt[i1], BSG_VLIST_POLY_DRAW);
	BSG_ADD_VLIST(s->vlfree, vhead, s1, BSG_VLIST_POLY_END);

	s1 = t1->t_pt[i1];
	s2 = t2->t_pt[i2];

	if ((--i1) < 0) i1 = t1->t_nused-1;
	if ((--i2) < 0) i2 = t2->t_nused-1;
    }
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
