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
#include <Inventor/nodes/SoDrawStyle.h>
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
 * Module-level shape → Obol subtree caches                               *
 *                                                                          *
 * shape_sep_map:  bsg_shape (leaf)  → SoSeparator wrapping its geometry  *
 * group_sep_map:  bsg_shape (group) → SoSeparator wrapping its children  *
 *                                                                          *
 * Both caches are keyed by pointer value only; we never dereference a     *
 * stale entry.  Entries are invalidated (removed) when s_changed is set   *
 * or when a shape is explicitly erased (obol_scene_clear clears both).    *
 * -------------------------------------------------------------------------- */
static std::unordered_map<bsg_shape *, SoSeparator *> &
shape_sep_map()
{
    static std::unordered_map<bsg_shape *, SoSeparator *> m;
    return m;
}

static std::unordered_map<bsg_shape *, SoSeparator *> &
group_sep_map()
{
    static std::unordered_map<bsg_shape *, SoSeparator *> m;
    return m;
}

/* Phong material defaults for leaf shapes.
 * Ambient is derived from the diffuse color (ambient = diffuse * factor). */
static constexpr float OBOL_MAT_AMBIENT_FACTOR = 0.3f;
static constexpr float OBOL_MAT_SPECULAR_VALUE  = 0.4f;
static constexpr float OBOL_MAT_SHININESS       = 0.5f;


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
    group_sep_map().clear();
}


/* --------------------------------------------------------------------------
 * obol_scene_update_shape
 *
 * Ensure the Obol subtree for a single bsg_shape leaf is current.  The leaf
 * separator is added as a child of @p parent_sep rather than the scene root,
 * so it appears nested under its group's SoSeparator.
 *
 * Visibility: each leaf is wrapped in a SoSwitch so that objects with
 * s_flag == DOWN are hidden without removing them from the cache.
 * -------------------------------------------------------------------------- */

