/*                  B S G / D E F I N E S . H
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
/** @addtogroup bsg_defines
 *
 * @brief
 * BRL-CAD Scene Graph (BSG) — Phase 1 type definitions.
 *
 * This header introduces the @c bsg_* type aliases designed to map more
 * directly onto the Open Inventor / Obol node hierarchy than the legacy
 * @c bv_* API does.
 *
 * ### Migration phases
 *
 * **Phase 1** (this work) — naming migration, zero behavioural change:
 *   - All @c bsg_* types are typedef aliases of the corresponding @c bv_*
 *     types.  This ensures binary compatibility during the transition.
 *   - All @c bsg_* functions are thin wrappers around their @c bv_*
 *     counterparts.
 *   - New code should use @c bsg_* names exclusively; the @c bv_* names
 *     remain in @c <bv/defines.h> as deprecated compatibility aliases.
 *   - @c bsg_camera is a genuinely new struct (extracted from @c bview
 *     for documentation purposes) but is not yet embedded in @c bsg_view.
 *
 * **Phase 2** — structural alignment with Open Inventor:
 *   - @c bsg_view will have @c bsg_camera embedded as a sub-struct.
 *   - Property inheritance (material, transform) will be modelled as
 *     proper scene-graph state propagation.
 *   - The @c bv_* types will become thin wrappers around @c bsg_* types,
 *     then deprecated and eventually removed.
 *
 * **Phase 3** — LoD caching improvements:
 *   - The @c brlcad_viewdbi LoD rework (per-view LoD sets managed by the
 *     application rather than libbv) will be integrated.
 *
 * ### Type mapping table
 *
 * | BSG type (new)              | Replaces (legacy)       | Obol/Inventor analog         |
 * |-----------------------------|-------------------------|------------------------------|
 * | bsg_material                | bsg_obj_settings         | SoMaterial + SoDrawStyle     |
 * | bsg_shape                   | bsg_shape            | SoShape (leaf geometry node) |
 * | bsg_group                   | bv_scene_group          | SoSeparator (group node)     |
 * | bsg_lod                     | bsg_mesh_lod             | SoLOD                        |
 * | bsg_mesh_lod_context        | bsg_mesh_lod_context     | (LoD cache context)          |
 * | bsg_camera                  | (inline fields in bview)| SoCamera                     |
 * | bsg_view_settings           | bview_settings          | (render/snap settings)       |
 * | bsg_view_objects            | bview_objs              | SoSeparator children list    |
 * | bsg_knobs                   | bview_knobs             | (dial/knob input state)      |
 * | bsg_view                    | bview                   | SoCamera + view state        |
 * | bsg_scene                   | bview_set               | root SoSeparator + cameras   |
 */

#ifndef BSG_DEFINES_H
#define BSG_DEFINES_H

#include "common.h"
#include <stdio.h>
#include "vmath.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bu/vls.h"
#include "bu/magic.h"
#include "bu/malloc.h"
#include "bu/color.h"
#include "bu/hash.h"
#include "bg/polygon.h"
#include "bg/polygon_types.h"

/** @{ */
/** @file bsg/defines.h */

/* The BSG API is implemented in libbsg.  Use BSG_DLL_EXPORTS when building
 * libbsg itself and BSG_DLL_IMPORTS when consuming it from other libraries.
 * If neither is defined we fall back to the old BV_EXPORT behaviour so that
 * in-tree builds that have not yet migrated to the new libbsg target still
 * link correctly. */
#ifndef BSG_EXPORT
#  ifdef BSG_DLL_EXPORTS
#    define BSG_EXPORT COMPILER_DLLEXPORT
#  elif defined(BSG_DLL_IMPORTS)
#    define BSG_EXPORT COMPILER_DLLIMPORT
#  else
#    define BSG_EXPORT
#  endif
#endif


__BEGIN_DECLS

/** @brief Magic number for @c bsg_view (= @c bview) instances. */
#define BSG_VIEW_MAGIC BV_MAGIC

/* ================================================================== *
 * Content inlined from bv/faceplate.h                                *
 * ================================================================== */
#ifndef DM_BV_FACEPLATE_H
#define DM_BV_FACEPLATE_H

struct bsg_adc_state {
    int         draw;
    int         dv_x;
    int         dv_y;
    int         dv_a1;
    int         dv_a2;
    int         dv_dist;
    fastf_t     pos_model[3];
    fastf_t     pos_view[3];
    fastf_t     pos_grid[3];
    fastf_t     a1;
    fastf_t     a2;
    fastf_t     dst;
    int         anchor_pos;
    int         anchor_a1;
    int         anchor_a2;
    int         anchor_dst;
    fastf_t     anchor_pt_a1[3];
    fastf_t     anchor_pt_a2[3];
    fastf_t     anchor_pt_dst[3];
    int         line_color[3];
    int         tick_color[3];
    int         line_width;
};

struct bsg_grid_state {
    int       rc;
    int       draw;
    int       adaptive;
    int       snap;
    fastf_t   anchor[3];
    fastf_t   res_h;
    fastf_t   res_v;
    int       res_major_h;
    int       res_major_v;
    int       color[3];
};

struct bsg_interactive_rect_state {
    int        active;
    int        draw;
    int        line_width;
    int        line_style;
    int        pos[2];
    int        dim[2];
    fastf_t    x;
    fastf_t    y;
    fastf_t    width;
    fastf_t    height;
    int        bg[3];
    int        color[3];
    int        cdim[2];
    fastf_t    aspect;
};

struct bsg_params_state {
    int draw;
    int draw_size;
    int draw_center;
    int draw_az;
    int draw_el;
    int draw_tw;
    int draw_fps;
    int color[3];
    int font_size;
};

struct bsg_other_state {
    int gos_draw;
    int gos_line_color[3];
    int gos_text_color[3];
    int gos_font_size;
};

