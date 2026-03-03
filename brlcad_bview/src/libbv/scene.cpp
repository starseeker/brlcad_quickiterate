/*                       S C E N E . C P P
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
/** @file scene.cpp
 *
 * Implementation of the BRL-CAD scene graph API (bv_scene / bv_node).
 *
 * The types defined here are the BRL-CAD native analogs to the following
 * Coin3D/Inventor types, designed so that the two representations can
 * eventually be translated to one another:
 *
 *   bv_node       <->  SoNode  (base scene graph element)
 *   bv_scene      <->  SoSceneManager scene root (SoSeparator-based)
 *   bview_new     <->  SoSceneManager / SoRenderArea (render context)
 *   bview_camera  <->  SoCamera (orthographic or perspective)
 *   bview_material <-> SoMaterial
 *   bview_viewport <-> SbViewportRegion
 *
 * No Coin3D/obol headers are referenced here.  Translation to Coin3D types
 * is intended to be done at the rendering backend layer.
 */

#include "common.h"

#include <string.h>

#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/color.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bn/mat.h"
#include "bv/defines.h"
#include "bv/lod.h"
#include "bv/util.h"
#include "bv/view_sets.h"
#include "bv/vlist.h"
#include "./bv_private.h"


/* Default DPI used when migrating from a legacy bview that does not store DPI */
#define BV_DEFAULT_DPI 96.0

struct bv_node *
bv_node_create(const char *name, enum bv_node_type type)
{
    struct bv_node *n;
    BU_GET(n, struct bv_node);

    n->type = type;
    bu_vls_init(&n->name);
    if (name)
	bu_vls_strcpy(&n->name, name);

    n->parent = NULL;
    bu_ptbl_init(&n->children, 8, "bv_node children");

    MAT_IDN(n->transform);
    MAT_IDN(n->world_transform);

    n->geometry  = NULL;
    memset(&n->material, 0, sizeof(struct bview_material));
    n->visible   = 1;
    n->user_data = NULL;
    n->lod_level = 0;
    n->selected  = 0;

    n->have_bounds = 0;
    VSETALL(n->bounds_min, 0.0);
    VSETALL(n->bounds_max, 0.0);

    n->dlist         = 0;
    n->dlist_stale   = 0;
    n->update_cb     = NULL;
    n->update_cb_data = NULL;
    n->draw_data     = NULL;

    return n;
}


void
bv_node_destroy(struct bv_node *node)
{
    if (!node)
	return;

    /* Recursively destroy children */
    for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	struct bv_node *child = (struct bv_node *)BU_PTBL_GET(&node->children, i);
	if (child)
	    bv_node_destroy(child);
    }

    bu_ptbl_free(&node->children);
    bu_vls_free(&node->name);
    BU_PUT(node, struct bv_node);
}


void
bv_node_transform_set(struct bv_node *node, const mat_t xform)
{
    if (!node)
	return;
    MAT_COPY(node->transform, xform);
    /* Invalidate cached world transform - caller must recompute if needed */
    MAT_IDN(node->world_transform);
}


const mat_t *
bv_node_transform_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return (const mat_t *)&node->transform;
}


void
bv_node_geometry_set(struct bv_node *node, const void *geometry)
{
    if (!node)
	return;
    node->geometry = (void *)geometry;
}


const void *
bv_node_geometry_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return node->geometry;
}


void
bv_node_material_set(struct bv_node *node, const struct bview_material *material)
{
    if (!node || !material)
	return;
    node->material = *material;
}


const struct bview_material *
bv_node_material_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return &node->material;
}


void
bv_node_add_child(struct bv_node *parent, struct bv_node *child)
{
    if (!parent || !child)
	return;
    child->parent = parent;
    bu_ptbl_ins_unique(&parent->children, (long *)child);
}


void
bv_node_remove_child(struct bv_node *parent, struct bv_node *child)
{
    if (!parent || !child)
	return;
    bu_ptbl_rm(&parent->children, (long *)child);
    child->parent = NULL;
}


const struct bu_ptbl *
bv_node_children(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return &node->children;
}


size_t
bv_node_child_count(const struct bv_node *node)
{
    if (!node)
	return 0;
    return BU_PTBL_LEN(&node->children);
}


void
bv_node_detach(struct bv_node *node)
{
    if (!node || !node->parent)
	return;
    bv_node_remove_child(node->parent, node);
}


size_t
bv_node_subtree_size(const struct bv_node *node)
{
    if (!node)
	return 0;
    size_t count = 1;
    for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	const struct bv_node *ch = (const struct bv_node *)BU_PTBL_GET(&node->children, i);
	count += bv_node_subtree_size(ch);
    }
    return count;
}


void
bv_node_visible_set(struct bv_node *node, int visible)
{
    if (!node)
	return;
    node->visible = visible;
}


int
bv_node_visible_get(const struct bv_node *node)
{
    if (!node)
	return 0;
    return node->visible;
}


void
bv_node_selected_set(struct bv_node *node, int selected)
{
    if (!node)
	return;
    node->selected = selected;
}


int
bv_node_selected_get(const struct bv_node *node)
{
    if (!node)
	return 0;
    return node->selected;
}


/* --- Native bounds accessors --- */

void
bv_node_bounds_set(struct bv_node *node, const point_t min, const point_t max)
{
    if (!node)
	return;
    VMOVE(node->bounds_min, min);
    VMOVE(node->bounds_max, max);
    node->have_bounds = 1;
}


void
bv_node_bounds_clear(struct bv_node *node)
{
    if (!node)
	return;
    node->have_bounds = 0;
    VSETALL(node->bounds_min, 0.0);
    VSETALL(node->bounds_max, 0.0);
}


int
bv_node_bounds_get(const struct bv_node *node, point_t *out_min, point_t *out_max)
{
    if (!node || !out_min || !out_max)
	return 0;
    if (!node->have_bounds)
	return 0;
    VMOVE(*out_min, node->bounds_min);
    VMOVE(*out_max, node->bounds_max);
    return 1;
}


/* --- LoD level accessors --- */

void
bv_node_lod_level_set(struct bv_node *node, int level)
{
    if (!node)
	return;
    node->lod_level = level;
}


int
bv_node_lod_level_get(const struct bv_node *node)
{
    if (!node)
	return 0;
    return node->lod_level;
}


/* --- Vlist accessors (Phase 2) --- */

void
bv_node_vlist_set(struct bv_node *node, struct bu_list *vlist)
{
    if (!node)
	return;
    node->geometry = vlist;
}


struct bu_list *
bv_node_vlist_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return (struct bu_list *)node->geometry;
}


