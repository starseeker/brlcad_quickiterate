/*                         B S G . H
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
/**
 * @file bsg.h
 *
 * @brief Convenience header: includes all BRL-CAD Scene Graph (BSG) headers.
 *
 * The BSG API is a Phase-1 redesign of the libbv @c bview / @c bv_scene_obj
 * layer, structured to map more cleanly to the Open Inventor / Obol scene
 * graph conventions.  See @c bsg/defines.h for the type mapping table and
 * the phased migration plan.
 *
 * New code should include this header (or specific sub-headers).  The
 * legacy @c bv.h convenience header is a deprecated alias for this header.
 *
 * Code that needs to remain source-compatible with the old @c bv_* names
 * during the transition period should also include @c <bsg/compat.h>.
 */

#ifndef BSG_H
#define BSG_H

#include "bsg/defines.h"
#include "bsg/adc.h"
#include "bsg/lod.h"
#include "bsg/polygon.h"
#include "bsg/snap.h"
#include "bsg/util.h"
#include "bsg/vlist.h"
#include "bsg/view_sets.h"

#endif /* BSG_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
