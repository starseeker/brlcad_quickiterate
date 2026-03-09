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
 * Counts of mesh (BoT) view objects by placeholder type, gathered by
 * inspect_scene_objs().  Used for programmatic pass/fail criteria instead
 * of relying on image pixel counting.
 */
struct SceneStats {
    int placeholder_aabb = 0;  /**< mesh_obj, draw_data==NULL, s_placeholder==1 */
    int placeholder_obb  = 0;  /**< mesh_obj, draw_data==NULL, s_placeholder==2 */
    int lod_active       = 0;  /**< mesh_obj, draw_data!=NULL (BSG_NODE_MESH_LOD) */
    int no_view_obj      = 0;  /**< mesh_obj, no per-view sub-object yet          */
    int total_mesh       = 0;  /**< total mesh_obj shapes in scene                */
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
    if (!s || !s->mesh_obj)
	return 0;

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
    return 0;
}

/**
 * inspect_scene_objs -- traverse the scene graph for @p v and count mesh
 * view objects by placeholder type (AABB / OBB / LoD / missing).
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
 * Drives a three-stage validation of the DrawPipeline in qged (swrast).
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
 * Pipeline progression for GenericTwin.g (706 BoTs, cold cache):
 *
 *   Stage 1 -- immediately after "draw all", before any drain events:
 *     - bot_adaptive_plot sees key==0 and obbs empty: draws AABB wireframes.
 *     - Scene check: placeholder_aabb > 0, placeholder_obb == 0, lod_active == 0.
 *
 *   Stage 2 -- first drain batch where some OBBs have arrived but no LoD yet:
 *     - drain_background_geom() detects new OBBs, calls stale_mesh_shapes_for_dp(),
 *       do_view_changed(QG_VIEW_DRAWN) triggers bvs->redraw().
 *     - The missing-view-obj scan in redraw() + bot_adaptive_plot() upgrades
 *       placeholders: OBB wireframes replace AABB boxes as OBBs arrive.
 *     - Bot_adaptive_plot AABB→OBB in-place upgrade fires for any shapes still
 *       holding an AABB placeholder when called during redraw.
 *     - LoD delay (200 ms/item) ensures LoD has not yet arrived.
 *     - Scene check: placeholder_obb > 0, lod_active == 0.
 *
 *   Stage 3 -- first drain batch where some LoD has arrived:
 *     - stale_mesh_shapes_for_dp() clears OBB placeholders; redraw() scan +
 *       bot_adaptive_plot() creates BSG_NODE_MESH_LOD view objects.
 *     - Scene check: lod_active > 0.
 *
 * Pass criteria (scene-object based, not pixel-count based):
 *   Stage 1: placeholder_aabb > 0,  placeholder_obb == 0, lod_active == 0
 *   Stage 2: placeholder_obb  > 0,  lod_active == 0
 *   Stage 3: lod_active > 0
 *   Final:   obbs   >= EXPECTED_OBBS  (all OBBs processed by pipeline)
 *            lod_results_processed() >= EXPECTED_LOD (all LoD results processed)
 *
 * Screenshots are still saved for visual reference (pipeline_01_aabb.png, etc.)
 * but are not used for pass/fail.
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
    /* At least this many LoD results expected (703/706 warm; allow tolerance) */
    static constexpr size_t EXPECTED_LOD    =  700;

    /* Polling parameters */
    static constexpr int POLL_INTERVAL_MS = 50;    /* ms between polls        */
    static constexpr int MAX_POLLS        = 4800;  /* 4800 x 50 ms = 240 s   */

public slots:
    /** Called once by QTimer::singleShot after event loop startup. */
    void start();

    /** Called every POLL_INTERVAL_MS by m_poll_timer. */
    void poll();

private:
    /** Evaluate final pass/fail criteria and call finish(). */
    void evaluate();

    /** Count pixels where max(R,G,B) > threshold (default 150). */
    static int bright_pixels(const QImage &img, int threshold = 150);

    /** Grab the main window and save to outdir/<name>.png.
     *  Calls processEvents() once to flush the render chain. */
    QImage grab_screenshot(const QString &name);

    /** Stop the poll timer and call QApplication::exit(). */
    void finish(bool pass);

    QgEdApp    *m_app;
    const char *m_gfile;
    QString     m_outdir;

    /*
     * Four-phase poll state machine:
     *   STAGE_OBB_WAIT  -- waiting for first OBB placeholder to appear in scene
     *   STAGE_LOD_WAIT  -- waiting for first LoD view object to appear in scene
     *   STAGE_FINAL_WAIT-- waiting for all LoD results to be processed
     *   STAGE_DONE      -- evaluate() called, finishing
     */
    enum Stage { STAGE_OBB_WAIT, STAGE_LOD_WAIT, STAGE_FINAL_WAIT, STAGE_DONE };
    Stage m_stage      = STAGE_OBB_WAIT;
    int   m_poll_count = 0;

    /* Scene-stat snapshots at each stage */
    SceneStats m_stats_1;   /* Stage 1: AABB placeholders                   */
    SceneStats m_stats_2;   /* Stage 2: OBB placeholders (no LoD yet)       */
    SceneStats m_stats_3;   /* Stage 3: first LoD objects present           */

    /* DbiState snapshots */
    size_t m_bboxes_at_draw = 0;
    size_t m_obbs_at_draw   = 0;

    /* Per-stage pass flags */
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


