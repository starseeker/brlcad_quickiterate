/*                   O B O L _ V I E W . C P P
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
/**
 * @file libtclcad/obol_view.cpp
 *
 * Tcl/Tk Obol 3D view widget — platform-neutral replacement for dm-ogl.
 *
 * Provides the "obol_view" Tcl command which creates a Tk widget that renders
 * BRL-CAD scenes using the Obol scene-graph library (via SoOffscreenRenderer +
 * CoinOSMesaContextManager) and displays the result as a Tk photo image.  This
 * is the Tcl/Tk analogue of QgObolSwrastView.
 *
 * Because rendering is done into an off-screen pixel buffer that is then
 * blitted to a standard Tk label widget, NO native GL window is required.
 * The widget is purely built on Tk's portable frame/label/photo abstraction
 * and works identically on X11, macOS (Tk Aqua), and Windows (Tk Win32) — the
 * same portability problem that prevented native Apple GUIs with the old
 * dm-ogl/dm-wgl approach is avoided entirely.
 *
 * Event handling uses Tk_CreateEventHandler() on the frame window, which is
 * also platform-neutral (Tk provides the XEvent-compatible struct on all
 * platforms through its internal event translation layer).  In particular, the
 * widget does NOT use Tk_CreateGenericHandler(), so it never interferes with
 * the existing mged/dm-ogl event dispatch in transitional setups where old
 * code and new code coexist.
 *
 * Widget command:
 *
 *   obol_view <path>               — create widget at Tk path <path>
 *
 * Instance sub-commands:
 *
 *   <path> attach <bsg_view_ptr>   — attach to a bsg_view (hex C pointer)
 *   <path> redraw                  — render scene → photo image → display
 *   <path> size <w> <h>            — resize rendering buffer
 *   <path> rotate <dx> <dy>        — apply screen-space rotation delta
 *   <path> pan    <dx> <dy>        — apply screen-space pan delta
 *   <path> zoom   <factor>         — zoom; factor > 1 zooms in
 *   <path> pick   <x>  <y>         — SoRayPickAction; returns object path
 *
 * Typical Tcl usage in mged/archer startup:
 *
 *   if {[info commands obol_view] ne ""} {
 *       obol_view .main.v
 *       .main.v attach [expr {[gedp] + 0}]   ;# hex GED ptr → bsg_view
 *       pack .main.v -fill both -expand 1
 *       bind .main.v <Configure> { .main.v size %w %h }
 *       bind .main.v <Button-1>   { ... }
 *   }
 *
 * See RADICAL_MIGRATION.md Stage 6 for context.
 */

#ifdef BRLCAD_ENABLE_OBOL

#include "common.h"

extern "C" {
#include <tcl.h>
#ifdef HAVE_TK
#  include <tk.h>
#endif
#include "vmath.h"
#include "bu/malloc.h"
#include "bu/log.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "ged/defines.h"
}

/* Suppress -Wfloat-equal from Obol/Inventor headers */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoNodeKit.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoPath.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#ifdef OBOL_BUILD_DUAL_GL
#  include <OSMesa/gl.h>
#  include <OSMesa/osmesa.h>
#endif
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include "../../libged/obol_scene.h"   /* obol_scene_create/assemble/clear */

#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>

/* ======================================================================== *
 * Internal widget state                                                     *
 * ======================================================================== */

struct ObolViewWidget {
    Tcl_Interp      *interp    = nullptr;
    Tk_Window        tkwin     = nullptr;   /* the outer frame window       */
    std::string      path;                  /* Tk widget path e.g. ".v"     */
    std::string      photo_name;            /* Tk photo image name          */
    std::string      label_path;            /* child label path             */

    bsg_view        *bsgv      = nullptr;   /* attached BRL-CAD view        */

    SoSeparator     *obol_root = nullptr;   /* scene root from obol_scene_create() */

    int              width     = 512;
    int              height    = 512;

    /* Mouse state for orbit/pan/zoom */
    int              mouse_x   = 0;
    int              mouse_y   = 0;
    int              dragging  = 0;         /* 0=none 1=rotate 2=pan 3=zoom */
};


/* ======================================================================== *
 * Camera sync helpers (mirrors ObolCameraHelpers in QgObolView.h)          *
 * ======================================================================== */

