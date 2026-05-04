/*                B S G _ V I E W _ O B J . C
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
/** @file libged/bsg_view_obj.c
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
 * allocated via bv_obj_get(v, BV_DB_OBJS) as before — they remain
 * in gv_objs.db_objs for the render path (bsg_scene_root_sync).
 *
 * gv_objs ptbls become a flat rendering index; the tree is the
 * authoritative store for group membership, path identity, and
 * ordered iteration.
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/color.h"
#include "bv/plot3.h"
#include "bg/clip.h"
#include "bsg/defines.h"
#include "bsg/visit.h"

#include "ged.h"
#include "ged/bsg_view_obj.h"
#include "./ged_private.h"

/* ------------------------------------------------------------------ */
/* Internal macros                                                     */
/* ------------------------------------------------------------------ */

#define FIRST_SOLID(_bdata)  ((_bdata)->s_fullpath.fp_names[0])
#define FREE_BV_SCENE_OBJ(p, fp, vlf) { \
        BU_LIST_APPEND(fp, &((p)->l)); \
        BV_FREE_VLIST(vlf, &((p)->s_vlist)); }

/* Increment the structural revision counter.  Call on every add/remove
 * of a group or shape (but NOT on incremental solid-data updates). */
#define _sg_bump_rev(_gedp) (++((_gedp)->i->ged_gdp->gd_draw_rev))

/* defined in draw_calc.cpp */
extern fastf_t brep_est_avg_curve_len(struct rt_brep_internal *bi);
extern void createDListSolid(struct bv_scene_obj *sp);

/* ------------------------------------------------------------------ */
/* BSG group-tree helpers (file-private)                               */
/* ------------------------------------------------------------------ */

/*
 * Lazily create (on first draw) and return the per-GED draw root.
 */
static struct bv_scene_obj *
_sg_root(struct ged *gedp)
{
    if (gedp->i->ged_gdp->gd_draw_root)
        return gedp->i->ged_gdp->gd_draw_root;

    struct bview *v = gedp->ged_gvp;
    if (!v)
        return NULL;

    struct bv_scene_obj *root = bv_obj_create(v, BV_CHILD_OBJS);
    if (!root)
        return NULL;

    root->s_type_flags = BSG_NODE_GROUP;
    root->s_flag = UP;
    root->parent = NULL;
    bu_vls_sprintf(&root->s_name, "_draw_root");

    gedp->i->ged_gdp->gd_draw_root = root;
    return root;
}


/*
 * Find the existing _overlays subgroup under the draw root, or NULL if
 * it has not been created yet.  Does not create it.
 */
static struct bv_scene_obj *
_sg_find_overlay_group(struct ged *gedp)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return NULL;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, i);
        if (BU_STR_EQUAL("_overlays", bu_vls_cstr(&g->s_name)))
            return g;
    }
    return NULL;
}


/*
 * Lazily create (on first overlay insertion) and return the _overlays
 * subgroup.  This group lives as a direct child of the draw root and
 * collects all pseudo-solid / invented overlay shapes.
 */
static struct bv_scene_obj *
_sg_overlay_root(struct ged *gedp)
{
    struct bv_scene_obj *ov = _sg_find_overlay_group(gedp);
    if (ov)
        return ov;

    struct bv_scene_obj *root = _sg_root(gedp);
    if (!root)
        return NULL;

    struct bview *v = gedp->ged_gvp;
    if (!v)
        return NULL;

    ov = bv_obj_create(v, BV_CHILD_OBJS);
    if (!ov)
        return NULL;

    ov->s_type_flags = BSG_NODE_GROUP;
    ov->s_flag = UP;
    ov->dp = NULL;
    ov->parent = root;
    bu_vls_sprintf(&ov->s_name, "_overlays");
    bu_ptbl_ins(&root->children, (long *)ov);
    return ov;
}


/*
 * Erase an overlay shape by name from the _overlays group.  If the
 * _overlays group becomes empty it is freed and removed from the root.
 */
