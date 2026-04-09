/*              R E G R E S S _ R E M R T . C P P
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
/** @file regress/remrt/regress_remrt.cpp
 *
 * Regression test for remrt (network distributed ray-tracing).
 *
 * Two sub-tests are executed:
 *
 *   Sub-test 1 — token-authenticated workers (full render):
 *     Starts remrt in -M mode feeding the exact m35 benchmark view script
 *     with three local rtsrv workers that each supply the session token via
 *     "-S <token>".  The assembled 512×512 .pix output is compared against
 *     bench/ref/m35.pix using a pixel-exact comparison to verify assembly
 *     correctness against the 1991 BRL-CAD benchmark reference image.
 *
 *   Sub-test 2 — token-less workers (backward-compatibility render):
 *     Starts remrt without -M (uses default az/el) for a quick 64×64 render
 *     with three local rtsrv workers that do NOT supply a token.  Verifies
 *     that remrt still accepts and uses unauthenticated workers and that the
 *     output file has the expected size.
 *
 * Both sub-tests start remrt with the benchmark flags (-B -H 0 -J 0) for
 * deterministic rendering identical to the standard BRL-CAD benchmark.
 *
 * A ".remrtrc" file is written to the test's working directory
 * (CMAKE_CURRENT_BINARY_DIR, set via the WORKING_DIRECTORY CTest property)
 * before each remrt invocation.  Because remrt checks ".remrtrc" in its
 * current directory first — and the child process inherits the parent's
 * working directory — no HOME manipulation is required.  The file registers
 * "localhost" as a passive host with ht_where=HT_CD pointing to the
 * directory containing m35.g, causing remrt to send MSG_CD + MSG_DIRBUILD
 * to each rtsrv worker without requiring SSH.
 *
 * Usage:
 *   regress_remrt --remrt PATH --rtsrv PATH --pixcmp PATH \
 *                 --m35g PATH --refpix PATH
 *
 * Exits 0 on full success, non-zero if either sub-test fails.
 */

#include "common.h"

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef HAVE_SYS_STAT_H
#  include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include "bio.h"

#include "bu/app.h"
#include "bu/file.h"
#include "bu/log.h"
#include "bu/process.h"
#include "bu/snooze.h"
#include "bu/str.h"
#include "bu/time.h"
#include "bu/vls.h"


/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

/* Maximum seconds to wait for remrt to print its port and session token. */
static const int PORT_TOKEN_WAIT_SEC = 30;

/* Maximum seconds to wait for remrt to complete each sub-test. */
static const int REMRT_WAIT_SEC = 500;

/* Maximum seconds to wait for each rtsrv worker to exit after remrt ends. */
static const int RTSRV_WAIT_SEC = 30;

/* Number of local rtsrv workers to launch per sub-test. */
static const int N_WORKERS = 3;

/* The m35 benchmark view script (identical to bench/run.sh "bench m35" invocation). */
static const char M35_SCRIPT[] =
    "viewsize 6.787387985e+03;\n"
    "eye_pt 3.974533127e+03 1.503320754e+03 2.874633221e+03;\n"
    "viewrot"
    " -5.527838919e-01 8.332423558e-01 1.171090926e-02 0.000000000e+00"
    " -4.815587087e-01 -3.308784486e-01 8.115544728e-01 0.000000000e+00"
    "  6.800964482e-01 4.429747496e-01 5.841593895e-01 0.000000000e+00"
    "  0.000000000e+00 0.000000000e+00 0.000000000e+00 1.000000000e+00;\n"
    "start 0;\n"
    "end;\n";

/* --------------------------------------------------------------------------
 * State shared between the stderr reader thread and the main thread.
 * -------------------------------------------------------------------------- */

struct RemrtDetected {
    std::mutex mtx;
    std::condition_variable cv;
    int port = 0;
    std::string token;
    bool ready = false;   /* port AND token found */

