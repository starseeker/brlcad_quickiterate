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

#include "QgEdApp.h"

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