static void
obol_view_sync_cam_from_bsg(SoCamera *cam, const bsg_view *v)
{
    if (!cam || !v) return;

    mat_t v2m;
    MAT_COPY(v2m, v->gv_view2model);

    point_t eye_v = {0, 0, 0}, eye_w;
    MAT4X3PNT(eye_w, v2m, eye_v);

    vect_t look_v = {0, 0, -1}, look_w;
    vect_t up_v   = {0, 1,  0}, up_w;
    MAT4X3VEC(look_w, v2m, look_v); VUNITIZE(look_w);
    MAT4X3VEC(up_w,   v2m, up_v);   VUNITIZE(up_w);

    cam->position.setValue((float)eye_w[X], (float)eye_w[Y], (float)eye_w[Z]);

    SbVec3f look_sb((float)look_w[X], (float)look_w[Y], (float)look_w[Z]);
    SbVec3f up_sb  ((float)up_w[X],   (float)up_w[Y],   (float)up_w[Z]);
    SbVec3f right_sb = look_sb.cross(up_sb); right_sb.normalize();
    up_sb = right_sb.cross(look_sb); up_sb.normalize();

    SbMatrix rot_mat(
	 right_sb[0],  right_sb[1],  right_sb[2], 0.f,
	   up_sb[0],    up_sb[1],    up_sb[2],    0.f,
	-look_sb[0], -look_sb[1], -look_sb[2],   0.f,
	      0.f,        0.f,        0.f,        1.f);
    cam->orientation.setValue(SbRotation(rot_mat));
    cam->focalDistance = (float)v->gv_size;

    if (cam->getTypeId() == SoPerspectiveCamera::getClassTypeId()) {
	SoPerspectiveCamera *pc = static_cast<SoPerspectiveCamera*>(cam);
	fastf_t persp = v->gv_perspective;
	pc->heightAngle = (float)((persp > SMALL_FASTF ? persp : 45.0) * DEG2RAD);
    }
}


static void
obol_view_sync_bsg_from_cam(const SoCamera *cam, bsg_view *v)
{
    if (!cam || !v) return;

    SbVec3f right_sb, up_sb, look_sb;
    cam->orientation.getValue().multVec(SbVec3f(1, 0, 0),  right_sb);
    cam->orientation.getValue().multVec(SbVec3f(0, 1, 0),  up_sb);
    cam->orientation.getValue().multVec(SbVec3f(0, 0, -1), look_sb);
    right_sb.normalize(); up_sb.normalize(); look_sb.normalize();

    mat_t rot; MAT_ZERO(rot);
    rot[0]=right_sb[0]; rot[1]=right_sb[1]; rot[2]=right_sb[2];
    rot[4]=up_sb[0];    rot[5]=up_sb[1];    rot[6]=up_sb[2];
    rot[8]=-look_sb[0]; rot[9]=-look_sb[1]; rot[10]=-look_sb[2];
    rot[15] = 1.0;
    MAT_COPY(v->gv_rotation, rot);

    const SbVec3f &pos = cam->position.getValue();
    float fd = cam->focalDistance.getValue();
    point_t sc;
    sc[X] = (double)pos[0] + (double)look_sb[0] * (double)fd;
    sc[Y] = (double)pos[1] + (double)look_sb[1] * (double)fd;
    sc[Z] = (double)pos[2] + (double)look_sb[2] * (double)fd;
    MAT_IDN(v->gv_center);
    MAT_DELTAS_VEC_NEG(v->gv_center, sc);
    v->gv_size  = (double)fd;
    v->gv_scale = (double)fd * 0.5;
    v->gv_isize = (v->gv_size > SMALL_FASTF) ? 1.0 / v->gv_size : 1.0;

    bsg_view_update(v);
    if (v->gv_s)
	v->gv_s->gv_progressive_autoview = 0;
}


/* ======================================================================== *
 * Render one frame and push it to the Tk photo image                       *
 * ======================================================================== */

