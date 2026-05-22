/*              T E S T _ H U D . C
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
/** @file libbsg/tests/test_hud.c
 *
 * HUD/view-option slice (slice 4) unit tests.
 *
 * Tests:
 *  H1 - bsg_adc_state_init: safe defaults
 *  H2 - bsg_adc_model_to_view: updates dv_x/dv_y and pos_view
 *  H3 - bsg_adc_reset: restores center state
 *  H4 - bsg_grid_state_init: safe defaults
 *  H5 - bsg_rect_state_init: safe defaults
 *  H6 - bsg_params_state_init: safe defaults
 *  H7 - bsg_other_state_init: safe defaults
 *  H8 - bsg_axes_init: safe defaults
 *  H9 - bsg_hud_opts_init: all sub-structures initialized
 *  H10 - bsg_hud_opts_from_bview_settings round-trip
 *  H11 - bsg_hud_opts_to_bview_settings round-trip
 *  H12 - NULL safety for all public functions
 */

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "vmath.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/hud.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

#define HUD_NEAR_ZERO(v) (fabs((double)(v)) < 1e-10)


/* ------------------------------------------------------------------ */
/* Test H1: bsg_adc_state_init defaults                                 */
/* ------------------------------------------------------------------ */

static int
test_adc_init(void)
{
    struct bsg_adc_state adc;
    printf("=== Test H1: bsg_adc_state_init defaults ===\n");

    bsg_adc_state_init(&adc);

    if (adc.draw    != 0) FAIL("adc.draw default not 0");
    if (adc.dv_x   != 0) FAIL("adc.dv_x default not 0");
    if (adc.dv_y   != 0) FAIL("adc.dv_y default not 0");
    if (!HUD_NEAR_ZERO(adc.a1 - 45.0)) FAIL("adc.a1 default not 45");
    if (!HUD_NEAR_ZERO(adc.a2 - 45.0)) FAIL("adc.a2 default not 45");
    if (adc.anchor_pos != 0) FAIL("adc.anchor_pos default not 0");
    if (adc.line_width != 1) FAIL("adc.line_width default not 1");
    /* dst: (0 * INV_BV + 1.0) * M_SQRT1_2 = M_SQRT1_2 */
    if (!HUD_NEAR_ZERO(adc.dst - M_SQRT1_2)) FAIL("adc.dst default wrong");

    bsg_adc_state_init(NULL); /* must not crash */
    PASS("adc_init");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H2: bsg_adc_model_to_view                                       */
/* ------------------------------------------------------------------ */

static int
test_adc_model_to_view(void)
{
    struct bsg_adc_state adc;
    mat_t m2v;

    printf("=== Test H2: bsg_adc_model_to_view ===\n");
    bsg_adc_state_init(&adc);

    /* Use identity matrix — pos_model=(1,0,0) maps to pos_view=(1,0,0) */
    MAT_IDN(m2v);
    VSET(adc.pos_model, 1.0, 0.0, 0.0);

    bsg_adc_model_to_view(&adc, m2v, 2047.0);

    if (!HUD_NEAR_ZERO(adc.pos_view[X] - 1.0)) FAIL("pos_view[X] after identity");
    if (!HUD_NEAR_ZERO(adc.pos_view[Y] - 0.0)) FAIL("pos_view[Y] after identity");
    /* dv_x = pos_view[X] * amax = 1.0 * 2047 = 2047 */
    if (adc.dv_x != 2047) FAIL("dv_x after identity");
    if (adc.dv_y != 0)    FAIL("dv_y after identity");

    bsg_adc_model_to_view(NULL, m2v, 2047.0); /* must not crash */
    PASS("adc_model_to_view");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H3: bsg_adc_reset                                               */
/* ------------------------------------------------------------------ */

static int
test_adc_reset(void)
{
    struct bsg_adc_state adc;
    mat_t v2m, m2v;

    printf("=== Test H3: bsg_adc_reset ===\n");
    bsg_adc_state_init(&adc);

    /* Set non-zero state, then reset */
    adc.dv_x = 500;
    adc.dv_y = -300;
    adc.a1   = 90.0;
    adc.a2   = 30.0;

    MAT_IDN(v2m);
    MAT_IDN(m2v);
    bsg_adc_reset(&adc, v2m, m2v);

    if (adc.dv_x != 0) FAIL("dv_x after reset");
    if (adc.dv_y != 0) FAIL("dv_y after reset");
    if (!HUD_NEAR_ZERO(adc.a1 - 45.0)) FAIL("a1 after reset");
    if (!HUD_NEAR_ZERO(adc.a2 - 45.0)) FAIL("a2 after reset");
    if (adc.anchor_pos  != 0) FAIL("anchor_pos after reset");
    if (adc.anchor_a1   != 0) FAIL("anchor_a1 after reset");
    if (adc.anchor_a2   != 0) FAIL("anchor_a2 after reset");
    if (adc.anchor_dst  != 0) FAIL("anchor_dst after reset");

    bsg_adc_reset(NULL, v2m, m2v); /* must not crash */
    PASS("adc_reset");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H4: bsg_grid_state_init defaults                                */
/* ------------------------------------------------------------------ */

static int
test_grid_init(void)
{
    struct bsg_grid_state grid;
    printf("=== Test H4: bsg_grid_state_init defaults ===\n");

    bsg_grid_state_init(&grid);

    if (grid.draw     != 0)   FAIL("grid.draw default not 0");
    if (grid.adaptive != 0)   FAIL("grid.adaptive default not 0");
    if (grid.snap     != 0)   FAIL("grid.snap default not 0");
    if (!HUD_NEAR_ZERO(grid.res_h - 1.0)) FAIL("grid.res_h default not 1.0");
    if (!HUD_NEAR_ZERO(grid.res_v - 1.0)) FAIL("grid.res_v default not 1.0");
    if (grid.res_major_h != 5) FAIL("grid.res_major_h default not 5");
    if (grid.res_major_v != 5) FAIL("grid.res_major_v default not 5");

    bsg_grid_state_init(NULL); /* must not crash */
    PASS("grid_init");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H5: bsg_rect_state_init defaults                                */
/* ------------------------------------------------------------------ */

static int
test_rect_init(void)
{
    struct bsg_rect_state rect;
    printf("=== Test H5: bsg_rect_state_init defaults ===\n");

    bsg_rect_state_init(&rect);

    if (rect.active     != 0) FAIL("rect.active default not 0");
    if (rect.draw       != 0) FAIL("rect.draw default not 0");
    if (rect.line_width != 1) FAIL("rect.line_width default not 1");
    if (!HUD_NEAR_ZERO(rect.aspect - 1.0)) FAIL("rect.aspect default not 1.0");
    if (rect.color[0] != 255 || rect.color[1] != 255 || rect.color[2] != 255)
	FAIL("rect.color default not white");

    bsg_rect_state_init(NULL); /* must not crash */
    PASS("rect_init");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H6: bsg_params_state_init defaults                              */
/* ------------------------------------------------------------------ */

static int
test_params_init(void)
{
    struct bsg_params_state params;
    printf("=== Test H6: bsg_params_state_init defaults ===\n");

    bsg_params_state_init(&params);

    if (params.draw       != 0)  FAIL("params.draw default not 0");
    if (params.draw_size  != 0)  FAIL("params.draw_size default not 0");
    if (params.draw_fps   != 0)  FAIL("params.draw_fps default not 0");
    if (params.font_size  != 10) FAIL("params.font_size default not 10");
    if (params.color[0] != 255 || params.color[1] != 255 || params.color[2] != 255)
	FAIL("params.color default not white");

    bsg_params_state_init(NULL); /* must not crash */
    PASS("params_init");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H7: bsg_other_state_init defaults                               */
/* ------------------------------------------------------------------ */

static int
test_other_init(void)
{
    struct bsg_other_state other;
    printf("=== Test H7: bsg_other_state_init defaults ===\n");

    bsg_other_state_init(&other);

    if (other.gos_draw      != 0)  FAIL("other.gos_draw default not 0");
    if (other.gos_font_size != 10) FAIL("other.gos_font_size default not 10");
    if (other.gos_text_color[0] != 255) FAIL("other.gos_text_color[0] not 255");
    if (other.gos_line_color[0] != 255) FAIL("other.gos_line_color[0] not 255");

    bsg_other_state_init(NULL); /* must not crash */
    PASS("other_init");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H8: bsg_axes_init defaults                                      */
/* ------------------------------------------------------------------ */

static int
test_axes_init(void)
{
    struct bsg_axes axes;
    printf("=== Test H8: bsg_axes_init defaults ===\n");

    bsg_axes_init(&axes);

    if (axes.draw       != 0)   FAIL("axes.draw default not 0");
    if (axes.line_width != 1)   FAIL("axes.line_width default not 1");
    if (!HUD_NEAR_ZERO(axes.axes_size  - 0.2)) FAIL("axes.axes_size default not 0.2");
    if (!HUD_NEAR_ZERO(axes.tick_interval - 1.0)) FAIL("axes.tick_interval default not 1.0");
    if (axes.ticks_per_major != 5) FAIL("axes.ticks_per_major default not 5");
    if (axes.tick_threshold  != 8) FAIL("axes.tick_threshold default not 8");
    if (axes.axes_color[0] != 255 || axes.axes_color[1] != 255 || axes.axes_color[2] != 255)
	FAIL("axes.axes_color default not white");

    bsg_axes_init(NULL); /* must not crash */
    PASS("axes_init");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H9: bsg_hud_opts_init initializes all sub-structures            */
/* ------------------------------------------------------------------ */

static int
test_hud_opts_init(void)
{
    struct bsg_hud_opts opts;
    printf("=== Test H9: bsg_hud_opts_init ===\n");

    bsg_hud_opts_init(&opts);

    /* Check a representative field from each sub-structure */
    if (opts.adc.draw      != 0)  FAIL("adc.draw not 0 after hud init");
    if (opts.grid.draw     != 0)  FAIL("grid.draw not 0 after hud init");
    if (opts.rect.draw     != 0)  FAIL("rect.draw not 0 after hud init");
    if (opts.view_params.draw != 0) FAIL("view_params.draw not 0 after hud init");
    if (opts.center_dot.gos_draw != 0) FAIL("center_dot.gos_draw not 0 after hud init");
    if (opts.view_scale.gos_draw != 0) FAIL("view_scale.gos_draw not 0 after hud init");
    if (opts.model_axes.draw != 0) FAIL("model_axes.draw not 0 after hud init");
    if (opts.view_axes.draw  != 0) FAIL("view_axes.draw not 0 after hud init");
    if (opts.fb_mode   != 0)  FAIL("fb_mode not 0 after hud init");
    if (!HUD_NEAR_ZERO(opts.frametime)) FAIL("frametime not 0.0 after hud init");

    bsg_hud_opts_init(NULL); /* must not crash */
    PASS("hud_opts_init");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H10: bsg_hud_opts_from_bview_settings round-trip               */
/* ------------------------------------------------------------------ */

static int
test_from_bview_settings(void)
{
    struct bview *v;
    struct bsg_hud_opts opts;
    int rc;

    printf("=== Test H10: bsg_hud_opts_from_bview_settings ===\n");

    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);

    /* Set distinguishable values in bview_settings */
    struct bview_settings *s = &v->gv_ls;
    s->gv_adc.draw    = 1;
    s->gv_adc.a1      = 33.0;
    s->gv_adc.a2      = 66.0;
    s->gv_adc.line_color[0] = 200;
    s->gv_adc.line_color[1] = 100;
    s->gv_adc.line_color[2] = 50;

    s->gv_grid.draw       = 1;
    s->gv_grid.snap       = 1;
    s->gv_grid.res_h      = 3.5;
    s->gv_grid.res_v      = 7.0;
    s->gv_grid.res_major_h = 4;
    s->gv_grid.color[0]   = 128;
    s->gv_grid.color[1]   = 64;
    s->gv_grid.color[2]   = 32;

    s->gv_rect.draw       = 1;
    s->gv_rect.active     = 1;
    s->gv_rect.x          = 0.5;
    s->gv_rect.y          = -0.25;
    s->gv_rect.width      = 0.3;
    s->gv_rect.height     = 0.4;
    s->gv_rect.color[0]   = 255;
    s->gv_rect.color[1]   = 0;
    s->gv_rect.color[2]   = 0;

    s->gv_view_params.draw     = 1;
    s->gv_view_params.draw_fps = 1;
    s->gv_view_params.font_size = 14;

    s->gv_center_dot.gos_draw = 1;
    s->gv_view_scale.gos_draw = 1;
    s->gv_view_scale.gos_font_size = 12;

    s->gv_model_axes.draw = 1;
    s->gv_model_axes.axes_size = 0.5;
    s->gv_model_axes.triple_color = 1;

    s->gv_view_axes.draw   = 1;
    s->gv_view_axes.label_flag = 1;

    s->gv_frametime = 0.016;
    s->gv_fb_mode   = 1;

    bsg_hud_opts_init(&opts);
    rc = bsg_hud_opts_from_bview_settings(&opts, s);
    if (rc != 0) FAIL("from_bview_settings returned non-zero");

    /* ADC */
    if (opts.adc.draw  != 1)   FAIL("adc.draw not transferred");
    if (!HUD_NEAR_ZERO(opts.adc.a1 - 33.0)) FAIL("adc.a1 not transferred");
    if (!HUD_NEAR_ZERO(opts.adc.a2 - 66.0)) FAIL("adc.a2 not transferred");
    if (opts.adc.line_color[0] != 200) FAIL("adc.line_color[0] not transferred");
    if (opts.adc.line_color[1] != 100) FAIL("adc.line_color[1] not transferred");
    if (opts.adc.line_color[2] != 50)  FAIL("adc.line_color[2] not transferred");

    /* Grid */
    if (opts.grid.draw     != 1)   FAIL("grid.draw not transferred");
    if (opts.grid.snap     != 1)   FAIL("grid.snap not transferred");
    if (!HUD_NEAR_ZERO(opts.grid.res_h - 3.5)) FAIL("grid.res_h not transferred");
    if (!HUD_NEAR_ZERO(opts.grid.res_v - 7.0)) FAIL("grid.res_v not transferred");
    if (opts.grid.res_major_h != 4) FAIL("grid.res_major_h not transferred");
    if (opts.grid.color[0] != 128)  FAIL("grid.color[0] not transferred");

    /* Rect */
    if (opts.rect.draw   != 1)   FAIL("rect.draw not transferred");
    if (opts.rect.active != 1)   FAIL("rect.active not transferred");
    if (!HUD_NEAR_ZERO(opts.rect.x - 0.5))    FAIL("rect.x not transferred");
    if (!HUD_NEAR_ZERO(opts.rect.y - (-0.25))) FAIL("rect.y not transferred");
    if (!HUD_NEAR_ZERO(opts.rect.width - 0.3)) FAIL("rect.width not transferred");
    if (opts.rect.color[0] != 255) FAIL("rect.color[0] not transferred");
    if (opts.rect.color[1] != 0)   FAIL("rect.color[1] not transferred");

    /* Params */
    if (opts.view_params.draw     != 1)  FAIL("view_params.draw not transferred");
    if (opts.view_params.draw_fps != 1)  FAIL("view_params.draw_fps not transferred");
    if (opts.view_params.font_size != 14) FAIL("view_params.font_size not transferred");

    /* Center dot / view scale */
    if (opts.center_dot.gos_draw != 1) FAIL("center_dot.gos_draw not transferred");
    if (opts.view_scale.gos_draw != 1) FAIL("view_scale.gos_draw not transferred");
    if (opts.view_scale.gos_font_size != 12) FAIL("view_scale.gos_font_size not transferred");

    /* Axes */
    if (opts.model_axes.draw != 1)      FAIL("model_axes.draw not transferred");
    if (!HUD_NEAR_ZERO(opts.model_axes.axes_size - 0.5)) FAIL("model_axes.axes_size not transferred");
    if (opts.model_axes.triple_color != 1) FAIL("model_axes.triple_color not transferred");
    if (opts.view_axes.draw  != 1)      FAIL("view_axes.draw not transferred");
    if (opts.view_axes.label_flag != 1) FAIL("view_axes.label_flag not transferred");

    /* Frametime and fb_mode */
    if (!HUD_NEAR_ZERO(opts.frametime - 0.016)) FAIL("frametime not transferred");
    if (opts.fb_mode != 1) FAIL("fb_mode not transferred");

    bv_free(v);
    bu_free(v, "test_view");
    PASS("from_bview_settings");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H11: bsg_hud_opts_to_bview_settings round-trip                  */
/* ------------------------------------------------------------------ */

static int
test_to_bview_settings(void)
{
    struct bview *v;
    struct bsg_hud_opts opts;
    int rc;

    printf("=== Test H11: bsg_hud_opts_to_bview_settings ===\n");

    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);

    /* Populate BSG opts with distinguishable values */
    bsg_hud_opts_init(&opts);

    opts.adc.draw  = 1;
    opts.adc.a1    = 10.0;
    opts.adc.a2    = 80.0;
    opts.adc.dv_x  = 512;
    opts.adc.line_color[0] = 77;
    opts.adc.line_color[1] = 88;
    opts.adc.line_color[2] = 99;

    opts.grid.draw     = 1;
    opts.grid.adaptive = 1;
    opts.grid.res_h    = 2.0;
    opts.grid.color[0] = 55;

    opts.rect.draw   = 1;
    opts.rect.active = 1;
    opts.rect.x      = 0.1;
    opts.rect.y      = 0.2;
    opts.rect.width  = 0.5;
    opts.rect.height = 0.6;
    opts.rect.color[0] = 11;
    opts.rect.color[1] = 22;
    opts.rect.color[2] = 33;

    opts.view_params.draw     = 1;
    opts.view_params.draw_center = 1;
    opts.view_params.font_size   = 16;

    opts.model_axes.draw       = 1;
    opts.model_axes.axes_size  = 0.3;
    opts.model_axes.label_flag = 1;

    opts.view_axes.draw        = 1;
    opts.view_axes.tick_enabled = 1;

    opts.center_dot.gos_draw = 1;
    opts.view_scale.gos_draw = 0;
    opts.frametime = 0.033;
    opts.fb_mode   = 2;

    struct bview_settings *s = &v->gv_ls;
    rc = bsg_hud_opts_to_bview_settings(&opts, s);
    if (rc != 0) FAIL("to_bview_settings returned non-zero");

    /* ADC */
    if (s->gv_adc.draw  != 1)   FAIL("gv_adc.draw not written back");
    if (!HUD_NEAR_ZERO(s->gv_adc.a1 - 10.0)) FAIL("gv_adc.a1 not written back");
    if (!HUD_NEAR_ZERO(s->gv_adc.a2 - 80.0)) FAIL("gv_adc.a2 not written back");
    if (s->gv_adc.dv_x  != 512) FAIL("gv_adc.dv_x not written back");
    if (s->gv_adc.line_color[0] != 77) FAIL("gv_adc.line_color[0] not written back");
    if (s->gv_adc.line_color[1] != 88) FAIL("gv_adc.line_color[1] not written back");
    if (s->gv_adc.line_color[2] != 99) FAIL("gv_adc.line_color[2] not written back");

    /* Grid */
    if (s->gv_grid.draw     != 1) FAIL("gv_grid.draw not written back");
    if (s->gv_grid.adaptive != 1) FAIL("gv_grid.adaptive not written back");
    if (!HUD_NEAR_ZERO(s->gv_grid.res_h - 2.0)) FAIL("gv_grid.res_h not written back");
    if (s->gv_grid.color[0] != 55) FAIL("gv_grid.color[0] not written back");

    /* Rect */
    if (s->gv_rect.draw   != 1) FAIL("gv_rect.draw not written back");
    if (s->gv_rect.active != 1) FAIL("gv_rect.active not written back");
    if (!HUD_NEAR_ZERO(s->gv_rect.x - 0.1)) FAIL("gv_rect.x not written back");
    if (!HUD_NEAR_ZERO(s->gv_rect.y - 0.2)) FAIL("gv_rect.y not written back");
    if (!HUD_NEAR_ZERO(s->gv_rect.width  - 0.5)) FAIL("gv_rect.width not written back");
    if (!HUD_NEAR_ZERO(s->gv_rect.height - 0.6)) FAIL("gv_rect.height not written back");
    if (s->gv_rect.color[0] != 11) FAIL("gv_rect.color[0] not written back");
    if (s->gv_rect.color[1] != 22) FAIL("gv_rect.color[1] not written back");
    if (s->gv_rect.color[2] != 33) FAIL("gv_rect.color[2] not written back");

    /* Params */
    if (s->gv_view_params.draw        != 1)  FAIL("gv_view_params.draw not written back");
    if (s->gv_view_params.draw_center != 1)  FAIL("gv_view_params.draw_center not written back");
    if (s->gv_view_params.font_size   != 16) FAIL("gv_view_params.font_size not written back");

    /* Axes */
    if (s->gv_model_axes.draw      != 1)   FAIL("gv_model_axes.draw not written back");
    if (!HUD_NEAR_ZERO(s->gv_model_axes.axes_size - 0.3)) FAIL("gv_model_axes.axes_size not written back");
    if (s->gv_model_axes.label_flag != 1)  FAIL("gv_model_axes.label_flag not written back");
    if (s->gv_view_axes.draw        != 1)   FAIL("gv_view_axes.draw not written back");
    if (s->gv_view_axes.tick_enabled != 1)  FAIL("gv_view_axes.tick_enabled not written back");

    /* Other */
    if (s->gv_center_dot.gos_draw != 1) FAIL("gv_center_dot.gos_draw not written back");
    if (s->gv_view_scale.gos_draw != 0) FAIL("gv_view_scale.gos_draw not written back");
    if (!HUD_NEAR_ZERO(s->gv_frametime - 0.033)) FAIL("gv_frametime not written back");
    if (s->gv_fb_mode != 2) FAIL("gv_fb_mode not written back");

    bv_free(v);
    bu_free(v, "test_view");
    PASS("to_bview_settings");
    return 0;
}


/* ------------------------------------------------------------------ */
/* Test H12: NULL safety for all public functions                       */
/* ------------------------------------------------------------------ */

static int
test_null_safety(void)
{
    struct bsg_adc_state adc;
    struct bsg_hud_opts opts;
    mat_t m;
    int rc;

    printf("=== Test H12: NULL safety ===\n");

    /* All init functions must not crash on NULL */
    bsg_adc_state_init(NULL);
    bsg_grid_state_init(NULL);
    bsg_rect_state_init(NULL);
    bsg_params_state_init(NULL);
    bsg_other_state_init(NULL);
    bsg_axes_init(NULL);
    bsg_hud_opts_init(NULL);

    /* ADC math: NULL adc */
    MAT_IDN(m);
    bsg_adc_state_init(&adc);
    bsg_adc_model_to_view(NULL, m, 2047.0);
    bsg_adc_grid_to_view(NULL, m, 2047.0);
    bsg_adc_view_to_grid(NULL, m);
    bsg_adc_reset(NULL, m, m);

    /* Bridge functions: NULL arguments */
    rc = bsg_hud_opts_from_bview_settings(NULL, NULL);
    if (rc != -1) FAIL("from_bview_settings(NULL,NULL) should return -1");
    bsg_hud_opts_init(&opts);
    rc = bsg_hud_opts_from_bview_settings(&opts, NULL);
    if (rc != -1) FAIL("from_bview_settings(opts,NULL) should return -1");
    rc = bsg_hud_opts_from_bview_settings(NULL, (const struct bview_settings *)&opts);
    if (rc != -1) FAIL("from_bview_settings(NULL,s) should return -1");

    rc = bsg_hud_opts_to_bview_settings(NULL, NULL);
    if (rc != -1) FAIL("to_bview_settings(NULL,NULL) should return -1");
    rc = bsg_hud_opts_to_bview_settings(&opts, NULL);
    if (rc != -1) FAIL("to_bview_settings(opts,NULL) should return -1");
    rc = bsg_hud_opts_to_bview_settings(NULL, (struct bview_settings *)&opts);
    if (rc != -1) FAIL("to_bview_settings(NULL,s) should return -1");

    PASS("null_safety");
    return 0;
}


/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    int failures = 0;

    bu_setprogname(argv[0]);
    (void)argc;

    printf("=== BSG HUD/view-option slice (slice 4) tests ===\n");

    failures += test_adc_init();
    failures += test_adc_model_to_view();
    failures += test_adc_reset();
    failures += test_grid_init();
    failures += test_rect_init();
    failures += test_params_init();
    failures += test_other_init();
    failures += test_axes_init();
    failures += test_hud_opts_init();
    failures += test_from_bview_settings();
    failures += test_to_bview_settings();
    failures += test_null_safety();

    if (failures == 0)
	printf("ALL TESTS PASSED\n");
    else
	printf("%d TEST(S) FAILED\n", failures);

    return failures ? 1 : 0;
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
