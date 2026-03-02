/*                        B V I E W . H
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
/** @addtogroup bv_defines
 *
 * This header is intended to be independent of any one BRL-CAD library and is
 * specifically intended to allow the easy definition of common display list
 * types between otherwise independent libraries (libdm and libged, for
 * example).
 */

#ifndef BV_DEFINES_H
#define BV_DEFINES_H

#include "common.h"
#include "vmath.h"
#include "bu/color.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bu/vls.h"

/** @{ */
/** @file bv.h */

#ifndef BV_EXPORT
#  if defined(BV_DLL_EXPORTS) && defined(BV_DLL_IMPORTS)
#    error "Only BV_DLL_EXPORTS or BV_DLL_IMPORTS can be defined, not both."
#  elif defined(BV_DLL_EXPORTS)
#    define BV_EXPORT COMPILER_DLLEXPORT
#  elif defined(BV_DLL_IMPORTS)
#    define BV_EXPORT COMPILER_DLLIMPORT
#  else
#    define BV_EXPORT
#  endif
#endif

#include "bg/polygon_types.h"
#include "bv/tcl_data.h"
#include "bv/faceplate.h"

__BEGIN_DECLS

/* Define view ranges.  The numbers -2048 and 2047 go all the way back to the
 * original angle-distance cursor code that predates even BRL-CAD itself, but
 * (at least right now) there doesn't seem to be any documentation of why those
 * specific values were chosen. */
#define BV_MAX 2047.0
#define BV_MIN -2048.0
#define BV_RANGE 4095.0
/* Map +/-2048 BV space into -1.0..+1.0 :: x/2048*/
#define INV_BV 0.00048828125
#define INV_4096 0.000244140625


#define BV_MINVIEWSIZE 0.0001
#define BV_MINVIEWSCALE 0.00005

#ifndef UP
#  define UP 0
#endif
#ifndef DOWN
#  define DOWN 1
#endif

// Forward declare struct bview
struct bview;

/*******************************************************************************
 *              EXPERIMENTAL EXPERIMENTAL EXPERIMENTALa
 *
 * Testing a new style of bview API.
 *
 * The exiting code is a result of organic refactoring of older and even more
 * adhoc code, but has a few undesirable properties - large among them being
 * direct access to the struct members is the norm, so we don't have a good way
 * to ensure certain actions are triggered when the view changes.
 *
 * Since changing that is a heavy lift, we'll try to see if we can do it
 * incrementally.
 *
 * UNTIL THIS LABEL IS REMOVED, EVERYTHING IN THIS CODE BLOCK IS TO BE
 * CONSIDERED EXPERIMENTAL.  It is located in this file in the hopes that a
 * gradual reduction of the old code in favor of the new can be achieved, but
 * this CANNOT be considered settled API until a great deal of work has
 * been completed.
 *
 *              EXPERIMENTAL EXPERIMENTAL EXPERIMENTALa
 *******************************************************************************/

/* Opaque rendering/view context (conceptually similar to Coin3D's SoRenderArea) */
struct bview_new;

/* Opaque scene graph root (conceptually similar to Coin3D's SoSeparator) */
struct bv_scene;

/* Forward declaration for migration helpers that accept legacy types */
struct bv_scene_obj;
struct bview_set;

/*
 * Scene graph node type enumeration, aligned with Inventor/Coin3D node types.
 *
 * Mapping to Inventor types:
 *   BV_NODE_GROUP      -> SoGroup     (ordered children, propagates traversal state)
 *   BV_NODE_SEPARATOR  -> SoSeparator (group that saves/restores traversal state,
 *                                      providing a scoping boundary like a push/pop)
 *   BV_NODE_GEOMETRY   -> SoShape     (leaf geometry node)
 *   BV_NODE_TRANSFORM  -> SoTransform (matrix/translation/rotation/scale node)
 *   BV_NODE_CAMERA     -> SoCamera    (orthographic or perspective camera)
 *   BV_NODE_LIGHT      -> SoLight     (directional/point/spot light)
 *   BV_NODE_MATERIAL   -> SoMaterial  (surface appearance properties)
 *   BV_NODE_OTHER      -> (extension point for application-specific node types)
 */
enum bv_node_type {
    BV_NODE_GROUP,
    BV_NODE_SEPARATOR,
    BV_NODE_GEOMETRY,
    BV_NODE_TRANSFORM,
    BV_NODE_CAMERA,
    BV_NODE_LIGHT,
    BV_NODE_MATERIAL,
    BV_NODE_OTHER
};

/* Scene graph node (analogous to Coin3D's SoNode - the base unit of the scene graph) */
struct bv_node;

/* Camera object (view's active camera) */
struct bview_camera {
    vect_t position;
    vect_t target;
    vect_t up;
    double fov;
    int perspective; /* 0 = ortho, 1 = perspective */
};

/* Viewport/window object */
struct bview_viewport {
    int width;
    int height;
    double dpi;
};

/* Appearance/material object (background, grid, axes, Coin3D appearance mapping) */
struct bview_material {
    struct bu_color diffuse_color;
    struct bu_color specular_color;
    struct bu_color emissive_color;
    float   transparency;
    float   shininess;
    /* Extensible: add Coin3D-style fields as needed */
};

struct bview_appearance {
    struct bu_color bg_color;
    struct bu_color grid_color;
    struct bu_color axes_color;
    float   line_width;
    int     show_axes;
    int     show_grid;
    int     show_origin;
    /* Extensible: add more fields as needed */
};

/* Overlay/HUD object */
struct bview_overlay {
    int    show_fps;
    int    show_gizmos;
    char   annotation_text[256];
    int    show_annotation;
    /* Extensible: add more overlay fields */
};

/* Pick set (Coin3D "pick set" analog) */
struct bview_pick_set {
    struct bu_ptbl selected_objs; /* scene object pointers */
    /* Extensible: add selection modes, groups, etc. */
};

/* --- bv_scene API --- */

/* Lifecycle */
BV_EXPORT struct bv_scene *bv_scene_create(void);
BV_EXPORT void bv_scene_destroy(struct bv_scene *scene);

