/*                        N M G . H
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
/** @addtogroup rt_nmg */
/** @{ */
/** @file rt/primitives/nmg.h */

#ifndef RT_PRIMITIVES_NMG_H
#define RT_PRIMITIVES_NMG_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bn/tol.h"
#include "nmg.h"
#include "rt/defines.h"
#include "rt/geom.h"
#include "rt/tree.h"

__BEGIN_DECLS

/* NMG specific editing info */
struct rt_nmg_edit {
    struct edgeuse *es_eu;      /* Currently selected NMG edgeuse */
    struct loopuse *lu_copy;    /* copy of loop to be extruded */
    plane_t lu_pl;              /* plane equation for loop to be extruded */
    struct shell *es_s;         /* Shell where extrusion is to end up */
    point_t lu_keypoint;        /* keypoint of lu_copy for extrusion */
    struct vertex *es_v;        /* Currently selected NMG vertex (ECMD_NMG_VPICK) */
    struct faceuse *es_fu;      /* Currently selected NMG faceuse (ECMD_NMG_FPICK) */
};

RT_EXPORT extern int
rt_nmg_do_bool(
	union tree *tp, union tree *tl, union tree *tr,
	int op, struct bu_list *vlfree, const struct bn_tol *tol, void *data);

/* NMG solid edit command codes */
#define ECMD_NMG_EPICK		11019	/**< edge pick */
#define ECMD_NMG_EMOVE		11020	/**< edge move */
#define ECMD_NMG_EDEBUG		11021	/**< edge debug */
#define ECMD_NMG_FORW		11022	/**< next eu */
#define ECMD_NMG_BACK		11023	/**< prev eu */
#define ECMD_NMG_RADIAL		11024	/**< radial+mate eu */
#define ECMD_NMG_ESPLIT		11025	/**< split current edge */
#define ECMD_NMG_EKILL		11026	/**< kill current edge */
#define ECMD_NMG_LEXTRU		11027	/**< extrude loop */
#define ECMD_NMG_VPICK		11028	/**< vertex pick */
#define ECMD_NMG_VMOVE		11029	/**< vertex move */
#define ECMD_NMG_FPICK		11030	/**< face pick */
#define ECMD_NMG_FMOVE		11031	/**< face move */
#define ECMD_NMG_LEXTRU_DIR	11032	/**< set extrude loop direction */

/** @} */

__END_DECLS

#endif /* RT_PRIMITIVES_BOT_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
