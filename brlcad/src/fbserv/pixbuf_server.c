/*                P I X B U F _ S E R V E R . C
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
/** @file fbserv/pixbuf_server.c
 *
 * Obol-path framebuffer server — same PKG wire protocol as server.c,
 * but all storage is a malloc()'d in-memory RGB pixel buffer.  No
 * libdm symbols are used; this file is compiled instead of server.c
 * when BRLCAD_ENABLE_OBOL is active.
 *
 * Pixel layout: packed RGB888, bottom-left origin (same convention as
 * libfb).  Stride = width * 3 bytes.
 *
 * The following MSG_FB* operations are fully implemented:
 *   FBOPEN, FBCLOSE, FBFREE, FBCLEAR, FBREAD, FBWRITE,
 *   FBREADRECT, FBWRITERECT, FBBWREADRECT, FBBWWRITERECT, FBFLUSH
 *
 * Operations that have no meaning in a headless pixel buffer
 * (cursor shape, colormap, viewport zoom/pan, poll) return success
 * stubs so that existing client code does not break.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#if defined(HAVE_SYS_TYPES_H)
#  include <sys/types.h>
#endif
#if defined(HAVE_SYS_TIME_H)
#  include <sys/time.h>
#endif
#include "bsocket.h"

#include "bu/log.h"
#include "bu/malloc.h"
#include "dm/fbserv.h"
#include "pkg.h"


/* ------------------------------------------------------------------ */
/* Globals required by fbserv.c (mirroring the server.c interface)    */
/* ------------------------------------------------------------------ */

/*
 * fbs_fbp is always NULL in the pixbuf path — no libdm struct fb is
 * allocated.  fbserv.c already guards all fb_poll / fb_set_fd calls
 * with "if (fb_server_fbp)" or with BRLCAD_ENABLE_OBOL ifdefs, so
 * leaving this NULL is safe.
 */
void               *fb_server_fbp        = NULL; /* type-erased; never dereferenced */
fd_set             *fb_server_select_list = NULL;
int                *fb_server_max_fd     = NULL;
int                 fb_server_got_fb_free    = 0;
int                 fb_server_refuse_fb_free = 0;
int                 fb_server_retain_on_close = 0;


/* ------------------------------------------------------------------ */
/* Internal pixel buffer state                                         */
/* ------------------------------------------------------------------ */

static unsigned char *g_pixbuf    = NULL;
static int            g_pixbuf_w  = 0;
static int            g_pixbuf_h  = 0;

/* Width/height from the fbserv command line (-w/-n/-s), used as the
 * default when MSG_FBOPEN does not request specific dimensions.       */
static int g_default_w = 512;
static int g_default_h = 512;

/*
 * pixbuf_server_set_defaults -- called by fbserv.c when the user passes
 * -w/-n/-s options on the command line.  Sets the fallback dimensions
 * used when MSG_FBOPEN receives zero for width or height.
 */
void
pixbuf_server_set_defaults(int w, int h)
{
    if (w > 0) g_default_w = w;
    if (h > 0) g_default_h = h;
}


/* ------------------------------------------------------------------ */
/* Helper macros                                                       */
/* ------------------------------------------------------------------ */

#define NET_LONG_LEN 4  /* bytes per network long */

/* Safe pixel address — returns NULL if (x,y) is out of bounds */
static unsigned char *
pixbuf_addr(int x, int y)
{
    if (!g_pixbuf || x < 0 || y < 0 || x >= g_pixbuf_w || y >= g_pixbuf_h)
        return NULL;
    return g_pixbuf + (y * g_pixbuf_w + x) * 3;
}

/* Allocate (or reallocate) the pixel buffer. */
static int
pixbuf_alloc(int w, int h)
{
    if (w <= 0) w = g_default_w;
    if (h <= 0) h = g_default_h;

    free(g_pixbuf);
    g_pixbuf = (unsigned char *)bu_calloc((size_t)w * (size_t)h * 3, 1,
                                           "pixbuf_server pixbuf");
    if (!g_pixbuf) {
        bu_log("pixbuf_server: malloc failed for %dx%d buffer\n", w, h);
        g_pixbuf_w = 0;
        g_pixbuf_h = 0;
        return -1;
    }
    g_pixbuf_w = w;
    g_pixbuf_h = h;
    return 0;
}


