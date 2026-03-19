/*                        F B S E R V . H
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
/** @addtogroup libdm */
/** @{ */
/** @file fbserv.h
 *
 * @brief
 * This header holds generic routines and data structures used for TCP based
 * communication between a framebuffer and a remote process.  Variations on
 * this logic, based originally on the stand-alone fbserv program,  are at the
 * core of MGED and Archer's ability to display incoming image data from a
 * separate rt process.
 *
 * Asynchronous interprocess communication and event monitoring is (as of 2021)
 * still very much platform and toolkit specific.  Hence, these data structures
 * contain some void pointers which are used by individual applications to
 * connect their own specific methods (for example, Tcl_Channel) to handle this
 * problem.  Improving this to be more generic and less dependent on specific
 * toolkits and/or platform mechanisms would be a laudable goal, if practical.
 *
 */

#ifndef DM_FBSERV_H
#define DM_FBSERV_H

#include "common.h"
#include "bio.h"
#include "bnetwork.h"
#include "bsocket.h"
#include "bu/defines.h"
#include "bu/log.h"
#include "bu/vls.h"
#include "pkg.h"
#include "dm/defines.h"

__BEGIN_DECLS

/* Framebuffer server object */

#define NET_LONG_LEN 4 /**< @brief # bytes to network long */
#define MAX_CLIENTS 32
#define MAX_PORT_TRIES 100
#define FBS_CALLBACK_NULL (void (*)(void))NULL
#define FBSERV_OBJ_NULL (struct fbserv_obj *)NULL

/*
 * Framebuffer protocol message types.  These are defined here (in addition to
 * dm.h) so that code that handles the fbserv wire protocol without linking
 * the full libdm — e.g. an Obol-native pixel-buffer server — can use them
 * without pulling in all of dm.h.
 */
#ifndef MSG_FBOPEN
#  define MSG_FBOPEN        1
#  define MSG_FBCLOSE       2
#  define MSG_FBCLEAR       3
#  define MSG_FBREAD        4
#  define MSG_FBWRITE       5
#  define MSG_FBCURSOR      6
#  define MSG_FBWINDOW      7
#  define MSG_FBZOOM        8
#  define MSG_FBSCURSOR     9
#  define MSG_FBVIEW        10
#  define MSG_FBGETVIEW     11
#  define MSG_FBRMAP        12
#  define MSG_FBWMAP        13
#  define MSG_FBHELP        14
#  define MSG_FBREADRECT    15
#  define MSG_FBWRITERECT   16
#  define MSG_FBFLUSH       17
#  define MSG_FBFREE        18
#  define MSG_FBGETCURSOR   19
#  define MSG_DATA          20
#  define MSG_RETURN        21
#  define MSG_CLOSE         22
#  define MSG_ERROR         23
#  define MSG_FBPOLL        30
#  define MSG_FBSETCURSOR   31
#  define MSG_FBBWREADRECT  32
#  define MSG_FBBWWRITERECT 33
#  define MSG_NORETURN     100
#endif /* MSG_FBOPEN */

struct fbserv_obj;

struct fbserv_listener {
    int fbsl_fd;                        /**< @brief socket to listen for connections */
    void *fbsl_chan;                    /**< @brief platform/toolkit specific channel */
    int fbsl_port;                      /**< @brief port number to listen on */
    int fbsl_listen;                    /**< @brief !0 means listen for connections */
    struct fbserv_obj *fbsl_fbsp;       /**< @brief points to its fbserv object */
};


struct fbserv_client {
    int fbsc_fd;                        /**< @brief socket to send data down */
    void *fbsc_chan;                    /**< @brief platform/toolkit specific channel */
    void *fbsc_handler;                 /**< @brief platform/toolkit specific handler */
    struct pkg_conn *fbsc_pkg;
    struct fbserv_obj *fbsc_fbsp;       /**< @brief points to its fbserv object */
};


struct fbserv_obj {
    struct fb *fbs_fbp;                            /**< @brief framebuffer pointer (legacy libdm path; NULL in Obol builds) */
    void *fbs_interp;                              /**< @brief interpreter */
    struct fbserv_listener fbs_listener;           /**< @brief data for listening */
    struct fbserv_client fbs_clients[MAX_CLIENTS]; /**< @brief connected clients */

