/*               Q G O B O L Q U A D W I D G E T . H
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
/** @file qtcad/QgObolQuadWidget.h
 *
 * @brief Four-quadrant Obol-backed view widget using SoQuadViewport.
 *
 * QgObolQuadWidget replaces QgQuadView for the Obol rendering path.  It
 * manages four independent SoViewport instances through Obol's SoQuadViewport,
 * all sharing the same scene graph (SoSeparator* from bv_render_ctx).
 *
 * ### Layout
 *
 * @verbatim
 *  ┌──────────────────┬──────────────────┐
 *  │  BV_QUAD_TOP_LEFT│ BV_QUAD_TOP_RIGHT │
 *  │    (ul / Q1)     │    (ur / Q0)      │  ← these map to QgQuadView
 *  ├──────────────────┼──────────────────┤     quadrant indices, matching
 *  │BV_QUAD_BOT_LEFT  │ BV_QUAD_BOT_RIGHT │     bv/render.h BV_QUAD_*
 *  │    (ll / Q2)     │    (lr / Q3)      │
 *  └──────────────────┴──────────────────┘
 * @endverbatim
 *
 * ### Sharing a scene with QgObolWidget
 *
 * The SoSeparator* can be obtained from bv_render_ctx_scene_root() and set
 * on both a QgObolWidget (single-view) and QgObolQuadWidget (quad-view),
 * allowing them to share geometry without duplication.
 */

#pragma once

#include "common.h"

#ifdef BRLCAD_OPENGL

#include <QOpenGLWidget>
#include <QGridLayout>
#include <QFrame>

extern "C" {
#include "bv.h"
#include "bv/render.h"
}

#include "qtcad/defines.h"
#include "qtcad/QgObolWidget.h"  /* for QtObolContextManager */

/* Obol headers are third-party code; suppress BRL-CAD's strict warnings. */
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-equal"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wdeprecated-copy"
#endif
#include <Inventor/SoQuadViewport.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbVec2s.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/actions/SoGLRenderAction.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif


/**
 * @brief Four-quadrant Obol view widget.
 *
 * Internally uses one QOpenGLWidget that renders all four quadrants in a
 * single GL surface via SoQuadViewport.  This keeps GL context management
 * simple while still providing four independently-controlled views.
 */
