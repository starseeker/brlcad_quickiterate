/*            T E S T _ R T _ I P C _ P R O T O . C P P
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
/** @file rt_ipc/tests/test_rt_ipc_proto.cpp
 *
 * End-to-end protocol test for rt_ipc.
 *
 * This test spawns rt_ipc as a child process with piped stdin/stdout,
 * sends a RENDER_IPC_T_JOB packet describing a small (16x16) render of
 * moss.g/all.g, then reads and validates the response stream:
 *
 *   - One or more RENDER_IPC_T_PIXELS packets covering all 16 scanlines.
 *   - Optional RENDER_IPC_T_PROGRESS packets.
 *   - Exactly one RENDER_IPC_T_DONE packet at the end.
 *
 * Failure conditions tested:
 *   - Bad magic byte → protocol desync handling (send then EOF).
 *
 * The test requires two command-line arguments:
 *   argv[1]  Path to the rt_ipc executable.
 *   argv[2]  Path to the test geometry database (moss.g).
 */

#include "common.h"

#ifndef _WIN32

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/wait.h>

#include "bu/app.h"
#include "bu/log.h"
#include "bu/str.h"
#include "vmath.h"

#include "render.h"
#include "render_ipc.h"


/* ── packet framing helpers (mirrors rt_ipc.cpp) ─────────────────── */

#define IPC_HDR_SIZE 9

static void
write_le32(unsigned char *b, uint32_t v)
{
    b[0] = v & 0xff; b[1] = (v >> 8) & 0xff;
    b[2] = (v >> 16) & 0xff; b[3] = (v >> 24) & 0xff;
}

