/*                         H U D . C
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
/** @file libbsg/hud.c
 *
 * HUD/view-option slice (slice 4 of bv_scene_obj_migrate.txt):
 * BSG HUD and view-option state implementation.
 *
 * Provides init, from-bview_settings, and to-bview_settings functions for
 * all BSG HUD types, plus the ADC math helpers that replace the legacy
 * adc_*() functions in bv/adc.h.
 */

#include "common.h"

#include <string.h>
#include <math.h>

#include "vmath.h"
#include "bv/defines.h"
#include "bsg/hud.h"

/* From include/bv/defines.h (BV_MAX is the BV half-range = 2047) */
#ifndef INV_BV
#  define INV_BV 0.00048828125
#endif


/* ------------------------------------------------------------------ */
/* bsg_adc_state                                                        */
/* ------------------------------------------------------------------ */

void
bsg_adc_state_init(struct bsg_adc_state *adc)
{
    if (!adc)
	return;
    memset(adc, 0, sizeof(*adc));
    adc->a1 = 45.0;
    adc->a2 = 45.0;
    adc->dst = M_SQRT1_2;  /* (0 * INV_BV + 1.0) * M_SQRT1_2 */
    adc->line_color[0] = 255;
    adc->line_color[1] = 255;
    adc->line_color[2] = 0;   /* yellow */
    adc->tick_color[0] = 255;
    adc->tick_color[1] = 255;
    adc->tick_color[2] = 255;
    adc->line_width = 1;
}


void
bsg_adc_model_to_view(struct bsg_adc_state *adc, mat_t model2view,
                      fastf_t amax)
{
    if (!adc)
	return;
    MAT4X3PNT(adc->pos_view, model2view, adc->pos_model);
    adc->dv_x = (int)(adc->pos_view[X] * amax);
    adc->dv_y = (int)(adc->pos_view[Y] * amax);
}


void
bsg_adc_grid_to_view(struct bsg_adc_state *adc, mat_t model2view,
                     fastf_t amax)
{
    point_t model_pt = VINIT_ZERO;
    point_t view_pt;

    if (!adc)
	return;
    MAT4X3PNT(view_pt, model2view, model_pt);
    VADD2(adc->pos_view, view_pt, adc->pos_grid);
    adc->dv_x = (int)(adc->pos_view[X] * amax);
    adc->dv_y = (int)(adc->pos_view[Y] * amax);
}


void
bsg_adc_view_to_grid(struct bsg_adc_state *adc, mat_t model2view)
{
    point_t model_pt = VINIT_ZERO;
    point_t view_pt;

    if (!adc)
	return;
    MAT4X3PNT(view_pt, model2view, model_pt);
    VSUB2(adc->pos_grid, adc->pos_view, view_pt);
}


void
bsg_adc_reset(struct bsg_adc_state *adc, mat_t view2model, mat_t model2view)
{
    if (!adc)
	return;
    adc->dv_x = 0;
    adc->dv_y = 0;
    adc->dv_a1 = 0;
    adc->dv_a2 = 0;
    adc->dv_dist = 0;
    VSETALL(adc->pos_view, 0.0);
    MAT4X3PNT(adc->pos_model, view2model, adc->pos_view);
    adc->dst = (adc->dv_dist * INV_BV + 1.0) * M_SQRT1_2;
    adc->a1 = 45.0;
    adc->a2 = 45.0;
    bsg_adc_view_to_grid(adc, model2view);
    VSETALL(adc->anchor_pt_a1, 0.0);
    VSETALL(adc->anchor_pt_a2, 0.0);
    VSETALL(adc->anchor_pt_dst, 0.0);
    adc->anchor_pos = 0;
    adc->anchor_a1  = 0;
    adc->anchor_a2  = 0;
    adc->anchor_dst = 0;
}


/* ------------------------------------------------------------------ */
/* bsg_grid_state                                                       */
/* ------------------------------------------------------------------ */

void
bsg_grid_state_init(struct bsg_grid_state *grid)
{
    if (!grid)
	return;
    memset(grid, 0, sizeof(*grid));
    grid->res_h = 1.0;
    grid->res_v = 1.0;
    grid->res_major_h = 5;
    grid->res_major_v = 5;
    grid->color[0] = 255;
    grid->color[1] = 255;
    grid->color[2] = 255;
}


