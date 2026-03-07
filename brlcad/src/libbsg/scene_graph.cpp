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
 * Because the two structs are distinct C types, wrappers that bridge
 * @c bsg_shape pointers to @c bv_* functions use @c bso_to_bv() /
 * @c bv_to_bso() — thin reinterpret casts that are safe because both
 * structs share the same memory layout throughout Phase 1.
 *
 * For all other @c bsg_* types (@c bsg_view, @c bsg_scene, @c bsg_lod, etc.)
 * the typedef-alias approach is still used, so no cast is required.
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
#include "bv/polygon.h"
#include "bv/view_sets.h"

/* bn/mat.h provides bn_mat_mul() used by bsg_traverse() */
#include "bn/mat.h"

/* bu/malloc.h for BU_ALLOC/BU_PUT used by standalone node allocator */
#include "bu/malloc.h"
#include "bu/vls.h"
#include "bu/ptbl.h"

/* bsg_private.h for bsg_scene_obj_internal and bsg_scene_set_internal */
#include "./bsg_private.h"

/* bv/snap.h declares bv_view_center_linesnap, wrapped as bsg_view_center_linesnap */
#include "bv/snap.h"

/* bv/vlist.h provides BV_FREE_VLIST used by bsg_node_free() */
#include "bv/vlist.h"

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

