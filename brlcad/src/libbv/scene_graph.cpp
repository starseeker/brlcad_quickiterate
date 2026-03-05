/*                  S C E N E _ G R A P H . C P P
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
/** @file libbv/scene_graph.cpp
 *
 * @brief Phase-1 BSG API implementation.
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
 */

#include "common.h"

/* bsg/defines.h pulls in bv/defines.h, bv/util.h, bv/lod.h, bv/view_sets.h */
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/lod.h"

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
    bv_autoview(v, scale, all_view_objs);
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
}

extern "C" int
bsg_view_update_selected(bsg_view *v)
{
    return bv_update_selected(v);
}

/* ====================================================================== *
 * View comparison / hashing                                               *
 * ====================================================================== */

extern "C" int
bsg_view_differ(bsg_view *v1, bsg_view *v2)
{
    return bv_differ(v1, v2);
}

extern "C" unsigned long long
bsg_view_hash(bsg_view *v)
{
    return bv_hash(v);
}

extern "C" unsigned long long
bsg_dl_hash(struct display_list *dl)
{
    return bv_dl_hash(dl);
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
    return bv_clear(v, flags);
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
    return bv_to_bso(bv_obj_get(v, type));
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
    bv_obj_put(bso_to_bv(s));
}

extern "C" void
bsg_shape_stale(bsg_shape *s)
{
    bv_obj_stale(bso_to_bv(s));
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

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
