/*                  Q G O B O L W I D G E T . H
 * BRL-CAD
 *
 * Copyright (c) 2021-2025 United States Government as represented by
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
/** @file qtcad/QgObolWidget.h
 *
 * @brief Qt/Obol integration widget for BRL-CAD scene rendering.
 *
 * QgObolWidget is the Obol-backed replacement for QgGL.  It renders a
 * BRL-CAD bv_scene via Obol's SoViewport + SoRenderManager APIs directly
 * in a QOpenGLWidget, without going through libdm.
 *
 * ### Architecture
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  QgObolWidget (QOpenGLWidget)                                        │
 * │    ├─ bv_render_ctx *ctx_          (libbv: bv_scene → SoSeparator)  │
 * │    ├─ SoViewport viewport_          (scene graph + camera)           │
 * │    ├─ SoRenderManager renderMgr_    (render modes / stereo)          │
 * │    ├─ QtObolContextManager ctxMgr_  (Qt GL context → Coin3D)         │
 * │    └─ struct bview_new *view_       (BRL-CAD view parameters)        │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 * The scene graph (SoSeparator*) is obtained from bv_render_ctx_scene_root()
 * — the same root that libbv builds from the bv_scene node tree, respecting
 * the proper OpenInventor hierarchy (comb→member→solid reflected as
 * SoSeparator→SoSeparator→SoCoordinate3+SoIndexedLineSet).
 *
 * ### Qt context manager
 *
 * QgObolWidget embeds a QtObolContextManager so that SoOffscreenRenderer
 * can share GL resources with the interactive viewport.  The context manager
 * is initialised in initializeGL() where the Qt GL context is available.
 *
 * ### Camera / view synchronisation
 *
 * When set_view() is called (or when the bview_new redraw callback fires),
 * the widget syncs the SoViewport camera to the BRL-CAD view parameters:
 * position, lookat, up-vector, FOV and perspective/orthographic mode.
 *
 * ### Render mode / stereo
 *
 * setRenderMode() and setStereoMode() map directly to SoRenderManager
 * methods, matching the feature set of the reference QtObolWidget in
 * obol/examples/qt_obol_widget.h.
 */

#pragma once

#include "common.h"

#ifdef BRLCAD_OPENGL

#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QActionGroup>
#include <QTimer>
#include <QCoreApplication>
#include <atomic>

extern "C" {
#include "bv.h"
#include "bv/render.h"
}

#include "qtcad/defines.h"

/* Obol / Inventor headers — confined to this translation unit and
 * libqtcad files that explicitly opt in.  Lower BRL-CAD libraries
 * (libged, libbv, librt …) remain Obol-free. */
#include <Inventor/SoDB.h>
#include <Inventor/SoViewport.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SbVec2s.h>
#include <Inventor/SbColor.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/nodekits/SoNodeKit.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/events/SoButtonEvent.h>
#include <Inventor/sensors/SoSensorManager.h>
#include <GL/gl.h>

/* ── QtObolContextManager ─────────────────────────────────────────────────
 * Identical to the reference implementation in obol/examples/qt_obol_widget.h.
 * Placed here so it is available to all libqtcad Qt/Obol widgets.          */
class QTCAD_EXPORT QtObolContextManager : public SoDB::ContextManager {
public:
    explicit QtObolContextManager(QOpenGLContext *shareCtx = nullptr)
	: shareCtx_(shareCtx) {}

