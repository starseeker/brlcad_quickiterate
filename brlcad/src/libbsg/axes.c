/*                        A X E S . C
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
/** @file libbsg/axes.c
 *
 * Slice 6 (bv_scene_obj_migrate): BSG axes overlay payload.
 *
 * Provides the create/destroy and accessor implementations for
 * BSG_PAYLOAD_TYPE_AXES payloads.  The payload stores the same fields
 * as bv_axes (position, size, line width, colors, label options,
 * tick options) but is owned by the BSG scene graph rather than being
 * embedded directly in a bview or bv_scene_obj.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bsg/defines.h"
#include "bsg/payload.h"
#include "bsg/axes.h"

/* ------------------------------------------------------------------ */
/* Private implementation struct                                       */
/* ------------------------------------------------------------------ */

struct _bsg_payload_axes {
    struct bsg_payload base;

    int       draw;
    point_t   axes_pos;
    fastf_t   axes_size;
    int       line_width;
    int       axes_color[3];

    int       pos_only;
    int       label_flag;
    int       label_color[3];
    int       triple_color;

    int       tick_enabled;
    int       tick_length;
    int       tick_major_length;
    fastf_t   tick_interval;
    int       ticks_per_major;
    int       tick_threshold;
    int       tick_color[3];
    int       tick_major_color[3];
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

struct bsg_payload *
bsg_payload_axes_create(void)
{
    struct _bsg_payload_axes *ap = NULL;
    struct bsg_payload *p = NULL;

    BU_ALLOC(ap, struct _bsg_payload_axes);
    if (!ap)
	return NULL;
    memset(ap, 0, sizeof(*ap));

    p = &ap->base;
    p->type = BSG_PAYLOAD_TYPE_AXES;
    /* free_fn is NULL: plain bu_free suffices (no heap sub-allocations). */
    return p;
}


/* draw flag */

void
bsg_payload_axes_draw_set(struct bsg_payload *axes_payload, int flag)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->draw = flag;
}

int
bsg_payload_axes_draw_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return 0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->draw;
}


/* position */

void
bsg_payload_axes_pos_set(struct bsg_payload *axes_payload, const point_t pos)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    if (!pos)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    VMOVE(ap->axes_pos, pos);
}

void
bsg_payload_axes_pos_get(const struct bsg_payload *axes_payload, point_t out)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    if (!out)
	return;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    VMOVE(out, ap->axes_pos);
}


/* size */

void
bsg_payload_axes_size_set(struct bsg_payload *axes_payload, fastf_t size)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->axes_size = size;
}

fastf_t
bsg_payload_axes_size_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return (fastf_t)0.0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->axes_size;
}


/* line width */

void
bsg_payload_axes_line_width_set(struct bsg_payload *axes_payload, int width)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->line_width = width;
}

int
bsg_payload_axes_line_width_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return 0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->line_width;
}


/* axes color */

void
bsg_payload_axes_color_set(struct bsg_payload *axes_payload, const int rgb[3])
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    if (!rgb)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->axes_color[0] = rgb[0];
    ap->axes_color[1] = rgb[1];
    ap->axes_color[2] = rgb[2];
}

void
bsg_payload_axes_color_get(const struct bsg_payload *axes_payload, int rgb_out[3])
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    if (!rgb_out)
	return;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    rgb_out[0] = ap->axes_color[0];
    rgb_out[1] = ap->axes_color[1];
    rgb_out[2] = ap->axes_color[2];
}


/* label flag */

void
bsg_payload_axes_label_flag_set(struct bsg_payload *axes_payload, int flag)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->label_flag = flag;
}

int
bsg_payload_axes_label_flag_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return 0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->label_flag;
}


/* label color */

void
bsg_payload_axes_label_color_set(struct bsg_payload *axes_payload, const int rgb[3])
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    if (!rgb)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->label_color[0] = rgb[0];
    ap->label_color[1] = rgb[1];
    ap->label_color[2] = rgb[2];
}

