/* QgObolView.h — BRL-CAD Qt/Obol 3D view widget
 *
 * This header provides:
 *
 *   QgObolView         — hardware-OpenGL single/quad view widget (QOpenGLWidget)
 *   QgObolSwrastView   — software-rasterized (OSMesa) single/quad view widget (QWidget)
 *
 * Both classes present the same BRL-CAD integration interface so that
 * QgEdMainWindow can use either one without any canvas-type logic leaking to
 * higher levels.  The rendering backend (system GL vs OSMesa) is selected by
 * the constructor; Obol's dual-GL context dispatch routes GL calls to the
 * correct backend internally.
 *
 * Architecture — QgObolView (hardware GL, QOpenGLWidget)
 * ────────────────────────────────────────────────────
 *
 *   QgObolView (QOpenGLWidget)
 *     ├─ SoViewport       — scene graph, camera, viewport region, event routing
 *     ├─ SoRenderManager  — GL render passes, render mode, stereo
 *     ├─ SoSeparator*     — per-view Obol scene root (from obol_scene_create())
 *     ├─ bsg_view*        — BRL-CAD view state (camera, tolerances, …)
 *     ├─ three QTimers    — Obol sensor-queue bridge (idle / delay / timer)
 *     └─ (optional) SoQuadViewport — four-quadrant split view
 *
 * Architecture — QgObolSwrastView (OSMesa swrast, QWidget)
 * ─────────────────────────────────────────────────────
 *
 *   QgObolSwrastView (QWidget)
 *     ├─ CoinOSMesaContextManager — per-widget OSMesa GL context manager
 *     ├─ SoOffscreenRenderer      — renders to CPU pixel buffer
 *     ├─ SoViewport / SoQuadViewport — scene, camera, event routing
 *     ├─ SoSeparator*             — per-view Obol scene root
 *     ├─ bsg_view*                — BRL-CAD view state
 *     └─ three QTimers            — Obol sensor-queue bridge
 *
 * Usage
 * ─────
 *
 *   // In application startup (before creating any QgObolView):
 *   //   For hardware GL:
 *   QgObolContextManager ctxMgr;
 *   SoDB::init(&ctxMgr);
 *   //   For swrast (OSMesa):
 *   CoinOSMesaContextManager osmesaMgr;
 *   SoDB::init(&osmesaMgr);
 *
 *   SoNodeKit::init();
 *   SoInteraction::init();
 *   bsg_obol_set_unref([](void *p){ static_cast<SoNode*>(p)->unref(); });
 *
 *   // Create a view (hardware GL, single viewport):
 *   QgObolView *view = new QgObolView(parent);
 *   view->setBsgView(gedp->ged_gvp);
 *   view->redraw();
 *
 *   // Create a view (swrast, quad viewport):
 *   QgObolSwrastView *view = new QgObolSwrastView(parent, true);
 *   view->setBsgView(gedp->ged_gvp);
 *   view->redraw();
 *
 * Camera synchronisation
 * ──────────────────────
 *
 *   BRL-CAD commands (ae, zoom, rot, …) update the bsg_view fields
 *   (gv_rotation, gv_center, gv_size).  After each command QgEdApp calls
 *   do_view_changed() → view->syncCameraFromBsgView() which reads those
 *   fields and writes them to the Obol SoPerspectiveCamera.
 *
 *   Mouse navigation does the reverse via syncBsgViewFromCamera().
 *
 *   In quad viewport mode the primary quadrant (BOTTOM_RIGHT, perspective)
 *   is bidirectionally synchronised with bsg_view.  The other three quadrants
 *   (TOP=top, TOP_RIGHT=right, BOTTOM_LEFT=front) use fixed standard cameras
 *   that are not affected by BRL-CAD camera commands.
 *
 * See RADICAL_MIGRATION.md Stage 0-5 for context.
 */

#pragma once

#ifdef BRLCAD_ENABLE_OBOL

#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QMenu>
#include <QActionGroup>
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QVBoxLayout>

/* Suppress -Wfloat-equal from third-party Obol/Inventor headers */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoViewport.h>
#include <Inventor/SoQuadViewport.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SbVec2s.h>
#include <Inventor/SbColor.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/sensors/SoSensorManager.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoPath.h>
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include <GL/gl.h>
#ifdef OBOL_BUILD_DUAL_GL
#  include <OSMesa/osmesa.h>
#endif
#include <cmath>
#include <atomic>

#include "bsg.h"
#include "bsg/util.h"
#include "obol_scene.h"
#include "vmath.h"
#include "bn/mat.h"
#include "qtcad/QgSignalFlags.h"

extern "C" {
#include "dm.h"
#include "dm/fbserv.h"
}

/* bsg/defines.h defines UP=0 and DOWN=1 as plain macros which conflict with
 * SoButtonEvent::UP / SoButtonEvent::DOWN enum values used below.  Undefine
 * them now that we have finished including BRL-CAD headers. */
#ifdef UP
#  undef UP
#endif
#ifdef DOWN
#  undef DOWN
#endif

// ============================================================================
// QgObolContextManager
//
// SoDB::ContextManager implementation backed by QOffscreenSurface and
// QOpenGLContext.  Instantiate ONE of these per application and pass it to
// SoDB::init() before creating any QgObolView widgets.
// ============================================================================

class QgObolContextManager : public SoDB::ContextManager {
public:
    explicit QgObolContextManager(QOpenGLContext *shareCtx = nullptr)
	: shareCtx_(shareCtx) {}

    void *createOffscreenContext(unsigned int /*w*/, unsigned int /*h*/) override {
	Ctx *c = new Ctx;
	QSurfaceFormat fmt;
	fmt.setDepthBufferSize(24);
	fmt.setStencilBufferSize(8);

	c->surface = new QOffscreenSurface;
	c->surface->setFormat(fmt);
	c->surface->create();
	if (!c->surface->isValid()) {
	    delete c->surface;
	    delete c;
	    return nullptr;
	}

	c->context = new QOpenGLContext;
	c->context->setFormat(fmt);
	if (shareCtx_)
	    c->context->setShareContext(shareCtx_);
	if (!c->context->create()) {
	    delete c->context;
	    delete c->surface;
	    delete c;
	    return nullptr;
	}
	return c;
    }

    SbBool makeContextCurrent(void *p) override {
	Ctx *c = static_cast<Ctx *>(p);
	c->prev     = QOpenGLContext::currentContext();
	c->prevSurf = c->prev ? c->prev->surface() : nullptr;
	return c->context->makeCurrent(c->surface) ? TRUE : FALSE;
    }

    void restorePreviousContext(void *p) override {
	Ctx *c = static_cast<Ctx *>(p);
	if (c->prev && c->prevSurf)
	    c->prev->makeCurrent(c->prevSurf);
    }

    void destroyContext(void *p) override {
	Ctx *c = static_cast<Ctx *>(p);
	c->context->doneCurrent();
	delete c->context;
	c->surface->destroy();
	delete c->surface;
	delete c;
    }

private:
    struct Ctx {
	QOffscreenSurface  *surface  = nullptr;
	QOpenGLContext     *context  = nullptr;
	QOpenGLContext     *prev     = nullptr;
	QSurface           *prevSurf = nullptr;
    };
    QOpenGLContext *shareCtx_ = nullptr;
};


// ============================================================================
// CoinOSMesaContextManager
//
// SoDB::ContextManager implementation backed by the OSMesa software
// rasterizer (from obol/external/osmesa).  Use this as the SoDB global
// context manager when pure-software rendering is required (e.g. headless
// CI, or when the user requests swrast mode via "qged -s").
//
// Each context handle wraps a private OSMesa context + pixel buffer.
// makeContextCurrent() saves the previously current OSMesa context so that
// restorePreviousContext() can reinstate it, enabling nested context use.
//
// Requires: Obol built with OBOL_BUILD_DUAL_GL or OBOL_USE_OSMESA.
// ============================================================================
#ifdef OBOL_BUILD_DUAL_GL

