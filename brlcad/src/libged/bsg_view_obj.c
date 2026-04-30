/*                B S G _ V I E W _ O B J . C
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
/** @file libged/bsg_view_obj.c
 *
 * Phase 6.5 (drawing-stack modernization) — Step 1.
 *
 * Implementation of the BSG view-object query API declared in
 * include/ged/bsg_view_obj.h.  These nine helpers form the migration
 * target for libged callers (and out-of-tree consumers) that currently
 * walk the legacy gedp->i->ged_gdp->gd_headDisplay chain via the
 * dl_* / _dl_* / invent_solid functions in display_list.c.
 *
 * The current implementation deliberately delegates to the legacy
 * dl_* routines.  This lets caller migration (Step 2 of the deletion
 * plan) proceed independently against a stable signature surface,
 * without any behavior change.  Once every caller has been migrated,
 * Step 7 of the plan will replace the bodies below with a pure BSG
 * view-tree walk and delete display_list.c.
 *
 * See doc/notes/drawing_stack_modernization.txt Phase 6.5 for the
 * staged plan and see brlcad/include/ged/bsg_view_obj.h for the API
 * contract each helper presents to callers.
 */

#include "common.h"

#include "ged.h"
#include "ged/bsg_view_obj.h"
#include "./ged_private.h"


void *
bsg_view_obj_lookup_or_add_path(struct ged *gedp, const char *path)
{
    if (!gedp || !path)
	return NULL;
    return (void *)dl_addToDisplay(gedp->i->ged_gdp->gd_headDisplay,
				   gedp->dbip, path);
}


void
bsg_view_obj_erase_by_path(struct ged *gedp, const char *path,
			   int allow_split)
{
    if (!gedp || !path)
	return;
    dl_erasePathFromDisplay(gedp, path, allow_split);
}


void
bsg_view_obj_erase_by_name(struct ged *gedp, const char *name,
			   int skip_first)
{
    if (!gedp || !name)
	return;
    _dl_eraseAllNamesFromDisplay(gedp, name, skip_first);
}


void
bsg_view_obj_erase_all_paths(struct ged *gedp, const char *path,
			     int skip_first)
{
    if (!gedp || !path)
	return;
    _dl_eraseAllPathsFromDisplay(gedp, path, skip_first);
}


int
bsg_view_obj_bounds(struct ged *gedp, vect_t *min, vect_t *max,
		    int pflag)
{
    if (!gedp || !min || !max)
	return 1;
    return dl_bounding_sph(gedp->i->ged_gdp->gd_headDisplay,
			   min, max, pflag);
}


void
bsg_view_obj_set_iflag(struct ged *gedp, int iflag)
{
    if (!gedp)
	return;
    dl_set_iflag(gedp->i->ged_gdp->gd_headDisplay, iflag);
}


void
bsg_view_obj_color_from_soltab(struct ged *gedp)
{
    if (!gedp)
	return;
    dl_color_soltab((struct bu_list *)ged_dl(gedp), gedp->dbip);
}


int
bsg_view_obj_invent(struct ged *gedp, char *name, struct bu_list *vhead,
		    long int rgb, int copy, fastf_t transparency,
		    int dmode, int csoltab)
{
    if (!gedp || !name || !vhead)
	return -1;
    return invent_solid(gedp, name, vhead, rgb, copy, transparency,
			dmode, csoltab);
}


unsigned long long
bsg_view_obj_name_hash(struct ged *gedp)
{
    if (!gedp)
	return 0;
    return dl_name_hash(gedp);
}


void
bsg_view_obj_foreach_solid(struct ged *gedp,
			   int (*cb)(struct bv_scene_obj *sp, void *userdata),
			   void *userdata)
{
    if (!gedp || !cb)
	return;
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    struct bv_scene_obj *sp;
    gdlp = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
    while (BU_LIST_NOT_HEAD(gdlp, (struct bu_list *)ged_dl(gedp))) {
	next_gdlp = BU_LIST_PNEXT(display_list, gdlp);
	for (BU_LIST_FOR(sp, bv_scene_obj, &gdlp->dl_head_scene_obj)) {
	    if (!(*cb)(sp, userdata))
		return;
	}
	gdlp = next_gdlp;
    }
}


