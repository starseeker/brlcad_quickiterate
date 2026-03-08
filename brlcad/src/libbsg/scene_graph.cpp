/*               S C E N E _ G R A P H . C P P
 * BRL-CAD
 *
 * Copyright (c) 1993-2025 United States Government as represented by
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
/** @file libbsg/scene_graph.cpp
 *
 * @brief Phase 1 and Phase 2 BSG API implementation.
 *
 * ### Phase 1 (struct layout)
 *
 * @c struct @c bsg_shape and @c struct @c bv_scene_obj are two independent
 * struct definitions with identical field layouts (defined in @c bv/defines.h).
 * This gives external libbv users unchanged behaviour while letting the
 * @c bsg_* API evolve its layout independently in later phases.
 *
 * The @c bsg_shape.i field is of type @c bsg_shape_internal* (managed by
 * libbsg; forward-declared in @c bv/defines.h).  The legacy @c bv_scene_obj.i
 * remains @c bv_scene_obj_internal*.  No reinterpret casts are needed for
 * @c bsg_shape in libbsg code.
 *
 * For @c bsg_scene (typedef alias of @c bview_set): the @c .i field is now
 * typed @c bsg_scene_set_internal* (defined in @c bsg/scene_set.h), so
 * BSG_SCENEI is also a direct accessor with no cast.  @c bview_set_internal
 * is a typedef alias for @c bsg_scene_set_internal in @c libbv/bv_private.h.
 *
 * ### Phase 2 additions (this file)
 *
 * 1. Camera as scene-graph node: @c bsg_node_alloc(), @c bsg_camera_node_alloc(),
 *    @c bsg_camera_node_set/get(), @c bsg_view_set_camera().
 * 2. Per-view scene roots: @c bsg_scene_root_get/set/create().
 * 3. Graph traversal entry point: @c bsg_view_traverse().
 * 4. Sensor / change notification: @c bsg_shape_add/rm_sensor(); sensors fire
 *    inside @c bsg_shape_stale().
 * 5. LoD as group node: @c bsg_lod_group_alloc(), @c bsg_lod_group_select_child();
 *    @c bsg_traverse() selects children by distance for @c BSG_NODE_LOD_GROUP nodes.
 */

#include "common.h"

/* bsg/defines.h pulls in bv/defines.h, bv/util.h, bv/lod.h, bv/view_sets.h */
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/lod.h"
#include "bsg/polygon.h"

/* bv/polygon.h declares the legacy functions wrapped in this file */
#include "bsg/polygon.h"
#include "bsg/view_sets.h"

/* bn/mat.h provides bn_mat_mul() used by bsg_traverse() */
#include "bn/mat.h"

/* bu/malloc.h for BU_ALLOC/BU_PUT used by standalone node allocator */
#include "bu/malloc.h"
#include "bu/vls.h"
#include "bu/ptbl.h"

/* bsg_private.h for bsg_scene_obj_internal and bsg_scene_set_internal */
#include "./bsg_private.h"

/* bv/snap.h declares bv_view_center_linesnap, wrapped as bsg_view_center_linesnap */
#include "bsg/snap.h"

/* bv/vlist.h provides BV_FREE_VLIST used by bsg_node_free() */
#include "bsg/vlist.h"

#include <unordered_map>
#include <vector>
#include <cstring>  /* memset */

/* ====================================================================== *
 * Phase 2: module-level side-channel state (forward declarations)       *
 * ====================================================================== */

