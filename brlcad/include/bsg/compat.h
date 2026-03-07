/*                   B S G / C O M P A T . H
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
/**
 * @file bsg/compat.h
 *
 * @brief Backwards-compatibility aliases: legacy @c bv_* function names
 *        → new @c bsg_* function names.
 *
 * In Phase 1 the @c bsg_* struct types are typedef aliases of the legacy
 * @c bv_* struct types (see @c bsg/defines.h), so struct-level
 * compatibility is automatic.  This header provides macro aliases for the
 * function names so that call sites can be migrated one at a time.
 *
 * Usage:
 *   - New code uses @c bsg_* names only; do NOT include this header in
 *     new code.
 *   - Code being migrated can temporarily add
 *     @c #include @c <bsg/compat.h> so that the old @c bv_* call sites
 *     continue to compile while new sites are updated to @c bsg_*.
 *
 * @warning These aliases are **deprecated** and will be removed once all
 *          call sites in the BRL-CAD tree have been migrated to @c bsg_*.
 */

#ifndef BSG_COMPAT_H
#define BSG_COMPAT_H

#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/lod.h"

/* ---------------------------------------------------------------------- *
 * Function aliases — view lifecycle                                       *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_view_init() instead. */
#define bv_init(v, s)                  bsg_view_init((v), (s))
/** @deprecated Use bsg_view_free() instead. */
#define bv_free(v)                     bsg_view_free((v))
/** @deprecated Use bsg_view_settings_init() instead. */
#define bv_settings_init(s)            bsg_view_settings_init((s))
/** @deprecated Use bsg_view_mat_aet() instead. */
#define bv_mat_aet(v)                  bsg_view_mat_aet((v))

/* ---------------------------------------------------------------------- *
 * Function aliases — view manipulation                                    *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_view_autoview() instead. */
#define bv_autoview(v, sc, ao)         bsg_view_autoview((v), (sc), (ao))
/** @deprecated Use bsg_view_sync() instead. */
#define bv_sync(d, s)                  bsg_view_sync((d), (s))
/** @deprecated Use bsg_material_sync() instead. */
#define bv_obj_settings_sync(d, s)     bsg_material_sync((d), (s))
/** @deprecated Use bsg_view_update() instead. */
#define bv_update(v)                   bsg_view_update((v))
/** @deprecated Use bsg_view_update_selected() instead. */
#define bv_update_selected(v)          bsg_view_update_selected((v))

/* ---------------------------------------------------------------------- *
 * Function aliases — view comparison / hashing                           *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_view_differ() instead. */
#define bv_differ(v1, v2)              bsg_view_differ((v1),(v2))
/** @deprecated Use bsg_view_hash() instead. */
#define bv_hash(v)                     bsg_view_hash((v))
/** @deprecated Use bsg_dl_hash() instead. */
#define bv_dl_hash(dl)                 bsg_dl_hash((dl))

/* ---------------------------------------------------------------------- *
 * Function aliases — view clearing / interaction                          *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_view_clear() instead. */
#define bv_clear(v, f)                 bsg_view_clear((v),(f))
/** @deprecated Use bsg_view_adjust() instead. */
#define bv_adjust(v, dx, dy, kp, m, f) \
    bsg_view_adjust((v),(dx),(dy),(kp),(m),(f))
/** @deprecated Use bsg_screen_to_view() instead. */
#define bv_screen_to_view(v, fx, fy, x, y) \
    bsg_screen_to_view((v),(fx),(fy),(x),(y))
/** @deprecated Use bsg_screen_pt() instead. */
#define bv_screen_pt(p, x, y, v)       bsg_screen_pt((p),(x),(y),(v))

/* ---------------------------------------------------------------------- *
 * Function aliases — knob manipulation                                    *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_knobs_reset() instead. */
#define bv_knobs_reset(k, c)           bsg_knobs_reset((k),(c))
/** @deprecated Use bsg_knobs_hash() instead. */
#define bv_knobs_hash(k, st)           bsg_knobs_hash((k),(st))
/** @deprecated Use bsg_knobs_cmd_process() instead. */
#define bv_knobs_cmd_process(rv, dr, tv, dt, v, cmd, f, o, m, i) \
    bsg_knobs_cmd_process((rv),(dr),(tv),(dt),(v),(cmd),(f),(o),(m),(i))
/** @deprecated Use bsg_knobs_rot() instead. */
#define bv_knobs_rot(v, rv, o, c, kr, pp) \
    bsg_knobs_rot((v),(rv),(o),(c),(kr),(pp))