/* ------------------------------------------------------------------ */
/* bsg_rect_state                                                       */
/* ------------------------------------------------------------------ */

void
bsg_rect_state_init(struct bsg_rect_state *rect)
{
    if (!rect)
	return;
    memset(rect, 0, sizeof(*rect));
    rect->color[0] = 255;
    rect->color[1] = 255;
    rect->color[2] = 255;
    rect->aspect   = 1.0;
    rect->line_width = 1;
}


/* ------------------------------------------------------------------ */
/* bsg_params_state                                                     */
/* ------------------------------------------------------------------ */

void
bsg_params_state_init(struct bsg_params_state *params)
{
    if (!params)
	return;
    memset(params, 0, sizeof(*params));
    params->color[0] = 255;
    params->color[1] = 255;
    params->color[2] = 255;
    params->font_size = 10;
}


/* ------------------------------------------------------------------ */
/* bsg_other_state                                                      */
/* ------------------------------------------------------------------ */

void
bsg_other_state_init(struct bsg_other_state *other)
{
    if (!other)
	return;
    memset(other, 0, sizeof(*other));
    other->gos_text_color[0] = 255;
    other->gos_text_color[1] = 255;
    other->gos_text_color[2] = 255;
    other->gos_line_color[0] = 255;
    other->gos_line_color[1] = 255;
    other->gos_line_color[2] = 255;
    other->gos_font_size = 10;
}


/* ------------------------------------------------------------------ */
/* bsg_axes                                                             */
/* ------------------------------------------------------------------ */

void
bsg_axes_init(struct bsg_axes *axes)
{
    if (!axes)
	return;
    memset(axes, 0, sizeof(*axes));
    VSETALL(axes->axes_pos, 0.0);
    axes->axes_size  = 0.2;   /* 20% of view width is a typical default */
    axes->line_width = 1;
    axes->axes_color[0] = 255;
    axes->axes_color[1] = 255;
    axes->axes_color[2] = 255;
    axes->label_color[0] = 255;
    axes->label_color[1] = 255;
    axes->label_color[2] = 255;
    axes->tick_color[0]  = 255;
    axes->tick_color[1]  = 255;
    axes->tick_color[2]  = 255;
    axes->tick_major_color[0] = 255;
    axes->tick_major_color[1] = 255;
    axes->tick_major_color[2] = 255;
    axes->tick_interval   = 1.0;
    axes->ticks_per_major = 5;
    axes->tick_threshold  = 8;
    axes->tick_length     = 4;
    axes->tick_major_length = 8;
}


/* ------------------------------------------------------------------ */
/* bsg_hud_opts                                                         */
/* ------------------------------------------------------------------ */

void
bsg_hud_opts_init(struct bsg_hud_opts *opts)
{
    if (!opts)
	return;
    bsg_axes_init(&opts->model_axes);
    bsg_axes_init(&opts->view_axes);
    bsg_grid_state_init(&opts->grid);
    bsg_other_state_init(&opts->center_dot);
    bsg_params_state_init(&opts->view_params);
    bsg_other_state_init(&opts->view_scale);
    opts->frametime = 0.0;
    opts->fb_mode   = 0;
    bsg_adc_state_init(&opts->adc);
    bsg_rect_state_init(&opts->rect);
}


/* ------------------------------------------------------------------ */
/* Bridge helpers: bview_settings <-> bsg_hud_opts                     */
/* ------------------------------------------------------------------ */

/* Inline helpers to copy bv_axes <-> bsg_axes */

