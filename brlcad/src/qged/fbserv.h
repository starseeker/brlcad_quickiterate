/*                        F B S E R V . H
 * BRL-CAD
 *
 * Copyright (c) 2021-2025 United States Government as represented by
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
/** @file fbserv.h
 *
 * Brief description
 *
 */

#ifndef QDM_FBSERV_H
#define QDM_FBSERV_H

#include "common.h"

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <iostream>

#include "dm/fbserv.h"

// Per client info
class QFBSocket : public QObject
{
    Q_OBJECT

    public:
	QTcpSocket *s;
	int ind;
	struct fbserv_obj *fbsp;

    signals:
	void updated();

    public slots:
	void client_handler();

    private:
        QByteArray buff;
};

// Overall server that sets up clients
// in response to connection requests
class QFBServer : public QTcpServer
{
    Q_OBJECT

    public:
	QFBServer(struct fbserv_obj *fp = NULL);
	~QFBServer();

	int port = -1;
	struct fbserv_obj *fbsp;

    public slots:
	void on_Connect();
};


__BEGIN_DECLS

extern int
qdm_is_listening(struct fbserv_obj *fbsp);
extern int
qdm_listen_on_port(struct fbserv_obj *fbsp, int available_port);
extern void
qdm_open_server_handler(struct fbserv_obj *fbsp);
extern void
qdm_close_server_handler(struct fbserv_obj *fbsp);
#ifdef BRLCAD_OPENGL
extern void
qdm_open_client_handler(struct fbserv_obj *fbsp, int i, void *data);
#endif
extern void
qdm_open_sw_client_handler(struct fbserv_obj *fbsp, int i, void *data);
extern void
qdm_close_client_handler(struct fbserv_obj *fbsp, int sub);

/**
 * Client handler for the Obol raytrace path (ert2).
 *
 * Installed as gedp->ged_fbs->fbs_open_client_handler by qged's
 * QgEdApp::run_cmd() when ert2 is executed.
 *
 * Connects QFBSocket::updated → QgObolWidget::onRtPixelsUpdated so that
 * each framebuffer packet received from rt causes an incremental texture
 * update via Obol's SoSFImage field notification mechanism.
 *
 * Requires fbsp->fbs_clientData to point to an ObolRtCtx allocated by
 * QgEdApp::run_cmd() before ert2 is invoked.
 */
extern void
qdm_open_obol_client_handler(struct fbserv_obj *fbsp, int i, void *data);

__END_DECLS

/* ── ObolRtCtx ────────────────────────────────────────────────────────────
 *
 * Passed as fbsp->fbs_clientData when running ert2.  Allows the fbserv
 * client handler and the cleanup callback to share the Obol widget pointer
 * and the memory framebuffer handle across the ert2 lifecycle.
 *
 * Layout:
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  QgEdApp::run_cmd()                                                  │
 *  │   ├── allocates  ObolRtCtx {widget, memfb, w, h}                    │
 *  │   ├── sets  gedp->ged_fbs->fbs_fbp = ctx->memfb                     │
 *  │   ├── sets  gedp->ged_fbs->fbs_clientData = ctx                     │
 *  │   └── sets  gedp->ged_fbs->fbs_open_client_handler                  │
 *  │                           = &qdm_open_obol_client_handler            │
 *  │  ert2.cpp (libged) runs:                                             │
 *  │   └── fbs_open(), _ged_run_rt() with -F <port>                      │
 *  │  rt connects to fbserv → qdm_open_obol_client_handler() fires:       │
 *  │   └── widget->beginRtOverlay(fb, w, h)                              │
 *  │   └── QFBSocket::updated → widget->onRtPixelsUpdated()              │
 *  │  ert2_raytrace_done() (LINGER callback):                             │
 *  │   ├── widget->onRtDone()                                             │
 *  │   ├── fb_close(ctx->memfb)                                          │
 *  │   ├── fbs_close(gedp->ged_fbs)                                      │
 *  │   └── delete ctx                                                     │
 *  └──────────────────────────────────────────────────────────────────────┘
 */
class QgObolWidget;

struct ObolRtCtx {
    QgObolWidget *widget = nullptr;  /**< @brief Obol rendering widget */
    struct fb    *memfb  = nullptr;  /**< @brief in-process memory framebuffer */
    int           width  = 0;        /**< @brief framebuffer width  (pixels) */
    int           height = 0;        /**< @brief framebuffer height (pixels) */
};

#endif /* QDM_FBSERV_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
