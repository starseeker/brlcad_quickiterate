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
 * ### How it works (Obol path) — single-view
 *
 * bv_render_ctx owns:
 *   - A SoViewport                  — Obol's new single-view API that pairs
 *                                     scene graph, camera, viewport region and
 *                                     background colour in one object
 *   - A SoRenderManager             — handles render modes (wireframe, shaded,
 *                                     hidden-line, bounding-box) and multi-pass
 *                                     stereo rendering on top of SoViewport
 *   - A root SoSeparator            — mirrors bv_scene's root bv_node tree
 *                                     with a *proper OpenInventor hierarchy*:
 *                                     bv_node SEPARATOR → SoSeparator
 *                                     bv_node GROUP     → SoGroup
 *                                     bv_node TRANSFORM → SoMatrixTransform
 *                                     bv_node MATERIAL  → SoMaterial
 *                                     bv_node GEOMETRY  → SoCoordinate3 +
 *                                                          SoIndexedLineSet /
 *                                                          SoIndexedFaceSet /
 *                                                          SoIndexedPointSet
 *   - std::unordered_map<bv_node*, SoNode*>
 *                                   — per-node Inventor node cache
 *
 * ### How it works — quad view
 *
 * bv_quad_render_ctx owns:
 *   - A SoQuadViewport              — Obol's new 4-quadrant viewport manager
 *                                     that splits the window into a 2×2 grid,
 *                                     with each tile being a full SoViewport
 *   - The same shared root SoSeparator that the four SoViewport tiles render
 *
 * The four quadrant indices (BV_QUAD_*) are aligned with
 * SoQuadViewport::QuadIndex for direct pass-through in libqtcad.
 *
 * ### Scene root access for Qt integration
 *
 * bv_render_ctx_scene_root() / bv_quad_render_ctx_scene_root() expose the
 * shared SoSeparator* as void* so that libqtcad's QgObolWidget and
 * QgObolQuadWidget can set it as the scene graph on their own SoViewport /
 * SoQuadViewport.  This lets the Qt layer own the viewport/camera while the
 * BRL-CAD libbv layer owns the scene graph, keeping Obol awareness contained
 * to the two layers that explicitly need it.
 *
 * ### Incremental updates
 *
 * bv_render_frame() / bv_quad_render_frame():
 *   1. Walk the bv_scene tree hierarchically (recursive, not flat).
 *   2. For each GEOMETRY node with dlist_stale == 1, rebuild its SoSeparator
 *      sub-graph in place (replacing the stale sub-graph in node_map).
 *   3. Call SoRenderManager::render() (single-view) or
 *      SoQuadViewport::renderQuadrant() (quad-view) to produce a frame.
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

/* Obol/Inventor public API headers (kept strictly inside this TU). */
/* Obol headers are third-party code; suppress BRL-CAD's strict warnings. */
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-equal"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wdeprecated-copy"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SbVec2s.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbColor.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SoViewport.h>
#include <Inventor/SoQuadViewport.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SoOffscreenRenderer.h>
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
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/fields/SoMFVec3f.h>
#include <Inventor/fields/SoMFInt32.h>
#include <Inventor/actions/SoGLRenderAction.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

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
/* bv_render_ctx struct and sync traversal                             */
/* ------------------------------------------------------------------ */

struct bv_render_ctx {
    SoViewport                                   viewport;  /* Obol single-view API */
    SoRenderManager                              render_mgr; /* render modes / stereo */
    SoSeparator                                 *root;      /* shared scene root */
    std::unordered_map<const struct bv_node *, SoNode *> node_map;
    struct bv_scene                             *scene;
    int                                          owns_scene; /* non-zero → destroy scene in bv_render_ctx_destroy */
    int                                          width;
    int                                          height;
};

/* ------------------------------------------------------------------ */
/* Hierarchy building: bv_node tree → Inventor node tree              */
/* ------------------------------------------------------------------ */