static void
_copy_bv_axes_to_bsg(struct bsg_axes *dst, const struct bv_axes *src)
{
    dst->draw = src->draw;
    VMOVE(dst->axes_pos, src->axes_pos);
    dst->axes_size  = src->axes_size;
    dst->line_width = src->line_width;
    dst->axes_color[0] = src->axes_color[0];
    dst->axes_color[1] = src->axes_color[1];
    dst->axes_color[2] = src->axes_color[2];
    dst->pos_only   = src->pos_only;
    dst->label_flag = src->label_flag;
    dst->label_color[0] = src->label_color[0];
    dst->label_color[1] = src->label_color[1];
    dst->label_color[2] = src->label_color[2];
    dst->triple_color = src->triple_color;
    dst->tick_enabled = src->tick_enabled;
    dst->tick_length  = src->tick_length;
    dst->tick_major_length = src->tick_major_length;
    dst->tick_interval     = src->tick_interval;
    dst->ticks_per_major   = src->ticks_per_major;
    dst->tick_threshold    = src->tick_threshold;
    dst->tick_color[0] = src->tick_color[0];
    dst->tick_color[1] = src->tick_color[1];
    dst->tick_color[2] = src->tick_color[2];
    dst->tick_major_color[0] = src->tick_major_color[0];
    dst->tick_major_color[1] = src->tick_major_color[1];
    dst->tick_major_color[2] = src->tick_major_color[2];
}

static void
_copy_bsg_axes_to_bv(struct bv_axes *dst, const struct bsg_axes *src)
{
    dst->draw = src->draw;
    VMOVE(dst->axes_pos, src->axes_pos);
    dst->axes_size  = src->axes_size;
    dst->line_width = src->line_width;
    dst->axes_color[0] = src->axes_color[0];
    dst->axes_color[1] = src->axes_color[1];
    dst->axes_color[2] = src->axes_color[2];
    dst->pos_only   = src->pos_only;
    dst->label_flag = src->label_flag;
    dst->label_color[0] = src->label_color[0];
    dst->label_color[1] = src->label_color[1];
    dst->label_color[2] = src->label_color[2];
    dst->triple_color = src->triple_color;
    dst->tick_enabled = src->tick_enabled;
    dst->tick_length  = src->tick_length;
    dst->tick_major_length = src->tick_major_length;
    dst->tick_interval     = src->tick_interval;
    dst->ticks_per_major   = src->ticks_per_major;
    dst->tick_threshold    = src->tick_threshold;
    dst->tick_color[0] = src->tick_color[0];
    dst->tick_color[1] = src->tick_color[1];
    dst->tick_color[2] = src->tick_color[2];
    dst->tick_major_color[0] = src->tick_major_color[0];
    dst->tick_major_color[1] = src->tick_major_color[1];
    dst->tick_major_color[2] = src->tick_major_color[2];
}