static void
obol_view_do_render(ObolViewWidget *w)
{
    if (!w || !w->obol_root || !w->bsgv)
	return;

#ifndef HAVE_TK
    return;
#else
    if (!SoDB::isInitialized())
	return;

    /* Assemble the scene from the current bsg_view's shape tree */
    obol_scene_assemble(w->obol_root, w->bsgv);

    /* Sync camera */
    SoCamera *cam = nullptr;
    if (w->obol_root->getNumChildren() > 0) {
	for (int i = 0; i < w->obol_root->getNumChildren(); i++) {
	    SoNode *child = w->obol_root->getChild(i);
	    if (child->isOfType(SoCamera::getClassTypeId())) {
		cam = static_cast<SoCamera*>(child);
		break;
	    }
	}
    }
    if (cam)
	obol_view_sync_cam_from_bsg(cam, w->bsgv);

    /* Render off-screen */
    SbViewportRegion vp((short)w->width, (short)w->height);
    SoOffscreenRenderer renderer(vp);
    renderer.setBackgroundColor(SbColor(0.2f, 0.2f, 0.2f));
    if (!renderer.render(w->obol_root))
	return;

    const unsigned char *buf = renderer.getBuffer();
    if (!buf)
	return;

    /* Push RGBA buffer into Tk photo image */
    Tk_PhotoHandle photo = Tk_FindPhoto(w->interp, w->photo_name.c_str());
    if (!photo)
	return;

    Tk_PhotoImageBlock blk;
    blk.pixelPtr  = (unsigned char *)buf;  /* Obol gives RGBA */
    blk.width     = w->width;
    blk.height    = w->height;
    blk.pitch     = w->width * 4;
    blk.pixelSize = 4;
    blk.offset[0] = 0;   /* R */
    blk.offset[1] = 1;   /* G */
    blk.offset[2] = 2;   /* B */
    blk.offset[3] = 3;   /* A (Tk ignores A for display but needs the slot) */
    Tk_PhotoPutBlock(w->interp, photo, &blk, 0, 0, w->width, w->height,
		     TK_PHOTO_COMPOSITE_SET);
#endif /* HAVE_TK */
}


/* ======================================================================== *
 * Tk event handler — platform-neutral, registered on the frame window      *
 * ======================================================================== */

#ifdef HAVE_TK
static void
ObolViewEventProc(ClientData clientData, XEvent *eventPtr)
{
    ObolViewWidget *w = (ObolViewWidget *)clientData;
    if (!w || !eventPtr) return;

    switch (eventPtr->type) {
	case ConfigureNotify: {
	    /* Window was resized */
	    int nw = eventPtr->xconfigure.width;
	    int nh = eventPtr->xconfigure.height;
	    if (nw < 1) nw = 1;
	    if (nh < 1) nh = 1;
	    if (nw == w->width && nh == w->height)
		break;
	    w->width  = nw;
	    w->height = nh;
	    /* Resize the photo image */
	    std::string cmd = "image create photo " + w->photo_name
		+ " -width " + std::to_string(nw)
		+ " -height " + std::to_string(nh);
	    Tcl_Eval(w->interp, cmd.c_str());
	    if (w->bsgv) {
		w->bsgv->gv_width  = nw;
		w->bsgv->gv_height = nh;
	    }
	    obol_view_do_render(w);
	    break;
	}

	case Expose:
	    if (eventPtr->xexpose.count == 0)
		obol_view_do_render(w);
	    break;

	case ButtonPress: {
	    int btn = eventPtr->xbutton.button;
	    w->mouse_x = eventPtr->xbutton.x;
	    w->mouse_y = eventPtr->xbutton.y;
	    /* Left=rotate, Middle=pan, Right=zoom */
	    if      (btn == 1) w->dragging = 1;
	    else if (btn == 2) w->dragging = 2;
	    else if (btn == 3) w->dragging = 3;
	    break;
	}

	case ButtonRelease:
	    w->dragging = 0;
	    break;

	case MotionNotify: {
	    if (!w->bsgv || !w->dragging) break;

	    int cx = eventPtr->xmotion.x;
	    int cy = eventPtr->xmotion.y;
	    int dx = cx - w->mouse_x;
	    int dy = cy - w->mouse_y;
	    w->mouse_x = cx;
	    w->mouse_y = cy;

	    if (dx == 0 && dy == 0) break;

	    point_t center = VINIT_ZERO;
	    {
		struct bsg_camera cm;
		bsg_view_get_camera(w->bsgv, &cm);
		MAT_DELTAS_GET_NEG(center, cm.center);
	    }

	    unsigned long long flags = BSG_IDLE;
	    int adx = dx, ady = dy;

	    if (w->dragging == 1) {
		/* Left button: rotate */
		flags = BSG_ROT;
	    } else if (w->dragging == 2) {
		/* Middle button: pan */
		flags = BSG_TRANS;
	    } else if (w->dragging == 3) {
		/* Right button: zoom via motion */
		flags = BSG_SCALE;
		int mdelta = (abs(dx) > abs(dy)) ? dx : -dy;
		int f = (int)(2*100*(double)abs(mdelta)/(double)w->bsgv->gv_height);
		adx = 100;
		ady = (mdelta > 0) ? 101 + f : 99 - f;
	    }

	    point_t keypoint = VINIT_ZERO;
	    if (flags != BSG_IDLE) {
		bsg_view_adjust(w->bsgv, adx, ady, keypoint, 0, flags);
		(void)center;  /* center used for pan in Qt path; placeholder here */
		obol_view_do_render(w);
	    }
	    break;
	}

	/* 4/5 are wheel up/down on X11 */
	case KeyPress:
	    /* Key events are left to higher-level Tcl bindings to interpret */
	    break;

	default:
	    break;
    }
}
#endif /* HAVE_TK */


