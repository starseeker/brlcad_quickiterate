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
 *   - one subgroup per drawn path (BSG_NODE_GROUP)
 *   - each subgroup holds BSG_NODE_SHAPE leaves in a bu_ptbl children list
 *
 * Group iteration: bsg_view_obj_first_group / bsg_view_obj_next_group
 * Shape iteration per group: BU_PTBL_LEN / BU_PTBL_GET on bsg_view_obj_group_solid_list()
 */
/** @{ */
/* @file ged/bsg_view_obj.h */

#ifndef GED_BSG_VIEW_OBJ_H
#define GED_BSG_VIEW_OBJ_H

#include "common.h"

#include "vmath.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "ged/defines.h"

__BEGIN_DECLS

/**
 * Look up a drawn path on @p gedp's active view set, or insert a new
 * top-level scene-group entry for it if not already present.  Returns
 * an opaque handle (a ged_scene_group *) usable as the
 * insertion-point for child scene objects.  Returns NULL if the leaf
 * directory entry does not exist or any argument is NULL.
 *
 * Replaces dl_addToDisplay().
 */
GED_EXPORT extern void *
bsg_view_obj_lookup_or_add_path(struct ged *gedp, const char *path);

/**
 * Erase from @p gedp's drawn-object set every entry whose path string
 * matches @p path exactly.  When @p allow_split is non-zero and the
 * scene-group's path is a strict ancestor of one of its children, the
 * group is split so that only the matching subpath is erased.
 *
 * Replaces dl_erasePathFromDisplay().
 */
GED_EXPORT extern void
bsg_view_obj_erase_by_path(struct ged *gedp, const char *path,
			   int allow_split);

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
 * has @p path as a prefix subset.  When @p skip_first is non-zero, the
 * first component of @p path is not required to match.
 *
 * Replaces _dl_eraseAllPathsFromDisplay().
 */
GED_EXPORT extern void
bsg_view_obj_erase_all_paths(struct ged *gedp, const char *path,
			     int skip_first);

/**
 * Compute the axis-aligned bounding box of every drawn scene object in
 * @p gedp's active view set.  When @p pflag is zero, pseudo-solids
 * (those whose first directory entry has d_addr == RT_DIR_PHONY_ADDR)
 * are excluded.  Returns 1 if the result is empty (no contributing
 * objects), 0 otherwise.
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
 * Refresh per-object base color from the dbip's region/material table
 * (mater_struct chain) for every drawn scene object.
 *
 * Replaces dl_color_soltab().
 */
GED_EXPORT extern void
bsg_view_obj_color_from_soltab(struct ged *gedp);

/**
 * Insert a "phony" pseudo-solid with the given vlist as a top-level
 * drawn entry.  If @p copy is non-zero the vlist is copied; otherwise
 * @p vhead is consumed (re-INIT'd).  @p rgb encodes the wireframe
 * color (0xRRGGBB), @p transparency is in [0,1], @p dmode is the draw
 * mode, and @p csoltab when non-zero applies soltab-based recoloring
 * after insertion.  Returns 0 on success, -1 on failure (e.g. name
 * collides with a real database entry).
 *
 * Replaces invent_solid().
 */
GED_EXPORT extern int
bsg_view_obj_invent(struct ged *gedp, char *name, struct bu_list *vhead,
		    long int rgb, int copy, fastf_t transparency,
		    int dmode, int csoltab);

/**
 * Compute a content-derived hash over the current drawn-object set's
 * path namespace.  Returns 0 when the set is empty.  Used by tooling
 * that needs to detect "what is drawn" changes cheaply.
 *
 * Replaces dl_name_hash().
 */
GED_EXPORT extern unsigned long long
bsg_view_obj_name_hash(struct ged *gedp);

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
 * Returns the next drawn solid after @p sp in display order, wrapping
 * circularly from the last solid back to the first.  @p sp must be a
 * currently-drawn solid.  Returns NULL only when no solids are drawn.
 *
 * Replaces f_aip() forward cross-list navigation.
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_next_solid(struct ged *gedp, struct bv_scene_obj *sp);

/**
 * Returns the previous drawn solid before @p sp in display order,
 * wrapping circularly from the first solid back to the last.  @p sp
 * must be a currently-drawn solid.  Returns NULL only when no solids
 * are drawn.
 *
 * Replaces f_aip() backward cross-list navigation.
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_prev_solid(struct ged *gedp, struct bv_scene_obj *sp);

/**
 * Returns the scene group (opaque handle; ged_scene_group *) that
 * contains @p sp, or NULL if @p sp is not
 * a member of any current group.
 *
 * Used to update MGED's illum_gdlp after finding the illuminated solid
 * via bsg_view_obj_foreach_solid() or bsg_view_obj_first_solid().
 */
