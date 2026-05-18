/*                        B V I E W . H
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
/** @addtogroup bv_defines
 *
 * This header is intended to be independent of any one BRL-CAD library and is
 * specifically intended to allow the easy definition of common display list
 * types between otherwise independent libraries (libdm and libged, for
 * example).
 */

#ifndef BV_DEFINES_H
#define BV_DEFINES_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bu/vls.h"

/** @{ */
/** @file bv.h */

#ifndef BV_EXPORT
#  if defined(BV_DLL_EXPORTS) && defined(BV_DLL_IMPORTS)
#    error "Only BV_DLL_EXPORTS or BV_DLL_IMPORTS can be defined, not both."
#  elif defined(BV_DLL_EXPORTS)
#    define BV_EXPORT COMPILER_DLLEXPORT
#  elif defined(BV_DLL_IMPORTS)
#    define BV_EXPORT COMPILER_DLLIMPORT
#  else
#    define BV_EXPORT
#  endif
#endif

#include "bg/polygon_types.h"
#include "bv/tcl_data.h"
#include "bv/faceplate.h"

__BEGIN_DECLS

/* Define view ranges.  The numbers -2048 and 2047 go all the way back to the
 * original angle-distance cursor code that predates even BRL-CAD itself, but
 * (at least right now) there doesn't seem to be any documentation of why those
 * specific values were chosen. */
#define BV_MAX 2047.0
#define BV_MIN -2048.0
#define BV_RANGE 4095.0
/* Map +/-2048 BV space into -1.0..+1.0 :: x/2048*/
#define INV_BV 0.00048828125
#define INV_4096 0.000244140625


#define BV_MINVIEWSIZE 0.0001
#define BV_MINVIEWSCALE 0.00005

#ifndef UP
#  define UP 0
#endif
#ifndef DOWN
#  define DOWN 1
#endif

