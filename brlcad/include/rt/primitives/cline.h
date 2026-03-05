/*                      C L I N E . H
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
/** @addtogroup rt_cline */
/** @{ */
/** @file rt/primitives/cline.h */

#ifndef RT_PRIMITIVES_CLINE_H
#define RT_PRIMITIVES_CLINE_H

#include "common.h"
#include "vmath.h"
#include "rt/defines.h"

__BEGIN_DECLS

/**
 * radius of a FASTGEN cline element.
 *
 * shared with rt/do.c
 */
RT_EXPORT extern fastf_t rt_cline_radius;


/* CLINE solid edit command codes */
#define ECMD_CLINE_SCALE_H	29077	/**< scale height vector */
#define ECMD_CLINE_MOVE_H	29078	/**< move end of height vector */
#define ECMD_CLINE_SCALE_R	29079	/**< scale radius */
#define ECMD_CLINE_SCALE_T	29080	/**< scale thickness */

__END_DECLS

#endif /* RT_PRIMITIVES_CLINE_H */

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
