/*                      D E F I N E S . H
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @addtogroup libbsg
 *
 * @brief
 * Scene-graph library core type definitions.
 *
 * @par BSG is the intended scene graph API for BRL-CAD.
 *
 * libbsg (the BRL-CAD Scene Graph library) is the *intended public API* for
 * working with drawn scene objects.  Legacy scene-object storage remains a
 * *transitional* backing implementation; new code must treat @c bsg_node as
 * the only public scene-graph node type and avoid depending on raw storage
 * details.
 *
 * @par Correct usage pattern for new code:
 * @code
 *   bsg_node *n = bsg_node_child(parent, i);     // traversal
 *   if (bsg_node_has_kind(n, BSG_NODE_SHAPE)) {
 *       bsg_node_set_visible(n, 1);               // mutation
 *       bsg_node_bounds_get(n, bmin, bmax);       // query
 *   }
 * @endcode
 *
 * @par Do NOT write new code that reaches into legacy raw storage:
 * @code
 *   bsg_node *n = ...;
 *   raw_storage->s_flag = UP;      // wrong: use bsg_node_set_visible()
 *   raw_storage->s_type_flags |= ...; // wrong: use bsg_node_set_kind()
 *   VMOVE(raw_storage->s_color, c); // wrong: use material/appearance APIs
 * @endcode
 *
 * @par Header layout (current and planned):
 * - bsg/node.h         generic node API, kind, name, parent/child, visibility,
 *                      transforms, bounds, revisions, user data
 * - bsg/identity.h     node, part, and instance identity APIs
 * - bsg/field.h        SoField-like field-change notification API
 * - bsg/sensor.h       Inventor-like FieldSensor/NodeSensor registry
 * - bsg/payload.h      payload base API and payload lifecycle
 * - bsg/node_group.h   group/root typed node helpers
 * - bsg/node_shape.h   shape leaf typed node helpers
 * - bsg/node_transform.h  transform node helpers
 * - bsg/lod_ops.h      LoD policy and adaptive-wireframe API
 * - bsg/view_scope.h   per-view visibility scoping
 * - bsg/visit.h        traversal / visitor API
 * - bsg/action.h       action/traversal interfaces (bbox/search/collect)
 * - bsg/draw_set.h     erase-by-name and draw-set management
 * - bsg/scene_set.h    scene-level object table
 *
 * Planned (Phase 3+):
 * - bsg/material.h     material and color-binding API
 * - bsg/appearance.h   draw style, line style, depth/hidden-line policy
 * - bsg/selection.h    scene selection sets and sub-primitive entries
 * - bsg/light.h        light node definitions (Phase 7 - DONE)
 * - bsg/camera.h       renderer-neutral camera/view description (Phase 7 - DONE)
 * - bsg/render.h       renderer-facing callbacks and scene render contract
 * - bsg/compat.h       temporary compatibility helpers for legacy scene-object
 *                      handles during the migration period
 *
 * See doc/notes/bsg_enhancement_plan.txt for the full migration roadmap.
 * See doc/notes/bsg_raw_field_inventory.txt for the current raw-field
 * usage inventory and migration status.
 *
 * bsg_node and bsg_shape are direct typedefs for struct bsg_node (defined in
 * bsg/node_type.h), which is the public scene-graph node type.
 */
/** @{ */
/* @file bsg/defines.h */

#ifndef BSG_DEFINES_H
#define BSG_DEFINES_H

#include "common.h"
#include "bu/defines.h"
#include "bsg/node_type.h"

__BEGIN_DECLS

/* Export macro */
#ifndef BSG_EXPORT
#  if defined(BSG_DLL_EXPORTS) && defined(BSG_DLL_IMPORTS)
#    error "Only BSG_DLL_EXPORTS or BSG_DLL_IMPORTS can be defined, not both."
#  elif defined(BSG_DLL_EXPORTS)
#    define BSG_EXPORT COMPILER_DLLEXPORT
#  elif defined(BSG_DLL_IMPORTS)
#    define BSG_EXPORT COMPILER_DLLIMPORT
#  else
#    define BSG_EXPORT
#  endif
#endif

/**
 * Node type flags used in s_type_flags alongside BV_* flags.
 * Values are chosen to not overlap with the BV_* flags in bv/defines.h.
 */
#define BSG_NODE_ROOT         0x10000000ULL  /**< @brief synthetic scene root */
#define BSG_NODE_GROUP        0x20000000ULL  /**< @brief group / combinator */
#define BSG_NODE_SHAPE        0x40000000ULL  /**< @brief leaf drawable shape */
#define BSG_NODE_LOD          0x80000000ULL  /**< @brief LoD proxy node */
#define BSG_NODE_VLIST       0x100000000ULL  /**< @brief raw vlist node */
#define BSG_NODE_TRANSFORM   0x200000000ULL  /**< @brief transform node */
#define BSG_NODE_SENSOR      0x400000000ULL  /**< @brief sensor / trigger node */
/**
 * Phase V1 (drawing_stack_modernization):
 * A view-scope container node.  Children are skipped during traversal of any
 * view whose pointer does not match the node's s_v slot.  When s_v is NULL
 * the scope is "shared" and its children are visible to all views.
 *
 * Bit allocation note:
 *   bits 28-34: BSG_NODE_* taxonomy flags (ROOT through SENSOR)
 *   bit  35:    BSG_NODE_VIEW_SCOPE (0x800000000)
 *   bit  39:    BSG_NODE_VIEW_REF (0x8000000000)
 */
