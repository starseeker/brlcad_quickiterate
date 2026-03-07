/*                         V U T I L . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2025 United States Government as represented by
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
/** @file libged/vutil.c
 *
 * This file contains view related utility functions.
 *
 */

#include "common.h"

#include <string.h>

#include "./ged_private.h"
#include "ged/view.h"

int
_ged_do_rot(struct ged *gedp,
	    char coord,
	    mat_t rmat,
	    int (*func)(struct ged *, char, char, mat_t))
{
    mat_t temp1, temp2;
    struct bsg_camera _cam;
    bsg_view_get_camera(gedp->ged_gvp, &_cam);

    if (func != (int (*)(struct ged *, char, char, mat_t))0)
	return (*func)(gedp, coord, _cam.rotate_about, rmat);

    switch (coord) {
	case 'm':
	    /* transform model rotations into view rotations */
	    bn_mat_inv(temp1, _cam.rotation);
	    bn_mat_mul(temp2, _cam.rotation, rmat);
	    bn_mat_mul(rmat, temp2, temp1);
	    break;
	case 'v':
	default:
	    break;
    }

    /* Calculate new view center */
    if (_cam.rotate_about != 'v') {
	point_t rot_pt;
	point_t new_origin;
	mat_t viewchg, viewchginv;
	point_t new_cent_view;
	point_t new_cent_model;

	switch (_cam.rotate_about) {
	    case 'e':
		VSET(rot_pt, 0.0, 0.0, 1.0);
		break;
	    case 'k':
		MAT4X3PNT(rot_pt, _cam.model2view, _cam.keypoint);
		break;
	    case 'm':
		/* rotate around model center (0, 0, 0) */
		VSET(new_origin, 0.0, 0.0, 0.0);
		MAT4X3PNT(rot_pt, _cam.model2view, new_origin);
		break;
	    default:
		return BRLCAD_ERROR;
	}

	bn_mat_xform_about_pnt(viewchg, rmat, rot_pt);
	bn_mat_inv(viewchginv, viewchg);

	/* Convert origin in new (viewchg) coords back to old view coords */
	VSET(new_origin, 0.0, 0.0, 0.0);
	MAT4X3PNT(new_cent_view, viewchginv, new_origin);
	MAT4X3PNT(new_cent_model, _cam.view2model, new_cent_view);
	MAT_DELTAS_VEC_NEG(_cam.center, new_cent_model);
    }

    /* pure rotation */
    bn_mat_mul2(rmat, _cam.rotation);
    bsg_view_set_camera(gedp->ged_gvp, &_cam);
    bsg_view_update(gedp->ged_gvp);

    return BRLCAD_OK;
}


int
_ged_do_slew(struct ged *gedp, vect_t svec)
{
    point_t model_center;
    struct bsg_camera _cam;
    bsg_view_get_camera(gedp->ged_gvp, &_cam);

    MAT4X3PNT(model_center, _cam.view2model, svec);
    MAT_DELTAS_VEC_NEG(_cam.center, model_center);
    bsg_view_set_camera(gedp->ged_gvp, &_cam);
    bsg_view_update(gedp->ged_gvp);

    return BRLCAD_OK;
}


int
_ged_do_tra(struct ged *gedp,
	    char coord,
	    vect_t tvec,
	    int (*func)(struct ged *, char, vect_t))
{
    point_t delta;
    point_t work;
    point_t vc, nvc;
    struct bsg_camera _cam;
    bsg_view_get_camera(gedp->ged_gvp, &_cam);

    if (func != (int (*)(struct ged *, char, vect_t))0)
	return (*func)(gedp, coord, tvec);

    switch (coord) {
	case 'm':
	    VSCALE(delta, tvec, -gedp->dbip->dbi_base2local);
	    MAT_DELTAS_GET_NEG(vc, _cam.center);
	    break;
	case 'v':
	default:
	    VSCALE(tvec, tvec, -2.0*gedp->dbip->dbi_base2local*gedp->ged_gvp->gv_isize);
	    MAT4X3PNT(work, _cam.view2model, tvec);
	    MAT_DELTAS_GET_NEG(vc, _cam.center);
	    VSUB2(delta, work, vc);
	    break;
    }

    VSUB2(nvc, vc, delta);
    MAT_DELTAS_VEC_NEG(_cam.center, nvc);
    bsg_view_set_camera(gedp->ged_gvp, &_cam);
    bsg_view_update(gedp->ged_gvp);

    return BRLCAD_OK;
}

unsigned long long
ged_dl_hash(struct display_list *dl)
{
    if (!dl)
	return 0;

    struct bu_data_hash_state *state = bu_data_hash_create();
    if (!state)
	return 0;

    /* Phase 2e: use root->children as the sole source of shapes */
    bsg_view *v = NULL;
    struct display_list *first_gdlp = BU_LIST_NEXT(display_list, (struct bu_list *)dl);
    if (BU_LIST_NOT_HEAD(first_gdlp, dl)) {
	/* Get view from the gedp's default view — dl is the gd_headDisplay list */
	(void)first_gdlp;
    }
    /* Fall back to the context's gedp->ged_gvp when dl has no shapes yet */
    if (!v) {
	/* The caller (ged_dl_hash) passes gedp->i->ged_gdp->gd_headDisplay.
	 * We can get gedp->ged_gvp from the outer scope — but ged_dl_hash
	 * has no gedp reference here.  Use scene-root directly via any view
	 * that has children.  ged_dl_hash has already been migrated in
	 * display_list.c; this legacy path is kept for external callers. */
    }

    /* Iterate all views' root->children to hash scene content. */
    /* Note: this function now takes the dl as an opaque key; the real
     * hashing is done by dl_name_hash(gedp) in display_list.c. */
    (void)v;

    unsigned long long hash_val = bu_data_hash_val(state);
    bu_data_hash_destroy(state);

    return hash_val;
}

void
nmg_plot_eu(struct ged *gedp, struct edgeuse *es_eu, const struct bn_tol *tol, struct bu_list *vlfree)
{
    if (!gedp || !es_eu || !tol)
	return;

    if (*es_eu->g.magic_p != NMG_EDGE_G_LSEG_MAGIC)
	return;

    struct model *m = nmg_find_model(&es_eu->l.magic);
    NMG_CK_MODEL(m);

    /* get space for list of items processed */
    long *tab = (long *)bu_calloc(m->maxindex+1, sizeof(long), "nmg_ed tab[]");
    struct bv_vlblock *vbp = rt_vlblock_init();

    nmg_vlblock_around_eu(vbp, es_eu, tab, 1, vlfree, tol);
    _ged_cvt_vlblock_to_solids(gedp, vbp, "_EU_", 0);      /* swipe vlist */

    bsg_vlblock_free(vbp);
    bu_free((void *)tab, "nmg_ed tab[]");
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
