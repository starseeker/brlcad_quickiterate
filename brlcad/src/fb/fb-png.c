/*                        F B - P N G . C
 * BRL-CAD
 *
 * Copyright (c) 1998-2025 United States Government as represented by
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
/** @file fb-png.c
 *
 * Program to take a frame buffer image and write a PNG (Portable
 * Network Graphics) format file.
 *
 * In Obol builds this tool speaks the fbserv PKG wire protocol
 * (MSG_FBOPEN / MSG_FBREAD / MSG_FBCLOSE) directly via libpkg, with
 * no libdm dependency.  The non-Obol path is unchanged and uses the
 * libdm fb_open / fb_read / fb_close API.
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "png.h"

#include "bio.h"

#include "bu/app.h"
#include "bu/getopt.h"
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


static int crunch = 0;		/* Color map crunch? */
static int inverse = 0;		/* Draw upside-down */
static int pixbytes = 3;	/* Default is 3 bytes/pixel */
int screen_height;		/* input height */
int screen_width;		/* input width */

double out_gamma = -1.0;	/* Gamma the image was created at */
char *framebuffer = NULL;
FILE *outfp;


int
get_args(int argc, char **argv)
{
    int c;
    char *file_name = NULL;

    while ((c = bu_getopt(argc, argv, "ciF:s:w:n:g:#:h?")) != -1) {
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
	    case 'g':
		out_gamma = atof(bu_optarg);
		break;
	    case '#':
		pixbytes = atoi(bu_optarg);
		if (pixbytes != 1 && pixbytes != 3)
		    bu_exit(EXIT_FAILURE, "fb-png: Only able to handle 1 and 3 byte pixels\n");
		break;

	    default:		/* '?' 'h' */
		return 0;
	}
    }

    if (bu_optind >= argc) {
	if (isatty(fileno(stdout)))
	    return 0;
	outfp = stdout;
	setmode(fileno(stdout), O_BINARY);
    } else {
	file_name = argv[bu_optind];
	if ((outfp = fopen(file_name, "wb")) == NULL) {
	    bu_log("fb-png: cannot open \"%s\" for writing\n", file_name);
	    return 0;
	}
	(void)bu_fchmod(fileno(outfp), 0444);
    }

    if (argc > ++bu_optind)
	bu_log("fb-png: excess argument(s) ignored\n");

    return 1;		/* OK */
}


#ifdef BRLCAD_ENABLE_OBOL

/* ------------------------------------------------------------------ */
/* Obol path: direct PKG wire protocol (no libdm)                      */
/* ------------------------------------------------------------------ */

/* NET_LONG_LEN is 4 bytes, matching pkg_glong / pkg_plong encoding   */
#define FBPNG_NLL 4

/* Encode a 32-bit big-endian unsigned long into buf (same as htonl). */
static void
fbpng_plong(char *buf, unsigned long val)
{
    unsigned char *p = (unsigned char *)buf;
    p[0] = (unsigned char)((val >> 24) & 0xff);
    p[1] = (unsigned char)((val >> 16) & 0xff);
    p[2] = (unsigned char)((val >>  8) & 0xff);
    p[3] = (unsigned char)((val      ) & 0xff);
}

