/*                      F B S E R V . C P P
 * BRL-CAD
 *
 * Copyright (c) 2004-2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file fbserv.cpp
 *
 *  These are the Qt specific callbacks used for I/O between client
 *  and server.
 *
 *  TODO - Look into QLocalSocket, and whether we might be able to
 *  generalize libpkg (or even just use parts of it) to allow us
 *  to communicate using that mechanism...
 *
 *  Initial thought - optional callback functions to replace
 *  select, read, etc - if not set default to current behavior,
 *  if set do the callback instead of those calls...
 */

#include "common.h"

#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/vls.h"
#include "qtcad/defines.h"
#ifndef BRLCAD_ENABLE_OBOL
/* dm.h and the legacy QgGL / QgSW headers are only needed when Obol is not
 * available.  In Obol builds the rendering is handled by QgObolView /
 * QgObolSwrastView and dm_get_ctx / dm_get_udata are never called. */
#  include "dm.h"
#  ifdef BRLCAD_OPENGL
#    include "qtcad/QgGL.h"
#  endif
#  include "qtcad/QgSW.h"
#endif /* !BRLCAD_ENABLE_OBOL */
#include "./fbserv.h"
#ifdef BRLCAD_ENABLE_OBOL
#  include "QgObolView.h"
#endif

#ifdef BRLCAD_ENABLE_OBOL
/* ──────────────────────────────────────────────────────────────────────────
 * Obol-native framebuffer protocol handlers.
 *
 * In the Obol path there is no struct fb / libdm backend.  Pixels sent by rt
 * over the fbserv TCP connection are written directly into fbsp->fbs_pixbuf.
 * pkc_server_data is set to the struct fbserv_obj * (not struct fb *).
 * ────────────────────────────────────────────────────────────────────────── */

static void
obol_rfbopen(struct pkg_conn *pcp, char *buf)
{
    struct fbserv_obj *fbsp = (struct fbserv_obj *)pcp->pkc_server_data;
    char rbuf[5*NET_LONG_LEN+1] = {0};
    int want = 5*NET_LONG_LEN;
    (void)pkg_plong(&rbuf[0*NET_LONG_LEN], 0);                  /* ret = success */
    (void)pkg_plong(&rbuf[1*NET_LONG_LEN], fbsp->fbs_pixbuf_w); /* max_width  */
    (void)pkg_plong(&rbuf[2*NET_LONG_LEN], fbsp->fbs_pixbuf_h); /* max_height */
    (void)pkg_plong(&rbuf[3*NET_LONG_LEN], fbsp->fbs_pixbuf_w); /* width  */
    (void)pkg_plong(&rbuf[4*NET_LONG_LEN], fbsp->fbs_pixbuf_h); /* height */
    if (pkg_send(MSG_RETURN, rbuf, want, pcp) != want)
	bu_log("obol_rfbopen: pkg_send error\n");
    free(buf);
}

static void
obol_rfbclose(struct pkg_conn *pcp, char *buf)
{
    char rbuf[NET_LONG_LEN+1] = {0};
    (void)pkg_plong(&rbuf[0], 0);
    (void)pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    free(buf);
}

static void
obol_rfbfree(struct pkg_conn *pcp, char *buf)
{
    char rbuf[NET_LONG_LEN+1] = {0};
    if (pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp) != NET_LONG_LEN)
	bu_log("obol_rfbfree: pkg_send error\n");
    free(buf);
}

static void
obol_rfbclear(struct pkg_conn *pcp, char *buf)
{
    struct fbserv_obj *fbsp = (struct fbserv_obj *)pcp->pkc_server_data;
    char rbuf[NET_LONG_LEN+1] = {0};
    if (buf && fbsp->fbs_pixbuf) {
	unsigned char r = (unsigned char)buf[0];
	unsigned char g = (unsigned char)buf[1];
	unsigned char b = (unsigned char)buf[2];
	size_t npix = (size_t)fbsp->fbs_pixbuf_w * fbsp->fbs_pixbuf_h;
	for (size_t k = 0; k < npix; k++) {
	    fbsp->fbs_pixbuf[k*3+0] = r;
	    fbsp->fbs_pixbuf[k*3+1] = g;
	    fbsp->fbs_pixbuf[k*3+2] = b;
	}
    }
    (void)pkg_plong(rbuf, 0);
    (void)pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    free(buf);
}

