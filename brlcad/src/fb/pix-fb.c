/*                        P I X - F B . C
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
/** @file pix-fb.c
 *
 * Program to take bottom-up pixel files and send them to a framebuffer.
 *
 * In Obol builds this tool speaks the fbserv PKG wire protocol
 * (MSG_FBOPEN / MSG_FBWRITERECT / MSG_FBCLOSE) directly via libpkg,
 * with no libdm dependency.  The non-Obol path is unchanged and uses
 * the libdm fb_open / fb_write / fb_close API.
 */

#include "common.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#  include <sys/stat.h>
#endif
#ifdef HAVE_WINSOCK_H
#  include <winsock.h>
#endif

#include "bio.h"

#include "bu/app.h"
#include "bu/getopt.h"
#include "bu/malloc.h"
#include "bu/file.h"
#include "bu/exit.h"
#include "bu/log.h"
#include "bu/snooze.h"
#include "vmath.h"

#ifndef BRLCAD_ENABLE_OBOL
#  include "dm.h"
#endif

#include "pkg.h"
#include "dm/fbserv.h"   /* MSG_FB* constants */


static unsigned char *scanline;	/* 1 scanline pixel buffer */
static int scanbytes;		/* # of bytes of scanline */
static int scanpix;		/* # of pixels of scanline */

static int multiple_lines = 0;	/* Streamlined operation */

static char *framebuffer = NULL;
static char *file_name;
static int infd;

static int fileinput = 0;	/* file of pipe on input? */
static int autosize = 0;	/* !0 to autosize input */

static size_t file_width = 512;	/* default input width */
static size_t file_height = 512;/* default input height */
static int scr_width = 0;	/* screen tracks file if not given */
static int scr_height = 0;
static int file_xoff, file_yoff;
static int scr_xoff, scr_yoff;
static int clear = 0;
static int zoom = 0;
static int inverse = 0;		/* Draw upside-down */
static int one_line_only = 0;	/* insist on 1-line writes */
static int pause_sec = 0; 	/* Pause that many seconds before
				   closing the FB and exiting */

static char usage[] = "\
Usage: pix-fb [-a -i -c -z -1] [-m #lines] [-F framebuffer]\n\
	[-s squarefilesize] [-w file_width] [-n file_height]\n\
	[-x file_xoff] [-y file_yoff] [-X scr_xoff] [-Y scr_yoff]\n\
	[-S squarescrsize] [-W scr_width] [-N scr_height] [-p seconds]\n\
	[file.pix]\n";

int
get_args(int argc, char **argv)
{
    int c;

    while ((c = bu_getopt(argc, argv, "1m:aiczF:p:s:w:n:x:y:X:Y:S:W:N:h?")) != -1) {
	switch (c) {
	    case '1':
		one_line_only = 1;
		break;
	    case 'm':
		multiple_lines = atoi(bu_optarg);
		break;
	    case 'a':
		autosize = 1;
		break;
	    case 'i':
		inverse = 1;
		break;
	    case 'c':
		clear = 1;
		break;
	    case 'z':
		zoom = 1;
		break;
	    case 'F':
		framebuffer = bu_optarg;
		break;
	    case 's':
		/* square file size */
		file_height = file_width = atoi(bu_optarg);
		autosize = 0;
		break;
	    case 'w':
		file_width = atoi(bu_optarg);
		autosize = 0;
		break;
	    case 'n':
		file_height = atoi(bu_optarg);
		autosize = 0;
		break;
	    case 'x':
		file_xoff = atoi(bu_optarg);
		break;
	    case 'y':
		file_yoff = atoi(bu_optarg);
		break;
	    case 'X':
		scr_xoff = atoi(bu_optarg);
		break;
	    case 'Y':
		scr_yoff = atoi(bu_optarg);
		break;
	    case 'S':
		scr_height = scr_width = atoi(bu_optarg);
		break;
	    case 'W':
		scr_width = atoi(bu_optarg);
		break;
	    case 'N':
		scr_height = atoi(bu_optarg);
		break;
	    case 'p':
		pause_sec=atoi(bu_optarg);
		break;

	    default:		/* '?' */
		return 0;
	}
    }

    if (bu_optind >= argc) {
	if (isatty(fileno(stdin)))
	    return 0;
	file_name = "-";
	infd = fileno(stdin);
	setmode(fileno(stdin), O_BINARY);
    } else {
	char *ifname;
	file_name = argv[bu_optind];
	ifname = bu_file_realpath(file_name, NULL);
	if ((infd = open(ifname, O_RDONLY|O_BINARY)) < 0) {
	    perror(ifname);
	    fprintf(stderr,
		    "pix-fb: cannot open \"%s(canonical %s)\" for reading\n",
		    file_name, ifname);
	    bu_free(ifname, "ifname alloc from bu_file_realpath");
	    bu_exit(1, NULL);
	}
	bu_free(ifname, "ifname alloc from bu_file_realpath");
	fileinput++;
    }

    if (argc > ++bu_optind)
	fprintf(stderr, "pix-fb: excess argument(s) ignored\n");

    return 1;		/* OK */
}


