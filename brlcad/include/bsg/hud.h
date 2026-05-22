/*                         H U D . H
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
/** @addtogroup libbsg
 *
 * @brief
 * HUD/view-option slice (slice 4 of bv_scene_obj_migrate.txt):
 * BSG HUD and view-option state types replacing the legacy libbv
 * faceplate and axes types.
 *
 * This header provides BSG-owned replacements for the following legacy types
 * from @c bv/faceplate.h and @c bv/defines.h:
 *
 * | Legacy type                    | BSG replacement           |
 * | ------------------------------ | ------------------------- |
 * | @c struct bv_adc_state         | @c struct bsg_adc_state   |
 * | @c struct bv_grid_state        | @c struct bsg_grid_state  |
 * | @c struct bv_interactive_rect_state | @c struct bsg_rect_state |
 * | @c struct bv_params_state      | @c struct bsg_params_state |
 * | @c struct bv_other_state       | @c struct bsg_other_state |
 * | @c struct bv_axes              | @c struct bsg_axes        |
 *
 * The aggregate container @c struct bsg_hud_opts collects all HUD and
 * view-option state in one place, mirroring the HUD fields of
 * @c struct bview_settings.
 *
 * Bridge functions convert between @c bsg_hud_opts and legacy
 * @c bview_settings for incremental migration:
 * - @c bsg_hud_opts_from_bview_settings() populates BSG HUD state from
 *   legacy @c bview_settings.
 * - @c bsg_hud_opts_to_bview_settings() writes BSG HUD state back to
 *   @c bview_settings.
 *
 * The ADC math helpers @c bsg_adc_model_to_view(),
 * @c bsg_adc_grid_to_view(), @c bsg_adc_view_to_grid(), and
 * @c bsg_adc_reset() are the BSG replacements for the legacy @c adc_*
 * functions in @c bv/adc.h.
 */
/** @{ */
/* @file bsg/hud.h */

#ifndef BSG_HUD_H
#define BSG_HUD_H

#include "common.h"
#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

struct bview_settings; /* forward declaration for migration bridge */

/* ------------------------------------------------------------------ */
/* Angle-Distance Cursor (ADC) state                                   */
/* Replaces struct bv_adc_state from bv/faceplate.h                   */
/* ------------------------------------------------------------------ */

/**
 * BSG angle-distance cursor (ADC) HUD state.
 *
 * Replaces @c struct @c bv_adc_state.  The ADC is a 2-D overlay element that
 * displays angle and distance measurements in the view plane.  Position fields
 * use the same conventions as the legacy type (dv_* in BV integer space,
 * pos_* in floating-point model/view/grid space).
 */
struct bsg_adc_state {
    int     draw;           /**< @brief 1 = draw the ADC overlay */
    int     dv_x;           /**< @brief cursor X position in BV integer space */
    int     dv_y;           /**< @brief cursor Y position in BV integer space */
    int     dv_a1;          /**< @brief angle-1 in BV integer space */
    int     dv_a2;          /**< @brief angle-2 in BV integer space */
    int     dv_dist;        /**< @brief distance tick in BV integer space */
    fastf_t pos_model[3];   /**< @brief cursor position in model space */
    fastf_t pos_view[3];    /**< @brief cursor position in view space */
    fastf_t pos_grid[3];    /**< @brief cursor position in grid space */
    fastf_t a1;             /**< @brief angle 1 in degrees */
    fastf_t a2;             /**< @brief angle 2 in degrees */
    fastf_t dst;            /**< @brief distance tick value */
    int     anchor_pos;     /**< @brief 1 = cursor position is anchored */
    int     anchor_a1;      /**< @brief 1 = angle-1 is anchored */
    int     anchor_a2;      /**< @brief 1 = angle-2 is anchored */
    int     anchor_dst;     /**< @brief 1 = distance is anchored */
    fastf_t anchor_pt_a1[3];  /**< @brief anchor point for angle-1 */
    fastf_t anchor_pt_a2[3];  /**< @brief anchor point for angle-2 */
    fastf_t anchor_pt_dst[3]; /**< @brief anchor point for distance */
    int     line_color[3];  /**< @brief RGB line color */
    int     tick_color[3];  /**< @brief RGB tick color */
    int     line_width;     /**< @brief line width in pixels */
};

