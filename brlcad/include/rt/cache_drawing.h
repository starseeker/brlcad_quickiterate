/*              C A C H E _ D R A W I N G . H
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
/** @file rt/cache_drawing.h
 *
 * Public interface to the 5-stage concurrent drawing-data pipeline
 * implemented in librt/cache_drawing.cpp.
 *
 * The pipeline pre-computes per-object drawing data (attributes, AABB,
 * OBB and LoD) in background threads and makes the results available
 * to the main thread via drain_results.
 */

#ifndef RT_CACHE_DRAWING_H
#define RT_CACHE_DRAWING_H

#include "common.h"
#include "vmath.h"
#include "rt/defines.h"
#include "rt/db_instance.h"
#include "bsg/lod.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DrawResult - a completed drawing-data computation posted by a pipeline
 * stage to the main-thread result queue.
 *
 * The type field distinguishes which stage produced this result.
 */
typedef struct DrawResult_s {
    int type;                  /* DRAWRESULT_AABB / OBB / LOD */
    unsigned long long hash;   /* bu_data_hash of the object name */
    char dp_name[512];         /* object name in the .g database */

    /* AABB (type == DRAWRESULT_AABB) */
    point_t bmin;
    point_t bmax;

    /* OBB (type == DRAWRESULT_OBB): 8 corner points from bg_3d_obb */
    int     obb_valid;
    point_t obb_pts[8];

    /* LOD (type == DRAWRESULT_LOD) */
    unsigned long long lod_key;
} DrawResult;

#define DRAWRESULT_AABB 0
#define DRAWRESULT_OBB  1
#define DRAWRESULT_LOD  2

/**
 * Start the 5-thread background pipeline for the given database.
 * Called automatically from db_open(); can also be called manually.
 * Returns BRLCAD_OK on success, BRLCAD_ERROR on failure.
 */
RT_EXPORT extern int db_cache_start(struct db_i *dbip);

/**
 * Stop the pipeline and free all pipeline state.  Block until the
 * shutdown is acknowledged by all worker threads (up to ~2 seconds).
 * Called automatically from db_close().
 */
RT_EXPORT extern void db_cache_stop(struct db_i *dbip);

/**
 * Enqueue a single database object for processing by the pipeline.
 * Safe to call from the main thread at any time after db_cache_start().
 */
RT_EXPORT extern void db_cache_queue_obj(struct db_i *dbip, const char *name);

/**
 * Returns non-zero if all pipeline queues are empty (pipeline has
 * caught up with all submitted work).
 */
RT_EXPORT extern int db_cache_settled(struct db_i *dbip);

/**
 * Update the LoD context used by the lod_worker stage.  Call this when
 * the ged_lod context becomes available (or changes).
 */
RT_EXPORT extern void db_cache_set_lod_ctx(struct db_i *dbip,
					   bsg_mesh_lod_context *lod_ctx);

/**
 * Drain all pending DrawResult notifications into the array pointed to
 * by out_vec (must be a std::vector<DrawResult>* in C++ code).
 * Returns the number of results drained.  MAIN THREAD ONLY.
 *
 * In C++, prefer calling via DrawPipeline::drain() which handles the
 * type cast automatically.
 */
RT_EXPORT extern size_t db_cache_drain_results(struct db_i *dbip,
					       void *out_vec);

#ifdef __cplusplus
}  /* extern "C" */

#  include <string>
#  include <vector>

/**
 * C++ helper: drain all pending DrawResult notifications.
 * Returns the number of results drained.  MAIN THREAD ONLY.
 */
inline size_t
db_cache_drain(struct db_i *dbip, std::vector<DrawResult> &out)
{
    return db_cache_drain_results(dbip, &out);
}

#endif /* __cplusplus */

#endif /* RT_CACHE_DRAWING_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
