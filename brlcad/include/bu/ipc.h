/*                          I P C . H
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @addtogroup bu_ipc */
/** @{ */
/** @file bu/ipc.h
 *
 * @brief
 *  Transport-agnostic byte-stream IPC channel API.
 *
 * bu_ipc provides a clean send/receive abstraction over several local
 * inter-process communication transports.  The channel setup routine
 * probes available mechanisms in preference order and returns an opaque
 * handle; callers never need to know which transport was selected.
 *
 * ### Motivation
 *
 * BRL-CAD historically used TCP sockets (via libpkg / fbserv) for
 * incremental display of raytrace results.  While TCP works everywhere,
 * it requires port allocation, may be blocked by firewalls, and has
 * higher overhead than local IPC.  bu_ipc replaces that with:
 *
 *   anonymous pipes  →  most portable and lowest overhead
 *   Unix domain sockets  →  fallback when pipes can't carry file descriptors
 *   TCP loopback  →  last resort; works everywhere but needs a free port
 *
 * ### Why full transport-agnosticism is bounded
 *
 * A completely method-agnostic API is constrained by the fact that
 * different IPC transports have fundamentally different **addressing
 * models** and **connection sequences**:
 *
 *   - Anonymous pipes have no address.  They must be created by the
 *     parent before fork/spawn; the file descriptors are inherited by
 *     the child.  You can't "connect" to a pipe from an independent
 *     process that wasn't descended from the same parent.
 *
 *   - Unix domain sockets have a filesystem path as an address.  Either
 *     party can be first; coordination happens through the path name.
 *
 *   - TCP has an IP address and a port number.  The server must bind and
 *     listen before the client calls connect; the port must be discovered
 *     (e.g., printed to stdout or written to a file) or pre-agreed.
 *
 * Because of this, no single "connect(addr)" call can express all three
 * models uniformly.  What IS uniform for the **parent-spawns-child**
 * pattern that librtrender uses is:
 *
 *   1.  Parent calls bu_ipc_pair() to allocate two connected channel ends.
 *   2.  Parent retrieves child's end address via bu_ipc_addr().
 *   3.  Parent passes that address to the child (environment variable or
 *       argument) when spawning it.
 *   4.  Child calls bu_ipc_connect(addr) to attach to its end.
 *   5.  Both sides call bu_ipc_write() / bu_ipc_read() identically.
 *
 * The transport-specific knowledge (which mechanism was chosen, what the
 * address format means) stays entirely inside this header + ipc.cpp.
 *
 * ### Environment variable convention
 *
 * When bu_ipc_pair() succeeds, the parent can pass BU_IPC_ADDR_ENVVAR as
 * an environment variable to the child, whose value is bu_ipc_addr().
 * bu_ipc_connect_env() reads this variable automatically so child code
 * needs no address-format knowledge.
 *
 * ### Thread safety
 *
 * bu_ipc_write() and bu_ipc_read() are safe to call from different threads
 * on the same channel only when the underlying transport supports atomic
 * writes of the transferred size (pipe writes ≤ PIPE_BUF are atomic on
 * POSIX; larger writes are not).  For the frame-delimited render packets
 * used by librtrender all writes are well below PIPE_BUF (64 KiB on Linux),
 * so concurrent use is safe in practice.
 */

#ifndef BU_IPC_H
#define BU_IPC_H

#include "common.h"
#include "bu/defines.h"

#include <stddef.h>  /* size_t */

#ifdef _WIN32
#  include <basetsd.h>   /* SSIZE_T */
typedef SSIZE_T bu_ssize_t;
#else
#  include <sys/types.h> /* ssize_t */
typedef ssize_t bu_ssize_t;
#endif

__BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Environment variable the parent sets for the child to read          */
/* ------------------------------------------------------------------ */

/** Name of the env-var that bu_ipc_pair() uses to convey the child-side
 *  channel address to the spawned process. */
#define BU_IPC_ADDR_ENVVAR "BU_IPC_ADDR"


/* ------------------------------------------------------------------ */
/* Transport type                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Which underlying IPC transport was selected.
 *
 * Callers should not need to branch on this; it is exposed only for
 * diagnostic messages and for event-loop integration code that needs
 * to pass the raw file descriptor to a non-blocking I/O library.
 */
typedef enum {
    BU_IPC_PIPE   = 1, /**< @brief Anonymous pipe (fastest, parent-child only) */
    BU_IPC_SOCKET = 2, /**< @brief Unix domain socket (local processes) */
    BU_IPC_TCP    = 3  /**< @brief TCP loopback (universal fallback) */
} bu_ipc_type_t;


/* ------------------------------------------------------------------ */
/* Opaque handle                                                        */
/* ------------------------------------------------------------------ */

/** @brief Opaque IPC channel handle. */
typedef struct bu_ipc_chan bu_ipc_chan_t;


