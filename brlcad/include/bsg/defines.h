/*                      D E F I N E S . H
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
/** @addtogroup libbsg
 *
 * @brief
 * Canonical scene-graph and view type definitions.
 *
 * This header is the authoritative home for all scene-graph and view
 * type definitions previously in bv/defines.h.  bv/defines.h is now a
 * backward-compatibility bridge that will be removed once all callers
 * have migrated to bsg/defines.h.
 */
/** @{ */
/* @file bsg/defines.h */

#ifndef BSG_DEFINES_H
#define BSG_DEFINES_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bu/vls.h"
#include "bg/polygon_types.h"
#include "bsg/tcl_data.h"
#include "bsg/faceplate.h"

__BEGIN_DECLS

#ifndef BSG_EXPORT
#  if defined(BSG_DLL_EXPORTS) && defined(BSG_DLL_IMPORTS)
#    error "Only BSG_DLL_EXPORTS or BSG_DLL_IMPORTS can be defined, not both."
#  elif defined(BSG_DLL_EXPORTS)
#    define BSG_EXPORT COMPILER_DLLEXPORT
#  elif defined(BSG_DLL_IMPORTS)
#    define BSG_EXPORT COMPILER_DLLIMPORT
#  else
#    define BSG_EXPORT
#  endif
#endif

/* Define view ranges.  The numbers -2048 and 2047 go all the way back to the
 * original angle-distance cursor code that predates even BRL-CAD itself, but
 * (at least right now) there doesn't seem to be any documentation of why those
 * specific values were chosen. */
#define BSG_VIEW_MAX 2047.0
#define BSG_VIEW_MIN -2048.0
#define BSG_VIEW_RANGE 4095.0
/* Map +/-2048 BV space into -1.0..+1.0 :: x/2048*/
#define BSG_INV_VIEW 0.00048828125
#define INV_BV BSG_INV_VIEW
#define BSG_INV_4096 0.000244140625
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
    point_t       p;         // 3D base of label text
    int           line_flag; // If 1, draw a line from label anchor to target
    point_t       target;
    int           anchor;    // Either closest candidate to target (AUTO), or fixed
    int           arrow;     // If 1, use an arrow indicating direction from label to target
};


/* Note - this container holds information both for data axes and for the more
 * elaborate visuals associated with the Archer style model axes.  The latter
 * is a superset of the former, so there should be no need for a separate data
 * type. */
struct bsg_axes {
    int       draw;
    point_t   axes_pos;             /* in model coordinates */
    fastf_t   axes_size;            /* in view coordinates for HUD drawing-mode axes */
    int       line_width;           /* in pixels */
    int       axes_color[3];

    /* The following are (currently) only used when drawing
     * the faceplace HUD axes */
    int       pos_only;
    int       label_flag;
    int       label_color[3];
    int       triple_color;
    int       tick_enabled;
    int       tick_length;          /* in pixels */
    int       tick_major_length;    /* in pixels */
    fastf_t   tick_interval;        /* in mm */
    int       ticks_per_major;
    int       tick_threshold;
    int       tick_color[3];
    int       tick_major_color[3];
};

/* Many settings have application level defaults that can be overridden for
 * individual shape nodes.
 *
 * Phase D5 (drawing_modernization) will replace the ad-hoc per-shape
 * s_dmode / color_override / transparency fields with a resolved BSG
 * appearance action computed from source material, command overrides, and
 * selection/edit state during render traversal. */
struct bsg_obj_settings {

    int s_dmode;         	/**< @brief  draw modes (TODO - are these accurate?):
				 *            0 - wireframe
				 *	      1 - shaded bots and polysolids only (booleans NOT evaluated)
				 *	      2 - shaded (booleans NOT evaluated)
				 *	      3 - shaded (booleans evaluated)
				 *	      4 - hidden line
				 */
    int mixed_modes;            /**< @brief  when drawing, don't remove an objects view objects for other modes */
    fastf_t transparency;	/**< @brief  holds a transparency value in the range [0.0, 1.0] - 1 is opaque */

    int color_override;
    unsigned char color[3];	/**< @brief  color to draw as */

