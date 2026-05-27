/*                         U T I L . H
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
 * Scene-graph lifecycle and query utilities.
 */
/** @{ */
/* @file bsg/util.h */

#ifndef BSG_UTIL_H
#define BSG_UTIL_H

#include "common.h"
#include "bu/hash.h"
#include "bn/tol.h"
#include "bsg/defines.h"

__BEGIN_DECLS

/**
 * Allocate and initialize a scene-graph root node for view @p v.
 * Stores the result in @p v->bsg_root and returns it.  Returns NULL
 * if @p v is NULL or allocation fails.
 */
BSG_EXPORT extern bsg_node *
bsg_scene_root_create(struct bsg_view *v);

/**
 * Synchronize the children list of @p root from the current draw state
 * of @p v.  The root's children table is cleared (pointers only, the
 * actual scene objects are owned by the view) and refilled from all
 * BSG_OBJ_DB and BSG_OBJ_VIEW tables accessible through @p v.
 *
 * This is the "shim" that mirrors the existing display-list contents
 * into the BSG tree (Phase 4-D).
 */
BSG_EXPORT extern void
bsg_scene_root_sync(bsg_node *root, struct bsg_view *v);

/**
 * Destroy a scene root previously created by bsg_scene_root_create().
 * The root's children are NOT freed (they are borrowed references
 * owned by the view).  Only the root node itself is released.
 * Also clears @p v->bsg_root if @p root matches it.
 */
BSG_EXPORT extern void
bsg_scene_root_destroy(bsg_node *root);

/**
 * Return the first child of @p root whose s_type_flags field has all
 * bits in @p flags set.  Returns NULL if no match is found or if either
 * argument is NULL.  Searches one level deep (direct children only).
 */
BSG_EXPORT extern bsg_node *
bsg_view_find_by_type(bsg_node *root, unsigned long long flags);

/**
 * Fire sensor callbacks on all nodes in the subtree rooted at @p root
 * whose type includes BSG_NODE_SENSOR.  @p v is passed to each
 * callback as context.  No-op when @p root is NULL.
 */
BSG_EXPORT extern void
bsg_sensor_fire(bsg_node *root, struct bsg_view *v);

/**
 * Allocate and initialize a scene-graph node using the BSG lifecycle API.
 *
 * The @p type flags use the BSG_OBJ_* storage aliases while the bsg_view
 * storage model is being migrated into libbsg.
 */
BSG_EXPORT extern bsg_node *
bsg_obj_create(struct bsg_view *v, int type);

/**
 * Allocate a scene-graph object without registering it in the view's legacy
 * flat object tables.
 */
BSG_EXPORT extern bsg_node *
bsg_obj_get_unregistered(struct bsg_view *v, int type);

/**
 * Recycle a scene-graph node allocated by bsg_obj_create() or
 * bsg_obj_get_unregistered().
 */
BSG_EXPORT extern void
bsg_obj_put(bsg_node *obj);

/**
 * Release backend-owned renderer state associated with @p obj.
 */
BSG_EXPORT extern void
bsg_scene_obj_release_backend(bsg_node *obj);

/**
 * Mark backend-owned renderer state associated with @p obj stale.
 */
BSG_EXPORT extern void
bsg_scene_obj_invalidate_backend(bsg_node *obj);

/**
 * Initialize a view object using the BSG namespace.  BSG wrapper around
 * bsg_init().  @p v must point to allocated but uninitialized storage.
 * @p s is the optional view-set the view belongs to; pass NULL when unused.
 */
BSG_EXPORT extern void
bsg_view_init(struct bsg_view *v, struct bsg_view_set *s);

/**
 * Free resources owned by a view object.  BSG wrapper around bsg_free().
 * Does not free the memory for @p v itself.
 */
BSG_EXPORT extern void
bsg_view_free(struct bsg_view *v);

/**
 * Duplicate the contents of a vlist.  BSG-namespaced wrapper around
 * bsg_vlist_copy().  @p vlists is the free-list pool; @p dest is cleared
 * and filled from @p src.
 */
BSG_EXPORT extern void
bsg_vlist_copy(struct bu_list *vlists,
               struct bu_list *dest,
               const struct bu_list *src);

/**
 * Return the table of views registered in the view-set @p s.
 * BSG-namespaced wrapper around bsg_set_views().
 * Returns NULL when @p s is NULL.
 */
BSG_EXPORT extern struct bu_ptbl *
bsg_set_views(struct bsg_view_set *s);

__END_DECLS

/* =========================================================================
 * Legacy bv_* API declarations
 *
 * The following declarations were previously in bv/util.h.  bv/util.h is
 * now a backward-compatibility bridge that includes this header.  All
 * callers should migrate to bsg/util.h.  The bv_* names will be retired
 * once the struct renames (bsg_view → bsg_view, etc.) are complete.
 * ========================================================================= */

__BEGIN_DECLS

/* Set default values for a bv. */
BV_EXPORT extern void bsg_init(struct bsg_view *v, struct bsg_view_set *s);
BV_EXPORT extern void bsg_free(struct bsg_view *v);

