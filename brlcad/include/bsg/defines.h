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
 * bsg_node and bsg_shape are layout-compatible aliases for struct bv_scene_obj
 * (identical field prefix), so no casts are needed when promoting existing
 * scene objects into the scene graph.
 */
/** @{ */
/* @file bsg/defines.h */

#ifndef BSG_DEFINES_H
#define BSG_DEFINES_H

#include "common.h"
#include "bu/defines.h"
#include "bv/defines.h"

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
 *   bits 40-44: BSG_PAYLOAD_* payload type flags (this block)
 *   bit  45:    BSG_NODE_VIEW_BRIDGE
 */
#define BSG_PAYLOAD_VLIST   0x10000000000ULL  /**< @brief raw bv_vlist payload (bit 40) */
#define BSG_PAYLOAD_CSG     0x20000000000ULL  /**< @brief CSG wireframe payload (bit 41) */
#define BSG_PAYLOAD_MESH    0x40000000000ULL  /**< @brief BoT LoD mesh payload (bit 42) */
#define BSG_PAYLOAD_BREP    0x80000000000ULL  /**< @brief BRep payload (bit 43) */
#define BSG_PAYLOAD_OVERLAY 0x100000000000ULL /**< @brief HUD overlay element (bit 44) */

/** Mask covering all payload bits */
#define BSG_PAYLOAD_MASK    0x1F0000000000ULL

/**
 * Object storage-type flags used by bsg_obj_create() / bsg_obj_get_unregistered().
 * These are aliases for the BV_ flags while the storage model is being moved
 * from libbv into libbsg.
 */
#define BSG_OBJ_DB      BV_DB_OBJS    /**< @brief database-backed scene object */
#define BSG_OBJ_VIEW    BV_VIEW_OBJS  /**< @brief view-only scene object */
#define BSG_OBJ_LOCAL   BV_LOCAL_OBJS /**< @brief local (per-view) scope */
#define BSG_OBJ_CHILD   BV_CHILD_OBJS /**< @brief child object (not in flat ptbl) */

/**
 * bsg_node is a layout-compatible alias for struct bv_scene_obj.
 * Using the same struct pointer avoids any ABI mismatch.  Callers can
 * freely cast between bsg_node * and struct bv_scene_obj * without risk.
 */
typedef struct bv_scene_obj bsg_node;

/**
 * bsg_shape is the same alias used for leaf drawable shapes.
 */
typedef struct bv_scene_obj bsg_shape;

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