    void reset() {
	port = 0;
	token.clear();
	ready = false;
    }
};

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/* Trim trailing whitespace (newline, CR, space, tab) from a string in-place. */
static void
rtrim(char *buf)
{
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
		     buf[n-1] == ' '  || buf[n-1] == '\t'))
	buf[--n] = '\0';
}


/*
 * Thread function: drain a process's stderr pipe, parse lines to extract
 * the TCP port and session token printed by remrt, and signal the condition
 * variable once both have been found.
 *
 * Continues reading until EOF so the pipe never fills up.
 */
static void
read_remrt_stderr(struct bu_process *proc, RemrtDetected *det, std::string *log_out)
{
    /* Use raw fd + dup so that fclose() does not close the original fd
     * and cause a double-close in bu_process_wait_n().                 */
    int raw_fd = bu_process_fileno(proc, BU_PROCESS_STDERR);
    if (raw_fd < 0)
	return;

    int dup_fd = dup(raw_fd);
    if (dup_fd < 0)
	return;

    FILE *ferr = fdopen(dup_fd, "rb");
    if (!ferr) {
	close(dup_fd);
	return;
    }

    char buf[4096];
    while (bu_fgets(buf, sizeof(buf), ferr)) {
	if (log_out)
	    log_out->append(buf);

	/* "Listening at TCP port NNNNN" */
	const char *p = strstr(buf, "Listening at TCP port ");
	if (p) {
	    int port_val = atoi(p + strlen("Listening at TCP port "));
	    if (port_val > 0) {
		std::lock_guard<std::mutex> lk(det->mtx);
		det->port = port_val;
	    }
	}

	/* Fallback: "Assigned LIBPKG permport NNNNN" */
	if (!det->port) {
	    p = strstr(buf, "Assigned LIBPKG permport ");
	    if (p) {
		int port_val = atoi(p + strlen("Assigned LIBPKG permport "));
		if (port_val > 0) {
		    std::lock_guard<std::mutex> lk(det->mtx);
		    det->port = port_val;
		}
	    }
	}

	/* "Session token: XXXXXXXXXX..." */
	p = strstr(buf, "Session token: ");
	if (p) {
	    char tok[512];
	    bu_strlcpy(tok, p + strlen("Session token: "), sizeof(tok));
	    rtrim(tok);
	    if (tok[0] != '\0') {
		std::lock_guard<std::mutex> lk(det->mtx);
		det->token = tok;
	    }
	}

	/* Signal once both are available. */
	{
	    std::lock_guard<std::mutex> lk(det->mtx);
	    if (det->port && !det->token.empty() && !det->ready) {
		det->ready = true;
		det->cv.notify_all();
	    }
	}
    }

    fclose(ferr);   /* closes dup_fd only, not the original raw_fd */
}


/*
 * Thread function: drain stdout of a process (prevents pipe buffer full-block
 * when the child writes a lot of output but the parent does not read it).
 */
static void
drain_stdout(struct bu_process *proc)
{
    /* Use raw fd (not fdopen) to avoid double-close with bu_process_wait_n. */
    int fd = bu_process_fileno(proc, BU_PROCESS_STDOUT);
    if (fd < 0)
	return;
    char buf[4096];
    while (read(fd, buf, sizeof(buf)) > 0) {}
}


/*
 * Drain both stdout and stderr of a process into strings (used for rtsrv
 * workers that run with -d, which produces verbose output).
 */
static void
drain_one_channel(struct bu_process *proc, bu_process_io_t channel)
{
    /* Read using the raw fd so that bu_process_wait_n() can still
     * close() the fd later without a double-close / heap corruption. */
    int fd = bu_process_fileno(proc, channel);
    if (fd < 0)
	return;
    char buf[4096];
    while (read(fd, buf, sizeof(buf)) > 0) {}
}

