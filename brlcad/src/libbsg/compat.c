/*                         C O M P A T . C
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

#include "common.h"

#include "bv/defines.h"

#include "bsg/compat.h"


bsg_node *
bsg_compat_from_bv_scene_obj(struct bv_scene_obj *s)
{
    return (bsg_node *)s;
}


const bsg_node *
bsg_compat_from_bv_scene_obj_const(const struct bv_scene_obj *s)
{
    return (const bsg_node *)s;
}


struct bv_scene_obj *
bsg_compat_to_bv_scene_obj(bsg_node *n)
{
    return (struct bv_scene_obj *)n;
}


const struct bv_scene_obj *
bsg_compat_to_bv_scene_obj_const(const bsg_node *n)
{
    return (const struct bv_scene_obj *)n;
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