#endif /* DM_BV_FACEPLATE_H */

/* ================================================================== *
 * Content inlined from bv/tcl_data.h                                 *
 * ================================================================== */
#ifndef DM_BV_TCL_DATA_H
#define DM_BV_TCL_DATA_H

#define BSG_POLY_CIRCLE_MODE 15
#define BSG_POLY_CONTOUR_MODE 16

struct bsg_scene_obj_old_settings {
    char s_wflag;
    char s_dflag;
    unsigned char s_basecolor[3];
    char s_uflag;
    char s_cflag;
    char s_Eflag;
    short s_regionid;
};

struct display_list {
    struct bu_list      l;
    void               *dl_dp;
    struct bu_vls       dl_path;
    int                 dl_wflag;
};

struct bsg_data_axes_state {
    int       draw;
    int       color[3];
    int       line_width;
    fastf_t   size;
    int       num_points;
    point_t   *points;
};

struct bsg_data_arrow_state {
    int       gdas_draw;
    int       gdas_color[3];
    int       gdas_line_width;
    int       gdas_tip_length;
    int       gdas_tip_width;
    int       gdas_num_points;
    point_t   *gdas_points;
};

struct bsg_data_label_state {
    int         gdls_draw;
    int         gdls_color[3];
    int         gdls_num_labels;
    int         gdls_size;
    char        **gdls_labels;
    point_t     *gdls_points;
};

struct bsg_data_line_state {
    int       gdls_draw;
    int       gdls_color[3];
    int       gdls_line_width;
    int       gdls_num_points;
    point_t   *gdls_points;
};

typedef struct {
    int                 gdps_draw;
    int                 gdps_moveAll;
    int                 gdps_color[3];
    int                 gdps_line_width;
    int                 gdps_line_style;
    int                 gdps_cflag;
    size_t              gdps_target_polygon_i;
    size_t              gdps_curr_polygon_i;
    size_t              gdps_curr_point_i;
    point_t             gdps_prev_point;
    bg_clip_t           gdps_clip_type;
    fastf_t             gdps_scale;
    point_t             gdps_origin;
    mat_t               gdps_rotation;
    mat_t               gdps_view2model;
    mat_t               gdps_model2view;
    struct bg_polygons  gdps_polygons;
    fastf_t             gdps_data_vZ;
} bsg_data_polygon_state;

struct bsg_data_tclcad {
    int                         gv_polygon_mode;
    int                         gv_hide;
    fastf_t                     gv_data_vZ;
    struct bsg_data_arrow_state  gv_data_arrows;
    struct bsg_data_axes_state   gv_data_axes;
    struct bsg_data_label_state  gv_data_labels;
    struct bsg_data_line_state   gv_data_lines;
    bsg_data_polygon_state       gv_data_polygons;
    struct bsg_data_arrow_state  gv_sdata_arrows;
    struct bsg_data_axes_state   gv_sdata_axes;
    struct bsg_data_label_state  gv_sdata_labels;
    struct bsg_data_line_state   gv_sdata_lines;
    bsg_data_polygon_state       gv_sdata_polygons;
    struct bsg_other_state       gv_prim_labels;
};

#endif /* DM_BV_TCL_DATA_H */

/* ================================================================== *
 * Content inlined from bv/defines.h                                  *
 * ================================================================== */

#define BSG_VIEW_MAX 2047.0
#define BSG_VIEW_MIN -2048.0
#define BSG_VIEW_RANGE 4095.0
#define INV_BV 0.00048828125
#define INV_4096 0.000244140625

#define BSG_MINVIEWSIZE 0.0001
#define BSG_MINVIEWSCALE 0.00005

#ifndef UP
#  define UP 0
#endif
#ifndef DOWN
#  define DOWN 1
#endif

#define BSG_ANCHOR_AUTO          0
#define BSG_ANCHOR_BOTTOM_LEFT   1
#define BSG_ANCHOR_BOTTOM_CENTER 2
#define BSG_ANCHOR_BOTTOM_RIGHT  3
#define BSG_ANCHOR_MIDDLE_LEFT   4
#define BSG_ANCHOR_MIDDLE_CENTER 5
#define BSG_ANCHOR_MIDDLE_RIGHT  6
#define BSG_ANCHOR_TOP_LEFT      7
#define BSG_ANCHOR_TOP_CENTER    8
#define BSG_ANCHOR_TOP_RIGHT     9

struct bsg_label {
    int           size;
    struct bu_vls label;
    point_t       p;
    int           line_flag;
    point_t       target;
    int           anchor;
    int           arrow;
};

struct bsg_axes {
    int       draw;
    point_t   axes_pos;
    fastf_t   axes_size;
    int       line_width;
    int       axes_color[3];
    int       pos_only;
    int       label_flag;
    int       label_color[3];
    int       triple_color;
    int       tick_enabled;
    int       tick_length;
    int       tick_major_length;
    fastf_t   tick_interval;
    int       ticks_per_major;
    int       tick_threshold;
    int       tick_color[3];
    int       tick_major_color[3];
};

struct bsg_obj_settings {
    int s_dmode;
    int mixed_modes;
    fastf_t transparency;
    int color_override;
    unsigned char color[3];
    int s_line_width;
    fastf_t s_arrow_tip_length;
    fastf_t s_arrow_tip_width;
    int draw_solid_lines_only;
    int draw_non_subtract_only;
};
#define BSG_OBJ_SETTINGS_INIT {0, 0, 1.0, 0, {255, 0, 0}, 1, 0.0, 0.0, 0, 0}

#define BSG_NODE_DBOBJ_BASED    0x01
#define BSG_NODE_VIEWONLY       0x02
#define BSG_NODE_LINES          0x04
#define BSG_NODE_LABELS         0x08
#define BSG_NODE_AXES           0x10
#define BSG_NODE_POLYGONS       0x20
#define BSG_NODE_MESH_LOD       0x40
#define BSG_NODE_CSG_LOD        0x80