static void
_sg_erase_overlay_by_name(struct ged *gedp, const char *name)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    struct bv_scene_obj *ov = _sg_find_overlay_group(gedp);
    if (!ov)
        return;

    struct bv_scene_obj *free_scene_obj = bv_set_fsos(&gedp->ged_views);
    struct bu_list *vlfree = &rt_vlfree;

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&ov->children); i++)
        bu_ptbl_ins(&snap, BU_PTBL_GET(&ov->children, i));

    for (size_t i = 0; i < BU_PTBL_LEN(&snap); i++) {
        struct bv_scene_obj *sp =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, i);
        if (!BU_STR_EQUAL(name, bu_vls_cstr(&sp->s_name)))
            continue;
        ged_destroy_vlist_cb(gedp, sp->s_dlist, 1);
        bu_ptbl_rm(&ov->children, (const long *)sp);
        sp->parent = NULL;
        FREE_BV_SCENE_OBJ(sp, &free_scene_obj->l, vlfree);
        _sg_bump_rev(gedp);
    }
    bu_ptbl_free(&snap);

    /* Remove empty _overlays group from root */
    if (BU_PTBL_LEN(&ov->children) == 0) {
        bu_ptbl_rm(&root->children, (const long *)ov);
        ov->parent = NULL;
        struct bv_scene_obj *fso = ov->free_scene_obj;
        if (fso)
            FREE_BV_SCENE_OBJ(ov, &fso->l, ov->vlfree);
    }
}


/*
 * Recursively free all descendants of @p g (shapes and nested sub-groups).
 * Does NOT free @p g itself.
 */
static void
_sg_free_children_recursive(struct ged *gedp, struct bv_scene_obj *g,
                              struct bv_scene_obj *fso, struct bu_list *vlf)
{
    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&g->children); i++)
        bu_ptbl_ins(&snap, BU_PTBL_GET(&g->children, i));

    for (size_t i = 0; i < BU_PTBL_LEN(&snap); i++) {
        struct bv_scene_obj *child =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, i);
        if (child->s_type_flags & BSG_NODE_GROUP) {
            _sg_free_children_recursive(gedp, child, fso, vlf);
            child->parent = NULL;
            struct bv_scene_obj *cfso = child->free_scene_obj;
            if (cfso)
                FREE_BV_SCENE_OBJ(child, &cfso->l, child->vlfree);
        } else {
            ged_destroy_vlist_cb(gedp, child->s_dlist, 1);
            child->parent = NULL;
            FREE_BV_SCENE_OBJ(child, &fso->l, vlf);
        }
    }
    bu_ptbl_free(&snap);
    bu_ptbl_reset(&g->children);
}


static void
_sg_free_group_contents(struct ged *gedp, struct bv_scene_obj *g)
{
    if (BU_PTBL_LEN(&g->children) == 0)
        return;
    struct bv_scene_obj *fso = bv_set_fsos(&gedp->ged_views);
    struct bu_list *vlf = &rt_vlfree;
    _sg_free_children_recursive(gedp, g, fso, vlf);
}


/*
 * Free a subgroup: free its descendants, then free the group node itself,
 * removing it from its parent.
 */
static void
_sg_free_group(struct ged *gedp, struct bv_scene_obj *g)
{
    _sg_free_group_contents(gedp, g);

    struct bv_scene_obj *parent = (struct bv_scene_obj *)g->parent;
    if (parent)
        bu_ptbl_rm(&parent->children, (const long *)g);

    _sg_bump_rev(gedp);

    g->parent = NULL;
    struct bv_scene_obj *fso = g->free_scene_obj;
    if (fso)
        FREE_BV_SCENE_OBJ(g, &fso->l, g->vlfree);
}


/*
 * Return the depth of @p g in the draw tree (root = 0, root children = 1, …).
 */
static int
_sg_tree_depth(const struct bv_scene_obj *g)
{
    int depth = 0;
    const struct bv_scene_obj *cur = g;
    while (cur->parent) {
        depth++;
        cur = (const struct bv_scene_obj *)cur->parent;
    }
    return depth;
}


/*
 * Find or create a BSG_NODE_GROUP child of @p parent named @p comp_name.
 * Returns the (possibly new) child group, or NULL on failure.
 */
