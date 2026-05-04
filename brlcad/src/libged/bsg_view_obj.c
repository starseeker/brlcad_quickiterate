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
 *     └─ subgroup per drawn path (BSG_NODE_GROUP)
 *          ├─ s_name  = drawn path string ("all/hull")
 *          ├─ dp      = (void *)(struct directory *) leaf dir entry
 *          ├─ parent  = draw root
 *          └─ children ptbl of BSG_NODE_SHAPE bv_scene_obj leaves
 *               └─ parent = containing subgroup
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

#include "bu/hash.h"
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
 * Free all shape nodes inside a subgroup (dlist callback + return to
 * free pool).  Does NOT free the group node itself.
 */
static void
_sg_free_group_contents(struct ged *gedp, struct bv_scene_obj *g)
{
    struct db_i *dbip = gedp->dbip;
    struct bv_scene_obj *free_scene_obj = bv_set_fsos(&gedp->ged_views);
    struct bu_list *vlfree = &rt_vlfree;

    if (BU_PTBL_LEN(&g->children) == 0)
        return;

    /* Single range dlist callback (matches existing zap behaviour) */
    if (gedp->ged_destroy_vlist_callback != GED_DESTROY_VLIST_FUNC_NULL) {
        struct bv_scene_obj *first =
            (struct bv_scene_obj *)BU_PTBL_GET(&g->children, 0);
        struct bv_scene_obj *last =
            (struct bv_scene_obj *)BU_PTBL_GET(&g->children,
                                                BU_PTBL_LEN(&g->children) - 1);
        ged_destroy_vlist_cb(gedp, first->s_dlist,
                             last->s_dlist - first->s_dlist + 1);
    }

    for (size_t i = 0; i < BU_PTBL_LEN(&g->children); i++) {
        struct bv_scene_obj *sp =
            (struct bv_scene_obj *)BU_PTBL_GET(&g->children, i);

        /* Overlay shapes carry BSG_PAYLOAD_OVERLAY and have no db entry
         * to delete.  Regular shapes that somehow still have a phony dir
         * entry (pre-transition data) have their entry cleaned up here. */
        if (!(sp->s_type_flags & BSG_PAYLOAD_OVERLAY) && sp->s_u_data) {
            struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
            if (bdata->s_fullpath.fp_len > 0 &&
                bdata->s_fullpath.fp_names != NULL) {
                struct directory *dp = FIRST_SOLID(bdata);
                RT_CK_DIR(dp);
                if (dp->d_addr == RT_DIR_PHONY_ADDR)
                    (void)db_dirdelete(dbip, dp);
            }
        }

        sp->parent = NULL;
        FREE_BV_SCENE_OBJ(sp, &free_scene_obj->l, vlfree);
    }

    bu_ptbl_reset(&g->children);
}


/*
 * Free an individual subgroup: free its shapes, then free the group node
 * itself, removing it from the draw root.
 */
static void
_sg_free_group(struct ged *gedp, struct bv_scene_obj *g)
{
    _sg_free_group_contents(gedp, g);

    /* Remove from draw root */
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (root)
        bu_ptbl_rm(&root->children, (const long *)g);

    g->parent = NULL;

    /* Return group node to free pool (children already cleared above) */
    struct bv_scene_obj *fso = g->free_scene_obj;
    if (fso)
        FREE_BV_SCENE_OBJ(g, &fso->l, g->vlfree);
}


/*
 * Add (or return existing) subgroup for @p name under the draw root.
 * Equivalent to the old dl_addToDisplay().
 */