struct CoinOSMesaCtxData {
    OSMesaContext ctx        = nullptr;
    unsigned char *buf       = nullptr;
    int            width     = 0;
    int            height    = 0;
    /* Saved state for restorePreviousContext() */
    OSMesaContext  prev_ctx  = nullptr;
    void          *prev_buf  = nullptr;
    GLsizei        prev_w    = 0, prev_h = 0, prev_bpr = 0;
    GLenum         prev_fmt  = 0;

    CoinOSMesaCtxData(int w, int h) : width(w), height(h) {
	ctx = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
	if (ctx)
	    buf = new unsigned char[(size_t)w * h * 4]();
    }
    ~CoinOSMesaCtxData() {
	if (ctx) OSMesaDestroyContext(ctx);
	delete[] buf;
    }
    bool isValid() const { return ctx != nullptr && buf != nullptr; }

    bool makeCurrent() {
	if (!ctx) return false;
	prev_ctx = OSMesaGetCurrentContext();
	prev_buf = nullptr; prev_w = prev_h = prev_bpr = 0; prev_fmt = 0;
	if (prev_ctx) {
	    GLint fmt = 0;
	    OSMesaGetColorBuffer(prev_ctx, &prev_w, &prev_h, &fmt, &prev_buf);
	    prev_bpr = prev_w * 4;
	    prev_fmt = (GLenum)fmt;
	}
	return OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, width, height) != 0;
    }
};

class CoinOSMesaContextManager : public SoDB::ContextManager {
public:
    void *createOffscreenContext(unsigned int w, unsigned int h) override {
	auto *d = new CoinOSMesaCtxData((int)w, (int)h);
	if (!d->isValid()) { delete d; return nullptr; }
	return d;
    }

    /* Returning TRUE tells Obol's GL-dispatch layer to route SoGLContext_*
     * calls through the osmesa_SoGLContext_* symbols rather than the
     * system-GL symbols.  This is the critical hook that makes dual-GL
     * rendering to OSMesa reliable (available since the latest upstream). */
    SbBool isOSMesaContext(void * /*ctx*/) override { return TRUE; }

    void maxOffscreenDimensions(unsigned int &w, unsigned int &h) const override {
	w = h = 16384;
    }

    SbBool makeContextCurrent(void *ctx) override {
	return ctx && static_cast<CoinOSMesaCtxData*>(ctx)->makeCurrent() ? TRUE : FALSE;
    }

    void restorePreviousContext(void *ctx) override {
	auto *d = static_cast<CoinOSMesaCtxData*>(ctx);
	if (!d) return;
	if (d->prev_ctx && d->prev_buf)
	    OSMesaMakeCurrent(d->prev_ctx, d->prev_buf, GL_UNSIGNED_BYTE,
			     d->prev_w, d->prev_h);
	else
	    OSMesaMakeCurrent(nullptr, nullptr, 0, 0, 0);
    }

    void destroyContext(void *ctx) override {
	delete static_cast<CoinOSMesaCtxData*>(ctx);
    }
};

#endif /* OBOL_BUILD_DUAL_GL */


// ============================================================================
// ObolCameraHelpers  —  shared camera-sync utilities for both view classes
// ============================================================================

namespace ObolCameraHelpers {

/** Sync an Obol SoCamera from BRL-CAD bsg_view camera state. */
inline void applyCameraFromBsgView(SoCamera *cam, const bsg_view *v) {
    mat_t v2m;
    MAT_COPY(v2m, v->gv_view2model);
    point_t eye_v = {0,0,0}, eye_w;
    MAT4X3PNT(eye_w, v2m, eye_v);
    vect_t look_v = {0,0,-1}, look_w, up_v = {0,1,0}, up_w;
    MAT4X3VEC(look_w, v2m, look_v); VUNITIZE(look_w);
    MAT4X3VEC(up_w,   v2m, up_v);   VUNITIZE(up_w);
    cam->position.setValue((float)eye_w[0], (float)eye_w[1], (float)eye_w[2]);
    SbVec3f look_sb((float)look_w[0], (float)look_w[1], (float)look_w[2]);
    SbVec3f up_sb  ((float)up_w[0],   (float)up_w[1],   (float)up_w[2]);
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

/** Sync a bsg_view from the current Obol camera. */
inline void applyCameraFromObol(SoCamera *cam, bsg_view *v) {
    const SbRotation &orient = cam->orientation.getValue();
    SbVec3f right_sb, up_sb, look_sb;
    orient.multVec(SbVec3f(1,0,0),  right_sb); right_sb.normalize();
    orient.multVec(SbVec3f(0,1,0),  up_sb);    up_sb.normalize();
    orient.multVec(SbVec3f(0,0,-1), look_sb);  look_sb.normalize();
    mat_t rot; MAT_ZERO(rot);
    rot[0]=right_sb[0]; rot[1]=right_sb[1]; rot[2]=right_sb[2];
    rot[4]=up_sb[0];    rot[5]=up_sb[1];    rot[6]=up_sb[2];
    rot[8]=-look_sb[0]; rot[9]=-look_sb[1]; rot[10]=-look_sb[2]; rot[15]=1.0;
    MAT_COPY(v->gv_rotation, rot);
    const SbVec3f &pos = cam->position.getValue();
    float fd = cam->focalDistance.getValue();
    point_t sc;
    sc[X]=(double)pos[0]+(double)look_sb[0]*(double)fd;
    sc[Y]=(double)pos[1]+(double)look_sb[1]*(double)fd;
    sc[Z]=(double)pos[2]+(double)look_sb[2]*(double)fd;
    MAT_IDN(v->gv_center); MAT_DELTAS_VEC_NEG(v->gv_center, sc);
    v->gv_size=(double)fd; v->gv_scale=(double)fd*0.5;
    v->gv_isize=(v->gv_size>SMALL_FASTF)?1.0/v->gv_size:1.0;
    bsg_view_update(v);
    if (v->gv_s) v->gv_s->gv_progressive_autoview = 0;
}

} /* namespace ObolCameraHelpers */


// ============================================================================
// QgObolView
//
// BRL-CAD Qt widget for Obol-based 3D rendering (hardware OpenGL path).
// Replaces QgView (libdm) for interactive use.
//
// When quad_view=true the widget renders four camera perspectives using
// SoQuadViewport.  The four standard views are:
//   TOP_LEFT     → top view    (ae = 270 0)
//   TOP_RIGHT    → right view  (ae = 0  -90)
//   BOTTOM_LEFT  → front view  (ae = 0  0)
//   BOTTOM_RIGHT → perspective (synced to bsg_view / primary view)
// ============================================================================

class QgObolView : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit QgObolView(QWidget *parent = nullptr, bool quad_view = false)
	: QOpenGLWidget(parent)
	, bsg_v_(nullptr)
	, obol_root_(nullptr)
	, quad_mode_(quad_view)
	, selectedShape_(nullptr)
    {
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);

	/* Note: SoRenderManager::enableRealTimeUpdate(FALSE) is a global
	 * call and is made once during Obol initialization in QgEdApp, not
	 * here, to avoid affecting other potential SoRenderManager users. */

