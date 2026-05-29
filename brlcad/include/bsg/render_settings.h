/*             R E N D E R _ S E T T I N G S . H
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
 * Render settings — per-view rendering policy (Phase D5).
 *
 * A `bsg_render_settings` collects the draw-mode, hidden-line, z-clip,
 * transparency, LoD, framebuffer, and HUD enablement knobs that control
 * how `bsg_render_request_execute` behaves.  The struct is attached to
 * a `bsg_view` (or to a `bsg_view_set` for shared-settings layouts) and
 * is consumed from the render request rather than being read from scattered
 * `bsg_view_settings` fields during traversal.
 *
 * Migration path
 * --------------
 * The field values in `bsg_render_settings` mirror the corresponding
 * `bsg_view_settings` members that are currently read directly inside
 * `dm_draw_objs` and `_render_collect`.  As consumers migrate:
 *   1. Build a `bsg_render_settings` from the view's `gv_s` via
 *      `bsg_render_settings_from_view`.
 *   2. Attach it to the `bsg_render_request` via
 *      `bsg_render_request_set_settings`.
 *   3. Remove the direct `gv_s->*` reads from traversal code.
 *
 * The struct is intentionally plain-old-data so it can be cheaply
 * copied for e.g. snapshot/restore.
 */
/** @{ */
/* @file bsg/render_settings.h */

#ifndef BSG_RENDER_SETTINGS_H
#define BSG_RENDER_SETTINGS_H

#include "common.h"
#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/* -----------------------------------------------------------------------
 * Transparency policy
 * ----------------------------------------------------------------------- */

/** How transparent items are blended. */
typedef enum bsg_transparency_policy {
    /** No transparency: all items rendered opaque. */
    BSG_TRANSPARENCY_NONE     = 0,
    /** Depth-sorted back-to-front alpha blending. */
    BSG_TRANSPARENCY_SORTED   = 1,
    /** Order-independent transparency (if the backend supports it). */
    BSG_TRANSPARENCY_OIT      = 2
} bsg_transparency_policy;


/* -----------------------------------------------------------------------
 * LoD policy
 * ----------------------------------------------------------------------- */

/** How level-of-detail is selected during traversal. */
typedef enum bsg_lod_policy {
    /** LoD selection disabled: always render the highest-detail level (0). */
    BSG_LOD_OFF              = 0,
    /** Automatic LoD selection based on screen-space size estimate. */
    BSG_LOD_AUTO             = 1,
    /** Force a specific LoD level for all nodes (level stored separately). */
    BSG_LOD_FORCE_LEVEL      = 2
} bsg_lod_policy;


/* -----------------------------------------------------------------------
 * Framebuffer mode
 * ----------------------------------------------------------------------- */

/** How a linked framebuffer is composited with the scene. */
typedef enum bsg_framebuffer_mode {
    /** Framebuffer display disabled. */
    BSG_FB_OFF               = 0,
    /** Framebuffer drawn as an overlay on top of the 3-D scene. */
    BSG_FB_OVERLAY           = 1,
    /** Framebuffer drawn as an underlay beneath the 3-D scene. */
    BSG_FB_UNDERLAY          = 2
} bsg_framebuffer_mode;


/* -----------------------------------------------------------------------
 * Render settings struct
 * ----------------------------------------------------------------------- */

/**
 * Rendering policy settings for a BSG render request.
 *
 * Allocate with bsg_render_settings_create(); release with
 * bsg_render_settings_destroy().  Alternatively, fill in a stack-allocated
 * struct with bsg_render_settings_init_defaults() and populate from a view
 * with bsg_render_settings_from_view().
 */
struct bsg_render_settings {

    /* ----- Draw mode --------------------------------------------------- */

    /**
     * Default display mode for shapes that have no per-node s_os override.
     *   0 = wireframe
     *   1 = shaded BoT/polysolid only (boolean NOT evaluated)
     *   2 = shaded (boolean NOT evaluated)
     *   3 = shaded (boolean evaluated)
     *   4 = hidden-line
     */
    int             draw_mode;