/* ------------------------------------------------------------------ */
/* PKG handler implementations                                         */
/* ------------------------------------------------------------------ */

static void
pixbuf_fb_open(struct pkg_conn *pcp, char *buf)
{
    int width, height;
    char rbuf[5*NET_LONG_LEN+1];

    if (!buf || !pcp) return;

    width  = pkg_glong(&buf[0*NET_LONG_LEN]);
    height = pkg_glong(&buf[1*NET_LONG_LEN]);

    if (!g_pixbuf || g_pixbuf_w != width || g_pixbuf_h != height) {
        /* Allocate a fresh buffer on every open request */
        if (pixbuf_alloc(width, height) < 0) {
            /* Report failure back to client */
            (void)pkg_plong(&rbuf[0*NET_LONG_LEN], -1);
            (void)pkg_plong(&rbuf[1*NET_LONG_LEN], 0);
            (void)pkg_plong(&rbuf[2*NET_LONG_LEN], 0);
            (void)pkg_plong(&rbuf[3*NET_LONG_LEN], 0);
            (void)pkg_plong(&rbuf[4*NET_LONG_LEN], 0);
            (void)pkg_send(MSG_RETURN, rbuf, 5*NET_LONG_LEN, pcp);
            free(buf);
            return;
        }
    }

    /* Return success with actual dimensions */
    (void)pkg_plong(&rbuf[0*NET_LONG_LEN], 0);          /* ret = 0 */
    (void)pkg_plong(&rbuf[1*NET_LONG_LEN], g_pixbuf_w); /* max_width  */
    (void)pkg_plong(&rbuf[2*NET_LONG_LEN], g_pixbuf_h); /* max_height */
    (void)pkg_plong(&rbuf[3*NET_LONG_LEN], g_pixbuf_w); /* width  */
    (void)pkg_plong(&rbuf[4*NET_LONG_LEN], g_pixbuf_h); /* height */

    (void)pkg_send(MSG_RETURN, rbuf, 5*NET_LONG_LEN, pcp);
    free(buf);
}


static void
pixbuf_fb_close(struct pkg_conn *pcp, char *buf)
{
    char rbuf[NET_LONG_LEN+1];

    if (fb_server_retain_on_close) {
        /* Keep the buffer open; just report success */
        (void)pkg_plong(&rbuf[0], 0);
    } else {
        free(g_pixbuf);
        g_pixbuf   = NULL;
        g_pixbuf_w = 0;
        g_pixbuf_h = 0;
        (void)pkg_plong(&rbuf[0], 0);
    }
    (void)pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    if (buf) free(buf);
}


static void
pixbuf_fb_free(struct pkg_conn *pcp, char *buf)
{
    char rbuf[NET_LONG_LEN+1];

    if (fb_server_refuse_fb_free) {
        (void)pkg_plong(&rbuf[0], -1);
    } else {
        free(g_pixbuf);
        g_pixbuf   = NULL;
        g_pixbuf_w = 0;
        g_pixbuf_h = 0;
        (void)pkg_plong(&rbuf[0], 0);
    }
    {
        int sret = pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
        if (sret != NET_LONG_LEN)
            bu_log("pixbuf_server: pkg_send fb_free reply failed (sent %d/%d)\n",
                   sret, NET_LONG_LEN);
    }
    if (buf) free(buf);

    if (!fb_server_refuse_fb_free)
        fb_server_got_fb_free = 1;
}


