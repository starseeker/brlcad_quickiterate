/*                B S G _ G E D _ D R A W . C
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
/** @file libged/bsg_ged_draw.c
 *
 * Phase 7 Layer A (drawing-stack modernization).
 *
 * The drawn set is now a BSG_NODE_GROUP tree:
 *
 *   gd_draw_root (BSG_NODE_GROUP)
 *     └─ subgroup per top-level path component (BSG_NODE_GROUP)
 *          ├─ s_name  = single directory component ("all.g")
 *          ├─ dp      = (void *)(struct directory *) dir entry
 *          ├─ parent  = draw root (or containing sub-group)
 *          └─ children: sub-groups (BSG_NODE_GROUP) or shapes (BSG_NODE_SHAPE)
 *               │  s_name = path component, parent = containing group
 *               └─ BSG_NODE_SHAPE bv_scene_obj leaves
 *                    └─ parent = containing sub-group
 *
 * Group nodes are allocated via bv_obj_create(v, BV_CHILD_OBJS) so
 * they are NOT inserted into any view object table.  Shape nodes are
 * allocated via bv_obj_get_unregistered(v, BV_DB_OBJS) — they have
 * s_type_flags = BV_DB_OBJS but are NOT inserted into any gv_objs ptbl.
 * The BSG tree (gd_draw_root) is the sole index for rendering and
 * iteration (bv_view_objs_visit_db).
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/color.h"
#include "bv/lod.h"
#include "bv/plot3.h"
#include "bg/clip.h"
#include "bsg/defines.h"
#include "bsg/appearance.h"
#include "bsg/draw_ctx.h"
#include "bsg/draw_set.h"
#include "bsg/field.h"
#include "bsg/identity.h"
#include "bsg/lod_ops.h"
#include "bsg/material.h"
#include "bsg/node.h"
#include "bsg/overlay.h"
#include "bsg/payload.h"
#include "bsg/selection.h"
#include "bsg/sensor.h"
#include "bsg/visit.h"

#include "bv/view_sets.h"

#include "ged.h"
#include "ged/bsg_ged_draw.h"
#include "./ged_private.h"

/* ------------------------------------------------------------------ */
/* Internal macros                                                     */
/* ------------------------------------------------------------------ */

#define FIRST_SOLID(_bdata)  ((_bdata)->s_fullpath.fp_names[0])
#define FREE_BV_SCENE_OBJ(p, fp, vlf) { \
        BU_LIST_APPEND(fp, &((p)->l)); \
        BV_FREE_VLIST(vlf, &((p)->s_vlist)); }

static void
_bsg_draw_root_identity_assign(struct bv_scene_obj *root)
{
    struct bsg_identity id;

    if (!root)
	return;

    bsg_identity_from_path_str(&id, "_draw_root", BSG_SOURCE_GENERATED);
    bsg_node_identity_set((bsg_node *)root, &id);
}

/* Thin wrapper: delegates to bsg_bump_rev_node() in libbsg/draw_set.c.
 * Phase 7 Step 11: the implementation moved to libbsg so that the free-group
 * helpers (also in libbsg) can bump the revision counter without carrying
 * a struct ged * pointer.
 */
static void
_sg_bump_rev_node(struct bv_scene_obj *n)
{
    bsg_bump_rev_node((bsg_node *)n);
}

/* defined in draw_calc.cpp */
extern fastf_t brep_est_avg_curve_len(struct rt_brep_internal *bi);
extern void createDListSolid(struct bv_scene_obj *sp);
extern int csg_wireframe_update(struct bv_scene_obj *vo, struct bview *v, int flag);

struct ged_lod_vstate {
    struct bview *v;
    int adaptive_on;
};

struct ged_lod_state {
    struct bv_scene_obj *s;
    struct ged_lod_vstate *vstates;
    size_t vcnt;
    size_t vcap;
};

static int
_lod_state_adaptive_index(struct ged_lod_state *st, struct bview *v, size_t *idx)
{
    if (!st || !v || !idx)
	return 0;
    *idx = 0;

    for (size_t i = 0; i < st->vcnt; i++) {
	if (st->vstates[i].v == v) {
	    *idx = i;
	    return 1;
	}
    }

    return 0;
}

static int
_lod_state_adaptive_get(struct ged_lod_state *st, struct bview *v, int *known)
{
    if (known)
	*known = 0;
    if (!st || !v)
	return 0;

    size_t idx = 0;
    if (!_lod_state_adaptive_index(st, v, &idx))
	return 0;
    if (known)
	*known = 1;

    return st->vstates[idx].adaptive_on;
}

static void
_lod_state_adaptive_set(struct ged_lod_state *st, struct bview *v, int adaptive_on)
{
    if (!st || !v)
	return;

    size_t idx = 0;
    if (_lod_state_adaptive_index(st, v, &idx)) {
	st->vstates[idx].adaptive_on = adaptive_on ? 1 : 0;
	return;
    }

    if (st->vcnt >= st->vcap) {
	size_t ncap = (st->vcap < 4) ? 4 : (st->vcap * 2);
	struct ged_lod_vstate *nstates = (struct ged_lod_vstate *)bu_realloc(st->vstates, ncap * sizeof(struct ged_lod_vstate), "lod_state_vstates");
	if (!nstates)
	    return;
	st->vstates = nstates;
	st->vcap = ncap;
    }

    st->vstates[st->vcnt].v = v;
    st->vstates[st->vcnt].adaptive_on = adaptive_on ? 1 : 0;
    st->vcnt++;
}

static int
_mesh_lod_select_level(bsg_node *node, struct bview *v)
{
    struct bsg_lod_payload *pl = (struct bsg_lod_payload *)bsg_node_user_data_get(node);
    if (!pl || !pl->user_data || !v)
	return 0;
    struct ged_lod_state *st = (struct ged_lod_state *)pl->user_data;
    if (!st->s || !st->s->draw_data)
	return 0;
    bv_mesh_lod_view(st->s, v, 0);
    return 0;
}

static void
_lod_activate_level(bsg_node *node, struct bview *v, int level)
{
    struct bsg_lod_view_cursor *c = bsg_lod_node_get_cursor(node, v);
    if (!c || !v)
	return;
    c->level = level;
    c->view_scale = v->gv_scale;
    c->perspective_flag = (SMALL_FASTF < v->gv_perspective) ? 1 : 0;
    c->last_frame_rev = v->gv_frame_rev;
}

static int
_lod_is_stale(bsg_node *node, struct bview *v)
{
    struct bsg_lod_view_cursor *c = bsg_lod_node_get_cursor(node, v);
    if (!c || !v)
	return 0;
    if (c->level < 0)
	return 1;
    if (!ZERO(c->view_scale - v->gv_scale))
	return 1;
    if (c->perspective_flag != ((SMALL_FASTF < v->gv_perspective) ? 1 : 0))
	return 1;
    return 0;
}

static void
_lod_state_free(bsg_node *node, const char *alloc_label)
{
    struct bsg_lod_payload *pl = (struct bsg_lod_payload *)bsg_node_user_data_get(node);
    if (!pl || !pl->user_data)
	return;
    struct ged_lod_state *st = (struct ged_lod_state *)pl->user_data;
    if (st->vstates)
	bu_free(st->vstates, "lod_state_vstates");
    bu_free(pl->user_data, alloc_label);
}

static void
_mesh_lod_free(bsg_node *node)
{
    _lod_state_free(node, "_mesh_lod_state");
}

static struct bsg_lod_ops _mesh_lod_ops = {
    _mesh_lod_select_level,
    _lod_activate_level,
    _lod_is_stale,
    _mesh_lod_free
};

static int
_csg_lod_select_level(bsg_node *node, struct bview *v)
{
    struct bsg_lod_payload *pl = (struct bsg_lod_payload *)bsg_node_user_data_get(node);
    if (!pl || !pl->user_data || !v)
	return 0;
    struct ged_lod_state *st = (struct ged_lod_state *)pl->user_data;
    if (!st->s || !bsg_node_user_data_get((const bsg_node *)st->s))
	return 0;
    csg_wireframe_update(st->s, v, 0);
    return 0;
}

static void
_csg_lod_free(bsg_node *node)
{
    _lod_state_free(node, "_csg_lod_state");
}

static struct bsg_lod_ops _csg_lod_ops = {
    _csg_lod_select_level,
    _lod_activate_level,
    _lod_is_stale,
    _csg_lod_free
};

int
ged_lod_install_mesh_ops(struct bv_scene_obj *lod, struct bv_scene_obj *s)
{
    if (!lod || !s)
	return -1;
    struct ged_lod_state *st;
    BU_GET(st, struct ged_lod_state);
    st->s = s;
    st->vstates = NULL;
    st->vcnt = 0;
    st->vcap = 0;
    bsg_lod_node_set_ops((bsg_node *)lod, &_mesh_lod_ops, (void *)st);
    return 0;
}

