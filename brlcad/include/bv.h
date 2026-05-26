/*                        B V . H
 * BRL-CAD
 *
 * Copyright (c) 1993-2026 United States Government as represented by
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
/* @file bv.h
 * @brief Compatibility bridge - use bsg.h instead */

#ifndef BV_H
#define BV_H

#include "bsg/defines.h"
#include "bsg/adc.h"
#include "bsg/lod.h"
#include "bsg/polygon.h"
#include "bsg/snap.h"
#include "bsg/util.h"
#include "bsg/vlist.h"
#include "bsg/view_sets.h"

/* Compat function aliases: bv_xxx → bsg_xxx for transitional consumers */
#define bv_adjust bsg_adjust
#define bv_autoview bsg_autoview
#define bv_ck_vlist bsg_ck_vlist
#define bv_clear bsg_clear
#define bv_create_polygon bsg_create_polygon
#define bv_create_polygon_obj bsg_create_polygon_obj
#define bv_differ bsg_differ
#define bv_dup_view_polygon bsg_dup_view_polygon
#define bv_find_child bsg_find_child
#define bv_find_obj bsg_find_obj
#define bv_free bsg_free
#define bv_hash bsg_hash
#define bv_illum_obj bsg_illum_obj
#define bv_init bsg_init
#define bv_knobs_cmd_process bsg_knobs_cmd_process
#define bv_knobs_hash bsg_knobs_hash
#define bv_knobs_reset bsg_knobs_reset
#define bv_knobs_rot bsg_knobs_rot
#define bv_knobs_tran bsg_knobs_tran
#define bv_log bsg_log
#define bv_mat_aet bsg_mat_aet
#define bv_move_polygon bsg_move_polygon
#define bv_name bsg_name
#define bv_obj_create bsg_obj_create
#define bv_obj_get bsg_obj_get
#define bv_obj_get_child bsg_obj_get_child
#define bv_obj_get_unregistered bsg_obj_get_unregistered
#define bv_obj_put bsg_obj_put
#define bv_obj_reset bsg_obj_reset
#define bv_obj_settings_sync bsg_obj_settings_sync
#define bv_obj_stale bsg_obj_stale
#define bv_obj_sync bsg_obj_sync
#define bv_plot_vlblock bsg_plot_vlblock
#define bv_polygon_calc_fdelta bsg_polygon_calc_fdelta
#define bv_polygon_cpy bsg_polygon_cpy
#define bv_polygon_csg bsg_polygon_csg
#define bv_polygon_vlist bsg_polygon_vlist
#define bv_scene_obj_bound bsg_scene_obj_bound
#define bv_scene_obj_invalidate_backend bsg_scene_obj_invalidate_backend
#define bv_scene_obj_release_backend bsg_scene_obj_release_backend
#define bv_screen_pt bsg_screen_pt
#define bv_screen_to_view bsg_screen_to_view
#define bv_select_polygon bsg_select_polygon
#define bv_settings_init bsg_settings_init
#define bv_sync bsg_sync
#define bv_uniq_obj_name bsg_uniq_obj_name
#define bv_update bsg_update
#define bv_update_polygon bsg_update_polygon
#define bv_update_rate_flags bsg_update_rate_flags
#define bv_update_selected bsg_update_selected
#define bv_vZ_calc bsg_vZ_calc
#define bv_view_center_linesnap bsg_view_center_linesnap
#define bv_view_get_aet bsg_view_get_aet
#define bv_view_get_center_vec bsg_view_get_center_vec
#define bv_view_get_perspective bsg_view_get_perspective
#define bv_view_get_rotation bsg_view_get_rotation
#define bv_view_get_scale bsg_view_get_scale
#define bv_view_get_size bsg_view_get_size
#define bv_view_independent_scope bsg_view_independent_scope
#define bv_view_independent_scope_destroy bsg_view_independent_scope_destroy
#define bv_view_is_independent bsg_view_is_independent
#define bv_view_knobs bsg_view_knobs
#define bv_view_obj_arrow_create bsg_view_obj_arrow_create
#define bv_view_obj_axes_create bsg_view_obj_axes_create
#define bv_view_obj_create bsg_view_obj_create
#define bv_view_obj_find bsg_view_obj_find
#define bv_view_obj_label_create bsg_view_obj_label_create
#define bv_view_obj_labels_sync bsg_view_obj_labels_sync
#define bv_view_obj_lines_create bsg_view_obj_lines_create
#define bv_view_obj_opts bsg_view_obj_opts
#define bv_view_obj_overlay_create bsg_view_obj_overlay_create
#define bv_view_obj_polygon_create bsg_view_obj_polygon_create
#define bv_view_obj_remove bsg_view_obj_remove
#define bv_view_obj_remove_all bsg_view_obj_remove_all
#define bv_view_obj_set_color bsg_view_obj_set_color
#define bv_view_obj_set_line_width bsg_view_obj_set_line_width
#define bv_view_obj_set_visible bsg_view_obj_set_visible
#define bv_view_obj_visit bsg_view_obj_visit
#define bv_view_objs bsg_view_objs
#define bv_view_objs_visit_db bsg_view_objs_visit_db
#define bv_view_plane bsg_view_plane
#define bv_view_print bsg_view_print
#define bv_view_select_polygon bsg_view_select_polygon
#define bv_view_set_aet bsg_view_set_aet
#define bv_view_set_center_vec bsg_view_set_center_vec
#define bv_view_set_perspective bsg_view_set_perspective
#define bv_view_set_rotation bsg_view_set_rotation
#define bv_view_set_scale bsg_view_set_scale
#define bv_view_set_size bsg_view_set_size
#define bv_vlblock_find bsg_vlblock_find
#define bv_vlblock_free bsg_vlblock_free
#define bv_vlblock_init bsg_vlblock_init
#define bv_vlblock_obj bsg_vlblock_obj
#define bv_vlblock_to_objs bsg_vlblock_to_objs
#define bv_vlist_bbox bsg_vlist_bbox
#define bv_vlist_cleanup bsg_vlist_cleanup
#define bv_vlist_cmd_cnt bsg_vlist_cmd_cnt
#define bv_vlist_copy bsg_vlist_copy
#define bv_vlist_export bsg_vlist_export
#define bv_vlist_get_cmd_description bsg_vlist_get_cmd_description
#define bv_vlist_import bsg_vlist_import
#define bv_vlist_rpp bsg_vlist_rpp
#define bv_vlist_to_uplot bsg_vlist_to_uplot

#endif /* BV_H */
