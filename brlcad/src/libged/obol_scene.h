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
 *   2. obol_scene_assemble() walks the bsg_shape tree and builds a hierarchy:
 *
 *        root SoSeparator
 *          SoDirectionalLight
 *          SoSwitch (group visibility, per s_flag)
 *            SoSeparator (group — one per top-level draw-call container)
 *              SoBaseColor       (group-level inherited color)
 *              SoSwitch (leaf visibility)
 *                SoSeparator (leaf)
 *                  SoDrawStyle   (per-object mode: LINES/FILLED/POINTS)
 *                  SoTransform   (from bsg_shape::s_mat)
 *                  SoMaterial    (per-leaf Phong material)
 *                  <s_obol_node> (geometry node from ft_scene_obj)
 *              ...
 *          SoSwitch (standalone leaf visibility)
 *            SoSeparator (standalone leaf)
 *              ...
 *
 *   3. QgObolView::setSceneGraph(root) → SoViewport/SoRenderManager renders
 *
 * @see RADICAL_MIGRATION.md Stage 2
 */

#pragma once

#ifdef BRLCAD_ENABLE_OBOL

#include "bsg/defines.h"

/* Suppress -Wfloat-equal from third-party Obol/Inventor headers */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/SoPath.h>
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

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
 * Stage 2 hierarchical scene assembly (RADICAL_MIGRATION.md §Stage 2):
 *
 * Walks the bsg_shape tree under @p v's scene root.  For each top-level
 * container shape (a draw-call group with children) a dedicated SoSeparator
 * group node is created or reused.  Each leaf solid inside the group is
 * nested under the group's SoSeparator with its own SoTransform and
 * SoMaterial, reflecting the hierarchy:
 *
 *   scene_root → [SoSwitch → SoSeparator(group) → [SoSwitch → SoSeparator(leaf)]]
 *
 * Visibility: each group and each leaf is wrapped in a SoSwitch node whose
 * @c whichChild is set to SO_SWITCH_ALL (visible) or SO_SWITCH_NONE (hidden)
 * based on the shape's @c s_flag field (UP = visible, DOWN = hidden).
 *
 * Material inheritance: a SoBaseColor is emitted at the group level so that
 * descendant leaves that do not override color inherit it through the Obol
 * traversal state stack.
 *
 * Shapes whose @c s_obol_node is NULL (ft_scene_obj has not yet run, or the
 * shape has a vlist-only fallback) are silently skipped.  They will be
 * included on the next call once the draw pipeline delivers their node.
 *
 * Incremental: only shapes whose @c s_changed flag is set (or which have no
 * corresponding SoSeparator in the cache yet) are rebuilt.
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

/* ====================================================================== *
 * Stage 5: Selection and Picking                                          *
 * ====================================================================== */

/**
 * Resolve an Obol pick path back to the BRL-CAD @c bsg_shape leaf it
 * corresponds to, if any.
 *
 * @p pick_path is the @c SoPath returned by @c SoPickedPoint::getPath().
 * This function walks the path from the tail (deepest node) toward the root,
 * looking for the first @c SoSeparator that appears in the internal
 * shape-separator reverse map maintained by @c obol_scene_assemble().
 *
 * Returns the matching @c bsg_shape pointer, or @c nullptr if the path does
 * not correspond to any assembled shape (e.g. the pick hit the background or
 * a group separator with no associated leaf shape).
 *
 * Thread safety: must be called on the main (GL) thread.
 */
bsg_shape *obol_find_shape_for_path(const SoPath *pick_path);

/**
 * Mark a @c bsg_shape's Obol separator as selected or deselected.
 *
 * Sets an internal selection flag on the shape's separator so that the next
 * call to @c obol_scene_assemble() (which re-runs with @c s_changed forced)
 * will rebuild the shape's @c SoMaterial with an emissive highlight colour.
 *
 * Calling with @p selected = @c false reverts the material to the normal
 * diffuse-only appearance.
 *
 * @param s        The leaf bsg_shape to select/deselect.
 * @param selected @c true to highlight; @c false to revert.
 */
void obol_shape_set_selected(bsg_shape *s, bool selected);

/**
 * Return @c true if @p s is currently marked as selected.
 */
bool obol_shape_is_selected(bsg_shape *s);

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