/* ======================================================================== *
 * Widget deletion callback                                                  *
 * ======================================================================== */

static void
ObolViewDelete(ClientData clientData)
{
    ObolViewWidget *w = (ObolViewWidget *)clientData;
    if (!w) return;
    if (w->obol_root) {
	w->obol_root->unref();
	w->obol_root = nullptr;
    }
    /* Delete the photo image */
    if (!w->photo_name.empty() && w->interp) {
	std::string cmd = "catch { image delete " + w->photo_name + " }";
	Tcl_Eval(w->interp, cmd.c_str());
    }
    delete w;
}


/* ======================================================================== *
 * Instance command dispatcher                                               *
 * ======================================================================== */

static int
ObolView_InstanceCmd(ClientData clientData, Tcl_Interp *interp,
		     int argc, const char **argv)
{
    ObolViewWidget *w = (ObolViewWidget *)clientData;
    if (!w || argc < 2) {
	Tcl_AppendResult(interp, "usage: ", (argc > 0 ? argv[0] : "obol_view"),
			 " subcommand ?args?", (char *)NULL);
	return TCL_ERROR;
    }

    const char *sub = argv[1];

    /* ── attach ── */
    if (BU_STR_EQUAL(sub, "attach")) {
	if (argc < 3) {
	    Tcl_AppendResult(interp, argv[0], " attach bsg_view_ptr", (char *)NULL);
	    return TCL_ERROR;
	}
	unsigned long long pval = 0;
	if (sscanf(argv[2], "%llx", &pval) != 1 &&
	    sscanf(argv[2], "%llu", &pval) != 1) {
	    Tcl_AppendResult(interp, "bad pointer: ", argv[2], (char *)NULL);
	    return TCL_ERROR;
	}
	w->bsgv = (bsg_view *)(uintptr_t)pval;

	if (w->bsgv) {
	    w->bsgv->gv_width  = w->width;
	    w->bsgv->gv_height = w->height;
	}
	return TCL_OK;
    }

    /* ── redraw ── */
    if (BU_STR_EQUAL(sub, "redraw")) {
	obol_view_do_render(w);
	return TCL_OK;
    }

    /* ── size ── */
    if (BU_STR_EQUAL(sub, "size")) {
	if (argc < 4) {
	    Tcl_AppendResult(interp, argv[0], " size w h", (char *)NULL);
	    return TCL_ERROR;
	}
	int nw = atoi(argv[2]);
	int nh = atoi(argv[3]);
	if (nw < 1) nw = 1;
	if (nh < 1) nh = 1;
	w->width  = nw;
	w->height = nh;
	if (w->bsgv) {
	    w->bsgv->gv_width  = nw;
	    w->bsgv->gv_height = nh;
	}
	std::string cmd = "image create photo " + w->photo_name
	    + " -width " + std::to_string(nw)
	    + " -height " + std::to_string(nh);
	Tcl_Eval(interp, cmd.c_str());
	obol_view_do_render(w);
	return TCL_OK;
    }

    /* ── rotate dx dy ── */
    if (BU_STR_EQUAL(sub, "rotate")) {
	if (argc < 4 || !w->bsgv) return TCL_OK;
	int dx = atoi(argv[2]);
	int dy = atoi(argv[3]);
	point_t keypoint = VINIT_ZERO;
	bsg_view_adjust(w->bsgv, dx, dy, keypoint, 0, BSG_ROT);
	obol_view_do_render(w);
	return TCL_OK;
    }

    /* ── pan dx dy ── */
    if (BU_STR_EQUAL(sub, "pan")) {
	if (argc < 4 || !w->bsgv) return TCL_OK;
	int dx = atoi(argv[2]);
	int dy = atoi(argv[3]);
	point_t keypoint = VINIT_ZERO;
	bsg_view_adjust(w->bsgv, dx, dy, keypoint, 0, BSG_TRANS);
	obol_view_do_render(w);
	return TCL_OK;
    }

    /* ── zoom factor ── */
    if (BU_STR_EQUAL(sub, "zoom")) {
	if (argc < 3 || !w->bsgv) return TCL_OK;
	double factor = atof(argv[2]);
	if (factor < 0.001) factor = 0.001;
	/* encode as dx/dy for BSG_SCALE: dy/dx ratio is the scale factor */
	int dx = 100;
	int dy = (int)(100.0 / factor);
	point_t origin = VINIT_ZERO;
	bsg_view_adjust(w->bsgv, dx, dy, origin, 0, BSG_SCALE);
	obol_view_do_render(w);
	return TCL_OK;
    }

    /* ── pick x y ── */
    if (BU_STR_EQUAL(sub, "pick")) {
	if (argc < 4 || !w->obol_root) {
	    Tcl_SetResult(interp, (char *)"", TCL_STATIC);
	    return TCL_OK;
	}
	int px = atoi(argv[2]);
	int py = atoi(argv[3]);
	SbViewportRegion vp((short)w->width, (short)w->height);
	SoRayPickAction pick(vp);
	pick.setPoint(SbVec2s((short)px, (short)(w->height - 1 - py)));
	pick.apply(w->obol_root);
	SoPickedPoint *pp = pick.getPickedPoint();
	if (!pp) {
	    Tcl_SetResult(interp, (char *)"", TCL_STATIC);
	    return TCL_OK;
	}
	bsg_shape *s = obol_find_shape_for_path(pp->getPath());
	if (!s || !s->s_path) {
	    Tcl_SetResult(interp, (char *)"", TCL_STATIC);
	    return TCL_OK;
	}
	/* Return the shape pointer as hex so Tcl callers can look it up */
	char buf[64];
	snprintf(buf, sizeof(buf), "%p", (void *)s);
	Tcl_SetResult(interp, buf, TCL_VOLATILE);
	return TCL_OK;
    }

    Tcl_AppendResult(interp, "unknown subcommand: ", sub, (char *)NULL);
    return TCL_ERROR;
}