static struct bv_scene_obj *
_sg_find_or_create_child_group(struct ged *gedp, struct bv_scene_obj *parent,
                                const char *comp_name)
{
    for (size_t i = 0; i < BU_PTBL_LEN(&parent->children); i++) {
        struct bv_scene_obj *c =
            (struct bv_scene_obj *)BU_PTBL_GET(&parent->children, i);
        if ((c->s_type_flags & BSG_NODE_GROUP) &&
            BU_STR_EQUAL(comp_name, bu_vls_cstr(&c->s_name)))
            return c;
    }

    struct bview *v = gedp->ged_gvp;
    if (!v)
        return NULL;

    struct bv_scene_obj *child = bv_obj_create(v, BV_CHILD_OBJS);
    if (!child)
        return NULL;

    struct directory *dp = db_lookup(gedp->dbip, comp_name, LOOKUP_QUIET);
    child->s_type_flags = BSG_NODE_GROUP;
    child->s_flag       = UP;
    child->s_iflag      = DOWN;
    child->dp           = (void *)dp;
    child->parent       = parent;
    bu_vls_sprintf(&child->s_name, "%s", comp_name);
    bu_ptbl_ins(&parent->children, (long *)child);
    _sg_bump_rev(gedp);
    return child;
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
        return _sg_find_or_create_child_group(gedp, root, cp);
    }

    if (pathcomp.fp_len == 0) {
        db_free_full_path(&pathcomp);
        return NULL;
    }

    /* Navigate/create one group node per path component starting from root */
    struct bv_scene_obj *cur = root;
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

/*
 * Navigate from @p parent following subpath->fp_names[depth_start..fp_len-1]
 * and erase the matching sub-group at the deepest level found.
 * After erasure the caller is responsible for removing empty ancestors.
 */
static void
_sg_erase_nested_subpath(struct ged *gedp, struct bv_scene_obj *parent,
                          struct db_full_path *subpath, size_t depth_start)
{
    struct bv_scene_obj *cur = parent;
    for (size_t i = depth_start; i < subpath->fp_len; i++) {
        const char *comp = subpath->fp_names[i]->d_namep;
        struct bv_scene_obj *child = NULL;
        for (size_t j = 0; j < BU_PTBL_LEN(&cur->children); j++) {
            struct bv_scene_obj *c =
                (struct bv_scene_obj *)BU_PTBL_GET(&cur->children, j);
            if ((c->s_type_flags & BSG_NODE_GROUP) &&
                BU_STR_EQUAL(bu_vls_cstr(&c->s_name), comp)) {
                child = c;
                break;
            }
        }
        if (!child)
            return;

        if (i == subpath->fp_len - 1) {
            _sg_free_group_contents(gedp, child);
            bu_ptbl_rm(&cur->children, (const long *)child);
            _sg_bump_rev(gedp);
            child->parent = NULL;
            struct bv_scene_obj *fso = child->free_scene_obj;
            if (fso)
                FREE_BV_SCENE_OBJ(child, &fso->l, child->vlfree);
            return;
        }
        cur = child;
    }
}