void
bsg_payload_axes_label_color_get(const struct bsg_payload *axes_payload, int rgb_out[3])
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    if (!rgb_out)
	return;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    rgb_out[0] = ap->label_color[0];
    rgb_out[1] = ap->label_color[1];
    rgb_out[2] = ap->label_color[2];
}


/* triple color */

void
bsg_payload_axes_triple_color_set(struct bsg_payload *axes_payload, int flag)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->triple_color = flag;
}

int
bsg_payload_axes_triple_color_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return 0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->triple_color;
}


/* pos_only */

void
bsg_payload_axes_pos_only_set(struct bsg_payload *axes_payload, int flag)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->pos_only = flag;
}

int
bsg_payload_axes_pos_only_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return 0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->pos_only;
}


/* tick enabled */

void
bsg_payload_axes_tick_enabled_set(struct bsg_payload *axes_payload, int flag)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->tick_enabled = flag;
}

int
bsg_payload_axes_tick_enabled_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return 0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->tick_enabled;
}


/* tick length */

void
bsg_payload_axes_tick_length_set(struct bsg_payload *axes_payload, int length)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->tick_length = length;
}

int
bsg_payload_axes_tick_length_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return 0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->tick_length;
}


/* tick major length */

void
bsg_payload_axes_tick_major_length_set(struct bsg_payload *axes_payload, int length)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->tick_major_length = length;
}

int
bsg_payload_axes_tick_major_length_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return 0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->tick_major_length;
}


/* tick interval */

void
bsg_payload_axes_tick_interval_set(struct bsg_payload *axes_payload, fastf_t interval)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->tick_interval = interval;
}

fastf_t
bsg_payload_axes_tick_interval_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return (fastf_t)0.0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->tick_interval;
}


/* ticks per major */

void
bsg_payload_axes_ticks_per_major_set(struct bsg_payload *axes_payload, int n)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->ticks_per_major = n;
}

int
bsg_payload_axes_ticks_per_major_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return 0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->ticks_per_major;
}


/* tick threshold */

void
bsg_payload_axes_tick_threshold_set(struct bsg_payload *axes_payload, int threshold)
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->tick_threshold = threshold;
}

int
bsg_payload_axes_tick_threshold_get(const struct bsg_payload *axes_payload)
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return 0;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    return ap->tick_threshold;
}


/* tick color */

void
bsg_payload_axes_tick_color_set(struct bsg_payload *axes_payload, const int rgb[3])
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    if (!rgb)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->tick_color[0] = rgb[0];
    ap->tick_color[1] = rgb[1];
    ap->tick_color[2] = rgb[2];
}

void
bsg_payload_axes_tick_color_get(const struct bsg_payload *axes_payload, int rgb_out[3])
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    if (!rgb_out)
	return;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    rgb_out[0] = ap->tick_color[0];
    rgb_out[1] = ap->tick_color[1];
    rgb_out[2] = ap->tick_color[2];
}


/* tick major color */

void
bsg_payload_axes_tick_major_color_set(struct bsg_payload *axes_payload, const int rgb[3])
{
    struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    if (!rgb)
	return;
    ap = (struct _bsg_payload_axes *)axes_payload;
    ap->tick_major_color[0] = rgb[0];
    ap->tick_major_color[1] = rgb[1];
    ap->tick_major_color[2] = rgb[2];
}

void
bsg_payload_axes_tick_major_color_get(const struct bsg_payload *axes_payload, int rgb_out[3])
{
    const struct _bsg_payload_axes *ap = NULL;
    if (!axes_payload || axes_payload->type != BSG_PAYLOAD_TYPE_AXES)
	return;
    if (!rgb_out)
	return;
    ap = (const struct _bsg_payload_axes *)axes_payload;
    rgb_out[0] = ap->tick_major_color[0];
    rgb_out[1] = ap->tick_major_color[1];
    rgb_out[2] = ap->tick_major_color[2];
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
