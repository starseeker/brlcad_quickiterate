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
 * Two rendering paths:
 *
 *   HW GL path (-type hw, or auto-detected):
 *     Creates a real OpenGL context on the Tk window using the platform
 *     GL API (GLX on X11, WGL on Windows, AGL/NSOpenGL on macOS).  Obol's
 *     SoRenderManager drives GL rendering directly.  This is the fast path
 *     for large geometry.  The approach is modelled on the Togl widget:
 *     Tk_SetWindowVisual is called BEFORE Tk_MakeWindowExist so the native
 *     window is created with the correct GL-capable visual/pixel-format.
 *
 *   SW path (-type sw, or auto-fallback):
 *     SoOffscreenRenderer + CoinOSMesaContextManager → RGBA pixel buffer →
 *     Tk photo image on a label widget.  Slow for large geometry but works
 *     everywhere without a display (headless, CI, etc.).
 *
 * Widget command:
 *
 *   obol_view <path> ?-type hw|sw? ?-width W? ?-height H?
 *
 * Instance sub-commands:
 *
 *   <path> attach <bsg_view_ptr>   — attach to a bsg_view (hex C pointer)
 *   <path> redraw                  — render scene
 *   <path> size <w> <h>            — resize
 *   <path> rotate <dx> <dy>        — orbit delta
 *   <path> pan    <dx> <dy>        — pan delta
 *   <path> zoom   <factor>         — zoom (>1 = in)
 *   <path> pick   <x>  <y>         — SoRayPickAction; returns shape ptr
 *   <path> type                    — returns "hw" or "sw"
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
/* X11 types must come BEFORE tk.h redefines Window etc. on some platforms */
#ifdef HAVE_X11_XLIB_H
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
#endif
#include "vmath.h"
#include "bu/malloc.h"
#include "bu/log.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "ged/defines.h"
}

/* GLX — available when we have X11 and OpenGL */
#if defined(HAVE_X11_XLIB_H) && defined(HAVE_GL_GL_H)
#  include <GL/glx.h>
#  include <GL/gl.h>
#  define OBOL_HW_GLX 1
#endif

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
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
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
    Tcl_Interp      *interp      = nullptr;
    Tk_Window        tkwin       = nullptr;   /* the outer window             */
    std::string      path;                    /* Tk widget path e.g. ".v"     */

    bsg_view        *bsgv        = nullptr;   /* attached BRL-CAD view        */
    SoSeparator     *obol_root   = nullptr;   /* scene root                   */

    int              width       = 512;
    int              height      = 512;

    /* Mouse state for orbit/pan/zoom */
    int              mouse_x     = 0;
    int              mouse_y     = 0;
    int              dragging    = 0;          /* 0=none 1=rotate 2=pan 3=zoom */

    /* ── Rendering path ─────────────────────────────────────────────────── */
    bool             use_hw      = false;      /* true = HW GL, false = SW     */

    /* HW GL path: SoRenderManager drives rendering into the Tk window's GL
     * context.  The SoRenderManager is given the same obol_root scene as
     * the SW path so camera sync helpers work unchanged. */
    SoRenderManager  render_mgr;               /* Obol render manager (HW)     */
    bool             render_mgr_active = false;

#ifdef OBOL_HW_GLX
    /* X11/GLX context */
    Display         *dpy         = nullptr;
    GLXContext       glxc        = nullptr;
    ::Window         xwin        = 0;
#endif

    /* SW path: photo image on a label widget */
    std::string      photo_name;               /* Tk photo image name          */
    std::string      label_path;               /* child label path             */
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

/* Locate the camera node inside the obol_root separator. */
static SoCamera *
obol_view_find_camera(ObolViewWidget *w)
{
    if (!w->obol_root) return nullptr;
    for (int i = 0; i < w->obol_root->getNumChildren(); i++) {
SoNode *child = w->obol_root->getChild(i);
if (child->isOfType(SoCamera::getClassTypeId()))
    return static_cast<SoCamera*>(child);
    }
    return nullptr;
}


/* ======================================================================== *
 * HW GL path — X11/GLX                                                     *
 * ======================================================================== */