namespace {

/* Per-view scene roots (Issue 2 / 3) */
std::unordered_map<const bsg_view *, bsg_shape *> &
scene_root_map()
{
    static std::unordered_map<const bsg_view *, bsg_shape *> m;
    return m;
}

/* Sensor entries: node → list of {handle, callback, data} */
struct SensorEntry {
    unsigned long long handle;
    void (*callback)(bsg_shape *, void *);
    void *data;
};
std::unordered_map<bsg_shape *, std::vector<SensorEntry>> &
sensor_map()
{
    static std::unordered_map<bsg_shape *, std::vector<SensorEntry>> m;
    return m;
}

/* Counter for unique sensor handles */
unsigned long long &
next_sensor_handle()
{
    static unsigned long long h = 1;
    return h;
}

/* Per-view filtered-shapes tables for bsg_view_shapes() cache.
 * Key: (view*, type_flag) → bu_ptbl of shapes from root->children with
 * s_type_flags & type == type.  Rebuilt lazily when the root's children
 * change.  We store ONE table per view per type; the pointer is stable
 * for the lifetime of the entry.  Entries are invalidated (and the table
 * reset) whenever bsg_shape_get/put modifies root->children. */
struct ShapesEntry {
    struct bu_ptbl tbl;
    bool valid = false;
    ShapesEntry() { bu_ptbl_init(&tbl, 8, "bsg_view_shapes cache"); }
    ~ShapesEntry() { bu_ptbl_free(&tbl); }
};
std::unordered_map<const bsg_view *,
    std::unordered_map<int, ShapesEntry>> &
view_shapes_cache()
{
    static std::unordered_map<const bsg_view *,
	std::unordered_map<int, ShapesEntry>> m;
    return m;
}


} /* anonymous namespace */

/* Fire sensors for a node — called from bsg_shape_stale() (Phase 4). */
static void
bsg_fire_sensors(bsg_shape *s)
{
    auto &m = sensor_map();
    auto it = m.find(s);
    if (it == m.end()) return;
    /* Iterate over a copy so callbacks may safely remove their own sensor. */
    std::vector<SensorEntry> snap = it->second;
    for (auto &e : snap) {
	if (e.callback)
	    e.callback(s, e.data);
    }
}

/* ====================================================================== *
 * Phase 1 layout sanity checks                                           *
 *                                                                        *
 * bsg_shape and bv_scene_obj are independent structs with the same       *
 * field layout; confirm their sizes agree.  The cast helpers             *
 * bso_to_bv/bv_to_bso have been removed since no direct cross-boundary  *
 * casts are needed in this file after the Scene/SHAPI cleanup.           *
 * ====================================================================== */
static_assert(sizeof(bsg_material)  == sizeof(bv_obj_settings),  "bsg_material size mismatch");
static_assert(sizeof(bsg_shape)     == sizeof(bv_scene_obj),     "bsg_shape / bv_scene_obj layout mismatch");
static_assert(sizeof(bsg_lod)       == sizeof(bv_mesh_lod),      "bsg_lod size mismatch");
static_assert(sizeof(bsg_view)      == sizeof(bview),            "bsg_view size mismatch");
static_assert(sizeof(bsg_scene)     == sizeof(bview_set),        "bsg_scene size mismatch");

/* ====================================================================== *
 * Phase 2: traversal state                                               *
 * ====================================================================== */

extern "C" void
bsg_traversal_state_init(bsg_traversal_state *state)
{
    if (!state)
	return;
    MAT_IDN(state->xform);
    struct bv_obj_settings defaults = BV_OBJ_SETTINGS_INIT;
    bsg_material_sync(&state->material, &defaults);
    state->depth         = 0;
    state->active_camera = NULL;
    state->view          = NULL;
}

/* ====================================================================== *
 * Phase 2: scene graph traversal (updated)                               *
 * ====================================================================== */