class QTCAD_EXPORT QgObolQuadWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    /**
     * @param parent       Parent widget.
     * @param ctx          bv_render_ctx providing the shared scene graph.
     * @param views        Four bview_new* (one per quadrant, may be NULL).
     */
    explicit QgObolQuadWidget(QWidget *parent,
			      struct bv_render_ctx *ctx,
			      struct bview_new *views[BV_QUAD_NUM_QUADS] = nullptr)
	: QOpenGLWidget(parent)
	, ctxMgr_(nullptr)
	, ctx_(ctx)
	, m_init(false)
	, activeQuad_(BV_QUAD_TOP_RIGHT)
    {
	for (int i = 0; i < BV_QUAD_NUM_QUADS; i++)
	    views_[i] = views ? views[i] : nullptr;

	setMouseTracking(true);
	setFocusPolicy(Qt::WheelFocus);
	SoRenderManager::enableRealTimeUpdate(FALSE);
    }

    ~QgObolQuadWidget() override {
	/* Unreference the render manager scenes */
	for (int i = 0; i < BV_QUAD_NUM_QUADS; i++)
	    renderMgrs_[i].setSceneGraph(nullptr);
    }

    /* ── Scene ──────────────────────────────────────────────────────── */

    void setRenderCtx(struct bv_render_ctx *ctx) {
	ctx_ = ctx;
	if (!m_init)
	    return;
	_rebuildScene();
    }

    struct bv_render_ctx *renderCtx() const { return ctx_; }

    void set_view(int quad, struct bview_new *v) {
	if (quad < 0 || quad >= BV_QUAD_NUM_QUADS)
	    return;
	views_[quad] = v;
	if (v) {
	    bview_redraw_callback_set(v, redraw_cb, this);
	}
    }
    struct bview_new *view(int quad) const {
	return (quad >= 0 && quad < BV_QUAD_NUM_QUADS) ? views_[quad] : nullptr;
    }

    /* ── Active quadrant ────────────────────────────────────────────── */
    void   setActiveQuad(int q) { activeQuad_ = q; update(); }
    int    activeQuad()   const { return activeQuad_; }

    /* ── SoQuadViewport access ──────────────────────────────────────── */
    SoQuadViewport       &getQuadViewport()       { return quadVp_; }
    const SoQuadViewport &getQuadViewport() const { return quadVp_; }

    /* ── Per-quadrant render manager ────────────────────────────────── */
    SoRenderManager &getRenderManager(int quad) { return renderMgrs_[quad]; }

    /* ── Standard BV quad indices (convenience aliases) ─────────────── */
    enum QuadId {
	UPPER_RIGHT = BV_QUAD_TOP_RIGHT,
	UPPER_LEFT  = BV_QUAD_TOP_LEFT,
	LOWER_LEFT  = BV_QUAD_BOTTOM_LEFT,
	LOWER_RIGHT = BV_QUAD_BOTTOM_RIGHT
    };

    /**
     * Set standard default AET values for the four quadrants:
     *   Q0 (upper-right) = (35, 25)   — perspective
     *   Q1 (upper-left)  = top view
     *   Q2 (lower-left)  = front view
     *   Q3 (lower-right) = right view
     */
    void default_views() {
	for (int q = 0; q < BV_QUAD_NUM_QUADS; q++) {
	    SoViewport *vp = quadVp_.getViewport(q);
	    if (!vp) continue;
	    SoCamera *cam = vp->getCamera();
	    if (!cam) {
		SoPerspectiveCamera *pc = new SoPerspectiveCamera;
		vp->setCamera(pc);
		cam = pc;
	    }
	    /* Default orientations matching BRL-CAD conventions */
	    switch (q) {
	    case BV_QUAD_TOP_RIGHT:  /* Perspective */
		cam->position.setValue(200.0f, -200.0f, 200.0f);
		cam->pointAt(SbVec3f(0,0,0), SbVec3f(0,0,1));
		break;
	    case BV_QUAD_TOP_LEFT:   /* Top */
		cam->position.setValue(0, 0, 500.0f);
		cam->pointAt(SbVec3f(0,0,0), SbVec3f(0,1,0));
		break;
	    case BV_QUAD_BOTTOM_LEFT: /* Front */
		cam->position.setValue(0, -500.0f, 0);
		cam->pointAt(SbVec3f(0,0,0), SbVec3f(0,0,1));
		break;
	    case BV_QUAD_BOTTOM_RIGHT: /* Right */
		cam->position.setValue(500.0f, 0, 0);
		cam->pointAt(SbVec3f(0,0,0), SbVec3f(0,0,1));
		break;
	    }
	    vp->viewAll();
	}
	update();
    }

    void viewAll() {
	quadVp_.viewAllQuadrants();
	update();
    }

    /* ── Misc ───────────────────────────────────────────────────────── */
    void stash_hashes() {
	/* TODO: hash scene/view state for change detection (camera matrices,
	 * bv_node dlist_stale flags) so that paintGL can skip rendering
	 * when nothing has changed. */
    }
    bool diff_hashes()  {
	/* TODO: return true when scene/view state differs from last stash */
	return false;
    }
    bool isValid()      { return m_init; }

signals:
    void changed();
    void init_done();
    void quadSelected(int quad);