/*
 * Forward declaration.
 */
static SoNode *build_so_node(struct bv_render_ctx *ctx,
			     const struct bv_node *node,
			     SoGroup *parent_so);

/*
 * Add an SoMatrixTransform child for a node's local transform if non-identity.
 */
static void
append_transform(SoSeparator *sep, const struct bv_node *node)
{
    const mat_t *m = bv_node_transform_get(node);
    if (!m)
	return;

    /* Check for identity before creating a node */
    static const double identity[16] = {
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
    };
    if (memcmp(*m, identity, 16 * sizeof(double)) == 0)
	return;

    SoMatrixTransform *xf = new SoMatrixTransform;
    const fastf_t *M = (const fastf_t *)*m;
    /* BRL-CAD mat_t is column-major; SbMatrix constructor is row-major */
    SbMatrix sb(
	(float)M[0],  (float)M[4],  (float)M[8],  (float)M[12],
	(float)M[1],  (float)M[5],  (float)M[9],  (float)M[13],
	(float)M[2],  (float)M[6],  (float)M[10], (float)M[14],
	(float)M[3],  (float)M[7],  (float)M[11], (float)M[15]
    );
    xf->matrix.setValue(sb);
    sep->addChild(xf);
}

/*
 * Add an SoMaterial child for a node's appearance/material if non-default.
 */
static void
append_material(SoSeparator *sep, const struct bv_node *node)
{
    const struct bview_material *mat = bv_node_material_get(node);
    if (!mat)
	return;

    SoMaterial *somat = new SoMaterial;
    const fastf_t *rgb = mat->diffuse_color.buc_rgb;
    somat->diffuseColor.setValue((float)rgb[0], (float)rgb[1], (float)rgb[2]);
    somat->transparency.setValue((float)mat->transparency);
    somat->shininess.setValue((float)mat->shininess);
    sep->addChild(somat);
}

/*
 * Add Inventor geometry children (lines, triangles, points) from a node's
 * vlist.  Returns the number of geometry SoSeparators added (0 if empty).
 */
static int
append_geometry(SoSeparator *sep, const struct bv_node *node)
{
    const struct bu_list *vl = bv_node_vlist_get(node);
    if (!vl || BU_LIST_IS_EMPTY(vl))
	return 0;

    VlistGeom g;
    vlist_to_geom(vl, g);
    int added = 0;

    /* Line segments */
    if (!g.line_pts.empty() && !g.line_idx.empty()) {
	SoSeparator *lsep = new SoSeparator;
	SoCoordinate3 *coords = new SoCoordinate3;
	coords->point.setValues(0, (int)g.line_pts.size(),
				reinterpret_cast<const float(*)[3]>(g.line_pts.data()));
	lsep->addChild(coords);
	SoIndexedLineSet *ils = new SoIndexedLineSet;
	ils->coordIndex.setValues(0, (int)g.line_idx.size(), g.line_idx.data());
	lsep->addChild(ils);
	sep->addChild(lsep);
	added++;
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
	added++;
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
	added++;
    }

    return added;
}

/*
 * Rebuild the Inventor sub-tree for a geometry node that was marked stale.
 * Replaces the old SoSeparator in the parent SoGroup and in node_map.
 */