/* Invalidate all cached filtered tables for a view (called after insert/rm) */
static void
_invalidate_shapes_cache(const bsg_view *v)
{
    auto it = view_shapes_cache().find(v);
    if (it == view_shapes_cache().end()) return;
    for (auto &kv : it->second)
	kv.second.valid = false;
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
 * field layout; confirm their sizes agree so the reinterpret casts in    *
 * this file remain safe.  Other bsg_* types are typedef aliases of their *
 * bv_* counterparts so no casts are needed there.                        *
 * ====================================================================== */
static_assert(sizeof(bsg_material)  == sizeof(bv_obj_settings),  "bsg_material size mismatch");
static_assert(sizeof(bsg_shape)     == sizeof(bv_scene_obj),     "bsg_shape / bv_scene_obj layout mismatch");
static_assert(sizeof(bsg_lod)       == sizeof(bv_mesh_lod),      "bsg_lod size mismatch");
static_assert(sizeof(bsg_view)      == sizeof(bview),            "bsg_view size mismatch");
static_assert(sizeof(bsg_scene)     == sizeof(bview_set),        "bsg_scene size mismatch");

/* ====================================================================== *
 * Cast helpers: bsg_shape * <-> bv_scene_obj *                           *
 *                                                                        *
 * Both structs have the same memory layout throughout Phase 1, so these  *
 * reinterpret casts are safe.  Using named helpers keeps the intent      *
 * visible and makes it trivial to grep for all cross-boundary sites.     *
 * ====================================================================== */
static inline bv_scene_obj *bso_to_bv(bsg_shape *s)
    { return reinterpret_cast<bv_scene_obj *>(s); }
static inline const bv_scene_obj *bso_to_bv(const bsg_shape *s)
    { return reinterpret_cast<const bv_scene_obj *>(s); }
static inline bsg_shape *bv_to_bso(bv_scene_obj *s)
    { return reinterpret_cast<bsg_shape *>(s); }

/* ====================================================================== *
 * View lifecycle                                                          *
 * ====================================================================== */

extern "C" void
bsg_view_init(bsg_view *v, bsg_scene *scene)
{
    bv_init(v, scene);
}

extern "C" void
bsg_view_free(bsg_view *v)
{
    bv_free(v);
}

extern "C" void
bsg_view_settings_init(bsg_view_settings *s)
{
    bv_settings_init(s);
}

extern "C" void
bsg_view_mat_aet(bsg_view *v)
{
    bv_mat_aet(v);
}

/* ====================================================================== *
 * View manipulation                                                       *
 * ====================================================================== */

extern "C" void
bsg_view_autoview(bsg_view *v, fastf_t scale, int all_view_objs)
{
    /* Fully-BSG path: compute bounds directly from root->children so we
     * never need the legacy flat tables.  Fall back to bv_autoview only
     * when there is no scene root (pre-BSG views). */
    bsg_shape *root = bsg_scene_root_get(v);
    if (!root) {
	bv_autoview(v, scale, all_view_objs);
    } else {
	/* Walk root->children, skipping structural nodes, and accumulate
	 * the bounding sphere of every DB-object-based shape. */
	double factor = (scale < SQRT_SMALL_FASTF) ? 2.0 : scale;
	vect_t min, max, sqrt_small;
	VSETALL(sqrt_small, SQRT_SMALL_FASTF);
	VSETALL(min,  INFINITY);
	VSETALL(max, -INFINITY);
	int is_empty = 1;

	unsigned long long structural =
	    (unsigned long long)(BSG_NODE_SEPARATOR | BSG_NODE_TRANSFORM |
				 BSG_NODE_CAMERA | BSG_NODE_LOD_GROUP);

	for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	    bsg_shape *s = (bsg_shape *)BU_PTBL_GET(&root->children, i);
	    if (s->s_type_flags & structural)
		continue;
	    if (!s->have_bbox && !bv_scene_obj_bound(bso_to_bv(s), v))
		continue;
	    if (!s->have_bbox)
		continue;
	    vect_t minus, plus;
	    minus[X] = s->s_center[X] - s->s_size;
	    minus[Y] = s->s_center[Y] - s->s_size;
	    minus[Z] = s->s_center[Z] - s->s_size;
	    plus[X]  = s->s_center[X] + s->s_size;
	    plus[Y]  = s->s_center[Y] + s->s_size;
	    plus[Z]  = s->s_center[Z] + s->s_size;
	    VMIN(min, minus);
	    VMAX(max, plus);
	    is_empty = 0;
	}

	if (is_empty) {
	    /* Nothing drawn — use a sensible default. */
	    bv_autoview(v, scale, all_view_objs);
	} else {
	    vect_t center, radial;
	    VADD2SCALE(center, max, min, 0.5);
	    VSUB2(radial, max, center);
	    VMAX(radial, sqrt_small);
	    if (VNEAR_ZERO(radial, SQRT_SMALL_FASTF))
		VSETALL(radial, 1.0);

	    MAT_IDN(v->gv_center);
	    MAT_DELTAS_VEC_NEG(v->gv_center, center);
	    v->gv_scale = radial[X];
	    V_MAX(v->gv_scale, radial[Y]);
	    V_MAX(v->gv_scale, radial[Z]);
	    v->gv_size  = factor * v->gv_scale;
	    v->gv_isize = 1.0 / v->gv_size;
	    bsg_view_update(v);
	}
    }

    /* Sync camera node after view matrices were updated. */
    bsg_shape *cam_node = bsg_scene_root_camera(v);
    if (cam_node) {
	struct bsg_camera cur;
	bsg_view_get_camera(v, &cur);
	bsg_camera_node_set(cam_node, &cur);
    }
}

extern "C" void
bsg_view_sync(bsg_view *dst, bsg_view *src)
{
    bv_sync(dst, src);
}

extern "C" void
bsg_view_update(bsg_view *v)
{
    bv_update(v);
    /* Keep the scene-root camera node in sync whenever the view matrices
     * change so bsg_view_traverse always uses current matrices. */
    bsg_shape *cam_node = bsg_scene_root_camera(v);
    if (cam_node) {
	struct bsg_camera cur;
	bsg_view_get_camera(v, &cur);
	bsg_camera_node_set(cam_node, &cur);
    }
}

extern "C" int
bsg_view_update_selected(bsg_view *v)
{
    return bv_update_selected(v);
}

