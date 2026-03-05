/*                         E T O . H
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
/** @addtogroup rt_eto */
/** @{ */
/** @file rt/primitives/eto.h */

#ifndef RT_PRIMITIVES_ETO_H
#define RT_PRIMITIVES_ETO_H

#include "common.h"
#include "rt/defines.h"

__BEGIN_DECLS

/* ETO solid edit command codes */
#define ECMD_ETO_ROT_C		21016	/**< rotate C vector */
#define ECMD_ETO_R		21057	/**< scale ETO major radius r */
#define ECMD_ETO_RD		21058	/**< scale ETO minor radius rd */
#define ECMD_ETO_SCALE_C	21059	/**< scale ETO semi-minor axis C */

__END_DECLS

/** @} */

#endif /* RT_PRIMITIVES_ETO_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
