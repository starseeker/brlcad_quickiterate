/*                 R E N D E R _ P R I V A T E . H
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
/** @file librender/render_private.h
 *
 * Private internal definitions for librender.
 *
 * Not part of the public API; do not include from outside src/librender/.
 */

#ifndef LIBRENDER_PRIVATE_H
#define LIBRENDER_PRIVATE_H

#include "common.h"

#include "bu/parallel.h"   /* MAX_PSW */
#include "vmath.h"
#include "raytrace.h"
#include "optical.h"
#include "optical/shadefuncs.h"  /* struct mfuncs */

#include "render.h"


/* ------------------------------------------------------------------ */
/* render_ctx (opaque in public header)                                 */
/* ------------------------------------------------------------------ */

struct render_ctx {
    struct rt_i    *rtip;    /**< @brief Loaded+prepped raytrace instance. */
    char           *dbfile;  /**< @brief Copy of the database file path. */
    struct mfuncs  *mfHead;  /**< @brief liboptical shader list. */
    struct resource *res;    /**< @brief Per-CPU ray resources [nres]. */
    int             nres;    /**< @brief Size of res[] array (== MAX_PSW). */
};


/* ------------------------------------------------------------------ */
/* render_opts (opaque in public header)                                */
/* ------------------------------------------------------------------ */

struct render_opts {
    int    width;          /**< @brief Image width in pixels. */
    int    height;         /**< @brief Image height in pixels. */
    mat_t  view2model;     /**< @brief 4x4 view-to-model matrix. */
    point_t eye_model;     /**< @brief Eye position in model coords. */
    double viewsize;       /**< @brief View diameter (model units, mm). */
    double aspect;         /**< @brief Pixel aspect ratio (W/H). */
    int    lightmodel;     /**< @brief RENDER_LIGHT_* constant. */
    int    nthreads;       /**< @brief Worker thread count (0 = all CPUs). */
    double background[3];  /**< @brief Background RGB, 0.0–1.0. */
    double perspective_deg;/**< @brief Perspective FOV (0 = orthographic). */
    int    view_set;       /**< @brief Non-zero if view params were set. */
};


/* ------------------------------------------------------------------ */
/* Shared render state passed through ap->a_uptr during a render       */
/* ------------------------------------------------------------------ */

struct render_state {
    const struct render_ctx  *ctx;
    const struct render_opts *opts;

    /* view grid vectors (precomputed in render_run) */
    point_t viewbase_model; /**< @brief Lower-left pixel centre in model space. */
    vect_t  dx_model;       /**< @brief Model-space step for one pixel right. */
    vect_t  dy_model;       /**< @brief Model-space step for one pixel up. */
    vect_t  ortho_dir;      /**< @brief Uniform ray dir for orthographic mode. */

    /* output state (written by hit/miss callbacks) */
    render_pixel_cb    pxcb;
    render_progress_cb progcb;
    render_done_cb     donecb;
    void              *ud;

    /* scanline buffer (one per thread, or shared if single-threaded) */
    unsigned char    **scanbufs; /**< @brief [nthreads][width*3] RGB buffers. */
    int                nthreads;

    /* progress counters (protected by BU_SEM_GENERAL) */
    int                scanlines_done;
    int                scanlines_total;
};


#endif /* LIBRENDER_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