/* MSG_FBWRITE: write a scanline starting at (x,y), num pixels */
static void
obol_rfbwrite(struct pkg_conn *pcp, char *buf)
{
    struct fbserv_obj *fbsp = (struct fbserv_obj *)pcp->pkc_server_data;
    char rbuf[NET_LONG_LEN+1] = {0};
    if (!buf) {
	bu_log("obol_rfbwrite: null buffer\n");
	return;
    }
    long x   = pkg_glong(&buf[0*NET_LONG_LEN]);
    long y   = pkg_glong(&buf[1*NET_LONG_LEN]);
    long num = pkg_glong(&buf[2*NET_LONG_LEN]);
    if (fbsp->fbs_pixbuf && x >= 0 && y >= 0 && num > 0
	    && y < fbsp->fbs_pixbuf_h && x + num <= fbsp->fbs_pixbuf_w) {
	size_t off     = ((size_t)y * fbsp->fbs_pixbuf_w + x) * 3;
	size_t to_copy = (size_t)num * 3;
	memcpy(&fbsp->fbs_pixbuf[off], &buf[3*NET_LONG_LEN], to_copy);
    }
    int type = pcp->pkc_type;
    if (type < MSG_NORETURN) {
	(void)pkg_plong(&rbuf[0], num);
	(void)pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    }
    free(buf);
}

/* MSG_FBWRITERECT: write a rectangle of pixels */
static void
obol_rfbwriterect(struct pkg_conn *pcp, char *buf)
{
    struct fbserv_obj *fbsp = (struct fbserv_obj *)pcp->pkc_server_data;
    char rbuf[NET_LONG_LEN+1] = {0};
    if (!buf) {
	bu_log("obol_rfbwriterect: null buffer\n");
	return;
    }
    int x = (int)pkg_glong(&buf[0*NET_LONG_LEN]);
    int y = (int)pkg_glong(&buf[1*NET_LONG_LEN]);
    int w = (int)pkg_glong(&buf[2*NET_LONG_LEN]);
    int h = (int)pkg_glong(&buf[3*NET_LONG_LEN]);
    if (fbsp->fbs_pixbuf && w > 0 && h > 0
	    && x >= 0 && y >= 0
	    && x + w <= fbsp->fbs_pixbuf_w
	    && y + h <= fbsp->fbs_pixbuf_h) {
	for (int row = 0; row < h; row++) {
	    size_t dst_off = ((size_t)(y + row) * fbsp->fbs_pixbuf_w + x) * 3;
	    size_t src_off = (size_t)4*NET_LONG_LEN + (size_t)row * w * 3;
	    memcpy(&fbsp->fbs_pixbuf[dst_off], &buf[src_off], (size_t)w * 3);
	}
    }
    int type = pcp->pkc_type;
    if (type < MSG_NORETURN) {
	(void)pkg_plong(&rbuf[0], w * h);
	(void)pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    }
    free(buf);
}

/* MSG_FBREAD, MSG_FBREADRECT, MSG_FBRMAP, MSG_FBWMAP, etc.:
 * rt does not send these in the embedded-raytrace use case, but we must
 * provide a handler entry (or NULL) for every slot.  A NULL handler causes
 * pkg_process() to log an error; send an empty-success reply instead. */
static void
obol_rfbnoop_return(struct pkg_conn *pcp, char *buf)
{
    char rbuf[NET_LONG_LEN+1] = {0};
    (void)pkg_plong(&rbuf[0], 0);
    (void)pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    free(buf);
}

static void
obol_rfbflush(struct pkg_conn *pcp, char *buf)
{
    char rbuf[NET_LONG_LEN+1] = {0};
    (void)pkg_plong(&rbuf[0], 0);
    (void)pkg_send(MSG_RETURN, rbuf, NET_LONG_LEN, pcp);
    free(buf);
}

