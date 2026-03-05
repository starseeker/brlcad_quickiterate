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

/* The BSG API lives inside libbv; reuse libbv's export macro. */
#ifndef BSG_EXPORT
#  ifdef BV_EXPORT
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
 * Phase 2: traversal state                                               *
 *                                                                         *
 * Analogous to the SoState object passed through every SoAction during   *
 * Open Inventor traversal.  It accumulates property-node contributions   *
 * (material settings, transform) as the scene graph is walked.           *
 * ====================================================================== */

/**
 * @brief State accumulated during a scene-graph traversal.
 *
 * Callers initialise one of these with @c bsg_traversal_state_init() and
 * pass it to @c bsg_traverse().  At each node the state reflects the
 * cumulative effect of all ancestor property/transform nodes.
 *
 * Nodes flagged with @c BSG_NODE_SEPARATOR cause the state to be saved on
 * descent and restored on ascent — exactly as @c SoSeparator does in
 * Open Inventor.
 */
struct bsg_traversal_state {
    /** @brief Accumulated model transform (product of all ancestor @c s_mat values). */
    mat_t        xform;
    /** @brief Accumulated material settings (last property node in traversal order wins). */
    bsg_material material;
    /** @brief Current traversal depth (root = 0). */
    int          depth;
};
/** @brief C convenience typedef. */
typedef struct bsg_traversal_state bsg_traversal_state;

/* ====================================================================== *
 * Obol / Open Inventor structural compatibility notes                    *
 *                                                                         *
 * The following issues must be considered when interfacing BRL-CAD's bsg *
 * with an Inventor-style API such as Obol.  None of these prevent the    *
 * integration, but each requires an explicit design decision.            *
 *                                                                         *
 * 1. Camera / view is NOT a scene-graph node.                            *
 *    In Open Inventor the camera (SoCamera) is a node inserted into the  *
 *    scene.  In BRL-CAD, bsg_view encapsulates the camera together with  *
 *    per-view rendering state and is kept entirely outside the shape      *
 *    hierarchy.  Obol integration should either:                         *
 *      a) Treat bsg_view as an external camera that is registered with   *
 *         the Obol root-scene node but not itself a child node; or       *
 *      b) Create a thin SoCamera wrapper whose field values are driven   *
 *         from bsg_view_get_camera() and written back via               *
 *         bsg_view_set_camera().                                         *
 *                                                                         *
 * 2. Multi-view shared object model.                                     *
 *    BRL-CAD allows identical scene objects to be active in multiple     *
 *    views simultaneously to reduce memory usage.  Open Inventor's DAG   *
 *    supports instancing (one node reachable via multiple paths), which  *
 *    is equivalent, but Obol may not expose this directly.  The per-view *
 *    shape overrides managed by bsg_shape_for_view() /                   *
 *    bsg_shape_get_view_obj() represent view-specific sub-graphs that    *
 *    would each need to be a separate Obol sub-scene per view.           *
 *                                                                         *
 * 3. Flat object tables vs. graph traversal.                             *
 *    The renderer currently iterates over bview_objs.db_objs and        *
 *    bview_objs.view_objs (flat bu_ptbl tables) rather than walking a    *
 *    scene tree.  Moving to Obol requires switching the render loop to   *
 *    use bsg_traverse() (or the equivalent Obol scene-walk) so that      *
 *    separator state-push/pop and accumulated transforms are honoured.   *
 *                                                                         *
 * 4. Stale-flag notification vs. Obol sensors.                          *
 *    BRL-CAD uses explicit stale flags (s_dlist_stale, bsg_shape_stale)  *
 *    to trigger re-renders; Obol uses an SoSensor system.  Integration  *
 *    should bridge the two: bsg_shape_stale() should ultimately schedule *
 *    an Obol one-shot sensor rather than set a raw flag.                 *
 *                                                                         *
 * 5. LoD is a payload, not a group node.                                 *
 *    bsg_lod attaches mesh-LoD data to a shape node (BSG_NODE_MESH_LOD); *
 *    it is not a group node that selects children by viewer distance as  *
 *    SoLOD does.  For full Obol compatibility the BSG_NODE_MESH_LOD flag *
 *    would need to trigger special Obol-side traversal logic that selects*
 *    the appropriate detail child rather than mapping directly to SoLOD. *
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
