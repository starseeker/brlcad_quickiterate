/*                         F B - F B . C
 * BRL-CAD
 *
 * Copyright (c) 1991-2025 United States Government as represented by
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
/** @file fb-fb.c
 *
 * Program to copy the entire image on a framebuffer to another
 * framebuffer.
 *
 * In Obol builds this tool speaks the fbserv PKG wire protocol
 * (MSG_FBOPEN / MSG_FBREADRECT / MSG_FBWRITERECT / MSG_FBCLOSE)
 * directly via libpkg, with no libdm dependency.  Both the source and
 * destination framebuffers are specified as "host:port" strings.
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/app.h"
#include "bu/getopt.h"
#include "bu/log.h"
#include "bu/malloc.h"

#ifndef BRLCAD_ENABLE_OBOL
#  include "dm.h"
#endif

#include "pkg.h"
#include "dm/fbserv.h"   /* MSG_FB* constants */


static int verbose;

static char *in_fb_name;
static char *out_fb_name;

static int scr_width = 0;		/* screen tracks file if not given */
static int scr_height = 0;

static char usage[] = "\
Usage: fb-fb [-v] [-F output_framebuffer]\n\
       or\n\
       fb-fb [-v] input_framebuffer [output_framebuffer]\n";

int
get_args(int argc, char **argv)
{
    int c;

    while ((c = bu_getopt(argc, argv, "vF:h?")) != -1) {
	switch (c) {
	    case 'F':
		out_fb_name = bu_optarg;
		break;
	    case 'v':
		verbose++;
		break;

	    default:		/* 'h' '?' */
		return 0;
	}
    }

    if (bu_optind >= argc) {
	return 0;		/* missing input framebuffer */
    }
    in_fb_name = argv[bu_optind++];

    if (bu_optind >= argc) {
	return 1;	/* OK */
    }
    out_fb_name = argv[bu_optind++];

    if (argc > bu_optind)
	fprintf(stderr, "fb-fb: excess argument(s) ignored\n");

    return 1;		/* OK */
}


#ifdef BRLCAD_ENABLE_OBOL

/* ------------------------------------------------------------------ */
/* Obol path: direct PKG wire protocol (no libdm)                      */
/* ------------------------------------------------------------------ */

/* NET_LONG_LEN is 4 bytes, matching pkg_glong / pkg_plong encoding   */
#define FBFB_NET_LONG_LEN 4

/* Encode a 32-bit big-endian unsigned long into buf (same as htonl). */
static void
fbfb_plong(char *buf, unsigned long val)
{
    unsigned char *p = (unsigned char *)buf;
    p[0] = (unsigned char)((val >> 24) & 0xff);
    p[1] = (unsigned char)((val >> 16) & 0xff);
    p[2] = (unsigned char)((val >>  8) & 0xff);
    p[3] = (unsigned char)((val      ) & 0xff);
}

/* Decode a 32-bit big-endian unsigned long from buf (same as ntohl). */
static unsigned long
fbfb_glong(const char *buf)
{
    const unsigned char *p = (const unsigned char *)buf;
    unsigned long u = p[0]; u <<= 8;
    u |= p[1]; u <<= 8;
    u |= p[2]; u <<= 8;
    return u | p[3];
}

/* Open a PKG connection to a "host:port" or "port" framebuffer string.
 * On success writes srv_w/srv_h from MSG_FBOPEN reply and returns pc.
 * On failure calls bu_exit. */
