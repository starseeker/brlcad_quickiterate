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
 * Phase 7: Camera snapshot — capture and restore bsg_view camera state.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "vmath.h"

#include "bsg/defines.h"
#include "bsg/camera.h"


struct bsg_camera *
bsg_camera_create(void)
{
    struct bsg_camera *cam;
    BU_ALLOC(cam, struct bsg_camera);
    memset(cam, 0, sizeof(struct bsg_camera));
    cam->scale       = 1.0;
    cam->perspective = 0.0;
    VSETALL(cam->aet, 0.0);
    VSETALL(cam->eye_pos, 0.0);
    MAT_IDN(cam->rotation);
    MAT_IDN(cam->center);
    MAT_IDN(cam->model2view);
    MAT_IDN(cam->view2model);
    return cam;
}


void
bsg_camera_destroy(struct bsg_camera *cam)
{
    if (!cam)
	return;
    bu_free(cam, "bsg_camera");
}


struct bsg_camera *
bsg_camera_snapshot(const struct bsg_view *v)
{
    if (!v)
	return NULL;

    struct bsg_camera *cam = bsg_camera_create();
    if (!cam)
	return NULL;

    cam->scale       = v->gv_scale;
    cam->perspective = v->gv_perspective;
    VMOVE(cam->aet,     v->gv_aet);
    VMOVE(cam->eye_pos, v->gv_eye_pos);
    MAT_COPY(cam->rotation,   v->gv_rotation);
    MAT_COPY(cam->center,     v->gv_center);
    MAT_COPY(cam->model2view, v->gv_model2view);
    MAT_COPY(cam->view2model, v->gv_view2model);

    return cam;
}


void
bsg_camera_apply(struct bsg_camera *cam, struct bsg_view *v)
{
    if (!cam || !v)
	return;

    v->gv_scale       = cam->scale;
    v->gv_perspective = cam->perspective;
    VMOVE(v->gv_aet,     cam->aet);
    VMOVE(v->gv_eye_pos, cam->eye_pos);
    MAT_COPY(v->gv_rotation,   cam->rotation);
    MAT_COPY(v->gv_center,     cam->center);
    MAT_COPY(v->gv_model2view, cam->model2view);
    MAT_COPY(v->gv_view2model, cam->view2model);
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