int
bv_node_vlist_bounds(const struct bv_node *node, point_t *out_min, point_t *out_max)
{
    struct bu_list *vl;

    if (!node || !out_min || !out_max)
	return 0;

    vl = (struct bu_list *)node->geometry;
    if (!vl || BU_LIST_IS_EMPTY(vl))
	return 0;

    VSETALL(*out_min,  INFINITY);
    VSETALL(*out_max, -INFINITY);

    /* bv_vlist_bbox returns non-zero on error (unsupported command), 0 on success */
    (void)bv_vlist_bbox(vl, out_min, out_max, NULL, NULL);

    /* Verify that at least one point was found (infinity unchanged means no points) */
    if ((*out_min)[X] > (*out_max)[X])
	return 0;

    return 1;
}


/* --- Display-list state accessors (Phase 2) --- */

void
bv_node_dlist_set(struct bv_node *node, unsigned int dlist)
{
    if (!node)
	return;
    node->dlist = dlist;
}


unsigned int
bv_node_dlist_get(const struct bv_node *node)
{
    if (!node)
	return 0;
    return node->dlist;
}


void
bv_node_dlist_stale_set(struct bv_node *node, int stale)
{
    if (!node)
	return;
    node->dlist_stale = stale;
}


int
bv_node_dlist_stale_get(const struct bv_node *node)
{
    if (!node)
	return 0;
    return node->dlist_stale;
}


/* --- Update callback accessors (Phase 2) --- */

void
bv_node_update_cb_set(struct bv_node *node, bv_node_update_cb cb, void *data)
{
    if (!node)
	return;
    node->update_cb      = cb;
    node->update_cb_data = data;
}


bv_node_update_cb
bv_node_update_cb_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return node->update_cb;
}


void *
bv_node_update_cb_data_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return node->update_cb_data;
}


/* --- Raw draw data accessor (Phase 4) --- */

void
bv_node_draw_data_set(struct bv_node *node, void *draw_data)
{
    if (!node)
	return;
    node->draw_data = draw_data;
}


void *
bv_node_draw_data_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return node->draw_data;
}


enum bv_node_type
bv_node_type_get(const struct bv_node *node)
{
    if (!node)
	return BV_NODE_OTHER;
    return node->type;
}


const char *
bv_node_name_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return bu_vls_cstr(&node->name);
}


void
bv_node_name_set(struct bv_node *node, const char *name)
{
    if (!node)
	return;
    if (name)
	bu_vls_sprintf(&node->name, "%s", name);
    else
	bu_vls_trunc(&node->name, 0);
}


void
bv_node_user_data_set(struct bv_node *node, void *user_data)
{
    if (!node)
	return;
    node->user_data = user_data;
}


void *
bv_node_user_data_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return node->user_data;
}


struct bv_node *
bv_node_parent_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    return node->parent;
}


/* Recompute world_transform by walking up the parent chain */
static void
_bv_node_update_world_transform(struct bv_node *node)
{
    if (!node)
	return;

    if (!node->parent) {
	MAT_COPY(node->world_transform, node->transform);
    } else {
	bn_mat_mul(node->world_transform,
		   node->parent->world_transform,
		   node->transform);
    }

    /* Propagate to children */
    for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	struct bv_node *child = (struct bv_node *)BU_PTBL_GET(&node->children, i);
	_bv_node_update_world_transform(child);
    }
}


const mat_t *
bv_node_world_transform_get(const struct bv_node *node)
{
    if (!node)
	return NULL;
    /*
     * The world transform is a conceptually mutable cache derived from the node
     * hierarchy.  Since C has no 'mutable' keyword, we cast away const here.
     * The logical const contract is preserved: external callers observe no
     * visible state change through this pointer.
     */
    _bv_node_update_world_transform((struct bv_node *)node);
    return (const mat_t *)&node->world_transform;
}


/* ---- bv_node traversal ---- */

void
bv_node_traverse(const struct bv_node *node, bv_scene_traverse_cb cb, void *user_data)
{
    if (!node || !cb)
	return;

    cb((struct bv_node *)node, user_data);

    for (size_t i = 0; i < BU_PTBL_LEN(&node->children); i++) {
	struct bv_node *child = (struct bv_node *)BU_PTBL_GET(&node->children, i);
	bv_node_traverse(child, cb, user_data);
    }
}


/* ---- bv_scene API ---- */

struct bv_scene *
bv_scene_create(void)
{
    struct bv_scene *scene;
    BU_GET(scene, struct bv_scene);

    /* Create the root separator node (analogous to constructing an SoSeparator
     * as the scene root in a Coin3D scene graph) */
    scene->root = bv_node_create("root", BV_NODE_SEPARATOR);
    bu_ptbl_init(&scene->nodes, 8, "bv_scene nodes");
    bu_ptbl_init(&scene->views, 4, "bv_scene views");
    scene->default_camera = NULL;

    return scene;
}


void
bv_scene_destroy(struct bv_scene *scene)
{
    if (!scene)
	return;

    if (scene->root)
	bv_node_destroy(scene->root);

    bu_ptbl_free(&scene->nodes);
    /* Do not destroy views — they are not owned by the scene */
    bu_ptbl_free(&scene->views);
    BU_PUT(scene, struct bv_scene);
}


struct bv_node *
bv_scene_root(const struct bv_scene *scene)
{
    if (!scene)
	return NULL;
    return scene->root;
}


void
bv_scene_add_node(struct bv_scene *scene, struct bv_node *node)
{
    if (!scene || !node)
	return;
    /* Add to flat lookup table */
    bu_ptbl_ins_unique(&scene->nodes, (long *)node);
    /* Attach to root separator if the node has no parent yet */
    if (!node->parent)
	bv_node_add_child(scene->root, node);
}


size_t
bv_scene_add_nodes(struct bv_scene *scene, const struct bu_ptbl *nodes)
{
    if (!scene || !nodes)
	return 0;
    size_t added = 0;
    for (size_t i = 0; i < BU_PTBL_LEN(nodes); i++) {
	struct bv_node *n = (struct bv_node *)BU_PTBL_GET(nodes, i);
	if (!n) continue;
	size_t before = BU_PTBL_LEN(&scene->nodes);
	bv_scene_add_node(scene, n);
	if (BU_PTBL_LEN(&scene->nodes) > before)
	    added++;
    }
    return added;
}


void
bv_scene_remove_node(struct bv_scene *scene, struct bv_node *node)
{
    if (!scene || !node)
	return;
    bu_ptbl_rm(&scene->nodes, (long *)node);
    if (node->parent)
	bv_node_remove_child(node->parent, node);
}


const struct bu_ptbl *
bv_scene_nodes(const struct bv_scene *scene)
{
    if (!scene)
	return NULL;
    return &scene->nodes;
}


