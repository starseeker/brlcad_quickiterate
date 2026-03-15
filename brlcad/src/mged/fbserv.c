/*                        F B S E R V . C
 * BRL-CAD
 *
 * Copyright (c) 1995-2025 United States Government as represented by
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
/** @file mged/fbserv.c
 *
 * Step 7.20: fbserv stub — the framebuffer-server path required a libdm
 * display manager handle (mp_fbp/mp_netfd/mp_clients), all of which have
 * been removed from mged_pane.  MGED is now Obol-only and has no legacy
 * dm attach path, so fbserv_set_port is a no-op.
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bio.h"
#include "bnetwork.h"
#include "bsocket.h"

#include "tcl.h"
#include "vmath.h"
#include "raytrace.h"

#include "./mged.h"
#include "./mged_dm.h"


/*
 * Communication error.  An error occurred on the PKG link.
 */
static void
communications_error(const char *str)
{
    bu_log("%s", str);
}


static void
fbserv_setup_socket(int fd)
{
    int on = 1;

#if defined(SO_KEEPALIVE)
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof(on)) < 0) {
	perror("setsockopt (SO_KEEPALIVE)");
    }
#endif
#if defined(SO_RCVBUF)
    /* try to set our buffers up larger */
    {
	int m = -1, n = -1;
	int val;
	int size;

	for (size = 256; size > 16; size /= 2) {
	    val = size * 1024;
	    m = setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
		    (char *)&val, sizeof(val));
	    val = size * 1024;
	    n = setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
		    (char *)&val, sizeof(val));
	    if (m >= 0 && n >= 0) break;
	}

	if (m < 0 || n < 0)
	    perror("setsockopt");
    }
#endif
}


static void
fbserv_drop_client(int sub)
{
    struct mged_state *s = MGED_STATE;
    if (clients[sub].c_pkg != PKC_NULL) {
	pkg_close(clients[sub].c_pkg);
	Tcl_DeleteChannelHandler(clients[sub].c_chan,
		clients[sub].c_handler,
		(ClientData)(size_t)clients[sub].c_fd);

	if (dm_interp(DMP) != NULL) {
	    Tcl_Close((Tcl_Interp *)dm_interp(DMP), clients[sub].c_chan);
	}
	clients[sub].c_chan = NULL;
	clients[sub].c_pkg = PKC_NULL;
	clients[sub].c_fd = 0;
    }
}

/*
 * Process arrivals from existing clients.
 */
static void
fbserv_existing_client_handler(ClientData clientData, int UNUSED(mask))
{
    struct mged_state *s = MGED_STATE;
    int i;

    /* NOTE: assumes fd's will be small */
    int fd = (uint16_t)((long)(uintptr_t)clientData & 0xFFFF);

    int npp;			/* number of processed packages */
    struct mged_pane *dlp = MGED_PANE_NULL;
    struct mged_pane *save_pane;

    /* Step 6.b: search active_pane_set for matching fd. */
    for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	if (!mp->mp_dmp) continue;
	for (i = MAX_CLIENTS-1; i >= 0; i--)
	    if (fd == mp->mp_clients[i].c_fd) {
		dlp = mp;
		goto found;
	    }
    }

    return;

found:
    /* save */
    save_pane = s->mged_curr_pane;

    {
	/* Find wrapper pane for dlp to call set_curr_pane. */
	struct mged_pane *mp = NULL;
	for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	    struct mged_pane *p2 = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	    if (p2 == dlp) { mp = p2; break; }
	}
	/* Step 7.5: always use set_curr_pane; if no wrapper pane found, keep current. */
	if (mp) set_curr_pane(s, mp);
    }
    for (i = MAX_CLIENTS-1; i >= 0; i--) {
	if (clients[i].c_fd == 0)
	    continue;

	if ((npp = pkg_process(clients[i].c_pkg)) < 0)
	    bu_log("pkg_process error encountered (1)\n");

	if (npp > 0) {
	    DMP_dirty = 1;
	    if (DMP) dm_set_dirty(DMP, 1);
	}

	if (clients[i].c_fd != fd)
	    continue;

	if (pkg_suckin(clients[i].c_pkg) <= 0) {
	    /* Probably EOF */
	    fbserv_drop_client(i);

	    continue;
	}

	if ((npp = pkg_process(clients[i].c_pkg)) < 0)
	    bu_log("pkg_process error encountered (2)\n");

	if (npp > 0) {
	    DMP_dirty = 1;
	    if (DMP) dm_set_dirty(DMP, 1);
	}
    }

    /* restore */
    set_curr_pane(s, save_pane);
}


