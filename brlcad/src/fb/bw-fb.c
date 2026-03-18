/*                         B W - F B . C
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
 */
/** @file bw-fb.c
 *
 * Write a black and white (.bw) image to the framebuffer.
 * From an 8-bit/pixel, pix order file (i.e. Bottom UP, left to right).
 *
 * This allows an offset into both the display and source file.
 * The color planes to be loaded are also selectable.
 *
 * In Obol builds this tool speaks the fbserv PKG wire protocol
 * (MSG_FBOPEN / MSG_FBBWWRITERECT / MSG_FBCLOSE) directly via libpkg,
 * with no libdm dependency.  Selective color-plane loading (-R/-G/-B)
 * is not supported in the Obol path.
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

#include "bio.h"

#include "bu/app.h"
#include "bu/color.h"
#include "bu/getopt.h"
#include "bu/malloc.h"
#include "bu/file.h"
#include "bu/exit.h"
#include "bu/log.h"
#include "vmath.h"

#ifndef BRLCAD_ENABLE_OBOL
#  include "dm.h"
#endif

#include "pkg.h"
#include "dm/fbserv.h"   /* MSG_FB* constants */


static int skipbytes(int fd, b_off_t num);

#define MAX_LINE (16*1024)	/* Largest output scan line length */

static char ibuf[MAX_LINE];
#ifndef BRLCAD_ENABLE_OBOL
static RGBpixel obuf[MAX_LINE];
#endif

static int fileinput = 0;		/* file of pipe on input? */
static int autosize = 0;		/* !0 to autosize input */

static size_t file_width = 512;	/* default input width */
static size_t file_height = 512;	/* default input height */
static int scr_width = 0;		/* screen tracks file if not given */
static int scr_height = 0;
static int file_xoff, file_yoff;
static int scr_xoff, scr_yoff;
static int clear = 0;
static int zoom = 0;
static int inverse = 0;
static int redflag   = 0;
static int greenflag = 0;
static int blueflag  = 0;

static char *framebuffer = NULL;
static char *file_name;
static int infd;
#ifndef BRLCAD_ENABLE_OBOL
static struct fb *fbp;
#endif

static char usage[] = "\
Usage: bw-fb [-a -i -c -z -R -G -B] [-F framebuffer]\n\
	[-s squarefilesize] [-w file_width] [-n file_height]\n\
	[-x file_xoff] [-y file_yoff] [-X scr_xoff] [-Y scr_yoff]\n\
	[-S squarescrsize] [-W scr_width] [-N scr_height] [file.bw]\n";
int
get_args(int argc, char **argv)
{
    int c;

    while ((c = bu_getopt(argc, argv, "aiczRGBF:s:w:n:x:y:X:Y:S:W:N:h?")) != -1) {
	switch (c) {
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
	    case 'R':
		redflag = 1;
		break;
	    case 'G':
		greenflag = 1;
		break;
	    case 'B':
		blueflag = 1;
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
	    fprintf(stderr,
		    "bw-fb: cannot open \"%s (canonical %s)\" for reading\n",
		    file_name, ifname);
	    bu_free(ifname, "ifname alloc from bu_file_realpath");
	    return 0;
	}
	bu_free(ifname, "ifname alloc from bu_file_realpath");
	fileinput++;
    }

    if (argc > ++bu_optind)
	fprintf(stderr, "bw-fb: excess argument(s) ignored\n");

    return 1;		/* OK */
}


#ifdef BRLCAD_ENABLE_OBOL

/* ------------------------------------------------------------------ */
/* Obol path: direct PKG wire protocol (no libdm)                      */
/* ------------------------------------------------------------------ */

/* NET_LONG_LEN is 4 bytes, matching pkg_glong / pkg_plong encoding   */
#define BWFB_NET_LONG_LEN 4

/* Encode a 32-bit big-endian unsigned long into buf (same as htonl). */
static void
bwfb_plong(char *buf, unsigned long val)
{
    unsigned char *p = (unsigned char *)buf;
    p[0] = (unsigned char)((val >> 24) & 0xff);
    p[1] = (unsigned char)((val >> 16) & 0xff);
    p[2] = (unsigned char)((val >>  8) & 0xff);
    p[3] = (unsigned char)((val      ) & 0xff);
}