/* ------------------------------------------------------------------ */
/* Channel creation (parent side)                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Create a matched pair of IPC channel ends for parent-child use.
 *
 * The implementation probes transports in order: anonymous pipe →
 * Unix domain socket → TCP loopback, and uses the first that succeeds.
 * Both ends are returned as fully connected (or ready-to-use) handles.
 *
 * @param[out] parent_end  Caller keeps this end; the parent reads/writes here.
 * @param[out] child_end   This end is closed after spawning; its address is
 *                         passed to the child via bu_ipc_addr().
 *
 * @return 0 on success, -1 if no transport could be established.
 */
BU_EXPORT int bu_ipc_pair(bu_ipc_chan_t **parent_end, bu_ipc_chan_t **child_end);


/* ------------------------------------------------------------------ */
/* Address handling                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Return the transport address of a channel.
 *
 * The returned string encodes both the transport type and the
 * endpoint information needed for the child to connect:
 *
 *   "pipe:<fd>"          — file descriptor number (inheritable)
 *   "socket:<path>"      — path to Unix domain socket
 *   "tcp:<port>"         — TCP loopback port number
 *
 * The returned pointer is owned by @p chan and is valid until
 * bu_ipc_close() is called.
 *
 * @param[in] chan  A channel created by bu_ipc_pair().
 * @return  NUL-terminated address string, or NULL on error.
 */
BU_EXPORT const char *bu_ipc_addr(const bu_ipc_chan_t *chan);


/* ------------------------------------------------------------------ */
/* Channel connection (child side)                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief Connect to an IPC channel using an address string.
 *
 * This is the child-side counterpart to bu_ipc_pair().  The address
 * string comes from bu_ipc_addr() on the parent's child_end handle.
 *
 * @param[in] addr  Address string as produced by bu_ipc_addr().
 * @return  A new channel handle, or NULL on failure.
 */
BU_EXPORT bu_ipc_chan_t *bu_ipc_connect(const char *addr);

/**
 * @brief Connect using the address stored in BU_IPC_ADDR_ENVVAR.
 *
 * Convenience wrapper: reads the environment variable set by the parent
 * and calls bu_ipc_connect() for you.
 *
 * @return  A new channel handle, or NULL if the variable is unset or
 *          the connection fails.
 */
BU_EXPORT bu_ipc_chan_t *bu_ipc_connect_env(void);


/* ------------------------------------------------------------------ */
/* Data transfer                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Write exactly @p nbytes from @p buf into the channel.
 *
 * Blocks until all bytes have been written or an error occurs.
 *
 * @param[in] chan    Channel to write to.
 * @param[in] buf    Data to send.
 * @param[in] nbytes Number of bytes to write.
 *
 * @return @p nbytes on complete success;
 *         -1 on error (channel broken, closed, etc.).
 */
BU_EXPORT bu_ssize_t bu_ipc_write(bu_ipc_chan_t *chan,
				  const void   *buf,
				  size_t        nbytes);

/**
 * @brief Read exactly @p nbytes from the channel into @p buf.
 *
 * Blocks until all bytes have been received or the channel closes.
 *
 * @param[in]  chan    Channel to read from.
 * @param[out] buf    Buffer to receive data.
 * @param[in]  nbytes Number of bytes to read.
 *
 * @return @p nbytes on complete success;
 *         0 if the channel closed before any bytes arrived (EOF);
 *         -1 on error.
 */
BU_EXPORT bu_ssize_t bu_ipc_read(bu_ipc_chan_t *chan,
				 void         *buf,
				 size_t        nbytes);


/* ------------------------------------------------------------------ */
/* Integration with event loops                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Return the underlying file descriptor for event-loop use.
 *
 * Callers may pass this descriptor to libuv's uv_pipe_open(),
 * Qt's QSocketNotifier, POSIX select/poll, etc.  The descriptor is
 * valid until bu_ipc_close() is called; do **not** close it directly.
 *
 * @return  File descriptor (>= 0) on success; -1 if this transport
 *          does not expose a file descriptor (e.g., on Windows when
 *          a HANDLE-only mechanism is used without an fd layer).
 */
BU_EXPORT int bu_ipc_fileno(const bu_ipc_chan_t *chan);

/**
 * @brief Return which transport was selected for this channel.
 */
BU_EXPORT bu_ipc_type_t bu_ipc_type(const bu_ipc_chan_t *chan);


/* ------------------------------------------------------------------ */
/* Cleanup                                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Close a channel and free all associated resources.
 *
 * After this call, @p chan is invalid and must not be used.
 * For the socket and TCP transports the implementation removes any
 * temporary filesystem paths or releases the port.
 */
BU_EXPORT void bu_ipc_close(bu_ipc_chan_t *chan);

/** @} */

__END_DECLS

#endif /* BU_IPC_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