extern "C" void
bsg_traverse(bsg_shape *root,
	     bsg_traversal_state *state,
	     int (*visit)(bsg_shape *, const bsg_traversal_state *, void *),
	     void *user_data)
{
    if (!root || !state || !visit)
	return;

    /* Separator: save current state so children cannot pollute ancestors. */
    int is_sep = (root->s_type_flags & BSG_NODE_SEPARATOR) != 0;
    bsg_traversal_state saved;
    if (is_sep)
	saved = *state;

    /* Camera node: update active_camera in traversal state. */
    const struct bsg_camera *prev_camera = state->active_camera;
    if (root->s_type_flags & BSG_NODE_CAMERA) {
	state->active_camera =
	    static_cast<const struct bsg_camera *>(root->s_i_data);
    }

    /* Accumulate transform: state->xform = old_xform * node->s_mat */
    mat_t new_xform;
    bn_mat_mul(new_xform, state->xform, root->s_mat);
    MAT_COPY(state->xform, new_xform);

    /* Accumulate material: update only when the node supplies its own settings. */
    if (root->s_os && !root->s_inherit_settings)
	state->material = *root->s_os;

    state->depth++;

    /* For structural/container nodes, call the visitor before children so
     * camera and transform state is already set up when children are drawn.
     *
     * For geometry (leaf) nodes with children (e.g. a polygon with a :fill
     * child), we want children drawn FIRST so the parent outline renders on
     * top of fill segments.  We detect geometry nodes as those that are NOT
     * one of the structural flag types. */
    unsigned long long structural_flags =
	(unsigned long long)(BSG_NODE_SEPARATOR | BSG_NODE_TRANSFORM |
			     BSG_NODE_CAMERA    | BSG_NODE_LOD_GROUP);
    int is_geometry = !(root->s_type_flags & structural_flags);

    /* For structural nodes: visit (setup) first, then recurse into children. */
    /* For geometry nodes: recurse into children first, visit (render) after. */
    int prune = 0;
    if (!is_geometry)
	prune = visit(root, state, user_data);

    if (!prune) {
	/* LoD group: pick one child based on viewer distance. */
	if (root->s_type_flags & BSG_NODE_LOD_GROUP) {
	    if (BU_PTBL_LEN(&root->children) > 0) {
		fastf_t dist = 0.0;
		if (state->view && state->active_camera) {
		    /* Compute distance from eye to node center in model space */
		    point_t eye;
		    VMOVE(eye, state->active_camera->eye_pos);
		    vect_t delta;
		    point_t center;
		    VMOVE(center, root->s_center);
		    VSUB2(delta, center, eye);
		    dist = MAGNITUDE(delta);
		}
		int child_idx = bsg_lod_group_select_child(root, dist);
		if (child_idx < 0 || (size_t)child_idx >= BU_PTBL_LEN(&root->children))
		    child_idx = 0;
		bsg_shape *child =
		    (bsg_shape *)BU_PTBL_GET(&root->children, (size_t)child_idx);
		bsg_traverse(child, state, visit, user_data);
	    }
	} else {
	    for (size_t ci = 0; ci < BU_PTBL_LEN(&root->children); ci++) {
		bsg_shape *child = (bsg_shape *)BU_PTBL_GET(&root->children, ci);
		bsg_traverse(child, state, visit, user_data);
	    }
	}
    }

    /* For geometry nodes, visit (render) AFTER children so the parent
     * outline/geometry paints on top of any fill/child geometry. */
    if (is_geometry && !prune)
	visit(root, state, user_data);

    state->depth--;

    /* Restore full state if this was a separator. */
    if (is_sep)
	*state = saved;
    else if (root->s_type_flags & BSG_NODE_CAMERA)
	state->active_camera = prev_camera;
}

/* ====================================================================== *
 * Phase 2: view-scale accessors                                         *
 * ====================================================================== */

extern "C" fastf_t
bsg_view_scale(const bsg_view *v)
{
    if (!v) return 0.0;
    return v->gv_scale;
}

extern "C" fastf_t
bsg_view_local2base(const bsg_view *v)
{
    if (!v) return 1.0;
    return v->gv_local2base;
}

extern "C" fastf_t
bsg_view_base2local(const bsg_view *v)
{
    if (!v) return 1.0;
    return v->gv_base2local;
}

extern "C" void
bsg_view_set_scale(bsg_view *v, fastf_t scale)
{
    if (!v) return;
    v->gv_scale = scale;
}

/* ====================================================================== *
 * Phase 2: camera accessor (updated: bsg_view_get_camera existed,        *
 *          bsg_view_set_camera is new)                                   *
 * ====================================================================== */

extern "C" void
bsg_view_get_camera(const bsg_view *v, struct bsg_camera *out)
{
    if (!v || !out)
	return;
    out->perspective  = v->gv_perspective;
    VMOVE(out->aet,      v->gv_aet);
    VMOVE(out->eye_pos,  v->gv_eye_pos);
    VMOVE(out->keypoint, v->gv_keypoint);
    out->coord        = v->gv_coord;
    out->rotate_about = v->gv_rotate_about;
    MAT_COPY(out->rotation,    v->gv_rotation);
    MAT_COPY(out->center,      v->gv_center);
    MAT_COPY(out->model2view,  v->gv_model2view);
    MAT_COPY(out->pmodel2view, v->gv_pmodel2view);
    MAT_COPY(out->view2model,  v->gv_view2model);
    MAT_COPY(out->pmat,        v->gv_pmat);
}