static struct bv_scene_obj *
_sg_add_path(struct ged *gedp, const char *name)
{
    struct bview *v = gedp->ged_gvp;
    if (!v)
        return NULL;

    struct bv_scene_obj *root = _sg_root(gedp);
    if (!root)
        return NULL;

    struct db_i *dbip = gedp->dbip;

    /* Resolve leaf directory entry */
    const char *cp = strrchr(name, '/');
    cp = cp ? cp + 1 : name;
    struct directory *dp = db_lookup(dbip, cp, LOOKUP_NOISY);
    if (dp == RT_DIR_NULL)
        return NULL;

    /* Parse full path for ancestor-match check */
    struct db_full_path namepath;
    int found_namepath = (db_string_to_path(&namepath, dbip, name) == 0);

    /* Check if path (or ancestor) already present */
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, i);

        if (BU_STR_EQUAL(name, bu_vls_cstr(&g->s_name))) {
            if (found_namepath)
                db_free_full_path(&namepath);
            return g;
        }

        if (found_namepath) {
            struct db_full_path gdlpath;
            if (db_string_to_path(&gdlpath, dbip,
                                  bu_vls_cstr(&g->s_name)) == 0) {
                int match = db_full_path_match_top(&gdlpath, &namepath);
                db_free_full_path(&gdlpath);
                if (match) {
                    db_free_full_path(&namepath);
                    return g;
                }
            }
        }
    }

    /* Create new subgroup node */
    struct bv_scene_obj *g = bv_obj_create(v, BV_CHILD_OBJS);
    if (!g) {
        if (found_namepath)
            db_free_full_path(&namepath);
        return NULL;
    }

    g->s_type_flags = BSG_NODE_GROUP;
    g->s_flag       = UP;
    g->s_iflag      = DOWN; /* wflag / visited marker */
    g->dp           = (void *)dp;
    g->parent       = root;
    bu_vls_sprintf(&g->s_name, "%s", name);

    bu_ptbl_ins(&root->children, (long *)g);

    if (found_namepath)
        db_free_full_path(&namepath);

    return g;
}


/*
 * Split subgroup @p g at path depth @p newlen for shape @p sp.
 * Used by the erase-by-subpath path.  Equivalent to headsolid_split().
 */
static void
_sg_split_at(struct ged *gedp, struct bv_scene_obj *sp, size_t newlen)
{
    if (!sp->s_u_data)
        return;
    struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;

    size_t savelen = bdata->s_fullpath.fp_len;
    bdata->s_fullpath.fp_len = newlen;
    char *pathname = db_path_to_string(&bdata->s_fullpath);
    bdata->s_fullpath.fp_len = savelen;

    struct bv_scene_obj *new_g = _sg_add_path(gedp, pathname);
    bu_free(pathname, "_sg_split_at pathname");
    if (!new_g)
        return;

    /* Remove sp from its current group */
    struct bv_scene_obj *old_g = (struct bv_scene_obj *)sp->parent;
    if (old_g)
        bu_ptbl_rm(&old_g->children, (const long *)sp);

    /* Insert into new group */
    sp->parent = new_g;
    bu_ptbl_ins(&new_g->children, (long *)sp);
}


/*
 * Recursively split subgroup @p g so that every shape is placed in the
 * deepest subgroup matching its path prefix (down to depth @p path->fp_len+1).
 * Equivalent to headsolid_splitGDL().
 */
static int
_sg_split_gdl(struct ged *gedp, struct bv_scene_obj *g,
              struct db_full_path *path)
{
    size_t newlen = path->fp_len + 1;

    /* Need a stable snapshot because _sg_split_at modifies children */
    struct bu_ptbl snapshot = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&g->children); i++)
        bu_ptbl_ins(&snapshot, BU_PTBL_GET(&g->children, i));

    if (BU_PTBL_LEN(&snapshot) == 0) {
        bu_ptbl_free(&snapshot);
        return 0;
    }

    if (newlen < 3) {
        for (size_t i = 0; i < BU_PTBL_LEN(&snapshot); i++) {
            struct bv_scene_obj *sp =
                (struct bv_scene_obj *)BU_PTBL_GET(&snapshot, i);
            _sg_split_at(gedp, sp, newlen);
        }
    } else {
        for (size_t i = 0; i < BU_PTBL_LEN(&snapshot); i++) {
            struct bv_scene_obj *sp =
                (struct bv_scene_obj *)BU_PTBL_GET(&snapshot, i);
            if (!sp->s_u_data)
                continue;
            struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
            if (db_full_path_match_top(path, &bdata->s_fullpath))
                _sg_split_at(gedp, sp, newlen);
        }
        --path->fp_len;
        _sg_split_gdl(gedp, g, path);
        ++path->fp_len;
    }

    bu_ptbl_free(&snapshot);
    return 1;
}