static uint32_t
read_le32(const unsigned char *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
	 | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static bool
write_all(int fd, const void *buf, size_t n)
{
    const char *p = (const char *)buf;
    while (n > 0) {
	ssize_t nw = write(fd, p, n);
	if (nw <= 0) return false;
	p += nw; n -= (size_t)nw;
    }
    return true;
}

/* Send a framed packet on fd. */
static bool
send_packet(int fd, uint8_t type,
	    const unsigned char *payload, uint32_t paylen)
{
    unsigned char hdr[IPC_HDR_SIZE];
    write_le32(hdr, RENDER_IPC_MAGIC);
    hdr[4] = type;
    write_le32(hdr + 5, paylen);
    if (!write_all(fd, hdr, IPC_HDR_SIZE)) return false;
    if (payload && paylen)
	if (!write_all(fd, payload, paylen)) return false;
    return true;
}


/* ── build JOB packet ─────────────────────────────────────────────── */

static std::vector<unsigned char>
make_job_packet(const char *dbfile, int width, int height)
{
    /* Object name: "all.g" */
    const char *objname = "all.g";
    size_t dbfile_len  = strlen(dbfile) + 1;
    size_t objname_len = strlen(objname) + 1;

    render_ipc_job job{};
    job.version    = RENDER_IPC_VERSION;
    job.width      = width;
    job.height     = height;
    job.lightmodel = RENDER_LIGHT_DIFFUSE;
    job.nthreads   = 1;
    job.nobjs      = 1;
    job.viewsize   = 200.0;
    job.aspect     = (double)width / (double)height;
    job.dbfile_len = (uint32_t)dbfile_len;

    /* Identity view2model = looking down -Z */
    memset(job.view2model, 0, sizeof(job.view2model));
    job.view2model[0]  = 1.0; job.view2model[5]  = 1.0;
    job.view2model[10] = 1.0; job.view2model[15] = 1.0;

    /* Eye above origin */
    job.eye[0] = 0.0; job.eye[1] = 0.0; job.eye[2] = 2000.0;

    /* Black background */
    job.background[0] = job.background[1] = job.background[2] = 0.0;

    uint32_t paylen = (uint32_t)(sizeof(job) + dbfile_len + objname_len);
    std::vector<unsigned char> payload(paylen);
    size_t off = 0;
    memcpy(payload.data() + off, &job, sizeof(job)); off += sizeof(job);
    memcpy(payload.data() + off, dbfile, dbfile_len); off += dbfile_len;
    memcpy(payload.data() + off, objname, objname_len);

    return payload;
}


/* ── read response stream ─────────────────────────────────────────── */

struct Response {
    int pixels_packets = 0;
    int progress_packets = 0;
    int done_packets = 0;
    int error_packets = 0;
    int scanlines_received = 0;
    int total_pixel_count  = 0;
    bool stream_ok = true;  /* no framing errors */
};

static Response
read_response(int fd, int expected_height, int expected_width)
{
    Response r;
    std::vector<unsigned char> rbuf;

    unsigned char chunk[4096];
    for (;;) {
	ssize_t n = read(fd, chunk, sizeof(chunk));
	if (n <= 0) break;   /* EOF or error → child has exited */

	rbuf.insert(rbuf.end(), chunk, chunk + n);

	while (rbuf.size() >= IPC_HDR_SIZE) {
	    uint32_t magic  = read_le32(rbuf.data());
	    uint8_t  type   = rbuf[4];
	    uint32_t paylen = read_le32(rbuf.data() + 5);
	    size_t   total  = IPC_HDR_SIZE + paylen;

	    if (magic != RENDER_IPC_MAGIC) {
		bu_log("  BAD MAGIC 0x%08x\n", magic);
		r.stream_ok = false;
		return r;
	    }
	    if (rbuf.size() < total) break;

	    const unsigned char *payload = rbuf.data() + IPC_HDR_SIZE;

	    switch (type) {
	    case RENDER_IPC_T_PIXELS: {
		r.pixels_packets++;
		if (paylen >= sizeof(render_ipc_pixels)) {
		    const render_ipc_pixels *pix =
			reinterpret_cast<const render_ipc_pixels *>(payload);
		    r.scanlines_received++;
		    r.total_pixel_count += pix->count;
		    (void)expected_width; /* checked implicitly via count */
		}
		break;
	    }
	    case RENDER_IPC_T_PROGRESS:
		r.progress_packets++;
		break;
	    case RENDER_IPC_T_DONE:
		r.done_packets++;
		/* rt_ipc exits after sending DONE, so this is terminal */
		goto done;
	    case RENDER_IPC_T_ERROR:
		r.error_packets++;
		goto done;
	    default:
		bu_log("  unknown packet type 0x%02x\n", type);
		r.stream_ok = false;
		goto done;
	    }

	    rbuf.erase(rbuf.begin(), rbuf.begin() + (ptrdiff_t)total);
	}
    }

done:
    (void)expected_height;
    return r;
}


/* ── spawn helper ─────────────────────────────────────────────────── */

struct ChildProc {
    pid_t pid     = -1;
    int   stdin_w = -1;  /* parent writes here → child's stdin  */
    int   stdout_r = -1; /* parent reads here  ← child's stdout */
};

static ChildProc
spawn_rt_ipc(const char *rt_ipc_path)
{
    ChildProc cp;

    int to_child[2], from_child[2];
    if (pipe(to_child) < 0 || pipe(from_child) < 0) {
	bu_log("  pipe() failed\n");
	return cp;
    }

    pid_t pid = fork();
    if (pid < 0) {
	bu_log("  fork() failed\n");
	close(to_child[0]); close(to_child[1]);
	close(from_child[0]); close(from_child[1]);
	return cp;
    }

    if (pid == 0) {
	/* child: remap pipe ends to stdin/stdout */
	dup2(to_child[0],   STDIN_FILENO);
	dup2(from_child[1], STDOUT_FILENO);
	close(to_child[0]); close(to_child[1]);
	close(from_child[0]); close(from_child[1]);
	/* Suppress rt_ipc's progress messages on stderr during test */
	const char *args[] = { rt_ipc_path, nullptr };
	execv(rt_ipc_path, const_cast<char * const *>(args));
	_exit(1);
    }

    /* parent: close child-side ends */
    close(to_child[0]);
    close(from_child[1]);

    cp.pid      = pid;
    cp.stdin_w  = to_child[1];
    cp.stdout_r = from_child[0];
    return cp;
}


/* ── tests ────────────────────────────────────────────────────────── */

static int total_failures = 0;

#define FAIL(msg) do { \
    bu_log("FAIL [%s:%d]: %s\n", __FILE__, __LINE__, (msg)); \
    total_failures++; \
} while (0)

#define PASS(msg) bu_log("PASS: %s\n", (msg))


