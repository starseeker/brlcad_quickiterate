/*                   R T _ F B _ P K G . H
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @file rt/rt_fb_pkg.h
 *
 * PKG-direct framebuffer abstraction for rt family tools in Obol builds.
 *
 * In non-Obol builds, the rt tools use the struct fb / libdm API.
 * In Obol builds, libdm's struct-fb stack is not compiled, so we
 * speak the fbserv PKG wire protocol directly using libpkg.  This
 * header provides a thin struct and static-inline helpers that map
 * the operations rt needs (open, write, writerect, read, readrect,
 * view, close) onto PKG messages.
 *
 * The wire protocol follows the same encoding as pix-fb.c (Stage 26)
 * and fb-pix.c (Stage 26).  All integers are sent big-endian using
 * the same 4-byte encoding as pkg_glong/pkg_plong.
 *
 * Only included when BRLCAD_ENABLE_OBOL is defined.
 */

#ifndef RT_FB_PKG_H
#define RT_FB_PKG_H

#ifdef BRLCAD_ENABLE_OBOL

#include "common.h"
#include <stdlib.h>
#include <string.h>
#include "pkg.h"
#include "dm/fbserv.h"  /* MSG_FB* constants */
#include "bu/log.h"
#include "bu/malloc.h"

/**
 * rt_fb_pkg — wraps a PKG connection to a running fbserv and caches the
 * negotiated framebuffer dimensions.
 */
struct rt_fb_pkg {
    struct pkg_conn *pc;
    int width;
    int height;
};

#define RT_FB_PKG_NULL ((struct rt_fb_pkg *)NULL)

/* ------------------------------------------------------------------ */
/* Network integer encoding helpers (big-endian, 4 bytes)             */
/* ------------------------------------------------------------------ */
#define RT_FB_PKG_NLL 4  /* bytes per network long */

static inline void
rt_fb_pkg_plong(char *buf, unsigned long val)
{
    unsigned char *p = (unsigned char *)buf;
    p[0] = (unsigned char)((val >> 24) & 0xff);
    p[1] = (unsigned char)((val >> 16) & 0xff);
    p[2] = (unsigned char)((val >>  8) & 0xff);
    p[3] = (unsigned char)((val      ) & 0xff);
}

static inline unsigned long
rt_fb_pkg_glong(const char *buf)
{
    const unsigned char *p = (const unsigned char *)buf;
    unsigned long u = p[0]; u <<= 8;
    u |= p[1]; u <<= 8;
    u |= p[2]; u <<= 8;
    return u | p[3];
}

/* ------------------------------------------------------------------ */
/* Open a connection to fbserv and negotiate width/height             */
/* framebuffer: "host:port" or "port" string.                         */
/* Returns NULL on failure; caller owns the returned struct.          */
/* ------------------------------------------------------------------ */
static inline struct rt_fb_pkg *
rt_fb_pkg_open(const char *framebuffer, int width, int height)
{
    struct pkg_conn *pc;
    char hostbuf[256];
    char portbuf[64];
    const char *colon;
    char openbuf[2*RT_FB_PKG_NLL + 2];
    char retbuf[5*RT_FB_PKG_NLL + 4];
    int srv_w, srv_h;
    struct rt_fb_pkg *rfp;

    if (!framebuffer || framebuffer[0] == '\0')
	return RT_FB_PKG_NULL;

    /* Parse "host:port" or just "port" */
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

    pc = pkg_open(hostbuf, portbuf, 0, 0, 0, NULL, NULL);
    if (pc == PKC_ERROR) {
	bu_log("rt_fb_pkg: cannot connect to fbserv at %s:%s\n",
	       hostbuf, portbuf);
	return RT_FB_PKG_NULL;
    }

    /* MSG_FBOPEN: [width(4B)][height(4B)] */
    memset(openbuf, 0, sizeof(openbuf));
    rt_fb_pkg_plong(&openbuf[0*RT_FB_PKG_NLL], (unsigned long)width);
    rt_fb_pkg_plong(&openbuf[1*RT_FB_PKG_NLL], (unsigned long)height);
    if (pkg_send(MSG_FBOPEN, openbuf, 2*RT_FB_PKG_NLL, pc) < 2*RT_FB_PKG_NLL) {
	pkg_close(pc);
	bu_log("rt_fb_pkg: MSG_FBOPEN send failed\n");
	return RT_FB_PKG_NULL;
    }

    /* Response: [ret(4B)][max_w(4B)][max_h(4B)][actual_w(4B)][actual_h(4B)] */
    memset(retbuf, 0, sizeof(retbuf));
    if (pkg_waitfor(MSG_RETURN, retbuf, sizeof(retbuf), pc) < 5*RT_FB_PKG_NLL) {
	pkg_close(pc);
	bu_log("rt_fb_pkg: MSG_FBOPEN reply too short\n");
	return RT_FB_PKG_NULL;
    }
    if (rt_fb_pkg_glong(&retbuf[0*RT_FB_PKG_NLL]) != 0) {
	pkg_close(pc);
	bu_log("rt_fb_pkg: fbserv refused open\n");
	return RT_FB_PKG_NULL;
    }
    srv_w = (int)rt_fb_pkg_glong(&retbuf[3*RT_FB_PKG_NLL]);
    srv_h = (int)rt_fb_pkg_glong(&retbuf[4*RT_FB_PKG_NLL]);
    if (width  > srv_w) width  = srv_w;
    if (height > srv_h) height = srv_h;

    rfp = (struct rt_fb_pkg *)bu_malloc(sizeof(struct rt_fb_pkg), "rt_fb_pkg");
    rfp->pc     = pc;
    rfp->width  = (width  > 0) ? width  : srv_w;
    rfp->height = (height > 0) ? height : srv_h;
    return rfp;
}