struct bview;

#define BSG_DB_OBJS    0x01
#define BSG_VIEW_OBJS  0x02
#define BSG_LOCAL_OBJS 0x04
#define BSG_CHILD_OBJS 0x08

struct bsg_scene_obj_internal;
struct bsg_shape_internal;

struct bsg_shape  {
    struct bu_list l;
    struct bsg_shape_internal *i;
    unsigned long long s_type_flags;
    struct bu_vls s_name;
    void *s_path;
    void *dp;
    mat_t s_mat;
    struct bview *s_v;
    void *s_i_data;
    int (*s_update_callback)(struct bsg_shape *, struct bview *, int);
    void (*s_free_callback)(struct bsg_shape *);
    struct bu_list s_vlist;
    size_t s_vlen;
    unsigned int s_dlist;
    int s_dlist_mode;
    int s_dlist_stale;
    void (*s_dlist_free_callback)(struct bsg_shape *);
    fastf_t s_size;
    fastf_t s_csize;
    vect_t s_center;
    int s_displayobj;
    point_t bmin;
    point_t bmax;
    int have_bbox;
    char s_flag;
    char s_iflag;
    int s_force_draw;
    unsigned char s_color[3];
    int s_soldash;
    int s_arrow;
    int s_changed;
    int current;
    int     adaptive_wireframe;
    int     csg_obj;
    int     mesh_obj;
    fastf_t view_scale;
    size_t  bot_threshold;
    fastf_t curve_scale;
    fastf_t point_scale;
    struct bsg_obj_settings *s_os;
    struct bsg_obj_settings s_local_os;
    int s_inherit_settings;
    struct bsg_scene_obj_old_settings s_old;
    struct bu_ptbl children;
    struct bsg_scene_ob *parent;
    struct bu_list *vlfree;
    struct bsg_shape *free_scene_obj;
    struct bu_ptbl *otbl;
    void *draw_data;
    void *s_u_data;
};
typedef struct bsg_shape bsg_shape;



struct bsg_mesh_lod {
    int fcnt;
    const int *faces;
    int pcnt;
    const point_t *points;
    int porig_cnt;
    const point_t *points_orig;
    const vect_t *normals;
    point_t bmin;
    point_t bmax;
    bsg_shape *s;
    void *c;
    void *i;
};

struct bsg_mesh_lod_context_internal;
struct bsg_mesh_lod_context {
    struct bsg_mesh_lod_context_internal *i;
};

#define BSG_SNAP_SHARED 0x1
#define BSG_SNAP_LOCAL  0x2
#define BSG_SNAP_DB     0x4
#define BSG_SNAP_VIEW   0x8
#define BSG_SNAP_TCL    0x10

struct bview_settings {
    int            gv_snap_lines;
    double         gv_snap_tol_factor;
    struct bu_ptbl gv_snap_objs;
    int            gv_snap_flags;
    int            gv_cleared;
    int            gv_zclip;
    int            gv_autoview;
    int           adaptive_plot_mesh;
    int           adaptive_plot_csg;
    size_t        bot_threshold;
    fastf_t       curve_scale;
    fastf_t       point_scale;
    int           redraw_on_zoom;
    fastf_t       lod_scale;
    struct bsg_axes           gv_model_axes;
    struct bsg_axes           gv_view_axes;
    struct bsg_grid_state     gv_grid;
    struct bsg_other_state    gv_center_dot;
    struct bsg_params_state   gv_view_params;
    struct bsg_other_state    gv_view_scale;
    double                   gv_frametime;
    int                      gv_fb_mode;
    struct bsg_adc_state              gv_adc;
    struct bsg_interactive_rect_state gv_rect;
    struct bu_ptbl                  *gv_selected;
};

struct bview_objs {
    struct bu_ptbl  *db_objs;
    struct bu_ptbl  *view_objs;
    struct bu_list  gv_vlfree;
    struct bsg_shape *free_scene_obj;
};

struct bview_knobs {
    vect_t      rot_m;
    int         rot_m_flag;
    char        origin_m;
    void       *rot_m_udata;
    vect_t      rot_o;
    int         rot_o_flag;
    char        origin_o;
    void       *rot_o_udata;
    vect_t      rot_v;
    int         rot_v_flag;
    char        origin_v;
    void       *rot_v_udata;
    fastf_t     sca;
    int         sca_flag;
    void       *sca_udata;
    vect_t      tra_m;
    int         tra_m_flag;
    void       *tra_m_udata;
    vect_t      tra_v;
    int         tra_v_flag;
    void       *tra_v_udata;
    vect_t      rot_m_abs;
    vect_t      rot_m_abs_last;
    vect_t      rot_o_abs;
    vect_t      rot_o_abs_last;
    vect_t      rot_v_abs;
    vect_t      rot_v_abs_last;
    fastf_t     sca_abs;
    vect_t      tra_m_abs;
    vect_t      tra_m_abs_last;
    vect_t      tra_v_abs;
    vect_t      tra_v_abs_last;
};

struct bview_set;