/* Access scene root node (SoSeparator analogy: saves/restores state, scopes children) */
BV_EXPORT struct bv_node *bv_scene_root(const struct bv_scene *scene);

/* Scene node management (analogous to SoGroup::addChild / removeChild) */
BV_EXPORT void bv_scene_add_node(struct bv_scene *scene, struct bv_node *node);
BV_EXPORT void bv_scene_remove_node(struct bv_scene *scene, struct bv_node *node);
BV_EXPORT const struct bu_ptbl *bv_scene_nodes(const struct bv_scene *scene);

/* Hierarchy/grouping (child management under a specific parent node) */
BV_EXPORT void bv_scene_add_child(struct bv_scene *scene, struct bv_node *parent, struct bv_node *child);
BV_EXPORT void bv_scene_remove_child(struct bv_scene *scene, struct bv_node *parent, struct bv_node *child);

/* Scene traversal (for rendering, picking, export, etc. -- analogous to SoAction traversal) */
typedef void (*bv_scene_traverse_cb)(struct bv_node *, void *);
BV_EXPORT void bv_scene_traverse(const struct bv_scene *scene, bv_scene_traverse_cb cb, void *user_data);
BV_EXPORT void bv_node_traverse(const struct bv_node *node, bv_scene_traverse_cb cb, void *user_data);

/* Lookup scene node by name (analogous to SoNode::getByName) */
BV_EXPORT struct bv_node *bv_scene_find_node(const struct bv_scene *scene, const char *name);

/* Access default camera node for scene (analogous to SoSceneManager::getCamera) */
BV_EXPORT struct bv_node *bv_scene_default_camera(const struct bv_scene *scene);

/* Set the default camera node for scene */
BV_EXPORT void bv_scene_default_camera_set(struct bv_scene *scene, struct bv_node *camera);

/* --- bview_new API --- */

/* Lifecycle */
BV_EXPORT struct bview_new *bview_create(const char *name);
BV_EXPORT void bview_destroy(struct bview_new *view);

/* Associate a scene with a view */
BV_EXPORT void bview_scene_set(struct bview_new *view, struct bv_scene *scene);
BV_EXPORT struct bv_scene *bview_scene_get(const struct bview_new *view);

/* Camera (active camera parameters) */
BV_EXPORT void bview_camera_set(struct bview_new *view, const struct bview_camera *camera);
BV_EXPORT const struct bview_camera *bview_camera_get(const struct bview_new *view);

/* Optionally associate a camera node (preparing for Coin3D mapping via SoCamera) */
BV_EXPORT void bview_camera_node_set(struct bview_new *view, struct bv_node *camera_node);
BV_EXPORT struct bv_node *bview_camera_node_get(const struct bview_new *view);

/* Viewport */
BV_EXPORT void bview_viewport_set(struct bview_new *view, const struct bview_viewport *viewport);
BV_EXPORT const struct bview_viewport *bview_viewport_get(const struct bview_new *view);

/* Appearance/material */
BV_EXPORT void bview_material_set(struct bview_new *view, const struct bview_material *material);
BV_EXPORT const struct bview_material *bview_material_get(const struct bview_new *view);
BV_EXPORT void bview_appearance_set(struct bview_new *view, const struct bview_appearance *appearance);
BV_EXPORT const struct bview_appearance *bview_appearance_get(const struct bview_new *view);

/* Overlay/HUD */
BV_EXPORT void bview_overlay_set(struct bview_new *view, const struct bview_overlay *overlay);
BV_EXPORT const struct bview_overlay *bview_overlay_get(const struct bview_new *view);

/* Pick set (Coin3D pick set analog) */
BV_EXPORT void bview_pick_set_set(struct bview_new *view, const struct bview_pick_set *pick_set);
BV_EXPORT const struct bview_pick_set *bview_pick_set_get(const struct bview_new *view);

/* Redraw callback (for integration with UI/event loop) */
typedef void (*bview_redraw_cb)(struct bview_new *, void *);
BV_EXPORT void bview_redraw_callback_set(struct bview_new *view, bview_redraw_cb cb, void *data);

/* --- Scene Node API --- */

/*
 * Scene graph nodes (bv_node) are the fundamental building blocks of the
 * BRL-CAD scene graph, directly analogous to Coin3D/Inventor's SoNode.
 *
 * A bv_node can represent:
 *   - A group of children (BV_NODE_GROUP / BV_NODE_SEPARATOR)
 *   - Geometry (BV_NODE_GEOMETRY -- analogous to SoShape)
 *   - A transform (BV_NODE_TRANSFORM -- analogous to SoTransform)
 *   - A camera (BV_NODE_CAMERA -- analogous to SoCamera)
 *   - A light (BV_NODE_LIGHT -- analogous to SoLight)
 *   - Material/appearance (BV_NODE_MATERIAL -- analogous to SoMaterial)
 *
 * Nodes are named (analogous to SoNode::setName/getName), typed (via
 * bv_node_type), and may carry arbitrary user data for application-layer
 * or future Coin3D mapping.
 */
BV_EXPORT struct bv_node *bv_node_create(const char *name, enum bv_node_type type);
BV_EXPORT void bv_node_destroy(struct bv_node *node);

/* Transform, geometry, material (analogous to SoTransform, SoShape, SoMaterial fields) */
BV_EXPORT void bv_node_transform_set(struct bv_node *node, const mat_t xform);
BV_EXPORT const mat_t *bv_node_transform_get(const struct bv_node *node);
BV_EXPORT void bv_node_geometry_set(struct bv_node *node, const void *geometry);
BV_EXPORT const void *bv_node_geometry_get(const struct bv_node *node);
BV_EXPORT void bv_node_material_set(struct bv_node *node, const struct bview_material *material);
BV_EXPORT const struct bview_material *bv_node_material_get(const struct bv_node *node);

