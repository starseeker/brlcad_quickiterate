/*                    B S G / U T I L . H
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
/** @addtogroup bsg_util
 *
 * @brief
 * Utility functions for the BRL-CAD Scene Graph (BSG) API.
 *
 * These functions replace the corresponding @c bv_* functions declared in
 * @c <bv/util.h> and @c <bv/view_sets.h>.  New code should use these
 * interfaces.
 *
 * In Phase 1 each @c bsg_* function is a trivial wrapper around its
 * @c bv_* counterpart.  Because @c bsg_view = @c bview and @c bsg_shape =
 * @c bv_scene_obj in Phase 1, the wrappers incur no conversion cost.
 *
 * ### Function naming convention
 *
 * | Old (legacy) function | New (BSG) function         |
 * |-----------------------|----------------------------|
 * | bv_init()             | bsg_view_init()            |
 * | bv_free()             | bsg_view_free()            |
 * | bv_settings_init()    | bsg_view_settings_init()   |
 * | bv_mat_aet()          | bsg_view_mat_aet()         |
 * | bv_autoview()         | bsg_view_autoview()        |
 * | bv_sync()             | bsg_view_sync()            |
 * | bv_obj_settings_sync()| bsg_material_sync()        |
 * | bv_update()           | bsg_view_update()          |
 * | bv_update_selected()  | bsg_view_update_selected() |
 * | bv_knobs_reset()      | bsg_knobs_reset()          |
 * | bv_knobs_hash()       | bsg_knobs_hash()           |
 * | bv_knobs_cmd_process()| bsg_knobs_cmd_process()    |
 * | bv_knobs_rot()        | bsg_knobs_rot()            |
 * | bv_knobs_tran()       | bsg_knobs_tran()           |
 * | bv_update_rate_flags()| bsg_view_update_rate_flags()|
 * | bv_differ()           | bsg_view_differ()          |
 * | bv_hash()             | bsg_view_hash()            |
 * | bv_dl_hash()          | bsg_dl_hash()              |
 * | bv_clear()            | bsg_view_clear()           |
 * | bv_adjust()           | bsg_view_adjust()          |
 * | bv_screen_to_view()   | bsg_screen_to_view()       |
 * | bv_screen_pt()        | bsg_screen_pt()            |
 * | bv_obj_create()       | bsg_shape_create()         |
 * | bv_obj_get()          | bsg_shape_get()            |
 * | bv_obj_get_child()    | bsg_shape_get_child()      |
 * | bv_obj_reset()        | bsg_shape_reset()          |
 * | bv_obj_put()          | bsg_shape_put()            |
 * | bv_obj_stale()        | bsg_shape_stale()          |
 * | bv_obj_sync()         | bsg_shape_sync()           |
 * | bv_find_child()       | bsg_shape_find_child()     |
 * | bv_find_obj()         | bsg_view_find_shape()      |
 * | bv_uniq_obj_name()    | bsg_view_uniq_name()       |
 * | bv_obj_for_view()     | bsg_shape_for_view()       |
 * | bv_obj_get_vo()       | bsg_shape_get_view_obj()   |
 * | bv_obj_have_vo()      | bsg_shape_have_view_obj()  |
 * | bv_clear_view_obj()   | bsg_shape_clear_view_obj() |
 * | bv_illum_obj()        | bsg_shape_illum()          |
 * | bv_scene_obj_bound()  | bsg_shape_bound()          |
 * | bv_vZ_calc()          | bsg_shape_vZ_calc()        |
 * | bv_view_objs()        | bsg_view_shapes()          |
 * | bv_view_plane()       | bsg_view_plane()           |
 * | bv_view_print()       | bsg_view_print()           |
 * | bv_set_init()         | bsg_scene_init()           |
 * | bv_set_free()         | bsg_scene_free()           |
 * | bv_set_add_view()     | bsg_scene_add_view()       |
 * | bv_set_rm_view()      | bsg_scene_rm_view()        |
 * | bv_set_views()        | bsg_scene_views()          |
 * | bv_set_find_view()    | bsg_scene_find_view()      |
 * | (new Phase 2)         | bsg_traversal_state_init() |
 * | (new Phase 2)         | bsg_traverse()             |
 * | (new Phase 2)         | bsg_view_get_camera()      |
 * | (new Phase 2)         | bsg_view_set_camera()      |
 * | (new Phase 2)         | bsg_node_alloc()           |
 * | (new Phase 2)         | bsg_node_free()            |
 * | (new Phase 2)         | bsg_camera_node_alloc()    |
 * | (new Phase 2)         | bsg_camera_node_set()      |
 * | (new Phase 2)         | bsg_camera_node_get()      |
 * | (new Phase 2)         | bsg_scene_root_get()       |
 * | (new Phase 2)         | bsg_scene_root_set()       |
 * | (new Phase 2)         | bsg_scene_root_create()    |
 * | (new Phase 2)         | bsg_view_traverse()        |
 * | (new Phase 2)         | bsg_shape_add_sensor()     |
 * | (new Phase 2)         | bsg_shape_rm_sensor()      |
 * | (new Phase 2)         | bsg_lod_group_alloc()      |
 * | (new Phase 2)         | bsg_lod_group_select_child()|
 */

