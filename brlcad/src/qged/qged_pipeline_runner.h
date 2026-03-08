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
 * QgedPipelineRunner: Qt QObject that drives the multi-stage AABB→OBB→LoD
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
 *
 * The test:
 *   1. Calls draw "all" on GenericTwin.g (706 BoTs).
 *   2. Records DbiState::bboxes/obbs BEFORE calling processEvents() —
 *      capturing the pre-drain pipeline state.
 *   3. Calls processEvents() to let the QgEdApp drain timer fire.
 *   4. Records DbiState::bboxes/obbs + lod_results_processed() AFTER drain.
 *   5. Polls until pipeline is quiescent (no new results for 10 cycles).
 *   6. Takes a final screenshot and evaluates pass/fail.
 *
 * Pass criteria:
 *   - bboxes ≥ EXPECTED_BBOXES (AABB stage populated)
 *   - obbs   ≥ EXPECTED_OBBS   (OBB stage populated after drain)
 *   - lod_results_processed() ≥ EXPECTED_LOD (LoD stage ran for all BoTs)
 *   - Stage 1 screenshot has bright pixels (AABB boxes rendered)
 *   - Stage 3 screenshot has bright pixels (real/LoD geometry rendered)
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
    /* At least 1 LoD result per BoT (703/706 expected from warm-cache path) */
    static constexpr size_t EXPECTED_LOD        =  700;
    /* At least this many shapes should have real LoD view objects (BSG_NODE_MESH_LOD)
     * after the LoD redraw.  All 706 BoTs should get real geometry; allow a
     * small tolerance for edge cases. */
    static constexpr size_t EXPECTED_LOD_SHAPES =  700;

    /* Polling parameters */
    static constexpr int POLL_INTERVAL_MS  =   50;
    static constexpr int MAX_LOD_POLLS     = 2400;  /* 2400 × 50 ms = 120 s */

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
     *  If flush_events is true, calls processEvents() before grabbing. */
    QImage grab_screenshot(const QString &name, bool flush_events = true);

    /** Stop the poll timer and call QApplication::exit(). */
    void finish(bool pass);

    QgEdApp    *m_app;
    const char *m_gfile;
    QString     m_outdir;

    /* Poll state */
    enum Stage { STAGE_INIT, STAGE_LOD_WAIT, STAGE_DONE };
    Stage  m_stage           = STAGE_INIT;
    int    m_poll_count      = 0;
    int    m_quiescent_polls = 0;
    size_t m_total_drain     = 0;
    size_t m_drain_after_events = 0;

    /* DbiState snapshots */
    size_t m_bboxes_before_drain = 0;
    size_t m_obbs_before_drain   = 0;
    size_t m_bboxes_after_drain  = 0;
    size_t m_obbs_after_drain    = 0;
    size_t m_lod_after_drain     = 0;  /* lod_results_processed() after drain */
    size_t m_lod_shapes_3        = 0;  /* lod_shape_count() at Stage 3 final  */

    /* Per-stage bright-pixel counts */
    int m_bright_1 = -1;   /* Stage 1: pre-drain screenshot            */
    int m_bright_2 = -1;   /* Stage 2: post-drain screenshot           */
    int m_bright_3 = -1;   /* Stage 3: final screenshot (LoD rendered) */

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