    void *createOffscreenContext(unsigned int, unsigned int) override {
	Ctx *c = new Ctx;
	QSurfaceFormat fmt;
	fmt.setDepthBufferSize(24);
	fmt.setStencilBufferSize(8);

	c->surface = new QOffscreenSurface;
	c->surface->setFormat(fmt);
	c->surface->create();
	if (!c->surface->isValid()) {
	    delete c->surface; delete c; return nullptr;
	}

	c->context = new QOpenGLContext;
	c->context->setFormat(fmt);
	if (shareCtx_) c->context->setShareContext(shareCtx_);
	if (!c->context->create()) {
	    delete c->context; delete c->surface; delete c;
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
	else
	    c->context->doneCurrent();
    }

    void destroyContext(void *p) override {
	Ctx *c = static_cast<Ctx *>(p);
	c->context->doneCurrent();
	delete c->context;
	delete c->surface;
	delete c;
    }

    void *getProcAddress(const char *name) override {
	QOpenGLContext *cur = QOpenGLContext::currentContext();
	if (!cur) return nullptr;
	return reinterpret_cast<void *>(cur->getProcAddress(name));
    }

    void updateShareContext(QOpenGLContext *shareCtx) {
	shareCtx_ = shareCtx;
    }

private:
    struct Ctx {
	QOffscreenSurface *surface  = nullptr;
	QOpenGLContext    *context  = nullptr;
	QOpenGLContext    *prev     = nullptr;
	QSurface          *prevSurf = nullptr;
    };

    QOpenGLContext *shareCtx_;
};


/* ── QgObolWidget ─────────────────────────────────────────────────────────*/

class QTCAD_EXPORT QgObolWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit QgObolWidget(QWidget *parent = nullptr)
	: QOpenGLWidget(parent)
	, ctxMgr_(nullptr)
	, ctx_(nullptr)
	, view_(nullptr)
	, m_init(false)
    {
	setMouseTracking(true);
	setFocusPolicy(Qt::WheelFocus);

	/* Disable SoRenderManager's own auto-redraw: we drive the render loop
	 * explicitly via paintGL() so that Qt's vsync / repaint logic is used.
	 * This mirrors the pattern in obol/examples/qt_obol_widget.h. */
	SoRenderManager::enableRealTimeUpdate(FALSE);

	/* Three-timer sensor bridge — same pattern as Quarter's SensorManager
	 * and the reference QtObolWidget example. */
	idleTimer_.setSingleShot(true);
	delayTimer_.setSingleShot(true);
	timerTimer_.setSingleShot(true);
	connect(&idleTimer_,  &QTimer::timeout, this, &QgObolWidget::onIdle);
	connect(&delayTimer_, &QTimer::timeout, this, &QgObolWidget::onDelay);
	connect(&timerTimer_, &QTimer::timeout, this, &QgObolWidget::onTimer);
    }

    ~QgObolWidget() override {
	if (SoDB::isInitialized())
	    SoDB::getSensorManager()->setChangedCallback(nullptr, nullptr);
	bv_render_ctx_destroy(ctx_);
    }

    /* ── Scene ──────────────────────────────────────────────────────── */

    /**
     * Attach a BRL-CAD render context.  The SoSeparator* root from
     * bv_render_ctx_scene_root(ctx) is fed to viewport_.setSceneGraph().
     * A default camera is added via viewport_.viewAll() when none is found.
     */
    void setRenderCtx(struct bv_render_ctx *ctx) {
	ctx_ = ctx;
	if (!ctx_) {
	    viewport_.setSceneGraph(nullptr);
	    renderMgr_.setSceneGraph(nullptr);
	    return;
	}

	SoNode *root = static_cast<SoNode *>(bv_render_ctx_scene_root(ctx_));
	if (!root)
	    return;

	viewport_.setSceneGraph(root);
	/* Camera: viewAll() auto-creates one if absent */
	if (!viewport_.getCamera())
	    viewport_.viewAll();

	/* SoRenderManager renders viewport_.getRoot() (= [camera + scene]) */
	renderMgr_.setSceneGraph(viewport_.getRoot());
	update();
    }

    struct bv_render_ctx *renderCtx() const { return ctx_; }

    /**
     * Associate a BRL-CAD view (camera / viewport parameters).
     * The widget will sync SoViewport's camera to this view on each
     * paintGL() call.  May be NULL.
     */
    void set_view(struct bview_new *v) {
	view_ = v;
	if (v) {
	    /* Install a redraw callback so external changes trigger a repaint. */
	    bview_redraw_callback_set(v, redraw_cb, this);
	}
    }
    struct bview_new *view() const { return view_; }

    /* ── SoViewport / SoRenderManager access ────────────────────────── */
    SoViewport       &getViewport()       { return viewport_; }
    const SoViewport &getViewport() const { return viewport_; }
    SoRenderManager  &getRenderManager()  { return renderMgr_; }