#define BV_ANCHOR_AUTO          0
#define BV_ANCHOR_BOTTOM_LEFT   1
#define BV_ANCHOR_BOTTOM_CENTER 2
#define BV_ANCHOR_BOTTOM_RIGHT  3
#define BV_ANCHOR_MIDDLE_LEFT   4
#define BV_ANCHOR_MIDDLE_CENTER 5
#define BV_ANCHOR_MIDDLE_RIGHT  6
#define BV_ANCHOR_TOP_LEFT      7
#define BV_ANCHOR_TOP_CENTER    8
#define BV_ANCHOR_TOP_RIGHT     9
struct bv_label {
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
struct bv_axes {
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

// Many settings have application level defaults that can be overridden for
// individual scene objects.
//
// TODO - once this settles down, it will probably warrant a bu_structparse
// for value setting
struct bv_obj_settings {

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
#define BV_OBJ_SETTINGS_INIT {0, 0, 1.0, 0, {255, 0, 0}, 1, 0.0, 0.0, 0, 0}


/* Note that it is possible for a view object to be view-only (not
 * corresponding directly to the wireframe of a database shape) but also based
 * off of database data.  Evaluated shaded objects would be an example, as
 * would NIRT solid shotline visualizations or overlap visualizations.  The
 * categorizations for the various types of bv_scene_obj objects would be:
 *
 * solid wireframe/triangles (obj.s):  BV_DBOBJ_BASED
 * rtcheck overlap visual:             BV_DBOBJ_BASED & BV_VIEWONLY
 * polygon/line/label:                 BV_VIEWONLY
 *
 * TODO - the distinction between view and db objs at this level probably needs
 * to go away - the application (or at least higher level libraries like
 * libged) should be the one managing the semantic meanings of objects.
 *
 * The distinction between objects (lines, labels, etc.) defined as
 * bv_scene_obj VIEW ONLY objects and the faceplate elements is objects defined
 * as bv_scene_obj objects DO exist in the 3D scene, and will move as 3D
 * elements when the view is manipulated (although label text is drawn parallel
 * to the view plane.)  Faceplate elements exist ONLY in the HUD and are not
 * managed as bv_scene_obj objects - they will not move with view manipulation.
 */
#define BV_DBOBJ_BASED    0x01
#define BV_VIEWONLY       0x02
#define BV_LINES          0x04
#define BV_LABELS         0x08
#define BV_AXES           0x10
#define BV_POLYGONS       0x20

struct bview;

#define BV_DB_OBJS 0x01
#define BV_VIEW_OBJS 0x02
#define BV_LOCAL_OBJS 0x04
#define BV_CHILD_OBJS 0x08

struct bv_scene_obj_internal;
struct bv_scene_obj;

/* Phase 11 (drawing_stack_modernization): renderer-backend contract.
 *
 * type_tag values for struct bv_obj_backend.  Backends register their tag at
 * dm registration time; the per-shape s_backend slot carries the matching tag
 * so cross-backend handle confusion can be caught.  More tags will be added as
 * additional backends adopt the contract (e.g. dm-obol). */
#define BV_BACKEND_NONE  0u   /* no backend state attached */
#define BV_BACKEND_GL    1u   /* OpenGL/GL-via-software-rasterizer (dm-gl, dm-swrast, dm-qtgl, dm-glx, dm-wgl) */

/**
 * Phase 11 (drawing_stack_modernization): per-shape backend state.
 *
 * Replaces the previous pattern of adding backend-specific fields directly on
 * struct bv_scene_obj.  One bv_obj_backend describes a single backend's
 * per-shape state; the active scene object stores the descriptor in
 * bv_scene_obj::s_backend.
 *
 * Lifecycle:
 *  - allocated lazily by the backend (typically when it first needs to cache
 *    a GPU resource for the shape);
 *  - released by bv_scene_obj_release_backend() when the shape is destroyed
 *    or recycled (also called from bv_obj_reset, bv_obj_put, and the libbsg
 *    tree free paths);
 *  - invalidated by bv_scene_obj_invalidate_backend() when the source data
 *    that drives the cached resource has changed (called from
 *    bv_obj_stale() and any other code that needs to flag the cached
 *    resource as out of date).
 *
 * Backends are expected to provide a free callback; invalidate is optional
 * and may be NULL for backends that have no separately-cacheable resource.
 */
struct bv_obj_backend {
    uint32_t type_tag;                          /**< @brief BV_BACKEND_* identifying the owner */
    void *handle;                               /**< @brief backend-private per-shape state */
    void (*free)(struct bv_scene_obj *);        /**< @brief release backend resources and free this descriptor */
    void (*invalidate)(struct bv_scene_obj *);  /**< @brief mark cached resource stale; may be NULL */
};

/**
 * Phase 10E (BSG enhancement): struct bsg_node is the first-class BSG
 * scene-graph node type.
 *
 * It MUST have struct bu_list l as its first field for bu_list pointer
 * compatibility.  struct bv_scene_obj embeds this as its first member,
 * so casting bsg_node* <-> bv_scene_obj* is valid via the first-member rule.
 *
 * Design constraints:
 *  - Only basic C types (no BSG-specific structs) so that bv/defines.h does
 *    not need to include BSG headers (which would create a circular dependency).
 *  - Identity and revision counters are stored inline (no heap allocation).
 *  - Material, appearance, and payload are stored as void* pointers; they
 *    are cast to the appropriate BSG types only inside libbsg code.
 *  - bsg_core_free_fn is called by bv_obj_reset() before the struct is
 *    zeroed, allowing libbsg to release any heap data it allocated without
 *    introducing a libbsg dependency in libbv.
 *
 * See doc/notes/bsg_enhancement_plan.txt Phase 10E for the full rationale.
 */
#define BSG_NODE_CORE_MAGIC 0x626e636fUL  /**< @brief magic: 'b','n','c','o' */
#define BSG_NODE_REV_MAX    8             /**< @brief max revision-counter slots */

struct bsg_node {
    struct bu_list l;              /**< @brief list linkage — MUST be first */
    unsigned long long bsg_kind;   /**< @brief BSG_NODE_* flags */
    struct bu_vls bsg_name;        /**< @brief object name */
    struct bsg_node *bsg_parent;   /**< @brief parent node */
    struct bu_ptbl bsg_children;   /**< @brief child node table */
    char bsg_flag;                 /**< @brief UP=visible, DOWN=invisible */
    char bsg_iflag;                /**< @brief UP=illuminated, DOWN=regular */
    int bsg_force_draw;            /**< @brief 1=always draw, overrides bsg_flag */
    /* Absorbed from bsg_node_core: */
    uint32_t bsg_magic;            /**< @brief BSG_NODE_CORE_MAGIC when initialized */
    int have_identity;
    uint64_t identity_node_id;
    uint64_t identity_part_id;
    uint64_t identity_instance_id;
    int identity_source_kind;      /**< @brief enum bsg_source_kind value */
    uint64_t revisions[BSG_NODE_REV_MAX];
    void *material;
    void *appearance;
    void *payload;
    void (*bsg_core_free_fn)(struct bsg_node *);
};

/**
 * BSG GUARDRAIL: new scene-graph code should use BSG APIs (include/bsg/),
 * not direct struct bv_scene_obj field accesses.
 *
 * struct bv_scene_obj is transitional storage.  It is typedef'd as bsg_node
 * in include/bsg/defines.h so that existing pointers remain valid without
 * casts.  All new code that creates, traverses, or mutates scene-graph nodes
 * should call bsg_node_*(), bsg_identity_*(), bsg_material_*(),
 * bsg_appearance_*(), and related BSG functions rather than reading or writing
 * the raw fields below.
 *
 * Direct field access is permissible only in:
 *   - libbsg internal implementation files (src/libbsg/)
 *   - libbv low-level utility and vlist code (src/libbv/)
 *   - libdm renderer backends that have no BSG accessor yet
 *   - Legacy compatibility paths explicitly labelled BV_DEPRECATED
 *
 * See doc/notes/bsg_enhancement_plan.txt for the migration roadmap and
 * doc/notes/bsg_raw_field_inventory.txt for the current usage inventory.
 */
struct bv_scene_obj  {
    struct bsg_node bsg;