static struct pkg_conn *
fbfb_open(const char *fbname, int req_w, int req_h, int *srv_w, int *srv_h)
{
    struct pkg_conn *pc;
    char hostbuf[256];
    char portbuf[64];
    char openbuf[2*FBFB_NET_LONG_LEN + 2];
    char retbuf[5*FBFB_NET_LONG_LEN + 4];
    const char *colon;

    colon = strrchr(fbname, ':');
    if (colon && colon != fbname) {
	size_t hlen = (size_t)(colon - fbname);
	if (hlen >= sizeof(hostbuf)) hlen = sizeof(hostbuf) - 1;
	memcpy(hostbuf, fbname, hlen);
	hostbuf[hlen] = '\0';
	snprintf(portbuf, sizeof(portbuf), "%s", colon + 1);
    } else {
	snprintf(hostbuf, sizeof(hostbuf), "localhost");
	snprintf(portbuf, sizeof(portbuf), "%s", colon ? colon + 1 : fbname);
    }

    pc = pkg_open(hostbuf, portbuf, 0, 0, 0, NULL, NULL);
    if (pc == PKC_ERROR)
	bu_exit(12, "fb-fb: cannot connect to fbserv at %s:%s\n", hostbuf, portbuf);

    memset(openbuf, 0, sizeof(openbuf));
    fbfb_plong(&openbuf[0*FBFB_NET_LONG_LEN], (unsigned long)req_w);
    fbfb_plong(&openbuf[1*FBFB_NET_LONG_LEN], (unsigned long)req_h);
    if (pkg_send(MSG_FBOPEN, openbuf, 2*FBFB_NET_LONG_LEN, pc) < 2*FBFB_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fb-fb: MSG_FBOPEN send failed for %s\n", fbname);
    }

    if (pkg_waitfor(MSG_RETURN, retbuf, sizeof(retbuf), pc) < 5*FBFB_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fb-fb: MSG_FBOPEN reply too short for %s\n", fbname);
    }
    if (fbfb_glong(&retbuf[0*FBFB_NET_LONG_LEN]) != 0) {
	pkg_close(pc);
	bu_exit(1, "fb-fb: fbserv at %s refused open\n", fbname);
    }
    *srv_w = (int)fbfb_glong(&retbuf[3*FBFB_NET_LONG_LEN]);
    *srv_h = (int)fbfb_glong(&retbuf[4*FBFB_NET_LONG_LEN]);
    return pc;
}

int
main(int argc, char **argv)
{
    struct pkg_conn *in_pc, *out_pc;
    int in_w, in_h, out_w, out_h;
    int y, streamline;
    unsigned char *scanline;
    size_t scanbytes;

    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    if (!in_fb_name) {
	(void)fputs(usage, stderr);
	bu_exit(1, "fb-fb: input framebuffer is required in Obol builds\n");
    }
    if (!out_fb_name) {
	(void)fputs(usage, stderr);
	bu_exit(1, "fb-fb: output framebuffer is required in Obol builds (-F or positional)\n");
    }

    if (verbose)
	fprintf(stderr, "fb-fb: infb=%s, outfb=%s\n", in_fb_name, out_fb_name);

    /* Open source (request 0x0 → server picks its current size) */
    in_pc = fbfb_open(in_fb_name, 0, 0, &in_w, &in_h);
    scr_width  = in_w;
    scr_height = in_h;

    if (verbose)
	fprintf(stderr, "fb-fb: width=%d height=%d\n", scr_width, scr_height);

    /* Open destination with source dimensions */
    out_pc = fbfb_open(out_fb_name, scr_width, scr_height, &out_w, &out_h);

    streamline = 64;
    scanbytes  = (size_t)scr_width * (size_t)streamline * 3;
    scanline   = (unsigned char *)bu_malloc(scanbytes, "scanline");

    /* Copy bottom-to-top in blocks of streamline rows */
    for (y = 0; y < scr_height; y += streamline) {
	char rrectbuf[4*FBFB_NET_LONG_LEN + 1];
	unsigned char *wrectbuf;
	int nrows, got, m;
	char wret[FBFB_NET_LONG_LEN + 1];

	nrows = streamline;
	if (y + nrows > scr_height)
	    nrows = scr_height - y;

	/* MSG_FBREADRECT from source */
	fbfb_plong(&rrectbuf[0*FBFB_NET_LONG_LEN], 0);
	fbfb_plong(&rrectbuf[1*FBFB_NET_LONG_LEN], (unsigned long)y);
	fbfb_plong(&rrectbuf[2*FBFB_NET_LONG_LEN], (unsigned long)scr_width);
	fbfb_plong(&rrectbuf[3*FBFB_NET_LONG_LEN], (unsigned long)nrows);
	if (pkg_send(MSG_FBREADRECT, rrectbuf, 4*FBFB_NET_LONG_LEN, in_pc) < 4*FBFB_NET_LONG_LEN)
	    break;

	got = (int)pkg_waitfor(MSG_RETURN, (char *)scanline,
			       scr_width * nrows * 3, in_pc);
	if (got <= 0) {
	    bu_log("fb-fb: read rect y=%d nrows=%d failed\n", y, nrows);
	    break;
	}

	/* Adjust nrows if partial read */
	if (got < scr_width * nrows * 3) {
	    nrows = got / (scr_width * 3);
	    if (nrows <= 0) break;
	}

	/* MSG_FBWRITERECT to destination */
	wrectbuf = (unsigned char *)bu_malloc(4*FBFB_NET_LONG_LEN + (size_t)scr_width * nrows * 3,
					      "wrectbuf");
	fbfb_plong((char *)&wrectbuf[0*FBFB_NET_LONG_LEN], 0);
	fbfb_plong((char *)&wrectbuf[1*FBFB_NET_LONG_LEN], (unsigned long)y);
	fbfb_plong((char *)&wrectbuf[2*FBFB_NET_LONG_LEN], (unsigned long)scr_width);
	fbfb_plong((char *)&wrectbuf[3*FBFB_NET_LONG_LEN], (unsigned long)nrows);
	memcpy(&wrectbuf[4*FBFB_NET_LONG_LEN], scanline, (size_t)scr_width * nrows * 3);
	m = 4*FBFB_NET_LONG_LEN + scr_width * nrows * 3;
	if (pkg_send(MSG_FBWRITERECT, (char *)wrectbuf, m, out_pc) < m)
	    bu_log("fb-fb: write rect y=%d failed\n", y);
	(void)pkg_waitfor(MSG_RETURN, wret, sizeof(wret), out_pc);
	bu_free(wrectbuf, "wrectbuf");
    }

    /* Close both connections */
    {
	char cret[FBFB_NET_LONG_LEN + 1];
	(void)pkg_send(MSG_FBCLOSE, NULL, 0, in_pc);
	(void)pkg_waitfor(MSG_RETURN, cret, sizeof(cret), in_pc);
	pkg_close(in_pc);

	(void)pkg_send(MSG_FBCLOSE, NULL, 0, out_pc);
	(void)pkg_waitfor(MSG_RETURN, cret, sizeof(cret), out_pc);
	pkg_close(out_pc);
    }

    bu_free(scanline, "scanline");
    return 0;
}

