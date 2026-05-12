/*                    P A Y L O A D . C
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
/** @file libbsg/payload.c
 *
 * Phase 4: typed payload model and geometry-container helpers.
 */

#include "common.h"

#include <string.h>

#include "bu/list.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/vlist.h"
#include "bsg/defines.h"
#include "bsg/field.h"
#include "bsg/identity.h"
#include "bsg/node.h"
#include "bsg/payload.h"

#include "./bsg_private.h"


struct _bsg_payload_vlist {
    struct bsg_payload base;
    bsg_node *owner;
};

struct _bsg_payload_wire {
    struct bsg_payload base;
    size_t polyline_count;
    struct bsg_wire_polyline *polylines;
};

struct _bsg_payload_mesh {
    struct bsg_payload base;
    const struct bv_mesh_lod *lod;
};

/* ------------------------------------------------------------------ */
/* Phase 10C: payload storage via bsg_node_core                         */
/*                                                                      */
/* The global _bsg_payload_map hash table (Phase 4) has been replaced   */
/* by a struct bsg_payload * stored in bsg_node_core::payload.          */
/* The pointer is owned by the core; _bsg_core_release() (called from   */
/* bv_obj_reset) calls bsg_payload_destroy() on it.                     */
/* ------------------------------------------------------------------ */

/* Return the payload currently attached to @p n from the core, or NULL. */
static struct bsg_payload *
_bsg_payload_sc_get(const bsg_node *n)
{
    if (!n)
	return NULL;

    if (n->bsg_magic != BSG_NODE_CORE_MAGIC)
	return NULL;
    return (struct bsg_payload *)n->payload;
}

/* Store @p payload in the core for @p n (NULL clears it without destroy). */
static void
_bsg_payload_sc_set(const bsg_node *n, struct bsg_payload *payload)
{
    bsg_node *core;

    if (!n)
	return;

    core = _bsg_core_ensure((bsg_node *)n);
    if (!core)
	return;

    /* Note: caller is responsible for destroying the old payload first
     * (see bsg_node_payload_set).  We just store the new pointer. */
    core->payload = payload;
}

static unsigned long long
_payload_type_to_flags(enum bsg_payload_type type)
{
    switch (type) {
	case BSG_PAYLOAD_TYPE_VLIST:
	    return BSG_PAYLOAD_VLIST;
	case BSG_PAYLOAD_TYPE_MESH:
	    return BSG_PAYLOAD_MESH;
	case BSG_PAYLOAD_TYPE_BREP_REF:
	    return BSG_PAYLOAD_BREP;
	case BSG_PAYLOAD_TYPE_CSG_REF:
	    return BSG_PAYLOAD_CSG;
	case BSG_PAYLOAD_TYPE_OVERLAY:
	    return BSG_PAYLOAD_OVERLAY;
	case BSG_PAYLOAD_TYPE_IMAGE:
	    return BSG_PAYLOAD_IMAGE;
	default:
	    return 0;
    }
}

static enum bsg_payload_type
_payload_flags_to_type(unsigned long long flags)
{
    if (flags & BSG_PAYLOAD_VLIST)
	return BSG_PAYLOAD_TYPE_VLIST;
    if (flags & BSG_PAYLOAD_MESH)
	return BSG_PAYLOAD_TYPE_MESH;
    if (flags & BSG_PAYLOAD_BREP)
	return BSG_PAYLOAD_TYPE_BREP_REF;
    if (flags & BSG_PAYLOAD_CSG)
	return BSG_PAYLOAD_TYPE_CSG_REF;
    if (flags & BSG_PAYLOAD_OVERLAY)
	return BSG_PAYLOAD_TYPE_OVERLAY;
    if (flags & BSG_PAYLOAD_IMAGE)
	return BSG_PAYLOAD_TYPE_IMAGE;
    return BSG_PAYLOAD_TYPE_NONE;
}

static void
_payload_wire_free(struct bsg_payload *payload)
{
    size_t i = 0;
    struct _bsg_payload_wire *wp = (struct _bsg_payload_wire *)payload;
    if (!wp)
	return;
    for (i = 0; i < wp->polyline_count; i++) {
	if (wp->polylines[i].points)
	    bu_free(wp->polylines[i].points, "bsg wire polyline points");
	wp->polylines[i].points = NULL;
	wp->polylines[i].point_count = 0;
    }
    if (wp->polylines)
	bu_free(wp->polylines, "bsg wire polylines");
    wp->polylines = NULL;
    wp->polyline_count = 0;
}