/** @deprecated Use bsg_knobs_tran() instead. */
#define bv_knobs_tran(v, tv, m)        bsg_knobs_tran((v),(tv),(m))
/** @deprecated Use bsg_view_update_rate_flags() instead. */
#define bv_update_rate_flags(v)        bsg_view_update_rate_flags((v))

/* ---------------------------------------------------------------------- *
 * Function aliases — shape lifecycle                                      *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_shape_create() instead. */
#define bv_obj_create(v, t)            bsg_shape_create((v),(t))
/** @deprecated Use bsg_shape_get() instead. */
#define bv_obj_get(v, t)               bsg_shape_get((v),(t))
/** @deprecated Use bsg_shape_get_child() instead. */
#define bv_obj_get_child(s)            bsg_shape_get_child((s))
/** @deprecated Use bsg_shape_reset() instead. */
#define bv_obj_reset(s)                bsg_shape_reset((s))
/** @deprecated Use bsg_shape_put() instead. */
#define bv_obj_put(s)                  bsg_shape_put((s))
/** @deprecated Use bsg_shape_stale() instead. */
#define bv_obj_stale(s)                bsg_shape_stale((s))
/** @deprecated Use bsg_shape_sync() instead. */
#define bv_obj_sync(d, s)              bsg_shape_sync((d),(s))

/* ---------------------------------------------------------------------- *
 * Function aliases — shape lookup                                         *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_shape_find_child() instead. */
#define bv_find_child(s, n)            bsg_shape_find_child((s),(n))
/** @deprecated Use bsg_view_find_shape() instead. */
#define bv_find_obj(v, n)              bsg_view_find_shape((v),(n))
/** @deprecated Use bsg_view_uniq_name() instead. */
#define bv_uniq_obj_name(r, seed, v)   bsg_view_uniq_name((r),(seed),(v))

/* ---------------------------------------------------------------------- *
 * Function aliases — shape view-specific objects                          *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_shape_for_view() instead. */
#define bv_obj_for_view(s, v)          bsg_shape_for_view((s),(v))
/** @deprecated Use bsg_shape_get_view_obj() instead. */
#define bv_obj_get_vo(s, v)            bsg_shape_get_view_obj((s),(v))
/** @deprecated Use bsg_shape_have_view_obj() instead. */
#define bv_obj_have_vo(s, v)           bsg_shape_have_view_obj((s),(v))
/** @deprecated Use bsg_shape_clear_view_obj() instead. */
#define bv_clear_view_obj(s, v)        bsg_shape_clear_view_obj((s),(v))

/* ---------------------------------------------------------------------- *
 * Function aliases — shape geometry / bounding / illumination            *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_shape_bound() instead. */
#define bv_scene_obj_bound(s, v)       bsg_shape_bound((s),(v))
/** @deprecated Use bsg_shape_vZ_calc() instead. */
#define bv_vZ_calc(s, v, m)            bsg_shape_vZ_calc((s),(v),(m))
/** @deprecated Use bsg_shape_illum() instead. */
#define bv_illum_obj(s, st)            bsg_shape_illum((s),(st))

/* ---------------------------------------------------------------------- *
 * Function aliases — view container / geometry                            *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_view_shapes() instead. */
#define bv_view_objs(v, t)             bsg_view_shapes((v),(t))
/** @deprecated Use bsg_view_plane() instead. */
#define bv_view_plane(p, v)            bsg_view_plane((p),(v))
/** @deprecated Use bsg_log() instead. */
#define bv_log                         bsg_log
/** @deprecated Use bsg_view_print() instead. */
#define bv_view_print(t, v, vb)        bsg_view_print((t),(v),(vb))

/* ---------------------------------------------------------------------- *
 * Function aliases — scene (view set) management                         *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_scene_init() instead. */
#define bv_set_init(s)                 bsg_scene_init((s))
/** @deprecated Use bsg_scene_free() instead. */
#define bv_set_free(s)                 bsg_scene_free((s))
/** @deprecated Use bsg_scene_add_view() instead. */
#define bv_set_add_view(s, v)          bsg_scene_add_view((s),(v))
/** @deprecated Use bsg_scene_rm_view() instead. */
#define bv_set_rm_view(s, v)           bsg_scene_rm_view((s),(v))
/** @deprecated Use bsg_scene_views() instead. */
#define bv_set_views(s)                bsg_scene_views((s))
/** @deprecated Use bsg_scene_find_view() instead. */
#define bv_set_find_view(s, n)         bsg_scene_find_view((s),(n))

/* ---------------------------------------------------------------------- *
 * Function aliases — LoD                                                  *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_mesh_lod_context_create() instead. */
