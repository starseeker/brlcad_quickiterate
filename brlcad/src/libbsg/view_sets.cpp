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
#include "bu/str.h"
#include "bn/mat.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/view_sets.h"
#include "./bv_private.h"

void
bv_set_init(struct bsg_view_set *s)
{
    BU_GET(s->i, struct bsg_view_set_internal);
    BU_PTBL_INIT(&s->i->views);
    bu_ptbl_init(&s->i->shared_db_objs, 8, "db_objs init");
    BU_LIST_INIT(&s->i->vlfree);
    /* init the solid list */
    BU_GET(s->i->free_scene_obj, struct bsg_node);
    BU_LIST_INIT(&s->i->free_scene_obj->l);
}

void
bv_set_free(struct bsg_view_set *s)
{
    if (s->i) {
	bu_ptbl_free(&s->i->views);
	bu_ptbl_free(&s->i->shared_db_objs);

	// TODO - replace free_scene_obj with bu_ptbl
	struct bsg_node *sp, *nsp;
	sp = BU_LIST_NEXT(bsg_node, &s->i->free_scene_obj->l);
	while (BU_LIST_NOT_HEAD(sp, &s->i->free_scene_obj->l)) {
	    nsp = BU_LIST_PNEXT(bsg_node, sp);
	    BU_LIST_DEQUEUE(&((sp)->l));
	    if (sp->s_free_callback)
		(*sp->s_free_callback)(sp);
	    /* Phase 11: route backend release through the generic contract. */
	    bv_scene_obj_release_backend(sp);
	    bu_ptbl_free(&sp->children);
	    BU_PUT(sp, struct bsg_node);
	    sp = nsp;
	}
	BU_PUT(s->i->free_scene_obj, struct bsg_node);
	BU_PUT(s->i, struct bsg_view_set_internal);
    }

    // TODO - clean up vlfree
}

void
bv_set_add_view(struct bsg_view_set *s, struct bsg_view *v){
    if (!s || !v)
	return;

    bu_ptbl_ins_unique(&s->i->views, (long *)v);

    v->vset = s;

    // By default, when we add a view to a set it is no longer independent;
    // remove any existing independent scope from the BSG tree.
    bv_view_independent_scope_destroy(v);
}

void
bv_set_rm_view(struct bsg_view_set *s, struct bsg_view *v){
    if (!s)
	return;

    if (!v) {
	bu_ptbl_reset(&s->i->views);
	return;
    }

    bu_ptbl_rm(&s->i->views, (long int *)v);

    v->vset = NULL;

    // By default, when we remove a view from a set it is independent;
    // create an independent scope in the BSG tree when possible.
    bv_view_independent_scope(v, 1 /*create*/);
}


struct bu_ptbl *
bv_set_views(struct bsg_view_set *s){
    if (!s)
	return NULL;

    return &s->i->views;
}

struct bsg_view *
bv_set_find_view(struct bsg_view_set *s, const char *vname)
{
    struct bsg_view *v = NULL;
    for (size_t i = 0; i < BU_PTBL_LEN(&s->i->views); i++) {
	struct bsg_view *tv = (struct bsg_view *)BU_PTBL_GET(&s->i->views, i);
	if (BU_STR_EQUAL(bu_vls_cstr(&tv->gv_name), vname)) {
	    v = tv;
	    break;
	}
    }

    return v;
}

struct bsg_node *
bv_set_fsos(struct bsg_view_set *s)
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
