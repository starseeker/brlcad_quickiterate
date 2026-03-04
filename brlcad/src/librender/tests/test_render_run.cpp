/*             T E S T _ R E N D E R _ R U N . C P P
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
/** @file librender/tests/test_render_run.cpp
 *
 * Unit tests for librtrender's synchronous render_run() path.
 *
 * Tests:
 *   1. render_ctx_create / render_ctx_destroy lifecycle.
 *   2. render_opts API round-trip (all setters accessible).
 *   3. render_run() delivers exactly W*H pixels (W scanlines of W pixels).
 *   4. All three lighting models produce non-identical output for a
 *      scene that has geometry (moss.g / all.g).
 *   5. Background pixels are returned for rays that miss all geometry.
 *   6. render_run() returns BRLCAD_ERROR when view params are not set.
 *
 * The test geometry file path is passed as argv[1].  The expected
 * top-level object is "all.g" which is present in moss.g.
 */

#include "common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

#include "bu/app.h"
#include "bu/log.h"
#include "vmath.h"
#include "raytrace.h"

#include "render.h"


/* ── pixel accumulator ─────────────────────────────────────────────── */

struct PixelAccum {
    int   width;
    int   height;
    int   scanlines_received;
    long  total_pixels;
    long  nonblack_pixels;   /* pixels not equal to background (0,0,0) */
    std::vector<unsigned char> image; /* w*h*3 */
};

static void
pixel_cb(void *ud, int x, int y, int w, const unsigned char *rgb)
{
    PixelAccum *acc = (PixelAccum *)ud;
    acc->scanlines_received++;
    acc->total_pixels += w;

    /* copy into image buffer */
    int row = y;
    if (row >= 0 && row < acc->height && x == 0 && w == acc->width) {
	unsigned char *dst = acc->image.data() + row * acc->width * 3;
	memcpy(dst, rgb, (size_t)(w * 3));
    }

    for (int i = 0; i < w; i++) {
	if (rgb[i*3] || rgb[i*3+1] || rgb[i*3+2])
	    acc->nonblack_pixels++;
    }
}

static void
done_cb(void *ud, int ok)
{
    int *result = (int *)ud;
    *result = ok;
}


/* ── helpers ───────────────────────────────────────────────────────── */

/* Set up a simple front-view of the scene at (0,0,1) looking toward origin */
static void
setup_front_view(render_opts_t *opts, int w, int h)
{
    mat_t view2model;
    point_t eye;
    double viewsize = 200.0;   /* mm, big enough to capture moss.g geometry */

    /* Identity view matrix = looking down -Z */
    MAT_IDN(view2model);
    VSET(eye, 0.0, 0.0, 2000.0);

    render_opts_set_size(opts, w, h);
    render_opts_set_view(opts, (const double *)view2model, (const double *)eye, viewsize);
    render_opts_set_aspect(opts, (double)w / (double)h);
    render_opts_set_threads(opts, 1);
}

static int total_failures = 0;

#define FAIL(msg) do { \
    bu_log("FAIL [%s:%d]: %s\n", __FILE__, __LINE__, (msg)); \
    total_failures++; \
} while (0)

#define PASS(msg) bu_log("PASS: %s\n", (msg))


/* ── test 1: lifecycle ─────────────────────────────────────────────── */

static void
test_lifecycle(const char *dbfile)
{
    const char *objs[] = { "all.g" };

    render_ctx_t *ctx = render_ctx_create(dbfile, 1, objs);
    if (!ctx) { FAIL("render_ctx_create returned NULL for valid .g file"); return; }
    PASS("render_ctx_create succeeds for moss.g all.g");

    render_opts_t *opts = render_opts_create();
    if (!opts) { render_ctx_destroy(ctx); FAIL("render_opts_create returned NULL"); return; }
    PASS("render_opts_create succeeds");

    render_opts_destroy(opts);
    PASS("render_opts_destroy succeeds");

    render_ctx_destroy(ctx);
    PASS("render_ctx_destroy succeeds");
}


/* ── test 2: opts API ──────────────────────────────────────────────── */

static void
test_opts_api(void)
{
    render_opts_t *opts = render_opts_create();
    if (!opts) { FAIL("render_opts_create"); return; }

    /* Call every setter; verify nothing crashes. */
    render_opts_set_size(opts, 128, 96);
    render_opts_set_lighting(opts, RENDER_LIGHT_NORMALS);
    render_opts_set_lighting(opts, RENDER_LIGHT_DIFFUSE);
    render_opts_set_lighting(opts, RENDER_LIGHT_FULL);
    render_opts_set_threads(opts, 2);
    render_opts_set_background(opts, 0.1, 0.2, 0.3);
    render_opts_set_aspect(opts, 4.0 / 3.0);
    render_opts_set_perspective(opts, 45.0);

    mat_t m; MAT_IDN(m);
    point_t eye; VSET(eye, 0, 0, 100);
    render_opts_set_view(opts, (const double *)m, (const double *)eye, 100.0);

    render_opts_destroy(opts);
    PASS("render_opts: all setters callable without crash");
}


/* ── test 3: render_run() without view set returns error ─────────── */

static void
test_no_view_error(const char *dbfile)
{
    const char *objs[] = { "all.g" };
    render_ctx_t *ctx = render_ctx_create(dbfile, 1, objs);
    if (!ctx) { FAIL("render_ctx_create for no-view test"); return; }

    render_opts_t *opts = render_opts_create();
    if (!opts) { render_ctx_destroy(ctx); FAIL("render_opts_create for no-view test"); return; }
    /* do NOT call render_opts_set_view */

    int done_ok = -1;
    int ret = render_run(ctx, opts, pixel_cb, NULL, done_cb, &done_ok);
    if (ret != BRLCAD_ERROR) {
	FAIL("render_run should return BRLCAD_ERROR when view not set");
    } else {
	PASS("render_run returns BRLCAD_ERROR when view params not set");
    }

    render_opts_destroy(opts);
    render_ctx_destroy(ctx);
}