int
ged_lod_install_csg_ops(struct bv_scene_obj *lod, struct bv_scene_obj *s)
{
    if (!lod || !s)
	return -1;
    struct ged_lod_state *st;
    BU_GET(st, struct ged_lod_state);
    st->s = s;
    st->vstates = NULL;
    st->vcnt = 0;
    st->vcap = 0;
    bsg_lod_node_set_ops((bsg_node *)lod, &_csg_lod_ops, (void *)st);
    return 0;
}

int
ged_lod_adaptive_toggle_sync(struct bv_scene_obj *lod, struct bview *v, int adaptive_on)
{
    if (!lod || !v || !bsg_node_has_kind((const bsg_node *)lod, BSG_NODE_LOD))
	return 0;

    struct bsg_lod_payload *pl = (struct bsg_lod_payload *)bsg_node_user_data_get((const bsg_node *)lod);
    if (!pl || !pl->user_data)
	return 0;

    struct ged_lod_state *st = (struct ged_lod_state *)pl->user_data;
    int known = 0;
    int prev = _lod_state_adaptive_get(st, v, &known);
    _lod_state_adaptive_set(st, v, adaptive_on);
    if (!known || prev != adaptive_on) {
	struct bsg_lod_view_cursor *c = bsg_lod_node_get_cursor((bsg_node *)lod, v);
	if (c)
	    c->level = -1;
	return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* BSG group-tree helpers (file-private)                               */
/* ------------------------------------------------------------------ */

/*
 * Lazily create (on first draw) and return the per-GED draw root.
 * Also stores the root in gedp->ged_gvp->gv_draw_root and
 * gedp->ged_gvp->bsg_root (Phase F alias) so that the BSG render
 * path in dm_draw_objs can traverse it directly (Phase 7 step 7 A3).
 */
static struct bv_scene_obj *
_sg_root(struct ged *gedp)
{
    bsg_identity_enable_view_obj_derivation();

    if (gedp->i->ged_gdp->gd_draw_root) {
	struct bview *v = gedp->ged_gvp;
	_bsg_draw_root_identity_assign(gedp->i->ged_gdp->gd_draw_root);
	if (v) {
	    /* Phase F aliasing: bsg_root and gv_draw_root intentionally point to
	     * the same shared draw-tree root for the active GED view. */
	    v->gv_draw_root = gedp->i->ged_gdp->gd_draw_root;
	    v->bsg_root = gedp->i->ged_gdp->gd_draw_root;
	}
        return gedp->i->ged_gdp->gd_draw_root;
    }

    struct bview *v = gedp->ged_gvp;
    if (!v)
        return NULL;

    struct bv_scene_obj *root = bv_obj_create(v, BV_CHILD_OBJS);
    if (!root)
        return NULL;

    bsg_node_set_kind((bsg_node *)root, BSG_NODE_GROUP);
    root->s_flag = UP;
    bsg_node_set_name((bsg_node *)root, "_draw_root");

    gedp->i->ged_gdp->gd_draw_root = root;

    /* Phase 7 Step 10: store a bsg_draw_ctx in root->s_i_data so that
     * freeing helpers can bump gd_draw_rev without carrying gedp.
     * Phase 7 Step 11: also store the free-object pool pointer (fso) so
     * that bsg_free_group_contents / bsg_free_children_recursive can
     * recycle nodes without calling bv_set_fsos (which needs gedp). */
    gedp->i->ged_gdp->bsg_ctx.draw_rev = &gedp->i->ged_gdp->gd_draw_rev;
    gedp->i->ged_gdp->bsg_ctx.fso      = bv_set_fsos(&gedp->ged_views);
    bsg_node_user_data_set((bsg_node *)root, &gedp->i->ged_gdp->bsg_ctx);

    /* A3: register in the view so that the BSG render loop can traverse the
     * draw tree directly without reading gv_objs (Phase 7 step 7 A3).
     * Phase F: bsg_root is an alias for gv_draw_root — same pointer, same
     * children list, maintained live by draw/erase mutations.  No per-frame
     * bsg_scene_root_sync rebuild is needed. */
    v->gv_draw_root = root;
    v->bsg_root = root;
    _bsg_draw_root_identity_assign(root);

    return root;
}



/*
 * Thin wrappers for overlay-group management.
 *
 * Phase 7 Step 13: the pure BSG tree logic has moved to libbsg/overlay.c
 * (bsg_find_overlay_group, bsg_ensure_overlay_group, bsg_erase_overlay_by_name).
 * These wrappers supply the GED context (draw root pointer, view pointer)
 * without requiring libbsg to know about struct ged.
 */
static struct bv_scene_obj *
_sg_overlay_root(struct ged *gedp)
{
    struct bv_scene_obj *root = _sg_root(gedp);
    if (!root)
        return NULL;
    return (struct bv_scene_obj *)bsg_ensure_overlay_group(
        (bsg_node *)root, gedp->ged_gvp);
}

static void
_sg_erase_overlay_by_name(struct ged *gedp, const char *name)
{
    bsg_erase_overlay_by_name(
        (bsg_node *)gedp->i->ged_gdp->gd_draw_root, name);
}

/*
 * Per-solid s_free_callback: clear the GED illumination tracker if this solid
 * is currently registered as the illuminated solid.
 *
 * This fires both when the BSG freeing path explicitly calls it before
 * FREE_BV_SCENE_OBJ, and again (harmlessly) during pool teardown in bv_free.
 * The second call is a no-op because gd_illum_solid will already be NULL or
 * pointing to a different solid.
 *
 * Registered at shape-creation time on each BSG_NODE_SHAPE node (Phase 7
 * Step 9).  Replaces the file-private _sg_clear_illum_if_match(gedp, sp)
 * call pattern, removing the gedp dependency from the BSG freeing path for
 * the illumination-clear concern.
 *
 * Phase 9.3: when the freed solid was the illuminated one, also tear down
 * the NodeSensor that was tracking its field changes and bump
 * gd_illum_rev so observers see the highlight-state transition.
 */
void
ged_bv_illum_free_cb(struct bv_scene_obj *sp)
{
    if (!sp->s_u_data)
        return;
    struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
    if (!bdata->gedp)
        return;
    struct ged_drawable *gdp = bdata->gedp->i->ged_gdp;
    if (gdp->gd_illum_sensor
        && bsg_sensor_target((bsg_node *)gdp->gd_illum_sensor) == (bsg_node *)sp) {
        bsg_sensor_destroy((bsg_node *)gdp->gd_illum_sensor);
        gdp->gd_illum_sensor = NULL;
        gdp->gd_illum_rev++;
    }
}


static void
_sg_free_group_contents(struct bv_scene_obj *g)
{
    bsg_free_group_contents((bsg_node *)g);
}


/*
 * Free a subgroup: free its descendants, then free the group node itself,
 * removing it from its parent.
 *
 * Phase 7 Step 11: delegates to bsg_free_group() in libbsg.
 */
static void
_sg_free_group(struct bv_scene_obj *g)
{
    bsg_free_group((bsg_node *)g);
}


/*
 * Return the depth of @p g in the draw tree (root = 0, root children = 1, …).
 * Delegates to bsg_draw_tree_depth() in libbsg (Phase 7 step 7 C1/C3).
 */
static int
_sg_tree_depth(const struct bv_scene_obj *g)
{
    return bsg_draw_tree_depth((const bsg_node *)g);
}


/*
 * Find or create a BSG_NODE_GROUP child of @p parent named @p comp_name.
 * Returns the (possibly new) child group, or NULL on failure.
 *
 * The GED-specific db_lookup() call belongs here (libged); the pure tree
 * manipulation delegates to bsg_group_ensure_child() in libbsg (C1/C3).
 * The revision bump must happen after the child is created so we keep it
 * here where gedp is available.
 */
static struct bv_scene_obj *
_sg_find_or_create_child_group(struct ged *gedp, struct bv_scene_obj *parent,
                                const char *comp_name)
{
    /* Fast path: already exists (no rev bump needed) */
    bsg_node *existing = bsg_group_find_child((bsg_node *)parent, comp_name);
    if (existing)
        return (struct bv_scene_obj *)existing;

    struct bview *v = gedp->ged_gvp;
    if (!v)
        return NULL;

    /* Resolve the db directory entry (GED-specific — stays in libged) */
    struct directory *dp = db_lookup(gedp->dbip, comp_name, LOOKUP_QUIET);

    /* Pure tree creation delegates to libbsg */
    bsg_node *child = bsg_group_ensure_child((bsg_node *)parent, v,
                                              comp_name, (void *)dp);
    if (!child)
        return NULL;

    _sg_bump_rev_node(parent);
    return (struct bv_scene_obj *)child;
}


/*
 * Navigate/create per-component group nodes for @p name under the draw root.
 * Returns the deepest (leaf) group node corresponding to the drawn path,
 * or NULL if any component cannot be resolved.
 */
static struct bv_scene_obj *
_sg_add_path(struct ged *gedp, const char *name)
{
    struct bv_scene_obj *root = _sg_root(gedp);
    if (!root)
        return NULL;
    struct bv_scene_obj *base = root;
    if (gedp->ged_gvp && bv_view_is_independent(gedp->ged_gvp)) {
	struct bv_scene_obj *scope = bv_view_independent_scope(gedp->ged_gvp, 1);
	if (scope)
	    base = scope;
    }

    struct db_i *dbip = gedp->dbip;

    /* Parse full path into directory components */
    struct db_full_path pathcomp;
    if (db_string_to_path(&pathcomp, dbip, name) != 0) {
        /* Fallback: single-name lookup */
        const char *cp = strrchr(name, '/');
        cp = cp ? cp + 1 : name;
        struct directory *dp = db_lookup(dbip, cp, LOOKUP_NOISY);
        if (dp == RT_DIR_NULL)
            return NULL;
        return _sg_find_or_create_child_group(gedp, base, cp);
    }

    if (pathcomp.fp_len == 0) {
        db_free_full_path(&pathcomp);
        return NULL;
    }

    /* Navigate/create one group node per path component starting from root */
    struct bv_scene_obj *cur = base;
    for (size_t i = 0; i < pathcomp.fp_len; i++) {
        const char *comp = pathcomp.fp_names[i]->d_namep;
        struct bv_scene_obj *child =
            _sg_find_or_create_child_group(gedp, cur, comp);
        if (!child) {
            db_free_full_path(&pathcomp);
            return NULL;
        }
        cur = child;
    }

    db_free_full_path(&pathcomp);
    return cur;
}


/* ------------------------------------------------------------------ */
/* Erase helpers — tree-navigation based, no split logic              */
/* ------------------------------------------------------------------ */

struct _sg_path_match_ctx {
    struct db_full_path *subpath;
    struct bsg_identity subpath_id;
};

/*
 * Path-match callback for bsg_erase_nested_subpath case (b).
 * @p shape is the candidate shape node.
 * @p shape_u_data is bv_scene_obj::s_u_data (struct ged_bv_data *).
 * @p match_ctx is struct _sg_path_match_ctx.
 *
 * Prefer BSG identity matching and fall back to legacy db_full_path matching
 * to preserve compatibility with callers that still rely on s_fullpath.
 */
static int
_sg_path_match_cb(const bsg_node *shape, void *shape_u_data, void *match_ctx)
{
    if (!shape || !match_ctx)
        return 0;

    struct _sg_path_match_ctx *ctx = (struct _sg_path_match_ctx *)match_ctx;
    struct bsg_identity sid;
    /* Identity-first: when both the target subpath hash and node identity are
     * available, match by identity.  If either side is unavailable (zero hash
     * or no side-car identity), fall back to legacy db_full_path matching. */
    if (ctx->subpath_id.node_id.value
        && bsg_node_identity_get(shape, &sid)
        && bsg_node_id_equal(&sid.node_id, &ctx->subpath_id.node_id))
        return 1;

    if (!shape_u_data || !ctx->subpath)
        return 0;
    struct ged_bv_data *bd = (struct ged_bv_data *)shape_u_data;
    return db_full_path_match_top(ctx->subpath, &bd->s_fullpath);
}


/*
 * Navigate from @p parent following subpath->fp_names[depth_start..fp_len-1]
 * and erase the matching sub-group (or leaf shape(s)) at the deepest level
 * found.
 *
 * Phase 7 Step 12: thin wrapper around bsg_erase_nested_subpath() in
 * libbsg/draw_set.c.  The GED-specific path-match logic is encapsulated
 * in _sg_path_match_cb; gedp is no longer needed here.
 */
static void
_sg_erase_nested_subpath(struct bv_scene_obj *parent,
                          struct db_full_path *subpath, size_t depth_start)
{
    struct _sg_path_match_ctx mctx = {0};
    mctx.subpath = subpath;
    {
	char *subpath_s = db_path_to_string(subpath);
	bsg_identity_from_path_str(&mctx.subpath_id, subpath_s, BSG_SOURCE_DB_OBJECT);
	if (subpath_s)
	    bu_free((void *)subpath_s, "subpath string");
    }

    const char **names = (const char **)bu_malloc(
        sizeof(const char *) * subpath->fp_len, "subpath names");
    for (size_t i = 0; i < subpath->fp_len; i++)
        names[i] = subpath->fp_names[i]->d_namep;

    bsg_erase_nested_subpath((bsg_node *)parent,
                              names, subpath->fp_len, depth_start,
                              _sg_path_match_cb, (void *)&mctx);

    bu_free(names, "subpath names");
}

static void
_sg_erase_path(struct ged *gedp, const char *path)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    struct db_i *dbip = gedp->dbip;
    struct db_full_path subpath;
    int found_subpath = (db_string_to_path(&subpath, dbip, path) == 0);

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < bsg_node_child_count((const bsg_node *)root); i++) {
        struct bv_scene_obj *g = (struct bv_scene_obj *)bsg_node_child((const bsg_node *)root, i);
        if (!BU_STR_EQUAL("_overlays", bsg_node_name((const bsg_node *)g)))
            bu_ptbl_ins(&snap, (long *)g);
    }

    for (size_t gi = 0; gi < BU_PTBL_LEN(&snap); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, gi);

        if (BU_STR_EQUAL(path, bsg_node_name((const bsg_node *)g))) {
            _sg_free_group(g);
            break;
        }

        if (!found_subpath)
            continue;

        /* Check if root child is an ancestor of the erase path */
        struct db_full_path gdlpath;
        if (db_string_to_path(&gdlpath, dbip, bsg_node_name((const bsg_node *)g)) != 0)
            continue;

        int is_ancestor = db_full_path_match_top(&gdlpath, &subpath);
        size_t ancestor_depth = gdlpath.fp_len;
        db_free_full_path(&gdlpath);

        if (is_ancestor && ancestor_depth < subpath.fp_len) {
            _sg_erase_nested_subpath(g, &subpath, ancestor_depth);
            if (bsg_node_child_count((const bsg_node *)g) == 0)
                _sg_free_group(g);
            break;
        }
    }

    bu_ptbl_free(&snap);
    if (found_subpath)
        db_free_full_path(&subpath);
}