    /* ----- Z-clip ------------------------------------------------------ */

    /** Non-zero enables Z-plane clipping during traversal. */
    int             zclip;

    /* ----- Transparency ------------------------------------------------- */

    /** Transparency rendering policy (see bsg_transparency_policy). */
    bsg_transparency_policy  transparency_policy;

    /* ----- LoD ---------------------------------------------------------- */

    /** Level-of-detail policy (see bsg_lod_policy). */
    bsg_lod_policy  lod_policy;

    /**
     * Forced LoD level when lod_policy == BSG_LOD_FORCE_LEVEL.
     * Ignored for BSG_LOD_OFF and BSG_LOD_AUTO.
     */
    int             lod_forced_level;

    /**
     * LoD scale factor (multiplies the view-space size estimate used by
     * BSG_LOD_AUTO).  1.0 = neutral.  Values > 1 favour coarser levels;
     * values < 1 favour finer levels.
     */
    fastf_t         lod_scale;

    /* ----- Framebuffer -------------------------------------------------- */

    /** Framebuffer compositing mode (see bsg_framebuffer_mode). */
    bsg_framebuffer_mode  fb_mode;

    /* ----- HUD ---------------------------------------------------------- */

    /** Non-zero enables the HUD render pass after the 3-D scene. */
    int             hud_enabled;

    /**
     * Non-zero enables the view-scale HUD feature specifically.
     * Respected only when hud_enabled is non-zero.
     */
    int             hud_view_scale;

    /**
     * Non-zero enables the view-parameters HUD feature.
     * Respected only when hud_enabled is non-zero.
     */
    int             hud_view_params;

    /* ----- Adaptive plotting ------------------------------------------- */

    /**
     * Non-zero enables mesh (BoT) adaptive plotting: wireframe density
     * is adjusted to match gv_scale / view window dimensions.
     */
    int             adaptive_plot_mesh;

    /**
     * Non-zero enables CSG adaptive plotting.
     */
    int             adaptive_plot_csg;

    /**
     * BoT threshold: meshes with more than this many triangles switch to
     * the LoD mesh path.  0 disables the threshold.
     */
    size_t          bot_threshold;

    /**
     * Curve scale for adaptive wireframe density (0.0 = default).
     */
    fastf_t         curve_scale;

    /**
     * Point scale for adaptive point cloud density (0.0 = default).
     */
    fastf_t         point_scale;
};


/* -----------------------------------------------------------------------
 * Lifecycle
 * ----------------------------------------------------------------------- */

/**
 * Allocate a bsg_render_settings with default values.
 * Returns NULL on allocation failure.
 */
BSG_EXPORT extern struct bsg_render_settings *
bsg_render_settings_create(void);

/**
 * Release a bsg_render_settings previously allocated by
 * bsg_render_settings_create().
 * No-op if @p rs is NULL.
 */
BSG_EXPORT extern void
bsg_render_settings_destroy(struct bsg_render_settings *rs);

/**
 * Fill @p rs with conservative defaults:
 *   draw_mode = 0 (wireframe), zclip = 0, transparency_policy = SORTED,
 *   lod_policy = AUTO, lod_scale = 1.0, fb_mode = OFF, hud_enabled = 1.
 * No-op if @p rs is NULL.
 */
BSG_EXPORT extern void
bsg_render_settings_init_defaults(struct bsg_render_settings *rs);

/**
 * Populate @p rs from the active bsg_view_settings of @p v.
 *
 * This is the migration bridge: it reads the current scattered
 * `gv_s->*` fields and fills in the corresponding `bsg_render_settings`
 * members, allowing callers to build a settings object for the render
 * request without changing the underlying view state.
 *
 * No-op if either argument is NULL.
 */
BSG_EXPORT extern void
bsg_render_settings_from_view(struct bsg_render_settings *rs,
			       const struct bsg_view *v);

__END_DECLS

#endif /* BSG_RENDER_SETTINGS_H */

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
