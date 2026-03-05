/*                        P A R T . H
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
/** @addtogroup rt_part */
/** @{ */
/** @file rt/primitives/part.h */

#ifndef RT_PRIMITIVES_PART_H
#define RT_PRIMITIVES_PART_H

#include "common.h"
#include "rt/defines.h"

__BEGIN_DECLS

/* PART solid edit command codes */
#define ECMD_PART_H		16088	/**< scale PART height */
#define ECMD_PART_VRAD		16089	/**< scale PART vertex radius */
#define ECMD_PART_HRAD		16090	/**< scale PART height-end radius */

__END_DECLS

/** @} */

#endif /* RT_PRIMITIVES_PART_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