/* ------------------------------------------------------------------ */
/* Public-facing erase helpers (was dl_erasePathFromDisplay etc.)     */
/* ------------------------------------------------------------------ */

static void
_sg_erase_path(struct ged *gedp, const char *path, int allow_split)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    struct db_i *dbip = gedp->dbip;
    struct bv_scene_obj *free_scene_obj = bv_set_fsos(&gedp->ged_views);
    struct bu_list *vlfree = &rt_vlfree;

    struct db_full_path subpath;
    int found_subpath = (db_string_to_path(&subpath, dbip, path) == 0);

    /* Snapshot children so we can mutate the ptbl during iteration */
    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++)
        bu_ptbl_ins(&snap, BU_PTBL_GET(&root->children, i));

    struct bv_scene_obj *last_g =
        (BU_PTBL_LEN(&root->children) > 0) ?
        (struct bv_scene_obj *)BU_PTBL_GET(&root->children,
                                            BU_PTBL_LEN(&root->children) - 1)
        : NULL;
    (void)last_g;

    for (size_t gi = 0; gi < BU_PTBL_LEN(&snap); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, gi);

        if (BU_STR_EQUAL(path, bu_vls_cstr(&g->s_name))) {
            /* Exact match: free whole group */
            _sg_free_group_contents(gedp, g);
            bu_ptbl_rm(&root->children, (const long *)g);
            g->parent = NULL;
            struct bv_scene_obj *fso = g->free_scene_obj;
            if (fso) FREE_BV_SCENE_OBJ(g, &fso->l, g->vlfree);
            break;
        }

        if (!found_subpath)
            continue;

        /* Partial match: erase matching shapes from this group */
        int need_split = 0;

        /* Snapshot shape children */
        struct bu_ptbl ssnap = BU_PTBL_INIT_ZERO;
        for (size_t si = 0; si < BU_PTBL_LEN(&g->children); si++)
            bu_ptbl_ins(&ssnap, BU_PTBL_GET(&g->children, si));

        for (size_t si = 0; si < BU_PTBL_LEN(&ssnap); si++) {
            struct bv_scene_obj *sp =
                (struct bv_scene_obj *)BU_PTBL_GET(&ssnap, si);
            if (!sp->s_u_data)
                continue;
            struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
            if (!db_full_path_match_top(&subpath, &bdata->s_fullpath))
                continue;

            ged_destroy_vlist_cb(gedp, sp->s_dlist, 1);
            bu_ptbl_rm(&g->children, (const long *)sp);
            sp->parent = NULL;
            FREE_BV_SCENE_OBJ(sp, &free_scene_obj->l, vlfree);
            need_split = 1;
        }
        bu_ptbl_free(&ssnap);

        if (BU_PTBL_LEN(&g->children) == 0) {
            /* Group is now empty — remove it */
            bu_ptbl_rm(&root->children, (const long *)g);
            g->parent = NULL;
            struct bv_scene_obj *fso = g->free_scene_obj;
            if (fso) FREE_BV_SCENE_OBJ(g, &fso->l, g->vlfree);
        } else if (allow_split && need_split) {
            /* Split remaining shapes into per-path subgroups */
            bu_ptbl_rm(&root->children, (const long *)g);
            --subpath.fp_len;
            _sg_split_gdl(gedp, g, &subpath);
            ++subpath.fp_len;
            /* Free the now-empty original group */
            g->parent = NULL;
            struct bv_scene_obj *fso = g->free_scene_obj;
            if (fso) FREE_BV_SCENE_OBJ(g, &fso->l, g->vlfree);
        }
    }

    bu_ptbl_free(&snap);
    if (found_subpath)
        db_free_full_path(&subpath);
}


