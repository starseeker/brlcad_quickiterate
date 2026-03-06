/*                  B S G / D E F I N E S . H
 * BRL-CAD
 *
 * Copyright (c) 1993-2025 United States Government as represented by
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
/** @addtogroup bsg_defines
 *
 * @brief
 * BRL-CAD Scene Graph (BSG) — Phase 1 type definitions.
 *
 * This header introduces the @c bsg_* type aliases designed to map more
 * directly onto the Open Inventor / Obol node hierarchy than the legacy
 * @c bv_* API does.
 *
 * ### Migration phases
 *
 * **Phase 1** (this work) — naming migration, zero behavioural change:
 *   - All @c bsg_* types are typedef aliases of the corresponding @c bv_*
 *     types.  This ensures binary compatibility during the transition.
 *   - All @c bsg_* functions are thin wrappers around their @c bv_*
 *     counterparts.
 *   - New code should use @c bsg_* names exclusively; the @c bv_* names
 *     remain in @c <bv/defines.h> as deprecated compatibility aliases.
 *   - @c bsg_camera is a genuinely new struct (extracted from @c bview
 *     for documentation purposes) but is not yet embedded in @c bsg_view.
 *
 * **Phase 2** — structural alignment with Open Inventor:
 *   - @c bsg_view will have @c bsg_camera embedded as a sub-struct.
 *   - Property inheritance (material, transform) will be modelled as
 *     proper scene-graph state propagation.
 *   - The @c bv_* types will become thin wrappers around @c bsg_* types,
 *     then deprecated and eventually removed.
 *
 * **Phase 3** — LoD caching improvements:
 *   - The @c brlcad_viewdbi LoD rework (per-view LoD sets managed by the
 *     application rather than libbv) will be integrated.
 *
 * ### Type mapping table
 *
 * | BSG type (new)              | Replaces (legacy)       | Obol/Inventor analog         |
 * |-----------------------------|-------------------------|------------------------------|
 * | bsg_material                | bv_obj_settings         | SoMaterial + SoDrawStyle     |
 * | bsg_shape                   | bv_scene_obj            | SoShape (leaf geometry node) |
 * | bsg_group                   | bv_scene_group          | SoSeparator (group node)     |
 * | bsg_lod                     | bv_mesh_lod             | SoLOD                        |
 * | bsg_mesh_lod_context        | bv_mesh_lod_context     | (LoD cache context)          |
 * | bsg_camera                  | (inline fields in bview)| SoCamera                     |
 * | bsg_view_settings           | bview_settings          | (render/snap settings)       |
 * | bsg_view_objects            | bview_objs              | SoSeparator children list    |
 * | bsg_knobs                   | bview_knobs             | (dial/knob input state)      |
 * | bsg_view                    | bview                   | SoCamera + view state        |
 * | bsg_scene                   | bview_set               | root SoSeparator + cameras   |
 */

#ifndef BSG_DEFINES_H
#define BSG_DEFINES_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bu/vls.h"

/** @{ */
/** @file bsg/defines.h */

/* The BSG API is implemented in libbsg.  Use BSG_DLL_EXPORTS when building
 * libbsg itself and BSG_DLL_IMPORTS when consuming it from other libraries.
 * If neither is defined we fall back to the old BV_EXPORT behaviour so that
 * in-tree builds that have not yet migrated to the new libbsg target still
 * link correctly. */
#ifndef BSG_EXPORT
#  ifdef BSG_DLL_EXPORTS
#    define BSG_EXPORT COMPILER_DLLEXPORT
#  elif defined(BSG_DLL_IMPORTS)
#    define BSG_EXPORT COMPILER_DLLIMPORT
#  elif defined(BV_EXPORT)
#    define BSG_EXPORT BV_EXPORT
#  elif defined(BV_DLL_EXPORTS)
#    define BSG_EXPORT COMPILER_DLLEXPORT
#  elif defined(BV_DLL_IMPORTS)
#    define BSG_EXPORT COMPILER_DLLIMPORT
#  else
#    define BSG_EXPORT
#  endif
#endif

