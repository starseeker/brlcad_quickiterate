/*                        E R T 3 . C P P
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
/** @file libged/dm/ert3.cpp
 *
 * The ert3 command: incremental embedded raytrace via librender + rt_ipc.
 *
 * ert3 is a drop-in replacement for ert2 that replaces the TCP-based
 * fbserv IPC channel with libuv anonymous pipes.  Each pixel scanline
 * produced by rt_ipc is written directly into the caller's memory
 * framebuffer as it arrives.
 *
 * ### Architecture (compared with ert2)
 *
 * ert2 pipeline:
 *   ert2 → rt subprocess → fbserv (TCP port) → memory fb → Obol texture
 *
 * ert3 pipeline:
 *   ert3 → rt_ipc subprocess → libuv pipe → memory fb → display callback
 *
 * Key improvements over ert2 / fbserv:
 *
 *  - No TCP stack: no port allocation, no firewall restrictions.
 *  - Simpler process lifecycle: rt_ipc has no "mode" (just listen/respond).
 *  - Killable: uv_process_kill() terminates the child cleanly at any time.
 *  - Extensible: adding new rendering options requires only updating
 *    render_opts (no changes to protocol packet layout needed thanks to the
 *    length-framed render_ipc.h format).
 *  - UI-ready: the RENDER_LIGHT_* constants are directly mappable to
 *    check-button states in a Qt preferences dialog.
 *
 * ### Rendering option flags exposed to the UI (future check-buttons)
 *
 *   RENDER_LIGHT_NORMALS  — Surface normals only (fast, good for geometry QC)
 *   RENDER_LIGHT_DIFFUSE  — Diffuse shading from eye (default)
 *   RENDER_LIGHT_FULL     — Full Phong model (shadows, materials)
 *
 * ### Usage
 *
 *   ert3 [-V view] [-w width] [-n height] [-l lighting] [--] [objects...]
 *
 *   -V view      : view name (default: current view)
 *   -w width     : image width  in pixels (default: viewport width or 512)
 *   -n height    : image height in pixels (default: viewport height or 512)
 *   -l lighting  : lighting model:  0=normals 1=diffuse(default) 2=full
 *   objects      : objects to raytrace (default: all drawn objects)
 *
 * ### BU_CLBK_DURING / BU_CLBK_LINGER
 *
 * The same callback protocol as ert / ert2 is used:
 *   BU_CLBK_DURING  receives the rt_ipc process pid (int*).
 *   BU_CLBK_LINGER  fired when rendering ends (pipe closed by rt_ipc).
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
#include "ged/database.h"
#include "bv/util.h"
#include "bv/view_sets.h"
#include "dm.h"
#include "raytrace.h"
#include "render.h"

#include "../ged_private.h"


/* ── helpers ──────────────────────────────────────────────────────────── */

static struct bview_new *
_ert3_get_view(struct ged *gedp, const char *viewname)
{
    if (viewname && strlen(viewname)) {
	struct bview_new *nv =
	    bv_viewset_find_new(&gedp->ged_views, viewname);
	if (!nv) {
	    bu_vls_printf(gedp->ged_result_str,
			 "ert3: view '%s' not found\n", viewname);
	    return NULL;
	}
	return nv;
    }
    return gedp->ged_gvnv;
}


/* ── ged_ert3_core ────────────────────────────────────────────────────── */

