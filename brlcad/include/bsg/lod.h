/*                     B S G / L O D . H
 * BRL-CAD
 *
 * Copyright (c) 2004-2025 United States Government as represented by
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
/** @addtogroup bsg_lod
 *
 * @brief
 * Level-of-Detail (LoD) management for mesh geometry in the BSG API.
 *
 * In Open Inventor, @c SoLOD is a group node that picks one of its child
 * groups depending on the distance from the viewer.  In BRL-CAD, mesh LoD
 * is computed externally (in libbg) and the results are cached on disk; this
 * API loads and manages those cached representations on behalf of @c bsg_shape
 * nodes.
 *
 * Functions here replace the @c bv_mesh_lod_* family declared in
 * @c <bv/lod.h>.  In Phase 1 every @c bsg_* function is a trivial
 * wrapper around the corresponding @c bv_* function.
 */
/** @{ */
/* @file bsg/lod.h */

#ifndef BSG_LOD_H
#define BSG_LOD_H

#include "common.h"
#include "vmath.h"
#include "bu/ptbl.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/* ====================================================================== *
 * Context lifecycle                                                       *
 * ====================================================================== */

/**
 * @brief Create (or reopen) an LoD context identified by @p name.
 *
 * @p name should be the full, unique path of the source .g database.
 * Returns NULL on failure.  Replaces bv_mesh_lod_context_create().
 */
BSG_EXPORT bsg_mesh_lod_context *
bsg_mesh_lod_context_create(const char *name);

/**
 * @brief Free all in-memory resources for context @p c.
 *
 * Does NOT remove on-disk cache data.
 * Replaces bv_mesh_lod_context_destroy().
 */
BSG_EXPORT void
bsg_mesh_lod_context_destroy(bsg_mesh_lod_context *c);

/**
 * @brief Remove cached LoD data.
 *
 * key != 0: remove only that key from @p c.
 * key == 0: remove all data in @p c.
 * key == 0 && c == NULL: clear all user LoD cache data.
 * Replaces bv_mesh_lod_clear_cache().
 */
BSG_EXPORT void
bsg_mesh_lod_clear_cache(bsg_mesh_lod_context *c,
			 unsigned long long key);

/* ====================================================================== *
 * LoD key management                                                      *
 * ====================================================================== */

/**
 * @brief Compute (or retrieve) the LoD cache key for a mesh.
 *
 * Returns the lookup key, or 0 on error.
 * Replaces bv_mesh_lod_cache().
 */
BSG_EXPORT unsigned long long
bsg_mesh_lod_cache(bsg_mesh_lod_context *c,
		   const point_t *v, size_t vcnt,
		   const vect_t *vn,
		   int *f, size_t fcnt,
		   unsigned long long user_key,
		   fastf_t fratio);

/**
 * @brief Look up the LoD key for @p name in @p c.  Returns 0 if not found.
 * Replaces bv_mesh_lod_key_get().
 */
BSG_EXPORT unsigned long long
bsg_mesh_lod_key_get(bsg_mesh_lod_context *c, const char *name);

/**
 * @brief Associate @p name with @p key in @p c.  Returns 0 on success.
 * Replaces bv_mesh_lod_key_put().
 */
BSG_EXPORT int
bsg_mesh_lod_key_put(bsg_mesh_lod_context *c,
		     const char *name, unsigned long long key);

/* ====================================================================== *
 * LoD object lifecycle                                                   *
 * ====================================================================== */

/**
 * @brief Create a @c bsg_lod object from cache key @p key.
 *
 * Returns NULL if no cache exists.  Call bsg_mesh_lod_cache() first.
 * Replaces bv_mesh_lod_create().
 */
BSG_EXPORT bsg_lod *
bsg_mesh_lod_create(bsg_mesh_lod_context *c, unsigned long long key);

/**
 * @brief Release all resources for @p lod.  Replaces bv_mesh_lod_destroy().
 */
BSG_EXPORT void
bsg_mesh_lod_destroy(bsg_lod *lod);

/* ====================================================================== *
 * LoD level selection                                                     *
 * ====================================================================== */

/**
 * @brief Select the view-appropriate LoD level for @p s in view @p v.
 *
 * Set @p reset to 1 to reload even if the level is unchanged.
 * Returns the selected level, or -1 on error.
 * Replaces bv_mesh_lod_view().
 */
BSG_EXPORT int
bsg_mesh_lod_view(bsg_shape *s, bsg_view *v, int reset);

/**
 * @brief Explicitly set the LoD level for @p s to @p level.
 *
 * Pass level == -1 to query the current level without changing it.
 * Returns the resulting level, or -1 on error.
 * Replaces bv_mesh_lod_level().
 */
BSG_EXPORT int
bsg_mesh_lod_level(bsg_shape *s, int level, int reset);

/**
 * @brief Reduce in-memory footprint after GPU display list is built.
 * Replaces bv_mesh_lod_memshrink().
 */
BSG_EXPORT void
bsg_mesh_lod_memshrink(bsg_shape *s);

/**
 * @brief Release LoD data for @p s.  Suitable as s->s_free_callback.
 * Replaces bv_mesh_lod_free().
 */
BSG_EXPORT void
bsg_mesh_lod_free(bsg_shape *s);

/* ====================================================================== *
 * LoD detail callbacks                                                   *
 * ====================================================================== */

/**
 * @brief Register the callback that loads full-detail data for @p lod.
 * Replaces bv_mesh_lod_detail_setup_clbk().
 */
BSG_EXPORT void
bsg_mesh_lod_detail_setup_clbk(bsg_lod *lod,
				int (*clbk)(bsg_lod *, void *),
				void *cb_data);

/**
 * @brief Register the callback that clears (but does not free) full-detail
 *        data for @p lod.
 * Replaces bv_mesh_lod_detail_clear_clbk().
 */
BSG_EXPORT void
bsg_mesh_lod_detail_clear_clbk(bsg_lod *lod,
				int (*clbk)(bsg_lod *, void *));

/**
 * @brief Register the callback that frees full-detail data for @p lod.
 * Replaces bv_mesh_lod_detail_free_clbk().
 */
BSG_EXPORT void
bsg_mesh_lod_detail_free_clbk(bsg_lod *lod,
			       int (*clbk)(bsg_lod *, void *));

/* ====================================================================== *
 * View bounds / selection helpers                                        *
 * ====================================================================== */

/**
 * @brief Update the oriented bounding box of the view volume.
 *
 * Intended to be stored in @c bsg_view::gv_bounds_update.
 * Replaces bv_view_bounds().
 */
BSG_EXPORT void
bsg_view_bounds(bsg_view *v);

/**
 * @brief Shapes whose AABB overlaps the projection of pixel (x, y).
 *
 * Appends matching @c bsg_shape pointers to @p result.
 * Returns the number appended.  Replaces bv_view_objs_select().
 */
BSG_EXPORT int
bsg_view_shapes_select(struct bu_ptbl *result, bsg_view *v,
		       int x, int y);

/**
 * @brief Shapes whose AABB overlaps the projection of rectangle (x1,y1)–(x2,y2).
 *
 * Appends matching @c bsg_shape pointers to @p result.
 * Returns the number appended.  Replaces bv_view_objs_rect_select().
 */
BSG_EXPORT int
bsg_view_shapes_rect_select(struct bu_ptbl *result, bsg_view *v,
			    int x1, int y1, int x2, int y2);

__END_DECLS

#endif /* BSG_LOD_H */
/** @} */
