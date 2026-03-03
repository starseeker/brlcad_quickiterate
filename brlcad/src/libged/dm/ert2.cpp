/*                        E R T 2 . C P P
 * BRL-CAD
 *
 * Copyright (c) 2008-2025 United States Government as represented by
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
/** @file libged/dm/ert2.cpp
 *
 * The ert2 command: Obol-aware incremental embedded raytrace.
 *
 * ert2 is the bview_new-based counterpart to the legacy ert command for
 * use with the Obol rendering path.  It preserves the incremental scanline
 * display feature of ert by using the same TCP framebuffer server (fbserv)
 * IPC protocol that ert uses, but writes to an in-memory libdm framebuffer
 * instead of a display-manager-embedded one.
 *
 * ### Architecture
 *
 * ert2 is deliberately kept free of Qt and Obol headers.  The incremental
 * display is achieved through the same mechanisms ert uses:
 *
 *  1. A memory framebuffer is opened via fb_open("/dev/mem", w, h).
 *     When qged sets up the Obol rendering path it pre-allocates this fb
 *     and stores it in gedp->ged_fbs->fbs_fbp before calling ert2.
 *
 *  2. The framebuffer server (fbserv) is opened on a random TCP port.
 *     rt is invoked with "-F <port>" to connect to that server and stream
 *     rendered pixels as each scanline is completed.
 *
 *  3. ert2 calls _ged_run_rt() which:
 *       - spawns rt as an async subprocess
 *       - writes the view matrix (from gedp->ged_gvp) to rt's stdin
 *       - installs a QSocketNotifier (via ged_create_io_handler) to watch
 *         rt's stderr for progress / error messages
 *
 *  4. Each time rt sends a packet over TCP, qged's QFBSocket::client_handler
 *     fires (Qt main thread, queued connection).  It:
 *       a. Calls pkg_process() → fbserv protocol handlers → writes pixels
 *          to the memory fb
 *       b. Emits QFBSocket::updated()
 *
 *  5. qged's qdm_open_obol_client_handler (fbserv.cpp) connected
 *     QFBSocket::updated() to QgObolWidget::onRtPixelsUpdated().
 *     That slot reads the full fb via fb_readrect() directly into the
 *     SoSFImage buffer (using startEditing/finishEditing) and the field
 *     notification mechanism automatically schedules an Obol repaint.
 *
 *  6. When rt finishes, the subprocess io handler calls the LINGER
 *     callback (ert2_raytrace_done in QgEdApp) which:
 *       - updates the UI (abort → raytrace icon)
 *       - calls QgObolWidget::onRtDone()
 *       - closes the memory fb and fbserv
 *
 * ### Usage
 *
 *   ert2 [-V view] [-w width] [-n height] [object ...]
 *
 *   -V view      : view name (default: current view)
 *   -w width     : image width  in pixels (default: viewport width or 512)
 *   -n height    : image height in pixels (default: viewport height or 512)
 *   objects      : objects to raytrace (default: all drawn objects via stdin)
 *
 * ### BU_CLBK_DURING / BU_CLBK_LINGER
 *
 * ert2 respects the same callback protocol as ert:
 *   BU_CLBK_DURING  receives the rt process pid (int*) — fired immediately
 *                    after rt is launched.
 *   BU_CLBK_LINGER  passed to _ged_run_rt() as the end_clbk and fired when
 *                    rt's IO streams are closed (i.e., when rendering ends).
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/app.h"
#include "bu/cmd.h"
#include "bu/opt.h"
#include "bu/process.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/view_sets.h"
#include "dm.h"
#include "raytrace.h"

#include "../ged_private.h"


/* ── helpers ──────────────────────────────────────────────────────────── */

static struct bview_new *
_ert2_get_view(struct ged *gedp, const char *viewname)
{
    if (viewname && strlen(viewname)) {
	struct bview_new *nv =
	    bv_viewset_find_new(&gedp->ged_views, viewname);
	if (!nv) {
	    bu_vls_printf(gedp->ged_result_str,
			 "ert2: view '%s' not found\n", viewname);
	    return NULL;
	}
	return nv;
    }
    return gedp->ged_gvnv;
}