struct bview {
    uint32_t      magic;
    struct bu_vls gv_name;
    fastf_t       gv_i_scale;
    fastf_t       gv_a_scale;
    fastf_t       gv_scale;
    fastf_t       gv_size;
    fastf_t       gv_isize;
    fastf_t       gv_base2local;
    fastf_t       gv_local2base;
    fastf_t       gv_rscale;
    fastf_t       gv_sscale;
    int           gv_width;
    int           gv_height;
    point2d_t     gv_wmin;
    point2d_t     gv_wmax;
    fastf_t       gv_perspective;
    vect_t        gv_aet;
    vect_t        gv_eye_pos;
    vect_t        gv_keypoint;
    char          gv_coord;
    char          gv_rotate_about;
    mat_t         gv_rotation;
    mat_t         gv_center;
    mat_t         gv_model2view;
    mat_t         gv_pmodel2view;
    mat_t         gv_view2model;
    mat_t         gv_pmat;
    fastf_t       gv_prevMouseX;
    fastf_t       gv_prevMouseY;
    int           gv_mouse_x;
    int           gv_mouse_y;
    point_t       gv_prev_point;
    point_t       gv_point;
    char          gv_key;
    unsigned long gv_mod_flags;
    fastf_t       gv_minMouseDelta;
    fastf_t       gv_maxMouseDelta;
    struct bview_settings *gv_s;
    struct bview_settings  gv_ls;
    int independent;
    struct bview_set *vset;
    struct bview_objs gv_objs;
    point_t obb_center;
    vect_t obb_extent1;
    vect_t obb_extent2;
    vect_t obb_extent3;
    void (*gv_bounds_update)(struct bview *);
    point_t gv_vc_backout;
    vect_t gv_lookat;
    double radius;
    struct bview_knobs k;
    point_t     orig_pos;
    struct bsg_data_tclcad gv_tcl;
    void          (*gv_callback)(struct bview *, void *);
    void           *gv_clientData;
    struct bu_ptbl *callbacks;
    void           *dmp;
    void           *u_data;
};

struct bview_set_internal;
struct bview_set {
    struct bview_set_internal   *i;
    struct bview_settings       settings;
};


/* ================================================================== *
 * Content inlined from bv/vlist.h                                    *
 * ================================================================== */

#define BSG_VLIST_CHUNK 35

struct bsg_vlist  {
    struct bu_list l;
    size_t nused;
    int cmd[BSG_VLIST_CHUNK];
    point_t pt[BSG_VLIST_CHUNK];
};
#define BSG_VLIST_NULL	((struct bsg_vlist *)0)
#define BSG_CK_VLIST(_p) BU_CKMAG((_p), BSG_VLIST_MAGIC, "bsg_vlist")

#define BSG_VLIST_LINE_MOVE		0
#define BSG_VLIST_LINE_DRAW		1
#define BSG_VLIST_POLY_START		2
#define BSG_VLIST_POLY_MOVE		3
#define BSG_VLIST_POLY_DRAW		4
#define BSG_VLIST_POLY_END		5
#define BSG_VLIST_POLY_VERTNORM		6
#define BSG_VLIST_TRI_START		7
#define BSG_VLIST_TRI_MOVE		8
#define BSG_VLIST_TRI_DRAW		9
#define BSG_VLIST_TRI_END		10
#define BSG_VLIST_TRI_VERTNORM		11
#define BSG_VLIST_POINT_DRAW		12
#define BSG_VLIST_POINT_SIZE		13
#define BSG_VLIST_LINE_WIDTH		14
#define BSG_VLIST_DISPLAY_MAT		15
#define BSG_VLIST_MODEL_MAT		16
#define BSG_VLIST_CMD_MAX		16

#define BSG_GET_VLIST(_free_hd, p) do {\
	(p) = BU_LIST_FIRST(bsg_vlist, (_free_hd)); \
	if (BU_LIST_IS_HEAD((p), (_free_hd))) { \
	    BU_ALLOC((p), struct bsg_vlist); \
	    (p)->l.magic = BSG_VLIST_MAGIC; \
	} else { \
	    BU_LIST_DEQUEUE(&((p)->l)); \
	} \
	(p)->nused = 0; \
    } while (0)

#define BSG_FREE_VLIST(_free_hd, hd) do { \
	BU_CK_LIST_HEAD((hd)); \
	BU_LIST_APPEND_LIST((_free_hd), (hd)); \
    } while (0)

#define BSG_ADD_VLIST(_free_hd, _dest_hd, pnt, draw) do { \
	struct bsg_vlist *_vp; \
	BU_CK_LIST_HEAD(_dest_hd); \
	_vp = BU_LIST_LAST(bsg_vlist, (_dest_hd)); \
	if (BU_LIST_IS_HEAD(_vp, (_dest_hd)) || _vp->nused >= BSG_VLIST_CHUNK) { \
	    BSG_GET_VLIST(_free_hd, _vp); \
	    BU_LIST_INSERT((_dest_hd), &(_vp->l)); \
	} \
	VMOVE(_vp->pt[_vp->nused], (pnt)); \
	_vp->cmd[_vp->nused++] = (draw); \
    } while (0)

#define BSG_VLIST_SET_DISP_MAT(_free_hd, _dest_hd, _ref_pt) do { \
	struct bsg_vlist *_vp; \
	BU_CK_LIST_HEAD(_dest_hd); \
	_vp = BU_LIST_LAST(bsg_vlist, (_dest_hd)); \
	if (BU_LIST_IS_HEAD(_vp, (_dest_hd)) || _vp->nused >= BSG_VLIST_CHUNK) { \
	    BSG_GET_VLIST(_free_hd, _vp); \
	    BU_LIST_INSERT((_dest_hd), &(_vp->l)); \
	} \
	VMOVE(_vp->pt[_vp->nused], (_ref_pt)); \
	_vp->cmd[_vp->nused++] = BSG_VLIST_DISPLAY_MAT; \
    } while (0)

#define BSG_VLIST_SET_MODEL_MAT(_free_hd, _dest_hd) do { \
	struct bsg_vlist *_vp; \
	BU_CK_LIST_HEAD(_dest_hd); \
	_vp = BU_LIST_LAST(bsg_vlist, (_dest_hd)); \
	if (BU_LIST_IS_HEAD(_vp, (_dest_hd)) || _vp->nused >= BSG_VLIST_CHUNK) { \
	    BSG_GET_VLIST(_free_hd, _vp); \
	    BU_LIST_INSERT((_dest_hd), &(_vp->l)); \
	} \
	_vp->cmd[_vp->nused++] = BSG_VLIST_MODEL_MAT; \
    } while (0)

