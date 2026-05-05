/*                    Q G E D _ T E S T . C P P
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
/** @file qged_test.cpp
 *
 * Automated validation test for qged's swrast draw pipeline.
 *
 * Creates a QgEdApp with software rendering, opens a .g file, issues "draw"
 * and "autoview" commands via QTimer::singleShot, grabs window screenshots
 * before and after drawing, and verifies that:
 *
 *   1. qged starts without crashing.
 *   2. The .g file opens successfully.
 *   3. After "draw all.g" + "autoview", the window contains more bright
 *      (wireframe) pixels than before drawing — confirming that swrast
 *      actually rendered geometry into the 3D viewport.
 *
 * Usage:  qged_test <file.g> [outdir]
 *
 * The test exits with code 0 on pass, non-zero on failure.
 */

#include "common.h"

#include <sstream>
#include <string>
#include <vector>

#include <QApplication>
#include <QPixmap>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QThread>

#include "bu/app.h"
#include "bu/log.h"
#include "bu/vls.h"
#include "bu/file.h"

#include "qged_test_runner.h"

/* --------------------------------------------------------------------------
 * Count pixels whose maximum RGB channel exceeds a brightness threshold.
 * Wireframe lines in swrast are bright (near 255); the dark theme background
 * is dim (< 100), so threshold=150 reliably separates them.
 *
 * Returns the number of pixels where max(R,G,B) > threshold.
 * -------------------------------------------------------------------------- */
static int
bright_pixels(const QImage &img, int threshold = 150)
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

/* --------------------------------------------------------------------------
 * QgedTestRunner implementation
 * -------------------------------------------------------------------------- */
QgedTestRunner::QgedTestRunner(QgEdApp *app, const char *gfile,
       const QString &outdir, QObject *parent)
    : QObject(parent), m_app(app), m_gfile(gfile), m_outdir(outdir)
{}