size_t
bv_scene_node_count(const struct bv_scene *scene)
{
    if (!scene)
	return 0;
    return BU_PTBL_LEN(&scene->nodes);
}


int
bv_scene_has_node(const struct bv_scene *scene, const struct bv_node *node)
{
    if (!scene || !node)
	return 0;
    /* bu_ptbl_locate returns -1 if not found, >= 0 if found */
    return (bu_ptbl_locate(&scene->nodes, (long *)node) >= 0) ? 1 : 0;
}


size_t
bv_scene_total_node_count(const struct bv_scene *scene)
{
    if (!scene)
	return 0;
    /* scene->nodes is the flat lookup table that contains ALL registered nodes
     * (top-level AND nested children added via bv_scene_add_child). */
    return BU_PTBL_LEN(&scene->nodes);
}


void
bv_scene_clear(struct bv_scene *scene)
{
    if (!scene)
	return;

    /* Collect the top-level nodes (direct children of root) into a temporary
     * array first, since bv_node_destroy() will mutate scene->nodes. */
    struct bu_ptbl tops;
    BU_PTBL_INIT(&tops);

    if (scene->root) {
	const struct bu_ptbl *ch = bv_node_children(scene->root);
	if (ch) {
	    for (size_t i = 0; i < BU_PTBL_LEN(ch); i++)
		bu_ptbl_ins(&tops, BU_PTBL_GET(ch, i));
	}
    } else {
	/* Flat list mode (no explicit root): treat all nodes as top-level */
	for (size_t i = 0; i < BU_PTBL_LEN(&scene->nodes); i++)
	    bu_ptbl_ins(&tops, BU_PTBL_GET(&scene->nodes, i));
    }

    for (size_t i = 0; i < BU_PTBL_LEN(&tops); i++) {
	struct bv_node *n = (struct bv_node *)BU_PTBL_GET(&tops, i);
	bv_scene_remove_node(scene, n);
	bv_node_destroy(n);
    }
    bu_ptbl_free(&tops);
}


/* Callback state for bv_scene_set_all_visible */
struct _set_visible_state {
    int    visible;
    size_t changed;
};

static void
_set_visible_cb(struct bv_node *node, void *user_data)
{
    struct _set_visible_state *st = (struct _set_visible_state *)user_data;
    if (!node || !st)
	return;
    if (node->type == BV_NODE_SEPARATOR)
	return;
    if (node->visible != st->visible) {
	node->visible = st->visible;
	st->changed++;
    }
}

size_t
bv_scene_set_all_visible(struct bv_scene *scene, int visible)
{
    if (!scene)
	return 0;

    struct _set_visible_state st;
    st.visible = visible ? 1 : 0;
    st.changed = 0;

    bv_scene_traverse(scene, _set_visible_cb, &st);
    return st.changed;
}


void
bv_scene_add_child(struct bv_scene *scene, struct bv_node *parent, struct bv_node *child)
{
    if (!scene || !parent || !child)
	return;
    bv_node_add_child(parent, child);
    bu_ptbl_ins_unique(&scene->nodes, (long *)child);
}


void
bv_scene_remove_child(struct bv_scene *scene, struct bv_node *parent, struct bv_node *child)
{
    if (!scene || !parent || !child)
	return;
    bv_node_remove_child(parent, child);
    bu_ptbl_rm(&scene->nodes, (long *)child);
}


/* Callback state for name search */
struct _bv_scene_find_state {
    const char     *name;
    struct bv_node *result;
};

static void
_bv_scene_find_cb(struct bv_node *node, void *user_data)
{
    struct _bv_scene_find_state *s = (struct _bv_scene_find_state *)user_data;
    if (s->result)
	return; /* already found */
    if (BU_STR_EQUAL(bu_vls_cstr(&node->name), s->name))
	s->result = node;
}


struct bv_node *
bv_scene_find_node(const struct bv_scene *scene, const char *name)
{
    if (!scene || !name)
	return NULL;

    struct _bv_scene_find_state state;
    state.name   = name;
    state.result = NULL;

    bv_scene_traverse(scene, _bv_scene_find_cb, &state);
    return state.result;
}


/* Callback state for find_all_nodes */
struct _bv_scene_find_all_state {
    const char     *name;
    struct bu_ptbl *out;
};

static void
_bv_scene_find_all_cb(struct bv_node *node, void *user_data)
{
    struct _bv_scene_find_all_state *s = (struct _bv_scene_find_all_state *)user_data;
    if (BU_STR_EQUAL(bu_vls_cstr(&node->name), s->name))
	bu_ptbl_ins_unique(s->out, (long *)node);
}

size_t
bv_scene_find_all_nodes(const struct bv_scene *scene,
                         const char *name,
                         struct bu_ptbl *out)
{
    if (!scene || !name || !out)
	return 0;

    size_t before = BU_PTBL_LEN(out);
    struct _bv_scene_find_all_state st;
    st.name = name;
    st.out  = out;
    bv_scene_traverse(scene, _bv_scene_find_all_cb, &st);
    return BU_PTBL_LEN(out) - before;
}


/* Callback state for bv_scene_nodes_of_type */
struct _type_collect_state {
    enum bv_node_type  type;
    struct bu_ptbl    *out;
    size_t             count;
};

static void
_type_collect_cb(struct bv_node *node, void *user_data)
{
    struct _type_collect_state *st = (struct _type_collect_state *)user_data;
    if (!node || !st)
	return;
    if (node->type == st->type) {
	bu_ptbl_ins_unique(st->out, (long *)node);
	st->count++;
    }
}

size_t
bv_scene_nodes_of_type(const struct bv_scene *scene,
                        enum bv_node_type type,
                        struct bu_ptbl *out)
{
    if (!scene || !out)
	return 0;

    struct _type_collect_state st;
    st.type  = type;
    st.out   = out;
    st.count = 0;

    bv_scene_traverse(scene, _type_collect_cb, &st);
    return st.count;
}


int
bv_node_is_descendant(const struct bv_node *candidate,
                       const struct bv_node *ancestor)
{
    if (!candidate || !ancestor)
	return 0;
    const struct bv_node *cur = candidate;
    while (cur) {
	if (cur == ancestor)
	    return 1;
	cur = cur->parent;
    }
    return 0;
}


struct bv_node *
bv_scene_default_camera(const struct bv_scene *scene)
{
    if (!scene)
	return NULL;
    return scene->default_camera;
}


void
bv_scene_default_camera_set(struct bv_scene *scene, struct bv_node *camera)
{
    if (!scene)
	return;
    scene->default_camera = camera;
}


