/*                        R E N D E R . H
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
/** @addtogroup librtrender */
/** @{ */
/** @file render.h
 * @brief
 *  Public API for librtrender: a clean, toolkit-agnostic C library for
 *  controlling BRL-CAD raytraced image generation.
 *
 * librtrender encapsulates the rt command's ability to generate images
 * without requiring callers to know about rt's command-line option nest.
 * Rendering options are exposed through ordinary C getter/setter APIs,
 * and pixel data is delivered via a user-supplied callback, one scanline
 * at a time, to enable incremental display.
 *
 * ### In-process rendering (synchronous)
 *
 *   render_ctx_t *ctx = render_ctx_create("model.g", nobjs, objs);
 *   render_opts_t *opts = render_opts_create();
 *   render_opts_set_size(opts, 512, 512);
 *   render_opts_set_view(opts, view2model, eye, viewsize);
 *   render_run(ctx, opts, my_pixel_cb, NULL, NULL, mydata);
 *   render_opts_destroy(opts);
 *   render_ctx_destroy(ctx);
 *
 * ### Out-of-process rendering (async, via rt_ipc + libuv)
 *
 *   render_ipc_client_t *cli = render_ipc_client_create();
 *   render_ipc_client_on_pixels(cli, my_pixel_cb, mydata);
 *   render_ipc_client_on_done(cli, my_done_cb, mydata);
 *   render_ipc_client_spawn(cli, rt_ipc_path, loop);
 *   render_ipc_client_submit(cli, ctx, opts);
 *   uv_run(loop, UV_RUN_DEFAULT);
 *   render_ipc_client_destroy(cli);
 *
 */

#ifndef RENDER_H
#define RENDER_H

#include "common.h"

#include <stdint.h>
#include "bu/defines.h"

/* ------------------------------------------------------------------ */
/* DLL export/import macros                                             */
/* ------------------------------------------------------------------ */

#ifndef RENDER_EXPORT
#  if defined(RTRENDER_DLL_EXPORTS) && defined(RTRENDER_DLL_IMPORTS)
#    error "Only RTRENDER_DLL_EXPORTS or RTRENDER_DLL_IMPORTS can be defined, not both."
#  elif defined(RTRENDER_DLL_EXPORTS)
#    define RENDER_EXPORT COMPILER_DLLEXPORT
#  elif defined(RTRENDER_DLL_IMPORTS)
#    define RENDER_EXPORT COMPILER_DLLIMPORT
#  else
#    define RENDER_EXPORT
#  endif
#endif

__BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Lighting models                                                      */
/* ------------------------------------------------------------------ */

/** @brief Surface normals mapped to RGB (fastest, no light setup needed) */
#define RENDER_LIGHT_NORMALS  0

/** @brief Simple diffuse N·L from eye direction with region color */
#define RENDER_LIGHT_DIFFUSE  1

/** @brief Full Phong model via liboptical (shadows, materials, lights) */
#define RENDER_LIGHT_FULL     2


/* ------------------------------------------------------------------ */
/* Opaque handle types                                                  */
/* ------------------------------------------------------------------ */

/** @brief Loaded geometry context (one per .g file / object combination). */
typedef struct render_ctx render_ctx_t;

/** @brief Per-render options (view, dimensions, lighting, etc.). */
typedef struct render_opts render_opts_t;

/** @brief Out-of-process IPC client handle (requires libuv). */
typedef struct render_ipc_client render_ipc_client_t;


/* ------------------------------------------------------------------ */
/* Callback types                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Called once per completed scanline during rendering.
 *
 * @param ud    User-supplied pointer passed to render_run() or
 *              render_ipc_client_on_pixels().
 * @param x     X coordinate of the leftmost pixel in this scanline
 *              (always 0 for full-width scanlines).
 * @param y     Y coordinate of the scanline (0 = bottom row).
 * @param w     Number of pixels in @p rgb (= image width for full scanlines).
 * @param rgb   Packed RGB triplets: [R0 G0 B0  R1 G1 B1 ...] each 0-255.
 *
 * The callback may be called from a worker thread; synchronisation is
 * the caller's responsibility if the callback touches shared state.
 */
