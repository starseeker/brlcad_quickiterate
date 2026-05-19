/*                      B V I E W _ U T I L . H
 * BRL-CAD
 *
 * Copyright (c) 1993-2026 United States Government as represented by
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
/** @addtogroup bv_util
 *
 */
/** @{ */
/** @file bv/util.h */

#ifndef BV_UTIL_H
#define BV_UTIL_H

#include "common.h"
#include "bu/hash.h"
#include "bn/tol.h"
#include "dm/defines.h"
#include "bv/defines.h"

__BEGIN_DECLS

/* Set default values for a bv. */
BV_EXPORT extern void bv_init(struct bview *v, struct bview_set *s);
BV_EXPORT extern void bv_free(struct bview *v);

/* Phase T3 (drawing_stack_modernization): zero-initialize a bv_data_tclcad
 * block.  Called by libtclcad when creating a new Tcl-backed view; also used
 * by any consumer that allocates its own bv_data_tclcad. */
BV_EXPORT extern void bv_data_tclcad_init(struct bv_data_tclcad *d);
BV_EXPORT extern int bv_view_is_independent(const struct bview *v);
BV_EXPORT extern struct bv_scene_obj *bv_view_independent_scope(struct bview *v, int create);
BV_EXPORT extern void bv_view_independent_scope_destroy(struct bview *v);

/**
 * FIXME: this routine is suspect and needs investigating.  if run
 * during view initialization, the shaders regression test fails.
 */
BV_EXPORT void bv_mat_aet(struct bview *v);

BV_EXPORT extern void bv_settings_init(struct bview_settings *s);

/* To use default scaling (0.5 model scale == 2.0 view factor) use
 * this as an argument to bv_autoview's scale parameter */
#define BV_AUTOVIEW_SCALE_DEFAULT -1
/**
 * Automatically set up the view to make the scene objects visible
 */
BV_EXPORT extern void bv_autoview(struct bview *v, fastf_t scale, int all_view_objs);

/* Copy the size and camera info (deliberately not a full copy of all view state) */
BV_EXPORT extern void bv_sync(struct bview *dest, struct bview *src);



/* Camera accessor functions
 *
 * These replace direct writes to gv_scale / gv_size / gv_isize / gv_perspective /
 * gv_aet / gv_rotation / gv_center, ensuring derived fields are kept consistent
 * and view-policy decisions stay above the scene-data layer.
 *
 * Size and scale are linked: size == 2 * scale; isize == 1 / size.
 * Setters maintain all three automatically. */
BV_EXPORT extern fastf_t bv_view_get_scale(const struct bview *v);
BV_EXPORT extern void    bv_view_set_scale(struct bview *v, fastf_t scale);
BV_EXPORT extern fastf_t bv_view_get_size(const struct bview *v);
BV_EXPORT extern void    bv_view_set_size(struct bview *v, fastf_t size);

/* Perspective angle (degrees; 0 means orthographic). */
BV_EXPORT extern fastf_t bv_view_get_perspective(const struct bview *v);
BV_EXPORT extern void    bv_view_set_perspective(struct bview *v, fastf_t perspective);

/* Azimuth / Elevation / Twist.  bv_view_set_aet recomputes gv_rotation. */
BV_EXPORT extern void bv_view_get_aet(const struct bview *v, vect_t aet);
BV_EXPORT extern void bv_view_set_aet(struct bview *v, const vect_t aet);

/* Raw rotation matrix.  Prefer bv_view_set_aet when the AET representation is
 * available; use this accessor only when restoring saved matrix state (e.g.
 * loadview). */
BV_EXPORT extern void bv_view_get_rotation(const struct bview *v, mat_t rot);
BV_EXPORT extern void bv_view_set_rotation(struct bview *v, const mat_t rot);

/* View center expressed as a point (positive = model origin offset).
 * get extracts the translation from gv_center; set stores it via
 * MAT_DELTAS_VEC_NEG so gv_center holds the negated translation. */
BV_EXPORT extern void bv_view_get_center_vec(const struct bview *v, point_t center);
BV_EXPORT extern void bv_view_set_center_vec(struct bview *v, const point_t center);



/* Copy settings (potentially) common to the view and scene objects.
 * Return 0 if no changes were made to dest.  If dest did have one
 * or more settings updated from src, return 1. */
