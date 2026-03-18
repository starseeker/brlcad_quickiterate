/*                       F B C L E A R . C
 * BRL-CAD
 *
 * Copyright (c) 1986-2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 *
 */
/** @file fbclear.c
 *
 * This program is intended to be used to clear a frame buffer
 * to black, or to the specified color
 *
 * In Obol builds this tool speaks the fbserv PKG wire protocol
 * (MSG_FBOPEN / MSG_FBCLEAR / MSG_FBCLOSE) directly via libpkg,
 * with no libdm dependency.  The -c (clear-and-reset) flag is
 * accepted but the colormap/viewport reset has no effect in Obol.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/app.h"
#include "bu/color.h"
#include "bu/getopt.h"
#include "bu/exit.h"

#ifndef BRLCAD_ENABLE_OBOL
#  include "dm.h"
#endif

#include "pkg.h"
#include "dm/fbserv.h"   /* MSG_FB* constants */


static char *framebuffer = NULL;
#ifndef BRLCAD_ENABLE_OBOL
static struct fb *fbp;
#endif
static int scr_width = 0;		/* use default size */
static int scr_height = 0;
static int clear_and_reset = 0;

#define u_char unsigned char

static char usage[] = "Usage: fbclear [-c] [-F framebuffer]\n\
	[-{sS} squarescrsize] [-{wW} scr_width] [-{nN} scr_height] [gray | r g b]\n";

int
get_args(int argc, char **argv)
{
    int c;

    while ((c = bu_getopt(argc, argv, "cF:s:w:n:S:W:N:h?")) != -1) {
	switch (c) {
	    case 'c':
		/* clear only, no cmap, pan, and zoom */
		clear_and_reset++;
		break;
	    case 'F':
		framebuffer = bu_optarg;
		break;
	    case 's':
	    case 'S':
		scr_height = scr_width = atoi(bu_optarg);
		break;
	    case 'w':
	    case 'W':
		scr_width = atoi(bu_optarg);
		break;
	    case 'n':
	    case 'N':
		scr_height = atoi(bu_optarg);
		break;

	    default:		/* '?' 'h' */
		return 0;
	}
    }
    return 1;		/* OK */
}


#ifdef BRLCAD_ENABLE_OBOL

/* ------------------------------------------------------------------ */
/* Obol path: direct PKG wire protocol (no libdm)                      */
/* ------------------------------------------------------------------ */

#define FBCLEAR_NET_LONG_LEN 4

static void
fbclear_plong(char *buf, unsigned long val)
{
    unsigned char *p = (unsigned char *)buf;
    p[0] = (unsigned char)((val >> 24) & 0xff);
    p[1] = (unsigned char)((val >> 16) & 0xff);
    p[2] = (unsigned char)((val >>  8) & 0xff);
    p[3] = (unsigned char)((val      ) & 0xff);
}

static unsigned long
fbclear_glong(const char *buf)
{
    const unsigned char *p = (const unsigned char *)buf;
    unsigned long u = p[0]; u <<= 8;
    u |= p[1]; u <<= 8;
    u |= p[2]; u <<= 8;
    return u | p[3];
}

