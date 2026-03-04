/*                          I P C . C P P
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
/** @file libbu/ipc.cpp
 *
 * Transport-agnostic byte-stream IPC channel implementation.
 *
 * ### Transport probe order
 *
 * 1. **Anonymous pipe** (bu_ipc_pair only)
 *    Created with pipe(2) / pipe2(2) on POSIX, CreatePipe() on Windows.
 *    The child's file descriptor number is carried in the address string
 *    "pipe:<fd>".  Because anonymous pipe descriptors must be inherited,
 *    they cannot be used once a process has already been launched without
 *    the right descriptors.
 *    Rationale for preference: zero kernel overhead beyond the pipe itself;
 *    no filesystem paths; no port allocation; backed by kernel memory so
 *    it works in chroot, containers, and sandboxes with no network at all.
 *
 * 2. **Unix domain socket** (socketpair(2) on POSIX only)
 *    Like anonymous pipes but bidirectional by default and supports
 *    ancillary data (fd passing).  On Windows this requires Windows 10
 *    build 17063+ and the AF_UNIX socket family.
 *    Address format: "socket:<fd>" (the child receives the connected
 *    socket descriptor directly, same inheritance model as pipes).
 *    Rationale for position: marginally more overhead than a pipe, but
 *    bidirectional without needing two pipe(2) calls.
 *
 * 3. **TCP loopback**
 *    A listener socket is created on 127.0.0.1 with a random ephemeral
 *    port.  The port is recorded in the address string "tcp:<port>".
 *    The child calls connect(AF_INET, 127.0.0.1, port).
 *    Rationale for last place: works on all platforms but requires a free
 *    port, is blocked by strict firewalls even on loopback, and has higher
 *    latency than local transports.
 *
 * ### Why libuv is NOT used here
 *
 * libbu is a foundational library that many BRL-CAD components depend on.
 * Adding a mandatory libuv dependency to libbu would impose that dependency
 * everywhere — including lightweight command-line tools and libraries that
 * have no event loop.
 *
 * Instead, bu_ipc uses only POSIX / Win32 primitives.  The file descriptor
 * returned by bu_ipc_fileno() can be passed directly to libuv via
 * uv_pipe_open(), to Qt via QSocketNotifier, or used directly in a poll(2)
 * loop.  The event-loop integration is thus the caller's responsibility.
 *
 * ### Why we cannot achieve *complete* transport transparency
 *
 * The three transports differ in their **addressing and connection
 * lifecycle**:
 *
 *   anonymous pipe  — must be created before fork/spawn; child inherits fd
 *   Unix socket     — path (or fd) shared before spawn; connect() is instant
 *   TCP             — port must be discovered; server binds before client
 *
 * These differences mean bu_ipc_pair() must know the child hasn't been
 * spawned yet; it creates the pair *before* spawn.  An API that tried to
 * abstract away this lifecycle completely would need to spawn the process
 * internally (hiding the process handle from the caller), which is
 * unacceptable for a general-purpose utility library.
 *
 * The compromise: bu_ipc_pair() returns *both ends* in one call.  The
 * caller passes bu_ipc_addr(child_end) to the child process, and the child
 * calls bu_ipc_connect(addr).  The addr string format is opaque to callers.
 */

#include "common.h"

/* ── POSIX includes ─────────────────────────────────────────────────── */
#ifndef _WIN32
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/types.h>
#  include <errno.h>
#  ifdef HAVE_SYS_UN_H
#    include <sys/un.h>
#  endif
#endif

/* ── Windows includes ───────────────────────────────────────────────── */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <io.h>        /* _open_osfhandle */
#  pragma comment(lib, "ws2_32.lib")
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cerrno>

#include "bu/defines.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/ipc.h"


/* ================================================================== */
/* Internal channel structure                                           */
/* ================================================================== */

struct bu_ipc_chan {
    bu_ipc_type_t type;