BV_EXPORT extern int bsg_settings_sync(struct bsg_settings *dest, struct bsg_settings *src);

/* Sync values within the bv, perform callbacks if any are defined */
BV_EXPORT extern void bv_update(struct bview *gvp);

/* Update objects in the selection set (if any) and their children */
BV_EXPORT extern int bv_update_selected(struct bview *gvp);

/* Clear or reset the knob states.  Specify a category to indicate which
 * variables should be reset:
 *
 * BV_KNOBS_ALL resets both rate and absolute values
 * BV_KNOBS_RATE resets rate only
 * BV_KNOBS_ABS resets absolute only
 */
#define BV_KNOBS_ALL 0
#define BV_KNOBS_RATE 1
#define BV_KNOBS_ABS 2
BV_EXPORT extern void bv_knobs_reset(struct bview_knobs *k, int category);

/* Hash the semantic (non-pointer) contents of a bview_knobs struct.  This
 * intentionally excludes any *_udata pointers to avoid pointer address noise
 * and skips any padding that would be present if hashing the raw struct.
 *
 * If 'state' is non-NULL the supplied hash state is updated in-place and the
 * function returns 0 (the caller is expected to finalize the hash later.)
 * If 'state' is NULL an internal hash state is created, populated and
 * finalized and the resulting hash value is returned.
 *
 * Returns:
 *   0     if k is NULL or state supplied (non-owning mode)
 *   hash  if state is NULL (owning mode)
 */
BV_EXPORT extern unsigned long long
bv_knobs_hash(struct bview_knobs *k, struct bu_data_hash_state *state);

/**
 * @brief
 * Process an individual libbv knob command.
 *
 * Note that the reason rvec, do_rot, tvec and do_tran are set, rather than an
 * immediate view update being performed, is to allow parent applications to
 * process multiple commands before finally triggering the bv_knobs_rot or
 * bv_knobs_tran functions to implement the accumulated instructions.
 *
 * @param[out] rvec     Pointer to rotation vector
 * @param[out] do_rot   Pointer to flag indicating whether the command implies a rotation op is needed
 * @param[out] tvec     Pointer to translation vector
 * @param[out] do_tran  Pointer to flag indicating whether the command implies a translation op is needed
 *
 * @param[in] v          bview structure
 * @param[in] cmd        command string - valid entries are x, y, z, X, Y Z, ax, ay, az, aX, aY, aZ, S, aS
 * @param[in] f          numerical parameter to cmd (i.e. aX 0.1 - required for all commands)
 * @param[in] origin     char indicating origin - may be 'e' (eye_pt), 'm' (model origin) or 'v' (view origin - default)
 * @param[in] model_flag Manipulate view using model coordinates rather than view coordinates
 * @param[in] incr_flag  Treat f parameter as an incremental change rather than an absolute setting
 *
 * @return
 * Returns BRLCAD_OK if command was successfully processed, BRLCAD_ERROR otherwise.
 * */
BV_EXPORT extern int bv_knobs_cmd_process(
	vect_t *rvec, int *do_rot, vect_t *tvec, int *do_tran,
        struct bview *v, const char *cmd, fastf_t f,
        char origin, int model_flag, int incr_flag
	);

/**
 * @brief
 * Rotate the view based on an Euler angle triplet (degrees) specified
 * in one of several coordinate frames, about one of several origins.
 *
 * coords:  Rotation input frame
 *   'v' - rvec in view coordinates
 *   'm' - rvec in model coordinates (converted via Rv * Rm * Rv^{-1})
 *   'o' - rvec in object coordinates (use obj_rot to map object->model->view,
 *         fallback to 'v' semantics if obj_rot is NULL)
 *
 * origin:  Rotation pivot specifier
 *   'v' : view center (0,0,0 in view space)
 *   'm' : model origin
 *   'e' : eye point (0,0,1 in view space)
 *   'k' : model-space custom pivot supplied via pvt_pt
 *   Any unrecognized value falls back to 'v'.
 *
 * obj_rot:
 *   Accumulated object->model rotation matrix when coords=='o'. NULL otherwise.
 *
 * pvt_pt:
 *   Model-space pivot point when origin=='k'. Ignored otherwise.
 *   NULL == model origin.
 *
 * BEHAVIOR
 * 1. rvec is converted into a pure view-space rotation matrix according
 *    to 'coords' (and obj_rot for 'o').
 * 2. If origin != 'v', the view center (gv_center) is relocated so the
 *    specified pivot is invariant under the applied rotation.
 * 3. gv_rotation is post-multiplied by the view rotation matrix.
 * 4. bv_update(v) refreshes derived matrices. Absolute translation bookkeeping
 *    (tra_v_abs / tra_m_abs) is always recomputed
 *
 * @param[in,out] v      target bview structure
 * @param[in] rvec       rotation vector (Euler angles, degrees) expressed in the coordinate frame indicated by coords.
 * @param[in] origin     char indicating origin - may be 'e' (eye_pt), 'm' (model origin), 'v' (view origin - default) or 'k' (keypoint)
 * @param[in] coords     coordinate frame - may be 'm' (model), 'o' (obj coords via obj_rot), or 'v' (view)
 * @param[in] obj_rot    pointer to accumulated object rotation matrix (may be NULL)
 * @param[in] pvt_pt     model space pivot point
 */
