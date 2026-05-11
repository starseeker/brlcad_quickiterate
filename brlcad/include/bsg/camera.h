/*                     C A M E R A . H
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
 * Phase 7 camera/view snapshot API for BSG.
 *
 * bsg_camera_snapshot is a renderer-neutral description of the current
 * view state derived from struct bview.  Renderers and Obol adapters
 * can consume this snapshot without reading dm_impl internals or bview
 * fields directly.
 *
 * Callers produce a snapshot with bsg_camera_snapshot_from_bview() and
 * pass it to renderers; the snapshot is valid for the duration of one
 * frame (or until the view changes).
 */
/** @{ */
/* @file bsg/camera.h */

#ifndef BSG_CAMERA_H
#define BSG_CAMERA_H

#include "common.h"

#include "vmath.h"
#include "bsg/defines.h"

__BEGIN_DECLS

struct bview; /* forward declaration */

/**
 * Projection mode of the camera.
 */
enum bsg_camera_projection {
    BSG_CAMERA_ORTHO        = 0, /**< @brief orthographic (parallel) projection */
    BSG_CAMERA_PERSPECTIVE  = 1  /**< @brief perspective projection */
};

/**
 * Renderer-neutral camera/view snapshot derived from struct bview.
 *
 * All matrix storage uses the same row-major convention as bview/vmath.
 * The snapshot is a plain C struct; copy or embed freely.
 */
struct bsg_camera_snapshot {
    /* Viewport dimensions */
    int    width;           /**< @brief viewport width in pixels */
    int    height;          /**< @brief viewport height in pixels */
    fastf_t aspect;         /**< @brief width / height aspect ratio */

    /* Projection */
    enum bsg_camera_projection projection; /**< @brief ortho or perspective */
    fastf_t perspective_angle; /**< @brief perspective angle in degrees (0 for ortho) */

    /* Matrices (row-major, vmath convention) */
    mat_t model2view;       /**< @brief model-to-view transform */
    mat_t view2model;       /**< @brief view-to-model transform */
    mat_t pmat;             /**< @brief perspective/projection matrix */

    /* Derived eye/look/up/right vectors in model space */
    point_t eye_pos;        /**< @brief eye position in model space */
    vect_t  look_dir;       /**< @brief look direction (unit vector, model space) */
    vect_t  up_dir;         /**< @brief up vector (unit vector, model space) */
    vect_t  right_dir;      /**< @brief right vector (unit vector, model space) */

    /* View scale data needed for CAD overlays */
    fastf_t scale;          /**< @brief gv_scale (half-width of view in model units) */
    fastf_t size;           /**< @brief gv_size  (2 * scale) */
    fastf_t base2local;     /**< @brief unit conversion: base (mm) to local units */
    fastf_t local2base;     /**< @brief unit conversion: local units to base (mm) */

    /* Clip policy */
    int     zclip;          /**< @brief non-zero if z-clipping is enabled */
};


/**
 * Populate @p snap from the current state of @p v.
 *
 * All fields are derived from @p v at call time; no pointer to @p v is
 * retained.  Returns 0 on success, -1 if @p snap or @p v is NULL.
 */
BSG_EXPORT extern int
bsg_camera_snapshot_from_bview(struct bsg_camera_snapshot *snap,
			       const struct bview *v);

/**
 * Initialize @p snap to a canonical identity/default state.
 * Useful before populating individual fields or as a null snapshot.
 * No-op if @p snap is NULL.
 */
BSG_EXPORT extern void
bsg_camera_snapshot_init(struct bsg_camera_snapshot *snap);

__END_DECLS

#endif /* BSG_CAMERA_H */

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
