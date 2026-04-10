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
 * (PIMPL) handle; callers never need to know which transport was selected.
 *
 * ### Motivation
 *
 * BRL-CAD historically used TCP sockets (via libpkg / fbserv) for
 * incremental display of raytrace results.  While TCP works everywhere,
 * it requires port allocation, may be blocked by firewalls, and has
 * higher overhead than local IPC.  bu_ipc replaces that with a
 * "try in order" approach:
 *
 *   anonymous pipe    →  fastest, zero kernel overhead beyond the pipe;
 *                        backed by kernel memory with no filesystem artifact
 *   socketpair        →  POSIX bidirectional socket pair; slightly more
 *                        overhead than a pipe but no path coordination needed
 *   TCP loopback      →  universal last resort; works everywhere but needs
 *                        a free port and is slower than local transports
 *
 * The caller never observes which transport was chosen; the opaque
 * bu_ipc_chan_t handle presents the same read/write surface in all cases.
 *
 *
 * ### Typical usage — two equivalent patterns
 *
 * **Pattern A: CLI argument (-I flag)**
 * Pass the channel address as a command-line argument to the child process.
 * This is the safest choice when the spawner is multi-threaded and runs
 * multiple concurrent spawns, because each bu_ipc_pair() call produces an
 * independent address string with no shared state:
 *
 * @code
 *   bu_ipc_chan_t *p, *c;
 *   bu_ipc_pair(&p, &c);
 *
 *   // Pass child-end address as a CLI arg; spawn child
 *   const char *argv[] = { "child", "-I", bu_ipc_addr(c), NULL };
 *   spawn_child(argv);
 *   bu_ipc_close(c);   // parent's copy of the child end
 *
 *   // Child calls: bu_ipc_connect(argv[iarg]) to get its channel handle
 * @endcode
 *
 * **Pattern B: environment variable**
 * When the spawner is single-threaded (so there can be no concurrent
 * setenv() calls from different threads), set an environment variable in
 * the parent before fork(), then clear it once the fork() has returned.
 * fork() gives the child its own independent copy of the environment, so
 * clearing the variable in the parent afterwards is safe:
 *
 * @code
 *   bu_ipc_chan_t *pe, *ce;
 *   bu_ipc_pair(&pe, &ce);
 *   bu_ipc_move_high_fd(ce, 64);  // survive close(3..19) sweep in spawner
 *
 *   bu_setenv("MY_IPC_ADDR", bu_ipc_addr(ce), 1);
 *   bu_process_create(&p, argv, BU_PROCESS_DEFAULT);  // fork happens here
 *   bu_setenv("MY_IPC_ADDR", "", 1);   // safe: child has its own env copy
 *   bu_ipc_close(ce);
 *
 *   // Child side: bu_ipc_connect_from_env("MY_IPC_ADDR")
 * @endcode
 *
 * Both patterns are fully transport-agnostic.  Prefer Pattern A in
 * multi-threaded spawners; Pattern B is simpler when the spawner is known
 * to be single-threaded (e.g. a dedicated main/event loop).
 *
 *
 * ### Transport safety for concurrent spawns
 *
 * Each call to bu_ipc_pair() creates two independent channel handles with
 * their own internal state.  The address string lives inside the struct.
 * Multiple simultaneous renders therefore each hold separate
 * bu_ipc_chan_t handles with separate address strings.  There is no
 * shared mutable state between them.
 *
 *
 * ### Optional transport preference (BU_IPC_PREFER)
 *
 * For testing, benchmarking, or environments where a specific transport is
 * known to be preferable, set BU_IPC_PREFER_ENVVAR before calling
 * bu_ipc_pair():
 *
 * @code
 *   setenv("BU_IPC_PREFER", "tcp", 0);   // or "pipe" / "socket"
 * @endcode
 *
 * When set, bu_ipc_pair() tries that transport first (but still falls back
 * to others if it fails).  When unset, the default probe order is used.
 * Applications can also call bu_ipc_pair_prefer() to specify the preference
 * programmatically without relying on an environment variable.
 *
 *
 * ### Why full transport-agnosticism is bounded
 *
 * A truly method-agnostic API is constrained by fundamentally different
 * addressing models across transports:
 *
 *   anonymous pipe   — no address; fd pair created before fork/spawn, inherited
 *   socketpair       — no address; same inheritance model as anonymous pipe
 *   TCP              — IP + port; server must bind before client connects
 *
 * These differences mean the "connect" operation is meaningful for TCP
 * but not for pipes, and the "listen" concept applies to TCP only.
 * A single bu_ipc_connect(addr) function works for all three because:
 *   - For pipe and socket transports the address is just the fd number
 *     (the fd was inherited, connect() is trivially "we already have it")
 *   - For TCP the address is a port number to connect() to
 *
 * This works cleanly for the parent-spawns-child pattern.  What would NOT
 * work is connecting two completely independent (non-parent-child) processes
 * with the pipe or socketpair transports, since those fds cannot be
 * transferred without a pre-existing connection.
 *
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
/* Transport preference hint                                            */
/* ------------------------------------------------------------------ */