BV_EXPORT extern void
bv_knobs_rot(struct bview *v,
	const vect_t rvec,
	char origin,
	char coords,
	const matp_t obj_rot,
	const pointp_t pvt_pt);

/* @brief
 * Process a knob translation vector.
 *
 * @param[in] v          bview structure
 * @param[in] tvec      Pointer to translation vector
 * @param[in] model_flag Manipulate view using model coordinates rather than view coordinates
 */
BV_EXPORT extern void
bv_knobs_tran(struct bview *v,
	const vect_t tvec,
	int model_flag);


/* Update the bview struct's knob rate flags based on the vector values. */
BV_EXPORT extern void
bv_update_rate_flags(struct bview *v);


/* Return 1 if the visible contents differ
 * Return 2 if visible content is the same but settings differ
 * Return 3 if content is the same but user data, dmp or callbacks differ
 * Return -1 if one or more of the views is NULL
 * Else return 0 */
BV_EXPORT extern int bv_differ(struct bview *v1, struct bview *v2);

/* Return a hash of the contents of the bv container.  Returns 0 on failure. */
BV_EXPORT extern unsigned long long bv_hash(struct bview *v);

/* Returns number of objects defined in any object container
 * known to this view (0 if completely cleared). */
BV_EXPORT extern size_t bv_clear(struct bview *v, int flags);

/* Note that some of these are mutually exclusive as far as producing any
 * changes - a simultaneous constraint in X and Y, for example, results in a
 * no-op. */
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

/* Update a view in response to X,Y coordinate changes as generated
 * by a graphical interface's mouse motion. */
BV_EXPORT extern int bv_adjust(struct bview *v, int dx, int dy, point_t keypoint, int mode, unsigned long long flags);

/* Beginning extraction of the core of libtclcad view object manipulation
 * logic.  The following functions will initially be pretty straightforward
 * mappings from libtclcad, and will likely evolve over time.
 */

/* Return -1 if width and/or height are unset (and hence a meaningful
 * calculation is impossible), else 0. */
BV_EXPORT extern int bv_screen_to_view(struct bview *v, fastf_t *fx, fastf_t *fy, fastf_t x, fastf_t y);

/* Return -1 if width and/or height are unset (and hence a meaningful
 * calculation is impossible), else 0.
 *
 * x and y will normally be integers, but the types are float to allow for
 * the possibility of sub-pixel coordinate specifications.
 */
BV_EXPORT extern int bv_screen_pt(point_t *p, fastf_t x, fastf_t y, struct bview *v);



/* Compute the min, max, and center points of the scene object.
 * Return 1 if a bound was computed, else 0 */
BV_EXPORT extern int bv_scene_obj_bound(struct bv_scene_obj *s, struct bview *v);

/* Find the nearest (mode == 0) or farthest (mode == 1) data_vZ value from
 * the vlist points in s in the context of view v */
BV_EXPORT extern fastf_t bv_vZ_calc(struct bv_scene_obj *s, struct bview *v, int mode);

/* Copy object attributes (but not geometry) from src to dest */
BV_EXPORT extern void bv_obj_sync(struct bv_scene_obj *dest, struct bv_scene_obj *src);

