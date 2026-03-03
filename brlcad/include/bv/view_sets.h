/*                      V I E W _ S E T S . H
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
/** @addtogroup bv_util
 *
 * In applications with multiple views, those views typically share common
 * scene objects and memory.  To manage this sharing, we define view sets.
 */
/** @{ */
/** @file bv/view_sets.h */

#ifndef BV_VIEW_SETS_H
#define BV_VIEW_SETS_H

#include "common.h"
#include "bv/defines.h"

__BEGIN_DECLS

/**
 * Initialize an empty view set.
 * DEPRECATED: use bv_viewset_init() in new code.
 */
DEPRECATED BV_EXPORT void
bv_set_init(struct bview_set *s);

/**
 * Free view set.
 * DEPRECATED: use bv_viewset_free() in new code.
 */
DEPRECATED BV_EXPORT void
bv_set_free(struct bview_set *s);

/**
 * Add view v to set s, handling shared memory assignments.
 * DEPRECATED: use bv_viewset_add() in new code.
 */
DEPRECATED BV_EXPORT void
bv_set_add_view(struct bview_set *s, struct bview *v);

/**
 * Remove view v from set s.  If v == NULL, all views
 * are removed from the set.
 * DEPRECATED: use bv_viewset_rm() in new code.
 */
DEPRECATED BV_EXPORT void
bv_set_rm_view(struct bview_set *s, struct bview *v);

/**
 * Return a bu_ptbl holding pointers to all views in set s.
 * DEPRECATED: use bv_viewset_views() in new code.
 */
DEPRECATED BV_EXPORT struct bu_ptbl *
bv_set_views(struct bview_set *s);

/**
 * Return a pointer to the view with name vname, if it is present in s.
 * DEPRECATED: use bv_viewset_find() in new code.
 */
DEPRECATED BV_EXPORT struct bview *
bv_set_find_view(struct bview_set *s, const char *vname);

/** Expose free_scene_obj for older codes.
 * DEPRECATED: use bv_viewset_fso() in new code.
 */
DEPRECATED BV_EXPORT struct bv_scene_obj *
bv_set_fsos(struct bview_set *s);


/*
 * Non-deprecated replacements for the bv_set_* family.
 * These have the same behavior but are not marked deprecated, allowing
 * incremental migration away from the old naming convention.
 */

/** Initialize an empty view set. */
BV_EXPORT void bv_viewset_init(struct bview_set *s);

/** Free view set resources. */
BV_EXPORT void bv_viewset_free(struct bview_set *s);

/** Add view v to set s. */
BV_EXPORT void bv_viewset_add(struct bview_set *s, struct bview *v);

/** Remove view v from set s (NULL removes all). */
BV_EXPORT void bv_viewset_rm(struct bview_set *s, struct bview *v);

/** Return a bu_ptbl of all bview pointers in set s. */
BV_EXPORT struct bu_ptbl *bv_viewset_views(struct bview_set *s);

/** Return the view with name vname, or NULL if not found. */
BV_EXPORT struct bview *bv_viewset_find(struct bview_set *s, const char *vname);

/** Return the head of the free-scene-object pool for set s. */
BV_EXPORT struct bv_scene_obj *bv_viewset_fso(struct bview_set *s);


__END_DECLS

/** @} */

#endif /* BV_VIEW_SETS_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
