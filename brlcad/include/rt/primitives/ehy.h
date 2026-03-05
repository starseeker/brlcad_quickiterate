/*                         E H Y . H
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
/** @addtogroup rt_ehy */
/** @{ */
/** @file rt/primitives/ehy.h */

#ifndef RT_PRIMITIVES_EHY_H
#define RT_PRIMITIVES_EHY_H

#include "common.h"
#include "rt/defines.h"

__BEGIN_DECLS

/* EHY solid edit command codes */
#define ECMD_EHY_H		20053	/**< scale EHY height H */
#define ECMD_EHY_R1		20054	/**< scale EHY semi-major axis r1 */
#define ECMD_EHY_R2		20055	/**< scale EHY semi-minor axis r2 */
#define ECMD_EHY_C		20056	/**< scale EHY dist-to-asymptotes c */

__END_DECLS

/** @} */

#endif /* RT_PRIMITIVES_EHY_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