typedef void (*render_pixel_cb)(void *ud,
				int x, int y, int w,
				const unsigned char *rgb);

/**
 * @brief Called periodically to report progress.
 *
 * @param ud    User-supplied pointer.
 * @param done  Number of scanlines completed so far.
 * @param total Total number of scanlines.
 */
typedef void (*render_progress_cb)(void *ud, int done, int total);

/**
 * @brief Called once when a render finishes (success or failure).
 *
 * @param ud    User-supplied pointer.
 * @param ok    Non-zero on success, 0 on failure.
 */
typedef void (*render_done_cb)(void *ud, int ok);


/* ------------------------------------------------------------------ */
/* Context API                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Create a render context by loading geometry from a .g file.
 *
 * Loads @p nobjs named objects from @p dbfile, runs rt_prep_parallel(),
 * and (when RENDER_LIGHT_FULL is planned) initialises liboptical shaders.
 *
 * @param dbfile  Path to the BRL-CAD geometry database (.g file).
 * @param nobjs   Number of top-level object names in @p objs.
 * @param objs    Array of top-level object names to raytrace.
 *
 * @return A new context, or NULL on failure.
 */
RENDER_EXPORT render_ctx_t *render_ctx_create(const char *dbfile,
					      int nobjs,
					      const char **objs);

/**
 * @brief Destroy a render context and free all associated resources.
 */
RENDER_EXPORT void render_ctx_destroy(render_ctx_t *ctx);


/* ------------------------------------------------------------------ */
/* Options API                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Allocate a render options object initialised to safe defaults.
 *
 * Defaults:
 *   size         = 512 x 512
 *   lightmodel   = RENDER_LIGHT_DIFFUSE
 *   nthreads     = 1
 *   background   = (0, 0, 0)  (black)
 *   aspect       = 1.0  (square pixels)
 *
 * @return A new options object, or NULL on allocation failure.
 */
RENDER_EXPORT render_opts_t *render_opts_create(void);

/**
 * @brief Free a render options object.
 */
RENDER_EXPORT void render_opts_destroy(render_opts_t *opts);

/**
 * @brief Set the output image dimensions in pixels.
 */
RENDER_EXPORT void render_opts_set_size(render_opts_t *opts,
					int width, int height);

/**
 * @brief Set the view parameters used to compute per-pixel ray directions.
 *
 * @param opts        Options to modify.
 * @param view2model  16-element column-major 4x4 view-to-model matrix in
 *                    BRL-CAD's Viewrotscale convention (element [15] ==
 *                    0.5 * viewsize).  Pass the gv_view2model field from a
 *                    struct bview, or compute it with bn_mat_inv() from the
 *                    model2view matrix.
 * @param eye         Eye position in model-space coordinates (3 doubles).
 * @param viewsize    View size (diameter, in model units, typically mm).
 */
RENDER_EXPORT void render_opts_set_view(render_opts_t *opts,
					const double *view2model,
					const double *eye,
					double viewsize);

/**
 * @brief Override the pixel aspect ratio (width / height).
 *
 * The default of 1.0 produces square pixels.  Call this if the
 * viewport is non-square and the horizontal FOV is fixed.
 */
RENDER_EXPORT void render_opts_set_aspect(render_opts_t *opts, double aspect);

/**
 * @brief Select the lighting model.  One of RENDER_LIGHT_*.
 */
RENDER_EXPORT void render_opts_set_lighting(render_opts_t *opts,
					    int lightmodel);

/**
 * @brief Set the number of parallel rendering threads.
 *
 * 0 or negative means "use all available CPUs".
 */
RENDER_EXPORT void render_opts_set_threads(render_opts_t *opts, int nthreads);

/**
 * @brief Set the background colour (each channel 0.0 – 1.0).
 */