/*
 * Recursively erase sub-groups named @p name from @p parent's sub-tree.
 * Does not erase @p parent itself.
 */
static void
_sg_erase_subgroups_by_name(struct ged *gedp, struct bv_scene_obj *parent,
                              const char *name)
{
    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < bsg_node_child_count((const bsg_node *)parent); i++) {
        struct bv_scene_obj *c = (struct bv_scene_obj *)bsg_node_child((const bsg_node *)parent, i);
        if (bsg_node_has_kind((const bsg_node *)c, BSG_NODE_GROUP))
            bu_ptbl_ins(&snap, (long *)c);
    }

    for (size_t i = 0; i < BU_PTBL_LEN(&snap); i++) {
        struct bv_scene_obj *c =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, i);
        if (BU_STR_EQUAL(bsg_node_name((const bsg_node *)c), name)) {
            _sg_free_group_contents(c);
            bsg_node_remove_child((bsg_node *)parent, (bsg_node *)c);
            /* parent is in the tree */
            _sg_bump_rev_node(parent);
            struct bv_scene_obj *fso = c->free_scene_obj;
            if (fso)
                FREE_BV_SCENE_OBJ(c, &fso->l, c->vlfree);
        } else {
            _sg_erase_subgroups_by_name(gedp, c, name);
        }
    }
    bu_ptbl_free(&snap);
}


static void
_sg_erase_all_names(struct ged *gedp, const char *name)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    _sg_erase_overlay_by_name(gedp, name);

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < bsg_node_child_count((const bsg_node *)root); i++) {
        struct bv_scene_obj *g = (struct bv_scene_obj *)bsg_node_child((const bsg_node *)root, i);
        if (!BU_STR_EQUAL("_overlays", bsg_node_name((const bsg_node *)g)))
            bu_ptbl_ins(&snap, (long *)g);
    }

    for (size_t gi = 0; gi < BU_PTBL_LEN(&snap); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, gi);

        /* Check path components for a name match */
        char *dup_path = bu_strdup(bsg_node_name((const bsg_node *)g));
        char *tok;
        int found = 0;
        tok = strtok(dup_path, "/");
        while (tok) {
            if (BU_STR_EQUAL(tok, name)) {
                _sg_free_group(g);
                found = 1;
                break;
            }
            tok = strtok(NULL, "/");
        }
        bu_free(dup_path, "_sg_erase_all_names dup");

        if (!found) {
            /* Recursively erase matching sub-groups within g */
            _sg_erase_subgroups_by_name(gedp, g, name);
        }
    }

    bu_ptbl_free(&snap);
}


