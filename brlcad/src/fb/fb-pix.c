/*                        F B - P I X . C
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
/** @file fb-pix.c
 *
 * Program to take a frame buffer image and write a .pix image.
 *
 * In Obol builds this tool speaks the fbserv PKG wire protocol
 * (MSG_FBOPEN / MSG_FBREADRECT / MSG_FBCLOSE) directly via libpkg,
 * with no libdm dependency.  The non-Obol path is unchanged and uses
 * the libdm fb_open / fb_read / fb_close API.
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bio.h"

#include "bu/app.h"
#include "bu/getopt.h"
#include "bu/exit.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "vmath.h"

#ifndef BRLCAD_ENABLE_OBOL
#  include "dm.h"
/* in cmap-crunch.c */
extern void cmap_crunch(RGBpixel (*scan_buf), int pixel_ct, ColorMap *colormap);
#endif

#include "pkg.h"
#include "dm/fbserv.h"   /* MSG_FB* constants */


char *framebuffer = NULL;
char *file_name;
FILE *outfp;

static int crunch = 0;		/* Color map crunch? */
static int inverse = 0;		/* Draw upside-down */
int screen_height;			/* input height */
int screen_width;			/* input width */


int
get_args(int argc, char **argv)
{
    int c;

    while ((c = bu_getopt(argc, argv, "ciF:s:w:n:h?")) != -1) {
	switch (c) {
	    case 'c':
		crunch = 1;
		break;
	    case 'i':
		inverse = 1;
		break;
	    case 'F':
		framebuffer = bu_optarg;
		break;
	    case 's':
		/* square size */
		screen_height = screen_width = atoi(bu_optarg);
		break;
	    case 'w':
		screen_width = atoi(bu_optarg);
		break;
	    case 'n':
		screen_height = atoi(bu_optarg);
		break;

	    default:		/* '?' */
		return 0;
	}
    }

    if (bu_optind >= argc) {
	if (isatty(fileno(stdout)))
	    return 0;
	file_name = "-";
	outfp = stdout;
	setmode(fileno(stdout), O_BINARY);
    } else {
	file_name = argv[bu_optind];
	if ((outfp = fopen(file_name, "wb")) == NULL) {
	    fprintf(stderr,
		    "fb-pix: cannot open \"%s\" for writing\n",
		    file_name);
	    return 0;
	}
	(void)bu_fchmod(fileno(outfp), 0444);
    }

    if (argc > ++bu_optind)
	fprintf(stderr, "fb-pix: excess argument(s) ignored\n");

    return 1;		/* OK */
}


#ifdef BRLCAD_ENABLE_OBOL

/* ------------------------------------------------------------------ */
/* Obol path: direct PKG wire protocol (no libdm)                      */
/* ------------------------------------------------------------------ */

/* NET_LONG_LEN is 4 bytes, matching pkg_glong / pkg_plong encoding   */
#define FBPIX_NET_LONG_LEN 4

/* Encode a 32-bit big-endian unsigned long into buf (same as htonl). */
static void
fbpix_plong(char *buf, unsigned long val)
{
    unsigned char *p = (unsigned char *)buf;
    p[0] = (unsigned char)((val >> 24) & 0xff);
    p[1] = (unsigned char)((val >> 16) & 0xff);
    p[2] = (unsigned char)((val >>  8) & 0xff);
    p[3] = (unsigned char)((val      ) & 0xff);
}