/* ====================================================================== *
 * Material sync                                                           *
 * ====================================================================== */

extern "C" int
bsg_material_sync(bsg_material *dst, const bsg_material *src)
{
    /* bv_obj_settings_sync takes non-const src in the legacy API */
    return bv_obj_settings_sync(dst, const_cast<bsg_material *>(src));
}

/* ====================================================================== *
 * View clearing                                                           *
 * ====================================================================== */

extern "C" size_t
bsg_view_clear(bsg_view *v, int flags)
{
    /* With the fully-separated BSG stack, shapes are in root->children ONLY
     * (otbl=NULL, not in flat tables).  We collect and free them directly via
     * bsg_shape_put rather than via bv_clear. */
    bsg_shape *root = bsg_scene_root_get(v);
    if (!root)
	return bv_clear(v, flags);

    unsigned long long structural =
	(unsigned long long)(BSG_NODE_SEPARATOR | BSG_NODE_TRANSFORM |
			     BSG_NODE_CAMERA    | BSG_NODE_LOD_GROUP);

    /* Snapshot shapes matching the requested type. */
    std::vector<bsg_shape *> to_free;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	bsg_shape *s = (bsg_shape *)BU_PTBL_GET(&root->children, i);
	if (s->s_type_flags & structural)
	    continue;

	bool type_match = false;
	if (!flags) {
	    type_match = true;
	} else {
	    if ((flags & BSG_DB_OBJS)   && (s->s_type_flags & BSG_DB_OBJS))
		type_match = true;
	    if ((flags & BSG_VIEW_OBJS) && (s->s_type_flags & BSG_VIEW_OBJS))
		type_match = true;
	}
	if (!type_match)
	    continue;

	bool is_local = (s->s_type_flags & BSG_LOCAL_OBJS) ||
			(s->s_v && s->s_v->independent);
	if (flags & BSG_LOCAL_OBJS) {
	    if (!is_local) continue;
	} else {
	    if (is_local) continue;
	}

	to_free.push_back(s);
    }

    if (to_free.empty())
	return 0;

    /* For shared objects, remove from ALL co-view roots first. */
    bool is_shared = !(flags & BSG_LOCAL_OBJS) && !v->independent && v->vset;
    if (is_shared) {
	struct bu_ptbl *vws = bv_set_views(v->vset);
	if (vws) {
	    for (size_t i = 0; i < BU_PTBL_LEN(vws); i++) {
		bsg_view *cv = (bsg_view *)BU_PTBL_GET(vws, i);
		if (cv->independent) continue;
		bsg_shape *croot = bsg_scene_root_get(cv);
		if (!croot) continue;
		for (bsg_shape *s : to_free)
		    bu_ptbl_rm(&croot->children, (long *)s);
		_invalidate_shapes_cache(cv);
	    }
	}
    } else {
	for (bsg_shape *s : to_free)
	    bu_ptbl_rm(&root->children, (long *)s);
	_invalidate_shapes_cache(v);
    }

    /* Free each shape: clear otbl so bv_obj_put only returns it to free-list. */
    for (bsg_shape *s : to_free) {
	s->otbl = NULL;   /* already NULL for BSG shapes; be explicit */
	bv_obj_put(bso_to_bv(s));
    }

    return to_free.size();
}

/* ====================================================================== *
 * View interaction                                                        *
 * ====================================================================== */

extern "C" int
bsg_view_adjust(bsg_view *v, int dx, int dy, point_t keypoint,
int mode, unsigned long long flags)
{
    return bv_adjust(v, dx, dy, keypoint, mode, flags);
}

extern "C" int
bsg_screen_to_view(bsg_view *v, fastf_t *fx, fastf_t *fy,
   fastf_t x, fastf_t y)
{
    return bv_screen_to_view(v, fx, fy, x, y);
}

extern "C" int
bsg_screen_pt(point_t *p, fastf_t x, fastf_t y, bsg_view *v)
{
    return bv_screen_pt(p, x, y, v);
}

