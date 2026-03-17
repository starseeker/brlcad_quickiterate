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

DM_EXPORT extern int fbs_open(struct fbserv_obj *fbsp, int port);
DM_EXPORT extern int fbs_close(struct fbserv_obj *fbsp);
DM_EXPORT extern struct pkg_switch *fbs_pkg_switch(void);
DM_EXPORT extern void fbs_setup_socket(int fd);
DM_EXPORT extern int fbs_new_client(struct fbserv_obj *fbsp, struct pkg_conn *pcp, void *data);
DM_EXPORT extern void fbs_existing_client_handler(void *clientData, int mask);


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