/*
 * Drain both stdout and stderr of a process concurrently.
 *
 * rtsrv in -d (foreground) mode generates several hundred KB of stderr
 * output (bu_log messages from prep/render).  The Linux pipe buffer is
 * only 64 KB.  If we drain stdout to EOF first and only then drain stderr,
 * the stderr pipe fills and the worker blocks before it can flush stdout,
 * causing a deadlock.  Running both drainers in parallel avoids this.
 *
 * We use the raw file descriptor (not fdopen) so that bu_process_wait_n()
 * can still close() the fds without a double-close / heap corruption.
 */
static void
drain_process(struct bu_process *proc)
{
    std::thread t_out(drain_one_channel, proc, BU_PROCESS_STDOUT);
    std::thread t_err(drain_one_channel, proc, BU_PROCESS_STDERR);
    t_out.join();
    t_err.join();
}



/*
 * Return the directory portion of a file path (everything up to and including
 * the last path separator, minus the trailing separator).  Result is written
 * into buf.  Returns buf.
 */
static char *
path_dirname(const char *path, char *buf, size_t bufsz)
{
    const char *last_sep = NULL;
    for (const char *p = path; *p; p++) {
	if (*p == '/' || *p == '\\')
	    last_sep = p;
    }
    if (!last_sep) {
	bu_strlcpy(buf, ".", bufsz);
    } else {
	size_t n = (size_t)(last_sep - path);
	if (n == 0) n = 1;   /* preserve leading "/" */
	if (n >= bufsz) n = bufsz - 1;
	memcpy(buf, path, n);
	buf[n] = '\0';
    }
    return buf;
}


/*
 * Write a ".remrtrc" into the current working directory.
 *
 * remrt(1) checks ".remrtrc" in its current directory first, before looking
 * in $HOME.  Because child processes inherit the parent's working directory,
 * this file will be found by remrt without any environment variable
 * manipulation.
 *
 * The file registers "localhost" as a passive host with ht_where=HT_CD
 * pointing to db_dir.  This causes remrt to send MSG_CD <db_dir> followed
 * by MSG_DIRBUILD to each connecting rtsrv worker, so rtsrv can open the
 * geometry file regardless of its own working directory.
 *
 * Returns 0 on success, -1 on failure.
 */
static int
write_remrtrc(const char *db_dir)
{
    char cwd[MAXPATHLEN] = {0};
    bu_dir(cwd, sizeof(cwd), BU_DIR_CURR, NULL);

    struct bu_vls rcpath = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&rcpath, "%s/.remrtrc", cwd);

    FILE *fp = fopen(bu_vls_cstr(&rcpath), "w");
    if (!fp) {
	fprintf(stderr, "regress_remrt: cannot write %s: %s\n",
		bu_vls_cstr(&rcpath), strerror(errno));
	bu_vls_free(&rcpath);
	return -1;
    }
    /* Syntax: host <name> <when> <where> <path>
     * passive  — remrt never auto-launches; waits for workers to connect.
     * cd       — remrt sends MSG_CD <db_dir> before MSG_DIRBUILD so rtsrv
     *            can open the geometry regardless of its own cwd.          */
    fprintf(fp, "host localhost passive cd %s\n", db_dir);
    fclose(fp);
    return 0;
}


/*
 * Remove the ".remrtrc" written by write_remrtrc() from the current
 * working directory.
 */
static void
cleanup_remrtrc(void)
{
    char cwd[MAXPATHLEN] = {0};
    bu_dir(cwd, sizeof(cwd), BU_DIR_CURR, NULL);

    struct bu_vls rcpath = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&rcpath, "%s/.remrtrc", cwd);
    bu_file_delete(bu_vls_cstr(&rcpath));
    bu_vls_free(&rcpath);
}


/* --------------------------------------------------------------------------
 * Sub-test infrastructure
 * -------------------------------------------------------------------------- */

struct TestOptions {
    std::string remrt_exe;
    std::string rtsrv_exe;
    std::string pixcmp_exe;
    std::string m35g_path;
    std::string refpix_path;
};


