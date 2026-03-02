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
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "bn/mat.h"
#include "bv/defines.h"
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


struct bv_node *
bv_scene_default_camera(const struct bv_scene *scene)
{
    if (!scene)
	return NULL;
    return scene->default_camera;
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

    bu_vls_free(&view->name);
    bu_ptbl_free(&view->pick_set.selected_objs);
    BU_PUT(view, struct bview_new);
}


void
bview_scene_set(struct bview_new *view, struct bv_scene *scene)
{
    if (!view)
	return;
    view->scene = scene;
}


struct bv_scene *
bview_scene_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return view->scene;
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


void
bview_lod_update(struct bview_new *view)
{
    if (!view)
	return;
    /* Placeholder: trigger LoD recomputation for all geometry nodes in scene */
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

    /* Copy viewport */
    view->viewport.width  = old->gv_width;
    view->viewport.height = old->gv_height;
    view->viewport.dpi    = BV_DEFAULT_DPI;

    /* Store reference to legacy view */
    view->old_bview = (struct bview *)old;
}


void
bview_to_old(const struct bview_new *view, struct bview *old)
{
    if (!view || !old)
	return;

    /* Push camera parameters back to old bview */
    VMOVE(old->gv_eye_pos, view->camera.position);
    old->gv_perspective = view->camera.perspective ? view->camera.fov : 0.0;

    /* Viewport */
    old->gv_width  = view->viewport.width;
    old->gv_height = view->viewport.height;
}


struct bview *
bview_old_get(const struct bview_new *view)
{
    if (!view)
	return NULL;
    return view->old_bview;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