    int s_line_width;		/**< @brief  current line width */
    fastf_t s_arrow_tip_length; /**< @brief  arrow tip length */
    fastf_t s_arrow_tip_width;  /**< @brief  arrow tip width */
    int draw_solid_lines_only;   /**< @brief do not use dashed lines for subtraction solids */
    int draw_non_subtract_only;  /**< @brief do not visualize subtraction solids */
};
#define BSG_OBJ_SETTINGS_INIT {0, 0, 1.0, 0, {255, 0, 0}, 1, 0.0, 0.0, 0, 0}


/* Note that it is possible for a view object to be view-only (not
 * corresponding directly to the wireframe of a database shape) but also based
 * off of database data.  Evaluated shaded objects would be an example, as
 * would NIRT solid shotline visualizations or overlap visualizations.  The
 * categorizations for the various types of bsg_node objects would be:
 *
 * solid wireframe/triangles (obj.s):  BSG_SHAPE_DBOBJ
 * rtcheck overlap visual:             BSG_SHAPE_DBOBJ | BSG_SHAPE_VIEWONLY
 * polygon/line/label:                 BSG_SHAPE_VIEWONLY
 *
 * Phase D2 (drawing_modernization): The distinction between database-backed
 * and view-only objects will be superseded by explicit draw-intent metadata on
 * BSG scene groups.  Application-level semantic meaning should not be encoded
 * as per-shape flags; see bsg/draw_intent.h (Phase D2).
 *
 * The distinction between objects (lines, labels, etc.) defined as
 * bsg_node VIEW ONLY objects and the faceplate elements is that objects defined
 * as bsg_node objects DO exist in the 3D scene and will move as 3D elements
 * when the view is manipulated (although label text is drawn parallel to the
 * view plane).  Faceplate elements exist ONLY in the HUD and are not managed
 * as bsg_node objects — they will not move with view manipulation.
 * Phase D4 (drawing_modernization) will migrate faceplate elements to explicit
 * HUD payload nodes.
 */
#define BSG_SHAPE_DBOBJ    0x01  /**< @brief shape drawn from a database path */
#define BSG_SHAPE_VIEWONLY 0x02  /**< @brief view-only / overlay shape */
#define BSG_SHAPE_LINES    0x04  /**< @brief line-segment set shape */
#define BSG_SHAPE_LABELS   0x08  /**< @brief text-label shape */
#define BSG_SHAPE_AXES     0x10  /**< @brief axes-widget shape */
#define BSG_SHAPE_POLYGONS 0x20  /**< @brief polygon region shape */

struct bsg_view;

#define BSG_OBJ_DB    0x01
#define BSG_OBJ_VIEW  0x02
#define BSG_OBJ_LOCAL 0x04
#define BSG_OBJ_CHILD 0x08

struct bsg_node_internal;
struct bsg_node;
struct bsg_payload;  /* Phase D1 (drawing_modernization): typed payload handle — see bsg/payload_typed.h */
struct bsg_draw_intent;  /* Phase D2 (drawing_modernization): draw-intent metadata — see bsg/draw_intent.h */

/* Phase 11 (drawing_stack_modernization): renderer-backend contract.
 *
 * type_tag values for struct bsg_backend.  Backends register their tag at
 * dm registration time; the per-shape s_backend slot carries the matching tag
 * so cross-backend handle confusion can be caught.  More tags will be added as
 * additional backends adopt the contract (e.g. dm-obol). */
#define BSG_BACKEND_NONE 0u   /* no backend state attached */
#define BSG_BACKEND_GL   1u   /* OpenGL/GL-via-software-rasterizer (dm-gl, dm-swrast, dm-qtgl, dm-glx, dm-wgl) */

