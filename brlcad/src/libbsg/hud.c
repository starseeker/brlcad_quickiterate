/*                         H U D . C
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
/** @file libbsg/hud.c
 *
 * Phase D4 (drawing_modernization): per-view HUD root and overlay metadata.
 *
 * bsg_hud_root_create() allocates a lightweight BSG_NODE_GROUP root plus one
 * BSG_NODE_SHAPE child per faceplate feature.  Each child carries a
 * bsg_hud_node_meta (stored as s_i_data) that records the feature type,
 * coordinate space, overlay role, lifecycle, and render-phase sort order.
 *
 * bsg_hud_sync() reads the bsg_view_settings faceplate flags and sets each
 * child's s_flag to UP (enabled) or DOWN (disabled).  Actual rasterisation
 * remains in libdm; libbsg only manages structure and ordering.
 */

#include "common.h"

#include <string.h>

#include "bu/malloc.h"
#include "bu/vls.h"
#include "bu/ptbl.h"

#include "bsg/defines.h"
#include "bsg/faceplate.h"
#include "bsg/node.h"
#include "bsg/payload.h"
#include "bsg/util.h"
#include "bsg/hud.h"


/* -----------------------------------------------------------------------
 * Feature descriptor table (one row per bsg_hud_feature_type enum value).
 * Entries are ordered so that the array index matches the feature enum value
 * and also the default sort_order.
 * ----------------------------------------------------------------------- */

struct hud_feature_desc {
    bsg_hud_feature_type  type;
    bsg_hud_coord         coord_space;
    bsg_overlay_role      role;
    bsg_overlay_class     overlay_class;
    bsg_overlay_lifecycle lifecycle;
    const char           *name;
};

static const struct hud_feature_desc _hud_features[BSG_HUD_FEATURE_COUNT] = {
    { BSG_HUD_FEATURE_CENTER_DOT,  BSG_HUD_COORD_NDC,            BSG_OVERLAY_ROLE_SCREEN, BSG_OVERLAY_CLASS_FACEPLATE,             BSG_OVERLAY_LC_PERSISTENT,      "_hud_center_dot"  },
    { BSG_HUD_FEATURE_MODEL_AXES,  BSG_HUD_COORD_MODEL_ANCHORED, BSG_OVERLAY_ROLE_SCREEN, BSG_OVERLAY_CLASS_DIAGNOSTIC,            BSG_OVERLAY_LC_PER_VIEW,        "_hud_model_axes"  },
    { BSG_HUD_FEATURE_VIEW_AXES,   BSG_HUD_COORD_VIEW_PLANE,     BSG_OVERLAY_ROLE_SCREEN, BSG_OVERLAY_CLASS_DIAGNOSTIC,            BSG_OVERLAY_LC_PER_VIEW,        "_hud_view_axes"   },
    { BSG_HUD_FEATURE_VIEW_SCALE,  BSG_HUD_COORD_NDC,            BSG_OVERLAY_ROLE_SCREEN, BSG_OVERLAY_CLASS_DIAGNOSTIC,            BSG_OVERLAY_LC_PERSISTENT,      "_hud_view_scale"  },
    { BSG_HUD_FEATURE_ADC,         BSG_HUD_COORD_VIEW_PLANE,     BSG_OVERLAY_ROLE_SCREEN, BSG_OVERLAY_CLASS_DIAGNOSTIC,            BSG_OVERLAY_LC_PER_VIEW,        "_hud_adc"         },
    { BSG_HUD_FEATURE_GRID,        BSG_HUD_COORD_MODEL_ANCHORED, BSG_OVERLAY_ROLE_MODEL,  BSG_OVERLAY_CLASS_DIAGNOSTIC,            BSG_OVERLAY_LC_PER_VIEW,        "_hud_grid"        },
    { BSG_HUD_FEATURE_RECT,        BSG_HUD_COORD_SCREEN_PX,      BSG_OVERLAY_ROLE_SCREEN, BSG_OVERLAY_CLASS_SELECTION_RUBBER_BAND, BSG_OVERLAY_LC_PER_FRAME,       "_hud_rect"        },
    { BSG_HUD_FEATURE_VIEW_PARAMS, BSG_HUD_COORD_NDC,            BSG_OVERLAY_ROLE_SCREEN, BSG_OVERLAY_CLASS_DIAGNOSTIC,            BSG_OVERLAY_LC_PER_FRAME,       "_hud_view_params" },
    { BSG_HUD_FEATURE_FRAMEBUFFER, BSG_HUD_COORD_SCREEN_PX,      BSG_OVERLAY_ROLE_SCREEN, BSG_OVERLAY_CLASS_DIAGNOSTIC,            BSG_OVERLAY_LC_SHARED_VIEW_SET, "_hud_framebuffer" }
};


