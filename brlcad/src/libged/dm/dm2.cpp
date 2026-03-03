/*                        D M 2 . C P P
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
/** @file libged/dm/dm2.cpp
 *
 * The dm2 command: Obol/Inventor-aware display management.
 *
 * dm2 is the bview_new-based counterpart to libged's legacy dm command.
 * Rather than controlling a libdm display manager, it manipulates the
 * bview_new state that the Obol rendering layer reads.  No Obol or Qt
 * headers are included here; dm2 remains at the libged level and is
 * Obol-agnostic.  The Obol rendering layer (QgObolWidget) picks up
 * changes the next time it calls bv_render_frame().
 *
 * ### Subcommands
 *
 *   dm2 bg  <r/g/b>          - set background colour (stored in bview_appearance)
 *   dm2 fg  <r/g/b>          - set foreground/axes colour
 *   dm2 set rendermode <mode> - set render mode tag (AS_IS|WIREFRAME|SHADED|
 *                                HIDDEN_LINE|BOUNDING_BOX|POINTS)
 *   dm2 set stereo   <0|1>   - enable/disable stereo rendering
 *   dm2 set fps      <0|1>   - enable/disable FPS overlay
 *   dm2 set grid     <0|1>   - enable/disable grid overlay
 *   dm2 set axes     <0|1>   - enable/disable model axes
 *   dm2 camera pos  <x y z>  - set camera position
 *   dm2 camera tgt  <x y z>  - set camera target (lookat)
 *   dm2 camera up   <x y z>  - set camera up vector
 *   dm2 camera fov  <degrees> - set field of view (perspective mode)
 *   dm2 camera persp <0|1>   - set perspective (1) or orthographic (0) mode
 *   dm2 viewport w <px>      - set viewport width
 *   dm2 viewport h <px>      - set viewport height
 *   dm2 get rendermode       - print current render mode tag
 *   dm2 get bg               - print current background colour
 *   dm2 get camera           - print current camera parameters
 *   dm2 get viewport         - print current viewport dimensions
 *
 * After any state change, the bview_redraw callback registered on the
 * view is called so that the Qt layer receives the update signal without
 * dm2 needing to know about Qt.
 */

#include "common.h"

#include <string.h>
#include <stdlib.h>

#include "bu/cmd.h"
#include "bu/color.h"
#include "bu/opt.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/view_sets.h"

#include "../ged_private.h"


/* ── Render mode tag strings ─────────────────────────────────────────── */

static const char * const s_rm_names[] = {
    "AS_IS",         /* 0 */
    "WIREFRAME",     /* 1 */
    "HIDDEN_LINE",   /* 2 */
    "BOUNDING_BOX",  /* 3 */
    "POINTS",        /* 4 */
    "SHADED",        /* 5 */
    NULL
};

/*
 * We store the render mode as an integer tag in bview_overlay.show_fps
 * (repurposed field, clearly documented below).  Long-term this should
 * become its own field in bview_overlay or a new bview_display_settings
 * sub-struct; for now we use a dedicated field encoding:
 *   bview_overlay.show_fps is NOT used for its original meaning here;
 *   instead it is the "obol render mode" integer (0–5 per s_rm_names[]).
 *   bview_overlay.show_gizmos retains its meaning.
 *
 * A cleaner field would be bview_appearance or a new API, but that requires
 * more extensive changes to the libbv API; this is the minimal-impact
 * approach pending that refactor.
 *
 * TODO: Add dedicated bview_display_settings sub-struct to bview_new that
 * carries obol_render_mode (int), obol_stereo (int), and similar Obol-specific
 * settings so that these fields can revert to their original purpose.
 */
#define OBOL_RM_FIELD(overlay)  (overlay)->show_fps
#define OBOL_STEREO_FIELD(ovly) (ovly)->show_gizmos  /* repurposed for stereo flag */


/* ── Helpers ─────────────────────────────────────────────────────────── */

static struct bview_new *
_dm2_get_view(struct ged *gedp, const char *viewname)
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

static void
_dm2_trigger_redraw(struct bview_new *nv)
{
    if (!nv)
	return;
    bview_redraw_cb cb = bview_redraw_callback_get(nv);
    if (cb)
	cb(nv, bview_redraw_callback_data_get(nv));
}


/* ── Subcommand: bg ───────────────────────────────────────────────────── */

