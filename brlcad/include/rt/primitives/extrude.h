/*                      E X T R U D E . H
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
/** @addtogroup rt_extrude */
/** @{ */
/** @file rt/primitives/extrude.h */

#ifndef RT_PRIMITIVES_EXTRUDE_H
#define RT_PRIMITIVES_EXTRUDE_H

#include "common.h"
#include "rt/defines.h"

__BEGIN_DECLS

/* EXTRUDE solid edit command codes */
#define ECMD_EXTR_SCALE_H	27073	/**< scale extrusion vector */
#define ECMD_EXTR_MOV_H		27074	/**< move end of extrusion vector */
#define ECMD_EXTR_ROT_H		27075	/**< rotate extrusion vector */
#define ECMD_EXTR_SKT_NAME	27076	/**< set sketch that the extrusion uses */
#define ECMD_EXTR_SCALE_A	27077	/**< scale A (sketch u_vec) reference vector */
#define ECMD_EXTR_SCALE_B	27078	/**< scale B (sketch v_vec) reference vector */
#define ECMD_EXTR_ROT_A		27079	/**< rotate A reference vector */
#define ECMD_EXTR_ROT_B		27080	/**< rotate B reference vector */

__END_DECLS

/** @} */

#endif /* RT_PRIMITIVES_EXTRUDE_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