#define BSG_VLIST_SET_POINT_SIZE(_free_hd, _dest_hd, _size) do { \
	struct bsg_vlist *_vp; \
	BU_CK_LIST_HEAD(_dest_hd); \
	_vp = BU_LIST_LAST(bsg_vlist, (_dest_hd)); \
	if (BU_LIST_IS_HEAD(_vp, (_dest_hd)) || _vp->nused >= BSG_VLIST_CHUNK) { \
	    BSG_GET_VLIST(_free_hd, _vp); \
	    BU_LIST_INSERT((_dest_hd), &(_vp->l)); \
	} \
	_vp->pt[_vp->nused][0] = (_size); \
	_vp->cmd[_vp->nused++] = BSG_VLIST_POINT_SIZE; \
    } while (0)

#define BSG_VLIST_SET_LINE_WIDTH(_free_hd, _dest_hd, _width) do { \
	struct bsg_vlist *_vp; \
	BU_CK_LIST_HEAD(_dest_hd); \
	_vp = BU_LIST_LAST(bsg_vlist, (_dest_hd)); \
	if (BU_LIST_IS_HEAD(_vp, (_dest_hd)) || _vp->nused >= BSG_VLIST_CHUNK) { \
	    BSG_GET_VLIST(_free_hd, _vp); \
	    BU_LIST_INSERT((_dest_hd), &(_vp->l)); \
	} \
	_vp->pt[_vp->nused][0] = (_width); \
	_vp->cmd[_vp->nused++] = BSG_VLIST_LINE_WIDTH; \
    } while (0)

struct bsg_vlblock {
    uint32_t magic;
    size_t nused;
    size_t max;
    long *rgb;
    struct bu_list *head;
    struct bu_list *free_vlist_hd;
};
#define BSG_CK_VLBLOCK(_p)	BU_CKMAG((_p), BSG_VLBLOCK_MAGIC, "bsg_vlblock")


/* ================================================================== *
 * struct bsg_polygon (inlined from bv/polygon.h)                      *
 * ================================================================== */

#define BSG_POLYGON_GENERAL    0
#define BSG_POLYGON_CIRCLE     1
#define BSG_POLYGON_ELLIPSE    2
#define BSG_POLYGON_RECTANGLE  3
#define BSG_POLYGON_SQUARE     4

struct bsg_polygon {
    int                 type;
    int                 fill_flag;
    vect2d_t            fill_dir;
    fastf_t             fill_delta;
    struct bu_color     fill_color;
    long                curr_contour_i;
    long                curr_point_i;
    point_t             origin_point;
    plane_t             vp;
    fastf_t             vZ;
    struct bg_polygon   polygon;
    void               *u_data;
};


#define BSG_POLYGON_UPDATE_DEFAULT         0
#define BSG_POLYGON_UPDATE_PROPS_ONLY      1
#define BSG_POLYGON_UPDATE_PT_SELECT       2
#define BSG_POLYGON_UPDATE_PT_SELECT_CLEAR 3
#define BSG_POLYGON_UPDATE_PT_MOVE         4
#define BSG_POLYGON_UPDATE_PT_APPEND       5



/* ====================================================================== *
 * BSG type definitions                                                    *
 *                                                                         *
 * The bsg_* names are the canonical types.  Self-typedefs below expose   *
 * them as plain (untagged) C identifiers for convenience.                *
 * ====================================================================== */

/**
 * @brief Appearance / rendering properties for a scene node.
 *
 * Phase 1: alias of @c bsg_obj_settings.
 * Phase 2: will gain proper Inventor-style property inheritance.
 *
 * Analogous to @c SoMaterial + @c SoDrawStyle in Open Inventor.
 */
typedef struct bsg_obj_settings bsg_material;

/**
 * @brief Leaf geometry node in the BSG scene graph.
 *
 * Independent struct with the same initial field layout as @c bsg_shape.
 * Having two separate definitions allows the @c bsg_* API to evolve its
 * field layout independently without affecting external libbv users.
 * During Phase 1 the layouts are kept in sync; Phase 2 will diverge them
 * as the BSG API matures.
 *
 * Analogous to @c SoShape in Open Inventor.
 */
/* struct bsg_shape is defined inline above */

/**
 * @brief Group node — aggregates child @c bsg_shape nodes.
 *
 * Phase 1: typedef alias of @c bsg_shape (same in-memory layout).
 * Phase 2: gains proper state isolation (SoSeparator semantics) via
 * the @c BSG_NODE_SEPARATOR type-flag on the underlying @c bsg_shape.
 *
 * Analogous to @c SoSeparator in Open Inventor.
 */
typedef bsg_shape bsg_group;

/**
 * @brief Mesh Level-of-Detail payload exposed to renderers.
 *
 * Phase 1: alias of @c bsg_mesh_lod.
 * Phase 2: will be integrated with the brlcad_viewdbi per-view LoD sets.
 *
 * Analogous to @c SoLOD in Open Inventor.
 */
typedef struct bsg_mesh_lod bsg_lod;

/**
 * @brief Per-database LoD bookkeeping context.
 *
 * Phase 1: alias of @c bsg_mesh_lod_context.
 */
typedef struct bsg_mesh_lod_context bsg_mesh_lod_context;

/**
 * @brief Rendering and interaction settings shareable across views.
 *
 * Phase 1: alias of @c bview_settings.
 * Phase 2: individual settings will migrate into proper scene-graph nodes.
 */
typedef struct bview_settings bsg_view_settings;

/**
 * @brief Per-view scene object containers.
 *
 * Phase 1: alias of @c bview_objs.
 */
typedef struct bview_objs bsg_view_objects;