static size_t
_wire_append_polyline(struct _bsg_payload_wire *wp,
		      point_t *cur_pts,
		      size_t cur_cnt,
		      size_t poly_cap)
{
    struct bsg_wire_polyline *pl = NULL;
    size_t i = 0;
    size_t ncap = 0;

    if (!wp || !cur_pts || cur_cnt <= 1)
	return poly_cap;

    if (wp->polyline_count + 1 > poly_cap) {
	ncap = (poly_cap == 0) ? 8 : poly_cap * 2;
	wp->polylines = (struct bsg_wire_polyline *)bu_realloc(
		wp->polylines, ncap * sizeof(struct bsg_wire_polyline),
		"bsg wire polylines grow");
	for (i = poly_cap; i < ncap; i++) {
	    wp->polylines[i].point_count = 0;
	    wp->polylines[i].points = NULL;
	}
	poly_cap = ncap;
    }

    pl = &wp->polylines[wp->polyline_count++];
    pl->point_count = cur_cnt;
    pl->points = (point_t *)bu_malloc(cur_cnt * sizeof(point_t),
				      "bsg wire polyline points");
    memcpy(pl->points, cur_pts, cur_cnt * sizeof(point_t));
    return poly_cap;
}

struct bsg_payload *
bsg_payload_create(enum bsg_payload_type type)
{
    struct bsg_payload *p = NULL;

    switch (type) {
	case BSG_PAYLOAD_TYPE_VLIST: {
	    struct _bsg_payload_vlist *vp = NULL;
	    BU_ALLOC(vp, struct _bsg_payload_vlist);
	    memset(vp, 0, sizeof(*vp));
	    p = &vp->base;
	    break;
	}
	case BSG_PAYLOAD_TYPE_WIRE: {
	    struct _bsg_payload_wire *wp = NULL;
	    BU_ALLOC(wp, struct _bsg_payload_wire);
	    memset(wp, 0, sizeof(*wp));
	    p = &wp->base;
	    break;
	}
	case BSG_PAYLOAD_TYPE_MESH: {
	    struct _bsg_payload_mesh *mp = NULL;
	    BU_ALLOC(mp, struct _bsg_payload_mesh);
	    memset(mp, 0, sizeof(*mp));
	    p = &mp->base;
	    break;
	}
	default:
	    BU_ALLOC(p, struct bsg_payload);
	    memset(p, 0, sizeof(*p));
	    break;
    }

    if (!p)
	return NULL;

    p->type = type;
    if (type == BSG_PAYLOAD_TYPE_WIRE)
	p->free_fn = _payload_wire_free;
    return p;
}

void
bsg_payload_destroy(struct bsg_payload *payload)
{
    if (!payload)
	return;

    if (payload->free_fn)
	payload->free_fn(payload);
    bu_free(payload, "bsg payload");
}

struct bsg_payload *
bsg_node_payload_get(const bsg_node *n)
{
    struct bsg_payload *p = NULL;
    enum bsg_payload_type ptype = BSG_PAYLOAD_TYPE_NONE;
    struct bv_scene_obj *s = NULL;

    if (!n)
	return NULL;

    p = _bsg_payload_sc_get(n);
    if (p)
	return p;

    ptype = _payload_flags_to_type(bsg_node_get_payload_type(n));
    if (ptype == BSG_PAYLOAD_TYPE_NONE)
	return NULL;

    if (ptype == BSG_PAYLOAD_TYPE_VLIST) {
	return bsg_payload_vlist_from_node((bsg_node *)n);
    }

    p = bsg_payload_create(ptype);
    if (!p)
	return NULL;
    s = (struct bv_scene_obj *)n;
    if (ptype == BSG_PAYLOAD_TYPE_MESH && s->mesh_obj && s->draw_data)
	bsg_payload_mesh_set(p, (const struct bv_mesh_lod *)s->draw_data);
    _bsg_payload_sc_set(n, p);
    return p;
}