    /* The file descriptor (read end for parent/pipe, write end separately). */
    int           fd;        /* primary fd (read OR bidirectional) */
    int           fd_write;  /* write-end for anonymous pipe (POSIX pair) */

    /* For TCP server: listener fd that has not yet been accepted */
    int           listen_fd;

    /* Address string (heap-allocated) */
    std::string   addr;

    /* For socket/TCP cleanup: path or port */
    std::string   sock_path;   /* Unix socket path to unlink */

    /* Windows HANDLE for pipe ends (when fd wrapping not available) */
#ifdef _WIN32
    HANDLE        hRead;
    HANDLE        hWrite;
#endif
};


/* ================================================================== */
/* Helpers                                                              */
/* ================================================================== */

static bu_ipc_chan_t *
make_chan(void)
{
    bu_ipc_chan_t *c = new bu_ipc_chan_t();
    c->type       = BU_IPC_PIPE;
    c->fd         = -1;
    c->fd_write   = -1;
    c->listen_fd  = -1;
#ifdef _WIN32
    c->hRead  = INVALID_HANDLE_VALUE;
    c->hWrite = INVALID_HANDLE_VALUE;
#endif
    return c;
}


/* ================================================================== */
/* Transport 1: anonymous pipe                                          */
/* ================================================================== */

#ifndef _WIN32
static int
try_pipe(bu_ipc_chan_t **parent_end, bu_ipc_chan_t **child_end)
{
    /* parent→child */
    int pc[2];  /* pc[0]=read(child), pc[1]=write(parent) */
    /* child→parent */
    int cp[2];  /* cp[0]=read(parent), cp[1]=write(child) */

    if (pipe(pc) < 0 || pipe(cp) < 0)
	return -1;

    bu_ipc_chan_t *pe = make_chan();
    pe->type     = BU_IPC_PIPE;
    pe->fd       = cp[0];      /* parent reads from cp */
    pe->fd_write = pc[1];      /* parent writes to pc */

    bu_ipc_chan_t *ce = make_chan();
    ce->type     = BU_IPC_PIPE;
    ce->fd       = pc[0];      /* child reads from pc */
    ce->fd_write = cp[1];      /* child writes to cp */

    /* Address encodes both child fds: "pipe:read_fd,write_fd" */
    char buf[64];
    snprintf(buf, sizeof(buf), "pipe:%d,%d", pc[0], cp[1]);
    ce->addr = buf;

    /* Parent end address (informational only) */
    snprintf(buf, sizeof(buf), "pipe_parent:%d,%d", cp[0], pc[1]);
    pe->addr = buf;

    *parent_end = pe;
    *child_end  = ce;
    return 0;
}
#endif /* !_WIN32 */

#ifdef _WIN32
static int
try_pipe_win32(bu_ipc_chan_t **parent_end, bu_ipc_chan_t **child_end)
{
    HANDLE hReadPC, hWritePC;  /* parent→child */
    HANDLE hReadCP, hWriteCP;  /* child→parent */

    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE; /* child must inherit */

    if (!CreatePipe(&hReadPC, &hWritePC, &sa, 0) ||
	!CreatePipe(&hReadCP, &hWriteCP, &sa, 0))
	return -1;

    /* Prevent parent's write end from being inherited by grandchildren */
    SetHandleInformation(hWritePC, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hReadCP,  HANDLE_FLAG_INHERIT, 0);

    /* Wrap HANDLEs as CRT fds for the read/write implementation */
    int pc_r = _open_osfhandle((intptr_t)hReadPC,  _O_RDONLY | _O_BINARY);
    int pc_w = _open_osfhandle((intptr_t)hWritePC, _O_WRONLY | _O_BINARY);
    int cp_r = _open_osfhandle((intptr_t)hReadCP,  _O_RDONLY | _O_BINARY);
    int cp_w = _open_osfhandle((intptr_t)hWriteCP, _O_WRONLY | _O_BINARY);

    bu_ipc_chan_t *pe = make_chan();
    pe->type     = BU_IPC_PIPE;
    pe->fd       = cp_r;
    pe->fd_write = pc_w;
    pe->hRead    = hReadCP;
    pe->hWrite   = hWritePC;

    bu_ipc_chan_t *ce = make_chan();
    ce->type     = BU_IPC_PIPE;
    ce->fd       = pc_r;
    ce->fd_write = cp_w;
    ce->hRead    = hReadPC;
    ce->hWrite   = hWriteCP;

    /* On Windows, child receives the HANDLE values (as integers).
     * The spawn code is responsible for not closing them before exec. */
    char buf[128];
    snprintf(buf, sizeof(buf), "pipe:%d,%d", pc_r, cp_w);
    ce->addr = buf;
    snprintf(buf, sizeof(buf), "pipe_parent:%d,%d", cp_r, pc_w);
    pe->addr = buf;

    *parent_end = pe;
    *child_end  = ce;
    return 0;
}
#endif /* _WIN32 */