static void
pixbuf_fb_clear(struct pkg_conn *pcp, char *buf)
{
    char rbuf[NET_LONG_LEN+1];
    unsigned char r, g, b;

    if (!buf || !pcp) return;

    r = (unsigned char)buf[0];
    g = (unsigned char)buf[1];
    b = (unsigned char)buf[2];

    if (g_pixbuf) {
        size_t npix = (size_t)g_pixbuf_w * (size_t)g_pixbuf_h;
        size_t i;
        unsigned char *p = g_pixbuf;
        for (i = 0; i < npix; i++) {
            p[0] = r; p[1] = g; p[2] = b;
            p += 3;
        }
    }

    (void)pkg_plong(rbuf, 0);
    pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    if (buf) free(buf);
}


static void
pixbuf_fb_read(struct pkg_conn *pcp, char *buf)
{
    int x, y;
    size_t num;

    if (!buf || !pcp) return;

    x   = pkg_glong(&buf[0*NET_LONG_LEN]);
    y   = pkg_glong(&buf[1*NET_LONG_LEN]);
    num = (size_t)pkg_glong(&buf[2*NET_LONG_LEN]);

    /* Pixels at (x,y), linearized (may wrap across rows) */
    if (g_pixbuf && (size_t)(y * g_pixbuf_w + x) + num <= (size_t)g_pixbuf_w * g_pixbuf_h) {
        unsigned char *src = pixbuf_addr(x, y);
        if (src)
            pkg_send(MSG_RETURN, (char *)src, (int)(num * 3), pcp);
        else
            pkg_send(MSG_RETURN, NULL, 0, pcp);
    } else {
        pkg_send(MSG_RETURN, NULL, 0, pcp);
    }
    if (buf) free(buf);
}


static void
pixbuf_fb_write(struct pkg_conn *pcp, char *buf)
{
    int x, y, num;
    char rbuf[NET_LONG_LEN+1];
    int type;

    if (!buf || !pcp) return;

    x    = pkg_glong(&buf[0*NET_LONG_LEN]);
    y    = pkg_glong(&buf[1*NET_LONG_LEN]);
    num  = pkg_glong(&buf[2*NET_LONG_LEN]);
    type = pcp->pkc_type;

    if (g_pixbuf) {
        size_t offset = (size_t)(y * g_pixbuf_w + x) * 3;
        size_t avail  = (size_t)g_pixbuf_w * (size_t)g_pixbuf_h * 3;
        size_t nbytes = (size_t)num * 3;
        if (offset + nbytes <= avail)
            memcpy(g_pixbuf + offset, &buf[3*NET_LONG_LEN], nbytes);
    }

    if (type < MSG_NORETURN) {
        (void)pkg_plong(&rbuf[0*NET_LONG_LEN], num);
        pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    }
    if (buf) free(buf);
}


static void
pixbuf_fb_readrect(struct pkg_conn *pcp, char *buf)
{
    int xmin, ymin, width, height;
    size_t num, buflen;
    unsigned char *scanbuf = NULL;
    int row;

    if (!buf || !pcp) return;

    xmin   = pkg_glong(&buf[0*NET_LONG_LEN]);
    ymin   = pkg_glong(&buf[1*NET_LONG_LEN]);
    width  = pkg_glong(&buf[2*NET_LONG_LEN]);
    height = pkg_glong(&buf[3*NET_LONG_LEN]);
    num    = (size_t)width * (size_t)height;

    if (!g_pixbuf || num == 0) {
        pkg_send(MSG_RETURN, NULL, 0, pcp);
        free(buf);
        return;
    }

    buflen = num * 3;
    scanbuf = (unsigned char *)bu_malloc(buflen, "pixbuf readrect");
    if (!scanbuf) {
        pkg_send(MSG_RETURN, NULL, 0, pcp);
        free(buf);
        return;
    }

    for (row = 0; row < height; row++) {
        unsigned char *src = pixbuf_addr(xmin, ymin + row);
        unsigned char *dst = scanbuf + (size_t)row * (size_t)width * 3;
        if (src) {
            /* Check that the whole row fits */
            if (xmin + width <= g_pixbuf_w)
                memcpy(dst, src, (size_t)width * 3);
            else
                memset(dst, 0, (size_t)width * 3);
        } else {
            memset(dst, 0, (size_t)width * 3);
        }
    }

    pkg_send(MSG_RETURN, (char *)scanbuf, (int)buflen, pcp);
    bu_free(scanbuf, "pixbuf readrect");
    if (buf) free(buf);
}