#ifndef BSG_UTIL_H
#define BSG_UTIL_H

#include "common.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/** @{ */
/** @file bsg/util.h */

/* ====================================================================== *
 * View lifecycle                                                          *
 * ====================================================================== */

/** @brief Initialise @p v with default values.  Replaces bv_init(). */
BSG_EXPORT void bsg_view_init(bsg_view *v, bsg_scene *scene);

/** @brief Release resources owned by @p v.  Replaces bv_free(). */
BSG_EXPORT void bsg_view_free(bsg_view *v);

/** @brief Initialise settings with defaults.  Replaces bv_settings_init(). */
BSG_EXPORT void bsg_view_settings_init(bsg_view_settings *s);

/** @brief Set camera orientation from AET angles.  Replaces bv_mat_aet(). */
BSG_EXPORT void bsg_view_mat_aet(bsg_view *v);

/* ====================================================================== *
 * View manipulation                                                       *
 * ====================================================================== */

/** Use the default autoview scale (0.5 model scale ↔ 2.0 view factor). */
#define BSG_AUTOVIEW_SCALE_DEFAULT BV_AUTOVIEW_SCALE_DEFAULT

/** @brief Auto-fit the view.  Replaces bv_autoview(). */
BSG_EXPORT void bsg_view_autoview(bsg_view *v, fastf_t scale,
  int all_view_objs);

/** @brief Copy camera and size info from @p src to @p dst.  Replaces bv_sync(). */
BSG_EXPORT void bsg_view_sync(bsg_view *dst, bsg_view *src);

/** @brief Recompute derived matrices; fire callbacks.  Replaces bv_update(). */
BSG_EXPORT void bsg_view_update(bsg_view *v);

/** @brief Update selected objects.  Replaces bv_update_selected(). */
BSG_EXPORT int bsg_view_update_selected(bsg_view *v);

/* ====================================================================== *
 * View comparison / hashing                                              *
 * ====================================================================== */

/**
 * @brief Compare two views.
 * Returns 1 (content differs), 2 (settings differ), 3 (data differs),
 * -1 (NULL input), or 0 (equal).  Replaces bv_differ().
 */
BSG_EXPORT int bsg_view_differ(bsg_view *v1, bsg_view *v2);

/** @brief Hash view contents.  Returns 0 on failure.  Replaces bv_hash(). */
BSG_EXPORT unsigned long long bsg_view_hash(bsg_view *v);

/** @brief Hash display list.  Returns 0 on failure.  Replaces bv_dl_hash(). */
BSG_EXPORT unsigned long long bsg_dl_hash(struct display_list *dl);

/* ====================================================================== *
 * Material sync                                                           *
 * ====================================================================== */

/**
 * @brief Sync material settings @p src → @p dst.
 * Returns 1 if any field changed, 0 if already equal.
 * Replaces bv_obj_settings_sync().
 */
BSG_EXPORT int bsg_material_sync(bsg_material *dst,
 const bsg_material *src);

