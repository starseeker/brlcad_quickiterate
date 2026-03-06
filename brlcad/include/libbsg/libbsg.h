/*                  L I B B S G / L I B B S G . H
 * BRL-CAD
 *
 * Copyright (c) 2024-2025 United States Government as represented by
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
/**
 * @file libbsg/libbsg.h
 *
 * @brief libbsg — BRL-CAD Scene Graph library umbrella header.
 *
 * This header is the single recommended include for code that uses the
 * BSG scene-graph API.  It pulls in @c bsg/defines.h (all @c bsg_*
 * type definitions, flags, and Phase 2 structs) and @c bsg/util.h
 * (all @c bsg_* function declarations).
 *
 * ### Naming convention
 *
 * All public types, functions, and macros are prefixed with @c bsg_.
 * The @c bv_* names in @c bv/defines.h are the legacy equivalents and
 * will be removed once the migration is complete.
 *
 * ### Migration path
 *
 * Old code (libbv-based):
 * @code
 * #include "bv/defines.h"
 * #include "bv/util.h"
 * @endcode
 *
 * New code (libbsg-based):
 * @code
 * #include "libbsg/libbsg.h"
 * @endcode
 *
 * The @c bsg_* type aliases in @c bsg/defines.h ensure binary
 * compatibility during the transition: @c bsg_shape == @c bv_scene_obj,
 * @c bsg_view == @c bview, etc.
 */

#ifndef LIBBSG_LIBBSG_H
#define LIBBSG_LIBBSG_H

/* Pull in all BSG type definitions (bsg_*, Phase 2 structs, flags). */
#include "bsg/defines.h"

/* Pull in all BSG function declarations (bsg_view_*, bsg_shape_*, etc.). */
#include "bsg/util.h"

/* BSG LoD utilities */
#include "bsg/lod.h"

/* BSG polygon utilities */
#include "bsg/polygon.h"

#endif /* LIBBSG_LIBBSG_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
