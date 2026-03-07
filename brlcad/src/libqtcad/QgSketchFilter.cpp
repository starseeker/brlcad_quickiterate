/*                 Q G S K E T C H F I L T E R . C P P
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @file QgSketchFilter.cpp
 *
 * Qt mouse-event filter implementations for interactive 2-D sketch
 * editing via the librt ECMD_SKETCH_* API.
 */

#include "common.h"

#include <math.h>

extern "C" {
#include "bu/malloc.h"
#include "bv/util.h"
#include "raytrace.h"
#include "rt/functab.h"
#include "rt/geom.h"
#include "rt/primitives/sketch.h"
#include "rt/rt_ecmds.h"
}

#include "qtcad/QgSketchFilter.h"
#include "qtcad/QgSignalFlags.h"


/* ------------------------------------------------------------------ */
/* QgSketchFilter — base helpers                                       */
/* ------------------------------------------------------------------ */

QMouseEvent *
QgSketchFilter::view_sync(QEvent *e)
{
    if (!v)
	return NULL;

    QMouseEvent *m_e = NULL;
    if (e->type() == QEvent::MouseButtonPress
	    || e->type() == QEvent::MouseButtonRelease
	    || e->type() == QEvent::MouseButtonDblClick
	    || e->type() == QEvent::MouseMove)
	m_e = (QMouseEvent *)e;
    if (!m_e)
	return NULL;

    int e_x, e_y;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    e_x = m_e->x();
    e_y = m_e->y();
#else
    e_x = (int)m_e->position().x();
    e_y = (int)m_e->position().y();
#endif

    v->gv_prevMouseX = v->gv_mouse_x;
    v->gv_prevMouseY = v->gv_mouse_y;
    v->gv_mouse_x = e_x;
    v->gv_mouse_y = e_y;
    bv_screen_pt(&v->gv_point, (fastf_t)e_x, (fastf_t)e_y, v);

    /* Keyboard modifiers usually mean view navigation rather than editing. */
    if (m_e->modifiers() != Qt::NoModifier)
	return NULL;

    return m_e;
}

void
QgSketchFilter::screen_to_view(int sx, int sy, vect_t mvec) const
{
    if (!v) {
	VSETALL(mvec, 0.0);
	return;
    }
    fastf_t vx = 0.0, vy = 0.0;
    bv_screen_to_view(v, &vx, &vy, (fastf_t)sx, (fastf_t)sy);
    VSET(mvec, vx, vy, 0.0);
}

bool
QgSketchFilter::screen_to_uv(int sx, int sy,
			     fastf_t *u_out, fastf_t *v_out) const
{
    if (!v || !es)
	return false;

    const struct rt_sketch_internal *skt =
	(const struct rt_sketch_internal *)es->es_int.idb_ptr;
    if (!skt)
	return false;
    RT_SKETCH_CK_MAGIC(skt);

    /* Unproject screen pixel → model-space 3-D point */
    point_t p3d;
    if (bv_screen_pt(&p3d, (fastf_t)sx, (fastf_t)sy, v) != 0)
	return false;

    /* Project the 3-D point onto the sketch plane.
     * u_vec and v_vec are unit vectors, so the projection is simply
     * the dot product with the offset from the sketch origin V. */
    vect_t delta;
    VSUB2(delta, p3d, skt->V);
    *u_out = VDOT(delta, skt->u_vec);
    *v_out = VDOT(delta, skt->v_vec);
    return true;
}


/* ------------------------------------------------------------------ */
/* QgSketchPickVertexFilter                                            */
/* ------------------------------------------------------------------ */

bool
QgSketchPickVertexFilter::eventFilter(QObject *, QEvent *e)
{
    QMouseEvent *m_e = view_sync(e);
    if (!m_e)
	return false;

    if (!es)
	return false;

    /* Act on left button press only */
    if (m_e->type() == QEvent::MouseButtonPress
	    && m_e->buttons().testFlag(Qt::LeftButton)) {

	const struct rt_sketch_internal *skt =
	    (const struct rt_sketch_internal *)es->es_int.idb_ptr;
	if (!skt || skt->vert_count == 0)
	    return true;

	/* Store view-space cursor; edsketch.c's proximity search will use it */
	struct rt_sketch_edit *se = (struct rt_sketch_edit *)es->ipe_ptr;
	if (!se)
	    return true;

	vect_t mvec;
	screen_to_view(v->gv_mouse_x, v->gv_mouse_y, mvec);
	VSET(se->v_pos, mvec[X], mvec[Y], 0.0);
	se->v_pos_valid = 1;

	es->e_inpara = 0;
	EDOBJ[es->es_int.idb_type].ft_set_edit_mode(es,
		ECMD_SKETCH_PICK_VERTEX);
	rt_edit_process(es);

	emit sketch_changed();
	emit view_updated(QG_VIEW_REFRESH);
	return true;
    }

    /* Swallow other button events while this filter is active */
    if (m_e->type() == QEvent::MouseButtonPress
	    || m_e->type() == QEvent::MouseButtonRelease)
	return true;

    return false;
}