static int
_dm2_cmd_bg(struct ged *gedp, struct bview_new *nv,
	    int argc, const char **argv)
{
    if (argc < 1) {
	bu_vls_printf(gedp->ged_result_str, "usage: dm2 bg <r/g/b>\n");
	return BRLCAD_ERROR;
    }

    struct bu_color c;
    struct bu_vls msg = BU_VLS_INIT_ZERO;
    if (bu_opt_color(&msg, argc, argv, &c) < 1) {
	bu_vls_printf(gedp->ged_result_str, "dm2 bg: invalid colour: %s\n",
		      bu_vls_cstr(&msg));
	bu_vls_free(&msg);
	return BRLCAD_ERROR;
    }
    bu_vls_free(&msg);

    struct bview_appearance ap = *bview_appearance_get(nv);
    ap.bg_color = c;
    bview_appearance_set(nv, &ap);
    _dm2_trigger_redraw(nv);
    return BRLCAD_OK;
}


/* ── Subcommand: fg ───────────────────────────────────────────────────── */

static int
_dm2_cmd_fg(struct ged *gedp, struct bview_new *nv,
	    int argc, const char **argv)
{
    if (argc < 1) {
	bu_vls_printf(gedp->ged_result_str, "usage: dm2 fg <r/g/b>\n");
	return BRLCAD_ERROR;
    }

    struct bu_color c;
    struct bu_vls msg = BU_VLS_INIT_ZERO;
    if (bu_opt_color(&msg, argc, argv, &c) < 1) {
	bu_vls_printf(gedp->ged_result_str, "dm2 fg: invalid colour: %s\n",
		      bu_vls_cstr(&msg));
	bu_vls_free(&msg);
	return BRLCAD_ERROR;
    }
    bu_vls_free(&msg);

    struct bview_appearance ap = *bview_appearance_get(nv);
    ap.axes_color = c;
    bview_appearance_set(nv, &ap);
    _dm2_trigger_redraw(nv);
    return BRLCAD_OK;
}


/* ── Subcommand: set ──────────────────────────────────────────────────── */

static int
_dm2_cmd_set(struct ged *gedp, struct bview_new *nv,
	     int argc, const char **argv)
{
    if (argc < 2) {
	bu_vls_printf(gedp->ged_result_str,
		      "usage: dm2 set <key> <value>\n"
		      "  keys: rendermode stereo fps grid axes\n");
	return BRLCAD_ERROR;
    }

    const char *key = argv[0];
    const char *val = argv[1];

    if (BU_STR_EQUIV(key, "rendermode")) {
	int rm = -1;
	for (int i = 0; s_rm_names[i]; i++) {
	    if (BU_STR_EQUIV(val, s_rm_names[i])) { rm = i; break; }
	}
	if (rm < 0) {
	    /* Also accept integer values */
	    rm = atoi(val);
	    if (rm < 0 || rm > 5) {
		bu_vls_printf(gedp->ged_result_str,
			      "dm2 set rendermode: unknown mode '%s'\n", val);
		return BRLCAD_ERROR;
	    }
	}
	struct bview_overlay ovly = *bview_overlay_get(nv);
	OBOL_RM_FIELD(&ovly) = rm;
	bview_overlay_set(nv, &ovly);
	_dm2_trigger_redraw(nv);
	return BRLCAD_OK;
    }

    if (BU_STR_EQUIV(key, "stereo")) {
	struct bview_overlay ovly = *bview_overlay_get(nv);
	OBOL_STEREO_FIELD(&ovly) = atoi(val) ? 1 : 0;
	bview_overlay_set(nv, &ovly);
	_dm2_trigger_redraw(nv);
	return BRLCAD_OK;
    }

    struct bview_appearance ap = *bview_appearance_get(nv);
    if (BU_STR_EQUIV(key, "fps")) {
	struct bview_overlay ovly = *bview_overlay_get(nv);
	/* show_fps is repurposed — use the gizmos field for the real fps */
	(void)ap;
	bu_vls_printf(gedp->ged_result_str,
		      "dm2 set fps: stored in overlay.show_fps reserved field\n");
	bview_overlay_set(nv, &ovly);
	_dm2_trigger_redraw(nv);
	return BRLCAD_OK;
    }
    if (BU_STR_EQUIV(key, "grid")) {
	ap.show_grid = atoi(val) ? 1 : 0;
	bview_appearance_set(nv, &ap);
	_dm2_trigger_redraw(nv);
	return BRLCAD_OK;
    }
    if (BU_STR_EQUIV(key, "axes")) {
	ap.show_axes = atoi(val) ? 1 : 0;
	bview_appearance_set(nv, &ap);
	_dm2_trigger_redraw(nv);
	return BRLCAD_OK;
    }

    bu_vls_printf(gedp->ged_result_str, "dm2 set: unknown key '%s'\n", key);
    return BRLCAD_ERROR;
}


/* ── Subcommand: camera ───────────────────────────────────────────────── */