#else /* !BRLCAD_ENABLE_OBOL ---------------------------------------- */

int
main(int argc, char **argv)
{
    int y;
    struct fb *in_fbp, *out_fbp;
    int n, m;
    int height;

    unsigned char *scanline = NULL;    /* 1 scanline pixel buffer */
    int scanbytes;              /* # of bytes of scanline */
    int scanpix;                /* # of pixels of scanline */
    int streamline;             /* # scanlines to do at once */

    bu_setprogname(argv[0]);

    if (!get_args(argc, argv)) {
	(void)fputs(usage, stderr);
	bu_exit(1, NULL);
    }

    if (verbose)
	fprintf(stderr, "fb-fb: infb=%s, outfb=%s\n", in_fb_name, out_fb_name);

    if ((in_fbp = fb_open(in_fb_name, 0, 0)) == NULL) {
	if (in_fb_name)
	    fprintf(stderr, "fb-fb: unable to open input '%s'\n", in_fb_name);
	bu_exit(12, NULL);
    }

    /* Get the screen size we were given */
    scr_width = fb_getwidth(in_fbp);
    scr_height = fb_getheight(in_fbp);

    if (verbose)
	fprintf(stderr, "fb-fb: width=%d height=%d\n", scr_width, scr_height);

    if ((out_fbp = fb_open(out_fb_name, scr_width, scr_height)) == FB_NULL) {
	if (out_fb_name)
	    fprintf(stderr, "fb-fb: unable to open output '%s'\n", out_fb_name);
	bu_exit(12, NULL);
    }

    scanpix = scr_width;			/* # pixels on scanline */
    streamline = 64;			/* # scanlines per block */
    scanbytes = scanpix * streamline * sizeof(RGBpixel);
    if ((scanline = (unsigned char *)malloc(scanbytes)) == RGBPIXEL_NULL) {
	fprintf(stderr,
		"fb-fb:  malloc(%d) failure for scanline buffer\n",
		scanbytes);
	bu_exit(2, NULL);
    }

    /* Bottom to top with multi-line reads & writes */
    for (y = 0; y < scr_height; y += streamline) {
	if (y+streamline > scr_height)
	    streamline = scr_height-y;
	if (verbose)
	    fprintf(stderr, "fb-fb: y=%d, nlines=%d\n", y, streamline);
	n = fb_readrect(in_fbp, 0, y, scr_width, streamline,
			scanline);
	if (n <= 0) break;
	height = streamline;
	if (n != scr_width * streamline) {
	    height = (n+scr_width-1)/scr_width;
	    if (height <= 0) break;
	}
	m = fb_writerect(out_fbp, 0, y, scr_width, height,
			 scanline);
	if (m != scr_width*height)
	    fprintf(stderr,
		    "fb-fb: fb_writerect(x=0, y=%d, w=%d, h=%d) failure, ret=%d, s/b=%d\n",
		    y, scr_width, height, m, scanbytes);
    }
    fb_close(in_fbp);
    fb_close(out_fbp);
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
