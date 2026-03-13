/* QgObolView.h — BRL-CAD Qt/Obol 3D view widget
 *
 * This header provides QgObolView: a BRL-CAD-integrated Qt widget that
 * uses Obol (Open Inventor fork) for 3D scene rendering.  It replaces the
 * libdm-based QgView widget in qged.
 *
 * Architecture
 * ─────────────
 *
 *   QgObolView (QOpenGLWidget)
 *     ├─ SoViewport       — scene graph, camera, viewport region, event routing
 *     ├─ SoRenderManager  — GL render passes, render mode, stereo
 *     ├─ SoSeparator*     — per-view Obol scene root (from obol_scene_create())
 *     ├─ bsg_view*        — BRL-CAD view state (camera, tolerances, …)
 *     └─ three QTimers    — Obol sensor-queue bridge (idle / delay / timer)
 *
 * Usage
 * ─────
 *
 *   // In application startup (before creating any QgObolView):
 *   QgObolContextManager ctxMgr;
 *   SoDB::init(&ctxMgr);
 *   SoNodeKit::init();
 *   SoInteraction::init();
 *   bsg_obol_set_unref([](void *p){ static_cast<SoNode*>(p)->unref(); });
 *
 *   // Create a view:
 *   QgObolView *view = new QgObolView(parent);
 *   view->setBsgView(gedp->ged_gvp);
 *   view->redraw();          // triggers obol_scene_assemble + SoRenderManager render
 *
 * Camera synchronisation
 * ──────────────────────
 *
 *   BRL-CAD commands (ae, zoom, rot, …) update the bsg_view fields
 *   (gv_rotation, gv_center, gv_size).  After each command QgEdApp calls
 *   do_view_changed() → QgObolView::syncCameraFromBsgView() which reads those
 *   fields and writes them to the Obol SoPerspectiveCamera / SoOrthographicCamera.
 *
 *   Mouse navigation in QgObolView does the reverse: camera changes made by
 *   Obol dragging are reflected back to bsg_view via syncBsgViewFromCamera().
 *
 * See RADICAL_MIGRATION.md Stage 0 for context.
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

#include <Inventor/SoDB.h>
#include <Inventor/SoViewport.h>
#include <Inventor/SoRenderManager.h>
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
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/events/SoKeyboardEvent.h>

#include <GL/gl.h>
#include <cmath>
#include <atomic>

#include "bsg.h"
#include "bsg/util.h"
#include "obol_scene.h"
#include "vmath.h"

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
// QgObolView
//
// BRL-CAD Qt widget for Obol-based 3D rendering.  Replaces QgView (libdm).
// ============================================================================

class QgObolView : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit QgObolView(QWidget *parent = nullptr)
	: QOpenGLWidget(parent)
	, bsg_v_(nullptr)
	, obol_root_(nullptr)
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
	if (!obol_root_) {
	    obol_root_ = obol_scene_create();
	    setObolSceneGraph(obol_root_);
	}
    }

    bsg_view *getBsgView() const { return bsg_v_; }

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

	SoCamera *cam = viewport_.getCamera();
	if (!cam)
	    return;

	/* Extract eye point, focal point, and up direction from bsg_view
	 * view-to-model matrix. */
	mat_t v2m;
	MAT_COPY(v2m, bsg_v_->gv_view2model);

	/* Eye is at view origin (0,0,0) mapped through view2model */
	point_t eye;
	VSET(eye, 0, 0, 0);
	point_t eye_w;
	MAT4X3PNT(eye_w, v2m, eye);

	/* Look direction: -Z in view space */
	vect_t look_v;
	VSET(look_v, 0, 0, -1);
	vect_t look_w;
	MAT4X3VEC(look_w, v2m, look_v);
	VUNITIZE(look_w);

	/* Up direction: +Y in view space */
	vect_t up_v;
	VSET(up_v, 0, 1, 0);
	vect_t up_w;
	MAT4X3VEC(up_w, v2m, up_v);
	VUNITIZE(up_w);

	cam->position.setValue((float)eye_w[0], (float)eye_w[1], (float)eye_w[2]);

	/* Build camera orientation from look direction and up vector.
	 * Construct the rotation as: from canonical forward (0,0,-1) and up
	 * (0,1,0) to the world-space look/up directions derived from bsg_view. */
	SbVec3f look_sb((float)look_w[0], (float)look_w[1], (float)look_w[2]);
	SbVec3f up_sb((float)up_w[0],   (float)up_w[1],   (float)up_w[2]);
	/* Right = look × up */
	SbVec3f right_sb = look_sb.cross(up_sb);
	right_sb.normalize();
	/* Reorthogonalise up = right × look */
	up_sb = right_sb.cross(look_sb);
	up_sb.normalize();
	/* Build rotation matrix columns: [right, up, -look] */
	SbMatrix rot_mat(
	     right_sb[0],  right_sb[1],  right_sb[2], 0.0f,
	       up_sb[0],    up_sb[1],    up_sb[2],    0.0f,
	    -look_sb[0], -look_sb[1], -look_sb[2],   0.0f,
	          0.0f,       0.0f,        0.0f,      1.0f
	);
	SbRotation orient(rot_mat);
	cam->orientation.setValue(orient);

	cam->focalDistance = (float)bsg_v_->gv_size;

	if (cam->getTypeId() == SoPerspectiveCamera::getClassTypeId()) {
	    SoPerspectiveCamera *pcam = static_cast<SoPerspectiveCamera *>(cam);
	    fastf_t persp = bsg_v_->gv_perspective;
	    if (persp > SMALL_FASTF)
		pcam->heightAngle = (float)(persp * DEG2RAD);
	    else
		pcam->heightAngle = (float)(45.0 * DEG2RAD);
	}
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

    // ── Render settings ──────────────────────────────────────────────────

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
    void viewAll() {
	viewport_.viewAll();
	update();
    }

