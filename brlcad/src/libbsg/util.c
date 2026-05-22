/*                         U T I L . C
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
/** @file libbsg/util.c
 *
 * BSG lifecycle API bridge for scene objects.  The implementation delegates to
 * the existing libbv storage routines until bview storage ownership is fully
 * moved into libbsg.
 */

#include "common.h"

#include "bv/util.h"
#include "bsg/util.h"


bsg_node *
bsg_obj_create(struct bview *v, int type)
{
    return (bsg_node *)bv_obj_create(v, type);
}


bsg_node *
bsg_obj_get_unregistered(struct bview *v, int type)
{
    return (bsg_node *)bv_obj_get_unregistered(v, type);
}


void
bsg_obj_put(bsg_node *obj)
{
    bv_obj_put((struct bv_scene_obj *)obj);
}


void
bsg_scene_obj_release_backend(bsg_node *obj)
{
    bv_scene_obj_release_backend((struct bv_scene_obj *)obj);
}


void
bsg_scene_obj_invalidate_backend(bsg_node *obj)
{
    bv_scene_obj_invalidate_backend((struct bv_scene_obj *)obj);
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
