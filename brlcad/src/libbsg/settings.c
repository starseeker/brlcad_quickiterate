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
    struct bsg_appearance app;
    struct bsg_material mat;
    if (!out)
	return 0;

    bsg_settings_init(out);
    bsg_appearance_init(&app);
    bsg_material_init(&mat);
    if (!n)
	return 0;

    const struct bsg_settings *settings = _bsg_settings_effective(n);

    *out = *settings;
    (void)bsg_node_appearance_get(n, &app);
    (void)bsg_node_material_get(n, &mat);

    out->draw_mode = app.draw_mode;
    out->line_width = app.line_width;
    out->arrow_tip_length = app.arrow_tip_length;
    out->arrow_tip_width = app.arrow_tip_width;
    out->draw_solid_lines_only = app.draw_solid_lines_only;
    out->draw_non_subtract_only = app.draw_non_subtract_only;
    out->transparency = mat.transparency;
    out->color_override = mat.use_override_color ? 1 : 0;
    if (mat.use_override_color) {
	out->color[0] = mat.override_rgb[0];
	out->color[1] = mat.override_rgb[1];
	out->color[2] = mat.override_rgb[2];
    }

    return 1;
}


void
bsg_node_settings_set(bsg_node *n, const struct bsg_settings *s)
{
    struct bsg_appearance app;
    struct bsg_material mat;
    struct bsg_settings *local;
    struct bsg_settings *effective;
    if (!n || !s)
	return;

    bsg_appearance_init(&app);
    bsg_material_init(&mat);
    (void)bsg_node_appearance_get(n, &app);
    (void)bsg_node_material_get(n, &mat);

    local = _bsg_settings_local_get_or_create(n);
    effective = _bsg_settings_effective_get_or_create(n);
    if (!local || !effective)
	return;

    *local = *s;
    *effective = *s;

    app.draw_mode = s->draw_mode;
    app.line_width = s->line_width;
    app.arrow_tip_length = s->arrow_tip_length;
    app.arrow_tip_width = s->arrow_tip_width;
    app.draw_solid_lines_only = s->draw_solid_lines_only;
    app.draw_non_subtract_only = s->draw_non_subtract_only;
    bsg_node_appearance_set(n, &app);

    mat.transparency = s->transparency;
    mat.use_override_color = s->color_override ? 1 : 0;
    if (mat.use_override_color) {
	mat.override_rgb[0] = s->color[0];
	mat.override_rgb[1] = s->color[1];
	mat.override_rgb[2] = s->color[2];
    }
    bsg_node_material_set(n, &mat);
    _bsg_settings_legacy_sync(n);
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
