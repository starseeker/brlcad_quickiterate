/*                       A C T I O N . C
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

#include <string.h>

#include "bn.h"
#include "bu/malloc.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bv/defines.h"

#include "bsg/action.h"
#include "bsg/lod_ops.h"
#include "bsg/node.h"
#include "bsg/payload.h"
#include "bsg/view_scope.h"

static void
_bsg_bbox_from_payload_or_shape(const struct bv_scene_obj *s, vect_t *lmin, vect_t *lmax)
{
    struct bsg_payload *payload = bsg_node_payload_get((const bsg_node *)s);
    if (payload && bsg_payload_bounds(payload, lmin, lmax))
	return;

    if (s->have_bbox) {
	VMOVE(*lmin, s->bmin);
	VMOVE(*lmax, s->bmax);
	return;
    }

    (*lmin)[X] = s->s_center[X] - s->s_size;
    (*lmin)[Y] = s->s_center[Y] - s->s_size;
    (*lmin)[Z] = s->s_center[Z] - s->s_size;
    (*lmax)[X] = s->s_center[X] + s->s_size;
    (*lmax)[Y] = s->s_center[Y] + s->s_size;
    (*lmax)[Z] = s->s_center[Z] + s->s_size;

}

static void
_bsg_bbox_xform(vect_t *out_min, vect_t *out_max, const vect_t in_min, const vect_t in_max, const mat_t m)
{
    point_t p, tp;
    int first = 1;

    for (int ix = 0; ix < 2; ix++) {
for (int iy = 0; iy < 2; iy++) {
    for (int iz = 0; iz < 2; iz++) {
p[X] = ix ? in_max[X] : in_min[X];
p[Y] = iy ? in_max[Y] : in_min[Y];
p[Z] = iz ? in_max[Z] : in_min[Z];
MAT4X3PNT(tp, m, p);
if (first) {
    VMOVE(*out_min, tp);
    VMOVE(*out_max, tp);
    first = 0;
} else {
    VMIN(*out_min, tp);
    VMAX(*out_max, tp);
}
    }
}
    }
}

static int
_bsg_action_traverse(struct bsg_action *action, struct bv_scene_obj *node, const mat_t parent_mat, int depth)
{
    if (!action || !node)
return 1;

    if (bsg_node_has_kind((bsg_node *)node, BSG_NODE_VIEW_SCOPE) &&
	!bsg_view_scope_visible((bsg_node *)node, action->view))
	return 1;

    mat_t world;
    MAT_COPY(world, parent_mat);
    if (bsg_node_has_kind((bsg_node *)node, BSG_NODE_TRANSFORM)) {
	mat_t nmat;
	bsg_node_transform_get((const bsg_node *)node, nmat);
	bn_mat_mul(world, parent_mat, nmat);
    }

    if (action->node_cb) {
	action->current_depth = depth;
	int result = action->node_cb(action, (bsg_node *)node, world);
	if (result == BSG_ACTION_STOP) {
	    action->stopped = 1;
	    return 0;
	} else if (result == BSG_ACTION_ERROR) {
	    action->error = 1;
	    return 0;
	}
    }

    if (bsg_node_has_kind((bsg_node *)node, BSG_NODE_LOD)) {
int nlevels = bsg_lod_node_level_count((bsg_node *)node);
if (nlevels <= 0)
    return 1;

int level = action->lod_level;
if (level < 0)
    level = bsg_lod_node_active_level((bsg_node *)node, action->view);
if (level < 0 || level >= nlevels)
    level = 0;

struct bv_scene_obj *child = (struct bv_scene_obj *)bsg_node_child((bsg_node *)node, (size_t)level);
if (!child)
    return 1;
	return _bsg_action_traverse(action, child, world, depth + 1);
    }

    for (size_t i = 0; i < bsg_node_child_count((bsg_node *)node); i++) {
struct bv_scene_obj *child = (struct bv_scene_obj *)bsg_node_child((bsg_node *)node, i);
if (!child)
    continue;
	if (!_bsg_action_traverse(action, child, world, depth + 1))
	    return 0;
    }

    return 1;
}

void
bsg_action_init(struct bsg_action *action,
int (*node_cb)(struct bsg_action *action, bsg_node *node, const mat_t world))
{
    if (!action)
return;

    memset(action, 0, sizeof(*action));
    action->node_cb = node_cb;
    action->lod_level = -1;
}

void
bsg_action_set_view(struct bsg_action *action, struct bview *view)
{
    if (!action)
return;
    action->view = view;
}

void
bsg_action_set_include_overlays(struct bsg_action *action, int include_overlays)
{
    if (!action)
return;
    action->include_overlays = include_overlays ? 1 : 0;
}

void
bsg_action_set_lod_level(struct bsg_action *action, int lod_level)
{
    if (!action)
return;
    action->lod_level = lod_level;
}

int
bsg_action_apply(struct bsg_action *action, bsg_node *root)
{
    mat_t ident;

    if (!action || !root)
return 0;

    action->stopped = 0;
    action->error = 0;

    MAT_IDN(ident);
    _bsg_action_traverse(action, (struct bv_scene_obj *)root, ident, 0);

    return action->error ? 0 : 1;
}

static int
_bsg_bbox_cb(struct bsg_action *base, bsg_node *node, const mat_t world)
{
    struct bsg_bbox_action *action = (struct bsg_bbox_action *)base;
    struct bv_scene_obj *s = (struct bv_scene_obj *)node;
    vect_t lmin, lmax, wmin, wmax;

    if (!bsg_node_has_kind((const bsg_node *)s, BSG_NODE_SHAPE))
return BSG_ACTION_CONTINUE;

    if (!base->include_overlays &&
	(bsg_node_get_payload_type((const bsg_node *)s) & BSG_PAYLOAD_OVERLAY))
return BSG_ACTION_CONTINUE;

    _bsg_bbox_from_payload_or_shape(s, &lmin, &lmax);

    _bsg_bbox_xform(&wmin, &wmax, lmin, lmax, world);

    if (!action->have_bounds) {
VMOVE(action->bmin, wmin);
VMOVE(action->bmax, wmax);
action->have_bounds = 1;
    } else {
VMIN(action->bmin, wmin);
VMAX(action->bmax, wmax);
    }

    return BSG_ACTION_CONTINUE;
}

void
bsg_bbox_action_init(struct bsg_bbox_action *action)
{
    if (!action)
return;

    bsg_action_init(&action->base, _bsg_bbox_cb);
    VSETALL(action->bmin, INFINITY);
    VSETALL(action->bmax, -INFINITY);
    action->have_bounds = 0;
}

int
bsg_bbox_action_result(const struct bsg_bbox_action *action, vect_t *bmin, vect_t *bmax)
{
    if (!action || !action->have_bounds)
return 0;

    if (bmin)
VMOVE(*bmin, action->bmin);
    if (bmax)
VMOVE(*bmax, action->bmax);

    return 1;
}

static int
_bsg_search_match(const struct bsg_search_action *action, const struct bv_scene_obj *node)
{
    struct bsg_identity nid;

    if (action->use_name) {
	const char *nname = bsg_node_name((const bsg_node *)node);
	if (!nname || !BU_STR_EQUAL(action->name, nname))
	    return 0;
    }

    if (action->use_kind_mask) {
if ((bsg_node_kind((const bsg_node *)node) & action->kind_mask) != action->kind_mask)
    return 0;
    }

    if (action->use_payload_mask) {
if ((bsg_node_get_payload_type((const bsg_node *)node) & action->payload_mask) != action->payload_mask)
    return 0;
    }

    if (action->use_node_id) {
	if (!bsg_node_identity_get((const bsg_node *)node, &nid))
	    return 0;
	if (nid.node_id.value != action->node_id.value)
	    return 0;
    }

    if (action->use_parent) {
	if (bsg_node_parent((const bsg_node *)node) != action->parent)
	    return 0;
    }

    if (action->use_source_path) {
if (!bsg_node_identity_get((const bsg_node *)node, &nid))
    return 0;
if (nid.node_id.value != action->source_path_hash)
    return 0;
if (action->source_path_kind != BSG_SOURCE_UNKNOWN && nid.source_kind != action->source_path_kind)
    return 0;
    }

    if (action->use_material_source) {
	struct bsg_material m;
	if (!bsg_node_material_get((const bsg_node *)node, &m))
	    return 0;
	if (m.source_kind != action->material_source)
	    return 0;
    }

    if (action->use_depth_range) {
	int depth = action->base.current_depth;
	if (depth < action->min_depth)
	    return 0;
	if (action->max_depth >= 0 && depth > action->max_depth)
	    return 0;
    }

    return 1;
}

static int
_bsg_search_cb(struct bsg_action *base, bsg_node *node, const mat_t UNUSED(world))
{
    struct bsg_search_action *action = (struct bsg_search_action *)base;

    if (!_bsg_search_match(action, (const struct bv_scene_obj *)node))
return BSG_ACTION_CONTINUE;

    bu_ptbl_ins(action->results, (long *)node);

    if (action->max_results > 0 && BU_PTBL_LEN(action->results) >= action->max_results)
return BSG_ACTION_STOP;

    return BSG_ACTION_CONTINUE;
}

void
bsg_search_action_init(struct bsg_search_action *action)
{
    if (!action)
return;

    memset(action, 0, sizeof(*action));
    bsg_action_init(&action->base, _bsg_search_cb);
    BU_ALLOC(action->results, struct bu_ptbl);
    bu_ptbl_init(action->results, 16, "bsg_search_action_results");
}

void
bsg_search_action_reset(struct bsg_search_action *action)
{
    if (!action)
return;

    if (action->name)
bu_free(action->name, "bsg_search_name");
    action->name = NULL;
    action->use_name = 0;
    action->use_kind_mask = 0;
    action->use_payload_mask = 0;
    action->use_node_id = 0;
    action->use_parent = 0;
    action->use_source_path = 0;
    action->use_material_source = 0;
    action->use_depth_range = 0;
    action->min_depth = 0;
    action->max_depth = -1;
    action->max_results = 0;
    if (action->results) {
	bu_ptbl_free(action->results);
	BU_PUT(action->results, struct bu_ptbl);
    }
    action->results = NULL;
}

void
bsg_search_action_add_name_criteria(struct bsg_search_action *action, const char *name)
{
    if (!action || !name)
return;

    if (action->name)
bu_free(action->name, "bsg_search_name");
    action->name = bu_strdup(name);
    action->use_name = 1;
}

void
bsg_search_action_add_kind_criteria(struct bsg_search_action *action, unsigned long long kind_mask)
{
    if (!action || !kind_mask)
return;

    action->kind_mask = kind_mask;
    action->use_kind_mask = 1;
}

void
bsg_search_action_add_payload_criteria(struct bsg_search_action *action, unsigned long long payload_mask)
{
    if (!action || !payload_mask)
return;

    action->payload_mask = payload_mask;
    action->use_payload_mask = 1;
}

void
bsg_search_action_add_node_id_criteria(struct bsg_search_action *action, struct bsg_node_id node_id)
{
    if (!action)
return;

    action->node_id = node_id;
    action->use_node_id = 1;
}

void
bsg_search_action_add_parent_criteria(struct bsg_search_action *action, bsg_node *parent)
{
    if (!action || !parent)
	return;

    action->parent = parent;
    action->use_parent = 1;
}

void
bsg_search_action_add_source_path_criteria(struct bsg_search_action *action,
					 const char *path,
					 enum bsg_source_kind source_kind)
{
    struct bsg_identity id;

    if (!action || !path)
return;

    bsg_identity_from_path_str(&id, path, source_kind);
    action->source_path_hash = id.node_id.value;
    action->source_path_kind = source_kind;
    action->use_source_path = 1;
}

void
bsg_search_action_add_material_source_criteria(struct bsg_search_action *action,
       enum bsg_material_source source)
{
    if (!action)
return;

    action->material_source = source;
    action->use_material_source = 1;
}

void
bsg_search_action_set_max_results(struct bsg_search_action *action, size_t max_results)
{
    if (!action)
return;

    action->max_results = max_results;
}

void
bsg_search_action_set_depth_range(struct bsg_search_action *action, int min_depth, int max_depth)
{
    if (!action || min_depth < 0)
	return;

    action->use_depth_range = 1;
    action->min_depth = min_depth;
    action->max_depth = max_depth;
}

size_t
bsg_search_action_result_count(const struct bsg_search_action *action)
{
    if (!action || !action->results)
return 0;

    return BU_PTBL_LEN(action->results);
}

bsg_node *
bsg_search_action_result_node(const struct bsg_search_action *action, size_t idx)
{
    if (!action || !action->results)
return NULL;

    if (idx >= BU_PTBL_LEN(action->results))
return NULL;

    return (bsg_node *)BU_PTBL_GET(action->results, idx);
}

static int
_bsg_collect_append(struct bsg_collect_action *action,
    bsg_node *node,
    struct bsg_payload *payload,
    const mat_t world,
    const vect_t bmin,
    const vect_t bmax)
{
    if (action->shape_count == action->shape_capacity) {
size_t new_cap = action->shape_capacity ? action->shape_capacity * 2 : 16;
action->shapes = (struct bsg_collect_shape *)bu_realloc(action->shapes,
new_cap * sizeof(struct bsg_collect_shape), "bsg_collect_shapes");
action->shape_capacity = new_cap;
    }

    struct bsg_collect_shape *dst = &action->shapes[action->shape_count++];
    dst->node = node;
    dst->payload = payload;
    MAT_COPY(dst->world, world);
    VMOVE(dst->bmin, bmin);
    VMOVE(dst->bmax, bmax);

    return 1;
}

static int
_bsg_collect_cb(struct bsg_action *base, bsg_node *node, const mat_t world)
{
    struct bsg_collect_action *action = (struct bsg_collect_action *)base;
    struct bv_scene_obj *s = (struct bv_scene_obj *)node;
    vect_t lmin, lmax, wmin, wmax;

    if (!bsg_node_has_kind((const bsg_node *)s, BSG_NODE_SHAPE))
return BSG_ACTION_CONTINUE;

    if (!base->include_overlays &&
	(bsg_node_get_payload_type((const bsg_node *)s) & BSG_PAYLOAD_OVERLAY))
return BSG_ACTION_CONTINUE;

    if (!bsg_node_visible((const bsg_node *)s))
return BSG_ACTION_CONTINUE;

    _bsg_bbox_from_payload_or_shape(s, &lmin, &lmax);

    _bsg_bbox_xform(&wmin, &wmax, lmin, lmax, world);

    if (!_bsg_collect_append(action,
    node,
    bsg_node_payload_get((const bsg_node *)s),
    world,
    wmin,
    wmax))
return BSG_ACTION_ERROR;

    return BSG_ACTION_CONTINUE;
}

void
bsg_collect_action_init(struct bsg_collect_action *action)
{
    if (!action)
return;

    memset(action, 0, sizeof(*action));
    bsg_action_init(&action->base, _bsg_collect_cb);
}

void
bsg_collect_action_reset(struct bsg_collect_action *action)
{
    if (!action)
return;

    if (action->shapes)
bu_free(action->shapes, "bsg_collect_shapes");
    action->shapes = NULL;
    action->shape_count = 0;
    action->shape_capacity = 0;
}

const struct bsg_collect_shape *
bsg_collect_action_shapes(const struct bsg_collect_action *action, size_t *shape_count)
{
    if (shape_count)
*shape_count = (action) ? action->shape_count : 0;

    if (!action)
return NULL;

    return action->shapes;
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