extern "C" void
bsg_view_set_camera(bsg_view *v, const struct bsg_camera *cam)
{
    if (!v || !cam)
	return;
    v->gv_perspective = cam->perspective;
    VMOVE(v->gv_aet,      cam->aet);
    VMOVE(v->gv_eye_pos,  cam->eye_pos);
    VMOVE(v->gv_keypoint, cam->keypoint);
    v->gv_coord        = cam->coord;
    v->gv_rotate_about = cam->rotate_about;
    MAT_COPY(v->gv_rotation,    cam->rotation);
    MAT_COPY(v->gv_center,      cam->center);
    MAT_COPY(v->gv_model2view,  cam->model2view);
    MAT_COPY(v->gv_pmodel2view, cam->pmodel2view);
    MAT_COPY(v->gv_view2model,  cam->view2model);
    MAT_COPY(v->gv_pmat,        cam->pmat);

    /* Keep the scene-root camera node in sync so bsg_view_traverse always
     * sees an up-to-date camera without callers having to do it explicitly. */
    bsg_shape *cam_node = bsg_scene_root_camera(v);
    if (cam_node)
	bsg_camera_node_set(cam_node, cam);
}

/* ====================================================================== *
 * Phase 2: standalone node allocation (Issues 1, 5)                     *
 * ====================================================================== */

/* Free callback for camera nodes: frees the heap-allocated bsg_camera. */
static void
camera_node_free_cb(struct bsg_shape *s)
{
    if (!s || !s->s_i_data)
	return;
    bu_free(s->s_i_data, "bsg_camera");
    s->s_i_data = NULL;
}

/* Free callback for LoD group nodes: frees the bsg_lod_switch_data. */
static void
lod_group_free_cb(struct bsg_shape *s)
{
    if (!s || !s->s_i_data)
	return;
    bsg_lod_switch_data *sd = static_cast<bsg_lod_switch_data *>(s->s_i_data);
    if (sd->switch_distances)
	bu_free(sd->switch_distances, "bsg_lod_switch_data.distances");
    bu_free(sd, "bsg_lod_switch_data");
    s->s_i_data = NULL;
}

extern "C" bsg_shape *
bsg_node_alloc(int type_flags)
{
    bsg_shape *s = NULL;
    BU_ALLOC(s, bsg_shape);
    if (!s) return NULL;

    s->i = new bsg_shape_internal;

    /* Minimal initialisation matching bv_obj_reset logic without needing pools. */
    s->s_type_flags = (unsigned long long)type_flags;
    s->s_free_callback = NULL;
    s->s_dlist_free_callback = NULL;
    s->s_update_callback = NULL;
    s->s_v = NULL;
    s->s_i_data = NULL;
    s->s_path = NULL;
    s->dp = NULL;
    s->draw_data = NULL;
    s->free_scene_obj = NULL;
    s->vlfree = NULL;
    s->otbl = NULL;
    s->s_os = &s->s_local_os;
    s->s_inherit_settings = 0;
    struct bv_obj_settings defaults = BV_OBJ_SETTINGS_INIT;
    bsg_material_sync(&s->s_local_os, &defaults);
    MAT_IDN(s->s_mat);
    BU_VLS_INIT(&s->s_name);
    BU_LIST_INIT(&s->s_vlist);
    BU_PTBL_INIT(&s->children);
    s->s_flag = UP;
    s->s_iflag = DOWN;
    s->s_force_draw = 0;
    VSET(s->s_color, 255, 0, 0);
    VSETALL(s->s_center, 0);
    VSETALL(s->bmin, INFINITY);
    VSETALL(s->bmax, -INFINITY);
    s->have_bbox = 0;
    s->s_dlist = 0;
    s->s_dlist_stale = 0;
    s->s_dlist_mode = 0;
    s->s_size = 0;
    s->s_csize = 0;
    s->s_displayobj = 0;
    s->s_soldash = 0;
    s->s_arrow = 0;
    s->s_changed = 0;
    s->current = 0;
    s->adaptive_wireframe = 0;
    s->csg_obj = 0;
    s->mesh_obj = 0;
    s->view_scale = 0;
    s->bot_threshold = 0;
    s->curve_scale = 0;
    s->point_scale = 0;
    s->s_vlen = 0;
    s->parent = NULL;
    s->s_u_data = NULL;
    memset(&s->s_old, 0, sizeof(s->s_old));
    return s;
}