static void
_sg_erase_all_paths(struct ged *gedp, const char *path)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    struct db_i *dbip = gedp->dbip;
    struct db_full_path subpath;

    if (db_string_to_path(&subpath, dbip, path) != 0)
        return;

    for (size_t i = 0; i < bsg_node_child_count((const bsg_node *)root); i++) {
        struct bv_scene_obj *g = (struct bv_scene_obj *)bsg_node_child((const bsg_node *)root, i);
        bsg_node_set_legacy_illum((bsg_node *)g, 0);
    }

    int restart;
    do {
        restart = 0;
        for (size_t i = 0; i < bsg_node_child_count((const bsg_node *)root); i++) {
            struct bv_scene_obj *g = (struct bv_scene_obj *)bsg_node_child((const bsg_node *)root, i);
            if (bsg_node_legacy_illum((const bsg_node *)g))
                continue;
            bsg_node_set_legacy_illum((bsg_node *)g, 1);

            struct db_full_path fullpath;
            if (db_string_to_path(&fullpath, dbip,
                                   bsg_node_name((const bsg_node *)g)) != 0)
                continue;

            /* Case A: root child is fully contained by (or equal to) subpath */
            if (db_full_path_subset(&fullpath, &subpath, 0)) {
                db_free_full_path(&fullpath);
                _sg_free_group(g);
                restart = 1;
                break;
            }

            /* Case B: root child is an ancestor of subpath — navigate sub-tree */
            if (db_full_path_match_top(&fullpath, &subpath) &&
                fullpath.fp_len < subpath.fp_len) {
                size_t depth = fullpath.fp_len;
                db_free_full_path(&fullpath);
                _sg_erase_nested_subpath(g, &subpath, depth);
                if (bsg_node_child_count((const bsg_node *)g) == 0)
                    _sg_free_group(g);
                restart = 1;
                break;
            }

            db_free_full_path(&fullpath);
        }
    } while (restart);

    db_free_full_path(&subpath);
}


/* ------------------------------------------------------------------ */
/* Bounds                                                              */
/* ------------------------------------------------------------------ */

static int
_sg_bounding_sph(struct ged *gedp, vect_t *min, vect_t *max, int pflag)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;

    /* Phase 9.1: delegate to the libbsg cached aggregator.
     * - pflag == 0 (no overlays): uses per-group bbox cache, O(touched).
     * - pflag != 0 (include overlays): full walk, no cache (rare path). */
    return bsg_subtree_bbox((bsg_node *)root, min, max, pflag);
}


/* ------------------------------------------------------------------ */
/* color_soltab (non-static: called by draw.c)                        */
/* ------------------------------------------------------------------ */

void
color_soltab(struct db_i *dbip, struct bv_scene_obj *sp)
{
    const struct mater *mp;

    sp->s_old.s_cflag = 0;

    if (sp->s_old.s_uflag) {
        sp->s_color[0] = sp->s_old.s_basecolor[0];
        sp->s_color[1] = sp->s_old.s_basecolor[1];
        sp->s_color[2] = sp->s_old.s_basecolor[2];
        return;
    }

    if (dbip) {
        for (mp = db_mater_head(dbip); mp != MATER_NULL; mp = mp->mt_forw) {
            if (sp->s_old.s_regionid <= mp->mt_high &&
                sp->s_old.s_regionid >= mp->mt_low) {
                sp->s_color[0] = mp->mt_r;
                sp->s_color[1] = mp->mt_g;
                sp->s_color[2] = mp->mt_b;
                return;
            }
        }
    }

    if (sp->s_old.s_dflag)
        sp->s_old.s_cflag = 1;

    sp->s_color[0] = sp->s_old.s_basecolor[0];
    sp->s_color[1] = sp->s_old.s_basecolor[1];
    sp->s_color[2] = sp->s_old.s_basecolor[2];
}


/* ------------------------------------------------------------------ */
/* invent_solid (pseudo-solid overlay insertion)                      */
/* ------------------------------------------------------------------ */

static void
_bsg_payload_vlist_touch(struct bv_scene_obj *sp)
{
    struct bsg_payload *payload = NULL;

    if (!sp)
	return;

    payload = bsg_payload_vlist_from_node((bsg_node *)sp);
    if (!payload)
	return;

    (void)bsg_payload_bump_revision(payload);
    payload->bounds_revision = payload->revision;
    bsg_node_field_touch((bsg_node *)sp, BSG_FIELD_PAYLOAD);
    (void)bsg_node_bump_revision((bsg_node *)sp, BSG_NODE_REV_PAYLOAD);
    bsg_node_mark_stale((bsg_node *)sp);
}

static void
solid_append_vlist(struct bv_scene_obj *sp, struct bv_vlist *vlist)
{
    if (BU_LIST_IS_EMPTY(&(sp->s_vlist)))
        sp->s_vlen = 0;
    sp->s_vlen += bv_vlist_cmd_cnt(vlist);
    BU_LIST_APPEND_LIST(&(sp->s_vlist), &(vlist->l));
    _bsg_payload_vlist_touch(sp);
}

static void
solid_copy_vlist(struct db_i *UNUSED(dbip), struct bv_scene_obj *sp,
                 struct bv_vlist *vlist, struct bu_list *vlfree)
{
    BU_LIST_INIT(&(sp->s_vlist));
    bv_vlist_copy(vlfree, &(sp->s_vlist), (struct bu_list *)vlist);
    sp->s_vlen = bv_vlist_cmd_cnt((struct bv_vlist *)(&(sp->s_vlist)));
    _bsg_payload_vlist_touch(sp);
}


static int
_sg_invent(struct ged *gedp, char *name, struct bu_list *vhead, long int rgb,
           int copy, fastf_t transparency, int dmode, int csoltab)
{
    if (!gedp || !gedp->ged_gvp)
        return 0;

    struct db_i *dbip = gedp->dbip;
    struct bu_list *vlfree = &rt_vlfree;

    if (dbip == DBI_NULL)
        return 0;

    /* Refuse to clobber a real (non-overlay) database entry. */
    if (db_lookup(dbip, name, LOOKUP_QUIET) != RT_DIR_NULL) {
        bu_log("invent_solid(%s) would clobber existing database entry, "
               "ignored\n", name);
        return -1;
    }

    /* Remove any pre-existing overlay with the same name. */
    _sg_erase_overlay_by_name(gedp, name);

    /* Obtain a fresh solid structure. */
    struct bv_scene_obj *sp = bv_obj_get(gedp->ged_gvp, BV_DB_OBJS);
    bsg_node_set_kind((bsg_node *)sp,
		      bsg_node_kind((const bsg_node *)sp) | BSG_NODE_SHAPE | BSG_PAYLOAD_OVERLAY);
    bsg_node_set_name((bsg_node *)sp, name);

    struct ged_bv_data *bdata =
        (sp->s_u_data) ? (struct ged_bv_data *)sp->s_u_data : NULL;
    if (!bdata) {
        BU_GET(bdata, struct ged_bv_data);
        db_full_path_init(&bdata->s_fullpath);
        sp->s_u_data = (void *)bdata;
    } else {
        bdata->s_fullpath.fp_len = 0;
    }
    if (!sp->s_u_data)
        return -1;
    /* Phase 7 Step 9: register back-pointer + illum-clear callback. */
    bdata->gedp = gedp;
    sp->s_free_callback = ged_bv_illum_free_cb;

    if (copy)
        solid_copy_vlist(dbip, sp, (struct bv_vlist *)vhead, vlfree);
    else {
        solid_append_vlist(sp, (struct bv_vlist *)vhead);
        BU_LIST_INIT(vhead);
    }
    bv_scene_obj_bound(sp, gedp->ged_gvp);

    /* Attach to the _overlays subgroup (no phony db entry needed). */
    struct bv_scene_obj *ov = _sg_overlay_root(gedp);
    if (ov) {
        bsg_node_add_child((bsg_node *)ov, (bsg_node *)sp);
        /* ov->parent is set (ov is under the draw root) */
        _sg_bump_rev_node(ov);
        /* Phase 9.1: a new shape under ov invalidates ancestors' bbox cache.
         * Overlay shapes themselves don't contribute to the cached
         * (no-overlay) aggregate, but invalidating is safe and cheap. */
        bsg_node_bbox_invalidate((bsg_node *)ov);
    }

    bsg_node_set_legacy_illum((bsg_node *)sp, 0);
    sp->s_soldash            = 0;
    sp->s_old.s_Eflag        = 1;
    sp->s_color[0]           = sp->s_old.s_basecolor[0] = (rgb >> 16) & 0xFF;
    sp->s_color[1]           = sp->s_old.s_basecolor[1] = (rgb >>  8) & 0xFF;
    sp->s_color[2]           = sp->s_old.s_basecolor[2] = (rgb      ) & 0xFF;
    sp->s_old.s_regionid     = 0;
    sp->s_old.s_uflag        = 0;
    sp->s_old.s_dflag        = 0;
    sp->s_old.s_cflag        = 0;
    sp->s_old.s_wflag        = 0;
    sp->s_os->transparency   = transparency;
    sp->s_os->s_dmode        = dmode;

    struct bsg_material m;
    bsg_material_from_legacy_obj((const bsg_node *)sp, &m);
    m.revision = (uint64_t)sp->s_color_rev;
    bsg_node_material_set((bsg_node *)sp, &m);

    struct bsg_appearance a;
    bsg_appearance_from_legacy_obj_settings((const bsg_node *)sp, &a);
    bsg_node_appearance_set((bsg_node *)sp, &a);

    if (csoltab) {
        color_soltab(gedp->dbip, sp);
        bsg_material_from_legacy_obj((const bsg_node *)sp, &m);
        m.revision = (uint64_t)sp->s_color_rev;
        bsg_node_material_set((bsg_node *)sp, &m);
    }

    return 0;
}