/* ====================================================================== *
 * View clearing                                                           *
 * ====================================================================== */

/**
 * @brief Remove objects from view @p v according to @p flags.
 * Returns the number of objects remaining.  Replaces bv_clear().
 */
BSG_EXPORT size_t bsg_view_clear(bsg_view *v, int flags);

/* ====================================================================== *
 * View interaction                                                        *
 * ====================================================================== */

/**
 * @brief Update view from pointer motion.  Replaces bv_adjust().
 */
BSG_EXPORT int bsg_view_adjust(bsg_view *v, int dx, int dy,
       point_t keypoint, int mode,
       unsigned long long flags);

/**
 * @brief Map screen pixel → view coordinates.
 * Returns -1 if viewport dimensions unset.  Replaces bv_screen_to_view().
 */
BSG_EXPORT int bsg_screen_to_view(bsg_view *v,
  fastf_t *fx, fastf_t *fy,
  fastf_t x, fastf_t y);

/**
 * @brief Project screen pixel → 3-D model point.
 * Returns -1 if viewport dimensions unset.  Replaces bv_screen_pt().
 */
BSG_EXPORT int bsg_screen_pt(point_t *p, fastf_t x, fastf_t y,
     bsg_view *v);

/* ====================================================================== *
 * Shape lifecycle                                                         *
 * ====================================================================== */

/**
 * @brief Allocate a shape of @p type and add it to view @p v.
 * Replaces bv_obj_get().
 */
BSG_EXPORT bsg_shape *bsg_shape_get(bsg_view *v, int type);

/**
 * @brief Allocate a shape of @p type without adding it to a view.
 * Replaces bv_obj_create().
 */
BSG_EXPORT bsg_shape *bsg_shape_create(bsg_view *v, int type);

/**
 * @brief Allocate a child shape under @p parent.
 * Replaces bv_obj_get_child().
 */
BSG_EXPORT bsg_shape *bsg_shape_get_child(bsg_shape *parent);

/**
 * @brief Clear contents of @p s (and children) but keep it in the view.
 * Replaces bv_obj_reset().
 */
BSG_EXPORT void bsg_shape_reset(bsg_shape *s);

/**
 * @brief Release @p s to the free pool.  Replaces bv_obj_put().
 */
BSG_EXPORT void bsg_shape_put(bsg_shape *s);

/**
 * @brief Mark @p s and descendants as stale.  Replaces bv_obj_stale().
 */
BSG_EXPORT void bsg_shape_stale(bsg_shape *s);

/**
 * @brief Copy attributes (not geometry) from @p src to @p dst.
 * Replaces bv_obj_sync().
 */
BSG_EXPORT void bsg_shape_sync(bsg_shape *dst,
       const bsg_shape *src);

/* ====================================================================== *
 * Shape lookup                                                            *
 * ====================================================================== */

/**
 * @brief Find a child of @p s matching @p pattern (glob/UUID).
 * Replaces bv_find_child().
 */
BSG_EXPORT bsg_shape *bsg_shape_find_child(bsg_shape *s,
  const char *pattern);

/**
 * @brief Find a top-level shape in view @p v matching @p pattern.
 * Replaces bv_find_obj().
 */
BSG_EXPORT bsg_shape *bsg_view_find_shape(bsg_view *v,
 const char *pattern);

/**
 * @brief Generate a unique name derived from @p seed in view @p v.
 * Replaces bv_uniq_obj_name().
 */
BSG_EXPORT void bsg_view_uniq_name(struct bu_vls *result, const char *seed,
   bsg_view *v);

/* ====================================================================== *
 * Shape view-specific objects                                             *
 * ====================================================================== */

/** @brief Return the view-appropriate shape for @p s in view @p v.
 *  Replaces bv_obj_for_view(). */
BSG_EXPORT bsg_shape *bsg_shape_for_view(bsg_shape *s,
bsg_view *v);

/** @brief Get or create a view-specific child for @p s in @p v.
 *  Replaces bv_obj_get_vo(). */