    /* ── Render mode ────────────────────────────────────────────────── */
    void setRenderMode(SoRenderManager::RenderMode mode) {
	renderMgr_.setRenderMode(mode);
	update();
    }
    SoRenderManager::RenderMode getRenderMode() const {
	return renderMgr_.getRenderMode();
    }

    /* ── Stereo mode ────────────────────────────────────────────────── */
    void setStereoMode(SoRenderManager::StereoMode mode) {
	renderMgr_.setStereoMode(mode);
	update();
    }
    SoRenderManager::StereoMode getStereoMode() const {
	return renderMgr_.getStereoMode();
    }

    /* ── Transparency type ──────────────────────────────────────────── */
    void setTransparencyType(SoGLRenderAction::TransparencyType t) {
	renderMgr_.getGLRenderAction()->setTransparencyType(t);
	update();
    }
    SoGLRenderAction::TransparencyType getTransparencyType() const {
	return renderMgr_.getGLRenderAction()->getTransparencyType();
    }

    /* ── Background colour ──────────────────────────────────────────── */
    void setBackgroundColor(const SbColor &c) {
	viewport_.setBackgroundColor(c);
	renderMgr_.setBackgroundColor(SbColor4f(c, 1.0f));
	update();
    }

    /* ── current flag (for multi-view selection) ────────────────────── */
    void set_current(int i) { current_ = i; }
    int  current()    const { return current_; }

    /* ── Hashing helpers for change detection ───────────────────────── */
    void stash_hashes() {
	/* TODO: hash scene/view state (camera matrix, bv_node dlist_stale
	 * flags) for use in diff_hashes() to skip unnecessary redraws. */
    }
    bool diff_hashes()  {
	/* TODO: return true when scene/view state differs from last stash */
	return false;
    }

    void need_update() { update(); }

public slots:
    void viewAll() {
	viewport_.viewAll();
	update();
    }

signals:
    void changed();
    void init_done();

protected:
    /* ── QOpenGLWidget overrides ────────────────────────────────────── */

    void initializeGL() override {
	glEnable(GL_DEPTH_TEST);

	/* Wire the GL render action cache context to Qt's context */
	QOpenGLContext *qctx = QOpenGLContext::currentContext();
	renderMgr_.getGLRenderAction()->setCacheContext(cacheContext_);

	/* Now that we have a GL context, initialise SoDB (once per process).
	 * Update the context manager share context so off-screen renders can
	 * share textures/VBOs with this widget. */
	if (!ctxMgr_) {
	    ctxMgr_ = new QtObolContextManager(qctx);
	    SoDB::init(ctxMgr_);
	    SoNodeKit::init();
	    SoInteraction::init();

	    /* Install sensor bridge now that SoDB is ready */
	    SoDB::getSensorManager()->setChangedCallback(sensorQueueChangedCB, this);
	} else {
	    ctxMgr_->updateShareContext(qctx);
	}

	m_init = true;
	emit init_done();
    }

    void resizeGL(int w, int h) override {
	const qreal dpr = devicePixelRatioF();
	SbVec2s physSize((short)(w * dpr), (short)(h * dpr));
	viewport_.setWindowSize(physSize);
	renderMgr_.setWindowSize(physSize);
	renderMgr_.setViewportRegion(viewport_.getViewportRegion());

	if (view_) {
	    struct bview_viewport vp;
	    vp.width  = (int)(w * dpr);
	    vp.height = (int)(h * dpr);
	    vp.dpi    = (double)physicalDpiX();
	    bview_viewport_set(view_, &vp);
	}
    }

    void paintGL() override {
	if (!m_init || !ctx_)
	    return;

	/* Sync stale scene nodes and apply camera from view_ */
	bv_render_frame(ctx_, view_);

	/* Keep SoRenderManager in sync with SoViewport */
	renderMgr_.setViewportRegion(viewport_.getViewportRegion());

	/* Render: SoRenderManager handles render mode overrides and stereo */
	renderMgr_.render(/*clearwindow=*/TRUE, /*clearzbuffer=*/TRUE);
    }

    /* ── Mouse events ───────────────────────────────────────────────── */