/* ------------------------------------------------------------------ */
/* dl_set_iflag                                                        */
/* ------------------------------------------------------------------ */

static int
_iflag_solid_cb(bsg_node *n, void *ud)
{
    struct bv_scene_obj *sp = (struct bv_scene_obj *)n;
    bsg_node_set_legacy_illum((bsg_node *)sp, (*(char *)ud == UP));
    return 1;
}

static void
_sg_set_iflag(struct ged *gedp, int iflag)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;
    char flag = (char)iflag;
    bsg_visit((bsg_node *)root, BSG_NODE_SHAPE, _iflag_solid_cb, &flag);
}


/* ------------------------------------------------------------------ */
/* name hash / revision counter                                        */
/* ------------------------------------------------------------------ */

/*
 * Return the revision counter as a cheap change-detection token.
 * Returns 0 when nothing has been drawn since the last zap (or ever).
 * Replaces the former O(N) name-concatenation hash with an O(1) read.
 */
static unsigned long long
_sg_name_hash(struct ged *gedp)
{
    return (unsigned long long)gedp->i->ged_gdp->gd_draw_rev;
}


/* ------------------------------------------------------------------ */
/* mater revision counter (B4: lazy-color token)                      */
/* ------------------------------------------------------------------ */

/*
 * Bump the mater-revision counter.  Must be called whenever the effective
 * material/color table changes (e.g. after 'color', 'mater', 'rmater').
 * Invalidates the per-shape color stamps so that the next call to
 * bsg_view_obj_color_from_soltab() recolors affected shapes.
 *
 * NOT called from inside color_from_soltab itself — the counter is
 * event-driven, not sweep-driven (B4 activation).
 */
static void
_sg_bump_mater_rev(struct ged *gedp)
{
    gedp->i->ged_gdp->gd_mater_rev++;
}


/* ------------------------------------------------------------------ */
/* illuminated-solid tracker (B5: O(1) set_iflag(DOWN))               */
/* ------------------------------------------------------------------ */

/*
 * Phase 9.3 (drawing_stack_modernization B5 residual): NodeSensor callback
 * fired on every bsg_node_field_touch() targeting the currently-illuminated
 * solid.  Bumps gd_illum_rev so external observers can detect highlight-
 * state changes by comparing snapshots — without needing to subscribe to
 * the sensor themselves.
 */
static int
_sg_illum_sensor_cb(bsg_node *target, void *data)
{
    struct ged *gedp = (struct ged *)data;
    if (!gedp || !target)
        return 1;
    /* The sensor's lifecycle is tied to the illumination identity: it is
     * created in _sg_set_illum and torn down before any transition to a
     * different target, so by the time this callback fires the target
     * is, by construction, the currently illuminated solid. */
    gedp->i->ged_gdp->gd_illum_rev++;
    return 1;
}

/*
 * Register @p sp as the single currently-illuminated solid.
 * Clears the s_iflag of any previously registered solid first.
 * Passing NULL deregisters without setting a new illuminated solid (used
 * after operations that place multiple solids in the UP state, such as
 * matpick, so that the fallback O(N) sweep remains safe).
 *
 * Phase 9.3: also manages the NodeSensor that fires on field changes to
 * the illuminated solid.  Each transition (clear, replace, or set) tears
 * down the previous sensor (if any), bumps gd_illum_rev, and creates a
 * fresh sensor on the new target.
 */
/*
 * Return the currently illuminated solid (the watched target of the
 * NodeSensor on @p gdp), or NULL when nothing is illuminated.
 *
 * Phase 13: the registered NodeSensor is the source of truth for the
 * illuminated-solid identity; the file no longer maintains a separate
 * gd_illum_solid cache.
 */
static struct bv_scene_obj *
_sg_illum_node(const struct ged_drawable *gdp)
{
    if (!gdp || !gdp->gd_illum_sensor)
        return NULL;
    return (struct bv_scene_obj *)bsg_sensor_target(
        (bsg_node *)gdp->gd_illum_sensor);
}

/*
 * Register @p sp as the single currently-illuminated solid.
 * Clears the s_iflag of any previously registered solid first.
 * Passing NULL deregisters without setting a new illuminated solid (used
 * after operations that place multiple solids in the UP state, such as
 * matpick, so that the fallback O(N) sweep remains safe).
 *
 * Phase 9.3 / 13: identity is tracked through the NodeSensor (target
 * field) rather than a parallel gd_illum_solid cache.  Each transition
 * (clear, replace, or set) tears down the previous sensor (if any),
 * creates a fresh sensor on the new target (so both the identity and the
 * field-touch notification side share the same handle), and bumps
 * gd_illum_rev.
 */
static void
_sg_set_illum(struct ged *gedp, struct bv_scene_obj *sp)
{
    struct ged_drawable *gdp = gedp->i->ged_gdp;
    struct bv_scene_obj *old = _sg_illum_node(gdp);

    if (old == sp) {
        /* No-op fast path: same target, no transition. */
        return;
    }

    if (old) {
        bsg_node_set_legacy_illum((bsg_node *)old, 0);
        /* Phase 6C: mirror s_iflag change in the BSG "active" selection set. */
        if (gdp->gd_draw_root)
            bsg_node_set_selected((bsg_node *)gdp->gd_draw_root,
                                  (bsg_node *)old, "active", 0);
    }
    if (sp) {
        bsg_node_set_legacy_illum((bsg_node *)sp, 1);
        /* Phase 6C: mirror s_iflag change in the BSG "active" selection set. */
        if (gdp->gd_draw_root)
            bsg_node_set_selected((bsg_node *)gdp->gd_draw_root,
                                  (bsg_node *)sp, "active", 1);
    }

    /* Tear down any previously-registered NodeSensor. */
    if (gdp->gd_illum_sensor) {
        bsg_sensor_destroy((bsg_node *)gdp->gd_illum_sensor);
        gdp->gd_illum_sensor = NULL;
    }

    /* Register a fresh sensor on the new target.  This both records the
     * illuminated-solid identity (read back via bsg_sensor_target) and
     * arms the field-touch notification side; if registry-full, both go
     * away together — consumers must subscribe their own sensors or
     * fall back to other means. */
    if (sp && gdp->gd_draw_root) {
        gdp->gd_illum_sensor = bsg_node_sensor_create(
            (bsg_node *)gdp->gd_draw_root,
            (bsg_node *)sp,
            _sg_illum_sensor_cb,
            (void *)gedp);
    }

    /* Every transition is itself a highlight-state change. */
    gdp->gd_illum_rev++;
}


/* ------------------------------------------------------------------ */
/* color_from_soltab                                                   */
/* ------------------------------------------------------------------ */

/*
 * Context passed to _color_solid_cb for Phase 7 Step 14.
 * Bundles the database pointer and the current mater-revision token so
 * the callback can stamp each shape it colors without needing gedp.
 */
struct _color_ctx {
    struct db_i *dbip;
    uint64_t     mater_rev;
};