/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/* Allocate and populate a bsg_hud_node_meta for feature index i. */
static struct bsg_hud_node_meta *
_meta_alloc(int i)
{
    struct bsg_hud_node_meta *m;
    BU_ALLOC(m, struct bsg_hud_node_meta);
    m->feature_type = _hud_features[i].type;
    m->coord_space  = _hud_features[i].coord_space;
    m->role         = _hud_features[i].role;
    m->overlay_class = _hud_features[i].overlay_class;
    m->lifecycle    = _hud_features[i].lifecycle;
    m->sort_order   = i; /* default: index == phase order */
    return m;
}

static struct bsg_hud_payload *
_payload_alloc(int i)
{
    struct bsg_hud_payload *p;
    BU_ALLOC(p, struct bsg_hud_payload);
    p->feature_type = _hud_features[i].type;
    return p;
}

/* Free the bsg_hud_node_meta stored as internal data on a node. */
static void
_meta_free_cb(struct bsg_node *node)
{
    if (!node)
	return;
    struct bsg_hud_node_meta *m = (struct bsg_hud_node_meta *)bsg_node_get_internal_data(node);
    if (m) {
	bu_free(m, "bsg_hud_node_meta");
	bsg_node_set_internal_data(node, NULL);
    }
    struct bsg_hud_payload *p = (struct bsg_hud_payload *)bsg_node_get_draw_data(node);
    if (p) {
	bu_free(p, "bsg_hud_payload");
	bsg_node_set_draw_data(node, NULL);
    }
}


/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

bsg_node *
bsg_hud_root_create(struct bsg_view *v)
{
    if (!v)
	return NULL;

    if (v->gv_hud_root)
	return (bsg_node *)v->gv_hud_root;

    /* Allocate the root group as an unregistered node (not tracked in the
     * view's gv_objs pool, just like the draw root). */
    bsg_node *root = bsg_obj_get_unregistered(v, BSG_OBJ_CHILD);
    if (!root)
	return NULL;

    root->s_type_flags = BSG_NODE_GROUP;
    root->s_flag       = UP;
    root->parent       = NULL;
    bu_vls_sprintf(&root->s_name, "_hud_root");

    /* Pre-allocate one BSG_NODE_SHAPE child per feature, all initially
     * disabled (s_flag = DOWN).  Children are appended in sort_order. */
    for (int i = 0; i < BSG_HUD_FEATURE_COUNT; i++) {
	bsg_node *child = bsg_node_create(v, BSG_NODE_SHAPE);
	if (!child) {
	    /* Partial construction — leak the already-created children rather
	     * than attempting a complicated teardown.  bsg_hud_root_destroy
	     * will clean up later if the caller holds the root pointer. */
	    bu_log("bsg_hud_root_create: failed to allocate feature node %d\n", i);
	    continue;
	}

	child->s_flag = DOWN;
	bsg_node_set_payload_type(child, BSG_PAYLOAD_OVERLAY);
	bu_vls_sprintf(&child->s_name, "%s", _hud_features[i].name);

	struct bsg_hud_node_meta *m = _meta_alloc(i);
	struct bsg_hud_payload *p = _payload_alloc(i);
	bsg_node_set_internal_data(child, m);
	bsg_node_set_draw_data(child, p);
	child->s_free_callback = _meta_free_cb;

	bsg_node_add_child(root, child);
    }

    v->gv_hud_root = root;
    return root;
}


bsg_node *
bsg_hud_root_get(struct bsg_view *v)
{
    if (!v)
	return NULL;
    return (bsg_node *)v->gv_hud_root;
}


void
bsg_hud_root_destroy(struct bsg_view *v)
{
    if (!v || !v->gv_hud_root)
	return;

    bsg_node *root = (bsg_node *)v->gv_hud_root;
    v->gv_hud_root = NULL;

    /* Destroy all children first. */
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	bsg_node *child = (bsg_node *)BU_PTBL_GET(&root->children, i);
	if (child)
	    bsg_node_destroy(child);
    }

    /* Free the root itself.  It was allocated via bsg_obj_get_unregistered
     * (not inserted into a ptbl), so release via bsg_obj_put. */
    bsg_obj_put(root);
}


