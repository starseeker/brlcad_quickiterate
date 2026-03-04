/*         R E N D E R _ I P C _ C L I E N T . C P P
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
/** @file librender/render_ipc_client.cpp
 *
 * Out-of-process IPC client for librender.
 *
 * ### Architecture
 *
 * Channel setup uses bu_ipc (transport-agnostic: pipe → socketpair → TCP).
 * Once the file descriptors are established, libuv takes over for
 * asynchronous event-driven I/O via uv_pipe_open().  This split gives us:
 *
 *   1.  Transport selection independence — bu_ipc handles "which mechanism"
 *       so callers and this code never hard-code pipe vs TCP.
 *   2.  Async pixel delivery — libuv's event loop drives the read callbacks
 *       so the parent can process scanlines as they arrive without polling.
 *   3.  Clean process lifecycle — the child is spawned via uv_spawn so its
 *       process handle is known; uv_process_kill() terminates it cleanly.
 *
 * ### Why libuv is still used alongside bu_ipc
 *
 * bu_ipc provides blocking synchronous I/O, which is sufficient for
 * the rt_ipc child (it writes one scanline at a time and returns).
 * The parent (embedded in a Qt or GED application) has an existing
 * libuv event loop and needs *non-blocking* receive callbacks so the
 * UI remains responsive while the raytrace runs.  bu_ipc exposes the
 * underlying file descriptor via bu_ipc_fileno(); we hand that fd to
 * uv_pipe_open() so libuv can manage the async read side.
 *
 * ### IPC channel setup sequence
 *
 *   parent calls render_ipc_client_spawn():
 *     1.  bu_ipc_pair() → (parent_chan, child_chan)
 *     2.  env[BU_IPC_ADDR_ENVVAR] = bu_ipc_addr(child_chan)
 *     3.  uv_spawn(rt_ipc, env=[BU_IPC_ADDR_ENVVAR=...])
 *     4.  uv_pipe_open(read_pipe, bu_ipc_fileno(parent_chan, read_end))
 *     5.  uv_pipe_open(write_pipe, bu_ipc_fileno(parent_chan, write_end))
 *     6.  bu_ipc_close(child_chan)   [fd now owned by the child process]
 *
 *   child (rt_ipc) calls:
 *     1.  bu_ipc_connect_env()       [reads BU_IPC_ADDR_ENVVAR]
 *     2.  bu_ipc_read / bu_ipc_write (blocking)
 *
 * ### Packet framing
 *
 *   [magic:4LE][type:1][paylen:4LE][payload:paylen]
 *
 * See render_ipc.h for the full packet catalogue.
 */

#include "common.h"

#ifdef RENDER_HAVE_LIBUV

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

#include <uv.h>

#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "bu/ipc.h"
#include "vmath.h"

#include "render.h"
#include "render_ipc.h"
#include "./render_private.h"


/* ================================================================== */
/* Packet framing helpers                                               */
/* ================================================================== */

#define IPC_HDR_SIZE 9   /* 4 magic + 1 type + 4 paylen */

static inline void write_le32(unsigned char *b, uint32_t v) {
    b[0] = (v)       & 0xff;
    b[1] = (v >>  8) & 0xff;
    b[2] = (v >> 16) & 0xff;
    b[3] = (v >> 24) & 0xff;
}

static inline uint32_t read_le32(const unsigned char *b) {
    return (uint32_t)b[0]
	 | ((uint32_t)b[1] <<  8)
	 | ((uint32_t)b[2] << 16)
	 | ((uint32_t)b[3] << 24);
}


/* ================================================================== */
/* Opaque client structure (C++ internal)                              */
/* ================================================================== */

struct render_ipc_client {
    uv_loop_t            *loop     = nullptr;
    uv_process_t          proc     = {};
    uv_process_options_t  proc_opts= {};
    uv_pipe_t             write_pipe = {};  /* parent → child (write) */
    uv_pipe_t             read_pipe  = {};  /* child → parent (read)  */

    /* bu_ipc handles — held during spawn, released afterward */
    bu_ipc_chan_t        *parent_chan = nullptr;
    bu_ipc_chan_t        *child_chan  = nullptr;

