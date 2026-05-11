/*                   N O D E _ C O R E . H
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
 * Phase 10 BSG node-core public API.
 *
 * Phase 10 embeds a @c struct @c bsg_node_core directly inside every
 * @c bv_scene_obj so that material, appearance, payload, identity, and
 * revision data can be stored inline rather than in process-global hash
 * maps.  This eliminates per-access hash-map lookups and makes node
 * ownership explicit.
 *
 * The struct is defined in bv/defines.h (using only basic C types) so that
 * libbv does not need to include BSG headers.  This header provides:
 *  - A typed accessor @c bsg_node_core_get() for libbsg consumers.
 *  - An explicit initializer @c bsg_node_core_init() for code that wants
 *    to pre-initialise the core before calling any other BSG function.
 *  - @c bsg_node_core_initialized() for diagnostic use.
 *
 * Normal application code should never call these functions directly; use
 * the standard BSG node APIs (bsg_node_material_get, bsg_node_identity_get,
 * etc.) which initialize the core lazily on first access.
 */
/** @{ */
/* @file bsg/node_core.h */

#ifndef BSG_NODE_CORE_H
#define BSG_NODE_CORE_H

#include "common.h"
#include "bv/defines.h"   /* struct bsg_node_core, BSG_NODE_CORE_MAGIC */
#include "bsg/defines.h"  /* bsg_node typedef */

__BEGIN_DECLS

/**
 * Return the embedded @c bsg_node_core for @p n, initialising it on first
 * call.  Never returns NULL for a non-NULL @p n.
 *
 * This is a low-level accessor; prefer the typed BSG APIs.
 */
BSG_EXPORT extern struct bsg_node_core *
bsg_node_core_get(bsg_node *n);

/**
 * Explicitly initialise the BSG node core for @p n.
 *
 * Calling this before the first BSG setter is optional; BSG functions
 * initialize the core lazily.  Explicit initialization is useful when the
 * caller wants to guarantee the core is ready before any concurrent access.
 * No-op if @p n is NULL or the core is already initialized.
 */
BSG_EXPORT extern void
bsg_node_core_init(bsg_node *n);

/**
 * Return 1 if the BSG node core for @p n has been initialized, 0 otherwise.
 * Returns 0 for NULL @p n.
 */
BSG_EXPORT extern int
bsg_node_core_initialized(const bsg_node *n);

__END_DECLS

#endif /* BSG_NODE_CORE_H */

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
