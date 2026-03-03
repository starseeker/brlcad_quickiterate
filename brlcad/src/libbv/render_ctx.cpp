/*                  R E N D E R _ C T X . C P P
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
/** @file libbv/render_ctx.cpp
 *
 * Implementation of the bv_render_ctx wrapper over Obol/Inventor.
 *
 * ### How it works (Obol path)
 *
 * bv_render_ctx owns:
 *   - A single SoSceneManager       — Obol's all-in-one render orchestrator
 *   - A root SoSeparator            — mirrors bv_scene's root bv_node tree
 *   - std::unordered_map<bv_node*, SoSeparator*>
 *                                   — per-node Inventor sub-graph cache
 *
 * bv_render_frame() calls bv_scene_traverse() to walk every node.  For each
 * geometry node whose dlist_stale flag is set (or that is newly added), the
 * sync callback rebuilds the corresponding Inventor sub-graph by:
 *   1. Converting the bv_vlist to SoCoordinate3 + SoIndexedLineSet (lines) or
 *      SoIndexedFaceSet (triangles/polygons).
 *   2. Applying bview_material colors via SoMaterial.
 *   3. Applying the node's local transform via SoMatrixTransform.
 *
 * When Obol is not compiled in (BRLCAD_HAVE_OBOL is absent), every public
 * function is a no-op that returns NULL / 0, allowing callers to check
 * bv_render_ctx_available() without compile-time guards.
 */

#include "common.h"

#include <string.h>

#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bv/defines.h"
#include "bv/vlist.h"
#include "bv/render.h"

#include "./bv_private.h"

/* ======================================================================== */
/* Obol-backed implementation                                                */
/* ======================================================================== */
#ifdef BRLCAD_HAVE_OBOL

#include <vector>
#include <unordered_map>
#include <mutex>

/* Obol/Inventor public API headers (kept strictly inside this TU). */
#include <Inventor/SoDB.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SoSceneManager.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoMatrixTransform.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedPointSet.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/fields/SoMFVec3f.h>
#include <Inventor/fields/SoMFInt32.h>

/* ------------------------------------------------------------------ */
/* Helpers: bv_vlist → Inventor geometry                               */
/* ------------------------------------------------------------------ */

/*
 * Walk one bv_vlist chain and separate line segments, triangle strips,
 * polygon patches, and isolated points into typed output buffers.
 */
struct VlistGeom {
    /* Line segments: pairs of points with SO_END_LINE_INDEX as separator */
    std::vector<SbVec3f> line_pts;
    std::vector<int32_t> line_idx;

    /* Triangles / polygons: flat vertex list with -1 as face terminator */
    std::vector<SbVec3f> tri_pts;
    std::vector<int32_t> tri_idx;

    /* Point cloud */
    std::vector<SbVec3f> point_pts;
    std::vector<int32_t> point_idx;
};

static void
vlist_to_geom(const struct bu_list *vlist_head, VlistGeom &g)
{
    if (!vlist_head || BU_LIST_IS_EMPTY(vlist_head))
	return;

    /* Current open line / polygon state */
    bool in_line = false;
    bool in_poly = false;
    int line_start = -1;   /* index of first coord in current line strip */
    int tri_start  = -1;   /* index of first coord in current poly/tri   */

    struct bv_vlist *vp;
    for (BU_LIST_FOR(vp, bv_vlist, vlist_head)) {
	size_t nused = vp->nused;
	for (size_t i = 0; i < nused; i++) {
	    const point_t &pt = vp->pt[i];
	    SbVec3f sv((float)pt[0], (float)pt[1], (float)pt[2]);

	    switch (vp->cmd[i]) {
	    case BV_VLIST_LINE_MOVE:
		if (in_line) {
		    /* close previous strip */
		    g.line_idx.push_back(SO_END_LINE_INDEX);
		}
		line_start = (int)g.line_pts.size();
		g.line_pts.push_back(sv);
		g.line_idx.push_back(line_start);
		in_line = true;
		break;

	    case BV_VLIST_LINE_DRAW:
		if (!in_line) {
		    /* orphan vertex — start a degenerate strip */
		    line_start = (int)g.line_pts.size();
		    in_line = true;
		}
		g.line_idx.push_back((int32_t)g.line_pts.size());
		g.line_pts.push_back(sv);
		break;

	    case BV_VLIST_TRI_START:
	    case BV_VLIST_POLY_START:
		if (in_poly)
		    g.tri_idx.push_back(-1);   /* close previous face */
		tri_start = -1;
		in_poly = false;
		break;

	    case BV_VLIST_TRI_MOVE:
	    case BV_VLIST_POLY_MOVE:
		tri_start = (int)g.tri_pts.size();
		g.tri_pts.push_back(sv);
		g.tri_idx.push_back(tri_start);
		in_poly = true;
		break;

	    case BV_VLIST_TRI_DRAW:
	    case BV_VLIST_POLY_DRAW:
		if (in_poly) {
		    g.tri_idx.push_back((int32_t)g.tri_pts.size());
		    g.tri_pts.push_back(sv);
		}
		break;

	    case BV_VLIST_TRI_END:
	    case BV_VLIST_POLY_END:
		if (in_poly) {
		    g.tri_idx.push_back(-1);
		    in_poly = false;
		}
		break;

	    case BV_VLIST_TRI_VERTNORM:
	    case BV_VLIST_POLY_VERTNORM:
		/* normals ignored for now */
		break;

	    case BV_VLIST_POINT_DRAW:
		g.point_idx.push_back((int32_t)g.point_pts.size());
		g.point_pts.push_back(sv);
		break;

	    case BV_VLIST_POINT_SIZE:
	    case BV_VLIST_LINE_WIDTH:
	    case BV_VLIST_DISPLAY_MAT:
	    case BV_VLIST_MODEL_MAT:
		/* state changes — not yet handled at this layer */
		break;

	    default:
		break;
	    }
	}
    }

    /* Close any open line strip */
    if (in_line)
	g.line_idx.push_back(SO_END_LINE_INDEX);
    /* Close any open polygon */
    if (in_poly)
	g.tri_idx.push_back(-1);
}