    /* Internal implementation storage */
    struct bv_scene_obj_internal *i;

    /* View object type id (see BV_* flags in bv/defines.h) */
    void *s_path;       	/**< @brief alternative (app specific) encoding of bsg.bsg_name */
    void *dp;       		/**< @brief app obj data */
    mat_t s_mat;		/**< @brief mat to use for internal lookup and mesh LoD drawing */

    /* Associated bv.  Note that scene objects are not assigned uniquely to
     * one view.  This value may be changed by the application in a multi-view
     * scenario as an object is edited from multiple different views, to supply
     * the necessary view context for editing. If the object needs to retain
     * knowledge of its original/creation view, it should save that info
     * internally in its s_i_data container.
     *
     * BV_DEPRECATED: do not use s_v for view-policy control flow;
     * scene data should not drive rendering decisions.  Use BViewState::redraw()
     * or bv_view_get/set_* accessors instead. */
    struct bview *s_v;

    /* Knowledge of how to create/update s_vlist and the other 3D geometry data, as well as
     * manage any custom data specific to this object */
    void *s_i_data;  /**< @brief custom view data (bv_line_seg, bv_label, bv_polyon, etc) */

    /* BV_DEPRECATED: LoD and CSG adaptive-wireframe update callbacks are now
     * driven by the BSG LoD node (bsg_lod_update via dm_draw_objs); this field
     * is retained for non-LoD users such as polygon update callbacks.
     */
    int (*s_update_callback)(struct bv_scene_obj *, struct bview *, int);  /**< @brief custom update/generator for s_vlist */
    void (*s_free_callback)(struct bv_scene_obj *);  /**< @brief free any info stored in s_i_data, s_path and draw_data */