/* Decode a 32-bit big-endian unsigned long from buf (same as ntohl). */
static unsigned long
fbpix_glong(const char *buf)
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
    unsigned char *scanline;
    int scanbytes;
    int scanpix;
    int y;
    char openbuf[2*FBPIX_NET_LONG_LEN + 2]; /* width + height + empty device */
    char retbuf[5*FBPIX_NET_LONG_LEN + 4];
    char hostbuf[256];
    char portbuf[64];
    const char *colon;

    char usage[] = "\
Usage: fb-pix [-i -c] [-F framebuffer]\n\
\t[-s squaresize] [-w width] [-n height] [file.pix]\n";

    screen_height = screen_width = 512;
    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    if (!framebuffer) {
	(void)fputs(usage, stderr);
	bu_exit(12, "fb-pix: -F framebuffer is required in Obol builds\n");
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
    if (pc == PKC_ERROR) {
	bu_exit(12, "fb-pix: cannot connect to fbserv at %s:%s\n",
		hostbuf, portbuf);
    }

    /* MSG_FBOPEN: [width(4B)][height(4B)]  (device string is empty) */
    memset(openbuf, 0, sizeof(openbuf));
    fbpix_plong(&openbuf[0*FBPIX_NET_LONG_LEN], (unsigned long)screen_width);
    fbpix_plong(&openbuf[1*FBPIX_NET_LONG_LEN], (unsigned long)screen_height);
    if (pkg_send(MSG_FBOPEN, openbuf, 2*FBPIX_NET_LONG_LEN, pc) < 2*FBPIX_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fb-pix: MSG_FBOPEN send failed\n");
    }

    /* Response: [ret(4B)][max_w(4B)][max_h(4B)][w(4B)][h(4B)] */
    if (pkg_waitfor(MSG_RETURN, retbuf, sizeof(retbuf), pc) < 5*FBPIX_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fb-pix: MSG_FBOPEN reply too short\n");
    }
    if (fbpix_glong(&retbuf[0*FBPIX_NET_LONG_LEN]) != 0) {
	pkg_close(pc);
	bu_exit(1, "fb-pix: fbserv refused open\n");
    }
    {
	int srv_w = (int)fbpix_glong(&retbuf[3*FBPIX_NET_LONG_LEN]);
	int srv_h = (int)fbpix_glong(&retbuf[4*FBPIX_NET_LONG_LEN]);
	if (screen_width  > srv_w) screen_width  = srv_w;
	if (screen_height > srv_h) screen_height = srv_h;
    }

    scanpix  = screen_width;
    scanbytes = scanpix * 3;  /* always 3 bytes/pixel (RGB) for .pix */
    scanline  = (unsigned char *)bu_malloc((size_t)scanbytes, "scanline");

    /* Read rows.  BRL-CAD fb convention: y=0 is the bottom row.
     * .pix convention: first row written is the bottom row.
     * Default (inverse=0): start at y=0, go up to screen_height-1.
     * Inverse (inverse=1): start at y=screen_height-1, go down to 0.
     */
    {
	int start_y = inverse ? screen_height - 1 : 0;
	int stop_y  = inverse ? -1                : screen_height;
	int step_y  = inverse ? -1                : 1;
	char rrectbuf[4*FBPIX_NET_LONG_LEN + 1];

	for (y = start_y; y != stop_y; y += step_y) {
	    int got;
	    /* Use MSG_FBREADRECT: [xmin(4B)][ymin(4B)][w(4B)][h(4B)] */
	    fbpix_plong(&rrectbuf[0*FBPIX_NET_LONG_LEN], 0);
	    fbpix_plong(&rrectbuf[1*FBPIX_NET_LONG_LEN], (unsigned long)y);
	    fbpix_plong(&rrectbuf[2*FBPIX_NET_LONG_LEN], (unsigned long)screen_width);
	    fbpix_plong(&rrectbuf[3*FBPIX_NET_LONG_LEN], 1); /* one row */
	    if (pkg_send(MSG_FBREADRECT, rrectbuf, 4*FBPIX_NET_LONG_LEN, pc) < 4*FBPIX_NET_LONG_LEN)
		break;
	    got = (int)pkg_waitfor(MSG_RETURN, (char *)scanline, scanbytes, pc);
	    if (got <= 0) {
		bu_log("fb-pix: read of row %d failed (got %d)\n", y, got);
		break;
	    }
	    if (fwrite(scanline, (size_t)scanbytes, 1, outfp) != 1) {
		perror("fwrite");
		break;
	    }
	}
    }

    /* MSG_FBCLOSE */
    {
	char closeret[FBPIX_NET_LONG_LEN + 1];
	(void)pkg_send(MSG_FBCLOSE, NULL, 0, pc);
	(void)pkg_waitfor(MSG_RETURN, closeret, sizeof(closeret), pc);
    }

    pkg_close(pc);
    bu_free(scanline, "scanline");

    if (outfp != stdout)
	fclose(outfp);

    return 0;
}

#else /* !BRLCAD_ENABLE_OBOL ---------------------------------------- */

int
main(int argc, char **argv)
{
    struct fb *fbp;
    int y;

    unsigned char *scanline = NULL;	/* 1 scanline pixel buffer */
    int scanbytes;		/* # of bytes of scanline */
    int scanpix;		/* # of pixels of scanline */
    ColorMap cmap;		/* libfb color map */

    char usage[] = "\
Usage: fb-pix [-i -c] [-F framebuffer]\n\
	[-s squaresize] [-w width] [-n height] [file.pix]\n";

    screen_height = screen_width = 512;		/* Defaults */

    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    setmode(fileno(stdout), O_BINARY);

    scanpix = screen_width;
    scanbytes = scanpix * sizeof(RGBpixel);
    if ((scanline = (unsigned char *)malloc(scanbytes)) == RGBPIXEL_NULL) {
	fprintf(stderr,
		"fb-pix:  malloc(%d) failure\n", scanbytes);
	bu_exit(2, NULL);
    }

    if ((fbp = fb_open(framebuffer, screen_width, screen_height)) == NULL) {
	bu_exit(12, NULL);
    }

    V_MIN(screen_height, fb_getheight(fbp));
    V_MIN(screen_width, fb_getwidth(fbp));

    if (crunch) {
	if (fb_rmap(fbp, &cmap) == -1) {
	    crunch = 0;
	} else if (fb_is_linear_cmap(&cmap)) {
	    crunch = 0;
	}
    }

    if (!inverse) {
	/* Regular -- read bottom to top */
	for (y=0; y < screen_height; y++) {
	    fb_read(fbp, 0, y, scanline, screen_width);
	    if (crunch)
		cmap_crunch((RGBpixel *)scanline, scanpix, &cmap);
	    if (fwrite((char *)scanline, scanbytes, 1, outfp) != 1) {
		perror("fwrite");
		break;
	    }
	}
    } else {
	/* Inverse -- read top to bottom */
	for (y = screen_height-1; y >= 0; y--) {
	    fb_read(fbp, 0, y, scanline, screen_width);
	    if (crunch)
		cmap_crunch((RGBpixel *)scanline, scanpix, &cmap);
	    if (fwrite((char *)scanline, scanbytes, 1, outfp) != 1) {
		perror("fwrite");
		break;
	    }
	}
    }
    fb_close(fbp);
    if (scanline)
	bu_free(scanline, "scanline");
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