/* ------------------------------------------------------------------ */
/* Per-node Inventor sub-graph                                          */
/* ------------------------------------------------------------------ */

/*
 * Build (or rebuild) the Inventor sub-graph for one geometry node.
 *
 * Layout inside the returned SoSeparator:
 *
 *   SoSeparator  (node_sep)
 *     ├─ SoMatrixTransform
 *     ├─ SoMaterial
 *     ├─ SoSeparator  (line_sep, optional)
 *     │    ├─ SoCoordinate3
 *     │    └─ SoIndexedLineSet
 *     ├─ SoSeparator  (tri_sep, optional)
 *     │    ├─ SoCoordinate3
 *     │    └─ SoIndexedFaceSet
 *     └─ SoSeparator  (pt_sep, optional)
 *          ├─ SoCoordinate3
 *          └─ SoIndexedPointSet
 */
static SoSeparator *
build_node_sep(const struct bv_node *node)
{
    SoSeparator *sep = new SoSeparator;
    sep->ref();

    /* --- Transform --- */
    SoMatrixTransform *xf = new SoMatrixTransform;
    const mat_t *m = bv_node_transform_get(node);
    if (m) {
	/* BRL-CAD mat_t is column-major (like OpenGL).
	 * SbMatrix row-major constructor: rows first. */
	const fastf_t *M = (const fastf_t *)m;
	SbMatrix sb(
	    (float)M[0],  (float)M[4],  (float)M[8],  (float)M[12],
	    (float)M[1],  (float)M[5],  (float)M[9],  (float)M[13],
	    (float)M[2],  (float)M[6],  (float)M[10], (float)M[14],
	    (float)M[3],  (float)M[7],  (float)M[11], (float)M[15]
	);
	xf->matrix.setValue(sb);
    }
    sep->addChild(xf);

    /* --- Material --- */
    const struct bview_material *mat = bv_node_material_get(node);
    if (mat) {
	SoMaterial *somat = new SoMaterial;
	const float *rgb = mat->diffuse_color.buc_rgb;
	somat->diffuseColor.setValue(rgb[0], rgb[1], rgb[2]);
	somat->transparency.setValue(mat->transparency);
	somat->shininess.setValue(mat->shininess);
	sep->addChild(somat);
    }

    /* --- Geometry from vlist --- */
    const struct bu_list *vl = bv_node_vlist_get(node);
    if (vl && !BU_LIST_IS_EMPTY(vl)) {
	VlistGeom g;
	vlist_to_geom(vl, g);

	/* Line segments */
	if (!g.line_pts.empty() && !g.line_idx.empty()) {
	    SoSeparator *lsep = new SoSeparator;

	    SoCoordinate3 *coords = new SoCoordinate3;
	    /* SbVec3f is {float[3]}, so the vector's storage is compatible with
	     * the float[][3] signature expected by setValues(). */
	    coords->point.setValues(0, (int)g.line_pts.size(),
				    reinterpret_cast<const float(*)[3]>(g.line_pts.data()));
	    lsep->addChild(coords);

	    SoIndexedLineSet *ils = new SoIndexedLineSet;
	    ils->coordIndex.setValues(0, (int)g.line_idx.size(), g.line_idx.data());
	    lsep->addChild(ils);

	    sep->addChild(lsep);
	}

	/* Triangles / polygons */
	if (!g.tri_pts.empty() && !g.tri_idx.empty()) {
	    SoSeparator *tsep = new SoSeparator;

	    SoCoordinate3 *coords = new SoCoordinate3;
	    coords->point.setValues(0, (int)g.tri_pts.size(),
				    reinterpret_cast<const float(*)[3]>(g.tri_pts.data()));
	    tsep->addChild(coords);

	    SoIndexedFaceSet *ifs = new SoIndexedFaceSet;
	    ifs->coordIndex.setValues(0, (int)g.tri_idx.size(), g.tri_idx.data());
	    tsep->addChild(ifs);

	    sep->addChild(tsep);
	}

	/* Points */
	if (!g.point_pts.empty() && !g.point_idx.empty()) {
	    SoSeparator *psep = new SoSeparator;

	    SoCoordinate3 *coords = new SoCoordinate3;
	    coords->point.setValues(0, (int)g.point_pts.size(),
				    reinterpret_cast<const float(*)[3]>(g.point_pts.data()));
	    psep->addChild(coords);

	    SoIndexedPointSet *ips = new SoIndexedPointSet;
	    ips->coordIndex.setValues(0, (int)g.point_idx.size(), g.point_idx.data());
	    psep->addChild(ips);

	    sep->addChild(psep);
	}
    }

    return sep;   /* caller takes ownership of the ref() */
}