static void
pixbuf_fb_writerect(struct pkg_conn *pcp, char *buf)
{
    int x, y, width, height;
    char rbuf[NET_LONG_LEN+1];
    int type, row;

    if (!buf || !pcp) return;

    x      = pkg_glong(&buf[0*NET_LONG_LEN]);
    y      = pkg_glong(&buf[1*NET_LONG_LEN]);
    width  = pkg_glong(&buf[2*NET_LONG_LEN]);
    height = pkg_glong(&buf[3*NET_LONG_LEN]);
    type   = pcp->pkc_type;

    if (g_pixbuf) {
        for (row = 0; row < height; row++) {
            unsigned char *dst = pixbuf_addr(x, y + row);
            const unsigned char *src =
                (const unsigned char *)&buf[4*NET_LONG_LEN] + (size_t)row * (size_t)width * 3;
            if (dst && x + width <= g_pixbuf_w)
                memcpy(dst, src, (size_t)width * 3);
        }
    }

    if (type < MSG_NORETURN) {
        (void)pkg_plong(&rbuf[0*NET_LONG_LEN], width * height);
        pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    }
    if (buf) free(buf);
}


static void
pixbuf_fb_bwreadrect(struct pkg_conn *pcp, char *buf)
{
    int xmin, ymin, width, height;
    size_t num;
    unsigned char *scanbuf = NULL;
    int row, col;

    if (!buf || !pcp) return;

    xmin   = pkg_glong(&buf[0*NET_LONG_LEN]);
    ymin   = pkg_glong(&buf[1*NET_LONG_LEN]);
    width  = pkg_glong(&buf[2*NET_LONG_LEN]);
    height = pkg_glong(&buf[3*NET_LONG_LEN]);
    num    = (size_t)width * (size_t)height;

    if (!g_pixbuf || num == 0) {
        pkg_send(MSG_RETURN, NULL, 0, pcp);
        free(buf);
        return;
    }

    scanbuf = (unsigned char *)bu_malloc(num, "pixbuf bwreadrect");
    if (!scanbuf) {
        pkg_send(MSG_RETURN, NULL, 0, pcp);
        free(buf);
        return;
    }

    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            unsigned char *src = pixbuf_addr(xmin + col, ymin + row);
            unsigned char *dst = scanbuf + (size_t)row * (size_t)width + (size_t)col;
            if (src) {
                /* Simple luminance: integer (R+G+B)/3 */
                *dst = (unsigned char)(((unsigned)src[0] + (unsigned)src[1] + (unsigned)src[2]) / 3);
            } else {
                *dst = 0;
            }
        }
    }

    pkg_send(MSG_RETURN, (char *)scanbuf, (int)num, pcp);
    bu_free(scanbuf, "pixbuf bwreadrect");
    if (buf) free(buf);
}


static void
pixbuf_fb_bwwriterect(struct pkg_conn *pcp, char *buf)
{
    int x, y, width, height;
    char rbuf[NET_LONG_LEN+1];
    int type, row, col;
    const unsigned char *src;

    if (!buf || !pcp) return;

    x      = pkg_glong(&buf[0*NET_LONG_LEN]);
    y      = pkg_glong(&buf[1*NET_LONG_LEN]);
    width  = pkg_glong(&buf[2*NET_LONG_LEN]);
    height = pkg_glong(&buf[3*NET_LONG_LEN]);
    type   = pcp->pkc_type;
    src    = (const unsigned char *)&buf[4*NET_LONG_LEN];

    if (g_pixbuf) {
        for (row = 0; row < height; row++) {
            for (col = 0; col < width; col++) {
                unsigned char *dst = pixbuf_addr(x + col, y + row);
                unsigned char  val = src[(size_t)row * (size_t)width + (size_t)col];
                if (dst) {
                    dst[0] = val;
                    dst[1] = val;
                    dst[2] = val;
                }
            }
        }
    }

    if (type < MSG_NORETURN) {
        (void)pkg_plong(&rbuf[0*NET_LONG_LEN], width * height);
        pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    }
    if (buf) free(buf);
}


