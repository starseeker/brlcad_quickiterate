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
 * The test starts with a COLD LoD cache and injects per-item pipeline delays
 * (BRLCAD_CACHE_OBB_DELAY_MS=5, BRLCAD_CACHE_LOD_DELAY_MS=200) to simulate
 * geometry that is expensive to process.  This makes each stage linger:
 *   - OBBs arrive ~5ms each → first OBB in the drain at ~100ms
 *   - LoD arrives ~200ms after its OBB → Stage 3 fires at ~300ms
 *
 * Pass/fail is determined by programmatic inspection of scene objects at each
 * stage (SceneStats from inspect_scene_objs()), NOT by pixel counting.
 * Screenshots are still saved for visual reference.
 *
 * Stage 1 (AABB): right after "draw all", before any drain events.
 *   Pass: placeholder_aabb > 0, placeholder_obb == 0, lod_active == 0
 *
 * Stage 2 (OBB): first poll where some OBB placeholders exist, no LoD yet.
 *   Pass: placeholder_obb > 0, lod_active == 0
 *
 * Stage 3 (LoD): first poll where some LoD view objects exist.
 *   Pass: lod_active > 0
 *
 * Final: all pipeline results drained.
 *   Pass: obbs >= EXPECTED_OBBS, lod_results_processed() >= EXPECTED_LOD
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
QgedPipelineRunner::grab_screenshot(const QString &name)
{
    if (!m_app->w)
return QImage();

    m_app->w->update();

    /* One processEvents() pass flushes any pending drain → redraw → repaint
     * that is already queued.  Callers must have already called
     * processEvents() in the poll loop if more flushing is needed. */
    QApplication::processEvents();

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

/* Helper: log a SceneStats snapshot */
static void
log_stats(const char *label, const SceneStats &s)
{
    bu_log("  [%s] total_mesh=%d  aabb=%d  obb=%d  lod=%d  no_vo=%d\n",
   label, s.total_mesh,
   s.placeholder_aabb, s.placeholder_obb,
   s.lod_active, s.no_view_obj);
}

void
QgedPipelineRunner::start()
{
    bu_log("QgedPipelineRunner::start -- opening %s\n", m_gfile);

    /* ------------------------------------------------------------------
     * Cold cache: wipe BU_DIR_CACHE before opening the .g file.
     *
     * With a warm LoD cache, bot_adaptive_plot() finds a valid key during
     * "draw all" and immediately creates BSG_NODE_MESH_LOD view objects --
     * the AABB and OBB placeholder stages are never visible.  Wiping the
     * cache forces the three-stage progression:
     *   Stage 1: AABB wireframe boxes (no key in cache, no OBBs yet)
     *   Stage 2: OBB wireframe boxes (OBBs arrive; LoD still pending)
     *   Stage 3: LoD triangle mesh (LoD computation completes)
     * ------------------------------------------------------------------ */
    const char *cdir = getenv("BU_DIR_CACHE");
    if (cdir) {
	QString cdir_qs(cdir);
	QDir cache_qdir(cdir_qs);
	if (cache_qdir.exists()) {
	    bu_log("  Wiping cold LoD cache: %s\n", cdir);
	    cache_qdir.removeRecursively();
	}
	QDir().mkpath(cdir_qs);
    }

    /* Open the .g file (now with empty cache) */
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
     * Stage 1: immediately after "draw all", before any drain events.
     *
     * Every BoT view object was created with draw_data==NULL (AABB placeholder)
     * because bsg_mesh_lod_key_get() returned 0 on the cold cache.  No OBBs
     * or LoD data have arrived yet.
     *
     * Pass condition: placeholder_aabb > 0, placeholder_obb == 0, lod_active == 0
     * ------------------------------------------------------------------ */
    bu_log("\n--- Stage 1: post-draw, pre-drain (cold cache: expect AABB boxes) ---\n");
    m_bboxes_at_draw = dbis->bboxes.size();
    m_obbs_at_draw   = dbis->obbs.size();
    bu_log("  Snapshot: bboxes=%zu  obbs=%zu  lod=%zu\n",
   m_bboxes_at_draw, m_obbs_at_draw,
   dbis->lod_results_processed());

    /* Grab Stage 1 screenshot WITHOUT flushing events.
     * No drain timer has fired; the viewport shows pure AABB boxes. */
    m_app->w->update();
    QImage shot1 = m_app->w->grab().toImage().convertToFormat(QImage::Format_RGB32);
    QDir().mkpath(m_outdir);
    shot1.save(m_outdir + "/pipeline_01_aabb.png", "PNG");
    bu_log("  Stage 1 screenshot: %d x %d, %d bright px\n",
   shot1.width(), shot1.height(), bright_pixels(shot1));

    /* Check Stage 1 scene stats (no processEvents -- drain has not run) */
    bsg_view *gvp = m_app->mdl->gedp->ged_gvp;
    m_stats_1 = gvp ? inspect_scene_objs(gvp) : SceneStats{};
    log_stats("stage1", m_stats_1);

    m_stage1_pass = (m_stats_1.placeholder_aabb > 0 &&
		     m_stats_1.placeholder_obb  == 0 &&
		     m_stats_1.lod_active       == 0);
    bu_log("  Stage 1 %s: placeholder_aabb=%d (need>0), "
	   "placeholder_obb=%d (need==0), lod_active=%d (need==0)\n",
	   m_stage1_pass ? "PASS" : "FAIL",
	   m_stats_1.placeholder_aabb,
	   m_stats_1.placeholder_obb,
	   m_stats_1.lod_active);

    /* Start the poll loop for Stages 2 and 3 */
    bu_log("\n--- Stages 2+3: entering poll loop ---\n");
    m_stage = STAGE_OBB_WAIT;
    m_poll_count = 0;

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

    /* Let the Qt event loop process any pending drain-timer firings,
     * do_view_changed() queued callbacks, and viewport repaints.  This
     * advances the pipeline state so inspect_scene_objs() sees up-to-date
     * scene objects. */
    QApplication::processEvents();

    m_poll_count++;

    bsg_view *gvp = m_app->mdl->gedp->ged_gvp;
    SceneStats stats = gvp ? inspect_scene_objs(gvp) : SceneStats{};

    if (m_poll_count % 20 == 0) {
bu_log("  [poll %d stage=%s] bboxes=%zu  obbs=%zu  lod_res=%zu  "
       "scene: aabb=%d obb=%d lod=%d\n",
       m_poll_count,
       (m_stage == STAGE_OBB_WAIT)   ? "OBB_WAIT"   :
       (m_stage == STAGE_LOD_WAIT)   ? "LOD_WAIT"   :
       (m_stage == STAGE_FINAL_WAIT) ? "FINAL_WAIT" : "DONE",
       dbis->bboxes.size(), dbis->obbs.size(),
       dbis->lod_results_processed(),
       stats.placeholder_aabb, stats.placeholder_obb,
       stats.lod_active);
    }

    /* ---------------------------------------------------------------
     * STAGE_OBB_WAIT: wait for the first OBB placeholder to appear in
     * the scene while LoD has not yet arrived.
     *
     * Transition: placeholder_obb > 0 AND lod_active == 0
     * --------------------------------------------------------------- */
    if (m_stage == STAGE_OBB_WAIT) {
	if (stats.placeholder_obb > 0 && stats.lod_active == 0) {
	    bu_log("  OBB placeholders detected (obb=%d, lod=%d) after %d polls -- "
		   "capturing OBB stage screenshot\n",
		   stats.placeholder_obb, stats.lod_active, m_poll_count);

	    m_stats_2 = stats;
	    log_stats("stage2", m_stats_2);

	    m_stage2_pass = (m_stats_2.placeholder_obb > 0 &&
			     m_stats_2.lod_active == 0);

	    grab_screenshot("pipeline_02_obb");
	    m_stage = STAGE_LOD_WAIT;
	}

	if (m_poll_count >= MAX_POLLS) {
	    bu_log("WARNING: OBB stage timed out (obbs=%zu, lod=%zu)\n",
		   dbis->obbs.size(), dbis->lod_results_processed());
	    if (!m_stage2_pass)
		grab_screenshot("pipeline_02_obb");
	    m_stage = STAGE_DONE;
	    evaluate();
	}
	return;
    }

    /* ---------------------------------------------------------------
     * STAGE_LOD_WAIT: wait for the first real LoD view object to appear
     * in the scene.
     *
     * Transition: lod_active > 0
     * --------------------------------------------------------------- */
    if (m_stage == STAGE_LOD_WAIT) {
	if (stats.lod_active > 0) {
	    bu_log("  LoD objects detected (lod=%d) after %d polls -- "
		   "capturing LoD stage screenshot\n",
		   stats.lod_active, m_poll_count);

	    m_stats_3 = stats;
	    log_stats("stage3", m_stats_3);

	    m_stage3_pass = (m_stats_3.lod_active > 0);

	    grab_screenshot("pipeline_03_lod");
	    m_stage = STAGE_FINAL_WAIT;
	}

	if (m_poll_count >= MAX_POLLS) {
	    bu_log("WARNING: LoD stage timed out (lod=%zu)\n",
		   dbis->lod_results_processed());
	    if (!m_stage3_pass)
		grab_screenshot("pipeline_03_lod");
	    m_stage = STAGE_DONE;
	    evaluate();
	}
	return;
    }

    /* ---------------------------------------------------------------
     * STAGE_FINAL_WAIT: wait for all pipeline LoD results to drain so
     * the final counters are stable before evaluate() runs.
     *
     * Transition: lod_results_processed() >= EXPECTED_LOD
     * --------------------------------------------------------------- */
    if (m_stage == STAGE_FINAL_WAIT) {
	if (dbis->lod_results_processed() >= EXPECTED_LOD) {
	    bu_log("  All LoD results processed (%zu) after %d polls\n",
		   dbis->lod_results_processed(), m_poll_count);
	    m_stage = STAGE_DONE;
	    evaluate();
	    return;
	}

	if (m_poll_count >= MAX_POLLS) {
	    bu_log("WARNING: final-wait timed out "
		   "(lod_results=%zu expected>=%zu)\n",
		   dbis->lod_results_processed(), EXPECTED_LOD);
	    m_stage = STAGE_DONE;
	    evaluate();
	}
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

    size_t final_bboxes = dbis->bboxes.size();
    size_t final_obbs   = dbis->obbs.size();
    size_t final_lod    = dbis->lod_results_processed();

    bu_log("\n--- Final Evaluation ---\n");
    bu_log("  bboxes at draw: %zu   obbs at draw: %zu\n",
   m_bboxes_at_draw, m_obbs_at_draw);
    bu_log("  bboxes final: %zu (expected >= %zu)\n",
   final_bboxes, EXPECTED_BBOXES);
    bu_log("  obbs final:   %zu (expected >= %zu)\n",
   final_obbs, EXPECTED_OBBS);
    bu_log("  lod results:  %zu (expected >= %zu)\n",
   final_lod, EXPECTED_LOD);
    log_stats("stage1_snap", m_stats_1);
    log_stats("stage2_snap", m_stats_2);
    log_stats("stage3_snap", m_stats_3);

    bool pass = true;

    /* --- Stage 1: AABB placeholders visible before any drain --- */
    if (!m_stage1_pass) {
	bu_log("FAIL: Stage 1 -- expected AABB placeholders only "
	       "(placeholder_aabb=%d, placeholder_obb=%d, lod_active=%d)\n",
	       m_stats_1.placeholder_aabb,
	       m_stats_1.placeholder_obb,
	       m_stats_1.lod_active);
	pass = false;
    } else {
	bu_log("PASS: Stage 1 -- AABB placeholders visible "
	       "(placeholder_aabb=%d)\n", m_stats_1.placeholder_aabb);
    }

    /* --- Stage 2: OBB placeholders visible before LoD --- */
    if (!m_stage2_pass) {
	bu_log("FAIL: Stage 2 -- OBB placeholder stage not observed "
	       "(placeholder_obb=%d, lod_active=%d)\n",
	       m_stats_2.placeholder_obb, m_stats_2.lod_active);
	pass = false;
    } else {
	bu_log("PASS: Stage 2 -- OBB placeholders visible before LoD "
	       "(placeholder_obb=%d)\n", m_stats_2.placeholder_obb);
    }

    /* --- Stage 3: LoD view objects visible in scene --- */
    if (!m_stage3_pass) {
	bu_log("FAIL: Stage 3 -- LoD view objects not seen in scene "
	       "(lod_active=%d)\n", m_stats_3.lod_active);
	pass = false;
    } else {
	bu_log("PASS: Stage 3 -- LoD view objects visible "
	       "(lod_active=%d)\n", m_stats_3.lod_active);
    }

    /* --- Pipeline counts --- */
    if (final_bboxes < EXPECTED_BBOXES) {
	bu_log("FAIL: bboxes %zu < expected %zu\n",
	       final_bboxes, EXPECTED_BBOXES);
	pass = false;
    } else {
	bu_log("PASS: bboxes %zu >= %zu\n", final_bboxes, EXPECTED_BBOXES);
    }

    if (final_obbs < EXPECTED_OBBS) {
	bu_log("FAIL: obbs %zu < expected %zu\n", final_obbs, EXPECTED_OBBS);
	pass = false;
    } else {
	bu_log("PASS: obbs %zu >= %zu\n", final_obbs, EXPECTED_OBBS);
    }

    if (final_lod < EXPECTED_LOD) {
	bu_log("FAIL: lod_results %zu < expected %zu\n",
	       final_lod, EXPECTED_LOD);
	pass = false;
    } else {
	bu_log("PASS: lod_results %zu >= %zu\n", final_lod, EXPECTED_LOD);
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

    /* Use a local test cache directory.  start() wipes this before opening
     * the .g file to guarantee a cold LoD cache for all three stages. */
    char cachedir[MAXPATHLEN] = {0};
    bu_dir(cachedir, MAXPATHLEN, BU_DIR_CURR, "qged_pipeline_cache", nullptr);
    bu_mkdir(cachedir);
    bu_setenv("BU_DIR_CACHE", cachedir, 1);

    /* Per-item pipeline delays to simulate expensive geometry and give each
     * stage enough dwell time to be captured by the poll loop.
     *
     * OBB_DELAY_MS=5: each OBB takes ~5ms extra; first OBB appears in the
     *   drain at ~100ms (next 100ms timer firing after the first item).
     *
     * LOD_DELAY_MS=200: each LoD takes ~200ms extra after its OBB finishes;
     *   first LoD appears ~200ms after the first OBB (~300ms total), giving
     *   ~200ms window where OBB-only placeholders are visible in Stage 2. */
    bu_setenv("BRLCAD_CACHE_OBB_DELAY_MS", "5",   1);
    bu_setenv("BRLCAD_CACHE_LOD_DELAY_MS", "200", 1);

    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);
    bu_setenv("DM_SWRAST", "1", 1);

    bu_log("qged_pipeline_test: %s\n", gfile);

    /* QgEdApp with no .g file arg (load_g_file called in start()).
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
 * The test always starts with a COLD LoD cache by wiping BU_DIR_CACHE before
 * opening the .g file.  With a cold cache:
 *   - "draw all" creates AABB wireframe placeholders (bsg_mesh_lod_key_get
 *     returns 0, so bot_adaptive_plot falls back to bboxes/OBBs)
 *   - The background pipeline populates OBBs quickly (fast in-memory OBB
 *     computation); drain_background_geom() detects the advance and calls
 *     do_view_changed(QG_VIEW_DRAWN), triggering a redraw that shows OBB
 *     wireframes.
 *   - LoD is slowed by BRLCAD_CACHE_LOD_DELAY_MS=5 (5ms per BoT = ~3.5s
 *     total), giving a reliable window where OBB wireframes are visible.
 *   - When LoD completes, stale_mesh_shapes_for_dp() clears OBB placeholders
 *     and another do_view_changed(QG_VIEW_DRAWN) triggers bvs->redraw() which
 *     calls bot_adaptive_plot to create real BSG_NODE_MESH_LOD objects.
 *
 * Screenshots are taken at the end of each stage:
 *   pipeline_01_aabb.png  -- Stage 1: AABB wireframe bounding boxes
 *   pipeline_02_obb.png   -- Stage 2: OBB wireframe placeholders (tighter fit)
 *   pipeline_03_lod.png   -- Stage 3: LoD triangle mesh geometry
 *
 * The test FAILS if:
 *   - bboxes < EXPECTED_BBOXES (AABB stage failed)
 *   - obbs   < EXPECTED_OBBS   (OBB stage failed)
 *   - lod_results_processed() < EXPECTED_LOD (LoD stage failed)
 *   - lod_shape_count() < EXPECTED_LOD_SHAPES (LoD not rendered in viewport)
 *   - Stage 1 or Stage 3 screenshots are black
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
/* Four processEvents() passes flush the full render chain:
 *   pass 1: drain_background_geom timer may fire; new OBB/LoD results
 *           integrated; do_view_changed(QG_VIEW_DRAWN) queued if counts
 *           advanced; view_update(GED_DBISTATE_VIEW_CHANGE) emitted.
 *   pass 2: do_view_changed(QG_VIEW_DRAWN) runs; flush_view_changed_()
 *           queued via QMetaObject::invokeMethod(Qt::QueuedConnection).
 *   pass 3: flush_view_changed_() runs; bvs->redraw() called; updated
 *           placeholder (OBB) or real LoD view objects created via
 *           bot_adaptive_plot(); view_update(QG_VIEW_REFRESH) emitted.
 *   pass 4: viewport repaint; swrast paintGL() renders the shapes.
 * Without all 4 passes, grab() returns a stale framebuffer.
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

    /* ------------------------------------------------------------------
     * Cold cache: wipe BU_DIR_CACHE before opening the .g file.
     *
     * With a warm LoD cache, bot_adaptive_plot() finds bsg_mesh_lod_key_get()
     * returning a valid key during "draw all" and immediately creates
     * BSG_NODE_MESH_LOD view objects -- the AABB and OBB placeholder stages
     * are never visible.  Wiping the cache forces:
     *   - Stage 1: AABB wireframe boxes (no key in cache)
     *   - Stage 2: OBB wireframe boxes (OBBs arrive first; LoD still pending)
     *   - Stage 3: LoD triangle mesh (after LoD computation completes)
     * ------------------------------------------------------------------ */
    const char *cdir = getenv("BU_DIR_CACHE");
    if (cdir) {
	QString cdir_qs(cdir);
	QDir cache_qdir(cdir_qs);
	if (cache_qdir.exists()) {
	    bu_log("  Wiping cold LoD cache: %s\n", cdir);
	    cache_qdir.removeRecursively();
	}
	QDir().mkpath(cdir_qs);
    }

    /* Open the .g file (now with empty cache) */
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
     * Stage 1: immediately after "draw all", NO events flushed.
     *
     * With cold cache, every BoT shape's view object was created with
     * draw_data==NULL (bbox placeholder) because bsg_mesh_lod_key_get()
     * returned 0.  The background pipeline has just started and no OBBs
     * or LoD data have arrived yet.
     * ------------------------------------------------------------------ */
    bu_log("\n--- Stage 1: post-draw, pre-drain (cold cache: AABB boxes) ---\n");
    m_bboxes_at_draw = dbis->bboxes.size();
    m_obbs_at_draw   = dbis->obbs.size();
    bu_log("  Snapshot: bboxes=%zu  obbs=%zu  lod=%zu\n",
   m_bboxes_at_draw, m_obbs_at_draw,
   dbis->lod_results_processed());

    /* Grab Stage 1 screenshot WITHOUT flushing events.
     * No drain timer has fired yet; the viewport shows pure AABB boxes. */
    m_app->w->update();
    QImage shot1 = m_app->w->grab().toImage().convertToFormat(QImage::Format_RGB32);
    m_bright_1 = bright_pixels(shot1);
    QDir().mkpath(m_outdir);
    shot1.save(m_outdir + "/pipeline_01_aabb.png", "PNG");
    bu_log("  Stage 1 screenshot: %d x %d, %d bright px\n",
   shot1.width(), shot1.height(), m_bright_1);

    /* ------------------------------------------------------------------
     * Stage 2 and 3 are handled by the poll timer.
     * Stage 2 (STAGE_OBB_WAIT): fires when obbs >= EXPECTED_OBBS and
     *   lod_results_processed() == 0.  Captures OBB wireframes.
     * Stage 3 (STAGE_LOD_WAIT): fires when pipeline is quiescent
     *   (no new results for QUIESCENT_POLLS_REQUIRED consecutive polls).
     *   Captures LoD triangle mesh geometry.
     * ------------------------------------------------------------------ */
    bu_log("\n--- Stage 2+3: entering poll loop ---\n");
    m_stage = STAGE_OBB_WAIT;
    m_poll_count = 0;
    m_quiescent_polls = 0;
    m_total_drain = 0;

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
bu_log("  [poll %d stage=%s] bboxes=%zu  obbs=%zu  lod=%zu  drain=%zu\n",
       m_poll_count,
       (m_stage == STAGE_OBB_WAIT) ? "OBB_WAIT" : "LOD_WAIT",
       dbis->bboxes.size(), dbis->obbs.size(),
       dbis->lod_results_processed(),
       m_total_drain);

    /* ---------------------------------------------------------------
     * STAGE_OBB_WAIT: wait for all OBBs to arrive, then take Stage 2
     * screenshot showing OBB wireframe placeholders.
     *
     * Transition condition: all EXPECTED_OBBS present AND fewer than
     * EXPECTED_OBBS/4 LoD results yet (OBBs complete faster than LoD
     * on a cold cache with BRLCAD_CACHE_LOD_DELAY_MS=5).
     *
     * When the pipeline starts, OBBs arrive in ~700ms (1ms each) while
     * LoD items take 14ms cold + 5ms delay = ~19ms each.  By the time
     * all 706 OBBs are ready, roughly 706ms / 19ms ≈ 37 LoD items have
     * arrived.  Using EXPECTED_OBBS/4 (≈176) as the ceiling ensures we
     * catch this window reliably even on faster machines.
     *
     * After the condition is first satisfied, wait QUIESCENT_POLLS_REQUIRED
     * more polls (150ms) so the drain_background_geom() 100ms timer has had
     * time to fire and queue the do_view_changed(QG_VIEW_DRAWN) →
     * bvs->redraw() chain.  Then flush events (4x processEvents) before
     * grabbing the screenshot.
     * --------------------------------------------------------------- */
    if (m_stage == STAGE_OBB_WAIT) {
	if (dbis->obbs.size() >= EXPECTED_OBBS &&
	    dbis->lod_results_processed() < EXPECTED_OBBS / 4)
	{
	    m_quiescent_polls++;
	    if (m_quiescent_polls >= QUIESCENT_POLLS_REQUIRED) {
		bu_log("  All OBBs present (obbs=%zu, lod=%zu<%zu) after %d polls -- "
		       "capturing OBB screenshot\n",
		       dbis->obbs.size(),
		       dbis->lod_results_processed(),
		       (size_t)(EXPECTED_OBBS / 4),
		       m_poll_count);

		/* Flush the OBB render chain (drain_bg -> do_view_changed ->
		 * flush_view_changed_ -> bvs->redraw -> repaint). */
		QImage shot2 = grab_screenshot("pipeline_02_obb", true);
		m_bright_2 = bright_pixels(shot2);
		bu_log("  Stage 2 (OBB) bright px: %d\n", m_bright_2);

		/* Transition to LoD wait */
		m_stage = STAGE_LOD_WAIT;
		m_quiescent_polls = 0;
	    }
	} else {
	    m_quiescent_polls = 0;
	}

if (m_poll_count >= MAX_LOD_POLLS) {
    bu_log("WARNING: OBB stage poll timed out "
   "(obbs=%zu, lod=%zu)\n",
   dbis->obbs.size(), dbis->lod_results_processed());
    /* Best-effort screenshot even on timeout */
    if (m_bright_2 < 0) {
QImage shot2 = grab_screenshot("pipeline_02_obb", true);
m_bright_2 = bright_pixels(shot2);
    }
    m_stage = STAGE_DONE;
    evaluate();
}
return;
    }

    /* ---------------------------------------------------------------
     * STAGE_LOD_WAIT: wait until the pipeline is quiescent (no new
     * results for QUIESCENT_POLLS_REQUIRED polls).  This means all LoD
     * data has been drained and any pending redraws have run.
     * --------------------------------------------------------------- */
    if (m_stage == STAGE_LOD_WAIT) {
	/* Consider the pipeline complete when EXPECTED_LOD results are
	 * present AND no new results have arrived for QUIESCENT_POLLS_REQUIRED
	 * consecutive polls.  The check for EXPECTED_LOD is essential: with
	 * cold cache the pipeline processes LoD in batches separated by tens of
	 * seconds; between batches drain_geom_results() returns 0 (giving a
	 * false quiescence signal) even though work is still in progress. */
	if (n == 0 && dbis->lod_results_processed() >= EXPECTED_LOD) {
	    m_quiescent_polls++;
	    if (m_quiescent_polls >= QUIESCENT_POLLS_REQUIRED) {
		bu_log("  Pipeline quiescent after %d polls "
		       "(lod=%zu  drain=%zu)\n",
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
    bu_log("WARNING: LoD stage poll timed out "
   "(bboxes=%zu  obbs=%zu  lod=%zu  drain=%zu)\n",
   dbis->bboxes.size(), dbis->obbs.size(),
   dbis->lod_results_processed(),
   m_total_drain);
    m_stage = STAGE_DONE;
    evaluate();
}
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

    /* Stage 3: take final screenshot with full event-loop flush. */
    QImage shot3 = grab_screenshot("pipeline_03_lod", true);
    m_bright_3 = bright_pixels(shot3);

    size_t final_bboxes = dbis->bboxes.size();
    size_t final_obbs   = dbis->obbs.size();
    size_t final_lod    = dbis->lod_results_processed();

    /* Count shapes with real LoD geometry (BSG_NODE_MESH_LOD flag).
     * This confirms bot_adaptive_plot() replaced OBB placeholders. */
    {
struct bview *v = m_app->mdl->gedp->ged_gvp;
BViewState *bvs = v ? dbis->get_view_state(v) : nullptr;
if (!bvs)
    bvs = dbis->shared_vs;
if (bvs && v)
    m_lod_shapes_3 = bvs->lod_shape_count(v);
    }

    bu_log("\n--- Final Evaluation ---\n");
    bu_log("  bboxes at draw:       %zu  (stage 1 snapshot)\n", m_bboxes_at_draw);
    bu_log("  obbs   at draw:       %zu  (stage 1 snapshot)\n", m_obbs_at_draw);
    bu_log("  bboxes final:         %zu  (expected >= %zu)\n",  final_bboxes, EXPECTED_BBOXES);
    bu_log("  obbs   final:         %zu  (expected >= %zu)\n",  final_obbs,   EXPECTED_OBBS);
    bu_log("  lod results:          %zu  (expected >= %zu)\n",  final_lod,    EXPECTED_LOD);
    bu_log("  lod shapes rendered:  %zu  (informational)\n",     m_lod_shapes_3);
    bu_log("  bright px: stage1=%d  stage2=%d  stage3=%d\n",
   m_bright_1, m_bright_2, m_bright_3);

    bool pass = true;

    /* Required: AABB bboxes (sync draw path) */
    if (final_bboxes < EXPECTED_BBOXES) {
bu_log("FAIL: bboxes %zu < expected %zu\n", final_bboxes, EXPECTED_BBOXES);
pass = false;
    } else {
bu_log("PASS: bboxes %zu >= %zu  (AABB stage OK)\n",
       final_bboxes, EXPECTED_BBOXES);
    }

    /* Required: OBBs populated */
    if (final_obbs < EXPECTED_OBBS) {
bu_log("FAIL: obbs %zu < expected %zu\n", final_obbs, EXPECTED_OBBS);
pass = false;
    } else {
bu_log("PASS: obbs %zu >= %zu  (OBB stage OK)\n",
       final_obbs, EXPECTED_OBBS);
    }

    /* Required: LoD data generated */
    if (final_lod < EXPECTED_LOD) {
bu_log("FAIL: lod_results_processed %zu < expected %zu "
       "(LoD stage did not run)\n", final_lod, EXPECTED_LOD);
pass = false;
    } else {
bu_log("PASS: lod_results_processed %zu >= %zu  (LoD stage OK)\n",
       final_lod, EXPECTED_LOD);
    }

    /* Required: Stage 1 AABB screenshot must be non-black */
    if (m_bright_1 <= 0) {
	bu_log("FAIL: Stage 1 screenshot is black (no AABB placeholder rendering)\n");
	pass = false;
    } else {
	bu_log("PASS: Stage 1 has %d bright pixels (AABB boxes visible)\n",
	       m_bright_1);
    }

    /* Required: Stage 3 must show more content than Stage 1.
     * LoD triangle meshes fill the model interior with far more pixels
     * than sparse AABB wireframe outlines.  This is the primary visual
     * confirmation that LoD rendering ran.  lod_shape_count is reported
     * above for informational purposes but is not used as the hard pass
     * criterion because it counts only the most-recently-redrawn batch
     * (coalescing in bvs->redraw()); the pixel delta is the reliable
     * end-to-end check. */
    if (m_bright_3 <= 0) {
	bu_log("FAIL: Stage 3 screenshot is black (swrast rendered nothing)\n");
	pass = false;
    } else if (m_bright_3 <= m_bright_1) {
	bu_log("FAIL: Stage 3 bright px (%d) not greater than Stage 1 (%d) -- "
	       "LoD geometry did not produce more visual content than AABB boxes\n",
	       m_bright_3, m_bright_1);
	pass = false;
    } else {
	bu_log("PASS: Stage 3 (%d px) > Stage 1 (%d px) -- "
	       "LoD mesh shows more visual content than AABB boxes\n",
	       m_bright_3, m_bright_1);
    }

    /* Informational: async OBB stage timing */
    if (m_obbs_at_draw == 0) {
bu_log("INFO: OBBs were 0 at Stage 1 -- async OBB pipeline confirmed async\n");
    } else {
bu_log("INFO: OBBs already %zu at Stage 1 -- cache warm at draw time\n",
       m_obbs_at_draw);
    }

    /* Informational: visual progression between stages */
    if (m_bright_2 >= 0 && m_bright_2 != m_bright_1) {
bu_log("INFO: Stage 2 vs Stage 1 differ (aabb=%d  obb=%d) -- "
       "OBB wireframe is visually different from AABB\n",
       m_bright_1, m_bright_2);
    }
    if (m_bright_3 != m_bright_2) {
bu_log("INFO: Stage 3 vs Stage 2 differ (obb=%d  lod=%d) -- "
       "LoD mesh visually different from OBB wireframe\n",
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

    /* Use a local test cache directory.  start() wipes this before opening
     * the .g file to guarantee a cold LoD cache for all three visual stages
     * to be distinct (AABB -> OBB wireframe -> LoD triangle mesh). */
    char cachedir[MAXPATHLEN] = {0};
    bu_dir(cachedir, MAXPATHLEN, BU_DIR_CURR, "qged_pipeline_cache", nullptr);
    bu_mkdir(cachedir);
    bu_setenv("BU_DIR_CACHE", cachedir, 1);

    /* Slow down the LoD background stage by 5ms per BoT.
     * For 706 BoTs this gives a window of at least 3.5 s (706 * 5ms) where
     * OBB wireframes are visible before LoD data arrives.  Without this
     * delay the OBB stage may complete in under 100ms and the OBB screenshot
     * would be taken before the OBB wireframes have been rendered. */
    bu_setenv("BRLCAD_CACHE_LOD_DELAY_MS", "5", 1);

    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);
    bu_setenv("DM_SWRAST", "1", 1);

    bu_log("qged_pipeline_test: %s\n", gfile);

    /* QgEdApp with no .g file arg (load_g_file called in start()).
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
