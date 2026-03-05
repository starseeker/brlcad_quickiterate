/*                         H Y P . H
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @addtogroup rt_hyp */
/** @{ */
/** @file rt/primitives/hyp.h */

#ifndef RT_PRIMITIVES_HYP_H
#define RT_PRIMITIVES_HYP_H

#include "common.h"
#include "rt/defines.h"

__BEGIN_DECLS

/* HYP solid edit command codes */
#define ECMD_HYP_ROT_H		38091	/**< rotate H vector */
#define ECMD_HYP_ROT_A		38092	/**< rotate A vector */
#define ECMD_HYP_H		38127	/**< scale HYP height H */
#define ECMD_HYP_SCALE_A	38128	/**< scale HYP semi-major axis A */
#define ECMD_HYP_SCALE_B	38129	/**< scale HYP semi-minor axis B */
#define ECMD_HYP_C		38130	/**< scale HYP neck parameter c */

__END_DECLS

/** @} */

#endif /* RT_PRIMITIVES_HYP_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