#ifdef OBOL_HW_GLX

/* Attribute list for glXChooseVisual: double-buffer, depth, RGBA */
static int s_glx_attribs[] = {
    GLX_RGBA,
    GLX_DOUBLEBUFFER,
    GLX_RED_SIZE,   8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE,  8,
    GLX_DEPTH_SIZE, 24,
    None
};

/**
 * Initialise the HW GL path for the widget.
 *
 * Must be called BEFORE Tk_MakeWindowExist() so that Tk_SetWindowVisual()
 * can configure the native window with the GL-capable visual.  This mirrors
 * the Togl widget's approach.
 *
 * Returns true on success.  On failure the widget should fall back to SW.
 */
static bool
obol_init_glx(ObolViewWidget *w, Tk_Window tkwin)
{
    Display *dpy = Tk_Display(tkwin);
    if (!dpy) return false;

    int screen = Tk_ScreenNumber(tkwin);
    XVisualInfo *vi = glXChooseVisual(dpy, screen, s_glx_attribs);
    if (!vi) {
bu_log("obol_view: glXChooseVisual failed — falling back to SW\n");
return false;
    }

    /* Create the GL context before realising the Tk window so we can
     * report failure early. */
    GLXContext glxc = glXCreateContext(dpy, vi, nullptr, GL_TRUE);
    if (!glxc) {
bu_log("obol_view: glXCreateContext failed — falling back to SW\n");
XFree(vi);
return false;
    }

    /* Build a colormap compatible with the chosen visual */
    Colormap cmap = XCreateColormap(dpy,
RootWindow(dpy, vi->screen), vi->visual, AllocNone);

    /* Tell Tk to use this visual for the window.  MUST happen before
     * Tk_MakeWindowExist(). */
    Tk_SetWindowVisual(tkwin, vi->visual, vi->depth, cmap);

    /* Realise the window now that the visual is set */
    Tk_MakeWindowExist(tkwin);

    w->dpy    = dpy;
    w->glxc   = glxc;
    w->xwin   = Tk_WindowId(tkwin);

    XFree(vi);

    /* Activate SoRenderManager.  Give it a GL cache context that is unique
     * to this widget (use the GLX context pointer cast as an integer). */
    glXMakeCurrent(dpy, w->xwin, glxc);
    glEnable(GL_DEPTH_TEST);

    SoGLRenderAction *ra = w->render_mgr.getGLRenderAction();
    ra->setCacheContext((uint32_t)(uintptr_t)glxc);

    SbViewportRegion vp((short)w->width, (short)w->height);
    w->render_mgr.setViewportRegion(vp);
    w->render_mgr.setWindowSize(SbVec2s((short)w->width, (short)w->height));
    w->render_mgr.setBackgroundColor(SbColor4f(0.2f, 0.2f, 0.2f, 1.0f));
    w->render_mgr_active = true;

    glXMakeCurrent(dpy, None, nullptr);
    return true;
}

/** Render one frame using GLX */
static void
obol_view_render_hw_glx(ObolViewWidget *w)
{
    if (!w->dpy || !w->glxc || !w->xwin || !w->render_mgr_active)
return;
    if (!w->obol_root || !w->bsgv)
return;

    glXMakeCurrent(w->dpy, w->xwin, w->glxc);

    /* Assemble scene + sync camera */
    obol_scene_assemble(w->obol_root, w->bsgv);
    SoCamera *cam = obol_view_find_camera(w);
    if (cam)
obol_view_sync_cam_from_bsg(cam, w->bsgv);

    /* Drive SoRenderManager */
    SoGLRenderAction *ra = w->render_mgr.getGLRenderAction();
    ra->setViewportRegion(w->render_mgr.getViewportRegion());
    w->render_mgr.setSceneGraph(w->obol_root);
    w->render_mgr.render(ra, TRUE /* clearZ */, TRUE /* clearWindow */);

    glXSwapBuffers(w->dpy, w->xwin);
    glXMakeCurrent(w->dpy, None, nullptr);
}