/* ================================================================== */
/* Transport 2: socketpair (POSIX only)                                 */
/* ================================================================== */

#if defined(HAVE_SYS_UN_H) && !defined(_WIN32)
static int
try_socketpair(bu_ipc_chan_t **parent_end, bu_ipc_chan_t **child_end)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
	return -1;

    bu_ipc_chan_t *pe = make_chan();
    pe->type     = BU_IPC_SOCKET;
    pe->fd       = sv[0];
    pe->fd_write = sv[0];  /* bidirectional */

    bu_ipc_chan_t *ce = make_chan();
    ce->type     = BU_IPC_SOCKET;
    ce->fd       = sv[1];
    ce->fd_write = sv[1];

    char buf[64];
    snprintf(buf, sizeof(buf), "socket:%d", sv[1]);
    ce->addr = buf;
    snprintf(buf, sizeof(buf), "socket_parent:%d", sv[0]);
    pe->addr = buf;

    *parent_end = pe;
    *child_end  = ce;
    return 0;
}
#endif


/* ================================================================== */
/* Transport 3: TCP loopback                                            */
/* ================================================================== */

static int
try_tcp(bu_ipc_chan_t **parent_end, bu_ipc_chan_t **child_end)
{
    /* Create a listening socket on an ephemeral port */
    int lsock;

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    lsock = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (lsock == INVALID_SOCKET)
	return -1;
#else
    lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0)
	return -1;
#endif

    {
	int opt = 1;
	setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR,
		   (const char *)&opt, sizeof(opt));
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port        = 0;  /* OS picks an ephemeral port */

    if (bind(lsock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
#ifdef _WIN32
	closesocket(lsock);
#else
	close(lsock);
#endif
	return -1;
    }

    if (listen(lsock, 1) < 0) {
#ifdef _WIN32
	closesocket(lsock);
#else
	close(lsock);
#endif
	return -1;
    }

    /* Retrieve the actual port */
    struct sockaddr_in bound;
    socklen_t blen = sizeof(bound);
    getsockname(lsock, (struct sockaddr *)&bound, &blen);
    int port = ntohs(bound.sin_port);

    char buf[64];
    snprintf(buf, sizeof(buf), "tcp:%d", port);

    /* child end: just carries the address, no fd yet */
    bu_ipc_chan_t *ce = make_chan();
    ce->type      = BU_IPC_TCP;
    ce->listen_fd = -1;
    ce->addr      = buf;

    /* parent end: carries the listening socket; accept() happens on first
     * read/write call */
    bu_ipc_chan_t *pe = make_chan();
    pe->type      = BU_IPC_TCP;
    pe->listen_fd = lsock;
    pe->addr      = std::string("tcp_server:") + std::to_string(port);

    *parent_end = pe;
    *child_end  = ce;
    return 0;
}


/* ================================================================== */
/* TCP lazy-accept helper                                               */
/* ================================================================== */

/* Called the first time the parent tries to read/write via TCP.
 * Blocks until the child connects. */