/**
 * QgedPipelineRunner
 *
 * Drives a three-stage validation of the DrawPipeline in qged (swrast).
 * The test always starts with a cold LoD cache (the caller wipes BU_DIR_CACHE
 * before calling load_g_file), ensuring the AABB -> OBB -> LoD progression is
 * visible in screenshots rather than jumping straight to LoD.
 *
 * Pipeline overview for GenericTwin.g (706 BoTs):
 *
 *   Stage 1 -- immediately after "draw all", no events flushed:
 *     - Cold cache: bot_adaptive_plot sees bsg_mesh_lod_key_get()==0, draws
 *       AABB wireframe placeholder for every BoT.
 *     - Screenshot: AABB bounding boxes.
 *
 *   Stage 2 -- after all 706 OBBs arrive but before LoD data:
 *     - drain_background_geom() triggers do_view_changed(QG_VIEW_DRAWN) when
 *       dbis->obbs.size() advances (new OBB tracking added in this session).
 *     - bvs->redraw() -> bot_adaptive_plot sees OBBs in dbis->obbs, key==0
 *       (LoD not yet ready) -> draws OBB wireframe (tighter than AABB).
 *     - Screenshot: OBB wireframe placeholders.
 *     - BRLCAD_CACHE_LOD_DELAY_MS=5 ensures ~3.5s window (706x5ms) before LoD.
 *
 *   Stage 3 -- after all LoD data arrives and bvs->redraw() completes:
 *     - stale_mesh_shapes_for_dp() cleared stale OBB placeholders.
 *     - bvs->redraw() -> bot_adaptive_plot finds key!=0 -> creates real
 *       BSG_NODE_MESH_LOD view objects.
 *     - Screenshot: LoD triangle mesh geometry.
 *
 * Pass criteria:
 *   - bboxes >= EXPECTED_BBOXES (AABB stage populated synchronously)
 *   - obbs   >= EXPECTED_OBBS   (OBB stage populated, all 706 BoTs)
 *   - lod_results_processed() >= EXPECTED_LOD (LoD stage ran)
 *   - lod_shape_count() >= EXPECTED_LOD_SHAPES (LoD geometry rendered)
 *   - Stage 1 screenshot has bright pixels (AABB boxes visible)
 *   - Stage 3 screenshot has bright pixels (LoD geometry visible)
 */
class QgedPipelineRunner : public QObject
{
    Q_OBJECT
public:
    explicit QgedPipelineRunner(QgEdApp *app, const char *gfile,
			       const QString &outdir, QObject *parent = nullptr);

    /* Expected DrawPipeline counts for GenericTwin.g */
    static constexpr size_t EXPECTED_BBOXES     = 2242;
    static constexpr size_t EXPECTED_OBBS       =  706;
    /* At least this many LoD results expected (703/706 warm; allow tolerance) */
    static constexpr size_t EXPECTED_LOD        =  700;
    /* lod_shape_count() result is informational -- not a hard pass criterion
     * (see evaluate() comment for why).  Kept for logging purposes. */

    /* Polling parameters */
    static constexpr int POLL_INTERVAL_MS  =   50;
    static constexpr int MAX_LOD_POLLS     = 2400;  /* 2400 x 50 ms = 120 s */

    /* Number of consecutive quiescent polls (n==0) required to declare
     * each stage complete.  Using 3 gives 150ms of silence before advancing,
     * long enough for any pending drain_background_geom batch to arrive. */
    static constexpr int QUIESCENT_POLLS_REQUIRED = 3;

public slots:
    /** Called once by QTimer::singleShot after event loop startup. */
    void start();

    /** Called every POLL_INTERVAL_MS by m_poll_timer. */
    void poll();

private:
    /** Evaluate final pass/fail criteria and call finish(). */
    void evaluate();

    /** Count pixels where max(R,G,B) > threshold (default 150). */
    static int bright_pixels(const QImage &img, int threshold = 150);

    /** Grab the main window and save to outdir/<name>.png.
     *  If flush_events is true, calls processEvents() x4 to flush the
     *  full render chain before grabbing (drain->redraw->repaint->paint). */
    QImage grab_screenshot(const QString &name, bool flush_events = true);

    /** Stop the poll timer and call QApplication::exit(). */
    void finish(bool pass);

    QgEdApp    *m_app;
    const char *m_gfile;
    QString     m_outdir;

    /* Three-phase poll state machine:
     *   STAGE_OBB_WAIT  -- waiting for all 706 OBBs to arrive (Stage 2 screenshot)
     *   STAGE_LOD_WAIT  -- waiting for all LoD data (>= EXPECTED_LOD results)
     *                       + quiescence (Stage 3 screenshot)
     *   STAGE_DONE      -- evaluate() called, finishing
     */
    enum Stage { STAGE_OBB_WAIT, STAGE_LOD_WAIT, STAGE_DONE };
    Stage  m_stage           = STAGE_OBB_WAIT;
    int    m_poll_count      = 0;
    int    m_quiescent_polls = 0;
    size_t m_total_drain     = 0;

    /* DbiState snapshots */
    size_t m_bboxes_at_draw  = 0;  /* obbs/lod at Stage 1 (after draw, before drain) */
    size_t m_obbs_at_draw    = 0;
    size_t m_lod_shapes_3    = 0;  /* lod_shape_count() at Stage 3 final              */

    /* Per-stage bright-pixel counts */
    int m_bright_1 = -1;   /* Stage 1: AABB wireframes                 */
    int m_bright_2 = -1;   /* Stage 2: OBB wireframes                  */
    int m_bright_3 = -1;   /* Stage 3: LoD triangle mesh               */

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
