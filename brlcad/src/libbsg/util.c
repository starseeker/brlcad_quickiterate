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
 * the existing libbv storage routines until bsg_view storage ownership is fully
 * moved into libbsg.
 */

#include "common.h"

#include "bu/list.h"
#include "bu/ptbl.h"
#include "bsg/util.h"
#include "bsg/view_sets.h"
#include "bsg/vlist.h"


void
bsg_view_init(struct bsg_view *v, struct bsg_view_set *s)
{
    bsg_init(v, s);
}


void
bsg_view_free(struct bsg_view *v)
{
    bsg_free(v);
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