/* ── ged_ert2_core ────────────────────────────────────────────────────── */

extern "C" int
ged_ert2_core(struct ged *gedp, int argc, const char *argv[])
{
    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);

    bu_vls_trunc(gedp->ged_result_str, 0);

    /* ── Parse options ─────────────────────────────────────────────── */
    struct bu_vls viewname = BU_VLS_INIT_ZERO;
    int width  = 0;
    int height = 0;

    struct bu_opt_desc d[5];
    BU_OPT(d[0], "V", "view",   "name", &bu_opt_vls, &viewname, "view name");
    BU_OPT(d[1], "w", "width",  "#",    &bu_opt_int, &width,    "image width");
    BU_OPT(d[2], "n", "height", "#",    &bu_opt_int, &height,   "image height");
    BU_OPT_NULL(d[3]);

    /* skip command name */
    argc--; argv++;

    int ac = bu_opt_parse(NULL, argc, argv, d);
    if (ac < 0) {
	bu_vls_printf(gedp->ged_result_str, "ert2: option parse error\n");
	bu_vls_free(&viewname);
	return BRLCAD_ERROR;
    }

    /* ── Resolve view ──────────────────────────────────────────────── */
    struct bview_new *nv = _ert2_get_view(gedp, bu_vls_cstr(&viewname));
    bu_vls_free(&viewname);
    if (!nv) {
	bu_vls_printf(gedp->ged_result_str, "ert2: no view available\n");
	return BRLCAD_ERROR;
    }

    /* ── Defaults ──────────────────────────────────────────────────── */
    if (width <= 0 || height <= 0) {
	const struct bview_viewport *vp = bview_viewport_get(nv);
	if (width  <= 0) width  = (vp && vp->width  > 0) ? vp->width  : 512;
	if (height <= 0) height = (vp && vp->height > 0) ? vp->height : 512;
    }

    /* ── Check fbserv is wired ─────────────────────────────────────── */
    struct fbserv_obj *fbs = gedp->ged_fbs;
    if (!fbs || !fbs->fbs_is_listening) {
	bu_vls_printf(gedp->ged_result_str,
		      "ert2: framebuffer server not initialised "
		      "(no fbs_is_listening hook — is qged running?)\n");
	return BRLCAD_ERROR;
    }

    /* ── Memory framebuffer ────────────────────────────────────────── */
    /* If the caller (qged's ObolRtCtx setup) already allocated a memory
     * fb and stored it in fbs->fbs_fbp, use it directly.  Otherwise open
     * one ourselves (standalone / non-Qt use case). */
    int own_fb = 0;
    if (!fbs->fbs_fbp) {
	struct fb *memfb = fb_open("/dev/mem", width, height);
	if (!memfb) {
	    bu_vls_printf(gedp->ged_result_str,
			 "ert2: failed to open memory framebuffer\n");
	    return BRLCAD_ERROR;
	}
	fbs->fbs_fbp = memfb;
	own_fb = 1;
    }

    /* ── Open fbserv listener ──────────────────────────────────────── */
    if (!fbs->fbs_is_listening(fbs)) {
	if (fbs_open(fbs, 0) != BRLCAD_OK) {
	    bu_vls_printf(gedp->ged_result_str,
			 "ert2: could not open framebuffer server\n");
	    if (own_fb) {
		fb_close(fbs->fbs_fbp);
		fbs->fbs_fbp = NULL;
	    }
	    return BRLCAD_ERROR;
	}
    }

    /* ── Find rt executable ────────────────────────────────────────── */
    char rt_path[MAXPATHLEN];
    bu_dir(rt_path, MAXPATHLEN, BU_DIR_BIN, "rt", BU_DIR_EXT, NULL);
    if (!bu_file_exists(rt_path, NULL)) {
	bu_vls_printf(gedp->ged_result_str,
		      "ert2: rt not found at '%s'\n", rt_path);
	return BRLCAD_ERROR;
    }

    /* ── Build rt argument list ────────────────────────────────────── */
    /* Follow the exact same pattern as ert.cpp / _ged_run_rt:
     *   rt -M -F <port> -w W -n H -V aspect_ratio db.g
     * The view matrix and object list are written to rt's stdin by
     * _ged_run_rt() via _ged_rt_write(). */
    struct bu_vls wstr = BU_VLS_INIT_ZERO;
    std::vector<std::string> args;

    args.push_back(std::string(rt_path));

    /* -M: rt reads view matrix from stdin */
    args.push_back("-M");

    /* -F <port>: connect to our fbserv */
    args.push_back("-F");
    args.push_back(std::to_string(fbs->fbs_listener.fbsl_port));

    /* -w/-n: pixel dimensions */
    args.push_back("-w");
    args.push_back(std::to_string(width));
    args.push_back("-n");
    args.push_back(std::to_string(height));

    /* -V: aspect ratio */
    double aspect = (width > 0 && height > 0)
		    ? (double)width / (double)height
		    : 1.0;
    bu_vls_sprintf(&wstr, "%.14e", aspect);
    args.push_back("-V");
    args.push_back(std::string(bu_vls_cstr(&wstr)));

    /* Pass any remaining positional args as extra rt options
     * (up to the "--" separator, after which they are objects). */
    int obj_start = 0;
    for (int i = 0; i < ac; i++) {
	if (argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2] == '\0') {
	    obj_start = i + 1;
	    break;
	}
	args.push_back(std::string(argv[i]));
	obj_start = i + 1;
    }

    /* Database file */
    const char *dbfile = gedp->dbip->dbi_filename;
    if (!dbfile || !bu_file_exists(dbfile, NULL)) {
	bu_vls_printf(gedp->ged_result_str,
		      "ert2: database file not accessible\n");
	bu_vls_free(&wstr);
	return BRLCAD_ERROR;
    }
    args.push_back(std::string(dbfile));

    /* Convert to C-string array for _ged_run_rt */
    int gd_rt_cmd_len = (int)args.size();
    char **gd_rt_cmd = (char **)bu_calloc(
		    gd_rt_cmd_len + 1, sizeof(char *), "ert2 args");
    for (int j = 0; j < gd_rt_cmd_len; j++)
	gd_rt_cmd[j] = bu_strdup(args[j].c_str());
    bu_vls_free(&wstr);

    /* ── Collect BU_CLBK callbacks ─────────────────────────────────── */
    /* LINGER callback (fired by _ged_run_rt when subprocess IO ends) */
    bu_clbk_t linger_clbk = NULL;
    void     *linger_ctx  = NULL;
    ged_clbk_get(&linger_clbk, &linger_ctx, gedp, "ert2", BU_CLBK_LINGER);

    /* ── Launch rt subprocess (async, like ert) ────────────────────── */
    int rt_pid = -1;

    /* Objects after "--" go to _ged_run_rt; if none, _ged_rt_write uses
     * all drawn objects from the current view state. */
    int   obj_argc = (ac > obj_start) ? (ac - obj_start) : 0;
    const char **obj_argv = (obj_argc > 0) ? &argv[obj_start] : NULL;

    int ret = _ged_run_rt(gedp,
			  gd_rt_cmd_len, (const char **)gd_rt_cmd,
			  obj_argc, obj_argv,
			  0 /*stdout_is_txt*/,
			  &rt_pid,
			  linger_clbk, linger_ctx);

    for (int j = 0; j < gd_rt_cmd_len; j++)
	bu_free(gd_rt_cmd[j], "ert2 arg");
    bu_free(gd_rt_cmd, "ert2 args");

    if (ret != BRLCAD_OK)
	return ret;

    /* ── Fire BU_CLBK_DURING with rt pid (same as ert) ─────────────── */
    bu_clbk_t during_clbk = NULL;
    void     *during_ctx  = NULL;
    ged_clbk_get(&during_clbk, &during_ctx, gedp, "ert2", BU_CLBK_DURING);
    if (during_clbk)
	(*during_clbk)(0, NULL, &rt_pid, during_ctx);

    /* rt is now running asynchronously; pixel data will stream via fbserv
     * and the Qt layer will update the Obol texture on each packet. */
    return BRLCAD_OK;
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