/* ------------------------------------------------------------------ */
/* Close the fbserv connection and free the struct.                   */
/* ------------------------------------------------------------------ */
static inline void
rt_fb_pkg_close(struct rt_fb_pkg *rfp)
{
    if (!rfp) return;
    {
	char closeret[RT_FB_PKG_NLL + 1];
	char empty[1] = {0};
	(void)pkg_send(MSG_FBCLOSE, empty, 0, rfp->pc);
	(void)pkg_waitfor(MSG_RETURN, closeret, sizeof(closeret), rfp->pc);
    }
    pkg_close(rfp->pc);
    bu_free(rfp, "rt_fb_pkg");
}

/* ------------------------------------------------------------------ */
/* Write npix pixels (RGB triples) starting at (x, y).               */
/* Uses MSG_FBWRITERECT with height=1 for a single scanline segment.  */
/* Returns npix on success, -1 on error.                              */
/* ------------------------------------------------------------------ */
static inline ssize_t
rt_fb_pkg_write(struct rt_fb_pkg *rfp, int x, int y,
		const unsigned char *pixelp, size_t npix)
{
    size_t databytes = npix * 3;
    size_t msglen    = 4*RT_FB_PKG_NLL + databytes;
    char   *buf;
    char   wret[RT_FB_PKG_NLL + 1];

    if (!rfp || !pixelp || npix == 0) return -1;

    buf = (char *)bu_malloc(msglen, "rt_fb_pkg_write buf");
    rt_fb_pkg_plong(&buf[0*RT_FB_PKG_NLL], (unsigned long)x);
    rt_fb_pkg_plong(&buf[1*RT_FB_PKG_NLL], (unsigned long)y);
    rt_fb_pkg_plong(&buf[2*RT_FB_PKG_NLL], (unsigned long)npix);
    rt_fb_pkg_plong(&buf[3*RT_FB_PKG_NLL], 1UL); /* height = 1 */
    memcpy(&buf[4*RT_FB_PKG_NLL], pixelp, databytes);

    if (pkg_send(MSG_FBWRITERECT, buf, (int)msglen, rfp->pc) < (int)msglen) {
	bu_free(buf, "rt_fb_pkg_write buf");
	return -1;
    }
    (void)pkg_waitfor(MSG_RETURN, wret, sizeof(wret), rfp->pc);
    bu_free(buf, "rt_fb_pkg_write buf");
    return (ssize_t)npix;
}