/* ====================================================================== *
 * Shape lifecycle                                                         *
 * ====================================================================== */

extern "C" bsg_shape *
bsg_shape_get(bsg_view *v, int type)
{
    if (!v) return NULL;

    /* Fully separate BSG stack: allocate via bv_obj_create for proper
     * initialisation (free-list, vlfree, etc.) but set otbl=NULL so the
     * shape is NEVER inserted into the legacy flat tables
     * (gv_objs.db_objs / view_objs or vset shared tables).  Only the BSG
     * scene-root children table is used for tracking. */
    int ltype = type;
    if (v->independent)
	ltype |= BV_LOCAL_OBJS;

    struct bv_scene_obj *raw = bv_obj_create(v, ltype);
    if (!raw) return NULL;

    /* Clear the flat-table pointer so bv_obj_put will not touch it. */
    raw->otbl = NULL;

    bsg_shape *s = bv_to_bso(raw);

    /* Register in the BSG scene root(s) so bsg_view_traverse finds this
     * shape.  Shared (non-local, non-independent) objects belong to every
     * non-independent view in the vset; local or independent-view objects
     * belong only to view v. */
    if (!(type & BSG_LOCAL_OBJS) && !v->independent && v->vset) {
	struct bu_ptbl *vws = bv_set_views(v->vset);
	if (vws) {
	    for (size_t i = 0; i < BU_PTBL_LEN(vws); i++) {
		bsg_view *cv = (bsg_view *)BU_PTBL_GET(vws, i);
		/* Skip independent views — they don't share objects. */
		if (cv->independent) continue;
		bsg_shape *root = bsg_scene_root_get(cv);
		if (root) {
		    bu_ptbl_ins_unique(&root->children, (long *)s);
		    _invalidate_shapes_cache(cv);
		}
	    }
	    return s;
	}
    }
    /* Independent / local — register only in this view's root. */
    bsg_shape *root = bsg_scene_root_get(v);
    if (root) {
	bu_ptbl_ins_unique(&root->children, (long *)s);
	_invalidate_shapes_cache(v);
    }
    return s;
}

extern "C" bsg_shape *
bsg_shape_create(bsg_view *v, int type)
{
    return bv_to_bso(bv_obj_create(v, type));
}

extern "C" bsg_shape *
bsg_shape_get_child(bsg_shape *parent)
{
    return bv_to_bso(bv_obj_get_child(bso_to_bv(parent)));
}

extern "C" void
bsg_shape_reset(bsg_shape *s)
{
    bv_obj_reset(bso_to_bv(s));
}

extern "C" void
bsg_shape_put(bsg_shape *s)
{
    /* Remove from BSG scene root(s) before freeing. */
    if (s && s->s_v) {
	bsg_view *v = s->s_v;
	int is_shared = !(s->s_type_flags & BSG_LOCAL_OBJS) &&
			!v->independent && v->vset;
	if (is_shared) {
	    struct bu_ptbl *vws = bv_set_views(v->vset);
	    if (vws) {
		for (size_t i = 0; i < BU_PTBL_LEN(vws); i++) {
		    bsg_view *cv = (bsg_view *)BU_PTBL_GET(vws, i);
		    /* Skip independent views — they don't share objects. */
		    if (cv->independent) continue;
		    bsg_shape *root = bsg_scene_root_get(cv);
		    if (root) {
			bu_ptbl_rm(&root->children, (long *)s);
			_invalidate_shapes_cache(cv);
		    }
		}
	    }
	} else {
	    bsg_shape *root = bsg_scene_root_get(v);
	    if (root) {
		bu_ptbl_rm(&root->children, (long *)s);
		_invalidate_shapes_cache(v);
	    }
	}
    }
    bv_obj_put(bso_to_bv(s));
}

