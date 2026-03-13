/*                    O B O L _ S C E N E . C P P
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
/** @file libged/obol_scene.cpp
 *
 * Scene assembler: bridges bsg_shape tree → Obol SoSeparator scene.
 *
 * This file is compiled only when BRLCAD_ENABLE_OBOL is set (the Obol
 * library was found in the bext dependency tree).
 *
 * @see obol_scene.h for API documentation
 * @see RADICAL_MIGRATION.md Stage 0 and Stage 2
 */

#include "common.h"

#ifdef BRLCAD_ENABLE_OBOL

#include "obol_scene.h"

#include "bsg.h"
#include "bu/ptbl.h"
#include "bu/vls.h"

/* Obol (Open Inventor) scene-graph nodes.
 * Suppress -Wfloat-equal from third-party Obol headers (SbVec*, SbBasic) */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbRotation.h>
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include <unordered_map>

/* --------------------------------------------------------------------------
 * Module-level shape → Obol subtree cache
 *
 * Maps each bsg_shape pointer to the SoSeparator that wraps its geometry
 * child in the Obol scene.  When obol_scene_assemble() runs again after an
 * update, it can find the existing subtree and update its children in-place
 * rather than recreating the whole tree.
 *
 * Note: bsg_shape pointers are only used as keys here; we never dereference
 * a stale key.  The cache is invalidated (entry removed) when a shape's
 * s_changed flag is set or when the shape is explicitly erased.
 * -------------------------------------------------------------------------- */
static std::unordered_map<bsg_shape *, SoSeparator *> &
shape_sep_map()
{
    static std::unordered_map<bsg_shape *, SoSeparator *> m;
    return m;
}


/* --------------------------------------------------------------------------
 * obol_scene_create
 * -------------------------------------------------------------------------- */

SoSeparator *
obol_scene_create(void)
{
    SoSeparator *root = new SoSeparator;
    root->ref();

    /* Default headlight-style directional light */
    SoDirectionalLight *light = new SoDirectionalLight;
    light->direction.setValue(-1.0f, -1.0f, -1.0f);
    light->intensity = 1.0f;
    root->addChild(light);

    return root;
}


/* --------------------------------------------------------------------------
 * obol_mat_to_transform
 * -------------------------------------------------------------------------- */

SoTransform *
obol_mat_to_transform(const mat_t m)
{
    /* BRL-CAD mat_t layout (row-major storage, column vectors):
     *
     *   Index:  [ 0  1  2  3]   row 0
     *           [ 4  5  6  7]   row 1
     *           [ 8  9 10 11]   row 2
     *           [12 13 14 15]   row 3
     *
     * The values are stored as m[row*4 + col], so m[0]=M00, m[1]=M01, ...
     * That is the same row-major order that SbMatrix expects for its
     * constructor argument.  No transposition is required.
     */
    SbMatrix sbm(
	(float)m[0],  (float)m[1],  (float)m[2],  (float)m[3],
	(float)m[4],  (float)m[5],  (float)m[6],  (float)m[7],
	(float)m[8],  (float)m[9],  (float)m[10], (float)m[11],
	(float)m[12], (float)m[13], (float)m[14], (float)m[15]
    );

    SbVec3f   translation;
    SbRotation rotation;
    SbVec3f   scaleFactor;
    SbRotation scaleOrientation;
    sbm.getTransform(translation, rotation, scaleFactor, scaleOrientation);

    SoTransform *xf = new SoTransform;
    xf->ref();
    xf->translation.setValue(translation);
    xf->rotation.setValue(rotation);
    xf->scaleFactor.setValue(scaleFactor);
    xf->scaleOrientation.setValue(scaleOrientation);
    return xf;
}


/* --------------------------------------------------------------------------
 * obol_scene_clear
 * -------------------------------------------------------------------------- */

void
obol_scene_clear(SoSeparator *scene_root)
{
    if (!scene_root)
	return;

    /* Keep the first child (directional light).  Remove everything else. */
    while (scene_root->getNumChildren() > 1)
	scene_root->removeChild(scene_root->getNumChildren() - 1);

    shape_sep_map().clear();
}