BSG_EXPORT bsg_shape *bsg_shape_get_view_obj(bsg_shape *s,
    bsg_view *v);

/** @brief Return non-zero if @p s has a view-specific child for @p v.
 *  Replaces bv_obj_have_vo(). */
BSG_EXPORT int bsg_shape_have_view_obj(bsg_shape *s,
       bsg_view *v);

/** @brief Remove the view-specific child of @p s for @p v.
 *  Replaces bv_clear_view_obj(). */
BSG_EXPORT int bsg_shape_clear_view_obj(bsg_shape *s,
bsg_view *v);

/* ====================================================================== *
 * Shape geometry / bounding                                              *
 * ====================================================================== */

/**
 * @brief Compute bounding box of @p s.  Returns 1 on success.
 * Replaces bv_scene_obj_bound().
 */
BSG_EXPORT int bsg_shape_bound(bsg_shape *s, bsg_view *v);

/**
 * @brief Return nearest (mode=0) or farthest (mode=1) Z in @p s's vlist.
 * Replaces bv_vZ_calc().
 */
BSG_EXPORT fastf_t bsg_shape_vZ_calc(bsg_shape *s,
     bsg_view *v, int mode);

/* ====================================================================== *
 * Shape illumination / selection                                         *
 * ====================================================================== */

/**
 * @brief Set illumination state on @p s and descendants.
 * Returns 1 if any state changed.  Replaces bv_illum_obj().
 */
BSG_EXPORT int bsg_shape_illum(bsg_shape *s, char ill_state);

/* ====================================================================== *
 * View container accessors                                               *
 * ====================================================================== */

/**
 * @brief Return the bu_ptbl of shapes of @p type in view @p v.
 * Replaces bv_view_objs().
 */
BSG_EXPORT struct bu_ptbl *bsg_view_shapes(bsg_view *v, int type);

/* ====================================================================== *
 * View geometry helpers                                                   *
 * ====================================================================== */

/** @brief Construct the view plane for @p v.  Replaces bv_view_plane(). */
BSG_EXPORT int bsg_view_plane(plane_t *p, bsg_view *v);

/* ====================================================================== *
 * Knob manipulation                                                       *
 * ====================================================================== */

/** @brief Reset knob state.  Replaces bv_knobs_reset(). */
BSG_EXPORT void bsg_knobs_reset(bsg_knobs *k, int category);

/** @brief Hash knob state.  Replaces bv_knobs_hash(). */
BSG_EXPORT unsigned long long bsg_knobs_hash(bsg_knobs *k,
     struct bu_data_hash_state *state);

/** @brief Process a knob command string.  Replaces bv_knobs_cmd_process(). */
BSG_EXPORT int bsg_knobs_cmd_process(
    vect_t *rvec, int *do_rot, vect_t *tvec, int *do_tran,
    bsg_view *v, const char *cmd, fastf_t f,
    char origin, int model_flag, int incr_flag);

/** @brief Apply a rotation to the view.  Replaces bv_knobs_rot(). */
BSG_EXPORT void bsg_knobs_rot(bsg_view *v,
      const vect_t rvec,
      char origin, char coords,
      const matp_t obj_rot,
      const pointp_t pvt_pt);

/** @brief Apply a translation to the view.  Replaces bv_knobs_tran(). */
BSG_EXPORT void bsg_knobs_tran(bsg_view *v,
       const vect_t tvec,
       int model_flag);

/** @brief Update rate flags.  Replaces bv_update_rate_flags(). */
BSG_EXPORT void bsg_view_update_rate_flags(bsg_view *v);

/* ====================================================================== *
 * Scene (view-set) management                                            *
 * ====================================================================== */

/** @brief Initialise an empty scene.  Replaces bv_set_init(). */
BSG_EXPORT void bsg_scene_init(bsg_scene *s);

/** @brief Release all resources.  Replaces bv_set_free(). */
BSG_EXPORT void bsg_scene_free(bsg_scene *s);

/** @brief Add view to scene.  Replaces bv_set_add_view(). */
BSG_EXPORT void bsg_scene_add_view(bsg_scene *s, bsg_view *v);

