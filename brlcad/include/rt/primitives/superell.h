/*                      S U P E R E L L . H
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
/** @addtogroup rt_superell */
/** @{ */
/** @file rt/primitives/superell.h */

#ifndef RT_PRIMITIVES_SUPERELL_H
#define RT_PRIMITIVES_SUPERELL_H

#include "common.h"
#include "rt/defines.h"

__BEGIN_DECLS

/* SUPERELL solid edit command codes */
#define ECMD_SUPERELL_SCALE_A	35113	/**< scale SUPERELL semiaxis A */
#define ECMD_SUPERELL_SCALE_B	35114	/**< scale SUPERELL semiaxis B */
#define ECMD_SUPERELL_SCALE_C	35115	/**< scale SUPERELL semiaxis C */
#define ECMD_SUPERELL_SCALE_ABC	35116	/**< scale SUPERELL A,B,C uniformly */

__END_DECLS

/** @} */

#endif /* RT_PRIMITIVES_SUPERELL_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