    int (*fbs_is_listening)(struct fbserv_obj *);          /**< @brief return 1 if listening, else 0 */
    int (*fbs_listen_on_port)(struct fbserv_obj *, int);  /**< @brief return 1 on success, 0 on failure */
    void (*fbs_open_server_handler)(struct fbserv_obj *);   /**< @brief platform/toolkit method to open listener handler */
    void (*fbs_close_server_handler)(struct fbserv_obj *);   /**< @brief platform/toolkit method to close handler listener */
    void (*fbs_open_client_handler)(struct fbserv_obj *, int, void *);   /**< @brief platform/toolkit specific client handler setup (called by fbs_new_client) */
    void (*fbs_close_client_handler)(struct fbserv_obj *, int);   /**< @brief platform/toolkit method to close handler for client at index client_id */

    void (*fbs_callback)(void *);                  /**< @brief callback function */
    void *fbs_clientData;
    struct bu_vls *msgs;
    int fbs_mode;                                  /**< @brief 0-off, 1-underlay, 2-interlay, 3-overlay */

    /* Obol path: raw RGB pixel buffer that replaces fbs_fbp when BRLCAD_ENABLE_OBOL
     * is active.  Allocated/resized by the ert command; freed on ged teardown.
     * Stride is fbs_pixbuf_w * 3 bytes (packed RGB888, libfb bottom-left origin). */
    unsigned char *fbs_pixbuf;  /**< @brief raw RGB pixel data (NULL when unused) */
    int fbs_pixbuf_w;           /**< @brief pixel buffer width  (0 when unused) */
    int fbs_pixbuf_h;           /**< @brief pixel buffer height (0 when unused) */
};

DM_EXPORT extern struct pkg_switch *fbs_pkg_switch(void);

/*
 * The following five functions provide the generic TCP communication setup
 * for the framebuffer server object.  They have no struct-fb / libdm
 * dependencies; moving them here as static inline removes the libdm link
 * requirement from Obol builds and from any code that needs only the
 * communication plumbing (not the full struct-fb rendering path).
 *
 * fbs_pkg_switch() (above) returns the legacy struct-fb-backed PKG handler
 * table and stays in libdm/fbserv.c.  Obol callers use their own pkg_switch.
 */

/* The static-inline helpers below are intended to be called from toolkit-
 * specific fbserv implementations (libtclcad, qged, libged/dm/ert.cpp).
 * Any translation unit that includes fbserv.h (via ged/defines.h → dm/fbserv.h)
 * but does not call these helpers would produce -Wunused-function warnings.
 * Suppress them explicitly so that the header remains includable everywhere.
 */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif

/* Internal helper: tear down a single client slot. */
static inline void
fbs_drop_client(struct fbserv_obj *fbsp, int sub)
{
    if (fbsp->fbs_clients[sub].fbsc_pkg != PKC_NULL) {
	pkg_close(fbsp->fbs_clients[sub].fbsc_pkg);
	fbsp->fbs_clients[sub].fbsc_pkg = PKC_NULL;
    }
    if (fbsp->fbs_clients[sub].fbsc_fd != 0) {
	(*fbsp->fbs_close_client_handler)(fbsp, sub);
	fbsp->fbs_clients[sub].fbsc_fd = 0;
    }
}


static inline int
fbs_open(struct fbserv_obj *fbsp, int port)
{
    int i;
    int available_port = port;
    int have_listen = 0;

    if ((*fbsp->fbs_is_listening)(fbsp))
	return BRLCAD_OK;

    if (available_port < 0)
	available_port = 5559;
    else if (available_port < 1024)
	available_port += 5559;

    for (i = 0; i < MAX_PORT_TRIES; ++i) {
	if (!(*fbsp->fbs_listen_on_port)(fbsp, available_port))
	    ++available_port;
	else {
	    have_listen = 1;
	    break;
	}
    }

    if (!have_listen) {
	if (fbsp->msgs)
	    bu_vls_printf(fbsp->msgs, "fbs_open: failed to hang a listen on ports %d - %d\n",
			 port, available_port);
	fbsp->fbs_listener.fbsl_port = -1;
	return BRLCAD_ERROR;
    }

    fbsp->fbs_listener.fbsl_port = available_port;
    (*fbsp->fbs_open_server_handler)(fbsp);
    return BRLCAD_OK;
}