/**
 * @brief Interactive knob / dial-box view manipulation state.
 *
 * Phase 1: alias of @c bview_knobs.
 */
typedef struct bview_knobs bsg_knobs;

/**
 * @brief A single rendered view into the scene.
 *
 * Phase 1: alias of @c bview.
 * Phase 2: will embed @c bsg_camera as a sub-struct.
 *
 * Analogous to the combination of @c SoCamera + @c SbViewportRegion in
 * Open Inventor.
 */
typedef struct bview bsg_view;

/**
 * @brief Top-level multi-view scene container.
 *
 * Phase 1: alias of @c bview_set.
 * Phase 2: will become the scene-graph root analogous to @c SoSeparator.
 */
typedef struct bview_set bsg_scene;

/**
 * @brief Floating label overlay attached to a scene node.
 *
 * Phase 1: alias of @c bsg_label.
 */
typedef struct bsg_label bsg_label;

/**
 * @brief Axes overlay display state (model-space or view-space).
 *
 * Phase 1: alias of @c bsg_axes.
 */
typedef struct bsg_axes bsg_axes;

/**
 * @brief Vector-list primitive (single-linked list of 3-D polyline segments).
 *
 * Phase 1: alias of @c bsg_vlist.
 */
typedef struct bsg_vlist bsg_vlist;

/**
 * @brief Named-colour vector-list container (for coloured wireframes).
 *
 * Phase 1: alias of @c bsg_vlblock.
 */
typedef struct bsg_vlblock bsg_vlblock;

/**
 * @brief Polygon overlay state (used by sketch/polygon tools).
 *
 * Phase 1: alias of @c bsg_polygon.
 */
typedef struct bsg_polygon bsg_polygon;

/* ----- faceplate overlay sub-states ----- */

/**
 * @brief Angular-deviation cursor (ADC) overlay state.
 *
 * Phase 1: alias of @c bsg_adc_state.
 */
typedef struct bsg_adc_state bsg_adc_state;

/**
 * @brief Construction-grid overlay state.
 *
 * Phase 1: alias of @c bsg_grid_state.
 */
typedef struct bsg_grid_state bsg_grid_state;

/**
 * @brief Rubber-band selection rectangle overlay state.
 *
 * Phase 1: alias of @c bsg_interactive_rect_state.
 */
typedef struct bsg_interactive_rect_state bsg_interactive_rect_state;

/**
 * @brief View parameter display overlay state.
 *
 * Phase 1: alias of @c bsg_params_state.
 */
typedef struct bsg_params_state bsg_params_state;

/**
 * @brief Single-bit overlay toggle (centre-dot, view-scale, etc.).
 *
 * Phase 1: alias of @c bsg_other_state.
 */
typedef struct bsg_other_state bsg_other_state;

/* ----- Tcl/legacy interactive data overlays ----- */

/**
 * @brief Tcl-driven data axes overlay state.
 *
 * Phase 1: alias of @c bsg_data_axes_state.
 */
typedef struct bsg_data_axes_state bsg_data_axes_state;

/**
 * @brief Tcl-driven arrow overlay state.
 *
 * Phase 1: alias of @c bsg_data_arrow_state.
 */
typedef struct bsg_data_arrow_state bsg_data_arrow_state;

/**
 * @brief Tcl-driven text label overlay state.
 *
 * Phase 1: alias of @c bsg_data_label_state.
 */
typedef struct bsg_data_label_state bsg_data_label_state;

/**
 * @brief Tcl-driven line overlay state.
 *
 * Phase 1: alias of @c bsg_data_line_state.
 */
typedef struct bsg_data_line_state bsg_data_line_state;

/**
 * @brief Container for all Tcl-driven interactive overlay state.
 *
 * Phase 1: alias of @c bsg_data_tclcad.
 */
typedef struct bsg_data_tclcad bsg_data_tclcad;

/* ====================================================================== *
 * bsg_camera — new struct (Phase 2 design target)                        *
 *                                                                         *
 * In Open Inventor, SoCamera (and its concrete subclasses               *
 * SoPerspectiveCamera / SoOrthographicCamera) encapsulate the viewpoint, *
 * orientation, and projection.  This struct collects the camera-specific  *
 * fields that are currently embedded directly in @c bview into a single  *
 * dedicated type.                                                         *
 *                                                                         *
 * Phase 1 status: this struct is defined for documentation and design    *
 * purposes; it is NOT yet embedded in @c bsg_view (which is still @c     *
 * bview in Phase 1).  Phase 2 will replace the inline camera fields in   *
 * @c bview with this struct.                                              *
 * ====================================================================== */

/**
 * @brief Camera (viewpoint) data — Phase 2 design target.
 *
 * Extracts the camera-specific fields from @c bview into a dedicated type
 * analogous to @c SoCamera in Open Inventor.
 *
 * @note In Phase 1 this struct is not yet integrated into @c bsg_view.
 *       Use the legacy field names via the @c bsg_view (= @c bview) alias
 *       in the meantime.  When Phase 2 integrates this struct, migration
 *       will be mechanical:
 *
 *       Old (Phase 1):   v->gv_perspective
 *       New (Phase 2):   v->camera.perspective
 */
struct bsg_camera {
    fastf_t perspective;      /**< @brief perspective angle (0 → orthographic)    */
    vect_t  aet;              /**< @brief azimuth / elevation / twist             */
    vect_t  eye_pos;          /**< @brief eye position in model space              */
    vect_t  keypoint;         /**< @brief rotation keypoint                        */
    char    coord;            /**< @brief active coordinate system                 */
    char    rotate_about;     /**< @brief rotation pivot specifier                 */
    mat_t   rotation;         /**< @brief view rotation matrix                     */
    mat_t   center;           /**< @brief view centre matrix                       */
    mat_t   model2view;       /**< @brief model-to-view transform                  */
    mat_t   pmodel2view;      /**< @brief perspective model-to-view transform      */
    mat_t   view2model;       /**< @brief view-to-model transform                  */
    mat_t   pmat;             /**< @brief perspective matrix                       */
};