    void mousePressEvent(QMouseEvent *e) override {
	lastMousePos_ = e->pos();
	SoMouseButtonEvent ev;
	ev.setPosition(mapToObol(e->pos()));
	ev.setState(SoButtonEvent::DOWN);
	ev.setButton(qtButtonToCoin(e->button()));
	viewport_.processEvent(&ev);
	update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
	SoMouseButtonEvent ev;
	ev.setPosition(mapToObol(e->pos()));
	ev.setState(SoButtonEvent::UP);
	ev.setButton(qtButtonToCoin(e->button()));
	viewport_.processEvent(&ev);
	update();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
	/* Left-button drag → orbit */
	if (e->buttons() & Qt::LeftButton) {
	    SoCamera *cam = viewport_.getCamera();
	    if (cam) {
		const QPoint delta = e->pos() - lastMousePos_;
		SbVec3f lookDir;
		cam->orientation.getValue().multVec(SbVec3f(0,0,-1), lookDir);
		const SbVec3f orbit =
		    cam->position.getValue() + lookDir * cam->focalDistance.getValue();
		cam->orbitCamera(orbit, (float)delta.x(), (float)delta.y());
		lastMousePos_ = e->pos();
		update();
		return;
	    }
	}
	/* Middle-button drag → pan */
	if (e->buttons() & Qt::MiddleButton) {
	    SoCamera *cam = viewport_.getCamera();
	    if (cam) {
		const QPoint delta = e->pos() - lastMousePos_;
		SbVec3f right, up;
		cam->orientation.getValue().multVec(SbVec3f(1,0,0), right);
		cam->orientation.getValue().multVec(SbVec3f(0,1,0), up);
		const float scale = cam->focalDistance.getValue() * 0.001f;
		cam->position.setValue(cam->position.getValue() +
				       right * (-delta.x() * scale) +
				       up   * ( delta.y() * scale));
		lastMousePos_ = e->pos();
		update();
		return;
	    }
	}
	SoLocation2Event ev;
	ev.setPosition(mapToObol(e->pos()));
	viewport_.processEvent(&ev);
	update();
    }

    void wheelEvent(QWheelEvent *e) override {
	int delta = e->angleDelta().y();
	if (delta == 0) delta = e->angleDelta().x();
	SoCamera *cam = viewport_.getCamera();
	if (cam) {
	    SbVec3f look;
	    cam->orientation.getValue().multVec(SbVec3f(0,0,-1), look);
	    const float step = cam->focalDistance.getValue() * (delta > 0 ? 0.1f : -0.1f);
	    cam->position.setValue(cam->position.getValue() + look * step);
	    cam->focalDistance.setValue(cam->focalDistance.getValue() - step);
	    update();
	}
    }

    /* ── Keyboard events ────────────────────────────────────────────── */

    void keyPressEvent(QKeyEvent *e) override {
	SoKeyboardEvent ev;
	ev.setState(SoButtonEvent::DOWN);
	ev.setKey(qtKeyToCoin(e->key()));
	viewport_.processEvent(&ev);
    }

    void keyReleaseEvent(QKeyEvent *e) override {
	SoKeyboardEvent ev;
	ev.setState(SoButtonEvent::UP);
	ev.setKey(qtKeyToCoin(e->key()));
	viewport_.processEvent(&ev);
    }

    /* ── Context menu ───────────────────────────────────────────────── */

    void contextMenuEvent(QContextMenuEvent *e) override {
	QMenu menu(this);

	QAction *va = menu.addAction("View All");
	connect(va, &QAction::triggered, this, &QgObolWidget::viewAll);
	menu.addSeparator();

	/* Render mode submenu */
	QMenu *rmMenu = menu.addMenu("Render Mode");
	QActionGroup *rmGrp = new QActionGroup(rmMenu);
	auto addRM = [&](const char *label, SoRenderManager::RenderMode mode) {
	    QAction *a = rmMenu->addAction(label);
	    a->setCheckable(true);
	    a->setChecked(renderMgr_.getRenderMode() == mode);
	    a->setActionGroup(rmGrp);
	    a->setData(static_cast<int>(mode));
	};
	addRM("As Is",             SoRenderManager::AS_IS);
	addRM("Wireframe",         SoRenderManager::WIREFRAME);
	addRM("Wireframe Overlay", SoRenderManager::WIREFRAME_OVERLAY);
	addRM("Points",            SoRenderManager::POINTS);
	addRM("Hidden Line",       SoRenderManager::HIDDEN_LINE);
	addRM("Bounding Box",      SoRenderManager::BOUNDING_BOX);
	connect(rmGrp, &QActionGroup::triggered, this,
		[this](QAction *a) {
		    setRenderMode(static_cast<SoRenderManager::RenderMode>(a->data().toInt()));
		});

	menu.exec(e->globalPos());
    }

private:
    /* ── Sensor bridge (three-timer pattern from Quarter/QtObolWidget) ─ */
    QTimer idleTimer_;
    QTimer delayTimer_;
    QTimer timerTimer_;