/*
 * Run one remrt + N_WORKERS rtsrv sub-test.
 *
 * Parameters
 *   opts        – paths to executables and geometry/reference files
 *   use_script  – if true: pass -M and pipe M35_SCRIPT to remrt's stdin
 *                 if false: use default az/el (no -M, no stdin)
 *   image_size  – pixel width/height of the render (e.g. 512 or 64)
 *   out_pix     – path to write the output .pix file
 *   use_token   – if true: each rtsrv worker gets -S <token>
 *   label       – short label printed in diagnostic messages
 *   remrt_log   – if non-NULL, receives the complete remrt stderr log
 *
 * Returns 0 on success, non-zero on failure.
 */
static int
run_subtest(const TestOptions &opts,
	    bool use_script,
	    int image_size,
	    const std::string &out_pix,
	    bool use_token,
	    const char *label,
	    std::string *remrt_log = NULL)
{
    fprintf(stderr, "\n=== remrt regression: %s ===\n", label);

    /* Compute the directory that contains m35.g so we can write .remrtrc. */
    char db_dir[MAXPATHLEN];
    path_dirname(opts.m35g_path.c_str(), db_dir, sizeof(db_dir));

    /* Write .remrtrc into the current working directory.  remrt checks
     * ".remrtrc" in its current dir first (before $HOME/.remrtrc), and
     * inherits this process's working directory, so no HOME manipulation
     * is needed.  bu_dir(BU_DIR_CURR) provides the path portably.       */
    if (write_remrtrc(db_dir) != 0) {
	fprintf(stderr, "regress_remrt: failed to set up .remrtrc\n");
	return 1;
    }

    /* ------------------------------------------------------------------ */
    /* Build remrt argv.                                                    */
    /* ------------------------------------------------------------------ */
    char size_str[32];
    snprintf(size_str, sizeof(size_str), "%d", image_size);

    std::vector<const char *> remrt_argv;
    remrt_argv.push_back(opts.remrt_exe.c_str());
    remrt_argv.push_back("-B");          /* benchmark mode: no randomness    */
    if (use_script)
	remrt_argv.push_back("-M");      /* read view matrix from stdin      */
    remrt_argv.push_back("-s");
    remrt_argv.push_back(size_str);
    remrt_argv.push_back("-H");
    remrt_argv.push_back("0");           /* no hypersample                   */
    remrt_argv.push_back("-J");
    remrt_argv.push_back("0");           /* no jitter                        */
    remrt_argv.push_back("-o");
    remrt_argv.push_back(out_pix.c_str());
    remrt_argv.push_back(opts.m35g_path.c_str());
    remrt_argv.push_back("all.g");
    remrt_argv.push_back(NULL);

    /* ------------------------------------------------------------------ */
    /* Launch remrt.                                                        */
    /* ------------------------------------------------------------------ */
    struct bu_process *remrt_proc = NULL;
    bu_process_create(&remrt_proc, remrt_argv.data(), BU_PROCESS_DEFAULT);

    if (!remrt_proc) {
	fprintf(stderr, "regress_remrt: failed to start remrt\n");
	cleanup_remrtrc();
	return 1;
    }

    /* ------------------------------------------------------------------ */
    /* Start threads: (1) drain remrt stdout, (2) read remrt stderr.       */
    /* ------------------------------------------------------------------ */
    RemrtDetected det;
    std::string remrt_stderr_log;

    std::thread stdout_drain_thr(drain_stdout, remrt_proc);
    std::thread stderr_read_thr(read_remrt_stderr, remrt_proc, &det, &remrt_stderr_log);

    /* ------------------------------------------------------------------ */
    /* Wait for remrt to print its port and session token.                  */
    /* ------------------------------------------------------------------ */
    int port = 0;
    std::string session_token;
    {
	std::unique_lock<std::mutex> lk(det.mtx);
	bool ok = det.cv.wait_for(lk,
				  std::chrono::seconds(PORT_TOKEN_WAIT_SEC),
				  [&det] { return det.ready; });
	if (!ok) {
	    /* Timed out; try to use whatever we have so far. */
	    fprintf(stderr, "regress_remrt [%s]: timeout waiting for remrt port/token\n"
		    "  stderr so far:\n%s\n", label, remrt_stderr_log.c_str());
	}
	port = det.port;
	session_token = det.token;
    }

    if (port <= 0) {
	fprintf(stderr, "regress_remrt [%s]: could not detect remrt listen port\n", label);
	/* Kill remrt so threads can finish. */
	bu_process_wait_n(&remrt_proc, 1000000 /* us */);
	stdout_drain_thr.join();
	stderr_read_thr.join();
	cleanup_remrtrc();
	return 1;
    }

    fprintf(stderr, "remrt port=%d  token=%s\n",
	    port, session_token.empty() ? "(none)" : session_token.c_str());

    /* ------------------------------------------------------------------ */
    /* If using -M, write the view script to remrt's stdin and close it.   */
    /* This unblocks remrt's eat_script() so do_work() can start.          */
    /* ------------------------------------------------------------------ */
    if (use_script) {
	FILE *fin = bu_process_file_open(remrt_proc, BU_PROCESS_STDIN);
	if (fin) {
	    fputs(M35_SCRIPT, fin);
	    bu_process_file_close(remrt_proc, BU_PROCESS_STDIN);
	} else {
	    fprintf(stderr, "regress_remrt [%s]: cannot open remrt stdin\n", label);
	}
    }

    /* ------------------------------------------------------------------ */
    /* Launch N_WORKERS rtsrv processes.                                    */
    /* ------------------------------------------------------------------ */
    char port_str[32];
    snprintf(port_str, sizeof(port_str), "%d", port);

    std::vector<struct bu_process *> worker_procs;
    std::vector<std::thread> worker_drainers;

    for (int i = 0; i < N_WORKERS; i++) {
	std::vector<const char *> rtsrv_argv;
	rtsrv_argv.push_back(opts.rtsrv_exe.c_str());
	rtsrv_argv.push_back("-d");   /* foreground (no daemonize) — required
				       * so we can wait() for the process    */
	if (use_token && !session_token.empty()) {
	    rtsrv_argv.push_back("-S");
	    rtsrv_argv.push_back(session_token.c_str());
	}
	rtsrv_argv.push_back("localhost");
	rtsrv_argv.push_back(port_str);
	rtsrv_argv.push_back(NULL);

	struct bu_process *wp = NULL;
	bu_process_create(&wp, rtsrv_argv.data(), BU_PROCESS_DEFAULT);
	if (!wp) {
	    fprintf(stderr, "regress_remrt [%s]: failed to start rtsrv worker %d\n",
		    label, i);
	    /* Continue — other workers may compensate. */
	    continue;
	}
	worker_procs.push_back(wp);

	/* Drain each worker's output in a background thread to prevent
	 * pipe-buffer deadlocks when -d produces verbose output.           */
	worker_drainers.emplace_back(drain_process, wp);
    }

    if (worker_procs.empty()) {
	fprintf(stderr, "regress_remrt [%s]: no rtsrv workers started\n", label);
	bu_process_wait_n(&remrt_proc, 1000000 /* us */);
	stdout_drain_thr.join();
	stderr_read_thr.join();
	cleanup_remrtrc();
	return 1;
    }

    /* ------------------------------------------------------------------ */
    /* Wait for remrt to finish.                                            */
    /* ------------------------------------------------------------------ */
    int remrt_status = bu_process_wait_n(&remrt_proc,
					 REMRT_WAIT_SEC * 1000000 /* us */);

    /* Join the stderr/stdout reader threads. */
    stdout_drain_thr.join();
    stderr_read_thr.join();

    /* Join worker drain threads FIRST.  Workers exit when remrt closes the
     * PKG connection (which happens when remrt exits).  Joining here before
     * calling bu_process_wait_n() avoids a race where bu_process_wait_n()
     * closes a worker's fd while a drain thread is still read()-ing it; if
     * that fd number were then reused by the next sub-test, the stale drain
     * thread would silently drain the new process's pipe.               */
    for (auto &thr : worker_drainers)
	thr.join();

    /* Now all drain threads have finished — it is safe to call
     * bu_process_wait_n() which closes the raw fds.                     */
    for (size_t i = 0; i < worker_procs.size(); i++) {
	bu_process_wait_n(&worker_procs[i], RTSRV_WAIT_SEC * 1000000 /* us */);
    }

    cleanup_remrtrc();

    if (remrt_status != 0) {
	fprintf(stderr, "regress_remrt [%s]: remrt exited with status %d\n",
		label, remrt_status);
	return 1;
    }

    /* ------------------------------------------------------------------ */
    /* Verify the output file exists.                                       */
    /* remrt appends the frame number to the -o argument, so the actual     */
    /* file on disk is out_pix + ".0" for frame 0.                          */
    /* ------------------------------------------------------------------ */
    std::string actual_pix = out_pix + ".0";
    if (!bu_file_exists(actual_pix.c_str(), NULL)) {
	fprintf(stderr, "regress_remrt [%s]: output file not created: %s\n",
		label, actual_pix.c_str());
	return 1;
    }

    /* Check the file size: a valid .pix must be width*height*3 bytes. */
    long expected_size = (long)image_size * image_size * 3;
    {
	FILE *fp = fopen(actual_pix.c_str(), "rb");
	if (!fp) {
	    fprintf(stderr, "regress_remrt [%s]: cannot open output %s\n",
		    label, actual_pix.c_str());
	    return 1;
	}
	fseek(fp, 0, SEEK_END);
	long actual = ftell(fp);
	fclose(fp);
	if (actual != expected_size) {
	    fprintf(stderr,
		    "regress_remrt [%s]: output size mismatch: "
		    "expected %ld bytes, got %ld bytes\n",
		    label, expected_size, actual);
	    return 1;
	}
    }

    fprintf(stderr, "regress_remrt [%s]: output file OK (%ld bytes)\n",
	    label, expected_size);

    /* Pass the accumulated remrt stderr log back to the caller so it can
     * be printed when a pixel comparison fails. */
    if (remrt_log)
	*remrt_log = remrt_stderr_log;

    return 0;
}