static int
tcp_accept(bu_ipc_chan_t *chan)
{
    if (chan->fd >= 0)
	return 0;   /* already accepted */
    if (chan->listen_fd < 0)
	return -1;

    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
#ifdef _WIN32
    int csock = (int)accept((SOCKET)chan->listen_fd,
			    (struct sockaddr *)&peer, &plen);
    if (csock == INVALID_SOCKET)
	return -1;
    closesocket((SOCKET)chan->listen_fd);
#else
    int csock = accept(chan->listen_fd, (struct sockaddr *)&peer, &plen);
    if (csock < 0)
	return -1;
    close(chan->listen_fd);
#endif
    chan->listen_fd  = -1;
    chan->fd         = csock;
    chan->fd_write   = csock;
    return 0;
}


/* ================================================================== */
/* bu_ipc_pair                                                          */
/* ================================================================== */

int
bu_ipc_pair(bu_ipc_chan_t **parent_end, bu_ipc_chan_t **child_end)
{
    if (!parent_end || !child_end)
	return -1;

    /* Probe 1: anonymous pipe */
#ifdef _WIN32
    if (try_pipe_win32(parent_end, child_end) == 0)
	return 0;
#else
    if (try_pipe(parent_end, child_end) == 0)
	return 0;
    bu_log("bu_ipc_pair: pipe() failed: %s — trying socketpair\n",
	   strerror(errno));
#endif

    /* Probe 2: socketpair (POSIX only) */
#if defined(HAVE_SYS_UN_H) && !defined(_WIN32)
    if (try_socketpair(parent_end, child_end) == 0)
	return 0;
    bu_log("bu_ipc_pair: socketpair() failed: %s — trying TCP loopback\n",
	   strerror(errno));
#endif

    /* Probe 3: TCP loopback */
    if (try_tcp(parent_end, child_end) == 0)
	return 0;

    bu_log("bu_ipc_pair: all IPC transports failed\n");
    return -1;
}


/* ================================================================== */
/* bu_ipc_addr                                                          */
/* ================================================================== */

const char *
bu_ipc_addr(const bu_ipc_chan_t *chan)
{
    if (!chan)
	return NULL;
    return chan->addr.c_str();
}


/* ================================================================== */
/* bu_ipc_connect                                                       */
/* ================================================================== */