/**
 * Phase 11 (drawing_stack_modernization): per-shape backend state.
 *
 * Replaces the previous pattern of adding backend-specific fields directly on
 * struct bsg_node.  One bsg_backend describes a single backend's
 * per-shape state; the active scene object stores the descriptor in
 * bsg_node::s_backend.
 *
 * Lifecycle:
 *  - allocated lazily by the backend (typically when it first needs to cache
 *    a GPU resource for the shape);
 *  - released by bsg_scene_obj_release_backend() when the shape is destroyed
 *    or recycled (also called from bsg_obj_reset, bsg_obj_put, and the libbsg
 *    tree free paths);
 *  - invalidated by bsg_scene_obj_invalidate_backend() when the source data
 *    that drives the cached resource has changed (called from
 *    bsg_obj_stale() and any other code that needs to flag the cached
 *    resource as out of date).
 *
 * Backends are expected to provide a free callback; invalidate is optional
 * and may be NULL for backends that have no separately-cacheable resource.
 */
struct bsg_backend {
    uint32_t type_tag;                          /**< @brief BSG_BACKEND_* identifying the owner */
    void *handle;                               /**< @brief backend-private per-shape state */
    void (*free)(struct bsg_node *);        /**< @brief release backend resources and free this descriptor */
    void (*invalidate)(struct bsg_node *);  /**< @brief mark cached resource stale; may be NULL */
};

/* bsg_scene_groups are BSG_NODE_GROUP nodes that record the user's draw-command
 * intent — which database path was drawn and how.  They sit one level above
 * the shape leaves that hold the realized geometry.  For example:
 *
 * ged> draw comb
 *
 * creates a scene group whose s_name is "comb" and whose BSG_NODE_SHAPE
 * children hold the wireframe vlists for all solids under comb/a and comb/b.
 * If comb/b is removed and comb/c added, the group's children are updated to
 * reflect the current state of comb, because the user drew "comb" (not
 * individual instances).
 *
 * By contrast:
 *
 * ged> draw comb/a
 * ged> draw comb/b
 *
 * produces two separate scene groups with s_names "comb/a" and "comb/b".
 * Adding comb/c to comb does NOT auto-draw it; only the explicitly drawn
 * paths are tracked.
 *
 * Phase D2 (drawing_modernization) will replace this ad-hoc group naming
 * convention with explicit bsg_draw_intent metadata so that draw-command
 * semantics are queryable through a stable API rather than inferred from
 * group names and child structure.
 */
#define bsg_scene_group bsg_node


/* The primary "working" data for mesh Level-of-Detail (LoD) drawing is stored
 * in a bsg_mesh_lod container.
 *
 * Most LoD information is deliberately hidden in the internal, but the key
 * data needed for drawing routines and view setup are exposed. Although this
 * data structure is primarily managed in libbg, the public data in this struct
 * is needed at many levels of the software stack, including libbv. */
struct bsg_mesh_lod {

    // The set of triangle faces to be used when drawing
    int fcnt;
    const int *faces;

    // The vertices used by the faces array
    int pcnt;
    const point_t *points;      // If using snapped points, that's this array.  Else, points == points_orig.
    int porig_cnt;
    const point_t *points_orig;

    // Optional: per-face-vertex normals (one normal per triangle vertex - NOT
    // one normal per vertex.  I.e., a given point from points_orig may have
    // multiple normals associated with it in different faces.)
    const vect_t *normals;

    // Bounding box of the original full-detail data
    point_t bmin;
    point_t bmax;

    // The scene object using this LoD structure
    struct bsg_node *s;

    // Pointer to the higher level LoD context associated with this LoD data
    void *c;

    // Pointer to internal LoD implementation information specific to this object
    void *i;
};

/* Flags to identify categories of objects to snap */
#define BSG_SNAP_SHARED 0x1
#define BSG_SNAP_LOCAL  0x2
#define BSG_SNAP_DB     0x4
#define BSG_SNAP_VIEW   0x8
#define BSG_SNAP_TCL    0x10

/* We encapsulate non-camera settings into a container mainly to allow for
 * easier reuse of the same settings between different views - if a common
 * setting set is maintained between different views, this container allows
 * us to just point to the common set from all views using it. */
struct bsg_view_settings {
    int            gv_snap_lines;
    double 	   gv_snap_tol_factor;
    struct bu_ptbl gv_snap_objs;
    int		   gv_snap_flags;
    int            gv_cleared;
    int            gv_zclip;
    int            gv_autoview;

