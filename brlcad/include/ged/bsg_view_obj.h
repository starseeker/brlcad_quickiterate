/*                B S G _ V I E W _ O B J . H
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
/** @addtogroup ged_view
 *
 * @brief
 * BSG view-object query API — BSG_NODE_GROUP tree backed.
 *
 * The drawn set is a BSG_NODE_GROUP tree rooted at gd_draw_root:
 *   - one subgroup per drawn path (BSG_NODE_GROUP), typed as struct bv_scene_obj *
 *   - each subgroup holds BSG_NODE_SHAPE leaves in a bu_ptbl children list
 *
 * Group iteration: bsg_view_obj_foreach_group(gedp, cb, userdata)
 *   where cb receives a struct bv_scene_obj * group directly.
 * Shape iteration per group: BU_PTBL_LEN / BU_PTBL_GET on &group->children
 */
/** @{ */
/* @file ged/bsg_view_obj.h */

#ifndef GED_BSG_VIEW_OBJ_H
#define GED_BSG_VIEW_OBJ_H

#include "common.h"

#include "vmath.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bsg/visit.h"
#include "ged/defines.h"

__BEGIN_DECLS

/**
 * Ensure the per-GED draw root exists (idempotent; safe to call at any time,
 * including before the first draw command).  Returns the root node or NULL
 * if no active view is configured.  Called automatically during ged_open so
 * that GED_CHECK_DRAWABLE always succeeds after initialization.
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_ensure_root(struct ged *gedp);

/**
 * Returns the BSG draw root node (BSG_NODE_GROUP) for @p gedp, or NULL if
 * no objects have been drawn yet.  Use together with bsg_visit() to iterate
 * all drawn shapes without going through the per-group wrapper API.
 *
 * The root node itself has BSG_NODE_GROUP set; its first-level children are
 * per-path subgroups (also BSG_NODE_GROUP); their children are the drawn
 * solid/shape nodes (BSG_NODE_SHAPE).
 *
 * Example — visit every drawn solid:
 * @code
 *   bsg_visit(bsg_view_obj_root(gedp), BSG_NODE_SHAPE, my_cb, userdata);
 * @endcode
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_root(struct ged *gedp);

/**
 * Look up a drawn path on @p gedp's active view set, or insert a new
 * top-level scene-group entry for it if not already present.  Returns
 * an opaque handle (a ged_scene_group *) usable as the
 * insertion-point for child scene objects.  Returns NULL if the leaf
 * directory entry does not exist or any argument is NULL.
 *
 * Replaces dl_addToDisplay().
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_lookup_or_add_path(struct ged *gedp, const char *path);

/**
 * Erase from @p gedp's drawn-object set every entry whose path string
 * matches @p path exactly.  When the scene-group's path is a strict
 * ancestor of the erase path, the matching sub-tree is removed
 * without disturbing sibling sub-groups.
 *
 * Replaces dl_erasePathFromDisplay().
 */
GED_EXPORT extern void
bsg_view_obj_erase_by_path(struct ged *gedp, const char *path);

/**
 * Erase from @p gedp's drawn-object set every scene object whose path
 * contains @p name as one of its directory components.  When
 * @p skip_first is non-zero, the first (top-level) component of each
 * path is excluded from the match.
 *
 * Replaces _dl_eraseAllNamesFromDisplay().
 */
GED_EXPORT extern void
bsg_view_obj_erase_by_name(struct ged *gedp, const char *name,
			   int skip_first);

/**
 * Erase from @p gedp's drawn-object set every scene object whose path
 * has @p path as a prefix subset.
 *
 * Replaces _dl_eraseAllPathsFromDisplay().
 */
GED_EXPORT extern void
bsg_view_obj_erase_all_paths(struct ged *gedp, const char *path);

/**
 * Compute the axis-aligned bounding box of every drawn scene object in
 * @p gedp's active view set.  When @p pflag is zero, overlay shapes
 * (those with BSG_PAYLOAD_OVERLAY set in s_type_flags) are excluded.
 * Returns 1 if the result is empty (no contributing objects), 0 otherwise.
 *
 * Replaces dl_bounding_sph().
 */
GED_EXPORT extern int
bsg_view_obj_bounds(struct ged *gedp, vect_t *min, vect_t *max,
		    int pflag);

