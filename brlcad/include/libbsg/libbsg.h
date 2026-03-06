/*                  L I B B S G / L I B B S G . H
 * BRL-CAD
 *
 * Copyright (c) 2024-2025 United States Government as represented by
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
 * @file libbsg/libbsg.h
 *
 * @brief libbsg — Fully independent BRL-CAD scene-graph library.
 *
 * libbsg is a clean-room implementation of the BRL-CAD scene-graph API
 * that depends only on @c libbu and @c libbn.  It contains NO reference to
 * @c libbv, @c bview, @c bv_scene_obj, or any other legacy view-layer type.
 *
 * ### Design goals
 *
 * | Goal | Approach |
 * |------|----------|
 * | Independence | No @c bv.h / @c bsg/defines.h includes anywhere in the library |
 * | Clean node hierarchy | @c bsg_node as the unified base; type flags determine role |
 * | Traversal-first | Scene is always walked via @c libbsg_traverse(); no flat tables |
 * | Incremental migration | Existing @c libbv / @c bsg code is unchanged; new code uses this header |
 *
 * ### Naming conventions
 *
 * All types defined here use the @c bsg_* prefix (same names as the
 * Phase 1 aliases in @c bsg/defines.h) but with **different struct layouts**.
 * The two header families (@c bsg/defines.h and @c libbsg/libbsg.h) are
 * **mutually exclusive** in the same translation unit; a compile-time guard
 * enforces this restriction.
 *
 * API functions specific to this library are prefixed with @c libbsg_ to
 * clearly distinguish them from the @c bsg_* wrappers in @c libbv.
 *
 * ### Migration path
 *
 * @code
 * // Old code (libbv-based):
 * #include "bsg/defines.h"
 *
 * // New code (libbsg-based):
 * #include "libbsg/libbsg.h"
 * @endcode
 *
 * Migrate one translation unit at a time.  When all callers have migrated,
 * the @c bsg/defines.h wrappers will be removed.
 */

#ifndef LIBBSG_LIBBSG_H
#define LIBBSG_LIBBSG_H

/*
 * Enforce mutual exclusion with the legacy bsg/defines.h header.
 * Including both headers in the same translation unit would cause
 * duplicate struct definitions for bsg_camera, bsg_shape, etc.
 */
#ifdef BSG_DEFINES_H
#  error "libbsg/libbsg.h and bsg/defines.h cannot be included in the same translation unit."
#endif

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bu/vls.h"

/* Export / import macro --------------------------------------------------- */
#ifndef LIBBSG_EXPORT
#  ifdef LIBBSG_DLL_EXPORTS
#    define LIBBSG_EXPORT COMPILER_DLLEXPORT
#  elif defined(LIBBSG_DLL_IMPORTS)
#    define LIBBSG_EXPORT COMPILER_DLLIMPORT
#  else
#    define LIBBSG_EXPORT
#  endif
#endif

__BEGIN_DECLS

/** @addtogroup libbsg
 * @{ */

/** @file libbsg/libbsg.h */

/* ========================================================================
 * Node-type flags
 *
 * These constants use the same numeric values as the BSG_NODE_* macros in
 * bsg/defines.h so that the libbsg_dm adapter layer (Step 5) can compare
 * node type flags without needing to include both headers.
 * ======================================================================== */

/** @brief Leaf geometry node (carries renderable vlist geometry). */
#define BSG_NODE_SHAPE        0x00000001ULL

/** @brief DB-sourced shape (geometry came from a database path). */
#define BSG_NODE_DBOBJ_BASED  0x00000002ULL

/** @brief View-only shape (not stored in the database). */
#define BSG_NODE_VIEWONLY     0x00000004ULL

/** @brief Node carries line/vector geometry. */
#define BSG_NODE_LINES        0x00000008ULL

/** @brief Node carries label geometry. */
#define BSG_NODE_LABELS       0x00000010ULL

/** @brief Node carries axes geometry. */
#define BSG_NODE_AXES         0x00000020ULL

/** @brief Node carries polygon geometry. */
#define BSG_NODE_POLYGONS     0x00000040ULL

/** @brief Node has a mesh LoD payload. */
#define BSG_NODE_MESH_LOD     0x00000080ULL