/* Phase T3 (drawing_stack_modernization): zero-initialize a bsg_data_tclcad
 * block. */
BV_EXPORT extern void bsg_data_tclcad_init(struct bsg_data_tclcad *d);
BV_EXPORT extern int bsg_view_is_independent(const struct bsg_view *v);
BV_EXPORT extern struct bsg_node *bsg_view_independent_scope(struct bsg_view *v, int create);
BV_EXPORT extern void bsg_view_independent_scope_destroy(struct bsg_view *v);

BV_EXPORT void bsg_mat_aet(struct bsg_view *v);

BV_EXPORT extern void bsg_settings_init(struct bsg_view_settings *s);

#define BV_AUTOVIEW_SCALE_DEFAULT -1
BV_EXPORT extern void bsg_autoview(struct bsg_view *v, fastf_t scale, int all_view_objs);

/* Copy the size and camera info */
BV_EXPORT extern void bsg_sync(struct bsg_view *dest, struct bsg_view *src);

/* Camera accessor functions */
BV_EXPORT extern fastf_t bsg_view_get_scale(const struct bsg_view *v);
BV_EXPORT extern void    bsg_view_set_scale(struct bsg_view *v, fastf_t scale);
BV_EXPORT extern fastf_t bsg_view_get_size(const struct bsg_view *v);
BV_EXPORT extern void    bsg_view_set_size(struct bsg_view *v, fastf_t size);
BV_EXPORT extern fastf_t bsg_view_get_perspective(const struct bsg_view *v);
BV_EXPORT extern void    bsg_view_set_perspective(struct bsg_view *v, fastf_t perspective);
BV_EXPORT extern void bsg_view_get_aet(const struct bsg_view *v, vect_t aet);
BV_EXPORT extern void bsg_view_set_aet(struct bsg_view *v, const vect_t aet);
BV_EXPORT extern void bsg_view_get_rotation(const struct bsg_view *v, mat_t rot);
BV_EXPORT extern void bsg_view_set_rotation(struct bsg_view *v, const mat_t rot);
BV_EXPORT extern void bsg_view_get_center_vec(const struct bsg_view *v, point_t center);
BV_EXPORT extern void bsg_view_set_center_vec(struct bsg_view *v, const point_t center);

/* Copy settings common to views and shape nodes */
BV_EXPORT extern int bsg_obj_settings_sync(struct bsg_obj_settings *dest, struct bsg_obj_settings *src);

/* Sync values within the bv, perform callbacks if any are defined */
BV_EXPORT extern void bsg_update(struct bsg_view *gvp);

/* Update objects in the selection set (if any) and their children */
BV_EXPORT extern int bsg_update_selected(struct bsg_view *gvp);

/* Clear or reset the knob states. */
#ifndef BV_KNOBS_ALL
#define BV_KNOBS_ALL 0
#define BV_KNOBS_RATE 1
#define BV_KNOBS_ABS 2
#endif
BV_EXPORT extern void bsg_knobs_reset(struct bsg_view_knobs *k, int category);
BV_EXPORT extern unsigned long long bsg_knobs_hash(struct bsg_view_knobs *k, struct bu_data_hash_state *state);
BV_EXPORT extern int bsg_knobs_cmd_process(vect_t *rvec, int *do_rot, vect_t *tvec, int *do_tran, struct bsg_view *v, const char *cmd, fastf_t f, char origin, int model_flag, int incr_flag);
BV_EXPORT extern void bsg_knobs_rot(struct bsg_view *v, const vect_t rvec, char origin, char coords, const matp_t obj_rot, const pointp_t pvt_pt);
BV_EXPORT extern void bsg_knobs_tran(struct bsg_view *v, const vect_t tvec, int model_flag);
BV_EXPORT extern void bsg_update_rate_flags(struct bsg_view *v);

/* Comparison and hash */
BV_EXPORT extern int bsg_differ(struct bsg_view *v1, struct bsg_view *v2);
BV_EXPORT extern unsigned long long bsg_hash(struct bsg_view *v);
BV_EXPORT extern size_t bsg_clear(struct bsg_view *v, int flags);

/* Mouse/view coordinate utilities */
#ifndef BV_IDLE
#define BV_IDLE       0x000
#define BV_ROT        0x001
#define BV_TRANS      0x002
#define BV_SCALE      0x004
#define BV_CENTER     0x008
#define BV_CON_X      0x010
#define BV_CON_Y      0x020
#define BV_CON_Z      0x040
#define BV_CON_GRID   0x080
#define BV_CON_LINES  0x100
#endif
BV_EXPORT extern int bsg_adjust(struct bsg_view *v, int dx, int dy, point_t keypoint, int mode, unsigned long long flags);
BV_EXPORT extern int bsg_screen_to_view(struct bsg_view *v, fastf_t *fx, fastf_t *fy, fastf_t x, fastf_t y);
BV_EXPORT extern int bsg_screen_pt(point_t *p, fastf_t x, fastf_t y, struct bsg_view *v);

