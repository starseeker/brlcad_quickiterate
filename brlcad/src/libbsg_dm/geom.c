/*                      G E O M . C
 * BRL-CAD
 *
 * Copyright (c) 2024-2025 United States Government as represented by
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
/**
 * @file libbsg_dm/geom.c
 *
 * @brief Geometry ingestion — convert @c rt_db_internal → @c libbsg_shape.
 *
 * Implements Step 6 of the libbsg migration plan.  Each BRL-CAD
 * primitive has an @c ft_plot() entry in the @c OBJ[] functab that
 * generates a wireframe vlist from an @c rt_db_internal.  This file
 * wraps that call so that the result lands in a @c libbsg_shape's vlist
 * chain rather than a legacy @c bsg_shape.
 *
 * ### Default tolerances
 *
 * If the caller passes NULL for @p ttol or @p tol, sensible defaults
 * are used:
 *   - @c ttol: absolute 0.0, relative 0.01, normalise 1.0.
 *   - @c tol:  dist 0.0005 mm, perp 1e-6.
 */

#include "common.h"

#include <string.h>   /* memset */

#include "libbsg/libbsg.h"
#include "libbsg_dm/libbsg_dm.h"

/* Pull in librt headers for rt_db_internal, OBJ[], bg_tess_tol, bn_tol. */
#include "raytrace.h"
#include "rt/functab.h"
#include "bg/defines.h"

/* ====================================================================== *
 * Implementation                                                          *
 * ====================================================================== */

int
libbsg_shape_wireframe(libbsg_shape             *s,
		       struct rt_db_internal    *ip,
		       const struct bg_tess_tol *ttol,
		       const struct bn_tol      *tol)
{
    struct bg_tess_tol default_ttol;
    struct bn_tol      default_tol;

    if (!s || !ip)
	return -1;

    /* Fill in defaults if tolerances are not provided. */
    if (!ttol) {
	memset(&default_ttol, 0, sizeof(default_ttol));
	default_ttol.magic  = BG_TESS_TOL_MAGIC;
	default_ttol.abs    = 0.0;
	default_ttol.rel    = 0.01;
	default_ttol.norm   = 0.0;
	ttol = &default_ttol;
    }
    if (!tol) {
	memset(&default_tol, 0, sizeof(default_tol));
	default_tol.magic = BN_TOL_MAGIC;
	default_tol.dist  = 0.0005;
	default_tol.dist_sq = default_tol.dist * default_tol.dist;
	default_tol.perp    = 1e-6;
	default_tol.para    = 1.0 - default_tol.perp;
	tol = &default_tol;
    }

    /* Look up the functab entry for this primitive type. */
    {
	const struct rt_functab *ftp;
	int ret;

	if (ip->idb_type >= ID_MAXIMUM)
	    return -1;

	ftp = ip->idb_meth;
	if (!ftp || !ftp->ft_plot)
	    return -1;

	/* ft_plot(vhead, ip, ttol, tol, view)
	 * We pass &s->vlist as the vlist head.  ft_plot appends BV_VLIST_*
	 * segments to it.  The last parameter (bsg_view *) is NULL because
	 * we are not performing adaptive tessellation here. */
	ret = ftp->ft_plot(&s->vlist, ip, ttol, tol, NULL /* no view */);

	if (ret < 0)
	    return -1;
    }

    /* Update bounding sphere now that geometry has been added. */
    if (BU_LIST_NON_EMPTY(&s->vlist)) {
	/* Simple centroid estimate: walk the vlist and find the first
	 * MOVE point, use it as an approximation of the centre.  A more
	 * accurate implementation can call bv_scene_obj_bound() if the
	 * caller later wraps this shape in a bsg_view context. */
	struct bv_vlist *vp;
	int found = 0;
	for (BU_LIST_FOR(vp, bv_vlist, &s->vlist)) {
	    size_t k;
	    for (k = 0; k < vp->nused; k++) {
		if (vp->cmd[k] == BV_VLIST_LINE_MOVE ||
		    vp->cmd[k] == BV_VLIST_POLY_MOVE) {
		    VMOVE(s->s_center, vp->pt[k]);
		    s->s_size  = 1.0;
		    s->have_bbox = 1;
		    found = 1;
		    break;
		}
	    }
	    if (found) break;
	}
    }

    return 0;
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