/* --------------------------------------------------------------------------
 * obol_scene_update_shape
 *
 * Ensure the Obol subtree for a single bsg_shape is current.  Called from
 * obol_scene_assemble() for each leaf shape.
 * -------------------------------------------------------------------------- */

static void
obol_scene_update_shape(SoSeparator *scene_root, bsg_shape *s)
{
    if (!s || !s->s_obol_node)
	return;   /* no Obol node yet — vlist fallback or not drawn */

    SoNode *geom_node = static_cast<SoNode *>(s->s_obol_node);
    auto &m = shape_sep_map();

    SoSeparator *shape_sep = nullptr;

    auto it = m.find(s);
    if (it != m.end()) {
	shape_sep = it->second;
	/* Check if a full rebuild is needed (s_changed) */
	if (!s->s_changed) {
	    /* The geometry node is always the last child; update it if changed. */
	    int nc = shape_sep->getNumChildren();
	    if (nc > 0) {
		SoNode *old_geom = shape_sep->getChild(nc - 1);
		if (old_geom != geom_node) {
		    shape_sep->replaceChild(old_geom, geom_node);
		}
	    }
	    return;
	}
	/* Full rebuild: remove old and recreate */
	scene_root->removeChild(shape_sep);
	shape_sep->unref();
	m.erase(it);
	shape_sep = nullptr;
    }

    /* Build a new SoSeparator for this shape:
     *   SoSeparator (shape_sep)
     *     SoTransform     — from s->s_mat
     *     SoBaseColor     — from s->s_os->s_color (always added)
     *     <s_obol_node>   — geometry from ft_scene_obj (always last child)
     *
     * SoBaseColor is always added (even if s_os is NULL) to maintain a
     * consistent child layout where the geometry node is always the last
     * child regardless of material availability.
     */
    shape_sep = new SoSeparator;
    shape_sep->ref();

    /* 1. Transform from cumulative matrix */
    SoTransform *xf = obol_mat_to_transform(s->s_mat);
    shape_sep->addChild(xf);
    xf->unref();  /* shape_sep now owns the reference */

    /* 2. Color from s_os->s_color (RGB 0-255) — always added */
    {
	SoBaseColor *col = new SoBaseColor;
	if (s->s_os) {
	    col->rgb.setValue(s->s_os->color[0] / 255.0f,
			     s->s_os->color[1] / 255.0f,
			     s->s_os->color[2] / 255.0f);
	} else {
	    col->rgb.setValue(1.0f, 0.0f, 0.0f);   /* fallback: red */
	}
	shape_sep->addChild(col);
    }

    /* 3. Geometry node from ft_scene_obj (last child) */
    shape_sep->addChild(geom_node);

    /* Register and attach */
    m[s] = shape_sep;
    scene_root->addChild(shape_sep);

    /* Clear the changed flag */
    s->s_changed = 0;
}


/* --------------------------------------------------------------------------
 * obol_scene_assemble
 * -------------------------------------------------------------------------- */

void
obol_scene_assemble(SoSeparator *scene_root, bsg_view *v)
{
    if (!scene_root || !v)
	return;

    bsg_shape *view_root = bsg_scene_root_get(v);
    if (!view_root)
	return;

    /* Walk the flat list of shapes in the view root's children table. */
    for (size_t i = 0; i < BU_PTBL_LEN(&view_root->children); i++) {
	bsg_shape *s = (bsg_shape *)BU_PTBL_GET(&view_root->children, i);
	if (!s)
	    continue;

	/* Container nodes (combs with children): iterate into their leaf
	 * shapes.  Containers do not carry their own s_obol_node in the
	 * current flat-list architecture (Stage 1 of RADICAL_MIGRATION.md).
	 * In Stage 2 these will become proper SoSeparator hierarchy nodes,
	 * but for now we skip the container itself and process only its
	 * leaf descendants directly into the flat scene root. */
	if (BU_PTBL_LEN(&s->children) > 0) {
	    for (size_t ci = 0; ci < BU_PTBL_LEN(&s->children); ci++) {
		bsg_shape *child = (bsg_shape *)BU_PTBL_GET(&s->children, ci);
		obol_scene_update_shape(scene_root, child);
	    }
	    continue;
	}

	obol_scene_update_shape(scene_root, s);
    }
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