void
bv_scene_traverse(const struct bv_scene *scene, bv_scene_traverse_cb cb, void *user_data)
{
    if (!scene || !cb)
	return;
    bv_node_traverse(scene->root, cb, user_data);
}


/* ---- bview_new API ---- */

struct bview_new *
bview_create(const char *name)
{
    struct bview_new *view;
    BU_GET(view, struct bview_new);

    bu_vls_init(&view->name);
    if (name)
	bu_vls_strcpy(&view->name, name);

    view->scene          = NULL;
    view->camera_node    = NULL;

    memset(&view->camera,     0, sizeof(struct bview_camera));
    memset(&view->viewport,   0, sizeof(struct bview_viewport));
    memset(&view->material,   0, sizeof(struct bview_material));
    memset(&view->appearance, 0, sizeof(struct bview_appearance));
    memset(&view->overlay,    0, sizeof(struct bview_overlay));

    /* Initialize pick_set ptbl */
    bu_ptbl_init(&view->pick_set.selected_objs, 8, "bview_new pick_set");

    view->redraw_cb      = NULL;
    view->redraw_cb_data = NULL;
    view->old_bview      = NULL;

    return view;
}


void
bview_destroy(struct bview_new *view)
{
    if (!view)
	return;

    /* Remove this view from the scene's tracking list before freeing */
    if (view->scene)
	bu_ptbl_rm(&view->scene->views, (long *)view);

    bu_vls_free(&view->name);
    bu_ptbl_free(&view->pick_set.selected_objs);
    BU_PUT(view, struct bview_new);
}


const char *
bview_name_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return bu_vls_cstr(&view->name);
}

void
bview_name_set(struct bview_new *view, const char *name)
{
    if (!view)
	return;
    if (name)
	bu_vls_sprintf(&view->name, "%s", name);
    else
	bu_vls_trunc(&view->name, 0);
}


void
bview_scene_set(struct bview_new *view, struct bv_scene *scene)
{
    if (!view)
	return;

    /* Unregister from the previous scene */
    if (view->scene)
	bu_ptbl_rm(&view->scene->views, (long *)view);

    view->scene = scene;

    /* Register with the new scene (NULL is a valid "no scene" state) */
    if (scene)
	bu_ptbl_ins_unique(&scene->views, (long *)view);
}


struct bv_scene *
bview_scene_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return view->scene;
}


size_t
bv_scene_view_count(const struct bv_scene *scene)
{
    if (!scene)
	return 0;
    return BU_PTBL_LEN(&scene->views);
}


const struct bu_ptbl *
bv_scene_views(const struct bv_scene *scene)
{
    if (!scene)
	return NULL;
    return &scene->views;
}


void
bview_camera_set(struct bview_new *view, const struct bview_camera *camera)
{
    if (!view || !camera)
	return;
    view->camera = *camera;
}


const struct bview_camera *
bview_camera_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return &view->camera;
}


void
bview_camera_scale_set(struct bview_new *view, double scale)
{
    if (!view)
	return;
    view->camera.scale = scale;
}


double
bview_camera_scale_get(const struct bview_new *view)
{
    if (!view)
	return 0.0;
    return view->camera.scale;
}


void
bview_camera_node_set(struct bview_new *view, struct bv_node *camera_node)
{
    if (!view)
	return;
    view->camera_node = camera_node;
}


struct bv_node *
bview_camera_node_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return view->camera_node;
}


void
bview_viewport_set(struct bview_new *view, const struct bview_viewport *viewport)
{
    if (!view || !viewport)
	return;
    view->viewport = *viewport;
}


const struct bview_viewport *
bview_viewport_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return &view->viewport;
}


void
bview_material_set(struct bview_new *view, const struct bview_material *material)
{
    if (!view || !material)
	return;
    view->material = *material;
}


const struct bview_material *
bview_material_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return &view->material;
}


void
bview_appearance_set(struct bview_new *view, const struct bview_appearance *appearance)
{
    if (!view || !appearance)
	return;
    view->appearance = *appearance;
}


const struct bview_appearance *
bview_appearance_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return &view->appearance;
}


void
bview_overlay_set(struct bview_new *view, const struct bview_overlay *overlay)
{
    if (!view || !overlay)
	return;
    view->overlay = *overlay;
}


const struct bview_overlay *
bview_overlay_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return &view->overlay;
}


void
bview_pick_set_set(struct bview_new *view, const struct bview_pick_set *pick_set)
{
    if (!view || !pick_set)
	return;
    /* Replace the selection set: clear then copy pointers */
    bu_ptbl_reset(&view->pick_set.selected_objs);
    for (size_t i = 0; i < BU_PTBL_LEN(&pick_set->selected_objs); i++) {
	bu_ptbl_ins(&view->pick_set.selected_objs,
		    BU_PTBL_GET(&pick_set->selected_objs, i));
    }
}


const struct bview_pick_set *
bview_pick_set_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return &view->pick_set;
}


void
bview_redraw_callback_set(struct bview_new *view, bview_redraw_cb cb, void *data)
{
    if (!view)
	return;
    view->redraw_cb      = cb;
    view->redraw_cb_data = data;
}

bview_redraw_cb
bview_redraw_callback_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return view->redraw_cb;
}

void *
bview_redraw_callback_data_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return view->redraw_cb_data;
}


void
bview_lod_update(struct bview_new *view)
{
    if (!view)
	return;
    /* Update LoD for all geometry nodes in the associated scene */
    if (view->scene)
	bv_scene_lod_update(view->scene, view);
}


void
bview_redraw(struct bview_new *view)
{
    if (!view)
	return;
    if (view->redraw_cb)
	view->redraw_cb(view, view->redraw_cb_data);
}


/* ---- Migration helpers ---- */

