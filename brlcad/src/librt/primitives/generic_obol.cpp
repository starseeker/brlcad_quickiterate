/*                G E N E R I C _ O B O L . C P P
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
/** @file primitives/generic_obol.cpp
 *
 * Compatibility shim: converts a bsg_shape's vlist (produced by the
 * existing rt_wireframe_plot / rt_shaded_plot path in generic.c) into
 * an Obol SoNode subtree stored in bsg_shape::s_obol_node.
 *
 * This shim is compiled only when BRLCAD_ENABLE_OBOL is defined (the Obol
 * library was found in the bext dependency tree).
 *
 * Called via the C-visible bridge:
 *   extern void rt_generic_vlist_to_obol(bsg_shape *s);
 *
 * Architecture note:
 *   This file is the "compatibility shim" described in RADICAL_MIGRATION.md
 *   Stage 1.  It allows the generic drawing path to feed the Obol renderer
 *   without requiring every primitive to have a native Obol implementation.
 *   Once a primitive has its own ft_scene_obj Obol implementation, this
 *   shim is no longer called for that primitive type.
 *
 * @see RADICAL_MIGRATION.md Stage 1
 * @see src/librt/primitives/generic.c (caller)
 */

#include "common.h"

#ifdef BRLCAD_ENABLE_OBOL

#include <vector>
#include <cstdint>

#include "bsg/defines.h"
/* Suppress -Wfloat-equal from third-party Obol (Open Inventor) headers */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#include "bsg/obol_node.h"

#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

/* BSG vlist command constants */
#ifndef BSG_VLIST_LINE_MOVE
#  include "bsg/defines.h"
#endif


/**
 * Convert a bsg_shape's vlist to an Obol SoNode subtree.
 *
 * Inspects s->s_vlist, classifies the content (wireframe lines or
 * solid polygons/triangles), and builds the corresponding Obol nodes:
 *
 *   - Wireframe content (LINE_MOVE / LINE_DRAW):
 *       SoCoordinate3 + SoIndexedLineSet
 *
 *   - Solid content (POLY_* / TRI_*):
 *       SoCoordinate3 + SoNormalBinding(PER_FACE) + SoNormal + SoFaceSet
 *
 * The resulting SoSeparator is stored in s->s_obol_node via
 * bsg_shape_set_obol_node().  If the vlist is empty or yields no
 * usable geometry, s->s_obol_node is left unchanged.
 *
 * C-callable bridge; called from generic.c under #ifdef BRLCAD_ENABLE_OBOL.
 */
