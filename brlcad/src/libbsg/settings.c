/*                   S E T T I N G S . C
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
/** @file libbsg/settings.c
 *
 * Phase 12: BSG settings-inheritance API.
 *
 * Provides a typed BSG wrapper around the legacy @c bv_obj_settings
 * "inherited-settings" concept so that libdm traversal no longer passes
 * raw @c bv_obj_settings pointers down the scene tree.
 *
 * Storage is backed by @c bv_scene_obj::s_os / @c s_local_os so that no
 * ABI change to @c bsg_node_core is required in this phase.
 */

#include "common.h"

#include <string.h>

#include "bv/defines.h"
#include "bsg/settings.h"

#include "./bsg_private.h"


void
bsg_settings_init(struct bsg_settings *s)
{
    if (!s)
	return;
    s->draw_mode             = 0;
    s->mixed_modes           = 0;
    s->transparency          = 1.0;
    s->color_override        = 0;
    s->color[0]              = 255;
    s->color[1]              = 255;
    s->color[2]              = 255;
    s->line_width            = 1;
    s->arrow_tip_length      = 0.0;
    s->arrow_tip_width       = 0.0;
    s->draw_solid_lines_only = 0;
    s->draw_non_subtract_only = 0;
}


void
bsg_settings_from_legacy_obj_settings(const struct bv_obj_settings *os,
				      struct bsg_settings *out)
{
    if (!out)
	return;

    bsg_settings_init(out);
    if (!os)
	return;

    out->draw_mode             = os->s_dmode;
    out->mixed_modes           = os->mixed_modes;
    out->transparency          = os->transparency;
    out->color_override        = os->color_override;
    out->color[0]              = os->color[0];
    out->color[1]              = os->color[1];
    out->color[2]              = os->color[2];
    out->line_width            = os->s_line_width;
    out->arrow_tip_length      = os->s_arrow_tip_length;
    out->arrow_tip_width       = os->s_arrow_tip_width;
    out->draw_solid_lines_only = os->draw_solid_lines_only;
    out->draw_non_subtract_only = os->draw_non_subtract_only;
}


void
bsg_settings_to_legacy_obj_settings(const struct bsg_settings *s,
				    struct bv_obj_settings *os)
{
    if (!s || !os)
	return;

    os->s_dmode              = s->draw_mode;
    os->mixed_modes          = s->mixed_modes;
    os->transparency         = s->transparency;
    os->color_override       = s->color_override;
    os->color[0]             = s->color[0];
    os->color[1]             = s->color[1];
    os->color[2]             = s->color[2];
    os->s_line_width         = s->line_width;
    os->s_arrow_tip_length   = s->arrow_tip_length;
    os->s_arrow_tip_width    = s->arrow_tip_width;
    os->draw_solid_lines_only  = s->draw_solid_lines_only;
    os->draw_non_subtract_only = s->draw_non_subtract_only;
}


int
bsg_node_settings_get(const bsg_node *n, struct bsg_settings *out)
{
    if (!out)
	return 0;

    bsg_settings_init(out);
    if (!n)
	return 0;

    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    const struct bv_obj_settings *os = (s->s_os) ? s->s_os : &s->s_local_os;

    bsg_settings_from_legacy_obj_settings(os, out);
    return 1;
}


void
bsg_node_settings_set(bsg_node *n, const struct bsg_settings *s)
{
    if (!n || !s)
	return;

    struct bv_scene_obj *obj = (struct bv_scene_obj *)n;
    bsg_settings_to_legacy_obj_settings(s, &obj->s_local_os);
    obj->s_os = &obj->s_local_os;
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
