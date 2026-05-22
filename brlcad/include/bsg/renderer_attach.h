/*              R E N D E R E R _ A T T A C H . H
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
 * Slice 11 (bv_scene_obj_migrate): BSG-owned renderer attachment.
 *
 * A @c bsg_renderer_attach descriptor holds the per-node backend cache
 * for a single renderer (GL, software rasterizer, or a future renderer).
 * It replaces the previous @c bv_obj_backend type that was defined in
 * @c bv/defines.h.
 *
 * Lifecycle:
 *  - allocated lazily by the renderer backend when it first needs to cache
 *    a GPU resource for a node;
 *  - released via @c bsg_node_backend_release() when the node is destroyed
 *    or recycled;
 *  - invalidated via @c bsg_node_backend_invalidate() when source data that
 *    drives the cached resource has changed.
 *
 * @note
 * The callbacks take @c struct @c bv_scene_obj * for transition-period
 * compatibility.  Backends that already accept a @c bsg_node* can cast
 * freely because @c struct @c bsg_node is the first member of
 * @c struct @c bv_scene_obj.
 */
/** @{ */
/* @file bsg/renderer_attach.h */

#ifndef BSG_RENDERER_ATTACH_H
#define BSG_RENDERER_ATTACH_H

#include "common.h"

#include <stdint.h>

#include "bsg/defines.h"

__BEGIN_DECLS

struct bv_scene_obj; /* transitional forward declaration */

/**
 * @name Backend type tags
 *
 * @c type_tag values for @c bsg_renderer_attach.  Renderers register their
 * tag at display-manager registration time; the per-node attachment slot
 * carries the matching tag so cross-backend confusion can be caught early.
 * Additional tags will be added as new renderer backends adopt this contract
 * (e.g. a future Vulkan / Obol backend).
 */
/** @{ */
#define BSG_BACKEND_NONE  0u  /**< @brief no backend state attached */
#define BSG_BACKEND_GL    1u  /**< @brief OpenGL / software rasterizer (dm-gl, dm-swrast, dm-qtgl, dm-glx, dm-wgl) */
/** @} */

/**
 * Per-node renderer attachment.
 *
 * One @c bsg_renderer_attach descriptor describes a single renderer
 * backend's per-node cache state.  The active scene node stores the
 * descriptor via the BSG-managed renderer attachment slot (previously
 * @c bv_scene_obj::s_backend, now accessed exclusively through
 * @c bsg_node_backend_get / @c bsg_node_backend_set).
 *
 * Backends are expected to:
 *  -# allocate one @c bsg_renderer_attach (typically lazily on first draw);
 *  -# store their per-node resource in @c handle;
 *  -# provide a @c free callback that releases @c handle resources and
 *     frees the @c bsg_renderer_attach descriptor itself;
 *  -# optionally provide an @c invalidate callback for backends that can
 *     regenerate a stale cache without a full release/re-allocate cycle.
 */
struct bsg_renderer_attach {
    uint32_t type_tag;                         /**< @brief BSG_BACKEND_* identifying the owner */
    void *handle;                              /**< @brief backend-private per-node state */
    void (*free)(struct bv_scene_obj *);       /**< @brief release backend resources and free this descriptor */
    void (*invalidate)(struct bv_scene_obj *); /**< @brief mark cached resource stale; may be NULL */
};


/* ---------------------------------------------------------------------- */
/* API                                                                      */
/* ---------------------------------------------------------------------- */

/**
 * Release the renderer attachment on @p n.
 *
 * Fires the @c free callback (if set) and clears the attachment slot.
 * No-op when @p n is NULL or has no attachment.
 */
BSG_EXPORT extern void
bsg_node_backend_release(bsg_node *n);

/**
 * Invalidate the renderer attachment on @p n.
 *
 * Fires the @c invalidate callback (if set).  Does NOT recurse into
 * children; call @c bsg_node_stale() for a recursive invalidation.
 * No-op when @p n is NULL, has no attachment, or has no @c invalidate
 * callback.
 */
BSG_EXPORT extern void
bsg_node_backend_invalidate(bsg_node *n);

/**
 * Recursively mark @p n and all its descendants as stale.
 *
 * Fires @c bsg_node_backend_invalidate on @p n and then recurses into
 * every child node.  This is the BSG-native replacement for
 * @c bv_obj_stale().
 */
BSG_EXPORT extern void
bsg_node_stale(bsg_node *n);

__END_DECLS

#endif /* BSG_RENDERER_ATTACH_H */

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