	// Three-timer sensor bridge (same as QtObolWidget / Quarter SensorManager)
	idleTimer_.setSingleShot(true);
	delayTimer_.setSingleShot(true);
	timerTimer_.setSingleShot(true);
	connect(&idleTimer_,  &QTimer::timeout, this, &QgObolView::onIdle);
	connect(&delayTimer_, &QTimer::timeout, this, &QgObolView::onDelay);
	connect(&timerTimer_, &QTimer::timeout, this, &QgObolView::onTimer);
	SoDB::getSensorManager()->setChangedCallback(sensorQueueChangedCB, this);
    }

    ~QgObolView() override {
	SoDB::getSensorManager()->setChangedCallback(nullptr, nullptr);
	if (obol_root_)
	    obol_root_->unref();
    }

    // ── BRL-CAD integration ──────────────────────────────────────────────

    /** Attach a BRL-CAD bsg_view.  Call after SoDB::init(). */
    void setBsgView(bsg_view *v) {
	bsg_v_ = v;
	selectedShape_ = nullptr;    /* Clear any dangling selection on view switch */
	if (!obol_root_) {
	    obol_root_ = obol_scene_create();
	    if (quad_mode_)
		setObolSceneGraphQuad(obol_root_);
	    else
		setObolSceneGraph(obol_root_);
	}
    }

    bsg_view *getBsgView() const { return bsg_v_; }

    /** Attach a framebuffer server so that rt output is overlaid on the
     *  3D scene.  Call once from do_obol_init() after ert is wired up.
     *  The fbserv pointer is not owned; it must outlive this widget. */
    void setFbServ(struct fbserv_obj *fbs) { fbs_ = fbs; }

    /**
     * Re-assemble the Obol scene from the current bsg_shape tree and
     * request a repaint.  Call this whenever the BRL-CAD scene changes
     * (from QgEdApp::do_view_changed()).
     */
    void redraw() {
	if (obol_root_ && bsg_v_) {
	    obol_scene_assemble(obol_root_, bsg_v_);
	    syncCameraFromBsgView();
	}
	update();
    }

    /**
     * Sync the Obol camera from the current bsg_view state.
     *
     * BRL-CAD commands that modify the view (ae, center, zoom, …) update
     * the bsg_view fields.  This function translates those fields into
     * Obol camera parameters so the next paintGL() renders the correct view.
     */
    void syncCameraFromBsgView() {
	if (!bsg_v_)
	    return;

	/* In quad mode, sync only the perspective (primary) quadrant.
	 * The other three quadrants keep their fixed standard-view cameras. */
	SoCamera *cam = quad_mode_
	    ? quad_viewport_.getCamera(SoQuadViewport::BOTTOM_RIGHT)
	    : viewport_.getCamera();
	if (!cam)
	    return;

	ObolCameraHelpers::applyCameraFromBsgView(cam, bsg_v_);
    }

    /**
     * Sync the bsg_view camera state FROM the current Obol camera.
     *
     * This is the inverse of syncCameraFromBsgView().  It is called after
     * every interactive mouse navigation (orbit, pan, zoom) and after
     * viewAll() so that BRL-CAD command-line tools and the rest of the
     * application see an up-to-date view state.
     *
     * The following bsg_view fields are updated:
     *   gv_rotation  — 4×4 pure-rotation matrix (rows: right, up, -look)
     *   gv_center    — 4×4 translation matrix (translate to -scene_center)
     *   gv_scale     — half the scene extent (focalDistance / 2)
     *   gv_size      — full scene extent (= focalDistance)
     *
     * After setting those fields, bsg_view_update() is called to recompute
     * the derived matrices (gv_model2view, gv_view2model, gv_aet, etc.).
     * The gv_progressive_autoview flag is cleared so that drain_background_geom
     * does not override the user's explicit navigation.
     */
    void syncBsgViewFromCamera() {
	if (!bsg_v_)
	    return;

	/* In quad mode, sync from the active quadrant's camera. */
	SoCamera *cam = quad_mode_
	    ? quad_viewport_.getCamera(quad_viewport_.getActiveQuadrant())
	    : viewport_.getCamera();
	if (!cam)
	    return;

	ObolCameraHelpers::applyCameraFromObol(cam, bsg_v_);
    }

    // ── Scene graph ──────────────────────────────────────────────────────

    /** Attach an Obol scene root directly (advanced). */
    void setObolSceneGraph(SoNode *root) {
	if (!root) {
	    viewport_.setSceneGraph(nullptr);
	    viewport_.setCamera(nullptr);
	    renderMgr_.setSceneGraph(nullptr);
	    return;
	}

	/* Find or create camera */
	SoSearchAction sa;
	sa.setType(SoCamera::getClassTypeId());
	sa.setInterest(SoSearchAction::FIRST);
	sa.apply(root);

	SoCamera *cam = nullptr;
	if (sa.getPath()) {
	    SoFullPath *fp = static_cast<SoFullPath *>(sa.getPath());
	    cam = static_cast<SoCamera *>(fp->getTail());
	}
	if (!cam) {
	    cam = new SoPerspectiveCamera;
	    /* Add camera as second child (after directional light) */
	    SoSeparator *sep = static_cast<SoSeparator *>(root);
	    if (sep->getNumChildren() > 0)
		sep->insertChild(cam, 1);
	    else
		sep->addChild(cam);
	}

	viewport_.setSceneGraph(root);
	viewport_.setCamera(cam);
	viewport_.viewAll();
	renderMgr_.setSceneGraph(viewport_.getRoot());
	update();
    }

    SoNode *getObolSceneGraph() const { return viewport_.getSceneGraph(); }

    /**
     * Set up a quad viewport: assign the scene to all four quadrants and
     * create standard cameras for top, right, front, and perspective views.
     *
     * The same obol_root_ is shared across all quadrants.  Cameras are
     * created fresh (not taken from the scene graph) so the scene can remain
     * camera-free for quad rendering.
     */
    void setObolSceneGraphQuad(SoNode *root) {
	if (!root) {
	    quad_viewport_.setSceneGraph(nullptr);
	    return;
	}

	quad_viewport_.setSceneGraph(root);

	/* Create standard fixed cameras for the three orthographic quadrants */
	auto makePerspCam = [](float az, float el) -> SoPerspectiveCamera* {
	    SoPerspectiveCamera *c = new SoPerspectiveCamera;
	    c->heightAngle = (float)(45.0 * M_PI / 180.0);
	    /* Orient from azimuth/elevation angles (BRL-CAD convention) */
	    SbRotation ry(SbVec3f(0,0,1), (float)(az * M_PI / 180.0));
	    SbRotation rx(SbVec3f(1,0,0), (float)(el * M_PI / 180.0));
	    c->orientation.setValue(ry * rx);
	    c->position.setValue(0.f, 0.f, 100.f);
	    c->focalDistance = 100.f;
	    return c;
	};

	/* Standard BRL-CAD quad view orientations */
	quad_viewport_.setCamera(SoQuadViewport::TOP_LEFT,     makePerspCam(270.f,  90.f));   /* top */
	quad_viewport_.setCamera(SoQuadViewport::TOP_RIGHT,    makePerspCam(270.f,   0.f));   /* front */
	quad_viewport_.setCamera(SoQuadViewport::BOTTOM_LEFT,  makePerspCam(  0.f, -90.f));   /* right */
	/* BOTTOM_RIGHT is the perspective/primary quadrant — set via syncCameraFromBsgView */
	quad_viewport_.setCamera(SoQuadViewport::BOTTOM_RIGHT, makePerspCam( 35.f,  25.f));   /* perspective */
	quad_viewport_.setActiveQuadrant(SoQuadViewport::BOTTOM_RIGHT);
	quad_viewport_.viewAllQuadrants();
    }


    void setRenderMode(SoRenderManager::RenderMode mode) {
	renderMgr_.setRenderMode(mode);
	update();
    }
    SoRenderManager::RenderMode getRenderMode() const {
	return renderMgr_.getRenderMode();
    }

    /**
     * Map a BRL-CAD global draw mode integer to an Obol SoRenderManager
     * render mode and apply it.
     *
     * This is useful when BRL-CAD commands change the global view drawing
     * mode and the Obol renderer needs to follow:
     *
     *   0 — wireframe             → AS_IS  (per-shape SoDrawStyle::LINES)
     *   1 — hidden-line           → HIDDEN_LINE
     *   2 — shaded (Phong)        → AS_IS  (per-shape SoDrawStyle::FILLED)
     *   3 — evaluated wireframe   → AS_IS  (vlist path, SoDrawStyle::LINES)
     *   4 — shaded + hidden-line  → SHADED_HIDDEN_LINES
     *   5 — point cloud           → POINTS
     *
     * The default global mode is AS_IS so that per-object SoDrawStyle nodes
     * (set by obol_scene_assemble from each shape's s_dmode) take effect.
     * Mixed-mode scenes (some wireframe, some shaded) require AS_IS so that
     * each object's own SoDrawStyle is respected.
     */
    void syncRenderModeFromDmode(int dmode) {
	SoRenderManager::RenderMode mode;
	switch (dmode) {
	    case 1:
		mode = SoRenderManager::HIDDEN_LINE;
		break;
	    case 4:
		mode = SoRenderManager::SHADED_HIDDEN_LINES;
		break;
	    case 5:
		mode = SoRenderManager::POINTS;
		break;
	    case 0:
	    case 2:
	    case 3:
	    default:
		/* AS_IS: each shape renders according to its own SoDrawStyle
		 * (set by obol_scene_assemble based on s->s_os->s_dmode). */
		mode = SoRenderManager::AS_IS;
		break;
	}
	setRenderMode(mode);
    }

    void setStereoMode(SoRenderManager::StereoMode mode) {
	renderMgr_.setStereoMode(mode);
	update();
    }

    void setBackgroundColor(const SbColor &c) {
	viewport_.setBackgroundColor(c);
	renderMgr_.setBackgroundColor(SbColor4f(c, 1.0f));
	update();
    }

