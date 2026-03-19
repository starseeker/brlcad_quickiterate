/*                         F B - B W . C
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
/** @file fb-bw.c
 *
 * Read a Black and White image from the framebuffer and output
 * it in 8-bit black and white form in pix order,
 * i.e. Bottom UP, left to right.
 *
 * In Obol builds this tool speaks the fbserv PKG wire protocol
 * (MSG_FBOPEN / MSG_FBBWREADRECT / MSG_FBCLOSE) directly via libpkg,
 * with no libdm dependency.
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bio.h"

#include "bu/app.h"
#include "bu/color.h"
#include "bu/getopt.h"
#include "bu/exit.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "vmath.h"

#ifndef BRLCAD_ENABLE_OBOL
#  include "dm.h"
#endif

#include "pkg.h"
#include "dm/fbserv.h"   /* MSG_FB* constants */


#ifndef BRLCAD_ENABLE_OBOL
#define LINELEN 8192

static unsigned char inbuf[LINELEN*3];
static unsigned char obuf[LINELEN];
#endif

int height;
int width;
int inverse;
int scr_xoff, scr_yoff;

char *framebuffer = NULL;
char *file_name;
FILE *outfp;


int
get_args(int argc, char **argv)
{
    int c;

    while ((c = bu_getopt(argc, argv, "iF:X:Y:s:w:n:h?")) != -1) {
	switch (c) {
	    case 'i':
		inverse = 1;
		break;
	    case 'F':
		framebuffer = bu_optarg;
		break;
	    case 'X':
		scr_xoff = atoi(bu_optarg);
		break;
	    case 'Y':
		scr_yoff = atoi(bu_optarg);
		break;
	    case 's':
		/* square size */
		height = width = atoi(bu_optarg);
		break;
	    case 'w':
		width = atoi(bu_optarg);
		break;
	    case 'n':
		height = atoi(bu_optarg);
		break;

	    default:		/* '?' 'h' */
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
		    "fb-bw: cannot open \"%s\" for writing\n",
		    file_name);
	    return 0;
	}
    }

    if (argc > ++bu_optind)
	fprintf(stderr, "fb-bw: excess argument(s) ignored\n");

    return 1;		/* OK */
}


#ifdef BRLCAD_ENABLE_OBOL

/* ------------------------------------------------------------------ */
/* Obol path: direct PKG wire protocol (no libdm)                      */
/* ------------------------------------------------------------------ */

/* NET_LONG_LEN is 4 bytes, matching pkg_glong / pkg_plong encoding   */
#define FBBW_NET_LONG_LEN 4

/* Encode a 32-bit big-endian unsigned long into buf (same as htonl). */
static void
fbbw_plong(char *buf, unsigned long val)
{
    unsigned char *p = (unsigned char *)buf;
    p[0] = (unsigned char)((val >> 24) & 0xff);
    p[1] = (unsigned char)((val >> 16) & 0xff);
    p[2] = (unsigned char)((val >>  8) & 0xff);
    p[3] = (unsigned char)((val      ) & 0xff);
}