/**
 * @brief Separator node (Open Inventor SoSeparator semantics).
 *
 * When set, the traversal engine saves the current traversal state
 * (accumulated transform, material, active camera) on descent and
 * fully restores it on ascent.  Children of this node may freely
 * modify state without affecting siblings or ancestors.
 */
#define BSG_NODE_SEPARATOR    0x00000100ULL

/**
 * @brief Transform node (Open Inventor SoTransform semantics).
 *
 * When set, the node's primary contribution is its @c xform matrix,
 * which is post-multiplied into the traversal state's accumulated
 * transform.  The node carries no renderable geometry of its own.
 */
#define BSG_NODE_TRANSFORM    0x00000200ULL

/**
 * @brief Camera node (Open Inventor SoCamera semantics).
 *
 * When set, the node's @c payload points to a heap-allocated
 * @c bsg_camera struct.  The traversal engine records the camera into
 * @c libbsg_traversal_state::active_camera when this node is visited.
 */
#define BSG_NODE_CAMERA       0x00000400ULL

/**
 * @brief Level-of-Detail group node (Open Inventor SoLOD semantics).
 *
 * When set, the node's @c payload points to a
 * @c libbsg_lod_switch_data struct.  The traversal engine picks
 * exactly one child (the best-match detail level) to visit, based on
 * the eye-to-node-center distance.  Children must be ordered
 * highest-to-lowest detail.
 */
#define BSG_NODE_LOD_GROUP    0x00000800ULL


/* ========================================================================
 * Camera
 * ======================================================================== */

/**
 * @brief Camera (viewpoint) parameters.
 *
 * This struct is layout-compatible with @c struct @c bsg_camera in
 * @c bsg/defines.h (identical field order and types) so that the
 * @c libbsg_dm adapter can safely cast between them when bridging the
 * two APIs during the transition period.
 */
struct bsg_camera {
    fastf_t  perspective;   /**< perspective angle; 0 = orthographic */
    vect_t   aet;           /**< azimuth / elevation / twist          */
    vect_t   eye_pos;       /**< eye position in model space          */
    vect_t   keypoint;      /**< rotation keypoint                    */
    char     coord;         /**< active coordinate system             */
    char     rotate_about;  /**< rotation pivot specifier             */
    mat_t    rotation;      /**< view rotation matrix                 */
    mat_t    center;        /**< view centre matrix                   */
    mat_t    model2view;    /**< model-to-view transform              */
    mat_t    pmodel2view;   /**< perspective model-to-view transform  */
    mat_t    view2model;    /**< view-to-model transform              */
    mat_t    pmat;          /**< perspective matrix                   */
};
typedef struct bsg_camera bsg_camera;


/* ========================================================================
 * Base scene-graph node
 * ======================================================================== */

/**
 * @brief Base scene-graph node.
 *
 * Every object in a libbsg scene graph is (or begins with) a @c bsg_node.
 * The @c type_flags field determines the node's role; the @c payload
 * pointer carries type-specific data owned by the node.
 *
 * The @c children table holds @c bsg_node* pointers.  The graph is a
 * DAG — a node may appear in multiple parents' children tables.  The
 * @c parent back-pointer points to the most-recently-assigned parent
 * (last call to @c libbsg_node_add_child); in a DAG this is the last
 * parent that adopted the node.
 *
 * All memory is managed by the caller.  Free nodes with
 * @c libbsg_node_free(); free shape nodes with @c libbsg_shape_free().
 */
struct bsg_node {
    unsigned long long  type_flags;       /**< BSG_NODE_* flags                      */
    struct bu_ptbl      children;         /**< child bsg_node* list (bu_ptbl)        */
    mat_t               xform;            /**< local transform (IDN if not a transform node) */
    void               *payload;          /**< type-specific payload (owned by node) */
    void (*free_payload)(void *);         /**< destructor for payload; NULL = no-op  */
    struct bsg_node    *parent;           /**< back-pointer to logical parent        */
};
typedef struct bsg_node bsg_node;

/**
 * @brief Convenience typedef: a separator node.
 *
 * A separator is a @c bsg_node with @c BSG_NODE_SEPARATOR set.
 * It saves / restores traversal state across its children.
 */
typedef bsg_node bsg_separator;

/**
 * @brief Convenience typedef: a transform node.
 *
 * A transform is a @c bsg_node with @c BSG_NODE_TRANSFORM set.
 * Its @c xform matrix is post-multiplied into the traversal state.
 */
