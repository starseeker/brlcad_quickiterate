/*                  B S G _ P R I V A T E . h
 * BRL-CAD
 *
 * Copyright (c) 2020-2025 United States Government as represented by
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
/** @file bsg_private.h
 *
 * Internal shared data structures for libbsg.
 *
 * In Phase 1, bsg_shape = bv_scene_obj and bsg_view = bview, so the
 * bsg_* internal types are aliases of the bv_* internals.  As the
 * migration progresses, these will become independent definitions.
 */

#ifndef LIBBSG_BSG_PRIVATE_H
#define LIBBSG_BSG_PRIVATE_H

/* Pull in the libbv internal structs so we can alias them.
 * BSG_LOCAL_INCLUDE_DIRS in libbsg/CMakeLists.txt adds libbv's source
 * directory to the include path so this relative include resolves. */
#include "bv_private.h"

#include "bsg/defines.h"

/* -----------------------------------------------------------------------
 * Phase 1 compatibility: bsg_scene_obj_internal is the same struct as
 * bv_scene_obj_internal (which is what bsg_shape::i actually points to
 * since bsg_shape == bv_scene_obj in Phase 1).
 * --------------------------------------------------------------------- */

/* Type alias — no layout difference during Phase 1. */
typedef bv_scene_obj_internal bsg_scene_obj_internal;

/* Type alias for the view-set internal struct. */
typedef bview_set_internal    bsg_scene_set_internal;

#endif /* LIBBSG_BSG_PRIVATE_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