/* ------------------------------------------------------------------ */
/* QgSketchMoveVertexFilter                                            */
/* ------------------------------------------------------------------ */

bool
QgSketchMoveVertexFilter::eventFilter(QObject *, QEvent *e)
{
    QMouseEvent *m_e = view_sync(e);
    if (!m_e)
	return false;

    if (!es)
	return false;

    struct rt_sketch_edit *se = (struct rt_sketch_edit *)es->ipe_ptr;
    if (!se || se->curr_vert < 0)
	return false;

    if (m_e->type() == QEvent::MouseButtonPress
	    && m_e->buttons().testFlag(Qt::LeftButton)) {
	m_dragging = true;
	return true;
    }

    if (m_e->type() == QEvent::MouseButtonRelease) {
	m_dragging = false;
	return true;
    }

    if (m_e->type() == QEvent::MouseMove && m_dragging
	    && m_e->buttons().testFlag(Qt::LeftButton)) {

	fastf_t u = 0.0, vv = 0.0;
	if (!screen_to_uv(v->gv_mouse_x, v->gv_mouse_y, &u, &vv))
	    return true;

	/* Convert base units → local units */
	fastf_t scale = (es->local2base > 0.0) ? (1.0 / es->local2base) : 1.0;
	es->e_para[0] = u * scale;
	es->e_para[1] = vv * scale;
	es->e_inpara  = 2;

	EDOBJ[es->es_int.idb_type].ft_set_edit_mode(es,
		ECMD_SKETCH_MOVE_VERTEX);
	rt_edit_process(es);

	emit sketch_changed();
	emit view_updated(QG_VIEW_REFRESH);
	return true;
    }

    return false;
}


/* ------------------------------------------------------------------ */
/* QgSketchAddVertexFilter                                             */
/* ------------------------------------------------------------------ */

bool
QgSketchAddVertexFilter::eventFilter(QObject *, QEvent *e)
{
    QMouseEvent *m_e = view_sync(e);
    if (!m_e)
	return false;

    if (!es)
	return false;

    if (m_e->type() == QEvent::MouseButtonPress
	    && m_e->buttons().testFlag(Qt::LeftButton)) {

	fastf_t u = 0.0, vv = 0.0;
	if (!screen_to_uv(v->gv_mouse_x, v->gv_mouse_y, &u, &vv))
	    return true;

	/* Convert base units → local units */
	fastf_t scale = (es->local2base > 0.0) ? (1.0 / es->local2base) : 1.0;
	es->e_para[0] = u * scale;
	es->e_para[1] = vv * scale;
	es->e_inpara  = 2;

	EDOBJ[es->es_int.idb_type].ft_set_edit_mode(es,
		ECMD_SKETCH_ADD_VERTEX);
	rt_edit_process(es);

	emit sketch_changed();
	emit view_updated(QG_VIEW_REFRESH);
	return true;
    }

    /* Swallow other button events */
    if (m_e->type() == QEvent::MouseButtonPress
	    || m_e->type() == QEvent::MouseButtonRelease)
	return true;

    return false;
}


/* ------------------------------------------------------------------ */
/* QgSketchPickSegmentFilter                                           */
/* ------------------------------------------------------------------ */

/*
 * Sample a segment at t in [0,1] and return the 3-D model-space point.
 * Returns false for unsupported segment types.
 */
static bool
sketch_seg_sample(const struct rt_sketch_internal *skt,
		  int seg_idx, fastf_t t,
		  point_t *p3d_out)
{
    void *seg = skt->curve.segment[seg_idx];
    if (!seg)
	return false;

    uint32_t magic = *(uint32_t *)seg;

    fastf_t u = 0.0, vv = 0.0;

    if (magic == CURVE_LSEG_MAGIC) {
	struct line_seg *ls = (struct line_seg *)seg;
	u  = (1.0 - t) * skt->verts[ls->start][0]
	   + t          * skt->verts[ls->end  ][0];
	vv = (1.0 - t) * skt->verts[ls->start][1]
	   + t          * skt->verts[ls->end  ][1];

    } else if (magic == CURVE_CARC_MAGIC) {
	struct carc_seg *cs = (struct carc_seg *)seg;
	if (cs->radius < 0.0) {
	    /* Full circle: parametrise by angle */
	    fastf_t cx = skt->verts[cs->end][0];
	    fastf_t cy = skt->verts[cs->end][1];
	    fastf_t r  = -cs->radius;
	    fastf_t theta = t * 2.0 * M_PI;
	    u  = cx + r * cos(theta);
	    vv = cy + r * sin(theta);
	} else {
	    /* Partial arc: interpolate between start and end angles */
	    fastf_t sx = skt->verts[cs->start][0];
	    fastf_t sy = skt->verts[cs->start][1];
	    fastf_t ex = skt->verts[cs->end  ][0];
	    fastf_t ey = skt->verts[cs->end  ][1];
	    u  = (1.0 - t) * sx + t * ex;
	    vv = (1.0 - t) * sy + t * ey;
	}

    } else if (magic == CURVE_BEZIER_MAGIC) {
	struct bezier_seg *bs = (struct bezier_seg *)seg;
	int deg = bs->degree;

	/* De Casteljau evaluation */
	fastf_t pu[16], pv[16];
	for (int i = 0; i <= deg; i++) {
	    pu[i] = skt->verts[bs->ctl_points[i]][0];
	    pv[i] = skt->verts[bs->ctl_points[i]][1];
	}
	for (int r = 1; r <= deg; r++) {
	    for (int i = 0; i <= deg - r; i++) {
		pu[i] = (1.0 - t) * pu[i] + t * pu[i + 1];
		pv[i] = (1.0 - t) * pv[i] + t * pv[i + 1];
	    }
	}
	u  = pu[0];
	vv = pv[0];

    } else {
	return false; /* NURB: not sampled for proximity */
    }

    VJOIN2(*p3d_out, skt->V,
	   u,  skt->u_vec,
	   vv, skt->v_vec);
    return true;
}