typedef bsg_node bsg_transform;


/* ========================================================================
 * Leaf shape node
 * ======================================================================== */

/**
 * @brief Leaf geometry node — carries renderable vlist geometry.
 *
 * A @c bsg_shape embeds a @c bsg_node as its first field; a pointer to
 * @c bsg_shape can be safely cast to @c bsg_node* and vice versa.
 *
 * Geometry is stored in @c vlist as a chain of BRL-CAD vlists compatible
 * with @c dm_draw_vlist().
 */
struct bsg_shape {
    struct bsg_node node;           /**< embedded base node — MUST be first */

    /* Renderable geometry */
    struct bu_list  vlist;          /**< chain of BN_VLIST_* vlist segments */

    /* Bounding sphere (computed on demand) */
    point_t         s_center;       /**< sphere centre in model space        */
    fastf_t         s_size;         /**< sphere radius                       */
    int             have_bbox;      /**< non-zero if bounding sphere is valid */

    /* Display attributes */
    unsigned char   s_color[3];     /**< RGB display colour (0–255)          */
    int             s_cflag;        /**< colour-override flag                 */
    int             s_dmode;        /**< draw mode (wireframe / shaded …)    */
    int             s_iflag;        /**< illumination / highlight flag        */

    /* Identity */
    struct bu_vls   s_name;         /**< unique node name (empty = unnamed)  */
};
typedef struct bsg_shape bsg_shape;


/* ========================================================================
 * View parameters
 *
 * A plain-C struct that captures all viewport and camera information
 * needed by the traversal engine.  Deliberately free of any bview /
 * bv_* types.
 * ======================================================================== */

/**
 * @brief Viewport and camera parameters for a single rendered view.
 *
 * Pass a pointer to one of these to @c libbsg_scene_root_create() or
 * @c libbsg_view_bind() to set up the camera node in the scene root.
 */
struct libbsg_view_params {
    int             width;      /**< viewport width in pixels   */
    int             height;     /**< viewport height in pixels  */
    fastf_t         scale;      /**< view scale (gv_scale)      */
    fastf_t         size;       /**< view size  (gv_size)       */
    fastf_t         isize;      /**< 1.0 / size (gv_isize)      */
    struct bsg_camera camera;   /**< camera state               */
};
typedef struct libbsg_view_params libbsg_view_params;


/* ========================================================================
 * Traversal state
 * ======================================================================== */

/**
 * @brief Accumulated state during a scene-graph traversal.
 *
 * Callers initialise one of these with @c libbsg_traversal_state_init()
 * before the first call to @c libbsg_traverse().  The traversal engine
 * updates the fields in-place as it descends the graph.
 *
 * ### Accumulation rules
 *
 *   - **Transform**: @c xform = parent_xform * node->xform (post-multiply)
 *     at every node.
 *   - **Camera**: @c active_camera is set to @c node->payload whenever a
 *     @c BSG_NODE_CAMERA node is encountered.
 *   - **Separator**: @c BSG_NODE_SEPARATOR causes the engine to save the
 *     full state on descent and restore it on ascent (SoSeparator
 *     semantics).
 *   - **LoD group**: @c BSG_NODE_LOD_GROUP selects exactly one child to
 *     visit, based on eye-to-center distance derived from @c view.
 */
struct libbsg_traversal_state {
    mat_t                     xform;         /**< accumulated model transform      */
    const struct bsg_camera  *active_camera; /**< camera from last camera node     */
    int                       depth;         /**< traversal depth (root = 0)       */
    const libbsg_view_params *view;          /**< view params for LoD; may be NULL */
};
typedef struct libbsg_traversal_state libbsg_traversal_state;

/**
 * @brief Visitor callback type for @c libbsg_traverse().
 *
 * @param node      Current node.
 * @param state     Traversal state at the point of this node.
 * @param user_data Opaque value passed to @c libbsg_traverse().
 * @return 0 to continue into the node's children; non-zero to prune.
 */
typedef int (*libbsg_visit_fn)(bsg_node                    *node,
			       const libbsg_traversal_state *state,
			       void                         *user_data);


/* ========================================================================
 * LoD group switch data
 * ======================================================================== */

