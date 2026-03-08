/*             Q G E D _ P I P E L I N E _ T E S T . C P P
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
/** @file qged_pipeline_test.cpp
 *
 * Validates the AABB -> OBB -> LoD progressive BoT drawing pipeline inside
 * a running qged (QgEdApp) instance using software rendering (swrast).
 *
 * GenericTwin.g contains 706 BoT solids.  The DrawPipeline computes:
 *   - AABB (bounding box): populated SYNCHRONOUSLY during "draw" command
 *   - OBB  (oriented bounding box): populated ASYNCHRONOUSLY (drain required)
 *   - LoD  (level of detail):       populated ASYNCHRONOUSLY (drain required)
 *
 * The test verifies this pipeline progression in three stages:
 *
 *   Stage 1 - right after "draw all": AABB placeholder wireframes drawn,
 *              OBBs and LoD not yet drained.
 *              Expected: bboxes >= EXPECTED_BBOXES, obbs == 0 (async pending).
 *
 *   Stage 2 - after processEvents() lets the QgEdApp 100ms drain timer fire.
 *              All three pipeline stages (AABB+OBB+LoD) are drained together.
 *              Expected: bboxes >= EXPECTED_BBOXES, obbs >= EXPECTED_OBBS,
 *                        lod_results_processed() >= EXPECTED_LOD.
 *
 *   Stage 3 - after additional processEvents() passes let the LoD-triggered
 *              bvs->redraw() complete (do_view_changed(QG_VIEW_DRAWN) fires
 *              from drain_background_geom, schedules flush_view_changed_,
 *              which calls bvs->redraw() -> bot_adaptive_plot -> real LoD
 *              view objects with BSG_NODE_MESH_LOD flag set).
 *              Expected: lod_shape_count >= EXPECTED_LOD_SHAPES.
 *
 * The test FAILS if:
 *   - lod_results_processed() == 0 after Stage 2 drain (LoD data never cached)
 *   - lod_shape_count() < EXPECTED_LOD_SHAPES after Stage 3 redraw (OBB
 *     placeholders not replaced by real LoD geometry in the viewport)
 *
 * Usage:
 *   qged_pipeline_test <path/to/GenericTwin.g> [outdir]
 *
 * Returns 0 on pass, non-zero on failure.  Requires a DISPLAY (Xvfb ok).
 */

#include "common.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPixmap>
#include <QTimer>

#include "bu/app.h"
#include "bu/file.h"
#include "bu/log.h"
#include "bu/vls.h"
#include "ged/dbi.h"

#include "qged_pipeline_runner.h"

/* --------------------------------------------------------------------------
 * QgedPipelineRunner implementation
 * -------------------------------------------------------------------------- */

QgedPipelineRunner::QgedPipelineRunner(QgEdApp *app, const char *gfile,
       const QString &outdir, QObject *parent)
    : QObject(parent), m_app(app), m_gfile(gfile), m_outdir(outdir)
{}

int
QgedPipelineRunner::bright_pixels(const QImage &img, int threshold)
{
    int cnt = 0;
    for (int y = 0; y < img.height(); y++) {
const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
for (int x = 0; x < img.width(); x++) {
    QRgb px = row[x];
    if (qMax(qMax(qRed(px), qGreen(px)), qBlue(px)) > threshold)
cnt++;
}
    }
    return cnt;
}