    /* 3D vector list geometry data */
    struct bu_list s_vlist;	/**< @brief  Pointer to unclipped vector list */
    size_t s_vlen;			/**< @brief  Number of actual cmd[] entries in vlist */

    /* Phase 11 (drawing_stack_modernization): generic renderer-backend slot.
     *
     * One backend-owned pointer per scene object replaces the previous pattern
     * of adding backend-specific fields directly on bv_scene_obj.  The
     * descriptor records:
     *   - type_tag: identifies the owning backend (e.g. BV_BACKEND_GL, future
     *     BV_BACKEND_OBOL) so cross-backend mistakes can be caught;
     *   - handle:   backend-private per-shape state (compiled GL display list,
     *     vertex buffer object, GPU resource handle, ...);
     *   - free:     cleanup callback fired by bv_scene_obj_release_backend()
     *     when the shape is destroyed/recycled;
     *   - invalidate: optional callback fired by
     *     bv_scene_obj_invalidate_backend() when the source data has changed
     *     and any cached GPU resource must be recomputed.
     *
     * NULL if the active backend does not need per-shape state.  Backends are
     * expected to allocate one bv_obj_backend per shape (typically lazily) and
     * store it here; bv_obj_reset() / bv_obj_put() will fire the free callback
     * and clear the slot.  See struct gl_backend_handle in libdm/dm-gl_lod.cpp
     * for the GL family's per-shape state (display list index/mode/stale
     * flag) — formerly the BV_DEPRECATED s_dlist / s_dlist_mode /
     * s_dlist_stale / s_dlist_free_callback fields, retired in Phase 13. */
    struct bv_obj_backend *s_backend;

    /* 3D geometry metadata */
    fastf_t s_size;		/**< @brief  Distance across solid, in model space */
    fastf_t s_csize;		/**< @brief  Dist across clipped solid (model space) */
    vect_t s_center;		/**< @brief  Center point of solid, in model space */
    int s_displayobj;		/**< @brief  Vector list contains vertices in display context flag */
    point_t bmin;
    point_t bmax;
    int have_bbox;
    /* Phase 9.1 (drawing_stack_modernization B3 residual):
     * For BSG_NODE_GROUP/ROOT nodes, indicates whether bmin/bmax currently
     * holds a valid cached aggregate bbox of the subtree's non-overlay
     * descendants.  Cleared on any structural mutation that could affect
     * the aggregate via bsg_node_bbox_invalidate().  Set by bsg_subtree_bbox
     * when it computes a fresh aggregate.  Unused on BSG_NODE_SHAPE leaves
     * (their bbox is always derivable from s_center/s_size). */
    int s_bbox_cached;

    /* Display properties */
    unsigned char s_color[3];	/**< @brief  color to draw as */
    uint32_t s_color_rev;       /**< @brief  material-revision stamp; set to gd_mater_rev each time this shape's color is recalculated by bsg_view_obj_color_from_soltab (B4 infrastructure, Phase 7 Step 14) */
    /* Phase 9.2 (drawing_stack_modernization): per-shape "drawn this frame"
     * generation counter.  When the renderer paints the object during
     * dm_draw_objs(), it stamps s_drawn_rev := bview::gv_frame_rev.  Callers
     * test whether a shape was actually drawn in the most recent frame by
     * comparing s_drawn_rev to the bview's current gv_frame_rev — replacing
     * the legacy "set every shape's s_flag = DOWN at the start of a frame
     * and UP only after rendering" sweep.  Initial value 0 is correct because
     * gv_frame_rev is bumped before the first draw, so an undrawn shape
     * will always disagree with the current frame. */
    uint64_t s_drawn_rev;
    int s_soldash;		/**< @brief  solid/dashed line flag: 0 = solid, 1 = dashed*/
    int s_arrow;		/**< @brief  arrow flag for view object drawing routines */
    int s_changed;		/**< @brief  changed flag - set by s_update_callback if a change occurred */
    int current;

