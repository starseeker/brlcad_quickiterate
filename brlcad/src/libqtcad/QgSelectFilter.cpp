/*                 Q G S E L E C T F I L T E R . C P P
 * BRL-CAD
 *
 * Copyright (c) 2021-2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
6.  *
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
/** @file QgSelectFilter.cpp
 *
 * Graphical selection tool for Qt views.
 *
 */

#include "common.h"

extern "C" {
#include "bu/malloc.h"
#include "bsg.h"
#include "raytrace.h"
}

#include <unordered_set>
#include "qtcad/QgSelectFilter.h"
#include "qtcad/QgSignalFlags.h"

static struct bsg_pick_record *
_qg_pick_record_create(struct bsg_node *node, struct bsg_view *v, int sx, int sy,
	const char *source_path, fastf_t hit_dist = -1.0)
{
    if (!node)
	return nullptr;

    struct bsg_pick_record *pr;
    BU_GET(pr, struct bsg_pick_record);
    bu_vls_init(&pr->pr_source_path);
    pr->pr_node = node;
    pr->pr_view = v;
    pr->pr_screen_x = sx;
    pr->pr_screen_y = sy;
    if (hit_dist >= 0.0) {
	pr->pr_hit_dist = hit_dist;
    } else if (v) {
	point_t center;
	VADD2SCALE(center, node->bmin, node->bmax, 0.5);
	pr->pr_hit_dist = DIST_PNT_PNT(center, v->gv_vc_backout);
    } else {
	pr->pr_hit_dist = -1.0;
    }
    bu_vls_sprintf(&pr->pr_source_path, "%s",
	(source_path && source_path[0]) ? source_path : bu_vls_cstr(&node->s_name));
    return pr;
}

static struct bsg_pick_result *
_qg_pick_result_from_ptbl(struct bsg_view *v, const struct bu_ptbl *nodes,
	int sx, int sy)
{
    struct bsg_pick_result *res = bsg_pick_result_create();
    if (!res || !nodes)
	return res;

    for (size_t i = 0; i < BU_PTBL_LEN(nodes); i++) {
	struct bsg_node *node = (struct bsg_node *)BU_PTBL_GET(nodes, i);
	struct bsg_pick_record *pr = _qg_pick_record_create(node, v, sx, sy, NULL);
	if (pr)
	    bu_ptbl_ins(&res->pr_records, (long *)pr);
    }

    return res;
}

static struct bsg_pick_result *
_qg_pick_result_single(struct bsg_pick_record *src)
{
    struct bsg_pick_result *res = bsg_pick_result_create();
    if (!res || !src)
	return res;

    struct bsg_pick_record *pr = _qg_pick_record_create(src->pr_node, src->pr_view,
	src->pr_screen_x, src->pr_screen_y, bu_vls_cstr(&src->pr_source_path),
	src->pr_hit_dist);
    if (pr)
	bu_ptbl_ins(&res->pr_records, (long *)pr);
    return res;
}

QgSelectFilter::~QgSelectFilter()
{
    clear_selected_result();
    if (BU_PTBL_IS_INITIALIZED(&selected_set))
	bu_ptbl_free(&selected_set);
}

void
QgSelectFilter::clear_selected_result()
{
    if (selected_result) {
	bsg_pick_result_free(selected_result);
	selected_result = nullptr;
    }
    if (BU_PTBL_IS_INITIALIZED(&selected_set))
	bu_ptbl_reset(&selected_set);
}

void
QgSelectFilter::set_selected_result(struct bsg_view *v, struct bsg_pick_result *res)
{
    if (!BU_PTBL_IS_INITIALIZED(&selected_set))
	bu_ptbl_init(&selected_set, 8, "QgSelectFilter selected_set");

    if (v && v->gv_s && v->gv_s->gv_selected)
	bsg_selection_unhighlight(v->gv_s->gv_selected);

    clear_selected_result();
    selected_result = res;
    if (selected_result)
	bsg_pick_result_to_ptbl(selected_result, &selected_set);

    if (v && v->gv_s && v->gv_s->gv_selected) {
	if (selected_result) {
	    bsg_pick_apply(v->gv_s->gv_selected, selected_result, BSG_PICK_OP_SET);
	    bsg_selection_highlight(v->gv_s->gv_selected);
	} else {
	    bsg_selection_clear(v->gv_s->gv_selected);
	}
    }
}

bool
QgSelectPntFilter::eventFilter(QObject *, QEvent *e)
{
    QMouseEvent *m_e = view_sync(e);
    if (!m_e)
	return false;

    struct bsg_view *v = view();

    if (e->type() != QEvent::MouseButtonRelease)
	return true;
    if (m_e->button() != Qt::LeftButton)
	return true;
    if (!v)
	return true;

    struct bsg_pick_result *res = first_only ?
	bsg_pick_nearest(v, v->gv_mouse_x, v->gv_mouse_y) :
	bsg_pick_point(v, v->gv_mouse_x, v->gv_mouse_y, 0);
    set_selected_result(v, res);

    return true;
}