/**
 * Initialize @p adc to safe default values (centered, not drawn).
 * No-op if @p adc is NULL.
 */
BSG_EXPORT extern void
bsg_adc_state_init(struct bsg_adc_state *adc);

/**
 * Update @p adc view-space position from model-space position.
 *
 * Equivalent to @c adc_model_to_adc_view() in @c bv/adc.h.
 * @p amax is the BV half-range (typically @c BV_MAX = 2047).
 */
BSG_EXPORT extern void
bsg_adc_model_to_view(struct bsg_adc_state *adc, mat_t model2view,
                      fastf_t amax);

/**
 * Update @p adc view-space position from grid-space offset.
 *
 * Equivalent to @c adc_grid_to_adc_view() in @c bv/adc.h.
 */
BSG_EXPORT extern void
bsg_adc_grid_to_view(struct bsg_adc_state *adc, mat_t model2view,
                     fastf_t amax);

/**
 * Update @p adc grid-space position from view-space position.
 *
 * Equivalent to @c adc_view_to_adc_grid() in @c bv/adc.h.
 */
BSG_EXPORT extern void
bsg_adc_view_to_grid(struct bsg_adc_state *adc, mat_t model2view);

/**
 * Reset @p adc to its default centered state.
 *
 * Equivalent to @c adc_reset() in @c bv/adc.h.
 */
BSG_EXPORT extern void
bsg_adc_reset(struct bsg_adc_state *adc, mat_t view2model, mat_t model2view);


/* ------------------------------------------------------------------ */
/* Grid state                                                           */
/* Replaces struct bv_grid_state from bv/faceplate.h                  */
/* ------------------------------------------------------------------ */

/**
 * BSG grid and snap-grid HUD state.
 *
 * Replaces @c struct @c bv_grid_state.
 */
struct bsg_grid_state {
    int     rc;             /**< @brief reference count (shared settings) */
    int     draw;           /**< @brief 1 = draw the grid */
    int     adaptive;       /**< @brief 1 = adapt grid spacing to view size */
    int     snap;           /**< @brief 1 = snap cursor to grid */
    fastf_t anchor[3];      /**< @brief grid anchor point in model space */
    fastf_t res_h;          /**< @brief grid resolution in horizontal direction */
    fastf_t res_v;          /**< @brief grid resolution in vertical direction */
    int     res_major_h;    /**< @brief major grid line interval (horizontal) */
    int     res_major_v;    /**< @brief major grid line interval (vertical) */
    int     color[3];       /**< @brief RGB grid line color */
};

/**
 * Initialize @p grid to safe defaults (not drawn, not adaptive, 1-unit resolution).
 * No-op if @p grid is NULL.
 */
BSG_EXPORT extern void
bsg_grid_state_init(struct bsg_grid_state *grid);


/* ------------------------------------------------------------------ */
/* Rubber-band rectangle state                                          */
/* Replaces struct bv_interactive_rect_state from bv/faceplate.h      */
/* ------------------------------------------------------------------ */

/**
 * BSG rubber-band / selection-rectangle HUD state.
 *
 * Replaces @c struct @c bv_interactive_rect_state.
 */
struct bsg_rect_state {
    int     active;         /**< @brief 1 = actively drawing a rectangle */
    int     draw;           /**< @brief 1 = draw the rubber-band rectangle */
    int     line_width;     /**< @brief line width in pixels */
    int     line_style;     /**< @brief 0 = solid, 1 = dashed */
    int     pos[2];         /**< @brief corner position in image (pixel) coordinates */
    int     dim[2];         /**< @brief rectangle dimensions in image coordinates */
    fastf_t x;              /**< @brief corner X in normalized view coords (+-1) */
    fastf_t y;              /**< @brief corner Y in normalized view coords (+-1) */
    fastf_t width;          /**< @brief width in normalized view coords */
    fastf_t height;         /**< @brief height in normalized view coords */
    int     bg[3];          /**< @brief RGB background color */
    int     color[3];       /**< @brief RGB rectangle border color */
    int     cdim[2];        /**< @brief canvas dimensions in pixels */
    fastf_t aspect;         /**< @brief canvas width/height aspect ratio */
};

