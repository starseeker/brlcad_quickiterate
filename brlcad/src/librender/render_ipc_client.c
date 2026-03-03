/*             R E N D E R _ I P C _ C L I E N T . C
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
/** @file librender/render_ipc_client.c
 *
 * libuv-based out-of-process IPC client for librender.
 *
 * Spawns an rt_ipc child process and communicates with it over a
 * libuv anonymous pipe (not TCP).  The pipe carries render_ipc.h
 * framed packets.  Pixel scanlines delivered by rt_ipc are forwarded
 * to the caller's render_pixel_cb.
 *
 * ### Why libuv pipes instead of TCP (fbserv)?
 *
 *   - No port allocation required.
 *   - Local IPC is significantly faster than loopback TCP on most OSes.
 *   - Works inside sandboxed / firewall-restricted environments.
 *   - libuv supports named pipes, UNIX domain sockets, and Windows
 *     named pipes transparently through the same API.
 *   - The child process can be killed cleanly through its process handle
 *     without needing to track a port number.
 *
 * ### Packet framing
 *
 *   Every message sent over the pipe begins with a fixed 9-byte header:
 *     [magic:4LE][type:1][paylen:4LE]
 *   followed by paylen bytes of payload.
 *
 * See render_ipc.h for the complete packet catalogue.
 */

#include "common.h"

#ifdef RENDER_HAVE_LIBUV

#include <stdlib.h>
#include <string.h>

#include <uv.h>

#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "vmath.h"

#include "render.h"
#include "render_ipc.h"
#include "./render_private.h"


/* ================================================================== */
/* Packet framing helpers                                               */
/* ================================================================== */

/* Minimum bytes needed to determine payload length: the header size. */
#define IPC_HDR_SIZE  9   /* 4 magic + 1 type + 4 paylen */

/**
 * @brief Write a host uint32 to a LE byte buffer.
 */
static void
write_le32(unsigned char *buf, uint32_t v)
{
    buf[0] = (unsigned char)(v & 0xff);
    buf[1] = (unsigned char)((v >> 8)  & 0xff);
    buf[2] = (unsigned char)((v >> 16) & 0xff);
    buf[3] = (unsigned char)((v >> 24) & 0xff);
}

/**
 * @brief Read a LE uint32 from a byte buffer.
 */
static uint32_t
read_le32(const unsigned char *buf)
{
    return ((uint32_t)buf[0])
	 | ((uint32_t)buf[1] << 8)
	 | ((uint32_t)buf[2] << 16)
	 | ((uint32_t)buf[3] << 24);
}


/* ================================================================== */
/* Opaque client structure                                              */
/* ================================================================== */

struct render_ipc_client {
    uv_loop_t         *loop;
    uv_process_t       proc;
    uv_process_options_t proc_opts;
    uv_pipe_t          stdin_pipe;   /* parent write end  → child stdin  */
    uv_pipe_t          stdout_pipe;  /* child stdout      → parent read  */

    /* read-side reassembly buffer */
    unsigned char      *rbuf;
    size_t              rbuf_used;
    size_t              rbuf_cap;

    /* caller-supplied callbacks */
    render_pixel_cb    pxcb;
    void              *pxud;
    render_progress_cb progcb;
    void              *progud;
    render_done_cb     donecb;
    void              *doneud;

    int                running;   /* 1 while rt_ipc process is alive */
};


/* ================================================================== */
/* Receive-buffer helpers                                               */
/* ================================================================== */

/**
 * @brief Ensure at least @p needed bytes of space in the receive buffer.
 */
static void
rbuf_reserve(struct render_ipc_client *cli, size_t needed)
{
    if (cli->rbuf_used + needed <= cli->rbuf_cap)
	return;

    cli->rbuf_cap = cli->rbuf_used + needed + 4096;
    cli->rbuf = (unsigned char *)bu_realloc(
	cli->rbuf, cli->rbuf_cap, "ipc client rbuf");
}

/**
 * @brief Append @p len bytes from @p data to the receive buffer.
 */
static void
rbuf_append(struct render_ipc_client *cli,
	    const unsigned char *data, size_t len)
{
    rbuf_reserve(cli, len);
    memcpy(cli->rbuf + cli->rbuf_used, data, len);
    cli->rbuf_used += len;
}

/**
 * @brief Consume @p len bytes from the front of the receive buffer.
 */
static void
rbuf_consume(struct render_ipc_client *cli, size_t len)
{
    if (len >= cli->rbuf_used) {
	cli->rbuf_used = 0;
	return;
    }
    memmove(cli->rbuf, cli->rbuf + len, cli->rbuf_used - len);
    cli->rbuf_used -= len;
}