extern "C" void
bsg_shape_stale(bsg_shape *s)
{
    bv_obj_stale(bso_to_bv(s));
    /* Fire any sensors registered on this node (Phase 2). */
    bsg_fire_sensors(s);
}

extern "C" void
bsg_shape_sync(bsg_shape *dst, const bsg_shape *src)
{
    bv_obj_sync(bso_to_bv(dst), const_cast<bv_scene_obj *>(bso_to_bv(src)));
}

/* ====================================================================== *
 * Shape lookup                                                            *
 * ====================================================================== */

extern "C" bsg_shape *
bsg_shape_find_child(bsg_shape *s, const char *pattern)
{
    return bv_to_bso(bv_find_child(bso_to_bv(s), pattern));
}

extern "C" bsg_shape *
bsg_view_find_shape(bsg_view *v, const char *pattern)
{
    return bv_to_bso(bv_find_obj(v, pattern));
}

extern "C" void
bsg_view_uniq_name(struct bu_vls *result, const char *seed, bsg_view *v)
{
    bv_uniq_obj_name(result, seed, v);
}

/* ====================================================================== *
 * Shape view-specific objects                                             *
 * ====================================================================== */

extern "C" bsg_shape *
bsg_shape_for_view(bsg_shape *s, bsg_view *v)
{
    return bv_to_bso(bv_obj_for_view(bso_to_bv(s), v));
}

extern "C" bsg_shape *
bsg_shape_get_view_obj(bsg_shape *s, bsg_view *v)
{
    return bv_to_bso(bv_obj_get_vo(bso_to_bv(s), v));
}

extern "C" int
bsg_shape_have_view_obj(bsg_shape *s, bsg_view *v)
{
    return bv_obj_have_vo(bso_to_bv(s), v);
}

extern "C" int
bsg_shape_clear_view_obj(bsg_shape *s, bsg_view *v)
{
    return bv_clear_view_obj(bso_to_bv(s), v);
}

/* ====================================================================== *
 * Shape geometry / bounding                                               *
 * ====================================================================== */

extern "C" int
bsg_shape_bound(bsg_shape *s, bsg_view *v)
{
    return bv_scene_obj_bound(bso_to_bv(s), v);
}

extern "C" fastf_t
bsg_shape_vZ_calc(bsg_shape *s, bsg_view *v, int mode)
{
    return bv_vZ_calc(bso_to_bv(s), v, mode);
}

/* ====================================================================== *
 * Shape illumination                                                      *
 * ====================================================================== */

extern "C" int
bsg_shape_illum(bsg_shape *s, char ill_state)
{
    return bv_illum_obj(bso_to_bv(s), ill_state);
}

/* ====================================================================== *
 * View container accessors                                                *
 * ====================================================================== */

extern "C" struct bu_ptbl *
bsg_view_shapes(bsg_view *v, int type)
{
    /* When a BSG scene root exists use root->children as the authoritative
     * source, filtered by type flag.  This keeps the bv_* flat tables as
     * a legacy fallback when no scene root has been created yet. */
    bsg_shape *root = bsg_scene_root_get(v);
    if (root) {
	/* BV_LOCAL_OBJS / BSG_LOCAL_OBJS — local shapes live only in gv_objs,
	 * not in the shared scene root; fall through to legacy path. */
	if (type & BSG_LOCAL_OBJS)
	    return bv_view_objs(v, type);

	auto &cache = view_shapes_cache()[v][type];
	if (!cache.valid) {
	    bu_ptbl_reset(&cache.tbl);
	    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
		bsg_shape *s = (bsg_shape *)BU_PTBL_GET(&root->children, i);
		/* Skip structural nodes (camera, separator, etc.) */
		unsigned long long structural =
		    (unsigned long long)(BSG_NODE_SEPARATOR | BSG_NODE_TRANSFORM |
					 BSG_NODE_CAMERA    | BSG_NODE_LOD_GROUP);
		if (s->s_type_flags & structural)
		    continue;
		/* Match the requested object type category */
		int cat = type & ~BSG_LOCAL_OBJS;
		if (!cat || (s->s_type_flags & cat))
		    bu_ptbl_ins(&cache.tbl, (long *)s);
	    }
	    cache.valid = true;
	}
	return &cache.tbl;
    }
    return bv_view_objs(v, type);
}

