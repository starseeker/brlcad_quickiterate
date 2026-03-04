/*                       R T _ I P C . C P P
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
/** @file rt_ipc/rt_ipc.cpp
 *
 * rt_ipc – out-of-process raytrace worker using librtrender + bu_ipc.
 *
 * ### IPC channel setup
 *
 * rt_ipc is launched by render_ipc_client_spawn().  The channel to
 * the parent is established via bu_ipc:
 *
 *   If the parent used pipes: stdin/stdout are already the pipe ends
 *     (libuv's uv_spawn redirected them).  rt_ipc reads from stdin and
 *     writes to stdout, exactly as before.
 *
 *   If the parent used a socket or TCP: the BU_IPC_ADDR_ENVVAR
 *     environment variable is set and bu_ipc_connect_env() attaches to
 *     the correct endpoint.  Stdin/stdout are inherited unchanged.
 *
 * After establishing the channel, rt_ipc uses bu_ipc_read() and
 * bu_ipc_write() (blocking) for all communication.  No libuv event
 * loop is needed in the child because:
 *
 *   - It handles a single job per process lifetime (start → raytrace →
 *     stream scanlines → exit).
 *   - The parent is responsible for the event loop and cancellation.
 *   - A separate process is the unit of isolation; if the raytrace hangs
 *     or loops infinitely, the parent kills the process cleanly.
 *
 * ### Packet protocol
 *
 *   stdin  (or socket): RENDER_IPC_T_JOB    (one per invocation)
 *                       RENDER_IPC_T_CANCEL  (optional, at any time)
 *   stdout (or socket): RENDER_IPC_T_PIXELS  (one per scanline)
 *                       RENDER_IPC_T_PROGRESS (periodic)
 *                       RENDER_IPC_T_DONE / RENDER_IPC_T_ERROR
 *
 * See render_ipc.h for the wire format.
 */

#include "common.h"

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <vector>
#include <string>


#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/ipc.h"
#include "bu/process.h"
#include "vmath.h"

#include "render.h"
#include "render_ipc.h"


/* ================================================================== */
/* Packet framing helpers                                               */
/* ================================================================== */

#define IPC_HDR_SIZE 9

static inline void write_le32(unsigned char *b, uint32_t v) {
    b[0] = v & 0xff; b[1] = (v>>8)&0xff;
    b[2] = (v>>16)&0xff; b[3] = (v>>24)&0xff;
}
static inline uint32_t read_le32(const unsigned char *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1]<<8)
	 | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}


/* ================================================================== */
/* Server state                                                         */
/* ================================================================== */

struct State {
    bu_ipc_chan_t *chan     = nullptr;  /* to/from parent */
    bool          via_stdio = false;   /* using stdin/stdout directly */
    int           cancelled = 0;
};

static State *g_state = nullptr;   /* process-singleton */


/* ================================================================== */
/* Blocking packet write helper                                         */
/* ================================================================== */

static void
send_packet(State *st, uint8_t type,
	    const unsigned char *payload, uint32_t paylen)
{
    unsigned char hdr[IPC_HDR_SIZE];
    write_le32(hdr, RENDER_IPC_MAGIC);
    hdr[4] = type;
    write_le32(hdr + 5, paylen);

    if (st->chan) {
	bu_ipc_write(st->chan, hdr, IPC_HDR_SIZE);
	if (payload && paylen)
	    bu_ipc_write(st->chan, payload, paylen);
    } else {
	/* stdio path: use fwrite so we get proper buffering and -Werror compat */
	if (fwrite(hdr, 1, IPC_HDR_SIZE, stdout) < IPC_HDR_SIZE)
	    bu_log("rt_ipc: fwrite(hdr) failed\n");
	if (payload && paylen)
	    if (fwrite(payload, 1, paylen, stdout) < paylen)
		bu_log("rt_ipc: fwrite(payload) failed\n");
	fflush(stdout);
    }
}


/* ================================================================== */
/* Pixel callback – streams scanlines to the parent in real time       */
/* ================================================================== */

static void
pixel_cb(void *ud, int x, int y, int w, const unsigned char *rgb)
{
    State *st = (State *)ud;
    if (st->cancelled) return;

    render_ipc_pixels phdr;
    phdr.x     = (int32_t)x;
    phdr.y     = (int32_t)y;
    phdr.count = (int32_t)w;

    size_t rgb_bytes = (size_t)(w * 3);
    std::vector<unsigned char> payload(sizeof(phdr) + rgb_bytes);
    std::memcpy(payload.data(), &phdr, sizeof(phdr));
    std::memcpy(payload.data() + sizeof(phdr), rgb, rgb_bytes);

    send_packet(st, RENDER_IPC_T_PIXELS,
		payload.data(), (uint32_t)payload.size());
}


/* ================================================================== */
/* Progress callback                                                    */
/* ================================================================== */

static void
progress_cb(void *ud, int done, int total)
{
    State *st = (State *)ud;
    render_ipc_progress p{ (int32_t)done, (int32_t)total };
    send_packet(st, RENDER_IPC_T_PROGRESS,
		(const unsigned char *)&p, sizeof(p));
}


/* ================================================================== */
/* Job handler                                                          */
/* ================================================================== */