GED_EXPORT extern void *
bsg_view_obj_group_of_solid(struct ged *gedp, struct bv_scene_obj *sp);

/**
 * Iterate over scene groups, calling @p cb(group_handle,
 * userdata) for each group.  @p cb returns 0 to stop iteration early.
 * The @p group_handle argument to @p cb is an opaque pointer
 * (ged_scene_group *) usable with
 * bsg_view_obj_group_first_solid(), bsg_view_obj_group_last_solid(),
 * bsg_view_obj_group_is_nonempty(), bsg_view_obj_group_solid_list(),
 * and bsg_view_obj_append_to_last_group().
 *
 * Replaces the outer for-gdlp loop in sites where per-group identity
 * matters (e.g. set.c OpenGL DList range freeing).
 */
GED_EXPORT extern void
bsg_view_obj_foreach_group(struct ged *gedp,
			   int (*cb)(void *group_handle, void *userdata),
			   void *userdata);

/**
 * Returns the first drawn scene object in a group returned by
 * bsg_view_obj_foreach_group(), or NULL if the group is empty.
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_group_first_solid(void *group_handle);

/**
 * Returns the last drawn scene object in a group returned by
 * bsg_view_obj_foreach_group(), or NULL if the group is empty.
 */
GED_EXPORT extern struct bv_scene_obj *
bsg_view_obj_group_last_solid(void *group_handle);

/**
 * Returns 1 if a group returned by bsg_view_obj_foreach_group() has
 * at least one drawn solid, 0 otherwise.
 */
GED_EXPORT extern int
bsg_view_obj_group_is_nonempty(void *group_handle);

/**
 * Returns a pointer to the bu_ptbl children list for a group returned
 * by bsg_view_obj_foreach_group().  Items are accessed via
 * BU_PTBL_LEN() / BU_PTBL_GET(), replacing BU_LIST_FOR loops.
 * Returns NULL if @p group_handle is NULL.
 */
GED_EXPORT extern struct bu_ptbl *
bsg_view_obj_group_solid_list(void *group_handle);

/**
 * Returns the path string associated with a group returned by
 * bsg_view_obj_foreach_group(), or NULL if @p group_handle is NULL.
 * The pointer is valid only for the lifetime of the group; callers
 * that need to retain it across draw modifications should copy.
 *
 * Replaces direct reads of gdlp->dl_path.
 */
GED_EXPORT extern const char *
bsg_view_obj_group_path(void *group_handle);

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
 * Set or update the path string associated with a group returned by
 * bsg_view_obj_foreach_group().  @p group_handle must be a valid group
 * and @p new_path must be non-NULL.
 *
 * Replaces direct writes to gdlp->dl_path via bu_vls_free/bu_vls_printf.
 */
GED_EXPORT extern void
bsg_view_obj_group_set_path(void *group_handle, const char *new_path);

/**
 * Returns 1 if the group is a pseudo-solid (phony), 0 otherwise.
 * A phony group has d_addr == RT_DIR_PHONY_ADDR for its directory entry.
 *
 * Replaces direct checks of ((struct directory *)gdlp->dl_dp)->d_addr ==
 * RT_DIR_PHONY_ADDR.
 */
GED_EXPORT extern int
bsg_view_obj_group_is_phony(void *group_handle);

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
 * Return a handle to the first group in @p gedp's draw set, or NULL
 * if the draw set is empty.  Used together with bsg_view_obj_next_group()
 * for simple for-loop iteration over all groups without a callback:
 *
 *   for (void *g = bsg_view_obj_first_group(gedp); g;
 *        g = bsg_view_obj_next_group(gedp, g)) { ... }
 */
GED_EXPORT extern void *
bsg_view_obj_first_group(struct ged *gedp);

/**
 * Return the group handle that follows @p group_handle in @p gedp's
 * draw set, or NULL when @p group_handle is the last group.
 */
GED_EXPORT extern void *
bsg_view_obj_next_group(struct ged *gedp, void *group_handle);

/**
 * Append @p sp to the solid list of the specific group @p group_handle.
 * Unlike bsg_view_obj_append_to_last_group(), this targets an arbitrary
 * group rather than always using the last one.  Used by the parallel
 * drawing path in draw.c / bigE.c where the group was looked up earlier.
 */
GED_EXPORT extern void
bsg_view_obj_append_solid_to_group(void *group_handle,
				   struct bv_scene_obj *sp);

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
