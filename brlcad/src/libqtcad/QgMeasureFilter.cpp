/*                 Q G M E A S U R E F I L T E R . C P P
 * BRL-CAD
 *
 * Copyright (c) 2021-2026 United States Government as represented by
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
/** @file QgMeasureFilter.cpp
 *
 * Measurement tool for Qt views.
 *
 */

#include "common.h"

extern "C" {
#include "bu/malloc.h"
#include "bsg.h"
#include "raytrace.h"
}

#include <vector>
#include "qtcad/QgMeasureFilter.h"
#include "qtcad/QgSignalFlags.h"
#include "bsg/node_private.h"

double
QgMeasureFilter::length1()
{
	if (mr12.mr_valid)
		return mr12.mr_distance;
	return DIST_PNT_PNT(p1, p2);
}

double
QgMeasureFilter::length2()
{
	if (mode < 3)
		return 0.0;

	if (mr23.mr_valid)
		return mr23.mr_distance;
	return DIST_PNT_PNT(p2, p3);
}

double
QgMeasureFilter::angle(bool radians)
{
	if (mode < 3)
		return 0.0;

	vect_t v1, v2;
	VSUB2(v1, p1, p2);
	VSUB2(v2, p3, p2);
	VUNITIZE(v1);
	VUNITIZE(v2);
	double a = acos(VDOT(v1, v2));
	if (radians)
		return a*180/M_PI;
	return a;
}

void
QgMeasureFilter::update_color(struct bu_color *c)
{
	if (!s || !c)
		return;
	bu_color_to_rgb_chars(c, s->s_color);
}

bool
QgMeasureFilter::eventFilter(QObject *, QEvent *e)
{
	QMouseEvent *m_e = view_sync(e);
	if (!m_e)
		return false;

	struct bsg_view *v = view();
	if (!v)
		return false;

	if (e->type() == QEvent::MouseButtonPress) {
		if (m_e->button() == Qt::RightButton) {
			if (s)
				bsg_obj_put(s);
			mode = 0;
			VSETALL(p1, 0.0);
			VSETALL(p2, 0.0);
			VSETALL(p3, 0.0);
			mr12 = {0.0, 0.0, 0.0, 0};
			mr23 = {0.0, 0.0, 0.0, 0};
			emit view_updated(QG_VIEW_REFRESH);
			return true;
		}
		if (mode == 4) {
			if (s)
				bsg_obj_put(s);
			mode = 0;
			mr12 = {0.0, 0.0, 0.0, 0};
			mr23 = {0.0, 0.0, 0.0, 0};
			emit view_updated(QG_VIEW_REFRESH);
			return true;
		}
		if (!mode) {
			if (!get_point())
				return true;

			VSETALL(p1, 0.0);
			VSETALL(p2, 0.0);
			VSETALL(p3, 0.0);
			mr12 = {0.0, 0.0, 0.0, 0};
			mr23 = {0.0, 0.0, 0.0, 0};

			if (s)
				bsg_obj_put(s);
			/* Phase A2: use typed view-object API instead of legacy bsg_obj_get. */
			s = bsg_view_obj_lines_create(v, oname.c_str(), 0);
			if (s) {
				bsg_overlay_register_owner(s, this,
					BSG_OVERLAY_ROLE_SCREEN,
					BSG_OVERLAY_CLASS_MEASURE,
					BSG_OVERLAY_LC_PER_TOOL,
					BSG_OVERLAY_ORDER_POST_TRANSPARENT,
					NULL,
					0);
			}

			mode = 1;
			VMOVE(p1, mpnt);
			VMOVE(p2, mpnt);
			bsg_node_append_vlist_payload(s, p1, BSG_VLIST_LINE_MOVE);
			emit view_updated(QG_VIEW_REFRESH);
			return true;
		}
		if (mode == 1) {
			if (!get_point())
				return true;

			VMOVE(p2, mpnt);
			return true;
		}
		if (mode == 2) {
			if (!get_point())
				return true;
			mode = 3;
			bsg_node_clear_vlist_payload(s);
			bsg_node_append_vlist_payload(s, p1, BSG_VLIST_LINE_MOVE);
			bsg_node_append_vlist_payload(s, p2, BSG_VLIST_LINE_DRAW);
			VMOVE(p3, mpnt);
			bsg_node_append_vlist_payload(s, p3, BSG_VLIST_LINE_DRAW);
			emit view_updated(QG_VIEW_REFRESH);
		}
		return true;
	}

	if (e->type() == QEvent::MouseMove) {
		if (!mode)
			return false;
		if (mode == 1) {
			if (!get_point())
				return true;

			bsg_node_clear_vlist_payload(s);
			bsg_node_append_vlist_payload(s, p1, BSG_VLIST_LINE_MOVE);
			VMOVE(p2, mpnt);
			bsg_node_append_vlist_payload(s, p2, BSG_VLIST_LINE_DRAW);
			emit view_updated(QG_VIEW_REFRESH);
		}
		if (mode == 3) {
			if (!get_point())
				return true;

			bsg_node_clear_vlist_payload(s);
			bsg_node_append_vlist_payload(s, p1, BSG_VLIST_LINE_MOVE);
			bsg_node_append_vlist_payload(s, p2, BSG_VLIST_LINE_DRAW);
			VMOVE(p3, mpnt);
			bsg_node_append_vlist_payload(s, p3, BSG_VLIST_LINE_DRAW);
			emit view_updated(QG_VIEW_REFRESH);
		}
		return true;
	}

	if (e->type() == QEvent::MouseButtonRelease) {
		if (m_e->button() == Qt::RightButton) {
			mode = 0;
			if (s) {
				bsg_overlay_clear_owned(v, this);
				emit view_updated(QG_VIEW_REFRESH);
			}
			s = nullptr;
			return true;
		}
		if (!mode)
			return false;
		if (mode == 1 && DIST_PNT_PNT(p1, p2) < SMALL_FASTF) {
			return true;
		}
		if (mode == 1) {
			if (!get_point())
				return true;

			if (length_only) {
				// Angle measurement disabled, starting over
				mode = 0;
				emit view_updated(QG_VIEW_REFRESH);
				return true;
			}

			mode = 2;
			bsg_node_clear_vlist_payload(s);
			bsg_node_append_vlist_payload(s, p1, BSG_VLIST_LINE_MOVE);
			VMOVE(p2, mpnt);
			bsg_node_append_vlist_payload(s, p2, BSG_VLIST_LINE_DRAW);
			/* Record p1→p2 measure via typed API for D3 consumers. */
			bsg_measure_candidates(v, p1, p2, &mr12);
			emit view_updated(QG_VIEW_REFRESH);
			return true;
		}
		if (mode == 3) {
			if (!get_point())
				return true;
			mode = 4;
			bsg_node_clear_vlist_payload(s);
			bsg_node_append_vlist_payload(s, p1, BSG_VLIST_LINE_MOVE);
			bsg_node_append_vlist_payload(s, p2, BSG_VLIST_LINE_DRAW);
			VMOVE(p3, mpnt);
			bsg_node_append_vlist_payload(s, p3, BSG_VLIST_LINE_DRAW);
			/* Record p2→p3 measure via typed API for D3 consumers. */
			bsg_measure_candidates(v, p2, p3, &mr23);
			emit view_updated(QG_VIEW_REFRESH);
			return true;
		}

		return true;
	}

	// Shouldn't get here
	return false;
}