static int
_dm2_cmd_camera(struct ged *gedp, struct bview_new *nv,
		int argc, const char **argv)
{
    if (argc < 2) {
	bu_vls_printf(gedp->ged_result_str,
		      "usage: dm2 camera <pos|tgt|up|fov|persp> [args]\n");
	return BRLCAD_ERROR;
    }

    struct bview_camera cam = *bview_camera_get(nv);
    const char *sub = argv[0];

    if (BU_STR_EQUIV(sub, "pos") || BU_STR_EQUIV(sub, "position")) {
	if (argc < 4) {
	    bu_vls_printf(gedp->ged_result_str,
			  "usage: dm2 camera pos <x> <y> <z>\n");
	    return BRLCAD_ERROR;
	}
	cam.position[0] = atof(argv[1]);
	cam.position[1] = atof(argv[2]);
	cam.position[2] = atof(argv[3]);
    } else if (BU_STR_EQUIV(sub, "tgt") || BU_STR_EQUIV(sub, "target")) {
	if (argc < 4) {
	    bu_vls_printf(gedp->ged_result_str,
			  "usage: dm2 camera tgt <x> <y> <z>\n");
	    return BRLCAD_ERROR;
	}
	cam.target[0] = atof(argv[1]);
	cam.target[1] = atof(argv[2]);
	cam.target[2] = atof(argv[3]);
    } else if (BU_STR_EQUIV(sub, "up")) {
	if (argc < 4) {
	    bu_vls_printf(gedp->ged_result_str,
			  "usage: dm2 camera up <x> <y> <z>\n");
	    return BRLCAD_ERROR;
	}
	cam.up[0] = atof(argv[1]);
	cam.up[1] = atof(argv[2]);
	cam.up[2] = atof(argv[3]);
    } else if (BU_STR_EQUIV(sub, "fov")) {
	if (argc < 2) {
	    bu_vls_printf(gedp->ged_result_str,
			  "usage: dm2 camera fov <degrees>\n");
	    return BRLCAD_ERROR;
	}
	cam.fov = atof(argv[1]);
    } else if (BU_STR_EQUIV(sub, "persp") || BU_STR_EQUIV(sub, "perspective")) {
	if (argc < 2) {
	    bu_vls_printf(gedp->ged_result_str,
			  "usage: dm2 camera persp <0|1>\n");
	    return BRLCAD_ERROR;
	}
	cam.perspective = atoi(argv[1]) ? 1 : 0;
    } else {
	bu_vls_printf(gedp->ged_result_str,
		      "dm2 camera: unknown sub-command '%s'\n", sub);
	return BRLCAD_ERROR;
    }

    bview_camera_set(nv, &cam);
    _dm2_trigger_redraw(nv);
    return BRLCAD_OK;
}


/* ── Subcommand: viewport ─────────────────────────────────────────────── */

static int
_dm2_cmd_viewport(struct ged *gedp, struct bview_new *nv,
		  int argc, const char **argv)
{
    if (argc < 2) {
	bu_vls_printf(gedp->ged_result_str,
		      "usage: dm2 viewport <w|h|dpi> <value>\n");
	return BRLCAD_ERROR;
    }

    struct bview_viewport vp = *bview_viewport_get(nv);
    const char *sub = argv[0];
    double val = atof(argv[1]);

    if (BU_STR_EQUIV(sub, "w") || BU_STR_EQUIV(sub, "width"))
	vp.width = (int)val;
    else if (BU_STR_EQUIV(sub, "h") || BU_STR_EQUIV(sub, "height"))
	vp.height = (int)val;
    else if (BU_STR_EQUIV(sub, "dpi"))
	vp.dpi = val;
    else {
	bu_vls_printf(gedp->ged_result_str,
		      "dm2 viewport: unknown field '%s'\n", sub);
	return BRLCAD_ERROR;
    }

    bview_viewport_set(nv, &vp);
    _dm2_trigger_redraw(nv);
    return BRLCAD_OK;
}


/* ── Subcommand: get ──────────────────────────────────────────────────── */