/*
 * Native axis-aligned bounding box for a node.
 *
 * These accessors store and retrieve a caller-supplied AABB directly on the
 * node.  The native AABB is optional: nodes that have no inherent geometry of
 * their own (group separators, cameras, etc.) need not set it.
 *
 * When native bounds are present (have_bounds == 1) they take priority over
 * the bounding sphere derived from a wrapped bv_scene_obj during bv_node_bbox
 * traversal.  This allows pure new-API geometry nodes (those that do NOT wrap
 * a legacy bv_scene_obj) to participate correctly in bounding-box queries.
 *
 * bv_node_bounds_set() marks have_bounds = 1 on the node.
 * bv_node_bounds_clear() marks have_bounds = 0 (reverts to legacy fallback).
 * bv_node_bounds_get() returns 1 if have_bounds is set and fills *out_min /
 * *out_max; returns 0 and leaves the outputs unchanged otherwise.
 */
BV_EXPORT void bv_node_bounds_set(struct bv_node *node,
                                   const point_t min, const point_t max);
BV_EXPORT void bv_node_bounds_clear(struct bv_node *node);
BV_EXPORT int  bv_node_bounds_get(const struct bv_node *node,
                                   point_t *out_min, point_t *out_max);

/* Hierarchy management (analogous to SoGroup::addChild/removeChild/getChildren) */
BV_EXPORT void bv_node_add_child(struct bv_node *parent, struct bv_node *child);
BV_EXPORT void bv_node_remove_child(struct bv_node *parent, struct bv_node *child);
BV_EXPORT const struct bu_ptbl *bv_node_children(const struct bv_node *node);

/* Visibility (analogous to toggling SoSwitch or using SoNode visibility) */
BV_EXPORT void bv_node_visible_set(struct bv_node *node, int visible);
BV_EXPORT int bv_node_visible_get(const struct bv_node *node);

/* Selection (analogous to highlight/pick state in Coin3D SoSelection) */
BV_EXPORT void bv_node_selected_set(struct bv_node *node, int selected);
BV_EXPORT int bv_node_selected_get(const struct bv_node *node);

/*
 * Level-of-detail index for a node (Phase 4).
 *
 * The lod_level field is a hint to the rendering pipeline indicating which
 * level of geometric detail should be rendered for this node.  Level 0 is
 * the most detailed representation; higher values are progressively coarser.
 * The field is informational — bv_scene_lod_update() sets it via the LoD
 * selection logic in lod.cpp; the rendering backend reads it.
 */
BV_EXPORT void bv_node_lod_level_set(struct bv_node *node, int level);
BV_EXPORT int  bv_node_lod_level_get(const struct bv_node *node);

/* Type, name, and user data access */
BV_EXPORT enum bv_node_type bv_node_type_get(const struct bv_node *node);
BV_EXPORT const char *bv_node_name_get(const struct bv_node *node);
BV_EXPORT void bv_node_user_data_set(struct bv_node *node, void *user_data);
BV_EXPORT void *bv_node_user_data_get(const struct bv_node *node);

/* Get world transform (accumulated from parent hierarchy -- analogous to SoGetMatrixAction) */
BV_EXPORT const mat_t *bv_node_world_transform_get(const struct bv_node *node);

/* Get the parent node (NULL if this is a root node) */
BV_EXPORT struct bv_node *bv_node_parent_get(const struct bv_node *node);

/* --- LoD/update API --- */

/* Force LoD or redraw updates if needed; LoD should be triggered automatically by
 * setters but (for the moment) allow for explicit invocation.
 * bview_lod_update() calls bv_scene_lod_update() on the scene currently associated
 * with this view. */
BV_EXPORT void bview_lod_update(struct bview_new *view);
BV_EXPORT void bview_redraw(struct bview_new *view);

/*
 * LoD-aware per-node update hook (Phase 4).
 *
 * Called by the rendering pipeline when a node's level of detail needs
 * to be reconsidered given a new view state.  node must be a
 * BV_NODE_GEOMETRY node whose user_data holds a bv_scene_obj
 * (as set by bv_scene_obj_to_node()).  view may be NULL to unconditionally
 * mark the node's display list stale.
 *
 * If view is non-NULL and the scene object carries BV_MESH_LOD data
 * (s->s_type_flags & BV_MESH_LOD), this function calls bv_mesh_lod_view()
 * using the legacy bview pointed to by bview_old_get(view).  This wires
 * the existing LoD pipeline into the new scene graph traversal.
 *
 * Returns 1 if the node was processed, 0 otherwise.
 */
BV_EXPORT int bview_lod_node_update(struct bv_node *node, const struct bview_new *view);

/*
 * Update LoD for all BV_NODE_GEOMETRY nodes in a scene.
 *
 * Traverses every node in 'scene' and calls bview_lod_node_update() on
 * each BV_NODE_GEOMETRY node.  This is the scene-level equivalent of
 * the per-object bv_mesh_lod_view() calls that the legacy rendering
 * pipeline performs when a view changes.
 *
 * view may be NULL, in which case only display-list stale marking is
 * performed (no view-dependent LoD level selection).
 *
 * Returns the number of geometry nodes that were processed.
 */
BV_EXPORT int bv_scene_lod_update(struct bv_scene *scene, const struct bview_new *view);

/* --- Selection API --- */

/*
 * Collect all nodes in 'scene' whose selected flag is non-zero into
 * 'out'.  'out' must already be initialized by the caller (bu_ptbl_init).
 *
 * Traverses the entire scene graph; both visible and hidden nodes are
 * included.  The ptbl will hold 'struct bv_node *' pointers.
 *
 * Returns the number of selected nodes found.
 */
BV_EXPORT int bv_scene_selected_nodes(const struct bv_scene *scene, struct bu_ptbl *out);

/*
 * Set the selected flag on a specific node (1 = selected, 0 = deselected).
 * This is a thin wrapper around bv_node_selected_set() that also
 * updates the scene's pick_set metadata if a bview_new is provided.
 * view may be NULL.
 */
BV_EXPORT void bv_scene_select_node(struct bv_node *node, int selected, struct bview_new *view);

/*
 * Deselect all nodes in the scene.
 *
 * Equivalent to traversing every node and calling bv_scene_select_node(n, 0, view).
 * view may be NULL.
 *
 * Returns the number of nodes that were previously selected and are now cleared.
 */
BV_EXPORT int bv_scene_deselect_all(struct bv_scene *scene, struct bview_new *view);

/* --- Multi-view sharing (Phase 3) --- */