/*
 * Pull in the full legacy bv API.  All bsg_* types in Phase 1 are
 * typedef aliases of the corresponding bv_* types defined here.
 */
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/lod.h"
#include "bv/view_sets.h"

__BEGIN_DECLS

/* ====================================================================== *
 * Phase 1: typedef aliases                                               *
 *                                                                         *
 * In Phase 1 the bsg_* names are thin aliases of the legacy bv_* types. *
 * This means no binary layout changes occur; all existing libbv code     *
 * continues to work without modification.                                 *
 * ====================================================================== */

/**
 * @brief Appearance / rendering properties for a scene node.
 *
 * Phase 1: alias of @c bv_obj_settings.
 * Phase 2: will gain proper Inventor-style property inheritance.
 *
 * Analogous to @c SoMaterial + @c SoDrawStyle in Open Inventor.
 */
typedef struct bv_obj_settings bsg_material;

/**
 * @brief Leaf geometry node in the BSG scene graph.
 *
 * Independent struct with the same initial field layout as @c bv_scene_obj.
 * Having two separate definitions allows the @c bsg_* API to evolve its
 * field layout independently without affecting external libbv users.
 * During Phase 1 the layouts are kept in sync; Phase 2 will diverge them
 * as the BSG API matures.
 *
 * Analogous to @c SoShape in Open Inventor.
 */
/* struct bsg_shape is defined in bv/defines.h, included above */

/**
 * @brief Group node — aggregates child @c bsg_shape nodes.
 *
 * Phase 1: typedef alias of @c bsg_shape (same in-memory layout).
 * Phase 2: gains proper state isolation (SoSeparator semantics) via
 * the @c BSG_NODE_SEPARATOR type-flag on the underlying @c bsg_shape.
 *
 * Analogous to @c SoSeparator in Open Inventor.
 */
typedef bsg_shape bsg_group;

/**
 * @brief Mesh Level-of-Detail payload exposed to renderers.
 *
 * Phase 1: alias of @c bv_mesh_lod.
 * Phase 2: will be integrated with the brlcad_viewdbi per-view LoD sets.
 *
 * Analogous to @c SoLOD in Open Inventor.
 */
typedef struct bv_mesh_lod bsg_lod;

/**
 * @brief Per-database LoD bookkeeping context.
 *
 * Phase 1: alias of @c bv_mesh_lod_context.
 */
typedef struct bv_mesh_lod_context bsg_mesh_lod_context;

/**
 * @brief Rendering and interaction settings shareable across views.
 *
 * Phase 1: alias of @c bview_settings.
 * Phase 2: individual settings will migrate into proper scene-graph nodes.
 */
typedef struct bview_settings bsg_view_settings;

/**
 * @brief Per-view scene object containers.
 *
 * Phase 1: alias of @c bview_objs.
 */
typedef struct bview_objs bsg_view_objects;

/**
 * @brief Interactive knob / dial-box view manipulation state.
 *
 * Phase 1: alias of @c bview_knobs.
 */
typedef struct bview_knobs bsg_knobs;

/**
 * @brief A single rendered view into the scene.
 *
 * Phase 1: alias of @c bview.
 * Phase 2: will embed @c bsg_camera as a sub-struct.
 *
 * Analogous to the combination of @c SoCamera + @c SbViewportRegion in
 * Open Inventor.
 */
typedef struct bview bsg_view;

/**
 * @brief Top-level multi-view scene container.
 *
 * Phase 1: alias of @c bview_set.
 * Phase 2: will become the scene-graph root analogous to @c SoSeparator.
 */
typedef struct bview_set bsg_scene;