static struct pkg_switch *
obol_fbs_pkg_switch(void)
{
    static struct pkg_switch pswitch[] = {
	{ MSG_FBOPEN,        obol_rfbopen,        "Open Framebuffer",  NULL },
	{ MSG_FBCLOSE,       obol_rfbclose,        "Close Framebuffer", NULL },
	{ MSG_FBCLEAR,       obol_rfbclear,        "Clear Framebuffer", NULL },
	{ MSG_FBREAD,        obol_rfbnoop_return,  "Read Pixels",       NULL },
	{ MSG_FBWRITE,       obol_rfbwrite,        "Write Pixels",      NULL },
	{ MSG_FBWRITE + MSG_NORETURN, obol_rfbwrite, "Asynch write",    NULL },
	{ MSG_FBREADRECT,    obol_rfbnoop_return,  "Read Rectangle",    NULL },
	{ MSG_FBWRITERECT,   obol_rfbwriterect,    "Write Rectangle",   NULL },
	{ MSG_FBWRITERECT + MSG_NORETURN, obol_rfbwriterect, "Asynch write rect", NULL },
	{ MSG_FBFLUSH,       obol_rfbflush,        "Flush",             NULL },
	{ MSG_FBFREE,        obol_rfbfree,         "Free Framebuffer",  NULL },
	{ MSG_FBRMAP,        obol_rfbnoop_return,  "R Map",             NULL },
	{ MSG_FBWMAP,        obol_rfbnoop_return,  "W Map",             NULL },
	{ MSG_FBHELP,        obol_rfbnoop_return,  "Help Request",      NULL },
	{ MSG_FBCURSOR,      obol_rfbnoop_return,  "Cursor",            NULL },
	{ MSG_FBGETCURSOR,   obol_rfbnoop_return,  "Get Cursor",        NULL },
	{ MSG_FBSCURSOR,     obol_rfbnoop_return,  "Screen Cursor",     NULL },
	{ MSG_FBWINDOW,      obol_rfbnoop_return,  "Window",            NULL },
	{ MSG_FBZOOM,        obol_rfbnoop_return,  "Zoom",              NULL },
	{ MSG_FBVIEW,        obol_rfbnoop_return,  "View",              NULL },
	{ MSG_FBGETVIEW,     obol_rfbnoop_return,  "Get View",          NULL },
	{ MSG_FBSETCURSOR,   obol_rfbnoop_return,  "Set Cursor",        NULL },
	{ MSG_FBBWREADRECT,  obol_rfbnoop_return,  "BW Read Rectangle", NULL },
	{ MSG_FBBWWRITERECT, obol_rfbnoop_return,  "BW Write Rectangle",NULL },
	{ 0, NULL, NULL, NULL }
    };
    return pswitch;
}

/* Obol-native client registration: mirrors fbs_new_client() from libdm but
 * does not call fbs_setup_socket() (Qt manages socket options) and does not
 * link against libdm. */
static int
qdm_obol_new_client(struct fbserv_obj *fbsp, struct pkg_conn *pcp, void *data)
{
    if (pcp == PKC_ERROR)
	return -1;

    for (int i = MAX_CLIENTS - 1; i >= 0; i--) {
	if (fbsp->fbs_clients[i].fbsc_fd != 0)
	    continue;
	fbsp->fbs_clients[i].fbsc_fd   = pcp->pkc_fd;
	fbsp->fbs_clients[i].fbsc_pkg  = pcp;
	fbsp->fbs_clients[i].fbsc_fbsp = fbsp;
	(*fbsp->fbs_open_client_handler)(fbsp, i, data);
	return i;
    }
    bu_log("qdm_obol_new_client: too many clients\n");
    pkg_close(pcp);
    return -1;
}
#endif /* BRLCAD_ENABLE_OBOL */

void
QFBSocket::client_handler()
{
    QTCAD_SLOT("QFBSocket::client_handler", 1);
    bu_log("client_handler\n");

    // Get the current libpkg connection
    struct pkg_conn *pkc = fbsp->fbs_clients[ind].fbsc_pkg;

    // Set the server-data pointer for the pkg_switch callback functions.
    // Legacy path: handlers cast pkc_server_data to struct fb *.
    // Obol  path: handlers cast pkc_server_data to struct fbserv_obj *.
#ifdef BRLCAD_ENABLE_OBOL
    pkc->pkc_server_data = (void *)fbsp;
#else
    pkc->pkc_server_data = (void *)fbsp->fbs_fbp;
#endif

    // Read data.  NOTE:  we're using the Qt read routines rather than
    // pkg_suckin, so we can't call fbs_existing_client_hander from libdm.
    // Initially tried pkg_suckin, but it didn't seem to work with the socket
    // as set up by Qt.
    QByteArray dbuff = s->read(s->bytesAvailable());

    // We may not have processed all the read data last time, so append
    // this to anything left over from before
    buff.append(dbuff);

    // If we don't have anything, we're done
    if (!buff.length())
	return;

    // Now that we have the data read using Qt methods, prepare for processing
    // using libpkg data structures.
    pkc->pkc_inbuf = (char *)realloc(pkc->pkc_inbuf, buff.length());
    memcpy(pkc->pkc_inbuf, buff.data(), buff.length());
    pkc->pkc_incur = 0;
    pkc->pkc_inlen = pkc->pkc_inend = buff.length();

    // Now it's up to libpkg - if anything is left over, we'll know it after
    // processing.  Clear buff so we're ready to preserve remaining data for
    // the next processing cycle.
    buff.clear();

    // Use the defined callbacks to handle the data sent from the client
    if ((pkg_process(pkc)) < 0)
	bu_log("client_handler pkg_process error encountered\n");

    if (pkc->pkc_inend != pkc->pkc_inlen - 1) {
	// If pkg_process didn't use all of the read data, store the rest for
	// the next cycle.
	//
	// TODO - need to find a way to test to to make sure we're copying the
	// right part of the buffer
	buff.append(&pkc->pkc_inbuf[pkc->pkc_inend], pkc->pkc_inlen - pkc->pkc_inend);
    }

    emit updated();

    // If we've got callbacks, execute them now.
    if (fbsp->fbs_callback != (void (*)(void *))FBS_CALLBACK_NULL) {
	/* need to cast func pointer explicitly to get the function call */
	void (*cfp)(void *);
	cfp = (void (*)(void *))fbsp->fbs_callback;
	cfp(fbsp->fbs_clientData);
    }
}


