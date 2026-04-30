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
 * Validates the progressive drawing pipeline inside a running qged (QgEdApp)
 * instance using software rendering (swrast).  ALL primitive types (BoT AND
 * CSG/wireframe) are now lazy under the BRLCAD_CACHE_AABB_DELAY_MS policy:
 * every shape starts with have_bbox=0 and no view object; geometry appears
 * progressively as the async DrawPipeline delivers AABB, OBB, and LoD data.
 *
 * GenericTwin.g contains 706 BoT solids and approximately 1536 CSG solids
 * (confirmed by previous synchronous AABB counts showing 2242 total bboxes).
 * The DrawPipeline processes all of them asynchronously:
 *   - AABB (bounding box): deferred for ALL primitives (BRLCAD_CACHE_AABB_DELAY_MS)
 *   - OBB  (oriented bbox): BoTs only — computed after AABB
 *   - LoD  (level of detail): BoTs only — computed after OBB
 *
 * The test starts with a COLD LoD cache and injects per-item delays to make
 * each stage linger long enough to be captured.
 *
 * BoT shape flow:     no_view_obj → AABB placeholder → OBB placeholder → LoD
 * CSG shape flow:     no_view_obj (csg_no_view_obj) → csg_active (wireframe)
 * (CSG shapes have no placeholder stage; they go directly to active once their
 * AABB arrives because draw_scene calls wireframe_plot immediately.)
 *
 * Stage 1 (first geom): first poll where any placeholder OR csg_active > 0.
 *   Pass: (placeholder_aabb OR placeholder_obb OR csg_active) > 0, lod == 0
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

