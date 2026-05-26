/*                    D B I _ S T A T E . H
 * BRL-CAD
 *
 * Copyright (c) 2024-2026 United States Government as represented by
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
/** @addtogroup ged_defines
 *
 * Thin C interface to the DbiState / BViewState path-drawn machinery.
 *
 * `DbiState` and `BViewState` are C++ classes that live inside libged.
 * This header exposes the subset of their behaviour that is generally
 * useful to C callers (MGED, Archer, libged commands) without requiring
 * those callers to include the private `dbi.h` C++ header.
 *
 * All entry points check `gedp->dbi_state` at the top.  When that
 * pointer is NULL (i.e. the database was opened without the new-command
 * forms machinery, as MGED does), the calls are no-ops that return safe
 * zero/empty values.  C++ callers that need the full `DbiState` API
 * continue to use `src/libged/dbi.h` directly.
 */
/** @{ */
/** @file ged/dbi_state.h */

#ifndef GED_DBI_STATE_H
#define GED_DBI_STATE_H

#include "common.h"
#include "bu/vls.h"
#include "bsg/defines.h"
#include "ged/defines.h"

__BEGIN_DECLS

/**
 * Report whether the path @p path is drawn in view @p v.
 *
 * @param gedp   GED instance that owns the drawn-set state.
 * @param v      The view to query.  When NULL the shared view state
 *               owned by `gedp->dbi_state` is queried instead.
 * @param path   Slash-separated path string (e.g. "component/region1").
 *               A leading '/' is tolerated and ignored.
 *
 * @return
 *   - 0  path is not drawn
 *   - 1  path is fully drawn (all children are visible)
 *   - 2  path is partially drawn (at least one child is visible)
 *   - 0  when `gedp->dbi_state` is NULL (no DbiState machinery)
 */
GED_EXPORT int ged_dbi_is_drawn(struct ged *gedp, struct bsg_view *v, const char *path);


/**
 * Append the list of currently drawn paths to @p result.
 *
 * Each path is written as a null-terminated string followed by a
 * newline character.  Paths are not sorted.
 *
 * @param gedp    GED instance that owns the drawn-set state.
 * @param v       The view to query.  When NULL the shared view state
 *                owned by `gedp->dbi_state` is queried.
 * @param mode    Draw mode to list.  Pass -1 to list all draw modes.
 * @param result  Output vls; drawn paths are appended (not reset).
 *
 * @return  Number of path strings appended, or 0 when `dbi_state` is NULL.
 */
GED_EXPORT size_t ged_dbi_list_drawn(struct ged *gedp, struct bsg_view *v, int mode, struct bu_vls *result);

__END_DECLS

#endif /* GED_DBI_STATE_H */

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