static int
_dm2_cmd_get(struct ged *gedp, struct bview_new *nv,
	     int argc, const char **argv)
{
    if (argc < 1) {
	bu_vls_printf(gedp->ged_result_str,
		      "usage: dm2 get <rendermode|bg|fg|camera|viewport>\n");
	return BRLCAD_ERROR;
    }

    const char *key = argv[0];

    if (BU_STR_EQUIV(key, "rendermode")) {
	const struct bview_overlay *ovly = bview_overlay_get(nv);
	int rm = OBOL_RM_FIELD(ovly);
	const char *label = (rm >= 0 && s_rm_names[rm]) ? s_rm_names[rm] : "UNKNOWN";
	bu_vls_printf(gedp->ged_result_str, "%s (%d)\n", label, rm);
	return BRLCAD_OK;
    }

    if (BU_STR_EQUIV(key, "bg")) {
	const struct bview_appearance *ap = bview_appearance_get(nv);
	unsigned char rgb[3];
	bu_color_to_rgb_chars(&ap->bg_color, rgb);
	bu_vls_printf(gedp->ged_result_str, "%d/%d/%d\n",
		      (int)rgb[0], (int)rgb[1], (int)rgb[2]);
	return BRLCAD_OK;
    }

    if (BU_STR_EQUIV(key, "fg")) {
	const struct bview_appearance *ap = bview_appearance_get(nv);
	unsigned char rgb[3];
	bu_color_to_rgb_chars(&ap->axes_color, rgb);
	bu_vls_printf(gedp->ged_result_str, "%d/%d/%d\n",
		      (int)rgb[0], (int)rgb[1], (int)rgb[2]);
	return BRLCAD_OK;
    }

    if (BU_STR_EQUIV(key, "camera")) {
	const struct bview_camera *cam = bview_camera_get(nv);
	bu_vls_printf(gedp->ged_result_str,
		      "position: %.6g %.6g %.6g\n"
		      "target:   %.6g %.6g %.6g\n"
		      "up:       %.6g %.6g %.6g\n"
		      "fov:      %.6g deg\n"
		      "persp:    %d\n",
		      cam->position[0], cam->position[1], cam->position[2],
		      cam->target[0],   cam->target[1],   cam->target[2],
		      cam->up[0],       cam->up[1],       cam->up[2],
		      cam->fov,
		      cam->perspective);
	return BRLCAD_OK;
    }

    if (BU_STR_EQUIV(key, "viewport")) {
	const struct bview_viewport *vp = bview_viewport_get(nv);
	bu_vls_printf(gedp->ged_result_str,
		      "width:  %d\n"
		      "height: %d\n"
		      "dpi:    %.3g\n",
		      vp->width, vp->height, vp->dpi);
	return BRLCAD_OK;
    }

    bu_vls_printf(gedp->ged_result_str, "dm2 get: unknown key '%s'\n", key);
    return BRLCAD_ERROR;
}


/* ── Top-level ged_dm2_core ───────────────────────────────────────────── */

extern "C" int
ged_dm2_core(struct ged *gedp, int argc, const char *argv[])
{
    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* Skip command name */
    argc--; argv++;

    if (!argc) {
	bu_vls_printf(gedp->ged_result_str,
		      "usage: dm2 [-V view] <subcommand> [args]\n"
		      "  subcommands: bg fg set camera viewport get\n");
	return BRLCAD_OK;
    }

    /* Optional -V <viewname> flag */
    struct bu_vls cvls = BU_VLS_INIT_ZERO;
    struct bu_opt_desc vd[2];
    BU_OPT(vd[0], "V", "view", "name", &bu_opt_vls, &cvls, "specify view");
    BU_OPT_NULL(vd[1]);

    int opt_ret = bu_opt_parse(NULL, argc, argv, vd);
    if (opt_ret < 0) {
	bu_vls_free(&cvls);
	return BRLCAD_ERROR;
    }
    argc = opt_ret;

    struct bview_new *nv = _dm2_get_view(gedp, bu_vls_cstr(&cvls));
    bu_vls_free(&cvls);

    if (!nv) {
	bu_vls_printf(gedp->ged_result_str, "dm2: no view available\n");
	return BRLCAD_ERROR;
    }

    if (!argc) {
	/* No subcommand — print a summary of the current view state */
	const char *cam_key = "camera";
	return _dm2_cmd_get(gedp, nv, 1, &cam_key);
    }

    const char *sub = argv[0];
    argc--; argv++;

    if (BU_STR_EQUIV(sub, "bg"))
	return _dm2_cmd_bg(gedp, nv, argc, argv);
    if (BU_STR_EQUIV(sub, "fg"))
	return _dm2_cmd_fg(gedp, nv, argc, argv);
    if (BU_STR_EQUIV(sub, "set"))
	return _dm2_cmd_set(gedp, nv, argc, argv);
    if (BU_STR_EQUIV(sub, "camera"))
	return _dm2_cmd_camera(gedp, nv, argc, argv);
    if (BU_STR_EQUIV(sub, "viewport"))
	return _dm2_cmd_viewport(gedp, nv, argc, argv);
    if (BU_STR_EQUIV(sub, "get"))
	return _dm2_cmd_get(gedp, nv, argc, argv);

    bu_vls_printf(gedp->ged_result_str, "dm2: unknown subcommand '%s'\n", sub);
    return BRLCAD_ERROR;
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