static struct pkg_conn *
fbserv_makeconn(int fd, const struct pkg_switch *switchp)
{
    struct pkg_conn *pc;
#ifdef HAVE_WINSOCK_H
    WORD wVersionRequested;		/* initialize Windows socket networking, increment reference count */
    WSADATA wsaData;
#endif

    if ((pc = (struct pkg_conn *)malloc(sizeof(struct pkg_conn))) == PKC_NULL) {
	communications_error("fbserv_makeconn: malloc failure\n");
	return PKC_ERROR;
    }

#ifdef HAVE_WINSOCK_H
    wVersionRequested = MAKEWORD(1, 1);
    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
	communications_error("fbserv_makeconn:  could not find a usable WinSock DLL\n");
	return PKC_ERROR;
    }
#endif

    memset((char *)pc, 0, sizeof(struct pkg_conn));
    pc->pkc_magic = PKG_MAGIC;
    pc->pkc_fd = fd;
    pc->pkc_switch = switchp;
    pc->pkc_errlog = 0;
    pc->pkc_left = -1;
    pc->pkc_buf = (char *)0;
    pc->pkc_curpos = (char *)0;
    pc->pkc_strpos = 0;
    pc->pkc_incur = pc->pkc_inend = 0;

    return pc;
}

static void
fbserv_new_client(struct pkg_conn *pcp, Tcl_Channel chan)
{
    struct mged_state *s = MGED_STATE;
    int i;

    if (pcp == PKC_ERROR)
	return;

    for (i = MAX_CLIENTS-1; i >= 0; i--) {
	if (clients[i].c_fd != 0)
	    continue;

	/* Found an available slot */
	clients[i].c_pkg = pcp;
	clients[i].c_fd = pcp->pkc_fd;
	fbserv_setup_socket(pcp->pkc_fd);

	clients[i].c_chan = chan;
	clients[i].c_handler = fbserv_existing_client_handler;
	Tcl_CreateChannelHandler(clients[i].c_chan, TCL_READABLE, clients[i].c_handler, (ClientData)(size_t)clients[i].c_fd);
	return;
    }

    bu_log("fbserv_new_client: too many clients\n");
    pkg_close(pcp);
}

/*
 * Accept any new client connections.  Callback signature matches
 * Tcl_TcpServerAcceptProc as required by Tcl_OpenTcpServer.
 */
static void
fbserv_new_client_handler(ClientData clientData,
	Tcl_Channel chan,
	char *UNUSED(host),
	int UNUSED(port))
{
    struct mged_state *s = MGED_STATE;
    /* Step 7.5: use pane for save/restore (was scdlp). */
    struct mged_pane *save_pane = s->mged_curr_pane;

    /* clientData is the mged_dm pointer passed to Tcl_OpenTcpServer */
    struct mged_dm *dlp = (struct mged_dm *)clientData;
    if (dlp == NULL)
	return;

    /* Find wrapper pane for dlp and make it current. */
    for (size_t pi = 0; pi < BU_PTBL_LEN(&active_pane_set); pi++) {
	struct mged_pane *mp = (struct mged_pane *)BU_PTBL_GET(&active_pane_set, pi);
	if (mp == dlp) { set_curr_pane(s, mp); break; }
    }

    /* Extract the native OS handle from the connected channel and wrap it
     * in a pkg_conn so the rest of the fbserv machinery can use it. */
    uintptr_t fd;
    if (Tcl_GetChannelHandle(chan, TCL_READABLE, (ClientData *)&fd) == TCL_OK)
	fbserv_new_client(fbserv_makeconn((int)fd, pkg_switch), chan);

    /* restore */
    set_curr_pane(MGED_STATE, save_pane);
}