/** @brief Remove view from scene (NULL = remove all).  Replaces bv_set_rm_view(). */
BSG_EXPORT void bsg_scene_rm_view(bsg_scene *s, bsg_view *v);

/** @brief Return all views in scene.  Replaces bv_set_views(). */
BSG_EXPORT struct bu_ptbl *bsg_scene_views(bsg_scene *s);

/** @brief Find view by name.  Returns NULL if not found.  Replaces bv_set_find_view(). */
BSG_EXPORT bsg_view *bsg_scene_find_view(bsg_scene *s,
const char *name);

/* ====================================================================== *
 * Phase 2: scene graph traversal                                         *
 * ====================================================================== */

/**
 * @brief Initialise @p state to identity values.
 *
 * Sets @c xform to the identity matrix, @c material to
 * @c BSG_MATERIAL_INIT, and @c depth to 0.  Must be called before
 * passing @p state to @c bsg_traverse().
 */
BSG_EXPORT void bsg_traversal_state_init(bsg_traversal_state *state);

/**
 * @brief Traverse the scene (sub-)graph rooted at @p root.
 *
 * Performs a pre-order depth-first walk.  At each node, @p visit is called
 * with the node pointer, the current accumulated @c bsg_traversal_state, and
 * @p user_data.  If @p visit returns non-zero the node's subtree is pruned
 * (no children are visited).
 *
 * State accumulation rules:
 *   - The accumulated transform is updated at every node by post-multiplying
 *     the node's @c s_mat into the current @c state->xform.
 *   - The accumulated material is updated at every node that has an explicit
 *     settings override (i.e. @c s_os != NULL and @c s_inherit_settings == 0).
 *   - Nodes flagged with @c BSG_NODE_SEPARATOR cause the traversal state to be
 *     saved on descent and fully restored on ascent — SoSeparator semantics.
 *
 * @param root      Root of the sub-graph to traverse.  NULL is a no-op.
 * @param state     Traversal state; updated in-place.  Caller must initialise
 *                  with @c bsg_traversal_state_init() before the first call.
 * @param visit     Callback invoked for every node (pre-order).
 *                  Return non-zero to prune this node's subtree.
 * @param user_data Opaque pointer forwarded to every @p visit call.
 */
BSG_EXPORT void bsg_traverse(bsg_shape *root,
			     bsg_traversal_state *state,
			     int (*visit)(bsg_shape *,
					  const bsg_traversal_state *,
					  void *),
			     void *user_data);

/* ====================================================================== *
 * Phase 2: camera accessor                                               *
 * ====================================================================== */

/**
 * @brief Copy camera data from view @p v into @p out.
 *
 * In Phase 1 the camera fields live inline in @c bsg_view (= @c bview).
 * This function populates a @c bsg_camera snapshot so that consumers can
 * use the Phase 2 interface without waiting for Phase 2 to embed the struct.
 *
 * When Phase 2 embeds @c bsg_camera directly in @c bsg_view the migration
 * is mechanical:
 *
 * @code
 * // Phase 1:
 * struct bsg_camera cam;
 * bsg_view_get_camera(v, &cam);
 *
 * // Phase 2 (after embed):
 * const struct bsg_camera *cam = &v->camera;
 * @endcode
 *
 * @param v   Source view (must not be NULL).
 * @param out Destination camera struct (must not be NULL).
 */
BSG_EXPORT void bsg_view_get_camera(const bsg_view *v, struct bsg_camera *out);

/* ====================================================================== *
 * Phase 2: standalone node allocation                                    *
 * ====================================================================== */

/**
 * @brief Allocate a standalone @c bsg_shape node not attached to any view.
 *
 * Used for camera nodes (@c BSG_NODE_CAMERA), LoD group nodes
 * (@c BSG_NODE_LOD_GROUP), separator roots, and other structural nodes
 * that own their own lifetime.  The returned node is independent of the
 * @c bsg_view / @c bsg_scene allocation pools.
 *
 * @param type_flags  One or more @c BSG_NODE_* flags describing the node.
 * @return Newly allocated node, or NULL on allocation failure.
 *         Free with @c bsg_node_free().
 */