/* ------------------------------------------------------------------ */
/* bv_render_ctx struct and sync traversal                             */
/* ------------------------------------------------------------------ */

struct bv_render_ctx {
    SoSceneManager                              *mgr;
    SoSeparator                                 *root;
    std::unordered_map<const struct bv_node *, SoSeparator *> node_map;
    struct bv_scene                             *scene;
    int                                          width;
    int                                          height;
};

/*
 * Traversal callback: for each bv_node, ensure its Inventor counterpart
 * exists and is up-to-date.  Nodes not yet in node_map are inserted.
 * Nodes with dlist_stale == 1 are rebuilt in place.
 */
struct SyncState {
    struct bv_render_ctx *ctx;
};

static void
sync_node_cb(struct bv_node *node, void *ud)
{
    SyncState *s = static_cast<SyncState *>(ud);
    struct bv_render_ctx *ctx = s->ctx;

    /* Skip non-geometry nodes (groups, transforms handled by hierarchy) */
    if (node->type != BV_NODE_GEOMETRY)
	return;

    auto it = ctx->node_map.find(node);
    bool stale = (node->dlist_stale != 0);

    if (it == ctx->node_map.end()) {
	/* New node — build and insert */
	SoSeparator *sep = build_node_sep(node);
	ctx->node_map[node] = sep;
	ctx->root->addChild(sep);
	node->dlist_stale = 0;
    } else if (stale) {
	/* Existing but dirty — rebuild */
	SoSeparator *old_sep = it->second;
	SoSeparator *new_sep = build_node_sep(node);
	/* Replace in root */
	int idx = ctx->root->findChild(old_sep);
	if (idx >= 0)
	    ctx->root->replaceChild(idx, new_sep);
	else
	    ctx->root->addChild(new_sep);
	old_sep->unref();
	it->second = new_sep;
	node->dlist_stale = 0;
    }
}

/* ------------------------------------------------------------------ */
/* SoDB initialisation guard                                           */
/* ------------------------------------------------------------------ */
static std::once_flag s_sodb_init_flag;

