/*                          S E G . H
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
/** @addtogroup rt_seg
 * @brief Intersection segment.
 *
 * A "seg" or segment is a low level in/out ray/shape intersection solution,
 * derived by performing intersection calculations between a ray and a single
 * object.  Depending on the type of shape, a single ray may produce
 * multiple segments when intersected with that shape. Unless the object
 * happens to also be a top-level unioned object in the database, individual
 * segments must be "woven" together using boolean hierarchies to obtain the
 * final partitions which describe solid geometry along a ray.  An individual
 * segment may have either a positive or negative contribution to the "final"
 * partition, depending on whether parent combs designate their children
 * as unioned, intersected, or subtracted.
 */
/** @{ */
/** @file seg.h */

#ifndef RT_SEG_H
#define RT_SEG_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bu/vls.h"
#include "rt/defines.h"
#include "rt/hit.h"

__BEGIN_DECLS

struct soltab;  /* forward declaration */

/**
 * Intersection segment.
 *
 * Includes information about both endpoints of intersection.
 * Contains forward link to additional intersection segments if the
 * intersection spans multiple segments (e.g., shooting a ray through
 * a torus).
 */
struct seg {
    struct bu_list      l;
    struct hit          seg_in;         /**< @brief IN information */
    struct hit          seg_out;        /**< @brief OUT information */
    struct soltab *     seg_stp;        /**< @brief pointer back to soltab */
};
#define RT_SEG_NULL     ((struct seg *)0)

#define RT_CHECK_SEG(_p) BU_CKMAG(_p, RT_SEG_MAGIC, "struct seg")
#define RT_CK_SEG(_p) BU_CKMAG(_p, RT_SEG_MAGIC, "struct seg")

/* Phase 7: seg freelist removed; RT_GET_SEG/RT_FREE_SEG now use direct
 * bu_malloc/bu_free.  The 'res' parameter is accepted but ignored so
 * that all existing call sites continue to compile without change.
 */
#define RT_GET_SEG(p, res) { \
	BU_ALLOC((p), struct seg); \
	(p)->l.magic = RT_SEG_MAGIC; \
	(p)->seg_in.hit_magic = (p)->seg_out.hit_magic = RT_HIT_MAGIC; \
    }


#define RT_FREE_SEG(p, res) { \
	RT_CHECK_SEG(p); \
	bu_free((void *)(p), "struct seg"); \
    }


#define RT_FREE_SEG_LIST(_segheadp, _res) { \
	register struct seg *_a; \
	while (BU_LIST_WHILE(_a, seg, &((_segheadp)->l))) { \
	    BU_LIST_DEQUEUE(&(_a->l)); \
	    RT_FREE_SEG(_a, _res); \
	} \
    }

/* Print seg struct */
RT_EXPORT extern void rt_pr_seg(const struct seg *segp);
RT_EXPORT extern void rt_pr_seg_vls(struct bu_vls *, const struct seg *);


__END_DECLS

#endif /* RT_SEG_H */
/** @} */
/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