/* Decode a 32-bit big-endian unsigned long from buf (same as ntohl). */
static unsigned long
fbpng_glong(const char *buf)
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
    png_structp png_p;
    png_infop info_p;
    unsigned char *scanline;
    int scanbytes;
    int y;
    char openbuf[2*FBPNG_NLL + 2]; /* width + height + empty device */
    char retbuf[5*FBPNG_NLL + 4];
    char hostbuf[256];
    char portbuf[64];
    const char *colon;

    char usage[] = "\
Usage: fb-png [-i -c] [-# nbytes/pixel] [-F framebuffer] [-g gamma]\n\
\t[-s squaresize] [-w width] [-n height] [file.png]\n";

    screen_height = screen_width = 512;
    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    if (!framebuffer) {
	(void)fputs(usage, stderr);
	bu_exit(12, "fb-png: -F framebuffer is required in Obol builds\n");
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
	bu_exit(12, "fb-png: cannot connect to fbserv at %s:%s\n",
		hostbuf, portbuf);
    }

    /* MSG_FBOPEN: [width(4B)][height(4B)]  (device string is empty) */
    memset(openbuf, 0, sizeof(openbuf));
    fbpng_plong(&openbuf[0*FBPNG_NLL], (unsigned long)screen_width);
    fbpng_plong(&openbuf[1*FBPNG_NLL], (unsigned long)screen_height);
    if (pkg_send(MSG_FBOPEN, openbuf, 2*FBPNG_NLL, pc) < 2*FBPNG_NLL) {
	pkg_close(pc);
	bu_exit(1, "fb-png: MSG_FBOPEN send failed\n");
    }

    /* Response: [ret(4B)][max_w(4B)][max_h(4B)][w(4B)][h(4B)] */
    if (pkg_waitfor(MSG_RETURN, retbuf, sizeof(retbuf), pc) < 5*FBPNG_NLL) {
	pkg_close(pc);
	bu_exit(1, "fb-png: MSG_FBOPEN reply too short\n");
    }
    if (fbpng_glong(&retbuf[0*FBPNG_NLL]) != 0) {
	pkg_close(pc);
	bu_exit(1, "fb-png: fbserv refused open\n");
    }
    {
	int srv_w = (int)fbpng_glong(&retbuf[3*FBPNG_NLL]);
	int srv_h = (int)fbpng_glong(&retbuf[4*FBPNG_NLL]);
	if (screen_width  > srv_w) screen_width  = srv_w;
	if (screen_height > srv_h) screen_height = srv_h;
    }

    scanbytes = screen_width * pixbytes;
    scanline  = (unsigned char *)bu_malloc((size_t)scanbytes, "scanline");

    /* PNG writer setup */
    png_p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_p) {
	pkg_close(pc);
	bu_exit(EXIT_FAILURE, "Could not create PNG write structure\n");
    }
    info_p = png_create_info_struct(png_p);
    if (!info_p) {
	pkg_close(pc);
	bu_exit(EXIT_FAILURE, "Could not create PNG info structure\n");
    }

    png_init_io(png_p, outfp);
    png_set_filter(png_p, 0, PNG_FILTER_NONE);
    png_set_compression_level(png_p, 9);
    png_set_IHDR(png_p, info_p,
		 (png_uint_32)screen_width, (png_uint_32)screen_height, 8,
		 pixbytes == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_GRAY,
		 PNG_INTERLACE_NONE,
		 PNG_COMPRESSION_TYPE_DEFAULT,
		 PNG_FILTER_TYPE_DEFAULT);
    if (out_gamma > 0.0)
	png_set_gAMA(png_p, info_p, out_gamma);
    png_write_info(png_p, info_p);

    /* Read rows.  BRL-CAD fb convention: y=0 is the bottom row.
     * PNG convention: first row written is the top.
     * Default (inverse=0): start at y=screen_height-1, go down to 0.
     * Inverse (inverse=1): start at y=0, go up to screen_height-1.
     */
    {
	int start_y = inverse ? 0             : screen_height - 1;
	int stop_y  = inverse ? screen_height : -1;
	int step_y  = inverse ? 1             : -1;
	int msgtype = (pixbytes == 1) ? MSG_FBBWREADRECT : MSG_FBREADRECT;
	char rrectbuf[4*FBPNG_NLL + 1];

	for (y = start_y; y != stop_y; y += step_y) {
	    int got;
	    /* Use MSG_FBREADRECT: [xmin(4B)][ymin(4B)][w(4B)][h(4B)] */
	    fbpng_plong(&rrectbuf[0*FBPNG_NLL], 0);
	    fbpng_plong(&rrectbuf[1*FBPNG_NLL], (unsigned long)y);
	    fbpng_plong(&rrectbuf[2*FBPNG_NLL], (unsigned long)screen_width);
	    fbpng_plong(&rrectbuf[3*FBPNG_NLL], 1); /* one row */
	    if (pkg_send(msgtype, rrectbuf, 4*FBPNG_NLL, pc) < 4*FBPNG_NLL)
		break;
	    got = (int)pkg_waitfor(MSG_RETURN, (char *)scanline, scanbytes, pc);
	    if (got <= 0) {
		bu_log("fb-png: read of row %d failed (got %d)\n", y, got);
		break;
	    }
	    png_write_row(png_p, scanline);
	}
    }

    /* MSG_FBCLOSE */
    {
	char closeret[FBPNG_NLL + 1];
	(void)pkg_send(MSG_FBCLOSE, NULL, 0, pc);
	(void)pkg_waitfor(MSG_RETURN, closeret, sizeof(closeret), pc);
    }

    pkg_close(pc);
    png_write_end(png_p, NULL);

    bu_free(scanline, "scanline");

    if (outfp != stdout)
	fclose(outfp);

    return 0;
}

