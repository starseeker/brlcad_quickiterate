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
#define ECMD_ARS_PICK		5034	/**< select an ARS point */
#define ECMD_ARS_NEXT_PT	5035	/**< select next ARS point in same curve */
#define ECMD_ARS_PREV_PT	5036	/**< select previous ARS point in same curve */
#define ECMD_ARS_NEXT_CRV	5037	/**< select corresponding ARS point in next curve */
#define ECMD_ARS_PREV_CRV	5038	/**< select corresponding ARS point in previous curve */
#define ECMD_ARS_MOVE_PT	5039	/**< translate an ARS point */
#define ECMD_ARS_DEL_CRV	5040	/**< delete an ARS curve */
#define ECMD_ARS_DEL_COL	5041	/**< delete all corresponding points in each curve */
#define ECMD_ARS_DUP_CRV	5042	/**< duplicate an ARS curve */
#define ECMD_ARS_DUP_COL	5043	/**< duplicate an ARS column */
#define ECMD_ARS_MOVE_CRV	5044	/**< translate an ARS curve */
#define ECMD_ARS_MOVE_COL	5045	/**< translate an ARS column */
#define ECMD_ARS_PICK_MENU	5046	/**< display the ARS pick menu */
#define ECMD_ARS_EDIT_MENU	5047	/**< display the ARS edit menu */
#define ECMD_ARS_SCALE_CRV	5048	/**< scale an ARS curve */
#define ECMD_ARS_SCALE_COL	5049	/**< scale an ARS column */
#define ECMD_ARS_INSERT_CRV	5050	/**< insert an ARS curve */

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