    /* Adaptive plotting info.
     *
     * The adaptive wireframe flag is set if the wireframe was created while
     * adaptive mode is on - this is to allow reversion to non-adaptive
     * wireframes if the mode is switched off without the view scale changing.
     *
     * NOTE: We store the following NOT for controlling the drawing, but so we
     * can determine if the vlist is current with respect to the parent view
     * settings.  These values SHOULD NOT be directly manipulated by any user
     * facing commands (such as view obj).
     *
     * BV_DEPRECATED: adaptive_wireframe, view_scale, bot_threshold,
     * curve_scale, and point_scale are snapshot values used to detect when a
     * redraw is needed.  The owning decision logic is being moved to
     * BViewState::redraw().
     * These fields will be removed after one release cycle once BViewState
     * owns the full redraw trigger. */
    int     adaptive_wireframe;
    int     csg_obj;
    int     mesh_obj;
    fastf_t view_scale;
    size_t  bot_threshold;
    fastf_t curve_scale;
    fastf_t point_scale;

    /* Scene object settings which also (potentially) have global defaults but
     * may be overridden locally */
    struct bv_obj_settings *s_os;
    struct bv_obj_settings s_local_os;
    int s_inherit_settings;           /**< @brief  Use current obj settings when drawing children instead of their settings */

    /* Settings that may be less necessary... */
    struct bv_scene_obj_old_settings s_old;

    /* Object level pointers to parent containers.  These are stored so
     * that the object itself knows everything needed for data manipulation
     * and it is unnecessary to explicitly pass other parameters. */

    /* Reusable vlists */
    struct bu_list *vlfree;

    /* Container for reusing bv_scene_obj allocations */
    struct bv_scene_obj *free_scene_obj;

    /* View container containing this object */
    struct bu_ptbl *otbl;

    /* For more specialized routines not using vlists, we may need
     * additional drawing data associated with a scene object */
    void *draw_data;

