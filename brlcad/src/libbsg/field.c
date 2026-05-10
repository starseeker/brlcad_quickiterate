/*                      F I E L D . C
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
/** @file libbsg/field.c
 *
 * Phase 6-A: Typed field accessors and notification for BSG nodes.
 *
 * bsg_node_field_touch() delegates to bsg_sensor_notify_field() (defined in
 * sensor.c) to fire any registered FieldSensor or NodeSensor callbacks.
 *
 * Typed accessor implementations mutate the underlying bv_scene_obj field and
 * then call bsg_node_field_touch() so that sensors are notified.
 */

#include "common.h"

#include "bv/defines.h"
#include "bsg/defines.h"
#include "bsg/field.h"
#include "bsg/node.h"
#include "bsg/sensor.h"


void
bsg_node_field_touch(bsg_node *n, bsg_field_id_t fid)
{
    if (!n)
	return;

    /* Delegate to the sensor registry in sensor.c */
    bsg_sensor_notify_field(n, fid);
}


void
bsg_node_set_flag(bsg_node *n, int flag)
{
    if (!n)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    s->s_flag = flag;
    bsg_node_field_touch(n, BSG_FIELD_FLAG);
}


int
bsg_node_get_flag(const bsg_node *n)
{
    if (!n)
	return 0;

    return ((const struct bv_scene_obj *)n)->s_flag;
}


void
bsg_node_set_color(bsg_node *n,
		   unsigned char r,
		   unsigned char g,
		   unsigned char b)
{
    if (!n)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    s->s_color[0] = r;
    s->s_color[1] = g;
    s->s_color[2] = b;
    bsg_node_field_touch(n, BSG_FIELD_COLOR);
}


void
bsg_node_get_color(const bsg_node *n,
		   unsigned char *r,
		   unsigned char *g,
		   unsigned char *b)
{
    if (!n)
	return;

    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    if (r) *r = s->s_color[0];
    if (g) *g = s->s_color[1];
    if (b) *b = s->s_color[2];
}


void
bsg_node_set_visible(bsg_node *n, int on)
{
    if (!n)
	return;

    struct bv_scene_obj *s = (struct bv_scene_obj *)n;
    s->s_flag = on ? UP : DOWN;
    bsg_node_field_touch(n, BSG_FIELD_VISIBILITY);
}


int
bsg_node_get_visible(const bsg_node *n)
{
    return bsg_node_visible(n);
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