bool
QMeasure2DFilter::get_point()
{
	struct bsg_view *v = view();
	fastf_t vx, vy;
	bsg_screen_to_view(v, &vx, &vy, v->gv_mouse_x, v->gv_mouse_y);
	point_t vpnt;
	VSET(vpnt, vx, vy, 0);
	MAT4X3PNT(mpnt, v->gv_view2model, vpnt);
	return true;
}

bool
QMeasure2DFilter::eventFilter(QObject *o, QEvent *e)
{
	return QgMeasureFilter::eventFilter(o, e);
}

QMeasure3DFilter::QMeasure3DFilter()
{
}

QMeasure3DFilter::~QMeasure3DFilter()
{
	bu_ptbl_free(&scene_obj_set);
}

struct measure_rec_state {
	double cdist;
	point_t pt;
};

static int
_cpnt_hit(struct application *ap, struct partition *p_hp, struct seg *UNUSED(segs))
{
	struct measure_rec_state *rc = (struct measure_rec_state *)ap->a_uptr;
	for (struct partition *pp = p_hp->pt_forw; pp != p_hp; pp = pp->pt_forw) {
		struct hit *hitp = pp->pt_inhit;
		if (hitp->hit_dist < rc->cdist) {
			rc->cdist = hitp->hit_dist;
			VJOIN1(rc->pt, ap->a_ray.r_pt, hitp->hit_dist, ap->a_ray.r_dir);
		}
	}
	return 1;
}

static int
_cpnt_ovlp(struct application *ap, struct partition *pp, struct region *UNUSED(reg1), struct region *UNUSED(reg2), struct partition *UNUSED(ihp))
{
	struct measure_rec_state *rc = (struct measure_rec_state *)ap->a_uptr;
	struct hit *hitp = pp->pt_inhit;
	rc->cdist = hitp->hit_dist;
	VJOIN1(rc->pt, ap->a_ray.r_pt, hitp->hit_dist, ap->a_ray.r_dir);
	return 1;
}

static const char *
_measure_pick_target(const struct bsg_pick_record *pr)
{
	if (!pr)
		return NULL;

	const char *spath = bu_vls_cstr(&pr->pr_source_path);
	if (spath && spath[0])
		return spath;

	return (pr->pr_node) ? bu_vls_cstr(&pr->pr_node->s_name) : NULL;
}

