/*                     C A M E R A . C
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @file libbsg/camera.c
 *
 * Phase 7A: BSG camera/view snapshot API.
 *
 * bsg_camera_snapshot_from_bview() extracts a renderer-neutral camera
 * description from struct bview without requiring the caller to know
 * bview internals.
 */

#include "common.h"

#include <string.h>
#include <math.h>

#include "vmath.h"
#include "bv/defines.h"
#include "bsg/camera.h"


void
bsg_camera_snapshot_init(struct bsg_camera_snapshot *snap)
{
    if (!snap)
	return;
    memset(snap, 0, sizeof(*snap));
    snap->width  = 512;
    snap->height = 512;
    snap->aspect = 1.0;
    snap->projection = BSG_CAMERA_ORTHO;
    snap->scale  = 1.0;
    snap->size   = 2.0;
    snap->base2local = 1.0;
    snap->local2base = 1.0;
    MAT_IDN(snap->model2view);
    MAT_IDN(snap->view2model);
    MAT_IDN(snap->pmat);
    MAT_IDN(snap->rotation);
    MAT_IDN(snap->center);
    VSETALL(snap->aet, 0.0);
    VSET(snap->look_dir,  0.0,  0.0, -1.0);
    VSET(snap->up_dir,    0.0,  1.0,  0.0);
    VSET(snap->right_dir, 1.0,  0.0,  0.0);
}


int
bsg_camera_snapshot_from_bview(struct bsg_camera_snapshot *snap,
			       const struct bview *v)
{
    if (!snap || !v)
	return -1;

    bsg_camera_snapshot_init(snap);

    /* Viewport */
    snap->width  = v->gv_width;
    snap->height = v->gv_height;
    if (v->gv_height > 0)
	snap->aspect = (fastf_t)v->gv_width / (fastf_t)v->gv_height;

    /* Projection */
    if (v->gv_perspective > 0.0) {
	snap->projection       = BSG_CAMERA_PERSPECTIVE;
	snap->perspective_angle = v->gv_perspective;
    } else {
	snap->projection       = BSG_CAMERA_ORTHO;
	snap->perspective_angle = 0.0;
    }

    /* Matrices */
    MAT_COPY(snap->model2view, v->gv_model2view);
    MAT_COPY(snap->view2model, v->gv_view2model);
    MAT_COPY(snap->pmat,       v->gv_pmat);

    /* Eye position: origin of model space mapped through view2model */
    {
	point_t eye_view;
	VSET(eye_view, 0.0, 0.0, 0.0);
	MAT4X3PNT(snap->eye_pos, v->gv_view2model, eye_view);
    }

    /*
     * Look direction: -Z axis of view space transformed to model space.
     * The look direction is the third column of gv_view2model (negated
     * because view -Z points into the scene).
     */
    {
	vect_t look_view;
	VSET(look_view, 0.0, 0.0, -1.0);
	MAT4X3VEC(snap->look_dir, v->gv_view2model, look_view);
	VUNITIZE(snap->look_dir);
    }

    /* Up direction: +Y axis of view space in model coordinates */
    {
	vect_t up_view;
	VSET(up_view, 0.0, 1.0, 0.0);
	MAT4X3VEC(snap->up_dir, v->gv_view2model, up_view);
	VUNITIZE(snap->up_dir);
    }

    /* Right direction: +X axis of view space in model coordinates */
    {
	vect_t right_view;
	VSET(right_view, 1.0, 0.0, 0.0);
	MAT4X3VEC(snap->right_dir, v->gv_view2model, right_view);
	VUNITIZE(snap->right_dir);
    }

    /* Scale / unit data */
    snap->scale      = v->gv_scale;
    snap->size       = v->gv_size;
    snap->base2local = v->gv_base2local;
    snap->local2base = v->gv_local2base;

    /* Clip policy: prefer shared settings, fall back to local */
    {
	const struct bview_settings *s =
	    v->gv_s ? v->gv_s : (const struct bview_settings *)&v->gv_ls;
	snap->zclip = s ? s->gv_zclip : 0;
    }

    /* Orientation state for faceplate/HUD consumers */
    MAT_COPY(snap->rotation, v->gv_rotation);
    MAT_COPY(snap->center,   v->gv_center);
    VMOVE(snap->aet, v->gv_aet);

    return 0;
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