/* Shape-node bounds and utilities */
BV_EXPORT extern int bsg_scene_obj_bound(struct bsg_node *s, struct bsg_view *v);
BV_EXPORT extern fastf_t bsg_vZ_calc(struct bsg_node *s, struct bsg_view *v, int mode);
BV_EXPORT extern void bsg_obj_sync(struct bsg_node *dest, struct bsg_node *src);
BV_EXPORT void bsg_obj_stale(struct bsg_node *s);

/* Backend contract helpers */
BV_EXPORT void bsg_scene_obj_release_backend(struct bsg_node *s);
BV_EXPORT void bsg_scene_obj_invalidate_backend(struct bsg_node *s);

/* Scene-graph node lifecycle */
BV_EXPORT struct bsg_node *bsg_obj_create(struct bsg_view *v, int type);
BV_EXPORT struct bsg_node *bsg_obj_get(struct bsg_view *v, int type);
BV_EXPORT struct bsg_node *bsg_obj_get_unregistered(struct bsg_view *v, int type);

/* View-only object API */
struct bsg_view_obj_opts {
    int local;
    int arrow;
};
#ifndef BV_VIEW_OBJ_OPTS_INIT
#define BV_VIEW_OBJ_OPTS_INIT {0, 0}
#endif
BV_EXPORT struct bsg_node *bsg_view_obj_create(struct bsg_view *v, const char *name, unsigned long long type_flags, const struct bsg_view_obj_opts *opts);
BV_EXPORT struct bsg_node *bsg_view_obj_axes_create(struct bsg_view *v, const char *name, int local);
BV_EXPORT struct bsg_node *bsg_view_obj_lines_create(struct bsg_view *v, const char *name, int local);
BV_EXPORT struct bsg_node *bsg_view_obj_label_create(struct bsg_view *v, const char *name, int local);
BV_EXPORT struct bsg_node *bsg_view_obj_arrow_create(struct bsg_view *v, const char *name, int local);
BV_EXPORT struct bsg_node *bsg_view_obj_overlay_create(struct bsg_view *v, const char *name, int local);
BV_EXPORT struct bsg_node *bsg_view_obj_polygon_create(struct bsg_view *v, const char *name, int local);

#ifndef BV_VIEW_OBJ_SCOPE_SHARED
#define BV_VIEW_OBJ_SCOPE_SHARED 0x1
#define BV_VIEW_OBJ_SCOPE_LOCAL  0x2
#define BV_VIEW_OBJ_SCOPE_ALL    (BV_VIEW_OBJ_SCOPE_SHARED | BV_VIEW_OBJ_SCOPE_LOCAL)
#endif
BV_EXPORT int bsg_view_obj_remove(struct bsg_view *v, const char *name);
BV_EXPORT size_t bsg_view_obj_remove_all(struct bsg_view *v, int scope);
BV_EXPORT struct bsg_node *bsg_view_obj_find(struct bsg_view *v, const char *name);
BV_EXPORT void bsg_view_obj_visit(struct bsg_view *v, int scope_mask, int (*cb)(struct bsg_node *obj, void *data), void *data);
BV_EXPORT void bsg_view_obj_labels_sync(struct bsg_view *v, struct bsg_data_label_state *gdlsp, const char *bsg_name);
BV_EXPORT void bsg_view_obj_set_color(struct bsg_node *s, int r, int g, int b);
BV_EXPORT void bsg_view_obj_set_line_width(struct bsg_node *s, int line_width);
BV_EXPORT void bsg_view_obj_set_visible(struct bsg_node *s, int visible);

/* Child objects */
BV_EXPORT struct bsg_node *bsg_obj_get_child(struct bsg_node *s);
BV_EXPORT void bsg_obj_reset(struct bsg_node *s);
BV_EXPORT void bsg_obj_put(struct bsg_node *o);

/* Object lookup */
BV_EXPORT struct bsg_node *bsg_find_child(struct bsg_node *s, const char *vname);
BV_EXPORT struct bsg_node *bsg_find_obj(struct bsg_view *v, const char *vname);
BV_EXPORT void bsg_uniq_obj_name(struct bu_vls *oname, const char *seed, struct bsg_view *v);
BV_EXPORT int bsg_illum_obj(struct bsg_node *s, char ill_state);
BV_EXPORT struct bu_ptbl *bsg_view_objs(struct bsg_view *v, int type);
BV_EXPORT void bsg_view_objs_visit_db(struct bsg_view *v, int (*cb)(struct bsg_node *obj, void *data), void *data);
BV_EXPORT int bsg_view_plane(plane_t *p, struct bsg_view *v);

/* Environment variable controlled logging */
#define BV_ENABLE_ENV_LOGGING 1
BV_EXPORT void bsg_log(int level, const char *fmt, ...)  _BU_ATTR_PRINTF23;

/* Debugging */
BV_EXPORT void bsg_view_print(const char *title, struct bsg_view *v, int verbosity);

__END_DECLS

#endif /* BSG_UTIL_H */

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