protected:
    /* ── QOpenGLWidget overrides ────────────────────────────────────── */

    void initializeGL() override {
	glEnable(GL_DEPTH_TEST);

	QOpenGLContext *qctx = QOpenGLContext::currentContext();
	if (!ctxMgr_) {
	    ctxMgr_ = new QtObolContextManager(qctx);
	    SoDB::init(ctxMgr_);
	    SoNodeKit::init();
	    SoInteraction::init();
	} else {
	    ctxMgr_->updateShareContext(qctx);
	}

	_rebuildScene();

	m_init = true;
	emit init_done();
    }

    void resizeGL(int w, int h) override {
	const qreal dpr = devicePixelRatioF();
	SbVec2s physSize((short)(w * dpr), (short)(h * dpr));
	quadVp_.setWindowSize(physSize);
	for (int q = 0; q < BV_QUAD_NUM_QUADS; q++) {
	    renderMgrs_[q].setWindowSize(physSize);
	    SoViewport *vp = quadVp_.getViewport(q);
	    if (vp)
		renderMgrs_[q].setViewportRegion(vp->getViewportRegion());
	}
    }

    void paintGL() override {
	if (!m_init)
	    return;

	/* Sync scene from bv_render_ctx */
	if (ctx_)
	    bv_render_frame(ctx_, nullptr);

	/* Render each quadrant */
	for (int q = 0; q < BV_QUAD_NUM_QUADS; q++) {
	    SoViewport *vp = quadVp_.getViewport(q);
	    if (!vp) continue;

	    /* Sync camera from associated bview_new if present */
	    if (views_[q])
		_syncCamera(vp, views_[q]);

	    renderMgrs_[q].setViewportRegion(vp->getViewportRegion());
	    renderMgrs_[q].render(TRUE, TRUE);
	}

	/* Highlight active quadrant border */
	_drawActiveBorder();
    }

    void mousePressEvent(QMouseEvent *e) override {
	int quad = _hitTestQuad(e->pos());
	if (quad >= 0 && quad != activeQuad_) {
	    activeQuad_ = quad;
	    emit quadSelected(quad);
	    update();
	}
	SoViewport *vp = quadVp_.getViewport(activeQuad_);
	if (vp) {
	    SoMouseButtonEvent ev;
	    ev.setPosition(_mapToQuad(e->pos(), activeQuad_));
	    ev.setState(SoButtonEvent::DOWN);
	    ev.setButton(e->button() == Qt::LeftButton   ? SoMouseButtonEvent::BUTTON1 :
			 e->button() == Qt::MiddleButton ? SoMouseButtonEvent::BUTTON2 :
							  SoMouseButtonEvent::BUTTON3);
	    vp->processEvent(&ev);
	}
	lastMousePos_ = e->pos();
	update();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
	SoViewport *vp = quadVp_.getViewport(activeQuad_);
	if (!vp) { lastMousePos_ = e->pos(); return; }

	if (e->buttons() & Qt::LeftButton) {
	    SoCamera *cam = vp->getCamera();
	    if (cam) {
		QPoint delta = e->pos() - lastMousePos_;
		SbVec3f look;
		cam->orientation.getValue().multVec(SbVec3f(0,0,-1), look);
		SbVec3f orbit = cam->position.getValue()
		    + look * cam->focalDistance.getValue();
		cam->orbitCamera(orbit, (float)delta.x(), (float)delta.y());
		update();
	    }
	}
	lastMousePos_ = e->pos();
    }

    void wheelEvent(QWheelEvent *e) override {
	int delta = e->angleDelta().y();
	SoViewport *vp = quadVp_.getViewport(activeQuad_);
	if (vp) {
	    SoCamera *cam = vp->getCamera();
	    if (cam) {
		SbVec3f look;
		cam->orientation.getValue().multVec(SbVec3f(0,0,-1), look);
		float step = cam->focalDistance.getValue() * (delta > 0 ? 0.1f : -0.1f);
		cam->position.setValue(cam->position.getValue() + look * step);
		cam->focalDistance.setValue(cam->focalDistance.getValue() - step);
		update();
	    }
	}
    }