/* ================================================================== */
/* Packet dispatch                                                      */
/* ================================================================== */

/**
 * @brief Process one complete packet from the receive buffer.
 *
 * Called when rbuf contains at least (IPC_HDR_SIZE + paylen) bytes.
 */
static void
dispatch_packet(struct render_ipc_client *cli,
		uint8_t type, uint32_t paylen,
		const unsigned char *payload)
{
    switch (type) {

    case RENDER_IPC_T_PIXELS: {
	const struct render_ipc_pixels *pix;
	const unsigned char *rgb;

	if (paylen < (uint32_t)sizeof(*pix)) {
	    bu_log("render_ipc_client: truncated PIXELS packet\n");
	    break;
	}
	pix = (const struct render_ipc_pixels *)payload;
	rgb = payload + sizeof(*pix);

	if (cli->pxcb)
	    cli->pxcb(cli->pxud, pix->x, pix->y, pix->count, rgb);
	break;
    }

    case RENDER_IPC_T_PROGRESS: {
	const struct render_ipc_progress *prg;
	if (paylen < (uint32_t)sizeof(*prg)) {
	    bu_log("render_ipc_client: truncated PROGRESS packet\n");
	    break;
	}
	prg = (const struct render_ipc_progress *)payload;
	if (cli->progcb)
	    cli->progcb(cli->progud, prg->done, prg->total);
	break;
    }

    case RENDER_IPC_T_DONE:
	cli->running = 0;
	if (cli->donecb)
	    cli->donecb(cli->doneud, 1 /* success */);
	break;

    case RENDER_IPC_T_ERROR: {
	const struct render_ipc_error *err;
	if (paylen >= (uint32_t)sizeof(*err)) {
	    err = (const struct render_ipc_error *)payload;
	    bu_log("render_ipc_client: rt_ipc error %d: %.*s\n",
		   err->code, (int)err->msglen,
		   (const char *)(payload + sizeof(*err)));
	} else {
	    bu_log("render_ipc_client: rt_ipc sent error packet\n");
	}
	cli->running = 0;
	if (cli->donecb)
	    cli->donecb(cli->doneud, 0 /* failure */);
	break;
    }

    default:
	bu_log("render_ipc_client: unknown packet type 0x%02x, skipping\n",
	       (unsigned)type);
	break;
    }
}

/**
 * @brief Try to parse and dispatch as many complete packets as possible
 *        from the receive buffer.
 */
static void
process_rbuf(struct render_ipc_client *cli)
{
    while (cli->rbuf_used >= IPC_HDR_SIZE) {
	uint32_t magic;
	uint8_t  type;
	uint32_t paylen;
	size_t   total;

	magic  = read_le32(cli->rbuf);
	type   = cli->rbuf[4];
	paylen = read_le32(cli->rbuf + 5);

	if (magic != RENDER_IPC_MAGIC) {
	    bu_log("render_ipc_client: bad magic 0x%08x, desync\n", magic);
	    cli->rbuf_used = 0;   /* discard all buffered data and hope for re-sync */
	    return;
	}

	total = IPC_HDR_SIZE + (size_t)paylen;
	if (cli->rbuf_used < total)
	    break;   /* wait for more data */

	dispatch_packet(cli, type, paylen, cli->rbuf + IPC_HDR_SIZE);
	rbuf_consume(cli, total);
    }
}


/* ================================================================== */
/* libuv callbacks                                                      */
/* ================================================================== */

/**
 * @brief Allocate a read buffer for libuv.
 */
static void
ipc_alloc_cb(uv_handle_t *UNUSED(handle),
	     size_t suggested_size,
	     uv_buf_t *buf)
{
    buf->base = (char *)bu_malloc(suggested_size, "ipc alloc");
    buf->len  = suggested_size;
}

/**
 * @brief Data received from the rt_ipc child's stdout.
 */
static void
ipc_read_cb(uv_stream_t *stream,
	    ssize_t      nread,
	    const uv_buf_t *buf)
{
    struct render_ipc_client *cli =
	(struct render_ipc_client *)stream->data;

    if (nread > 0) {
	rbuf_append(cli, (const unsigned char *)buf->base, (size_t)nread);
	process_rbuf(cli);
    } else if (nread == UV_EOF) {
	/* rt_ipc closed its stdout: treat as completion if not already done. */
	if (cli->running) {
	    cli->running = 0;
	    if (cli->donecb)
		cli->donecb(cli->doneud, 1);
	}
    } else if (nread < 0) {
	bu_log("render_ipc_client: read error: %s\n", uv_strerror((int)nread));
	if (cli->running) {
	    cli->running = 0;
	    if (cli->donecb)
		cli->donecb(cli->doneud, 0);
	}
    }

    if (buf->base)
	bu_free(buf->base, "ipc alloc");
}