static int
_color_solid_cb(bsg_node *n, void *ud)
{
    struct bv_scene_obj *sp = (struct bv_scene_obj *)n;
    struct _color_ctx *ctx = (struct _color_ctx *)ud;

    /* B4 lazy-color skip: if this shape's color stamp already matches the
     * current mater-revision, it was colored since the last material-change
     * event and can be skipped.  gd_mater_rev is bumped only by external
     * material-change events (via bsg_view_obj_bump_mater_rev), not by the
     * sweep itself, so shapes stamped at the current revision will be skipped
     * on every subsequent call until another material change occurs. */
    if ((uint64_t)sp->s_color_rev == ctx->mater_rev)
        return 1;

    color_soltab(ctx->dbip, sp);
    struct bsg_material m;
    bsg_material_from_legacy_obj((const bsg_node *)sp, &m);
    m.revision = ctx->mater_rev;
    bsg_node_material_set((bsg_node *)sp, &m);
    return 1;
}

static void
_sg_color_soltab(struct ged *gedp)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;
    struct _color_ctx ctx;
    ctx.dbip      = gedp->dbip;
    ctx.mater_rev = gedp->i->ged_gdp->gd_mater_rev;
    bsg_visit((bsg_node *)root, BSG_NODE_SHAPE, _color_solid_cb, &ctx);
    /* B4 activation: do NOT bump gd_mater_rev here.  The counter is
     * event-driven — only bsg_view_obj_bump_mater_rev() (called by
     * material-change commands) advances it. */
}


/* ================================================================== */
/* Public BSG view-object API                                         */
/* ================================================================== */

struct bv_scene_obj *
bsg_view_obj_ensure_root(struct ged *gedp)
{
    if (!gedp)
        return NULL;
    return _sg_root(gedp);
}


struct bv_scene_obj *
bsg_view_obj_root(struct ged *gedp)
{
    if (!gedp)
        return NULL;
    return gedp->i->ged_gdp->gd_draw_root;
}


void
bsg_view_obj_erase_by_name(struct ged *gedp, const char *name)
{
    if (!gedp || !name)
        return;
    _sg_erase_all_names(gedp, name);
}


/*
 * Phase 10/13 (drawing-stack modernization): db_full_path-keyed
 * lookup/erase entry points.  Each formats @p dfp via db_path_to_string()
 * and forwards to the file-private path-string helpers (_sg_add_path,
 * _sg_erase_path, _sg_erase_all_paths).  The Phase 10 deprecated
 * public path-string wrappers (bsg_view_obj_lookup_or_add_path /
 * _erase_by_path / _erase_all_paths) were removed in Phase 13.
 *
 * db_path_to_string() prepends a leading '/'; the path-string
 * implementations expect no leading slash, so normalize before
 * forwarding.
 */
static const char *
_dbpath_skip_lead_slash(const char *s)
{
    if (s && *s == '/')
        return s + 1;
    return s;
}

struct bv_scene_obj *
bsg_view_obj_lookup_or_add_dbpath(struct ged *gedp,
                                  const struct db_full_path *dfp)
{
    if (!gedp || !dfp)
        return NULL;
    char *s = db_path_to_string(dfp);
    if (!s)
        return NULL;
    /* Call the file-private helper directly so this entry point does
     * not depend on the deprecated public path-string wrapper. */
    struct bv_scene_obj *r =
        (struct bv_scene_obj *)_sg_add_path(gedp, _dbpath_skip_lead_slash(s));
    bu_free(s, "bsg_view_obj_lookup_or_add_dbpath: path string");
    return r;
}


void
bsg_view_obj_erase_by_dbpath(struct ged *gedp,
                             const struct db_full_path *dfp)
{
    if (!gedp || !dfp)
        return;
    char *s = db_path_to_string(dfp);
    if (!s)
        return;
    _sg_erase_path(gedp, _dbpath_skip_lead_slash(s));
    bu_free(s, "bsg_view_obj_erase_by_dbpath: path string");
}


void
bsg_view_obj_erase_all_dbpaths(struct ged *gedp,
                               const struct db_full_path *dfp)
{
    if (!gedp || !dfp)
        return;
    char *s = db_path_to_string(dfp);
    if (!s)
        return;
    _sg_erase_all_paths(gedp, _dbpath_skip_lead_slash(s));
    bu_free(s, "bsg_view_obj_erase_all_dbpaths: path string");
}


int
bsg_view_obj_bounds(struct ged *gedp, vect_t *min, vect_t *max, int pflag)
{
    if (!gedp || !min || !max)
        return 1;
    return _sg_bounding_sph(gedp, min, max, pflag);
}


void
bsg_view_obj_set_iflag(struct ged *gedp, int iflag)
{
    if (!gedp)
        return;
    if (iflag == DOWN) {
        /* B5 fast path: when exactly one solid is illuminated and tracked,
         * clear only that solid in O(1) rather than sweeping the whole tree. */
        struct ged_drawable *gdp = gedp->i->ged_gdp;
        struct bv_scene_obj *illum = _sg_illum_node(gdp);
        if (illum) {
            bsg_node_set_legacy_illum((bsg_node *)illum, 0);
            /* Phase 9.3 / 13: tear down NodeSensor + bump highlight rev. */
            bsg_sensor_destroy((bsg_node *)gdp->gd_illum_sensor);
            gdp->gd_illum_sensor = NULL;
            gdp->gd_illum_rev++;
            return;
        }
    }
    _sg_set_iflag(gedp, iflag);
}


void
bsg_view_obj_color_from_soltab(struct ged *gedp)
{
    if (!gedp)
        return;
    _sg_color_soltab(gedp);
}


int
bsg_view_obj_invent(struct ged *gedp, char *name, struct bu_list *vhead,
                    long int rgb, int copy, fastf_t transparency,
                    int dmode, int csoltab)
{
    if (!gedp || !name || !vhead)
        return -1;
    return _sg_invent(gedp, name, vhead, rgb, copy, transparency,
                      dmode, csoltab);
}


unsigned long long
bsg_view_obj_name_hash(struct ged *gedp)
{
    if (!gedp)
        return 0;
    return _sg_name_hash(gedp);
}


uint64_t
bsg_view_obj_draw_rev(struct ged *gedp)
{
    if (!gedp)
        return 0;
    return gedp->i->ged_gdp->gd_draw_rev;
}


void
bsg_view_obj_foreach_solid(struct ged *gedp,
                           int (*cb)(struct bv_scene_obj *, void *),
                           void *userdata)
{
    if (!gedp || !cb)
        return;
    bsg_visit((bsg_node *)gedp->i->ged_gdp->gd_draw_root,
              BSG_NODE_SHAPE,
              (int (*)(bsg_node *, void *))cb,
              userdata);
}


static int
_any_solid_cb(bsg_node *n, void *ud)
{
    (void)n;
    *(int *)ud = 1;
    return 0;
}

int
bsg_view_obj_is_nonempty(struct ged *gedp)
{
    if (!gedp)
        return 0;
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return 0;
    int found = 0;
    bsg_visit((bsg_node *)root, BSG_NODE_SHAPE, _any_solid_cb, &found);
    return found;
}


struct _first_solid_data { struct bv_scene_obj *result; };

static int
_first_solid_cb(bsg_node *n, void *ud)
{
    struct _first_solid_data *d = (struct _first_solid_data *)ud;
    d->result = (struct bv_scene_obj *)n;
    return 0;
}

struct bv_scene_obj *
bsg_view_obj_first_solid(struct ged *gedp)
{
    if (!gedp)
        return NULL;
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return NULL;
    for (size_t gi = 0; gi < bsg_node_child_count((const bsg_node *)root); gi++) {
        struct bv_scene_obj *g = (struct bv_scene_obj *)bsg_node_child((const bsg_node *)root, gi);
        if (BU_STR_EQUAL("_overlays", bsg_node_name((const bsg_node *)g)))
            continue;
        struct _first_solid_data d = { NULL };
        bsg_visit((bsg_node *)g, BSG_NODE_SHAPE, _first_solid_cb, &d);
        if (d.result)
            return d.result;
    }
    return NULL;
}


/*
 * Build a DFS-order flat snapshot of all non-overlay drawn shapes into
 * @p out.  The _overlays group is skipped entirely so that invented
 * pseudo-solids are not included in navigation order.
 *
 * Callers must call bu_ptbl_free(out) when done.
 */
static int
_snap_solid_cb(bsg_node *n, void *ud)
{
    bu_ptbl_ins((struct bu_ptbl *)ud, (long *)n);
    return 1;
}

static void
_sg_build_solid_snapshot(struct ged *gedp, struct bu_ptbl *out)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;
    for (size_t gi = 0; gi < bsg_node_child_count((const bsg_node *)root); gi++) {
        struct bv_scene_obj *g = (struct bv_scene_obj *)bsg_node_child((const bsg_node *)root, gi);
        if (BU_STR_EQUAL("_overlays", bsg_node_name((const bsg_node *)g)))
            continue;
        bsg_visit((bsg_node *)g, BSG_NODE_SHAPE, _snap_solid_cb, (void *)out);
    }
}