private:
    /* Build the Inventor scene in all four quadrant viewports */
    void _rebuildScene() {
	if (!ctx_)
	    return;

	SoNode *root = static_cast<SoNode *>(bv_render_ctx_scene_root(ctx_));
	if (!root)
	    return;

	SbVec2s sz((short)width(), (short)height());
	quadVp_.setWindowSize(sz);
	quadVp_.setSceneGraph(root);
	if (!quadVp_.getViewport(0) || !quadVp_.getViewport(0)->getCamera())
	    quadVp_.viewAllQuadrants();

	for (int q = 0; q < BV_QUAD_NUM_QUADS; q++) {
	    SoViewport *vp = quadVp_.getViewport(q);
	    if (!vp) continue;
	    renderMgrs_[q].setSceneGraph(vp->getRoot());
	    renderMgrs_[q].setWindowSize(sz);
	    renderMgrs_[q].setViewportRegion(vp->getViewportRegion());
	}
    }

    /* Sync SoViewport camera from bview_new */
    static void _syncCamera(SoViewport *vp, struct bview_new *view) {
	const struct bview_camera *cam = bview_camera_get(view);
	if (!cam) return;
	SoCamera *sc = vp->getCamera();
	if (!sc) {
	    sc = new SoPerspectiveCamera;
	    vp->setCamera(sc);
	}
	sc->position.setValue((float)cam->position[0],
			      (float)cam->position[1],
			      (float)cam->position[2]);
	SbVec3f viewdir(
	    (float)(cam->target[0] - cam->position[0]),
	    (float)(cam->target[1] - cam->position[1]),
	    (float)(cam->target[2] - cam->position[2]));
	if (viewdir.length() > 1e-10f) {
	    viewdir.normalize();
	    sc->orientation.setValue(SbRotation(SbVec3f(0,0,-1), viewdir));
	}
    }

    int _hitTestQuad(const QPoint &p) const {
	int w2 = width()  / 2;
	int h2 = height() / 2;
	bool right  = (p.x() >= w2);
	bool bottom = (p.y() >= h2);
	if (!right && !bottom) return BV_QUAD_TOP_LEFT;
	if ( right && !bottom) return BV_QUAD_TOP_RIGHT;
	if (!right &&  bottom) return BV_QUAD_BOTTOM_LEFT;
	return BV_QUAD_BOTTOM_RIGHT;
    }

    SbVec2s _mapToQuad(const QPoint &p, int q) const {
	int qx = 0, qy = 0;
	int qw = width()  / 2;
	int qh = height() / 2;
	switch (q) {
	case BV_QUAD_TOP_RIGHT:    qx = qw; qy = 0;  break;
	case BV_QUAD_TOP_LEFT:     qx = 0;  qy = 0;  break;
	case BV_QUAD_BOTTOM_LEFT:  qx = 0;  qy = qh; break;
	case BV_QUAD_BOTTOM_RIGHT: qx = qw; qy = qh; break;
	}
	return SbVec2s((short)(p.x() - qx),
		       (short)(qh - (p.y() - qy) - 1));
    }

    /* Draw a coloured border around the active quadrant using legacy GL */
    void _drawActiveBorder() {
	/* Highlight colour for the active quadrant border.  Orange was chosen
	 * to contrast well with both dark and light scene backgrounds; a
	 * future configurable method (setActiveBorderColor) could expose this.
	 */
	static const float ACTIVE_BORDER_R = 1.0f;
	static const float ACTIVE_BORDER_G = 0.5f;
	static const float ACTIVE_BORDER_B = 0.0f;
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, width(), height(), 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glDisable(GL_DEPTH_TEST);
	glLineWidth(3.0f);
	glColor3f(ACTIVE_BORDER_R, ACTIVE_BORDER_G, ACTIVE_BORDER_B);

	int qw = width()  / 2;
	int qh = height() / 2;
	int x0 = 0, y0 = 0;
	switch (activeQuad_) {
	case BV_QUAD_TOP_RIGHT:    x0 = qw; y0 = 0;  break;
	case BV_QUAD_TOP_LEFT:     x0 = 0;  y0 = 0;  break;
	case BV_QUAD_BOTTOM_LEFT:  x0 = 0;  y0 = qh; break;
	case BV_QUAD_BOTTOM_RIGHT: x0 = qw; y0 = qh; break;
	}
	glBegin(GL_LINE_LOOP);
	glVertex2i(x0,      y0);
	glVertex2i(x0 + qw, y0);
	glVertex2i(x0 + qw, y0 + qh);
	glVertex2i(x0,      y0 + qh);
	glEnd();

	glEnable(GL_DEPTH_TEST);
	glLineWidth(1.0f);

	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
    }

    /* Redraw callback from bview_new */
    static void redraw_cb(struct bview_new *, void *data) {
	QgObolQuadWidget *w = static_cast<QgObolQuadWidget *>(data);
	w->update();
	emit w->changed();
    }

    /* ── Data members ───────────────────────────────────────────────── */
    QtObolContextManager  *ctxMgr_;
    struct bv_render_ctx  *ctx_;
    struct bview_new      *views_[BV_QUAD_NUM_QUADS];
    SoQuadViewport         quadVp_;
    SoRenderManager        renderMgrs_[BV_QUAD_NUM_QUADS];
    bool                   m_init;
    int                    activeQuad_;
    QPoint                 lastMousePos_;
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
