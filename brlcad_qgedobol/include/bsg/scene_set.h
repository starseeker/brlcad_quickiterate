/*                  B S G / S C E N E _ S E T . H
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
/** @file bsg/scene_set.h
 *
 * @brief  Internal definition of @c bsg_scene_set_internal.
 *
 * This header is an implementation detail shared between libbsg and libbv.
 * It is installed with the BRL-CAD headers for technical reasons (both
 * libraries need the full struct definition) but it is @b not part of the
 * public BSG API; application code should never include it directly.
 *
 * @note @c bview_set.i is declared as @c bsg_scene_set_internal* in
 *       @c bv/defines.h.  libbv aliases @c bview_set_internal to this
 *       type (see @c bv_private.h).
 */

#ifndef BSG_SCENE_SET_H
#define BSG_SCENE_SET_H

#include "bu/list.h"
#include "bu/ptbl.h"

/* Forward declaration only — full definition is in bsg/defines.h / bv/defines.h */
struct bsg_shape;

/**
 * @brief Private storage for a @c bsg_scene (= @c bview_set) instance.
 *
 * This is the canonical internal representation.  @c bview_set_internal
 * (in libbv) is a typedef alias of this struct.
 */
struct bsg_scene_set_internal {
    struct bu_ptbl views;              /**< @brief registered bsg_view pointers */
    struct bu_ptbl shared_db_objs;     /**< @brief DB shapes shared across views */
    struct bu_ptbl shared_view_objs;   /**< @brief view shapes shared across views */
    struct bsg_shape *free_scene_obj;  /**< @brief recycling pool head */
    struct bu_list   vlfree;           /**< @brief vlist free-list */
};

#endif /* BSG_SCENE_SET_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