protected:
    // ── QOpenGLWidget overrides ──────────────────────────────────────────

    void initializeGL() override {
	glEnable(GL_DEPTH_TEST);
	renderMgr_.getGLRenderAction()->setCacheContext(cacheContext_);
    }

    void resizeGL(int w, int h) override {
	const qreal dpr = devicePixelRatioF();
	SbVec2s physSize((short)(w * dpr), (short)(h * dpr));
	viewport_.setWindowSize(physSize);
	renderMgr_.setWindowSize(physSize);
	renderMgr_.setViewportRegion(viewport_.getViewportRegion());
    }

    void paintGL() override {
	SoGLRenderAction *ra = renderMgr_.getGLRenderAction();
	ra->setViewportRegion(viewport_.getViewportRegion());

	renderMgr_.setSceneGraph(viewport_.getRoot());
	renderMgr_.render(ra, TRUE /* clearZ */, TRUE /* clearWindow */);
    }

    // ── Mouse navigation ─────────────────────────────────────────────────

    void mousePressEvent(QMouseEvent *e) override {
	lastMousePos_ = e->position();

	SoMouseButtonEvent ev;
	ev.setPosition(SbVec2s((short)e->position().x(), (short)e->position().y()));
	if (e->button() == Qt::LeftButton)
	    ev.setButton(SoMouseButtonEvent::BUTTON1);
	else if (e->button() == Qt::RightButton)
	    ev.setButton(SoMouseButtonEvent::BUTTON3);
	else if (e->button() == Qt::MiddleButton)
	    ev.setButton(SoMouseButtonEvent::BUTTON2);
	ev.setState(SoButtonEvent::DOWN);
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
	viewport_.processEvent(&ev);
	update();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
	QPointF delta = e->position() - lastMousePos_;
	lastMousePos_ = e->position();

	SoCamera *cam = viewport_.getCamera();
	if (!cam) return;

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
	} else if (e->buttons() & Qt::MiddleButton) {
	    /* Pan: translate camera in view plane */
	    float scale = cam->focalDistance.getValue() * 0.001f;
	    SbVec3f right, up, look;
	    cam->orientation.getValue().multVec(SbVec3f(1, 0, 0), right);
	    cam->orientation.getValue().multVec(SbVec3f(0, 1, 0), up);
	    SbVec3f pan = right * (-(float)delta.x() * scale)
		       + up   *  ((float)delta.y() * scale);
	    cam->position = cam->position.getValue() + pan;
	}

	/* Route hover location to scene for dragger/selection highlight */
	SoLocation2Event le;
	le.setPosition(SbVec2s((short)e->position().x(), (short)e->position().y()));
	viewport_.processEvent(&le);

	update();
    }

    void wheelEvent(QWheelEvent *e) override {
	SoCamera *cam = viewport_.getCamera();
	if (!cam) return;

	float delta = e->angleDelta().y() / 120.0f;
	SbVec3f look;
	cam->orientation.getValue().multVec(SbVec3f(0, 0, -1), look);
	float step = cam->focalDistance.getValue() * 0.1f * delta;
	cam->position = cam->position.getValue() + look * step;
	cam->focalDistance = cam->focalDistance.getValue() - step;
	if (cam->focalDistance.getValue() < 0.001f)
	    cam->focalDistance = 0.001f;
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
	SbTime t = SoDB::getSensorManager()->getNextTimerInterval();
	if (t != SbTime::max())
	    timerTimer_.start((int)(t.getValue() * 1000));
    }

    // ── GL cache context ──────────────────────────────────────────────────

    static uint32_t allocCacheContext() {
	static std::atomic<uint32_t> ctx{1};
	return ctx.fetch_add(1, std::memory_order_relaxed);
    }

    // ── Members ───────────────────────────────────────────────────────────

    bsg_view        *bsg_v_;       /* BRL-CAD view (not owned) */
    SoSeparator     *obol_root_;   /* Obol scene root (owned, ref counted) */
    SoViewport       viewport_;
    SoRenderManager  renderMgr_;
    QTimer           idleTimer_, delayTimer_, timerTimer_;
    QPointF          lastMousePos_;
    uint32_t         cacheContext_ = allocCacheContext();
};

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