/*
 * Compare two raw .pix (RGB) files byte by byte.  Returns:
 *   0   – files are pixel-exact (identical)
 *   >0  – one or more bytes differ
 *
 * A detailed summary is printed to stderr so the caller can see how many
 * pixels are affected and by how much.
 */
static int
compare_pix_tolerant(const std::string &file_a,
		     const std::string &file_b)
{
    FILE *fa = fopen(file_a.c_str(), "rb");
    FILE *fb = fopen(file_b.c_str(), "rb");
    if (!fa || !fb) {
	if (fa) fclose(fa);
	if (fb) fclose(fb);
	fprintf(stderr, "regress_remrt: cannot open files for comparison\n");
	return 1;
    }

    long bad_pixels  = 0;   /* pixels where any channel differs by >1 */
    long warn_pixels = 0;   /* pixels where any channel differs by 1 */
    long pixel_no    = 0;
    int width        = 512; /* assumed from context */

    struct diff_detail { long pix; int ra,ga,ba,rb,gb,bb; };
    std::vector<diff_detail> diffs;

    int ra, ga, ba, rb, gb, bb;
    while (true) {
	ra = fgetc(fa); ga = fgetc(fa); ba = fgetc(fa);
	rb = fgetc(fb); gb = fgetc(fb); bb = fgetc(fb);
	if (ra == EOF || rb == EOF)
	    break;
	++pixel_no;

	int dr = abs(ra - rb);
	int dg = abs(ga - gb);
	int db = abs(ba - bb);
	int maxd = dr > dg ? dr : dg;
	maxd = maxd > db ? maxd : db;

	if (maxd > 1)
	    ++bad_pixels;
	else if (maxd == 1) {
	    ++warn_pixels;
	    if ((int)diffs.size() < 30)
		diffs.push_back({pixel_no-1, ra,ga,ba,rb,gb,bb});
	}
    }

    fclose(fa);
    fclose(fb);

    if (bad_pixels > 0) {
	fprintf(stderr,
		"  %ld pixel(s) differ by >1 channel value (assembly error)\n",
		bad_pixels);
    }
    if (warn_pixels > 0) {
	fprintf(stderr,
		"  %ld pixel(s) differ by exactly 1 channel value"
		" (floating-point rounding difference)\n",
		warn_pixels);
	fprintf(stderr, "  First differing pixels (pix_no x y  a_rgb  b_rgb  delta):\n");
	for (auto &d : diffs) {
	    int x = (int)(d.pix % width);
	    int y = (int)(d.pix / width);
	    fprintf(stderr, "    pix=%ld (%d,%d)  a=(%d,%d,%d) b=(%d,%d,%d)  d=(%d,%d,%d)\n",
		    d.pix, x, y,
		    d.ra, d.ga, d.ba,
		    d.rb, d.gb, d.bb,
		    d.ra-d.rb, d.ga-d.gb, d.ba-d.bb);
	}
    }
    if (bad_pixels == 0 && warn_pixels == 0) {
	fprintf(stderr, "  pixel-exact match\n");
    }

    /* Any pixel difference is a failure — the fix to ph_matrix() (calling
     * grid_setup + view_2init with the correct view parameters after MSG_MATRIX
     * arrives) eliminates the ±1 blue-channel rounding difference that was
     * previously observed for ~19 shadow-edge pixels.  The reference image was
     * generated by standalone rt with identical flags so pixel-exact parity is
     * achievable. */
    return (bad_pixels > 0 || warn_pixels > 0) ? 1 : 0;
}