/**
 * Set the s_iflag field on every drawn scene object to @p iflag (UP or
 * DOWN).  Used by edit-mode and illumination paths to clear or assert
 * highlights across the whole drawn set.
 *
 * Replaces dl_set_iflag().
 */
GED_EXPORT extern void
bsg_view_obj_set_iflag(struct ged *gedp, int iflag);

/**
 * Register @p sp as the single currently-illuminated solid (B5).
 * Clears any previously registered solid's s_iflag to DOWN first.
 * Pass NULL to deregister (signals that set_iflag(DOWN) must fall back
 * to the O(N) sweep because multiple solids may be in the UP state).
 *
 * Call this after any operation that illuminates exactly one solid so
 * that the subsequent bsg_view_obj_set_iflag(gedp, DOWN) can run in O(1)
 * instead of sweeping the whole draw tree.
 */
GED_EXPORT extern void
bsg_view_obj_set_illum(struct ged *gedp, struct bv_scene_obj *sp);

/**
 * Return the currently-tracked illuminated solid, or NULL when none is
 * registered or tracking has been invalidated.
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_get_illum(const struct ged *gedp);

/**
 * Return the mater-revision counter (B4).  The counter is incremented by
 * bsg_view_obj_bump_mater_rev() whenever the material/color table changes.
 * bsg_view_obj_color_from_soltab() does NOT increment the counter; it only
 * stamps per-shape s_color_rev fields.  Callers that cache per-solid color
 * data can compare a saved snapshot to detect when a recolor sweep is needed.
 */
GED_EXPORT extern uint64_t
bsg_view_obj_mater_rev(const struct ged *gedp);

/**
 * Bump the mater-revision counter (B4 activation).
 *
 * Call this after any operation that changes the effective material or
 * color table (e.g. 'color', 'mater', 'rmater', 'edmater') so that the
 * next bsg_view_obj_color_from_soltab() call recolors shapes that are
 * now stale (s_color_rev < gd_mater_rev).
 */
GED_EXPORT extern void
bsg_view_obj_bump_mater_rev(struct ged *gedp);

/**
 * Refresh per-object base color from the dbip's region/material table
 * (mater_struct chain) for every drawn scene object whose s_color_rev is
 * stale (i.e. less than the current gd_mater_rev counter).  Shapes that
 * were already colored since the last material-change event are skipped.
 *
 * Callers must invoke bsg_view_obj_bump_mater_rev() before this function
 * to signal that a material change occurred; without that bump, successive
 * calls will skip all already-stamped shapes.
 *
 * Replaces dl_color_soltab().
 */
GED_EXPORT extern void
bsg_view_obj_color_from_soltab(struct ged *gedp);

/**
 * Insert a pseudo-solid overlay with the given vlist.  The overlay is placed
 * into the `_overlays` BSG_NODE_GROUP under the scene root and tagged with
 * BSG_PAYLOAD_OVERLAY — no phony database directory entry is created.  If
 * @p copy is non-zero the vlist is copied; otherwise @p vhead is consumed
 * (re-INIT'd).  @p rgb encodes the wireframe color (0xRRGGBB),
 * @p transparency is in [0,1], @p dmode is the draw mode, and @p csoltab
 * when non-zero applies soltab-based recoloring after insertion.  Returns 0
 * on success, -1 on failure (e.g. name collides with a real database entry).
 *
 * Replaces invent_solid().
 */
GED_EXPORT extern int
bsg_view_obj_invent(struct ged *gedp, char *name, struct bu_list *vhead,
		    long int rgb, int copy, fastf_t transparency,
		    int dmode, int csoltab);

/**
 * Compute a content-derived hash over the current drawn-object set's
 * path namespace.  Returns 0 when the set is empty or after a zap.
 * Uses a structural revision counter (O(1)) rather than computing a
 * content hash, so two calls may return equal values only when no
 * structural mutation has occurred between them.
 *
 * Replaces dl_name_hash().
 */
GED_EXPORT extern unsigned long long
bsg_view_obj_name_hash(struct ged *gedp);

/**
 * Return the raw structural revision counter for @p gedp's draw tree.
 * The counter is incremented on every structural mutation (group/shape
 * addition or removal) and reset to 0 by bsg_view_obj_zap().  Returns
 * 0 when no objects have been drawn since the last zap (or ever).
 *
 * Callers that need to detect "drawn set changed" cheaply should compare
 * snapshots of this value rather than recomputing a content hash.
 */
