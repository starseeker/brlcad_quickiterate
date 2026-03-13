/*                  B S G / O B O L _ N O D E . H
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
/** @file bsg/obol_node.h
 *
 * C++ helpers for attaching Obol SoNode scene objects to bsg_shape.
 *
 * This header is intentionally C++-only: it includes Obol headers and is
 * compiled only when BRLCAD_ENABLE_OBOL is set.  Pure-C translation units
 * that merely pass bsg_shape pointers around do not need to include it.
 *
 * Typical usage in an ft_scene_obj callback (C++):
 * @code
 *   #include "bsg/obol_node.h"
 *   #include <Inventor/nodes/SoSeparator.h>
 *   #include <Inventor/nodes/SoBaseColor.h>
 *   #include <Inventor/nodes/SoIndexedLineSet.h>
 *
 *   int rt_mytype_scene_obj(bsg_shape *s, ...) {
 *       SoSeparator *root = new SoSeparator;
 *       // ... populate root with material, geometry children ...
 *       bsg_shape_set_obol_node(s, root);
 *       return BRLCAD_OK;
 *   }
 * @endcode
 *
 * The scene assembler in libged calls bsg_shape_get_obol_node() to retrieve
 * the node and insert it (with a separate SoTransform for s->s_mat) into the
 * live Obol scene graph.
 *
 * @see RADICAL_MIGRATION.md Stage 0 and Stage 1
 */

#ifndef BSG_OBOL_NODE_H
#define BSG_OBOL_NODE_H

#ifdef __cplusplus

#include "bsg/defines.h"
#include <Inventor/nodes/SoNode.h>

/**
 * Attach an Obol SoNode to a bsg_shape, managing reference counts.
 *
 * If @p node is non-NULL, ref() is called on it.  If @p s already holds a
 * previous node that node is unref()-ed first.  Passing NULL clears the
 * current node (triggering unref on the old one if present).
 *
 * This function is safe to call from ft_scene_obj callbacks running on the
 * main thread.  Do NOT call it from worker threads — Obol node mutation must
 * occur on the GL/render thread.
 */
inline void
bsg_shape_set_obol_node(bsg_shape *s, SoNode *node)
{
    if (!s)
	return;
    SoNode *old = static_cast<SoNode *>(s->s_obol_node);
    if (old)
	old->unref();
    if (node)
	node->ref();
    s->s_obol_node = static_cast<void *>(node);
}

/**
 * Retrieve the Obol SoNode attached to a bsg_shape.
 *
 * Returns NULL if no node has been set (ft_scene_obj has not yet run, or the
 * shape uses the vlist fallback path).
 *
 * The caller does NOT receive a new reference — do not call unref() on the
 * returned pointer unless you explicitly added a reference via ref().
 */
inline SoNode *
bsg_shape_get_obol_node(const bsg_shape *s)
{
    if (!s)
	return nullptr;
    return static_cast<SoNode *>(s->s_obol_node);
}

/**
 * Release the Obol node currently attached to @p s and clear the field.
 *
 * Equivalent to bsg_shape_set_obol_node(s, nullptr).  Typically called from
 * a shape's s_free_callback.
 */
inline void
bsg_shape_free_obol_node(bsg_shape *s)
{
    bsg_shape_set_obol_node(s, nullptr);
}

#endif /* __cplusplus */

#endif /* BSG_OBOL_NODE_H */

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