/*
 * Return the number of bview_new instances currently sharing 'scene'.
 *
 * bview_scene_set() and bview_scene_get() maintain an internal list of views
 * associated with each scene.  bv_scene_view_count() queries that list.
 *
 * This is the new-API replacement for counting entries returned by the
 * DEPRECATED bv_set_views() function.
 *
 * Returns 0 if scene is NULL.
 */
BV_EXPORT size_t bv_scene_view_count(const struct bv_scene *scene);

/*
 * Return the table of bview_new pointers currently sharing 'scene'.
 *
 * The returned bu_ptbl holds bview_new* entries.  The caller must not
 * modify the table.  Returns NULL if scene is NULL.
 *
 * Typical use: iterate with BU_PTBL_LEN / BU_PTBL_GET to broadcast an
 * event (e.g. a LoD update) to all views that share the same scene.
 */
BV_EXPORT const struct bu_ptbl *bv_scene_views(const struct bv_scene *scene);

/*
 * Compute the axis-aligned bounding box of a bv_node subtree.
 *
 * Traverses all visible BV_NODE_GEOMETRY nodes in the subtree rooted at
 * 'node'.  For each geometry node the bounding box is determined as follows:
 *
 *  1. If the node has native bounds set (bv_node_bounds_set() was called),
 *     those bounds are used directly.
 *  2. Otherwise, if the node's user_data points to a bv_scene_obj (as set by
 *     bv_scene_obj_to_node()), the object's pre-computed bounding sphere
 *     (s_center + s_size) is expanded to an AABB.
 *  3. Nodes with neither native bounds nor a wrapped bv_scene_obj contribute
 *     nothing to the result.
 *
 * On return:
 *   *out_min is the component-wise minimum corner of the AABB.
 *   *out_max is the component-wise maximum corner of the AABB.
 *   Returns 1 if at least one geometry node contributed to the bounds,
 *   0 if the subtree contained no visible/bounded geometry.
 *
 * out_min and out_max must be non-NULL.
 */
BV_EXPORT int bv_node_bbox(const struct bv_node *node, point_t *out_min, point_t *out_max);

/*
 * Compute the bounding box of an entire scene (all top-level nodes and
 * their subtrees).
 *
 * Equivalent to calling bv_node_bbox() on each top-level node and merging
 * the results.  Returns 1 if any geometry was found, 0 otherwise.
 */
BV_EXPORT int bv_scene_bbox(const struct bv_scene *scene, point_t *out_min, point_t *out_max);

/*
 * Use as 'scale_factor' to bview_autoview_new() to reproduce the same 2×
 * radial factor used by the legacy bv_autoview().
 * (Also defined in bv/util.h for the legacy API; the value is identical.)
 */
#ifndef BV_AUTOVIEW_SCALE_DEFAULT
#  define BV_AUTOVIEW_SCALE_DEFAULT -1
#endif

/*
 * Auto-position the camera in a bview_new to fit all visible geometry.
 *
 * Analog of bv_autoview() for the new scene graph API.  Computes the
 * AABB of all visible geometry in 'scene' using bv_scene_bbox(), then
 * repositions view->camera so the scene fills the viewport.
 *
 * 'scale_factor' controls the camera distance from the scene center:
 *   BV_AUTOVIEW_SCALE_DEFAULT (-1) uses the same 2× radial factor as
 *   the legacy bv_autoview(): camera is placed 2 * radius from center.
 *   Any positive value 'f' places the camera f * radius away from the
 *   scene center along the current viewing direction.
 *
 * The camera target is set to the scene center.  The camera position is
 * moved along the current eye-to-target direction (derived from the
 * current camera position and target) to achieve the requested distance.
 * If the current camera has no meaningful eye/target separation the
 * camera is moved along +Z.  The camera.up and perspective fields are
 * not altered.
 *
 * Returns 1 if camera was updated, 0 if the scene was empty (camera
 * unchanged).
 */
BV_EXPORT int bview_autoview_new(struct bview_new *view, const struct bv_scene *scene, double scale_factor);

/* --- Migration Helpers (optional) --- */

/* Sync with legacy struct during migration */
BV_EXPORT void bview_from_old(struct bview_new *view, const struct bview *old);
BV_EXPORT void bview_to_old(const struct bview_new *view, struct bview *old);
BV_EXPORT struct bview *bview_old_get(const struct bview_new *view);

/*
 * Apply BRL-CAD default settings to a bview_new instance.
 *
 * Sets the same initial values for camera, viewport, appearance, and overlay
 * that bv_init() / bv_settings_init() establish for a legacy struct bview.
 * Useful when creating a new bview_new from scratch (not migrated from an old
 * bview) to ensure all fields start from a consistent state.
 */
BV_EXPORT void bview_settings_apply(struct bview_new *view);

/*
 * Wrap a legacy bv_scene_obj in a new bv_node for use in the new scene graph
 * API.  Children of s are recursively wrapped.
 *
 * The original bv_scene_obj pointer is stored in the returned node's
 * user_data field so callers can recover it via bv_node_user_data_get().
 *
 * The caller is responsible for the lifecycle of both the returned bv_node
 * (use bv_node_destroy()) and the original bv_scene_obj.  No ownership is
 * transferred.
 *
 * Returns NULL if s is NULL.
 */
BV_EXPORT struct bv_node *bv_scene_obj_to_node(struct bv_scene_obj *s);

/*
 * Create a bv_scene populated with bv_node wrappers for every db_obj and
 * view_obj visible in legacy bview v.  Each top-level bv_scene_obj becomes a
 * direct child of the scene root; their children become sub-nodes.
 *
 * Returns a newly allocated bv_scene that the caller must destroy with
 * bv_scene_destroy() when done.  Returns NULL if v is NULL.
 */
BV_EXPORT struct bv_scene *bv_scene_from_view(const struct bview *v);

/*
 * Create a bv_scene populated from all shared scene objects stored in a
 * bview_set.  Each shared db_obj / view_obj becomes a child of the scene root.
 *
 * Returns a newly allocated bv_scene that the caller must destroy with
 * bv_scene_destroy() when done.  Returns NULL if s is NULL.
 */