/* ====================================================================== *
 * Canonical BSG_* constants                                               *
 *                                                                         *
 * These constants were migrated from the legacy BV_* macros.             *
 * BSG_* names are the primary definitions; BV_* names are gone.         *
 * ====================================================================== */

/* Node type flags */

/**
 * @brief Phase 2 node-type flag: Separator group (SoSeparator semantics).
 *
 * When set on a @c bsg_shape, the node acts as a scope boundary during
 * scene-graph traversal: the traversal state (accumulated transform and
 * material) is saved on entry and restored on exit.  Children of the node
 * may freely modify state without affecting siblings or ancestors.
 *
 * Analogous to @c SoSeparator in Open Inventor.
 */
#define BSG_NODE_SEPARATOR    0x100

/**
 * @brief Phase 2 node-type flag: Transform node (SoTransform semantics).
 *
 * When set on a @c bsg_shape, the node's primary contribution to the scene
 * is its @c s_mat transform matrix; it carries no renderable geometry of its
 * own.  During traversal the matrix is accumulated into the traversal state.
 *
 * Analogous to @c SoTransform in Open Inventor.
 */
#define BSG_NODE_TRANSFORM    0x200

/**
 * @brief Phase 2 node-type flag: Camera node (SoCamera semantics).
 *
 * When set on a @c bsg_shape, the node acts as a camera definition inside
 * the scene graph.  Its @c s_i_data points to a heap-allocated
 * @c bsg_camera struct (freed via @c s_free_callback).
 *
 * During scene-graph traversal (@c bsg_traverse / @c bsg_view_traverse)
 * the traversal engine copies the camera pointer into
 * @c bsg_traversal_state::active_camera so that descendant render callbacks
 * can read the active projection.
 *
 * The camera node integrates naturally with @c bsg_view:
 *   - @c bsg_camera_node_set() syncs a @c bsg_camera into the node.
 *   - @c bsg_camera_node_get() reads it back.
 *   - @c bsg_view_set_camera() writes a @c bsg_camera back into a
 *     @c bsg_view (the legacy view struct) so that bv_update and existing
 *     render code see the updated camera.
 *
 * Analogous to @c SoPerspectiveCamera / @c SoOrthographicCamera in Open
 * Inventor.
 */
#define BSG_NODE_CAMERA       0x400

/**
 * @brief Phase 2 node-type flag: LoD group node (SoLOD semantics).
 *
 * When set on a @c bsg_shape, the node acts as a level-of-detail selector
 * that picks exactly one of its children for rendering based on the
 * eye-to-center distance.  Its @c s_i_data points to a heap-allocated
 * @c bsg_lod_switch_data struct (freed via @c s_free_callback).
 *
 * Children must be added in highest-to-lowest detail order (child[0] is
 * the highest-detail representation, the last child is the coarsest).
 * The switch distances array has (num_levels - 1) entries: the n-th entry
 * is the distance at which the renderer transitions from child[n] to
 * child[n+1].
 *
 * During traversal @c bsg_traverse computes the eye-to-node-center distance
 * and visits only the selected child.  The user-facing LoD feature is fully
 * preserved; existing @c bsg_lod (BSG_NODE_MESH_LOD) nodes remain supported
 * on leaf shapes.
 *
 * Analogous to @c SoLOD in Open Inventor.
 */
#define BSG_NODE_LOD_GROUP    0x800

/* Container-selection flags */

/* Anchor position constants */

/* Material initialiser */
#define BSG_MATERIAL_INIT  BSG_OBJ_SETTINGS_INIT

/* Interaction mode flags */
#define BSG_IDLE      0x000
#define BSG_ROT       0x001
#define BSG_TRANS     0x002
#define BSG_SCALE     0x004
#define BSG_CENTER    0x008
#define BSG_CON_X     0x010
#define BSG_CON_Y     0x020
#define BSG_CON_Z     0x040
#define BSG_CON_GRID  0x080
#define BSG_CON_LINES 0x100

/* Knob reset categories */
#define BSG_KNOBS_ALL  0
#define BSG_KNOBS_RATE 1
#define BSG_KNOBS_ABS  2

/* Autoview scale sentinel */
#define BSG_AUTOVIEW_SCALE_DEFAULT -1

/* View range constants */

/* ====================================================================== *
 * Phase 2: LoD group switch data                                         *
 * ====================================================================== */

/**
 * @brief Switch-distance table for a @c BSG_NODE_LOD_GROUP node.
 *
 * Stored in @c s_i_data of every LoD group node.  The @c s_free_callback
 * registered by @c bsg_lod_group_alloc() frees this struct and the heap
 * memory it owns.
 *
 * @c num_levels == number of child detail levels (== number of children
 * added to the node).  @c switch_distances has @c (num_levels - 1) entries.
 * Entry @c n is the eye-to-center distance at which the traversal engine
 * switches from child @c n to child @c n+1 (coarser).
 */
struct bsg_lod_switch_data {
    int      num_levels;         /**< @brief total number of detail levels */
    fastf_t *switch_distances;   /**< @brief (num_levels-1) distance thresholds */
};

/* ====================================================================== *
 * Phase 2: sensor / change notification                                  *
 *                                                                         *
 * Analogous to SoDataSensor / SoOneShotSensor in Open Inventor.         *
 * When bsg_shape_stale() is called the engine sets the node's stale flag  *
 * (backward compat) AND fires every sensor registered on that node.      *
 * ====================================================================== */