public slots:
    /**
     * Called by QgEdApp::view_update signal.  When QG_VIEW_DRAWN is set the
     * Obol scene is reassembled from the current bsg_shape tree; then the
     * camera is synced from bsg_view and a repaint is scheduled.
     */
    void need_update(unsigned long long flags) {
	if ((flags & QG_VIEW_DRAWN) && obol_root_ && bsg_v_)
	    obol_scene_assemble(obol_root_, bsg_v_);
	syncCameraFromBsgView();
	update();
    }

    /**
     * Fit the scene into the view using Obol's bounding-box camera fitting.
     *
     * Stage 4: After fitting, syncBsgViewFromCamera() writes the new camera
     * state back to bsg_view so command-line tools and the rest of BRL-CAD
     * remain consistent with the Obol camera.
     */
    void viewAll() {
	if (quad_mode_) {
	    quad_viewport_.viewAllQuadrants();
	    syncBsgViewFromCamera();
	    update();
	    return;
	}
	SoCamera *cam = viewport_.getCamera();
	SoNode *scene = viewport_.getSceneGraph();
	if (cam && scene) {
	    /* Use SoGetBoundingBoxAction for the scene bbox, then let the
	     * camera do the fitting.  This avoids using the SoViewport's
	     * internal helper so we can feed the result to syncBsgViewFromCamera(). */
	    SbViewportRegion vpr = viewport_.getViewportRegion();
	    SoGetBoundingBoxAction bba(vpr);
	    bba.apply(scene);
	    SbBox3f bbox = bba.getBoundingBox();
	    if (!bbox.isEmpty())
		cam->viewAll(scene, vpr);
	    else
		viewport_.viewAll();    /* fallback: no geometry yet */
	} else {
	    viewport_.viewAll();
	}

	/* Stage 4: propagate the new camera state back to bsg_view regardless
	 * of which viewAll path was taken (both modify the Obol camera).  This
	 * is intentional even for the empty-scene fallback so that bsg_view
	 * always mirrors the Obol camera after viewAll(). */
	syncBsgViewFromCamera();
	update();
    }

signals:
    /**
     * Emitted once after the first successful initializeGL().  QgEdMainWindow
     * connects this to do_obol_init() for any post-initialisation setup that
     * requires a live GL context.
     */
    void init_done();

    /**
     * Stage 5: Emitted when the user picks an object in the scene.
     *
     * @p s  The leaf bsg_shape that was hit.  The shape's @c s_path contains
     *       the db_full_path to the BRL-CAD object.  May be @c nullptr if
     *       the background was clicked (deselect).
     */
    void picked(bsg_shape *s);