BV_EXPORT struct bv_scene *bv_scene_from_view_set(const struct bview_set *s);

/*******************************************************************************/
/*              EXPERIMENTAL EXPERIMENTAL EXPERIMENTAL - END                   */
/*******************************************************************************/




#define BV_ANCHOR_AUTO          0
#define BV_ANCHOR_BOTTOM_LEFT   1
#define BV_ANCHOR_BOTTOM_CENTER 2
#define BV_ANCHOR_BOTTOM_RIGHT  3
#define BV_ANCHOR_MIDDLE_LEFT   4
#define BV_ANCHOR_MIDDLE_CENTER 5
#define BV_ANCHOR_MIDDLE_RIGHT  6
#define BV_ANCHOR_TOP_LEFT      7
#define BV_ANCHOR_TOP_CENTER    8
#define BV_ANCHOR_TOP_RIGHT     9
struct bv_label {
    int           size;
    struct bu_vls label;
    point_t       p;         // 3D base of label text
    int           line_flag; // If 1, draw a line from label anchor to target
    point_t       target;
    int           anchor;    // Either closest candidate to target (AUTO), or fixed
    int           arrow;     // If 1, use an arrow indicating direction from label to target
};


/* Note - this container holds information both for data axes and for the more
 * elaborate visuals associated with the Archer style model axes.  The latter
 * is a superset of the former, so there should be no need for a separate data
 * type. */
struct bv_axes {
    int       draw;
    point_t   axes_pos;             /* in model coordinates */
    fastf_t   axes_size;            /* in view coordinates for HUD drawing-mode axes */
    int       line_width;           /* in pixels */
    int       axes_color[3];

    /* The following are (currently) only used when drawing
     * the faceplace HUD axes */
    int       pos_only;
    int       label_flag;
    int       label_color[3];
    int       triple_color;
    int       tick_enabled;
    int       tick_length;          /* in pixels */
    int       tick_major_length;    /* in pixels */
    fastf_t   tick_interval;        /* in mm */
    int       ticks_per_major;
    int       tick_threshold;
    int       tick_color[3];
    int       tick_major_color[3];
};

// Many settings have application level defaults that can be overridden for
// individual scene objects.
//
// TODO - once this settles down, it will probably warrant a bu_structparse
// for value setting
struct bv_obj_settings {

    int s_dmode;         	/**< @brief  draw modes (TODO - are these accurate?):
				 *            0 - wireframe
				 *	      1 - shaded bots and polysolids only (booleans NOT evaluated)
				 *	      2 - shaded (booleans NOT evaluated)
				 *	      3 - shaded (booleans evaluated)
				 *	      4 - hidden line
				 */
    int mixed_modes;            /**< @brief  when drawing, don't remove an objects view objects for other modes */
    fastf_t transparency;	/**< @brief  holds a transparency value in the range [0.0, 1.0] - 1 is opaque */

    int color_override;
    unsigned char color[3];	/**< @brief  color to draw as */

    int s_line_width;		/**< @brief  current line width */
    fastf_t s_arrow_tip_length; /**< @brief  arrow tip length */
    fastf_t s_arrow_tip_width;  /**< @brief  arrow tip width */
    int draw_solid_lines_only;   /**< @brief do not use dashed lines for subtraction solids */
    int draw_non_subtract_only;  /**< @brief do not visualize subtraction solids */
};
#define BV_OBJ_SETTINGS_INIT {0, 0, 1.0, 0, {255, 0, 0}, 1, 0.0, 0.0, 0, 0}


/* Note that it is possible for a view object to be view-only (not
 * corresponding directly to the wireframe of a database shape) but also based
 * off of database data.  Evaluated shaded objects would be an example, as
 * would NIRT solid shotline visualizations or overlap visualizations.  The
 * categorizations for the various types of bv_scene_obj objects would be:
 *
 * solid wireframe/triangles (obj.s):  BV_DBOBJ_BASED
 * rtcheck overlap visual:             BV_DBOBJ_BASED & BV_VIEWONLY
 * polygon/line/label:                 BV_VIEWONLY
 *
 * TODO - the distinction between view and db objs at this level probably needs
 * to go away - the application (or at least higher level libraries like
 * libged) should be the one managing the semantic meanings of objects.
 *
 * The distinction between objects (lines, labels, etc.) defined as
 * bv_scene_obj VIEW ONLY objects and the faceplate elements is objects defined
 * as bv_scene_obj objects DO exist in the 3D scene, and will move as 3D
 * elements when the view is manipulated (although label text is drawn parallel
 * to the view plane.)  Faceplate elements exist ONLY in the HUD and are not
 * managed as bv_scene_obj objects - they will not move with view manipulation.
 */
#define BV_DBOBJ_BASED    0x01
#define BV_VIEWONLY       0x02
#define BV_LINES          0x04
#define BV_LABELS         0x08
#define BV_AXES           0x10
#define BV_POLYGONS       0x20
#define BV_MESH_LOD       0x40
#define BV_CSG_LOD        0x80


#define BV_DB_OBJS 0x01
#define BV_VIEW_OBJS 0x02
#define BV_LOCAL_OBJS 0x04
#define BV_CHILD_OBJS 0x08

struct bv_scene_obj_internal;

/* Display style */
struct bview_display_style {
    int wireframe;
    int shaded;
    int hidden_line;
    double transparency;
    unsigned char color[3];
    int line_width;
    int point_size;
    // Extendable: linestyle, material, etc.
};

struct bv_scene_obj  {
    struct bu_list l;

    /* Internal implementation storage */
    struct bv_scene_obj_internal *i;

    /* View object name and type id */
    unsigned long long s_type_flags;
    struct bu_vls s_name;       /**< @brief object name (should be unique if view objects are to be addressed by name) */
    void *s_path;       	/**< @brief alternative (app specific) encoding of s_name */
    void *dp;       		/**< @brief app obj data */
    mat_t s_mat;		/**< @brief mat to use for internal lookup and mesh LoD drawing */