QImage
QgedPipelineRunner::grab_screenshot(const QString &name, bool flush_events)
{
    if (!m_app->w)
return QImage();

    m_app->w->update();

    if (flush_events) {
/* Four processEvents() passes flush the full LoD render chain.
 * Fewer than 4 would leave the viewport render incomplete:
 *   pass 1: QTimer fires drain_background_geom; it detects new LoD results
 *           and calls do_view_changed(QG_VIEW_DRAWN), which queues
 *           flush_view_changed_ via QueuedConnection.
 *   pass 2: flush_view_changed_ runs; bvs->redraw() is called; LoD view
 *           objects created via bot_adaptive_plot(); emits view_update(DRAWN),
 *           itself delivered as a queued signal.
 *   pass 3: view_update slots fire (QgQuadView, etc.);
 *           QgView::need_update() schedules the swrast repaint event.
 *   pass 4: swrast paintGL() executes; LoD geometry written to framebuffer.
 * With only 3 passes, grab() would capture the pre-repaint framebuffer.
 */
QApplication::processEvents();
QApplication::processEvents();
QApplication::processEvents();
QApplication::processEvents();
    }

    QImage img = m_app->w->grab().toImage().convertToFormat(QImage::Format_RGB32);

    QDir().mkpath(m_outdir);
    QString path = m_outdir + "/" + name + ".png";
    img.save(path, "PNG");
    bu_log("  Saved %s (%d x %d, %d bright px)\n",
   path.toLocal8Bit().data(),
   img.width(), img.height(),
   bright_pixels(img));
    return img;
}

void
QgedPipelineRunner::finish(bool pass)
{
    m_pass = pass;
    if (m_poll_timer) {
m_poll_timer->stop();
m_poll_timer->deleteLater();
m_poll_timer = nullptr;
    }
    QApplication::exit(pass ? 0 : 1);
}

/* Helper: get DbiState or nullptr */
static DbiState *
get_dbis(QgEdApp *app)
{
    if (!app || !app->mdl || !app->mdl->gedp)
return nullptr;
    return static_cast<DbiState *>(app->mdl->gedp->dbi_state);
}

