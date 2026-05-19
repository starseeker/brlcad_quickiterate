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
 * @c struct @c bsg_settings captures the inherited drawing-settings concept.
 * When a node has @c inherit_settings set (see
 * @c bsg_appearance.inherit_settings), its settings are pushed down to
 * children via a typed @c bsg_settings snapshot.
 */
/** @{ */
/* @file bsg/settings.h */

#ifndef BSG_SETTINGS_H
#define BSG_SETTINGS_H

#include "common.h"
#include "bsg/defines.h"
#include "bsg/settings_types.h"

__BEGIN_DECLS

/**
 * Initialize @p s to safe defaults (wireframe, opaque white, 1-pixel lines).
 */
BSG_EXPORT extern void
bsg_settings_init(struct bsg_settings *s);

/**
 * Populate @p out with the effective settings for @p n.
 *
 * Reads the node's current BSG settings storage.  Returns 1 if a node was
 * supplied, 0 otherwise.
 */
BSG_EXPORT extern int
bsg_node_settings_get(const bsg_node *n, struct bsg_settings *out);

/**
 * Write @p s into the node's local settings storage.
 */
BSG_EXPORT extern void
bsg_node_settings_set(bsg_node *n, const struct bsg_settings *s);

/**
 * Copy settings state from @p src to @p dest.
 *
 * Returns 0 if no settings changed in @p dest and 1 otherwise.
 */
BSG_EXPORT extern int
bsg_settings_sync(struct bsg_settings *dest, struct bsg_settings *src);

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