void
bview_from_old(struct bview_new *view, const struct bview *old)
{
    if (!view || !old)
	return;

    /* Copy camera parameters from old bview */
    VMOVE(view->camera.position, old->gv_eye_pos);
    /* target = eye + lookat (gv_lookat is a direction vector pointing from eye toward scene) */
    VADD2(view->camera.target, old->gv_eye_pos, old->gv_lookat);
    /* up vector is not directly stored; use the Y column of gv_rotation */
    view->camera.up[0] = old->gv_rotation[1];
    view->camera.up[1] = old->gv_rotation[5];
    view->camera.up[2] = old->gv_rotation[9];
    view->camera.fov         = old->gv_perspective;
    view->camera.perspective = (old->gv_perspective > 0.0) ? 1 : 0;
    /* gv_scale is the half-size of the view; gv_size = 2*gv_scale */
    view->camera.scale       = old->gv_scale;

    /* Copy viewport */
    view->viewport.width  = old->gv_width;
    view->viewport.height = old->gv_height;
    view->viewport.dpi    = BV_DEFAULT_DPI;

    /* Copy display settings from bview_settings (use shared if set, else local) */
    {
	const struct bview_settings *s = old->gv_s ? old->gv_s : &old->gv_ls;

	view->appearance.show_grid   = s->gv_grid.draw;
	view->appearance.show_axes   = s->gv_view_axes.draw;
	view->appearance.show_origin = s->gv_model_axes.draw;

	/* Grid color: bv_axes stores int[3] in 0-255 range */
	{
	    unsigned char gc[3];
	    gc[0] = (unsigned char)s->gv_grid.color[0];
	    gc[1] = (unsigned char)s->gv_grid.color[1];
	    gc[2] = (unsigned char)s->gv_grid.color[2];
	    bu_color_from_rgb_chars(&view->appearance.grid_color, gc);
	}

	/* Axes color */
	{
	    unsigned char ac[3];
	    ac[0] = (unsigned char)s->gv_view_axes.axes_color[0];
	    ac[1] = (unsigned char)s->gv_view_axes.axes_color[1];
	    ac[2] = (unsigned char)s->gv_view_axes.axes_color[2];
	    bu_color_from_rgb_chars(&view->appearance.axes_color, ac);
	}

	view->appearance.line_width = (float)s->gv_view_axes.line_width;
    }

    /* Overlay: show_fps mirrors frametime display in bview_settings */
    {
	const struct bview_settings *s = old->gv_s ? old->gv_s : &old->gv_ls;
	view->overlay.show_fps = s->gv_view_params.draw_fps;
    }

    /* Store reference to legacy view */
    view->old_bview = (struct bview *)old;
}


void
bview_to_old(const struct bview_new *view, struct bview *old)
{
    struct bview_settings *s;

    if (!view || !old)
	return;

    /* Push camera parameters back to old bview */
    VMOVE(old->gv_eye_pos, view->camera.position);
    old->gv_perspective = view->camera.perspective ? view->camera.fov : 0.0;
    /* Push scale: gv_size = 2*gv_scale, gv_isize = 1/gv_size */
    if (view->camera.scale > 0.0) {
	old->gv_scale  = (fastf_t)view->camera.scale;
	old->gv_size   = 2.0 * old->gv_scale;
	old->gv_isize  = 1.0 / old->gv_size;
    }

    /* Viewport */
    old->gv_width  = view->viewport.width;
    old->gv_height = view->viewport.height;

    /* Push appearance settings back into whichever bview_settings is active */
    s = old->gv_s ? old->gv_s : &old->gv_ls;

    /* Grid: draw flag and color */
    s->gv_grid.draw = view->appearance.show_grid;
    {
	unsigned char gc[3];
	bu_color_to_rgb_chars(&view->appearance.grid_color, gc);
	s->gv_grid.color[0] = (int)gc[0];
	s->gv_grid.color[1] = (int)gc[1];
	s->gv_grid.color[2] = (int)gc[2];
    }

    /* View axes: draw flag and color */
    s->gv_view_axes.draw = view->appearance.show_axes;
    {
	unsigned char ac[3];
	bu_color_to_rgb_chars(&view->appearance.axes_color, ac);
	s->gv_view_axes.axes_color[0] = (int)ac[0];
	s->gv_view_axes.axes_color[1] = (int)ac[1];
	s->gv_view_axes.axes_color[2] = (int)ac[2];
    }
    s->gv_view_axes.line_width = (int)view->appearance.line_width;

    /* Model axes: draw flag */
    s->gv_model_axes.draw = view->appearance.show_origin;

    /* Overlay: fps display */
    s->gv_view_params.draw_fps = view->overlay.show_fps;
}


struct bview *
bview_old_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return view->old_bview;
}


void
bview_old_set(struct bview_new *view, struct bview *old)
{
    if (!view)
	return;
    view->old_bview = old;
}


/* ================================================================
 * bview_companion_create
 *
 * Convenience wrapper: create(name) + from_old + old_set.
 * The standard first step for migrating bv_init() callers.
 * ================================================================ */

struct bview_new *
bview_companion_create(const char *name, struct bview *old)
{
    if (!old)
	return NULL;

    struct bview_new *nv = bview_create(name);
    if (!nv)
	return NULL;

    bview_from_old(nv, old);
    bview_old_set(nv, old);

    return nv;
}


void
bview_sync_from_old(struct bview_new *view)
{
    if (!view)
	return;
    struct bview *old = bview_old_get(view);
    if (!old)
	return;
    bview_from_old(view, old);
}


void
bview_sync_to_old(struct bview_new *view)
{
    if (!view)
	return;
    struct bview *old = bview_old_get(view);
    if (!old)
	return;
    bview_to_old(view, old);
}


/* ================================================================
 * bview_settings_apply
 *
 * Mirrors the initial state that bv_init() + bv_settings_init() set on a
 * legacy struct bview, but expressed through the new accessor API.
 * ================================================================ */

void
bview_settings_apply(struct bview_new *view)
{
    const unsigned char white[3] = {255, 255, 255};
    const unsigned char black[3] = {0,   0,   0  };

    if (!view)
	return;

    /* Camera defaults: orthographic, eye at (0,0,1), looking at origin */
    VSET(view->camera.position, 0.0, 0.0, 1.0);
    VSET(view->camera.target,   0.0, 0.0, 0.0);
    VSET(view->camera.up,       0.0, 1.0, 0.0);
    view->camera.fov         = 0.0;
    view->camera.perspective = 0;
    /* Scale default: matches gv_scale initial value in bv_init() */
    view->camera.scale       = 500.0;

    /* Viewport defaults */
    view->viewport.width  = 0;
    view->viewport.height = 0;
    view->viewport.dpi    = BV_DEFAULT_DPI;

    /* Appearance defaults (mirrors bv_settings_init) */
    bu_color_from_rgb_chars(&view->appearance.bg_color,   black);
    bu_color_from_rgb_chars(&view->appearance.grid_color, white);
    bu_color_from_rgb_chars(&view->appearance.axes_color, white);
    view->appearance.line_width  = 0.0f;
    view->appearance.show_axes   = 0;
    view->appearance.show_grid   = 0;
    view->appearance.show_origin = 0;

    /* Overlay defaults */
    view->overlay.show_fps        = 0;
    view->overlay.show_gizmos     = 0;
    view->overlay.show_annotation = 0;
    view->overlay.annotation_text[0] = '\0';
}


/* ================================================================
 * bv_scene_obj_to_node
 *
 * Wraps a legacy bv_scene_obj (and its children) in bv_node instances.
 * ================================================================ */

