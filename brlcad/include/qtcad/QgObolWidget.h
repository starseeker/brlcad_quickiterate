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
#include <cstdlib>  /* calloc for RT overlay black init */

extern "C" {
#include "bv.h"
#include "bv/render.h"
#include "dm.h"       /* fb_open, fb_close, fb_readrect, struct fb */
}

#include "qtcad/defines.h"

/* Obol / Inventor headers — confined to this translation unit and
 * libqtcad files that explicitly opt in.  Lower BRL-CAD libraries
 * (libged, libbv, librt …) remain Obol-free. */
/* Obol headers are third-party code; suppress BRL-CAD's strict warnings. */
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-equal"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wdeprecated-copy"
#endif
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

/* Additional Obol includes for the RT texture overlay */
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureCoordinateBinding.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/fields/SoSFImage.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif
/* SoButtonEvent.h's push/pop macros restore UP/DOWN as numeric macros
 * (e.g. from X11/Xlib.h) after its enum definition.  Undef them here so
 * that SoButtonEvent::UP / SoButtonEvent::DOWN are usable as C++ identifiers
 * in the rest of this header and in files that include it. */
#ifdef UP
#  undef UP
#endif
#ifdef DOWN
#  undef DOWN
#endif

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
	, local_bv_(nullptr)
	, view_(nullptr)
	, own_view_(false)
	, m_init(false)
    {
	setMouseTracking(true);
	setFocusPolicy(Qt::WheelFocus);

	/* Allocate a legacy struct bview and its bview_new companion so that
	 * both view() and bview_old_get() return valid pointers immediately.
	 * Callers may replace the view with set_view(); the owned copies are
	 * freed in the destructor (or released in set_view). */
	BU_GET(local_bv_, struct bview);
	bv_init(local_bv_, NULL);
	bu_vls_sprintf(&local_bv_->gv_name, "obol");
	view_ = bview_companion_create("obol", local_bv_);
	own_view_ = (view_ != nullptr);

	/* NOTE: SoRenderManager and SoViewport are NOT constructed here.
	 * They are allocated in initializeGL() AFTER SoDB::init() — creating
	 * Coin3D objects before SoDB::init() leaves SoGLRenderAction with
	 * uninitialized type IDs and causes SoActionMethodList::setUp() to crash. */

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
	if (own_view_)
	    bview_destroy(view_);
	if (local_bv_) {
	    bv_free(local_bv_);
	    BU_PUT(local_bv_, struct bview);
	}
	delete renderMgr_;
	delete viewport_;
    }

    /* ── Scene ──────────────────────────────────────────────────────── */

    /**
     * Attach a BRL-CAD render context.  The SoSeparator* root from
     * bv_render_ctx_scene_root(ctx) is fed to viewport_->setSceneGraph().
     * A default camera is added via viewport_->viewAll() when none is found.
     */
    void setRenderCtx(struct bv_render_ctx *ctx) {
	ctx_ = ctx;
	scene_fitted_ = false;        /* will refit when geometry first appears */
	camera_user_dirty_ = false;   /* reset so BRL-CAD camera syncs initially */
	if (!ctx_ || !viewport_ || !renderMgr_) {
	    if (viewport_)  viewport_->setSceneGraph(nullptr);
	    if (renderMgr_) renderMgr_->setSceneGraph(nullptr);
	    return;
	}

	SoNode *root = static_cast<SoNode *>(bv_render_ctx_scene_root(ctx_));
	if (!root)
	    return;

	viewport_->setSceneGraph(root);

	/* viewAll() requires an existing camera AND a non-degenerate bounding
	 * box.  With only a SoDirectionalLight in the scene (no geometry yet),
	 * the bounding box is empty and viewAll() may fail to create a camera.
	 * Explicitly create and position a default camera first so
	 * SoRenderManager::render() always has a valid camera to work with.
	 * The camera is re-fitted to actual geometry via viewport_->viewAll()
	 * in paintGL() once bv_render_ctx_sync_scene() populates the scene. */
	if (!viewport_->getCamera()) {
	    SoPerspectiveCamera *defaultCam = new SoPerspectiveCamera;
	    /* Place at a sensible default position so near/far are non-zero */
	    defaultCam->position.setValue(0.0f, 0.0f, 100.0f);
	    defaultCam->heightAngle.setValue(float(M_PI / 4.0));
	    defaultCam->nearDistance.setValue(0.1f);
	    defaultCam->farDistance.setValue(1000.0f);
	    viewport_->setCamera(defaultCam);
	}

	/* SoRenderManager renders viewport_->getRoot() (= [camera + scene]) */
	renderMgr_->setSceneGraph(viewport_->getRoot());
	update();
    }

    struct bv_render_ctx *renderCtx() const { return ctx_; }

    /**
     * Associate a BRL-CAD view (camera / viewport parameters).
     * The widget will sync SoViewport's camera to this view on each
     * paintGL() call.  May be NULL.
     * If an owned view was previously created in the constructor, it is
     * destroyed and ownership is relinquished when an external view is set.
     */
    void set_view(struct bview_new *v) {
	if (own_view_ && view_) {
	    bview_destroy(view_);
	    own_view_ = false;
	}
	if (local_bv_) {
	    bv_free(local_bv_);
	    BU_PUT(local_bv_, struct bview);
	    local_bv_ = nullptr;
	}
	view_ = v;
	if (v) {
	    /* Install a redraw callback so external changes trigger a repaint. */
	    bview_redraw_callback_set(v, redraw_cb, this);
	}
    }
    struct bview_new *view() const { return view_; }

    /* ── SoViewport / SoRenderManager access ────────────────────────── */
    SoViewport       *getViewport()       { return viewport_; }
    const SoViewport *getViewport() const { return viewport_; }
    SoRenderManager  *getRenderManager()  { return renderMgr_; }

    /* ── Render mode ────────────────────────────────────────────────── */
    void setRenderMode(SoRenderManager::RenderMode mode) {
	if (renderMgr_) renderMgr_->setRenderMode(mode);
	update();
    }
    SoRenderManager::RenderMode getRenderMode() const {
	return renderMgr_ ? renderMgr_->getRenderMode()
	                  : SoRenderManager::AS_IS;
    }

    /* ── Stereo mode ────────────────────────────────────────────────── */
    void setStereoMode(SoRenderManager::StereoMode mode) {
	if (renderMgr_) renderMgr_->setStereoMode(mode);
	update();
    }
    SoRenderManager::StereoMode getStereoMode() const {
	return renderMgr_ ? renderMgr_->getStereoMode()
	                  : SoRenderManager::MONO;
    }

    /* ── Transparency type ──────────────────────────────────────────── */
    void setTransparencyType(SoGLRenderAction::TransparencyType t) {
	if (renderMgr_) renderMgr_->getGLRenderAction()->setTransparencyType(t);
	update();
    }
    SoGLRenderAction::TransparencyType getTransparencyType() const {
	return renderMgr_ ? renderMgr_->getGLRenderAction()->getTransparencyType()
	                  : SoGLRenderAction::SCREEN_DOOR;
    }

    /* ── Background colour ──────────────────────────────────────────── */
    void setBackgroundColor(const SbColor &c) {
	if (viewport_)  viewport_->setBackgroundColor(c);
	if (renderMgr_) renderMgr_->setBackgroundColor(SbColor4f(c, 1.0f));
	update();
    }

    /* ── current flag (for multi-view selection) ────────────────────── */
    void set_current(int i) { current_ = i; }
    int  current()    const { return current_; }

    /* ── Hashing helpers for change detection ───────────────────────── */
    void stash_hashes() {
	/* Stash the bv_scene node count so diff_hashes() can detect draw/erase
	 * changes and trigger a repaint after console commands. */
	struct bv_scene *sc = ctx_ ? bv_render_ctx_get_scene(ctx_) : nullptr;
	const struct bu_ptbl *nodes = sc ? bv_scene_nodes(sc) : nullptr;
	prev_node_count_ = nodes ? BU_PTBL_LEN(nodes) : 0;
    }
    bool diff_hashes() {
	struct bv_scene *sc = ctx_ ? bv_render_ctx_get_scene(ctx_) : nullptr;
	const struct bu_ptbl *nodes = sc ? bv_scene_nodes(sc) : nullptr;
	size_t cur_node_count = nodes ? BU_PTBL_LEN(nodes) : 0;
	return (cur_node_count != prev_node_count_);
    }

    void need_update() { update(); }

    /* ── Raytrace texture overlay (ert2 incremental display) ──────────
     *
     * The overlay is an SoAnnotation sub-graph that renders on top of
     * the 3D scene (depth buffer cleared before rendering), containing
     * an SoOrthographicCamera + SoTexture2 + full-viewport quad.
     *
     * Obol's idiomatic way to push new pixel data is to edit the
     * SoTexture2::image field in-place using startEditing/finishEditing.
     * The finishEditing() call triggers Obol's field-change notification
     * chain which automatically schedules a repaint — no explicit
     * update() call needed.
     *
     * beginRtOverlay() must be called before onRtPixelsUpdated() will
     * do anything.  It is called from qdm_open_obol_client_handler() in
     * qged/fbserv.cpp, which is invoked when rt first connects.
     */

    /**
     * @brief Prepare the Obol scene for incremental raytrace display.
     *
     * Creates an SoAnnotation sub-graph with a full-viewport quad
     * textured by an SoTexture2 whose image field will be updated
     * incrementally.  The sub-graph is appended to the viewport's scene
     * root so it renders on top of (and independently from) the 3D scene.
     *
     * @param fb     Pointer to the libdm memory framebuffer that rt will
     *               write into via the fbserv protocol.  Stored for use
     *               by onRtPixelsUpdated().  Must remain valid until
     *               clearRtOverlay() or onRtDone() is called.
     * @param w      Framebuffer / image width  in pixels.
     * @param h      Framebuffer / image height in pixels.
     */
    void beginRtOverlay(struct fb *fb, int w, int h) {
	/* Remove any stale overlay from a previous render */
	_removeRtOverlay();

	m_rtFb     = fb;
	m_rtWidth  = w;
	m_rtHeight = h;

	if (!m_init || w <= 0 || h <= 0)
	    return;

	SoNode *sceneRoot = static_cast<SoNode *>(
				bv_render_ctx_scene_root(ctx_));
	if (!sceneRoot || !sceneRoot->isOfType(SoSeparator::getClassTypeId()))
	    return;
	SoSeparator *root = static_cast<SoSeparator *>(sceneRoot);

	/* ── Build overlay sub-graph ──────────────────────────────────
	 *
	 *  SoAnnotation            ← renders after main scene, depth cleared
	 *    SoOrthographicCamera  ← 2-unit-high view centred at origin
	 *    SoTexture2            ← m_rtTex_: the raytrace image
	 *    SoTextureCoordinateBinding (PER_VERTEX_INDEXED)
	 *    SoTextureCoordinate2  ← (0,0)…(1,1)
	 *    SoCoordinate3         ← full-viewport quad in cam space
	 *    SoIndexedFaceSet      ← 1 quad
	 */

	m_rtOverlay_ = new SoAnnotation;
	m_rtOverlay_->ref();

	/* 2D orthographic camera: height=2 covers Y in [-1,1]; Obol
	 * automatically adjusts the X range for the viewport aspect ratio
	 * so the quad always fills the full window. */
	float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
	m_rtCam_ = new SoOrthographicCamera;
	m_rtCam_->position.setValue(0.0f, 0.0f, 1.0f);
	m_rtCam_->nearDistance.setValue(0.1f);
	m_rtCam_->farDistance.setValue(10.0f);
	m_rtCam_->height.setValue(2.0f);     /* Y: [-1, +1] */
	m_rtCam_->aspectRatio.setValue(aspect);
	m_rtOverlay_->addChild(m_rtCam_);

	/* SoTexture2: initially black, will be updated by onRtPixelsUpdated */
	m_rtTex_ = new SoTexture2;
	m_rtTex_->ref();
	{
	    /* Initialise to opaque black so the overlay is visible until
	     * the first packet arrives.  Use NO_COPY_AND_FREE so Obol takes
	     * ownership of the calloc'd buffer and frees it later — no
	     * temporary vector needed. */
	    unsigned char *black =
		(unsigned char *)calloc((size_t)w * (size_t)h * 3,
					sizeof(unsigned char));
	    m_rtTex_->image.setValue(SbVec2s((short)w, (short)h), 3,
				     black, SoSFImage::NO_COPY_AND_FREE);
	}
	m_rtTex_->wrapS.setValue(SoTexture2::CLAMP);
	m_rtTex_->wrapT.setValue(SoTexture2::CLAMP);
	m_rtOverlay_->addChild(m_rtTex_);

	/* Texture coordinate binding */
	SoTextureCoordinateBinding *tcb = new SoTextureCoordinateBinding;
	tcb->value = SoTextureCoordinateBinding::PER_VERTEX_INDEXED;
	m_rtOverlay_->addChild(tcb);

	/* Texture coordinates: (0,0) BL → (1,0) BR → (1,1) TR → (0,1) TL */
	SoTextureCoordinate2 *tc = new SoTextureCoordinate2;
	tc->point.set1Value(0, SbVec2f(0.0f, 0.0f));
	tc->point.set1Value(1, SbVec2f(1.0f, 0.0f));
	tc->point.set1Value(2, SbVec2f(1.0f, 1.0f));
	tc->point.set1Value(3, SbVec2f(0.0f, 1.0f));
	m_rtOverlay_->addChild(tc);

	/* Quad vertices in orthographic camera space: cover [-aspect,+aspect] × [-1,+1]
	 * Using ±2 ensures full coverage regardless of minor aspect-ratio drift. */
	SoCoordinate3 *coords = new SoCoordinate3;
	coords->point.set1Value(0, SbVec3f(-aspect, -1.0f, 0.0f));
	coords->point.set1Value(1, SbVec3f( aspect, -1.0f, 0.0f));
	coords->point.set1Value(2, SbVec3f( aspect,  1.0f, 0.0f));
	coords->point.set1Value(3, SbVec3f(-aspect,  1.0f, 0.0f));
	m_rtCoords_ = coords;
	m_rtOverlay_->addChild(coords);

	/* One quad face: vertices 0,1,2,3 then -1 (end-of-face sentinel) */
	SoIndexedFaceSet *fs = new SoIndexedFaceSet;
	fs->coordIndex.set1Value(0, 0);
	fs->coordIndex.set1Value(1, 1);
	fs->coordIndex.set1Value(2, 2);
	fs->coordIndex.set1Value(3, 3);
	fs->coordIndex.set1Value(4, -1);
	fs->textureCoordIndex.set1Value(0, 0);
	fs->textureCoordIndex.set1Value(1, 1);
	fs->textureCoordIndex.set1Value(2, 2);
	fs->textureCoordIndex.set1Value(3, 3);
	fs->textureCoordIndex.set1Value(4, -1);
	m_rtOverlay_->addChild(fs);

	root->addChild(m_rtOverlay_);
    }

    /**
     * @brief Remove the raytrace texture overlay from the scene.
     *
     * Unrefs and removes the SoAnnotation sub-graph.  Does NOT close or
     * free the framebuffer — that is the caller's responsibility (see
     * ert2_raytrace_done in QgEdApp.cpp).
     */
    void clearRtOverlay() {
	_removeRtOverlay();
	m_rtFb     = nullptr;
	m_rtWidth  = 0;
	m_rtHeight = 0;
    }

    /**
     * @brief Called when rt finishes rendering.
     *
     * The final frame has already been received via onRtPixelsUpdated().
     * This slot keeps the overlay visible with the completed image and
     * resets the "active render" state.  The overlay can be dismissed
     * later by the user or by a clearRtOverlay() call.
     */
    void onRtDone() {
	/* Nothing special needed: the texture already holds the final
	 * frame.  Just mark no active render. */
	m_rtFb = nullptr;   /* rt is done; fb will be closed by caller */
    }