/* Decode a 32-bit big-endian unsigned long from buf (same as ntohl). */
static unsigned long
bwfb_glong(const char *buf)
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
    long xout, yout;
    long xstart, xskip;
    char openbuf[2*BWFB_NET_LONG_LEN + 2];
    char retbuf[5*BWFB_NET_LONG_LEN + 4];
    char hostbuf[256];
    char portbuf[64];
    const char *colon;
    unsigned char *wrectbuf;
    int bw_per_row;

    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    if (!framebuffer) {
	(void)fputs(usage, stderr);
	bu_exit(12, "bw-fb: -F framebuffer is required in Obol builds\n");
    }

    /* Selective color-plane loading is not available in the Obol path */
    if (redflag || greenflag || blueflag) {
	if (!(redflag && greenflag && blueflag)) {
	    bu_exit(1, "bw-fb: selective color-plane loading (-R/-G/-B) is not supported in Obol builds\n");
	}
    }

    /* autosize input? */
    if (fileinput && autosize) {
	struct stat st;
	if (fstat(infd, &st) == 0 && st.st_size > 0) {
	    size_t npix = (size_t)st.st_size;
	    double sqrtval = sqrt((double)npix);
	    size_t w = (size_t)sqrtval;
	    if (w > 0 && w * w == npix) {
		file_width = file_height = w;
	    } else {
		fprintf(stderr, "bw-fb: unable to autosize\n");
	    }
	} else {
	    fprintf(stderr, "bw-fb: unable to autosize\n");
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
	bu_exit(12, "bw-fb: cannot connect to fbserv at %s:%s\n",
		hostbuf, portbuf);
    }

    /* MSG_FBOPEN: [width(4B)][height(4B)] */
    memset(openbuf, 0, sizeof(openbuf));
    bwfb_plong(&openbuf[0*BWFB_NET_LONG_LEN], (unsigned long)scr_width);
    bwfb_plong(&openbuf[1*BWFB_NET_LONG_LEN], (unsigned long)scr_height);
    if (pkg_send(MSG_FBOPEN, openbuf, 2*BWFB_NET_LONG_LEN, pc) < 2*BWFB_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "bw-fb: MSG_FBOPEN send failed\n");
    }

    /* Response: [ret(4B)][max_w(4B)][max_h(4B)][w(4B)][h(4B)] */
    if (pkg_waitfor(MSG_RETURN, retbuf, sizeof(retbuf), pc) < 5*BWFB_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "bw-fb: MSG_FBOPEN reply too short\n");
    }
    if (bwfb_glong(&retbuf[0*BWFB_NET_LONG_LEN]) != 0) {
	pkg_close(pc);
	bu_exit(1, "bw-fb: fbserv refused open\n");
    }
    {
	int srv_w = (int)bwfb_glong(&retbuf[3*BWFB_NET_LONG_LEN]);
	int srv_h = (int)bwfb_glong(&retbuf[4*BWFB_NET_LONG_LEN]);
	if (scr_width  > srv_w) scr_width  = srv_w;
	if (scr_height > srv_h) scr_height = srv_h;
    }

    /* Clear if requested */
    if (clear) {
	char clearbuf[4] = {0, 0, 0, 0};
	char clearret[BWFB_NET_LONG_LEN + 1];
	(void)pkg_send(MSG_FBCLEAR, clearbuf, 3, pc);
	(void)pkg_waitfor(MSG_RETURN, clearret, sizeof(clearret), pc);
    }

    /* Zoom not supported in Obol path */
    if (zoom) {
	fprintf(stderr, "bw-fb: -z (zoom) not supported in Obol builds, ignored\n");
    }

    /* Compute output extents */
    if (scr_xoff < 0) {
	xout   = scr_width + scr_xoff;
	xskip  = (-scr_xoff);
	xstart = 0;
    } else {
	xout   = scr_width - scr_xoff;
	xskip  = 0;
	xstart = scr_xoff;
    }
    CLAMP(xout, 0, (long)(file_width - (size_t)file_xoff));

    if (inverse)
	scr_yoff = (-scr_yoff);

    yout = scr_height - scr_yoff;
    CLAMP(yout, 0, (long)(file_height - (size_t)file_yoff));

    if (xout > MAX_LINE) {
	fprintf(stderr, "bw-fb: can't output %ld pixel lines.\n", xout);
	pkg_close(pc);
	return 2;
    }

    /* Each MSG_FBBWWRITERECT packet: 4 longs header + xout bytes of BW data */
    bw_per_row = (int)xout;
    wrectbuf = (unsigned char *)bu_malloc(4*BWFB_NET_LONG_LEN + (size_t)bw_per_row, "wrectbuf");

    if (file_yoff != 0) skipbytes(infd, (b_off_t)file_yoff * (b_off_t)file_width);

    for (y = inverse ? (scr_height - 1 - scr_yoff) : scr_yoff;
	 inverse ? (y >= scr_height - scr_yoff - yout) : (y < scr_yoff + yout);
	 y += inverse ? -1 : 1) {
	int n;

	if (y < 0 || y >= scr_height) {
	    skipbytes(infd, (b_off_t)file_width);
	    continue;
	}
	if (file_xoff + xskip != 0)
	    skipbytes(infd, (b_off_t)(file_xoff + xskip));
	n = bu_mread(infd, ibuf, bw_per_row);
	if (n <= 0) break;

	bwfb_plong((char *)&wrectbuf[0*BWFB_NET_LONG_LEN], (unsigned long)xstart);
	bwfb_plong((char *)&wrectbuf[1*BWFB_NET_LONG_LEN], (unsigned long)y);
	bwfb_plong((char *)&wrectbuf[2*BWFB_NET_LONG_LEN], (unsigned long)bw_per_row);
	bwfb_plong((char *)&wrectbuf[3*BWFB_NET_LONG_LEN], 1UL);
	memcpy(&wrectbuf[4*BWFB_NET_LONG_LEN], ibuf, (size_t)n);
	{
	    int sendlen = 4*BWFB_NET_LONG_LEN + n;
	    char wret[BWFB_NET_LONG_LEN + 1];
	    if (pkg_send(MSG_FBBWWRITERECT, (char *)wrectbuf, sendlen, pc) < sendlen)
		break;
	    (void)pkg_waitfor(MSG_RETURN, wret, sizeof(wret), pc);
	}

	/* slop at end of line? */
	if ((size_t)file_xoff + xskip + xout < file_width)
	    skipbytes(infd, (b_off_t)(file_width - (size_t)file_xoff - xskip - xout));
    }

    /* MSG_FBCLOSE */
    {
	char closeret[BWFB_NET_LONG_LEN + 1];
	(void)pkg_send(MSG_FBCLOSE, NULL, 0, pc);
	(void)pkg_waitfor(MSG_RETURN, closeret, sizeof(closeret), pc);
    }

    pkg_close(pc);
    bu_free(wrectbuf, "wrectbuf");

    return 0;
}