int
bsg_hud_opts_from_bview_settings(struct bsg_hud_opts *opts,
                                  const struct bview_settings *s)
{
    if (!opts || !s)
	return -1;

    /* Axes */
    _copy_bv_axes_to_bsg(&opts->model_axes, &s->gv_model_axes);
    _copy_bv_axes_to_bsg(&opts->view_axes,  &s->gv_view_axes);

    /* Grid */
    opts->grid.rc           = s->gv_grid.rc;
    opts->grid.draw         = s->gv_grid.draw;
    opts->grid.adaptive     = s->gv_grid.adaptive;
    opts->grid.snap         = s->gv_grid.snap;
    VMOVE(opts->grid.anchor, s->gv_grid.anchor);
    opts->grid.res_h        = s->gv_grid.res_h;
    opts->grid.res_v        = s->gv_grid.res_v;
    opts->grid.res_major_h  = s->gv_grid.res_major_h;
    opts->grid.res_major_v  = s->gv_grid.res_major_v;
    opts->grid.color[0]     = s->gv_grid.color[0];
    opts->grid.color[1]     = s->gv_grid.color[1];
    opts->grid.color[2]     = s->gv_grid.color[2];

    /* Center dot */
    opts->center_dot.gos_draw        = s->gv_center_dot.gos_draw;
    opts->center_dot.gos_line_color[0] = s->gv_center_dot.gos_line_color[0];
    opts->center_dot.gos_line_color[1] = s->gv_center_dot.gos_line_color[1];
    opts->center_dot.gos_line_color[2] = s->gv_center_dot.gos_line_color[2];
    opts->center_dot.gos_text_color[0] = s->gv_center_dot.gos_text_color[0];
    opts->center_dot.gos_text_color[1] = s->gv_center_dot.gos_text_color[1];
    opts->center_dot.gos_text_color[2] = s->gv_center_dot.gos_text_color[2];
    opts->center_dot.gos_font_size     = s->gv_center_dot.gos_font_size;

    /* View params */
    opts->view_params.draw        = s->gv_view_params.draw;
    opts->view_params.draw_size   = s->gv_view_params.draw_size;
    opts->view_params.draw_center = s->gv_view_params.draw_center;
    opts->view_params.draw_az     = s->gv_view_params.draw_az;
    opts->view_params.draw_el     = s->gv_view_params.draw_el;
    opts->view_params.draw_tw     = s->gv_view_params.draw_tw;
    opts->view_params.draw_fps    = s->gv_view_params.draw_fps;
    opts->view_params.color[0]    = s->gv_view_params.color[0];
    opts->view_params.color[1]    = s->gv_view_params.color[1];
    opts->view_params.color[2]    = s->gv_view_params.color[2];
    opts->view_params.font_size   = s->gv_view_params.font_size;

    /* View scale */
    opts->view_scale.gos_draw        = s->gv_view_scale.gos_draw;
    opts->view_scale.gos_line_color[0] = s->gv_view_scale.gos_line_color[0];
    opts->view_scale.gos_line_color[1] = s->gv_view_scale.gos_line_color[1];
    opts->view_scale.gos_line_color[2] = s->gv_view_scale.gos_line_color[2];
    opts->view_scale.gos_text_color[0] = s->gv_view_scale.gos_text_color[0];
    opts->view_scale.gos_text_color[1] = s->gv_view_scale.gos_text_color[1];
    opts->view_scale.gos_text_color[2] = s->gv_view_scale.gos_text_color[2];
    opts->view_scale.gos_font_size     = s->gv_view_scale.gos_font_size;

    /* Frametime and framebuffer mode */
    opts->frametime = s->gv_frametime;
    opts->fb_mode   = s->gv_fb_mode;

    /* ADC */
    opts->adc.draw         = s->gv_adc.draw;
    opts->adc.dv_x         = s->gv_adc.dv_x;
    opts->adc.dv_y         = s->gv_adc.dv_y;
    opts->adc.dv_a1        = s->gv_adc.dv_a1;
    opts->adc.dv_a2        = s->gv_adc.dv_a2;
    opts->adc.dv_dist      = s->gv_adc.dv_dist;
    VMOVE(opts->adc.pos_model, s->gv_adc.pos_model);
    VMOVE(opts->adc.pos_view,  s->gv_adc.pos_view);
    VMOVE(opts->adc.pos_grid,  s->gv_adc.pos_grid);
    opts->adc.a1           = s->gv_adc.a1;
    opts->adc.a2           = s->gv_adc.a2;
    opts->adc.dst          = s->gv_adc.dst;
    opts->adc.anchor_pos   = s->gv_adc.anchor_pos;
    opts->adc.anchor_a1    = s->gv_adc.anchor_a1;
    opts->adc.anchor_a2    = s->gv_adc.anchor_a2;
    opts->adc.anchor_dst   = s->gv_adc.anchor_dst;
    VMOVE(opts->adc.anchor_pt_a1,  s->gv_adc.anchor_pt_a1);
    VMOVE(opts->adc.anchor_pt_a2,  s->gv_adc.anchor_pt_a2);
    VMOVE(opts->adc.anchor_pt_dst, s->gv_adc.anchor_pt_dst);
    opts->adc.line_color[0] = s->gv_adc.line_color[0];
    opts->adc.line_color[1] = s->gv_adc.line_color[1];
    opts->adc.line_color[2] = s->gv_adc.line_color[2];
    opts->adc.tick_color[0] = s->gv_adc.tick_color[0];
    opts->adc.tick_color[1] = s->gv_adc.tick_color[1];
    opts->adc.tick_color[2] = s->gv_adc.tick_color[2];
    opts->adc.line_width    = s->gv_adc.line_width;

    /* Rubber-band rectangle */
    opts->rect.active     = s->gv_rect.active;
    opts->rect.draw       = s->gv_rect.draw;
    opts->rect.line_width = s->gv_rect.line_width;
    opts->rect.line_style = s->gv_rect.line_style;
    opts->rect.pos[0]     = s->gv_rect.pos[0];
    opts->rect.pos[1]     = s->gv_rect.pos[1];
    opts->rect.dim[0]     = s->gv_rect.dim[0];
    opts->rect.dim[1]     = s->gv_rect.dim[1];
    opts->rect.x          = s->gv_rect.x;
    opts->rect.y          = s->gv_rect.y;
    opts->rect.width      = s->gv_rect.width;
    opts->rect.height     = s->gv_rect.height;
    opts->rect.bg[0]      = s->gv_rect.bg[0];
    opts->rect.bg[1]      = s->gv_rect.bg[1];
    opts->rect.bg[2]      = s->gv_rect.bg[2];
    opts->rect.color[0]   = s->gv_rect.color[0];
    opts->rect.color[1]   = s->gv_rect.color[1];
    opts->rect.color[2]   = s->gv_rect.color[2];
    opts->rect.cdim[0]    = s->gv_rect.cdim[0];
    opts->rect.cdim[1]    = s->gv_rect.cdim[1];
    opts->rect.aspect     = s->gv_rect.aspect;

    return 0;
}