/* ====================================================================== *
 * bsg_camera — new struct (Phase 2 design target)                        *
 *                                                                         *
 * In Open Inventor, SoCamera (and its concrete subclasses               *
 * SoPerspectiveCamera / SoOrthographicCamera) encapsulate the viewpoint, *
 * orientation, and projection.  This struct collects the camera-specific  *
 * fields that are currently embedded directly in @c bview into a single  *
 * dedicated type.                                                         *
 *                                                                         *
 * Phase 1 status: this struct is defined for documentation and design    *
 * purposes; it is NOT yet embedded in @c bsg_view (which is still @c     *
 * bview in Phase 1).  Phase 2 will replace the inline camera fields in   *
 * @c bview with this struct.                                              *
 * ====================================================================== */

/**
 * @brief Camera (viewpoint) data — Phase 2 design target.
 *
 * Extracts the camera-specific fields from @c bview into a dedicated type
 * analogous to @c SoCamera in Open Inventor.
 *
 * @note In Phase 1 this struct is not yet integrated into @c bsg_view.
 *       Use the legacy field names via the @c bsg_view (= @c bview) alias
 *       in the meantime.  When Phase 2 integrates this struct, migration
 *       will be mechanical:
 *
 *       Old (Phase 1):   v->gv_perspective
 *       New (Phase 2):   v->camera.perspective
 */
struct bsg_camera {
    fastf_t perspective;      /**< @brief perspective angle (0 → orthographic)    */
    vect_t  aet;              /**< @brief azimuth / elevation / twist             */
    vect_t  eye_pos;          /**< @brief eye position in model space              */
    vect_t  keypoint;         /**< @brief rotation keypoint                        */
    char    coord;            /**< @brief active coordinate system                 */
    char    rotate_about;     /**< @brief rotation pivot specifier                 */
    mat_t   rotation;         /**< @brief view rotation matrix                     */
    mat_t   center;           /**< @brief view centre matrix                       */
    mat_t   model2view;       /**< @brief model-to-view transform                  */
    mat_t   pmodel2view;      /**< @brief perspective model-to-view transform      */
    mat_t   view2model;       /**< @brief view-to-model transform                  */
    mat_t   pmat;             /**< @brief perspective matrix                       */
};

/* ====================================================================== *
 * Convenience macros: new constant names                                  *
 *                                                                         *
 * These duplicate the BV_* macros from bv/defines.h under BSG_* names   *
 * so that new code does not need to reference the legacy header.         *
 * ====================================================================== */

/* Node type flags */
#define BSG_NODE_DBOBJ_BASED  BV_DBOBJ_BASED
#define BSG_NODE_VIEWONLY     BV_VIEWONLY
#define BSG_NODE_LINES        BV_LINES
#define BSG_NODE_LABELS       BV_LABELS
#define BSG_NODE_AXES         BV_AXES
#define BSG_NODE_POLYGONS     BV_POLYGONS
#define BSG_NODE_MESH_LOD     BV_MESH_LOD
#define BSG_NODE_CSG_LOD      BV_CSG_LOD

/**
 * @brief Phase 2 node-type flag: Separator group (SoSeparator semantics).
 *
 * When set on a @c bsg_shape, the node acts as a scope boundary during
 * scene-graph traversal: the traversal state (accumulated transform and
 * material) is saved on entry and restored on exit.  Children of the node
 * may freely modify state without affecting siblings or ancestors.
 *
 * Analogous to @c SoSeparator in Open Inventor.
 */
#define BSG_NODE_SEPARATOR    0x100

/**
 * @brief Phase 2 node-type flag: Transform node (SoTransform semantics).
 *
 * When set on a @c bsg_shape, the node's primary contribution to the scene
 * is its @c s_mat transform matrix; it carries no renderable geometry of its
 * own.  During traversal the matrix is accumulated into the traversal state.
 *
 * Analogous to @c SoTransform in Open Inventor.
 */
#define BSG_NODE_TRANSFORM    0x200