struct bv_node *
bv_scene_obj_to_node(struct bv_scene_obj *s)
{
    struct bv_node *n;
    enum bv_node_type ntype;
    const char *nm;
    size_t i;
    size_t nchildren;

    if (!s)
	return NULL;

    /* Nodes with children become groups; leaf nodes are geometry */
    nchildren = (size_t)BU_PTBL_LEN(&s->children);
    ntype = (nchildren > 0) ? BV_NODE_GROUP : BV_NODE_GEOMETRY;

    nm = bu_vls_cstr(&s->s_name);
    n  = bv_node_create((nm && nm[0] != '\0') ? nm : "(unnamed)", ntype);
    if (!n)
	return NULL;

    /* Visibility: UP (0) means visible in legacy bview, DOWN (1) means hidden */
    bv_node_visible_set(n, (s->s_flag == UP) ? 1 : 0);

    /* Material: convert s_color to bview_material */
    {
	struct bview_material mat;
	unsigned char c[3];
	memset(&mat, 0, sizeof(mat));
	c[0] = s->s_color[0];
	c[1] = s->s_color[1];
	c[2] = s->s_color[2];
	bu_color_from_rgb_chars(&mat.diffuse_color, c);
	bv_node_material_set(n, &mat);
    }

    /* Transform: use the scene object's s_mat */
    bv_node_transform_set(n, s->s_mat);

    /* Store original object as geometry pointer and user_data */
    bv_node_geometry_set(n, s);
    bv_node_user_data_set(n, s);

    /* Recursively wrap children */
    for (i = 0; i < nchildren; i++) {
	struct bv_scene_obj *child =
	    (struct bv_scene_obj *)BU_PTBL_GET(&s->children, i);
	struct bv_node *child_node = bv_scene_obj_to_node(child);
	if (child_node)
	    bv_node_add_child(n, child_node);
    }

    return n;
}


/* ================================================================
 * bv_scene_from_view
 *
 * Create a new bv_scene containing bv_node wrappers for every top-level
 * scene object (db_objs + view_objs) visible in a legacy bview.
 * ================================================================ */

struct bv_scene *
bv_scene_from_view(const struct bview *v)
{
    struct bv_scene *scene;
    struct bu_ptbl  *tbl_db;
    struct bu_ptbl  *tbl_view;
    size_t i;

    if (!v)
	return NULL;

    scene = bv_scene_create();
    if (!scene)
	return NULL;

    /*
     * Use bv_view_objs() to correctly handle both independent views
     * (objects in v->gv_objs.*) and shared views (objects in vset->i->shared_*).
     *
     * For a non-independent view with a vset:
     *   bv_view_objs(BV_DB_OBJS)  → shared_db_objs   (contains objects)
     *   bv_view_objs(BV_VIEW_OBJS) → shared_view_objs (contains objects)
     *   local tables are empty by design.
     *
     * For an independent view with no vset:
     *   bv_view_objs(BV_DB_OBJS)   → NULL  (no vset)
     *   bv_view_objs(BV_VIEW_OBJS) → NULL  (no vset)
     *   Use BV_LOCAL_OBJS flag to retrieve the per-view tables.
     *
     * This structure guarantees each bv_scene_obj is visited exactly once.
     */
    if (v->vset) {
	/* Shared view: objects are in the vset's shared tables */
	tbl_db   = bv_view_objs((struct bview *)v, BV_DB_OBJS);
	tbl_view = bv_view_objs((struct bview *)v, BV_VIEW_OBJS);
    } else {
	/* Independent view: objects are in the per-view local tables */
	tbl_db   = bv_view_objs((struct bview *)v, BV_DB_OBJS   | BV_LOCAL_OBJS);
	tbl_view = bv_view_objs((struct bview *)v, BV_VIEW_OBJS | BV_LOCAL_OBJS);
    }

    if (tbl_db) {
	for (i = 0; i < (size_t)BU_PTBL_LEN(tbl_db); i++) {
	    struct bv_scene_obj *s =
		(struct bv_scene_obj *)BU_PTBL_GET(tbl_db, i);
	    struct bv_node *n = bv_scene_obj_to_node(s);
	    if (n)
		bv_scene_add_node(scene, n);
	}
    }

    if (tbl_view) {
	for (i = 0; i < (size_t)BU_PTBL_LEN(tbl_view); i++) {
	    struct bv_scene_obj *s =
		(struct bv_scene_obj *)BU_PTBL_GET(tbl_view, i);
	    struct bv_node *n = bv_scene_obj_to_node(s);
	    if (n)
		bv_scene_add_node(scene, n);
	}
    }

    return scene;
}


/* ================================================================
 * bv_scene_from_view_set
 *
 * Create a new bv_scene containing bv_node wrappers for all shared scene
 * objects stored in a bview_set.
 * ================================================================ */

struct bv_scene *
bv_scene_from_view_set(const struct bview_set *s)
{
    struct bv_scene *scene;
    struct bu_ptbl  *tbl;
    size_t i;

    if (!s || !s->i)
	return NULL;

    scene = bv_scene_create();
    if (!scene)
	return NULL;

    /* Shared db objects */
    tbl = &s->i->shared_db_objs;
    for (i = 0; i < (size_t)BU_PTBL_LEN(tbl); i++) {
	struct bv_scene_obj *obj =
	    (struct bv_scene_obj *)BU_PTBL_GET(tbl, i);
	struct bv_node *n = bv_scene_obj_to_node(obj);
	if (n)
	    bv_scene_add_node(scene, n);
    }

    /* Shared view objects */
    tbl = &s->i->shared_view_objs;
    for (i = 0; i < (size_t)BU_PTBL_LEN(tbl); i++) {
	struct bv_scene_obj *obj =
	    (struct bv_scene_obj *)BU_PTBL_GET(tbl, i);
	struct bv_node *n = bv_scene_obj_to_node(obj);
	if (n)
	    bv_scene_add_node(scene, n);
    }

    return scene;
}


/* ================================================================
 * bview_lod_node_update  (Phase 4 — wired to bv_mesh_lod_view)
 * ================================================================ */