#else /* !BRLCAD_ENABLE_OBOL ---------------------------------------- */

int
main(int argc, char **argv)
{
    static unsigned char *scanline;	/* scanline pixel buffers */
    static int scanbytes;		/* # of bytes of scanline */
    static int scanpix;			/* # of pixels of scanline */
    static ColorMap cmap;		/* libfb color map */

    struct fb *fbp;
    int y;
    int got;
    png_structp png_p;
    png_infop info_p;

    char usage[] = "\
Usage: fb-png [-i -c] [-# nbytes/pixel] [-F framebuffer] [-g gamma]\n\
\t[-s squaresize] [-w width] [-n height] [file.png]\n";

    screen_height = screen_width = 512;		/* Defaults */

    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    png_p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_p) {
	bu_exit(EXIT_FAILURE, "Could not create PNG write structure\n");
    }

    info_p = png_create_info_struct(png_p);
    if (!info_p) {
	bu_exit(EXIT_FAILURE, "Could not create PNG info structure\n");
    }

    if ((fbp = fb_open(framebuffer, screen_width, screen_height)) == NULL) {
	bu_exit(12, NULL);
    }

    /* If actual screen is smaller than requested size, trim down */
    V_MIN(screen_height, fb_getheight(fbp));
    V_MIN(screen_width, fb_getwidth(fbp));

    scanpix = screen_width;
    scanbytes = scanpix * sizeof(RGBpixel);
    scanline = (unsigned char *)bu_malloc(scanbytes, "scanline");

    if (crunch) {
	if (fb_rmap(fbp, &cmap) == -1) {
	    crunch = 0;
	} else if (fb_is_linear_cmap(&cmap)) {
	    crunch = 0;
	}
    }

    png_init_io(png_p, outfp);
    png_set_filter(png_p, 0, PNG_FILTER_NONE);
    png_set_compression_level(png_p, 9);
    png_set_IHDR(png_p, info_p,
		 screen_width, screen_height, 8,
		 pixbytes == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_GRAY,
		 PNG_INTERLACE_NONE,
		 PNG_COMPRESSION_TYPE_DEFAULT,
		 PNG_FILTER_TYPE_DEFAULT);

    /* default to no gamma correction */
    if (out_gamma > 0.0)
	png_set_gAMA(png_p, info_p, out_gamma);

    png_write_info(png_p, info_p);

    if (inverse) {
	/* Read bottom to top */
	for (y=0; y < screen_height; y++) {
	    if (pixbytes == 3)
		got = fb_read(fbp, 0, y, scanline, screen_width);
	    else
		got = fb_bwreadrect(fbp, 0, y, screen_width, 1, scanline);

	    if (got != screen_width) {
		bu_log("fb-png: Read of scanline %d returned %d, expected %d, aborting.\n",
		       y, got, screen_width);
		break;
	    }
	    if (crunch)
		cmap_crunch((RGBpixel *)scanline, scanpix, &cmap);
	    png_write_row(png_p, scanline);
	}
    } else {
	/* Read top to bottom */
	for (y = screen_height-1; y >= 0; y--) {
	    if (pixbytes == 3)
		got = fb_read(fbp, 0, y, scanline, screen_width);
	    else
		got = fb_bwreadrect(fbp, 0, y, screen_width, 1, scanline);

	    if (got != screen_width) {
		bu_log("fb-png: Read of scanline %d returned %d, expected %d, aborting.\n",
		       y, got, screen_width);
		break;
	    }
	    if (crunch)
		cmap_crunch((RGBpixel *)scanline, scanpix, &cmap);
	    png_write_row(png_p, scanline);
	}
    }
    fb_close(fbp);
    png_write_end(png_p, NULL);
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