void
QgedPipelineRunner::start()
{
    bu_log("QgedPipelineRunner::start -- opening %s\n", m_gfile);

    /* Open the .g file */
    int ret = m_app->load_g_file(m_gfile, false);
    if (ret != BRLCAD_OK) {
bu_log("FAIL: load_g_file(%s) failed (ret=%d)\n", m_gfile, ret);
finish(false);
return;
    }
    bu_log("  load_g_file OK\n");

    if (!m_app->w || !m_app->mdl || !m_app->mdl->gedp) {
bu_log("FAIL: window or gedp not available\n");
finish(false);
return;
    }

    DbiState *dbis = get_dbis(m_app);
    if (!dbis) {
bu_log("FAIL: DbiState not available\n");
finish(false);
return;
    }

    /* Enable LoD on the view */
    struct bu_vls msg = BU_VLS_INIT_ZERO;
    {
const char *av[4] = {"view", "lod", "mesh", "1"};
int r = m_app->run_cmd(&msg, 4, av);
bu_log("  view lod mesh 1 -> ret=%d msg='%s'\n", r, bu_vls_cstr(&msg));
bu_vls_trunc(&msg, 0);
    }

    /* Draw "all" (the top-level comb containing all 706 BoTs) */
    {
const char *av[3] = {"draw", "all", nullptr};
int r = m_app->run_cmd(&msg, 2, av);
bu_log("  draw all -> ret=%d msg='%s'\n", r, bu_vls_cstr(&msg));
bu_vls_trunc(&msg, 0);
    }

    /* Autoview */
    {
const char *av[2] = {"autoview", nullptr};
int r = m_app->run_cmd(&msg, 1, av);
bu_log("  autoview -> ret=%d msg='%s'\n", r, bu_vls_cstr(&msg));
bu_vls_trunc(&msg, 0);
    }
    bu_vls_free(&msg);

    /* ------------------------------------------------------------------
     * Stage 1: immediately after "draw all", BEFORE draining the pipeline.
     *
     * AABB bboxes are populated synchronously during the draw command
     * (via update_dp walking the comb tree).  OBBs and LoD are computed
     * asynchronously by the background DrawPipeline; they will be 0 here
     * UNLESS the pipeline completed before our code ran (hot cache / fast CPU).
     * ------------------------------------------------------------------ */
    bu_log("\n--- Stage 1: post-draw, pre-drain ---\n");
    m_bboxes_before_drain = dbis->bboxes.size();
    m_obbs_before_drain   = dbis->obbs.size();
    bu_log("  Pre-drain  bboxes=%zu  obbs=%zu  lod=%zu\n",
   m_bboxes_before_drain, m_obbs_before_drain,
   dbis->lod_results_processed());

    /* Grab a raw screenshot without flushing events (no drain timer fires) */
    m_app->w->update();
    QImage shot1 = m_app->w->grab().toImage().convertToFormat(QImage::Format_RGB32);
    m_bright_1 = bright_pixels(shot1);
    QDir().mkpath(m_outdir);
    shot1.save(m_outdir + "/pipeline_01_pre_drain.png", "PNG");
    bu_log("  Stage 1 screenshot: %d x %d, %d bright px\n",
   shot1.width(), shot1.height(), m_bright_1);

    /* ------------------------------------------------------------------
     * Drain via processEvents():
     *
     * The QgEdApp drain timer fires every 100ms.  Two processEvents()
     * calls let the timer fire and process queued signals.
     * ------------------------------------------------------------------ */
    QApplication::processEvents();
    QApplication::processEvents();

    /* ------------------------------------------------------------------
     * Stage 2: immediately after initial drain.
     * All three pipeline stages should be reflected in the DbiState counters.
     * ------------------------------------------------------------------ */
    bu_log("\n--- Stage 2: immediately after drain ---\n");
    m_bboxes_after_drain = dbis->bboxes.size();
    m_obbs_after_drain   = dbis->obbs.size();
    m_lod_after_drain    = dbis->lod_results_processed();
    m_drain_after_events = dbis->drain_geom_results(); /* extra manual drain */
    bu_log("  Post-drain bboxes=%zu  obbs=%zu  lod=%zu  manual_drain=%zu\n",
   m_bboxes_after_drain, m_obbs_after_drain,
   m_lod_after_drain, m_drain_after_events);

    /* Stage 2 screenshot (before LoD redraw cycle completes) */
    QImage shot2 = grab_screenshot("pipeline_02_post_drain", false);
    m_bright_2 = bright_pixels(shot2);
    bu_log("  Stage 2 bright px: %d\n", m_bright_2);

    /* ------------------------------------------------------------------
     * Stage 3: poll until pipeline is quiescent, then take final screenshot.
     *
     * After lod_results_processed() advances, drain_background_geom() calls
     * do_view_changed(QG_VIEW_DRAWN) which triggers flush_view_changed_ ->
     * bvs->redraw() -> bot_adaptive_plot creates real LoD view objects.
     * This cycle runs asynchronously; poll until quiescent.
     * ------------------------------------------------------------------ */
    bu_log("\n--- Stage 3: polling for LoD redraw completion ---\n");
    m_stage = STAGE_LOD_WAIT;
    m_poll_count = 0;
    m_total_drain = m_drain_after_events;

    m_poll_timer = new QTimer(this);
    m_poll_timer->setInterval(POLL_INTERVAL_MS);
    connect(m_poll_timer, &QTimer::timeout, this, &QgedPipelineRunner::poll);
    m_poll_timer->start();
}

void
QgedPipelineRunner::poll()
{
    DbiState *dbis = get_dbis(m_app);
    if (!dbis)
return;

    size_t n = dbis->drain_geom_results();
    m_total_drain += n;
    m_poll_count++;

    if (m_poll_count % 40 == 0)
bu_log("  [poll %d] bboxes=%zu  obbs=%zu  lod=%zu  drain=%zu\n",
       m_poll_count,
       dbis->bboxes.size(), dbis->obbs.size(),
       dbis->lod_results_processed(),
       m_total_drain);

    /* Pipeline is complete when:
     *   - OBBs reached expected count (OBB stage done), AND
     *   - no new drain results for 10 consecutive polls (pipeline quiescent) */
    if (dbis->obbs.size() >= EXPECTED_OBBS && n == 0) {
m_quiescent_polls++;
if (m_quiescent_polls >= 10) {
    bu_log("  Pipeline quiescent after %d polls  "
   "lod=%zu  drain=%zu\n",
   m_poll_count,
   dbis->lod_results_processed(),
   m_total_drain);
    m_stage = STAGE_DONE;
    evaluate();
    return;
}
    } else {
m_quiescent_polls = 0;
    }

    if (m_poll_count >= MAX_LOD_POLLS) {
bu_log("WARNING: pipeline poll timed out after %d polls "
       "(bboxes=%zu  obbs=%zu  lod=%zu  drain=%zu)\n",
       m_poll_count,
       dbis->bboxes.size(), dbis->obbs.size(),
       dbis->lod_results_processed(),
       m_total_drain);
m_stage = STAGE_DONE;
evaluate();
    }
}