/** Handle window resize for GLX path */
static void
obol_view_resize_hw_glx(ObolViewWidget *w)
{
    if (!w->render_mgr_active) return;
    SbViewportRegion vp((short)w->width, (short)w->height);
    w->render_mgr.setViewportRegion(vp);
    w->render_mgr.setWindowSize(SbVec2s((short)w->width, (short)w->height));
}

#endif /* OBOL_HW_GLX */


/* ======================================================================== *
 * SW path — SoOffscreenRenderer → Tk photo image                           *
 * ======================================================================== */

#ifdef HAVE_TK
static void
obol_view_render_sw(ObolViewWidget *w)
{
    if (!w->obol_root || !w->bsgv)
return;
    if (!SoDB::isInitialized())
return;

    obol_scene_assemble(w->obol_root, w->bsgv);
    SoCamera *cam = obol_view_find_camera(w);
    if (cam)
obol_view_sync_cam_from_bsg(cam, w->bsgv);

    SbViewportRegion vp((short)w->width, (short)w->height);
    SoOffscreenRenderer renderer(vp);
    renderer.setBackgroundColor(SbColor(0.2f, 0.2f, 0.2f));
    if (!renderer.render(w->obol_root))
return;

    const unsigned char *buf = renderer.getBuffer();
    if (!buf)
return;

    Tk_PhotoHandle photo = Tk_FindPhoto(w->interp, w->photo_name.c_str());
    if (!photo)
return;

    Tk_PhotoImageBlock blk;
    blk.pixelPtr  = (unsigned char *)buf;
    blk.width     = w->width;
    blk.height    = w->height;
    blk.pitch     = w->width * 4;
    blk.pixelSize = 4;
    blk.offset[0] = 0;
    blk.offset[1] = 1;
    blk.offset[2] = 2;
    blk.offset[3] = 3;
    Tk_PhotoPutBlock(w->interp, photo, &blk, 0, 0, w->width, w->height,
     TK_PHOTO_COMPOSITE_SET);
}
#endif /* HAVE_TK */


/* ======================================================================== *
 * Unified render entry — dispatches to HW or SW                            *
 * ======================================================================== */

static void
obol_view_do_render(ObolViewWidget *w)
{
    if (!w) return;
#ifdef OBOL_HW_GLX
    if (w->use_hw) {
obol_view_render_hw_glx(w);
return;
    }
#endif
#ifdef HAVE_TK
    obol_view_render_sw(w);
#endif
}


/* ======================================================================== *
 * Tk event handler — registered on the widget window                       *
 * ======================================================================== */

