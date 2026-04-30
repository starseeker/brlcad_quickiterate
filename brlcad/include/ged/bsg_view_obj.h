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
 * BSG view-object query API.
 *
 * These helpers are the migration target for the legacy
 * src/libged/display_list.c routines that walk the
 * gedp->i->ged_gdp->gd_headDisplay chain.  The drawing-stack
 * modernization plan (Phase 6.5) calls for a phased excision of
 * display_list.c; new and migrated callers should use this API instead
 * of the dl_* / _dl_* / invent_solid functions.
 *
 * The current implementation is a thin wrapper around the legacy
 * dl_* functions.  This is deliberate: it lets caller migration (Step 2
 * of the plan) proceed against a stable API surface while the
 * implementation is still backed by the well-tested legacy code.  Once
 * every caller has migrated, the implementation will be replaced with a
 * pure BSG view-tree walk and display_list.c will be deleted (Step 7).
 *
 * Migration map (see doc/notes/drawing_stack_modernization.txt
 * Phase 6.5):
 *
 *   dl_addToDisplay              -> bsg_view_obj_lookup_or_add_path
 *   dl_erasePathFromDisplay      -> bsg_view_obj_erase_by_path
 *   _dl_eraseAllNamesFromDisplay -> bsg_view_obj_erase_by_name
 *   _dl_eraseAllPathsFromDisplay -> bsg_view_obj_erase_all_paths
 *   dl_bounding_sph              -> bsg_view_obj_bounds
 *   dl_color_soltab              -> bsg_view_obj_color_from_soltab
 *   invent_solid                 -> bsg_view_obj_invent
 *   dl_set_iflag                 -> bsg_view_obj_set_iflag
 *   dl_name_hash                 -> bsg_view_obj_name_hash
 */
/** @{ */
/* @file ged/bsg_view_obj.h */

#ifndef GED_BSG_VIEW_OBJ_H
#define GED_BSG_VIEW_OBJ_H

#include "common.h"

#include "vmath.h"
#include "bu/list.h"
#include "ged/defines.h"

__BEGIN_DECLS

/**
 * Look up a drawn path on @p gedp's active view set, or insert a new
 * top-level scene-group entry for it if not already present.  Returns
 * an opaque handle (currently a struct display_list *) usable as the
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