static void
rebuild_geometry_node(struct bv_render_ctx *ctx, const struct bv_node *node)
{
    auto it = ctx->node_map.find(node);
    if (it == ctx->node_map.end())
	return;

    SoNode *old_so = it->second;

    /* Build new SoSeparator for this geometry node */
    SoSeparator *new_sep = new SoSeparator;
    new_sep->ref();
    append_transform(new_sep, node);
    append_material(new_sep, node);
    append_geometry(new_sep, node);

    /* Replace in parent.  Walk node_map to find the parent SoNode. */
    const struct bv_node *parent_bv = node->parent;
    if (parent_bv) {
	auto pit = ctx->node_map.find(parent_bv);
	if (pit != ctx->node_map.end()) {
	    SoGroup *parent_so = dynamic_cast<SoGroup *>(pit->second);
	    if (parent_so) {
		int idx = parent_so->findChild(old_so);
		if (idx >= 0)
		    parent_so->replaceChild(idx, new_sep);
		else
		    parent_so->addChild(new_sep);
	    }
	}
    } else {
	/* Geometry node directly under the scene root */
	int idx = ctx->root->findChild(old_so);
	if (idx >= 0)
	    ctx->root->replaceChild(idx, new_sep);
	else
	    ctx->root->addChild(new_sep);
    }

    /* old_so was already ref'd; release it.
     * All geometry nodes in node_map are stored as SoSeparator*. */
    if (SoSeparator *old_sep = dynamic_cast<SoSeparator *>(old_so))
	old_sep->unref();
    else
	bu_log("rebuild_geometry_node: unexpected non-SoSeparator in node_map for node '%s'\n",
	       bu_vls_cstr(&node->name));

    it->second = new_sep;  /* new_sep retains its ref() */
    const_cast<struct bv_node *>(node)->dlist_stale = 0;
}

/*
 * Build (or update) the Inventor sub-tree for @a node and all its children.
 * Newly encountered nodes are added to node_map.  Geometry nodes with
 * dlist_stale == 1 are rebuilt.
 *
 * @param parent_so  The Inventor parent node to attach to (may be NULL for
 *                   the scene root, in which case ctx->root is used).
 * Returns the SoNode* created for this node (may be NULL for unhandled types).
 */
static SoNode *
build_so_node(struct bv_render_ctx *ctx,
	      const struct bv_node *node,
	      SoGroup *parent_so)
{
    if (!node)
	return nullptr;

    if (!parent_so)
	parent_so = ctx->root;

    /* Check if this node is already in the map */
    auto it = ctx->node_map.find(node);
    if (it != ctx->node_map.end()) {
	/* Node already exists — check if stale */
	if (node->type == BV_NODE_GEOMETRY && node->dlist_stale) {
	    rebuild_geometry_node(ctx, node);
	}
	/* Recurse into children even for existing nodes (new children may
	 * have been added since last time) */
	SoGroup *so_group = dynamic_cast<SoGroup *>(it->second);
	for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	    struct bv_node *child =
		(struct bv_node *)BU_PTBL_GET(&node->children, i);
	    build_so_node(ctx, child, so_group ? so_group : parent_so);
	}
	return it->second;
    }

    /* New node — build its Inventor representation */
    SoNode *so_node = nullptr;

    switch (node->type) {
    case BV_NODE_SEPARATOR: {
	SoSeparator *sep = new SoSeparator;
	sep->ref();
	append_transform(sep, node);
	append_material(sep, node);
	parent_so->addChild(sep);
	ctx->node_map[node] = sep;

	/* Recurse into children */
	for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	    struct bv_node *child =
		(struct bv_node *)BU_PTBL_GET(&node->children, i);
	    build_so_node(ctx, child, sep);
	}
	so_node = sep;
	break;
    }

    case BV_NODE_GROUP: {
	/* SoGroup has no intrinsic transform; wrap in a separator if needed */
	SoSeparator *wrapper = new SoSeparator;
	wrapper->ref();
	append_transform(wrapper, node);
	SoGroup *grp = new SoGroup;
	grp->ref();
	wrapper->addChild(grp);
	parent_so->addChild(wrapper);
	ctx->node_map[node] = wrapper;
	for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	    struct bv_node *c =
		(struct bv_node *)BU_PTBL_GET(&node->children, i);
	    build_so_node(ctx, c, grp);
	}
	so_node = wrapper;
	grp->unref(); /* wrapper holds the ref via addChild */
	break;
    }

    case BV_NODE_GEOMETRY: {
	SoSeparator *sep = new SoSeparator;
	sep->ref();
	append_transform(sep, node);
	append_material(sep, node);
	append_geometry(sep, node);
	parent_so->addChild(sep);
	ctx->node_map[node] = sep;
	const_cast<struct bv_node *>(node)->dlist_stale = 0;
	so_node = sep;
	break;
    }

    case BV_NODE_TRANSFORM: {
	/* A pure transform node: wrap in a separator that only carries xform */
	SoSeparator *sep = new SoSeparator;
	sep->ref();
	append_transform(sep, node);
	parent_so->addChild(sep);
	ctx->node_map[node] = sep;
	for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	    struct bv_node *child =
		(struct bv_node *)BU_PTBL_GET(&node->children, i);
	    build_so_node(ctx, child, sep);
	}
	so_node = sep;
	break;
    }

    case BV_NODE_MATERIAL: {
	SoSeparator *sep = new SoSeparator;
	sep->ref();
	append_material(sep, node);
	parent_so->addChild(sep);
	ctx->node_map[node] = sep;
	for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	    struct bv_node *child =
		(struct bv_node *)BU_PTBL_GET(&node->children, i);
	    build_so_node(ctx, child, sep);
	}
	so_node = sep;
	break;
    }

    case BV_NODE_CAMERA:
    case BV_NODE_LIGHT:
    case BV_NODE_OTHER:
    default:
	/* These node types are handled by the viewport/camera layer, not here */
	break;
    }

    return so_node;
}

