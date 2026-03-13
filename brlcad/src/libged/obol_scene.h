/*                    O B O L _ S C E N E . H
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
/** @file libged/obol_scene.h
 *
 * Scene assembler: builds an Obol SoSeparator scene from a BRL-CAD
 * bsg_shape tree after the draw pipeline has populated s_obol_node on
 * individual shapes.
 *
 * This is the primary bridge between BRL-CAD's drawing pipeline and Obol's
 * scene-graph renderer.  It is called from QgEdApp::do_view_changed() (or
 * equivalent) whenever the scene is dirty.
 *
 * Architecture summary:
 *
 *   1. libged draw command → draw_gather_paths() + draw_scene()
 *      → ft_scene_obj() populates bsg_shape::s_obol_node (SoNode*)
 *
 *   2. obol_scene_assemble() walks the bsg_shape tree and builds:
 *        root SoSeparator
 *          SoDirectionalLight
 *          SoPerspectiveCamera (if not already present)
 *          per-shape SoSeparator
 *            SoTransform    (from bsg_shape::s_mat)
 *            SoMaterial     (from bsg_shape::s_os->s_color)
 *            <s_obol_node>  (geometry node from ft_scene_obj)
 *
 *   3. QgObolView::setSceneGraph(root) → SoViewport/SoRenderManager renders
 *
 * @see RADICAL_MIGRATION.md Stage 0 and Stage 2
 */

#pragma once

#ifdef BRLCAD_ENABLE_OBOL

#include "bsg/defines.h"

#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoTransform.h>

/**
 * Create a new (empty) Obol scene root with default lighting.
 *
 * The returned SoSeparator has one reference (ref() has been called).
 * The caller owns this reference and must call unref() when done.
 *
 * A SoDirectionalLight is added automatically.  A camera is NOT added here;
 * it is added later by QgObolView::setSceneGraph() if none is present.
 */
SoSeparator *obol_scene_create(void);

/**
 * Assemble / update the Obol scene from the bsg_shape tree for @p v.
 *
 * Walks bsg_shape objects in @p v's scene root and for each shape with a
 * non-NULL s_obol_node, ensures a corresponding child SoSeparator exists in
 * @p scene_root with the correct SoTransform (from s->s_mat) and the shape's
 * node as the geometry child.
 *
 * Shapes whose s_obol_node is NULL (ft_scene_obj has not yet run, or the
 * shape has a vlist-only fallback) are skipped; they will be picked up on
 * the next call once the draw pipeline delivers their node.
 *
 * This function is safe to call on every view_changed event; it is designed
 * to be incremental — only shapes whose s_changed flag is set (or whose
 * corresponding SoSeparator child does not yet exist) are updated.
 *
 * @param scene_root  Root SoSeparator returned by obol_scene_create().
 * @param v           View whose scene root (bsg_scene_root_get()) is walked.
 */
void obol_scene_assemble(SoSeparator *scene_root, bsg_view *v);

/**
 * Remove all geometry children from @p scene_root, keeping the light.
 *
 * Useful when the database is closed (erase all) or a full redraw is
 * needed.  The light node is NOT removed.
 */
void obol_scene_clear(SoSeparator *scene_root);

/**
 * Helper: convert a BRL-CAD mat_t (column-major 4×4) to an SoTransform.
 *
 * The returned node has one reference added.  Pass to SoSeparator::addChild()
 * then call unref() on the pointer.
 */
SoTransform *obol_mat_to_transform(const mat_t m);

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