int
bsg_view_obj_is_nonempty(struct ged *gedp)
{
    if (!gedp)
	return 0;
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    gdlp = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
    while (BU_LIST_NOT_HEAD(gdlp, (struct bu_list *)ged_dl(gedp))) {
	next_gdlp = BU_LIST_PNEXT(display_list, gdlp);
	if (BU_LIST_NON_EMPTY(&gdlp->dl_head_scene_obj))
	    return 1;
	gdlp = next_gdlp;
    }
    return 0;
}


struct bv_scene_obj *
bsg_view_obj_first_solid(struct ged *gedp)
{
    if (!gedp)
	return NULL;
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    gdlp = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
    while (BU_LIST_NOT_HEAD(gdlp, (struct bu_list *)ged_dl(gedp))) {
	next_gdlp = BU_LIST_PNEXT(display_list, gdlp);
	if (BU_LIST_NON_EMPTY(&gdlp->dl_head_scene_obj))
	    return BU_LIST_NEXT(bv_scene_obj, &gdlp->dl_head_scene_obj);
	gdlp = next_gdlp;
    }
    return NULL;
}


struct bv_scene_obj *
bsg_view_obj_next_solid(struct ged *gedp, struct bv_scene_obj *sp)
{
    if (!gedp || !sp)
	return NULL;
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    gdlp = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
    while (BU_LIST_NOT_HEAD(gdlp, (struct bu_list *)ged_dl(gedp))) {
	next_gdlp = BU_LIST_PNEXT(display_list, gdlp);
	struct bv_scene_obj *cur;
	for (BU_LIST_FOR(cur, bv_scene_obj, &gdlp->dl_head_scene_obj)) {
	    if (cur != sp) continue;
	    /* Found sp - advance within this gdlp or move to next */
	    if (!BU_LIST_NEXT_IS_HEAD(sp, &gdlp->dl_head_scene_obj)) {
		return BU_LIST_PNEXT(bv_scene_obj, sp);
	    }
	    /* End of this gdlp: find next non-empty gdlp, wrapping circularly */
	    struct display_list *ng = BU_LIST_PNEXT(display_list, gdlp);
	    if (BU_LIST_IS_HEAD(ng, (struct bu_list *)ged_dl(gedp)))
		ng = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
	    struct display_list *start = ng;
	    while (BU_LIST_IS_EMPTY(&ng->dl_head_scene_obj)) {
		ng = BU_LIST_PNEXT(display_list, ng);
		if (BU_LIST_IS_HEAD(ng, (struct bu_list *)ged_dl(gedp)))
		    ng = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
		if (ng == start) return NULL;
	    }
	    return BU_LIST_NEXT(bv_scene_obj, &ng->dl_head_scene_obj);
	}
	gdlp = next_gdlp;
    }
    return NULL;
}