int
bsg_hud_opts_to_bview_settings(const struct bsg_hud_opts *opts,
                                struct bview_settings *s)
{
    if (!opts || !s)
	return -1;

    /* Axes */
    _copy_bsg_axes_to_bv(&s->gv_model_axes, &opts->model_axes);
    _copy_bsg_axes_to_bv(&s->gv_view_axes,  &opts->view_axes);

    /* Grid */
    s->gv_grid.rc          = opts->grid.rc;
    s->gv_grid.draw        = opts->grid.draw;
    s->gv_grid.adaptive    = opts->grid.adaptive;
    s->gv_grid.snap        = opts->grid.snap;
    VMOVE(s->gv_grid.anchor, opts->grid.anchor);
    s->gv_grid.res_h       = opts->grid.res_h;
    s->gv_grid.res_v       = opts->grid.res_v;
    s->gv_grid.res_major_h = opts->grid.res_major_h;
    s->gv_grid.res_major_v = opts->grid.res_major_v;
    s->gv_grid.color[0]    = opts->grid.color[0];
    s->gv_grid.color[1]    = opts->grid.color[1];
    s->gv_grid.color[2]    = opts->grid.color[2];

    /* Center dot */
    s->gv_center_dot.gos_draw        = opts->center_dot.gos_draw;
    s->gv_center_dot.gos_line_color[0] = opts->center_dot.gos_line_color[0];
    s->gv_center_dot.gos_line_color[1] = opts->center_dot.gos_line_color[1];
    s->gv_center_dot.gos_line_color[2] = opts->center_dot.gos_line_color[2];
    s->gv_center_dot.gos_text_color[0] = opts->center_dot.gos_text_color[0];
    s->gv_center_dot.gos_text_color[1] = opts->center_dot.gos_text_color[1];
    s->gv_center_dot.gos_text_color[2] = opts->center_dot.gos_text_color[2];
    s->gv_center_dot.gos_font_size     = opts->center_dot.gos_font_size;

    /* View params */
    s->gv_view_params.draw        = opts->view_params.draw;
    s->gv_view_params.draw_size   = opts->view_params.draw_size;
    s->gv_view_params.draw_center = opts->view_params.draw_center;
    s->gv_view_params.draw_az     = opts->view_params.draw_az;
    s->gv_view_params.draw_el     = opts->view_params.draw_el;
    s->gv_view_params.draw_tw     = opts->view_params.draw_tw;
    s->gv_view_params.draw_fps    = opts->view_params.draw_fps;
    s->gv_view_params.color[0]    = opts->view_params.color[0];
    s->gv_view_params.color[1]    = opts->view_params.color[1];
    s->gv_view_params.color[2]    = opts->view_params.color[2];
    s->gv_view_params.font_size   = opts->view_params.font_size;