    /* User data to associate with this view object */
    void *s_u_data;
};



/* bv_scene_groups (one level above scene objects, conceptually equivalent
 * to display_list) are used to capture the intent of drawing commands.  For
 * example, in the scenario where a draw command is used to visualize a comb
 * with sub-combs a and b:
 *
 * ged> draw comb
 *
 * The drawing code will check the proposed group against existing groups,
 * adding and removing accordingly.  It will then walk the hierarchy and create
 * bv_scene_obj instances for all solids below comb/a and comb/b as children
 * of the scene group.  Note that since we specified "comb" as the drawn
 * object, if comb/b is removed from comb and comb/c is added, we would expect
 * comb's displayed view to be updated to reflect its current structure.  If,
 * however, we instead did the original visualization with the commands:
 *
 * ged> draw comb/a
 * ged> draw comb/b
 *
 * The same solids would be drawn, but conceptually the comb itself is not
 * drawn - the two instances are.  If comb/b is removed and comb/c added, we
 * would not expect comb/c to be drawn since we never drew either that instance
 * or its parent comb.
 *
 * However, if comb/a and comb/b are drawn and then comb is drawn, the new comb
 * scene group will replace both the comb/a and comb/b groups since they are now
 * part of a higher level object being drawn.  If comb is drawn and comb/a is
 * subsequently drawn, it will be a no-op since "comb" is already covering that
 * case.
 *
 * The rule with bv_scene_group instances is their children must specify a
 * fully realized entity - if the s_name is "/comb/a" then everything below
 * /comb/a is drawn.  If /comb/a/obj1.s is erased, new bv_scene_group
 * entities will be needed to reflect the partial nature of /comb/a in the
 * visualization.  That requirement also propagates back up the tree. If a has
 * obj1.s and obj2.s below it, and /comb/a/obj1.s is erased, an original
 * "/comb" scene group will be replaced by new scene groups: /comb/a/obj2.s and
 * /comb/b.  Note that if /comb/a/obj1.s is subsequently drawn in isolation,
 * the scene groups will not collapse back to a single comb group - the user
 * will not at that point have explicitly issued instructions to draw comb as a
 * whole, even though all the individual elements have been drawn.  A "view
 * simplify" command should probably be added to support collapsing to the
 * simplest available option automatically in that situation.
 *
 * Note that the above rule is for explicit erasure from the drawn scene group
 * - if the structure of /comb/a is changed the drawn object is still "comb"
 * and the solid children of the existing group are updated to reflect the
 * current state of comb, rather than introducing new scene groups.
 *
 * Much like point_t and vect_t, the distinction between a group and an
 * individual object is largely semantic rather than a question of different
 * data storage.  A group just uses the bv_scene_obj container to store
 * group-wide default settings, and g.children holds the individual
 * bv_scene_obj entries corresponding to the solids.  A bv_scene_obj
 * should always map to a solid - a group may specify a solid but more
 * typically will reference the root of a CSG tree and have solids below it.
 * We define them to have different types only to help keep straight in the
 * code what is a conceptually a group and what is an individual scene object.
 *
 * TODO - once the latest drawing code update matures, the path management
 * done there should make the idea of a bv_scene_group moot.
 */
#define bv_scene_group bv_scene_obj


/* The primary "working" data for mesh Level-of-Detail (LoD) drawing is stored
 * in a bv_mesh_lod container.
 *
 * Most LoD information is deliberately hidden in the internal, but the key
 * data needed for drawing routines and view setup are exposed. Although this
 * data structure is primarily managed in libbg, the public data in this struct
 * is needed at many levels of the software stack, including libbv. */
struct bv_mesh_lod {

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
    struct bv_scene_obj *s;

    // Pointer to the higher level LoD context associated with this LoD data
    void *c;

    // Pointer to internal LoD implementation information specific to this object
    void *i;
};

/* Flags to identify categories of objects to snap */
#define BV_SNAP_SHARED 0x1
#define BV_SNAP_LOCAL  0x2
#define BV_SNAP_DB  0x4
#define BV_SNAP_VIEW    0x8
#define BV_SNAP_TCL    0x10

/* We encapsulate non-camera settings into a container mainly to allow for
 * easier reuse of the same settings between different views - if a common
 * setting set is maintained between different views, this container allows
 * us to just point to the common set from all views using it. */
struct bview_settings {
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
    struct bv_axes           gv_model_axes;
    struct bv_axes           gv_view_axes;
    struct bv_grid_state     gv_grid;
    struct bv_other_state    gv_center_dot;
    struct bv_params_state   gv_view_params;
    struct bv_other_state    gv_view_scale;
    double                   gv_frametime;

    // Framebuffer visualization is possible if there is an attached dm and
    // that dm has an associated framebuffer.  If those conditions are met,
    // this variable is used to control how the fb is visualized.
    int                      gv_fb_mode; // 0 = off, 1 = overlay, 2 = underlay

    // More complex are the faceplate view elements not corresponding to
    // geometry objects but editable by the user.  These aren't managed as
    // gv_view_objs (they are HUD visuals and thus not part of the scene) so
    // they have some unique requirements.
    struct bv_adc_state              gv_adc;
    struct bv_interactive_rect_state gv_rect;


};

/* A view needs to know what objects are active within it, but this is a
 * function not just of adding and removing objects via commands like
 * "draw" and "erase" but also what settings are active.  Shared objects
 * are common to multiple views, but if adaptive plotting is enabled the
 * scene objects cannot also be common - the representations of the objects
 * may be different in each view, even though the object list is shared.
 */
struct bview_objs {

