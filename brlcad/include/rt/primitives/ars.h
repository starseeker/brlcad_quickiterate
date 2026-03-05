/*                       R T / P R I M I T I V E S / A R S . H
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
/** @addtogroup rt_ars
 * @{
 */
/** @file rt/primitives/ars.h */

#ifndef RT_PRIMITIVES_ARS_H
#define RT_PRIMITIVES_ARS_H

#include "common.h"
#include "vmath.h"

__BEGIN_DECLS

/**
 * Per-primitive edit state for ARS editing.
 *
 * Exposed here so that MGED (and other editors) can access the currently
 * selected ARS curve/column without duplicating the data as a global.
 */
struct rt_ars_edit {
    int es_ars_crv;	/**< @brief currently selected ARS curve index (-1 if none) */
    int es_ars_col;	/**< @brief currently selected ARS column index (-1 if none) */
    point_t es_pt;	/**< @brief coordinates of selected ARS point */
};

/* ARS solid edit command codes */
/* ECMD_ARS_* are in the scanner-generated rt/rt_ecmds.h */

__END_DECLS

#endif /* RT_PRIMITIVES_ARS_H */
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