static void
_sg_erase_all_subpaths_from_group(struct ged *gedp, struct bv_scene_obj *g,
                                   struct db_full_path *subpath,
                                   int skip_first)
{
    struct bv_scene_obj *free_scene_obj = bv_set_fsos(&gedp->ged_views);
    struct bu_list *vlfree = &rt_vlfree;

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&g->children); i++)
        bu_ptbl_ins(&snap, BU_PTBL_GET(&g->children, i));

    for (size_t i = 0; i < BU_PTBL_LEN(&snap); i++) {
        struct bv_scene_obj *sp =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, i);
        if (!sp->s_u_data)
            continue;
        struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
        if (!db_full_path_subset(&bdata->s_fullpath, subpath, skip_first))
            continue;
        ged_destroy_vlist_cb(gedp, sp->s_dlist, 1);
        bu_ptbl_rm(&g->children, (const long *)sp);
        sp->parent = NULL;
        FREE_BV_SCENE_OBJ(sp, &free_scene_obj->l, vlfree);
    }

    bu_ptbl_free(&snap);
}


static void
_sg_erase_all_names(struct ged *gedp, const char *name, int skip_first)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    /* Erase any overlay shape matching this name first. */
    _sg_erase_overlay_by_name(gedp, name);

    struct db_i *dbip = gedp->dbip;

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, i);
        /* Skip the _overlays meta-group; overlays are handled above. */
        if (BU_STR_EQUAL("_overlays", bu_vls_cstr(&g->s_name)))
            continue;
        bu_ptbl_ins(&snap, (long *)g);
    }

    for (size_t gi = 0; gi < BU_PTBL_LEN(&snap); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, gi);

        /* Walk path components */
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
            struct db_full_path subpath;
            if (db_string_to_path(&subpath, dbip, name) == 0) {
                _sg_erase_all_subpaths_from_group(gedp, g, &subpath,
                                                   skip_first);
                db_free_full_path(&subpath);
            }
        }
    }

    bu_ptbl_free(&snap);
}


static int
_sg_erase_first_subpath(struct ged *gedp, struct bv_scene_obj *g,
                         struct db_full_path *subpath, int skip_first)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    struct bv_scene_obj *free_scene_obj = bv_set_fsos(&gedp->ged_views);
    struct bu_list *vlfree = &rt_vlfree;

    struct bu_ptbl snap = BU_PTBL_INIT_ZERO;
    for (size_t i = 0; i < BU_PTBL_LEN(&g->children); i++)
        bu_ptbl_ins(&snap, BU_PTBL_GET(&g->children, i));

    int ret = 0;
    for (size_t i = 0; i < BU_PTBL_LEN(&snap); i++) {
        struct bv_scene_obj *sp =
            (struct bv_scene_obj *)BU_PTBL_GET(&snap, i);
        if (!sp->s_u_data)
            continue;
        struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
        if (!db_full_path_subset(&bdata->s_fullpath, subpath, skip_first))
            continue;

        struct db_full_path dup_path;
        db_full_path_init(&dup_path);
        size_t full_len = bdata->s_fullpath.fp_len;

        ged_destroy_vlist_cb(gedp, sp->s_dlist, 1);
        bdata->s_fullpath.fp_len = full_len - 1;
        db_dup_full_path(&dup_path, &bdata->s_fullpath);
        bdata->s_fullpath.fp_len = full_len;

        bu_ptbl_rm(&g->children, (const long *)sp);
        sp->parent = NULL;
        FREE_BV_SCENE_OBJ(sp, &free_scene_obj->l, vlfree);

        bu_ptbl_rm(&root->children, (const long *)g);
        ret = _sg_split_gdl(gedp, g, &dup_path);
        db_free_full_path(&dup_path);

        /* Free original (now-residual) group */
        g->parent = NULL;
        struct bv_scene_obj *fso = g->free_scene_obj;
        if (fso) FREE_BV_SCENE_OBJ(g, &fso->l, g->vlfree);

        break;
    }

    bu_ptbl_free(&snap);
    return ret;
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

    /* Clear visited flags (reuse s_iflag) */
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
            g->s_iflag = UP; /* mark visited */

            struct db_full_path fullpath;
            if (db_string_to_path(&fullpath, dbip,
                                  bu_vls_cstr(&g->s_name)) != 0)
                continue;

            if (db_full_path_subset(&fullpath, &subpath, skip_first)) {
                db_free_full_path(&fullpath);
                _sg_free_group(gedp, g);
                restart = 1; /* children ptbl changed — restart */
                break;
            }

            if (_sg_erase_first_subpath(gedp, g, &subpath, skip_first)) {
                db_free_full_path(&fullpath);
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

    VSETALL((*min),  INFINITY);
    VSETALL((*max), -INFINITY);

    if (!root)
        return 1;

    int is_empty = 1;

    for (size_t gi = 0; gi < BU_PTBL_LEN(&root->children); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, gi);

        for (size_t si = 0; si < BU_PTBL_LEN(&g->children); si++) {
            struct bv_scene_obj *sp =
                (struct bv_scene_obj *)BU_PTBL_GET(&g->children, si);

            /* When pflag is 0, exclude overlay shapes from the bounds
             * computation (B3: check BSG_PAYLOAD_OVERLAY, not RT_DIR_PHONY_ADDR). */
            if (!pflag && (sp->s_type_flags & BSG_PAYLOAD_OVERLAY))
                continue;

            vect_t minus, plus;
            minus[X] = sp->s_center[X] - sp->s_size;
            minus[Y] = sp->s_center[Y] - sp->s_size;
            minus[Z] = sp->s_center[Z] - sp->s_size;
            VMIN((*min), minus);
            plus[X] = sp->s_center[X] + sp->s_size;
            plus[Y] = sp->s_center[Y] + sp->s_size;
            plus[Z] = sp->s_center[Z] + sp->s_size;
            VMAX((*max), plus);
            is_empty = 0;
        }
    }

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