    /* Associated bv.  Note that scene objects are not assigned uniquely to
     * one view.  This value may be changed by the application in a multi-view
     * scenario as an object is edited from multiple different views, to supply
     * the necessary view context for editing. If the object needs to retain
     * knowledge of its original/creation view, it should save that info
     * internally in its s_i_data container. */
    struct bview *s_v;

    /* Knowledge of how to create/update s_vlist and the other 3D geometry data, as well as
     * manage any custom data specific to this object */
    void *s_i_data;  /**< @brief custom view data (bv_line_seg, bv_label, bv_polyon, etc) */
    int (*s_update_callback)(struct bv_scene_obj *, struct bview *, int);  /**< @brief custom update/generator for s_vlist */
    void (*s_free_callback)(struct bv_scene_obj *);  /**< @brief free any info stored in s_i_data, s_path and draw_data */

    /* 3D vector list geometry data */
    struct bu_list s_vlist;	/**< @brief  Pointer to unclipped vector list */
    size_t s_vlen;			/**< @brief  Number of actual cmd[] entries in vlist */

    /* Display lists accelerate drawing when we can use them */
    unsigned int s_dlist;	/**< @brief  display list index */
    int s_dlist_mode;		/**< @brief  drawing mode in which display list was generated (if it doesn't match s_os.s_dmode, dlist is out of date.) */
    int s_dlist_stale;		/**< @brief  set by client codes when dlist is out of date - dm must update. */
    void (*s_dlist_free_callback)(struct bv_scene_obj *);  /**< @brief free any dlist specific data */

    /* 3D geometry metadata */
    fastf_t s_size;		/**< @brief  Distance across solid, in model space */
    fastf_t s_csize;		/**< @brief  Dist across clipped solid (model space) */
    vect_t s_center;		/**< @brief  Center point of solid, in model space */
    int s_displayobj;		/**< @brief  Vector list contains vertices in display context flag */
    point_t bmin;
    point_t bmax;
    int have_bbox;

    /* Display properties */
    char s_flag;		/**< @brief  UP = object visible, DOWN = obj invis */
    char s_iflag;	        /**< @brief  UP = illuminated, DOWN = regular */
    int s_force_draw;           /**< @brief  1 = overrides s_flag and s_iflag - always draw (allows parents to force children to be visible) */
    unsigned char s_color[3];	/**< @brief  color to draw as */
    int s_soldash;		/**< @brief  solid/dashed line flag: 0 = solid, 1 = dashed*/
    int s_arrow;		/**< @brief  arrow flag for view object drawing routines */
    int s_changed;		/**< @brief  changed flag - set by s_update_callback if a change occurred */
    int current;

    /* Adaptive plotting info.
     *
     * The adaptive wireframe flag is set if the wireframe was created while
     * adaptive mode is on - this is to allow reversion to non-adaptive
     * wireframes if the mode is switched off without the view scale changing.
     *
     * NOTE: We store the following NOT for controlling the drawing, but so we
     * can determine if the vlist is current with respect to the parent view
     * settings.  These values SHOULD NOT be directly manipulated by any user
     * facing commands (such as view obj).
     *
     * TODO - should the above be true?  Managing the loading of appropriate
     * geometry for individual objects based on the local settings might make
     * sense.  If we use these, perhaps the bview level settings can be removed
     * altogether and the view won't need to care about anything except what is
     * in the scene obj at draw time....  Maybe add these to bv_obj_settings?*/
    int     adaptive_wireframe;
    int     csg_obj;
    int     mesh_obj;
    fastf_t view_scale;
    size_t  bot_threshold;
    fastf_t curve_scale;
    fastf_t point_scale;

    /* Scene object settings which also (potentially) have global defaults but
     * may be overridden locally */
    struct bv_obj_settings *s_os;
    struct bv_obj_settings s_local_os;
    int s_inherit_settings;           /**< @brief  Use current obj settings when drawing children instead of their settings */

    /* Settings that may be less necessary... */
    struct bv_scene_obj_old_settings s_old;

    /* Child objects of this object */
    struct bu_ptbl children;

    /* Parent object of this object */
    struct bv_scene_ob *parent;

    /* Object level pointers to parent containers.  These are stored so
     * that the object itself knows everything needed for data manipulation
     * and it is unnecessary to explicitly pass other parameters. */

    /* Reusable vlists */
    struct bu_list *vlfree;

    /* Container for reusing bv_scene_obj allocations */
    struct bv_scene_obj *free_scene_obj;

    /* View container containing this object */
    struct bu_ptbl *otbl;

    /* For more specialized routines not using vlists, we may need
     * additional drawing data associated with a scene object */
    void *draw_data;

    /* User data to associate with this view object */
    void *s_u_data;
};



/* bv_scene_groups (one level above scene objects, conceptually equivalent
 * to display_list) are used to capture the intent of drawing commands.  For
 * example, in the scenario where a draw command is used to visualize a comb
 * with sub-combs a and b:
 *
 * ged> draw comb
 *
 * The drawing code will check the proposed group against existing groups,
 * adding and removing accordingly.  It will then walk the hierarchy and create
 * bv_scene_obj instances for all solids below comb/a and comb/b as children
 * of the scene group.  Note that since we specified "comb" as the drawn
 * object, if comb/b is removed from comb and comb/c is added, we would expect
 * comb's displayed view to be updated to reflect its current structure.  If,
 * however, we instead did the original visualization with the commands:
 *
 * ged> draw comb/a
 * ged> draw comb/b
 *
 * The same solids would be drawn, but conceptually the comb itself is not
 * drawn - the two instances are.  If comb/b is removed and comb/c added, we
 * would not expect comb/c to be drawn since we never drew either that instance
 * or its parent comb.
 *
 * However, if comb/a and comb/b are drawn and then comb is drawn, the new comb
 * scene group will replace both the comb/a and comb/b groups since they are now
 * part of a higher level object being drawn.  If comb is drawn and comb/a is
 * subsequently drawn, it will be a no-op since "comb" is already covering that
 * case.
 *
 * The rule with bv_scene_group instances is their children must specify a
 * fully realized entity - if the s_name is "/comb/a" then everything below
 * /comb/a is drawn.  If /comb/a/obj1.s is erased, new bv_scene_group
 * entities will be needed to reflect the partial nature of /comb/a in the
 * visualization.  That requirement also propagates back up the tree. If a has
 * obj1.s and obj2.s below it, and /comb/a/obj1.s is erased, an original
 * "/comb" scene group will be replaced by new scene groups: /comb/a/obj2.s and
 * /comb/b.  Note that if /comb/a/obj1.s is subsequently drawn in isolation,
 * the scene groups will not collapse back to a single comb group - the user
 * will not at that point have explicitly issued instructions to draw comb as a
 * whole, even though all the individual elements have been drawn.  A "view
 * simplify" command should probably be added to support collapsing to the
 * simplest available option automatically in that situation.
 *
 * Note that the above rule is for explicit erasure from the drawn scene group
 * - if the structure of /comb/a is changed the drawn object is still "comb"
 * and the solid children of the existing group are updated to reflect the
 * current state of comb, rather than introducing new scene groups.
 *
 * Much like point_t and vect_t, the distinction between a group and an
 * individual object is largely semantic rather than a question of different
 * data storage.  A group just uses the bv_scene_obj container to store
 * group-wide default settings, and g.children holds the individual
 * bv_scene_obj entries corresponding to the solids.  A bv_scene_obj
 * should always map to a solid - a group may specify a solid but more
 * typically will reference the root of a CSG tree and have solids below it.
 * We define them to have different types only to help keep straight in the
 * code what is a conceptually a group and what is an individual scene object.
 *
 * TODO - once the latest drawing code update matures, the path management
 * done there should make the idea of a bv_scene_group moot.
 */