#ifdef HAVE_TK
static void
ObolViewEventProc(ClientData clientData, XEvent *eventPtr)
{
    ObolViewWidget *w = (ObolViewWidget *)clientData;
    if (!w || !eventPtr) return;

    switch (eventPtr->type) {
case ConfigureNotify: {
    int nw = eventPtr->xconfigure.width;
    int nh = eventPtr->xconfigure.height;
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    if (nw == w->width && nh == w->height)
break;
    w->width  = nw;
    w->height = nh;

    if (w->bsgv) {
w->bsgv->gv_width  = nw;
w->bsgv->gv_height = nh;
    }

    if (w->use_hw) {
#ifdef OBOL_HW_GLX
obol_view_resize_hw_glx(w);
#endif
    } else {
/* Resize the photo image for the SW path */
std::string cmd = "image create photo " + w->photo_name
    + " -width " + std::to_string(nw)
    + " -height " + std::to_string(nh);
Tcl_Eval(w->interp, cmd.c_str());
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
    if      (btn == 1) w->dragging = 1;  /* left   → rotate */
    else if (btn == 2) w->dragging = 2;  /* middle → pan    */
    else if (btn == 3) w->dragging = 3;  /* right  → zoom   */
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

    unsigned long long flags = BSG_IDLE;
    int adx = dx, ady = dy;

    if (w->dragging == 1) {
flags = BSG_ROT;
    } else if (w->dragging == 2) {
flags = BSG_TRANS;
    } else if (w->dragging == 3) {
flags = BSG_SCALE;
int mdelta = (abs(dx) > abs(dy)) ? dx : -dy;
int f = (int)(2*100*(double)abs(mdelta) /
      (double)(w->bsgv->gv_height > 0 ? w->bsgv->gv_height : w->height));
adx = 100;
ady = (mdelta > 0) ? 101 + f : 99 - f;
    }

    if (flags != BSG_IDLE) {
point_t keypoint = VINIT_ZERO;
bsg_view_adjust(w->bsgv, adx, ady, keypoint, 0, flags);
obol_view_do_render(w);
    }
    break;
}

case KeyPress:
    /* Key events handled by Tcl-level bindings */
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

    if (w->render_mgr_active) {
w->render_mgr.setSceneGraph(nullptr);
w->render_mgr_active = false;
    }

#ifdef OBOL_HW_GLX
    if (w->glxc) {
glXMakeCurrent(w->dpy, None, nullptr);
glXDestroyContext(w->dpy, w->glxc);
w->glxc = nullptr;
    }
#endif

    if (w->obol_root) {
w->obol_root->unref();
w->obol_root = nullptr;
    }

    /* Delete the SW-path photo image if present */
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

    /* ── type ── returns "hw" or "sw" */
    if (BU_STR_EQUAL(sub, "type")) {
Tcl_SetResult(interp, (char *)(w->use_hw ? "hw" : "sw"), TCL_STATIC);
return TCL_OK;
    }

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

    /* ── size w h ── */
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
if (w->use_hw) {
#ifdef OBOL_HW_GLX
    obol_view_resize_hw_glx(w);
#endif
} else {
    std::string cmd = "image create photo " + w->photo_name
+ " -width " + std::to_string(nw)
+ " -height " + std::to_string(nh);
    Tcl_Eval(interp, cmd.c_str());
}
obol_view_do_render(w);
return TCL_OK;
    }

    /* ── rotate dx dy ── */
    if (BU_STR_EQUAL(sub, "rotate")) {
if (argc < 4 || !w->bsgv) return TCL_OK;
int dx = atoi(argv[2]), dy = atoi(argv[3]);
point_t keypoint = VINIT_ZERO;
bsg_view_adjust(w->bsgv, dx, dy, keypoint, 0, BSG_ROT);
obol_view_do_render(w);
return TCL_OK;
    }

    /* ── pan dx dy ── */
    if (BU_STR_EQUAL(sub, "pan")) {
if (argc < 4 || !w->bsgv) return TCL_OK;
int dx = atoi(argv[2]), dy = atoi(argv[3]);
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
int dx = 100, dy = (int)(100.0 / factor);
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
int px = atoi(argv[2]), py = atoi(argv[3]);
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
Tcl_AppendResult(interp,
 "usage: obol_view <path> ?-type hw|sw? ?-width W? ?-height H?",
 (char *)NULL);
return TCL_ERROR;
    }

    if (!SoDB::isInitialized()) {
Tcl_AppendResult(interp, "obol_view: SoDB not initialized — "
 "call obol_init first", (char *)NULL);
return TCL_ERROR;
    }

    const char *path = argv[1];

    /* ── Parse options ── */
    int  init_w   = 512, init_h = 512;
    int  want_hw  = -1;   /* -1 = auto, 1 = force HW, 0 = force SW */

    for (int i = 2; i < argc - 1; i++) {
if (BU_STR_EQUAL(argv[i], "-width") && i+1 < argc)
    init_w = atoi(argv[++i]);
else if (BU_STR_EQUAL(argv[i], "-height") && i+1 < argc)
    init_h = atoi(argv[++i]);
else if (BU_STR_EQUAL(argv[i], "-type") && i+1 < argc) {
    ++i;
    if (BU_STR_EQUAL(argv[i], "hw"))  want_hw = 1;
    else if (BU_STR_EQUAL(argv[i], "sw")) want_hw = 0;
}
    }
    if (init_w < 1) init_w = 1;
    if (init_h < 1) init_h = 1;

    /* ── Create the Tk frame ── */
    std::string frame_cmd = "frame " + std::string(path)
+ " -width "  + std::to_string(init_w)
+ " -height " + std::to_string(init_h);
    if (Tcl_Eval(interp, frame_cmd.c_str()) != TCL_OK)
return TCL_ERROR;

    Tk_Window tkwin = Tk_NameToWindow(interp, path, Tk_MainWindow(interp));
    if (!tkwin) {
Tcl_Eval(interp, (std::string("destroy ") + path).c_str());
return TCL_ERROR;
    }

    /* ── Allocate widget state ── */
    ObolViewWidget *w = new ObolViewWidget;
    w->interp  = interp;
    w->tkwin   = tkwin;
    w->path    = path;
    w->width   = init_w;
    w->height  = init_h;

    /* ── Build Obol scene root (shared by both paths) ── */
    w->obol_root = obol_scene_create();
    if (!w->obol_root) {
delete w;
Tcl_Eval(interp, (std::string("destroy ") + path).c_str());
Tcl_AppendResult(interp, "obol_view: obol_scene_create() failed",
 (char *)NULL);
return TCL_ERROR;
    }
    w->obol_root->ref();

    /* ── Try HW GL path ── */
    bool hw_ok = false;

#ifdef OBOL_HW_GLX
    if (want_hw != 0) {
/* Attempt HW GL.  On success, obol_init_glx() calls
 * Tk_SetWindowVisual + Tk_MakeWindowExist internally. */
hw_ok = obol_init_glx(w, tkwin);
if (!hw_ok && want_hw == 1) {
    /* Caller explicitly requested HW; honour the failure */
    w->obol_root->unref();
    delete w;
    Tcl_Eval(interp, (std::string("destroy ") + path).c_str());
    Tcl_AppendResult(interp,
     "obol_view: HW GL (-type hw) init failed",
     (char *)NULL);
    return TCL_ERROR;
}
    }
#endif

    if (hw_ok) {
w->use_hw = true;
/* HW path: no photo image / label — the GL context renders directly
 * into the native window that Tk owns. */
    } else {
/* ── SW path setup ── */
w->use_hw = false;

/* Derive a unique photo image name from the widget path */
std::string photo_name = "_obol_photo_";
for (char c : std::string(path)) {
    if (c == '.' || c == ':' || c == '/') photo_name += '_';
    else photo_name += c;
}
w->photo_name = photo_name;

/* Create the photo image */
std::string photo_cmd = "image create photo " + photo_name
    + " -width "  + std::to_string(init_w)
    + " -height " + std::to_string(init_h);
if (Tcl_Eval(interp, photo_cmd.c_str()) != TCL_OK) {
    w->obol_root->unref();
    delete w;
    Tcl_Eval(interp, (std::string("destroy ") + path).c_str());
    return TCL_ERROR;
}

/* Create a label inside the frame to display the photo image */
std::string label_path = std::string(path) + ".img";
std::string label_cmd  = "label " + label_path
    + " -image " + photo_name
    + " -borderwidth 0 -highlightthickness 0";
if (Tcl_Eval(interp, label_cmd.c_str()) != TCL_OK) {
    w->obol_root->unref();
    delete w;
    Tcl_Eval(interp, (std::string("destroy ") + path).c_str());
    return TCL_ERROR;
}
w->label_path = label_path;
Tcl_Eval(interp, ("pack " + label_path + " -fill both -expand 1").c_str());

/* For SW path: ensure the Tk window is realized (HW path already did
 * this inside obol_init_glx). */
Tk_MakeWindowExist(tkwin);
    }

    /* ── Register Tk event handler (both paths) ── */
    Tk_CreateEventHandler(tkwin,
ExposureMask | StructureNotifyMask |
ButtonPressMask | ButtonReleaseMask |
PointerMotionMask | KeyPressMask,
ObolViewEventProc, (ClientData)w);

    /* ── Register the instance sub-command ── */
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
 * ======================================================================== */

#ifdef OBOL_BUILD_DUAL_GL
static CoinOSMesaContextManager *s_osmesa_ctx_mgr = nullptr;
#endif

extern "C" int
Obol_Init_Cmd(ClientData UNUSED(clientData), Tcl_Interp *interp,
      int UNUSED(argc), const char **UNUSED(argv))
{
    if (SoDB::isInitialized()) {
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