static void
_sg_erase_path(struct ged *gedp, const char *path, int allow_split)
{
    (void)allow_split; /* no longer needed with nested group tree */

    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    struct db_i *dbip = gedp->dbip;
    struct db_full_path subpath;
    int found_subpath = (db_string_to_path(&subpath, dbip, path) == 0);

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, i);
        if (!BU_STR_EQUAL("_overlays", bu_vls_cstr(&g->s_name)))
            bu_ptbl_ins(&snap, (long *)g);
    }

    for (size_t gi = 0; gi < BU_PTBL_LEN(&snap); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, gi);

        if (BU_STR_EQUAL(path, bu_vls_cstr(&g->s_name))) {
            _sg_free_group(gedp, g);
            break;
        }

        if (!found_subpath)
            continue;

        /* Check if root child is an ancestor of the erase path */
        struct db_full_path gdlpath;
        if (db_string_to_path(&gdlpath, dbip, bu_vls_cstr(&g->s_name)) != 0)
            continue;

        int is_ancestor = db_full_path_match_top(&gdlpath, &subpath);
        size_t ancestor_depth = gdlpath.fp_len;
        db_free_full_path(&gdlpath);

        if (is_ancestor && ancestor_depth < subpath.fp_len) {
            _sg_erase_nested_subpath(gedp, g, &subpath, ancestor_depth);
            if (BU_PTBL_LEN(&g->children) == 0)
                _sg_free_group(gedp, g);
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
    for (size_t i = 0; i < BU_PTBL_LEN(&parent->children); i++) {
        struct bv_scene_obj *c =
            (struct bv_scene_obj *)BU_PTBL_GET(&parent->children, i);
        if (c->s_type_flags & BSG_NODE_GROUP)
            bu_ptbl_ins(&snap, (long *)c);
    }

    for (size_t i = 0; i < BU_PTBL_LEN(&snap); i++) {
        struct bv_scene_obj *c =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, i);
        if (BU_STR_EQUAL(bu_vls_cstr(&c->s_name), name)) {
            _sg_free_group_contents(gedp, c);
            bu_ptbl_rm(&parent->children, (const long *)c);
            _sg_bump_rev(gedp);
            c->parent = NULL;
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
_sg_erase_all_names(struct ged *gedp, const char *name, int skip_first)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    _sg_erase_overlay_by_name(gedp, name);

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, i);
        if (!BU_STR_EQUAL("_overlays", bu_vls_cstr(&g->s_name)))
            bu_ptbl_ins(&snap, (long *)g);
    }

    for (size_t gi = 0; gi < BU_PTBL_LEN(&snap); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, gi);

        /* Check root child's path components for a direct name match */
        char *dup_path = bu_strdup(bu_vls_cstr(&g->s_name));
        char *tok;
        int first = 1, found = 0;
        tok = strtok(dup_path, "/");
        while (tok) {
            if (first) {
                first = 0;
                if (skip_first) {
                    tok = strtok(NULL, "/");
                    continue;
                }
            }
            if (BU_STR_EQUAL(tok, name)) {
                _sg_free_group(gedp, g);
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
_sg_erase_all_paths(struct ged *gedp, const char *path, int skip_first)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    struct db_i *dbip = gedp->dbip;
    struct db_full_path subpath;

    if (db_string_to_path(&subpath, dbip, path) != 0)
        return;

    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, i);
        g->s_iflag = DOWN;
    }

    int restart;
    do {
        restart = 0;
        for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
            struct bv_scene_obj *g =
                (struct bv_scene_obj *)BU_PTBL_GET(&root->children, i);
            if (g->s_iflag == UP)
                continue;
            g->s_iflag = UP;

            struct db_full_path fullpath;
            if (db_string_to_path(&fullpath, dbip,
                                   bu_vls_cstr(&g->s_name)) != 0)
                continue;

            /* Case A: root child is fully contained by (or equal to) subpath */
            if (db_full_path_subset(&fullpath, &subpath, skip_first)) {
                db_free_full_path(&fullpath);
                _sg_free_group(gedp, g);
                restart = 1;
                break;
            }

            /* Case B: root child is an ancestor of subpath — navigate sub-tree */
            if (!skip_first &&
                db_full_path_match_top(&fullpath, &subpath) &&
                fullpath.fp_len < subpath.fp_len) {
                size_t depth = fullpath.fp_len;
                db_free_full_path(&fullpath);
                _sg_erase_nested_subpath(gedp, g, &subpath, depth);
                if (BU_PTBL_LEN(&g->children) == 0)
                    _sg_free_group(gedp, g);
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

struct _sph_ctx {
    vect_t *min;
    vect_t *max;
    int pflag;
    int *is_empty;
};

static int
_sph_solid_cb(bsg_node *n, void *ud)
{
    struct bv_scene_obj *sp = (struct bv_scene_obj *)n;
    struct _sph_ctx *ctx = (struct _sph_ctx *)ud;
    if (!ctx->pflag && (sp->s_type_flags & BSG_PAYLOAD_OVERLAY))
        return 1;
    vect_t minus, plus;
    minus[X] = sp->s_center[X] - sp->s_size;
    minus[Y] = sp->s_center[Y] - sp->s_size;
    minus[Z] = sp->s_center[Z] - sp->s_size;
    VMIN(*(ctx->min), minus);
    plus[X] = sp->s_center[X] + sp->s_size;
    plus[Y] = sp->s_center[Y] + sp->s_size;
    plus[Z] = sp->s_center[Z] + sp->s_size;
    VMAX(*(ctx->max), plus);
    *(ctx->is_empty) = 0;
    return 1;
}

static int
_sg_bounding_sph(struct ged *gedp, vect_t *min, vect_t *max, int pflag)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;

    VSETALL((*min),  INFINITY);
    VSETALL((*max), -INFINITY);

    if (!root)
        return 1;

    int is_empty = 1;
    struct _sph_ctx ctx = { min, max, pflag, &is_empty };
    bsg_visit((bsg_node *)root, BSG_NODE_SHAPE, _sph_solid_cb, &ctx);
    return is_empty;
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
solid_append_vlist(struct bv_scene_obj *sp, struct bv_vlist *vlist)
{
    if (BU_LIST_IS_EMPTY(&(sp->s_vlist)))
        sp->s_vlen = 0;
    sp->s_vlen += bv_vlist_cmd_cnt(vlist);
    BU_LIST_APPEND_LIST(&(sp->s_vlist), &(vlist->l));
}

static void
solid_copy_vlist(struct db_i *UNUSED(dbip), struct bv_scene_obj *sp,
                 struct bv_vlist *vlist, struct bu_list *vlfree)
{
    BU_LIST_INIT(&(sp->s_vlist));
    bv_vlist_copy(vlfree, &(sp->s_vlist), (struct bu_list *)vlist);
    sp->s_vlen = bv_vlist_cmd_cnt((struct bv_vlist *)(&(sp->s_vlist)));
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
    sp->s_type_flags |= BSG_NODE_SHAPE | BSG_PAYLOAD_OVERLAY;
    bu_vls_sprintf(&sp->s_name, "%s", name);

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
        sp->parent = ov;
        bu_ptbl_ins(&ov->children, (long *)sp);
        _sg_bump_rev(gedp);
    }

    sp->s_iflag              = DOWN;
    sp->s_soldash            = 0;
    sp->s_old.s_Eflag        = 1;
    sp->s_color[0]           = sp->s_old.s_basecolor[0] = (rgb >> 16) & 0xFF;
    sp->s_color[1]           = sp->s_old.s_basecolor[1] = (rgb >>  8) & 0xFF;
    sp->s_color[2]           = sp->s_old.s_basecolor[2] = (rgb      ) & 0xFF;
    sp->s_old.s_regionid     = 0;
    sp->s_dlist              = 0;
    sp->s_old.s_uflag        = 0;
    sp->s_old.s_dflag        = 0;
    sp->s_old.s_cflag        = 0;
    sp->s_old.s_wflag        = 0;
    sp->s_os->transparency   = transparency;
    sp->s_os->s_dmode        = dmode;

    if (csoltab)
        color_soltab(gedp->dbip, sp);

    ged_create_vlist_solid_cb(gedp, sp);
    return 0;
}


/* ------------------------------------------------------------------ */
/* dl_set_iflag                                                        */
/* ------------------------------------------------------------------ */

static int
_iflag_solid_cb(bsg_node *n, void *ud)
{
    struct bv_scene_obj *sp = (struct bv_scene_obj *)n;
    sp->s_iflag = *(char *)ud;
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
/* color_from_soltab                                                   */
/* ------------------------------------------------------------------ */

static int
_color_solid_cb(bsg_node *n, void *ud)
{
    struct bv_scene_obj *sp = (struct bv_scene_obj *)n;
    color_soltab((struct db_i *)ud, sp);
    return 1;
}

static void
_sg_color_soltab(struct ged *gedp)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;
    bsg_visit((bsg_node *)root, BSG_NODE_SHAPE, _color_solid_cb,
              (void *)gedp->dbip);
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


struct bv_scene_obj *
bsg_view_obj_lookup_or_add_path(struct ged *gedp, const char *path)
{
    if (!gedp || !path)
        return NULL;
    return (struct bv_scene_obj *)_sg_add_path(gedp, path);
}


void
bsg_view_obj_erase_by_path(struct ged *gedp, const char *path, int allow_split)
{
    if (!gedp || !path)
        return;
    _sg_erase_path(gedp, path, allow_split);
}


void
bsg_view_obj_erase_by_name(struct ged *gedp, const char *name, int skip_first)
{
    if (!gedp || !name)
        return;
    _sg_erase_all_names(gedp, name, skip_first);
}


void
bsg_view_obj_erase_all_paths(struct ged *gedp, const char *path, int skip_first)
{
    if (!gedp || !path)
        return;
    _sg_erase_all_paths(gedp, path, skip_first);
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
    for (size_t gi = 0; gi < BU_PTBL_LEN(&root->children); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, gi);
        if (BU_STR_EQUAL("_overlays", bu_vls_cstr(&g->s_name)))
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
    for (size_t gi = 0; gi < BU_PTBL_LEN(&root->children); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, gi);
        if (BU_STR_EQUAL("_overlays", bu_vls_cstr(&g->s_name)))
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
    return sp->parent; /* O(1) via parent pointer */
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

    for (size_t gi = 0; gi < BU_PTBL_LEN(&root->children); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, gi);
        if (!(*cb)(g, userdata))
            return;
    }
}


struct bv_scene_obj *
bsg_view_obj_group_first_solid(struct bv_scene_obj *group)
{
    if (!group)
        return NULL;
    if (BU_PTBL_LEN(&group->children) == 0)
        return NULL;
    return (struct bv_scene_obj *)BU_PTBL_GET(&group->children, 0);
}


struct bv_scene_obj *
bsg_view_obj_group_last_solid(struct bv_scene_obj *group)
{
    if (!group)
        return NULL;
    size_t n = BU_PTBL_LEN(&group->children);
    if (n == 0)
        return NULL;
    return (struct bv_scene_obj *)BU_PTBL_GET(&group->children, n - 1);
}


int
bsg_view_obj_group_is_nonempty(struct bv_scene_obj *group)
{
    if (!group)
        return 0;
    return (BU_PTBL_LEN(&group->children) > 0) ? 1 : 0;
}


const char *
bsg_view_obj_group_path(struct bv_scene_obj *group)
{
    if (!group)
        return NULL;
    return bu_vls_cstr(&group->s_name);
}


void
bsg_view_obj_append_to_last_group(struct ged *gedp, struct bv_scene_obj *sp)
{
    if (!gedp || !sp)
        return;

    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root || BU_PTBL_LEN(&root->children) == 0)
        return;

    struct bv_scene_obj *g =
        (struct bv_scene_obj *)BU_PTBL_GET(&root->children,
                                            BU_PTBL_LEN(&root->children) - 1);
    sp->parent = g;
    bu_ptbl_ins(&g->children, (long *)sp);
}


