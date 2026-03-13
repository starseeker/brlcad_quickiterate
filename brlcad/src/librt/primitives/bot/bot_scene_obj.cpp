/*              B O T _ S C E N E _ O B J . C P P
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
/** @file primitives/bot/bot_scene_obj.cpp
 *
 * ft_scene_obj implementation for Bag-of-Triangles (BoT) primitives.
 *
 * Builds a native Obol SoNode subtree directly from rt_bot_internal without
 * going through the vlist intermediate representation.  This is the first
 * "native Obol" ft_scene_obj implementation per RADICAL_MIGRATION.md Stage 1.
 *
 * Drawing modes supported:
 *   dmode 0 (wireframe)    → SoCoordinate3 + SoIndexedLineSet (unique edges)
 *   dmode 2 (shaded)       → SoCoordinate3 + SoNormal + SoIndexedFaceSet
 *   dmode 4 (shaded+HL)    → same as dmode 2 (highlight pass by renderer)
 *   other                  → falls back to wireframe
 *
 * When Obol is NOT available (#ifndef BRLCAD_ENABLE_OBOL), rt_bot_scene_obj
 * falls back to rt_generic_scene_obj so that the functab entry always has a
 * valid function pointer.
 *
 * Progressive refinement (placeholder path):
 *   When s->have_bbox == 0, no geometry is available yet.  We draw an OBB
 *   placeholder wireframe if OBB data is present, otherwise nothing.
 *   The draw pipeline will retry once the async AABB/OBB pipeline delivers
 *   results.
 *
 * @see RADICAL_MIGRATION.md Stage 1
 */

#include "common.h"

extern "C" {
#include "bsg/defines.h"
#include "bsg/vlist.h"
#include "raytrace.h"
#include "rt/primitives/bot.h"
#include "rt/geom.h"
#include "rt/db_internal.h"
#include "rt/global.h"
}

#ifdef BRLCAD_ENABLE_OBOL

#include "bsg/obol_node.h"

/* Suppress -Wfloat-equal from third-party Obol (Open Inventor) headers */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include <vector>
#include <utility>
#include <map>
#include <cmath>


/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/**
 * Build a wireframe SoIndexedLineSet from the unique edges of a BoT.
 *
 * Uses a std::map to deduplicate edges in O(E log E) time where E is
 * proportional to num_faces.
 */
static SoSeparator *
bot_wireframe_node(const struct rt_bot_internal *bot)
{
    if (!bot || bot->num_faces == 0 || bot->num_vertices == 0)
	return nullptr;

    /* Collect unique edges: each edge is an ordered pair (lo_idx, hi_idx) */
    std::map<std::pair<int,int>, bool> seen;
    std::vector<int> edge_v0;
    std::vector<int> edge_v1;

    for (size_t fi = 0; fi < bot->num_faces; fi++) {
	int v[3];
	v[0] = bot->faces[fi*3 + 0];
	v[1] = bot->faces[fi*3 + 1];
	v[2] = bot->faces[fi*3 + 2];

	for (int ei = 0; ei < 3; ei++) {
	    int a = v[ei];
	    int b = v[(ei+1) % 3];
	    if (a > b) { int tmp = a; a = b; b = tmp; }
	    std::pair<int,int> key(a, b);
	    if (!seen.count(key)) {
		seen[key] = true;
		edge_v0.push_back(a);
		edge_v1.push_back(b);
	    }
	}
    }

    if (edge_v0.empty())
	return nullptr;

    /* Build SoCoordinate3 from the BoT vertex array */
    SoCoordinate3 *coord = new SoCoordinate3;
    coord->point.setNum((int)bot->num_vertices);
    for (size_t vi = 0; vi < bot->num_vertices; vi++) {
	coord->point.set1Value((int)vi,
	    (float)bot->vertices[vi*3 + 0],
	    (float)bot->vertices[vi*3 + 1],
	    (float)bot->vertices[vi*3 + 2]);
    }

    /* Build SoIndexedLineSet: each edge = [v0, v1, -1] */
    int nedges = (int)edge_v0.size();
    SoIndexedLineSet *ils = new SoIndexedLineSet;
    ils->coordIndex.setNum(nedges * 3);
    for (int i = 0; i < nedges; i++) {
	ils->coordIndex.set1Value(i*3 + 0, edge_v0[i]);
	ils->coordIndex.set1Value(i*3 + 1, edge_v1[i]);
	ils->coordIndex.set1Value(i*3 + 2, SO_END_LINE_INDEX);
    }

    SoSeparator *sep = new SoSeparator;
    sep->addChild(coord);
    sep->addChild(ils);
    return sep;
}