/**
 * @brief Switch-distance table for a @c BSG_NODE_LOD_GROUP node.
 *
 * The @c distances array has @c num_levels - 1 entries.  The n-th entry
 * is the eye-to-center distance at which the renderer transitions from
 * child[n] (finer) to child[n+1] (coarser).
 */
struct libbsg_lod_switch_data {
    size_t   num_levels;    /**< number of detail levels (== number of children) */
    fastf_t *distances;     /**< (num_levels - 1) switch distances               */
};
typedef struct libbsg_lod_switch_data libbsg_lod_switch_data;


/* ========================================================================
 * Node lifecycle
 * ======================================================================== */

/**
 * @brief Allocate a new @c bsg_node with the given type flags.
 *
 * The @c children ptbl is initialised; the @c xform is set to the
 * identity matrix; all other fields are zeroed.
 *
 * @param type_flags  One or more @c BSG_NODE_* flags.
 * @return Newly allocated node.  Free with @c libbsg_node_free().
 */
LIBBSG_EXPORT bsg_node *libbsg_node_alloc(unsigned long long type_flags);

/**
 * @brief Free a @c bsg_node.
 *
 * Calls @c free_payload (if set), frees the @c children ptbl, and
 * releases the node memory.  If @p recurse is non-zero, all child
 * nodes are freed first (depth-first).  Child nodes that were obtained
 * with @c libbsg_shape_alloc() should be freed with @c libbsg_shape_free()
 * instead; @c libbsg_node_free() with @p recurse handles the distinction
 * automatically because shapes embed @c bsg_node as their first field.
 *
 * @param node     Node to free.  NULL is a no-op.
 * @param recurse  If non-zero, recursively free all children first.
 */
LIBBSG_EXPORT void libbsg_node_free(bsg_node *node, int recurse);

/**
 * @brief Allocate a leaf geometry node (@c BSG_NODE_SHAPE).
 *
 * Returns a heap-allocated @c bsg_shape with all geometry lists
 * empty.  The embedded @c bsg_node base has @c BSG_NODE_SHAPE set.
 *
 * @return Newly allocated shape.  Free with @c libbsg_shape_free().
 */
LIBBSG_EXPORT bsg_shape *libbsg_shape_alloc(void);

/**
 * @brief Free a leaf geometry node.
 *
 * Frees all vlists in @c s->vlist, then frees the @c s_name vls, and
 * finally frees the @c bsg_node fields (children ptbl, payload) before
 * releasing the node memory.  Does NOT recurse into children; shapes
 * that own child nodes must free them separately first.
 *
 * @param s  Shape to free.  NULL is a no-op.
 */
LIBBSG_EXPORT void libbsg_shape_free(bsg_shape *s);


/* ========================================================================
 * Parent–child management
 * ======================================================================== */

/**
 * @brief Append @p child to @p parent's children list.
 *
 * Sets @c child->parent = @p parent.  If @p child is already in
 * @p parent's children list this call is a no-op.
 */
LIBBSG_EXPORT void libbsg_node_add_child(bsg_node *parent, bsg_node *child);

/**
 * @brief Remove @p child from @p parent's children list.
 *
 * Sets @c child->parent = NULL if @c child->parent == @p parent.
 * Does nothing if @p child is not in @p parent's children list.
 */
LIBBSG_EXPORT void libbsg_node_rm_child(bsg_node *parent, bsg_node *child);


/* ========================================================================
 * Scene root
 * ======================================================================== */

/**
 * @brief Create a fresh separator scene root.
 *
 * Allocates a @c BSG_NODE_SEPARATOR root node and prepends a camera
 * node derived from @p params (or a default identity camera if
 * @p params is NULL).
 *
 * @param params  Optional view parameters for the initial camera node.
 * @return Newly allocated root node.  Free with @c libbsg_node_free(root, 1).
 */
LIBBSG_EXPORT bsg_node *libbsg_scene_root_create(const libbsg_view_params *params);

/**
 * @brief Allocate a camera node (@c BSG_NODE_CAMERA) pre-populated from @p cam.
 *
 * A copy of @p cam is stored in the node's @c payload.  The node owns
 * the copy and frees it when freed with @c libbsg_node_free().
 * If @p cam is NULL the camera fields are zeroed.
 *
 * @return Newly allocated camera node.  Free with @c libbsg_node_free().
 */
LIBBSG_EXPORT bsg_node *libbsg_camera_node_alloc(const bsg_camera *cam);