/* Mark object and any child objects as stale for the drawing routines */
/* There are a few options for this situation - this one, which requires the client code
 * to explicitly notify the drawing routines they need to do work, an internal options
 * hash stored in the bv_scene_obj itself which is checked at render time, and setter
 * wrapper functions that do the bookkeeping for the caller (in lieu of directly setting
 * values in the bv_scene_obj struct.)  The first one isn't ideal because the visual will
 * be wrong if the caller doesn't supply the notification, the second has unknown
 * performance implications, and the third would be a major rework of how the bv_scene_obj
 * data is accessed (effectively, making the internal storage of bv_scene_obj fully hidden
 * a.l.a the libdm rework.)  Not sure what the best option is yet... leaning towards #2
 * if it is "fast enough"... */
BV_EXPORT void bv_obj_stale(struct bv_scene_obj *s);

/* Phase 11 (drawing_stack_modernization): renderer-backend contract helpers.
 *
 * These are the canonical entry points for releasing/invalidating per-shape
 * backend resources.
 *
 * bv_scene_obj_release_backend  - shape is being destroyed/recycled; fires
 *   the backend free callback (if set) and clears s_backend.  Safe to call
 *   when s_backend is NULL.
 * bv_scene_obj_invalidate_backend - cached backend resource is stale and
 *   needs to be regenerated.  Fires the backend invalidate callback if set.
 *   Does NOT recurse into children. */
BV_EXPORT void bv_scene_obj_release_backend(struct bv_scene_obj *s);
BV_EXPORT void bv_scene_obj_invalidate_backend(struct bv_scene_obj *s);


/* Given a view, create an object of the specified type.  Like bv_obj_get, except it
 * leaves the addition of objects to the client.  Lower level. */
BV_EXPORT struct bv_scene_obj *
bv_obj_create(struct bview *v, int type);

/* Given a view, create an object of the specified type and add it to the
 * appropriate container.  Issues such as memory management as a function of
 * view settings are handled internally, so client codes don't need to manage
 * it. */
BV_EXPORT struct bv_scene_obj *
bv_obj_get(struct bview *v, int type);

/* Like bv_obj_get, but does NOT register the object in any gv_objs ptbl.
 * Use this when the object will be owned and indexed by an external structure
 * (e.g. the BSG draw tree) so that ptbl-based iterators do not double-count
 * it.  The object type flags are set as requested; lifecycle management
 * (bv_obj_put) works normally. */
BV_EXPORT struct bv_scene_obj *
bv_obj_get_unregistered(struct bview *v, int type);

/* Create and attach view-only objects directly under BSG view-scope nodes.
 * This is the preferred API family for view-only producers. */
struct bv_view_obj_opts {
    int local;
    int arrow;
};
/* C99/C++ aggregate initializer for bv_view_obj_opts. */
#define BV_VIEW_OBJ_OPTS_INIT {0, 0}

/* Optional hook for assigning identity metadata when a view-only object is
 * created via bv_view_obj_create().  This lets higher layers (e.g. libbsg)
 * derive BSG identity without introducing a libbv->libbsg dependency. */
typedef void (*bv_view_obj_identity_hook_t)(struct bv_scene_obj *obj,
					    struct bview *v,
					    struct bv_scene_obj *scope,
					    const char *name,
					    int local,
					    int name_ordinal);
BV_EXPORT void
bv_view_obj_identity_hook_set(bv_view_obj_identity_hook_t hook);

/* Optional hooks for typed view-object setters.  These allow higher layers
 * (e.g. libbsg) to route typed setter requests through BSG material/
 * appearance APIs without introducing a libbv->libbsg dependency.  Hook
 * callbacks return non-zero when they handled the request. */
typedef int (*bv_view_obj_color_hook_t)(struct bv_scene_obj *obj,
					unsigned char r,
					unsigned char g,
					unsigned char b);
BV_EXPORT void
bv_view_obj_color_hook_set(bv_view_obj_color_hook_t hook);

typedef int (*bv_view_obj_line_width_hook_t)(struct bv_scene_obj *obj,
					     int line_width);
BV_EXPORT void
bv_view_obj_line_width_hook_set(bv_view_obj_line_width_hook_t hook);

