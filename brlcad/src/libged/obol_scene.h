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
#include "ged/defines.h"

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

GED_EXPORT SoSeparator *obol_scene_create(void);
GED_EXPORT void obol_scene_assemble(SoSeparator *scene_root, bsg_view *v);
GED_EXPORT void obol_scene_clear(SoSeparator *scene_root);
GED_EXPORT SoTransform *obol_mat_to_transform(const mat_t m);
GED_EXPORT bsg_shape *obol_find_shape_for_path(const SoPath *pick_path);
GED_EXPORT void obol_shape_set_selected(bsg_shape *s, bool selected);
GED_EXPORT bool obol_shape_is_selected(bsg_shape *s);

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