void
bsg_node_payload_set(bsg_node *n, struct bsg_payload *payload)
{
    struct bsg_payload *old = NULL;

    if (!n)
	return;

    old = _bsg_payload_sc_get(n);
    if (old && old != payload)
	bsg_payload_destroy(old);

    /* Store the new pointer (clear from core if payload==NULL so that
     * bsg_node_get_payload_type returning 0 is consistent). */
    _bsg_payload_sc_set(n, payload);
    if (!payload) {
	bsg_node_set_payload_type(n, 0);
	return;
    }

    bsg_node_set_payload_type(n, _payload_type_to_flags(payload->type));
}

enum bsg_payload_type
bsg_payload_type(const struct bsg_payload *payload)
{
    if (!payload)
	return BSG_PAYLOAD_TYPE_NONE;
    return payload->type;
}

uint64_t
bsg_payload_revision(const struct bsg_payload *payload)
{
    if (!payload)
	return 0;
    return payload->revision;
}

uint64_t
bsg_payload_bump_revision(struct bsg_payload *payload)
{
    if (!payload)
	return 0;
    payload->revision++;
    return payload->revision;
}

int
bsg_payload_bounds(const struct bsg_payload *payload, point_t *bmin, point_t *bmax)
{
    if (!payload)
	return 0;

    if (payload->type == BSG_PAYLOAD_TYPE_VLIST) {
	const struct _bsg_payload_vlist *vp = (const struct _bsg_payload_vlist *)payload;
	struct bv_scene_obj *s = (struct bv_scene_obj *)vp->owner;
	struct bv_vlist *tvp = NULL;
	int have_pt = 0;
	if (!s)
	    return 0;
	if (bv_vlist_bbox(&s->s_vlist, bmin, bmax, NULL, NULL))
	    return 1;

	for (BU_LIST_FOR(tvp, bv_vlist, &s->s_vlist)) {
	    size_t j = 0;
	    for (j = 0; j < tvp->nused; j++) {
		point_t *pt = &tvp->pt[j];
		if (!have_pt) {
		    if (bmin)
			VMOVE((*bmin), *pt);
		    if (bmax)
			VMOVE((*bmax), *pt);
		    have_pt = 1;
		} else {
		    if (bmin)
			VMIN((*bmin), *pt);
		    if (bmax)
			VMAX((*bmax), *pt);
		}
	    }
	}
	return have_pt ? 1 : 0;
    }

    if (payload->type == BSG_PAYLOAD_TYPE_MESH) {
	const struct _bsg_payload_mesh *mp = (const struct _bsg_payload_mesh *)payload;
	if (!mp->lod)
	    return 0;
	if (bmin)
	    VMOVE((*bmin), mp->lod->bmin);
	if (bmax)
	    VMOVE((*bmax), mp->lod->bmax);
	return 1;
    }

    return 0;
}

struct bsg_payload *
bsg_payload_vlist_from_node(bsg_node *n)
{
    struct bsg_payload *p = NULL;
    struct _bsg_payload_vlist *vp = NULL;

    if (!n)
	return NULL;

    p = _bsg_payload_sc_get(n);
    if (p && p->type == BSG_PAYLOAD_TYPE_VLIST)
	return p;
    if (p && p->type != BSG_PAYLOAD_TYPE_VLIST)
	return NULL;

    p = bsg_payload_create(BSG_PAYLOAD_TYPE_VLIST);
    if (!p)
	return NULL;

    vp = (struct _bsg_payload_vlist *)p;
    vp->owner = n;
    p->source = (void *)n;
    _bsg_payload_sc_set(n, p);
    bsg_node_set_payload_type(n, bsg_node_get_payload_type(n) | BSG_PAYLOAD_VLIST);
    return p;
}