static void
obol_scene_update_shape(SoSeparator *parent_sep, bsg_shape *s)
{
    if (!s || !s->s_obol_node)
	return;   /* no Obol node yet — vlist fallback or not drawn */

    SoNode *geom_node = static_cast<SoNode *>(s->s_obol_node);
    auto &m = shape_sep_map();

    SoSeparator *shape_sep = nullptr;

    auto it = m.find(s);
    if (it != m.end()) {
	shape_sep = it->second;
	if (!s->s_changed) {
	    /* Incremental update: just swap geometry node if it changed. */
	    /* The geometry node is always the last child of shape_sep (layout:
	     * SoTransform, SoMaterial, geometry).  The shape_sep itself is the
	     * single child of a SoSwitch that is a child of parent_sep. */
	    int nc = shape_sep->getNumChildren();
	    if (nc > 0) {
		SoNode *old_geom = shape_sep->getChild(nc - 1);
		if (old_geom != geom_node)
		    shape_sep->replaceChild(old_geom, geom_node);
	    }
	    return;
	}
	/* Full rebuild needed: remove old separator from parent */
	/* The separator's parent is the SoSwitch; remove the whole switch */
	for (int ci = parent_sep->getNumChildren() - 1; ci >= 0; ci--) {
	    SoNode *cn = parent_sep->getChild(ci);
	    if (cn->isOfType(SoSwitch::getClassTypeId())) {
		SoSwitch *sw = static_cast<SoSwitch *>(cn);
		if (sw->getNumChildren() > 0 && sw->getChild(0) == shape_sep) {
		    parent_sep->removeChild(ci);
		    break;
		}
	    }
	}
	shape_sep->unref();
	m.erase(it);
	shape_sep = nullptr;
    }

    /* ------------------------------------------------------------------ *
     * Build a new SoSwitch + SoSeparator pair for this leaf:             *
     *                                                                      *
     *   SoSwitch  (whichChild = SO_SWITCH_ALL or SO_SWITCH_NONE)         *
     *     SoSeparator (shape_sep)                                         *
     *       SoDrawStyle   — per-object draw style from s->s_os->s_dmode  *
     *       SoTransform   — from s->s_mat                                *
     *       SoMaterial    — from s->s_os->s_color (Phong diffuse)        *
     *       <s_obol_node> — geometry from ft_scene_obj (always last)     *
     * ------------------------------------------------------------------ */
    SoSwitch *sw = new SoSwitch;
    sw->whichChild = (s->s_flag == DOWN) ? SO_SWITCH_NONE : SO_SWITCH_ALL;

    shape_sep = new SoSeparator;
    shape_sep->ref();

    /* 1. Per-object draw style (Stage 3: Drawing Modes).
     *
     * Each shape carries its own SoDrawStyle so that mixed-mode scenes
     * (wireframe + shaded objects in the same view) render correctly when the
     * global SoRenderManager mode is AS_IS.
     *
     * Mapping from BRL-CAD s_dmode:
     *   0 — wireframe             → SoDrawStyle::LINES
     *   1 — hidden-line           → SoDrawStyle::LINES  (render manager
     *                                handles the hidden-line pass globally)
     *   2 — shaded (Phong)        → SoDrawStyle::FILLED
     *   3 — evaluated wireframe   → SoDrawStyle::LINES  (draw_m3 vlist path)
     *   4 — shaded + hidden-line  → SoDrawStyle::FILLED (render manager pass)
     *   5 — point cloud           → SoDrawStyle::POINTS
     */
    {
	SoDrawStyle *ds = new SoDrawStyle;
	int dmode = s->s_os ? s->s_os->s_dmode : 0;
	switch (dmode) {
	    case 2:
	    case 4:
		ds->style = SoDrawStyle::FILLED;
		break;
	    case 5:
		ds->style = SoDrawStyle::POINTS;
		ds->pointSize = 3.0f;
		break;
	    case 0:
	    case 1:
	    case 3:
	    default:
		ds->style = SoDrawStyle::LINES;
		ds->lineWidth = 1.0f;
		break;
	}
	shape_sep->addChild(ds);
    }

    /* 2. Transform */
    SoTransform *xf = obol_mat_to_transform(s->s_mat);
    shape_sep->addChild(xf);
    xf->unref();

    /* 3. Material (Phong diffuse from s_os color; SoBaseColor is flat-shaded,
     *    SoMaterial supports full lighting model including specular). */
    {
	SoMaterial *mat = new SoMaterial;
	if (s->s_os) {
	    float r = s->s_os->color[0] / 255.0f;
	    float g = s->s_os->color[1] / 255.0f;
	    float b = s->s_os->color[2] / 255.0f;
	    mat->diffuseColor.setValue(r, g, b);
	    mat->ambientColor.setValue(r * OBOL_MAT_AMBIENT_FACTOR,
				       g * OBOL_MAT_AMBIENT_FACTOR,
				       b * OBOL_MAT_AMBIENT_FACTOR);
	    mat->specularColor.setValue(OBOL_MAT_SPECULAR_VALUE,
					OBOL_MAT_SPECULAR_VALUE,
					OBOL_MAT_SPECULAR_VALUE);
	    mat->shininess = OBOL_MAT_SHININESS;
	} else {
	    mat->diffuseColor.setValue(1.0f, 0.0f, 0.0f);  /* fallback: red */
	    mat->ambientColor.setValue(OBOL_MAT_AMBIENT_FACTOR, 0.0f, 0.0f);
	}
	shape_sep->addChild(mat);
    }

    /* 4. Geometry node (last child) */
    shape_sep->addChild(geom_node);

    sw->addChild(shape_sep);
    parent_sep->addChild(sw);

    m[s] = shape_sep;
    s->s_changed = 0;
}


/* --------------------------------------------------------------------------
 * obol_scene_update_group
 *
 * Ensure an Obol SoSeparator exists for a group bsg_shape (a top-level draw-
 * call container with children).  Returns the group's SoSeparator so that
 * callers can add leaf children under it.
 *
 * Structure:
 *   SoSwitch  (whichChild = SO_SWITCH_ALL or SO_SWITCH_NONE per s_flag)
 *     SoSeparator (group_sep)
 *       SoBaseColor  — group-level inherited color from s_os
 *       [leaf SoSwitch / SoSeparator children added by caller]
 *
 * Visibility: the SoSwitch honours s_flag == DOWN to hide the whole group.
 *
 * Material inheritance: SoBaseColor is used at the group level so that leaves
 * which do not override color inherit it via the Open Inventor state stack
 * (SoSeparator saves/restores state, so per-leaf SoMaterial overrides do not
 * bleed into siblings).
 * -------------------------------------------------------------------------- */