/**
 * Optional parent-process preference variable.
 *
 * If set to "pipe", "socket", or "tcp" in the parent's environment before
 * calling bu_ipc_pair(), that transport is tried first (with fallback to
 * others on failure).  Unset means use the default probe order.
 *
 * Safe to read from the global env since it is only a preference hint, not a
 * coordination mechanism; all concurrent bu_ipc_pair() calls reading the
 * same value is correct behaviour.
 */
#define BU_IPC_PREFER_ENVVAR "BU_IPC_PREFER"


/* ------------------------------------------------------------------ */
/* Transport type                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Which underlying IPC transport was selected.
 *
 * Callers should not need to branch on this; it is exposed only for
 * diagnostic messages and event-loop integration code that needs to pass
 * the raw file descriptor to a non-blocking I/O library (e.g.
 * uv_pipe_open(), QSocketNotifier, poll(2)).
 */
typedef enum {
    BU_IPC_PIPE   = 1, /**< @brief Anonymous pipe (fastest, parent-child only) */
    BU_IPC_SOCKET = 2, /**< @brief POSIX socketpair() (local processes)       */
    BU_IPC_TCP    = 3  /**< @brief TCP loopback 127.0.0.1 (universal fallback) */
} bu_ipc_type_t;


/* ------------------------------------------------------------------ */
/* Opaque PIMPL handle                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Opaque IPC channel handle (PIMPL).
 *
 * All transport-specific state — file descriptors, port numbers, paths,
 * address strings — is hidden inside this struct.  Callers hold handles
 * and call bu_ipc_* functions; the implementation details are never
 * exposed.
 */
typedef struct bu_ipc_chan bu_ipc_chan_t;


/* ------------------------------------------------------------------ */
/* Channel creation (parent side)                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Create a matched pair of IPC channel ends for parent-child use.
 *
 * Reads BU_IPC_PREFER_ENVVAR (if set) to pick a preferred transport, then
 * probes in order until one succeeds: pipe → socketpair → TCP loopback.
 *
 * Both ends are returned as connected handles.  The parent keeps
 * @p parent_end for its own I/O.  The child-end address is retrieved with
 * bu_ipc_addr() and passed to the child as a command-line argument (e.g.
 * @c -I @c addr); the child calls bu_ipc_connect(addr) to open its end.
 *
 * @param[out] parent_end  Channel for the parent process.
 * @param[out] child_end   Channel whose address is given to the child.
 *
 * @return 0 on success, -1 if no transport could be established.
 */
BU_EXPORT int bu_ipc_pair(bu_ipc_chan_t **parent_end,
  bu_ipc_chan_t **child_end);

/**
 * @brief Like bu_ipc_pair() but with an explicit transport preference.
 *
 * Tries @p preferred first; falls back to others if it fails.
 * Pass BU_IPC_PIPE / BU_IPC_SOCKET / BU_IPC_TCP for explicit control.
 * Prefer bu_ipc_pair() for normal use; this variant is for testing and
 * environments where a particular transport must be exercised.
 *
 * @return 0 on success, -1 on failure.
 */
BU_EXPORT int bu_ipc_pair_prefer(bu_ipc_chan_t **parent_end,
 bu_ipc_chan_t **child_end,
 bu_ipc_type_t   preferred);


/* ------------------------------------------------------------------ */
/* Address handling (hidden inside PIMPL; exposed only as strings)      */
/* ------------------------------------------------------------------ */

/**
 * @brief Return the raw transport address of a channel.
 *
 * Format:
 *   "pipe:<rfd>,<wfd>"   — fd numbers (child must inherit them)
 *   "socket:<fd>"        — bidirectional socket fd
 *   "tcp:<port>"         — TCP loopback port
 *
 * The returned pointer is owned by @p chan (valid until bu_ipc_close()).
 * Pass this string to the child process as a command-line argument (e.g.
 * @c -I @c addr) and use bu_ipc_connect() on the child side to open the
 * corresponding channel end.
 */
BU_EXPORT const char *bu_ipc_addr(const bu_ipc_chan_t *chan);


/* ------------------------------------------------------------------ */
/* Channel connection (child side)                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief Connect to an IPC channel using an address string.
 *
 * Child-side counterpart to bu_ipc_pair().  The address string is the
 * raw value returned by bu_ipc_addr() on the parent side (e.g.
 * "pipe:4,7", "socket:5", "tcp:54321").  It is typically passed to the
 * child as a command-line argument so that concurrent spawns each receive
 * their own unique address with no shared state.
 *
 * @return  New channel handle, or NULL on failure.
 */