BV_EXPORT struct bv_scene_obj *
bv_view_obj_create(struct bview *v, const char *name, unsigned long long type_flags, const struct bv_view_obj_opts *opts);
BV_EXPORT struct bv_scene_obj *
bv_view_obj_axes_create(struct bview *v, const char *name, int local);
BV_EXPORT struct bv_scene_obj *
bv_view_obj_lines_create(struct bview *v, const char *name, int local);
BV_EXPORT struct bv_scene_obj *
bv_view_obj_label_create(struct bview *v, const char *name, int local);
BV_EXPORT struct bv_scene_obj *
bv_view_obj_arrow_create(struct bview *v, const char *name, int local);
BV_EXPORT struct bv_scene_obj *
bv_view_obj_overlay_create(struct bview *v, const char *name, int local);

/** Create a view-only polygon container under the BSG view scope for @p v.
 * Equivalent to bv_view_obj_overlay_create but marks the object with
 * BV_VIEWONLY so downstream renderers treat it as a polygon carrier. */
BV_EXPORT struct bv_scene_obj *
bv_view_obj_polygon_create(struct bview *v, const char *name, int local);

#define BV_VIEW_OBJ_SCOPE_SHARED 0x1
#define BV_VIEW_OBJ_SCOPE_LOCAL  0x2
#define BV_VIEW_OBJ_SCOPE_ALL    (BV_VIEW_OBJ_SCOPE_SHARED | BV_VIEW_OBJ_SCOPE_LOCAL)
BV_EXPORT int
bv_view_obj_remove(struct bview *v, const char *name);
BV_EXPORT size_t
bv_view_obj_remove_all(struct bview *v, int scope);
BV_EXPORT struct bv_scene_obj *
bv_view_obj_find(struct bview *v, const char *name);
/* Visit view-only objects visible in @p v for the requested scope mask.
 * Valid scope_mask flags are BV_VIEW_OBJ_SCOPE_SHARED,
 * BV_VIEW_OBJ_SCOPE_LOCAL, and BV_VIEW_OBJ_SCOPE_ALL.
 * Callback signature is cb(struct bv_scene_obj *obj, void *data).
 * Callback contract: return non-zero to continue iteration, return 0 to stop. */
BV_EXPORT void
bv_view_obj_visit(struct bview *v,
		  int scope_mask,
		  int (*cb)(struct bv_scene_obj *obj, void *data),
		  void *data);

/* Phase T3 (drawing_stack_modernization): BSG-backed label sync helper.
 * Replaces the deprecated dm_draw_labels() direct-render path.  Removes any
 * existing BSG VIEW_SCOPE object named @p bsg_name, then – if gdlsp->gdls_draw
 * is set and there are labels – creates a new VIEW_SCOPE container under @p v
 * with one BV_LABELS child per label entry.  The result is picked up by
 * dm_draw_objs() on the next frame without any direct dm_* calls.
 * External consumers that previously called dm_draw_labels() should call this
 * instead.
 */
BV_EXPORT void
bv_view_obj_labels_sync(struct bview *v,
                        struct bv_data_label_state *gdlsp,
                        const char *bsg_name);

/* Phase A3 (drawing_stack_modernization): typed setters for view-only object
 * properties.  These mutate fields that BSG-resident view-only objects expose
 * and stale the object so the next frame picks up the change.  They are the
 * forcing-function API for Phase T1's Tcl-adornment migration: any new
 * adornment producer should call into these rather than poking
 * bv_scene_obj fields directly.
 *
 * - bv_view_obj_set_color: set RGB color (0-255 per channel).  Sets the
 *   per-shape color override path (s_color).
 * - bv_view_obj_set_line_width: set the wireframe line width in pixels.
 * - bv_view_obj_set_visible: set the per-shape visibility / force-draw flag.
 *   When 0, the object is skipped during BSG traversal; when 1, it draws
 *   regardless of inherited s_flag state.
 *
 * All setters are no-ops on NULL objects.  All setters call bv_obj_stale(s)
 * so dependent backend caches are invalidated. */
BV_EXPORT void
bv_view_obj_set_color(struct bv_scene_obj *s, int r, int g, int b);
BV_EXPORT void
bv_view_obj_set_line_width(struct bv_scene_obj *s, int line_width);
BV_EXPORT void
bv_view_obj_set_visible(struct bv_scene_obj *s, int visible);

/* Given an object, create an object that is a child of that object.  Issues
 * such as memory management as a function of view settings are handled
 * internally, so client codes don't need to manage it. */
