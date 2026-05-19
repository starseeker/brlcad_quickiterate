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
#include "bsg/appearance.h"
#include "bsg/material.h"
#include "bsg/settings.h"
#include "vmath.h"

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
    struct bsg_appearance appearance;
    struct bsg_material material;
    if (!out)
	return 0;

    bsg_settings_init(out);
    if (!n)
	return 0;

    const struct bv_scene_obj *s = (const struct bv_scene_obj *)n;
    const struct bsg_settings *settings = (s->s_os) ? s->s_os : &s->s_local_os;

    *out = *settings;
    (void)bsg_node_appearance_get(n, &appearance);
    (void)bsg_node_material_get(n, &material);

    out->draw_mode = appearance.draw_mode;
    out->line_width = appearance.line_width;
    out->arrow_tip_length = appearance.arrow_tip_length;
    out->arrow_tip_width = appearance.arrow_tip_width;
    out->draw_solid_lines_only = appearance.draw_solid_lines_only;
    out->draw_non_subtract_only = appearance.draw_non_subtract_only;
    out->transparency = material.transparency;
    out->color_override = material.use_override_color ? 1 : 0;
    if (material.use_override_color) {
	out->color[0] = material.override_rgb[0];
	out->color[1] = material.override_rgb[1];
	out->color[2] = material.override_rgb[2];
    }

    return 1;
}


void
bsg_node_settings_set(bsg_node *n, const struct bsg_settings *s)
{
    struct bsg_appearance appearance;
    struct bsg_material material;
    if (!n || !s)
	return;

    struct bv_scene_obj *obj = (struct bv_scene_obj *)n;
    (void)bsg_node_appearance_get(n, &appearance);
    (void)bsg_node_material_get(n, &material);

    obj->s_local_os = *s;
    obj->s_os = &obj->s_local_os;

    appearance.draw_mode = s->draw_mode;
    appearance.line_width = s->line_width;
    appearance.transparency = s->transparency;
    appearance.arrow_tip_length = s->arrow_tip_length;
    appearance.arrow_tip_width = s->arrow_tip_width;
    appearance.draw_solid_lines_only = s->draw_solid_lines_only;
    appearance.draw_non_subtract_only = s->draw_non_subtract_only;
    bsg_node_appearance_set(n, &appearance);

    material.transparency = s->transparency;
    material.use_override_color = s->color_override ? 1 : 0;
    material.override_rgb[0] = s->color[0];
    material.override_rgb[1] = s->color[1];
    material.override_rgb[2] = s->color[2];
    bsg_node_material_set(n, &material);
}

int
bsg_settings_sync(struct bsg_settings *dest, struct bsg_settings *src)
{
    int ret = 0;
    if (!dest || !src)
	return ret;

    if (dest->line_width != src->line_width) {
	dest->line_width = src->line_width;
	ret = 1;
    }
    if (dest->mixed_modes != src->mixed_modes) {
	dest->mixed_modes = src->mixed_modes;
	ret = 1;
    }
    if (!NEAR_EQUAL(dest->arrow_tip_length, src->arrow_tip_length, SMALL_FASTF)) {
	dest->arrow_tip_length = src->arrow_tip_length;
	ret = 1;
    }
    if (!NEAR_EQUAL(dest->arrow_tip_width, src->arrow_tip_width, SMALL_FASTF)) {
	dest->arrow_tip_width = src->arrow_tip_width;
	ret = 1;
    }
    if (!NEAR_EQUAL(dest->transparency, src->transparency, SMALL_FASTF)) {
	dest->transparency = src->transparency;
	ret = 1;
    }
    if (dest->draw_mode != src->draw_mode) {
	dest->draw_mode = src->draw_mode;
	ret = 1;
    }
    if (dest->color_override != src->color_override) {
	dest->color_override = src->color_override;
	ret = 1;
    }
    if (!VNEAR_EQUAL(dest->color, src->color, SMALL_FASTF)) {
	VMOVE(dest->color, src->color);
	ret = 1;
    }
    if (dest->draw_solid_lines_only != src->draw_solid_lines_only) {
	dest->draw_solid_lines_only = src->draw_solid_lines_only;
	ret = 1;
    }
    if (dest->draw_non_subtract_only != src->draw_non_subtract_only) {
	dest->draw_non_subtract_only = src->draw_non_subtract_only;
	ret = 1;
    }

    return ret;
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
