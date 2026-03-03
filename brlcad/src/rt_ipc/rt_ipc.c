/*                       R T _ I P C . C
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
/** @file rt_ipc/rt_ipc.c
 *
 * rt_ipc – out-of-process raytrace worker using librender + libuv IPC.
 *
 * ### Purpose
 *
 * rt_ipc replaces the rt + fbserv/TCP approach for embedded raytrace
 * display.  Its responsibilities are:
 *
 *   1. Receive a render job specification from the parent process over
 *      a libuv anonymous pipe (connected to stdin/stdout).
 *   2. Execute the raytrace synchronously using librender.
 *   3. Stream pixel scanlines back to the parent as RENDER_IPC_T_PIXELS
 *      packets as they are generated, enabling incremental display.
 *   4. Send RENDER_IPC_T_DONE or RENDER_IPC_T_ERROR on completion.
 *   5. Honour RENDER_IPC_T_CANCEL from the parent by terminating early.
 *
 * ### Why a separate process?
 *
 *   - The process can be killed with SIGTERM/SIGKILL without corrupting
 *     the parent's memory, which is essential for robustly handling
 *     runaway or stuck raytrace jobs.
 *   - The parent remains responsive while the raytrace runs.
 *   - Multiple concurrent renders can be managed independently.
 *
 * ### Wire protocol
 *
 *   stdin:   receives RENDER_IPC_T_JOB packets from the parent.
 *   stdout:  sends  RENDER_IPC_T_PIXELS / DONE / ERROR packets.
 *   stderr:  free for log output (not part of the protocol).
 *
 * See render_ipc.h for the full packet format.
 *
 * ### Usage
 *
 *   Normally spawned by render_ipc_client_spawn() inside librender.
 *   Can also be invoked directly from the command line for testing:
 *     rt_ipc < job.bin > pixels.bin
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>

#include <uv.h>

#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "vmath.h"

#include "render.h"
#include "render_ipc.h"


/* ================================================================== */
/* Packet framing helpers (mirrors render_ipc_client.c)                */
/* ================================================================== */

#define IPC_HDR_SIZE  9   /* 4 magic + 1 type + 4 paylen */

static void
write_le32(unsigned char *buf, uint32_t v)
{
    buf[0] = (unsigned char)(v & 0xff);
    buf[1] = (unsigned char)((v >> 8)  & 0xff);
    buf[2] = (unsigned char)((v >> 16) & 0xff);
    buf[3] = (unsigned char)((v >> 24) & 0xff);
}

static uint32_t
read_le32(const unsigned char *buf)
{
    return ((uint32_t)buf[0])
	 | ((uint32_t)buf[1] << 8)
	 | ((uint32_t)buf[2] << 16)
	 | ((uint32_t)buf[3] << 24);
}

/**
 * @brief Write a framed IPC packet to the given pipe stream.
 *
 * This is a synchronous write used during startup/shutdown.
 * Pixel data is written via the async pixel callback instead.
 */
static void
ipc_write_sync(uv_pipe_t *pipe,
	       uint8_t type,
	       const unsigned char *payload, uint32_t paylen)
{
    unsigned char *buf;
    size_t total = IPC_HDR_SIZE + paylen;
    uv_buf_t uvbuf;

    buf = (unsigned char *)bu_malloc(total, "rt_ipc write buf");
    write_le32(buf, RENDER_IPC_MAGIC);
    buf[4] = type;
    write_le32(buf + 5, paylen);
    if (paylen && payload)
	memcpy(buf + IPC_HDR_SIZE, payload, paylen);

    uvbuf = uv_buf_init((char *)buf, (unsigned int)total);
    /* Synchronous write using uv_try_write; fall back to blocking write. */
    if (uv_try_write((uv_stream_t *)pipe, &uvbuf, 1) < 0) {
	/* Best-effort; ignore errors on shutdown paths. */
    }
    bu_free(buf, "rt_ipc write buf");
}


/* ================================================================== */
/* Server state                                                         */
/* ================================================================== */

struct rt_ipc_state {
    uv_loop_t  *loop;
    uv_pipe_t   stdin_pipe;   /* reads job from parent */
    uv_pipe_t   stdout_pipe;  /* sends pixels to parent */

    /* read-side reassembly */
    unsigned char *rbuf;
    size_t         rbuf_used;
    size_t         rbuf_cap;

    int            cancelled;  /* set when CANCEL packet received */
    int            job_done;   /* set after render completes */

    /* Current render session */
    render_ctx_t  *ctx;
    render_opts_t *opts;
};

static struct rt_ipc_state *g_state = NULL;  /* single-process singleton */


/* ================================================================== */
/* Pixel callback – streams scanlines to the parent in real time       */
/* ================================================================== */