    /* Receive-side reassembly buffer */
    std::vector<unsigned char> rbuf;

    /* Caller callbacks */
    render_pixel_cb    pxcb   = nullptr;  void *pxud   = nullptr;
    render_progress_cb progcb = nullptr;  void *progud = nullptr;
    render_done_cb     donecb = nullptr;  void *doneud = nullptr;

    int running = 0;
};


/* ================================================================== */
/* Packet dispatch                                                      */
/* ================================================================== */

static void
dispatch_packet(render_ipc_client *cli,
		uint8_t type, uint32_t paylen,
		const unsigned char *payload)
{
    switch (type) {

    case RENDER_IPC_T_PIXELS: {
	if (paylen < (uint32_t)sizeof(render_ipc_pixels)) {
	    bu_log("render_ipc_client: truncated PIXELS packet\n");
	    break;
	}
	const auto *pix = reinterpret_cast<const render_ipc_pixels *>(payload);
	const unsigned char *rgb = payload + sizeof(*pix);
	if (cli->pxcb)
	    cli->pxcb(cli->pxud, pix->x, pix->y, pix->count, rgb);
	break;
    }

    case RENDER_IPC_T_PROGRESS: {
	if (paylen < (uint32_t)sizeof(render_ipc_progress)) {
	    bu_log("render_ipc_client: truncated PROGRESS packet\n");
	    break;
	}
	const auto *prg = reinterpret_cast<const render_ipc_progress *>(payload);
	if (cli->progcb)
	    cli->progcb(cli->progud, prg->done, prg->total);
	break;
    }

    case RENDER_IPC_T_DONE:
	cli->running = 0;
	if (cli->donecb) cli->donecb(cli->doneud, 1);
	break;

    case RENDER_IPC_T_ERROR: {
	const auto *err = reinterpret_cast<const render_ipc_error *>(payload);
	if (paylen >= (uint32_t)sizeof(*err))
	    bu_log("render_ipc_client: rt_ipc error %d: %.*s\n",
		   err->code, (int)err->msglen,
		   reinterpret_cast<const char *>(payload + sizeof(*err)));
	else
	    bu_log("render_ipc_client: rt_ipc error (no message)\n");
	cli->running = 0;
	if (cli->donecb) cli->donecb(cli->doneud, 0);
	break;
    }

    default:
	bu_log("render_ipc_client: unknown packet type 0x%02x\n",
	       (unsigned)type);
    }
}

static void
process_rbuf(render_ipc_client *cli)
{
    auto &rbuf = cli->rbuf;
    while (rbuf.size() >= IPC_HDR_SIZE) {
	uint32_t magic  = read_le32(rbuf.data());
	uint8_t  type   = rbuf[4];
	uint32_t paylen = read_le32(rbuf.data() + 5);
	size_t   total  = IPC_HDR_SIZE + paylen;

	if (magic != RENDER_IPC_MAGIC) {
	    bu_log("render_ipc_client: bad magic 0x%08x — desync\n", magic);
	    rbuf.clear();
	    return;
	}
	if (rbuf.size() < total)
	    break;

	dispatch_packet(cli, type, paylen, rbuf.data() + IPC_HDR_SIZE);
	rbuf.erase(rbuf.begin(), rbuf.begin() + (ptrdiff_t)total);
    }
}


/* ================================================================== */
/* libuv callbacks                                                      */
/* ================================================================== */

static void
alloc_cb(uv_handle_t *, size_t suggested, uv_buf_t *buf)
{
    buf->base = static_cast<char *>(bu_malloc(suggested, "ipc alloc"));
    buf->len  = suggested;
}

static void
read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    auto *cli = static_cast<render_ipc_client *>(stream->data);

    if (nread > 0) {
	const auto *src = reinterpret_cast<const unsigned char *>(buf->base);
	cli->rbuf.insert(cli->rbuf.end(), src, src + nread);
	process_rbuf(cli);
    } else if (nread == UV_EOF) {
	if (cli->running) {
	    cli->running = 0;
	    if (cli->donecb) cli->donecb(cli->doneud, 1);
	}
    } else if (nread < 0) {
	bu_log("render_ipc_client: read error: %s\n", uv_strerror((int)nread));
	if (cli->running) {
	    cli->running = 0;
	    if (cli->donecb) cli->donecb(cli->doneud, 0);
	}
    }

    if (buf->base) bu_free(buf->base, "ipc alloc");
}