void
bsg_view_obj_group_set_path(struct bv_scene_obj *group, const char *new_path)
{
    if (!group || !new_path)
        return;
    bu_vls_sprintf(&group->s_name, "%s", new_path);
}


int
bsg_view_obj_group_is_phony(struct bv_scene_obj *group)
{
    if (!group)
        return 0;
    /* The _overlays group is the only pseudo-group; real drawn-path
     * groups always have a valid dp and are not phony. */
    return BU_STR_EQUAL("_overlays", bu_vls_cstr(&group->s_name)) ? 1 : 0;
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
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++)
        bu_ptbl_ins(&snap, BU_PTBL_GET(&root->children, i));

    for (size_t gi = 0; gi < BU_PTBL_LEN(&snap); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, gi);
        _sg_free_group_contents(gedp, g);
        g->parent = NULL;
        struct bv_scene_obj *fso = g->free_scene_obj;
        if (fso)
            FREE_BV_SCENE_OBJ(g, &fso->l, g->vlfree);
    }
    bu_ptbl_free(&snap);

    /* Clear all subgroups from the root but leave the root node alive so
     * that GED_CHECK_DRAWABLE (which tests ged_dl() != NULL) continues to
     * pass after a zap.  This preserves the invariant established by
     * bsg_view_obj_ensure_root() in ged_open(). */
    bu_ptbl_reset(&root->children);

    /* Reset revision counter: after a full zap the drawn set is empty,
     * so the hash should return 0 (matching initial state). */
    gedp->i->ged_gdp->gd_draw_rev = 0;
}


int
bsg_view_obj_has_groups(struct ged *gedp)
{
    if (!gedp || !gedp->i || !gedp->i->ged_gdp)
        return 0;
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return 0;
    return (BU_PTBL_LEN(&root->children) > 0) ? 1 : 0;
}


void
bsg_view_obj_append_solid_to_group(struct bv_scene_obj *group, struct bv_scene_obj *sp)
{
    if (!group || !sp)
        return;
    sp->parent = group;
    bu_ptbl_ins(&group->children, (long *)sp);
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
