/*                  R E N D E R _ O P T S . C P P
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
/** @file librender/render_opts.cpp
 *
 * Render options management: allocation, destruction, and setter functions.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "vmath.h"

#include "render.h"
#include "./render_private.h"


struct render_opts *
render_opts_create(void)
{
    struct render_opts *opts;

    BU_GET(opts, struct render_opts);
    memset(opts, 0, sizeof(*opts));

    /* safe defaults */
    opts->width          = 512;
    opts->height         = 512;
    opts->aspect         = 1.0;
    opts->lightmodel     = RENDER_LIGHT_DIFFUSE;
    opts->nthreads       = 1;
    opts->perspective_deg = 0.0;   /* orthographic */
    opts->view_set       = 0;

    MAT_IDN(opts->view2model);
    VSETALL(opts->eye_model, 0.0);
    opts->viewsize = 1.0;

    VSETALL(opts->background, 0.0);   /* black */

    return opts;
}


void
render_opts_destroy(struct render_opts *opts)
{
    if (!opts)
	return;
    BU_PUT(opts, struct render_opts);
}


void
render_opts_set_size(struct render_opts *opts, int width, int height)
{
    if (!opts)
	return;
    opts->width  = (width  > 0) ? width  : 1;
    opts->height = (height > 0) ? height : 1;
}


void
render_opts_set_view(struct render_opts *opts,
		     const double *view2model,
		     const double *eye,
		     double viewsize)
{
    int i;

    if (!opts)
	return;

    if (view2model) {
	for (i = 0; i < 16; i++)
	    opts->view2model[i] = view2model[i];
    }

    if (eye) {
	opts->eye_model[X] = eye[0];
	opts->eye_model[Y] = eye[1];
	opts->eye_model[Z] = eye[2];
    }

    if (viewsize > 0.0)
	opts->viewsize = viewsize;

    opts->view_set = 1;
}


void
render_opts_set_aspect(struct render_opts *opts, double aspect)
{
    if (!opts)
	return;
    opts->aspect = (aspect > 0.0) ? aspect : 1.0;
}


void
render_opts_set_lighting(struct render_opts *opts, int lightmodel)
{
    if (!opts)
	return;
    opts->lightmodel = lightmodel;
}


void
render_opts_set_threads(struct render_opts *opts, int nthreads)
{
    if (!opts)
	return;
    opts->nthreads = nthreads;
}


void
render_opts_set_background(struct render_opts *opts,
			   double r, double g, double b)
{
    if (!opts)
	return;
    opts->background[0] = r;
    opts->background[1] = g;
    opts->background[2] = b;
}


void
render_opts_set_perspective(struct render_opts *opts, double perspective_deg)
{
    if (!opts)
	return;
    opts->perspective_deg = (perspective_deg >= 0.0) ? perspective_deg : 0.0;
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