    // Container for db object groups unique to this view (typical use case is
    // adaptive plotting, where geometry wireframes may differ from view to
    // view and thus need unique vlists.)
    struct bu_ptbl  *db_objs;

    // Available bv_vlist entities to recycle before allocating new for local
    // view objects. This is used only if the app doesn't supply a vlfree -
    // normally the app should do so, so memory from one view can be reused for
    // other views.
    struct bu_list  gv_vlfree;

    /* Container for reusing bv_scene_obj allocations */
    struct bv_scene_obj *free_scene_obj;
};

// Data for managing "knob" manipulation of views.  One historical hardware
// example of this "knob" concept of view manipulation would be Dial boxes such
// as the Silicon Graphics SN-921, used with 3D workstations in the early days.
// Although we've not heard of Dial boxes being used with BRL-CAD in many
// years, the mathematics of view manipulation used to support them still
// underpins interactions driven with inputs from modern peripherals such as
// the mouse.
struct bview_knobs {

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

struct bview_set;

struct bview {
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
     * definitions are needed in bview to support "view aware" algorithms that
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
    struct bview_settings *gv_s;     /**< @brief shared settings supplied by user */
    struct bview_settings gv_ls;     /**< @brief locally maintained settings specific to view (used if gv_s is null) */

    /* Set containing this view.  Also holds pointers to resources shared
     * across multiple views */
    struct bview_set *vset;

    /* Scene objects active in a view.  Managing these is a relatively complex
     * topic and depends on whether a view is shared, independent or adaptive.
     * Shared objects are common across views to make more efficient use of
     * system memory. */
    struct bview_objs gv_objs;

    /* We sometimes need to define the volume in space that is "active" for the
     * view.  For an orthogonal camera this is the oriented bounding box
     * extruded to contain active scene objects visible in the view  The app
     * must set the gv_bounds_update callback to bg_view_bound so a bv_update
     * call can update these values.*/
    point_t obb_center;
    vect_t obb_extent1;
    vect_t obb_extent2;
    vect_t obb_extent3;
    void (*gv_bounds_update)(struct bview *);

    /* "Backed out" point, lookat direction, scene radius. Used for geometric
     * view based interrogation. */
    point_t gv_vc_backout;
    vect_t gv_lookat;
    double radius;

    /* Knob-based view manipulation data */
    struct bview_knobs k;

    /* Virtual trackball position */
    point_t     orig_pos;

    // libtclcad data (optional: NULL for non-Tcl views, allocated and owned by
    // libtclcad tclcad_view_data when a Tcl-backed view is created)
    struct bv_data_tclcad *gv_tcl;

    /* Callback, external data */
    void          (*gv_callback)(struct bview *, void *);  /**< @brief  called in ged_view_update with gvp and gv_clientData */
    void           *gv_clientData;   /**< @brief  passed to gv_callback */
    struct bu_ptbl *callbacks;
    void           *dmp;             /* Display manager pointer, if one is associated with this view */
    void           *u_data;          /* Caller data associated with this view */

    /* Phase 4 (drawing_stack_modernization): BSG scene-graph root for this
     * view.  Stored as void * to avoid a circular include dependency between
     * bv/defines.h and bsg/defines.h.  Cast to struct bv_scene_obj * (which
     * is typedef'd as bsg_node) before use.  NULL until bsg_scene_root_create
     * is called for this view. */
    void           *bsg_root;

    /* Phase 7 step 7 A3 (drawing_stack_modernization): GED draw-tree root for
     * this view.  Stored as void * to avoid circular headers.  Cast to
     * struct bv_scene_obj * (= bsg_node) before use.  Set by
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
};

// Because bview instances frequently share objects in applications, they are
// not always fully independent - we define a container and some basic
// operations to manage this.
struct bview_set_internal;
struct bview_set {
    struct bview_set_internal   *i;
    struct bview_settings       settings;
};

__END_DECLS

#endif /* BV_DEFINES_H */

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