/* ======================================================================== *
 * Top-level "obol_view" command — creates a new widget instance            *
 * ======================================================================== */

extern "C" int
Obol_View_Cmd(ClientData UNUSED(clientData), Tcl_Interp *interp,
	      int argc, const char **argv)
{
#ifndef HAVE_TK
    Tcl_AppendResult(interp, "obol_view requires Tk", (char *)NULL);
    return TCL_ERROR;
#else
    if (argc < 2) {
	Tcl_AppendResult(interp, "usage: obol_view <path> ?-width W -height H?",
			 (char *)NULL);
	return TCL_ERROR;
    }

    if (!SoDB::isInitialized()) {
	Tcl_AppendResult(interp, "obol_view: SoDB not initialized — "
			 "call obol_init first", (char *)NULL);
	return TCL_ERROR;
    }

    const char *path = argv[1];

    /* Parse optional -width / -height */
    int init_w = 512, init_h = 512;
    for (int i = 2; i < argc - 1; i++) {
	if (BU_STR_EQUAL(argv[i], "-width") && i+1 < argc)
	    init_w = atoi(argv[++i]);
	else if (BU_STR_EQUAL(argv[i], "-height") && i+1 < argc)
	    init_h = atoi(argv[++i]);
    }
    if (init_w < 1) init_w = 1;
    if (init_h < 1) init_h = 1;

    /* ------------------------------------------------------------------
     * Build the Tk widget hierarchy:
     *   <path>              — outer frame (receives events)
     *   <path>.img          — label displaying the photo image
     * ------------------------------------------------------------------ */
    std::string frame_cmd = "frame " + std::string(path)
	+ " -width " + std::to_string(init_w)
	+ " -height " + std::to_string(init_h);
    if (Tcl_Eval(interp, frame_cmd.c_str()) != TCL_OK)
	return TCL_ERROR;

    /* Unique photo image name derived from widget path */
    std::string photo_name = std::string("_obol_photo_");
    for (char c : std::string(path)) {
	if (c == '.' || c == ':' || c == '/') photo_name += '_';
	else photo_name += c;
    }

    /* Create the photo image */
    std::string photo_cmd = "image create photo " + photo_name
	+ " -width " + std::to_string(init_w)
	+ " -height " + std::to_string(init_h);
    if (Tcl_Eval(interp, photo_cmd.c_str()) != TCL_OK) {
	Tcl_Eval(interp, (std::string("destroy ") + path).c_str());
	return TCL_ERROR;
    }

    /* Create a label inside the frame to show the image */
    std::string label_path = std::string(path) + ".img";
    std::string label_cmd  = "label " + label_path
	+ " -image " + photo_name
	+ " -borderwidth 0 -highlightthickness 0";
    if (Tcl_Eval(interp, label_cmd.c_str()) != TCL_OK) {
	Tcl_Eval(interp, (std::string("destroy ") + path).c_str());
	return TCL_ERROR;
    }
    std::string pack_cmd = "pack " + label_path + " -fill both -expand 1";
    Tcl_Eval(interp, pack_cmd.c_str());

    /* Get the Tk_Window handle for the outer frame */
    Tk_Window tkwin = Tk_NameToWindow(interp, path, Tk_MainWindow(interp));
    if (!tkwin) {
	Tcl_Eval(interp, (std::string("destroy ") + path).c_str());
	return TCL_ERROR;
    }

    /* Allocate widget state */
    ObolViewWidget *w = new ObolViewWidget;
    w->interp      = interp;
    w->tkwin       = tkwin;
    w->path        = path;
    w->photo_name  = photo_name;
    w->label_path  = label_path;
    w->width       = init_w;
    w->height      = init_h;

    /* Build Obol scene root */
    w->obol_root = obol_scene_create();
    if (!w->obol_root) {
	delete w;
	Tcl_Eval(interp, (std::string("destroy ") + path).c_str());
	Tcl_AppendResult(interp, "obol_view: obol_scene_create() failed",
			 (char *)NULL);
	return TCL_ERROR;
    }
    /* ref so it survives assembly/clear cycles */
    w->obol_root->ref();

    /* Register Tk event handler on the frame window.
     * We use Tk_CreateEventHandler (not Tk_CreateGenericHandler) so we
     * only receive events for our specific window, platform-neutrally. */
    Tk_CreateEventHandler(tkwin,
	ExposureMask | StructureNotifyMask |
	ButtonPressMask | ButtonReleaseMask |
	PointerMotionMask | KeyPressMask,
	ObolViewEventProc, (ClientData)w);

    /* Register instance command: the frame path becomes the command name */
    Tcl_CreateCommand(interp, path, ObolView_InstanceCmd,
		      (ClientData)w, ObolViewDelete);

    /* Return the widget path (Tk convention) */
    Tcl_SetResult(interp, (char *)path, TCL_VOLATILE);
    return TCL_OK;
#endif /* HAVE_TK */
}


