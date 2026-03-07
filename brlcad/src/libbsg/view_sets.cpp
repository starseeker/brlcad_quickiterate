/*                   V I E W _ S E T S . C P P
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
/** @file view_sets.cpp
 *
 * Utility functions for operating on BRL-CAD view sets
 *
 */

#include "common.h"
#include <string.h>
#include "vmath.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "bn/mat.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/view_sets.h"
#include "./bsg_private.h"

void
bsg_scene_init(bsg_scene *s)
{
    BU_GET(s->i, struct bsg_scene_set_internal);
    BU_PTBL_INIT(&s->i->views);
    bu_ptbl_init(&s->i->shared_db_objs, 8, "db_objs init");
    bu_ptbl_init(&s->i->shared_view_objs, 8, "view_objs init");
    BU_LIST_INIT(&s->i->vlfree);
    /* init the solid list */
    BU_GET(s->i->free_scene_obj, bsg_shape);
    BU_LIST_INIT(&s->i->free_scene_obj->l);
}

void
bsg_scene_free(bsg_scene *s)
{
    if (s->i) {
	bu_ptbl_free(&s->i->views);
	bu_ptbl_free(&s->i->shared_db_objs);
	bu_ptbl_free(&s->i->shared_view_objs);

	// TODO - replace free_scene_obj with bu_ptbl
	bsg_shape *sp, *nsp;
	sp = BU_LIST_NEXT(bsg_shape, &s->i->free_scene_obj->l);
	while (BU_LIST_NOT_HEAD(sp, &s->i->free_scene_obj->l)) {
	    nsp = BU_LIST_PNEXT(bsg_shape, sp);
	    BU_LIST_DEQUEUE(&((sp)->l));
	    if (sp->s_free_callback)
		(*sp->s_free_callback)(sp);
	    if (sp->s_dlist_free_callback)
		(*sp->s_dlist_free_callback)(sp);
	    bu_ptbl_free(&sp->children);
	    BU_PUT(sp, bsg_shape);
	    sp = nsp;
	}
	BU_PUT(s->i->free_scene_obj, bsg_shape);
	BU_PUT(s->i, struct bsg_scene_set_internal);
    }

    // TODO - clean up vlfree
}

void
bsg_scene_add_view(bsg_scene *s, bsg_view *v){
    if (!s || !v)
	return;

    bu_ptbl_ins_unique(&s->i->views, (long *)v);

    v->vset = s;

    // By default, when we add a view to a set it is no longer considered
    // independent
    v->independent = 0;
}

void
bsg_scene_rm_view(bsg_scene *s, bsg_view *v){
    if (!s)
	return;

    if (!v) {
	bu_ptbl_reset(&s->i->views);
	return;
    }

    bu_ptbl_rm(&s->i->views, (long int *)v);

    v->vset = NULL;

    // By default, when we remove a view from a set it is considered
    // independent
    v->independent = 1;
}


struct bu_ptbl *
bsg_scene_views(bsg_scene *s){
    if (!s)
	return NULL;

    return &s->i->views;
}

bsg_view *
bsg_scene_find_view(bsg_scene *s, const char *vname)
{
    bsg_view *v = NULL;
    for (size_t i = 0; i < BU_PTBL_LEN(&s->i->views); i++) {
	bsg_view *tv = (bsg_view *)BU_PTBL_GET(&s->i->views, i);
	if (BU_STR_EQUAL(bu_vls_cstr(&tv->gv_name), vname)) {
	    v = tv;
	    break;
	}
    }

    return v;
}

bsg_shape *
bsg_scene_fsos(bsg_scene *s)
{
    return s->i->free_scene_obj;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
