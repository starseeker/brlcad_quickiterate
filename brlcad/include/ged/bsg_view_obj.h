/*                B S G _ V I E W _ O B J . H
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @addtogroup ged_view
 *
 * @brief
 * Phase 10 (drawing-stack modernization): legacy compatibility shim.
 * The canonical home of the BSG-backed GED draw-tree API has moved to
 * @ref ged/bsg_ged_draw.h.  This header simply forwards to the new
 * location so that out-of-tree callers do not break.
 *
 * Migrate at your earliest convenience by replacing
 *   #include "ged/bsg_view_obj.h"
 * with
 *   #include "ged/bsg_ged_draw.h"
 */
/** @{ */
/* @file ged/bsg_view_obj.h */

#ifndef GED_BSG_VIEW_OBJ_H
#define GED_BSG_VIEW_OBJ_H

#include "ged/bsg_ged_draw.h"

#endif /* GED_BSG_VIEW_OBJ_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