void
bsg_payload_vlist_set(struct bsg_payload *payload, struct bu_list *vhead)
{
    struct _bsg_payload_vlist *vp = NULL;
    struct bv_scene_obj *s = NULL;

    if (!payload || payload->type != BSG_PAYLOAD_TYPE_VLIST)
	return;
    if (!vhead)
	return;

    vp = (struct _bsg_payload_vlist *)payload;
    if (!vp->owner)
	return;
    s = (struct bv_scene_obj *)vp->owner;

    if (BU_LIST_NON_EMPTY(&s->s_vlist))
	BV_FREE_VLIST(s->vlfree, &s->s_vlist);
    BU_LIST_INIT(&s->s_vlist);
    bv_vlist_copy(s->vlfree, &s->s_vlist, vhead);
    s->s_vlen = bv_vlist_cmd_cnt((struct bv_vlist *)&s->s_vlist);

    (void)bsg_payload_bump_revision(payload);
    payload->bounds_revision = payload->revision;
    bsg_node_field_touch((bsg_node *)s, BSG_FIELD_PAYLOAD);
    (void)bsg_node_bump_revision((bsg_node *)s, BSG_NODE_REV_PAYLOAD);
    bsg_node_mark_stale((bsg_node *)s);
}

struct bu_list *
bsg_payload_vlist_head(const struct bsg_payload *payload)
{
    const struct _bsg_payload_vlist *vp = NULL;
    struct bv_scene_obj *s = NULL;

    if (!payload || payload->type != BSG_PAYLOAD_TYPE_VLIST)
	return NULL;

    vp = (const struct _bsg_payload_vlist *)payload;
    if (!vp->owner)
	return NULL;
    s = (struct bv_scene_obj *)vp->owner;
    return (struct bu_list *)&s->s_vlist;
}

size_t
bsg_payload_vlist_count(const struct bsg_payload *payload)
{
    const struct _bsg_payload_vlist *vp = NULL;
    struct bv_scene_obj *s = NULL;

    if (!payload || payload->type != BSG_PAYLOAD_TYPE_VLIST)
	return 0;
    vp = (const struct _bsg_payload_vlist *)payload;
    if (!vp->owner)
	return 0;
    s = (struct bv_scene_obj *)vp->owner;
    return bv_vlist_cmd_cnt((struct bv_vlist *)&s->s_vlist);
}

struct bsg_payload *
bsg_payload_wire_from_vlist(const struct bsg_payload *vlist_payload)
{
    const struct _bsg_payload_vlist *vp = NULL;
    struct bv_scene_obj *s = NULL;
    struct _bsg_payload_wire *wp = NULL;
    struct bsg_payload *wire = NULL;
    struct bv_vlist *tvp = NULL;
    point_t *cur_pts = NULL;
    size_t cur_cnt = 0;
    size_t cur_cap = 0;
    size_t poly_cap = 0;

    if (!vlist_payload || vlist_payload->type != BSG_PAYLOAD_TYPE_VLIST)
	return NULL;

    vp = (const struct _bsg_payload_vlist *)vlist_payload;
    if (!vp->owner)
	return NULL;
    s = (struct bv_scene_obj *)vp->owner;

    wire = bsg_payload_create(BSG_PAYLOAD_TYPE_WIRE);
    if (!wire)
	return NULL;
    wp = (struct _bsg_payload_wire *)wire;

    for (BU_LIST_FOR(tvp, bv_vlist, &s->s_vlist)) {
	int *cmd = tvp->cmd;
	point_t *pt = tvp->pt;
	int nused = tvp->nused;
	int j = 0;

	for (j = 0; j < nused; j++, cmd++, pt++) {
	    int is_move = 0;
	    int is_draw = 0;
	    if (*cmd == BV_VLIST_LINE_MOVE || *cmd == BV_VLIST_POLY_MOVE || *cmd == BV_VLIST_TRI_MOVE)
		is_move = 1;
	    if (*cmd == BV_VLIST_LINE_DRAW || *cmd == BV_VLIST_POLY_DRAW || *cmd == BV_VLIST_TRI_DRAW)
		is_draw = 1;

	    if (is_move) {
		poly_cap = _wire_append_polyline(wp, cur_pts, cur_cnt, poly_cap);
		cur_cnt = 0;
		if (cur_cnt + 1 > cur_cap) {
		    size_t ncap = (cur_cap == 0) ? 16 : cur_cap * 2;
		    cur_pts = (point_t *)bu_realloc(cur_pts, ncap * sizeof(point_t),
						    "bsg wire temp points grow");
		    cur_cap = ncap;
		}
		VMOVE(cur_pts[cur_cnt], *pt);
		cur_cnt++;
		continue;
	    }

	    if (is_draw) {
		if (cur_cnt + 1 > cur_cap) {
		    size_t ncap = (cur_cap == 0) ? 16 : cur_cap * 2;
		    cur_pts = (point_t *)bu_realloc(cur_pts, ncap * sizeof(point_t),
						    "bsg wire temp points grow");
		    cur_cap = ncap;
		}
		VMOVE(cur_pts[cur_cnt], *pt);
		cur_cnt++;
	    }
	}
    }
    poly_cap = _wire_append_polyline(wp, cur_pts, cur_cnt, poly_cap);

    if (cur_pts)
	bu_free(cur_pts, "bsg wire temp points");
    wire->source = (void *)vlist_payload;
    wire->revision = vlist_payload->revision;
    return wire;
}