#define bv_mesh_lod_context_create(n)  bsg_mesh_lod_context_create((n))
/** @deprecated Use bsg_mesh_lod_context_destroy() instead. */
#define bv_mesh_lod_context_destroy(c) bsg_mesh_lod_context_destroy((c))
/** @deprecated Use bsg_mesh_lod_clear_cache() instead. */
#define bv_mesh_lod_clear_cache(c, k)  bsg_mesh_lod_clear_cache((c),(k))
/** @deprecated Use bsg_mesh_lod_cache() instead. */
#define bv_mesh_lod_cache(c, v, vc, vn, f, fc, uk, fr) \
    bsg_mesh_lod_cache((c),(v),(vc),(vn),(f),(fc),(uk),(fr))
/** @deprecated Use bsg_mesh_lod_key_get() instead. */
#define bv_mesh_lod_key_get(c, n)      bsg_mesh_lod_key_get((c),(n))
/** @deprecated Use bsg_mesh_lod_key_put() instead. */
#define bv_mesh_lod_key_put(c, n, k)   bsg_mesh_lod_key_put((c),(n),(k))
/** @deprecated Use bsg_mesh_lod_create() instead. */
#define bv_mesh_lod_create(c, k)       bsg_mesh_lod_create((c),(k))
/** @deprecated Use bsg_mesh_lod_destroy() instead. */
#define bv_mesh_lod_destroy(l)         bsg_mesh_lod_destroy((l))
/** @deprecated Use bsg_mesh_lod_memshrink() instead. */
#define bv_mesh_lod_memshrink(s)       bsg_mesh_lod_memshrink((s))
/** @deprecated Use bsg_mesh_lod_view() instead. */
#define bv_mesh_lod_view(s, v, r)      bsg_mesh_lod_view((s),(v),(r))
/** @deprecated Use bsg_mesh_lod_level() instead. */
#define bv_mesh_lod_level(s, l, r)     bsg_mesh_lod_level((s),(l),(r))
/** @deprecated Use bsg_mesh_lod_free() instead. */
#define bv_mesh_lod_free(s)            bsg_mesh_lod_free((s))
/** @deprecated Use bsg_mesh_lod_detail_setup_clbk() instead. */
#define bv_mesh_lod_detail_setup_clbk(l, c, d) \
    bsg_mesh_lod_detail_setup_clbk((l),(c),(d))
/** @deprecated Use bsg_mesh_lod_detail_clear_clbk() instead. */
#define bv_mesh_lod_detail_clear_clbk(l, c) \
    bsg_mesh_lod_detail_clear_clbk((l),(c))
/** @deprecated Use bsg_mesh_lod_detail_free_clbk() instead. */
#define bv_mesh_lod_detail_free_clbk(l, c) \
    bsg_mesh_lod_detail_free_clbk((l),(c))
/** @deprecated Use bsg_view_bounds() instead. */
#define bv_view_bounds(v)              bsg_view_bounds((v))
/** @deprecated Use bsg_view_shapes_select() instead. */
#define bv_view_objs_select(r, v, x, y) \
    bsg_view_shapes_select((r),(v),(x),(y))
/** @deprecated Use bsg_view_shapes_rect_select() instead. */
#define bv_view_objs_rect_select(r, v, x1, y1, x2, y2) \
    bsg_view_shapes_rect_select((r),(v),(x1),(y1),(x2),(y2))
/** @deprecated Use bsg_scene_fsos() instead. */
#define bv_set_fsos(s)                 bsg_scene_fsos((s))
/** @deprecated Use bsg_view_center_linesnap() instead. */
#define bv_view_center_linesnap(v)     bsg_view_center_linesnap((v))

/* ---------------------------------------------------------------------- *
 * Function aliases — polygon update                                       *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_update_polygon() instead. */
#define bv_update_polygon(s, v, u)     bsg_update_polygon((s),(v),(u))

/* ---------------------------------------------------------------------- *
 * Function aliases — snap                                                 *
 * ---------------------------------------------------------------------- */
/** @deprecated Use bsg_snap_lines_2d() instead. */
#define bv_snap_lines_2d(v, x, y)     bsg_snap_lines_2d((v),(x),(y))
/** @deprecated Use bsg_snap_grid_2d() instead. */
#define bv_snap_grid_2d(v, x, y)      bsg_snap_grid_2d((v),(x),(y))
/** @deprecated Use bsg_snap_lines_3d() instead. */
#define bv_snap_lines_3d(o, v, p)     bsg_snap_lines_3d((o),(v),(p))

#endif /* BSG_COMPAT_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