/**
 * @brief Return the first @c BSG_NODE_CAMERA child of @p root, or NULL.
 *
 * Scans @p root->children in order.  Returns the first child whose
 * @c type_flags has @c BSG_NODE_CAMERA set, or NULL if none.
 */
LIBBSG_EXPORT bsg_node *libbsg_scene_root_camera(const bsg_node *root);

/**
 * @brief Extract camera data from a camera node into @p out.
 *
 * @p node must have @c BSG_NODE_CAMERA set and a non-NULL @c payload.
 * Returns 0 on success, -1 if @p node is not a valid camera node.
 */
LIBBSG_EXPORT int libbsg_camera_node_get(const bsg_node *node,
					 bsg_camera     *out);

/**
 * @brief Write camera data @p cam into a camera node @p node.
 *
 * @p node must have @c BSG_NODE_CAMERA set.  The existing payload is
 * updated in-place (or a new payload is allocated if @p node->payload
 * is NULL).
 */
LIBBSG_EXPORT void libbsg_camera_node_set(bsg_node       *node,
					  const bsg_camera *cam);


/* ========================================================================
 * View binding
 * ======================================================================== */

/**
 * @brief Bind view parameters to a scene root.
 *
 * Finds the first @c BSG_NODE_CAMERA child of @p root and updates its
 * payload with the camera from @p params.  If no camera node exists, a
 * new one is prepended to @p root->children.
 *
 * @param root    Scene root (must not be NULL).
 * @param params  New view parameters (must not be NULL).
 */
LIBBSG_EXPORT void libbsg_view_bind(bsg_node                 *root,
				    const libbsg_view_params *params);


/* ========================================================================
 * Traversal engine
 * ======================================================================== */

/**
 * @brief Initialise a traversal state to identity values.
 *
 * Sets @c xform to the identity matrix, @c active_camera to NULL,
 * @c depth to 0, and @c view to NULL.  Must be called before passing
 * the state to @c libbsg_traverse().
 */
LIBBSG_EXPORT void libbsg_traversal_state_init(libbsg_traversal_state *state);

/**
 * @brief Traverse the scene (sub-)graph rooted at @p root.
 *
 * Performs a pre-order depth-first walk.  At each node @p visit is
 * called with the node pointer, the current accumulated state, and
 * @p user_data.  If @p visit returns non-zero the node's subtree is
 * pruned (no children are visited for that node).
 *
 * @par State accumulation
 *   - **Transform**: @c state->xform = parent_xform * node->xform
 *     (post-multiply) at every node.
 *   - **Camera**: @c state->active_camera = node->payload for every
 *     @c BSG_NODE_CAMERA node encountered.
 *   - **Separator**: @c BSG_NODE_SEPARATOR saves state on descent
 *     and restores it on ascent (SoSeparator semantics).
 *   - **LoD group**: @c BSG_NODE_LOD_GROUP visits exactly one child,
 *     selected by eye-to-center distance from @c state->view.  If
 *     @c state->view is NULL, child[0] (highest detail) is chosen.
 *
 * @param root       Root of the sub-graph.  NULL is a no-op.
 * @param state      Traversal state (in-out).  Caller must initialise.
 * @param visit      Visitor callback invoked at each node (pre-order).
 * @param user_data  Opaque pointer forwarded to every @p visit call.
 */
LIBBSG_EXPORT void libbsg_traverse(bsg_node               *root,
				   libbsg_traversal_state *state,
				   libbsg_visit_fn         visit,
				   void                   *user_data);


/* ========================================================================
 * Typed query helper
 * ======================================================================== */

/**
 * @brief Collect all nodes in the sub-tree rooted at @p root whose
 *        @c type_flags & @p mask is non-zero.
 *
 * @p root itself is included if it matches.  Results are appended to
 * @p result; the caller must initialise @p result before calling.
 *
 * @param root    Root of the sub-graph to search.  NULL is a no-op.
 * @param mask    Type-flag mask to test against.
 * @param result  Initialised @c bu_ptbl to which matching nodes are appended.
 */
LIBBSG_EXPORT void libbsg_find_by_type(bsg_node           *root,
				       unsigned long long  mask,
				       struct bu_ptbl     *result);

/** @} */

__END_DECLS

#endif /* LIBBSG_LIBBSG_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
