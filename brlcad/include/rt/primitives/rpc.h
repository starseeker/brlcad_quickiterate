/*                        R P C . H
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
/** @addtogroup rt_rpc */
/** @{ */
/** @file rt/primitives/rpc.h */

#ifndef RT_PRIMITIVES_RPC_H
#define RT_PRIMITIVES_RPC_H

#include "common.h"
#include "vmath.h"
#include "rt/defines.h"

__BEGIN_DECLS

struct rt_pnt_node; /* forward declaration - defined in rt/private.h */

RT_EXPORT extern int rt_mk_parabola(struct rt_pnt_node *pts,
				    fastf_t r,
				    fastf_t b,
				    fastf_t dtol,
				    fastf_t ntol);

/* RPC solid edit command codes */
#define ECMD_RPC_B		17043	/**< scale RPC breadth B */
#define ECMD_RPC_H		17044	/**< scale RPC height H */
#define ECMD_RPC_R		17045	/**< scale RPC half-width r */

__END_DECLS

/** @} */

#endif /* RT_PRIMITIVES_RPC_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