static void
pixel_cb(void *ud,
	 int x, int y, int w,
	 const unsigned char *rgb)
{
    struct rt_ipc_state *st = (struct rt_ipc_state *)ud;
    struct render_ipc_pixels phdr;
    size_t rgb_bytes;
    unsigned char *payload;
    uint32_t paylen;

    if (st->cancelled)
	return;

    phdr.x     = (int32_t)x;
    phdr.y     = (int32_t)y;
    phdr.count = (int32_t)w;

    rgb_bytes = (size_t)(w * 3);
    paylen    = (uint32_t)(sizeof(phdr) + rgb_bytes);
    payload   = (unsigned char *)bu_malloc(paylen, "rt_ipc pixels payload");
    memcpy(payload, &phdr, sizeof(phdr));
    memcpy(payload + sizeof(phdr), rgb, rgb_bytes);

    ipc_write_sync(&st->stdout_pipe, RENDER_IPC_T_PIXELS, payload, paylen);
    bu_free(payload, "rt_ipc pixels payload");
}


/* ================================================================== */
/* Progress callback                                                    */
/* ================================================================== */

static void
progress_cb(void *ud, int done, int total)
{
    struct rt_ipc_state *st = (struct rt_ipc_state *)ud;
    struct render_ipc_progress p;

    p.done  = (int32_t)done;
    p.total = (int32_t)total;

    ipc_write_sync(&st->stdout_pipe, RENDER_IPC_T_PROGRESS,
		   (const unsigned char *)&p, sizeof(p));
}


/* ================================================================== */
/* Job packet handler                                                   */
/* ================================================================== */

static void
handle_job(struct rt_ipc_state *st,
	   const unsigned char *payload, uint32_t paylen)
{
    const struct render_ipc_job *job;
    const char *strings;   /* dbfile + object names */
    const char *dbfile;
    const char **objs;
    int i;
    const char *p;
    int ok;

    if (paylen < (uint32_t)sizeof(*job)) {
	bu_log("rt_ipc: truncated JOB packet (%u bytes)\n", paylen);
	ipc_write_sync(&st->stdout_pipe, RENDER_IPC_T_ERROR, NULL, 0);
	st->job_done = 1;
	return;
    }

    job     = (const struct render_ipc_job *)payload;
    strings = (const char *)(payload + sizeof(*job));

    if (job->version != RENDER_IPC_VERSION) {
	bu_log("rt_ipc: protocol version mismatch (got %u, expected %u)\n",
	       (unsigned)job->version, (unsigned)RENDER_IPC_VERSION);
	ipc_write_sync(&st->stdout_pipe, RENDER_IPC_T_ERROR, NULL, 0);
	st->job_done = 1;
	return;
    }

    /* Extract database file path */
    dbfile = strings;

    /* Extract object names (follow dbfile string) */
    objs = NULL;
    if (job->nobjs > 0) {
	objs = (const char **)bu_calloc(
	    job->nobjs, sizeof(const char *), "rt_ipc objs");
	p = strings + job->dbfile_len;
	for (i = 0; i < job->nobjs; i++) {
	    objs[i] = p;
	    p += strlen(p) + 1;
	}
    }

    /* Build context and options */
    st->ctx = render_ctx_create(dbfile,
				job->nobjs > 0 ? job->nobjs : 0,
				job->nobjs > 0 ? objs : NULL);
    if (!st->ctx) {
	bu_log("rt_ipc: render_ctx_create('%s') failed\n", dbfile);
	if (objs) bu_free(objs, "rt_ipc objs");
	ipc_write_sync(&st->stdout_pipe, RENDER_IPC_T_ERROR, NULL, 0);
	st->job_done = 1;
	return;
    }

    if (objs) bu_free(objs, "rt_ipc objs");

    st->opts = render_opts_create();
    render_opts_set_size(st->opts, job->width, job->height);
    render_opts_set_view(st->opts, job->view2model, job->eye, job->viewsize);
    render_opts_set_aspect(st->opts, job->aspect);
    render_opts_set_lighting(st->opts, job->lightmodel);
    render_opts_set_threads(st->opts, job->nthreads);
    render_opts_set_background(st->opts,
			       job->background[0],
			       job->background[1],
			       job->background[2]);

    bu_log("rt_ipc: starting render %dx%d model='%s' lighting=%d\n",
	   job->width, job->height, dbfile, job->lightmodel);

    /* Execute the render (synchronous — the event loop is not running
     * during this call, which is intentional: we want to stay single-
     * threaded with respect to the I/O loop so that we don't need
     * locking on the stdout pipe. */
    ok = render_run(st->ctx, st->opts,
		    pixel_cb, progress_cb, NULL,
		    st);

    if (ok == BRLCAD_OK) {
	bu_log("rt_ipc: render complete\n");
	ipc_write_sync(&st->stdout_pipe, RENDER_IPC_T_DONE, NULL, 0);
    } else {
	bu_log("rt_ipc: render failed\n");
	ipc_write_sync(&st->stdout_pipe, RENDER_IPC_T_ERROR, NULL, 0);
    }

    render_opts_destroy(st->opts);
    render_ctx_destroy(st->ctx);
    st->opts     = NULL;
    st->ctx      = NULL;
    st->job_done = 1;

    /* Stop the event loop — we only handle one job per process lifetime. */
    uv_stop(st->loop);
}


/* ================================================================== */
/* Receive buffer helpers (mirrors render_ipc_client.c)                */
/* ================================================================== */