/* ====================================================================== *
 * View geometry                                                           *
 * ====================================================================== */

extern "C" int
bsg_view_plane(plane_t *p, bsg_view *v)
{
    return bv_view_plane(p, v);
}

/* ====================================================================== *
 * Knob manipulation                                                       *
 * ====================================================================== */

extern "C" void
bsg_knobs_reset(bsg_knobs *k, int category)
{
    bv_knobs_reset(k, category);
}

extern "C" unsigned long long
bsg_knobs_hash(bsg_knobs *k, struct bu_data_hash_state *state)
{
    return bv_knobs_hash(k, state);
}

extern "C" int
bsg_knobs_cmd_process(vect_t *rvec, int *do_rot, vect_t *tvec, int *do_tran,
		      bsg_view *v, const char *cmd, fastf_t f,
		      char origin, int model_flag, int incr_flag)
{
    return bv_knobs_cmd_process(rvec, do_rot, tvec, do_tran,
				v, cmd, f, origin, model_flag, incr_flag);
}

extern "C" void
bsg_knobs_rot(bsg_view *v, const vect_t rvec,
	      char origin, char coords,
	      const matp_t obj_rot, const pointp_t pvt_pt)
{
    bv_knobs_rot(v, rvec, origin, coords, obj_rot, pvt_pt);
}

extern "C" void
bsg_knobs_tran(bsg_view *v, const vect_t tvec, int model_flag)
{
    bv_knobs_tran(v, tvec, model_flag);
}

extern "C" void
bsg_view_update_rate_flags(bsg_view *v)
{
    bv_update_rate_flags(v);
}

/* ====================================================================== *
 * Scene (view-set) management                                             *
 * ====================================================================== */

extern "C" void
bsg_scene_init(bsg_scene *s)
{
    bv_set_init(s);
}

extern "C" void
bsg_scene_free(bsg_scene *s)
{
    bv_set_free(s);
}

extern "C" void
bsg_scene_add_view(bsg_scene *s, bsg_view *v)
{
    bv_set_add_view(s, v);
}

extern "C" void
bsg_scene_rm_view(bsg_scene *s, bsg_view *v)
{
    bv_set_rm_view(s, v);
}

extern "C" struct bu_ptbl *
bsg_scene_views(bsg_scene *s)
{
    return bv_set_views(s);
}

extern "C" bsg_view *
bsg_scene_find_view(bsg_scene *s, const char *name)
{
    return bv_set_find_view(s, name);
}

/* ====================================================================== *
 * Logging / debug                                                         *
 * ====================================================================== */

extern "C" void
bsg_log(int level, const char *fmt, ...)
{
    /* Forward to the legacy bv_log variadic implementation.
     * We cannot use a va_list bridge without a matching bv_vlog() partner,
     * so we delegate via a two-step call through a local va_list. */
    va_list ap;
    va_start(ap, fmt);
    /* bv_log is a variadic function; the only portable way to forward to it
     * is to call it with an explicit argument list.  Since we cannot do that
     * generically, we build a small formatted string first. */
    struct bu_vls msg = BU_VLS_INIT_ZERO;
    bu_vls_vprintf(&msg, fmt, ap);
    va_end(ap);
    bv_log(level, "%s", bu_vls_cstr(&msg));
    bu_vls_free(&msg);
}

extern "C" void
bsg_view_print(const char *title, bsg_view *v, int verbosity)
{
    bv_view_print(title, v, verbosity);
}