static SoSeparator *
obol_scene_update_group(SoSeparator *scene_root, bsg_shape *g)
{
    auto &gm = group_sep_map();
    auto git = gm.find(g);
    if (git != gm.end()) {
	/* Group separator already exists; return it for incremental updates.
	 * Per-child updates are handled by obol_scene_update_shape above. */
	return git->second;
    }

    /* New group: build SoSwitch + SoSeparator */
    SoSwitch *sw = new SoSwitch;
    sw->whichChild = (g->s_flag == DOWN) ? SO_SWITCH_NONE : SO_SWITCH_ALL;

    SoSeparator *group_sep = new SoSeparator;
    group_sep->ref();

    /* Group-level color for material inheritance */
    if (g->s_os) {
	SoBaseColor *col = new SoBaseColor;
	col->rgb.setValue(g->s_os->color[0] / 255.0f,
			  g->s_os->color[1] / 255.0f,
			  g->s_os->color[2] / 255.0f);
	group_sep->addChild(col);
    }

    sw->addChild(group_sep);
    scene_root->addChild(sw);

    gm[g] = group_sep;
    return group_sep;
}


/* --------------------------------------------------------------------------
 * obol_scene_assemble
 *
 * Stage 2: build a hierarchical Obol scene from the bsg_shape tree.
 *
 * Hierarchy mapping (per RADICAL_MIGRATION.md Stage 2):
 *
 *   scene_root SoSeparator          ← created by obol_scene_create()
 *     SoDirectionalLight            ← added by obol_scene_create()
 *     SoSwitch (group visibility)
 *       SoSeparator (group)         ← one per top-level draw-call container
 *         SoBaseColor               ← group-level inherited color
 *         SoSwitch (leaf vis)
 *           SoSeparator (leaf)      ← one per leaf solid
 *             SoTransform           ← from s->s_mat
 *             SoMaterial            ← per-leaf Phong material
 *             <s_obol_node>         ← geometry from ft_scene_obj
 *         SoSwitch (leaf vis)
 *           ...
 *     SoSwitch (group visibility)
 *       SoSeparator (group)
 *         ...
 *
 * Shapes that have no children are treated as standalone leaves and added
 * directly under the scene root (not under a group SoSeparator).
 *
 * The function is incremental: shapes that have not changed (s_changed == 0
 * and already in the cache) are skipped.
 * -------------------------------------------------------------------------- */

void
obol_scene_assemble(SoSeparator *scene_root, bsg_view *v)
{
    if (!scene_root || !v)
	return;

    bsg_shape *view_root = bsg_scene_root_get(v);
    if (!view_root)
	return;

    for (size_t i = 0; i < BU_PTBL_LEN(&view_root->children); i++) {
	bsg_shape *s = (bsg_shape *)BU_PTBL_GET(&view_root->children, i);
	if (!s)
	    continue;

	if (BU_PTBL_LEN(&s->children) > 0) {
	    /* ------------------------------------------------------------ *
	     * Container node: one SoSeparator (via SoSwitch) per group,    *
	     * with each leaf child nested inside it.                        *
	     * ------------------------------------------------------------ */
	    SoSeparator *group_sep = obol_scene_update_group(scene_root, s);
	    for (size_t ci = 0; ci < BU_PTBL_LEN(&s->children); ci++) {
		bsg_shape *child = (bsg_shape *)BU_PTBL_GET(&s->children, ci);
		obol_scene_update_shape(group_sep, child);
	    }
	} else {
	    /* ------------------------------------------------------------ *
	     * Standalone leaf (no parent container): add directly to root. *
	     * ------------------------------------------------------------ */
	    obol_scene_update_shape(scene_root, s);
	}
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