/*
 * Throw bytes away.  Use reads into scanline buffer if a pipe, else seek.
 */
static int
skipbytes(int fd, b_off_t num)
{
    int n, tries;

    if (fileinput) {
	(void)bu_lseek(fd, num, 1);
	return 0;
    }

    while (num > 0) {
	tries = num > scanbytes ? scanbytes : num;
	n = read(fd, scanline, tries);
	if (n <= 0) {
	    return -1;
	}
	num -= n;
    }
    return 0;
}


#ifdef BRLCAD_ENABLE_OBOL

/* ------------------------------------------------------------------ */
/* Obol path: direct PKG wire protocol (no libdm)                      */
/* ------------------------------------------------------------------ */

/* NET_LONG_LEN is 4 bytes, matching pkg_glong / pkg_plong encoding   */
#define PIXFB_NET_LONG_LEN 4

/* Encode a 32-bit big-endian unsigned long into buf (same as htonl). */
static void
pixfb_plong(char *buf, unsigned long val)
{
    unsigned char *p = (unsigned char *)buf;
    p[0] = (unsigned char)((val >> 24) & 0xff);
    p[1] = (unsigned char)((val >> 16) & 0xff);
    p[2] = (unsigned char)((val >>  8) & 0xff);
    p[3] = (unsigned char)((val      ) & 0xff);
}

