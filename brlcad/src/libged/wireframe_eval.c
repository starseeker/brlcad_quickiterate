/*                 W I R E F R A M E _ E V A L . C
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
/** @file libged/draw/wireframe_eval.c
 *
 * DEPRECATED — no longer compiled as part of libged (removed from
 * CMakeLists.txt 2026-03-14).  Preserved for reference only.
 *
 * The evaluated wireframe (mode 3, "bigE") engine has been migrated to
 * librt as rt_comb_eval_m3() in src/librt/comb/comb_scene_obj.c, called
 * by rt_comb_scene_obj.  draw_scene() dispatches via
 * OBJ[dp->d_minor_type].ft_scene_obj() for all types including ID_COMBINATION;
 * there is no separate draw_m3() call-site remaining in BRL-CAD.
 */

#include "common.h"

#include "raytrace.h"
#include "bsg/defines.h"

#include "./ged_private.h"

/* Forward declaration of the librt implementation */
extern int rt_comb_eval_m3(bsg_shape *s,
   struct db_i *dbip,
   const struct bg_tess_tol *ttol,
   const struct bn_tol *tol);

int
draw_m3(bsg_shape *s)
{
    struct draw_update_data_t *d = (struct draw_update_data_t *)s->s_i_data;
    if (!d)
return BRLCAD_ERROR;
    return rt_comb_eval_m3(s, d->dbip, d->ttol, d->tol);
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
