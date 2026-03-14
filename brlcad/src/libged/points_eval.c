/*                    P O I N T S _ E V A L . C
 * BRL-CAD
 *
 * Copyright (c) 1997-2025 United States Government as represented by
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
/** @addtogroup libged */
/** @{ */
/** @file libged/draw/points_eval.c
 *
 * DEPRECATED — no longer compiled as part of libged (removed from
 * CMakeLists.txt 2026-03-14).  Preserved for reference only.
 *
 * Point-cloud visualization (mode 5, "draw -m5") has been migrated to
 * rt_generic_scene_obj() in src/librt/primitives/generic.c, which calls
 * rt_sample_pnts() directly in its mode-5 switch case.  draw_scene()
 * dispatches via OBJ[dp->d_minor_type].ft_scene_obj() for all types;
 * there is no separate draw_points() call-site remaining in BRL-CAD.
 */
/** @} */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "vmath.h"
#include "rt/geom.h"
#include "raytrace.h"
#include "analyze.h"

#include "./ged_private.h"

int
draw_points(bsg_shape *s)
{
    if (!s)
	return BRLCAD_OK; /* nothing to do is fine */

    struct draw_update_data_t *d = (struct draw_update_data_t *)s->s_i_data;
    if (!d)
	return BRLCAD_OK; /* nothing to do is fine */

    struct db_full_path *fp = (struct db_full_path *)s->s_path;
    struct directory *dp = (fp) ? DB_FULL_PATH_CUR_DIR(fp) : (struct directory *)s->dp;
    if (!dp)
	return BRLCAD_OK; /* nothing to do is fine */

    struct rt_db_internal intern;
    if (rt_db_get_internal(&intern, dp, d->dbip, NULL, &rt_uniresource) < 0)
	return BRLCAD_ERROR;

    int ret = rt_sample_pnts(s, &intern);

    rt_db_free_internal(&intern);

    return ret;
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