int
bsg_view_obj_solid_count(struct ged *gedp)
{
    if (!gedp)
        return 0;
    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    _sg_build_solid_snapshot(gedp, &snap);
    int n = (int)BU_PTBL_LEN(&snap);
    bu_ptbl_free(&snap);
    return n;
}


struct bv_scene_obj *
bsg_view_obj_solid_at(struct ged *gedp, int idx)
{
    if (!gedp)
        return NULL;
    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    _sg_build_solid_snapshot(gedp, &snap);
    struct bv_scene_obj *sp = NULL;
    int n = (int)BU_PTBL_LEN(&snap);
    if (n > 0) {
        /* Wrap negative indices as well as overflow */
        idx = ((idx % n) + n) % n;
        sp = (struct bv_scene_obj *)BU_PTBL_GET(&snap, idx);
    }
    bu_ptbl_free(&snap);
    return sp;
}


int
bsg_view_obj_solid_index(struct ged *gedp, struct bv_scene_obj *target)
{
    if (!gedp || !target)
        return -1;
    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    _sg_build_solid_snapshot(gedp, &snap);
    int found = -1;
    for (int i = 0; i < (int)BU_PTBL_LEN(&snap); i++) {
        if ((struct bv_scene_obj *)BU_PTBL_GET(&snap, i) == target) {
            found = i;
            break;
        }
    }
    bu_ptbl_free(&snap);
    return found;
}


/*
 * Advance @p sp by @p delta positions (positive = forward, negative = backward)
 * in DFS snapshot order, with circular wraparound.  Overlay shapes are
 * excluded from the index.  Returns the shape at the new position, or NULL
 * when no non-overlay shapes are drawn.
 *
 * Builds one DFS snapshot internally.
 */
struct bv_scene_obj *
bsg_view_obj_advance_solid(struct ged *gedp, struct bv_scene_obj *sp, int delta)
{
    if (!gedp)
        return NULL;
    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    _sg_build_solid_snapshot(gedp, &snap);
    struct bv_scene_obj *result = NULL;
    int n = (int)BU_PTBL_LEN(&snap);
    if (n > 0) {
        int idx = 0;
        if (sp) {
            for (int i = 0; i < n; i++) {
                if ((struct bv_scene_obj *)BU_PTBL_GET(&snap, i) == sp) {
                    idx = i;
                    break;
                }
            }
        }
        int new_idx = (((idx + delta) % n) + n) % n;
        result = (struct bv_scene_obj *)BU_PTBL_GET(&snap, new_idx);
    }
    bu_ptbl_free(&snap);
    return result;
}


/*
 * DFS-order next solid with circular wraparound.
 * Now delegates to the snapshotted index approach.
 */
struct bv_scene_obj *
bsg_view_obj_next_solid(struct ged *gedp, struct bv_scene_obj *sp)
{
    return bsg_view_obj_advance_solid(gedp, sp, 1);
}


struct bv_scene_obj *
bsg_view_obj_prev_solid(struct ged *gedp, struct bv_scene_obj *sp)
{
    return bsg_view_obj_advance_solid(gedp, sp, -1);
}


struct bv_scene_obj *
bsg_view_obj_group_of_solid(struct ged *gedp, struct bv_scene_obj *sp)
{
    (void)gedp;
    if (!sp)
        return NULL;
    /* Walk up the parent chain to find the root child (depth == 1):
     * that is the group whose parent is the draw root (which has no parent). */
    struct bv_scene_obj *g = (struct bv_scene_obj *)bsg_node_parent((const bsg_node *)sp);
    while (g) {
	struct bv_scene_obj *gp = (struct bv_scene_obj *)bsg_node_parent((const bsg_node *)g);
	if (!gp || !bsg_node_parent((const bsg_node *)gp))
	    break;
	g = gp;
    }
    return g;
}


void
bsg_view_obj_foreach_group(struct ged *gedp,
                           int (*cb)(struct bv_scene_obj *, void *),
                           void *userdata)
{
    if (!gedp || !cb)
        return;

    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    for (size_t gi = 0; gi < bsg_node_child_count((const bsg_node *)root); gi++) {
        struct bv_scene_obj *g = (struct bv_scene_obj *)bsg_node_child((const bsg_node *)root, gi);
        if (!(*cb)(g, userdata))
            return;
    }
}


struct bv_scene_obj *
bsg_view_obj_group_first_solid(struct bv_scene_obj *group)
{
    if (!group)
        return NULL;
    struct _first_solid_data d = { NULL };
    bsg_visit((bsg_node *)group, BSG_NODE_SHAPE, _first_solid_cb, &d);
    return d.result;
}


static int
_last_solid_cb(bsg_node *n, void *ud)
{
    *(struct bv_scene_obj **)ud = (struct bv_scene_obj *)n;
    return 1; /* keep going to find the last one */
}

struct bv_scene_obj *
bsg_view_obj_group_last_solid(struct bv_scene_obj *group)
{
    if (!group)
        return NULL;
    struct bv_scene_obj *last = NULL;
    bsg_visit((bsg_node *)group, BSG_NODE_SHAPE, _last_solid_cb, &last);
    return last;
}


int
bsg_view_obj_group_is_nonempty(struct bv_scene_obj *group)
{
    if (!group)
        return 0;
    int found = 0;
    bsg_visit((bsg_node *)group, BSG_NODE_SHAPE, _any_solid_cb, &found);
    return found;
}


const char *
bsg_view_obj_group_path(struct bv_scene_obj *group)
{
    if (!group)
        return NULL;
    return bsg_node_name((const bsg_node *)group);
}


void
bsg_view_obj_append_to_last_group(struct ged *gedp, struct bv_scene_obj *sp)
{
    if (!gedp || !sp)
        return;

    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root || bsg_node_child_count((const bsg_node *)root) == 0)
        return;

    struct bv_scene_obj *g = (struct bv_scene_obj *)bsg_node_child(
	(const bsg_node *)root,
	bsg_node_child_count((const bsg_node *)root) - 1);
    bsg_view_obj_append_solid_to_group(gedp, g, sp);
}


/*
 * Phase 10/13: db_full_path-keyed setter.  Formats @p new_dfp via
 * db_path_to_string() and writes the (slash-stripped) string into the
 * group's s_name.  The Phase 10 deprecated public path-string variant
 * bsg_view_obj_group_set_path() was removed in Phase 13.
 */
void
bsg_view_obj_group_set_dbpath(struct bv_scene_obj *group,
                              const struct db_full_path *new_dfp)
{
    if (!group || !new_dfp)
        return;
    char *s = db_path_to_string(new_dfp);
    if (!s)
        return;
    bsg_node_set_name((bsg_node *)group, _dbpath_skip_lead_slash(s));
    bu_free(s, "bsg_view_obj_group_set_dbpath: path string");
}


/*
 * Phase 10: db_full_path-keyed getter.  Group nodes currently store their
 * path as a string in s_name; this accessor parses that string into the
 * caller-supplied @p out buffer using @p gedp's dbip.  @p out must be
 * caller-initialized via db_full_path_init() (or equivalent), and the
 * caller is responsible for db_free_full_path() afterwards.  Returns 0
 * on success, non-zero on failure (NULL group, NULL dbip, parse error,
 * or synthetic group with no path).
 */
int
bsg_view_obj_group_dbpath(struct ged *gedp,
                          struct bv_scene_obj *group,
                          struct db_full_path *out)
{
    if (!gedp || !group || !out || !gedp->dbip)
        return -1;
    if (bsg_view_obj_group_is_phony(group))
        return -1;
    const char *s = bsg_node_name((const bsg_node *)group);
    if (!s || !*s)
        return -1;
    return db_string_to_path(out, gedp->dbip, s);
}


int
bsg_view_obj_group_is_phony(struct bv_scene_obj *group)
{
    if (!group)
        return 0;
    /* The _overlays group is the only pseudo-group; real drawn-path
     * groups always have a valid dp and are not phony. */
    return BU_STR_EQUAL("_overlays", bsg_node_name((const bsg_node *)group)) ? 1 : 0;
}