GED_EXPORT extern uint64_t
bsg_view_obj_draw_rev(struct ged *gedp);

/**
 * Iterate over every drawn scene object in display order, calling
 * @p cb(sp, userdata) for each.  Iteration stops early when @p cb
 * returns 0.
 *
 * Replaces the two-level "for each gdlp → for each sp in
 * gdlp->dl_head_scene_obj" idiom used throughout MGED.
 */
GED_EXPORT extern void
bsg_view_obj_foreach_solid(struct ged *gedp,
			   int (*cb)(struct bv_scene_obj *sp, void *userdata),
			   void *userdata);

/**
 * Returns 1 if @p gedp has at least one display list containing at
 * least one drawn solid, 0 otherwise.
 *
 * Replaces: loop-over-gdlp checking BU_LIST_NON_EMPTY(&gdlp->dl_head_scene_obj).
 */
GED_EXPORT extern int
bsg_view_obj_is_nonempty(struct ged *gedp);

/**
 * Returns the first drawn scene object in display order, or NULL if
 * none are drawn.
 *
 * Replaces: find first non-empty gdlp, then BU_LIST_NEXT(bv_scene_obj,
 * &gdlp->dl_head_scene_obj).
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_first_solid(struct ged *gedp);

/**
 * Returns the total number of non-overlay drawn solids (i.e. excludes
 * shapes in the _overlays group).  Builds a DFS snapshot internally
 * and frees it before returning; O(N) per call.
 */
GED_EXPORT extern int
bsg_view_obj_solid_count(struct ged *gedp);

/**
 * Returns the drawn solid at position @p idx in DFS snapshot order
 * (overlay shapes excluded).  @p idx is wrapped modulo the total
 * solid count, so negative indices and out-of-range indices are both
 * handled safely.  Returns NULL when no non-overlay solids are drawn.
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_solid_at(struct ged *gedp, int idx);

/**
 * Returns the DFS snapshot index of @p target among non-overlay drawn
 * solids, or -1 if @p target is not currently drawn (or is an overlay).
 *
 * Together with bsg_view_obj_solid_at() this allows callers to express
 * "advance illuminated solid by N" purely as integer arithmetic without
 * holding any internal iterator state.
 */
GED_EXPORT extern int
bsg_view_obj_solid_index(struct ged *gedp, struct bv_scene_obj *target);

/**
 * Advance @p sp by @p delta positions in DFS snapshot order (positive =
 * forward, negative = backward), wrapping circularly.  Overlay shapes
 * are excluded from the index.  Builds a single DFS snapshot internally.
 *
 * Replaces the pair of bsg_view_obj_next_solid / bsg_view_obj_prev_solid
 * calls inside MGED's f_aip(); direct callers of those functions can
 * migrate to this single API to avoid repeated snapshot construction.
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_advance_solid(struct ged *gedp, struct bv_scene_obj *sp, int delta);

/**
 * Returns the next drawn solid after @p sp in display order, wrapping
 * circularly from the last solid back to the first.  @p sp must be a
 * currently-drawn non-overlay solid.  Returns NULL only when no solids
 * are drawn.
 *
 * Delegates to bsg_view_obj_advance_solid(gedp, sp, +1).
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_next_solid(struct ged *gedp, struct bv_scene_obj *sp);

/**
 * Returns the previous drawn solid before @p sp in display order,
 * wrapping circularly from the first solid back to the last.  @p sp
 * must be a currently-drawn non-overlay solid.  Returns NULL only when
 * no solids are drawn.
 *
 * Delegates to bsg_view_obj_advance_solid(gedp, sp, -1).
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_prev_solid(struct ged *gedp, struct bv_scene_obj *sp);

/**
 * Returns the scene group (struct bv_scene_obj *) that contains @p sp,
 * or NULL if @p sp is not a member of any current group.
 *
 * Used to update MGED's illum_gdlp after finding the illuminated solid
 * via bsg_view_obj_foreach_solid() or bsg_view_obj_first_solid().
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_group_of_solid(struct ged *gedp, struct bv_scene_obj *sp);

/**
 * Iterate over scene groups, calling @p cb(group, userdata) for each
 * group.  @p cb returns 0 to stop iteration early.  The @p group
 * argument to @p cb is a struct bv_scene_obj * usable with
 * bsg_view_obj_group_first_solid(), bsg_view_obj_group_last_solid(),
 * bsg_view_obj_group_is_nonempty(), and bsg_view_obj_append_solid_to_group().
 *
 * Replaces the outer for-gdlp loop in sites where per-group identity
 * matters (e.g. set.c OpenGL DList range freeing).
 */