BU_EXPORT bu_ipc_chan_t *bu_ipc_connect(const char *addr);

/**
 * @brief Connect to an IPC channel whose address is stored in an
 * environment variable.
 *
 * Reads the environment variable named @p envvar_name and calls
 * bu_ipc_connect() with the resulting string.  Returns NULL if the
 * variable is absent, empty, or bu_ipc_connect() fails.
 *
 * This is the child-side helper for the env-var spawn pattern (Pattern B
 * in the header overview):
 *
 * @code
 *   // Child main():
 *   bu_ipc_chan_t *ch = bu_ipc_connect_from_env("MY_IPC_ADDR");
 *   if (ch) {
 *       // IPC mode — wrap into the application's I/O layer
 *   } else {
 *       // fall back to TCP or another mechanism
 *   }
 * @endcode
 *
 * @param[in] envvar_name  Name of the environment variable to read.
 * @return  New channel handle, or NULL if the variable is absent/empty
 *          or connection fails.
 */
BU_EXPORT bu_ipc_chan_t *bu_ipc_connect_from_env(const char *envvar_name);


/* ------------------------------------------------------------------ */
/* Data transfer (blocking)                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Write exactly @p nbytes from @p buf into the channel.
 *
 * Blocks until all bytes have been written.
 *
 * @return @p nbytes on success; -1 on error.
 */
BU_EXPORT bu_ssize_t bu_ipc_write(bu_ipc_chan_t *chan,
  const void   *buf,
  size_t        nbytes);

/**
 * @brief Read exactly @p nbytes from the channel into @p buf.
 *
 * Blocks until all bytes are received or the channel closes.
 *
 * @return @p nbytes on complete success; 0 on EOF; -1 on error.
 */
BU_EXPORT bu_ssize_t bu_ipc_read(bu_ipc_chan_t *chan,
 void         *buf,
 size_t        nbytes);


/* ------------------------------------------------------------------ */
/* Event-loop integration                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Return the read file descriptor for event-loop registration.
 *
 * May be passed to uv_pipe_open(), QSocketNotifier, poll(2), etc.
 * Valid until bu_ipc_close(); do NOT close it directly.
 *
 * @return  fd >= 0 on success; -1 if not applicable.
 */
BU_EXPORT int bu_ipc_fileno(const bu_ipc_chan_t *chan);

/**
 * @brief Return the write file descriptor.
 *
 * For socketpair and TCP transports this is the same as bu_ipc_fileno().
 * For anonymous pipe transport the read and write fds differ; this
 * function returns the write end.
 *
 * @return  fd >= 0 on success; -1 if not applicable.
 */
BU_EXPORT int bu_ipc_fileno_write(const bu_ipc_chan_t *chan);

/**
 * @brief Return which transport was selected.
 */
BU_EXPORT bu_ipc_type_t bu_ipc_type(const bu_ipc_chan_t *chan);


/* ------------------------------------------------------------------ */
/* Cleanup                                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Close a channel and free all associated resources.
 *
 * After this call @p chan is invalid.  For socket/TCP transports the
 * implementation releases the port; for named-path transports it removes
 * any temporary filesystem entries.
 */
BU_EXPORT void bu_ipc_close(bu_ipc_chan_t *chan);

/**
 * @brief Release the channel struct without closing the underlying fds.
 *
 * Use when ownership of the underlying file descriptor(s) has been
 * transferred to another entity (e.g. libpkg via pkg_open_fds()).
 * The fds are left open; only the channel wrapper struct is freed.
 *
 * After this call @p chan is invalid.
 */
BU_EXPORT void bu_ipc_detach(bu_ipc_chan_t *chan);

/**
 * @brief Move the channel's fd(s) to fd numbers ≥ @p min_fd.
 *
 * Some spawn helpers (e.g. bu_process_create()) perform a sweep that
 * closes all file descriptors below a certain threshold before exec().
 * Call this on the child-end channel after bu_ipc_pair() and before
 * spawning the child to ensure the fd survives the sweep.
 *
 * Example:
 * @code
 *   bu_ipc_pair(&pe, &ce);
 *   bu_ipc_move_high_fd(ce, 64);   // move out of the swept range
 *   const char *addr = bu_ipc_addr(ce);
 *   // now pass addr as -I argument to child
 * @endcode
 *
 * @param[in] chan    Channel whose fd(s) should be relocated.
 * @param[in] min_fd Minimum acceptable fd number (must be ≥ 3).
 * @return 0 on success, -1 on error.
 */
BU_EXPORT int bu_ipc_move_high_fd(bu_ipc_chan_t *chan, int min_fd);


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