void
bsg_view_obj_zap(struct ged *gedp)
{
    if (!gedp)
        return;

    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    /* Snapshot so we can safely modify children during iteration */
    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < bsg_node_child_count((const bsg_node *)root); i++)
        bu_ptbl_ins(&snap, (long *)bsg_node_child((const bsg_node *)root, i));

    for (size_t gi = 0; gi < BU_PTBL_LEN(&snap); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, gi);
        _sg_free_group_contents(g);
        bsg_node_remove_child((bsg_node *)root, (bsg_node *)g);
        struct bv_scene_obj *fso = g->free_scene_obj;
        if (fso)
            FREE_BV_SCENE_OBJ(g, &fso->l, g->vlfree);
    }
    bu_ptbl_free(&snap);

    /* Reset revision counter: after a full zap the drawn set is empty,
     * so the hash should return 0 (matching initial state). */
    gedp->i->ged_gdp->gd_draw_rev = 0;

    /* Clear illuminated-solid tracker: the solid no longer exists. */
    {
        struct ged_drawable *gdp = gedp->i->ged_gdp;
        /* Phase 9.3 / 13: tear down sensor (which also forgets the
         * illuminated-solid identity) + bump highlight rev. */
        if (gdp->gd_illum_sensor) {
            bsg_sensor_destroy((bsg_node *)gdp->gd_illum_sensor);
            gdp->gd_illum_sensor = NULL;
        }
        gdp->gd_illum_rev++;
    }
}


int
bsg_view_obj_has_groups(struct ged *gedp)
{
    if (!gedp || !gedp->i || !gedp->i->ged_gdp)
        return 0;
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return 0;
    return (bsg_node_child_count((const bsg_node *)root) > 0) ? 1 : 0;
}


/*
 * Append @p sp to the correct position in the nested group hierarchy
 * rooted at @p group.
 *
 * @p group is at tree depth D (root = 0, root-children = 1, ...).
 * @p sp's db_full_path has fp_len components.  The components at indices
 * [D .. fp_len-2] name intermediate sub-groups that must exist between
 * @p group and the leaf shape; these are created on demand via
 * _sg_find_or_create_child_group.  The shape is then appended to the
 * deepest sub-group.
 *
 * When sp has no s_u_data (e.g. an internally created shape without a
 * full db path), it is appended directly to @p group.
 */
void
bsg_view_obj_append_solid_to_group(struct ged *gedp,
                                    struct bv_scene_obj *group,
                                    struct bv_scene_obj *sp)
{
    if (!group || !sp)
        return;

    /* Determine which sub-groups to create/navigate */
    struct ged_bv_data *bdata =
        (sp->s_u_data) ? (struct ged_bv_data *)sp->s_u_data : NULL;
    int fp_len = bdata ? (int)bdata->s_fullpath.fp_len : 0;

    if (!gedp || fp_len == 0) {
        /* No path info — append directly */
        bsg_node_add_child((bsg_node *)group, (bsg_node *)sp);
        /* Phase 9.1: shape added under group invalidates aggregate bbox cache. */
        bsg_node_bbox_invalidate((bsg_node *)group);
        return;
    }

    int group_depth = _sg_tree_depth(group);
    /* Number of intermediate sub-group components to create between
     * group (at group_depth) and the shape (at fp_len - 1 = leaf index).
     * Components at indices [group_depth .. fp_len - 2] are sub-groups.
     * (fp_len - 2) is the index of the deepest comb above the leaf;
     * if group_depth >= fp_len - 1 there are no intermediate groups needed. */
    struct bv_scene_obj *cur = group;
    for (int d = group_depth; d < fp_len - 1; d++) {
        const char *comp = bdata->s_fullpath.fp_names[d]->d_namep;
        struct bv_scene_obj *child =
            _sg_find_or_create_child_group(gedp, cur, comp);
        if (!child)
            break;
        cur = child;
    }

    bsg_node_add_child((bsg_node *)cur, (bsg_node *)sp);
    /* Phase 9.1: shape added under cur invalidates aggregate bbox cache. */
    bsg_node_bbox_invalidate((bsg_node *)cur);
}


/* ================================================================== */
/* B5: illuminated-solid tracker — public API                         */
/* ================================================================== */

/**
 * Set @p sp as the single currently-illuminated solid (s_iflag = UP).
 * Any previously registered solid is cleared first (s_iflag = DOWN).
 * Passing NULL deregisters without setting a new illuminated solid.
 *
 * After this call, bsg_view_obj_set_iflag(gedp, DOWN) can resolve in
 * O(1) rather than sweeping the entire draw tree.
 *
 * When an operation may place MULTIPLE solids in the UP state (e.g.
 * matpick path-prefix sweep), call bsg_view_obj_set_illum(gedp, NULL)
 * to signal that the fast path cannot be used; set_iflag(DOWN) then
 * falls back to the O(N) full sweep.
 */
void
bsg_view_obj_set_illum(struct ged *gedp, struct bv_scene_obj *sp)
{
    if (!gedp)
        return;
    _sg_set_illum(gedp, sp);
}


/**
 * Return the currently-registered illuminated solid, or NULL when none
 * is tracked (either nothing is illuminated or tracking was invalidated
 * because multiple solids may be UP).
 */
struct bv_scene_obj *
bsg_view_obj_get_illum(const struct ged *gedp)
{
    if (!gedp)
        return NULL;
    return _sg_illum_node(gedp->i->ged_gdp);
}


/**
 * Phase H (drawing_stack_modernization): find the first drawn solid in
 * any display-list group whose basename (last slash-separated path
 * component) equals @p name.  Illuminates that solid via
 * bsg_view_obj_set_illum() and returns it.  Passes NULL to
 * bsg_view_obj_set_illum() (clearing any current highlight) when no
 * match is found.
 */

struct _illum_by_name_ctx {
    struct ged          *gedp;
    const char          *name;
    struct bv_scene_obj *result;
};

static int
_illum_by_name_group_cb(struct bv_scene_obj *group, void *udata)
{
    struct _illum_by_name_ctx *ctx = (struct _illum_by_name_ctx *)udata;
    const char *path = bsg_view_obj_group_path(group);
    if (!path)
        return 1; /* continue */

    /* Extract the last path component (after the final '/') */
    const char *tail = path;
    const char *p = path;
    while (*p) {
        if (*p == '/')
            tail = p + 1;
        p++;
    }
    if (!BU_STR_EQUAL(tail, ctx->name))
        return 1; /* continue */

    struct bv_scene_obj *sp = bsg_view_obj_group_first_solid(group);
    if (!sp)
        return 1; /* continue */

    ctx->result = sp;
    return 0; /* stop */
}

struct bv_scene_obj *
bsg_view_obj_illum_by_name(struct ged *gedp, const char *name)
{
    if (!gedp || !name)
        return NULL;
    struct _illum_by_name_ctx ctx;
    ctx.gedp   = gedp;
    ctx.name   = name;
    ctx.result = NULL;
    bsg_view_obj_foreach_group(gedp, _illum_by_name_group_cb, &ctx);
    /* NULL clears any existing highlight when no match is found */
    bsg_view_obj_set_illum(gedp, ctx.result);
    return ctx.result;
}


/**
 * Phase 9.3 (drawing_stack_modernization B5 residual): return the
 * highlight-state revision counter.  Bumped on every transition of
 * gd_illum_solid and on every bsg_node_field_touch on the currently
 * illuminated solid (delivered through the registered NodeSensor).
 *
 * Cache a snapshot, then compare against a later live read to detect
 * "highlight may have changed since I last looked" without subscribing
 * to the NodeSensor or calling bsg_view_obj_get_illum repeatedly.
 */
uint64_t
bsg_view_obj_illum_rev(const struct ged *gedp)
{
    if (!gedp || !gedp->i || !gedp->i->ged_gdp)
        return 0;
    return gedp->i->ged_gdp->gd_illum_rev;
}


/* ================================================================== */
/* B4: mater-revision counter — public API                            */
/* ================================================================== */

/**
 * Return the current mater-revision counter.  The counter is bumped by
 * bsg_view_obj_bump_mater_rev() whenever the material/color table changes.
 * color_from_soltab() does NOT bump the counter; it only stamps per-shape
 * s_color_rev fields to match the current counter value.
 *
 * Consumers that cache per-solid colors can store a snapshot of this
 * value and skip re-querying when the counter is unchanged.  For example:
 *
 *   if (saved_mater_rev != bsg_view_obj_mater_rev(gedp)) {
 *       bsg_view_obj_color_from_soltab(gedp);
 *       saved_mater_rev = bsg_view_obj_mater_rev(gedp);
 *   }
 */
uint64_t
bsg_view_obj_mater_rev(const struct ged *gedp)
{
    if (!gedp)
        return 0;
    return gedp->i->ged_gdp->gd_mater_rev;
}


/**
 * Bump the mater-revision counter (B4 activation).
 *
 * Must be called after any operation that changes the effective material
 * or color table so that the next bsg_view_obj_color_from_soltab() call
 * recolors shapes whose s_color_rev is now stale.
 *
 * Typical callers: 'color', 'mater', 'rmater', 'edmater' commands and
 * any other code path that mutates dbip->dbi_mater or per-combination
 * shader/rgb attributes.
 */
void
bsg_view_obj_bump_mater_rev(struct ged *gedp)
{
    if (!gedp)
        return;
    _sg_bump_mater_rev(gedp);
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