protected:
    // ── QOpenGLWidget overrides ──────────────────────────────────────────

    void initializeGL() override {
	glEnable(GL_DEPTH_TEST);
	renderMgr_.getGLRenderAction()->setCacheContext(cacheContext_);
	if (!init_done_emitted_) {
	    init_done_emitted_ = true;
	    emit init_done();
	}
    }


    void resizeGL(int w, int h) override {
	const qreal dpr = devicePixelRatioF();
	SbVec2s physSize((short)(w * dpr), (short)(h * dpr));
	if (quad_mode_) {
	    quad_viewport_.setWindowSize(physSize);
	} else {
	    viewport_.setWindowSize(physSize);
	    renderMgr_.setWindowSize(physSize);
	    renderMgr_.setViewportRegion(viewport_.getViewportRegion());
	}
    }

    void paintGL() override {
	if (quad_mode_) {
	    /* Quad mode: render each quadrant via SoOffscreenRenderer and
	     * composite the result into the widget using QPainter. */
	    const qreal dpr  = devicePixelRatioF();
	    int pw = (int)(width()  * dpr);
	    int ph = (int)(height() * dpr);
	    SbVec2s qsz = quad_viewport_.getQuadrantSize();
	    if (qsz[0] <= 0 || qsz[1] <= 0) return;

	    if (!quad_renderer_) {
		SbViewportRegion qvr(qsz[0], qsz[1]);
		/* Use the current GL context (QOpenGLWidget) for offscreen FBOs */
		quad_renderer_ = new SoOffscreenRenderer(nullptr, qvr);
	    }

	    /* Render each quadrant and build a composite QImage */
	    int qw = qsz[0], qh = qsz[1];
	    std::vector<unsigned char> composite((size_t)pw * ph * 3, 0);

	    static const int QUADS[4] = {
		SoQuadViewport::TOP_LEFT,    SoQuadViewport::TOP_RIGHT,
		SoQuadViewport::BOTTOM_LEFT, SoQuadViewport::BOTTOM_RIGHT
	    };
	    /* Column/row offsets for each quadrant in the full window */
	    int col_off[4] = { 0, qw, 0, qw };
	    int row_off[4] = { 0, 0, qh, qh };

	    SoOffscreenRenderer *r = quad_renderer_;
	    SbViewportRegion qvr2(qw, qh);
	    r->setViewportRegion(qvr2);
	    r->setComponents(SoOffscreenRenderer::RGB_TRANSPARENCY);

	    for (int qi = 0; qi < 4; ++qi) {
		SoViewport *tile = quad_viewport_.getViewport(QUADS[qi]);
		if (!tile) continue;
		SoNode *troot = tile->getRoot();
		if (!troot) continue;
		r->setBackgroundColor(tile->getBackgroundColor());
		if (!r->render(troot)) continue;
		const unsigned char *src = r->getBuffer();
		if (!src) continue;
		/* Copy quadrant pixels into composite (bottom-up → top-down flip) */
		for (int row = 0; row < qh; ++row) {
		    const unsigned char *s = src + (size_t)(qh-1-row) * qw * 4;
		    int dst_row = row_off[qi] + row;
		    int dst_col = col_off[qi];
		    if (dst_row >= ph) continue;
		    unsigned char *d = composite.data() + (size_t)dst_row * pw * 3 + dst_col * 3;
		    for (int col = 0; col < qw && dst_col + col < pw; ++col) {
			d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
			s += 4; d += 3;
		    }
		}
	    }

	    /* Draw composite onto widget via QPainter */
	    QImage img(composite.data(), pw, ph, pw * 3, QImage::Format_RGB888);
	    QPainter p(this);
	    p.drawImage(QRect(0, 0, width(), height()), img);
	    return;
	}

	/* Single-view path */
	SoGLRenderAction *ra = renderMgr_.getGLRenderAction();
	ra->setViewportRegion(viewport_.getViewportRegion());

	renderMgr_.setSceneGraph(viewport_.getRoot());
	renderMgr_.render(ra, TRUE /* clearZ */, TRUE /* clearWindow */);

	/* Framebuffer overlay: composite rt output on top of the 3D scene.
	 * QPainter on a QOpenGLWidget draws on top of raw GL content. */
	_paintFbOverlay();
    }

    // ── Mouse navigation ─────────────────────────────────────────────────

    void mousePressEvent(QMouseEvent *e) override {
	lastMousePos_ = e->position();

	/* Stage 5: Ctrl+left-click triggers object picking instead of orbit.
	 * Right-click is handled by contextMenuEvent (no picking there). */
	if (e->button() == Qt::LeftButton &&
	    (e->modifiers() & Qt::ControlModifier)) {
	    pickAt((int)e->position().x(), (int)e->position().y());
	    return;
	}

	SoMouseButtonEvent ev;
	ev.setPosition(SbVec2s((short)e->position().x(), (short)e->position().y()));
	if (e->button() == Qt::LeftButton)
	    ev.setButton(SoMouseButtonEvent::BUTTON1);
	else if (e->button() == Qt::RightButton)
	    ev.setButton(SoMouseButtonEvent::BUTTON3);
	else if (e->button() == Qt::MiddleButton)
	    ev.setButton(SoMouseButtonEvent::BUTTON2);
	ev.setState(SoButtonEvent::DOWN);
	if (quad_mode_)
	    quad_viewport_.processEvent(&ev);
	else
	    viewport_.processEvent(&ev);
	update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
	SoMouseButtonEvent ev;
	ev.setPosition(SbVec2s((short)e->position().x(), (short)e->position().y()));
	if (e->button() == Qt::LeftButton)
	    ev.setButton(SoMouseButtonEvent::BUTTON1);
	else if (e->button() == Qt::RightButton)
	    ev.setButton(SoMouseButtonEvent::BUTTON3);
	else if (e->button() == Qt::MiddleButton)
	    ev.setButton(SoMouseButtonEvent::BUTTON2);
	ev.setState(SoButtonEvent::UP);
	if (quad_mode_)
	    quad_viewport_.processEvent(&ev);
	else
	    viewport_.processEvent(&ev);
	update();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
	QPointF delta = e->position() - lastMousePos_;
	lastMousePos_ = e->position();

	SoCamera *cam = quad_mode_
	    ? quad_viewport_.getCamera(quad_viewport_.getActiveQuadrant())
	    : viewport_.getCamera();
	if (!cam) return;

	bool navigated = false;

	if (e->buttons() & Qt::LeftButton) {
	    /* Orbit: rotate camera around focal point */
	    float dx = (float)delta.x() * 0.005f;
	    float dy = (float)delta.y() * 0.005f;
	    SbVec3f axis_h(0, 1, 0);
	    SbVec3f axis_v(1, 0, 0);
	    SbRotation r = cam->orientation.getValue();
	    SbRotation ry(axis_h, -dx);
	    SbRotation rx(axis_v, -dy);
	    cam->orientation.setValue(ry * rx * r);
	    navigated = true;
	} else if (e->buttons() & Qt::MiddleButton) {
	    /* Pan: translate camera in view plane */
	    float scale = cam->focalDistance.getValue() * 0.001f;
	    SbVec3f right, up;
	    cam->orientation.getValue().multVec(SbVec3f(1, 0, 0), right);
	    cam->orientation.getValue().multVec(SbVec3f(0, 1, 0), up);
	    SbVec3f pan = right * (-(float)delta.x() * scale)
		       + up   *  ((float)delta.y() * scale);
	    cam->position = cam->position.getValue() + pan;
	    navigated = true;
	}

	/* Route hover location to scene for dragger/selection highlight */
	SoLocation2Event le;
	le.setPosition(SbVec2s((short)e->position().x(), (short)e->position().y()));
	if (quad_mode_)
	    quad_viewport_.processEvent(&le);
	else
	    viewport_.processEvent(&le);

	/* Stage 4: sync back to bsg_view after any camera-changing navigation */
	if (navigated)
	    syncBsgViewFromCamera();

	update();
    }

    void wheelEvent(QWheelEvent *e) override {
	SoCamera *cam = quad_mode_
	    ? quad_viewport_.getCamera(quad_viewport_.getActiveQuadrant())
	    : viewport_.getCamera();
	if (!cam) return;

	float delta = e->angleDelta().y() / 120.0f;
	SbVec3f look;
	cam->orientation.getValue().multVec(SbVec3f(0, 0, -1), look);
	float step = cam->focalDistance.getValue() * 0.1f * delta;
	cam->position = cam->position.getValue() + look * step;
	cam->focalDistance = cam->focalDistance.getValue() - step;
	if (cam->focalDistance.getValue() < 0.001f)
	    cam->focalDistance = 0.001f;

	/* Stage 4: wheelEvent always modifies the camera (zoom always applies),
	 * so syncBsgViewFromCamera() is called unconditionally here rather than
	 * using a navigated flag — every wheel tick is a camera change. */
	syncBsgViewFromCamera();
	update();
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
	QMenu menu(this);
	QMenu *renderMenu = menu.addMenu("Render Mode");
	QActionGroup *rg = new QActionGroup(renderMenu);
	auto addRM = [&](const QString &label, SoRenderManager::RenderMode m) {
	    QAction *a = rg->addAction(label);
	    a->setCheckable(true);
	    a->setChecked(renderMgr_.getRenderMode() == m);
	    connect(a, &QAction::triggered, [this, m]{ setRenderMode(m); });
	    renderMenu->addAction(a);
	};
	addRM("As-Is (per-object mode)",    SoRenderManager::AS_IS);
	addRM("Wireframe (all)",             SoRenderManager::WIREFRAME);
	addRM("Wireframe Overlay",           SoRenderManager::WIREFRAME_OVERLAY);
	addRM("Hidden Line",                 SoRenderManager::HIDDEN_LINE);
	addRM("Shaded + Hidden Line",        SoRenderManager::SHADED_HIDDEN_LINES);
	addRM("Points",                      SoRenderManager::POINTS);
	addRM("Bounding Box",                SoRenderManager::BOUNDING_BOX);
	menu.addAction("View All", this, &QgObolView::viewAll);
	menu.exec(e->globalPos());
    }