void
QgedPipelineRunner::evaluate()
{
    DbiState *dbis = get_dbis(m_app);
    if (!dbis) {
bu_log("FAIL: dbi_state not available at evaluate\n");
finish(false);
return;
    }

    /* Take final screenshot with full event-loop flush.
     * Four processEvents() passes let the LoD-triggered redraw chain
     * complete: drain->do_view_changed->flush_view_changed_->bvs->redraw. */
    QImage shot3 = grab_screenshot("pipeline_03_final", true);
    m_bright_3 = bright_pixels(shot3);

    size_t final_bboxes = dbis->bboxes.size();
    size_t final_obbs   = dbis->obbs.size();
    size_t final_lod    = dbis->lod_results_processed();

    /* Count shapes that bot_adaptive_plot replaced with real LoD geometry
     * (BSG_NODE_MESH_LOD flag set in the per-view object).  This is the
     * definitive check that the LoD render transition happened.
     *
     * Use the active view (ged_gvp) so that get_view_state() returns the
     * BViewState that actually owns the shapes drawn into that view.  If
     * ged_gvp is NULL fall back to shared_vs which covers all shared-state
     * views used by QgEdApp in single-view mode. */
    {
struct bview *v = m_app->mdl->gedp->ged_gvp;
BViewState *bvs = v ? dbis->get_view_state(v) : nullptr;
if (!bvs)
    bvs = dbis->shared_vs;
if (bvs && v)
    m_lod_shapes_3 = bvs->lod_shape_count(v);
    }

    bu_log("\n--- Final Evaluation ---\n");
    bu_log("  bboxes before drain: %zu  (stage 1 snapshot)\n", m_bboxes_before_drain);
    bu_log("  obbs   before drain: %zu  (stage 1 snapshot)\n", m_obbs_before_drain);
    bu_log("  bboxes after  drain: %zu  (expected >= %zu)\n",  final_bboxes, EXPECTED_BBOXES);
    bu_log("  obbs   after  drain: %zu  (expected >= %zu)\n",  final_obbs,   EXPECTED_OBBS);
    bu_log("  lod results:         %zu  (expected >= %zu)\n",  final_lod,    EXPECTED_LOD);
    bu_log("  lod shapes rendered: %zu  (expected >= %zu)\n",  m_lod_shapes_3, EXPECTED_LOD_SHAPES);
    bu_log("  bright px: stage1=%d  stage2=%d  stage3=%d\n",
   m_bright_1, m_bright_2, m_bright_3);

    bool pass = true;

    /* Required: AABB bboxes must be populated (sync draw path) */
    if (final_bboxes < EXPECTED_BBOXES) {
bu_log("FAIL: bboxes %zu < expected %zu\n", final_bboxes, EXPECTED_BBOXES);
pass = false;
    } else {
bu_log("PASS: bboxes %zu >= %zu  (AABB stage OK)\n",
       final_bboxes, EXPECTED_BBOXES);
    }

    /* Required: OBBs must be populated after drain */
    if (final_obbs < EXPECTED_OBBS) {
bu_log("FAIL: obbs %zu < expected %zu\n", final_obbs, EXPECTED_OBBS);
pass = false;
    } else {
bu_log("PASS: obbs %zu >= %zu  (OBB stage OK)\n",
       final_obbs, EXPECTED_OBBS);
    }

    /* Required: LoD data must have been generated for the BoTs */
    if (final_lod < EXPECTED_LOD) {
bu_log("FAIL: lod_results_processed %zu < expected %zu "
       "(LoD stage did not run in qged)\n", final_lod, EXPECTED_LOD);
pass = false;
    } else {
bu_log("PASS: lod_results_processed %zu >= %zu  (LoD stage OK)\n",
       final_lod, EXPECTED_LOD);
    }

    /* Required: LoD geometry must have been RENDERED (OBB placeholders
     * replaced by actual LoD view objects via bot_adaptive_plot).
     * This confirms the full pipeline AABB -> OBB -> LoD visual transition. */
    if (m_lod_shapes_3 < EXPECTED_LOD_SHAPES) {
bu_log("FAIL: lod_shape_count %zu < expected %zu "
       "(LoD geometry not rendered -- OBB placeholders still active)\n",
       m_lod_shapes_3, EXPECTED_LOD_SHAPES);
pass = false;
    } else {
bu_log("PASS: lod_shape_count %zu >= %zu  (LoD rendering OK)\n",
       m_lod_shapes_3, EXPECTED_LOD_SHAPES);
    }

    /* Required: viewport must show geometry (bright pixels) */
    if (m_bright_3 <= 0) {
bu_log("FAIL: Stage 3 screenshot is black (swrast rendered nothing)\n");
pass = false;
    } else {
bu_log("PASS: Stage 3 has %d bright pixels (viewport shows geometry)\n",
       m_bright_3);
    }

    /* Required: Stage 1 AABB rendering must show something */
    if (m_bright_1 <= 0) {
bu_log("FAIL: Stage 1 screenshot is black (no AABB placeholder rendering)\n");
pass = false;
    } else {
bu_log("PASS: Stage 1 has %d bright pixels (AABB boxes visible)\n",
       m_bright_1);
    }

    /* Informational: async OBB stage timing */
    if (m_obbs_before_drain == 0) {
bu_log("INFO: OBBs were 0 at Stage 1 -- async OBB pipeline confirmed async\n");
    } else {
bu_log("INFO: OBBs already %zu at Stage 1 -- pipeline completed early "
       "(warm cache / fast CPU)\n", m_obbs_before_drain);
    }

    /* Informational: progressive rendering if screenshots differ */
    if (m_bright_3 != m_bright_2) {
bu_log("INFO: Stage 3 differs from Stage 2 "
       "(stage2=%d  stage3=%d) -- LoD replaced OBB placeholders visually\n",
       m_bright_2, m_bright_3);
    } else {
bu_log("INFO: Stage 3 and Stage 2 have same pixel count -- "
       "LoD geometry visually similar to OBB at current zoom level\n");
    }

    finish(pass);
}

