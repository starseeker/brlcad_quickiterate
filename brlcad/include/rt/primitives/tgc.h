/*                        T G C . H
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
/** @addtogroup rt_tgc */
/** @{ */
/** @file rt/primitives/tgc.h */

#ifndef RT_PRIMITIVES_TGC_H
#define RT_PRIMITIVES_TGC_H

#include "common.h"
#include "vmath.h"
#include "rt/defines.h"

__BEGIN_DECLS

/* TODO - should this be in libbn? */
RT_EXPORT extern void rt_pnt_sort(fastf_t t[],
				 int npts);


/* TGC solid edit command codes */
#define ECMD_TGC_MV_H		2005	/**< move H to new position */
#define ECMD_TGC_MV_HH		2006	/**< move H and HH to new position */
#define ECMD_TGC_ROT_H		2007	/**< rotate H vector */
#define ECMD_TGC_ROT_AB		2008	/**< rotate A,B vectors */
#define ECMD_TGC_SCALE_H	2027	/**< scale H */
#define ECMD_TGC_SCALE_H_V	2028	/**< scale H, move V */
#define ECMD_TGC_SCALE_A	2029	/**< scale A */
#define ECMD_TGC_SCALE_B	2030	/**< scale B */
#define ECMD_TGC_SCALE_C	2031	/**< scale C */
#define ECMD_TGC_SCALE_D	2032	/**< scale D */
#define ECMD_TGC_SCALE_AB	2033	/**< scale A and B */
#define ECMD_TGC_SCALE_CD	2034	/**< scale C and D */
#define ECMD_TGC_SCALE_ABCD	2035	/**< scale A, B, C and D */
#define ECMD_TGC_MV_H_CD	2081	/**< move end of tgc, while scaling CD */
#define ECMD_TGC_MV_H_V_AB	2082	/**< move vertex end of tgc, while scaling AB */
#define ECMD_TGC_SCALE_H_CD	2111	/**< scale H adjusting C,D */
#define ECMD_TGC_S_H_CD		ECMD_TGC_SCALE_H_CD	/**< scale H adjusting C,D (MGED alias) */
#define ECMD_TGC_SCALE_H_V_AB	2112	/**< scale H+move V adjusting A,B */
#define ECMD_TGC_S_H_V_AB	ECMD_TGC_SCALE_H_V_AB	/**< scale H+move V adjusting A,B (MGED alias) */

__END_DECLS

/** @} */

#endif /* RT_PRIMITIVES_TGC_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