int
main(int argc, char **argv)
{
    struct pkg_conn *pc;
    char hostbuf[256];
    char portbuf[64];
    char openbuf[2*FBCLEAR_NET_LONG_LEN + 2];
    char retbuf[5*FBCLEAR_NET_LONG_LEN + 4];
    char clearbuf[3];
    char closeret[FBCLEAR_NET_LONG_LEN + 1];
    const char *colon;
    unsigned char r = 0, g = 0, b = 0;

    bu_setprogname(argv[0]);
    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    if (!framebuffer) {
	(void)fputs(usage, stderr);
	bu_exit(12, "fbclear: -F framebuffer is required in Obol builds\n");
    }

    if (clear_and_reset)
	fprintf(stderr, "fbclear: -c (colormap/viewport reset) is not functional in Obol builds\n");

    /* Parse color from remaining arguments */
    if (bu_optind + 3 == argc) {
	r = (unsigned char)atoi(argv[bu_optind+0]);
	g = (unsigned char)atoi(argv[bu_optind+1]);
	b = (unsigned char)atoi(argv[bu_optind+2]);
    } else if (bu_optind + 1 == argc) {
	r = g = b = (unsigned char)atoi(argv[bu_optind]);
    } else if (bu_optind != argc) {
	fprintf(stderr, "fbclear: extra arguments ignored\n");
    }

    /* Parse "host:port" or "port" */
    colon = strrchr(framebuffer, ':');
    if (colon && colon != framebuffer) {
	size_t hlen = (size_t)(colon - framebuffer);
	if (hlen >= sizeof(hostbuf)) hlen = sizeof(hostbuf) - 1;
	memcpy(hostbuf, framebuffer, hlen);
	hostbuf[hlen] = '\0';
	snprintf(portbuf, sizeof(portbuf), "%s", colon + 1);
    } else {
	snprintf(hostbuf, sizeof(hostbuf), "localhost");
	snprintf(portbuf, sizeof(portbuf), "%s", colon ? colon + 1 : framebuffer);
    }

    /* Connect to fbserv */
    pc = pkg_open(hostbuf, portbuf, 0, 0, 0, NULL, NULL);
    if (pc == PKC_ERROR)
	bu_exit(12, "fbclear: cannot connect to fbserv at %s:%s\n", hostbuf, portbuf);

    /* MSG_FBOPEN */
    memset(openbuf, 0, sizeof(openbuf));
    fbclear_plong(&openbuf[0*FBCLEAR_NET_LONG_LEN], (unsigned long)scr_width);
    fbclear_plong(&openbuf[1*FBCLEAR_NET_LONG_LEN], (unsigned long)scr_height);
    if (pkg_send(MSG_FBOPEN, openbuf, 2*FBCLEAR_NET_LONG_LEN, pc) < 2*FBCLEAR_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fbclear: MSG_FBOPEN send failed\n");
    }
    if (pkg_waitfor(MSG_RETURN, retbuf, sizeof(retbuf), pc) < 5*FBCLEAR_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fbclear: MSG_FBOPEN reply too short\n");
    }
    if (fbclear_glong(&retbuf[0*FBCLEAR_NET_LONG_LEN]) != 0) {
	pkg_close(pc);
	bu_exit(1, "fbclear: fbserv refused open\n");
    }

    /* MSG_FBCLEAR: payload is 3 bytes [R, G, B] */
    clearbuf[0] = (char)r;
    clearbuf[1] = (char)g;
    clearbuf[2] = (char)b;
    if (pkg_send(MSG_FBCLEAR, clearbuf, 3, pc) < 3) {
	pkg_close(pc);
	bu_exit(1, "fbclear: MSG_FBCLEAR send failed\n");
    }
    (void)pkg_waitfor(MSG_RETURN, closeret, sizeof(closeret), pc);

    /* MSG_FBCLOSE */
    (void)pkg_send(MSG_FBCLOSE, NULL, 0, pc);
    (void)pkg_waitfor(MSG_RETURN, closeret, sizeof(closeret), pc);

    pkg_close(pc);
    return 0;
}

#else /* !BRLCAD_ENABLE_OBOL ---------------------------------------- */

int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);
    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    if ((fbp = fb_open(framebuffer, scr_width, scr_height)) == NULL) {
	bu_exit(2, NULL);
    }

    /* Get the screen size we were given */
    scr_width = fb_getwidth(fbp);
    scr_height = fb_getheight(fbp);

    if (clear_and_reset) {
	if (fb_wmap(fbp, COLORMAP_NULL) < 0)
	    bu_exit(3, NULL);
	(void)fb_view(fbp, scr_width/2, scr_height/2, 1, 1);
    } else {
	ColorMap cmap;
	int xcent, ycent, xzoom, yzoom;
	if (fb_rmap(fbp, &cmap) >= 0) {
	    if (!fb_is_linear_cmap(&cmap)) {
		fprintf(stderr, "fbclear: NOTE: non-linear colormap in effect.  -c flag loads linear colormap.\n");
	    }
	}
	(void)fb_getview(fbp, &xcent, &ycent, &xzoom, &yzoom);
	if (xzoom != 1 || yzoom != 1) {
	    fprintf(stderr, "fbclear:  NOTE: framebuffer is zoomed.  -c will un-zoom.\n");
	}
    }

    if (bu_optind+3 == argc) {
	static RGBpixel pixel;
	pixel[RED] = (u_char) atoi(argv[bu_optind+0]);
	pixel[GRN] = (u_char) atoi(argv[bu_optind+1]);
	pixel[BLU] = (u_char) atoi(argv[bu_optind+2]);
	fb_clear(fbp, pixel);
    } else if (bu_optind+1 == argc) {
	static RGBpixel pixel;
	pixel[RED] = pixel[GRN] = pixel[BLU]
	    = (u_char) atoi(argv[bu_optind+0]);
	fb_clear(fbp, pixel);
    } else {
	if (bu_optind != argc)
	    fprintf(stderr, "fbclear: extra arguments ignored\n");
	fb_clear(fbp, PIXEL_NULL);
    }
    (void)fb_close(fbp);
    return 0;
}

#endif /* BRLCAD_ENABLE_OBOL */


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