/**
 * @brief Phase 2 node-type flag: Camera node (SoCamera semantics).
 *
 * When set on a @c bsg_shape, the node acts as a camera definition inside
 * the scene graph.  Its @c s_i_data points to a heap-allocated
 * @c bsg_camera struct (freed via @c s_free_callback).
 *
 * During scene-graph traversal (@c bsg_traverse / @c bsg_view_traverse)
 * the traversal engine copies the camera pointer into
 * @c bsg_traversal_state::active_camera so that descendant render callbacks
 * can read the active projection.
 *
 * The camera node integrates naturally with @c bsg_view:
 *   - @c bsg_camera_node_set() syncs a @c bsg_camera into the node.
 *   - @c bsg_camera_node_get() reads it back.
 *   - @c bsg_view_set_camera() writes a @c bsg_camera back into a
 *     @c bsg_view (the legacy view struct) so that bv_update and existing
 *     render code see the updated camera.
 *
 * Analogous to @c SoPerspectiveCamera / @c SoOrthographicCamera in Open
 * Inventor.
 */
#define BSG_NODE_CAMERA       0x400

/**
 * @brief Phase 2 node-type flag: LoD group node (SoLOD semantics).
 *
 * When set on a @c bsg_shape, the node acts as a level-of-detail selector
 * that picks exactly one of its children for rendering based on the
 * eye-to-center distance.  Its @c s_i_data points to a heap-allocated
 * @c bsg_lod_switch_data struct (freed via @c s_free_callback).
 *
 * Children must be added in highest-to-lowest detail order (child[0] is
 * the highest-detail representation, the last child is the coarsest).
 * The switch distances array has (num_levels - 1) entries: the n-th entry
 * is the distance at which the renderer transitions from child[n] to
 * child[n+1].
 *
 * During traversal @c bsg_traverse computes the eye-to-node-center distance
 * and visits only the selected child.  The user-facing LoD feature is fully
 * preserved; existing @c bsg_lod (BSG_NODE_MESH_LOD) nodes remain supported
 * on leaf shapes.
 *
 * Analogous to @c SoLOD in Open Inventor.
 */
#define BSG_NODE_LOD_GROUP    0x800

/* Container-selection flags */
#define BSG_DB_OBJS    BV_DB_OBJS
#define BSG_VIEW_OBJS  BV_VIEW_OBJS
#define BSG_LOCAL_OBJS BV_LOCAL_OBJS
#define BSG_CHILD_OBJS BV_CHILD_OBJS

/* Anchor position constants */
#define BSG_ANCHOR_AUTO          BV_ANCHOR_AUTO
#define BSG_ANCHOR_BOTTOM_LEFT   BV_ANCHOR_BOTTOM_LEFT
#define BSG_ANCHOR_BOTTOM_CENTER BV_ANCHOR_BOTTOM_CENTER
#define BSG_ANCHOR_BOTTOM_RIGHT  BV_ANCHOR_BOTTOM_RIGHT
#define BSG_ANCHOR_MIDDLE_LEFT   BV_ANCHOR_MIDDLE_LEFT
#define BSG_ANCHOR_MIDDLE_CENTER BV_ANCHOR_MIDDLE_CENTER
#define BSG_ANCHOR_MIDDLE_RIGHT  BV_ANCHOR_MIDDLE_RIGHT
#define BSG_ANCHOR_TOP_LEFT      BV_ANCHOR_TOP_LEFT
#define BSG_ANCHOR_TOP_CENTER    BV_ANCHOR_TOP_CENTER
#define BSG_ANCHOR_TOP_RIGHT     BV_ANCHOR_TOP_RIGHT

/* Material initialiser */
#define BSG_MATERIAL_INIT  BV_OBJ_SETTINGS_INIT

/* Interaction mode flags */
#define BSG_IDLE      BV_IDLE
#define BSG_ROT       BV_ROT
#define BSG_TRANS     BV_TRANS
#define BSG_SCALE     BV_SCALE
#define BSG_CENTER    BV_CENTER
#define BSG_CON_X     BV_CON_X
#define BSG_CON_Y     BV_CON_Y
#define BSG_CON_Z     BV_CON_Z
#define BSG_CON_GRID  BV_CON_GRID
#define BSG_CON_LINES BV_CON_LINES