/**
 * Build a shaded SoIndexedFaceSet from a BoT's triangular faces.
 *
 * Normals are per-face (from face_normals if available, otherwise computed
 * from the cross product of the face edges).
 */
static SoSeparator *
bot_shaded_node(const struct rt_bot_internal *bot)
{
    if (!bot || bot->num_faces == 0 || bot->num_vertices == 0)
	return nullptr;

    /* Coordinate node */
    SoCoordinate3 *coord = new SoCoordinate3;
    coord->point.setNum((int)bot->num_vertices);
    for (size_t vi = 0; vi < bot->num_vertices; vi++) {
	coord->point.set1Value((int)vi,
	    (float)bot->vertices[vi*3 + 0],
	    (float)bot->vertices[vi*3 + 1],
	    (float)bot->vertices[vi*3 + 2]);
    }

    /* Face-index array: each triangle = [v0, v1, v2, -1] */
    int nfaces = (int)bot->num_faces;
    SoIndexedFaceSet *ifs = new SoIndexedFaceSet;
    ifs->coordIndex.setNum(nfaces * 4);
    for (int fi = 0; fi < nfaces; fi++) {
	ifs->coordIndex.set1Value(fi*4 + 0, bot->faces[fi*3 + 0]);
	ifs->coordIndex.set1Value(fi*4 + 1, bot->faces[fi*3 + 1]);
	ifs->coordIndex.set1Value(fi*4 + 2, bot->faces[fi*3 + 2]);
	ifs->coordIndex.set1Value(fi*4 + 3, SO_END_FACE_INDEX);
    }

    /* Normals: prefer stored face normals, otherwise compute */
    bool have_normals = (bot->num_face_normals == (size_t)nfaces &&
			 bot->face_normals != nullptr &&
			 bot->normals != nullptr);

    SoNormalBinding *nb = new SoNormalBinding;
    nb->value = SoNormalBinding::PER_FACE_INDEXED;

    SoNormal *norm = new SoNormal;
    norm->vector.setNum(nfaces);

    if (have_normals) {
	/* Use one normal per face: take the first vertex normal index of
	 * each face as a representative face normal */
	for (int fi = 0; fi < nfaces; fi++) {
	    int ni = bot->face_normals[fi*3 + 0]; /* first vertex's normal */
	    if (ni < 0 || (size_t)ni >= bot->num_normals) {
		norm->vector.set1Value(fi, 0.0f, 0.0f, 1.0f);
	    } else {
		norm->vector.set1Value(fi,
		    (float)bot->normals[ni*3 + 0],
		    (float)bot->normals[ni*3 + 1],
		    (float)bot->normals[ni*3 + 2]);
	    }
	}
    } else {
	/* Compute per-face normals from cross products */
	for (int fi = 0; fi < nfaces; fi++) {
	    int v0 = bot->faces[fi*3 + 0];
	    int v1 = bot->faces[fi*3 + 1];
	    int v2 = bot->faces[fi*3 + 2];

	    float ax = (float)(bot->vertices[v1*3+0] - bot->vertices[v0*3+0]);
	    float ay = (float)(bot->vertices[v1*3+1] - bot->vertices[v0*3+1]);
	    float az = (float)(bot->vertices[v1*3+2] - bot->vertices[v0*3+2]);
	    float bx = (float)(bot->vertices[v2*3+0] - bot->vertices[v0*3+0]);
	    float by = (float)(bot->vertices[v2*3+1] - bot->vertices[v0*3+1]);
	    float bz = (float)(bot->vertices[v2*3+2] - bot->vertices[v0*3+2]);

	    float nx = ay*bz - az*by;
	    float ny = az*bx - ax*bz;
	    float nz = ax*by - ay*bx;
	    float len = sqrtf(nx*nx + ny*ny + nz*nz);
	    if (len > 1e-10f) { nx /= len; ny /= len; nz /= len; }

	    norm->vector.set1Value(fi, nx, ny, nz);
	}
    }

    /* normalIndex: one per face (indexed at face position) */
    ifs->normalIndex.setNum(nfaces * 4);
    for (int fi = 0; fi < nfaces; fi++) {
	/* PER_FACE_INDEXED: one normal index per face, at the first slot */
	ifs->normalIndex.set1Value(fi*4 + 0, fi);
	ifs->normalIndex.set1Value(fi*4 + 1, fi);
	ifs->normalIndex.set1Value(fi*4 + 2, fi);
	ifs->normalIndex.set1Value(fi*4 + 3, SO_END_FACE_INDEX);
    }

    SoSeparator *sep = new SoSeparator;
    sep->addChild(coord);
    sep->addChild(nb);
    sep->addChild(norm);
    sep->addChild(ifs);
    return sep;
}