extern "C" void
bsg_node_free(bsg_shape *s, int recurse)
{
    if (!s)
	return;

    /* Remove sensors if any are registered. */
    sensor_map().erase(s);

    /* Recurse into children first, if requested. */
    if (recurse && BU_PTBL_IS_INITIALIZED(&s->children)) {
	for (size_t ci = 0; ci < BU_PTBL_LEN(&s->children); ci++) {
	    bsg_shape *child = (bsg_shape *)BU_PTBL_GET(&s->children, ci);
	    bsg_node_free(child, recurse);
	}
	bu_ptbl_reset(&s->children);
    }

    /* Fire s_free_callback to release s_i_data (e.g., camera, lod data). */
    if (s->s_free_callback)
	(*s->s_free_callback)(s);

    if (BU_PTBL_IS_INITIALIZED(&s->children))
	bu_ptbl_free(&s->children);

    if (BU_LIST_IS_INITIALIZED(&s->s_vlist))
	BV_FREE_VLIST(s->vlfree, &s->s_vlist);

    if (BU_VLS_IS_INITIALIZED(&s->s_name))
	bu_vls_free(&s->s_name);

    if (s->i) {
	delete s->i;
	s->i = NULL;
    }

    bu_free(s, "bsg_shape standalone");
}

/* ====================================================================== *
 * Phase 2: camera node API (Issue 1)                                     *
 * ====================================================================== */

extern "C" bsg_shape *
bsg_camera_node_alloc(const bsg_view *v)
{
    bsg_shape *node = bsg_node_alloc(BSG_NODE_CAMERA);
    if (!node)
	return NULL;

    struct bsg_camera *cam = NULL;
    BU_ALLOC(cam, struct bsg_camera);
    if (!cam) {
	bsg_node_free(node, 0);
	return NULL;
    }
    memset(cam, 0, sizeof(*cam));
    if (v)
	bsg_view_get_camera(v, cam);

    node->s_i_data = cam;
    node->s_free_callback = camera_node_free_cb;
    return node;
}

extern "C" void
bsg_camera_node_set(bsg_shape *node, const struct bsg_camera *cam)
{
    if (!node || !cam)
	return;
    if (!(node->s_type_flags & BSG_NODE_CAMERA))
	return;
    if (!node->s_i_data) {
	struct bsg_camera *c = NULL;
	BU_ALLOC(c, struct bsg_camera);
	if (!c) return;
	node->s_i_data = c;
	node->s_free_callback = camera_node_free_cb;
    }
    *static_cast<struct bsg_camera *>(node->s_i_data) = *cam;
}

extern "C" int
bsg_camera_node_get(const bsg_shape *node, struct bsg_camera *out)
{
    if (!node || !out)
	return -1;
    if (!(node->s_type_flags & BSG_NODE_CAMERA))
	return -1;
    if (!node->s_i_data)
	return -1;
    *out = *static_cast<const struct bsg_camera *>(node->s_i_data);
    return 0;
}

/* ====================================================================== *
 * Phase 2: per-view scene roots (Issues 2 / 3)                          *
 * ====================================================================== */

extern "C" bsg_shape *
bsg_scene_root_get(const bsg_view *v)
{
    if (!v) return NULL;
    auto &m = scene_root_map();
    auto it = m.find(v);
    return (it != m.end()) ? it->second : NULL;
}