bool
QgSelectBoxFilter::eventFilter(QObject *, QEvent *e)
{
    QMouseEvent *m_e = view_sync(e);
    if (!m_e)
	return false;

    struct bsg_view *v = view();
    if (!v)
	return false;

    if (e->type() == QEvent::MouseButtonDblClick)
	return true;
    if (m_e->button() != Qt::LeftButton && e->type() != QEvent::MouseMove)
	return true;

    if (e->type() == QEvent::MouseButtonPress) {
	px = v->gv_mouse_x;
	py = v->gv_mouse_y;
	struct bsg_interactive_rect_state *grsp = &v->gv_s->gv_rect;
	grsp->line_width = 1;
	grsp->dim[0] = 0;
	grsp->dim[1] = 0;
	grsp->x = px;
	grsp->y = v->gv_height - py;
	grsp->pos[0] = grsp->x;
	grsp->pos[1] = grsp->y;
	grsp->cdim[0] = v->gv_width;
	grsp->cdim[1] = v->gv_height;
	grsp->aspect = (fastf_t)v->gv_s->gv_rect.cdim[X] / v->gv_s->gv_rect.cdim[Y];
	emit view_updated(QG_VIEW_DRAWN);
	return true;
    }

    if (e->type() == QEvent::MouseMove) {
	struct bsg_interactive_rect_state *grsp = &v->gv_s->gv_rect;
	grsp->draw = 1;
	grsp->dim[0] = v->gv_mouse_x - px;
	grsp->dim[1] = (v->gv_height - v->gv_mouse_y) - v->gv_s->gv_rect.pos[1];
	grsp->x = (grsp->pos[X] / (fastf_t)grsp->cdim[X] - 0.5) * 2.0;
	grsp->y = ((0.5 - (grsp->cdim[Y] - grsp->pos[Y]) / (fastf_t)grsp->cdim[Y]) / grsp->aspect * 2.0);
	grsp->width = grsp->dim[X] * 2.0 / (fastf_t)grsp->cdim[X];
	grsp->height = grsp->dim[Y] * 2.0 / (fastf_t)grsp->cdim[X];
	emit view_updated(QG_VIEW_DRAWN);
	return true;
    }

    if (e->type() == QEvent::MouseButtonRelease) {
	int ipx = (int)px;
	int ipy = (int)py;
	struct bsg_pick_result *res =
	    bsg_pick_rect(v, ipx, ipy, v->gv_mouse_x, v->gv_mouse_y);
	if (first_only && res && bsg_pick_result_count(res) > 1) {
	    struct bsg_pick_result *nearest =
		_qg_pick_result_single(bsg_pick_result_get(res, 0));
	    bsg_pick_result_free(res);
	    res = nearest;
	}
	set_selected_result(v, res);

	struct bsg_interactive_rect_state *grsp = &v->gv_s->gv_rect;
	grsp->draw = 0;
	grsp->line_width = 0;
	grsp->pos[0] = 0;
	grsp->pos[1] = 0;
	grsp->dim[0] = 0;
	grsp->dim[1] = 0;
	emit view_updated(QG_VIEW_DRAWN);
	return true;
    }

    return false;
}

struct select_rec_state {
    std::unordered_set<std::string> active;
    int rec_all;
    double cdist;
    std::string closest;
};

static int
_obj_record(struct application *ap, struct partition *p_hp, struct seg *UNUSED(segs))
{
    struct select_rec_state *rc = (struct select_rec_state *)ap->a_uptr;
    for (struct partition *pp = p_hp->pt_forw; pp != p_hp; pp = pp->pt_forw) {
	if (rc->rec_all) {
	    rc->active.insert(std::string(pp->pt_regionp->reg_name));
	} else {
	    struct hit *hitp = pp->pt_inhit;
	    if (hitp->hit_dist < rc->cdist) {
		rc->closest = std::string(pp->pt_regionp->reg_name);
		rc->cdist = hitp->hit_dist;
	    }
	}
    }
    return 1;
}

static int
_ovlp_record(struct application *ap, struct partition *pp, struct region *reg1, struct region *reg2, struct partition *UNUSED(ihp))
{
    struct select_rec_state *rc = (struct select_rec_state *)ap->a_uptr;
    if (rc->rec_all) {
	rc->active.insert(std::string(reg1->reg_name));
	rc->active.insert(std::string(reg2->reg_name));
    } else {
	rc->closest = std::string(reg1->reg_name);
	rc->cdist = pp->pt_inhit->hit_dist;
    }
    return 1;
}