public slots:
    void viewAll() {
	if (viewport_) {
	    viewport_->viewAll();
	    scene_fitted_ = true;
	    camera_user_dirty_ = true;  /* treat viewAll as a user-initiated action */
	    _pushViewportCameraToView();
	    update();
	}
    }

    /**
     * @brief Incrementally update the raytrace texture from the fb.
     *
     * Called (via Qt queued connection) each time a framebuffer packet
     * arrives from rt.  Reads the full pixel buffer from the memory fb
     * and updates the SoTexture2::image field using the idiomatic Obol
     * in-place editing pattern:
     *
     *   1. startEditing()   — obtain writable pointer to Obol's buffer
     *   2. fb_readrect()    — copy pixels directly from the memory fb
     *   3. finishEditing()  — send field notification → auto repaint
     *
     * For incremental scanline rendering the texture updates in real time
     * as each packet arrives, without needing an explicit update() call.
     */
    void onRtPixelsUpdated() {
	if (!m_rtFb || !m_rtTex_ || m_rtWidth <= 0 || m_rtHeight <= 0)
	    return;

	SbVec2s size;
	int nc = 0;
	unsigned char *buf = m_rtTex_->image.startEditing(size, nc);

	if (buf && size[0] == (short)m_rtWidth
	       && size[1] == (short)m_rtHeight
	       && nc == 3) {
	    /* Fast path: write directly into Obol's texture buffer.
	     * fb_readrect copies (x0,y0)→(x0+w, y0+h) RGB scanlines. */
	    fb_readrect(m_rtFb, 0, 0, m_rtWidth, m_rtHeight, buf);
	    /* finishEditing() propagates SoSFImage notification through the
	     * scene graph — Obol automatically schedules a GL repaint.     */
	    m_rtTex_->image.finishEditing();
	} else {
	    /* Size mismatch or first-frame init: use setValue (allocates). */
	    m_rtTex_->image.finishEditing();  /* must exit editing even on mismatch */
	    std::vector<unsigned char> pixels(m_rtWidth * m_rtHeight * 3);
	    fb_readrect(m_rtFb, 0, 0, m_rtWidth, m_rtHeight, pixels.data());
	    m_rtTex_->image.setValue(SbVec2s((short)m_rtWidth, (short)m_rtHeight),
				     3, pixels.data(), SoSFImage::COPY);
	}
    }