extern "C" void
bsg_scene_root_set(bsg_view *v, bsg_shape *root)
{
    if (!v) return;
    auto &m = scene_root_map();
    if (root)
	m[v] = root;
    else {
	m.erase(v);
	/* Drop cached filtered tables for this view too. */
	view_shapes_cache().erase(v);
    }
}

extern "C" bsg_shape *
bsg_scene_root_create(bsg_view *v)
{
    if (!v) return NULL;
    bsg_shape *root = bsg_node_alloc(BSG_NODE_SEPARATOR);
    if (!root) return NULL;

    /* Add a camera node as first child. */
    bsg_shape *cam_node = bsg_camera_node_alloc(v);
    if (cam_node) {
	bu_ptbl_ins(&root->children, (long *)cam_node);
    }

    bsg_scene_root_set(v, root);
    return root;
}

/* ====================================================================== *
 * Phase 2: view traversal entry point (Issue 3)                         *
 * ====================================================================== */

extern "C" void
bsg_view_traverse(bsg_view *v,
		  int (*visit)(bsg_shape *, const bsg_traversal_state *, void *),
		  void *user_data)
{
    if (!v || !visit) return;
    bsg_shape *root = bsg_scene_root_get(v);
    if (!root) return;

    bsg_traversal_state state;
    bsg_traversal_state_init(&state);
    state.view = v;

    bsg_traverse(root, &state, visit, user_data);
}

/* ====================================================================== *
 * Phase 2: sensor / change notification (Issue 4)                       *
 * ====================================================================== */

extern "C" unsigned long long
bsg_shape_add_sensor(bsg_shape *s,
		     void (*callback)(bsg_shape *, void *),
		     void *data)
{
    if (!s || !callback) return 0;

    unsigned long long h = next_sensor_handle()++;
    SensorEntry e;
    e.handle = h;
    e.callback = callback;
    e.data = data;
    sensor_map()[s].push_back(e);
    return h;
}

extern "C" int
bsg_shape_rm_sensor(bsg_shape *s, unsigned long long handle)
{
    if (!s || handle == 0) return -1;
    auto &m = sensor_map();
    auto it = m.find(s);
    if (it == m.end()) return -1;

    auto &vec = it->second;
    for (auto vi = vec.begin(); vi != vec.end(); ++vi) {
	if (vi->handle == handle) {
	    vec.erase(vi);
	    if (vec.empty())
		m.erase(it);
	    return 0;
	}
    }
    return -1;
}

/* ====================================================================== *
 * Phase 2: LoD group node (Issue 5)                                     *
 * ====================================================================== */

extern "C" bsg_shape *
bsg_lod_group_alloc(int num_levels, const fastf_t *switch_distances)
{
    if (num_levels < 1) return NULL;

    bsg_shape *node = bsg_node_alloc(BSG_NODE_LOD_GROUP);
    if (!node) return NULL;

    bsg_lod_switch_data *sd = NULL;
    BU_ALLOC(sd, bsg_lod_switch_data);
    if (!sd) { bsg_node_free(node, 0); return NULL; }

    sd->num_levels = num_levels;
    int ndist = num_levels - 1;
    if (ndist > 0 && switch_distances) {
	sd->switch_distances = (fastf_t *)bu_malloc(
	    (size_t)ndist * sizeof(fastf_t), "lod switch distances");
	if (!sd->switch_distances) {
	    bu_free(sd, "bsg_lod_switch_data");
	    bsg_node_free(node, 0);
	    return NULL;
	}
	for (int i = 0; i < ndist; i++)
	    sd->switch_distances[i] = switch_distances[i];
    } else {
	sd->switch_distances = NULL;
    }

    node->s_i_data = sd;
    node->s_free_callback = lod_group_free_cb;
    return node;
}

extern "C" int
bsg_lod_group_select_child(const bsg_shape *node, fastf_t viewer_distance)
{
    if (!node) return -1;
    if (!(node->s_type_flags & BSG_NODE_LOD_GROUP)) return -1;
    if (!node->s_i_data) return 0;

    const bsg_lod_switch_data *sd =
	static_cast<const bsg_lod_switch_data *>(node->s_i_data);

    if (sd->num_levels <= 1 || !sd->switch_distances)
	return 0;

    for (int i = 0; i < sd->num_levels - 1; i++) {
	if (viewer_distance <= sd->switch_distances[i])
	    return i;
    }
    return sd->num_levels - 1;
}