bool
QgSketchPickSegmentFilter::eventFilter(QObject *, QEvent *e)
{
    QMouseEvent *m_e = view_sync(e);
    if (!m_e)
	return false;

    if (!es)
	return false;

    if (m_e->type() == QEvent::MouseButtonPress
	    && m_e->buttons().testFlag(Qt::LeftButton)) {

	const struct rt_sketch_internal *skt =
	    (const struct rt_sketch_internal *)es->es_int.idb_ptr;
	if (!skt || skt->curve.count == 0)
	    return true;

	/* Build model→view matrix including any edit transform */
	mat_t m2v;
	bn_mat_mul(m2v, v->gv_model2view, es->model_changes);

	/* Cursor in view space */
	vect_t cursor_v;
	screen_to_view(v->gv_mouse_x, v->gv_mouse_y, cursor_v);

	int    best_seg  = -1;
	fastf_t best_d2  = INFINITY;
	int    nsamples  = 16;

	for (size_t si = 0; si < skt->curve.count; si++) {
	    for (int k = 0; k <= nsamples; k++) {
		fastf_t t = (fastf_t)k / (fastf_t)nsamples;
		point_t p3d;
		if (!sketch_seg_sample(skt, (int)si, t, &p3d))
		    continue;

		point_t p_view;
		MAT4X3PNT(p_view, m2v, p3d);

		fastf_t dx = p_view[X] - cursor_v[X];
		fastf_t dy = p_view[Y] - cursor_v[Y];
		fastf_t d2 = dx * dx + dy * dy;
		if (d2 < best_d2) {
		    best_d2  = d2;
		    best_seg = (int)si;
		}
	    }
	}

	if (best_seg < 0)
	    return true;

	es->e_para[0] = (fastf_t)best_seg;
	es->e_inpara  = 1;
	EDOBJ[es->es_int.idb_type].ft_set_edit_mode(es,
		ECMD_SKETCH_PICK_SEGMENT);
	rt_edit_process(es);

	emit sketch_changed();
	emit view_updated(QG_VIEW_REFRESH);
	return true;
    }

    if (m_e->type() == QEvent::MouseButtonPress
	    || m_e->type() == QEvent::MouseButtonRelease)
	return true;

    return false;
}


/* ------------------------------------------------------------------ */
/* QgSketchMoveSegmentFilter                                           */
/* ------------------------------------------------------------------ */

bool
QgSketchMoveSegmentFilter::eventFilter(QObject *, QEvent *e)
{
    QMouseEvent *m_e = view_sync(e);
    if (!m_e)
	return false;

    if (!es)
	return false;

    struct rt_sketch_edit *se = (struct rt_sketch_edit *)es->ipe_ptr;
    if (!se || se->curr_seg < 0)
	return false;

    if (m_e->type() == QEvent::MouseButtonPress
	    && m_e->buttons().testFlag(Qt::LeftButton)) {
	/* Record starting UV position */
	screen_to_uv(v->gv_mouse_x, v->gv_mouse_y, &m_prev_u, &m_prev_v);
	m_dragging = true;
	return true;
    }

    if (m_e->type() == QEvent::MouseButtonRelease) {
	m_dragging = false;
	return true;
    }

    if (m_e->type() == QEvent::MouseMove && m_dragging
	    && m_e->buttons().testFlag(Qt::LeftButton)) {

	fastf_t cur_u = 0.0, cur_v = 0.0;
	if (!screen_to_uv(v->gv_mouse_x, v->gv_mouse_y, &cur_u, &cur_v))
	    return true;

	fastf_t du = cur_u - m_prev_u;
	fastf_t dv = cur_v - m_prev_v;

	/* Convert delta from base units → local units */
	fastf_t scale = (es->local2base > 0.0) ? (1.0 / es->local2base) : 1.0;
	es->e_para[0] = du * scale;
	es->e_para[1] = dv * scale;
	es->e_inpara  = 2;

	EDOBJ[es->es_int.idb_type].ft_set_edit_mode(es,
		ECMD_SKETCH_MOVE_SEGMENT);
	rt_edit_process(es);

	m_prev_u = cur_u;
	m_prev_v = cur_v;

	emit sketch_changed();
	emit view_updated(QG_VIEW_REFRESH);
	return true;
    }

    return false;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