bool
QgSelectRayFilter::eventFilter(QObject *, QEvent *e)
{
    QMouseEvent *m_e = view_sync(e);
    if (!m_e)
	return false;

    struct bsg_view *v = view();
    if (!v || !dbip)
	return false;
    if (e->type() != QEvent::MouseButtonRelease)
	return true;
    if (m_e->button() != Qt::LeftButton)
	return true;

    struct bsg_pick_result *candidates =
	bsg_pick_point(v, v->gv_mouse_x, v->gv_mouse_y, 0);
    if (!candidates || !bsg_pick_result_count(candidates)) {
	set_selected_result(v, candidates);
	return true;
    }

    struct application *ap;
    BU_GET(ap, struct application);
    RT_APPLICATION_INIT(ap);
    ap->a_onehit = 0;
    ap->a_hit = _obj_record;
    ap->a_miss = nullptr;
    ap->a_overlap = _ovlp_record;
    ap->a_logoverlap = nullptr;

    struct rt_i *rtip = rt_new_rti(dbip);
    struct resource *resp = nullptr;
    BU_GET(resp, struct resource);
    rt_init_resource(resp, 0, rtip);
    ap->a_resource = resp;
    ap->a_rt_i = rtip;
    const char **objs = (const char **)bu_calloc(bsg_pick_result_count(candidates) + 1, sizeof(char *), "objs");
    for (size_t i = 0; i < bsg_pick_result_count(candidates); i++) {
	struct bsg_pick_record *pr = bsg_pick_result_get(candidates, i);
	objs[i] = (pr && pr->pr_node) ? bu_vls_cstr(&pr->pr_node->s_name) : NULL;
    }
    if (rt_gettrees_and_attrs(rtip, nullptr, (int)bsg_pick_result_count(candidates), objs, 1)) {
	bu_free(objs, "objs");
	rt_free_rti(rtip);
	BU_PUT(resp, struct resource);
	BU_PUT(ap, struct application);
	bsg_pick_result_free(candidates);
	return false;
    }
    size_t ncpus = bu_avail_cpus();
    rt_prep_parallel(rtip, (int)ncpus);
    fastf_t vx = -FLT_MAX;
    fastf_t vy = -FLT_MAX;
    bsg_screen_to_view(v, &vx, &vy, v->gv_mouse_x, v->gv_mouse_y);
    point_t vpnt, mpnt;
    VSET(vpnt, vx, vy, 0);
    MAT4X3PNT(mpnt, v->gv_view2model, vpnt);
    vect_t dir;
    VMOVEN(dir, v->gv_rotation + 8, 3);
    VUNITIZE(dir);
    VSCALE(dir, dir, v->radius);
    VADD2(ap->a_ray.r_pt, mpnt, dir);
    VUNITIZE(dir);
    VSCALE(ap->a_ray.r_dir, dir, -1);

    struct select_rec_state rc;
    if (!first_only) {
	rc.rec_all = 1;
    } else {
	rc.rec_all = 0;
	rc.cdist = INFINITY;
    }
    ap->a_uptr = (void *)&rc;

    (void)rt_shootray(ap);
    bu_free(objs, "objs");
    rt_free_rti(rtip);
    BU_PUT(resp, struct resource);
    BU_PUT(ap, struct application);

    struct bu_ptbl ray_nodes = BU_PTBL_INIT_ZERO;
    struct bu_vls dpath = BU_VLS_INIT_ZERO;
    if (first_only) {
	bu_vls_sprintf(&dpath, "%s",  rc.closest.c_str());
	if (bu_vls_cstr(&dpath)[0] == '/')
	    bu_vls_nibble(&dpath, 1);
	struct bsg_node *so = bsg_find_obj(v, bu_vls_cstr(&dpath));
	if (so)
	    bu_ptbl_ins(&ray_nodes, (long *)so);
    } else {
	std::unordered_set<std::string>::iterator a_it;
	for (a_it = rc.active.begin(); a_it != rc.active.end(); a_it++) {
	    bu_vls_sprintf(&dpath, "%s",  a_it->c_str());
	    if (bu_vls_cstr(&dpath)[0] == '/')
		bu_vls_nibble(&dpath, 1);
	    struct bsg_node *so = bsg_find_obj(v, bu_vls_cstr(&dpath));
	    if (so)
		bu_ptbl_ins(&ray_nodes, (long *)so);
	}
    }
    bu_vls_free(&dpath);
    bsg_pick_result_free(candidates);

    struct bsg_pick_result *res =
	_qg_pick_result_from_ptbl(v, &ray_nodes, v->gv_mouse_x, v->gv_mouse_y);
    bu_ptbl_free(&ray_nodes);
    set_selected_result(v, res);

    return true;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