size_t
bsg_payload_wire_polyline_count(const struct bsg_payload *wire_payload)
{
    const struct _bsg_payload_wire *wp = NULL;
    if (!wire_payload || wire_payload->type != BSG_PAYLOAD_TYPE_WIRE)
	return 0;
    wp = (const struct _bsg_payload_wire *)wire_payload;
    return wp->polyline_count;
}

const struct bsg_wire_polyline *
bsg_payload_wire_polyline_get(const struct bsg_payload *wire_payload, size_t idx)
{
    const struct _bsg_payload_wire *wp = NULL;
    if (!wire_payload || wire_payload->type != BSG_PAYLOAD_TYPE_WIRE)
	return NULL;
    wp = (const struct _bsg_payload_wire *)wire_payload;
    if (idx >= wp->polyline_count)
	return NULL;
    return &wp->polylines[idx];
}

void
bsg_payload_mesh_set(struct bsg_payload *payload, const struct bv_mesh_lod *lod)
{
    struct _bsg_payload_mesh *mp = NULL;
    if (!payload || payload->type != BSG_PAYLOAD_TYPE_MESH)
	return;
    mp = (struct _bsg_payload_mesh *)payload;
    mp->lod = lod;
    payload->source = (void *)lod;
    (void)bsg_payload_bump_revision(payload);
}

const struct bv_mesh_lod *
bsg_payload_mesh_get(const struct bsg_payload *payload)
{
    const struct _bsg_payload_mesh *mp = NULL;
    if (!payload || payload->type != BSG_PAYLOAD_TYPE_MESH)
	return NULL;
    mp = (const struct _bsg_payload_mesh *)payload;
    return mp->lod;
}

const struct bv_mesh_lod *
bsg_payload_mesh_lod_get(const struct bsg_payload *payload)
{
    return bsg_payload_mesh_get(payload);
}

void
bsg_node_set_payload_type(bsg_node *node, unsigned long long payload_flags)
{
    unsigned long long new_flags;

    if (!node)
	return;

    new_flags = (bsg_node_kind(node) & ~BSG_PAYLOAD_MASK) |
		(payload_flags & BSG_PAYLOAD_MASK);
    node->bsg_kind = new_flags;

    bsg_node_field_touch(node, BSG_FIELD_PAYLOAD);
    (void)bsg_node_bump_revision(node, BSG_NODE_REV_PAYLOAD);
}

unsigned long long
bsg_node_get_payload_type(const bsg_node *node)
{
    if (!node)
	return 0;

    return bsg_node_kind(node) & BSG_PAYLOAD_MASK;
}

void
bsg_payload_dispatch(void *dmp, bsg_node *node, struct bview *v)
{
    struct bsg_payload *p = NULL;
    struct bv_scene_obj *s = NULL;
    unsigned long long ptype = 0;

    if (!node)
	return;

    p = bsg_node_payload_get(node);
    if (p && p->update_fn) {
	(void)dmp;
	(void)p->update_fn(p, v);
	return;
    }

    ptype = bsg_node_get_payload_type(node);
    if (!ptype || (ptype & BSG_PAYLOAD_VLIST))
	return;

    s = (struct bv_scene_obj *)node;
    if (s->s_update_callback) {
	(void)dmp;
	(*s->s_update_callback)(s, v, 0);
    }
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