struct bv_scene_obj *
bsg_view_obj_prev_solid(struct ged *gedp, struct bv_scene_obj *sp)
{
    if (!gedp || !sp)
	return NULL;
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    gdlp = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
    while (BU_LIST_NOT_HEAD(gdlp, (struct bu_list *)ged_dl(gedp))) {
	next_gdlp = BU_LIST_PNEXT(display_list, gdlp);
	struct bv_scene_obj *cur;
	for (BU_LIST_FOR(cur, bv_scene_obj, &gdlp->dl_head_scene_obj)) {
	    if (cur != sp) continue;
	    /* Found sp - retreat within this gdlp or move to prev */
	    if (!BU_LIST_PREV_IS_HEAD(sp, &gdlp->dl_head_scene_obj)) {
		return BU_LIST_PLAST(bv_scene_obj, sp);
	    }
	    /* Beginning of this gdlp: find prev non-empty gdlp, wrapping circularly */
	    struct display_list *pg = BU_LIST_PLAST(display_list, gdlp);
	    if (BU_LIST_IS_HEAD(pg, (struct bu_list *)ged_dl(gedp)))
		pg = BU_LIST_PLAST(display_list, (struct display_list *)ged_dl(gedp));
	    struct display_list *start = pg;
	    while (BU_LIST_IS_EMPTY(&pg->dl_head_scene_obj)) {
		pg = BU_LIST_PLAST(display_list, pg);
		if (BU_LIST_IS_HEAD(pg, (struct bu_list *)ged_dl(gedp)))
		    pg = BU_LIST_PLAST(display_list, (struct display_list *)ged_dl(gedp));
		if (pg == start) return NULL;
	    }
	    return BU_LIST_PREV(bv_scene_obj, &pg->dl_head_scene_obj);
	}
	gdlp = next_gdlp;
    }
    return NULL;
}


void *
bsg_view_obj_group_of_solid(struct ged *gedp, struct bv_scene_obj *sp)
{
    if (!gedp || !sp)
	return NULL;
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    gdlp = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
    while (BU_LIST_NOT_HEAD(gdlp, (struct bu_list *)ged_dl(gedp))) {
	next_gdlp = BU_LIST_PNEXT(display_list, gdlp);
	struct bv_scene_obj *cur;
	for (BU_LIST_FOR(cur, bv_scene_obj, &gdlp->dl_head_scene_obj)) {
	    if (cur == sp)
		return (void *)gdlp;
	}
	gdlp = next_gdlp;
    }
    return NULL;
}


void
bsg_view_obj_foreach_group(struct ged *gedp,
			   int (*cb)(void *group_handle, void *userdata),
			   void *userdata)
{
    if (!gedp || !cb)
	return;
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    gdlp = BU_LIST_NEXT(display_list, (struct bu_list *)ged_dl(gedp));
    while (BU_LIST_NOT_HEAD(gdlp, (struct bu_list *)ged_dl(gedp))) {
	next_gdlp = BU_LIST_PNEXT(display_list, gdlp);
	if (!(*cb)((void *)gdlp, userdata))
	    return;
	gdlp = next_gdlp;
    }
}


struct bv_scene_obj *
bsg_view_obj_group_first_solid(void *group_handle)
{
    if (!group_handle)
	return NULL;
    struct display_list *gdlp = (struct display_list *)group_handle;
    if (BU_LIST_IS_EMPTY(&gdlp->dl_head_scene_obj))
	return NULL;
    return BU_LIST_NEXT(bv_scene_obj, &gdlp->dl_head_scene_obj);
}


struct bv_scene_obj *
bsg_view_obj_group_last_solid(void *group_handle)
{
    if (!group_handle)
	return NULL;
    struct display_list *gdlp = (struct display_list *)group_handle;
    if (BU_LIST_IS_EMPTY(&gdlp->dl_head_scene_obj))
	return NULL;
    return BU_LIST_PREV(bv_scene_obj, &gdlp->dl_head_scene_obj);
}


int
bsg_view_obj_group_is_nonempty(void *group_handle)
{
    if (!group_handle)
	return 0;
    struct display_list *gdlp = (struct display_list *)group_handle;
    return BU_LIST_NON_EMPTY(&gdlp->dl_head_scene_obj) ? 1 : 0;
}


struct bu_list *
bsg_view_obj_group_solid_list(void *group_handle)
{
    if (!group_handle)
	return NULL;
    return &((struct display_list *)group_handle)->dl_head_scene_obj;
}


void
bsg_view_obj_append_to_last_group(struct ged *gedp, struct bv_scene_obj *sp)
{
    if (!gedp || !sp)
	return;
    struct display_list *gdlp = BU_LIST_PREV(display_list, (struct bu_list *)ged_dl(gedp));
    BU_LIST_APPEND(gdlp->dl_head_scene_obj.back, &sp->l);
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