GED_EXPORT extern void
bsg_view_obj_foreach_group(struct ged *gedp,
			   int (*cb)(struct bv_scene_obj *group, void *userdata),
			   void *userdata);

/**
 * Returns the first drawn scene object in a group, or NULL if the group is empty.
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_group_first_solid(struct bv_scene_obj *group);

/**
 * Returns the last drawn scene object in a group, or NULL if the group is empty.
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_group_last_solid(struct bv_scene_obj *group);

/**
 * Returns 1 if a group has at least one drawn solid, 0 otherwise.
 */
GED_EXPORT extern int
bsg_view_obj_group_is_nonempty(struct bv_scene_obj *group);

/**
 * Returns the path string associated with a group, or NULL if @p group
 * is NULL.  The pointer is valid only for the lifetime of the group;
 * callers that need to retain it across draw modifications should copy.
 *
 * Replaces direct reads of gdlp->dl_path.
 */
GED_EXPORT extern const char *
bsg_view_obj_group_path(struct bv_scene_obj *group);

/**
 * Append @p sp to the last display-list group in @p gedp's draw set.
 * Used by dodraw.c when inserting a newly computed solid into the
 * current draw operation.
 *
 * Replaces:
 *   gdlp = BU_LIST_PREV(display_list, (struct bu_list *)ged_dl(gedp));
 *   BU_LIST_APPEND(gdlp->dl_head_scene_obj.back, &sp->l);
 */
GED_EXPORT extern void
bsg_view_obj_append_to_last_group(struct ged *gedp, struct bv_scene_obj *sp);

/**
 * Set or update the path string associated with a group.  @p group
 * must be a valid group and @p new_path must be non-NULL.
 *
 * Replaces direct writes to gdlp->dl_path via bu_vls_free/bu_vls_printf.
 */
GED_EXPORT extern void
bsg_view_obj_group_set_path(struct bv_scene_obj *group, const char *new_path);

/**
 * Returns 1 if @p group is the synthetic _overlays group (which holds
 * overlay/invented shapes and should be excluded from "who" listings and
 * rt write commands), 0 for any real drawn-path group.
 *
 * Replaces direct checks of ((struct directory *)gdlp->dl_dp)->d_addr ==
 * RT_DIR_PHONY_ADDR.
 */
GED_EXPORT extern int
bsg_view_obj_group_is_phony(struct bv_scene_obj *group);

/**
 * Erase all display-list groups from @p gedp's drawn-object set,
 * destroying vlists and freeing all associated scene objects.
 * This is the "zap" operation.
 */
GED_EXPORT extern void
bsg_view_obj_zap(struct ged *gedp);

/**
 * Return 1 if any display-list groups exist (i.e., something is drawn),
 * 0 if the display is empty.
 */
GED_EXPORT extern int
bsg_view_obj_has_groups(struct ged *gedp);

/**
 * Append @p sp to the solid list of the specific group @p group,
 * creating nested sub-group nodes as needed based on @p sp's db_full_path.
 * Unlike bsg_view_obj_append_to_last_group(), this targets an arbitrary
 * group rather than always using the last one.  Used by the parallel
 * drawing path in draw.c / bigE.c where the group was looked up earlier.
 *
 * @p gedp is required when @p sp has a db_full_path deeper than the current
 * group depth, so that intermediate sub-group nodes can be created.
 */
GED_EXPORT extern void
bsg_view_obj_append_solid_to_group(struct ged *gedp,
				   struct bv_scene_obj *group,
				   struct bv_scene_obj *sp);

/**
 * Per-solid s_free_callback that clears the GED illumination tracker
 * (gd_illum_solid) when the shape being freed is currently registered as the
 * illuminated solid.
 *
 * Register this on every BSG_NODE_SHAPE node at creation time alongside
 * setting ged_bv_data::gedp.  The BSG freeing paths call it explicitly before
 * FREE_BV_SCENE_OBJ; bv_free() calls it again during pool teardown, but the
 * second call is a safe no-op (Phase 7 Step 9).
 */
GED_EXPORT extern void ged_bv_illum_free_cb(struct bv_scene_obj *sp);

__END_DECLS

#endif /* GED_BSG_VIEW_OBJ_H */

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