/**
 * @brief A lightweight change-notification callback attached to a shape.
 *
 * Register with @c bsg_shape_add_sensor(); remove with
 * @c bsg_shape_rm_sensor() using the handle returned by the add call.
 *
 * Analogous to @c SoDataSensor in Open Inventor: the callback fires
 * whenever @c bsg_shape_stale() is invoked on the associated node.
 */
struct bsg_sensor {
    /** @brief Called when the node becomes stale.  May be NULL (no-op). */
    void (*callback)(bsg_shape *s, void *data);
    /** @brief Opaque data forwarded to @c callback. */
    void  *data;
};
/** @brief C convenience typedef. */
typedef struct bsg_sensor bsg_sensor;

/* ====================================================================== *
 * Phase 2: traversal state                                               *
 *                                                                         *
 * Analogous to the SoState object passed through every SoAction during   *
 * Open Inventor traversal.  It accumulates property-node contributions   *
 * (material settings, transform, camera) as the scene graph is walked.  *
 * ====================================================================== */

/**
 * @brief State accumulated during a scene-graph traversal.
 *
 * Callers initialise one of these with @c bsg_traversal_state_init() and
 * pass it to @c bsg_traverse() / @c bsg_view_traverse().  At each node
 * the state reflects the cumulative effect of all ancestor property /
 * transform / camera nodes.
 *
 * State accumulation rules:
 *   - Transform:  @c xform = parent_xform * node->s_mat at every node.
 *   - Material:   updated at every node whose @c s_os != NULL and
 *                 @c s_inherit_settings == 0.
 *   - Camera:     @c active_camera is set to @c node->s_i_data whenever a
 *                 @c BSG_NODE_CAMERA node is encountered.
 *   - Separator:  @c BSG_NODE_SEPARATOR saves the full state on descent and
 *                 restores it on ascent (SoSeparator semantics).
 *
 * The @c view pointer is set by @c bsg_view_traverse(); direct callers of
 * @c bsg_traverse() may leave it NULL if LoD distance calculation is not
 * needed.
 */
struct bsg_traversal_state {
    /** @brief Accumulated model transform (product of all ancestor @c s_mat values). */
    mat_t                   xform;
    /** @brief Accumulated material settings (last property node in traversal order wins). */
    bsg_material            material;
    /** @brief Current traversal depth (root = 0). */
    int                     depth;
    /** @brief Camera set by the last @c BSG_NODE_CAMERA node encountered (NULL = none yet). */
    const struct bsg_camera *active_camera;
    /**
     * @brief View being traversed, for LoD distance calculation.
     *
     * Set automatically by @c bsg_view_traverse().  If NULL, distance-based
     * LoD group child selection falls back to always picking child[0]
     * (highest detail).
     */
    const bsg_view          *view;
};
/** @brief C convenience typedef. */
typedef struct bsg_traversal_state bsg_traversal_state;

/* ====================================================================== *
 * Design decisions for Obol / Open Inventor compatibility                *
 *                                                                         *
 * The following notes document how each structural difference between    *
 * BRL-CAD's scene model and Inventor/Obol has been resolved.            *
 *                                                                         *
 * 1. Camera IS now a scene-graph node (BSG_NODE_CAMERA).                *
 *    bsg_camera_node_alloc() creates a standalone bsg_shape with the     *
 *    BSG_NODE_CAMERA flag.  It is inserted into the scene root ahead of  *
 *    geometry nodes, exactly as SoCamera is in Open Inventor.            *
 *    bsg_traversal_state::active_camera is updated each time a camera    *
 *    node is encountered during traversal.                               *
 *    bsg_view_set_camera() writes a bsg_camera back into a bsg_view so   *
 *    that legacy bv_update() / render code continues to work during the  *
 *    migration.                                                           *
 *                                                                         *
 * 2. Multi-view instancing via per-view scene roots.                     *
 *    Each view now has an optional scene root (bsg_scene_root_set/get).  *
 *    To share geometry between views:                                    *
 *      a) Create the geometry node once.                                 *
 *      b) Add it as a child of multiple view roots.                      *
 *    This is identical to Open Inventor's DAG instancing: the same node  *
 *    pointer appears in multiple parent children tables.  The legacy     *
 *    bsg_shape_for_view() / bsg_shape_get_view_obj() per-view override   *
 *    mechanism is deprecated.  Per-view LoD variation is handled by      *
 *    BSG_NODE_LOD_GROUP nodes (see 5 below).                             *
 *                                                                         *
 * 3. Graph traversal replaces flat-table iteration.                     *
 *    bsg_view_traverse() walks from the per-view scene root through      *
 *    bsg_traverse(), honouring separator state, transforms, camera and   *
 *    LoD selection.  Renderers should migrate from iterating              *
 *    bview_objs.db_objs / view_objs to calling bsg_view_traverse().      *
 *    The legacy flat tables remain populated for existing callers.       *
 *                                                                         *
 * 4. Sensors replace raw stale flags.                                   *
 *    bsg_shape_add_sensor() / bsg_shape_rm_sensor() register change-     *
 *    notification callbacks on any bsg_shape.  bsg_shape_stale() fires   *
 *    all registered sensors in addition to setting the legacy            *
 *    s_dlist_stale flag.  Callers that previously polled the stale flag  *
 *    should migrate to sensors; the stale flag is kept for compatibility.*
 *                                                                         *
 * 5. LoD IS now a group node (BSG_NODE_LOD_GROUP).                      *
 *    bsg_lod_group_alloc() creates a standalone group node whose        *
 *    children are detail-level shapes ordered highest-to-lowest.         *
 *    The traversal engine picks the appropriate child from the           *
 *    bsg_lod_switch_data distances table.  The existing BSG_NODE_MESH_LOD*
 *    payload mechanism on leaf shapes is preserved for cases that need   *
 *    only a single adaptive mesh (not multiple pre-baked LODs).          *
 * ====================================================================== */

__END_DECLS

/** @} */

#endif /* BSG_DEFINES_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