#define bv_scene_group bv_scene_obj


/* The primary "working" data for mesh Level-of-Detail (LoD) drawing is stored
 * in a bv_mesh_lod container.
 *
 * Most LoD information is deliberately hidden in the internal, but the key
 * data needed for drawing routines and view setup are exposed. Although this
 * data structure is primarily managed in libbg, the public data in this struct
 * is needed at many levels of the software stack, including libbv. */
struct bv_mesh_lod {

    // The set of triangle faces to be used when drawing
    int fcnt;
    const int *faces;

    // The vertices used by the faces array
    int pcnt;
    const point_t *points;      // If using snapped points, that's this array.  Else, points == points_orig.
    int porig_cnt;
    const point_t *points_orig;

    // Optional: per-face-vertex normals (one normal per triangle vertex - NOT
    // one normal per vertex.  I.e., a given point from points_orig may have
    // multiple normals associated with it in different faces.)
    const vect_t *normals;

    // Bounding box of the original full-detail data
    point_t bmin;
    point_t bmax;

    // The scene object using this LoD structure
    struct bv_scene_obj *s;

    // Pointer to the higher level LoD context associated with this LoD data
    void *c;

    // Pointer to internal LoD implementation information specific to this object
    void *i;
};

/* Flags to identify categories of objects to snap */
#define BV_SNAP_SHARED 0x1
#define BV_SNAP_LOCAL  0x2
#define BV_SNAP_DB  0x4
#define BV_SNAP_VIEW    0x8
#define BV_SNAP_TCL    0x10

/* We encapsulate non-camera settings into a container mainly to allow for
 * easier reuse of the same settings between different views - if a common
 * setting set is maintained between different views, this container allows
 * us to just point to the common set from all views using it. */
struct bview_settings {
    int            gv_snap_lines;
    double 	   gv_snap_tol_factor;
    struct bu_ptbl gv_snap_objs;
    int		   gv_snap_flags;
    int            gv_cleared;
    int            gv_zclip;
    int            gv_autoview;

    // Adaptive plotting related settings - these are used when the wireframe
    // generated by primitives is based on the view information.
    int           adaptive_plot_mesh;
    int           adaptive_plot_csg;
    size_t        bot_threshold;
    fastf_t       curve_scale;
    fastf_t       point_scale;
    int           redraw_on_zoom;
    fastf_t 	  lod_scale;

    // Faceplate elements fall into two general categories: those which are
    // interactively adjusted (in a geometric sense) and those which are not.
    // The non-interactive are generally just enabled or disabled:
    struct bv_axes           gv_model_axes;
    struct bv_axes           gv_view_axes;
    struct bv_grid_state     gv_grid;
    struct bv_other_state    gv_center_dot;
    struct bv_params_state   gv_view_params;
    struct bv_other_state    gv_view_scale;
    double                   gv_frametime;

    // Framebuffer visualization is possible if there is an attached dm and
    // that dm has an associated framebuffer.  If those conditions are met,
    // this variable is used to control how the fb is visualized.
    int                      gv_fb_mode; // 0 = off, 1 = overlay, 2 = underlay

    // More complex are the faceplate view elements not corresponding to
    // geometry objects but editable by the user.  These aren't managed as
    // gv_view_objs (they are HUD visuals and thus not part of the scene) so
    // they have some unique requirements.
    struct bv_adc_state              gv_adc;
    struct bv_interactive_rect_state gv_rect;


    // Not yet implemented - mechanism for defining a set of selected view
    // objects
    struct bu_ptbl                      *gv_selected;
};

/* A view needs to know what objects are active within it, but this is a
 * function not just of adding and removing objects via commands like
 * "draw" and "erase" but also what settings are active.  Shared objects
 * are common to multiple views, but if adaptive plotting is enabled the
 * scene objects cannot also be common - the representations of the objects
 * may be different in each view, even though the object list is shared.
 */
struct bview_objs {

    // Container for db object groups unique to this view (typical use case is
    // adaptive plotting, where geometry wireframes may differ from view to
    // view and thus need unique vlists.)
    struct bu_ptbl  *db_objs;
    // Container for storing bv_scene_obj elements unique to this view.
    struct bu_ptbl  *view_objs;

    // Available bv_vlist entities to recycle before allocating new for local
    // view objects. This is used only if the app doesn't supply a vlfree -
    // normally the app should do so, so memory from one view can be reused for
    // other views.
    struct bu_list  gv_vlfree;

    /* Container for reusing bv_scene_obj allocations */
    struct bv_scene_obj *free_scene_obj;
};