/* ====================================================================== *
 * LoD — context                                                           *
 * ====================================================================== */

extern "C" bsg_mesh_lod_context *
bsg_mesh_lod_context_create(const char *name)
{
    return bv_mesh_lod_context_create(name);
}

extern "C" void
bsg_mesh_lod_context_destroy(bsg_mesh_lod_context *c)
{
    bv_mesh_lod_context_destroy(c);
}

extern "C" void
bsg_mesh_lod_clear_cache(bsg_mesh_lod_context *c,
 unsigned long long key)
{
    bv_mesh_lod_clear_cache(c, key);
}

/* ====================================================================== *
 * LoD — key management                                                    *
 * ====================================================================== */

extern "C" unsigned long long
bsg_mesh_lod_cache(bsg_mesh_lod_context *c,
   const point_t *v, size_t vcnt, const vect_t *vn,
   int *f, size_t fcnt, unsigned long long user_key,
   fastf_t fratio)
{
    return bv_mesh_lod_cache(c, v, vcnt, vn, f, fcnt, user_key, fratio);
}

extern "C" unsigned long long
bsg_mesh_lod_key_get(bsg_mesh_lod_context *c, const char *name)
{
    return bv_mesh_lod_key_get(c, name);
}

extern "C" int
bsg_mesh_lod_key_put(bsg_mesh_lod_context *c, const char *name,
     unsigned long long key)
{
    return bv_mesh_lod_key_put(c, name, key);
}

/* ====================================================================== *
 * LoD — object lifecycle and level selection                              *
 * ====================================================================== */

extern "C" bsg_lod *
bsg_mesh_lod_create(bsg_mesh_lod_context *c, unsigned long long key)
{
    return bv_mesh_lod_create(c, key);
}

extern "C" void
bsg_mesh_lod_destroy(bsg_lod *lod)
{
    bv_mesh_lod_destroy(lod);
}

extern "C" int
bsg_mesh_lod_view(bsg_shape *s, bsg_view *v, int reset)
{
    return bv_mesh_lod_view(bso_to_bv(s), v, reset);
}

extern "C" int
bsg_mesh_lod_level(bsg_shape *s, int level, int reset)
{
    return bv_mesh_lod_level(bso_to_bv(s), level, reset);
}

extern "C" void
bsg_mesh_lod_memshrink(bsg_shape *s)
{
    bv_mesh_lod_memshrink(bso_to_bv(s));
}

extern "C" void
bsg_mesh_lod_free(bsg_shape *s)
{
    bv_mesh_lod_free(bso_to_bv(s));
}

/* ====================================================================== *
 * LoD — detail callbacks                                                  *
 * ====================================================================== */

extern "C" void
bsg_mesh_lod_detail_setup_clbk(bsg_lod *lod,
int (*clbk)(bsg_lod *, void *),
void *cb_data)
{
    bv_mesh_lod_detail_setup_clbk(lod, clbk, cb_data);
}

extern "C" void
bsg_mesh_lod_detail_clear_clbk(bsg_lod *lod,
				int (*clbk)(bsg_lod *, void *))
{
    bv_mesh_lod_detail_clear_clbk(lod, clbk);
}

extern "C" void
bsg_mesh_lod_detail_free_clbk(bsg_lod *lod,
			       int (*clbk)(bsg_lod *, void *))
{
    bv_mesh_lod_detail_free_clbk(lod, clbk);
}

/* ====================================================================== *
 * View bounds / selection                                                 *
 * ====================================================================== */

extern "C" void
bsg_view_bounds(bsg_view *v)
{
    bv_view_bounds(v);
}

extern "C" int
bsg_view_shapes_select(struct bu_ptbl *result, bsg_view *v,
		       int x, int y)
{
    return bv_view_objs_select(result, v, x, y);
}