/* --------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------- */

static void
usage(const char *argv0)
{
    fprintf(stderr,
	    "Usage: %s --remrt PATH --rtsrv PATH --pixcmp PATH"
	    " --m35g PATH --refpix PATH\n",
	    argv0);
}


int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);

    TestOptions opts;

    for (int i = 1; i < argc - 1; i++) {
	if (BU_STR_EQUAL(argv[i], "--remrt"))   opts.remrt_exe  = argv[++i];
	else if (BU_STR_EQUAL(argv[i], "--rtsrv"))  opts.rtsrv_exe  = argv[++i];
	else if (BU_STR_EQUAL(argv[i], "--pixcmp")) opts.pixcmp_exe = argv[++i];
	else if (BU_STR_EQUAL(argv[i], "--m35g"))   opts.m35g_path  = argv[++i];
	else if (BU_STR_EQUAL(argv[i], "--refpix")) opts.refpix_path = argv[++i];
    }

    if (opts.remrt_exe.empty() || opts.rtsrv_exe.empty() ||
	opts.m35g_path.empty() ||
	opts.refpix_path.empty()) {
	usage(argv[0]);
	return 1;
    }

    /* Verify input files exist. */
    if (!bu_file_exists(opts.remrt_exe.c_str(), NULL)) {
	fprintf(stderr, "regress_remrt: remrt not found: %s\n",
		opts.remrt_exe.c_str());
	return 1;
    }
    if (!bu_file_exists(opts.rtsrv_exe.c_str(), NULL)) {
	fprintf(stderr, "regress_remrt: rtsrv not found: %s\n",
		opts.rtsrv_exe.c_str());
	return 1;
    }
    if (!bu_file_exists(opts.m35g_path.c_str(), NULL)) {
	fprintf(stderr, "regress_remrt: m35.g not found: %s\n",
		opts.m35g_path.c_str());
	return 1;
    }
    if (!bu_file_exists(opts.refpix_path.c_str(), NULL)) {
	fprintf(stderr, "regress_remrt: reference pix not found: %s\n",
		opts.refpix_path.c_str());
	return 1;
    }

    /* Derive a suitable output directory from the test's working directory
     * (CMAKE_CURRENT_BINARY_DIR, set via WORKING_DIRECTORY ctest property).
     * This keeps output files alongside .remrtrc and simplifies distclean. */
    char cwd[MAXPATHLEN] = {0};
    bu_dir(cwd, sizeof(cwd), BU_DIR_CURR, NULL);

    /* Base paths passed to remrt via -o.  remrt appends ".N" (frame number)
     * so the actual files on disk are out_token + ".0", out_notoken + ".0". */
    std::string out_token   = std::string(cwd) + "/m35_remrt_token.pix";
    std::string out_notoken = std::string(cwd) + "/m35_remrt_notoken.pix";

    /* Remove any stale outputs (both base name and frame-suffixed) from a
     * previous run. */
    bu_file_delete(out_token.c_str());
    bu_file_delete((out_token  + ".0").c_str());
    bu_file_delete(out_notoken.c_str());
    bu_file_delete((out_notoken + ".0").c_str());

    int failures = 0;

    /* ------------------------------------------------------------------ */
    /* Sub-test 1: 512×512 render with 3 token-authenticated workers.      */
    /* Uses the exact m35 benchmark view to allow pixcmp against reference. */
    /* ------------------------------------------------------------------ */
    std::string subtest1_log;
    int rc = run_subtest(opts,
			 /* use_script */ true,
			 /* image_size */ 512,
			 out_token,
			 /* use_token  */ true,
			 "token-auth 512x512",
			 &subtest1_log);
    if (rc != 0) {
	fprintf(stderr, "FAIL: sub-test 1 (token-auth render)\n");
	failures++;
    } else {
	/* Pixel comparison against bench/ref/m35.pix.
	 * Any difference indicates a ray-tracing correctness regression.
	 * The reference image was generated by standalone rt with identical
	 * flags so pixel-exact parity is required. */
	std::string actual_token = out_token + ".0";
	fprintf(stderr, "Comparing %s with %s ...\n",
		actual_token.c_str(), opts.refpix_path.c_str());
	int cmp = compare_pix_tolerant(actual_token, opts.refpix_path);
	if (cmp != 0) {
	    fprintf(stderr,
		    "FAIL: sub-test 1 pixel comparison: output differs from %s\n"
		    "  This may indicate a ray-tracing correctness regression.\n",
		    opts.refpix_path.c_str());
	    /* Print remrt/rtsrv diagnostic output collected during the test.
	     * This includes RT_SETUP_DEBUG grid vectors and light positions
	     * forwarded by rtsrv via bu_log(), which are invaluable for
	     * comparing the initialization sequence against standalone rt. */
	    if (!subtest1_log.empty())
		fprintf(stderr, "remrt/rtsrv stderr log:\n%s\n",
			subtest1_log.c_str());
	    failures++;
	} else {
	    fprintf(stderr, "PASS: sub-test 1 (token-auth render matches reference)\n");
	}
    }

    /* ------------------------------------------------------------------ */
    /* Sub-test 2: 64×64 render with 3 token-less workers.                 */
    /* Verifies backward-compatible acceptance of unauthenticated workers. */
    /* ------------------------------------------------------------------ */
    rc = run_subtest(opts,
		     /* use_script */ false,
		     /* image_size */ 64,
		     out_notoken,
		     /* use_token  */ false,
		     "no-token 64x64");
    if (rc != 0) {
	fprintf(stderr, "FAIL: sub-test 2 (no-token backward-compat render)\n");
	failures++;
    } else {
	fprintf(stderr, "PASS: sub-test 2 (no-token backward-compat render)\n");
    }

    /* Clean up output files (base name and frame-suffixed). */
    bu_file_delete(out_token.c_str());
    bu_file_delete((out_token  + ".0").c_str());
    bu_file_delete(out_notoken.c_str());
    bu_file_delete((out_notoken + ".0").c_str());

    if (failures == 0)
	fprintf(stderr, "\nAll remrt regression sub-tests PASSED.\n");
    else
	fprintf(stderr, "\n%d remrt regression sub-test(s) FAILED.\n", failures);

    return (failures == 0) ? 0 : 1;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