/* ------------------------------------------------------------------ */
/* rt_bot_scene_obj                                                     */
/* ------------------------------------------------------------------ */

extern "C" int
rt_bot_scene_obj(bsg_shape *s,
		 struct directory *dp,
		 struct db_i *dbip,
		 const struct bg_tess_tol *UNUSED(ttol),
		 const struct bn_tol *UNUSED(tol),
		 const bsg_view *v)
{
    if (!s || !dp || !dbip)
	return BRLCAD_ERROR;

    /* -------------------------------------------------------------- */
    /* Placeholder path: AABB not yet available                        */
    /* -------------------------------------------------------------- */
    if (!s->have_bbox) {
	s->csg_obj = 0;
	s->mesh_obj = 1;

	if (v && s->s_have_obb) {
	    bsg_shape *vo = bsg_shape_for_view(s, (bsg_view *)v);
	    if (!vo)
		vo = bsg_shape_get_view_obj(s, (bsg_view *)v);
	    vo->mesh_obj = 1;
	    vo->csg_obj  = 0;

	    if (!vo->draw_data && BU_LIST_IS_EMPTY(&vo->s_vlist)) {
		/* Draw the OBB placeholder as a wireframe box */
		point_t obb_pts[8];
		for (int k = 0; k < 8; k++)
		    VSET(obb_pts[k],
			 s->s_obb_pts[k*3+0],
			 s->s_obb_pts[k*3+1],
			 s->s_obb_pts[k*3+2]);
		bsg_vlist_arb8(vo->vlfree, &vo->s_vlist, (const point_t *)obb_pts);
		vo->s_placeholder = 2;
	    }
	}

	/* No AABB and no OBB → no-op; will be retried on next redraw. */
	return BRLCAD_OK;
    }

    /* -------------------------------------------------------------- */
    /* Full geometry path                                              */
    /* -------------------------------------------------------------- */
    struct resource *res = s->s_res ? s->s_res : &rt_uniresource;
    struct rt_db_internal intern;

    if (rt_db_get_internal(&intern, dp, dbip, NULL, res) < 0)
	return BRLCAD_ERROR;
    RT_CK_DB_INTERNAL(&intern);

    if (intern.idb_minor_type != DB5_MINORTYPE_BRLCAD_BOT) {
	rt_db_free_internal(&intern);
	return BRLCAD_ERROR;
    }

    struct rt_bot_internal *bot = (struct rt_bot_internal *)intern.idb_ptr;
    RT_BOT_CK_MAGIC(bot);

    int dmode = s->s_os ? s->s_os->s_dmode : 0;
    SoSeparator *geom_sep = nullptr;

    switch (dmode) {
	case 2:
	case 4:
	    /* Shaded (and shaded + hidden-line): build face set */
	    geom_sep = bot_shaded_node(bot);
	    if (!geom_sep)
		geom_sep = bot_wireframe_node(bot); /* fallback */
	    break;

	case 0:
	default:
	    /* Wireframe */
	    geom_sep = bot_wireframe_node(bot);
	    break;
    }

    if (geom_sep) {
	geom_sep->ref();
	bsg_shape_set_obol_node(s, geom_sep);
	geom_sep->unref();
    }

    s->mesh_obj = 1;
    s->csg_obj  = 0;
    s->current  = 1;

    rt_db_free_internal(&intern);
    return BRLCAD_OK;
}

#else /* !BRLCAD_ENABLE_OBOL */

/* When Obol is not available, fall back to rt_generic_scene_obj so the
 * functab entry is always a valid function pointer. */
extern int rt_generic_scene_obj(bsg_shape *s, struct directory *dp,
				struct db_i *dbip,
				const struct bg_tess_tol *ttol,
				const struct bn_tol *tol,
				const bsg_view *v);

extern "C" int
rt_bot_scene_obj(bsg_shape *s,
		 struct directory *dp,
		 struct db_i *dbip,
		 const struct bg_tess_tol *ttol,
		 const struct bn_tol *tol,
		 const bsg_view *v)
{
    return rt_generic_scene_obj(s, dp, dbip, ttol, tol, v);
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
