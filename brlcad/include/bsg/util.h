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