static void
_sg_set_iflag(struct ged *gedp, int iflag)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    for (size_t gi = 0; gi < BU_PTBL_LEN(&root->children); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, gi);
        for (size_t si = 0; si < BU_PTBL_LEN(&g->children); si++) {
            struct bv_scene_obj *sp =
                (struct bv_scene_obj *)BU_PTBL_GET(&g->children, si);
            sp->s_iflag = (char)iflag;
        }
    }
}


/* ------------------------------------------------------------------ */
/* name hash                                                           */
/* ------------------------------------------------------------------ */

static unsigned long long
_sg_name_hash(struct ged *gedp)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root || BU_PTBL_LEN(&root->children) == 0)
        return 0;

    struct bu_data_hash_state *state = bu_data_hash_create();
    if (!state)
        return 0;

    for (size_t gi = 0; gi < BU_PTBL_LEN(&root->children); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, gi);
        for (size_t si = 0; si < BU_PTBL_LEN(&g->children); si++) {
            struct bv_scene_obj *sp =
                (struct bv_scene_obj *)BU_PTBL_GET(&g->children, si);
            if (!sp->s_u_data)
                continue;
            struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
            for (size_t pi = 0; pi < bdata->s_fullpath.fp_len; pi++) {
                struct directory *pdp = bdata->s_fullpath.fp_names[pi];
                bu_data_hash_update(state, pdp->d_namep,
                                    strlen(pdp->d_namep));
            }
        }
    }

    unsigned long long hv = bu_data_hash_val(state);
    bu_data_hash_destroy(state);
    return hv;
}


/* ------------------------------------------------------------------ */
/* color_from_soltab                                                   */
/* ------------------------------------------------------------------ */

static void
_sg_color_soltab(struct ged *gedp)
{
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return;

    for (size_t gi = 0; gi < BU_PTBL_LEN(&root->children); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, gi);
        for (size_t si = 0; si < BU_PTBL_LEN(&g->children); si++) {
            struct bv_scene_obj *sp =
                (struct bv_scene_obj *)BU_PTBL_GET(&g->children, si);
            color_soltab(gedp->dbip, sp);
        }
    }
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


int
bsg_view_obj_is_nonempty(struct ged *gedp)
{
    if (!gedp)
        return 0;
    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return 0;
    for (size_t gi = 0; gi < BU_PTBL_LEN(&root->children); gi++) {
        struct bv_scene_obj *g =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, gi);
        if (BU_PTBL_LEN(&g->children) > 0)
            return 1;
    }
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
        if (BU_PTBL_LEN(&g->children) > 0)
            return (struct bv_scene_obj *)BU_PTBL_GET(&g->children, 0);
    }
    return NULL;
}


