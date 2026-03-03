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
 * The ert2 command: Obol-aware embedded raytrace.
 *
 * ert2 is the bview_new-based counterpart to the legacy ert command.
 * Instead of rendering into a libdm framebuffer via a framebuffer server,
 * it renders directly to a PNG file on disk and reports the output path
 * via ged_result_str.
 *
 * The rendering layer (QgObolWidget / QgObolQuadWidget) can then load the
 * PNG as an SoImage node in the Inventor scene graph.  This keeps ert2
 * completely free of Qt / Obol / framebuffer awareness.
 *
 * ### Usage
 *
 *   ert2 [-V view] [-w width] [-n height] [-o output.png]
 *        [-p] [object ...]
 *
 *   -V view      : view name (defaults to current view)
 *   -w width     : image width  in pixels (default: viewport width or 512)
 *   -n height    : image height in pixels (default: viewport height or 512)
 *   -o output    : output PNG path (default: auto-generated temp file)
 *   -p           : perspective mode (overrides view camera setting)
 *   objects      : objects to raytrace (default: all drawn objects)
 *
 * ### Return value
 *
 * On success ged_result_str contains the output PNG file path.
 * On failure it contains an error message.
 *
 * ### Integration with QgObolWidget
 *
 * After ert2 completes (use the subprocess end-callback mechanism that
 * qged already wires up for ert/preview), the Qt layer calls:
 *
 *   1. canvas_obol->loadRaytracedImage(outputPath)  [see QgObolWidget]
 *
 * which creates / updates an SoImage node in the scene graph, compositing
 * the raytraced image over the wireframe scene.
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/app.h"
#include "bu/cmd.h"
#include "bu/file.h"
#include "bu/opt.h"
#include "bu/path.h"
#include "bu/process.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/view_sets.h"
#include "raytrace.h"

#include "../ged_private.h"


/* ── Helpers ─────────────────────────────────────────────────────────── */

static struct bview_new *
_ert2_get_view(struct ged *gedp, const char *viewname)
{
    if (viewname && strlen(viewname)) {
	struct bview_new *nv = bv_viewset_find_new(&gedp->ged_views, viewname);
	if (!nv) {
	    bu_vls_printf(gedp->ged_result_str, "view '%s' not found\n", viewname);
	    return NULL;
	}
	return nv;
    }
    return gedp->ged_gvnv;
}

/*
 * Build the rt argument list from bview_new camera parameters.
 * Returns a bu_ptbl of newly allocated strings; caller must free them.
 */
static void
_ert2_camera_args(struct bu_ptbl *args, const struct bview_new *nv,
		  int width, int height)
{
    const struct bview_camera *cam = bview_camera_get(nv);
    if (!cam)
	return;

    struct bu_vls tmp = BU_VLS_INIT_ZERO;

    /* -s size or -w / -n */
    bu_vls_sprintf(&tmp, "%d", width);
    bu_ptbl_ins(args, (long *)bu_strdup("-w"));
    bu_ptbl_ins(args, (long *)bu_strdup(bu_vls_cstr(&tmp)));
    bu_vls_sprintf(&tmp, "%d", height);
    bu_ptbl_ins(args, (long *)bu_strdup("-n"));
    bu_ptbl_ins(args, (long *)bu_strdup(bu_vls_cstr(&tmp)));

    /* Eye point */
    bu_vls_sprintf(&tmp, "%.10g,%.10g,%.10g",
		   cam->position[0], cam->position[1], cam->position[2]);
    bu_ptbl_ins(args, (long *)bu_strdup("-e"));
    bu_ptbl_ins(args, (long *)bu_strdup(bu_vls_cstr(&tmp)));

    /* Orientation from lookat/up (expressed as a 3×3 view matrix) */
    /* Build view-to-model rotation from lookat and up */
    vect_t  z_view;          /* -lookat direction */
    vect_t  x_view;          /* right */
    vect_t  y_view;          /* up */
    VSUB2(z_view, cam->position, cam->target);   /* from target to eye */
    VUNITIZE(z_view);
    VCROSS(x_view, cam->up, z_view);
    VUNITIZE(x_view);
    VCROSS(y_view, z_view, x_view);
    VUNITIZE(y_view);

    /* rt's -M flag: view-to-model matrix in row order */
    /* (Using the 4×4 identity for the translation part here; the eye
     * position is already set with -e.  We pass the 3×3 rotation block.) */
    bu_vls_sprintf(&tmp,
		   "%.6g %.6g %.6g 0 "
		   "%.6g %.6g %.6g 0 "
		   "%.6g %.6g %.6g 0 "
		   "0 0 0 1",
		   x_view[0], x_view[1], x_view[2],
		   y_view[0], y_view[1], y_view[2],
		   z_view[0], z_view[1], z_view[2]);
    bu_ptbl_ins(args, (long *)bu_strdup("-M"));
    bu_ptbl_ins(args, (long *)bu_strdup(bu_vls_cstr(&tmp)));

    /* Perspective vs orthographic */
    if (!cam->perspective) {
	/* Use orthographic projection; -o option handled elsewhere */
	bu_ptbl_ins(args, (long *)bu_strdup("-P"));
    } else if (cam->fov > 0.0) {
	/* half-angle to rt's -p (perspective half-angle in degrees) */
	bu_vls_sprintf(&tmp, "%.6g", cam->fov / 2.0);
	bu_ptbl_ins(args, (long *)bu_strdup("-p"));
	bu_ptbl_ins(args, (long *)bu_strdup(bu_vls_cstr(&tmp)));
    }

    bu_vls_free(&tmp);
}