extern "C" void
rt_generic_vlist_to_obol(bsg_shape *s)
{
    if (!s)
	return;
    if (BU_LIST_IS_EMPTY(&s->s_vlist))
	return;

    /* Pass 1: classify vlist content */
    bool has_lines = false;
    bool has_polys = false;

    {
	struct bsg_vlist *tvp;
	for (BU_LIST_FOR(tvp, bsg_vlist, &s->s_vlist)) {
	    int nused = tvp->nused;
	    int *cmd = tvp->cmd;
	    for (int i = 0; i < nused; i++, cmd++) {
		switch (*cmd) {
		    case BSG_VLIST_LINE_MOVE:
		    case BSG_VLIST_LINE_DRAW:
			has_lines = true;
			break;
		    case BSG_VLIST_POLY_START:
		    case BSG_VLIST_POLY_MOVE:
		    case BSG_VLIST_POLY_DRAW:
		    case BSG_VLIST_POLY_END:
		    case BSG_VLIST_TRI_START:
		    case BSG_VLIST_TRI_MOVE:
		    case BSG_VLIST_TRI_DRAW:
		    case BSG_VLIST_TRI_END:
			has_polys = true;
			break;
		    default:
			break;
		}
		/* Short-circuit once we know the content type */
		if (has_lines && has_polys)
		    break;
	    }
	    if (has_lines && has_polys)
		break;
	}
    }

    if (!has_lines && !has_polys)
	return;

    SoSeparator *root = new SoSeparator;
    root->ref();

    /* ------------------------------------------------------------------ */
    /* Solid geometry path                                                  */
    /* ------------------------------------------------------------------ */
    if (has_polys) {
	/* Collect sequential face vertices and per-face normals.
	 *
	 * Vlist polygon / triangle encoding (following GL convention):
	 *   POLY_START(normal) POLY_MOVE(v0) POLY_DRAW(v1) ... POLY_END
	 *   TRI_START(normal)  TRI_MOVE(v0)  TRI_DRAW(v1) TRI_DRAW(v2) TRI_END
	 *
	 * POLY_END and TRI_END terminate the face without contributing a
	 * vertex (the pt field of those commands is ignored here).
	 */
	std::vector<float> coords;      /* sequential vertices x,y,z */
	std::vector<int32_t> num_verts; /* vertex count per face */
	std::vector<float> normals;     /* one normal (x,y,z) per face */

	int face_vert_count = 0;
	bool in_face = false;

	struct bsg_vlist *tvp;
	for (BU_LIST_FOR(tvp, bsg_vlist, &s->s_vlist)) {
	    int nused = tvp->nused;
	    int *cmd = tvp->cmd;
	    point_t *pt = tvp->pt;
	    for (int i = 0; i < nused; i++, cmd++, pt++) {
		switch (*cmd) {
		    case BSG_VLIST_POLY_START:
		    case BSG_VLIST_TRI_START:
			/* pt is the face / triangle normal */
			normals.push_back((float)(*pt)[0]);
			normals.push_back((float)(*pt)[1]);
			normals.push_back((float)(*pt)[2]);
			in_face = true;
			face_vert_count = 0;
			break;

		    case BSG_VLIST_POLY_MOVE:
		    case BSG_VLIST_TRI_MOVE:
			/* First vertex of current face */
			coords.push_back((float)(*pt)[0]);
			coords.push_back((float)(*pt)[1]);
			coords.push_back((float)(*pt)[2]);
			face_vert_count++;
			break;

		    case BSG_VLIST_POLY_DRAW:
		    case BSG_VLIST_TRI_DRAW:
			/* Subsequent vertices */
			coords.push_back((float)(*pt)[0]);
			coords.push_back((float)(*pt)[1]);
			coords.push_back((float)(*pt)[2]);
			face_vert_count++;
			break;

		    case BSG_VLIST_POLY_END:
		    case BSG_VLIST_TRI_END:
			/* Close the face; pt is NOT a vertex here
			 * (consistent with the GL renderer's glEnd() path). */
			if (in_face && face_vert_count > 0)
			    num_verts.push_back(face_vert_count);
			in_face = false;
			face_vert_count = 0;
			break;

		    case BSG_VLIST_POLY_VERTNORM:
		    case BSG_VLIST_TRI_VERTNORM:
			/* Per-vertex normals ignored in this pass;
			 * face normals from START commands are used instead. */
			break;

		    default:
			break;
		}
	    }
	}

	if (!num_verts.empty() && !coords.empty()) {
	    /* Coordinate node */
	    SoCoordinate3 *coord = new SoCoordinate3;
	    int npts = (int)coords.size() / 3;
	    coord->point.setNum(npts);
	    for (int i = 0; i < npts; i++)
		coord->point.set1Value(i, coords[i*3], coords[i*3+1], coords[i*3+2]);
	    root->addChild(coord);

	    /* Per-face normals */
	    if (!normals.empty()) {
		SoNormalBinding *nb = new SoNormalBinding;
		nb->value = SoNormalBinding::PER_FACE;
		root->addChild(nb);

		SoNormal *norm = new SoNormal;
		int nf = (int)normals.size() / 3;
		norm->vector.setNum(nf);
		for (int i = 0; i < nf; i++)
		    norm->vector.set1Value(i, normals[i*3], normals[i*3+1], normals[i*3+2]);
		root->addChild(norm);
	    }

	    /* Face set (non-indexed sequential vertices) */
	    SoFaceSet *fs = new SoFaceSet;
	    int nfaces = (int)num_verts.size();
	    fs->numVertices.setNum(nfaces);
	    for (int i = 0; i < nfaces; i++)
		fs->numVertices.set1Value(i, num_verts[i]);
	    root->addChild(fs);
	}
    }

    /* ------------------------------------------------------------------ */
    /* Wireframe path                                                       */
    /* ------------------------------------------------------------------ */
    if (has_lines) {
	/* Build an SoIndexedLineSet from LINE_MOVE / LINE_DRAW commands.
	 * Each LINE_MOVE starts a new polyline; consecutive LINE_DRAWs
	 * extend it.  Polylines are separated by SO_END_LINE_INDEX (-1).
	 */
	std::vector<float> coords;
	std::vector<int32_t> indices;

	int cur_idx = 0;
	bool in_segment = false;

	struct bsg_vlist *tvp;
	for (BU_LIST_FOR(tvp, bsg_vlist, &s->s_vlist)) {
	    int nused = tvp->nused;
	    int *cmd = tvp->cmd;
	    point_t *pt = tvp->pt;
	    for (int i = 0; i < nused; i++, cmd++, pt++) {
		switch (*cmd) {
		    case BSG_VLIST_LINE_MOVE:
			/* Start a new polyline */
			if (in_segment)
			    indices.push_back(SO_END_LINE_INDEX);
			coords.push_back((float)(*pt)[0]);
			coords.push_back((float)(*pt)[1]);
			coords.push_back((float)(*pt)[2]);
			indices.push_back(cur_idx++);
			in_segment = true;
			break;

		    case BSG_VLIST_LINE_DRAW:
			coords.push_back((float)(*pt)[0]);
			coords.push_back((float)(*pt)[1]);
			coords.push_back((float)(*pt)[2]);
			indices.push_back(cur_idx++);
			break;

		    default:
			break;
		}
	    }
	}
	if (in_segment)
	    indices.push_back(SO_END_LINE_INDEX);

	if (!coords.empty()) {
	    SoCoordinate3 *coord = new SoCoordinate3;
	    int npts = (int)coords.size() / 3;
	    coord->point.setNum(npts);
	    for (int i = 0; i < npts; i++)
		coord->point.set1Value(i, coords[i*3], coords[i*3+1], coords[i*3+2]);
	    root->addChild(coord);

	    SoIndexedLineSet *ils = new SoIndexedLineSet;
	    int nidx = (int)indices.size();
	    ils->coordIndex.setNum(nidx);
	    for (int i = 0; i < nidx; i++)
		ils->coordIndex.set1Value(i, indices[i]);
	    root->addChild(ils);
	}
    }

    /* Store node on the shape if we produced any geometry */
    if (root->getNumChildren() > 0)
	bsg_shape_set_obol_node(s, root);

    root->unref();  /* bsg_shape_set_obol_node added its own ref() */
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
