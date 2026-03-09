/*             Q G E D _ P I P E L I N E _ R U N N E R . H
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
/** @file qged_pipeline_runner.h
 *
 * QgedPipelineRunner: Qt QObject that drives the multi-stage AABB->OBB->LoD
 * validation test inside a running QgEdApp event loop.
 */

#ifndef QGED_PIPELINE_RUNNER_H
#define QGED_PIPELINE_RUNNER_H

#include <cstddef>

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include "bu/hash.h"

#include "bsg/defines.h"
#include "bsg/util.h"

#include "ged/dbi.h"
#include "QgEdApp.h"

/**
 * SceneStats
 *
 * Counts of view objects by placeholder type, gathered by
 * inspect_scene_objs().  Now covers BOTH mesh (BoT) and CSG shapes since all
 * primitive types are treated equally under the lazy AABB policy.
 * Used for programmatic pass/fail criteria instead of relying on pixel
 * counting.
 */
struct SceneStats {
    /* mesh (BoT) shape counts */
    int placeholder_aabb = 0;  /**< mesh_obj, draw_data==NULL, s_placeholder==1 */
    int placeholder_obb  = 0;  /**< mesh_obj, draw_data==NULL, s_placeholder==2 */
    int lod_active       = 0;  /**< mesh_obj, draw_data!=NULL (BSG_NODE_MESH_LOD) */
    int no_view_obj      = 0;  /**< mesh_obj, no per-view sub-object yet          */
    int total_mesh       = 0;  /**< total mesh_obj shapes in scene                */
    /* CSG shape counts (newly tracked; all are lazy now) */
    int csg_no_view_obj  = 0;  /**< csg_obj, have_bbox==0, no view obj yet        */
    int csg_placeholder  = 0;  /**< csg_obj, view obj exists but s_placeholder>0  */
    int csg_active       = 0;  /**< csg_obj, view obj exists with real vlist       */
    int total_csg        = 0;  /**< total csg_obj shapes in scene                 */
};

/* Visitor state for inspect_scene_objs(). */
struct _InspectVisitorData {
    bsg_view  *v;
    SceneStats stats;
};

static int
_inspect_visitor(bsg_shape *s, const bsg_traversal_state * /*ts*/, void *ud)
{
    _InspectVisitorData *d = (_InspectVisitorData *)ud;
    if (!s)
	return 0;

    if (s->mesh_obj) {
	d->stats.total_mesh++;

	bsg_shape *vo = bsg_shape_for_view(s, d->v);
	if (!vo) {
	    d->stats.no_view_obj++;
	} else if (vo->draw_data) {
	    d->stats.lod_active++;
	} else if (vo->s_placeholder == 2) {
	    d->stats.placeholder_obb++;
	} else {
	    /* s_placeholder == 1 (AABB) or 0 (untagged old placeholder) */
	    d->stats.placeholder_aabb++;
	}
    } else if (s->csg_obj) {
	d->stats.total_csg++;

	if (!s->have_bbox) {
	    /* Async AABB not yet delivered — no usable view object */
	    d->stats.csg_no_view_obj++;
	} else {
	    bsg_shape *vo = bsg_shape_for_view(s, d->v);
	    if (!vo) {
		d->stats.csg_no_view_obj++;
	    } else if (vo->s_placeholder > 0) {
		d->stats.csg_placeholder++;
	    } else {
		d->stats.csg_active++;
	    }
	}
    }
    return 0;
}

/**
 * inspect_scene_objs -- traverse the scene graph for @p v and count both
 * mesh and CSG view objects by placeholder type (AABB / OBB / LoD / missing).
 */
static inline SceneStats
inspect_scene_objs(bsg_view *v)
{
    _InspectVisitorData d;
    d.v = v;
    bsg_view_traverse(v, _inspect_visitor, &d);
    return d.stats;
}