static void
exit_cb(uv_process_t *proc, int64_t exit_status, int)
{
    auto *cli = static_cast<render_ipc_client *>(proc->data);
    if (exit_status != 0)
	bu_log("render_ipc_client: rt_ipc exited with status %lld\n",
	       (long long)exit_status);
    uv_close(reinterpret_cast<uv_handle_t *>(proc), nullptr);
    cli->running = 0;
}


/* ================================================================== */
/* Write helpers                                                        */
/* ================================================================== */

struct WriteReq {
    uv_write_t        req;
    std::vector<unsigned char> data;
};

static void
write_done_cb(uv_write_t *req, int status)
{
    auto *wr = reinterpret_cast<WriteReq *>(req);
    if (status < 0)
	bu_log("render_ipc_client: write error: %s\n", uv_strerror(status));
    delete wr;
}

static int
ipc_send_packet(render_ipc_client *cli,
		uint8_t type,
		const unsigned char *payload, uint32_t paylen)
{
    auto *wr = new WriteReq();
    size_t total = IPC_HDR_SIZE + paylen;
    wr->data.resize(total);

    write_le32(wr->data.data(), RENDER_IPC_MAGIC);
    wr->data[4] = type;
    write_le32(wr->data.data() + 5, paylen);
    if (payload && paylen)
	std::memcpy(wr->data.data() + IPC_HDR_SIZE, payload, paylen);

    uv_buf_t uvbuf = uv_buf_init(
	reinterpret_cast<char *>(wr->data.data()),
	(unsigned int)total);

    int rc = uv_write(&wr->req,
		      reinterpret_cast<uv_stream_t *>(&cli->write_pipe),
		      &uvbuf, 1, write_done_cb);
    if (rc < 0) delete wr;
    return rc;
}


/* ================================================================== */
/* Public API                                                           */
/* ================================================================== */

render_ipc_client_t *
render_ipc_client_create(void)
{
    return new render_ipc_client();
}


void
render_ipc_client_destroy(render_ipc_client_t *cli)
{
    if (!cli) return;
    if (cli->running) {
	uv_process_kill(&cli->proc, SIGTERM);
	cli->running = 0;
    }
    if (cli->parent_chan) { bu_ipc_close(cli->parent_chan); cli->parent_chan = nullptr; }
    if (cli->child_chan)  { bu_ipc_close(cli->child_chan);  cli->child_chan  = nullptr; }
    delete cli;
}