int
bview_lod_node_update(struct bv_node *node, const struct bview_new *view)
{
    struct bv_scene_obj *s;

    if (!node)
	return 0;

    if (bv_node_type_get(node) != BV_NODE_GEOMETRY)
	return 0;

    /* Retrieve the wrapped legacy scene object if present */
    s = (struct bv_scene_obj *)bv_node_user_data_get(node);
    if (!s) {
	/*
	 * Native node (no wrapped bv_scene_obj): use native LoD path if
	 * the node carries LoD mesh data in draw_data, otherwise just mark
	 * the display-list stale.
	 */
	if (node->draw_data && view) {
	    /* bv_mesh_lod_view_new() is implemented in lod.cpp and handles
	     * the draw_data cast and level selection internally. */
	    bv_mesh_lod_view_new(node, view, 0);
	} else {
	    node->dlist_stale = 1;
	}
	return 1;
    }

    /*
     * If the scene object carries LoD mesh data (BV_MESH_LOD flag) and we
     * have a view to compute scale from, call bv_mesh_lod_view() to select
     * the appropriate detail level.  This wires the existing lod.cpp pipeline
     * into new-API scene graph traversals.
     *
     * If view is NULL or the legacy bview pointer is not available, fall back
     * to marking the display list stale so the legacy rendering path will
     * regenerate it on the next draw cycle.
     */
    if ((s->s_type_flags & BV_MESH_LOD) && view) {
	struct bview *old_v = bview_old_get(view);
	if (old_v) {
	    bv_mesh_lod_view(s, old_v, 0);
	    return 1;
	}
    }

    /* Fallback: mark display list stale */
    s->s_dlist_stale = 1;

    return 1;
}


/* ================================================================
 * bv_scene_lod_update
 *
 * Update LoD for all BV_NODE_GEOMETRY nodes in a scene.
 * ================================================================ */

struct _lod_update_state {
    const struct bview_new *view;
    int count;
};

static void
_lod_update_cb(struct bv_node *node, void *user_data)
{
    struct _lod_update_state *st = (struct _lod_update_state *)user_data;
    if (!node || !st)
	return;
    if (bv_node_type_get(node) != BV_NODE_GEOMETRY)
	return;
    if (bview_lod_node_update(node, st->view))
	st->count++;
}

int
bv_scene_lod_update(struct bv_scene *scene, const struct bview_new *view)
{
    struct _lod_update_state st;

    if (!scene)
	return 0;

    st.view  = view;
    st.count = 0;

    bv_scene_traverse(scene, _lod_update_cb, &st);

    return st.count;
}


/* ================================================================
 * bv_scene_selected_nodes / bv_scene_select_node / bv_scene_deselect_all
 * ================================================================ */

struct _select_collect_state {
    struct bu_ptbl *out;
    int count;
};

static void
_select_collect_cb(struct bv_node *node, void *user_data)
{
    struct _select_collect_state *st = (struct _select_collect_state *)user_data;
    if (!node || !st)
	return;
    if (node->selected) {
	bu_ptbl_ins(st->out, (long *)node);
	st->count++;
    }
}

int
bv_scene_selected_nodes(const struct bv_scene *scene, struct bu_ptbl *out)
{
    struct _select_collect_state st;

    if (!scene || !out)
	return 0;

    st.out   = out;
    st.count = 0;

    bv_scene_traverse(scene, _select_collect_cb, &st);

    return st.count;
}


void
bv_scene_select_node(struct bv_node *node, int selected, struct bview_new *view)
{
    (void)view; /* reserved for future pick_set integration */
    bv_node_selected_set(node, selected);
}


struct _deselect_state {
    struct bview_new *view;
    int count;
};

static void
_deselect_cb(struct bv_node *node, void *user_data)
{
    struct _deselect_state *st = (struct _deselect_state *)user_data;
    if (!node || !st)
	return;
    if (node->selected) {
	bv_scene_select_node(node, 0, st->view);
	st->count++;
    }
}

int
bv_scene_deselect_all(struct bv_scene *scene, struct bview_new *view)
{
    struct _deselect_state st;

    if (!scene)
	return 0;

    st.view  = view;
    st.count = 0;

    bv_scene_traverse(scene, _deselect_cb, &st);

    return st.count;
}


/* Callback state for bv_scene_visible_nodes */
struct _visible_collect_state {
    struct bu_ptbl *out;
    size_t          count;
};

static void
_visible_collect_cb(struct bv_node *node, void *user_data)
{
    struct _visible_collect_state *st = (struct _visible_collect_state *)user_data;
    if (!node || !st)
	return;
    /* Skip the synthetic root separator — it is not a user-visible node */
    if (node->type == BV_NODE_SEPARATOR)
	return;
    if (node->visible) {
	bu_ptbl_ins_unique(st->out, (long *)node);
	st->count++;
    }
}

size_t
bv_scene_visible_nodes(const struct bv_scene *scene, struct bu_ptbl *out)
{
    if (!scene || !out)
	return 0;

    struct _visible_collect_state st;
    st.out   = out;
    st.count = 0;

    bv_scene_traverse(scene, _visible_collect_cb, &st);
    return st.count;
}


/* ================================================================
 * bv_node_bbox / bv_scene_bbox
 *
 * Compute axis-aligned bounding boxes for node subtrees and full scenes.
 * ================================================================ */

/*
 * Internal traverse callback state for bounding box accumulation.
 */
struct _bbox_state {
    int   have_bound; /* 1 once at least one node contributed */
    point_t min;
    point_t max;
};

static void
_bbox_cb(struct bv_node *node, void *user_data)
{
    struct _bbox_state *st = (struct _bbox_state *)user_data;
    const struct bv_scene_obj *s;
    vect_t obj_min, obj_max;

    if (!node || !st)
	return;

    /* Only geometry nodes contribute bounds */
    if (bv_node_type_get(node) != BV_NODE_GEOMETRY)
	return;

    /* Only visible nodes */
    if (!bv_node_visible_get(node))
	return;

    /* Priority 1: native AABB set via bv_node_bounds_set() */
    if (node->have_bounds) {
	if (!st->have_bound) {
	    VMOVE(st->min, node->bounds_min);
	    VMOVE(st->max, node->bounds_max);
	    st->have_bound = 1;
	} else {
	    VMIN(st->min, node->bounds_min);
	    VMAX(st->max, node->bounds_max);
	}
	return;
    }

    /* Priority 2: bounding sphere from a wrapped legacy bv_scene_obj */
    s = (const struct bv_scene_obj *)bv_node_user_data_get(node);
    if (s && s->s_size > 0.0) {
	obj_min[X] = s->s_center[X] - s->s_size;
	obj_min[Y] = s->s_center[Y] - s->s_size;
	obj_min[Z] = s->s_center[Z] - s->s_size;
	obj_max[X] = s->s_center[X] + s->s_size;
	obj_max[Y] = s->s_center[Y] + s->s_size;
	obj_max[Z] = s->s_center[Z] + s->s_size;

	if (!st->have_bound) {
	    VMOVE(st->min, obj_min);
	    VMOVE(st->max, obj_max);
	    st->have_bound = 1;
	} else {
	    VMIN(st->min, obj_min);
	    VMAX(st->max, obj_max);
	}
	return;
    }

    /* Priority 3: compute bounds from the node's vlist (bv_node_vlist_set) */
    {
	struct bu_list *vl = (struct bu_list *)node->geometry;
	if (vl && !BU_LIST_IS_EMPTY(vl)) {
	    point_t vmin, vmax;
	    VSETALL(vmin,  INFINITY);
	    VSETALL(vmax, -INFINITY);
	    (void)bv_vlist_bbox(vl, &vmin, &vmax, NULL, NULL);
	    if (vmin[X] <= vmax[X]) {
		if (!st->have_bound) {
		    VMOVE(st->min, vmin);
		    VMOVE(st->max, vmax);
		    st->have_bound = 1;
		} else {
		    VMIN(st->min, vmin);
		    VMAX(st->max, vmax);
		}
	    }
	}
    }
}


