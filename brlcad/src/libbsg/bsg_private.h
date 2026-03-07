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
 */

#ifndef LIBBSG_BSG_PRIVATE_H
#define LIBBSG_BSG_PRIVATE_H

#include "common.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bsg/defines.h"
#include <unordered_map>

struct bsg_scene_set_internal {
    struct bu_ptbl views;
    struct bu_ptbl shared_db_objs;
    struct bu_ptbl shared_view_objs;

    bsg_shape  *free_scene_obj;
    struct bu_list vlfree;
};

struct bsg_shape_internal {
    std::unordered_map<bsg_view *, bsg_shape *> vobjs;
};

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