    /* View scale */
    s->gv_view_scale.gos_draw        = opts->view_scale.gos_draw;
    s->gv_view_scale.gos_line_color[0] = opts->view_scale.gos_line_color[0];
    s->gv_view_scale.gos_line_color[1] = opts->view_scale.gos_line_color[1];
    s->gv_view_scale.gos_line_color[2] = opts->view_scale.gos_line_color[2];
    s->gv_view_scale.gos_text_color[0] = opts->view_scale.gos_text_color[0];
    s->gv_view_scale.gos_text_color[1] = opts->view_scale.gos_text_color[1];
    s->gv_view_scale.gos_text_color[2] = opts->view_scale.gos_text_color[2];
    s->gv_view_scale.gos_font_size     = opts->view_scale.gos_font_size;

    /* Frametime and framebuffer mode */
    s->gv_frametime = opts->frametime;
    s->gv_fb_mode   = opts->fb_mode;

    /* ADC */
    s->gv_adc.draw         = opts->adc.draw;
    s->gv_adc.dv_x         = opts->adc.dv_x;
    s->gv_adc.dv_y         = opts->adc.dv_y;
    s->gv_adc.dv_a1        = opts->adc.dv_a1;
    s->gv_adc.dv_a2        = opts->adc.dv_a2;
    s->gv_adc.dv_dist      = opts->adc.dv_dist;
    VMOVE(s->gv_adc.pos_model, opts->adc.pos_model);
    VMOVE(s->gv_adc.pos_view,  opts->adc.pos_view);
    VMOVE(s->gv_adc.pos_grid,  opts->adc.pos_grid);
    s->gv_adc.a1           = opts->adc.a1;
    s->gv_adc.a2           = opts->adc.a2;
    s->gv_adc.dst          = opts->adc.dst;
    s->gv_adc.anchor_pos   = opts->adc.anchor_pos;
    s->gv_adc.anchor_a1    = opts->adc.anchor_a1;
    s->gv_adc.anchor_a2    = opts->adc.anchor_a2;
    s->gv_adc.anchor_dst   = opts->adc.anchor_dst;
    VMOVE(s->gv_adc.anchor_pt_a1,  opts->adc.anchor_pt_a1);
    VMOVE(s->gv_adc.anchor_pt_a2,  opts->adc.anchor_pt_a2);
    VMOVE(s->gv_adc.anchor_pt_dst, opts->adc.anchor_pt_dst);
    s->gv_adc.line_color[0] = opts->adc.line_color[0];
    s->gv_adc.line_color[1] = opts->adc.line_color[1];
    s->gv_adc.line_color[2] = opts->adc.line_color[2];
    s->gv_adc.tick_color[0] = opts->adc.tick_color[0];
    s->gv_adc.tick_color[1] = opts->adc.tick_color[1];
    s->gv_adc.tick_color[2] = opts->adc.tick_color[2];
    s->gv_adc.line_width    = opts->adc.line_width;

    /* Rubber-band rectangle */
    s->gv_rect.active     = opts->rect.active;
    s->gv_rect.draw       = opts->rect.draw;
    s->gv_rect.line_width = opts->rect.line_width;
    s->gv_rect.line_style = opts->rect.line_style;
    s->gv_rect.pos[0]     = opts->rect.pos[0];
    s->gv_rect.pos[1]     = opts->rect.pos[1];
    s->gv_rect.dim[0]     = opts->rect.dim[0];
    s->gv_rect.dim[1]     = opts->rect.dim[1];
    s->gv_rect.x          = opts->rect.x;
    s->gv_rect.y          = opts->rect.y;
    s->gv_rect.width      = opts->rect.width;
    s->gv_rect.height     = opts->rect.height;
    s->gv_rect.bg[0]      = opts->rect.bg[0];
    s->gv_rect.bg[1]      = opts->rect.bg[1];
    s->gv_rect.bg[2]      = opts->rect.bg[2];
    s->gv_rect.color[0]   = opts->rect.color[0];
    s->gv_rect.color[1]   = opts->rect.color[1];
    s->gv_rect.color[2]   = opts->rect.color[2];
    s->gv_rect.cdim[0]    = opts->rect.cdim[0];
    s->gv_rect.cdim[1]    = opts->rect.cdim[1];
    s->gv_rect.aspect     = opts->rect.aspect;

    return 0;
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