/* ------------------------------------------------------------------ */
/* Stub handlers for operations that have no meaning in a pixel buffer */
/* ------------------------------------------------------------------ */

static void
pixbuf_return_success(struct pkg_conn *pcp, char *buf)
{
    char rbuf[NET_LONG_LEN+1];
    if (pcp)  {
        (void)pkg_plong(rbuf, 0);
        pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    }
    if (buf) free(buf);
}

static void
pixbuf_ignore(struct pkg_conn *pcp, char *buf)
{
    (void)pcp;
    if (buf) free(buf);
}

static void
pixbuf_fb_flush(struct pkg_conn *pcp, char *buf)
{
    /* Flush is a no-op for an in-memory buffer, but acknowledge if needed */
    if (pcp && pcp->pkc_type < MSG_NORETURN) {
        char rbuf[NET_LONG_LEN+1];
        (void)pkg_plong(rbuf, 0);
        pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    }
    if (buf) free(buf);
}

static void
pixbuf_fb_unknown(struct pkg_conn *pcp, char *buf)
{
    if (pcp)
        bu_log("pixbuf_server: unknown message type %d\n", pcp->pkc_type);
    if (buf) free(buf);
}

/* Colormap read: return an identity ramp */
static void
pixbuf_fb_rmap(struct pkg_conn *pcp, char *buf)
{
    char rbuf[NET_LONG_LEN+1];
    unsigned char cm[256*2*3];
    int i;

    if (!pcp) { if (buf) free(buf); return; }

    (void)pkg_plong(&rbuf[0*NET_LONG_LEN], 0); /* ret */
    for (i = 0; i < 256; i++) {
        /* Identity ramp */
        (void)pkg_pshort((char *)(cm + 2*(0   + i)), (short)(i << 8));
        (void)pkg_pshort((char *)(cm + 2*(256 + i)), (short)(i << 8));
        (void)pkg_pshort((char *)(cm + 2*(512 + i)), (short)(i << 8));
    }
    pkg_send(MSG_DATA,   (char *)cm,   sizeof(cm),     pcp);
    pkg_send(MSG_RETURN, rbuf,         NET_LONG_LEN,   pcp);
    if (buf) free(buf);
}

/* Viewport-get: return 1x zoom centered at (w/2, h/2) */
static void
pixbuf_fb_getview(struct pkg_conn *pcp, char *buf)
{
    char rbuf[5*NET_LONG_LEN+1];
    if (!pcp) { if (buf) free(buf); return; }
    (void)pkg_plong(&rbuf[0*NET_LONG_LEN], 0);              /* ret */
    (void)pkg_plong(&rbuf[1*NET_LONG_LEN], g_pixbuf_w / 2); /* xcenter */
    (void)pkg_plong(&rbuf[2*NET_LONG_LEN], g_pixbuf_h / 2); /* ycenter */
    (void)pkg_plong(&rbuf[3*NET_LONG_LEN], 1);              /* xzoom */
    (void)pkg_plong(&rbuf[4*NET_LONG_LEN], 1);              /* yzoom */
    pkg_send(MSG_RETURN, rbuf, 5*NET_LONG_LEN, pcp);
    if (buf) free(buf);
}