/**
 * Initialize @p rect to safe defaults (not active, not drawn, white border).
 * No-op if @p rect is NULL.
 */
BSG_EXPORT extern void
bsg_rect_state_init(struct bsg_rect_state *rect);


/* ------------------------------------------------------------------ */
/* Faceplate view-parameters state                                      */
/* Replaces struct bv_params_state from bv/faceplate.h                */
/* ------------------------------------------------------------------ */

/**
 * BSG faceplate view-parameters display state.
 *
 * Replaces @c struct @c bv_params_state.  Controls which view-parameter
 * values are printed on the faceplate HUD.
 */
struct bsg_params_state {
    int draw;           /**< @brief 1 = draw the params overlay */
    int draw_size;      /**< @brief 1 = show view size */
    int draw_center;    /**< @brief 1 = show view center X,Y,Z */
    int draw_az;        /**< @brief 1 = show azimuth angle */
    int draw_el;        /**< @brief 1 = show elevation angle */
    int draw_tw;        /**< @brief 1 = show twist angle */
    int draw_fps;       /**< @brief 1 = show frames per second */
    int color[3];       /**< @brief RGB text color */
    int font_size;      /**< @brief text font size in points */
};

/**
 * Initialize @p params to safe defaults (not drawn, white text).
 * No-op if @p params is NULL.
 */
BSG_EXPORT extern void
bsg_params_state_init(struct bsg_params_state *params);


/* ------------------------------------------------------------------ */
/* Generic on/off HUD element state (center dot, view scale label)     */
/* Replaces struct bv_other_state from bv/faceplate.h                 */
/* ------------------------------------------------------------------ */

/**
 * BSG generic on/off HUD element state (center dot, view scale label).
 *
 * Replaces @c struct @c bv_other_state.
 */
struct bsg_other_state {
    int gos_draw;           /**< @brief 1 = draw this element */
    int gos_line_color[3];  /**< @brief RGB line/border color */
    int gos_text_color[3];  /**< @brief RGB text color */
    int gos_font_size;      /**< @brief text font size in points */
};

/**
 * Initialize @p other to safe defaults (not drawn, white text/lines).
 * No-op if @p other is NULL.
 */
BSG_EXPORT extern void
bsg_other_state_init(struct bsg_other_state *other);


/* ------------------------------------------------------------------ */
/* Axes state (model axes, view axes, data axes)                       */
/* Replaces struct bv_axes from bv/defines.h                          */
/* ------------------------------------------------------------------ */

/**
 * BSG axes HUD/overlay state.
 *
 * Replaces @c struct @c bv_axes from @c bv/defines.h.  This container holds
 * state for both the simple data-axes overlay and the more elaborate
 * faceplate HUD axes display (which is a superset of the data-axes fields).
 */
struct bsg_axes {
    int     draw;                   /**< @brief 1 = draw the axes */
    point_t axes_pos;               /**< @brief axes origin in model coordinates */
    fastf_t axes_size;              /**< @brief axes size in view coords (HUD) */
    int     line_width;             /**< @brief line width in pixels */
    int     axes_color[3];          /**< @brief RGB axes color */

    /* Faceplate HUD extended fields (unused for simple data-axes) */
    int     pos_only;               /**< @brief 1 = draw position indicator only */
    int     label_flag;             /**< @brief 1 = draw X/Y/Z axis labels */
    int     label_color[3];         /**< @brief RGB label color */
    int     triple_color;           /**< @brief 1 = use separate R/G/B per axis */
    int     tick_enabled;           /**< @brief 1 = draw tick marks */
    int     tick_length;            /**< @brief tick length in pixels */
    int     tick_major_length;      /**< @brief major tick length in pixels */
    fastf_t tick_interval;          /**< @brief tick spacing in mm */
    int     ticks_per_major;        /**< @brief minor ticks between major ticks */
    int     tick_threshold;         /**< @brief min pixel spacing before ticks are hidden */
    int     tick_color[3];          /**< @brief RGB minor tick color */
    int     tick_major_color[3];    /**< @brief RGB major tick color */
};