#include <sstream>
#include <string>

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
    bu_log("  [%s] mesh(total=%d aabb=%d obb=%d lod=%d no_vo=%d) "
	   "csg(total=%d no_vo=%d ph=%d active=%d)\n",
	   label,
	   s.total_mesh, s.placeholder_aabb, s.placeholder_obb,
	   s.lod_active, s.no_view_obj,
	   s.total_csg, s.csg_no_view_obj, s.csg_placeholder, s.csg_active);
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

    /* Draw all top-level objects.
     *
     * Use the GED "tops -n" command to discover the un-referenced (top-level)
     * objects in the database.  The "-n" flag suppresses comb ("/") and
     * region ("R") decorators, giving us plain names we can pass directly to
     * "draw".  We draw them all in a single "draw" invocation. */
    {
	struct bu_vls tops_msg = BU_VLS_INIT_ZERO;
	const char *tops_av[3] = {"tops", "-n", nullptr};
	(void)m_app->run_cmd(&tops_msg, 2, tops_av);
	std::string tops_str(bu_vls_cstr(&tops_msg));
	bu_vls_free(&tops_msg);

	/* Collect non-empty tokens (whitespace-separated) */
	std::vector<std::string> top_names;
	std::istringstream iss(tops_str);
	std::string tok;
	while (iss >> tok)
	    top_names.push_back(tok);

	if (top_names.empty())
	    top_names.push_back("all");  /* fallback for GenericTwin.g */

	bu_log("  tops: %zu top-level objects\n", top_names.size());

	/* Build argv for "draw name1 name2 ..." */
	std::vector<const char *> draw_av;
	draw_av.push_back("draw");
	for (const auto &n : top_names)
	    draw_av.push_back(n.c_str());
	draw_av.push_back(nullptr);

	int r = m_app->run_cmd(&msg, (int)draw_av.size() - 1, draw_av.data());
	bu_log("  draw -> ret=%d msg='%s'\n", r, bu_vls_cstr(&msg));
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
     * Stage 0: immediately after "draw all", before any drain events.
     *
     * ALL primitive types (BoT AND CSG) are now lazy: when
     * BRLCAD_CACHE_AABB_DELAY_MS is set, rt_bound_instance is skipped for
     * every leaf solid.  Every shape starts with have_bbox=0 and no view
     * object — the scene is completely empty at this point.
     * Progressive autoview (gv_progressive_autoview) is set because
     * autoview fired during "draw all" but the scene was empty.
     * ------------------------------------------------------------------ */
    bu_log("\n--- Stage 0: post-draw, pre-drain (all shapes lazy: expect empty scene) ---\n");
    m_bboxes_at_draw = dbis->bboxes.size();
    m_obbs_at_draw   = dbis->obbs.size();
    bu_log("  Snapshot: bboxes=%zu  obbs=%zu  lod=%zu\n",
   m_bboxes_at_draw, m_obbs_at_draw,
   dbis->lod_results_processed());

    /* Check progressive autoview flag */
    bsg_view *gvp = m_app->mdl->gedp->ged_gvp;
    bool pav_set = (gvp && gvp->gv_s && gvp->gv_s->gv_progressive_autoview);
    bu_log("  gv_progressive_autoview: %s\n", pav_set ? "SET" : "not set");

    /* Stage 0 screenshot: ALL shapes invisible (no view obj yet) */
    m_app->w->update();
    QImage shot0 = m_app->w->grab().toImage().convertToFormat(QImage::Format_RGB32);
    QDir().mkpath(m_outdir);
    shot0.save(m_outdir + "/pipeline_00_predrain.png", "PNG");
    bu_log("  Stage 0 screenshot: %d x %d, %d bright px\n",
   shot0.width(), shot0.height(), bright_pixels(shot0));

    m_stats_0 = gvp ? inspect_scene_objs(gvp) : SceneStats{};
    log_stats("stage0", m_stats_0);

    /* Start the poll loop for all subsequent stages */
    bu_log("\n--- Stages 1+: entering poll loop ---\n");
    m_stage = STAGE_AABB_WAIT;
    m_poll_count    = 0;
    m_periodic_count = 0;
    m_next_periodic  = PERIODIC_SNAP_POLLS;

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

    /* Periodic progress logging */
    if (m_poll_count % 20 == 0) {
bu_log("  [poll %4d stage=%-12s] bboxes=%4zu  obbs=%4zu  lod=%4zu  "
       "scene: no_vo=%3d aabb=%3d obb=%3d lod=%3d\n",
       m_poll_count,
       (m_stage == STAGE_AABB_WAIT)  ? "AABB_WAIT"  :
       (m_stage == STAGE_OBB_WAIT)   ? "OBB_WAIT"   :
       (m_stage == STAGE_LOD_WAIT)   ? "LOD_WAIT"   :
       (m_stage == STAGE_FINAL_WAIT) ? "FINAL_WAIT" : "DONE",
       dbis->bboxes.size(), dbis->obbs.size(),
       dbis->lod_results_processed(),
       stats.no_view_obj, stats.placeholder_aabb,
       stats.placeholder_obb, stats.lod_active);
    }

    /* Periodic screenshots: capture mid-stage snapshots every ~5s to show
     * gradual progression (empty → AABB fill → OBB replace → LoD replace). */
    if (m_poll_count >= m_next_periodic &&
	m_stage != STAGE_DONE &&
	m_stage != STAGE_FINAL_WAIT)
    {
	QString pname = QString("pipeline_p%1_poll%2")
	    .arg(m_periodic_count, 2, 10, QLatin1Char('0'))
	    .arg(m_poll_count, 5, 10, QLatin1Char('0'));
	grab_screenshot(pname);
	SceneStats ps = gvp ? inspect_scene_objs(gvp) : SceneStats{};
	log_stats(pname.toLocal8Bit().data(), ps);
	m_periodic_count++;
	m_next_periodic = m_poll_count + PERIODIC_SNAP_POLLS;
    }

    /* ---------------------------------------------------------------
     * STAGE_AABB_WAIT: wait for the first geometry to appear — either a
     * BoT placeholder (AABB or OBB wireframe box) or an active CSG vlist
     * — while no LoD has arrived yet.
     *
     * Background: AABB and OBB workers run sequentially per item with
     * similar delays, so by the first 50ms poll we may already see OBB
     * placeholders rather than AABB-only placeholders.  CSG shapes do not
     * get a placeholder stage: once their async AABB arrives draw_scene
     * calls wireframe_plot and they jump directly from no_view_obj to
     * csg_active.
     *
     * Transition: (placeholder_aabb > 0 OR placeholder_obb > 0 OR
     *              csg_active > 0) AND lod_active == 0
     * --------------------------------------------------------------- */
    if (m_stage == STAGE_AABB_WAIT) {
	if ((stats.placeholder_aabb > 0 || stats.placeholder_obb > 0 ||
	     stats.csg_active > 0) &&
	    stats.lod_active == 0)
	{
	    bu_log("  First geometry detected "
		   "(bot_aabb=%d obb=%d csg_active=%d) after %d polls\n",
		   stats.placeholder_aabb, stats.placeholder_obb,
		   stats.csg_active, m_poll_count);
	    m_stats_1 = stats;
	    log_stats("stage1", m_stats_1);
	    m_stage1_pass = true;
	    grab_screenshot("pipeline_01_first_geom");
	    /* If this file has no BoTs (CSG-only), skip OBB and LoD stages. */
	    if (stats.total_mesh == 0) {
		bu_log("  No BoT shapes in scene -- skipping OBB/LoD stages "
		       "(CSG-only file)\n");
		m_stage2_pass = true; /* OBB stage N/A */
		m_stage3_pass = true; /* LoD stage N/A */
		m_stage = STAGE_FINAL_WAIT;
	    } else {
		m_stage = STAGE_OBB_WAIT;
	    }
	}

	if (m_poll_count >= MAX_POLLS) {
	    bu_log("WARNING: AABB stage timed out "
		   "(bboxes=%zu, no_vo=%d, bot_aabb=%d, obb=%d, csg_active=%d)\n",
		   dbis->bboxes.size(),
		   stats.no_view_obj, stats.placeholder_aabb,
		   stats.placeholder_obb, stats.csg_active);
	    if (!m_stage1_pass)
		grab_screenshot("pipeline_01_first_geom");
	    m_stage = STAGE_DONE;
	    evaluate();
	}
	return;
    }

    /* ---------------------------------------------------------------
     * STAGE_OBB_WAIT: wait for first OBB placeholder while no LoD yet.
     *
     * Transition: placeholder_obb > 0 AND lod_active == 0
     * --------------------------------------------------------------- */
    if (m_stage == STAGE_OBB_WAIT) {
	if (stats.placeholder_obb > 0 && stats.lod_active == 0) {
	    bu_log("  First OBB placeholders detected (obb=%d, aabb=%d) after %d polls\n",
		   stats.placeholder_obb, stats.placeholder_aabb, m_poll_count);
	    m_stats_2 = stats;
	    log_stats("stage2", m_stats_2);
	    m_stage2_pass = true;
	    grab_screenshot("pipeline_02_first_obb");
	    m_stage = STAGE_LOD_WAIT;
	}

	if (m_poll_count >= MAX_POLLS) {
	    bu_log("WARNING: OBB stage timed out (obbs=%zu, lod=%zu)\n",
		   dbis->obbs.size(), dbis->lod_results_processed());
	    if (!m_stage2_pass)
		grab_screenshot("pipeline_02_first_obb");
	    m_stage = STAGE_DONE;
	    evaluate();
	}
	return;
    }

    /* ---------------------------------------------------------------
     * STAGE_LOD_WAIT: wait for first LoD view object in scene.
     *
     * Transition: lod_active > 0
     * --------------------------------------------------------------- */
    if (m_stage == STAGE_LOD_WAIT) {
	if (stats.lod_active > 0) {
	    bu_log("  First LoD objects detected (lod=%d) after %d polls\n",
		   stats.lod_active, m_poll_count);
	    m_stats_3 = stats;
	    log_stats("stage3", m_stats_3);
	    m_stage3_pass = true;
	    grab_screenshot("pipeline_03_first_lod");
	    m_stage = STAGE_FINAL_WAIT;
	}

	if (m_poll_count >= MAX_POLLS) {
	    bu_log("WARNING: LoD stage timed out (lod=%zu)\n",
		   dbis->lod_results_processed());
	    if (!m_stage3_pass)
		grab_screenshot("pipeline_03_first_lod");
	    m_stage = STAGE_DONE;
	    evaluate();
	}
	return;
    }

    /* ---------------------------------------------------------------
     * STAGE_FINAL_WAIT: wait for all LoD results to complete.
     *
     * Transition: lod_results_processed() >= EXPECTED_LOD
     * --------------------------------------------------------------- */
    if (m_stage == STAGE_FINAL_WAIT) {
	/* For CSG-only files (stage2/3 skipped), finish once all bboxes
	 * have been populated AND gv_progressive_autoview is cleared.
	 * For BoT files, wait for LoD results as before. */
	bsg_view *fw_gvp = (m_app && m_app->mdl && m_app->mdl->gedp)
	    ? m_app->mdl->gedp->ged_gvp : nullptr;
	bool pav = (fw_gvp && fw_gvp->gv_s &&
		    fw_gvp->gv_s->gv_progressive_autoview);
	bool lod_done = (dbis->lod_results_processed() >= EXPECTED_LOD ||
			 EXPECTED_LOD == 0);
	bool csg_stable = (m_stats_0.total_mesh == 0 && !pav);

	if (lod_done || csg_stable) {
	    if (csg_stable)
		bu_log("  CSG-only scene: all bboxes populated, "
		       "progressive autoview cleared after %d polls\n",
		       m_poll_count);
	    else
		bu_log("  All LoD results processed (%zu) after %d polls\n",
		       dbis->lod_results_processed(), m_poll_count);
	    /* Final screenshot showing fully-resolved scene */
	    grab_screenshot("pipeline_04_all_lod");
	    m_stage = STAGE_DONE;
	    evaluate();
	    return;
	}

	if (m_poll_count >= MAX_POLLS) {
	    bu_log("WARNING: final-wait timed out "
		   "(lod_results=%zu expected>=%zu, pav=%d)\n",
		   dbis->lod_results_processed(), EXPECTED_LOD, (int)pav);
	    grab_screenshot("pipeline_04_all_lod");
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
    log_stats("stage0_snap", m_stats_0);
    log_stats("stage1_snap", m_stats_1);
    log_stats("stage2_snap", m_stats_2);
    log_stats("stage3_snap", m_stats_3);

    bool pass = true;

    /* --- Stage 1: first geometry visible (BoT placeholder OR CSG active) --- */
    if (!m_stage1_pass) {
	bu_log("FAIL: Stage 1 -- expected first geometry "
	       "(bot_aabb=%d obb=%d csg_active=%d lod=%d)\n",
	       m_stats_1.placeholder_aabb, m_stats_1.placeholder_obb,
	       m_stats_1.csg_active,
	       m_stats_1.lod_active);
	pass = false;
    } else {
	bu_log("PASS: Stage 1 -- first geometry visible "
	       "(bot_aabb=%d obb=%d csg_active=%d)\n",
	       m_stats_1.placeholder_aabb, m_stats_1.placeholder_obb,
	       m_stats_1.csg_active);
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
    /* CSG-only: no BoTs in the scene → skip OBB and LoD checks */
    bool csg_only = (m_stats_0.total_mesh == 0 && m_stats_0.total_csg > 0);

    if (final_bboxes == 0) {
	bu_log("FAIL: bboxes = 0 (no AABB data populated at all)\n");
	pass = false;
    } else if (!csg_only && EXPECTED_BBOXES > 0 && final_bboxes < EXPECTED_BBOXES) {
	bu_log("FAIL: bboxes %zu < expected %zu\n",
	       final_bboxes, EXPECTED_BBOXES);
	pass = false;
    } else {
	bu_log("PASS: bboxes %zu %s\n",
	       final_bboxes,
	       csg_only ? "(CSG-only; all shapes populated)" :
			  "(>= GenericTwin expected)");
    }

    /* OBB and LoD checks: only apply when the file has BoTs. */
    if (!csg_only && EXPECTED_OBBS > 0) {
	if (final_obbs < EXPECTED_OBBS) {
	    bu_log("FAIL: obbs %zu < expected %zu\n",
		   final_obbs, EXPECTED_OBBS);
	    pass = false;
	} else {
	    bu_log("PASS: obbs %zu >= %zu\n", final_obbs, EXPECTED_OBBS);
	}
    } else if (csg_only) {
	bu_log("NOTE: OBB count check skipped (CSG-only file; no BoT shapes)\n");
    }

    if (!csg_only && EXPECTED_LOD > 0) {
	if (final_lod < EXPECTED_LOD) {
	    bu_log("FAIL: lod_results %zu < expected %zu\n",
		   final_lod, EXPECTED_LOD);
	    pass = false;
	} else {
	    bu_log("PASS: lod_results %zu >= %zu\n", final_lod, EXPECTED_LOD);
	}
    } else if (csg_only) {
	bu_log("NOTE: LoD count check skipped (CSG-only file; no BoT shapes)\n");
    }

    /* --- Progressive autoview: should be cleared once all bboxes arrived --- */
    {
	bsg_view *gvp = (m_app && m_app->mdl && m_app->mdl->gedp)
	    ? m_app->mdl->gedp->ged_gvp : nullptr;
	bool pav = (gvp && gvp->gv_s && gvp->gv_s->gv_progressive_autoview);
	if (pav) {
	    /* Still set: autoview hasn't stabilised yet.  This is a soft
	     * warning — the drain may not have fully completed when we reach
	     * evaluate() in a timeout path.  Not a hard FAIL. */
	    bu_log("NOTE: gv_progressive_autoview still set at evaluate() "
		   "(shapes_without_bbox may be > 0)\n");
	} else {
	    bu_log("PASS: gv_progressive_autoview cleared "
		   "(all shape bboxes populated, autoview stable)\n");
	}
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
     * AABB_DELAY_MS=10: each AABB takes ~10ms extra.  With synchronous bbox
     *   computation skipped for BoTs (defer_bot logic in get_bbox), shapes
     *   start with have_bbox=0 and no view object.  As AABBs trickle in over
     *   ~7s (706 x 10ms), we see a gradual fill from empty → AABB boxes.
     *
     * OBB_DELAY_MS=10: each OBB takes ~10ms extra, starting after AABB.
     *   Gradual AABB → OBB transition visible as OBBs arrive.
     *
     * LOD_DELAY_MS=500: each LoD takes ~500ms extra, starting after OBB.
     *   First LoD appears ~500ms after its OBB; gradual OBB → LoD transition
     *   visible with many intermediate screenshots. */
    bu_setenv("BRLCAD_CACHE_AABB_DELAY_MS", "10",  1);
    bu_setenv("BRLCAD_CACHE_OBB_DELAY_MS",  "10",  1);
    bu_setenv("BRLCAD_CACHE_LOD_DELAY_MS",  "500", 1);

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