/*
 * Sync the entire bv_scene tree into the Inventor scene.
 * Walk from the bv_scene root node; new and stale nodes are updated.
 */
static void
sync_scene(struct bv_render_ctx *ctx)
{
    if (!ctx || !ctx->scene)
	return;

    struct bv_node *root_node = bv_scene_root(ctx->scene);
    if (!root_node)
	return;

    size_t n_children = BU_PTBL_LEN(&root_node->children);
    bu_log("sync_scene: %zu bv_node children in scene\n", n_children);

    /* Use the scene root's children as top-level nodes */
    for (size_t i = 0; i < n_children; i++) {
	struct bv_node *top =
	    (struct bv_node *)BU_PTBL_GET(&root_node->children, i);
	build_so_node(ctx, top, ctx->root);
    }
}

/* ------------------------------------------------------------------ */
/* Camera synchronisation: bview_new → SoCamera                       */
/* ------------------------------------------------------------------ */

static void
sync_camera_to_viewport(SoViewport *vp, const struct bview_new *view)
{
    if (!vp || !view)
	return;

    const struct bview_camera *cam = bview_camera_get(view);
    if (!cam)
	return;

    SoCamera *so_cam = vp->getCamera();

    /* Create/replace camera if needed or if perspective mode changed */
    bool need_persp  = (cam->perspective != 0);
    bool have_persp  = (so_cam && so_cam->isOfType(SoPerspectiveCamera::getClassTypeId()));
    bool have_ortho  = (so_cam && so_cam->isOfType(SoOrthographicCamera::getClassTypeId()));

    if (!so_cam || (need_persp && !have_persp) || (!need_persp && !have_ortho)) {
	SoCamera *new_cam = need_persp
	    ? static_cast<SoCamera *>(new SoPerspectiveCamera)
	    : static_cast<SoCamera *>(new SoOrthographicCamera);
	vp->setCamera(new_cam);
	so_cam = new_cam;
    }

    /* Position */
    so_cam->position.setValue(
	(float)cam->position[0],
	(float)cam->position[1],
	(float)cam->position[2]);

    /* Orientation from lookat + up */
    SbVec3f viewdir(
	(float)(cam->target[0] - cam->position[0]),
	(float)(cam->target[1] - cam->position[1]),
	(float)(cam->target[2] - cam->position[2]));
    if (viewdir.length() > 1e-10f) {
	viewdir.normalize();
	SbVec3f up((float)cam->up[0], (float)cam->up[1], (float)cam->up[2]);
	if (up.length() < 1e-10f)
	    up.setValue(0.0f, 0.0f, 1.0f);
	up.normalize();
	/* Build rotation from -Z to viewdir */
	SbRotation rot(SbVec3f(0.0f, 0.0f, -1.0f), viewdir);
	so_cam->orientation.setValue(rot);
    }

    /* Focal distance (distance from camera to target) */
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

    /* FOV (perspective only) */
    if (need_persp && cam->fov > 0.0) {
	static_cast<SoPerspectiveCamera *>(so_cam)->heightAngle.setValue(
	    (float)(cam->fov * M_PI / 180.0));
    }

    /* Viewport size */
    const struct bview_viewport *vport = bview_viewport_get(view);
    if (vport && vport->width > 0 && vport->height > 0) {
	vp->setWindowSize(SbVec2s((short)vport->width, (short)vport->height));
    }
}

