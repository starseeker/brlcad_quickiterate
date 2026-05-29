/*          R E N D E R _ S E T T I N G S . C
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
/** @file libbsg/render_settings.c
 *
 * Phase D5: bsg_render_settings — per-view rendering policy object.
 * Provides lifecycle and migration helpers.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"

#include "bsg/defines.h"
#include "bsg/render_settings.h"


struct bsg_render_settings *
bsg_render_settings_create(void)
{
    struct bsg_render_settings *rs;
    BU_ALLOC(rs, struct bsg_render_settings);
    bsg_render_settings_init_defaults(rs);
    return rs;
}


void
bsg_render_settings_destroy(struct bsg_render_settings *rs)
{
    if (!rs)
	return;
    bu_free(rs, "bsg_render_settings");
}


void
bsg_render_settings_init_defaults(struct bsg_render_settings *rs)
{
    if (!rs)
	return;
    memset(rs, 0, sizeof(struct bsg_render_settings));
    rs->draw_mode             = 0;   /* wireframe */
    rs->zclip                 = 0;
    rs->transparency_policy   = BSG_TRANSPARENCY_SORTED;
    rs->lod_policy            = BSG_LOD_AUTO;
    rs->lod_forced_level      = 0;
    rs->lod_scale             = 1.0;
    rs->fb_mode               = BSG_FB_OFF;
    rs->hud_enabled           = 1;
    rs->hud_view_scale        = 0;
    rs->hud_view_params       = 0;
    rs->adaptive_plot_mesh    = 0;
    rs->adaptive_plot_csg     = 0;
    rs->bot_threshold         = 0;
    rs->curve_scale           = 0.0;
    rs->point_scale           = 0.0;
}


void
bsg_render_settings_from_view(struct bsg_render_settings *rs,
			       const struct bsg_view *v)
{
    if (!rs || !v)
	return;

    bsg_render_settings_init_defaults(rs);

    const struct bsg_view_settings *s = v->gv_s ? v->gv_s : &v->gv_ls;

    rs->zclip               = s->gv_zclip;
    rs->fb_mode             = (bsg_framebuffer_mode)s->gv_fb_mode;
    rs->adaptive_plot_mesh  = s->adaptive_plot_mesh;
    rs->adaptive_plot_csg   = s->adaptive_plot_csg;
    rs->bot_threshold       = s->bot_threshold;
    rs->curve_scale         = (fastf_t)s->curve_scale;
    rs->point_scale         = (fastf_t)s->point_scale;
    rs->lod_scale           = (fastf_t)s->lod_scale;

    /* View params and scale HUD features mirror their settings flags */
    rs->hud_view_params     = s->gv_view_params.draw;
    rs->hud_view_scale      = s->gv_view_scale.gos_draw;
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