void
fbserv_set_port(const struct bu_structparse *UNUSED(sp), const char *UNUSED(c1), void *UNUSED(v1), const char *UNUSED(c2), void *UNUSED(v2))
{
    struct mged_state *s = MGED_STATE;
    int i;
    int save_port;

    /* Stage 7 guard: fbserv requires a real libdm display manager; skip for
     * Obol panes (dm_dmp == NULL).  Obol has its own fb overlay mechanism. */
    if (!DMP) return;

    /* Step 7.7: access dm_netfd/dm_netchan via the pane's mp_dm pointer rather
     * than through s->mged_curr_dm.  The !DMP guard above ensures mp_dm != NULL. */
    struct mged_pane *cdm = s->mged_curr_pane;

#define MAX_PORT_TRIES 100

    /* Check to see if previously active --- if so then deactivate */
    if (s->mged_curr_dm->dm_netchan != NULL) {
	/* first drop all clients */
	for (i = 0; i < MAX_CLIENTS; ++i)
	    fbserv_drop_client(i);

	/* Close the server channel; this unregisters the accept callback and
	 * closes the underlying listen socket. */
	if (dm_interp(DMP) != NULL)
	    Tcl_Close((Tcl_Interp *)dm_interp(DMP), cdm->mp_netchan);

	s->mged_curr_dm->dm_netchan = NULL;
	s->mged_curr_dm->dm_netfd = -1;
    }

    if (!mged_variables->mv_listen)
	return;

    if (!mged_variables->mv_fb) {
	mged_variables->mv_listen = 0;
	return;
    }

    save_port = mged_variables->mv_port;

    /* Compute the actual port number to try first */
    int port;
    if (mged_variables->mv_port < 0)
	port = 5559;
    else if (mged_variables->mv_port < 1024)
	port = mged_variables->mv_port + 5559;
    else
	port = mged_variables->mv_port;

    /* Try a reasonable number of times to hang a listen.
     * Tcl_OpenTcpServer is fully cross-platform and replaces the previous
     * POSIX-only pkg_permserver + Tcl_CreateFileHandler approach. */
    for (i = 0; i < MAX_PORT_TRIES; ++i) {
	s->mged_curr_dm->dm_netchan = NULL;
	if (dm_interp(DMP) != NULL) {
	    /* NULL host means listen on all interfaces (INADDR_ANY) */
	    s->mged_curr_dm->dm_netchan = Tcl_OpenTcpServer(
		    (Tcl_Interp *)dm_interp(DMP), port, NULL,
		    fbserv_new_client_handler, (ClientData)s->mged_curr_dm);
	}

	if (cdm->mp_netchan == NULL)
	    ++port;
	else
	    break;
    }

    if (s->mged_curr_dm->dm_netchan == NULL) {
	mged_variables->mv_port = save_port;
	mged_variables->mv_listen = 0;
	bu_log("fbserv_set_port: failed to hang a listen on ports %d - %d\n",
		save_port, save_port + MAX_PORT_TRIES - 1);
    } else {
	mged_variables->mv_port = port;
	/* Stash the underlying fd for diagnostics; not used for I/O. */
	{
	    uintptr_t fd = 0;
	    Tcl_GetChannelHandle(s->mged_curr_dm->dm_netchan, TCL_READABLE, (ClientData *)&fd);
	    s->mged_curr_dm->dm_netfd = (int)fd;
	}
    }
}

/*
 * This is where we go for message types we don't understand.
 */
void
fbserv_set_port(const struct bu_structparse *UNUSED(sdp),
const char *UNUSED(name),
void *UNUSED(base),
const char *UNUSED(value),
void *UNUSED(data))
{
    /* Step 7.20: libdm framebuffer server removed — no-op. */
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
