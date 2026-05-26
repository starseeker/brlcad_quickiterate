/*                      H A S H . C
 * BRL-CAD
 *
 * Copyright (c) 2020-2026 United States Government as represented by
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
/** @file hash.c
 *
 * Calculate hashes of bv containers
 *
 */

#include "common.h"
#include <string.h>
#include "vmath.h"
#include "bu/hash.h"
#include "bu/log.h"
#include "bn/mat.h"
#include "bsg/vlist.h"
#include "bsg/defines.h"
#include "bsg/util.h"
#include "bsg/view_sets.h"

static void
_bv_adc_state_hash(struct bu_data_hash_state *state, struct bv_adc_state *v)
{
    /* First, do sanity checks */
    if (!v || !state)
	return;

    bu_data_hash_update(state, v, sizeof(struct bv_adc_state));
}

static void
_bv_axes_hash(struct bu_data_hash_state *state, struct bv_axes *v)
{
    /* First, do sanity checks */
    if (!v || !state)
	return;

    bu_data_hash_update(state, v, sizeof(struct bv_axes));
}

/* Phase T-final (drawing_stack_modernization): the legacy gv_tcl per-state
 * hash helpers (_bv_data_arrow_state_hash, _bv_data_axes_state_hash,
 * _bv_data_label_state_hash, _bv_data_line_state_hash, _bg_poly_contour_hash,
 * _bg_polygon_hash, _bg_polygons_hash, _bv_data_polygon_state_hash) were
 * removed.  After T1, the same renderable state is hashed indirectly via
 * the BSG view-scope visit below (each `_tcl_*` BSG object's vlist is
 * hashed by _bv_hash_view_obj_cb). */

static void
_bv_grid_state_hash(struct bu_data_hash_state *state, struct bv_grid_state *v)
{
    /* First, do sanity checks */
    if (!v || !state)
	return;

    bu_data_hash_update(state, v, sizeof(struct bv_grid_state));
}

static void
_bv_params_state_hash(struct bu_data_hash_state *state, struct bv_params_state *v)
{
    /* First, do sanity checks */
    if (!v || !state)
	return;

    bu_data_hash_update(state, v, sizeof(struct bv_params_state));
}

static void
_bv_other_state_hash(struct bu_data_hash_state *state, struct bv_other_state *v)
{
    /* First, do sanity checks */
    if (!v || !state)
	return;

    bu_data_hash_update(state, v, sizeof(struct bv_other_state));
}


static void
_bv_interactive_rect_state_hash(struct bu_data_hash_state *state, struct bv_interactive_rect_state *v)
{
    /* First, do sanity checks */
    if (!v || !state)
	return;

    bu_data_hash_update(state, v, sizeof(struct bv_interactive_rect_state));
}

static void
_bv_obj_settings_hash(struct bu_data_hash_state *state, struct bv_obj_settings *v)
{
    /* First, do sanity checks */
    if (!v || !state)
	return;

    bu_data_hash_update(state, v, sizeof(struct bv_obj_settings));
}

void
bv_scene_obj_hash(struct bu_data_hash_state *state, struct bv_scene_obj *s)
{
    /* First, do sanity checks */
    if (!s || !state)
	return;

    bu_data_hash_update(state, s, sizeof(struct bv_scene_obj));
    struct bsg_vlist *tvp;
    for (BU_LIST_FOR(tvp, bsg_vlist, &((struct bsg_vlist *)&s->s_vlist)->l)) {
	bu_data_hash_update(state, tvp, sizeof(struct bsg_vlist));
    }
    if (s->s_os)
	_bv_obj_settings_hash(state, s->s_os);
    _bv_obj_settings_hash(state, &s->s_local_os);
}