static void
handle_job(State *st,
	   const unsigned char *payload, uint32_t paylen)
{
    if (paylen < (uint32_t)sizeof(render_ipc_job)) {
	bu_log("rt_ipc: truncated JOB packet\n");
	send_packet(st, RENDER_IPC_T_ERROR, nullptr, 0);
	return;
    }

    const auto *job     = reinterpret_cast<const render_ipc_job *>(payload);
    const char *strings = reinterpret_cast<const char *>(payload + sizeof(*job));
    const char *dbfile  = strings;

    if (job->version != RENDER_IPC_VERSION) {
	bu_log("rt_ipc: protocol version mismatch (got %u, expected %u)\n",
	       (unsigned)job->version, (unsigned)RENDER_IPC_VERSION);
	send_packet(st, RENDER_IPC_T_ERROR, nullptr, 0);
	return;
    }

    /* Collect object names (follow dbfile string) */
    std::vector<const char *> objs;
    const char *p = strings + job->dbfile_len;
    for (int i = 0; i < job->nobjs; ++i) {
	objs.push_back(p);
	p += std::strlen(p) + 1;
    }

    render_ctx_t *ctx = render_ctx_create(
	dbfile,
	job->nobjs,
	job->nobjs > 0 ? objs.data() : nullptr);

    if (!ctx) {
	bu_log("rt_ipc: render_ctx_create('%s') failed\n", dbfile);
	send_packet(st, RENDER_IPC_T_ERROR, nullptr, 0);
	return;
    }

    render_opts_t *opts = render_opts_create();
    render_opts_set_size(opts, job->width, job->height);
    render_opts_set_view(opts, job->view2model, job->eye, job->viewsize);
    render_opts_set_aspect(opts, job->aspect);
    render_opts_set_lighting(opts, job->lightmodel);
    render_opts_set_threads(opts, job->nthreads);
    render_opts_set_background(opts,
			       job->background[0],
			       job->background[1],
			       job->background[2]);

    bu_log("rt_ipc: rendering %dx%d '%s' lighting=%d\n",
	   job->width, job->height, dbfile, job->lightmodel);

    int ok = render_run(ctx, opts, pixel_cb, progress_cb, nullptr, st);

    render_opts_destroy(opts);
    render_ctx_destroy(ctx);

    if (ok == BRLCAD_OK) {
	bu_log("rt_ipc: render complete\n");
	send_packet(st, RENDER_IPC_T_DONE, nullptr, 0);
    } else {
	bu_log("rt_ipc: render failed\n");
	send_packet(st, RENDER_IPC_T_ERROR, nullptr, 0);
    }
}


/* ================================================================== */
/* main read loop                                                       */
/* ================================================================== */

static void
run_loop(State *st)
{
    std::vector<unsigned char> rbuf;
    rbuf.reserve(4096);

    unsigned char chunk[4096];

    for (;;) {
	/* Read a chunk of bytes */
	bu_ssize_t n;
	if (st->chan) {
	    n = bu_ipc_read(st->chan, chunk, sizeof(chunk));
	} else {
	    n = (bu_ssize_t)fread(chunk, 1, sizeof(chunk), stdin);
	    if (n == 0) n = -1; /* EOF or error */
	}

	if (n <= 0) {
	    bu_log("rt_ipc: channel closed (n=%d)\n", (int)n);
	    break;
	}

	rbuf.insert(rbuf.end(), chunk, chunk + n);

	/* Parse and dispatch all complete packets */
	while (rbuf.size() >= IPC_HDR_SIZE) {
	    uint32_t magic  = read_le32(rbuf.data());
	    uint8_t  type   = rbuf[4];
	    uint32_t paylen = read_le32(rbuf.data() + 5);
	    size_t   total  = IPC_HDR_SIZE + paylen;

	    if (magic != RENDER_IPC_MAGIC) {
		bu_log("rt_ipc: bad magic 0x%08x — aborting\n", magic);
		return;
	    }

	    /* Need to accumulate more bytes */
	    if (rbuf.size() < total) break;

	    /* Dispatch */
	    const unsigned char *payload = rbuf.data() + IPC_HDR_SIZE;
	    switch (type) {
	    case RENDER_IPC_T_JOB:
		handle_job(st, payload, paylen);
		/* We only handle one job per process lifetime */
		return;

	    case RENDER_IPC_T_CANCEL:
		bu_log("rt_ipc: received CANCEL\n");
		st->cancelled = 1;
		return;

	    default:
		bu_log("rt_ipc: unknown packet type 0x%02x\n",
		       (unsigned)type);
	    }

	    rbuf.erase(rbuf.begin(), rbuf.begin() + (ptrdiff_t)total);
	}
    }
}


/* ================================================================== */
/* main                                                                 */
/* ================================================================== */

int
main(int argc, const char *argv[])
{
    bu_setprogname(argv[0]);
    (void)argc;

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    State st;
    g_state = &st;

    /* Determine channel source:
     *   BU_IPC_ADDR_ENVVAR set → socket or TCP transport
     *   Otherwise             → use stdin/stdout (pipe transport from uv_spawn) */
    const char *addr = getenv(BU_IPC_ADDR_ENVVAR);
    if (addr && addr[0]) {
	st.chan = bu_ipc_connect(addr);
	if (!st.chan) {
	    bu_log("rt_ipc: bu_ipc_connect('%s') failed\n", addr);
	    return 1;
	}
	st.via_stdio = false;
	bu_log("rt_ipc: connected via %s (pid %d)\n", addr, (int)bu_pid());
    } else {
	/* stdio mode: parent redirected our stdin/stdout to its pipe ends */
	st.chan      = nullptr;
	st.via_stdio = true;
	bu_log("rt_ipc: using stdin/stdout (pid %d)\n", (int)bu_pid());
    }

    run_loop(&st);

    if (st.chan)
	bu_ipc_close(st.chan);

    return st.cancelled ? 1 : 0;
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