/**
 * QgedPipelineRunner
 *
 * Drives a multi-stage validation of the DrawPipeline in qged (swrast).
 * The test always starts with a cold LoD cache (the caller wipes BU_DIR_CACHE
 * before calling load_g_file), ensuring the AABB -> OBB -> LoD progression is
 * visible in screenshots rather than jumping straight to LoD.
 *
 * Per-item delays are injected via environment variables before the pipeline
 * starts, simulating geometry that is expensive to process (hard to visualize
 * quickly).  This makes each stage linger long enough to be captured:
 *
 *   BRLCAD_CACHE_OBB_DELAY_MS=5   -- each OBB sleeps 5 ms after computation
 *   BRLCAD_CACHE_LOD_DELAY_MS=200 -- each LoD sleeps 200 ms after computation
 *
 * Pipeline progression for GenericTwin.g (706 BoTs + CSG, cold cache):
 *
 * ALL primitive types (BoT AND CSG) are now lazy: when
 * BRLCAD_CACHE_AABB_DELAY_MS is set, rt_bound_instance is skipped for
 * every leaf solid, and every shape starts with have_bbox=0.  The scene
 * begins completely empty and populates progressively as the async
 * DrawPipeline delivers AABB, OBB, and LoD data.
 *
 *   Stage 0 -- immediately after "draw all", before any drain events:
 *     - ALL shapes (BoT and CSG alike) start with have_bbox=0 and no view
 *       object.  The scene is effectively empty.
 *     - gv_progressive_autoview is set (autoview fired but scene is empty).
 *     - Scene check: (no_view_obj + csg_no_view_obj) == total shapes drawn.
 *     - Note: Stage 0 is a snapshot taken before any drain, as a reference.
 *
 *   Stage 1 -- first AABB placeholders visible (BoT and/or CSG):
 *     - drain_background_geom() fires; new AABBs trigger do_view_changed;
 *       BViewState::redraw() retries all shapes with have_bbox==0;
 *       bot_adaptive_plot / draw_scene late-set have_bbox and draw placeholder.
 *       Progressive autoview re-runs bsg_view_autoview() as bboxes accumulate.
 *     - Scene check: placeholder_aabb > 0 (BoT) or csg_placeholder > 0 (CSG).
 *
 *   Stage 2 -- AABB+OBB mix (most AABBs arrived, first OBBs appearing):
 *     - Scene check: placeholder_obb > 0, lod_active == 0.
 *
 *   Stage 3 -- first LoD objects visible:
 *     - Scene check: lod_active > 0.
 *
 *   Final -- gv_progressive_autoview cleared:
 *     - shapes_without_bbox() == 0 → autoview stabilises.
 *
 * Pass criteria (scene-object based):
 *   Stage 1: (placeholder_aabb > 0 OR csg_placeholder > 0), lod_active == 0
 *   Stage 2: placeholder_obb > 0, lod_active == 0
 *   Stage 3: lod_active > 0
 *   Final:   obbs >= EXPECTED_OBBS, lod_results_processed() >= EXPECTED_LOD
 *
 * NOTE: The architectural direction for a future session is to remove
 * rt_bound_instance from gather_paths entirely so that path-building ONLY
 * walks the comb tree structure, and all leaf data (AABB, OBB, LoD, CSG
 * vlists) flows exclusively through the async DrawPipeline cache mechanism.
 * This will allow large hierarchies to open without any blocking I/O.
 */
class QgedPipelineRunner : public QObject
{
    Q_OBJECT
public:
    explicit QgedPipelineRunner(QgEdApp *app, const char *gfile,
			       const QString &outdir, QObject *parent = nullptr);

    /* Expected DrawPipeline counts for GenericTwin.g */
    static constexpr size_t EXPECTED_BBOXES = 2242;
    static constexpr size_t EXPECTED_OBBS   =  706;
    static constexpr size_t EXPECTED_LOD    =  700;

    /* Polling parameters */
    static constexpr int POLL_INTERVAL_MS   = 50;    /* ms between polls     */
    static constexpr int MAX_POLLS          = 7200;  /* 7200 x 50ms = 360 s  */
    /* Save a periodic mid-stage screenshot every N polls (N*50ms interval) */
    static constexpr int PERIODIC_SNAP_POLLS = 100;  /* every 5 s            */

public slots:
    void start();
    void poll();

private:
    void evaluate();
    static int bright_pixels(const QImage &img, int threshold = 150);
    QImage grab_screenshot(const QString &name);
    void finish(bool pass);

    QgEdApp    *m_app;
    const char *m_gfile;
    QString     m_outdir;

    /*
     * Five-phase poll state machine:
     *   STAGE_AABB_WAIT -- waiting for first AABB placeholder (no_view_obj → aabb)
     *   STAGE_OBB_WAIT  -- waiting for first OBB placeholder (aabb → obb mix)
     *   STAGE_LOD_WAIT  -- waiting for first LoD view object
     *   STAGE_FINAL_WAIT-- waiting for all LoD results
     *   STAGE_DONE      -- evaluate() called
     */
    enum Stage {
	STAGE_AABB_WAIT, STAGE_OBB_WAIT, STAGE_LOD_WAIT,
	STAGE_FINAL_WAIT, STAGE_DONE
    };
    Stage m_stage           = STAGE_AABB_WAIT;
    int   m_poll_count      = 0;
    int   m_next_periodic   = PERIODIC_SNAP_POLLS; /* next periodic screenshot poll */
    int   m_periodic_count  = 0;                   /* sequential number of periodic screenshots */

    /* Scene-stat snapshots */
    SceneStats m_stats_0;   /* Stage 0: pre-drain (reference)               */
    SceneStats m_stats_1;   /* Stage 1: first AABB placeholders             */
    SceneStats m_stats_2;   /* Stage 2: first OBB placeholders              */
    SceneStats m_stats_3;   /* Stage 3: first LoD objects                   */

    size_t m_bboxes_at_draw = 0;
    size_t m_obbs_at_draw   = 0;

    bool m_stage1_pass = false;
    bool m_stage2_pass = false;
    bool m_stage3_pass = false;

    QTimer *m_poll_timer = nullptr;

public:
    bool m_pass = false;
};

#endif /* QGED_PIPELINE_RUNNER_H */

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