/*
 * Collect drawn object paths from ged_views for the current view.
 */
static void
_ert2_collect_objects(struct bu_ptbl *args, struct ged *gedp)
{
    if (!gedp->ged_gvp)
	return;

    struct bu_ptbl *dobjs = bv_view_objs(gedp->ged_gvp, BV_DB_OBJS);
    if (!dobjs || !BU_PTBL_LEN(dobjs))
	return;

    for (size_t i = 0; i < BU_PTBL_LEN(dobjs); i++) {
	struct bv_scene_obj *sp = (struct bv_scene_obj *)BU_PTBL_GET(dobjs, i);
	if (!sp || !sp->s_u_data)
	    continue;
	/* The path name is stored in s_name for legacy scene objects */
	if (sp->s_name[0])
	    bu_ptbl_ins(args, (long *)bu_strdup(sp->s_name));
    }
}


/* ── Top-level ged_ert2_core ──────────────────────────────────────────── */

extern "C" int
ged_ert2_core(struct ged *gedp, int argc, const char *argv[])
{
    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);

    bu_vls_trunc(gedp->ged_result_str, 0);

    /* ── Parse options ───────────────────────────────────────────── */
    struct bu_vls viewname = BU_VLS_INIT_ZERO;
    struct bu_vls outfile  = BU_VLS_INIT_ZERO;
    int width  = 0;
    int height = 0;
    int persp_override = 0;

    struct bu_opt_desc d[7];
    BU_OPT(d[0], "V", "view",   "name",   &bu_opt_vls,  &viewname, "view name");
    BU_OPT(d[1], "w", "width",  "#",      &bu_opt_int,  &width,    "image width");
    BU_OPT(d[2], "n", "height", "#",      &bu_opt_int,  &height,   "image height");
    BU_OPT(d[3], "o", "output", "file",   &bu_opt_vls,  &outfile,  "output file");
    BU_OPT(d[4], "p", "persp",  "",       NULL,         &persp_override, "perspective override");
    BU_OPT_NULL(d[5]);

    /* skip command name */
    argc--; argv++;

    int ac = bu_opt_parse(NULL, argc, argv, d);
    if (ac < 0) {
	bu_vls_printf(gedp->ged_result_str, "ert2: option parse error\n");
	bu_vls_free(&viewname);
	bu_vls_free(&outfile);
	return BRLCAD_ERROR;
    }

    /* ── Resolve view ────────────────────────────────────────────── */
    struct bview_new *nv = _ert2_get_view(gedp, bu_vls_cstr(&viewname));
    bu_vls_free(&viewname);
    if (!nv) {
	bu_vls_printf(gedp->ged_result_str, "ert2: no view available\n");
	bu_vls_free(&outfile);
	return BRLCAD_ERROR;
    }

    /* ── Defaults ────────────────────────────────────────────────── */
    if (width  <= 0) { const struct bview_viewport *vp = bview_viewport_get(nv);
		       width  = (vp && vp->width  > 0) ? vp->width  : 512; }
    if (height <= 0) { const struct bview_viewport *vp = bview_viewport_get(nv);
		       height = (vp && vp->height > 0) ? vp->height : 512; }

    /* ── Output path ─────────────────────────────────────────────── */
    if (!bu_vls_strlen(&outfile)) {
	char tmp[MAXPATHLEN];
	bu_temp_file(tmp, MAXPATHLEN, 1);
	bu_vls_sprintf(&outfile, "%s.png", tmp);
    }
    const char *outpath = bu_vls_cstr(&outfile);

    /* ── Find rt executable ──────────────────────────────────────── */
    char rt_path[MAXPATHLEN];
    bu_dir(rt_path, MAXPATHLEN, BU_DIR_BIN, "rt", BU_DIR_EXT, NULL);
    if (!bu_file_exists(rt_path, NULL)) {
	bu_vls_printf(gedp->ged_result_str, "ert2: rt not found at '%s'\n", rt_path);
	bu_vls_free(&outfile);
	return BRLCAD_ERROR;
    }

    /* ── Build rt argument list ──────────────────────────────────── */
    struct bu_ptbl args;
    bu_ptbl_init(&args, 64, "ert2 args");

    /* rt itself */
    bu_ptbl_ins(&args, (long *)bu_strdup(rt_path));

    /* Output: write to PNG file via -F */
    bu_ptbl_ins(&args, (long *)bu_strdup("-F"));
    bu_ptbl_ins(&args, (long *)bu_strdup(outpath));

    /* Camera params from view */
    _ert2_camera_args(&args, nv, width, height);

    if (persp_override) {
	/* Remove any -P flag that camera_args may have added */
	/* (simpler: just add a known perspective half-angle) */
	bu_ptbl_ins(&args, (long *)bu_strdup("-p"));
	bu_ptbl_ins(&args, (long *)bu_strdup("45"));
    }

    /* Database file */
    const char *dbfile = gedp->dbip->dbi_filename;
    if (!dbfile || !bu_file_exists(dbfile, NULL)) {
	bu_vls_printf(gedp->ged_result_str, "ert2: database file not accessible\n");
	for (size_t i = 0; i < BU_PTBL_LEN(&args); i++)
	    free((void *)BU_PTBL_GET(&args, i));
	bu_ptbl_free(&args);
	bu_vls_free(&outfile);
	return BRLCAD_ERROR;
    }
    bu_ptbl_ins(&args, (long *)bu_strdup(dbfile));

    /* Objects from command line or from drawn objects */
    if (ac > 0) {
	for (int i = 0; i < ac; i++)
	    bu_ptbl_ins(&args, (long *)bu_strdup(argv[i]));
    } else {
	_ert2_collect_objects(&args, gedp);
    }

    if (BU_PTBL_LEN(&args) < 4) {
	bu_vls_printf(gedp->ged_result_str,
		      "ert2: no objects to raytrace\n");
	for (size_t i = 0; i < BU_PTBL_LEN(&args); i++)
	    free((void *)BU_PTBL_GET(&args, i));
	bu_ptbl_free(&args);
	bu_vls_free(&outfile);
	return BRLCAD_ERROR;
    }

    /* NULL-terminate */
    bu_ptbl_ins(&args, (long *)NULL);

    /* ── Launch rt subprocess ────────────────────────────────────── */
    int exit_code = 0;
    struct bu_process *proc = NULL;
    bu_process_create(&proc,
		      (const char **)BU_PTBL_BASEADDR(&args),
		      BU_PROCESS_HIDE_WINDOW | BU_PROCESS_OUT_EQ_ERR);

    if (!proc) {
	bu_vls_printf(gedp->ged_result_str, "ert2: failed to launch rt\n");
	for (size_t i = 0; i + 1 < BU_PTBL_LEN(&args); i++)
	    free((void *)BU_PTBL_GET(&args, i));
	bu_ptbl_free(&args);
	bu_vls_free(&outfile);
	return BRLCAD_ERROR;
    }

    /* Wait for rt to finish and collect output */
    struct bu_vls rt_out = BU_VLS_INIT_ZERO;
    char buf[4096];
    int nread;
    while ((nread = bu_process_read((char *)buf, &proc,
				    BU_PROCESS_STDERR, sizeof(buf) - 1)) > 0) {
	buf[nread] = '\0';
	bu_vls_strcat(&rt_out, buf);
    }
    bu_process_wait(&exit_code, proc, 0);

    for (size_t i = 0; i + 1 < BU_PTBL_LEN(&args); i++)
	free((void *)BU_PTBL_GET(&args, i));
    bu_ptbl_free(&args);

    if (exit_code != 0) {
	bu_vls_printf(gedp->ged_result_str,
		      "ert2: rt exited with code %d:\n%s\n",
		      exit_code, bu_vls_cstr(&rt_out));
	bu_vls_free(&rt_out);
	bu_vls_free(&outfile);
	return BRLCAD_ERROR;
    }
    bu_vls_free(&rt_out);

    /* ── Return the output path ──────────────────────────────────── */
    if (!bu_file_exists(outpath, NULL)) {
	bu_vls_printf(gedp->ged_result_str,
		      "ert2: rt succeeded but output file '%s' not found\n",
		      outpath);
	bu_vls_free(&outfile);
	return BRLCAD_ERROR;
    }

    bu_vls_printf(gedp->ged_result_str, "%s", outpath);
    bu_vls_free(&outfile);
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