/*
 * DFS-order next solid with circular wraparound.
 * Finds sp's index within its parent group, then advances; wraps
 * across groups and back to the start.
 */
struct bv_scene_obj *
bsg_view_obj_next_solid(struct ged *gedp, struct bv_scene_obj *sp)
{
    if (!gedp || !sp)
        return NULL;

    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return NULL;

    struct bv_scene_obj *g = (struct bv_scene_obj *)sp->parent;
    if (!g)
        return NULL;

    /* Find sp index in its group */
    size_t si = (size_t)-1;
    for (size_t i = 0; i < BU_PTBL_LEN(&g->children); i++) {
        if ((struct bv_scene_obj *)BU_PTBL_GET(&g->children, i) == sp) {
            si = i;
            break;
        }
    }
    if (si == (size_t)-1)
        return NULL;

    /* Next within same group? */
    if (si + 1 < BU_PTBL_LEN(&g->children))
        return (struct bv_scene_obj *)BU_PTBL_GET(&g->children, si + 1);

    /* Find group index */
    size_t gi = (size_t)-1;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
        if ((struct bv_scene_obj *)BU_PTBL_GET(&root->children, i) == g) {
            gi = i;
            break;
        }
    }
    if (gi == (size_t)-1)
        return NULL;

    /* Advance to next non-empty group, wrapping */
    size_t ngroups = BU_PTBL_LEN(&root->children);
    for (size_t k = 1; k <= ngroups; k++) {
        size_t next_gi = (gi + k) % ngroups;
        struct bv_scene_obj *ng =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, next_gi);
        if (BU_PTBL_LEN(&ng->children) > 0)
            return (struct bv_scene_obj *)BU_PTBL_GET(&ng->children, 0);
    }

    return NULL;
}


struct bv_scene_obj *
bsg_view_obj_prev_solid(struct ged *gedp, struct bv_scene_obj *sp)
{
    if (!gedp || !sp)
        return NULL;

    struct bv_scene_obj *root = gedp->i->ged_gdp->gd_draw_root;
    if (!root)
        return NULL;

    struct bv_scene_obj *g = (struct bv_scene_obj *)sp->parent;
    if (!g)
        return NULL;

    size_t si = (size_t)-1;
    for (size_t i = 0; i < BU_PTBL_LEN(&g->children); i++) {
        if ((struct bv_scene_obj *)BU_PTBL_GET(&g->children, i) == sp) {
            si = i;
            break;
        }
    }
    if (si == (size_t)-1)
        return NULL;

    /* Prev within same group? */
    if (si > 0)
        return (struct bv_scene_obj *)BU_PTBL_GET(&g->children, si - 1);

    /* Find group index */
    size_t gi = (size_t)-1;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
        if ((struct bv_scene_obj *)BU_PTBL_GET(&root->children, i) == g) {
            gi = i;
            break;
        }
    }
    if (gi == (size_t)-1)
        return NULL;

    /* Retreat to prev non-empty group, wrapping */
    size_t ngroups = BU_PTBL_LEN(&root->children);
    for (size_t k = 1; k <= ngroups; k++) {
        size_t prev_gi = (gi + ngroups - k) % ngroups;
        struct bv_scene_obj *pg =
            (struct bv_scene_obj *)BU_PTBL_GET(&root->children, prev_gi);
        size_t plen = BU_PTBL_LEN(&pg->children);
        if (plen > 0)
            return (struct bv_scene_obj *)BU_PTBL_GET(&pg->children,
                                                       plen - 1);
    }

    return NULL;
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

    bu_ptbl_reset(&root->children);

    /* Free the root node itself */
    struct bv_scene_obj *fso = root->free_scene_obj;
    root->parent = NULL;
    if (fso)
        FREE_BV_SCENE_OBJ(root, &fso->l, root->vlfree);

    gedp->i->ged_gdp->gd_draw_root = NULL;
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
