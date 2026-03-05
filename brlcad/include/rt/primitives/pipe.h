/*                        P I P E . H
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
/** @addtogroup rt_pipe */
/** @{ */
/** @file rt/primitives/pipe.h */

#ifndef RT_PRIMITIVES_PIPE_H
#define RT_PRIMITIVES_PIPE_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bu/vls.h"
#include "rt/defines.h"

__BEGIN_DECLS

/* PIPE specific editing info */
struct rt_pipe_edit {
    struct wdb_pipe_pnt *es_pipe_pnt; /* Currently selected PIPE segment */
};


RT_EXPORT extern void rt_vls_pipe_pnt(struct bu_vls *vp,
				    int seg_no,
				    const struct rt_db_internal *ip,
				    double mm2local);
RT_EXPORT extern void rt_pipe_pnt_print(const struct wdb_pipe_pnt *pipe_pnt, double mm2local);
RT_EXPORT extern int rt_pipe_ck(const struct bu_list *headp);

RT_EXPORT extern int rt_pipe_get_i_seg(struct rt_pipe_internal *pipeip, struct wdb_pipe_pnt *ps);
RT_EXPORT extern struct wdb_pipe_pnt *rt_pipe_get_seg_i(struct rt_pipe_internal *pipeip, int seg_i);

RT_EXPORT extern int rt_pipe_move_pnt(struct rt_pipe_internal *pipeip, struct wdb_pipe_pnt *ps, const point_t new_pt);
RT_EXPORT extern struct wdb_pipe_pnt *rt_pipe_add_pnt(struct rt_pipe_internal *pipeip, struct wdb_pipe_pnt *pp, const point_t new_pt);
RT_EXPORT extern struct wdb_pipe_pnt *rt_pipe_delete_pnt(struct wdb_pipe_pnt *ps);
RT_EXPORT extern struct wdb_pipe_pnt *rt_pipe_ins_pnt(struct rt_pipe_internal *pipeip, struct wdb_pipe_pnt *pp, const point_t new_pt);
RT_EXPORT extern struct wdb_pipe_pnt *rt_pipe_find_pnt_nearest_pnt(const struct bu_list *pipe_hd, const point_t model_pt, matp_t view2model);


/* PIPE solid edit command codes */
#define ECMD_PIPE_SELECT	15028	/**< pick pipe point (canonical name) */
#define ECMD_PIPE_PICK		ECMD_PIPE_SELECT	/**< pick pipe point (MGED alias) */
#define ECMD_PIPE_SPLIT		15029	/**< split a pipe segment into two */
#define ECMD_PIPE_PT_ADD	15030	/**< add a pipe point to end of pipe */
#define ECMD_PIPE_PT_INS	15031	/**< add a pipe point to start of pipe */
#define ECMD_PIPE_PT_DEL	15032	/**< delete a pipe point */
#define ECMD_PIPE_PT_MOVE	15033	/**< move a pipe point */
#define ECMD_PIPE_NEXT_PT	15062	/**< select next pipe point */
#define ECMD_PIPE_PREV_PT	15063	/**< select previous pipe point */
#define ECMD_PIPE_PT_OD		15065	/**< scale OD of one pipe segment */
#define ECMD_PIPE_PT_ID		15066	/**< scale ID of one pipe segment */
#define ECMD_PIPE_SCALE_OD	15067	/**< scale entire pipe OD */
#define ECMD_PIPE_SCALE_ID	15068	/**< scale entire pipe ID */
#define ECMD_PIPE_PT_RADIUS	15073	/**< scale bend radius at selected point */
#define ECMD_PIPE_SCALE_RADIUS	15074	/**< scale entire pipe bend radius */

/** @} */

__END_DECLS

#endif /* RT_PRIMITIVES_PIPE_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