/* ------------------------------------------------------------------ */
/* Write a rectangle of pixels: xcount × ycount starting at (x, y). */
/* pixelp must contain xcount*ycount*3 bytes (row-major order).      */
/* Returns 0 on success, -1 on error.                                 */
/* ------------------------------------------------------------------ */
static inline int
rt_fb_pkg_writerect(struct rt_fb_pkg *rfp, int x, int y,
		    int xcount, int ycount,
		    const unsigned char *pixelp)
{
    size_t databytes = (size_t)xcount * (size_t)ycount * 3;
    size_t msglen    = 4*RT_FB_PKG_NLL + databytes;
    char   *buf;
    char   wret[RT_FB_PKG_NLL + 1];

    if (!rfp || !pixelp || xcount <= 0 || ycount <= 0) return -1;

    buf = (char *)bu_malloc(msglen, "rt_fb_pkg_writerect buf");
    rt_fb_pkg_plong(&buf[0*RT_FB_PKG_NLL], (unsigned long)x);
    rt_fb_pkg_plong(&buf[1*RT_FB_PKG_NLL], (unsigned long)y);
    rt_fb_pkg_plong(&buf[2*RT_FB_PKG_NLL], (unsigned long)xcount);
    rt_fb_pkg_plong(&buf[3*RT_FB_PKG_NLL], (unsigned long)ycount);
    memcpy(&buf[4*RT_FB_PKG_NLL], pixelp, databytes);

    if (pkg_send(MSG_FBWRITERECT, buf, (int)msglen, rfp->pc) < (int)msglen) {
	bu_free(buf, "rt_fb_pkg_writerect buf");
	return -1;
    }
    (void)pkg_waitfor(MSG_RETURN, wret, sizeof(wret), rfp->pc);
    bu_free(buf, "rt_fb_pkg_writerect buf");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Read npix pixels (RGB triples) from framebuffer at (x, y).        */
/* Uses MSG_FBREADRECT.  Fills pixelp with npix*3 bytes.              */
/* Returns npix on success, -1 on error.                              */
/* ------------------------------------------------------------------ */
static inline ssize_t
rt_fb_pkg_read(struct rt_fb_pkg *rfp, int x, int y,
	       unsigned char *pixelp, size_t npix)
{
    size_t databytes = npix * 3;
    char   reqbuf[4*RT_FB_PKG_NLL];
    size_t retbuf_sz = RT_FB_PKG_NLL + databytes;
    char   *retbuf;
    ssize_t ret;

    if (!rfp || !pixelp || npix == 0) return -1;

    /* MSG_FBREADRECT request: [x(4B)][y(4B)][xcount(4B)][ycount(4B)] */
    rt_fb_pkg_plong(&reqbuf[0*RT_FB_PKG_NLL], (unsigned long)x);
    rt_fb_pkg_plong(&reqbuf[1*RT_FB_PKG_NLL], (unsigned long)y);
    rt_fb_pkg_plong(&reqbuf[2*RT_FB_PKG_NLL], (unsigned long)npix);
    rt_fb_pkg_plong(&reqbuf[3*RT_FB_PKG_NLL], 1UL); /* height = 1 */

    if (pkg_send(MSG_FBREADRECT, reqbuf, 4*RT_FB_PKG_NLL, rfp->pc) < 4*RT_FB_PKG_NLL)
	return -1;

    /* Response: return_code(4B) + pixel_data */
    retbuf = (char *)bu_malloc(retbuf_sz + 8, "rt_fb_pkg_read retbuf");
    ret = pkg_waitfor(MSG_RETURN, retbuf, (int)(retbuf_sz + 8), rfp->pc);
    if (ret < (ssize_t)(RT_FB_PKG_NLL + databytes)) {
	bu_free(retbuf, "rt_fb_pkg_read retbuf");
	return -1;
    }
    if (rt_fb_pkg_glong(&retbuf[0]) != 0) {
	bu_free(retbuf, "rt_fb_pkg_read retbuf");
	return -1;
    }
    memcpy(pixelp, &retbuf[RT_FB_PKG_NLL], databytes);
    bu_free(retbuf, "rt_fb_pkg_read retbuf");
    return (ssize_t)npix;
}

/* ------------------------------------------------------------------ */
/* Set the framebuffer view (zoom + center pan).                      */
/* Sends MSG_FBVIEW: [xcenter(4B)][ycenter(4B)][xzoom(4B)][yzoom(4B)]*/
/* Returns 0 on success, -1 on error.                                 */
/* ------------------------------------------------------------------ */
static inline int
rt_fb_pkg_view(struct rt_fb_pkg *rfp,
	       int xcenter, int ycenter, int xzoom, int yzoom)
{
    char viewbuf[4*RT_FB_PKG_NLL];
    char viewret[RT_FB_PKG_NLL + 1];

    if (!rfp) return -1;

    rt_fb_pkg_plong(&viewbuf[0*RT_FB_PKG_NLL], (unsigned long)xcenter);
    rt_fb_pkg_plong(&viewbuf[1*RT_FB_PKG_NLL], (unsigned long)ycenter);
    rt_fb_pkg_plong(&viewbuf[2*RT_FB_PKG_NLL], (unsigned long)xzoom);
    rt_fb_pkg_plong(&viewbuf[3*RT_FB_PKG_NLL], (unsigned long)yzoom);

    if (pkg_send(MSG_FBVIEW, viewbuf, 4*RT_FB_PKG_NLL, rfp->pc) < 4*RT_FB_PKG_NLL)
	return -1;
    (void)pkg_waitfor(MSG_RETURN, viewret, sizeof(viewret), rfp->pc);
    return 0;
}

#endif /* BRLCAD_ENABLE_OBOL */
#endif /* RT_FB_PKG_H */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