static inline int
fbs_close(struct fbserv_obj *fbsp)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; ++i)
	fbs_drop_client(fbsp, i);

    (*fbsp->fbs_close_server_handler)(fbsp);

    if (0 <= fbsp->fbs_listener.fbsl_fd)
	close(fbsp->fbs_listener.fbsl_fd);
    fbsp->fbs_listener.fbsl_fd  = -1;
    fbsp->fbs_listener.fbsl_port = -1;

    return BRLCAD_OK;
}


static inline void
fbs_setup_socket(int fd)
{
    int on     = 1;
    int retval = 0;

#if defined(SO_KEEPALIVE)
    if ((retval = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof(on))) < 0)
	bu_log("setsockopt (SO_KEEPALIVE) error return: %d", retval);
#endif
#if defined(SO_RCVBUF)
    {
	int m = -1, n = -1, val, size;
	for (size = 256; size > 16; size /= 2) {
	    val = size * 1024;
	    m = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&val, sizeof(val));
	    val = size * 1024;
	    n = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *)&val, sizeof(val));
	    if (m >= 0 && n >= 0) break;
	}
	if (m < 0 || n < 0)
	    bu_log("setup_socket: setsockopt()");
    }
#endif
}


static inline int
fbs_new_client(struct fbserv_obj *fbsp, struct pkg_conn *pcp, void *data)
{
    int i;
    if (pcp == PKC_ERROR)
	return -1;

    for (i = MAX_CLIENTS - 1; i >= 0; i--) {
	if (fbsp->fbs_clients[i].fbsc_fd != 0)
	    continue;

	fbsp->fbs_clients[i].fbsc_fd   = pcp->pkc_fd;
	fbsp->fbs_clients[i].fbsc_pkg  = pcp;
	fbsp->fbs_clients[i].fbsc_fbsp = fbsp;
	fbs_setup_socket(pcp->pkc_fd);

	(*fbsp->fbs_open_client_handler)(fbsp, i, data);
	return i;
    }

    bu_log("fbs_new_client: too many clients\n");
    pkg_close(pcp);
    return -1;
}


/*
 * Process one round of data from all existing clients.
 *
 * pkc_server_data is set differently depending on the rendering path:
 *   Non-Obol: handlers cast it to (struct fb *) — use fbs_fbp.
 *   Obol:     handlers cast it to (struct fbserv_obj *) — use fbsp itself.
 */
static inline void
fbs_existing_client_handler(void *clientData, int UNUSED(mask))
{
    int i;
    struct fbserv_client *fbscp = (struct fbserv_client *)clientData;
    struct fbserv_obj *fbsp = fbscp->fbsc_fbsp;
    int fd = fbscp->fbsc_fd;
#ifdef BRLCAD_ENABLE_OBOL
    void *server_data = (void *)fbsp;
#else
    void *server_data = (void *)fbsp->fbs_fbp;
#endif

    for (i = MAX_CLIENTS - 1; i >= 0; i--) {
	if (fbsp->fbs_clients[i].fbsc_fd == 0)
	    continue;

	fbsp->fbs_clients[i].fbsc_pkg->pkc_server_data = server_data;

	if ((pkg_process(fbsp->fbs_clients[i].fbsc_pkg)) < 0)
	    bu_log("pkg_process error encountered (1)\n");

	if (fbsp->fbs_clients[i].fbsc_fd != fd)
	    continue;

	if (pkg_suckin(fbsp->fbs_clients[i].fbsc_pkg) <= 0) {
	    fbs_drop_client(fbsp, i);
	    continue;
	}

	if ((pkg_process(fbsp->fbs_clients[i].fbsc_pkg)) < 0)
	    bu_log("pkg_process error encountered (2)\n");
    }

    if (fbsp->fbs_callback != (void (*)(void *))FBS_CALLBACK_NULL) {
	void (*cfp)(void *);
	cfp = (void (*)(void *))fbsp->fbs_callback;
	cfp(fbsp->fbs_clientData);
    }
}

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

__END_DECLS

#endif /* DM_FBSERV_H */
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