#else /* !BRLCAD_ENABLE_OBOL ---------------------------------------- */

int
main(int argc, char **argv)
{
    int x=0, y=0, n=0;
    long xout=1, yout=1;		/* number of screen output lines */
    long xstart=0, xskip=0;

    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    /* autosize input? */
    if (fileinput && autosize) {
	size_t w, h;
	if (fb_common_file_size(&w, &h, file_name, 1)) {
	    file_width = w;
	    file_height = h;
	} else {
	    fprintf(stderr, "bw-fb: unable to autosize\n");
	}
    }

    /* If no color planes were selected, load them all */
    if (redflag == 0 && greenflag == 0 && blueflag == 0)
	redflag = greenflag = blueflag = 1;

    /* If screen size was not set, track the file size */
    if (scr_width == 0)
	scr_width = file_width;
    if (scr_height == 0)
	scr_height = file_height;

    /* Open Display Device */
    if ((fbp = fb_open(framebuffer, scr_width, scr_height)) == NULL) {
	fprintf(stderr, "fb_open failed\n");
	bu_exit(3, NULL);
    }

    /* Get the screen size we were given */
    scr_width = fb_getwidth(fbp);
    scr_height = fb_getheight(fbp);

    /* compute pixels output to screen */
    if (scr_xoff < 0) {
	xout = scr_width + scr_xoff;
	xskip = (-scr_xoff);
	xstart = 0;
    } else {
	xout = scr_width - scr_xoff;
	xskip = 0;
	xstart = scr_xoff;
    }
    CLAMP(xout, 0, (long)(file_width-file_xoff));

    if (inverse)
	scr_yoff = (-scr_yoff);

    yout = scr_height - scr_yoff;
    CLAMP(yout, 0, (long)(file_height-file_yoff));

    if (xout > MAX_LINE) {
	fprintf(stderr, "bw-fb: can't output %ld pixel lines.\n", xout);
	return 2;
    }

    if (clear) {
	fb_clear(fbp, PIXEL_NULL);
    }
    if (zoom && xout && yout) {
	/* Zoom in, and center the file */
	fb_zoom(fbp, scr_width/xout, scr_height/yout);
	if (inverse)
	    fb_window(fbp, scr_xoff+xout/2, scr_height-1-(scr_yoff+yout/2));
	else
	    fb_window(fbp, scr_xoff+xout/2, scr_yoff+yout/2);
    }

    /* Test for simplest case */
    if (inverse == 0 && file_xoff == 0 && file_yoff == 0 && scr_xoff+file_width <= (unsigned)fb_getwidth(fbp)) {
	unsigned char *buf;
	int npix = file_width * yout;

	if ((buf = (unsigned char *)malloc(npix)) == NULL) {
	    perror("bw-fb malloc");
	    goto general;
	}
	n = bu_mread(infd, (char *)buf, npix);
	if (n != npix) {
	    fprintf(stderr, "bw-fb: read got %d, s/b %d\n", n, npix);
	    if (n <= 0)
		return 7;
	    npix = n;	/* show what we got */
	}
	n = (npix+file_width-1)/file_width;	/* num lines got */
	n = fb_bwwriterect(fbp, scr_xoff, scr_yoff, file_width, n, buf);
	if (npix != n) {
	    fprintf(stderr, "bw-fb: fb_bwwriterect() got %d, s/b %d\n", n, npix);
	    bu_exit(8, NULL);
	}
	fb_close(fbp);
	return 0;
    }

    /* Begin general case */
general:
    if (file_yoff != 0) skipbytes(infd, file_yoff*file_width);

    for (y = scr_yoff; y < scr_yoff + yout; y++) {
	if (y < 0 || y >= scr_height) {
	    skipbytes(infd, file_width);
	    continue;
	}
	if (file_xoff+xskip != 0)
	    skipbytes(infd, file_xoff+xskip);
	n = bu_mread(infd, &ibuf[0], xout);
	if (n <= 0) break;
	/*
	 * If we are not loading all color planes, we have
	 * to do a pre-read.
	 */
	if (redflag == 0 || greenflag == 0 || blueflag == 0) {
	    if (inverse)
		n = fb_read(fbp, scr_xoff, scr_height-1-y,
			    (unsigned char *)obuf, xout);
	    else
		n = fb_read(fbp, scr_xoff, y,
			    (unsigned char *)obuf, xout);
	    if (n < 0) break;
	}
	for (x = 0; x < xout; x++) {
	    if (redflag)
		obuf[x][RED] = ibuf[x];
	    if (greenflag)
		obuf[x][GRN] = ibuf[x];
	    if (blueflag)
		obuf[x][BLU] = ibuf[x];
	}
	if (inverse)
	    fb_write(fbp, xstart, scr_height-1-y, (unsigned char *)obuf, xout);
	else
	    fb_write(fbp, xstart, y, (unsigned char *)obuf, xout);

	/* slop at the end of the line? */
	if ((unsigned)(file_xoff+xskip+xout) < file_width)
	    skipbytes(infd, file_width-file_xoff-xskip-xout);
    }

    fb_close(fbp);
    return 0;
}

#endif /* BRLCAD_ENABLE_OBOL */


/*
 * Throw bytes away.  Use reads into ibuf buffer if a pipe, else seek.
 */
int
skipbytes(int fd, b_off_t num)
{
    int n, tries;

    if (fileinput) {
	(void)bu_lseek(fd, num, 1);
	return 0;
    }

    while (num > 0) {
	tries = num > MAX_LINE ? MAX_LINE : num;
	n = read(fd, ibuf, tries);
	if (n <= 0) {
	    return -1;
	}
	num -= n;
    }
    return 0;
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