/* Decode a 32-bit big-endian unsigned long from buf (same as ntohl). */
static unsigned long
pixfb_glong(const char *buf)
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
    int y;
    struct pkg_conn *pc;
    int xout, xstart, xskip;
    int yout;
    char openbuf[2*PIXFB_NET_LONG_LEN + 2];
    char retbuf[5*PIXFB_NET_LONG_LEN + 4];
    char hostbuf[256];
    char portbuf[64];
    const char *colon;
    unsigned char *wrectbuf;

    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    if (!framebuffer) {
	(void)fputs(usage, stderr);
	bu_exit(12, "pix-fb: -F framebuffer is required in Obol builds\n");
    }

    /* autosize input? */
    if (fileinput && autosize) {
	struct stat st;
	if (fstat(infd, &st) == 0 && st.st_size > 0) {
	    size_t npix = (size_t)st.st_size / 3;
	    /* Try to find a square: width such that width * height = npix */
	    double sqrtval = sqrt((double)npix);
	    size_t w = (size_t)sqrtval;
	    if (w > 0 && w * w == npix) {
		file_width = file_height = w;
	    } else {
		fprintf(stderr, "pix-fb: unable to autosize\n");
	    }
	} else {
	    fprintf(stderr, "pix-fb: unable to autosize\n");
	}
    }

    /* If screen size was not set, track the file size */
    if (scr_width == 0)
	scr_width = (int)file_width;
    if (scr_height == 0)
	scr_height = (int)file_height;

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
	bu_exit(12, "pix-fb: cannot connect to fbserv at %s:%s\n",
		hostbuf, portbuf);
    }

    /* MSG_FBOPEN: [width(4B)][height(4B)] */
    memset(openbuf, 0, sizeof(openbuf));
    pixfb_plong(&openbuf[0*PIXFB_NET_LONG_LEN], (unsigned long)scr_width);
    pixfb_plong(&openbuf[1*PIXFB_NET_LONG_LEN], (unsigned long)scr_height);
    if (pkg_send(MSG_FBOPEN, openbuf, 2*PIXFB_NET_LONG_LEN, pc) < 2*PIXFB_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "pix-fb: MSG_FBOPEN send failed\n");
    }

    /* Response: [ret(4B)][max_w(4B)][max_h(4B)][w(4B)][h(4B)] */
    if (pkg_waitfor(MSG_RETURN, retbuf, sizeof(retbuf), pc) < 5*PIXFB_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "pix-fb: MSG_FBOPEN reply too short\n");
    }
    if (pixfb_glong(&retbuf[0*PIXFB_NET_LONG_LEN]) != 0) {
	pkg_close(pc);
	bu_exit(1, "pix-fb: fbserv refused open\n");
    }
    {
	int srv_w = (int)pixfb_glong(&retbuf[3*PIXFB_NET_LONG_LEN]);
	int srv_h = (int)pixfb_glong(&retbuf[4*PIXFB_NET_LONG_LEN]);
	if (scr_width  > srv_w) scr_width  = srv_w;
	if (scr_height > srv_h) scr_height = srv_h;
    }

    /* Clear the framebuffer if requested: send MSG_FBCLEAR with black (0,0,0) */
    if (clear) {
	char clearbuf[4] = {0, 0, 0, 0};
	char clearret[PIXFB_NET_LONG_LEN + 1];
	(void)pkg_send(MSG_FBCLEAR, clearbuf, 3, pc);
	(void)pkg_waitfor(MSG_RETURN, clearret, sizeof(clearret), pc);
    }

    /* Zoom is not supported in the Obol PKG path */
    if (zoom) {
	fprintf(stderr, "pix-fb: -z (zoom) not supported in Obol builds, ignored\n");
    }

    /* Compute output dimensions */
    if (scr_xoff < 0) {
	xout   = scr_width + scr_xoff;
	xskip  = (-scr_xoff);
	xstart = 0;
    } else {
	xout   = scr_width - scr_xoff;
	xskip  = 0;
	xstart = scr_xoff;
    }
    if (xout < 0) {
	pkg_close(pc);
	return 0;
    }
    if ((size_t)xout > file_width - (size_t)file_xoff)
	xout = (int)(file_width - (size_t)file_xoff);
    scanpix = xout;

    if (inverse)
	scr_yoff = (-scr_yoff);

    yout = scr_height - scr_yoff;
    if (yout < 0) {
	pkg_close(pc);
	return 0;
    }
    if ((size_t)yout > file_height - (size_t)file_yoff)
	yout = (int)(file_height - (size_t)file_yoff);

    /* Only in the simplest case use multi-line writes */
    if (!one_line_only
	&& multiple_lines > 0
	&& !inverse
	&& (size_t)xout == file_width
	&& file_width <= (size_t)scr_width)
    {
	scanpix *= multiple_lines;
    }

    scanbytes = scanpix * 3;  /* 3 bytes/pixel (RGB) */
    scanline = (unsigned char *)bu_malloc((size_t)scanbytes, "scanline");

    /* For MSG_FBWRITERECT we send the header (4 longs) + pixel data together */
    wrectbuf = (unsigned char *)bu_malloc(4*PIXFB_NET_LONG_LEN + (size_t)scanbytes, "wrectbuf");

    if (file_yoff != 0)
	skipbytes(infd, (b_off_t)file_yoff * (b_off_t)file_width * 3);

    if (multiple_lines && !one_line_only && !inverse
	&& (size_t)xout == file_width
	&& file_width <= (size_t)scr_width) {
	/* Multi-line path */
	unsigned long height;
	int n;
	for (y = scr_yoff; y < scr_yoff + yout; y += multiple_lines) {
	    n = bu_mread(infd, (char *)scanline, scanbytes);
	    if (n <= 0) break;
	    height = multiple_lines;
	    if (n != scanbytes) {
		height = ((size_t)n / 3 + (size_t)xout - 1) / (size_t)xout;
		if (height <= 0) break;
	    }
	    if ((size_t)(y + height) > (size_t)(scr_yoff + yout))
		height = (unsigned long)(scr_yoff + yout - y);
	    if (!height) break;
	    pixfb_plong((char *)&wrectbuf[0*PIXFB_NET_LONG_LEN], (unsigned long)scr_xoff);
	    pixfb_plong((char *)&wrectbuf[1*PIXFB_NET_LONG_LEN], (unsigned long)y);
	    pixfb_plong((char *)&wrectbuf[2*PIXFB_NET_LONG_LEN], (unsigned long)file_width);
	    pixfb_plong((char *)&wrectbuf[3*PIXFB_NET_LONG_LEN], height);
	    memcpy(&wrectbuf[4*PIXFB_NET_LONG_LEN], scanline, n);
	    {
		size_t sendlen = 4*PIXFB_NET_LONG_LEN + file_width * height * 3;
		char wret[PIXFB_NET_LONG_LEN + 1];
		if (pkg_send(MSG_FBWRITERECT, (char *)wrectbuf, (int)sendlen, pc) < (int)sendlen)
		    break;
		(void)pkg_waitfor(MSG_RETURN, wret, sizeof(wret), pc);
	    }
	}
    } else if (!inverse) {
	/* Normal: bottom to top */
	int n;
	for (y = scr_yoff; y < scr_yoff + yout; y++) {
	    if (y < 0 || y > scr_height) {
		skipbytes(infd, (b_off_t)file_width * 3);
		continue;
	    }
	    if (file_xoff + xskip != 0)
		skipbytes(infd, (b_off_t)(file_xoff + xskip) * 3);
	    n = bu_mread(infd, (char *)scanline, scanbytes);
	    if (n <= 0) break;
	    pixfb_plong((char *)&wrectbuf[0*PIXFB_NET_LONG_LEN], (unsigned long)xstart);
	    pixfb_plong((char *)&wrectbuf[1*PIXFB_NET_LONG_LEN], (unsigned long)y);
	    pixfb_plong((char *)&wrectbuf[2*PIXFB_NET_LONG_LEN], (unsigned long)xout);
	    pixfb_plong((char *)&wrectbuf[3*PIXFB_NET_LONG_LEN], 1UL);
	    memcpy(&wrectbuf[4*PIXFB_NET_LONG_LEN], scanline, scanbytes);
	    {
		int sendlen = 4*PIXFB_NET_LONG_LEN + scanbytes;
		char wret[PIXFB_NET_LONG_LEN + 1];
		if (pkg_send(MSG_FBWRITERECT, (char *)wrectbuf, sendlen, pc) < sendlen)
		    break;
		(void)pkg_waitfor(MSG_RETURN, wret, sizeof(wret), pc);
	    }
	    /* slop at the end of the line? */
	    if ((size_t)file_xoff + xskip + scanpix < file_width)
		skipbytes(infd, (b_off_t)(file_width - (size_t)file_xoff - xskip - scanpix) * 3);
	}
    } else {
	/* Inverse: top to bottom */
	int n;
	for (y = scr_height - 1 - scr_yoff; y >= scr_height - scr_yoff - yout; y--) {
	    if (y < 0 || y >= scr_height) {
		skipbytes(infd, (b_off_t)file_width * 3);
		continue;
	    }
	    if (file_xoff + xskip != 0)
		skipbytes(infd, (b_off_t)(file_xoff + xskip) * 3);
	    n = bu_mread(infd, (char *)scanline, scanbytes);
	    if (n <= 0) break;
	    pixfb_plong((char *)&wrectbuf[0*PIXFB_NET_LONG_LEN], (unsigned long)xstart);
	    pixfb_plong((char *)&wrectbuf[1*PIXFB_NET_LONG_LEN], (unsigned long)y);
	    pixfb_plong((char *)&wrectbuf[2*PIXFB_NET_LONG_LEN], (unsigned long)xout);
	    pixfb_plong((char *)&wrectbuf[3*PIXFB_NET_LONG_LEN], 1UL);
	    memcpy(&wrectbuf[4*PIXFB_NET_LONG_LEN], scanline, scanbytes);
	    {
		int sendlen = 4*PIXFB_NET_LONG_LEN + scanbytes;
		char wret[PIXFB_NET_LONG_LEN + 1];
		if (pkg_send(MSG_FBWRITERECT, (char *)wrectbuf, sendlen, pc) < sendlen)
		    break;
		(void)pkg_waitfor(MSG_RETURN, wret, sizeof(wret), pc);
	    }
	    /* slop at the end of the line? */
	    if ((size_t)file_xoff + xskip + scanpix < file_width)
		skipbytes(infd, (b_off_t)(file_width - (size_t)file_xoff - xskip - scanpix) * 3);
	}
    }

    bu_snooze(BU_SEC2USEC(pause_sec));

    /* MSG_FBCLOSE */
    {
	char closeret[PIXFB_NET_LONG_LEN + 1];
	(void)pkg_send(MSG_FBCLOSE, NULL, 0, pc);
	(void)pkg_waitfor(MSG_RETURN, closeret, sizeof(closeret), pc);
    }

    pkg_close(pc);
    bu_free(scanline, "scanline");
    bu_free(wrectbuf, "wrectbuf");

    return 0;
}

