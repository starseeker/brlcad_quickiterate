/*               T E S T _ F B _ D I R T Y _ R E C T . C
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @file libdm/tests/test_fb_dirty_rect.c
 *
 * Regression tests for fb dirty-rectangle tracking.
 */

#include "common.h"

#include "bu/app.h"
#include "bu/log.h"
#define DM_WITH_RT
#include "dm.h"

static int g_fail = 0;

#define CHECK(c, m) do { \
	if (!(c)) { \
	    bu_log("FAIL [%s:%d] %s\n", __FILE__, __LINE__, (m)); \
	    g_fail++; \
	} \
    } while (0)

static void
expect_rect(struct fb *fbp, int ex0, int ey0, int ex1, int ey1, const char *msg)
{
    int x0 = -1, y0 = -1, x1 = -1, y1 = -1;
    int have = fb_dirty_consume(fbp, &x0, &y0, &x1, &y1);
    CHECK(have == 1, msg);
    CHECK(x0 == ex0, "xmin mismatch");
    CHECK(y0 == ey0, "ymin mismatch");
    CHECK(x1 == ex1, "xmax mismatch");
    CHECK(y1 == ey1, "ymax mismatch");
}

int
main(int argc, char **argv)
{
    unsigned char px[3] = {255, 0, 0};
    int width = 11;
    int height = 7;
    int x0 = -1, y0 = -1, x1 = -1, y1 = -1;

    bu_setprogname(argv[0]);
    if (argc != 1) {
	bu_log("Usage: %s\n", argv[0]);
	return 1;
    }

    struct fb *fbp = fb_open("/dev/null", width, height);
    CHECK(fbp != FB_NULL, "fb_open(/dev/null) succeeded");
    if (!fbp)
	return 1;

    CHECK(fb_dirty_consume(fbp, &x0, &y0, &x1, &y1) == 0, "initial dirty region empty");

    /* Last pixel (rightmost column, bottom row) must be included in dirty bounds. */
    CHECK(fb_write(fbp, width - 1, height - 1, px, 1) == 1, "fb_write last pixel");
    expect_rect(fbp, width - 1, height - 1, width - 1, height - 1, "consume last-pixel rect");

    /* Out-of-bounds writes must clamp cleanly and preserve edge rows/cols. */
    CHECK(fb_writerect(fbp, width - 2, height - 2, 8, 8, px) > 0, "fb_writerect edge clamp");
    expect_rect(fbp, width - 2, height - 2, width - 1, height - 1, "consume clamped edge rect");

    CHECK(fb_writerect(fbp, -6, -4, 8, 8, px) > 0, "fb_writerect negative origin clamp");
    expect_rect(fbp, 0, 0, 1, 3, "consume clamped origin rect");

    /* Multiple writes before consume should union correctly. */
    CHECK(fb_write(fbp, 0, height - 1, px, 1) == 1, "fb_write top-left edge");
    CHECK(fb_write(fbp, width - 1, 0, px, 1) == 1, "fb_write bottom-right edge");
    expect_rect(fbp, 0, 0, width - 1, height - 1, "consume unioned edge writes");

    /* Clear and view operations should request full refresh. */
    CHECK(fb_clear(fbp, PIXEL_NULL) == 0, "fb_clear");
    expect_rect(fbp, 0, 0, width - 1, height - 1, "consume clear full rect");

    CHECK(fb_view(fbp, width / 2, height / 2, 1, 1) == 0, "fb_view");
    expect_rect(fbp, 0, 0, width - 1, height - 1, "consume view full rect");

    CHECK(fb_dirty_consume(fbp, &x0, &y0, &x1, &y1) == 0, "dirty region resets after consume");

    fb_close(fbp);

    if (g_fail) {
	bu_log("RESULT: %d failure(s)\n", g_fail);
	return 1;
    }
    bu_log("RESULT: fb dirty-rect tests PASSED\n");
    return 0;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