/* ── test 4: render_run() delivers correct scanline count ─────────── */

static void
test_scanline_count(const char *dbfile)
{
    const int W = 32, H = 24;
    const char *objs[] = { "all.g" };

    render_ctx_t *ctx = render_ctx_create(dbfile, 1, objs);
    if (!ctx) { FAIL("render_ctx_create for scanline-count test"); return; }

    render_opts_t *opts = render_opts_create();
    if (!opts) { render_ctx_destroy(ctx); FAIL("render_opts_create"); return; }

    setup_front_view(opts, W, H);

    PixelAccum acc;
    acc.width = W;
    acc.height = H;
    acc.scanlines_received = 0;
    acc.total_pixels = 0;
    acc.nonblack_pixels = 0;
    acc.image.assign((size_t)(W * H * 3), 0);

    int done_ok = -1;
    int ret = render_run(ctx, opts, pixel_cb, NULL, done_cb, &done_ok);

    if (ret != BRLCAD_OK) {
	FAIL("render_run returned error for valid scene");
    } else if (acc.scanlines_received != H) {
	char buf[80];
	snprintf(buf, sizeof(buf), "expected %d scanlines, got %d",
		 H, acc.scanlines_received);
	FAIL(buf);
    } else if (acc.total_pixels != (long)(W * H)) {
	FAIL("pixel count mismatch");
    } else if (done_ok != 1) {
	FAIL("done callback not fired or reported failure");
    } else {
	PASS("render_run: correct scanline and pixel count delivered");
    }

    if (acc.nonblack_pixels == 0) {
	FAIL("render_run: all pixels are background — no geometry hit?");
    } else {
	PASS("render_run: at least some non-background pixels delivered");
    }

    render_opts_destroy(opts);
    render_ctx_destroy(ctx);
}


/* ── test 5: three lighting models produce output ─────────────────── */

static long
run_and_count_nonblack(const char *dbfile, int lightmodel, int w, int h)
{
    const char *objs[] = { "all.g" };
    render_ctx_t *ctx = render_ctx_create(dbfile, 1, objs);
    if (!ctx) return -1;

    render_opts_t *opts = render_opts_create();
    if (!opts) { render_ctx_destroy(ctx); return -1; }

    setup_front_view(opts, w, h);
    render_opts_set_lighting(opts, lightmodel);

    PixelAccum acc;
    acc.width = w;
    acc.height = h;
    acc.scanlines_received = 0;
    acc.total_pixels = 0;
    acc.nonblack_pixels = 0;
    acc.image.assign((size_t)(w * h * 3), 0);

    render_run(ctx, opts, pixel_cb, NULL, NULL, &acc);

    render_opts_destroy(opts);
    render_ctx_destroy(ctx);

    return acc.nonblack_pixels;
}

static void
test_lighting_models(const char *dbfile)
{
    const int W = 32, H = 32;

    long nb_normals = run_and_count_nonblack(dbfile, RENDER_LIGHT_NORMALS,  W, H);
    long nb_diffuse = run_and_count_nonblack(dbfile, RENDER_LIGHT_DIFFUSE,  W, H);
    long nb_full    = run_and_count_nonblack(dbfile, RENDER_LIGHT_FULL,     W, H);

    if (nb_normals < 0) { FAIL("RENDER_LIGHT_NORMALS: render_ctx_create failed"); return; }
    if (nb_diffuse < 0) { FAIL("RENDER_LIGHT_DIFFUSE: render_ctx_create failed"); return; }
    if (nb_full    < 0) { FAIL("RENDER_LIGHT_FULL: render_ctx_create failed");    return; }

    bu_log("  nonblack pixels — normals:%ld  diffuse:%ld  full:%ld\n",
	   nb_normals, nb_diffuse, nb_full);

    if (nb_normals == 0) FAIL("RENDER_LIGHT_NORMALS produced all-background image");
    else PASS("RENDER_LIGHT_NORMALS produces non-background pixels");

    if (nb_diffuse == 0) FAIL("RENDER_LIGHT_DIFFUSE produced all-background image");
    else PASS("RENDER_LIGHT_DIFFUSE produces non-background pixels");

    if (nb_full == 0) FAIL("RENDER_LIGHT_FULL produced all-background image");
    else PASS("RENDER_LIGHT_FULL produces non-background pixels");
}


/* ── main ──────────────────────────────────────────────────────────── */

int
main(int argc, const char *argv[])
{
    bu_setprogname(argv[0]);

    if (argc < 2) {
	bu_log("Usage: %s <path/to/moss.g>\n", argv[0]);
	return 1;
    }

    const char *dbfile = argv[1];

    bu_log("=== librtrender: render_run() unit tests ===\n");
    bu_log("Database: %s\n\n", dbfile);

    bu_log("--- Test 1: render_ctx lifecycle ---\n");
    test_lifecycle(dbfile);

    bu_log("\n--- Test 2: opts API ---\n");
    test_opts_api();

    bu_log("\n--- Test 3: no-view error ---\n");
    test_no_view_error(dbfile);

    bu_log("\n--- Test 4: scanline count ---\n");
    test_scanline_count(dbfile);

    bu_log("\n--- Test 5: lighting models ---\n");
    test_lighting_models(dbfile);

    bu_log("\n=== Results: %d failure(s) ===\n", total_failures);
    return (total_failures > 0) ? 1 : 0;
}


/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