BSG_EXPORT bsg_shape *bsg_node_alloc(int type_flags);

/**
 * @brief Free a standalone node previously created with @c bsg_node_alloc().
 *
 * Fires @c s_free_callback (if set), clears children, fires sensors, then
 * releases memory.  Does NOT recurse into children automatically; callers
 * that own a tree must traverse and free children themselves, or set
 * @c recurse to non-zero to recursively free the entire sub-tree.
 *
 * @param s       Node to free (NULL is a no-op).
 * @param recurse If non-zero, recursively free all children first.
 */
BSG_EXPORT void bsg_node_free(bsg_shape *s, int recurse);

/* ====================================================================== *
 * Phase 2: camera node (Issue 1)                                        *
 * ====================================================================== */

/**
 * @brief Allocate a camera node (@c BSG_NODE_CAMERA) pre-populated from
 *        an existing view.
 *
 * Equivalent to @c bsg_node_alloc(BSG_NODE_CAMERA) followed by
 * @c bsg_camera_node_set() from the current state of @p v.
 * If @p v is NULL the camera fields are zeroed.
 *
 * @return New camera node.  Free with @c bsg_node_free().
 */
BSG_EXPORT bsg_shape *bsg_camera_node_alloc(const bsg_view *v);

/**
 * @brief Copy camera data @p cam into a camera node @p node.
 *
 * @p node must have @c BSG_NODE_CAMERA set in @c s_type_flags.
 */
BSG_EXPORT void bsg_camera_node_set(bsg_shape *node,
				    const struct bsg_camera *cam);

/**
 * @brief Extract camera data from camera node @p node into @p out.
 *
 * @p node must have @c BSG_NODE_CAMERA set in @c s_type_flags.
 * Returns 0 on success, -1 if @p node is not a camera node.
 */
BSG_EXPORT int bsg_camera_node_get(const bsg_shape *node,
				   struct bsg_camera *out);

/**
 * @brief Write camera data from @p cam back into the legacy @c bsg_view.
 *
 * This is the inverse of @c bsg_view_get_camera().  It allows code that
 * manipulates a camera node to propagate changes back to the @c bsg_view
 * so that @c bsg_view_update() and existing render code continue to work.
 *
 * @param v   Destination view (must not be NULL).
 * @param cam Source camera data (must not be NULL).
 */
BSG_EXPORT void bsg_view_set_camera(bsg_view *v, const struct bsg_camera *cam);

/* ====================================================================== *
 * Phase 2: per-view scene root (Issue 2 / 3)                            *
 * ====================================================================== */

/**
 * @brief Return the scene-graph root node for view @p v, or NULL if none
 *        has been set.
 *
 * The scene root is the Inventor-style root @c SoSeparator for this view.
 * Geometry is added as children of the root; a camera node is typically
 * the first child.  The root is NOT automatically created — call
 * @c bsg_scene_root_set() or @c bsg_scene_root_create() to establish one.
 */
BSG_EXPORT bsg_shape *bsg_scene_root_get(const bsg_view *v);

/**
 * @brief Set the scene-graph root node for view @p v.
 *
 * Passing @p root == NULL removes any existing root association without
 * freeing the root node.  The caller retains ownership of @p root and is
 * responsible for freeing it with @c bsg_node_free().
 */
BSG_EXPORT void bsg_scene_root_set(bsg_view *v, bsg_shape *root);

/**
 * @brief Allocate and set a fresh @c BSG_NODE_SEPARATOR scene root for view
 *        @p v, pre-populated with a camera node derived from the view's
 *        current camera state.
 *
 * Returns the new root node.  The view takes logical association with the
 * root but the caller is still responsible for freeing it when the view is
 * torn down.  If @p v already has a scene root this call replaces the
 * association (the old root is NOT freed).
 */
BSG_EXPORT bsg_shape *bsg_scene_root_create(bsg_view *v);

/* ====================================================================== *
 * Phase 2: view traversal (Issue 3)                                     *
 * ====================================================================== */