/* ======================================================================== *
 * One-time Obol initialization command                                     *
 *                                                                           *
 * "obol_init" must be called once per process before any obol_view widget. *
 * It calls SoDB::init, SoNodeKit::init, SoInteraction::init and registers  *
 * the bsg_obol_set_unref hook.  Subsequent calls are no-ops.               *
 * ======================================================================== */

#ifdef OBOL_BUILD_DUAL_GL
/* Global OSMesa context manager for the process — owned by libtclcad */
static CoinOSMesaContextManager *s_osmesa_ctx_mgr = nullptr;
#endif

extern "C" int
Obol_Init_Cmd(ClientData UNUSED(clientData), Tcl_Interp *interp,
	      int UNUSED(argc), const char **UNUSED(argv))
{
    if (SoDB::isInitialized()) {
	/* Already done; idempotent */
	Tcl_SetResult(interp, (char *)"1", TCL_STATIC);
	return TCL_OK;
    }

#ifdef OBOL_BUILD_DUAL_GL
    s_osmesa_ctx_mgr = new CoinOSMesaContextManager();
    SoDB::init(s_osmesa_ctx_mgr);
#else
    SoDB::init();
#endif

    SoNodeKit::init();
    SoInteraction::init();
    SoRenderManager::enableRealTimeUpdate(FALSE);

    /* Hook so that libbsg can call SoNode::unref() without depending on
     * libObol at link time. */
    bsg_obol_set_unref([](void *p) {
	static_cast<SoNode *>(p)->unref();
    });

    Tcl_SetResult(interp, (char *)"1", TCL_STATIC);
    return TCL_OK;
}

#endif /* BRLCAD_ENABLE_OBOL */

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
