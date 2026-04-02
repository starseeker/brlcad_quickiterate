/*               O B O L _ C A D _ A S S E M B L Y . H
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
/** @file libged/obol_cad_assembly.h
 *
 * SoCADAssembly-based scene manager for large CAD hierarchies.
 *
 * This module manages a single SoCADAssembly Obol node per view.  The
 * assembly node renders all leaf-solid instances without the per-node
 * Open Inventor scene-graph overhead that limits scalability in the
 * traditional obol_scene_assemble() path.
 *
 * Architecture:
 *
 *   1. For each leaf bsg_shape (after draw_gather_paths / draw_scene):
 *      - A PartId is derived from the primitive's directory name.
 *      - An InstanceId is derived from the full traversal path using
 *        CadIdBuilder::extendNameOccBool() at each level.
 *      - ft_scene_obj_part() (new functab entry) fills an obol::PartGeometry
 *        with wire/shaded data.  For primitives without ft_scene_obj_part the
 *        shape falls back to the traditional per-node SoSeparator path.
 *
 *   2. obol_cad_assembly_upsert_shape() registers the part + instance in the
 *      SoCADAssembly.  Subsequent calls with the same InstanceId update
 *      transform and style without re-uploading geometry.
 *
 *   3. obol_scene_assemble_cad() drives the assembly update from a bsg_view,
 *      routing BoT and generic shapes to the assembly and leaving shapes
 *      without ft_scene_obj_part to the standard obol_scene_update_shape()
 *      path.
 *
 *   4. Picking: SoCADAssembly rayPick() returns an SoCADDetail whose
 *      InstanceId is looked up via obol_find_shape_for_instance_id() to
 *      recover the originating bsg_shape*.
 *
 * @see obol_scene.h for obol_scene_assemble_cad() declaration
 * @see RADICAL_MIGRATION.md Stage 3
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
#include <obol/cad/SoCADAssembly.h>
#include <obol/cad/SoCADDetail.h>
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif


/**
 * Set or clear GL 2.x compatibility mode.
 *
 * When enabled, obol_cad_assembly_upsert_shape() always returns false so that
 * all shapes are routed through the per-shape SoSeparator fallback instead of
 * SoCADAssembly.  This is necessary when the active GL context only supports
 * OpenGL 2.x (e.g. the bundled Mesa 6.5 OSMesa), because SoCADAssembly's
 * shader pipeline requires GL 3.3+.
 *
 * Call this from the GL context manager (e.g. CoinOSMesaContextManager) after
 * creating the context and detecting the GL version.
 */
GED_EXPORT void obol_cad_assembly_set_gl2_compat(bool enabled);

/**
 * Return true when GL 2.x compatibility mode is active.
 */
GED_EXPORT bool obol_cad_assembly_is_gl2_compat(void);

/**
 * Initialise the SoCADAssembly and SoCADDetail Obol node classes.
 *
 * Must be called once after SoDB::init() / SoInteraction::init(), before
 * creating any SoCADAssembly nodes.  Idempotent — safe to call multiple times.
 */
GED_EXPORT void obol_cad_assembly_init_classes(void);

/**
 * Allocate and return a new SoCADAssembly node with ref-count 1.
 *
 * The caller is responsible for calling node->unref() when done.
 */
GED_EXPORT SoCADAssembly *obol_cad_assembly_create(void);

/**
 * Register or update the part geometry and instance transform for @p s.
 *
 * - If ft_scene_obj_part is available for the primitive type, the geometry is
 *   uploaded to @p cad_asm.
 * - Otherwise the function returns false, telling the caller to fall back to
 *   the traditional per-shape SoSeparator path.
 *
 * @return true  if the shape was successfully registered in the assembly.
 * @return false if the shape must use the per-shape SoSeparator fallback.
 */
GED_EXPORT bool obol_cad_assembly_upsert_shape(SoCADAssembly *cad_asm,
					       bsg_shape *s);

/**
 * Remove a previously-registered instance for @p s from the assembly.
 *
 * No-op if the shape was never registered.
 */
GED_EXPORT void obol_cad_assembly_remove_shape(SoCADAssembly *cad_asm,
					       bsg_shape *s);

/**
 * Remove all parts and instances from @p cad_asm, and clear the reverse
 * InstanceId → bsg_shape* lookup table.
 */
GED_EXPORT void obol_cad_assembly_clear(SoCADAssembly *cad_asm);

/**
 * Look up the bsg_shape* that corresponds to a pick result InstanceId.
 *
 * @param iid  InstanceId from SoCADDetail::getInstanceId().
 * @return     The originating bsg_shape*, or nullptr if not found.
 */
GED_EXPORT bsg_shape *obol_find_shape_for_instance_id(obol::InstanceId iid);

/**
 * Set or clear the selection highlight for a shape registered in the assembly.
 *
 * Calls SoCADAssembly::setSelectedInstances() with the current selection set.
 */
GED_EXPORT void obol_cad_assembly_set_selected(SoCADAssembly *cad_asm,
					       bsg_shape *s,
					       bool selected);

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