private:
    // ── Obol sensor-queue bridge ─────────────────────────────────────────

    static void sensorQueueChangedCB(void *data) {
	QgObolView *w = static_cast<QgObolView *>(data);
	/* A sensor has been scheduled — process it from the event loop. */
	if (!w->idleTimer_.isActive())
	    w->idleTimer_.start(0);
    }

    void onIdle()  { SoDB::getSensorManager()->processTimerQueue();
		     SoDB::getSensorManager()->processDelayQueue(TRUE);
		     update(); }
    void onDelay() { SoDB::getSensorManager()->processDelayQueue(FALSE); update(); }
    void onTimer() { SoDB::getSensorManager()->processTimerQueue();
		     scheduleTimerUpdate(); }

    void scheduleTimerUpdate() {
	SbTime t;
	if (SoDB::getSensorManager()->isTimerSensorPending(t))
	    timerTimer_.start((int)(t.getValue() * 1000));
    }

    // ── GL cache context ──────────────────────────────────────────────────

    static uint32_t allocCacheContext() {
	static std::atomic<uint32_t> ctx{1};
	return ctx.fetch_add(1, std::memory_order_relaxed);
    }

    // ── Stage 7: framebuffer overlay ─────────────────────────────────────

    /** Composite rt framebuffer pixels on top of the Obol 3D scene.
     *  Called at the end of paintGL() (single-view path) when an embedded
     *  rt framebuffer is present and gv_fb_mode is non-zero. */
    void _paintFbOverlay() {
	if (!fbs_ || !fbs_->fbs_fbp || !bsg_v_ || !bsg_v_->gv_s)
	    return;
	if (!bsg_v_->gv_s->gv_fb_mode)
	    return;   /* framebuffer display disabled */
	int fw = fb_getwidth(fbs_->fbs_fbp);
	int fh = fb_getheight(fbs_->fbs_fbp);
	if (fw <= 0 || fh <= 0)
	    return;
	/* Resize cached buffer only when dimensions change. */
	size_t needed = (size_t)fw * fh * 3;
	if (fb_buf_.size() != needed)
	    fb_buf_.resize(needed);
	int nread = fb_readrect(fbs_->fbs_fbp, 0, 0, fw, fh, fb_buf_.data());
	if (nread < fw * fh)
	    return;
	/* libfb origin is bottom-left; QImage origin is top-left — flip Y. */
	QImage img(fb_buf_.data(), fw, fh, fw * 3, QImage::Format_RGB888);
	QPainter p(this);
	p.drawImage(rect(), img.mirrored(false, true));
    }

    // ── Stage 5: picking helper ───────────────────────────────────────────

    /**
     * Cast a pick ray at viewport coordinate (@p x, @p y) and emit the
     * `picked(bsg_shape*)` signal with the closest hit.
     *
     * Uses `SoRayPickAction` against the current scene graph.  The hit path
     * is resolved to a `bsg_shape` via `obol_find_shape_for_path()`.  If the
     * hit resolves to a shape that is already selected it is deselected;
     * otherwise the previous selection is cleared and the new shape is
     * selected.  After modifying the selection state, `obol_scene_assemble()`
     * is called to rebuild the affected separators (highlight on/off) and a
     * repaint is requested.
     *
     * Emits `picked(nullptr)` when the background is clicked (deselect all).
     */
    void pickAt(int x, int y) {
	SoNode *scene = viewport_.getSceneGraph();
	if (!scene)
	    return;

	SbViewportRegion vpr = viewport_.getViewportRegion();
	SoRayPickAction rpa(vpr);
	rpa.setPoint(SbVec2s((short)x, (short)y));
	rpa.setPickAll(false);   /* closest hit only */
	rpa.apply(scene);

	const SoPickedPoint *pp = rpa.getPickedPoint(0);
	bsg_shape *hit = pp ? obol_find_shape_for_path(pp->getPath()) : nullptr;

	/* Toggle selection: clicking the already-selected shape deselects it;
	 * clicking a new shape first clears the previous selection. */
	bsg_shape *prev = selectedShape_;
	if (selectedShape_) {
	    obol_shape_set_selected(selectedShape_, false);
	    selectedShape_ = nullptr;
	}

	if (hit && hit != prev) {
	    /* New shape: select it. */
	    obol_shape_set_selected(hit, true);
	    selectedShape_ = hit;
	} else {
	    /* Background click, or re-click on already-selected shape: deselect. */
	    hit = nullptr;
	}

	/* Rebuild affected shapes and repaint. */
	if (obol_root_ && bsg_v_)
	    obol_scene_assemble(obol_root_, bsg_v_);

	emit picked(hit);
	update();
    }

    // ── Members ───────────────────────────────────────────────────────────

    bsg_view        *bsg_v_;       /* BRL-CAD view (not owned) */
    SoSeparator     *obol_root_;   /* Obol scene root (owned, ref counted) */
    bool             quad_mode_;   /* true → use SoQuadViewport */
    SoViewport       viewport_;
    SoQuadViewport   quad_viewport_;   /* four-quadrant viewport (quad_mode_ only) */
    SoOffscreenRenderer *quad_renderer_ = nullptr; /* for quad paintGL compositing */
    SoRenderManager  renderMgr_;
    QTimer           idleTimer_, delayTimer_, timerTimer_;
    QPointF          lastMousePos_;
    uint32_t         cacheContext_ = allocCacheContext();
    bsg_shape       *selectedShape_ = nullptr;  /* Stage 5: current selection */
    bool             init_done_emitted_ = false; /* guard: emit init_done() only once */
    struct fbserv_obj *fbs_ = nullptr;  /* Stage 7: embedded rt framebuffer (not owned) */
    std::vector<unsigned char> fb_buf_; /* Stage 7: cached pixel buffer for fb overlay */
};


// ============================================================================
// QgObolSwrastView
//
// BRL-CAD Qt widget for Obol-based 3D rendering using the OSMesa software
// rasterizer.  Use when hardware OpenGL is unavailable or when "qged -s"
// (swrast mode) is requested.
//
// Architecture differs from QgObolView:
//   - Does NOT extend QOpenGLWidget.  The base class is plain QWidget.
//   - Uses CoinOSMesaContextManager (OSMesa) for all rendering.
//   - Rendering is done to a CPU pixel buffer via SoOffscreenRenderer; the
//     result is displayed by QPainter in paintEvent().
//   - Supports both single-view and quad-viewport modes (same API as
//     QgObolView).
//
// CoinOSMesaContextManager must have already been registered with SoDB::init()
// before the first QgObolSwrastView is created (done by QgEdApp when
// swrast_mode is true).
//
// Requires: Obol built with OBOL_BUILD_DUAL_GL.
// ============================================================================
#ifdef OBOL_BUILD_DUAL_GL

class QgObolSwrastView : public QWidget {
    Q_OBJECT

public:
    explicit QgObolSwrastView(QWidget *parent = nullptr, bool quad_view = false)
	: QWidget(parent)
	, bsg_v_(nullptr)
	, obol_root_(nullptr)
	, selectedShape_(nullptr)
	, quad_mode_(quad_view)
	, offscreen_(nullptr)
    {
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

	idleTimer_.setSingleShot(true);
	delayTimer_.setSingleShot(true);
	timerTimer_.setSingleShot(true);
	connect(&idleTimer_,  &QTimer::timeout, this, &QgObolSwrastView::onIdle);
	connect(&delayTimer_, &QTimer::timeout, this, &QgObolSwrastView::onDelay);
	connect(&timerTimer_, &QTimer::timeout, this, &QgObolSwrastView::onTimer);
	SoDB::getSensorManager()->setChangedCallback(sensorQueueChangedCB, this);
    }

    ~QgObolSwrastView() override {
	SoDB::getSensorManager()->setChangedCallback(nullptr, nullptr);
	if (obol_root_)
	    obol_root_->unref();
	delete offscreen_;
    }

    /** Returns false: swrast always works (no hardware GL dependency). */
    bool isValid() const { return true; }

    // ── BRL-CAD integration ──────────────────────────────────────────────

    void setBsgView(bsg_view *v) {
	bsg_v_ = v;
	selectedShape_ = nullptr;
	if (!obol_root_) {
	    obol_root_ = obol_scene_create();
	    if (quad_mode_)
		setObolSceneGraphQuad(obol_root_);
	    else
		setObolSceneGraph(obol_root_);
	}
    }

    bsg_view *getBsgView() const { return bsg_v_; }

    /** Attach a framebuffer server so that rt output is overlaid on the
     *  3D scene.  Call once from do_obol_init() after ert is wired up. */
    void setFbServ(struct fbserv_obj *fbs) { fbs_ = fbs; }

    void redraw() {
	if (obol_root_ && bsg_v_) {
	    obol_scene_assemble(obol_root_, bsg_v_);
	    syncCameraFromBsgView();
	}
	update();
    }

    void syncCameraFromBsgView() {
	if (!bsg_v_) return;
	SoCamera *cam = quad_mode_
	    ? quad_viewport_.getCamera(SoQuadViewport::BOTTOM_RIGHT)
	    : viewport_.getCamera();
	if (!cam) return;
	ObolCameraHelpers::applyCameraFromBsgView(cam, bsg_v_);
    }

    void syncBsgViewFromCamera() {
	if (!bsg_v_) return;
	SoCamera *cam = quad_mode_
	    ? quad_viewport_.getCamera(quad_viewport_.getActiveQuadrant())
	    : viewport_.getCamera();
	if (!cam) return;
	ObolCameraHelpers::applyCameraFromObol(cam, bsg_v_);
    }

    void viewAll() {
	if (quad_mode_) {
	    quad_viewport_.viewAllQuadrants();
	    syncBsgViewFromCamera();
	} else {
	    viewport_.viewAll();
	    syncBsgViewFromCamera();
	}
	update();
    }

