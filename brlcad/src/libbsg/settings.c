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
 * Provides the typed BSG "inherited-settings" concept used by libdm traversal.
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


int
bsg_node_settings_get(const bsg_node *n, struct bsg_settings *out)
{
    if (!out)
	return 0;

    bsg_settings_init(out);
    if (!n)
	return 0;

    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    const struct bsg_settings *settings = (s->s_os) ? s->s_os : &s->s_local_os;

    *out = *settings;
    return 1;
}


void
bsg_node_settings_set(bsg_node *n, const struct bsg_settings *s)
{
    if (!n || !s)
	return;

    struct bv_scene_obj *obj = (struct bv_scene_obj *)n;
    obj->s_local_os = *s;
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