/* --------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------- */
int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);

    if (argc < 2) {
fprintf(stderr, "Usage: %s <GenericTwin.g> [outdir]\n", argv[0]);
return 1;
    }

    const char *gfile  = argv[1];
    const char *outdir = (argc >= 3) ? argv[2] : ".";

    if (!bu_file_exists(gfile, nullptr)) {
fprintf(stderr, "ERROR: %s not found\n", gfile);
return 2;
    }

    /* Local cache */
    char cachedir[MAXPATHLEN] = {0};
    bu_dir(cachedir, MAXPATHLEN, BU_DIR_CURR, "qged_pipeline_cache", nullptr);
    bu_mkdir(cachedir);
    bu_setenv("BU_DIR_CACHE", cachedir, 1);

    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);
    bu_setenv("DM_SWRAST", "1", 1);

    bu_log("qged_pipeline_test: %s\n", gfile);

    /* QgEdApp with no .g file arg (we call load_g_file in start()).
     * qargc=0 so QgEdApp's "if (argc)" block skips automatic file loading.
     * qargv[0]=argv[0] gives Qt the program name for internal use. */
    int   qargc = 0;
    char *qargv[2] = { argv[0], nullptr };
    QgEdApp app(qargc, qargv, 1 /*swrast*/, 0 /*quad*/);

    /* Schedule start() 500 ms after the event loop begins */
    QgedPipelineRunner runner(&app, gfile, QString(outdir));
    QTimer::singleShot(500, &runner, &QgedPipelineRunner::start);

    int ret = app.exec();
    bu_log("qged_pipeline_test: %s\n", (ret == 0) ? "PASSED" : "FAILED");
    return ret;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