    void setObolSceneGraph(SoNode *root) {
	if (!root) { viewport_.setSceneGraph(nullptr); return; }
	SoCamera *cam = ensureCamera(root);
	viewport_.setSceneGraph(root);
	viewport_.setCamera(cam);
	viewport_.viewAll();
    }

    void setObolSceneGraphQuad(SoNode *root) {
	if (!root) { quad_viewport_.setSceneGraph(nullptr); return; }
	quad_viewport_.setSceneGraph(root);
	auto makePerspCam = [](float az, float el) -> SoPerspectiveCamera* {
	    SoPerspectiveCamera *c = new SoPerspectiveCamera;
	    c->heightAngle = (float)(45.0 * M_PI / 180.0);
	    SbRotation ry(SbVec3f(0,0,1), (float)(az * M_PI / 180.0));
	    SbRotation rx(SbVec3f(1,0,0), (float)(el * M_PI / 180.0));
	    c->orientation.setValue(ry * rx);
	    c->position.setValue(0.f, 0.f, 100.f);
	    c->focalDistance = 100.f;
	    return c;
	};
	quad_viewport_.setCamera(SoQuadViewport::TOP_LEFT,     makePerspCam(270.f,  90.f));
	quad_viewport_.setCamera(SoQuadViewport::TOP_RIGHT,    makePerspCam(270.f,   0.f));
	quad_viewport_.setCamera(SoQuadViewport::BOTTOM_LEFT,  makePerspCam(  0.f, -90.f));
	quad_viewport_.setCamera(SoQuadViewport::BOTTOM_RIGHT, makePerspCam( 35.f,  25.f));
	quad_viewport_.setActiveQuadrant(SoQuadViewport::BOTTOM_RIGHT);
	quad_viewport_.viewAllQuadrants();
    }

    void setRenderMode(SoRenderManager::RenderMode mode) { (void)mode; update(); }
    SoRenderManager::RenderMode getRenderMode() const { return SoRenderManager::AS_IS; }
    void syncRenderModeFromDmode(int /*dmode*/) {}

    void setBackgroundColor(const SbColor &c) {
	viewport_.setBackgroundColor(c);
	update();
    }

public slots:
    void need_update(unsigned long long flags) {
	if ((flags & QG_VIEW_DRAWN) && obol_root_ && bsg_v_)
	    obol_scene_assemble(obol_root_, bsg_v_);
	syncCameraFromBsgView();
	update();
    }

signals:
    void init_done();
    void picked(bsg_shape *s);

protected:
    // ── QWidget overrides ────────────────────────────────────────────────

    void showEvent(QShowEvent *) override {
	if (!init_done_emitted_) {
	    init_done_emitted_ = true;
	    emit init_done();
	}
    }

    void resizeEvent(QResizeEvent *) override {
	const qreal dpr = devicePixelRatioF();
	SbVec2s sz((short)(width() * dpr), (short)(height() * dpr));
	if (quad_mode_)
	    quad_viewport_.setWindowSize(sz);
	else
	    viewport_.setWindowSize(sz);
    }

    void paintEvent(QPaintEvent *) override {
	const qreal dpr  = devicePixelRatioF();
	int pw = std::max((int)(width()  * dpr), 1);
	int ph = std::max((int)(height() * dpr), 1);

	if (!offscreen_) {
	    SbViewportRegion vr(pw, ph);
	    offscreen_ = new SoOffscreenRenderer(&osmesa_ctx_mgr_, vr);
	}

	if (quad_mode_) {
	    SbVec2s qsz = quad_viewport_.getQuadrantSize();
	    if (qsz[0] <= 0 || qsz[1] <= 0) return;
	    renderQuad(pw, ph, qsz[0], qsz[1]);
	} else {
	    renderSingle(pw, ph);
	}
    }

    void mousePressEvent(QMouseEvent *e) override {
	lastMousePos_ = e->position();
	if (e->button() == Qt::LeftButton &&
	    (e->modifiers() & Qt::ControlModifier)) {
	    pickAt((int)e->position().x(), (int)e->position().y());
	    return;
	}
	SoMouseButtonEvent ev;
	fillMouseEvent(&ev, e);
	ev.setState(SoButtonEvent::DOWN);
	if (quad_mode_) quad_viewport_.processEvent(&ev);
	else viewport_.processEvent(&ev);
	update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
	SoMouseButtonEvent ev;
	fillMouseEvent(&ev, e);
	ev.setState(SoButtonEvent::UP);
	if (quad_mode_) quad_viewport_.processEvent(&ev);
	else viewport_.processEvent(&ev);
	update();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
	QPointF delta = e->position() - lastMousePos_;
	lastMousePos_ = e->position();

	SoCamera *cam = quad_mode_
	    ? quad_viewport_.getCamera(quad_viewport_.getActiveQuadrant())
	    : viewport_.getCamera();
	if (!cam) return;

	bool navigated = false;
	if (e->buttons() & Qt::LeftButton) {
	    float dx = (float)delta.x() * 0.005f;
	    float dy = (float)delta.y() * 0.005f;
	    SbRotation r = cam->orientation.getValue();
	    SbRotation ry(SbVec3f(0,1,0), -dx);
	    SbRotation rx(SbVec3f(1,0,0), -dy);
	    cam->orientation.setValue(ry * rx * r);
	    navigated = true;
	} else if (e->buttons() & Qt::MiddleButton) {
	    float scale = cam->focalDistance.getValue() * 0.001f;
	    SbVec3f right, up;
	    cam->orientation.getValue().multVec(SbVec3f(1,0,0), right);
	    cam->orientation.getValue().multVec(SbVec3f(0,1,0), up);
	    SbVec3f pan = right * (-(float)delta.x() * scale)
			+ up   *  ((float)delta.y() * scale);
	    cam->position = cam->position.getValue() + pan;
	    navigated = true;
	}

	SoLocation2Event le;
	le.setPosition(SbVec2s((short)e->position().x(), (short)e->position().y()));
	if (quad_mode_) quad_viewport_.processEvent(&le);
	else viewport_.processEvent(&le);

	if (navigated) syncBsgViewFromCamera();
	update();
    }

    void wheelEvent(QWheelEvent *e) override {
	SoCamera *cam = quad_mode_
	    ? quad_viewport_.getCamera(quad_viewport_.getActiveQuadrant())
	    : viewport_.getCamera();
	if (!cam) return;

	float delta = e->angleDelta().y() / 120.0f;
	SbVec3f look;
	cam->orientation.getValue().multVec(SbVec3f(0,0,-1), look);
	float step = cam->focalDistance.getValue() * 0.1f * delta;
	cam->position = cam->position.getValue() + look * step;
	cam->focalDistance = cam->focalDistance.getValue() - step;
	if (cam->focalDistance.getValue() < 0.001f)
	    cam->focalDistance = 0.001f;
	syncBsgViewFromCamera();
	update();
    }

private:
    // ── Internal helpers ──────────────────────────────────────────────────

    static SoCamera* ensureCamera(SoNode *root) {
	SoSearchAction sa;
	sa.setType(SoCamera::getClassTypeId());
	sa.setInterest(SoSearchAction::FIRST);
	sa.apply(root);
	if (sa.getPath()) {
	    SoFullPath *fp = static_cast<SoFullPath*>(sa.getPath());
	    return static_cast<SoCamera*>(fp->getTail());
	}
	SoPerspectiveCamera *cam = new SoPerspectiveCamera;
	SoSeparator *sep = static_cast<SoSeparator*>(root);
	if (sep->getNumChildren() > 0) sep->insertChild(cam, 1);
	else sep->addChild(cam);
	return cam;
    }