int
bsg_hud_sync(struct bsg_view *v)
{
    if (!v)
	return -1;

    /* Auto-create on first sync. */
    if (!v->gv_hud_root) {
	if (!bsg_hud_root_create(v))
	    return -1;
    }

    bsg_node *root = (bsg_node *)v->gv_hud_root;

    /* Resolve the settings block (may be shared or local). */
    struct bsg_view_settings *s = v->gv_s ? v->gv_s : &v->gv_ls;

    /* Walk children; each corresponds to one feature in sort_order. */
    if (BU_PTBL_LEN(&root->children) < BSG_HUD_FEATURE_COUNT)
	return -1;

    for (int i = 0; i < BSG_HUD_FEATURE_COUNT; i++) {
	bsg_node *child = (bsg_node *)BU_PTBL_GET(&root->children, (size_t)i);
	if (!child)
	    continue;

	struct bsg_hud_node_meta *m = (struct bsg_hud_node_meta *)bsg_node_get_internal_data(child);
	struct bsg_hud_payload *p = (struct bsg_hud_payload *)bsg_node_get_draw_data(child);
	if (!m)
	    continue;
	if (!p)
	    continue;

	int enabled = 0;
	switch (m->feature_type) {
	    case BSG_HUD_FEATURE_CENTER_DOT:
		enabled = s->gv_center_dot.gos_draw;
		memcpy(&p->data.other, &s->gv_center_dot, sizeof(struct bsg_other_state));
		break;
	    case BSG_HUD_FEATURE_MODEL_AXES:
		enabled = s->gv_model_axes.draw;
		memcpy(&p->data.axes, &s->gv_model_axes, sizeof(struct bsg_axes));
		break;
	    case BSG_HUD_FEATURE_VIEW_AXES:
		enabled = s->gv_view_axes.draw;
		memcpy(&p->data.axes, &s->gv_view_axes, sizeof(struct bsg_axes));
		break;
	    case BSG_HUD_FEATURE_VIEW_SCALE:
		enabled = s->gv_view_scale.gos_draw;
		memcpy(&p->data.other, &s->gv_view_scale, sizeof(struct bsg_other_state));
		break;
	    case BSG_HUD_FEATURE_ADC:
		enabled = s->gv_adc.draw;
		memcpy(&p->data.adc, &s->gv_adc, sizeof(struct bsg_adc_state));
		break;
	    case BSG_HUD_FEATURE_GRID:
		enabled = s->gv_grid.draw;
		memcpy(&p->data.grid, &s->gv_grid, sizeof(struct bsg_grid_state));
		break;
	    case BSG_HUD_FEATURE_RECT:
		enabled = (s->gv_rect.draw && s->gv_rect.line_width > 0);
		memcpy(&p->data.rect, &s->gv_rect, sizeof(struct bsg_interactive_rect_state));
		break;
	    case BSG_HUD_FEATURE_VIEW_PARAMS:
		enabled = s->gv_view_params.draw;
		memcpy(&p->data.params, &s->gv_view_params, sizeof(struct bsg_params_state));
		break;
	    case BSG_HUD_FEATURE_FRAMEBUFFER:
		enabled = (s->gv_fb_mode != 0);
		p->data.framebuffer.mode = s->gv_fb_mode;
		break;
	    default:
		break;
	}

	child->s_flag = enabled ? UP : DOWN;
    }

    return 0;
}


struct bsg_hud_node_meta *
bsg_hud_node_get_meta(bsg_node *node)
{
    if (!node)
	return NULL;
    if (!(bsg_node_get_payload_type(node) & BSG_PAYLOAD_OVERLAY))
	return NULL;
    return (struct bsg_hud_node_meta *)bsg_node_get_internal_data(node);
}


const struct bsg_hud_payload *
bsg_hud_node_get_payload(bsg_node *node)
{
    if (!node)
	return NULL;
    if (!(bsg_node_get_payload_type(node) & BSG_PAYLOAD_OVERLAY))
	return NULL;
    return (const struct bsg_hud_payload *)bsg_node_get_draw_data(node);
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