/* ====================================================================== *
 * Phase 2: new helper functions (Section 9 of TODO.BSG-modernization)   *
 * ====================================================================== */

/*
 * bsg_view_find_by_type — collect all nodes in the scene graph for view @p v
 * whose s_type_flags has @p type_flag bits set.  Results are appended to
 * @p result (must already be initialised by the caller).
 */

/* Internal visitor state for bsg_view_find_by_type. */
struct _find_by_type_ctx {
    unsigned long long type_flag;
    struct bu_ptbl    *result;
};

static int
_find_by_type_visitor(bsg_shape *s, const bsg_traversal_state * /*state*/,
		      void *user_data)
{
    struct _find_by_type_ctx *ctx =
	static_cast<struct _find_by_type_ctx *>(user_data);
    if (s->s_type_flags & ctx->type_flag)
	bu_ptbl_ins(ctx->result, (long *)s);
    return 0; /* always recurse */
}

extern "C" void
bsg_view_find_by_type(bsg_view *v, unsigned long long type_flag,
		      struct bu_ptbl *result)
{
    if (!v || !result)
	return;
    bsg_shape *root = bsg_scene_root_get(v);
    if (!root)
	return;

    struct _find_by_type_ctx ctx;
    ctx.type_flag = type_flag;
    ctx.result    = result;

    bsg_traversal_state state;
    bsg_traversal_state_init(&state);
    state.view = v;
    bsg_traverse(root, &state, _find_by_type_visitor, &ctx);
}

/*
 * bsg_scene_root_camera — convenience: return the first BSG_NODE_CAMERA child
 * of the scene root for view @p v, or NULL if none exists.
 */
extern "C" bsg_shape *
bsg_scene_root_camera(const bsg_view *v)
{
    if (!v) return NULL;
    bsg_shape *root = bsg_scene_root_get(v);
    if (!root) return NULL;

    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	bsg_shape *child = (bsg_shape *)BU_PTBL_GET(&root->children, i);
	if (child->s_type_flags & BSG_NODE_CAMERA)
	    return child;
    }
    return NULL;
}

/*
 * bsg_view_mat_aet_camera — like bsg_view_mat_aet() but operates on a
 * standalone bsg_camera struct.  Recomputes the rotation matrix from
 * cam->aet using the same convention as bsg_view_mat_aet().
 *
 * Internally we round-trip through a temporary bsg_view so that the
 * well-tested bsg_view_mat_aet() code path handles all the matrix algebra.
 */
extern "C" void
bsg_view_mat_aet_camera(struct bsg_camera *cam)
{
    if (!cam)
	return;

    /* Round-trip: copy into a temp view, run bv_mat_aet, copy back. */
    bsg_view tmp;
    bsg_view_init(&tmp, NULL);
    bsg_view_set_camera(&tmp, cam);
    bsg_view_mat_aet(&tmp);
    bsg_view_get_camera(&tmp, cam);
    bsg_view_free(&tmp);
}

/*
 * bsg_sensor_fire — walk the sub-tree rooted at @p root and fire sensors on
 * every node whose s_type_flags has @p type_mask bits set.  Pass type_mask=0
 * to fire sensors on ALL nodes in the sub-tree.
 *
 * This is a bulk notification utility; individual nodes are still notified
 * through the normal bsg_shape_stale() path.
 */
extern "C" void
bsg_sensor_fire(bsg_shape *root, unsigned long long type_mask)
{
    if (!root)
	return;

    /* Fire sensors on this node if it matches the mask (or mask is 0). */
    if (!type_mask || (root->s_type_flags & type_mask))
	bsg_fire_sensors(root);

    /* Recurse into children. */
    if (BU_PTBL_IS_INITIALIZED(&root->children)) {
	for (size_t ci = 0; ci < BU_PTBL_LEN(&root->children); ci++) {
	    bsg_shape *child = (bsg_shape *)BU_PTBL_GET(&root->children, ci);
	    bsg_sensor_fire(child, type_mask);
	}
    }
}

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
