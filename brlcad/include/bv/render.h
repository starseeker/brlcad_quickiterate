/*                      R E N D E R . H
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @addtogroup libbv
 * @{
 */
/** @file bv/render.h
 *
 * @brief Opaque render-context wrapper over the Obol/Inventor scene-graph
 * rendering backend.
 *
 * This header is the *only* place in the BRL-CAD public API where the
 * connection between BRL-CAD's native @c bv_scene / @c bv_node types and
 * the Obol (Open Inventor) rendering system is exposed.  No Inventor or
 * Obol headers are included here; all Inventor types remain private to
 * @c libbv's @c render_ctx.cpp translation unit.
 *
 * ### Design overview
 *
 * The @c bv_render_ctx type is an opaque handle that owns:
 *  - An @c SoSceneManager instance (Obol's all-in-one render manager)
 *  - An @c SoSeparator root node that mirrors the @c bv_scene node tree
 *  - A mapping from each @c bv_node to its corresponding @c SoNode so that
 *    incremental updates can be applied efficiently
 *
 * On each call to @c bv_render_frame() the implementation:
 *  1. Traverses the @c bv_scene tree via @c bv_scene_traverse()
 *  2. For each node whose @c dlist_stale flag is set, rebuilds the
 *     corresponding Inventor node (geometry, transform, material)
 *  3. Calls @c SoSceneManager::render() to produce a frame
 *
 * ### Context manager
 *
 * @c bv_render_ctx_create() accepts an opaque @c context_manager pointer.
 * This pointer is passed directly to @c SoDB::init(ContextManager*).
 *
 * Two convenience factories are provided:
 *  - @c bv_render_ctx_osmesa_mgr_create() – returns an OSMesa-backed
 *    @c SoDB::ContextManager* for headless / CI rendering.  Returns NULL
 *    when Obol was not built with OSMesa support.
 *  - @c bv_render_ctx_osmesa_mgr_destroy() – releases the manager.
 *
 * The application is responsible for keeping the context manager alive for
 * the entire lifetime of every @c bv_render_ctx that was created with it.
 *
 * ### When Obol is not available
 *
 * All functions in this header compile and link cleanly when Obol is absent.
 * @c bv_render_ctx_create() returns NULL, @c bv_render_frame() returns 0,
 * and @c bv_render_ctx_available() returns 0.  This allows callers to check
 * availability at run-time without compile-time conditionals.
 */

#ifndef BV_RENDER_H
#define BV_RENDER_H

#include "common.h"

#include "bv/defines.h"

__BEGIN_DECLS

/**
 * @brief Opaque handle to an Obol/Inventor render context.
 *
 * Created by @c bv_render_ctx_create(), destroyed by
 * @c bv_render_ctx_destroy().  All Inventor types are hidden behind this
 * pointer; no @c <Inventor/...> headers need to be visible to the caller.
 */
struct bv_render_ctx;

/**
 * @brief Returns 1 if the Obol rendering backend is compiled in, 0 otherwise.
 *
 * Callers can gate Obol code paths on this without compile-time guards.
 */
BV_EXPORT int bv_render_ctx_available(void);

/**
 * @brief Create an Obol render context for the given scene.
 *
 * Initialises @c SoDB (once per process) and creates an @c SoSceneManager
 * that mirrors @p scene's node tree.
 *
 * @param scene           The BRL-CAD scene to render.  Must not be NULL.
 * @param context_manager An @c SoDB::ContextManager* cast to @c void*, or
 *                        NULL to let the implementation attempt to create
 *                        a default OSMesa context manager.  The caller
 *                        retains ownership of this object.
 * @param width           Initial viewport width  in pixels (> 0).
 * @param height          Initial viewport height in pixels (> 0).
 * @return A new @c bv_render_ctx on success, or NULL when Obol is not
 *         available or initialisation fails.
 */
BV_EXPORT struct bv_render_ctx *bv_render_ctx_create(struct bv_scene *scene,
						      void *context_manager,
						      int width, int height);

/**
 * @brief Release all resources owned by @p ctx.
 *
 * Unrefs all Obol nodes, destroys the @c SoSceneManager, and frees @p ctx.
 * No-op if @p ctx is NULL.
 */
BV_EXPORT void bv_render_ctx_destroy(struct bv_render_ctx *ctx);

/**
 * @brief Resize the viewport associated with @p ctx.
 *
 * Should be called whenever the display window changes size.  No-op if
 * @p ctx is NULL or either dimension is <= 0.
 */
BV_EXPORT void bv_render_ctx_set_size(struct bv_render_ctx *ctx, int width, int height);

/**
 * @brief Render one frame for @p view using @p ctx.
 *
 * Synchronises any stale @c bv_node entries in the scene to their Inventor
 * equivalents, then calls @c SoSceneManager::render().
 *
 * @param ctx   The render context to use.  Must not be NULL.
 * @param view  The view whose camera and viewport parameters drive the
 *              render.  May be NULL to use the context's default camera.
 * @return 1 on success, 0 if Obol is not available or @p ctx is NULL.
 */
BV_EXPORT int bv_render_frame(struct bv_render_ctx *ctx, struct bview_new *view);

/**
 * @brief Create an OSMesa-backed ContextManager for headless rendering.
 *
 * Returns an @c SoDB::ContextManager* (cast to @c void*) that uses the
 * Mesa off-screen renderer.  The returned pointer must be passed to
 * @c bv_render_ctx_create() and kept alive until after the last
 * @c bv_render_ctx_destroy() call for any context created with it.
 *
 * Returns NULL when Obol was not built with OSMesa support, or when Obol
 * is not available at all.
 */
BV_EXPORT void *bv_render_ctx_osmesa_mgr_create(void);

/**
 * @brief Destroy an OSMesa ContextManager returned by
 *        @c bv_render_ctx_osmesa_mgr_create().
 *
 * No-op if @p mgr is NULL.
 */
BV_EXPORT void bv_render_ctx_osmesa_mgr_destroy(void *mgr);

__END_DECLS

#endif /* BV_RENDER_H */

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