RENDER_EXPORT void render_opts_set_background(render_opts_t *opts,
					      double r, double g, double b);

/**
 * @brief Enable or disable perspective projection.
 *
 * @param perspective_deg  Full horizontal field-of-view in degrees.
 *                         0 (default) = orthographic projection.
 */
RENDER_EXPORT void render_opts_set_perspective(render_opts_t *opts,
					       double perspective_deg);


/* ------------------------------------------------------------------ */
/* Synchronous (in-process) render                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief Execute a raytrace synchronously, delivering scanlines via callback.
 *
 * Blocks until the entire image has been raytraced.  @p pxcb is called
 * once per scanline as it completes; @p progcb and @p donecb are optional.
 *
 * @param ctx     Loaded geometry context (from render_ctx_create()).
 * @param opts    Render options (from render_opts_create() + setters).
 * @param pxcb    Scanline delivery callback (required).
 * @param progcb  Progress callback (may be NULL).
 * @param donecb  Completion callback (may be NULL).
 * @param ud      User data pointer forwarded to all callbacks.
 *
 * @return BRLCAD_OK on success, BRLCAD_ERROR on failure.
 */
RENDER_EXPORT int render_run(render_ctx_t *ctx,
			     const render_opts_t *opts,
			     render_pixel_cb  pxcb,
			     render_progress_cb progcb,
			     render_done_cb  donecb,
			     void *ud);


/* ------------------------------------------------------------------ */
/* Out-of-process IPC client (requires libuv)                          */
/* ------------------------------------------------------------------ */

#ifdef RENDER_HAVE_LIBUV
#  include <uv.h>

/**
 * @brief Allocate an IPC client handle.
 *
 * The client communicates with an @c rt_ipc process via a libuv
 * anonymous pipe (not TCP), which is more portable and avoids the
 * need for port allocation and TCP stack overhead.
 *
 * @return A new client, or NULL on allocation failure.
 */
RENDER_EXPORT render_ipc_client_t *render_ipc_client_create(void);

/**
 * @brief Free an IPC client handle and disconnect from any active process.
 */
RENDER_EXPORT void render_ipc_client_destroy(render_ipc_client_t *cli);

/**
 * @brief Spawn an @c rt_ipc child process and connect the IPC pipe.
 *
 * @param cli         Client handle.
 * @param rt_ipc_path Full path to the @c rt_ipc executable.
 * @param loop        libuv event loop to attach to.
 *
 * @return 0 on success, negative libuv error code on failure.
 */
RENDER_EXPORT int render_ipc_client_spawn(render_ipc_client_t *cli,
					  const char *rt_ipc_path,
					  uv_loop_t *loop);

/**
 * @brief Submit a render job to the spawned @c rt_ipc process.
 *
 * Serialises @p ctx and @p opts into the IPC wire format and writes
 * them to the pipe.  Pixel data will arrive asynchronously via the
 * callback registered with render_ipc_client_on_pixels().
 *
 * @return 0 on success, negative libuv error code on failure.
 */
RENDER_EXPORT int render_ipc_client_submit(render_ipc_client_t *cli,
					   render_ctx_t *ctx,
					   const render_opts_t *opts);

/**
 * @brief Cancel the current render and optionally kill the rt_ipc process.
 */
RENDER_EXPORT void render_ipc_client_cancel(render_ipc_client_t *cli);

/** @brief Register a scanline-delivery callback. */
RENDER_EXPORT void render_ipc_client_on_pixels(render_ipc_client_t *cli,
					       render_pixel_cb cb,
					       void *ud);

/** @brief Register a progress callback. */
RENDER_EXPORT void render_ipc_client_on_progress(render_ipc_client_t *cli,
						 render_progress_cb cb,
						 void *ud);

/** @brief Register a completion callback. */
RENDER_EXPORT void render_ipc_client_on_done(render_ipc_client_t *cli,
					     render_done_cb cb,
					     void *ud);

#endif /* RENDER_HAVE_LIBUV */


__END_DECLS

#endif /* RENDER_H */

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