#define BSG_NODE_VIEW_SCOPE  0x800000000ULL  /**< @brief view-scope container node */
/**
 * Phase V2 (drawing_stack_modernization):
 * DEPRECATED (Phase V4): The temporary BV_VIEW_OBJS bridge has been removed.
 * These defines are retained only for ABI compatibility; no new code should
 * reference BSG_NODE_VIEW_REF or BSG_NODE_VIEW_BRIDGE.
 */
#define BSG_NODE_VIEW_REF   0x8000000000ULL  /**< @brief DEPRECATED: view-object bridge proxy node */
/** @brief DEPRECATED: internal bridge container group (Phase V2 only) */
#define BSG_NODE_VIEW_BRIDGE 0x200000000000ULL

/**
 * Payload type flags — stored in s_type_flags alongside BSG_NODE_* bits.
 * These describe what kind of data payload a BSG_NODE_SHAPE carries.
 *
 * Bit allocation:
 *   bits 28-34: BSG_NODE_* taxonomy flags
 *   bit  35:    BSG_NODE_VIEW_SCOPE
 *   bits 36-38: BSG_SENSOR_* sub-type flags (defined in sensor.h)
 *   bit  39:    BSG_NODE_VIEW_REF
 *   bits 40-44: BSG_PAYLOAD_* payload type flags (vlist/csg/mesh/brep/overlay)
 *   bit  45:    BSG_NODE_VIEW_BRIDGE
 *   bit  46:    BSG_PAYLOAD_IMAGE
 */
#define BSG_PAYLOAD_VLIST   0x10000000000ULL  /**< @brief raw bv_vlist payload (bit 40) */
#define BSG_PAYLOAD_CSG     0x20000000000ULL  /**< @brief CSG wireframe payload (bit 41) */
#define BSG_PAYLOAD_MESH    0x40000000000ULL  /**< @brief BoT LoD mesh payload (bit 42) */
#define BSG_PAYLOAD_BREP    0x80000000000ULL  /**< @brief BRep payload (bit 43) */
#define BSG_PAYLOAD_OVERLAY 0x100000000000ULL /**< @brief HUD overlay element (bit 44) */
#define BSG_PAYLOAD_IMAGE   0x400000000000ULL /**< @brief framebuffer/image layer (bit 46) */
/**
 * Slice 6 (bv_scene_obj_migrate):
 * Text/label adornment payload — stores a string, origin, rotation,
 * scale, and optional anchor/arrow fields.  Replaces ad-hoc use of
 * @c bv_label stored in @c s_i_data.
 */
#define BSG_PAYLOAD_TEXT    0x800000000000ULL /**< @brief text/label adornment payload (bit 47) */
/**
 * Slice 6 (bv_scene_obj_migrate):
 * Axes overlay payload — stores an axes position, size, colors, and
 * per-axis display options.  Replaces ad-hoc use of @c bv_axes.
 */
#define BSG_PAYLOAD_AXES    0x1000000000000ULL /**< @brief axes overlay payload (bit 48) */
/**
 * Slice 7 (bv_scene_obj_migrate):
 * Polygon overlay payload — stores a view-space polygon (bg_polygon),
 * shape type, view plane, fill parameters, and edit cursor state.
 * Replaces ad-hoc use of @c bv_polygon stored in @c s_i_data.
 */
#define BSG_PAYLOAD_POLYGON 0x2000000000000ULL /**< @brief polygon overlay payload (bit 49) */

/** Mask covering all payload bits (expression form avoids stale literal masks
 * when payload flags are extended). */
#define BSG_PAYLOAD_MASK    (BSG_PAYLOAD_VLIST | BSG_PAYLOAD_CSG | BSG_PAYLOAD_MESH | BSG_PAYLOAD_BREP | BSG_PAYLOAD_OVERLAY | BSG_PAYLOAD_IMAGE | BSG_PAYLOAD_TEXT | BSG_PAYLOAD_AXES | BSG_PAYLOAD_POLYGON)

/**
 * bsg_node is the first-class BSG scene-graph node type.
 * It is a direct typedef for struct bsg_node (defined in bsg/node_type.h).
 */
typedef struct bsg_node bsg_node;

/**
 * bsg_shape is the same alias used for leaf drawable shapes.
 */
typedef struct bsg_node bsg_shape;

__END_DECLS

#endif /* BSG_DEFINES_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
