/*                    B V _ P R I V A T E . h
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
/** @file bv_private.h
 *
 * Internal shared data structures
 *
 */

#ifndef LIBBV_BV_PRIVATE_H
#define LIBBV_BV_PRIVATE_H

#include "common.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "bv/defines.h"
#include <unordered_map>

/* --- bview_new: Render/View Context --- */
struct bview_new {
    struct bu_vls           name;
    struct bv_scene        *scene;         /* Associated scene graph (not owned) */

    struct bview_camera     camera;        /* Camera parameters */
    struct bv_node         *camera_node;   /* Optional: camera as scene node (SoCamera analogy) */

    struct bview_viewport   viewport;      /* Viewport/window info */

    struct bview_material   material;      /* View-level material/appearance (optional) */
    struct bview_appearance appearance;    /* Display/appearance (axes, grid, bg, etc.) */
    struct bview_overlay    overlay;       /* HUD/overlay settings */

    struct bview_pick_set   pick_set;      /* Selection/pick state */

    /* Redraw callback */
    bview_redraw_cb         redraw_cb;
    void                   *redraw_cb_data;

    /* Legacy bview sync (for migration only) */
    struct bview           *old_bview;
};

/* --- bv_scene: Scene Graph --- */
struct bv_scene {
    struct bv_node         *root;           /* Root separator node (SoSeparator analogy) */
    struct bu_ptbl          nodes;          /* All nodes (flat list for fast lookup) */
    struct bu_ptbl          views;          /* bview_new* pointers sharing this scene (not owned) */
    struct bv_node         *default_camera; /* Default camera node, if any */
};

/*
 * --- bv_node: Scene Graph Node ---
 *
 * This is the native BRL-CAD analog to Coin3D/Inventor's SoNode.  It serves
 * as the base building block of the scene graph and can represent geometry,
 * grouping, transforms, cameras, lights, and materials without requiring any
 * direct reference to Coin3D types.
 *
 * Long-term the intent is that a bv_node can be translated to/from a
 * corresponding SoNode for Coin3D/obol rendering, but all core BRL-CAD
 * logic operates on bv_node directly.
 */
struct bv_node {
    enum bv_node_type       type;           /* Node type (geometry, group, separator, etc.) */
    struct bu_vls           name;           /* Unique or descriptive name */
    struct bv_node         *parent;         /* Parent in hierarchy (NULL if root) */
    struct bu_ptbl          children;       /* Children (bv_node pointers) */

    /* Transform (analogous to SoTransform fields) */
    mat_t                   transform;      /* Local transform matrix */
    mat_t                   world_transform;/* Cached world transform (product of ancestors) */

    /* Geometry and Appearance (analogous to SoShape + SoMaterial) */
    void                   *geometry;       /* Geometry data pointer (type-specific) */
    struct bview_material   material;       /* Appearance/material for this node */

    int                     visible;        /* Visibility flag (0 = hidden, 1 = visible) */

    /* User data for custom extension, future Coin3D/obol node mapping, etc. */
    void                   *user_data;

    /* Optional fields for LoD, selection, etc. */
    int                     lod_level;      /* Level of detail (if used) */
    int                     selected;       /* Selection flag */

    /* Native axis-aligned bounding box (optional; overrides legacy obj sphere when set) */
    int                     have_bounds;    /* 1 when bounds_min/max are valid */
    point_t                 bounds_min;     /* AABB minimum corner */
    point_t                 bounds_max;     /* AABB maximum corner */

    /* Render-backend draw state (analog of bv_scene_obj::s_dlist / s_dlist_stale) */
    unsigned int            dlist;          /* Display list handle (backend-specific) */
    int                     dlist_stale;    /* 1 = display list needs regeneration */

    /* Per-node update/regenerate callback (analog of bv_scene_obj::s_update_callback) */
    bv_node_update_cb       update_cb;      /* Called when node geometry needs rebuilding */
    void                   *update_cb_data; /* Caller-managed user data for update_cb */
};



struct bview_set_internal {
    struct bu_ptbl views;
    struct bu_ptbl shared_db_objs;
    struct bu_ptbl shared_view_objs;

    struct bv_scene_obj  *free_scene_obj;
    struct bu_list vlfree;
};

struct bv_scene_obj_internal {
    std::unordered_map<struct bview *, struct bv_scene_obj *> vobjs;
};

#endif /* LIBBV_BV_PRIVATE_H */

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