    // Adaptive plotting related settings - these are used when the wireframe
    // generated by primitives is based on the view information.
    int           adaptive_plot_mesh;
    int           adaptive_plot_csg;
    size_t        bot_threshold;
    fastf_t       curve_scale;
    fastf_t       point_scale;
    int           redraw_on_zoom;
    fastf_t 	  lod_scale;

    // Faceplate elements fall into two general categories: those which are
    // interactively adjusted (in a geometric sense) and those which are not.
    // The non-interactive are generally just enabled or disabled:
    struct bsg_axes           gv_model_axes;
    struct bsg_axes           gv_view_axes;
    struct bsg_grid_state     gv_grid;
    struct bsg_other_state    gv_center_dot;
    struct bsg_params_state   gv_view_params;
    struct bsg_other_state    gv_view_scale;
    double                   gv_frametime;

    // Framebuffer visualization is possible if there is an attached dm and
    // that dm has an associated framebuffer.  If those conditions are met,
    // this variable is used to control how the fb is visualized.
    int                      gv_fb_mode; // 0 = off, 1 = overlay, 2 = underlay

    // More complex are the faceplate view elements not corresponding to
    // geometry objects but editable by the user.  These aren't managed as
    // gv_view_objs (they are HUD visuals and thus not part of the scene) so
    // they have some unique requirements.
    struct bsg_adc_state              gv_adc;
    struct bsg_interactive_rect_state gv_rect;


    /* Current view selection set.  Legacy raw-node-table consumers should
     * migrate to bsg_selection_* APIs; the pointer remains here as the
     * canonical per-view selection model storage. */
    struct bsg_selection               *gv_selected;
};

/* A view needs to know what objects are active within it, but this is a
 * function not just of adding and removing objects via commands like
 * "draw" and "erase" but also what settings are active.  Shared objects
 * are common to multiple views, but if adaptive plotting is enabled the
 * scene objects cannot also be common - the representations of the objects
 * may be different in each view, even though the object list is shared.
 */
struct bsg_view_obj_pool {

    // Container for db object groups unique to this view (typical use case is
    // adaptive plotting, where geometry wireframes may differ from view to
    // view and thus need unique vlists.)
    struct bu_ptbl  *db_objs;

    // Available bsg_vlist entities to recycle before allocating new for local
    // view objects. This is used only if the app doesn't supply a vlfree -
    // normally the app should do so, so memory from one view can be reused for
    // other views.
    struct bu_list  gv_vlfree;

    /* Container for reusing bsg_node allocations */
    struct bsg_node *free_scene_obj;
};

// Data for managing "knob" manipulation of views.  One historical hardware
// example of this "knob" concept of view manipulation would be Dial boxes such
// as the Silicon Graphics SN-921, used with 3D workstations in the early days.
// Although we've not heard of Dial boxes being used with BRL-CAD in many
// years, the mathematics of view manipulation used to support them still
// underpins interactions driven with inputs from modern peripherals such as
// the mouse.
struct bsg_view_knobs {

    /* Rate data */
    vect_t      rot_m;      // rotation - model coords
    int         rot_m_flag;
    char        origin_m;
    void	*rot_m_udata;

    vect_t	rot_o;      // rotation - object coords
    int		rot_o_flag;
    char	origin_o;
    void	*rot_o_udata;

    vect_t      rot_v;      // rotation - view coords
    int         rot_v_flag;
    char        origin_v;
    void	*rot_v_udata;

    fastf_t     sca;        // scale
    int         sca_flag;
    void	*sca_udata;

    vect_t      tra_m;      // translation - model coords
    int         tra_m_flag;
    void	*tra_m_udata;

    vect_t      tra_v;      // translation - view coords
    int         tra_v_flag;
    void	*tra_v_udata;

    /* Absolute data */
    vect_t      rot_m_abs;       // rotation - model coords
    vect_t      rot_m_abs_last;

    vect_t      rot_o_abs;       // rotation - object coords
    vect_t      rot_o_abs_last;