/**
 * @brief Child process exit notification.
 */
static void
ipc_exit_cb(uv_process_t *proc,
	    int64_t       exit_status,
	    int           UNUSED(term_signal))
{
    struct render_ipc_client *cli =
	(struct render_ipc_client *)proc->data;

    if (exit_status != 0)
	bu_log("render_ipc_client: rt_ipc exited with status %lld\n",
	       (long long)exit_status);

    uv_close((uv_handle_t *)proc, NULL);
    cli->running = 0;
}


/* ================================================================== */
/* Write helpers                                                        */
/* ================================================================== */

typedef struct {
    uv_write_t req;
    unsigned char *buf;
} write_req_t;

static void
ipc_write_done_cb(uv_write_t *req, int status)
{
    write_req_t *wr = (write_req_t *)req;
    if (status < 0)
	bu_log("render_ipc_client: write error: %s\n", uv_strerror(status));
    bu_free(wr->buf, "ipc write buf");
    bu_free(wr, "ipc write_req");
}

/**
 * @brief Send @p nbytes of data to rt_ipc's stdin.
 */
static int
ipc_write(struct render_ipc_client *cli,
	  const unsigned char *data, size_t nbytes)
{
    write_req_t *wr;
    uv_buf_t uvbuf;

    wr = (write_req_t *)bu_malloc(sizeof(*wr), "ipc write_req");
    wr->buf = (unsigned char *)bu_malloc(nbytes, "ipc write buf");
    memcpy(wr->buf, data, nbytes);

    uvbuf = uv_buf_init((char *)wr->buf, (unsigned int)nbytes);
    return uv_write((uv_write_t *)wr,
		    (uv_stream_t *)&cli->stdin_pipe,
		    &uvbuf, 1,
		    ipc_write_done_cb);
}

/**
 * @brief Write a framed IPC packet to the child's stdin.
 *
 * @p payload may be NULL when paylen == 0.
 */
static int
ipc_send_packet(struct render_ipc_client *cli,
		uint8_t type,
		const unsigned char *payload, uint32_t paylen)
{
    unsigned char hdr[IPC_HDR_SIZE];
    unsigned char *buf;
    size_t total = IPC_HDR_SIZE + paylen;
    int rc;

    write_le32(hdr, RENDER_IPC_MAGIC);
    hdr[4] = type;
    write_le32(hdr + 5, paylen);

    buf = (unsigned char *)bu_malloc(total, "ipc pkt buf");
    memcpy(buf, hdr, IPC_HDR_SIZE);
    if (paylen && payload)
	memcpy(buf + IPC_HDR_SIZE, payload, paylen);

    rc = ipc_write(cli, buf, total);
    bu_free(buf, "ipc pkt buf");
    return rc;
}


/* ================================================================== */
/* Public API                                                           */
/* ================================================================== */

struct render_ipc_client *
render_ipc_client_create(void)
{
    struct render_ipc_client *cli;

    BU_GET(cli, struct render_ipc_client);
    memset(cli, 0, sizeof(*cli));

    cli->rbuf_cap = 4096;
    cli->rbuf = (unsigned char *)bu_malloc(cli->rbuf_cap, "ipc client rbuf");

    return cli;
}


void
render_ipc_client_destroy(struct render_ipc_client *cli)
{
    if (!cli)
	return;

    if (cli->running) {
	uv_process_kill(&cli->proc, SIGTERM);
	cli->running = 0;
    }

    if (cli->rbuf)
	bu_free(cli->rbuf, "ipc client rbuf");

    BU_PUT(cli, struct render_ipc_client);
}


