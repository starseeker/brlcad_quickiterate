/*                         V O L . H
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
/** @addtogroup rt_vol */
/** @{ */
/** @file rt/primitives/vol.h */

#ifndef RT_PRIMITIVES_VOL_H
#define RT_PRIMITIVES_VOL_H

#include "common.h"
#include "rt/defines.h"

__BEGIN_DECLS

/* VOL solid edit command codes */
#define ECMD_VOL_CSIZE		13048	/**< set voxel size */
#define ECMD_VOL_FSIZE		13049	/**< set VOL file dimensions */
#define ECMD_VOL_THRESH_LO	13050	/**< set VOL threshold (lo) */
#define ECMD_VOL_THRESH_HI	13051	/**< set VOL threshold (hi) */
#define ECMD_VOL_FNAME		13052	/**< set VOL file name */

__END_DECLS

/** @} */

#endif /* RT_PRIMITIVES_VOL_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