static void
ensure_sodb_init(void *context_manager)
{
    std::call_once(s_sodb_init_flag, [&]() {
	SoDB::init(static_cast<SoDB::ContextManager *>(context_manager));
    });
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

extern "C" {

int
bv_render_ctx_available(void)
{
    return 1;
}

struct bv_render_ctx *
bv_render_ctx_create(struct bv_scene *scene, void *context_manager,
		     int width, int height)
{
    if (!scene) {
	bu_log("bv_render_ctx_create: scene is NULL\n");
	return nullptr;
    }
    if (width <= 0 || height <= 0) {
	bu_log("bv_render_ctx_create: invalid dimensions %dx%d\n", width, height);
	return nullptr;
    }

    /* Initialise SoDB exactly once per process.  If the caller did not
     * provide a context manager we attempt to create an OSMesa one; if
     * that is not available either we pass NULL (SoDB handles it). */
    if (!context_manager)
	context_manager = SoDB::createOSMesaContextManager();

    ensure_sodb_init(context_manager);

    struct bv_render_ctx *ctx = new struct bv_render_ctx;
    ctx->scene  = scene;
    ctx->width  = width;
    ctx->height = height;

    ctx->root = new SoSeparator;
    ctx->root->ref();

    ctx->mgr = new SoSceneManager;
    ctx->mgr->setSceneGraph(ctx->root);
    ctx->mgr->setWindowSize(SbVec2s((short)width, (short)height));
    ctx->mgr->setViewportRegion(SbViewportRegion((short)width, (short)height));

    /* Initial sync: walk existing scene nodes */
    SyncState ss;
    ss.ctx = ctx;
    bv_scene_traverse(scene, sync_node_cb, &ss);

    return ctx;
}

void
bv_render_ctx_destroy(struct bv_render_ctx *ctx)
{
    if (!ctx)
	return;

    /* Unref all cached node separators */
    for (auto &kv : ctx->node_map)
	kv.second->unref();
    ctx->node_map.clear();

    delete ctx->mgr;
    ctx->root->unref();
    delete ctx;
}

void
bv_render_ctx_set_size(struct bv_render_ctx *ctx, int width, int height)
{
    if (!ctx || width <= 0 || height <= 0)
	return;

    ctx->width  = width;
    ctx->height = height;
    ctx->mgr->setWindowSize(SbVec2s((short)width, (short)height));
    ctx->mgr->setViewportRegion(SbViewportRegion((short)width, (short)height));
}

int
bv_render_frame(struct bv_render_ctx *ctx, struct bview_new *view)
{
    if (!ctx)
	return 0;

    /* Synchronise stale or new nodes */
    SyncState ss;
    ss.ctx = ctx;
    bv_scene_traverse(ctx->scene, sync_node_cb, &ss);

    /* Apply camera from view if provided */
    if (view) {
	const struct bview_camera *cam = bview_camera_get(view);
	if (cam) {
	    /* Use perspective camera by default.  The mgr already has a
	     * camera set during setSceneGraph; we update its position. */
	    SoCamera *so_cam = ctx->mgr->getCamera();
	    if (!so_cam) {
		SoPerspectiveCamera *pc = new SoPerspectiveCamera;
		ctx->root->insertChild(pc, 0);
		ctx->mgr->setCamera(pc);
		so_cam = pc;
	    }
	    /* Map BRL-CAD camera position/lookat to SoCamera */
	    so_cam->position.setValue(
		(float)cam->position[0],
		(float)cam->position[1],
		(float)cam->position[2]);
	    /* Orientation: derive from lookat/up vectors if available */
	    /* (Full rotation derivation deferred; basic position sync here) */
	}

	/* Resize to match view's viewport */
	const struct bview_viewport *vp = bview_viewport_get(view);
	if (vp && vp->width > 0 && vp->height > 0)
	    bv_render_ctx_set_size(ctx, vp->width, vp->height);
    }

    ctx->mgr->render(/*clearwindow=*/TRUE, /*clearzbuffer=*/TRUE);
    return 1;
}

void *
bv_render_ctx_osmesa_mgr_create(void)
{
    return static_cast<void *>(SoDB::createOSMesaContextManager());
}

void
bv_render_ctx_osmesa_mgr_destroy(void *mgr)
{
    if (!mgr)
	return;
    delete static_cast<SoDB::ContextManager *>(mgr);
}

} /* extern "C" */

/* ======================================================================== */
/* No-op stub implementation (Obol not available)                            */
/* ======================================================================== */
#else /* BRLCAD_HAVE_OBOL */

extern "C" {

int
bv_render_ctx_available(void)
{
    return 0;
}

struct bv_render_ctx *
bv_render_ctx_create(struct bv_scene *UNUSED(scene),
		     void *UNUSED(context_manager),
		     int UNUSED(width), int UNUSED(height))
{
    return NULL;
}

void
bv_render_ctx_destroy(struct bv_render_ctx *UNUSED(ctx))
{
    /* no-op */
}

void
bv_render_ctx_set_size(struct bv_render_ctx *UNUSED(ctx),
		       int UNUSED(width), int UNUSED(height))
{
    /* no-op */
}

int
bv_render_frame(struct bv_render_ctx *UNUSED(ctx),
		struct bview_new *UNUSED(view))
{
    return 0;
}

void *
bv_render_ctx_osmesa_mgr_create(void)
{
    return NULL;
}

void
bv_render_ctx_osmesa_mgr_destroy(void *UNUSED(mgr))
{
    /* no-op */
}

} /* extern "C" */

#endif /* BRLCAD_HAVE_OBOL */

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