#else /* !BRLCAD_ENABLE_OBOL ---------------------------------------- */

int
main(int argc, char **argv)
{
    int y;
    struct fb *fbp;
    int xout, yout, n, m, xstart, xskip;

    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    /* autosize input? */
    if (fileinput && autosize) {
	size_t w, h;
	if (fb_common_file_size(&w, &h, file_name, 3)) {
	    file_width = w;
	    file_height = h;
	} else {
	    fprintf(stderr, "pix-fb: unable to autosize\n");
	}
    }

    /* If screen size was not set, track the file size */
    if (scr_width == 0)
	scr_width = file_width;
    if (scr_height == 0)
	scr_height = file_height;

    if ((fbp = fb_open(framebuffer, scr_width, scr_height)) == NULL) {
	bu_exit(12, NULL);
    }

    /* Get the screen size we were given */
    scr_width = fb_getwidth(fbp);
    scr_height = fb_getheight(fbp);

    /* compute number of pixels to be output to screen */
    if (scr_xoff < 0) {
	xout = scr_width + scr_xoff;
	xskip = (-scr_xoff);
	xstart = 0;
    } else {
	xout = scr_width - scr_xoff;
	xskip = 0;
	xstart = scr_xoff;
    }

    if (xout < 0)
	bu_exit(0, NULL);			/* off screen */
    if ((size_t)xout > (file_width-file_xoff))
	xout = (file_width-file_xoff);
    scanpix = xout;				/* # pixels on scanline */

    if (inverse)
	scr_yoff = (-scr_yoff);

    yout = scr_height - scr_yoff;
    if (yout < 0)
	bu_exit(0, NULL);			/* off screen */
    if ((size_t)yout > (file_height-file_yoff))
	yout = (file_height-file_yoff);

    /* Only in the simplest case use multi-line writes */
    if (!one_line_only
	&& multiple_lines > 0
	&& !inverse
	&& !zoom
	&& (size_t)xout == file_width
	&& file_width <= (size_t)scr_width)
    {
	scanpix *= multiple_lines;
    }

    scanbytes = scanpix * sizeof(RGBpixel);
    if ((scanline = (unsigned char *)malloc(scanbytes)) == RGBPIXEL_NULL) {
	fprintf(stderr,
		"pix-fb:  malloc(%d) failure for scanline buffer\n",
		scanbytes);
	bu_exit(2, NULL);
    }

    if (clear) {
	fb_clear(fbp, PIXEL_NULL);
    }
    if (zoom) {
	/* Zoom in, and center the display.  Use square zoom. */
	int zoomit;
	zoomit = scr_width/xout;
	V_MIN(zoomit, scr_height/yout);

	if (inverse) {
	    fb_view(fbp,
		    scr_xoff+xout/2, scr_height-1-(scr_yoff+yout/2),
		    zoomit, zoomit);
	} else {
	    fb_view(fbp,
		    scr_xoff+xout/2, scr_yoff+yout/2,
		    zoomit, zoomit);
	}
    }

    if (file_yoff != 0) skipbytes(infd, (b_off_t)file_yoff*(b_off_t)file_width*sizeof(RGBpixel));

    if (multiple_lines) {
	/* Bottom to top with multi-line reads & writes */
	unsigned long height;
	for (y = scr_yoff; y < scr_yoff + yout; y += multiple_lines) {
	    n = bu_mread(infd, (char *)scanline, scanbytes);
	    if (n <= 0) break;
	    height = multiple_lines;
	    if (n != scanbytes) {
		height = (n/sizeof(RGBpixel)+xout-1)/xout;
		if (height <= 0) break;
	    }
	    /* Don't over-write */
	    if ((size_t)(y + height) > (size_t)(scr_yoff + yout))
		height = scr_yoff + yout - y;
	    if (height <= 0) break;
	    m = fb_writerect(fbp, scr_xoff, y,
			     file_width, height,
			     scanline);
	    if ((size_t)m != file_width*height) {
		fprintf(stderr,
			"pix-fb: fb_writerect(x=%d, y=%d, w=%lu, h=%lu) failure, ret=%d, s/b=%d\n",
			scr_xoff, y,
			(unsigned long)file_width, height, m, scanbytes);
	    }
	}
    } else if (!inverse) {
	/* Normal way -- bottom to top */
	for (y = scr_yoff; y < scr_yoff + yout; y++) {
	    if (y < 0 || y > scr_height) {
		skipbytes(infd, (b_off_t)file_width*sizeof(RGBpixel));
		continue;
	    }
	    if (file_xoff+xskip != 0)
		skipbytes(infd, (b_off_t)(file_xoff+xskip)*sizeof(RGBpixel));
	    n = bu_mread(infd, (char *)scanline, scanbytes);
	    if (n <= 0) break;
	    m = fb_write(fbp, xstart, y, scanline, xout);
	    if (m != xout) {
		fprintf(stderr,
			"pix-fb: fb_write(x=%d, y=%d, npix=%d) ret=%d, s/b=%d\n",
			scr_xoff, y, xout,
			m, xout);
	    }
	    /* slop at the end of the line? */
	    if ((size_t)file_xoff+xskip+scanpix < file_width)
		skipbytes(infd, (b_off_t)(file_width-file_xoff-xskip-scanpix)*sizeof(RGBpixel));
	}
    } else {
	/* Inverse -- top to bottom */
	for (y = scr_height-1-scr_yoff; y >= scr_height-scr_yoff-yout; y--) {
	    if (y < 0 || y >= scr_height) {
		skipbytes(infd, (b_off_t)file_width*sizeof(RGBpixel));
		continue;
	    }
	    if (file_xoff+xskip != 0)
		skipbytes(infd, (b_off_t)(file_xoff+xskip)*sizeof(RGBpixel));
	    n = bu_mread(infd, (char *)scanline, scanbytes);
	    if (n <= 0) break;
	    m = fb_write(fbp, xstart, y, scanline, xout);
	    if (m != xout) {
		fprintf(stderr,
			"pix-fb: fb_write(x=%d, y=%d, npix=%d) ret=%d, s/b=%d\n",
			scr_xoff, y, xout,
			m, xout);
	    }
	    /* slop at the end of the line? */
	    if ((size_t)file_xoff+xskip+scanpix < file_width)
		skipbytes(infd, (b_off_t)(file_width-file_xoff-xskip-scanpix)*sizeof(RGBpixel));
	}
    }
    bu_snooze(BU_SEC2USEC(pause_sec));
    if (fb_close(fbp) < 0) {
	fprintf(stderr, "pix-fb: Warning: fb_close() error\n");
    }

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