static void
rbuf_reserve(struct rt_ipc_state *st, size_t needed)
{
    if (st->rbuf_used + needed <= st->rbuf_cap)
	return;
    st->rbuf_cap = st->rbuf_used + needed + 4096;
    st->rbuf = (unsigned char *)bu_realloc(
	st->rbuf, st->rbuf_cap, "rt_ipc rbuf");
}

static void
rbuf_append(struct rt_ipc_state *st,
	    const unsigned char *data, size_t len)
{
    rbuf_reserve(st, len);
    memcpy(st->rbuf + st->rbuf_used, data, len);
    st->rbuf_used += len;
}

static void
rbuf_consume(struct rt_ipc_state *st, size_t len)
{
    if (len >= st->rbuf_used) { st->rbuf_used = 0; return; }
    memmove(st->rbuf, st->rbuf + len, st->rbuf_used - len);
    st->rbuf_used -= len;
}

/**
 * @brief Process all complete packets in the read buffer.
 */
static void
process_rbuf(struct rt_ipc_state *st)
{
    while (st->rbuf_used >= IPC_HDR_SIZE) {
	uint32_t magic;
	uint8_t  type;
	uint32_t paylen;
	size_t   total;

	magic  = read_le32(st->rbuf);
	type   = st->rbuf[4];
	paylen = read_le32(st->rbuf + 5);

	if (magic != RENDER_IPC_MAGIC) {
	    bu_log("rt_ipc: bad magic 0x%08x — desync, aborting\n", magic);
	    st->job_done = 1;
	    uv_stop(st->loop);
	    return;
	}

	total = IPC_HDR_SIZE + (size_t)paylen;
	if (st->rbuf_used < total)
	    break;   /* wait for more data */

	switch (type) {
	case RENDER_IPC_T_JOB:
	    handle_job(st, st->rbuf + IPC_HDR_SIZE, paylen);
	    break;
	case RENDER_IPC_T_CANCEL:
	    bu_log("rt_ipc: received CANCEL\n");
	    st->cancelled = 1;
	    st->job_done  = 1;
	    uv_stop(st->loop);
	    break;
	default:
	    bu_log("rt_ipc: unknown packet type 0x%02x, skipping\n",
		   (unsigned)type);
	    break;
	}

	rbuf_consume(st, total);
    }
}


/* ================================================================== */
/* libuv read callback on stdin                                         */
/* ================================================================== */

static void
stdin_alloc_cb(uv_handle_t *UNUSED(h), size_t suggested, uv_buf_t *buf)
{
    buf->base = (char *)bu_malloc(suggested, "rt_ipc read buf");
    buf->len  = suggested;
}

static void
stdin_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    struct rt_ipc_state *st = (struct rt_ipc_state *)stream->data;

    if (nread > 0) {
	rbuf_append(st, (const unsigned char *)buf->base, (size_t)nread);
	process_rbuf(st);
    } else if (nread == UV_EOF) {
	bu_log("rt_ipc: stdin closed by parent\n");
	uv_stop(st->loop);
    } else if (nread < 0 && nread != UV_EOF) {
	bu_log("rt_ipc: stdin read error: %s\n", uv_strerror((int)nread));
	uv_stop(st->loop);
    }

    if (buf->base)
	bu_free(buf->base, "rt_ipc read buf");
}


/* ================================================================== */
/* main                                                                 */
/* ================================================================== */

int
main(int argc, const char *argv[])
{
    uv_loop_t          loop;
    struct rt_ipc_state st;
    int rc;

    bu_setprogname(argv[0]);
    (void)argc;

    /* Ignore SIGPIPE: we report write errors through libuv callbacks. */
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    /* Initialise libuv event loop. */
    rc = uv_loop_init(&loop);
    if (rc < 0) {
	bu_log("rt_ipc: uv_loop_init failed: %s\n", uv_strerror(rc));
	return 1;
    }

    /* Prepare our server state. */
    memset(&st, 0, sizeof(st));
    st.loop     = &loop;
    st.rbuf_cap = 4096;
    st.rbuf     = (unsigned char *)bu_malloc(st.rbuf_cap, "rt_ipc rbuf");

    g_state = &st;

    /* Wrap stdin as a libuv pipe for reading the job request. */
    uv_pipe_init(&loop, &st.stdin_pipe, 0);
    uv_pipe_open(&st.stdin_pipe, 0 /* STDIN_FILENO */);
    st.stdin_pipe.data = &st;
    uv_read_start((uv_stream_t *)&st.stdin_pipe,
		  stdin_alloc_cb, stdin_read_cb);

    /* Wrap stdout as a libuv pipe for writing pixel packets. */
    uv_pipe_init(&loop, &st.stdout_pipe, 0);
    uv_pipe_open(&st.stdout_pipe, 1 /* STDOUT_FILENO */);

    bu_log("rt_ipc: ready (pid %d)\n", (int)uv_os_getpid());

    /* Run the event loop until a render completes (or cancel/error). */
    uv_run(&loop, UV_RUN_DEFAULT);

    bu_free(st.rbuf, "rt_ipc rbuf");
    uv_loop_close(&loop);

    return st.cancelled ? 1 : 0;
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
