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
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoPath.h>

#include <GL/gl.h>
#include <cmath>
#include <atomic>

#include "bsg.h"
#include "bsg/util.h"
#include "obol_scene.h"
#include "vmath.h"
#include "bn/mat.h"

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

	SoCamera *cam = viewport_.getCamera();
	if (!cam)
	    return;

	const SbRotation &orient = cam->orientation.getValue();

	/* Extract world-space right / up / look vectors from the orientation.
	 * Camera canonical frame: right=+X, up=+Y, look=-Z. */
	SbVec3f right_sb, up_sb, look_sb;
	orient.multVec(SbVec3f(1.0f, 0.0f,  0.0f), right_sb);
	orient.multVec(SbVec3f(0.0f, 1.0f,  0.0f), up_sb);
	orient.multVec(SbVec3f(0.0f, 0.0f, -1.0f), look_sb);
	right_sb.normalize();
	up_sb.normalize();
	look_sb.normalize();

	/* Build gv_rotation: rows are [right | 0, up | 0, -look | 0, 0 0 0 1].
	 * This matches the construction in syncCameraFromBsgView() where the
	 * rot_mat columns are [right, up, -look].  BRL-CAD mat_t is row-major. */
	mat_t rot;
	MAT_ZERO(rot);
	rot[0]  = right_sb[0]; rot[1]  = right_sb[1]; rot[2]  = right_sb[2];
	rot[4]  = up_sb[0];    rot[5]  = up_sb[1];    rot[6]  = up_sb[2];
	rot[8]  = -look_sb[0]; rot[9]  = -look_sb[1]; rot[10] = -look_sb[2];
	rot[15] = 1.0;
	MAT_COPY(bsg_v_->gv_rotation, rot);

	/* The scene center is at eye_pos + look * focalDistance.
	 * gv_center is the translation that maps scene_center → view origin:
	 *   gv_center = translate(-scene_center). */
	const SbVec3f &pos_sb = cam->position.getValue();
	float fd = cam->focalDistance.getValue();
	point_t scene_center;
	scene_center[X] = (double)pos_sb[0] + (double)look_sb[0] * (double)fd;
	scene_center[Y] = (double)pos_sb[1] + (double)look_sb[1] * (double)fd;
	scene_center[Z] = (double)pos_sb[2] + (double)look_sb[2] * (double)fd;

	MAT_IDN(bsg_v_->gv_center);
	MAT_DELTAS_VEC_NEG(bsg_v_->gv_center, scene_center);

	/* gv_size = focalDistance, gv_scale = gv_size / 2 (matches autoview). */
	bsg_v_->gv_size  = (double)fd;
	bsg_v_->gv_scale = (double)fd * 0.5;
	bsg_v_->gv_isize = (bsg_v_->gv_size > SMALL_FASTF)
			   ? 1.0 / bsg_v_->gv_size : 1.0;

	/* Recompute derived matrices (model2view, view2model, aet, …). */
	bsg_view_update(bsg_v_);

	/* User explicitly navigated: stop progressive autoview. */
	if (bsg_v_->gv_s)
	    bsg_v_->gv_s->gv_progressive_autoview = 0;
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
    /**
     * Fit the scene into the view using Obol's bounding-box camera fitting.
     *
     * Stage 4: After fitting, syncBsgViewFromCamera() writes the new camera
     * state back to bsg_view so command-line tools and the rest of BRL-CAD
     * remain consistent with the Obol camera.
     */
    void viewAll() {
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
	viewport_.processEvent(&le);

	/* Stage 4: sync back to bsg_view after any camera-changing navigation */
	if (navigated)
	    syncBsgViewFromCamera();

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
	SbTime t = SoDB::getSensorManager()->getNextTimerInterval();
	if (t != SbTime::max())
	    timerTimer_.start((int)(t.getValue() * 1000));
    }

    // ── GL cache context ──────────────────────────────────────────────────

    static uint32_t allocCacheContext() {
	static std::atomic<uint32_t> ctx{1};
	return ctx.fetch_add(1, std::memory_order_relaxed);
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
    SoViewport       viewport_;
    SoRenderManager  renderMgr_;
    QTimer           idleTimer_, delayTimer_, timerTimer_;
    QPointF          lastMousePos_;
    uint32_t         cacheContext_ = allocCacheContext();
    bsg_shape       *selectedShape_ = nullptr;  /* Stage 5: current selection */
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