signals:
    void changed();
    void init_done();

protected:
    /* ── QOpenGLWidget overrides ────────────────────────────────────── */

    void initializeGL() override {
	glEnable(GL_DEPTH_TEST);

	/* Now that we have a GL context, initialise SoDB (once per process).
	 * Update the context manager share context so off-screen renders can
	 * share textures/VBOs with this widget. */
	QOpenGLContext *qctx = QOpenGLContext::currentContext();
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

	/* Allocate SoViewport and SoRenderManager NOW, after SoDB::init().
	 * Creating these objects before SoDB::init() leaves their internal
	 * SoGLRenderAction with uninitialized type IDs, causing
	 * SoActionMethodList::setUp() to crash on first render(). */
	if (!renderMgr_) {
	    viewport_  = new SoViewport;
	    renderMgr_ = new SoRenderManager;
	    /* Disable auto-redraw: paintGL() drives the render loop explicitly */
	    SoRenderManager::enableRealTimeUpdate(FALSE);
	}

	/* Wire the GL render action cache context to Qt's GL context */
	renderMgr_->getGLRenderAction()->setCacheContext(cacheContext_);

	m_init = true;
	emit init_done();
    }

    void resizeGL(int w, int h) override {
	const qreal dpr = devicePixelRatioF();
	SbVec2s physSize((short)(w * dpr), (short)(h * dpr));
	if (!viewport_ || !renderMgr_) return;
	viewport_->setWindowSize(physSize);
	renderMgr_->setWindowSize(physSize);
	renderMgr_->setViewportRegion(viewport_->getViewportRegion());

	if (view_) {
	    struct bview_viewport vp;
	    vp.width  = (int)(w * dpr);
	    vp.height = (int)(h * dpr);
	    vp.dpi    = (double)physicalDpiX();
	    bview_viewport_set(view_, &vp);
	}

	/* Keep the RT overlay camera aspect ratio in sync with the widget */
	if (m_rtCam_ && h > 0) {
	    float newAspect = (float)w / (float)h;
	    m_rtCam_->aspectRatio.setValue(newAspect);
	    /* Update quad vertices to match */
	    if (m_rtCoords_) {
		m_rtCoords_->point.set1Value(0, SbVec3f(-newAspect, -1.0f, 0.0f));
		m_rtCoords_->point.set1Value(1, SbVec3f( newAspect, -1.0f, 0.0f));
		m_rtCoords_->point.set1Value(2, SbVec3f( newAspect,  1.0f, 0.0f));
		m_rtCoords_->point.set1Value(3, SbVec3f(-newAspect,  1.0f, 0.0f));
	    }
	}
    }

    void paintGL() override {
	if (!m_init || !viewport_ || !renderMgr_)
	    return;

	if (ctx_) {
	    /* Pull the latest BRL-CAD camera into view_ so that ae/zoom/center
	     * commands issued in the console are reflected here.  The legacy
	     * ged_gvp is the authoritative source; bview_sync_from_old() copies
	     * it into view_ (the bview_new companion).
	     * Guard: only call if old_bview is valid (set_view may race). */
	    if (view_ && bview_old_get(view_))
		bview_sync_from_old(view_);

	    /* Snapshot child count before sync so we can detect newly added
	     * geometry (child[0] is always the directional light). */
	    SoSeparator *sceneRoot =
		static_cast<SoSeparator *>(bv_render_ctx_scene_root(ctx_));
	    int pre_sync_children = sceneRoot ? sceneRoot->getNumChildren() : 0;

	    /* Sync the Inventor scene graph from bv_scene (build new nodes,
	     * update stale ones, remove erased ones).  Pass nullptr for the
	     * view so it does not redundantly update ctx_->viewport — the
	     * widget manages viewport_'s camera below. */
	    bv_render_ctx_sync_scene(ctx_, nullptr);

	    int post_sync_children = sceneRoot ? sceneRoot->getNumChildren() : 0;

	    /* Apply the BRL-CAD camera (from view_) to viewport_ when:
	     *   (a) geometry just appeared and the widget hasn't been fitted yet, or
	     *   (b) new objects were added (newly drawn geometry should be visible), or
	     *   (c) the user hasn't orbited since the camera was last set by BRL-CAD.
	     * For case (a)/(b) use viewAll() to auto-fit the camera to the scene.
	     * For ongoing frames (after fit) sync the camera from view_ directly so
	     * that ae/zoom/center commands in the console move the Obol viewport. */
	    if (post_sync_children > 1) {
		if (!scene_fitted_ || post_sync_children > pre_sync_children) {
		    /* New geometry: auto-fit and write the result back to view_ */
		    viewport_->viewAll();
		    scene_fitted_ = true;
		    _pushViewportCameraToView();
		} else if (view_ && !camera_user_dirty_) {
		    /* No new geometry and user hasn't orbited: track BRL-CAD camera */
		    _syncCameraFromView(view_);
		}
	    }

	    renderMgr_->setViewportRegion(viewport_->getViewportRegion());
	    renderMgr_->render(/*clearwindow=*/TRUE, /*clearzbuffer=*/TRUE);
	    return;
	}

	/* No bv_render_ctx yet — nothing to render.  This path is taken during
	 * the brief window between initializeGL() and the deferred ctx setup.
	 * Only render if renderMgr_ has a scene graph (set by setRenderCtx). */
	if (renderMgr_->getSceneGraph()) {
	    renderMgr_->setViewportRegion(viewport_->getViewportRegion());
	    renderMgr_->render(/*clearwindow=*/TRUE, /*clearzbuffer=*/TRUE);
	}
    }

    /* ── Mouse events ───────────────────────────────────────────────── */

    void mousePressEvent(QMouseEvent *e) override {
	if (!viewport_) return;
	lastMousePos_ = e->pos();
	SoMouseButtonEvent ev;
	ev.setPosition(mapToObol(e->pos()));
	ev.setState(SoButtonEvent::DOWN);
	ev.setButton(qtButtonToCoin(e->button()));
	viewport_->processEvent(&ev);
	update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
	if (!viewport_) return;
	SoMouseButtonEvent ev;
	ev.setPosition(mapToObol(e->pos()));
	ev.setState(SoButtonEvent::UP);
	ev.setButton(qtButtonToCoin(e->button()));
	viewport_->processEvent(&ev);
	update();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
	if (!viewport_) return;
	/* Left-button drag → orbit */
	if (e->buttons() & Qt::LeftButton) {
	    SoCamera *cam = viewport_->getCamera();
	    if (cam) {
		const QPoint delta = e->pos() - lastMousePos_;
		SbVec3f lookDir;
		cam->orientation.getValue().multVec(SbVec3f(0,0,-1), lookDir);
		const SbVec3f orbit =
		    cam->position.getValue() + lookDir * cam->focalDistance.getValue();
		cam->orbitCamera(orbit, (float)delta.x(), (float)delta.y());
		lastMousePos_ = e->pos();
		camera_user_dirty_ = true;
		_pushViewportCameraToView();
		update();
		return;
	    }
	}
	/* Middle-button drag → pan */
	if (e->buttons() & Qt::MiddleButton) {
	    SoCamera *cam = viewport_->getCamera();
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
		camera_user_dirty_ = true;
		_pushViewportCameraToView();
		update();
		return;
	    }
	}
	SoLocation2Event ev;
	ev.setPosition(mapToObol(e->pos()));
	viewport_->processEvent(&ev);
	update();
    }

    void wheelEvent(QWheelEvent *e) override {
	if (!viewport_) return;
	int delta = e->angleDelta().y();
	if (delta == 0) delta = e->angleDelta().x();
	SoCamera *cam = viewport_->getCamera();
	if (cam) {
	    SbVec3f look;
	    cam->orientation.getValue().multVec(SbVec3f(0,0,-1), look);
	    const float step = cam->focalDistance.getValue() * (delta > 0 ? 0.1f : -0.1f);
	    cam->position.setValue(cam->position.getValue() + look * step);
	    cam->focalDistance.setValue(cam->focalDistance.getValue() - step);
	    camera_user_dirty_ = true;
	    _pushViewportCameraToView();
	    update();
	}
    }

    /* ── Keyboard events ────────────────────────────────────────────── */

    void keyPressEvent(QKeyEvent *e) override {
	if (!viewport_) return;
	SoKeyboardEvent ev;
	ev.setState(SoButtonEvent::DOWN);
	ev.setKey(qtKeyToCoin(e->key()));
	viewport_->processEvent(&ev);
    }

    void keyReleaseEvent(QKeyEvent *e) override {
	if (!viewport_) return;
	SoKeyboardEvent ev;
	ev.setState(SoButtonEvent::UP);
	ev.setKey(qtKeyToCoin(e->key()));
	viewport_->processEvent(&ev);
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
	    a->setChecked(renderMgr_ && renderMgr_->getRenderMode() == mode);
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

	SbTime timerT;
	SbBool timerPending = mgr->isTimerSensorPending(timerT);
	const SbTime &delayT = mgr->getDelaySensorTimeout();

	if (timerPending) {
	    if (timerT.getValue() <= 0.0)
		w->timerTimer_.start(0);
	    else
		w->timerTimer_.start((int)(timerT.getValue() * 1000.0));
	}

	if (delayT.getValue() < 1e-9)
	    w->delayTimer_.start(0);
	else
	    w->delayTimer_.start((int)(delayT.getValue() * 1000.0));

	if (mgr->isDelaySensorPending())
	    w->idleTimer_.start(0);
    }

    void onIdle()  { SoDB::getSensorManager()->processDelayQueue(TRUE); update(); }
    void onDelay() { SoDB::getSensorManager()->processDelayQueue(FALSE); }
    void onTimer() {
	SoDB::getSensorManager()->processTimerQueue();
	SbTime next;
	if (SoDB::getSensorManager()->isTimerSensorPending(next) && next.getValue() > 0.0)
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
    struct bview         *local_bv_;  /* owned companion legacy bview (may be NULL after set_view) */
    struct bview_new     *view_;
    bool                  own_view_; /* true if view_ was created in ctor */
    SoViewport           *viewport_ = nullptr;  /* allocated in initializeGL after SoDB::init() */
    SoRenderManager      *renderMgr_ = nullptr; /* allocated in initializeGL after SoDB::init() */
    bool                  m_init;
    bool                  scene_fitted_ = false; /* true once viewport_->viewAll() fitted to geometry */
    bool                  camera_user_dirty_ = false; /* true when user has orbited/panned/zoomed */
    int                   current_ = 0;
    QPoint                lastMousePos_;
    /* Stashed values for diff_hashes() */
    size_t                prev_node_count_ = 0;

    /* ── RT texture overlay members ─────────────────────────────────── */
    struct fb            *m_rtFb      = nullptr; /* memory fb owned by ObolRtCtx */
    int                   m_rtWidth   = 0;
    int                   m_rtHeight  = 0;
    SoAnnotation         *m_rtOverlay_ = nullptr; /* ref'd */
    SoTexture2           *m_rtTex_     = nullptr; /* ref'd */
    SoOrthographicCamera *m_rtCam_     = nullptr; /* child of m_rtOverlay_ */
    SoCoordinate3        *m_rtCoords_  = nullptr; /* child of m_rtOverlay_ */

    /* ── Camera helpers ─────────────────────────────────────────────── */

    /**
     * Apply the bview_new camera to viewport_'s camera so BRL-CAD view
     * commands (ae, zoom, center, etc.) are reflected in the Obol viewport.
     * Called each frame when the user has not been interactively orbiting.
     */
    void _syncCameraFromView(const struct bview_new *view) {
	if (!view || !viewport_) return;
	const struct bview_camera *cam = bview_camera_get(view);
	if (!cam) return;

	SoCamera *so_cam = viewport_->getCamera();

	/* Create/replace camera if perspective mode changed */
	bool need_persp = (cam->perspective != 0);
	if (!so_cam
	    || (need_persp  && !so_cam->isOfType(SoPerspectiveCamera::getClassTypeId()))
	    || (!need_persp && !so_cam->isOfType(SoOrthographicCamera::getClassTypeId()))) {
	    so_cam = need_persp
		? static_cast<SoCamera *>(new SoPerspectiveCamera)
		: static_cast<SoCamera *>(new SoOrthographicCamera);
	    viewport_->setCamera(so_cam);
	}

	so_cam->position.setValue(
	    (float)cam->position[0],
	    (float)cam->position[1],
	    (float)cam->position[2]);

	SbVec3f viewdir(
	    (float)(cam->target[0] - cam->position[0]),
	    (float)(cam->target[1] - cam->position[1]),
	    (float)(cam->target[2] - cam->position[2]));
	if (viewdir.length() > 1e-10f) {
	    viewdir.normalize();
	    so_cam->orientation.setValue(
		SbRotation(SbVec3f(0.0f, 0.0f, -1.0f), viewdir));
	}

	SbVec3f tgt(
	    (float)cam->target[0],
	    (float)cam->target[1],
	    (float)cam->target[2]);
	SbVec3f pos(
	    (float)cam->position[0],
	    (float)cam->position[1],
	    (float)cam->position[2]);
	float fdist = (tgt - pos).length();
	if (fdist > 1e-10f)
	    so_cam->focalDistance.setValue(fdist);

	if (need_persp && cam->fov > 0.0)
	    static_cast<SoPerspectiveCamera *>(so_cam)->heightAngle.setValue(
		(float)(cam->fov * M_PI / 180.0));
    }

    /**
     * Write viewport_'s current camera back to view_ (and the legacy ged_gvp
     * via bview_sync_to_old) so that orbit/pan/zoom in the widget are reflected
     * in the BRL-CAD view state.  Called after each interactive camera move.
     */
    void _pushViewportCameraToView() {
	if (!view_ || !viewport_) return;
	SoCamera *so_cam = viewport_->getCamera();
	if (!so_cam) return;

	struct bview_camera cam;
	SbVec3f pos = so_cam->position.getValue();
	SbVec3f look;
	so_cam->orientation.getValue().multVec(SbVec3f(0.0f, 0.0f, -1.0f), look);
	float fdist = so_cam->focalDistance.getValue();

	VSET(cam.position, pos[0], pos[1], pos[2]);
	VSET(cam.target,
	     pos[0] + look[0] * fdist,
	     pos[1] + look[1] * fdist,
	     pos[2] + look[2] * fdist);

	/* Derive up vector from orientation */
	SbVec3f up;
	so_cam->orientation.getValue().multVec(SbVec3f(0.0f, 1.0f, 0.0f), up);
	VSET(cam.up, up[0], up[1], up[2]);

	cam.perspective = so_cam->isOfType(SoPerspectiveCamera::getClassTypeId()) ? 1 : 0;
	if (cam.perspective)
	    cam.fov = (double)static_cast<SoPerspectiveCamera *>(so_cam)->heightAngle.getValue()
		      * 180.0 / M_PI;
	else
	    cam.fov = 0.0;
	cam.scale = fdist;

	bview_camera_set(view_, &cam);
	bview_sync_to_old(view_);  /* push into ged_gvp so ae/zoom see it too */
    }

    /* Remove overlay from scene and unref nodes */
    void _removeRtOverlay() {
	if (!m_rtOverlay_) return;
	SoNode *sceneRoot = static_cast<SoNode *>(
				bv_render_ctx_scene_root(ctx_));
	if (sceneRoot && sceneRoot->isOfType(SoSeparator::getClassTypeId())) {
	    SoSeparator *root = static_cast<SoSeparator *>(sceneRoot);
	    root->removeChild(m_rtOverlay_);
	}
	if (m_rtTex_) {
	    m_rtTex_->unref();
	    m_rtTex_ = nullptr;
	}
	m_rtOverlay_->unref();
	m_rtOverlay_ = nullptr;
	m_rtCam_     = nullptr;
	m_rtCoords_  = nullptr;
    }
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