QFBServer::QFBServer(struct fbserv_obj *fp)
{
    fbsp = fp;
}

QFBServer::~QFBServer()
{
}

void
QFBServer::on_Connect()
{
    QTCAD_SLOT("QFBServer::on_Connect", 1);
    // Have a new connection pending, accept it.
    QTcpSocket *tcps = nextPendingConnection();

    bu_log("new connection");

    QFBSocket *fs = new QFBSocket;
    fs->s = tcps;
    fs->fbsp = fbsp;

    int fd = tcps->socketDescriptor();
    bu_log("fd: %d\n", fd);
    struct pkg_conn *pc;
    BU_GET(pc, struct pkg_conn);
    pc->pkc_magic = PKG_MAGIC;
    pc->pkc_fd = fd;
#ifdef BRLCAD_ENABLE_OBOL
    /* Obol path: use the Obol-native pkg_switch that writes into fbs_pixbuf. */
    pc->pkc_switch = obol_fbs_pkg_switch();
#else
    pc->pkc_switch = fbs_pkg_switch();
#endif
    pc->pkc_errlog = 0;
    pc->pkc_left = -1;
    pc->pkc_buf = (char *)0;
    pc->pkc_curpos = (char *)0;
    pc->pkc_strpos = 0;
    pc->pkc_incur = pc->pkc_inend = 0;

#ifdef BRLCAD_ENABLE_OBOL
    fs->ind = qdm_obol_new_client(fbsp, pc, (void *)fs);
#else
    fs->ind = fbs_new_client(fbsp, pc, (void *)fs);
#endif
    if (fs->ind == -1) {
	bu_log("new connection failed");
	BU_PUT(pc, struct pkg_conn);
	tcps->close();
    }
}

/* Check if we're already listening. */
int
qdm_is_listening(struct fbserv_obj *fbsp)
{
    bu_log("is_listening\n");
    if (fbsp->fbs_listener.fbsl_fd >= 0) {
	return 1;
    }
    return 0;
}

int
qdm_listen_on_port(struct fbserv_obj *fbsp, int available_port)
{
    bu_log("listen on port\n");
    QFBServer *nl = new QFBServer(fbsp);
    nl->port = available_port;
    if (!nl->listen(QHostAddress::LocalHost, available_port)) {
	bu_log("Failed to start listening on %d\n", available_port);
	delete nl;
	return 0;
    }
    fbsp->fbs_listener.fbsl_chan = (void *)nl;
    fbsp->fbs_listener.fbsl_fd = nl->socketDescriptor();
    if (fbsp->fbs_listener.fbsl_fd >= 0)
	return 1;
    return 0;
}

void
qdm_open_server_handler(struct fbserv_obj *fbsp)
{
    bu_log("open_server_handler\n");
    QFBServer *nl = (QFBServer *)fbsp->fbs_listener.fbsl_chan;
    if (!nl->isListening())
	bu_log("not listening!\n");
    QObject::connect(nl, &QTcpServer::newConnection, nl, &QFBServer::on_Connect, Qt::QueuedConnection);
}

void
qdm_close_server_handler(struct fbserv_obj *fbsp)
{
    bu_log("close_server_handler\n");
    QFBServer *nl = (QFBServer *)fbsp->fbs_listener.fbsl_chan;
    delete nl;
}