/* Cursor-get: return mode=0, pos=(0,0) */
static void
pixbuf_fb_getcursor(struct pkg_conn *pcp, char *buf)
{
    char rbuf[4*NET_LONG_LEN+1];
    if (!pcp) { if (buf) free(buf); return; }
    (void)pkg_plong(&rbuf[0*NET_LONG_LEN], 0); /* ret */
    (void)pkg_plong(&rbuf[1*NET_LONG_LEN], 0); /* mode */
    (void)pkg_plong(&rbuf[2*NET_LONG_LEN], 0); /* x */
    (void)pkg_plong(&rbuf[3*NET_LONG_LEN], 0); /* y */
    pkg_send(MSG_RETURN, rbuf, 4*NET_LONG_LEN, pcp);
    if (buf) free(buf);
}


/* ------------------------------------------------------------------ */
/* pkg_switch table                                                     */
/* ------------------------------------------------------------------ */

const struct pkg_switch pkg_switch[] = {
    { MSG_FBOPEN,                       pixbuf_fb_open,       "Open Framebuffer",    NULL },
    { MSG_FBCLOSE,                      pixbuf_fb_close,      "Close Framebuffer",   NULL },
    { MSG_FBCLEAR,                      pixbuf_fb_clear,      "Clear Framebuffer",   NULL },
    { MSG_FBREAD,                       pixbuf_fb_read,       "Read Pixels",         NULL },
    { MSG_FBWRITE,                      pixbuf_fb_write,      "Write Pixels",        NULL },
    { MSG_FBWRITE + MSG_NORETURN,       pixbuf_fb_write,      "Asynch write",        NULL },
    { MSG_FBCURSOR,                     pixbuf_return_success,"Cursor",              NULL },
    { MSG_FBGETCURSOR,                  pixbuf_fb_getcursor,  "Get Cursor",          NULL },
    { MSG_FBSCURSOR,                    pixbuf_return_success,"Screen Cursor (old)", NULL },
    { MSG_FBWINDOW,                     pixbuf_return_success,"Window (old)",        NULL },
    { MSG_FBZOOM,                       pixbuf_return_success,"Zoom (old)",          NULL },
    { MSG_FBVIEW,                       pixbuf_return_success,"View",                NULL },
    { MSG_FBGETVIEW,                    pixbuf_fb_getview,    "Get View",            NULL },
    { MSG_FBRMAP,                       pixbuf_fb_rmap,       "R Map",               NULL },
    { MSG_FBWMAP,                       pixbuf_return_success,"W Map",               NULL },
    { MSG_FBHELP,                       pixbuf_return_success,"Help Request",        NULL },
    { MSG_ERROR,                        pixbuf_fb_unknown,    "Error Message",       NULL },
    { MSG_CLOSE,                        pixbuf_fb_unknown,    "Close Connection",    NULL },
    { MSG_FBREADRECT,                   pixbuf_fb_readrect,   "Read Rectangle",      NULL },
    { MSG_FBWRITERECT,                  pixbuf_fb_writerect,  "Write Rectangle",     NULL },
    { MSG_FBWRITERECT + MSG_NORETURN,   pixbuf_fb_writerect,  "Write Rectangle",     NULL },
    { MSG_FBBWREADRECT,                 pixbuf_fb_bwreadrect, "Read BW Rectangle",   NULL },
    { MSG_FBBWWRITERECT,                pixbuf_fb_bwwriterect,"Write BW Rectangle",  NULL },
    { MSG_FBBWWRITERECT + MSG_NORETURN, pixbuf_fb_bwwriterect,"Write BW Rectangle",  NULL },
    { MSG_FBFLUSH,                      pixbuf_fb_flush,      "Flush Output",        NULL },
    { MSG_FBFLUSH + MSG_NORETURN,       pixbuf_fb_flush,      "Flush Output",        NULL },
    { MSG_FBFREE,                       pixbuf_fb_free,       "Free Resources",      NULL },
    { MSG_FBPOLL,                       pixbuf_ignore,        "Handle Events",       NULL },
    { MSG_FBSETCURSOR,                  pixbuf_return_success,"Set Cursor Shape",    NULL },
    { MSG_FBSETCURSOR + MSG_NORETURN,   pixbuf_return_success,"Set Cursor Shape",    NULL },
    { 0,                                NULL,                  NULL,                  NULL }
};


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