bool
QMeasure3DFilter::get_point()
{
	if (!dbip)
		return false;

	struct bsg_view *v = view();
	fastf_t vx, vy;
	bsg_screen_to_view(v, &vx, &vy, v->gv_mouse_x, v->gv_mouse_y);
	point_t vpnt;
	VSET(vpnt, vx, vy, 0);
	MAT4X3PNT(mpnt, v->gv_view2model, vpnt);

	// With this filter we want a 3D point based on scene geometry (hard case)
	// - need to interrogate the scene with the raytracer.
	//
	// Rather than prepping the whole of what is drawn, we will instead prep
	// only those objects whose bounding boxes are currently under the mouse.
	// Under most circumstances that should substantially cut down the
	// interrogation time for large models.
	struct bsg_pick_result *candidates = bsg_pick_point(v, v->gv_mouse_x, v->gv_mouse_y, 0);
	struct bu_ptbl sset = BU_PTBL_INIT_ZERO;
	std::vector<const char *> candidate_names;
	if (candidates) {
		candidate_names.reserve(bsg_pick_result_count(candidates));
		for (size_t i = 0; i < bsg_pick_result_count(candidates); i++) {
			struct bsg_pick_record *pr = bsg_pick_result_get(candidates, i);
			if (!pr || !pr->pr_node)
				continue;
			bu_ptbl_ins(&sset, (long *)pr->pr_node);
			candidate_names.push_back(_measure_pick_target(pr));
		}
	}
	int scnt = (int)BU_PTBL_LEN(&sset);

	// If we didn't see anything, we have a no-op
	if (!scnt) {
		prev_cnt = scnt;
		bu_ptbl_free(&sset);
		if (candidates)
			bsg_pick_result_free(candidates);
		return false;
	}

	bool need_prep = (!ap || !rtip) ? true : false;
	if (need_prep || prev_cnt != scnt || scnt != (int)BU_PTBL_LEN(&scene_obj_set)) {
		// Something changed - need to reset the raytrace data
		bu_ptbl_reset(&scene_obj_set);
		bu_ptbl_cat(&scene_obj_set, &sset);
		need_prep = true;
	}
	if (!need_prep) {
		// We may be able to reuse the existing prep - make sure the scene obj
		// sets match.  The above check should ensure that the lengths of sset
		// and scene_obj_set match - if they don't, we already know we need to
		// re-prep.
		for (size_t i = 0; i < BU_PTBL_LEN(&scene_obj_set); i++) {
			if (BU_PTBL_GET(&sset, i) != BU_PTBL_GET(&scene_obj_set, i)) {
				need_prep = true;
				break;
			}
		}
	}

	prev_cnt = scnt;

	if (need_prep) {
		if (!ap) {
			BU_GET(ap, struct application);
			RT_APPLICATION_INIT(ap);
			ap->a_onehit = 1;
			ap->a_hit = _cpnt_hit;
			ap->a_miss = nullptr;
			ap->a_overlap = _cpnt_ovlp;
			ap->a_logoverlap = nullptr;
		}
		if (rtip) {
			rt_i_destroy(rtip);
			rtip = nullptr;
		}
		rtip = rt_i_create(dbip);
		struct resource *resp = nullptr;
		BU_GET(resp, struct resource);
		rt_init_resource(resp, 0, rtip);
		ap->a_resource = resp;
		ap->a_rt_i = rtip;

		const char **objs = (const char **)bu_calloc(BU_PTBL_LEN(&scene_obj_set) + 1, sizeof(char *), "objs");
		for (size_t i = 0; i < BU_PTBL_LEN(&scene_obj_set); i++) {
			objs[i] = (i < candidate_names.size()) ? candidate_names[i] : NULL;
		}
		if (rt_gettrees_and_attrs(rtip, nullptr, scnt, objs, 1)) {
			bu_free(objs, "objs");
			rt_i_destroy(rtip);
			rtip = nullptr;
			BU_PUT(resp, struct resource);
			bu_ptbl_free(&sset);
			if (candidates)
				bsg_pick_result_free(candidates);
			return false;
		}
		size_t ncpus = bu_avail_cpus();
		rt_prep_parallel(rtip, (int)ncpus);
		bu_free(objs, "objs");
	}

	bu_ptbl_free(&sset);
	if (candidates)
		bsg_pick_result_free(candidates);

	// Set up data container for result
	struct measure_rec_state rc;
	rc.cdist = INFINITY;
	ap->a_uptr = (void *)&rc;

	// Set up the ray itself
	vect_t dir;
	VMOVEN(dir, v->gv_rotation + 8, 3);
	VUNITIZE(dir);
	VSCALE(dir, dir, v->radius);
	VADD2(ap->a_ray.r_pt, mpnt, dir);
	VUNITIZE(dir);
	VSCALE(ap->a_ray.r_dir, dir, -1);

	(void)rt_shootray(ap);

	if (rc.cdist < INFINITY) {
		VMOVE(mpnt, rc.pt);
		return true;
	}

	return false;
}

bool
QMeasure3DFilter::eventFilter(QObject *o, QEvent *e)
{
	return QgMeasureFilter::eventFilter(o, e);
}




// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