/* ------------------------------------------------------------------ */
/* SoDB initialisation guard                                           */
/* ------------------------------------------------------------------ */

static void
ensure_sodb_init(void *context_manager)
{
    /* Use SoDB::isInitialized() so we never re-initialise SoDB when the
     * calling widget (e.g. QgObolWidget::initializeGL) has already called
     * SoDB::init() with its own Qt GL context manager.  Re-initialising
     * SoDB with a different context manager (e.g. OSMesa) after it has
     * already been set up with Qt GL corrupts render state and crashes. */
    if (SoDB::isInitialized())
	return;

    SoDB::init(static_cast<SoDB::ContextManager *>(context_manager));
}

/* ------------------------------------------------------------------ */
/* Public API — single-view                                            */
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

    /* If SoDB is already initialized (e.g. by QgObolWidget::initializeGL with
     * a Qt GL context manager), do NOT create an OSMesa context manager —
     * doing so would re-initialize SoDB with a different backend and crash. */
    if (!context_manager && !SoDB::isInitialized())
	context_manager = SoDB::createOSMesaContextManager();

    ensure_sodb_init(context_manager);

    struct bv_render_ctx *ctx = new struct bv_render_ctx;
    ctx->scene      = scene;
    ctx->owns_scene = 0;
    ctx->width  = width;
    ctx->height = height;

    /* Scene root — shared by SoViewport and SoRenderManager */
    ctx->root = new SoSeparator;
    ctx->root->ref();

    /* Add a default directional light so geometry is visible */
    SoDirectionalLight *light = new SoDirectionalLight;
    light->direction.setValue(-1.0f, -1.0f, -1.0f);
    ctx->root->addChild(light);

    /* SoViewport: single-view, pairs scene + camera + viewport region */
    ctx->viewport.setWindowSize(SbVec2s((short)width, (short)height));
    ctx->viewport.setSceneGraph(ctx->root);
    /* Default camera is added by viewAll() if not yet present */
    ctx->viewport.viewAll();

    /* SoRenderManager: render modes, stereo, multi-pass on top of SoViewport */
    SoRenderManager::enableRealTimeUpdate(FALSE); /* caller drives render loop */
    ctx->render_mgr.setSceneGraph(ctx->viewport.getRoot());
    ctx->render_mgr.setWindowSize(SbVec2s((short)width, (short)height));
    ctx->render_mgr.setViewportRegion(
	ctx->viewport.getViewportRegion());

    /* Build initial Inventor hierarchy from bv_scene */
    sync_scene(ctx);

    return ctx;
}

void
bv_render_ctx_destroy(struct bv_render_ctx *ctx)
{
    if (!ctx)
	return;

    /* Unref all cached Inventor nodes */
    for (auto &kv : ctx->node_map) {
	if (SoSeparator *sep = dynamic_cast<SoSeparator *>(kv.second))
	    sep->unref();
    }
    ctx->node_map.clear();

    ctx->root->unref();

    if (ctx->owns_scene && ctx->scene)
	bv_scene_destroy(ctx->scene);

    delete ctx;
}

