/*                        F B F R E E . C
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
/** @file fbfree.c
 *
 * Free any resources associated with a frame buffer.
 * Just calls fb_free().
 *
 * In Obol builds this tool speaks the fbserv PKG wire protocol
 * (MSG_FBOPEN / MSG_FBFREE) directly via libpkg, with no libdm
 * dependency.
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/app.h"
#include "bu/exit.h"
#include "bu/getopt.h"
#include "bu/log.h"

#ifndef BRLCAD_ENABLE_OBOL
#  include "dm.h"
#endif

#include "pkg.h"
#include "dm/fbserv.h"   /* MSG_FB* constants */


static char *framebuffer = NULL;

static char usage[] = "\
Usage: fbfree [-F framebuffer]\n";

#ifdef BRLCAD_ENABLE_OBOL

/* ------------------------------------------------------------------ */
/* Obol path: direct PKG wire protocol (no libdm)                      */
/* ------------------------------------------------------------------ */

#define FBFREE_NET_LONG_LEN 4

static void
fbfree_plong(char *buf, unsigned long val)
{
    unsigned char *p = (unsigned char *)buf;
    p[0] = (unsigned char)((val >> 24) & 0xff);
    p[1] = (unsigned char)((val >> 16) & 0xff);
    p[2] = (unsigned char)((val >>  8) & 0xff);
    p[3] = (unsigned char)((val      ) & 0xff);
}

static unsigned long
fbfree_glong(const char *buf)
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
    int c;
    struct pkg_conn *pc;
    char hostbuf[256];
    char portbuf[64];
    char openbuf[2*FBFREE_NET_LONG_LEN + 2];
    char retbuf[5*FBFREE_NET_LONG_LEN + 4];
    char freeret[FBFREE_NET_LONG_LEN + 1];
    const char *colon;

    bu_setprogname(argv[0]);

    while ((c = bu_getopt(argc, argv, "F:h?")) != -1) {
	switch (c) {
	    case 'F':
		framebuffer = bu_optarg;
		break;
	    default:
		(void)fputs(usage, stderr);
		return 1;
	}
    }
    if (argc > ++bu_optind)
	fprintf(stderr, "fbfree: excess argument(s) ignored\n");

    if (!framebuffer) {
	(void)fputs(usage, stderr);
	bu_exit(1, "fbfree: -F framebuffer is required in Obol builds\n");
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
	bu_exit(1, "fbfree: cannot connect to fbserv at %s:%s\n", hostbuf, portbuf);

    /* MSG_FBOPEN */
    memset(openbuf, 0, sizeof(openbuf));
    fbfree_plong(&openbuf[0*FBFREE_NET_LONG_LEN], 0); /* use server default size */
    fbfree_plong(&openbuf[1*FBFREE_NET_LONG_LEN], 0);
    if (pkg_send(MSG_FBOPEN, openbuf, 2*FBFREE_NET_LONG_LEN, pc) < 2*FBFREE_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fbfree: MSG_FBOPEN send failed\n");
    }
    if (pkg_waitfor(MSG_RETURN, retbuf, sizeof(retbuf), pc) < 5*FBFREE_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fbfree: MSG_FBOPEN reply too short\n");
    }
    if (fbfree_glong(&retbuf[0*FBFREE_NET_LONG_LEN]) != 0) {
	pkg_close(pc);
	bu_exit(1, "fbfree: fbserv refused open\n");
    }

    /* MSG_FBFREE */
    if (pkg_send(MSG_FBFREE, NULL, 0, pc) < 0) {
	pkg_close(pc);
	bu_exit(1, "fbfree: MSG_FBFREE send failed\n");
    }
    if (pkg_waitfor(MSG_RETURN, freeret, sizeof(freeret), pc) < FBFREE_NET_LONG_LEN) {
	pkg_close(pc);
	bu_exit(1, "fbfree: MSG_FBFREE reply too short\n");
    }

    pkg_close(pc);
    return (int)fbfree_glong(&freeret[0]);
}

#else /* !BRLCAD_ENABLE_OBOL ---------------------------------------- */

int
main(int argc, char **argv)
{
    int c;
    struct fb *fbp;

    bu_setprogname(argv[0]);

    while ((c = bu_getopt(argc, argv, "F:h?")) != -1) {
	switch (c) {
	    case 'F':
		framebuffer = bu_optarg;
		break;
	    default:		/* '?' */
		(void)fputs(usage, stderr);
		return 1;
	}
    }
    if (argc > ++bu_optind) {
	fprintf(stderr, "fbfree: excess argument(s) ignored\n");
    }

    if ((fbp = fb_open(framebuffer, 0, 0)) == FB_NULL) {
	fprintf(stderr, "fbfree: Can't open frame buffer\n");
	return 1;
    }
    return fb_free(fbp);
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