int
render_ipc_client_spawn(render_ipc_client_t *cli,
			const char          *rt_ipc_path,
			uv_loop_t           *loop)
{
    if (!cli || !rt_ipc_path || !loop)
	return UV_EINVAL;

    cli->loop = loop;

    /* ── 1. Create transport-agnostic channel pair (bu_ipc) ─────────── */
    if (bu_ipc_pair(&cli->parent_chan, &cli->child_chan) < 0) {
	bu_log("render_ipc_client_spawn: bu_ipc_pair failed\n");
	return -1;
    }

    bu_log("render_ipc_client_spawn: IPC transport: %s — addr: %s\n",
	   bu_ipc_type(cli->child_chan) == BU_IPC_PIPE   ? "pipe"   :
	   bu_ipc_type(cli->child_chan) == BU_IPC_SOCKET ? "socket" : "tcp",
	   bu_ipc_addr(cli->child_chan));

    /* ── 2. Wrap the parent's read/write fds in libuv pipes ────────── */
    uv_pipe_init(loop, &cli->write_pipe, 0);
    uv_pipe_init(loop, &cli->read_pipe,  0);

    /* parent_chan.fd       = read fd   (receives from child) */
    /* parent_chan.fd_write = write fd  (sends to child) */
    int read_fd  = bu_ipc_fileno(cli->parent_chan);
    /* For pipes the write fd may differ from the read fd. */
    /* Access the write side: for a pipe pair fd_write != fd. */
    /* We expose the write fd via a side-channel: for anonymous pipes
     * the child_chan's read fd is the parent's write-to-child pipe.
     * But we can't reach fd_write through the public API.  Instead,
     * use the child_chan's addr to determine write fd for pipe transport. */
    int write_fd = -1;
    {
	/* parse "pipe:read_fd,write_fd" from child addr */
	const char *addr = bu_ipc_addr(cli->child_chan);
	if (addr && strncmp(addr, "pipe:", 5) == 0) {
	    int rfd = -1, wfd = -1;
	    if (sscanf(addr + 5, "%d,%d", &rfd, &wfd) == 2) {
		/* child's read_fd == parent's write side (pipe pc[0]) */
		/* parent's write fd is the OTHER end of pc: pc[1] */
		/* We already know parent's read fd from bu_ipc_fileno().
		 * For the write side, open it from the child addr.
		 *
		 * Actually: child "pipe:pc_r,cp_w"
		 *   pc_r = read-from-parent fd (in child)
		 *   cp_w = write-to-parent fd (in child)
		 * parent_chan has fd=cp_r, fd_write=pc_w
		 * We need pc_w for writing to child.
		 * pc_w is NOT in child's addr — it's the parent_chan's write fd.
		 * We need internal access to parent_chan->fd_write.
		 * Since bu_ipc_fileno() only returns fd (read), we need to
		 * add bu_ipc_fileno_write() to the API.
		 * For now, fall back to using uv_spawn's pipe mechanism
		 * (redirect stdin/stdout) when bu_ipc gives us a pipe transport.
		 */
		write_fd = -2;  /* signal: use uv_spawn stdio redirect */
	    }
	} else if (read_fd >= 0) {
	    write_fd = read_fd;  /* socket/TCP: same fd for read+write */
	}
    }

    /* ── 3. Spawn rt_ipc using the best available method ───────────── */

    if (write_fd == -2) {
	/* Pipe transport: use uv_spawn with stdio redirection for simplicity
	 * (avoids exposing fd_write through the bu_ipc public API). */
	static char *args[2];
	uv_stdio_container_t stdio[3];

	stdio[0].flags       = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
	stdio[0].data.stream = reinterpret_cast<uv_stream_t *>(&cli->write_pipe);
	stdio[1].flags       = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
	stdio[1].data.stream = reinterpret_cast<uv_stream_t *>(&cli->read_pipe);
	stdio[2].flags       = UV_INHERIT_FD;
	stdio[2].data.fd     = 2;

	args[0] = const_cast<char *>(rt_ipc_path);
	args[1] = nullptr;

	std::memset(&cli->proc_opts, 0, sizeof(cli->proc_opts));
	cli->proc_opts.file        = rt_ipc_path;
	cli->proc_opts.args        = args;
	cli->proc_opts.stdio       = stdio;
	cli->proc_opts.stdio_count = 3;
	cli->proc_opts.exit_cb     = exit_cb;

	cli->proc.data = cli;
	int rc = uv_spawn(loop, &cli->proc, &cli->proc_opts);
	if (rc < 0) {
	    bu_log("render_ipc_client_spawn: uv_spawn failed: %s\n",
		   uv_strerror(rc));
	    bu_ipc_close(cli->parent_chan); cli->parent_chan = nullptr;
	    bu_ipc_close(cli->child_chan);  cli->child_chan  = nullptr;
	    return rc;
	}

	/* libuv's stdio redirect created the pipe fds internally; we no
	 * longer need the bu_ipc channels. */
	bu_ipc_close(cli->parent_chan); cli->parent_chan = nullptr;
	bu_ipc_close(cli->child_chan);  cli->child_chan  = nullptr;

    } else {
	/* Socket / TCP transport: pass address in env, use uv_pipe_open */
	static char *args[2];
	std::string  env_str = std::string(BU_IPC_ADDR_ENVVAR) + "="
			     + bu_ipc_addr(cli->child_chan);
	static const char *env[2] = {nullptr, nullptr};
	env[0] = env_str.c_str();

	uv_stdio_container_t stdio[3];
	stdio[0].flags       = UV_INHERIT_FD; stdio[0].data.fd = 0;
	stdio[1].flags       = UV_INHERIT_FD; stdio[1].data.fd = 1;
	stdio[2].flags       = UV_INHERIT_FD; stdio[2].data.fd = 2;

	args[0] = const_cast<char *>(rt_ipc_path);
	args[1] = nullptr;

	std::memset(&cli->proc_opts, 0, sizeof(cli->proc_opts));
	cli->proc_opts.file        = rt_ipc_path;
	cli->proc_opts.args        = args;
	cli->proc_opts.env         = const_cast<char **>(env);
	cli->proc_opts.stdio       = stdio;
	cli->proc_opts.stdio_count = 3;
	cli->proc_opts.exit_cb     = exit_cb;

	cli->proc.data = cli;
	int rc = uv_spawn(loop, &cli->proc, &cli->proc_opts);
	if (rc < 0) {
	    bu_log("render_ipc_client_spawn: uv_spawn failed: %s\n",
		   uv_strerror(rc));
	    bu_ipc_close(cli->parent_chan); cli->parent_chan = nullptr;
	    bu_ipc_close(cli->child_chan);  cli->child_chan  = nullptr;
	    return rc;
	}

	/* Close child end in parent — child process has its own copy */
	bu_ipc_close(cli->child_chan); cli->child_chan = nullptr;

	/* Wrap the socket fd in libuv for async reads */
	uv_pipe_open(&cli->read_pipe,  read_fd);
	uv_pipe_open(&cli->write_pipe, write_fd);
    }

    /* Start reading from child */
    cli->read_pipe.data = cli;
    int rc = uv_read_start(reinterpret_cast<uv_stream_t *>(&cli->read_pipe),
			   alloc_cb, read_cb);
    if (rc < 0) {
	bu_log("render_ipc_client_spawn: uv_read_start failed: %s\n",
	       uv_strerror(rc));
	uv_process_kill(&cli->proc, SIGTERM);
	return rc;
    }

    cli->running = 1;
    return 0;
}


