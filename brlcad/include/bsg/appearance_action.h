/*          A P P E A R A N C E _ A C T I O N . H
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
 * Resolved appearance action (Phase D5).
 *
 * `bsg_appearance_resolve` walks the node path from @p node toward the
 * scene root and accumulates:
 *   - base material color from the node and its ancestors
 *   - command-level color override (bsg_obj_settings::color_override)
 *   - display mode (bsg_obj_settings::s_dmode)
 *   - line width / style (bsg_obj_settings::s_line_width / s_soldash)
 *   - selection / highlight state from bsg_selection or s_iflag
 *   - transparency from bsg_obj_settings::transparency
 *   - active appearance layers (highlight, edit_preview, ghosting, etc.)
 *
 * The result is stored in a `bsg_resolved_appearance` and stamped onto the
 * `bsg_render_item` by the render traversal in `bsg_render_request_execute`.
 * Backends read the resolved appearance from the render item and MUST NOT
 * re-derive it from node fields during drawing.
 *
 * Appearance layer composition
 * ----------------------------
 * Layers are applied in priority order (lowest to highest):
 *   BSG_ALAY_BASE        — raw node material
 *   BSG_ALAY_INHERITED   — group/ancestor color override
 *   BSG_ALAY_COMMAND     — s_os color override
 *   BSG_ALAY_TRANSPARENCY — transparency from s_os
 *   BSG_ALAY_GHOSTING    — dim/phantom for objects drawn by reference
 *   BSG_ALAY_HIDDEN_LINE — hidden-line mode (overrides display mode)
 *   BSG_ALAY_EDIT_PREVIEW — edit-plugin preview tint (yellow-ish)
 *   BSG_ALAY_HIGHLIGHT   — selection/illumination highlight (white)
 *
 * Higher-priority layers win for color and display mode.
 */
/** @{ */
/* @file bsg/appearance_action.h */

#ifndef BSG_APPEARANCE_ACTION_H
#define BSG_APPEARANCE_ACTION_H

#include "common.h"
#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/* -----------------------------------------------------------------------
 * Appearance layer enum
 * ----------------------------------------------------------------------- */

/**
 * Appearance layer flags.
 *
 * These bits are OR'd into bsg_resolved_appearance::active_layers to
 * record which appearance overrides are active for a given render item.
 * They are NOT a priority order — they are bit flags for post-resolve
 * queries such as "is this item being highlighted?".
 */
typedef enum bsg_appearance_layer {
    /** Raw material color from the node (always set). */
    BSG_ALAY_BASE         = 0x001,

    /** Ancestor group provided an inherited color override. */
    BSG_ALAY_INHERITED    = 0x002,

    /** bsg_obj_settings color override is active. */
    BSG_ALAY_COMMAND      = 0x004,

    /** Transparency layer: transparency < 1.0. */
    BSG_ALAY_TRANSPARENCY = 0x008,

    /** Ghosting: object is drawn at reduced opacity (phantom/reference). */
    BSG_ALAY_GHOSTING     = 0x010,

    /** Hidden-line: display mode forces hidden-line rendering. */
    BSG_ALAY_HIDDEN_LINE  = 0x020,

    /** Edit-preview tint: active in an editor's per-tool scope. */
    BSG_ALAY_EDIT_PREVIEW = 0x040,

    /** Highlight: node is selected/illuminated (s_iflag == UP). */
    BSG_ALAY_HIGHLIGHT    = 0x080
} bsg_appearance_layer;


/* -----------------------------------------------------------------------
 * Resolved appearance struct
 * ----------------------------------------------------------------------- */

/**
 * Fully resolved appearance for one render item.
 *
 * Produced by `bsg_appearance_resolve`.  The backend reads these fields
 * directly from the `bsg_render_item` and must not re-derive them.
 *
 * Revision fields allow backends to detect stale cached resources:
 * - `material_rev`: bumped by `bsg_material_set_rgb` or color overrides.
 * - `appearance_rev`: bumped by any highlight/transparency/dmode change.
 *
 * Both revisions are per-node values copied out at resolve time; a change
 * in either indicates the backend cache key must be invalidated.
 */
struct bsg_resolved_appearance {
    /** Resolved RGB color (after all layer overrides). */
    unsigned char   color[3];

    /** Resolved transparency in [0.0, 1.0]; 1.0 = fully opaque. */
    fastf_t         transparency;

    /** Resolved display mode (BSG_WIREFRAME / BSG_SHADED / etc.). */
    int             dmode;

    /** Resolved line width in pixels. */
    int             line_width;

    /** Resolved line style: 0 = solid, non-zero = dashed. */
    int             line_style;

    /** Non-zero when the node is highlighted (s_iflag == UP). */
    int             highlighted;

    /**
     * Active layer bitmask — which appearance overrides are in effect.
     * OR of `bsg_appearance_layer` values.
     */
    unsigned int    active_layers;

    /** Material revision at resolve time (for backend cache invalidation). */
    uint32_t        material_rev;

    /** Appearance revision at resolve time (for backend cache invalidation). */
    uint32_t        appearance_rev;
};


/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/**
 * Resolve the final appearance for @p node in view @p v, writing the
 * result into @p out.
 *
 * The resolution accumulates:
 *   1. Base material RGB from the node (s_color / s_material).
 *   2. Ancestor group settings passed in as @p inherited_os (may be NULL).
 *   3. Per-node command override from s_os (when set and color_override set).
 *   4. Transparency from s_os (defaults to 1.0 = fully opaque).
 *   5. Display mode, line width, line style from s_os (defaults: 0, 1, 0).
 *   6. Highlight state from s_iflag == UP.
 *   7. Active layer bits in out->active_layers.
 *   8. Material and appearance revision stamps copied from the node.
 *
 * No-op (returns 0) if @p node or @p out is NULL.
 * Returns non-zero on success.
 *
 * @param v             View context (used for selection state lookup; may be NULL).
 * @param node          Shape node to resolve appearance for.
 * @param inherited_os  Inherited object settings from an ancestor group
 *                      (s_inherit_settings on the parent); may be NULL.
 * @param out           Caller-allocated output struct filled on success.
 */
BSG_EXPORT extern int
bsg_appearance_resolve(const struct bsg_view *v,
		       const bsg_node *node,
		       const struct bsg_obj_settings *inherited_os,
		       struct bsg_resolved_appearance *out);

__END_DECLS

#endif /* BSG_APPEARANCE_ACTION_H */

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