    vect_t      rot_v_abs;       // rotation - view coords
    vect_t      rot_v_abs_last;

    fastf_t     sca_abs;

    vect_t      tra_m_abs;       // translation - model coords
    vect_t      tra_m_abs_last;

    vect_t      tra_v_abs;       // translation - view coords
    vect_t      tra_v_abs_last;

};

struct bsg_view_set;
struct bsg_camera;       /* forward decl for Phase D5 camera-node binding */
struct bsg_render_settings; /* forward decl for Phase D5 render settings */

struct bsg_view {
    uint32_t	  magic;             /**< @brief magic number */
    struct bu_vls gv_name;

    /* Size info */
    fastf_t       gv_i_scale;
    fastf_t       gv_a_scale;        /**< @brief absolute scale */
    fastf_t       gv_scale;
    fastf_t       gv_size;           /**< @brief  2.0 * scale */
    fastf_t       gv_isize;          /**< @brief  1.0 / size */
    fastf_t       gv_base2local;
    fastf_t       gv_local2base;
    fastf_t       gv_rscale;
    fastf_t       gv_sscale;

    /* Information about current "window" into view.  This view may not be
     * displayed (that's up to the display managers) and it is up to the
     * calling code to set gv_width and gv_height to the current correct values
     * for such a display, if it is associated with this view.  These
     * definitions are needed in bsg_view to support "view aware" algorithms that
     * require information defining an active pixel "window" into the view. */
    int		  gv_width;
    int		  gv_height;
    point2d_t	  gv_wmin; // view space bbox minimum of gv_width/gv_height window
    point2d_t	  gv_wmax; // view space bbox maximum of gv_width/gv_height window

    /* Camera info */
    fastf_t       gv_perspective;    /**< @brief  perspective angle */
    vect_t        gv_aet;
    vect_t        gv_eye_pos;        /**< @brief  eye position */
    vect_t        gv_keypoint;
    char          gv_coord;          /**< @brief  coordinate system */
    char          gv_rotate_about;   /**< @brief  indicates what point rotations are about */
    mat_t         gv_rotation;
    mat_t         gv_center;
    mat_t         gv_model2view;
    mat_t         gv_pmodel2view;
    mat_t         gv_view2model;
    mat_t         gv_pmat;           /**< @brief  perspective matrix */

    /* Keyboard/mouse info */
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

    /* Settings */
    struct bsg_view_settings *gv_s;     /**< @brief shared settings supplied by user */
    struct bsg_view_settings gv_ls;     /**< @brief locally maintained settings specific to view (used if gv_s is null) */

    /* Set containing this view.  Also holds pointers to resources shared
     * across multiple views */
    struct bsg_view_set *vset;

    /* Scene objects active in a view.  Managing these is a relatively complex
     * topic and depends on whether a view is shared, independent or adaptive.
     * Shared objects are common across views to make more efficient use of
     * system memory. */
    struct bsg_view_obj_pool gv_objs;

    /* We sometimes need to define the volume in space that is "active" for the
     * view.  For an orthogonal camera this is the oriented bounding box
     * extruded to contain active scene objects visible in the view  The app
     * must set the gv_bounds_update callback to bg_view_bound so a bsg_update
     * call can update these values.*/
    point_t obb_center;
    vect_t obb_extent1;
    vect_t obb_extent2;
    vect_t obb_extent3;
    void (*gv_bounds_update)(struct bsg_view *);

    /* "Backed out" point, lookat direction, scene radius. Used for geometric
     * view based interrogation. */
    point_t gv_vc_backout;
    vect_t gv_lookat;
    double radius;

    /* Knob-based view manipulation data */
    struct bsg_view_knobs k;

    /* Virtual trackball position */
    point_t     orig_pos;

    // libtclcad data (optional: NULL for non-Tcl views, allocated and owned by
    // libtclcad tclcad_view_data when a Tcl-backed view is created)
    struct bsg_data_tclcad *gv_tcl;