/* Knob reset categories */
#define BSG_KNOBS_ALL  BV_KNOBS_ALL
#define BSG_KNOBS_RATE BV_KNOBS_RATE
#define BSG_KNOBS_ABS  BV_KNOBS_ABS

/* Autoview scale sentinel */
#define BSG_AUTOVIEW_SCALE_DEFAULT BV_AUTOVIEW_SCALE_DEFAULT

/* View range constants */
#define BSG_VIEW_MAX    BV_MAX
#define BSG_VIEW_MIN    BV_MIN
#define BSG_VIEW_RANGE  BV_RANGE
#define BSG_MINVIEWSIZE   BV_MINVIEWSIZE
#define BSG_MINVIEWSCALE  BV_MINVIEWSCALE

/* ====================================================================== *
 * Phase 2: LoD group switch data                                         *
 * ====================================================================== */

/**
 * @brief Switch-distance table for a @c BSG_NODE_LOD_GROUP node.
 *
 * Stored in @c s_i_data of every LoD group node.  The @c s_free_callback
 * registered by @c bsg_lod_group_alloc() frees this struct and the heap
 * memory it owns.
 *
 * @c num_levels == number of child detail levels (== number of children
 * added to the node).  @c switch_distances has @c (num_levels - 1) entries.
 * Entry @c n is the eye-to-center distance at which the traversal engine
 * switches from child @c n to child @c n+1 (coarser).
 */
struct bsg_lod_switch_data {
    int      num_levels;         /**< @brief total number of detail levels */
    fastf_t *switch_distances;   /**< @brief (num_levels-1) distance thresholds */
};

/* ====================================================================== *
 * Phase 2: sensor / change notification                                  *
 *                                                                         *
 * Analogous to SoDataSensor / SoOneShotSensor in Open Inventor.         *
 * When bsg_shape_stale() is called the engine sets the node's stale flag  *
 * (backward compat) AND fires every sensor registered on that node.      *
 * ====================================================================== */

/**
 * @brief A lightweight change-notification callback attached to a shape.
 *
 * Register with @c bsg_shape_add_sensor(); remove with
 * @c bsg_shape_rm_sensor() using the handle returned by the add call.
 *
 * Analogous to @c SoDataSensor in Open Inventor: the callback fires
 * whenever @c bsg_shape_stale() is invoked on the associated node.
 */
struct bsg_sensor {
    /** @brief Called when the node becomes stale.  May be NULL (no-op). */
    void (*callback)(bsg_shape *s, void *data);
    /** @brief Opaque data forwarded to @c callback. */
    void  *data;
};
/** @brief C convenience typedef. */
typedef struct bsg_sensor bsg_sensor;

/* ====================================================================== *
 * Phase 2: traversal state                                               *
 *                                                                         *
 * Analogous to the SoState object passed through every SoAction during   *
 * Open Inventor traversal.  It accumulates property-node contributions   *
 * (material settings, transform, camera) as the scene graph is walked.  *
 * ====================================================================== */

/**
 * @brief State accumulated during a scene-graph traversal.
 *
 * Callers initialise one of these with @c bsg_traversal_state_init() and
 * pass it to @c bsg_traverse() / @c bsg_view_traverse().  At each node
 * the state reflects the cumulative effect of all ancestor property /
 * transform / camera nodes.
 *
 * State accumulation rules:
 *   - Transform:  @c xform = parent_xform * node->s_mat at every node.
 *   - Material:   updated at every node whose @c s_os != NULL and
 *                 @c s_inherit_settings == 0.
 *   - Camera:     @c active_camera is set to @c node->s_i_data whenever a
 *                 @c BSG_NODE_CAMERA node is encountered.
 *   - Separator:  @c BSG_NODE_SEPARATOR saves the full state on descent and
 *                 restores it on ascent (SoSeparator semantics).
 *
 * The @c view pointer is set by @c bsg_view_traverse(); direct callers of
 * @c bsg_traverse() may leave it NULL if LoD distance calculation is not
 * needed.
 */
