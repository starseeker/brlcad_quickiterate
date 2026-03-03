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
 *  - An @c SoViewport instance that pairs a scene graph with an independent
 *    camera and viewport region (new Obol viewport API)
 *  - An @c SoRenderManager for GL render mode / stereo / multi-pass control
 *  - A hierarchical @c SoSeparator root that mirrors the @c bv_scene node
 *    tree structure (groups, separators, transforms, geometry, materials)
 *  - A mapping from each @c bv_node to its corresponding @c SoNode so that
 *    incremental updates can be applied efficiently
 *
 * On each call to @c bv_render_frame() the implementation:
 *  1. Traverses the @c bv_scene tree recursively (respecting parent/child
 *     relationships) to build or update the corresponding Inventor node tree
 *  2. For each geometry node whose @c dlist_stale flag is set, rebuilds the
 *     corresponding SoSeparator containing SoCoordinate3 + SoIndexedLineSet
 *     (wireframe), SoIndexedFaceSet (shaded) or SoIndexedPointSet (points)
 *  3. Calls @c SoRenderManager::render() (or @c SoViewport::render() for
 *     headless use) to produce a frame
 *
 * ### Quad-view render context
 *
 * @c bv_quad_render_ctx wraps Obol's new @c SoQuadViewport API to manage
 * four independent views (top-left, top-right, bottom-left, bottom-right)
 * sharing the same @c bv_scene.  The four quadrant indices are:
 *  - @c BV_QUAD_TOP_LEFT (0)
 *  - @c BV_QUAD_TOP_RIGHT (1)
 *  - @c BV_QUAD_BOTTOM_LEFT (2)
 *  - @c BV_QUAD_BOTTOM_RIGHT (3)
 *
 * ### Scene root access for Qt integration
 *
 * @c bv_render_ctx_scene_root() returns the internal @c SoNode* (as void*)
 * so that Qt-aware rendering code in @c libqtcad can set it as the scene
 * graph of its own @c SoViewport or @c SoRenderManager without the core
 * BRL-CAD libraries needing to know about Qt or Obol directly.
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

/**
 * @brief Return the internal Inventor scene root (SoNode*) as a void*.
 *
 * The returned pointer is owned by @p ctx and must not be freed by the
 * caller.  It remains valid until @c bv_render_ctx_destroy() is called.
 *
 * Typical use: libqtcad's QgObolWidget calls this to obtain the SoNode*
 * and pass it to its own SoViewport, allowing the GL render path to share
 * the same scene graph that was built from the bv_scene without the core
 * BRL-CAD libraries needing Qt or Obol awareness.
 *
 * Returns NULL when Obol is not available or @p ctx is NULL.
 */
BV_EXPORT void *bv_render_ctx_scene_root(struct bv_render_ctx *ctx);

/* ======================================================================== */
/* Quad-view render context (SoQuadViewport-backed)                         */
/* ======================================================================== */

/**
 * @brief Symbolic indices for the four quadrants of a quad render context.
 *
 * These values are intentionally aligned with SoQuadViewport::QuadIndex so
 * that libqtcad can pass them directly through without translation.
 *
 *  Layout (OpenGL origin at bottom-left):
 *  @verbatim
 *   ┌──────────────┬──────────────┐
 *   │ TOP_LEFT (0) │ TOP_RIGHT(1) │
 *   ├──────────────┼──────────────┤
 *   │BOT_LEFT  (2) │ BOT_RIGHT(3) │
 *   └──────────────┴──────────────┘
 *  @endverbatim
 */
#define BV_QUAD_TOP_LEFT     0
#define BV_QUAD_TOP_RIGHT    1
#define BV_QUAD_BOTTOM_LEFT  2
#define BV_QUAD_BOTTOM_RIGHT 3
#define BV_QUAD_NUM_QUADS    4

/**
 * @brief Opaque handle to an Obol/SoQuadViewport render context.
 *
 * Created by @c bv_quad_render_ctx_create(),
 * destroyed by @c bv_quad_render_ctx_destroy().
 */
struct bv_quad_render_ctx;

/**
 * @brief Create an Obol quad render context for the given scene.
 *
 * All four quadrant SoViewport instances share @p scene.  Each quadrant
 * maintains its own independent camera and viewport region.
 *
 * @param scene           The BRL-CAD scene to render.  Must not be NULL.
 * @param context_manager An @c SoDB::ContextManager* cast to @c void*, or
 *                        NULL to try a default OSMesa context manager.
 * @param width           Initial full-window width  in pixels (> 0).
 * @param height          Initial full-window height in pixels (> 0).
 * @return A new @c bv_quad_render_ctx on success, or NULL on failure.
 */
BV_EXPORT struct bv_quad_render_ctx *bv_quad_render_ctx_create(
    struct bv_scene *scene, void *context_manager, int width, int height);

/**
 * @brief Release all resources owned by @p ctx.
 * No-op if @p ctx is NULL.
 */
BV_EXPORT void bv_quad_render_ctx_destroy(struct bv_quad_render_ctx *ctx);

/**
 * @brief Resize the quad context to a new full-window size.
 *
 * Updates all four quadrant viewport regions accordingly.
 * No-op if @p ctx is NULL or either dimension is <= 0.
 */
BV_EXPORT void bv_quad_render_ctx_set_size(struct bv_quad_render_ctx *ctx,
					   int width, int height);

/**
 * @brief Render all four quadrants into a composite RGB file.
 *
 * @param ctx          The quad render context.  Must not be NULL.
 * @param views        Array of four @c bview_new pointers (one per quadrant).
 *                     A NULL entry uses the quadrant's default camera.
 * @param output_path  Path for the composite SGI RGB output file.
 * @return 1 on success, 0 on failure or when Obol is not available.
 */
BV_EXPORT int bv_quad_render_frame(struct bv_quad_render_ctx *ctx,
				   struct bview_new *views[BV_QUAD_NUM_QUADS],
				   const char *output_path);

/**
 * @brief Return the shared Inventor scene root (SoNode*) as a void*.
 *
 * The same scene root is rendered by all four quadrants.  Callers in
 * libqtcad can use this to set up their own SoQuadViewport without
 * rebuilding the scene.
 *
 * Returns NULL when Obol is not available or @p ctx is NULL.
 */
BV_EXPORT void *bv_quad_render_ctx_scene_root(struct bv_quad_render_ctx *ctx);

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