/**
 * Initialize @p axes to safe defaults (not drawn, white, no ticks).
 * No-op if @p axes is NULL.
 */
BSG_EXPORT extern void
bsg_axes_init(struct bsg_axes *axes);


/* ------------------------------------------------------------------ */
/* Aggregate HUD options container                                     */
/* Replaces the HUD/faceplate fields of struct bview_settings          */
/* ------------------------------------------------------------------ */

/**
 * Aggregate BSG HUD and view-option state.
 *
 * Collects all HUD element state in one container, replacing the HUD/faceplate
 * fields of @c struct @c bview_settings.  Intended to be embedded in or
 * pointed to by a BSG view/camera object once @c struct @c bview is replaced.
 *
 * Field correspondence with @c bview_settings:
 *
 * | @c bsg_hud_opts field | @c bview_settings field            |
 * | --------------------- | ---------------------------------- |
 * | @c model_axes         | @c gv_model_axes                   |
 * | @c view_axes          | @c gv_view_axes                    |
 * | @c grid               | @c gv_grid                         |
 * | @c center_dot         | @c gv_center_dot                   |
 * | @c view_params        | @c gv_view_params                  |
 * | @c view_scale         | @c gv_view_scale                   |
 * | @c frametime          | @c gv_frametime                    |
 * | @c fb_mode            | @c gv_fb_mode                      |
 * | @c adc                | @c gv_adc                          |
 * | @c rect               | @c gv_rect                         |
 */
struct bsg_hud_opts {
    struct bsg_axes        model_axes;  /**< @brief model-space axes overlay */
    struct bsg_axes        view_axes;   /**< @brief view-space (faceplate) axes */
    struct bsg_grid_state  grid;        /**< @brief grid display and snap options */
    struct bsg_other_state center_dot;  /**< @brief center dot display state */
    struct bsg_params_state view_params; /**< @brief faceplate view-parameter text */
    struct bsg_other_state view_scale;  /**< @brief view scale label display state */
    double                 frametime;   /**< @brief last frame time in seconds */
    int                    fb_mode;     /**< @brief framebuffer mode: 0=off, 1=overlay, 2=underlay */
    struct bsg_adc_state   adc;         /**< @brief angle-distance cursor state */
    struct bsg_rect_state  rect;        /**< @brief rubber-band rectangle state */
};

/**
 * Initialize @p opts to safe defaults.
 * No-op if @p opts is NULL.
 */
BSG_EXPORT extern void
bsg_hud_opts_init(struct bsg_hud_opts *opts);

/**
 * Populate @p opts from the HUD/faceplate fields of @p s.
 *
 * Reads the HUD and view-option fields of @p s and writes corresponding BSG
 * state into @p opts.  Returns 0 on success, -1 if either argument is NULL.
 *
 * This is a migration bridge: once @c bview_settings is removed, callers
 * that currently read @c bview_settings::gv_adc, @c gv_grid, @c gv_rect, etc.
 * will read from @c bsg_hud_opts directly.
 */
BSG_EXPORT extern int
bsg_hud_opts_from_bview_settings(struct bsg_hud_opts *opts,
				 const struct bview_settings *s);

/**
 * Write @p opts HUD state back into @p s.
 *
 * This is the reverse of @c bsg_hud_opts_from_bview_settings() and is
 * provided as a migration bridge for callers that still write through
 * @c bview_settings.  Returns 0 on success, -1 if either argument is NULL.
 */
BSG_EXPORT extern int
bsg_hud_opts_to_bview_settings(const struct bsg_hud_opts *opts,
				struct bview_settings *s);

__END_DECLS

#endif /* BSG_HUD_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
