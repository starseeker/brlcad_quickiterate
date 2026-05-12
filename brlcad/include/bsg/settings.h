/*                   S E T T I N G S . H
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
 * Phase 12 settings-inheritance API for BSG nodes.
 *
 * @c struct @c bsg_settings captures the inherited drawing-settings concept
 * previously represented by raw @c bv_obj_settings pointer propagation in
 * libdm traversal.  When a node has @c inherit_settings set (see
 * @c bsg_appearance.inherit_settings), its settings are pushed down to
 * children via a typed @c bsg_settings snapshot rather than a raw
 * @c bv_obj_settings pointer.
 *
 * The struct mirrors all fields of @c struct @c bv_obj_settings and provides
 * round-trip conversion helpers, so that legacy code remains correct while new
 * code only sees the BSG type.
 */
/** @{ */
/* @file bsg/settings.h */

#ifndef BSG_SETTINGS_H
#define BSG_SETTINGS_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * BSG settings-inheritance snapshot.
 *
 * Contains the drawing settings that a parent node propagates to its children
 * when @c bsg_appearance.inherit_settings is non-zero.  Fields map 1:1 to
 * the @c struct @c bv_obj_settings fields they replaced.
 */
struct bsg_settings {
    int draw_mode;               /**< @brief draw mode (0=wire, 1-4 shaded); maps s_dmode */
    int mixed_modes;             /**< @brief don't remove objects for other modes; maps mixed_modes */
    fastf_t transparency;        /**< @brief [0..1], 1 is opaque; maps transparency */
    int color_override;          /**< @brief 1 if color[] should override node's own; maps color_override */
    unsigned char color[3];      /**< @brief inherited RGB; maps color[3] */
    int line_width;              /**< @brief line width; maps s_line_width */
    fastf_t arrow_tip_length;    /**< @brief arrow tip length; maps s_arrow_tip_length */
    fastf_t arrow_tip_width;     /**< @brief arrow tip width; maps s_arrow_tip_width */
    int draw_solid_lines_only;   /**< @brief no dashed lines for subtraction solids; maps draw_solid_lines_only */
    int draw_non_subtract_only;  /**< @brief do not visualize subtraction solids; maps draw_non_subtract_only */
};

/**
 * Initialize @p s to safe defaults (wireframe, opaque white, 1-pixel lines).
 */
BSG_EXPORT extern void
bsg_settings_init(struct bsg_settings *s);

/**
 * Populate @p out with the effective settings for @p n.
 *
 * Reads from @p n->s_os when non-NULL, otherwise from @p n->s_local_os.
 * Returns 1 if a non-default settings source was found, 0 otherwise.
 */
BSG_EXPORT extern int
bsg_node_settings_get(const bsg_node *n, struct bsg_settings *out);

/**
 * Write @p s into the node's local settings storage (@p n->s_local_os) and
 * point @p n->s_os at the local storage so that the next legacy read sees the
 * updated values.
 */
BSG_EXPORT extern void
bsg_node_settings_set(bsg_node *n, const struct bsg_settings *s);

/**
 * Fill @p out from a raw @c bv_obj_settings pointer.
 *
 * @p os may be NULL; in that case @p out is initialised to defaults.
 * This helper is provided for migration boundaries where a legacy pointer
 * must be converted to a typed snapshot before being passed down the BSG API.
 */
BSG_EXPORT extern void
bsg_settings_from_legacy_obj_settings(const struct bv_obj_settings *os,
				      struct bsg_settings *out);

/**
 * Write @p s back into a raw @c bv_obj_settings struct.
 *
 * No-op if @p os or @p s is NULL.
 */
BSG_EXPORT extern void
bsg_settings_to_legacy_obj_settings(const struct bsg_settings *s,
				    struct bv_obj_settings *os);

__END_DECLS

#endif /* BSG_SETTINGS_H */

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