void
bv_settings_hash(struct bu_data_hash_state *state, struct bview_settings *s)
{
    bu_data_hash_update(state, s, sizeof(struct bview_settings));

    _bv_adc_state_hash(state, &s->gv_adc);
    _bv_axes_hash(state, &s->gv_model_axes);
    _bv_axes_hash(state, &s->gv_view_axes);
    _bv_grid_state_hash(state, &s->gv_grid);
    _bv_other_state_hash(state, &s->gv_center_dot);
    _bv_params_state_hash(state, &s->gv_view_params);
    _bv_other_state_hash(state, &s->gv_view_scale);
    _bv_interactive_rect_state_hash(state, &s->gv_rect);

#if 0
    for (size_t i = 0; i < BU_PTBL_LEN(v->gv_selected); i++) {
	long *p = BU_PTBL_GET(v->gv_selected, i);
	bu_data_hash_update(state, p, sizeof(long *));
    }
#endif

}

/* Phase B: callback for bv_view_objs_visit_db in bv_hash. */
static int
_bv_hash_db_obj_cb(struct bv_scene_obj *s, void *data)
{
    struct bu_data_hash_state *state = (struct bu_data_hash_state *)data;
    /* Hash children first (view-specific adaptive objects) */
    if (BU_PTBL_IS_INITIALIZED(&s->children)) {
	for (size_t j = 0; j < BU_PTBL_LEN(&s->children); j++) {
	    struct bv_scene_obj *s_c = (struct bv_scene_obj *)BU_PTBL_GET(&s->children, j);
	    bv_scene_obj_hash(state, s_c);
	}
    }
    bv_scene_obj_hash(state, s);
    return 1;
}

/* Phase A0 (drawing_stack_modernization): callback for view-only object
 * hashing.  Walks each visited object's children (mirroring the legacy
 * BV_VIEW_OBJS scan) and then hashes the object itself. */
static int
_bv_hash_view_obj_cb(struct bv_scene_obj *s, void *data)
{
    struct bu_data_hash_state *state = (struct bu_data_hash_state *)data;
    if (BU_PTBL_IS_INITIALIZED(&s->children)) {
	for (size_t j = 0; j < BU_PTBL_LEN(&s->children); j++) {
	    struct bv_scene_obj *s_c = (struct bv_scene_obj *)BU_PTBL_GET(&s->children, j);
	    bv_scene_obj_hash(state, s_c);
	}
    }
    bv_scene_obj_hash(state, s);
    return 1;
}

unsigned long long
bv_hash(struct bview *v)
{
    if (!v)
	return 0;

    struct bu_data_hash_state *state = bu_data_hash_create();
    if (!state)
	return 0;

    // Deliberately not checking name - a rename doesn't change the view
    bu_data_hash_update(state, v, sizeof(struct bview));

    if (v->gv_s)
	bv_settings_hash(state, v->gv_s);
    bv_settings_hash(state, &v->gv_ls);

    /* Phase T-final (drawing_stack_modernization): the Tcl-specific
     * gv_tcl block (gv_data_arrows / gv_data_axes / gv_data_labels /
     * gv_data_lines / gv_data_polygons and their sdata twins, plus
     * gv_prim_labels) is no longer hashed here.  After T1 these are
     * mirrored into BSG VIEW_SCOPE objects (`_tcl_data_*` /
     * `_tcl_sdata_*`) and are picked up by the bv_view_obj_visit walk
     * below.  BSG is now the source of truth for renderable view
     * adornment state. */

    /* Phase A0 (drawing_stack_modernization): use bv_view_obj_visit so we
     * walk the BSG view-scope subtree directly rather than the legacy
     * BV_VIEW_OBJS ptbl.  Both shared and local scopes are covered by
     * BV_VIEW_OBJ_SCOPE_ALL. */
    bv_view_obj_visit(v, BV_VIEW_OBJ_SCOPE_ALL, _bv_hash_view_obj_cb, state);

    /* Hash DB-derived objects via the BSG-aware helper */
    bv_view_objs_visit_db(v, _bv_hash_db_obj_cb, state);

    unsigned long long hash_val = bu_data_hash_val(state);
    bu_data_hash_destroy(state);

    return hash_val;
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