    void renderSingle(int pw, int ph) {
	SoNode *root = viewport_.getRoot();
	if (!root) { fillBlack(pw, ph); return; }
	SbViewportRegion vr(pw, ph);
	offscreen_->setViewportRegion(vr);
	offscreen_->setComponents(SoOffscreenRenderer::RGB_TRANSPARENCY);
	offscreen_->setBackgroundColor(viewport_.getBackgroundColor());
	if (!offscreen_->render(root)) { fillBlack(pw, ph); return; }
	drawBuffer(offscreen_->getBuffer(), pw, ph, pw, ph);
	/* Framebuffer overlay: composite rt output on top of the 3D scene. */
	_paintFbOverlay();
    }

    void renderQuad(int pw, int ph, int qw, int qh) {
	std::vector<unsigned char> composite((size_t)pw * ph * 3, 0);

	static const int QUADS[4] = {
	    SoQuadViewport::TOP_LEFT,    SoQuadViewport::TOP_RIGHT,
	    SoQuadViewport::BOTTOM_LEFT, SoQuadViewport::BOTTOM_RIGHT
	};
	int col_off[4] = { 0, qw, 0, qw };
	int row_off[4] = { 0, 0,  qh, qh };

	SbViewportRegion qvr(qw, qh);
	offscreen_->setViewportRegion(qvr);
	offscreen_->setComponents(SoOffscreenRenderer::RGB_TRANSPARENCY);

	for (int qi = 0; qi < 4; ++qi) {
	    SoViewport *tile = quad_viewport_.getViewport(QUADS[qi]);
	    if (!tile) continue;
	    SoNode *troot = tile->getRoot();
	    if (!troot) continue;
	    offscreen_->setBackgroundColor(tile->getBackgroundColor());
	    if (!offscreen_->render(troot)) continue;
	    const unsigned char *src = offscreen_->getBuffer();
	    if (!src) continue;
	    for (int row = 0; row < qh; ++row) {
		const unsigned char *s = src + (size_t)(qh-1-row) * qw * 4;
		int dst_row = row_off[qi] + row;
		int dst_col = col_off[qi];
		if (dst_row >= ph) continue;
		unsigned char *d = composite.data() + (size_t)dst_row*pw*3 + dst_col*3;
		for (int col=0; col<qw && dst_col+col<pw; ++col) {
		    d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; s+=4; d+=3;
		}
	    }
	}
	QImage img(composite.data(), pw, ph, pw*3, QImage::Format_RGB888);
	QPainter p(this);
	p.drawImage(QRect(0,0,width(),height()), img);
    }

    void fillBlack(int pw, int ph) {
	std::vector<unsigned char> buf((size_t)pw*ph*3, 0);
	QImage img(buf.data(), pw, ph, pw*3, QImage::Format_RGB888);
	QPainter p(this); p.drawImage(QRect(0,0,width(),height()), img);
    }

    void drawBuffer(const unsigned char *src, int sw, int sh, int pw, int ph) {
	if (!src) { fillBlack(pw, ph); return; }
	std::vector<unsigned char> rgb((size_t)pw*ph*3);
	for (int row=0; row<ph; ++row) {
	    const unsigned char *s = src + (size_t)(sh-1-row)*sw*4;
	    unsigned char *d = rgb.data() + (size_t)row*pw*3;
	    for (int col=0; col<pw; ++col) {
		d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; s+=4; d+=3;
	    }
	}
	QImage img(rgb.data(), pw, ph, pw*3, QImage::Format_RGB888);
	QPainter p(this); p.drawImage(QRect(0,0,width(),height()), img);
    }

    static void fillMouseEvent(SoMouseButtonEvent *ev, QMouseEvent *e) {
	ev->setPosition(SbVec2s((short)e->position().x(), (short)e->position().y()));
	if      (e->button() == Qt::LeftButton)   ev->setButton(SoMouseButtonEvent::BUTTON1);
	else if (e->button() == Qt::RightButton)  ev->setButton(SoMouseButtonEvent::BUTTON3);
	else if (e->button() == Qt::MiddleButton) ev->setButton(SoMouseButtonEvent::BUTTON2);
    }

    void pickAt(int x, int y) {
	SoNode *scene = quad_mode_
	    ? quad_viewport_.getViewport(quad_viewport_.getActiveQuadrant())->getRoot()
	    : viewport_.getSceneGraph();
	if (!scene) return;
	SbViewportRegion vpr(std::max(width(),1), std::max(height(),1));
	SoRayPickAction rpa(vpr);
	rpa.setPoint(SbVec2s((short)x, (short)y));
	rpa.setPickAll(false);
	rpa.apply(scene);
	const SoPickedPoint *pp = rpa.getPickedPoint(0);
	bsg_shape *hit = pp ? obol_find_shape_for_path(pp->getPath()) : nullptr;
	bsg_shape *prev = selectedShape_;
	if (selectedShape_) { obol_shape_set_selected(selectedShape_, false); selectedShape_=nullptr; }
	if (hit && hit != prev) { obol_shape_set_selected(hit, true); selectedShape_=hit; }
	else hit = nullptr;
	if (obol_root_ && bsg_v_) obol_scene_assemble(obol_root_, bsg_v_);
	emit picked(hit);
	update();
    }

    // ── Stage 7: framebuffer overlay ──────────────────────────────────────

    void _paintFbOverlay() {
	if (!fbs_ || !fbs_->fbs_fbp || !bsg_v_ || !bsg_v_->gv_s)
	    return;
	if (!bsg_v_->gv_s->gv_fb_mode)
	    return;
	int fw = fb_getwidth(fbs_->fbs_fbp);
	int fh = fb_getheight(fbs_->fbs_fbp);
	if (fw <= 0 || fh <= 0)
	    return;
	/* Resize cached buffer only when dimensions change. */
	size_t needed = (size_t)fw * fh * 3;
	if (fb_buf_.size() != needed)
	    fb_buf_.resize(needed);
	int nread = fb_readrect(fbs_->fbs_fbp, 0, 0, fw, fh, fb_buf_.data());
	if (nread < fw * fh)
	    return;
	/* libfb origin is bottom-left; QImage origin is top-left — flip Y. */
	QImage img(fb_buf_.data(), fw, fh, fw * 3, QImage::Format_RGB888);
	QPainter p(this);
	p.drawImage(rect(), img.mirrored(false, true));
    }

    static void sensorQueueChangedCB(void *data) {
	QgObolSwrastView *w = static_cast<QgObolSwrastView*>(data);
	if (!w->idleTimer_.isActive())
	    w->idleTimer_.start(0);
    }

    static uint32_t allocCacheContext() {
	static std::atomic<uint32_t> ctx{0x8000};
	return ctx.fetch_add(1, std::memory_order_relaxed);
    }

    void onIdle()  { SoDB::getSensorManager()->processTimerQueue();
		     SoDB::getSensorManager()->processDelayQueue(TRUE); }
    void onDelay() { SoDB::getSensorManager()->processDelayQueue(FALSE); update(); }
    void onTimer() { SoDB::getSensorManager()->processTimerQueue();
		     QTimer::singleShot(0, this, &QgObolSwrastView::scheduleTimer); }
    void scheduleTimer() {
	SbTime t; SbBool b;
	if (SoDB::getSensorManager()->isTimerSensorPending(t))
	    timerTimer_.start((int)(t.getValue()*1000 + 0.5));
    }

    // ── Members ─────────────────────────────────────────────────────────

    bsg_view            *bsg_v_;
    SoSeparator         *obol_root_;
    bool                 quad_mode_;
    SoViewport           viewport_;
    SoQuadViewport       quad_viewport_;
    CoinOSMesaContextManager osmesa_ctx_mgr_;
    SoOffscreenRenderer *offscreen_;
    QTimer               idleTimer_, delayTimer_, timerTimer_;
    QPointF              lastMousePos_;
    bsg_shape           *selectedShape_ = nullptr;
    bool                 init_done_emitted_ = false;
    struct fbserv_obj   *fbs_ = nullptr;  /* Stage 7: embedded rt framebuffer (not owned) */
    std::vector<unsigned char> fb_buf_;   /* Stage 7: cached pixel buffer for fb overlay */
};

#endif /* OBOL_BUILD_DUAL_GL */




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