    /* Callback, external data */
    void          (*gv_callback)(struct bsg_view *, void *);  /**< @brief  called in ged_view_update with gvp and gv_clientData */
    void           *gv_clientData;   /**< @brief  passed to gv_callback */
    struct bu_ptbl *callbacks;
    void           *dmp;             /* Display manager pointer, if one is associated with this view */
    void           *u_data;          /* Caller data associated with this view */

    /* Phase 4 (drawing_stack_modernization): BSG scene-graph root for this
     * view.  Stored as void * to avoid a circular include dependency between
     * bv/defines.h and bsg/defines.h.  Cast to struct bsg_node * (which
     * is typedef'd as bsg_node) before use.  NULL until bsg_scene_root_create
     * is called for this view. */
    void           *bsg_root;

    /* Phase 7 step 7 A3 (drawing_stack_modernization): GED draw-tree root for
     * this view.  Stored as void * to avoid circular headers.  Cast to
     * struct bsg_node * (= bsg_node) before use.  Set by
     * bsg_view_obj_ensure_root() when GED initialises the draw tree.  When
     * non-NULL, bsg_scene_root_sync() uses this tree as the authoritative
     * source of drawn objects (gv_objs is then a derived flat index). */
    void           *gv_draw_root;

    /* Phase 5 (drawing_stack_modernization): per-frame edit-mode matrix
     * override.  When non-NULL, draw_scene_obj() renders objects whose
     * s_iflag == UP with this matrix instead of gv_model2view.  MGED sets
     * this to view_state->vs_model2objview while the frame is being painted
     * and clears it to NULL immediately afterward.  Never heap-allocated;
     * always points into caller-owned storage. */
    matp_t          gv_edit_mat;

    /* Phase 9.2 (drawing_stack_modernization B5 residual): per-frame
     * generation counter.  Bumped at the top of dm_draw_objs() so that
     * shapes painted in the current frame can be identified by
     * s_drawn_rev == gv_frame_rev without any per-frame full-tree
     * "reset s_flag = DOWN" sweep.  Starts at 0; first dm_draw_objs()
     * call observes 1.  Wraps cleanly on uint64_t overflow (would take
     * billions of years at 60 fps, but the comparison still produces
     * the right answer for any pair of consecutive frames). */
    uint64_t        gv_frame_rev;

    /* Phase D4 (drawing_modernization): per-view HUD scene-graph root.
     * Stored as void * to avoid a circular include dependency.  Cast to
     * struct bsg_node * (= bsg_node) before use.  NULL until
     * bsg_hud_root_create() is called.  Contains one child node per
     * faceplate feature; bsg_hud_sync() updates s_flag on each child to
     * reflect the current bsg_view_settings before each render pass. */
    void           *gv_hud_root;

    /* Phase D5 (drawing_modernization): bound camera snapshot for this
     * view.  When non-NULL, bsg_view_apply_camera_node() restores the
     * camera state from this snapshot.  Stored as a borrowed pointer
     * (the view does NOT own the struct bsg_camera); callers must keep
     * the camera alive for the lifetime of the binding.  NULL by default. */
    struct bsg_camera *gv_camera_node;

    /* Phase D5 (drawing_modernization): per-view render settings.
     * When non-NULL, the render traversal reads policy from this struct
     * instead of from gv_s fields directly.  The view does NOT own this
     * pointer; the caller allocates/frees via bsg_render_settings_create/
     * bsg_render_settings_destroy.  NULL means "use view defaults". */
    struct bsg_render_settings *gv_render_settings;
};

// Because bsg_view instances frequently share objects in applications, they are
// not always fully independent - we define a container and some basic
// operations to manage this.
struct bsg_view_set_internal;
struct bsg_view_set {
    struct bsg_view_set_internal   *i;
    struct bsg_view_settings       settings;
};

/* Export macro */
#ifndef BSG_EXPORT
#  if defined(BSG_DLL_EXPORTS) && defined(BSG_DLL_IMPORTS)
#    error "Only BSG_DLL_EXPORTS or BSG_DLL_IMPORTS can be defined, not both."
#  elif defined(BSG_DLL_EXPORTS)
#    define BSG_EXPORT COMPILER_DLLEXPORT
#  elif defined(BSG_DLL_IMPORTS)
#    define BSG_EXPORT COMPILER_DLLIMPORT
#  else
#    define BSG_EXPORT
#  endif
#endif

