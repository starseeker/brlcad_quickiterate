/*                        S K E T C H . H
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
/** @addtogroup rt_sketch */
/** @{ */
/** @file rt/primitives/sketch.h */

#ifndef RT_PRIMITIVES_SKETCH_H
#define RT_PRIMITIVES_SKETCH_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bu/vls.h"
#include "bn/tol.h"
#include "bv/defines.h"
#include "rt/defines.h"
#include "rt/directory.h"
#include "rt/db_instance.h"

__BEGIN_DECLS

/* SKETCH specific editing info */
struct rt_sketch_edit {
    int curr_vert;  /* index of the currently selected vertex (-1 = none) */
    int curr_seg;   /* index of the currently selected curve segment (-1 = none) */
    /* Mouse-proximity pick: ft_edit_xy stores cursor here; ft_edit reads it */
    point_t v_pos;      /* view-space cursor position set by ft_edit_xy */
    int v_pos_valid;    /* non-zero when v_pos holds a pending proximity query */
};

/* SKETCH solid edit command codes (ID_SKETCH = 26, so codes use 26nnn) */

/** Select a vertex by index (e_para[0] = vertex index) or by mouse
 *  proximity when invoked after ft_edit_xy sets v_pos. */
#define ECMD_SKETCH_PICK_VERTEX		26001
/** Move the currently selected vertex (e_para[0..1] = new UV coords in mm). */
#define ECMD_SKETCH_MOVE_VERTEX		26002
/** Select a curve segment by index (e_para[0] = segment index). */
#define ECMD_SKETCH_PICK_SEGMENT	26003
/** Translate the currently selected segment (e_para[0..1] = UV delta in mm). */
#define ECMD_SKETCH_MOVE_SEGMENT	26004
/** Append a line segment (e_para[0] = start vert index, e_para[1] = end vert index). */
#define ECMD_SKETCH_APPEND_LINE		26005
/** Append a circular arc segment (e_para[0]=start, [1]=end vert, [2]=radius, [3]=center_is_left, [4]=orientation; e_inpara=5). */
#define ECMD_SKETCH_APPEND_ARC		26006
/** Append a Bezier curve segment (e_para[0..e_inpara-1] = control point vert indices; e_inpara >= 2). */
#define ECMD_SKETCH_APPEND_BEZIER	26007
/** Delete the currently selected vertex (only if not used by any segment). */
#define ECMD_SKETCH_DELETE_VERTEX	26008
/** Delete the currently selected curve segment. */
#define ECMD_SKETCH_DELETE_SEGMENT	26009
/** Move a list of vertices by a common UV delta (e_para[0]=U delta, [1]=V delta, [2..e_inpara-1]=vert indices). */
#define ECMD_SKETCH_MOVE_VERTEX_LIST	26010
/** Split the currently selected segment at parameter t (e_para[0]=seg index, [1]=t in (0,1); e_inpara=2). */
#define ECMD_SKETCH_SPLIT_SEGMENT	26011
/** Append a non-rational NURB curve segment (e_para[0]=order, [1..e_inpara-1]=ctrl pt vert indices). */
#define ECMD_SKETCH_APPEND_NURB		26012
/** Replace the knot vector of the currently selected NURB segment (e_para[0]=k_size, [1..e_inpara-1]=knot values). */
#define ECMD_SKETCH_NURB_EDIT_KV	26013
/** Set/replace the weight array of the currently selected NURB segment (e_para[0]=seg idx, [1]=c_size, [2..e_inpara-1]=weights). */
#define ECMD_SKETCH_NURB_EDIT_WEIGHTS	26014

RT_EXPORT extern int rt_check_curve(const struct rt_curve *crv,
				    const struct rt_sketch_internal *skt,
				    int noisy);

RT_EXPORT extern void rt_curve_reverse_segment(uint32_t *lng);
RT_EXPORT extern void rt_curve_order_segments(struct rt_curve *crv);

RT_EXPORT extern void rt_copy_curve(struct rt_curve *crv_out,
				    const struct rt_curve *crv_in);

RT_EXPORT extern void rt_curve_free(struct rt_curve *crv);
RT_EXPORT extern void rt_copy_curve(struct rt_curve *crv_out,
				    const struct rt_curve *crv_in);
RT_EXPORT extern struct rt_sketch_internal *rt_copy_sketch(const struct rt_sketch_internal *sketch_ip);

RT_EXPORT extern struct bv_scene_obj *
db_sketch_to_scene_obj(const char *sname, struct db_i *dbip, struct directory *dp, struct bview *sv, int flags);

RT_EXPORT extern struct directory *
db_scene_obj_to_sketch(struct db_i *dbip, const char *sname, struct bv_scene_obj *s);

__END_DECLS

/** @} */

#endif /* RT_PRIMITIVES_SKETCH_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
