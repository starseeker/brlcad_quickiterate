/*                          R E C T . C
 * BRL-CAD
 *
 * Copyright (c) 1998-2025 United States Government as represented by
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
/** @file mged/rect.c
 *
 * Routines to implement MGED's rubber band rectangle capability.
 *
 */

#include "common.h"

#include <math.h>

#include "vmath.h"
#include "ged.h"
/* Step 8: dm.h removed — all drawing functions in rect.c are no-op stubs. */
#include "./mged.h"
#include "bsg/util.h"
#include "./mged_dm.h"

extern int mged_vscale(struct mged_state *s, fastf_t sfactor);

static void adjust_rect_for_zoom(struct mged_state *);

struct _rubber_band default_rubber_band = {
    /* rb_rc */		1,
    /* rb_active */	0,
    /* rb_draw */	0,
    /* rb_linewidth */	0,
    /* rb_linestyle */	's',
    /* rb_pos */	{ 0, 0 },
    /* rb_dim */	{ 0, 0 },
    /* rb_x */		0.0,
    /* rb_y */		0.0,
    /* rb_width */	0.0,
    /* rb_height */	0.0
};


#define RB_O(_m) bu_offsetof(struct _rubber_band, _m)
struct bu_structparse rubber_band_vparse[] = {
    {"%d",	1, "draw",	RB_O(rb_draw),		rb_set_dirty_flag, NULL, NULL },
    {"%d",	1, "linewidth",	RB_O(rb_linewidth),	rb_set_dirty_flag, NULL, NULL },
    {"%c",	1, "linestyle",	RB_O(rb_linestyle),	rb_set_dirty_flag, NULL, NULL },
    {"%d",	2, "pos",	RB_O(rb_pos),		set_rect, NULL, NULL },
    {"%d",	2, "dim",	RB_O(rb_dim),		set_rect, NULL, NULL },
    {"",	0, (char *)0,	0,			BU_STRUCTPARSE_FUNC_NULL, NULL, NULL }
};


void
rb_set_dirty_flag(const struct bu_structparse *UNUSED(sdp),
		  const char *UNUSED(name),
		  void *UNUSED(base),
		  const char *UNUSED(value),
		  void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    /* Step 7.20: mp_dmp removed; update_views triggers Obol refresh. */
    s->update_views = 1;
}


/*
 * Given position and dimensions in normalized view coordinates, calculate
 * position and dimensions in image coordinates.
 */
void
rect_view2image(struct mged_state *UNUSED(s))
{
    /* Step 7.20: libdm removed — no-op. */
}


/*
 * Given position and dimensions in image coordinates, calculate
 * position and dimensions in normalized view coordinates.
 */
void
rect_image2view(struct mged_state *UNUSED(s))
{
    /* Step 7.20: libdm removed — no-op. */
}


void
set_rect(const struct bu_structparse *sdp,
	 const char *name,
	 void *base,
	 const char *value,
	 void *data)
{
    struct mged_state *s = (struct mged_state *)data;
    MGED_CK_STATE(s);
    rect_image2view(s);
    rb_set_dirty_flag(sdp, name, base, value, data);
}


/*
 * Adjust the rubber band to have the same aspect ratio as the window.
 */
static void
adjust_rect_for_zoom(struct mged_state *s)
{
    fastf_t width, height;

    if (rubber_band->rb_width >= 0.0)
	width = rubber_band->rb_width;
    else
	width = -rubber_band->rb_width;

    if (rubber_band->rb_height >= 0.0)
	height = rubber_band->rb_height;
    else
	height = -rubber_band->rb_height;

    if (width >= height) {
	if (rubber_band->rb_height >= 0.0)
	    rubber_band->rb_height = width;
	else
	    rubber_band->rb_height = -width;
    } else {
	if (rubber_band->rb_width >= 0.0)
	    rubber_band->rb_width = height;
	else
	    rubber_band->rb_width = -height;
    }
}


void
draw_rect(struct mged_state *UNUSED(s))
{
    /* Step 7.20: libdm removed — no-op. */
}


void
paint_rect_area(struct mged_state *UNUSED(s))
{
    /* Step 7.20: fbp removed — no-op. */
}


void
rt_rect_area(struct mged_state *UNUSED(s))
{
    /* Step 7.20: fbp/DMP removed — no-op. */
}

void
mged_center(struct mged_state *s, point_t center)
{
    char *av[5];
    char xbuf[32];
    char ybuf[32];
    char zbuf[32];

    if (s->gedp == GED_NULL) {
       return;
    }

    snprintf(xbuf, 32, "%f", center[X]);
    snprintf(ybuf, 32, "%f", center[Y]);
    snprintf(zbuf, 32, "%f", center[Z]);

    av[0] = "center";
    av[1] = xbuf;
    av[2] = ybuf;
    av[3] = zbuf;
    av[4] = (char *)0;
    ged_exec_center(s->gedp, 4, (const char **)av);
    (void)mged_svbase(s);
    s->update_views = 1;
    view_state->vs_flag = 1;
}

void
zoom_rect_area(struct mged_state *s)
{
    fastf_t width, height;
    fastf_t sf;
    point_t old_model_center;
    point_t new_model_center;
    point_t old_view_center;
    point_t new_view_center;

    if (ZERO(rubber_band->rb_width) &&
	ZERO(rubber_band->rb_height))
	return;

    adjust_rect_for_zoom(s);

    /* find old view center */
    {
	struct bsg_camera _rc;
	bsg_view_get_camera(view_state->vs_gvp, &_rc);
	MAT_DELTAS_GET_NEG(old_model_center, _rc.center);
	MAT4X3PNT(old_view_center, _rc.model2view, old_model_center);

	/* calculate new view center */
	VSET(new_view_center,
	     rubber_band->rb_x + rubber_band->rb_width / 2.0,
	     rubber_band->rb_y + rubber_band->rb_height / 2.0,
	     old_view_center[Z]);

	/* find new model center */
	MAT4X3PNT(new_model_center, _rc.view2model, new_view_center);
    }
    mged_center(s, new_model_center);

    /* zoom in to fill rectangle */
    if (rubber_band->rb_width >= 0.0)
	width = rubber_band->rb_width;
    else
	width = -rubber_band->rb_width;

    if (rubber_band->rb_height >= 0.0)
	height = rubber_band->rb_height;
    else
	height = -rubber_band->rb_height;

    if (width >= height)
	sf = width / 2.0;
    else
	sf = height / 2.0;

    mged_vscale(s, sf);

    rubber_band->rb_x = -1.0;
    rubber_band->rb_y = -1.0;
    rubber_band->rb_width = 2.0;
    rubber_band->rb_height = 2.0;

    rect_view2image(s);

    {
	/* need dummy values for func signature--they are unused in the func */
	const struct bu_structparse *sdp = 0;
	const char name[] = "name";
	void *base = 0;
	const char value[] = "value";
	rb_set_dirty_flag(sdp, name, base, value, NULL);
    }
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