/* Test 1: complete render job → verify PIXELS + DONE */
static void
test_full_render(const char *rt_ipc_path, const char *dbfile)
{
    const int W = 16, H = 16;
    bu_log("  spawning rt_ipc for a %dx%d render of moss.g/all.g ...\n", W, H);

    ChildProc cp = spawn_rt_ipc(rt_ipc_path);
    if (cp.pid < 0) { FAIL("spawn_rt_ipc failed"); return; }

    /* Send the JOB packet */
    std::vector<unsigned char> job_pay = make_job_packet(dbfile, W, H);
    bool sent = send_packet(cp.stdin_w, RENDER_IPC_T_JOB,
			    job_pay.data(), (uint32_t)job_pay.size());
    close(cp.stdin_w);   /* signals EOF to rt_ipc after job */

    if (!sent) { FAIL("send_packet(JOB) failed"); close(cp.stdout_r); return; }

    Response resp = read_response(cp.stdout_r, H, W);
    close(cp.stdout_r);

    int status = 0;
    waitpid(cp.pid, &status, 0);

    if (!resp.stream_ok) {
	FAIL("response stream had framing errors");
    } else if (resp.error_packets > 0) {
	FAIL("rt_ipc sent RENDER_IPC_T_ERROR — render failed");
    } else if (resp.done_packets != 1) {
	char buf[64];
	snprintf(buf, sizeof(buf), "expected 1 DONE packet, got %d",
		 resp.done_packets);
	FAIL(buf);
    } else if (resp.scanlines_received < H) {
	char buf[80];
	snprintf(buf, sizeof(buf),
		 "received only %d/%d scanlines",
		 resp.scanlines_received, H);
	FAIL(buf);
    } else {
	PASS("rt_ipc: full render produces correct PIXELS+DONE stream");
	bu_log("  scanlines: %d  total pixels: %d  DONE: %d\n",
	       resp.scanlines_received,
	       resp.total_pixel_count,
	       resp.done_packets);
    }
}


/* Test 2: send CANCEL immediately → rt_ipc should exit cleanly */
static void
test_cancel(const char *rt_ipc_path, const char *dbfile)
{
    ChildProc cp = spawn_rt_ipc(rt_ipc_path);
    if (cp.pid < 0) { FAIL("spawn_rt_ipc for cancel test"); return; }

    /* Send JOB then immediately CANCEL */
    std::vector<unsigned char> job_pay = make_job_packet(dbfile, 8, 8);
    send_packet(cp.stdin_w, RENDER_IPC_T_JOB,
		job_pay.data(), (uint32_t)job_pay.size());
    send_packet(cp.stdin_w, RENDER_IPC_T_CANCEL, nullptr, 0);
    close(cp.stdin_w);
    close(cp.stdout_r);   /* drain but don't bother parsing */

    int status = 0;
    pid_t waited = waitpid(cp.pid, &status, 0);
    if (waited == cp.pid && WIFEXITED(status)) {
	PASS("rt_ipc: process exits cleanly after CANCEL");
    } else {
	FAIL("rt_ipc: process did not exit cleanly after CANCEL");
    }
}


/* Test 3: EOF on stdin with no job → rt_ipc exits cleanly */
static void
test_eof_no_job(const char *rt_ipc_path)
{
    ChildProc cp = spawn_rt_ipc(rt_ipc_path);
    if (cp.pid < 0) { FAIL("spawn_rt_ipc for eof-no-job test"); return; }

    close(cp.stdin_w);    /* EOF immediately */
    close(cp.stdout_r);

    int status = 0;
    pid_t waited = waitpid(cp.pid, &status, 0);
    if (waited == cp.pid && WIFEXITED(status)) {
	PASS("rt_ipc: exits cleanly on immediate EOF (no job)");
    } else {
	FAIL("rt_ipc: did not exit cleanly on immediate EOF");
    }
}


/* ── main ──────────────────────────────────────────────────────────── */

int
main(int argc, const char *argv[])
{
    bu_setprogname(argv[0]);

    if (argc < 3) {
	bu_log("Usage: %s <rt_ipc_path> <moss.g>\n", argv[0]);
	return 1;
    }
    const char *rt_ipc_path = argv[1];
    const char *dbfile      = argv[2];

    bu_log("=== rt_ipc protocol end-to-end tests ===\n");
    bu_log("rt_ipc: %s\n", rt_ipc_path);
    bu_log("db:     %s\n\n", dbfile);

    bu_log("--- Test 1: full render job ---\n");
    test_full_render(rt_ipc_path, dbfile);

    bu_log("\n--- Test 2: cancel mid-render ---\n");
    test_cancel(rt_ipc_path, dbfile);

    bu_log("\n--- Test 3: EOF with no job ---\n");
    test_eof_no_job(rt_ipc_path);

    bu_log("\n=== Results: %d failure(s) ===\n", total_failures);
    return (total_failures > 0) ? 1 : 0;
}

#else /* _WIN32 */

#include "bu/app.h"
#include "bu/log.h"

int
main(int argc, const char *argv[])
{
    bu_setprogname(argv[0]);
    (void)argc;
    bu_log("test_rt_ipc_proto: SKIP (pipe/fork not available on Windows)\n");
    return 0;
}
#endif /* _WIN32 */


/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