void
QgedTestRunner::run()
{
    bu_log("QgedTestRunner::run — loading .g file and drawing\n");

    /* Step 1: open the .g file */
    int ret = m_app->load_g_file(m_gfile, false);
    if (ret != BRLCAD_OK) {
bu_log("FAIL: load_g_file(%s) returned %d\n", m_gfile, ret);
m_pass = false;
QApplication::exit(1);
return;
    }
    bu_log("  load_g_file OK\n");

    if (!m_app->w) {
bu_log("FAIL: main window not accessible\n");
m_pass = false;
QApplication::exit(1);
return;
    }

    /* Step 1b: verify the 3D GL context is valid */
    bu_log("  3D context valid: %s\n", m_app->w->isValid3D() ? "YES" : "NO");

    /* Helper lambda: capture the 3D viewport as a QImage.
     *
     * For Obol builds, QWidget::grab() does NOT reliably include
     * QOpenGLWidget content in headless Xvfb environments — the 3D area
     * comes out black even though the GL context is valid and rendering is
     * occurring.  QOpenGLWidget::grabFramebuffer() (exposed here via
     * grabObolView()) reads directly from the GL FBO and is the correct API
     * to use in headless tests. */
    auto capture_gl = [&]() -> QImage {
	QImage gl = m_app->w->grabObolView();
	if (!gl.isNull())
	    return gl.convertToFormat(QImage::Format_RGB32);
	/* Fallback: libdm / non-Obol path uses QWidget::grab() */
	return m_app->w->grab().toImage().convertToFormat(QImage::Format_RGB32);
    };

    /* Step 2: baseline screenshot (before draw). */
    m_app->w->update();
    QApplication::processEvents();
    QApplication::processEvents();
    QImage before = capture_gl();
    int bright_before = bright_pixels(before);
    bu_log("  Before draw: %d bright pixels\n", bright_before);

    QDir().mkpath(m_outdir);
    before.save(m_outdir + "/qged_test_before.png", "PNG");

    /* Step 3: draw all top-level objects (discovered via "tops -n") and autoview */
    struct bu_vls msg = BU_VLS_INIT_ZERO;
    {
	/* Use "tops -n" to discover the un-referenced (top-level) objects so
	 * the draw command works on any .g file regardless of what names it
	 * contains (avoids "Invalid path element: all.g" for fg4-converted
	 * files). */
	struct bu_vls tops_msg = BU_VLS_INIT_ZERO;
	const char *tops_av[3] = {"tops", "-n", nullptr};
	(void)m_app->run_cmd(&tops_msg, 2, tops_av);
	std::string tops_str(bu_vls_cstr(&tops_msg));
	bu_vls_free(&tops_msg);

	std::vector<std::string> top_names;
	std::istringstream iss(tops_str);
	std::string tok;
	while (iss >> tok)
	    if (!tok.empty())
		top_names.push_back(tok);

	bu_log("  tops: %zu top-level objects\n", top_names.size());

	if (!top_names.empty()) {
	    std::vector<const char *> draw_av;
	    draw_av.push_back("draw");
	    for (const auto &n : top_names)
		draw_av.push_back(n.c_str());
	    draw_av.push_back(nullptr);
	    int r = m_app->run_cmd(&msg, (int)draw_av.size() - 1, draw_av.data());
	    bu_log("  draw -> ret=%d msg='%s'\n", r, bu_vls_cstr(&msg));
	} else {
	    bu_log("  tops returned nothing; skipping draw\n");
	}
	bu_vls_trunc(&msg, 0);
    }
    {
	const char *av[2] = {"autoview", nullptr};
	int r = m_app->run_cmd(&msg, 1, av);
	bu_log("  autoview → ret=%d msg='%s'\n", r, bu_vls_cstr(&msg));
	bu_vls_trunc(&msg, 0);
    }
    bu_vls_free(&msg);

    /* Step 4: pump the event loop for up to 8 seconds to allow the async
     * DrawPipeline to deliver AABB results, trigger obol_scene_assemble_cad,
     * and repaint the Obol OpenGL widget.  We stop early once the bright-pixel
     * count stops rising (3 consecutive stable samples 200 ms apart). */
    QElapsedTimer timer;
    timer.start();
    int bright_after = bright_before;
    int stable_count = 0;
    int prev_bright = bright_before;

    /* The stable-count early exit only fires when bright_after > 0 so that
     * the loop doesn't exit immediately when the async pipeline hasn't
     * delivered any geometry yet (i.e. both before and after are 0). */
    while (timer.elapsed() < 15000 && (bright_after == 0 || stable_count < 3)) {
	m_app->w->update();
	QApplication::processEvents(QEventLoop::AllEvents, 200);
	QThread::msleep(50);
	QApplication::processEvents();

	QImage snap = capture_gl();
	bright_after = bright_pixels(snap);

	if (bright_after == prev_bright && bright_after > 0) {
	    stable_count++;
	} else if (bright_after != prev_bright) {
	    stable_count = 0;
	    prev_bright = bright_after;
	}
    }
    bu_log("  After draw:  %d bright pixels (waited %lld ms)\n",
	   bright_after, (long long)timer.elapsed());

    /* Save final screenshot */
    QImage after = capture_gl();

    /* Save screenshot */
    QString out_path = m_outdir + "/qged_test_after.png";
    bool saved = after.save(out_path, "PNG");
    bu_log("  Screenshot:  %s (%s)\n",
   out_path.toLocal8Bit().data(),
   saved ? "saved" : "SAVE FAILED");

    bu_log("  Window: %d × %d pixels\n", after.width(), after.height());

    /* Pass criteria: drawing must add bright pixels to the viewport */
    if (bright_after <= bright_before) {
bu_log("FAIL: draw produced no additional bright pixels "
       "(before=%d  after=%d)\n", bright_before, bright_after);
m_pass = false;
    } else {
bu_log("PASS: draw added %d bright pixels (before=%d  after=%d)\n",
       bright_after - bright_before, bright_before, bright_after);
m_pass = true;
    }

    QApplication::exit(m_pass ? 0 : 1);
}

/* --------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------- */
int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);

    if (argc < 2) {
fprintf(stderr, "Usage: %s <file.g> [outdir]\n", argv[0]);
return 1;
    }

    const char *gfile  = argv[1];
    const char *outdir = (argc >= 3) ? argv[2] : ".";

    if (!bu_file_exists(gfile, nullptr)) {
fprintf(stderr, "ERROR: %s not found\n", gfile);
return 2;
    }

    /* Local cache to avoid polluting the system cache */
    char cachedir[MAXPATHLEN] = {0};
    bu_dir(cachedir, MAXPATHLEN, BU_DIR_CURR, "qged_test_cache", nullptr);
    bu_mkdir(cachedir);
    bu_setenv("BU_DIR_CACHE", cachedir, 1);

    bu_setenv("LIBRT_USE_COMB_INSTANCE_SPECIFIERS", "1", 1);

    bu_log("Starting qged (swrast mode), will open %s ...\n", gfile);

    /* QgEdApp with no .g file arg (we call load_g_file in the runner).
     * qargc=0 so QgEdApp's "if (argc)" block skips automatic file loading.
     * qargv[0]=argv[0] gives Qt the program name for internal use. */
    int   qargc = 0;
    char *qargv[2] = { argv[0], nullptr };
    QgEdApp app(qargc, qargv, 1 /*swrast*/, 0 /*quad*/);

    /* Schedule test runner 500 ms after the event loop starts */
    QgedTestRunner runner(&app, gfile, QString(outdir));
    QTimer::singleShot(500, &runner, &QgedTestRunner::run);

    int ret = app.exec();
    bu_log("\nTest %s.\n", (ret == 0) ? "PASSED" : "FAILED");
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