// Data for managing "knob" manipulation of views.  One historical hardware
// example of this "knob" concept of view manipulation would be Dial boxes such
// as the Silicon Graphics SN-921, used with 3D workstations in the early days.
// Although we've not heard of Dial boxes being used with BRL-CAD in many
// years, the mathematics of view manipulation used to support them still
// underpins interactions driven with inputs from modern peripherals such as
// the mouse.
struct bview_knobs {

    /* Rate data */
    vect_t      rot_m;      // rotation - model coords
    int         rot_m_flag;
    char        origin_m;
    void	*rot_m_udata;

    vect_t	rot_o;      // rotation - object coords
    int		rot_o_flag;
    char	origin_o;
    void	*rot_o_udata;

    vect_t      rot_v;      // rotation - view coords
    int         rot_v_flag;
    char        origin_v;
    void	*rot_v_udata;

    fastf_t     sca;        // scale
    int         sca_flag;
    void	*sca_udata;

    vect_t      tra_m;      // translation - model coords
    int         tra_m_flag;
    void	*tra_m_udata;

    vect_t      tra_v;      // translation - view coords
    int         tra_v_flag;
    void	*tra_v_udata;

    /* Absolute data */
    vect_t      rot_m_abs;       // rotation - model coords
    vect_t      rot_m_abs_last;

    vect_t      rot_o_abs;       // rotation - object coords
    vect_t      rot_o_abs_last;

    vect_t      rot_v_abs;       // rotation - view coords
    vect_t      rot_v_abs_last;

    fastf_t     sca_abs;

    vect_t      tra_m_abs;       // translation - model coords
    vect_t      tra_m_abs_last;

    vect_t      tra_v_abs;       // translation - view coords
    vect_t      tra_v_abs_last;

};

struct bview_set;

struct bview {
    uint32_t	  magic;             /**< @brief magic number */
    struct bu_vls gv_name;

    /* Size info */
    fastf_t       gv_i_scale;
    fastf_t       gv_a_scale;        /**< @brief absolute scale */
    fastf_t       gv_scale;
    fastf_t       gv_size;           /**< @brief  2.0 * scale */
    fastf_t       gv_isize;          /**< @brief  1.0 / size */
    fastf_t       gv_base2local;
    fastf_t       gv_local2base;
    fastf_t       gv_rscale;
    fastf_t       gv_sscale;

    /* Information about current "window" into view.  This view may not be
     * displayed (that's up to the display managers) and it is up to the
     * calling code to set gv_width and gv_height to the current correct values
     * for such a display, if it is associated with this view.  These
     * definitions are needed in bview to support "view aware" algorithms that
     * require information defining an active pixel "window" into the view. */
    int		  gv_width;
    int		  gv_height;
    point2d_t	  gv_wmin; // view space bbox minimum of gv_width/gv_height window
    point2d_t	  gv_wmax; // view space bbox maximum of gv_width/gv_height window

    /* Camera info */
    fastf_t       gv_perspective;    /**< @brief  perspective angle */
    vect_t        gv_aet;
    vect_t        gv_eye_pos;        /**< @brief  eye position */
    vect_t        gv_keypoint;
    char          gv_coord;          /**< @brief  coordinate system */
    char          gv_rotate_about;   /**< @brief  indicates what point rotations are about */
    mat_t         gv_rotation;
    mat_t         gv_center;
    mat_t         gv_model2view;
    mat_t         gv_pmodel2view;
    mat_t         gv_view2model;
    mat_t         gv_pmat;           /**< @brief  perspective matrix */

    /* Keyboard/mouse info */
    fastf_t       gv_prevMouseX;
    fastf_t       gv_prevMouseY;
    int           gv_mouse_x;
    int           gv_mouse_y;
    point_t       gv_prev_point;
    point_t       gv_point;
    char          gv_key;
    unsigned long gv_mod_flags;
    fastf_t       gv_minMouseDelta;
    fastf_t       gv_maxMouseDelta;

    /* Settings */
    struct bview_settings *gv_s;     /**< @brief shared settings supplied by user */
    struct bview_settings gv_ls;     /**< @brief locally maintained settings specific to view (used if gv_s is null) */

    /* If a view is marked as independent, its local containers are used even
     * if pointers to shared tables are set. This allows for fully independent
     * views with the same GED instance, at the cost of increased memory usage
     * if multiple views draw the same objects. */
    int independent;

    /* Set containing this view.  Also holds pointers to resources shared
     * across multiple views */
    struct bview_set *vset;

    /* Scene objects active in a view.  Managing these is a relatively complex
     * topic and depends on whether a view is shared, independent or adaptive.
     * Shared objects are common across views to make more efficient use of
     * system memory. */
    struct bview_objs gv_objs;

    /* We sometimes need to define the volume in space that is "active" for the
     * view.  For an orthogonal camera this is the oriented bounding box
     * extruded to contain active scene objects visible in the view  The app
     * must set the gv_bounds_update callback to bg_view_bound so a bv_update
     * call can update these values.*/
    point_t obb_center;
    vect_t obb_extent1;
    vect_t obb_extent2;
    vect_t obb_extent3;
    void (*gv_bounds_update)(struct bview *);

    /* "Backed out" point, lookat direction, scene radius. Used for geometric
     * view based interrogation. */
    point_t gv_vc_backout;
    vect_t gv_lookat;
    double radius;

    /* Knob-based view manipulation data */
    struct bview_knobs k;

    /* Virtual trackball position */
    point_t     orig_pos;

    // libtclcad data
    struct bv_data_tclcad gv_tcl;

    /* Callback, external data */
    void          (*gv_callback)(struct bview *, void *);  /**< @brief  called in ged_view_update with gvp and gv_clientData */
    void           *gv_clientData;   /**< @brief  passed to gv_callback */
    struct bu_ptbl *callbacks;
    void           *dmp;             /* Display manager pointer, if one is associated with this view */
    void           *u_data;          /* Caller data associated with this view */
};

// Because bview instances frequently share objects in applications, they are
// not always fully independent - we define a container and some basic
// operations to manage this.
struct bview_set_internal;
struct bview_set {
    struct bview_set_internal   *i;
    struct bview_settings       settings;
};

__END_DECLS

#endif /* BV_DEFINES_H */

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