int
bv_node_bbox(const struct bv_node *node, point_t *out_min, point_t *out_max)
{
    struct _bbox_state st;

    if (!node || !out_min || !out_max)
	return 0;

    st.have_bound = 0;
    VSETALL(st.min,  INFINITY);
    VSETALL(st.max, -INFINITY);

    bv_node_traverse(node, _bbox_cb, &st);

    if (st.have_bound) {
	VMOVE(*out_min, st.min);
	VMOVE(*out_max, st.max);
    }
    return st.have_bound;
}


int
bv_scene_bbox(const struct bv_scene *scene, point_t *out_min, point_t *out_max)
{
    struct _bbox_state st;

    if (!scene || !out_min || !out_max)
	return 0;

    st.have_bound = 0;
    VSETALL(st.min,  INFINITY);
    VSETALL(st.max, -INFINITY);

    bv_scene_traverse(scene, _bbox_cb, &st);

    if (st.have_bound) {
	VMOVE(*out_min, st.min);
	VMOVE(*out_max, st.max);
    }
    return st.have_bound;
}


/* ================================================================
 * bview_autoview_new
 *
 * Auto-position the camera in a bview_new to fit all visible geometry.
 * Analog of bv_autoview() for the new scene graph API.
 * ================================================================ */

int
bview_autoview_new(struct bview_new *view, const struct bv_scene *scene, double scale_factor)
{
    point_t bmin, bmax;
    vect_t  center;
    vect_t  radial;
    vect_t  sqrt_small;
    double  radius;
    double  dist;
    struct bview_camera cam;

    if (!view || !scene)
	return 0;

    /* Use same default factor as legacy bv_autoview */
    if (scale_factor < SQRT_SMALL_FASTF)
	scale_factor = 2.0;

    VSETALL(sqrt_small, SQRT_SMALL_FASTF);

    if (!bv_scene_bbox(scene, &bmin, &bmax)) {
	/* Empty scene: nothing to fit */
	return 0;
    }

    /* Scene center and radial extent */
    VADD2SCALE(center, bmax, bmin, 0.5);
    VSUB2(radial, bmax, center);

    /* Protect against inverted or degenerate bbox */
    VMAX(radial, sqrt_small);
    if (VNEAR_ZERO(radial, SQRT_SMALL_FASTF))
	VSETALL(radial, 1.0);

    radius = radial[X];
    V_MAX(radius, radial[Y]);
    V_MAX(radius, radial[Z]);

    /*
     * Derive a viewing direction from the current camera state.
     * Direction = normalize(eye - target).  If the camera has no
     * meaningful eye/target separation (i.e. it is fresh/default), fall
     * back to looking from +Z (front view).
     */
    {
	const struct bview_camera *cur = bview_camera_get(view);
	vect_t eye_dir;

	cam = *cur;   /* copy all fields, then overwrite position + target */

	VSUB2(eye_dir, cur->position, cur->target);
	if (VNEAR_ZERO(eye_dir, SQRT_SMALL_FASTF))
	    VSET(eye_dir, 0.0, 0.0, 1.0);
	VUNITIZE(eye_dir);

	/* Distance from target such that the scene subtends the view: */
	dist = scale_factor * radius;
	VJOIN1(cam.position, center, dist, eye_dir);
	VMOVE(cam.target, center);
	/* Set the scale to the scene radius so downstream LoD code gets a
	 * meaningful view-size (analogous to gv_scale after bv_autoview). */
	cam.scale = radius;
    }

    bview_camera_set(view, &cam);
    return 1;
}


/* ================================================================
 * Phase 2 convenience helpers
 *
 * bv_scene_insert_obj — wrap a legacy bv_scene_obj + add to scene
 * bview_insert_obj    — same, but through a bview_new
 * bv_scene_find_obj   — find the node whose user_data == obj
 * ================================================================ */

struct bv_node *
bv_scene_insert_obj(struct bv_scene *scene, struct bv_scene_obj *obj)
{
    if (!scene || !obj)
	return NULL;

    struct bv_node *n = bv_scene_obj_to_node(obj);
    if (!n)
	return NULL;

    bv_scene_add_node(scene, n);
    return n;
}


struct bv_node *
bview_insert_obj(struct bview_new *view, struct bv_scene_obj *obj)
{
    if (!view || !obj)
	return NULL;

    /* Create a scene for this view on demand if none exists */
    struct bv_scene *scene = bview_scene_get(view);
    if (!scene) {
	scene = bv_scene_create();
	if (!scene)
	    return NULL;
	bview_scene_set(view, scene);
    }

    return bv_scene_insert_obj(scene, obj);
}


/* Traversal callback state for bv_scene_find_obj */
struct _find_obj_state {
    const struct bv_scene_obj *target;
    struct bv_node            *result;
};

static void
_find_obj_cb(struct bv_node *node, void *ctx)
{
    struct _find_obj_state *st = (struct _find_obj_state *)ctx;
    if (st->result)
	return; /* already found */
    if (bv_node_user_data_get(node) == (void *)st->target)
	st->result = node;
}

struct bv_node *
bv_scene_find_obj(const struct bv_scene *scene, const struct bv_scene_obj *obj)
{
    if (!scene || !obj)
	return NULL;

    struct _find_obj_state st;
    st.target = obj;
    st.result = NULL;

    bv_scene_traverse(scene, _find_obj_cb, &st);
    return st.result;
}


int
bv_scene_remove_obj(struct bv_scene *scene, const struct bv_scene_obj *obj)
{
    if (!scene || !obj)
	return 0;

    struct bv_node *n = bv_scene_find_obj(scene, obj);
    if (!n)
	return 0;

    bv_scene_remove_node(scene, n);
    bv_node_destroy(n);
    return 1;
}


int
bview_remove_obj(struct bview_new *view, const struct bv_scene_obj *obj)
{
    if (!view || !obj)
	return 0;

    return bv_scene_remove_obj(bview_scene_get(view), obj);
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