#ifndef BRLCAD_ENABLE_OBOL
/* qdm_open_client_handler and qdm_open_sw_client_handler require the libdm
 * QgGL / QgSW widgets.  They are unused (and excluded) in Obol builds where
 * QgEdMainWindow sets qdm_open_obol_client_handler instead. */
#ifdef BRLCAD_OPENGL
void
qdm_open_client_handler(struct fbserv_obj *fbsp, int i, void *data)
{
    bu_log("open_client_handler\n");
    fbsp->fbs_clients[i].fbsc_chan = data;
    QFBSocket *s = (QFBSocket *)data;
    QObject::connect(s->s, &QTcpSocket::readyRead, s, &QFBSocket::client_handler, Qt::QueuedConnection);

    QgGL *ctx = (QgGL *)dm_get_ctx(fb_get_dm(fbsp->fbs_fbp));
    if (ctx) {
	QObject::connect(s, &QFBSocket::updated, ctx, &QgGL::need_update, Qt::QueuedConnection);
    }
}
#endif

// Because swrast uses a bsg_view as its context pointer, we need to unpack the
// app data to get our Qt widget ctx when using that display method.  In other
// words, the swrast backend is generic - it has no knowledge of Qt - and the
// Qt widget we need to notify for update/redraw purposes is coming (from the
// libdm perspective) solely from the application - which is why it lives in
// the user data slot rather than the context.  (The swrast offscreen rendering
// context is still present and relevant, hence the need for a separate user
// pointer.)  The advantage of using a generic swrast backend is that such a
// setup allows us to use the same logic both for Qt widget rendering and
// headless image generation.
void
qdm_open_sw_client_handler(struct fbserv_obj *fbsp, int i, void *data)
{
    bu_log("open_client_handler\n");
    fbsp->fbs_clients[i].fbsc_chan = data;
    QFBSocket *s = (QFBSocket *)data;
    QObject::connect(s->s, &QTcpSocket::readyRead, s, &QFBSocket::client_handler, Qt::QueuedConnection);

    QgSW *ctx = (QgSW *)dm_get_udata(fb_get_dm(fbsp->fbs_fbp));
    if (ctx) {
	QObject::connect(s, &QFBSocket::updated, ctx, &QgSW::need_update, Qt::QueuedConnection);
    }
}
#endif /* !BRLCAD_ENABLE_OBOL */

#ifdef BRLCAD_ENABLE_OBOL
/* Obol path: the view widget pointer is stored in fbs_clientData by do_obol_init().
 * Connect new-data notification to the Obol view's need_update() slot so that
 * incoming rt pixels trigger a repaint (which overlays the fb in paintGL). */
void
qdm_open_obol_client_handler(struct fbserv_obj *fbsp, int i, void *data)
{
    bu_log("open_obol_client_handler\n");
    fbsp->fbs_clients[i].fbsc_chan = data;
    QFBSocket *s = (QFBSocket *)data;
    QObject::connect(s->s, &QTcpSocket::readyRead, s, &QFBSocket::client_handler, Qt::QueuedConnection);

    QgObolView *ctx = (QgObolView *)fbsp->fbs_clientData;
    if (ctx) {
	QObject::connect(s, &QFBSocket::updated, ctx, [ctx]() {
	    ctx->need_update(QG_VIEW_REFRESH);
	}, Qt::QueuedConnection);
    }
}
#  ifdef OBOL_BUILD_DUAL_GL
void
qdm_open_obol_sw_client_handler(struct fbserv_obj *fbsp, int i, void *data)
{
    bu_log("open_obol_sw_client_handler\n");
    fbsp->fbs_clients[i].fbsc_chan = data;
    QFBSocket *s = (QFBSocket *)data;
    QObject::connect(s->s, &QTcpSocket::readyRead, s, &QFBSocket::client_handler, Qt::QueuedConnection);

    QgObolSwrastView *ctx = (QgObolSwrastView *)fbsp->fbs_clientData;
    if (ctx) {
	QObject::connect(s, &QFBSocket::updated, ctx, [ctx]() {
	    ctx->need_update(QG_VIEW_REFRESH);
	}, Qt::QueuedConnection);
    }
}
#  endif /* OBOL_BUILD_DUAL_GL */
#endif /* BRLCAD_ENABLE_OBOL */

void
qdm_close_client_handler(struct fbserv_obj *fbsp, int i)
{
    bu_log("close_client_handler\n");
    QFBSocket *s = (QFBSocket *)fbsp->fbs_clients[i].fbsc_chan;
    delete s;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