bu_ipc_chan_t *
bu_ipc_connect(const char *addr)
{
    if (!addr)
	return NULL;

    bu_ipc_chan_t *c = make_chan();
    c->addr = addr;

    /* ── pipe:<read_fd>,<write_fd> ────────────────────────────────── */
    if (strncmp(addr, "pipe:", 5) == 0) {
	int rfd = -1, wfd = -1;
	if (sscanf(addr + 5, "%d,%d", &rfd, &wfd) == 2) {
	    c->type     = BU_IPC_PIPE;
	    c->fd       = rfd;
	    c->fd_write = wfd;
	    return c;
	}
    }

    /* ── socket:<fd> ──────────────────────────────────────────────── */
    if (strncmp(addr, "socket:", 7) == 0) {
	int sfd = -1;
	if (sscanf(addr + 7, "%d", &sfd) == 1) {
	    c->type     = BU_IPC_SOCKET;
	    c->fd       = sfd;
	    c->fd_write = sfd;
	    return c;
	}
    }

    /* ── tcp:<port> ───────────────────────────────────────────────── */
    if (strncmp(addr, "tcp:", 4) == 0) {
	int port = 0;
	if (sscanf(addr + 4, "%d", &port) == 1) {
#ifdef _WIN32
	    WSADATA wsaData;
	    WSAStartup(MAKEWORD(2, 2), &wsaData);
	    int csock = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	    if (csock == INVALID_SOCKET) { delete c; return NULL; }
#else
	    int csock = socket(AF_INET, SOCK_STREAM, 0);
	    if (csock < 0) { delete c; return NULL; }
#endif
	    struct sockaddr_in sa;
	    memset(&sa, 0, sizeof(sa));
	    sa.sin_family      = AF_INET;
	    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	    sa.sin_port        = htons((uint16_t)port);
	    if (connect(csock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
#ifdef _WIN32
		closesocket((SOCKET)csock);
#else
		close(csock);
#endif
		delete c;
		return NULL;
	    }
	    c->type     = BU_IPC_TCP;
	    c->fd       = csock;
	    c->fd_write = csock;
	    return c;
	}
    }

    bu_log("bu_ipc_connect: unrecognized address '%s'\n", addr);
    delete c;
    return NULL;
}


bu_ipc_chan_t *
bu_ipc_connect_env(void)
{
    const char *addr = getenv(BU_IPC_ADDR_ENVVAR);
    if (!addr) {
	bu_log("bu_ipc_connect_env: %s not set\n", BU_IPC_ADDR_ENVVAR);
	return NULL;
    }
    return bu_ipc_connect(addr);
}


/* ================================================================== */
/* bu_ipc_write                                                         */
/* ================================================================== */

bu_ssize_t
bu_ipc_write(bu_ipc_chan_t *chan, const void *buf, size_t nbytes)
{
    if (!chan || !buf)
	return -1;

    if (chan->type == BU_IPC_TCP && tcp_accept(chan) < 0)
	return -1;

    int wfd = (chan->fd_write >= 0) ? chan->fd_write : chan->fd;
    if (wfd < 0)
	return -1;

    const char *p   = (const char *)buf;
    size_t      rem = nbytes;
    while (rem > 0) {
#ifdef _WIN32
	int n = _write(wfd, p, (unsigned int)(rem > 65536 ? 65536 : rem));
#else
	ssize_t n = write(wfd, p, rem);
#endif
	if (n <= 0)
	    return -1;
	p   += n;
	rem -= (size_t)n;
    }
    return (bu_ssize_t)nbytes;
}


/* ================================================================== */
/* bu_ipc_read                                                          */
/* ================================================================== */

bu_ssize_t
bu_ipc_read(bu_ipc_chan_t *chan, void *buf, size_t nbytes)
{
    if (!chan || !buf)
	return -1;

    if (chan->type == BU_IPC_TCP && tcp_accept(chan) < 0)
	return -1;

    int rfd = chan->fd;
    if (rfd < 0)
	return -1;

    char   *p   = (char *)buf;
    size_t  rem = nbytes;
    while (rem > 0) {
#ifdef _WIN32
	int n = _read(rfd, p, (unsigned int)(rem > 65536 ? 65536 : rem));
#else
	ssize_t n = read(rfd, p, rem);
#endif
	if (n == 0)
	    return 0;   /* EOF */
	if (n < 0)
	    return -1;
	p   += n;
	rem -= (size_t)n;
    }
    return (bu_ssize_t)nbytes;
}


/* ================================================================== */
/* bu_ipc_fileno                                                        */
/* ================================================================== */

int
bu_ipc_fileno(const bu_ipc_chan_t *chan)
{
    if (!chan)
	return -1;
    return chan->fd;
}


bu_ipc_type_t
bu_ipc_type(const bu_ipc_chan_t *chan)
{
    if (!chan)
	return BU_IPC_PIPE;
    return chan->type;
}


/* ================================================================== */
/* bu_ipc_close                                                         */
/* ================================================================== */

void
bu_ipc_close(bu_ipc_chan_t *chan)
{
    if (!chan)
	return;

#ifdef _WIN32
    if (chan->fd       >= 0) _close(chan->fd);
    if (chan->fd_write >= 0 && chan->fd_write != chan->fd)
	_close(chan->fd_write);
    if (chan->listen_fd >= 0) closesocket((SOCKET)chan->listen_fd);
#else
    if (chan->fd       >= 0) close(chan->fd);
    if (chan->fd_write >= 0 && chan->fd_write != chan->fd)
	close(chan->fd_write);
    if (chan->listen_fd >= 0) close(chan->listen_fd);
#endif

    if (!chan->sock_path.empty())
	(void)remove(chan->sock_path.c_str());

    delete chan;
}


/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