extern "C" int
bsg_view_shapes_rect_select(struct bu_ptbl *result, bsg_view *v,
			    int x1, int y1, int x2, int y2)
{
    return bv_view_objs_rect_select(result, v, x1, y1, x2, y2);
}

/* ====================================================================== *
 * Polygon creation / update                                               *
 * ====================================================================== */

extern "C" bsg_shape *
bsg_create_polygon_obj(bsg_view *v, int flags, struct bv_polygon *p)
{
    return bv_to_bso(bv_create_polygon_obj(v, flags, p));
}

extern "C" bsg_shape *
bsg_create_polygon(bsg_view *v, int flags, int type, point_t *fp)
{
    return bv_to_bso(bv_create_polygon(v, flags, type, fp));
}

extern "C" int
bsg_update_polygon(bsg_shape *s, bsg_view *v, int utype)
{
    return bv_update_polygon(bso_to_bv(s), v, utype);
}

extern "C" void
bsg_polygon_vlist(bsg_shape *s)
{
    bv_polygon_vlist(bso_to_bv(s));
}

/* ====================================================================== *
 * Polygon selection / movement                                            *
 * ====================================================================== */

extern "C" bsg_shape *
bsg_select_polygon(struct bu_ptbl *objs, point_t *cp)
{
    return bv_to_bso(bv_select_polygon(objs, cp));
}

extern "C" int
bsg_move_polygon(bsg_shape *s, point_t *cp, point_t *pp)
{
    return bv_move_polygon(bso_to_bv(s), cp, pp);
}

extern "C" bsg_shape *
bsg_dup_view_polygon(const char *nname, bsg_shape *s)
{
    return bv_to_bso(bv_dup_view_polygon(nname, bso_to_bv(s)));
}

/* ====================================================================== *
 * Polygon data helpers                                                    *
 * ====================================================================== */

extern "C" void
bsg_polygon_cpy(struct bv_polygon *dest, struct bv_polygon *src)
{
    bv_polygon_cpy(dest, src);
}

extern "C" int
bsg_polygon_calc_fdelta(struct bv_polygon *p)
{
    return bv_polygon_calc_fdelta(p);
}

extern "C" struct bg_polygon *
bsg_polygon_fill_segments(struct bg_polygon *poly, plane_t *vp,
			   vect2d_t line_slope, fastf_t line_spacing)
{
    return bv_polygon_fill_segments(poly, vp, line_slope, line_spacing);
}

/* ====================================================================== *
 * Polygon Boolean operations                                              *
 * ====================================================================== */

extern "C" int
bsg_polygon_csg(bsg_shape *target, bsg_shape *stencil, bg_clip_t op)
{
    return bv_polygon_csg(bso_to_bv(target), bso_to_bv(stencil), op);
}

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
    bv_obj_settings_sync(&state->material, &defaults);
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

    /* Visit this node; prune subtree on non-zero return. */
    int prune = visit(root, state, user_data);

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

    state->depth--;

    /* Restore full state if this was a separator. */
    if (is_sep)
	*state = saved;
    else if (root->s_type_flags & BSG_NODE_CAMERA)
	state->active_camera = prev_camera;
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

    s->i = reinterpret_cast<bv_scene_obj_internal *>(new bsg_shape_internal);

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
    bv_obj_settings_sync(&s->s_local_os, &defaults);
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
	delete reinterpret_cast<bsg_shape_internal *>(s->i);
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
 * cam->aet using the same convention as bv_mat_aet().
 *
 * Internally we round-trip through a temporary bsg_view so that the
 * well-tested bv_mat_aet() code path handles all the matrix algebra.
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
    bv_mat_aet(&tmp);
    bsg_view_get_camera(&tmp, cam);
    bv_free(&tmp);
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

struct bv_scene_obj *
bsg_scene_fsos(bsg_scene *s)
{
    return bv_set_fsos(s);
}

void
bsg_view_center_linesnap(bsg_view *v)
{
    bv_view_center_linesnap(v);
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
