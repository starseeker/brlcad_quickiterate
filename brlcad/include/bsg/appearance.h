/*                   A P P E A R A N C E . H
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
 * Node appearance properties independent of material.
 */
/** @{ */
/* @file bsg/appearance.h */

#ifndef BSG_APPEARANCE_H
#define BSG_APPEARANCE_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

BSG_EXPORT extern void
bsg_appearance_set_visible(bsg_node *node, int visible);

BSG_EXPORT extern int
bsg_appearance_visible(const bsg_node *node);

BSG_EXPORT extern void
bsg_appearance_set_force_draw(bsg_node *node, int force_draw);

BSG_EXPORT extern int
bsg_appearance_force_draw(const bsg_node *node);

BSG_EXPORT extern void
bsg_appearance_set_line_style(bsg_node *node, int dashed);

BSG_EXPORT extern int
bsg_appearance_line_style(const bsg_node *node);

BSG_EXPORT extern void
bsg_appearance_set_line_width(bsg_node *node, int line_width);

BSG_EXPORT extern int
bsg_appearance_line_width(const bsg_node *node);

/* -----------------------------------------------------------------------
 * Illumination / highlight state (s_iflag)
 *
 * s_iflag == UP means the node is currently illuminated/selected for
 * highlighting in the renderer.  Use these accessors in place of direct
 * s_iflag reads and writes.  For batch selection work, prefer the
 * bsg_selection API; these accessors are for per-node one-shot queries.
 * ----------------------------------------------------------------------- */

/**
 * Set the highlight (illumination) state of @p node.
 * @p highlighted non-zero sets s_iflag = UP; zero sets s_iflag = DOWN.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_appearance_set_highlighted(bsg_node *node, int highlighted);

/**
 * Return non-zero if @p node is currently highlighted (s_iflag == UP).
 * Returns 0 if @p node is NULL.
 */
BSG_EXPORT extern int
bsg_appearance_is_highlighted(const bsg_node *node);

/* -----------------------------------------------------------------------
 * Changed / dirty flag (s_changed)
 *
 * s_changed is set by s_update_callback when it regenerates the vlist,
 * signalling the renderer that cached resources are stale.
 * ----------------------------------------------------------------------- */

/**
 * Set the changed flag on @p node.
 * @p changed non-zero asserts the flag; zero clears it.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_appearance_set_changed(bsg_node *node, int changed);

/**
 * Return the current value of the changed flag for @p node.
 * Returns 0 if @p node is NULL.
 */
BSG_EXPORT extern int
bsg_appearance_get_changed(const bsg_node *node);

/* -----------------------------------------------------------------------
 * Drawn-frame revision stamp (s_drawn_rev)
 *
 * s_drawn_rev is stamped by the renderer with the bsg_view's gv_frame_rev
 * each time the node is drawn.  Callers can detect "was this node drawn
 * in the most recent frame" by comparing s_drawn_rev to the view's current
 * gv_frame_rev.  Replaces the legacy s_flag sweep pattern.
 * ----------------------------------------------------------------------- */

/**
 * Set the drawn-frame revision stamp for @p node.
 * No-op if @p node is NULL.
 */
BSG_EXPORT extern void
bsg_appearance_set_drawn_rev(bsg_node *node, uint64_t rev);

/**
 * Return the drawn-frame revision stamp for @p node.
 * Returns 0 if @p node is NULL.
 */
BSG_EXPORT extern uint64_t
bsg_appearance_drawn_rev(const bsg_node *node);

/* -----------------------------------------------------------------------
 * Display mode (s_dmode)
 *
 * s_dmode is stored in bsg_obj_settings and controls how the node is
 * rendered (wireframe, shaded, etc.).
 * ----------------------------------------------------------------------- */

/**
 * Return the display mode for @p node.
 * Returns 0 if @p node or its settings are NULL.
 */
BSG_EXPORT extern int
bsg_appearance_dmode(const bsg_node *node);

/**
 * Set the display mode for @p node.
 * No-op if @p node or its settings are NULL.
 */
BSG_EXPORT extern void
bsg_appearance_set_dmode(bsg_node *node, int dmode);

/* -----------------------------------------------------------------------
 * Transparency (transparency)
 *
 * transparency is stored in bsg_obj_settings as a fastf_t in [0.0, 1.0].
 * ----------------------------------------------------------------------- */

/**
 * Return the transparency value for @p node.
 * Returns 1.0 (fully opaque) if @p node or its settings are NULL.
 */
BSG_EXPORT extern fastf_t
bsg_appearance_transparency(const bsg_node *node);

/**
 * Set the transparency value for @p node (range [0.0, 1.0]).
 * No-op if @p node or its settings are NULL.
 */
BSG_EXPORT extern void
bsg_appearance_set_transparency(bsg_node *node, fastf_t t);

__END_DECLS

#endif /* BSG_APPEARANCE_H */

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