extern "C" int
ged_ert3_core(struct ged *gedp, int argc, const char *argv[])
{
    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);

    bu_vls_trunc(gedp->ged_result_str, 0);

    /* ── Parse options ─────────────────────────────────────────────── */
    struct bu_vls viewname = BU_VLS_INIT_ZERO;
    int width      = 0;
    int height     = 0;
    int lightmodel = RENDER_LIGHT_DIFFUSE;

    struct bu_opt_desc d[6];
    BU_OPT(d[0], "V", "view",     "name", &bu_opt_vls, &viewname,   "view name");
    BU_OPT(d[1], "w", "width",    "#",    &bu_opt_int, &width,      "image width");
    BU_OPT(d[2], "n", "height",   "#",    &bu_opt_int, &height,     "image height");
    BU_OPT(d[3], "l", "lighting", "#",    &bu_opt_int, &lightmodel, "lighting model");
    BU_OPT_NULL(d[4]);

    /* skip command name */
    argc--; argv++;

    int ac = bu_opt_parse(NULL, argc, argv, d);
    if (ac < 0) {
	bu_vls_printf(gedp->ged_result_str, "ert3: option parse error\n");
	bu_vls_free(&viewname);
	return BRLCAD_ERROR;
    }

    /* ── Resolve view ──────────────────────────────────────────────── */
    struct bview_new *nv = _ert3_get_view(gedp, bu_vls_cstr(&viewname));
    bu_vls_free(&viewname);
    if (!nv) {
	bu_vls_printf(gedp->ged_result_str, "ert3: no view available\n");
	return BRLCAD_ERROR;
    }

    /* ── Defaults ──────────────────────────────────────────────────── */
    if (width <= 0 || height <= 0) {
	const struct bview_viewport *vp = bview_viewport_get(nv);
	if (width  <= 0) width  = (vp && vp->width  > 0) ? vp->width  : 512;
	if (height <= 0) height = (vp && vp->height > 0) ? vp->height : 512;
    }

    /* Clamp lighting model to valid range. */
    if (lightmodel < RENDER_LIGHT_NORMALS || lightmodel > RENDER_LIGHT_FULL)
	lightmodel = RENDER_LIGHT_DIFFUSE;

    /* ── Verify the memory framebuffer is available ────────────────── */
    struct fbserv_obj *fbs = gedp->ged_fbs;
    if (!fbs || !fbs->fbs_fbp) {
	bu_vls_printf(gedp->ged_result_str,
		      "ert3: no memory framebuffer available "
		      "(set gedp->ged_fbs->fbs_fbp before calling ert3)\n");
	return BRLCAD_ERROR;
    }

    /* ── Locate rt_ipc executable ──────────────────────────────────── */
    char rt_ipc_path[MAXPATHLEN];
    bu_dir(rt_ipc_path, MAXPATHLEN, BU_DIR_BIN, "rt_ipc", BU_DIR_EXT, NULL);
    if (!bu_file_exists(rt_ipc_path, NULL)) {
	/* Fall back to the build directory search. */
	bu_dir(rt_ipc_path, MAXPATHLEN, BU_DIR_BIN, "rt_ipc", NULL);
    }
    if (!bu_file_exists(rt_ipc_path, NULL)) {
	bu_vls_printf(gedp->ged_result_str,
		      "ert3: rt_ipc not found (tried '%s')\n", rt_ipc_path);
	return BRLCAD_ERROR;
    }

    /* ── Database file ─────────────────────────────────────────────── */
    const char *dbfile = gedp->dbip->dbi_filename;
    if (!dbfile || !bu_file_exists(dbfile, NULL)) {
	bu_vls_printf(gedp->ged_result_str,
		      "ert3: database file not accessible\n");
	return BRLCAD_ERROR;
    }

    /* ── Build render options from current view ────────────────────── */
    render_opts_t *opts = render_opts_create();
    render_opts_set_size(opts, width, height);
    render_opts_set_lighting(opts, lightmodel);
    render_opts_set_threads(opts, 0 /* all CPUs */);

    /* Extract view2model and eye from the bview. */
    {
	struct bview *bv = (struct bview *)nv;   /* bview_new IS-A bview */
	double aspect = (height > 0) ? (double)width / (double)height : 1.0;
	render_opts_set_view(opts,
			     (const double *)bv->gv_view2model,
			     (const double *)bv->gv_eye_pos,
			     bv->gv_size);
	render_opts_set_aspect(opts, aspect);
    }

    /* ── Collect object names from drawn geometry or command line ──── */
    /* When no positional arguments are given, fall back to the currently
     * drawn objects — the same behaviour as ert / ert2. */
    int   obj_argc = ac;
    const char **obj_argv = (ac > 0) ? argv : NULL;

    /* Drawn-object fallback (mirrors the ert.cpp / nirt.cpp pattern). */
    char **drawn_buf = NULL;
    if (obj_argc == 0) {
	int ndrawn = ged_who_argc(gedp);
	if (ndrawn == 0) {
	    bu_vls_printf(gedp->ged_result_str,
			 "ert3: no objects displayed\n");
	    render_opts_destroy(opts);
	    return BRLCAD_ERROR;
	}
	drawn_buf = (char **)bu_calloc(ndrawn + 1, sizeof(char *), "ert3 drawn buf");
	obj_argc  = ged_who_argv(gedp, drawn_buf,
				 (const char **)(drawn_buf + ndrawn + 1));
	obj_argv  = (const char **)drawn_buf;
    }

    /* ── Create a render_ctx for this job ───────────────────────────── */
    /* render_ctx_create opens the .g database and preps the geometry BVH.
     * Ownership is transferred to the Qt handler via Ert3JobCtx. */
    render_ctx_t *ctx = render_ctx_create(dbfile, obj_argc,
					  (const char **)obj_argv);
    /* drawn_buf is only used by render_ctx_create; free it now. */
    if (drawn_buf)
	bu_free(drawn_buf, "ert3 drawn buf");

    if (!ctx) {
	bu_vls_printf(gedp->ged_result_str,
		      "ert3: failed to open database '%s'\n", dbfile);
	render_opts_destroy(opts);
	return BRLCAD_ERROR;
    }

    /* ── Collect BU_CLBK callbacks ─────────────────────────────────── */
    bu_clbk_t linger_clbk = NULL;
    void     *linger_ctx  = NULL;
    ged_clbk_get(&linger_clbk, &linger_ctx, gedp, "ert3", BU_CLBK_LINGER);

    /* ── Spawn rt_ipc and submit the job ────────────────────────────── */
    /* For the full async flow (requiring a running libuv event loop from
     * the application), we need to delegate spawn/receive to the Qt layer.
     * Here we simply record the rt_ipc_path, opts, and ctx on the fbs
     * object's client data so that the Qt layer can pick them up.
     *
     * The Qt side (libqtcad or qged) should then call:
     *   render_ipc_client_spawn(cli, rt_ipc_path, loop);
     *   render_ipc_client_submit(cli, ctx, opts);
     *
     * and forward pixel callbacks to fb_write() on the memory fb.
     *
     * This approach cleanly separates the toolkit-agnostic job description
     * (built here in libged) from the toolkit-specific event loop
     * integration (handled in the Qt layer).
     */
    if (fbs->fbs_open_client_handler) {
	/* Signal the Qt layer that a new ert3 job is ready.
	 * The client data points to an Ert3JobCtx populated below. */
	struct Ert3JobCtx *jctx = (struct Ert3JobCtx *)bu_malloc(
	    sizeof(*jctx), "ert3 job ctx");
	bu_strlcpy(jctx->rt_ipc_path, rt_ipc_path, MAXPATHLEN);
	jctx->ctx         = ctx;
	jctx->opts        = opts;
	jctx->linger_clbk = linger_clbk;
	jctx->linger_ctx  = linger_ctx;

	/* Store the job context where the Qt handler can find it. */
	void *old_clientData = fbs->fbs_clientData;
	fbs->fbs_clientData  = (void *)jctx;

	/* Invoke the open-client handler — this normally spawns rt_ipc
	 * and connects the pixel pipe.  The index (0) is a placeholder. */
	fbs->fbs_open_client_handler(fbs, 0, (void *)jctx);

	/* Fire BU_CLBK_DURING with a stub pid so the caller can update its UI. */
	bu_clbk_t during_clbk = NULL;
	void     *during_ctx  = NULL;
	ged_clbk_get(&during_clbk, &during_ctx, gedp, "ert3", BU_CLBK_DURING);
	if (during_clbk) {
	    int pid = 0;   /* actual pid will be filled in by the Qt handler */
	    (*during_clbk)(0, NULL, &pid, during_ctx);
	}

	fbs->fbs_clientData = old_clientData;
    } else {
	bu_vls_printf(gedp->ged_result_str,
		      "ert3: no fbs_open_client_handler set — "
		      "ert3 requires a Qt IPC handler\n");
	render_ctx_destroy(ctx);
	render_opts_destroy(opts);
	return BRLCAD_ERROR;
    }

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