/* Decode a 32-bit big-endian unsigned long from buf (same as ntohl). */
static unsigned long
fbbw_glong(const char *buf)
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
    unsigned char *bwbuf;
    int xin, yin;
    int y;
    char openbuf[2*FBBW_NET_LONG_LEN + 2];
    char retbuf[5*FBBW_NET_LONG_LEN + 4];
    char hostbuf[256];
    char portbuf[64];
    const char *colon;
    int srv_w, srv_h;

    char usage[] = "Usage: fb-bw [-i] [-F framebuffer]\n\
	[-X scr_xoff] [-Y scr_yoff]\n\
	[-s squaresize] [-w width] [-n height] [file.bw]\n";

    height = width = 512;

    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    if (!framebuffer) {
	(void)fputs(usage, stderr);
	bu_exit(12, "fb-bw: -F framebuffer is required in Obol builds\n");
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
	bu_exit(12, "fb-bw: cannot connect to fbserv at %s:%s\n",
		hostbuf, portbuf);
    }

    /* MSG_FBOPEN: [width(4B)][height(4B)] (device string empty) */
    memset(openbuf, 0, sizeof(openbuf));
    fbbw_plong(&openbuf[0*FBBW_NET_LONG_LEN], (unsigned long)width);
    fbbw_plong(&openbuf[1*FBBW_NET_LONG_LEN], (unsigned long)height);
    if (pkg_send(MSG_FBOPEN, openbuf, 2*FBBW_NET_LONG_LEN, pc) < 2*FBBW_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fb-bw: MSG_FBOPEN send failed\n");
    }

    /* Response: [ret(4B)][max_w(4B)][max_h(4B)][w(4B)][h(4B)] */
    if (pkg_waitfor(MSG_RETURN, retbuf, sizeof(retbuf), pc) < 5*FBBW_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fb-bw: MSG_FBOPEN reply too short\n");
    }
    if (fbbw_glong(&retbuf[0*FBBW_NET_LONG_LEN]) != 0) {
	pkg_close(pc);
	bu_exit(1, "fb-bw: fbserv refused open\n");
    }
    srv_w = (int)fbbw_glong(&retbuf[3*FBBW_NET_LONG_LEN]);
    srv_h = (int)fbbw_glong(&retbuf[4*FBBW_NET_LONG_LEN]);

    /* Determine "reasonable" extents -- same logic as non-Obol path */
    xin = srv_w - scr_xoff;
    CLAMP(xin, 0, width);
    yin = srv_h - scr_yoff;
    CLAMP(yin, 0, height);

    bwbuf = (unsigned char *)bu_malloc((size_t)xin + 1, "bwbuf");

    /* Read rows via MSG_FBBWREADRECT (server converts RGB → BW) */
    {
	char rrectbuf[4*FBBW_NET_LONG_LEN + 1];
	for (y = scr_yoff; y < scr_yoff + yin; y++) {
	    int read_y = inverse ? (srv_h - 1 - y) : y;
	    int got;
	    size_t ret;

	    fbbw_plong(&rrectbuf[0*FBBW_NET_LONG_LEN], (unsigned long)scr_xoff);
	    fbbw_plong(&rrectbuf[1*FBBW_NET_LONG_LEN], (unsigned long)read_y);
	    fbbw_plong(&rrectbuf[2*FBBW_NET_LONG_LEN], (unsigned long)xin);
	    fbbw_plong(&rrectbuf[3*FBBW_NET_LONG_LEN], 1UL); /* one row */
	    if (pkg_send(MSG_FBBWREADRECT, rrectbuf, 4*FBBW_NET_LONG_LEN, pc) < 4*FBBW_NET_LONG_LEN)
		break;
	    got = (int)pkg_waitfor(MSG_RETURN, (char *)bwbuf, xin, pc);
	    if (got <= 0) {
		bu_log("fb-bw: read of row %d failed (got %d)\n", y, got);
		break;
	    }
	    ret = fwrite(bwbuf, 1, (size_t)got, outfp);
	    if (ret != (size_t)got)
		perror("fwrite");
	}
    }

    /* MSG_FBCLOSE */
    {
	char closeret[FBBW_NET_LONG_LEN + 1];
	(void)pkg_send(MSG_FBCLOSE, NULL, 0, pc);
	(void)pkg_waitfor(MSG_RETURN, closeret, sizeof(closeret), pc);
    }

    pkg_close(pc);
    bu_free(bwbuf, "bwbuf");

    if (outfp != stdout)
	fclose(outfp);

    return 0;
}

#else /* !BRLCAD_ENABLE_OBOL ---------------------------------------- */

int
main(int argc, char **argv)
{
    struct fb *fbp;

    int x, y;
    int xin, yin;		/* number of screen output lines */

    char usage[] = "Usage: fb-bw [-i] [-F framebuffer]\n\
	[-X scr_xoff] [-Y scr_yoff]\n\
	[-s squaresize] [-w width] [-n height] [file.bw]\n";

    height = width = 512;		/* Defaults */

    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    /* Open Display Device */
    if ((fbp = fb_open(framebuffer, width, height)) == NULL) {
	fprintf(stderr, "fb_open failed\n");
	bu_exit(1, NULL);
    }

    /* determine "reasonable" behavior */
    xin = fb_getwidth(fbp) - scr_xoff;
    CLAMP(xin, 0, width);
    yin = fb_getheight(fbp) - scr_yoff;
    CLAMP(yin, 0, height);

    for (y = scr_yoff; y < scr_yoff + yin; y++) {
	size_t ret;
	if (inverse) {
	    (void)fb_read(fbp, scr_xoff, fb_getheight(fbp)-1-y, inbuf, xin);
	} else {
	    (void)fb_read(fbp, scr_xoff, y, inbuf, xin);
	}
	for (x = 0; x < xin; x++) {
	    obuf[x] = (((int)inbuf[3*x+RED]) + ((int)inbuf[3*x+GRN])
		       + ((int)inbuf[3*x+BLU])) / 3;
	}
	ret = fwrite(&obuf[0], sizeof(char), xin, outfp);
	if (ret != (size_t)xin)
	    perror("fwrite");
    }

    fb_close(fbp);
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
