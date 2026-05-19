/*                         V I E W . H
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
/** @addtogroup libdm */
/** @{ */
/** @file dm/view.h
 *
 * dm routines related to view management (migrated from libtclcad.) These may
 * ultimately take a different form or move elsewhere - the immediate idea here
 * is to extract the key logic from libtclcad for use in non-Tcl environments.
 *
 */

#include "common.h"

#include "vmath.h"

#include "bu/hash.h"
#include "bu/vls.h"
#include "bv/defines.h"
#include "bsg/settings.h"
#include "dm/defines.h"

#ifndef DM_VIEW_H
#define DM_VIEW_H

__BEGIN_DECLS

struct dm_path_edit_params {
    int edit_mode;
    double dx;
    double dy;
    mat_t edit_mat;
};

struct dm_view_data {
    struct bu_hash_tbl  *edited_paths;
    struct bu_vls       *prim_label_list;
    int                 prim_label_list_size;
    int                 dlist_on;
    int                 refresh_on;
};

DM_EXPORT extern void dm_draw_faceplate(struct bview *v);

/**
 * Phase 5 (drawing_stack_modernization): draw a single scene object through
 * the display manager associated with @p v, honouring v->gv_edit_mat for
 * illuminated objects (s_iflag == UP).
 *
 * @param dmp           display manager to draw into
 * @param s             scene object to draw
 * @param v             view providing the projection matrices
 * @param force_draw    non-zero to draw even when s->s_flag == DOWN
 * @param obj_settings  if non-NULL, override per-object colour/style
 */
DM_EXPORT extern void dm_draw_scene_obj(struct dm *dmp,
					struct bv_scene_obj *s,
					struct bview *v,
					int force_draw,
					const struct bsg_settings *obj_settings);

/* As a temporary measure, require client codes to specifically ask to enable
 * the bits that require librt in the headers if they're not going to be
 * calling them.  Not ideal, but pulling in rt also pulls in openNURBS, which
 * can have significant implications. */
#ifdef DM_WITH_RT
#include "rt/wdb.h"
#endif /* DM_WITH_RT */

/* Stripped down form of dm_draw_viewobjs that does just what's needed for the new setup */
DM_EXPORT extern void dm_draw_objs(struct bview *v, void (*dm_draw_custom)(struct bview *, void *), void *u_data);

/**
 * Phase 4 (drawing_stack_modernization): traverse a BSG scene root and
 * draw all children through the display manager associated with @p v.
 * If @p v->dmp is NULL or @p root is NULL the function is a no-op.
 *
 * bsg_view_traverse is implemented in libdm/view.c (DM_EXPORT) rather
 * than in libbsg because it calls draw_scene_obj() which requires the
 * dm_* rendering symbols — placing it here avoids a libbsg→libdm
 * circular dependency.
 */
DM_EXPORT extern void bsg_view_traverse(struct bview *v, void *root);

__END_DECLS

#endif /* DM_VIEW_H */

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