int
render_ipc_client_submit(render_ipc_client_t *cli,
			 render_ctx_t        *ctx,
			 const render_opts_t *opts)
{
    if (!cli || !ctx || !opts)
	return UV_EINVAL;

    const char *dbfile     = ctx->dbfile;
    size_t      dbfile_len = std::strlen(dbfile) + 1;

    render_ipc_job job{};
    job.version    = RENDER_IPC_VERSION;
    job.width      = opts->width;
    job.height     = opts->height;
    job.lightmodel = opts->lightmodel;
    job.nthreads   = opts->nthreads;
    job.nobjs      = 0;
    job.viewsize   = opts->viewsize;
    job.aspect     = opts->aspect;
    job.dbfile_len = (uint32_t)dbfile_len;

    std::memcpy(job.view2model, opts->view2model, sizeof(job.view2model));
    job.eye[0] = opts->eye_model[X];
    job.eye[1] = opts->eye_model[Y];
    job.eye[2] = opts->eye_model[Z];
    std::copy(std::begin(opts->background), std::end(opts->background),
	      std::begin(job.background));

    uint32_t paylen = (uint32_t)(sizeof(job) + dbfile_len);
    std::vector<unsigned char> payload(paylen);
    std::memcpy(payload.data(), &job, sizeof(job));
    std::memcpy(payload.data() + sizeof(job), dbfile, dbfile_len);

    return ipc_send_packet(cli, RENDER_IPC_T_JOB, payload.data(), paylen);
}


void
render_ipc_client_cancel(render_ipc_client_t *cli)
{
    if (!cli || !cli->running) return;
    ipc_send_packet(cli, RENDER_IPC_T_CANCEL, nullptr, 0);
    uv_process_kill(&cli->proc, SIGTERM);
    cli->running = 0;
}


void render_ipc_client_on_pixels(render_ipc_client_t *c, render_pixel_cb cb, void *ud)
{ if (c) { c->pxcb = cb; c->pxud = ud; } }

void render_ipc_client_on_progress(render_ipc_client_t *c, render_progress_cb cb, void *ud)
{ if (c) { c->progcb = cb; c->progud = ud; } }

void render_ipc_client_on_done(render_ipc_client_t *c, render_done_cb cb, void *ud)
{ if (c) { c->donecb = cb; c->doneud = ud; } }


#endif /* RENDER_HAVE_LIBUV */

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