BV_EXPORT struct bv_scene_obj *
bv_obj_get_child(struct bv_scene_obj *s);

/* Clear the contents of an object (including releasing its children), but keep
 * it active in the view.  Generally used when redrawing an object */
BV_EXPORT void
bv_obj_reset(struct bv_scene_obj *s);

/* Release an object to the internal pools. */
BV_EXPORT void
bv_obj_put(struct bv_scene_obj *o);

/**
 * Return the embedded @c struct bsg_node pointer for @p s.
 *
 * Because @c struct bsg_node is the first member of @c struct bv_scene_obj
 * (Phase 10E first-member embedding), this is simply a pointer cast.  The
 * helper is provided as an explicit, typed accessor so that call sites are
 * self-documenting and to avoid accidental raw casts.
 */
static inline struct bsg_node *
bv_obj_bsg_node(struct bv_scene_obj *s)
{
    return s ? &s->bsg : NULL;
}

/* Given a scene object and a name vname, glob match child names and uuids to
 * attempt to locate a child of s that matches vname */
BV_EXPORT struct bv_scene_obj *
bv_find_child(struct bv_scene_obj *s, const char *vname);

/* Given a view and a name vname, glob match names and uuids to attempt to
 * locate a scene object in v that matches vname.
 *
 * NOTE - currently this is searching the top level objects, but does not walk
 * down into their children.  May want to support that in the future... */
BV_EXPORT struct bv_scene_obj *
bv_find_obj(struct bview *v, const char *vname);

/* Given a seed name, generate a name that does not collide with any existing
 * object names in the top level.  If the seed name does not collide, it is
 * returned as the result - otherwise, a name based on the seed name will be
 * generated.
 */
BV_EXPORT void
bv_uniq_obj_name(struct bu_vls *oname, const char *seed, struct bview *v);

/* Set the illumination state on the object and its children to ill_state.
 * Returns 0 if no states were changed, and 1 if one or more states were
 * updated. */
BV_EXPORT int
bv_illum_obj(struct bv_scene_obj *s, char ill_state);

/* For the given view, return a pointer to the bu_ptbl holding active scene
 * objects with the specified type.  Note that view-specific db objects are not
 * part of these sets.
 *
 * Valid type flags: BV_DB_OBJS, BV_DB_OBJS|BV_LOCAL_OBJS.
 * BV_VIEW_OBJS queries are no longer supported (Phase D,
 * drawing_stack_modernization); use bv_view_obj_visit instead.
 */
BV_EXPORT struct bu_ptbl *
bv_view_objs(struct bview *v, int type);

/* Iterate all database-derived shape leaves visible to @p v, invoking
 * @p cb(obj, data) for each one.  Return 0 from @p cb to stop early.
 *
 * When @p v->gv_draw_root is non-NULL (GED consumers with the BSG draw tree),
 * the tree is traversed depth-first and the callback is invoked for every node
 * whose s_type_flags has BV_DB_OBJS set.
 *
 * When @p v->gv_draw_root is NULL (non-GED / legacy consumers), the callback
 * is invoked for every object in bv_view_objs(v, BV_DB_OBJS) and
 * bv_view_objs(v, BV_DB_OBJS | BV_LOCAL_OBJS), preserving the previous flat
 * ptbl semantics.
 *
 * Phase B (drawing_stack_modernization): this is the single migration target
 * for all callers that previously read bv_view_objs(v, BV_DB_OBJS) directly.
 */
BV_EXPORT void
bv_view_objs_visit_db(struct bview *v,
		      int (*cb)(struct bv_scene_obj *obj, void *data),
		      void *data);

/* Given a view, construct the view plane */
BV_EXPORT int
bv_view_plane(plane_t *p, struct bview *v);


/* Environment variable controlled logging.
 *
 * Set BV_LOG to numerical levels to get increasingly
 * verbose reporting of drawing info */
#define BV_ENABLE_ENV_LOGGING 1
BV_EXPORT void
bv_log(int level, const char *fmt, ...)  _BU_ATTR_PRINTF23;


/* Debugging function for printing contents of views */
BV_EXPORT void
bv_view_print(const char *title, struct bview *v, int verbosity);

__END_DECLS

/** @} */

#endif /* BV_UTIL_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