/**
 * Node type flags used in s_type_flags alongside BV_* flags.
 * Values are chosen to not overlap with the BV_* flags in bv/defines.h.
 */
#define BSG_NODE_ROOT         0x10000000ULL  /**< @brief synthetic scene root */
#define BSG_NODE_GROUP        0x20000000ULL  /**< @brief group / combinator */
#define BSG_NODE_SHAPE        0x40000000ULL  /**< @brief leaf drawable shape */
#define BSG_NODE_LOD          0x80000000ULL  /**< @brief LoD proxy node */
#define BSG_NODE_VLIST       0x100000000ULL  /**< @brief raw vlist node */
#define BSG_NODE_TRANSFORM   0x200000000ULL  /**< @brief transform node */
#define BSG_NODE_SENSOR      0x400000000ULL  /**< @brief sensor / trigger node */
/**
 * Phase V1 (drawing_stack_modernization):
 * A view-scope container node.  Children are skipped during traversal of any
 * view whose pointer does not match the node's s_v slot.  When s_v is NULL
 * the scope is "shared" and its children are visible to all views.
 *
 * Bit allocation note:
 *   bits 28-34: BSG_NODE_* taxonomy flags (ROOT through SENSOR)
 *   bit  35:    BSG_NODE_VIEW_SCOPE (0x800000000)
 *   bit  39:    BSG_NODE_VIEW_REF (0x8000000000)
 */
#define BSG_NODE_VIEW_SCOPE  0x800000000ULL  /**< @brief view-scope container node */
/**
 * Phase V2 (drawing_stack_modernization):
 * DEPRECATED (Phase V4): The temporary BSG_OBJ_VIEW bridge has been removed.
 * These defines are retained only for ABI compatibility; no new code should
 * reference BSG_NODE_VIEW_REF or BSG_NODE_VIEW_BRIDGE.
 */
#define BSG_NODE_VIEW_REF   0x8000000000ULL  /**< @brief DEPRECATED: view-object bridge proxy node */
/** @brief DEPRECATED: internal bridge container group (Phase V2 only) */
#define BSG_NODE_VIEW_BRIDGE 0x200000000000ULL

/**
 * Payload type flags — stored in s_type_flags alongside BSG_NODE_* bits.
 * These describe what kind of data payload a BSG_NODE_SHAPE carries.
 *
 * Bit allocation:
 *   bits 28-34: BSG_NODE_* taxonomy flags
 *   bit  35:    BSG_NODE_VIEW_SCOPE
 *   bits 36-38: BSG_SENSOR_* sub-type flags (defined in sensor.h)
 *   bit  39:    BSG_NODE_VIEW_REF
 *   bits 40-44: BSG_PAYLOAD_* payload type flags (this block)
 *   bit  45:    BSG_NODE_VIEW_BRIDGE
 */
#define BSG_PAYLOAD_VLIST   0x10000000000ULL  /**< @brief raw bsg_vlist payload (bit 40) */
#define BSG_PAYLOAD_CSG     0x20000000000ULL  /**< @brief CSG wireframe payload (bit 41) */
#define BSG_PAYLOAD_MESH    0x40000000000ULL  /**< @brief BoT LoD mesh payload (bit 42) */
#define BSG_PAYLOAD_BREP    0x80000000000ULL  /**< @brief BRep payload (bit 43) */
#define BSG_PAYLOAD_OVERLAY 0x100000000000ULL /**< @brief HUD overlay element (bit 44) */

/** Mask covering all payload bits */
#define BSG_PAYLOAD_MASK    0x1F0000000000ULL

typedef struct bsg_node bsg_node;
typedef struct bsg_node bsg_shape;

/* Phase D7 (drawing_modernization.txt): all BV_* compatibility aliases live in
 * this single compatibility header and are controlled by
 * BSG_ENABLE_LEGACY_BV_ALIASES. */

__END_DECLS

#endif /* BSG_DEFINES_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