void
bv_render_ctx_update_scene(struct bv_render_ctx *ctx,
			   struct bv_scene *scene,
			   int take_ownership)
{
    if (!ctx || !scene)
	return;

    /* Free old owned scene if any */
    if (ctx->owns_scene && ctx->scene)
	bv_scene_destroy(ctx->scene);

    /* Clear all cached Inventor nodes and remove from root */
    for (auto &kv : ctx->node_map) {
	if (SoSeparator *sep = dynamic_cast<SoSeparator *>(kv.second))
	    sep->unref();
    }
    ctx->node_map.clear();

    /* Remove all children from the root except the first (directional light) */
    int n = ctx->root->getNumChildren();
    for (int i = n - 1; i >= 1; i--)
	ctx->root->removeChild(i);

    ctx->scene      = scene;
    ctx->owns_scene = take_ownership ? 1 : 0;

    /* Build fresh Inventor hierarchy from new scene */
    sync_scene(ctx);

    /* Let SoViewport / SoRenderManager see the new geometry */
    if (!ctx->viewport.getCamera())
	ctx->viewport.viewAll();
}


void
bv_render_ctx_set_size(struct bv_render_ctx *ctx, int width, int height)
{
    if (!ctx || width <= 0 || height <= 0)
	return;

    ctx->width  = width;
    ctx->height = height;
    SbVec2s sz((short)width, (short)height);
    ctx->viewport.setWindowSize(sz);
    ctx->render_mgr.setWindowSize(sz);
    ctx->render_mgr.setViewportRegion(ctx->viewport.getViewportRegion());
}

void
bv_render_ctx_sync_scene(struct bv_render_ctx *ctx, struct bview_new *view)
{
    if (!ctx)
	return;

    sync_scene(ctx);

    if (view)
	sync_camera_to_viewport(&ctx->viewport, view);

    ctx->render_mgr.setViewportRegion(ctx->viewport.getViewportRegion());
}

int
bv_render_frame(struct bv_render_ctx *ctx, struct bview_new *view)
{
    if (!ctx)
	return 0;

    bv_render_ctx_sync_scene(ctx, view);
    ctx->render_mgr.render(/*clearwindow=*/TRUE, /*clearzbuffer=*/TRUE);
    return 1;
}