    static void sensorQueueChangedCB(void *data) {
	QgObolWidget *w = static_cast<QgObolWidget *>(data);
	SoSensorManager *mgr = SoDB::getSensorManager();

	SbTime delayT, timerT;
	mgr->getTimerInterval(timerT);
	mgr->getDelaySensorTimeout(delayT);

	if (!timerT.getValue())
	    w->timerTimer_.start(0);
	else
	    w->timerTimer_.start((int)(timerT.getValue() * 1000.0));

	if (!delayT.getValue())
	    w->delayTimer_.start(0);
	else
	    w->delayTimer_.start((int)(delayT.getValue() * 1000.0));

	if (mgr->isIdleTimerActive())
	    w->idleTimer_.start(0);
    }

    void onIdle()  { SoDB::getSensorManager()->processIdleSensors(); update(); }
    void onDelay() { SoDB::getSensorManager()->processDelayQueue(FALSE); }
    void onTimer() {
	SoDB::getSensorManager()->processTimerQueue();
	SbTime next;
	SoDB::getSensorManager()->getTimerInterval(next);
	if (next.getValue() > 0.0)
	    timerTimer_.start((int)(next.getValue() * 1000.0));
	update();
    }

    /* ── Obol event helpers ─────────────────────────────────────────── */

    SbVec2s mapToObol(const QPoint &p) const {
	/* Obol origin is bottom-left; Qt is top-left */
	return SbVec2s((short)p.x(), (short)(height() - p.y() - 1));
    }

    static SoMouseButtonEvent::Button qtButtonToCoin(Qt::MouseButton b) {
	switch (b) {
	case Qt::LeftButton:   return SoMouseButtonEvent::BUTTON1;
	case Qt::MiddleButton: return SoMouseButtonEvent::BUTTON2;
	case Qt::RightButton:  return SoMouseButtonEvent::BUTTON3;
	default:               return SoMouseButtonEvent::ANY;
	}
    }

    static SoKeyboardEvent::Key qtKeyToCoin(int k) {
	/* Minimal mapping — extend as needed */
	switch (k) {
	case Qt::Key_Escape: return SoKeyboardEvent::ESCAPE;
	case Qt::Key_Return: return SoKeyboardEvent::RETURN;
	case Qt::Key_Space:  return SoKeyboardEvent::SPACE;
	default:             return SoKeyboardEvent::ANY;
	}
    }

    /* ── Display-list / cache context ──────────────────────────────── */
    static unsigned int allocCacheContext() {
	static std::atomic<unsigned int> s_next{1};
	return s_next.fetch_add(1, std::memory_order_relaxed);
    }
    unsigned int cacheContext_ = allocCacheContext();

    /* ── Redraw callback registered on bview_new ────────────────────── */
    static void redraw_cb(struct bview_new *, void *data) {
	QgObolWidget *w = static_cast<QgObolWidget *>(data);
	w->update();
	emit w->changed();
    }

    /* ── Data members ───────────────────────────────────────────────── */
    QtObolContextManager *ctxMgr_;
    struct bv_render_ctx *ctx_;
    struct bview_new     *view_;
    SoViewport            viewport_;
    SoRenderManager       renderMgr_;
    bool                  m_init;
    int                   current_ = 0;
    QPoint                lastMousePos_;
};

#endif /* BRLCAD_OPENGL */

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
