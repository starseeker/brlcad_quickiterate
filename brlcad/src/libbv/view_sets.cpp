/*                   V I E W _ S E T S . C P P
 * BRL-CAD
 *
 * Copyright (c) 2020-2026 United States Government as represented by
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
#include "bsg/scene_set.h"
#include "bsg/node.h"
#include "bu/str.h"
#include "bn/mat.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/view_sets.h"
#include "./bv_private.h"

void
bv_set_init(struct bview_set *s)
{
    BU_GET(s->i, struct bview_set_internal);
    BU_PTBL_INIT(&s->i->views);
    bu_ptbl_init(&s->i->shared_db_objs, 8, "db_objs init");
    s->i->scene_set = bsg_scene_set_create();
    BU_LIST_INIT(&s->i->vlfree);
    /* init the solid list */
    BU_GET(s->i->free_scene_obj, struct bv_scene_obj);
    BU_LIST_INIT(&s->i->free_scene_obj->bsg.l);
}

void
bv_set_free(struct bview_set *s)
{
    if (s->i) {
	bu_ptbl_free(&s->i->views);
	bu_ptbl_free(&s->i->shared_db_objs);
	bsg_scene_set_destroy(s->i->scene_set);
	s->i->scene_set = NULL;

	// TODO - replace free_scene_obj with bu_ptbl
	struct bv_scene_obj *sp, *nsp;
	sp = BU_LIST_NEXT(bv_scene_obj, &s->i->free_scene_obj->bsg.l);
	while (BU_LIST_NOT_HEAD(sp, &s->i->free_scene_obj->bsg.l)) {
	    nsp = BU_LIST_PNEXT(bv_scene_obj, sp);
	    BU_LIST_DEQUEUE(&((sp)->bsg.l));
	    bsg_node_invoke_free_callback((bsg_node *)sp);
	    /* Phase 11: route backend release through the generic contract. */
	    bv_scene_obj_release_backend(sp);
	    bu_ptbl_free(&sp->bsg.bsg_children);
	    BU_PUT(sp, struct bv_scene_obj);
	    sp = nsp;
	}
	BU_PUT(s->i->free_scene_obj, struct bv_scene_obj);
	BU_PUT(s->i, struct bview_set_internal);
    }

    // TODO - clean up vlfree
}

void
bv_set_add_view(struct bview_set *s, struct bview *v){
    if (!s || !v)
	return;

    bu_ptbl_ins_unique(&s->i->views, (long *)v);

    v->vset = s;
    if (s->i->scene_set)
	bsg_scene_set_add(s->i->scene_set, v, (bsg_node *)v->gv_draw_root);

    // By default, when we add a view to a set it is no longer independent;
    // remove any existing independent scope from the BSG tree.
    bv_view_independent_scope_destroy(v);
}

void
bv_set_rm_view(struct bview_set *s, struct bview *v){
    if (!s)
	return;

    if (!v) {
	if (s->i->scene_set) {
	    bsg_scene_set_destroy(s->i->scene_set);
	    s->i->scene_set = bsg_scene_set_create();
	}
	bu_ptbl_reset(&s->i->views);
	return;
    }

    bu_ptbl_rm(&s->i->views, (long int *)v);
    if (s->i->scene_set)
	bsg_scene_set_remove(s->i->scene_set, v);

    v->vset = NULL;

    // By default, when we remove a view from a set it is independent;
    // create an independent scope in the BSG tree when possible.
    bv_view_independent_scope(v, 1 /*create*/);
}


struct bu_ptbl *
bv_set_views(struct bview_set *s){
    if (!s)
	return NULL;

    return &s->i->views;
}

struct bview *
bv_set_find_view(struct bview_set *s, const char *vname)
{
    if (!s || !s->i || !vname)
	return NULL;

    struct bview *v = NULL;
    for (size_t i = 0; i < BU_PTBL_LEN(&s->i->views); i++) {
	struct bview *tv = (struct bview *)BU_PTBL_GET(&s->i->views, i);
	if (BU_STR_EQUAL(bu_vls_cstr(&tv->gv_name), vname)) {
	    v = tv;
	    break;
	}
    }

    return v;
}

struct bsg_scene_set *
bv_set_scene_set(struct bview_set *s)
{
    if (!s || !s->i)
	return NULL;
    return s->i->scene_set;
}

struct bv_scene_obj *
bv_set_fsos(struct bview_set *s)
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