void *
bv_render_ctx_scene_root(struct bv_render_ctx *ctx)
{
    if (!ctx)
	return nullptr;
    return static_cast<void *>(ctx->root);
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

/* ------------------------------------------------------------------ */
/* Public API — quad-view (SoQuadViewport)                            */
/* ------------------------------------------------------------------ */

struct bv_quad_render_ctx {
    SoQuadViewport                               quad_vp;   /* Obol quad-view API */
    SoSeparator                                 *root;      /* shared scene root */
    std::unordered_map<const struct bv_node *, SoNode *> node_map;
    struct bv_scene                             *scene;
    int                                          width;
    int                                          height;
};

extern "C" {

struct bv_quad_render_ctx *
bv_quad_render_ctx_create(struct bv_scene *scene, void *context_manager,
			  int width, int height)
{
    if (!scene || width <= 0 || height <= 0)
	return nullptr;

    if (!context_manager)
	context_manager = SoDB::createOSMesaContextManager();

    ensure_sodb_init(context_manager);

    struct bv_quad_render_ctx *ctx = new struct bv_quad_render_ctx;
    ctx->scene  = scene;
    ctx->width  = width;
    ctx->height = height;

    /* Shared scene root for all four quadrants */
    ctx->root = new SoSeparator;
    ctx->root->ref();

    SoDirectionalLight *light = new SoDirectionalLight;
    light->direction.setValue(-1.0f, -1.0f, -1.0f);
    ctx->root->addChild(light);

    /* SoQuadViewport: 4-quadrant manager sharing the same scene */
    ctx->quad_vp.setWindowSize(SbVec2s((short)width, (short)height));
    ctx->quad_vp.setSceneGraph(ctx->root);
    ctx->quad_vp.viewAllQuadrants();

    /* Build initial Inventor hierarchy.  We reuse the same logic as single-
     * view by temporarily pointing a bv_render_ctx at the shared state. */
    struct bv_render_ctx tmp_ctx;
    tmp_ctx.scene    = scene;
    tmp_ctx.root     = ctx->root;
    tmp_ctx.width    = width;
    tmp_ctx.height   = height;
    sync_scene(&tmp_ctx);
    ctx->node_map = std::move(tmp_ctx.node_map);

    return ctx;
}

void
bv_quad_render_ctx_destroy(struct bv_quad_render_ctx *ctx)
{
    if (!ctx)
	return;

    for (auto &kv : ctx->node_map) {
	if (SoSeparator *sep = dynamic_cast<SoSeparator *>(kv.second))
	    sep->unref();
    }
    ctx->node_map.clear();

    ctx->root->unref();
    delete ctx;
}

void
bv_quad_render_ctx_set_size(struct bv_quad_render_ctx *ctx,
			    int width, int height)
{
    if (!ctx || width <= 0 || height <= 0)
	return;
    ctx->width  = width;
    ctx->height = height;
    ctx->quad_vp.setWindowSize(SbVec2s((short)width, (short)height));
}

int
bv_quad_render_frame(struct bv_quad_render_ctx *ctx,
		     struct bview_new **views,
		     const char *output_path)
{
    if (!ctx || !output_path)
	return 0;

    /* Sync scene */
    struct bv_render_ctx tmp_ctx;
    tmp_ctx.scene    = ctx->scene;
    tmp_ctx.root     = ctx->root;
    tmp_ctx.width    = ctx->width;
    tmp_ctx.height   = ctx->height;
    tmp_ctx.node_map = ctx->node_map;
    sync_scene(&tmp_ctx);
    ctx->node_map = std::move(tmp_ctx.node_map);

    /* Apply per-quadrant cameras */
    if (views) {
	for (int q = 0; q < BV_QUAD_NUM_QUADS; q++) {
	    if (!views[q])
		continue;
	    SoViewport *vp = ctx->quad_vp.getViewport(q);
	    if (vp)
		sync_camera_to_viewport(vp, views[q]);
	}
    }

    /* Render all quadrants to composite RGB file */
    SbVec2s qs = ctx->quad_vp.getQuadrantSize();
    SoOffscreenRenderer qren(SbViewportRegion(qs[0], qs[1]));
    return ctx->quad_vp.writeCompositeToRGB(output_path, &qren) ? 1 : 0;
}

void *
bv_quad_render_ctx_scene_root(struct bv_quad_render_ctx *ctx)
{
    if (!ctx)
	return nullptr;
    return static_cast<void *>(ctx->root);
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
bv_render_ctx_scene_root(struct bv_render_ctx *UNUSED(ctx))
{
    return NULL;
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

struct bv_quad_render_ctx *
bv_quad_render_ctx_create(struct bv_scene *UNUSED(scene),
			  void *UNUSED(context_manager),
			  int UNUSED(width), int UNUSED(height))
{
    return NULL;
}

void
bv_quad_render_ctx_destroy(struct bv_quad_render_ctx *UNUSED(ctx))
{
    /* no-op */
}

void
bv_quad_render_ctx_set_size(struct bv_quad_render_ctx *UNUSED(ctx),
			    int UNUSED(width), int UNUSED(height))
{
    /* no-op */
}

int
bv_quad_render_frame(struct bv_quad_render_ctx *UNUSED(ctx),
		     struct bview_new **UNUSED(views),
		     const char *UNUSED(output_path))
{
    return 0;
}

void *
bv_quad_render_ctx_scene_root(struct bv_quad_render_ctx *UNUSED(ctx))
{
    return NULL;
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