/**
 * @brief Traverse the scene graph for view @p v from its scene root.
 *
 * Convenience wrapper around @c bsg_traverse() that:
 *   1. Retrieves the scene root via @c bsg_scene_root_get().
 *   2. Initialises a @c bsg_traversal_state and sets @c state.view = v.
 *   3. Calls @c bsg_traverse() on the root.
 *
 * If the view has no scene root this function is a no-op.
 *
 * @param v         View to traverse.
 * @param visit     Visitor callback (pre-order); return non-zero to prune.
 * @param user_data Opaque pointer forwarded to @p visit.
 */
BSG_EXPORT void bsg_view_traverse(bsg_view *v,
				  int (*visit)(bsg_shape *,
					       const bsg_traversal_state *,
					       void *),
				  void *user_data);

/* ====================================================================== *
 * Phase 2: sensors (Issue 4)                                            *
 * ====================================================================== */

/**
 * @brief Register a change-notification sensor on node @p s.
 *
 * The @p callback is invoked every time @c bsg_shape_stale() marks @p s
 * as dirty.  The sensor fires AFTER the stale flag is set, so callbacks
 * may safely read or update the stale flag.
 *
 * Multiple sensors may be registered on the same node.
 *
 * @return An opaque non-zero handle that identifies this sensor
 *         registration.  Pass to @c bsg_shape_rm_sensor() to deregister.
 *         Returns 0 on failure.
 */
BSG_EXPORT unsigned long long bsg_shape_add_sensor(bsg_shape *s,
					void (*callback)(bsg_shape *, void *),
					void *data);

/**
 * @brief Remove a previously registered sensor from node @p s.
 *
 * @param s      Node from which to remove the sensor.
 * @param handle Handle returned by @c bsg_shape_add_sensor().
 * @return 0 if the sensor was found and removed, -1 if not found.
 */
BSG_EXPORT int bsg_shape_rm_sensor(bsg_shape *s, unsigned long long handle);

/* ====================================================================== *
 * Phase 2: LoD group node (Issue 5)                                     *
 * ====================================================================== */

/**
 * @brief Allocate a @c BSG_NODE_LOD_GROUP node.
 *
 * @p switch_distances must have @p (num_levels - 1) entries.  Entry @p n
 * is the eye-to-center distance (in model units) at which the renderer
 * switches from child @p n to the coarser child @p (n+1).
 *
 * Children should be added after creation with @c bsg_shape_get_child()
 * or by inserting into @c node->children, in highest-to-lowest detail
 * order (child[0] = most detailed).
 *
 * @param num_levels       Number of detail levels (must be >= 1).
 * @param switch_distances Array of @p (num_levels - 1) distances; may be
 *                         NULL when @p num_levels == 1.
 * @return New LoD group node.  Free with @c bsg_node_free(node, 1).
 */
BSG_EXPORT bsg_shape *bsg_lod_group_alloc(int num_levels,
					  const fastf_t *switch_distances);

/**
 * @brief Given a viewer distance and an LoD group node, return the child
 *        index that should be rendered.
 *
 * Returns 0 (highest detail) when no @p view is available or the node
 * has no switch distances.  Returns -1 if @p node is not a
 * @c BSG_NODE_LOD_GROUP node.
 *
 * @param node             An LoD group node.
 * @param viewer_distance  Distance from the eye to the node's center.
 * @return Child index in @c [0, num_levels - 1], or -1 on error.
 */
BSG_EXPORT int bsg_lod_group_select_child(const bsg_shape *node,
					  fastf_t viewer_distance);

/* ====================================================================== *
 * Logging / debug                                                         *
 * ====================================================================== */
#define BSG_ENABLE_ENV_LOGGING 1

/** @brief Conditionally log a debug message (set BSG_LOG env var for level). */
BSG_EXPORT void bsg_log(int level, const char *fmt, ...) _BU_ATTR_PRINTF23;

/** @brief Print view contents for debugging.  Replaces bv_view_print(). */
BSG_EXPORT void bsg_view_print(const char *title, bsg_view *v,
       int verbosity);

__END_DECLS

/** @} */

#endif /* BSG_UTIL_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