int
render_ipc_client_spawn(struct render_ipc_client *cli,
			const char              *rt_ipc_path,
			uv_loop_t               *loop)
{
    static char *args[2];
    uv_stdio_container_t stdio[3];
    int rc;

    if (!cli || !rt_ipc_path || !loop)
	return UV_EINVAL;

    cli->loop = loop;

    /* stdin pipe (parent writes to child) */
    uv_pipe_init(loop, &cli->stdin_pipe,  0 /* not IPC handle */);
    /* stdout pipe (child writes to parent) */
    uv_pipe_init(loop, &cli->stdout_pipe, 0);

    cli->stdout_pipe.data = cli;   /* passed to read callbacks */

    /* child stdin = our write pipe */
    stdio[0].flags         = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
    stdio[0].data.stream   = (uv_stream_t *)&cli->stdin_pipe;

    /* child stdout = our read pipe */
    stdio[1].flags         = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[1].data.stream   = (uv_stream_t *)&cli->stdout_pipe;

    /* child stderr = inherit (so rt_ipc log output reaches the terminal) */
    stdio[2].flags         = UV_INHERIT_FD;
    stdio[2].data.fd       = 2;

    args[0] = (char *)rt_ipc_path;
    args[1] = NULL;

    memset(&cli->proc_opts, 0, sizeof(cli->proc_opts));
    cli->proc_opts.file      = rt_ipc_path;
    cli->proc_opts.args      = args;
    cli->proc_opts.stdio     = stdio;
    cli->proc_opts.stdio_count = 3;
    cli->proc_opts.exit_cb   = ipc_exit_cb;
    cli->proc_opts.flags     = 0;

    cli->proc.data = cli;

    rc = uv_spawn(loop, &cli->proc, &cli->proc_opts);
    if (rc < 0) {
	bu_log("render_ipc_client_spawn: uv_spawn(%s) failed: %s\n",
	       rt_ipc_path, uv_strerror(rc));
	return rc;
    }

    /* start receiving from child stdout */
    rc = uv_read_start((uv_stream_t *)&cli->stdout_pipe,
		       ipc_alloc_cb, ipc_read_cb);
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
render_ipc_client_submit(struct render_ipc_client *cli,
			 render_ctx_t            *ctx,
			 const render_opts_t     *opts)
{
    struct render_ipc_job job;
    const char *dbfile;
    size_t dbfile_len;
    unsigned char *payload;
    uint32_t paylen;
    int rc;

    if (!cli || !ctx || !opts)
	return UV_EINVAL;

    dbfile     = ctx->dbfile;
    dbfile_len = strlen(dbfile) + 1;   /* include NUL terminator */

    memset(&job, 0, sizeof(job));
    job.version    = RENDER_IPC_VERSION;
    job.width      = opts->width;
    job.height     = opts->height;
    job.lightmodel = opts->lightmodel;
    job.nthreads   = opts->nthreads;
    job.nobjs      = 0;   /* objects already loaded in ctx; server uses dbfile */
    job.viewsize   = opts->viewsize;
    job.aspect     = opts->aspect;
    job.dbfile_len = (uint32_t)dbfile_len;

    memcpy(job.view2model, opts->view2model, sizeof(job.view2model));
    job.eye[0] = opts->eye_model[X];
    job.eye[1] = opts->eye_model[Y];
    job.eye[2] = opts->eye_model[Z];
    job.background[0] = opts->background[0];
    job.background[1] = opts->background[1];
    job.background[2] = opts->background[2];

    /* payload = struct render_ipc_job + dbfile string */
    paylen  = (uint32_t)(sizeof(job) + dbfile_len);
    payload = (unsigned char *)bu_malloc(paylen, "ipc job payload");
    memcpy(payload, &job, sizeof(job));
    memcpy(payload + sizeof(job), dbfile, dbfile_len);

    rc = ipc_send_packet(cli, RENDER_IPC_T_JOB, payload, paylen);
    bu_free(payload, "ipc job payload");
    return rc;
}


void
render_ipc_client_cancel(struct render_ipc_client *cli)
{
    if (!cli || !cli->running)
	return;

    /* Send a CANCEL packet first (rt_ipc should finish cleanly). */
    ipc_send_packet(cli, RENDER_IPC_T_CANCEL, NULL, 0);

    /* Give it 500 ms to exit gracefully; libuv loop will be running
     * in the caller, so we don't block here. */
    uv_process_kill(&cli->proc, SIGTERM);
    cli->running = 0;
}


void
render_ipc_client_on_pixels(struct render_ipc_client *cli,
			    render_pixel_cb          cb,
			    void                    *ud)
{
    if (!cli) return;
    cli->pxcb = cb;
    cli->pxud = ud;
}


void
render_ipc_client_on_progress(struct render_ipc_client *cli,
			      render_progress_cb        cb,
			      void                     *ud)
{
    if (!cli) return;
    cli->progcb  = cb;
    cli->progud  = ud;
}


void
render_ipc_client_on_done(struct render_ipc_client *cli,
			  render_done_cb            cb,
			  void                     *ud)
{
    if (!cli) return;
    cli->donecb  = cb;
    cli->doneud  = ud;
}


#endif /* RENDER_HAVE_LIBUV */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