struct bsg_traversal_state {
    /** @brief Accumulated model transform (product of all ancestor @c s_mat values). */
    mat_t                   xform;
    /** @brief Accumulated material settings (last property node in traversal order wins). */
    bsg_material            material;
    /** @brief Current traversal depth (root = 0). */
    int                     depth;
    /** @brief Camera set by the last @c BSG_NODE_CAMERA node encountered (NULL = none yet). */
    const struct bsg_camera *active_camera;
    /**
     * @brief View being traversed, for LoD distance calculation.
     *
     * Set automatically by @c bsg_view_traverse().  If NULL, distance-based
     * LoD group child selection falls back to always picking child[0]
     * (highest detail).
     */
    const bsg_view          *view;
};
/** @brief C convenience typedef. */
typedef struct bsg_traversal_state bsg_traversal_state;

/* ====================================================================== *
 * Design decisions for Obol / Open Inventor compatibility                *
 *                                                                         *
 * The following notes document how each structural difference between    *
 * BRL-CAD's scene model and Inventor/Obol has been resolved.            *
 *                                                                         *
 * 1. Camera IS now a scene-graph node (BSG_NODE_CAMERA).                *
 *    bsg_camera_node_alloc() creates a standalone bsg_shape with the     *
 *    BSG_NODE_CAMERA flag.  It is inserted into the scene root ahead of  *
 *    geometry nodes, exactly as SoCamera is in Open Inventor.            *
 *    bsg_traversal_state::active_camera is updated each time a camera    *
 *    node is encountered during traversal.                               *
 *    bsg_view_set_camera() writes a bsg_camera back into a bsg_view so   *
 *    that legacy bv_update() / render code continues to work during the  *
 *    migration.                                                           *
 *                                                                         *
 * 2. Multi-view instancing via per-view scene roots.                     *
 *    Each view now has an optional scene root (bsg_scene_root_set/get).  *
 *    To share geometry between views:                                    *
 *      a) Create the geometry node once.                                 *
 *      b) Add it as a child of multiple view roots.                      *
 *    This is identical to Open Inventor's DAG instancing: the same node  *
 *    pointer appears in multiple parent children tables.  The legacy     *
 *    bsg_shape_for_view() / bsg_shape_get_view_obj() per-view override   *
 *    mechanism is deprecated.  Per-view LoD variation is handled by      *
 *    BSG_NODE_LOD_GROUP nodes (see 5 below).                             *
 *                                                                         *
 * 3. Graph traversal replaces flat-table iteration.                     *
 *    bsg_view_traverse() walks from the per-view scene root through      *
 *    bsg_traverse(), honouring separator state, transforms, camera and   *
 *    LoD selection.  Renderers should migrate from iterating              *
 *    bview_objs.db_objs / view_objs to calling bsg_view_traverse().      *
 *    The legacy flat tables remain populated for existing callers.       *
 *                                                                         *
 * 4. Sensors replace raw stale flags.                                   *
 *    bsg_shape_add_sensor() / bsg_shape_rm_sensor() register change-     *
 *    notification callbacks on any bsg_shape.  bsg_shape_stale() fires   *
 *    all registered sensors in addition to setting the legacy            *
 *    s_dlist_stale flag.  Callers that previously polled the stale flag  *
 *    should migrate to sensors; the stale flag is kept for compatibility.*
 *                                                                         *
 * 5. LoD IS now a group node (BSG_NODE_LOD_GROUP).                      *
 *    bsg_lod_group_alloc() creates a standalone group node whose        *
 *    children are detail-level shapes ordered highest-to-lowest.         *
 *    The traversal engine picks the appropriate child from the           *
 *    bsg_lod_switch_data distances table.  The existing BSG_NODE_MESH_LOD*
 *    payload mechanism on leaf shapes is preserved for cases that need   *
 *    only a single adaptive mesh (not multiple pre-baked LODs).          *
 * ====================================================================== */

__END_DECLS

/** @} */

#endif /* BSG_DEFINES_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
