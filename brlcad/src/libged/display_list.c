/*                  D I S P L A Y _ L I S T . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2025 United States Government as represented by
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
/** @file libged/display_list.c
 *
 * Collect display list manipulation logic here as it is refactored.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "bu/hash.h"
#include "bu/ptbl.h"
#include "bu/str.h"
#include "bu/color.h"
#include "bsg/plot3.h"
#include "bg/clip.h"

#include "bsg/util.h"
#include "ged.h"
#include "./ged_private.h"

#define FIRST_SOLID(_sp)      ((_sp)->s_fullpath.fp_names[0])
#define FREE_BV_SCENE_OBJ(p, fp, vlf) { \
        BU_LIST_APPEND(fp, &((p)->l)); \
        BV_FREE_VLIST(vlf, &((p)->s_vlist)); }


/* defined in draw_calc.cpp */
extern fastf_t brep_est_avg_curve_len(struct rt_brep_internal *bi);
extern void createDListSolid(bsg_shape *sp);

/* --------------------------------------------------------------------------
 * Phase 2e helpers: iterate root->children across all views, filtering by
 * path prefix (dl_path_str) or subpath subset.  These replace all patterns
 * that previously iterated gdlp->dl_head_scene_obj.
 * ------------------------------------------------------------------------ */

/*
 * Collect all bsg_shape nodes whose s_fullpath starts with match_path
 * from every view's scene-root children.
 */
static void
dl_match_shapes(struct ged *gedp, struct db_full_path *match_path,
struct bu_ptbl *out)
{
    struct bu_ptbl *views = bsg_scene_views(&gedp->ged_views);
    if (!views) return;
    for (size_t vi = 0; vi < BU_PTBL_LEN(views); vi++) {
bsg_view *v = (bsg_view *)BU_PTBL_GET(views, vi);
bsg_shape *root = bsg_scene_root_get(v);
if (!root || !BU_PTBL_IS_INITIALIZED(&root->children)) continue;
for (size_t si = 0; si < BU_PTBL_LEN(&root->children); si++) {
    bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, si);
    if (!sp || !sp->s_u_data) continue;
    struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
    if (db_full_path_match_top(match_path, &bdata->s_fullpath))
bu_ptbl_ins_unique(out, (long *)sp);
}
    }
}

/*
 * Collect shapes for a specific gdlp (by gdlp->dl_path prefix).
 */
static void
dl_gdlp_shapes(struct ged *gedp, struct display_list *gdlp, struct bu_ptbl *out)
{
    struct db_full_path gdlp_path;
    db_full_path_init(&gdlp_path);
    if (db_string_to_path(&gdlp_path, gedp->dbip,
  bu_vls_addr(&gdlp->dl_path)) != 0) return;
    dl_match_shapes(gedp, &gdlp_path, out);
    db_free_full_path(&gdlp_path);
}

/*
 * Free a single bsg_shape: fire destroy-vlist callback, remove from
 * scene-root children, and recycle via free_scene_obj pool.
 */
static void
dl_free_shape(struct ged *gedp, bsg_shape *sp)
{
    bsg_shape *free_scene_obj = bsg_scene_fsos(&gedp->ged_views);
    struct bu_list *vlfree = &rt_vlfree;

    ged_destroy_vlist_cb(gedp, sp->s_dlist, 1);

    if (sp->s_u_data) {
struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
struct directory *dp = FIRST_SOLID(bdata);
RT_CK_DIR(dp);
if (dp->d_addr == RT_DIR_PHONY_ADDR)
    (void)db_dirdelete(gedp->dbip, dp);
    }

    bsg_view *sp_view = (bsg_view *)sp->s_v;
    if (sp_view) {
bsg_shape *scene_root = bsg_scene_root_get(sp_view);
if (scene_root) bu_ptbl_rm(&scene_root->children, (const long *)sp);
    }

    FREE_BV_SCENE_OBJ(sp, &free_scene_obj->l, vlfree);
}



struct display_list *
dl_addToDisplay(struct bu_list *hdlp, struct db_i *dbip,
		const char *name)
{
    struct directory *dp = NULL;
    struct display_list *gdlp = NULL;
    char *cp = NULL;
    int found_namepath = 0;
    struct db_full_path namepath;

    cp = strrchr(name, '/');
    if (!cp)
        cp = (char *)name;
    else
        ++cp;

    if ((dp = db_lookup(dbip, cp, LOOKUP_NOISY)) == RT_DIR_NULL) {
        gdlp = GED_DISPLAY_LIST_NULL;
        goto end;
    }

    if (db_string_to_path(&namepath, dbip, name) == 0)
        found_namepath = 1;

    /* Make sure name is not already in the list */
    gdlp = BU_LIST_NEXT(display_list, hdlp);
    while (BU_LIST_NOT_HEAD(gdlp, hdlp)) {
        if (BU_STR_EQUAL(name, bu_vls_addr(&gdlp->dl_path)))
            goto end;

	if (found_namepath) {
            struct db_full_path gdlpath;

            if (db_string_to_path(&gdlpath, dbip, bu_vls_addr(&gdlp->dl_path)) == 0) {
                if (db_full_path_match_top(&gdlpath, &namepath)) {
                    db_free_full_path(&gdlpath);
                    goto end;
                }

                db_free_full_path(&gdlpath);
            }
        }

        gdlp = BU_LIST_PNEXT(display_list, gdlp);
    }

    BU_ALLOC(gdlp, struct display_list);
    BU_LIST_INIT(&gdlp->l);
    BU_LIST_INSERT(hdlp, &gdlp->l);
    gdlp->dl_dp = (void *)dp;
    bu_vls_init(&gdlp->dl_path);
    bu_vls_printf(&gdlp->dl_path, "%s", name);

end:
    if (found_namepath)
        db_free_full_path(&namepath);

    return gdlp;
}


void
headsolid_split(struct bu_list *hdlp, struct db_i *dbip, bsg_shape *sp, int newlen)
{
    if (!sp->s_u_data)
	return;
    struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;

    size_t savelen = bdata->s_fullpath.fp_len;
    bdata->s_fullpath.fp_len = newlen;
    char *pathname = db_path_to_string(&bdata->s_fullpath);
    bdata->s_fullpath.fp_len = savelen;

    /* Phase 2e: just register the new shorter-path gdlp entry.
     * Shapes stay in their view's scene-root children and are found by
     * path-prefix filtering; no longer moved via dl_head_scene_obj. */
    dl_addToDisplay(hdlp, dbip, pathname);
    bu_free((void *)pathname, "headsolid_split pathname");
}


int
headsolid_splitGDL(struct ged *gedp, struct display_list *gdlp, struct db_full_path *path)
{
    struct bu_list *hdlp = gedp->i->ged_gdp->gd_headDisplay;
    struct db_i *dbip = gedp->dbip;

    /* Phase 2e: collect remaining shapes for this gdlp from root->children
     * across all views, then create sub-gdlp entries using path-level logic
     * that mirrors the original recursive split algorithm. */
    struct bu_ptbl shapes = BU_PTBL_INIT_ZERO;
    bu_ptbl_init(&shapes, 8, "headsolid_splitGDL");
    dl_gdlp_shapes(gedp, gdlp, &shapes);

    if (!BU_PTBL_LEN(&shapes)) {
	bu_ptbl_free(&shapes);
	return 0;
    }

    for (size_t i = 0; i < BU_PTBL_LEN(&shapes); i++) {
	bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&shapes, i);
	if (!sp->s_u_data) continue;
	struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
	size_t fp_len = bdata->s_fullpath.fp_len;

	/* Determine the longest prefix of path[] that is also a prefix of
	 * this shape's fullpath, then split one level deeper.
	 * This replicates the original recursive descent without linked-list
	 * manipulation. */
	size_t common = 0;
	for (size_t k = 0; k < path->fp_len && k < fp_len; k++) {
	    if (path->fp_names[k] != bdata->s_fullpath.fp_names[k]) break;
	    common = k + 1;
	}
	size_t split_at = common + 1;
	if (split_at > fp_len) split_at = fp_len;
	if (split_at < 1)      split_at = 1;

	headsolid_split(hdlp, dbip, sp, (int)split_at);
    }

    bu_ptbl_free(&shapes);
    return 1;
}


int
dl_bounding_sph(struct bu_list *hdlp, vect_t *min, vect_t *max, int pflag)
{
    /* Phase 2e: dl_bounding_sph via hdlp/dl_head_scene_obj is replaced by
     * bsg_bounding_sph(v, min, max, pflag).  This stub avoids breaking any
     * callers that haven't been updated yet. */
    (void)hdlp; (void)pflag;
    VSETALL((*min),  INFINITY);
    VSETALL((*max), -INFINITY);
    return 1; /* is_empty = 1 */
}


/*
 * BSG Phase 2e version of dl_bounding_sph: iterates scene-root children
 * for the given view, avoiding the nested gdlp → dl_head_scene_obj loop.
 */
int
bsg_bounding_sph(bsg_view *v, vect_t *min, vect_t *max, int pflag)
{
    vect_t minus, plus;
    int is_empty = 1;

    VSETALL((*min),  INFINITY);
    VSETALL((*max), -INFINITY);

    bsg_shape *root = bsg_scene_root_get(v);
    size_t nshapes = root ? BU_PTBL_LEN(&root->children) : 0;
    for (size_t si = 0; si < nshapes; si++) {
	bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, si);
	if (!sp->s_u_data)
	    continue;
	struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
	if (!pflag &&
	    bdata->s_fullpath.fp_names != (struct directory **)0 &&
	    bdata->s_fullpath.fp_names[0] != (struct directory *)0 &&
	    bdata->s_fullpath.fp_names[0]->d_addr == RT_DIR_PHONY_ADDR)
	    continue;

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

    return is_empty;
}


/*
 * Erase/remove the display list item from headDisplay if path matches the list item's path.
 *
 */
void
dl_erasePathFromDisplay(struct ged *gedp, const char *path, int allow_split)
{
    struct bu_list *hdlp = gedp->i->ged_gdp->gd_headDisplay;
    struct db_i *dbip = gedp->dbip;
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    struct display_list *last_gdlp;
    struct db_full_path subpath;
    int found_subpath;

    if (db_string_to_path(&subpath, dbip, path) == 0)
	found_subpath = 1;
    else
	found_subpath = 0;

    gdlp = BU_LIST_NEXT(display_list, hdlp);
    last_gdlp = BU_LIST_LAST(display_list, hdlp);
    while (BU_LIST_NOT_HEAD(gdlp, hdlp)) {
	next_gdlp = BU_LIST_PNEXT(display_list, gdlp);

	if (BU_STR_EQUAL(path, bu_vls_addr(&gdlp->dl_path))) {
	    /* Phase 2e: collect shapes from root->children and free them */
	    struct bu_ptbl gdlp_shapes = BU_PTBL_INIT_ZERO;
	    bu_ptbl_init(&gdlp_shapes, 8, "erasePathFromDisplay exact");
	    dl_gdlp_shapes(gedp, gdlp, &gdlp_shapes);
	    for (size_t _si = 0; _si < BU_PTBL_LEN(&gdlp_shapes); _si++) {
		bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&gdlp_shapes, _si);
		if (sp) dl_free_shape(gedp, sp);
	    }
	    bu_ptbl_free(&gdlp_shapes);

	    BU_LIST_DEQUEUE(&gdlp->l);
	    bu_vls_free(&gdlp->dl_path);
	    BU_FREE(gdlp, struct display_list);

	    break;
	} else if (found_subpath) {
	    /* Phase 2e: find shapes that belong to this gdlp AND match subpath */
	    struct db_full_path gdlp_fp;
	    db_full_path_init(&gdlp_fp);
	    if (db_string_to_path(&gdlp_fp, dbip, bu_vls_addr(&gdlp->dl_path)) != 0) {
		if (gdlp == last_gdlp) gdlp = (struct display_list *)hdlp;
		else gdlp = next_gdlp;
		continue;
	    }

	    struct bu_ptbl to_free = BU_PTBL_INIT_ZERO;
	    bu_ptbl_init(&to_free, 8, "erasePathFromDisplay sub");
	    dl_match_shapes(gedp, &subpath, &to_free);
	    int need_split = 0;
	    for (size_t _si = 0; _si < BU_PTBL_LEN(&to_free); _si++) {
		bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&to_free, _si);
		if (!sp || !sp->s_u_data) continue;
		struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
		if (db_full_path_match_top(&gdlp_fp, &bdata->s_fullpath)) {
		    dl_free_shape(gedp, sp);
		    need_split = 1;
		}
	    }
	    bu_ptbl_free(&to_free);
	    db_free_full_path(&gdlp_fp);

	    /* Check remaining shapes */
	    struct bu_ptbl remain = BU_PTBL_INIT_ZERO;
	    bu_ptbl_init(&remain, 4, "erasePathFromDisplay remain");
	    dl_gdlp_shapes(gedp, gdlp, &remain);
	    int nremain = (int)BU_PTBL_LEN(&remain);
	    bu_ptbl_free(&remain);

	    if (!nremain) {
		BU_LIST_DEQUEUE(&gdlp->l);
		bu_vls_free(&gdlp->dl_path);
		BU_FREE(gdlp, struct display_list);
	    } else if (allow_split && need_split) {
		BU_LIST_DEQUEUE(&gdlp->l);
		--subpath.fp_len;
		(void)headsolid_splitGDL(gedp, gdlp, &subpath);
		++subpath.fp_len;
		bu_vls_free(&gdlp->dl_path);
		BU_FREE(gdlp, struct display_list);
	    }
	}

	if (gdlp == last_gdlp)
	    gdlp = (struct display_list *)hdlp;
	else
	    gdlp = next_gdlp;
    }

    if (found_subpath)
	db_free_full_path(&subpath);
}


static void
eraseAllSubpathsFromSolidList(struct ged *gedp, struct display_list *gdlp,
			      struct db_full_path *subpath,
			      const int skip_first, struct bu_list *UNUSED(vlfree))
{
    /* Phase 2e: collect shapes via root->children, filter by gdlp prefix
     * AND subset-of-subpath criterion, then free each match. */
    struct db_full_path gdlp_fp;
    db_full_path_init(&gdlp_fp);
    if (db_string_to_path(&gdlp_fp, gedp->dbip, bu_vls_addr(&gdlp->dl_path)) != 0)
	return;

    struct bu_ptbl candidates = BU_PTBL_INIT_ZERO;
    bu_ptbl_init(&candidates, 8, "eraseAllSubpaths");
    dl_match_shapes(gedp, &gdlp_fp, &candidates);
    db_free_full_path(&gdlp_fp);

    for (size_t si = 0; si < BU_PTBL_LEN(&candidates); si++) {
	bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&candidates, si);
	if (!sp || !sp->s_u_data) continue;
	struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
	if (db_full_path_subset(&bdata->s_fullpath, subpath, skip_first))
	    dl_free_shape(gedp, sp);
    }
    bu_ptbl_free(&candidates);
}


/*
 * Erase/remove display list item from headDisplay if name is found anywhere along item's path with
 * the exception that the first path element is skipped if skip_first is true.
 *
 * Note - name is not expected to contain path separators.
 *
 */
void
_dl_eraseAllNamesFromDisplay(struct ged *gedp,  const char *name, const int skip_first)
{
    struct bu_list *hdlp = gedp->i->ged_gdp->gd_headDisplay;
    struct db_i *dbip = gedp->dbip;
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    struct bu_list *vlfree = &rt_vlfree;

    gdlp = BU_LIST_NEXT(display_list, hdlp);
    while (BU_LIST_NOT_HEAD(gdlp, hdlp)) {
	char *dup_path;
	char *tok;
	int first = 1;
	int found = 0;

	next_gdlp = BU_LIST_PNEXT(display_list, gdlp);

	dup_path = bu_strdup(bu_vls_addr(&gdlp->dl_path));
	tok = strtok(dup_path, "/");
	while (tok) {
	    if (first) {
		first = 0;

		if (skip_first) {
		    tok = strtok((char *)NULL, "/");
		    continue;
		}
	    }

	    if (BU_STR_EQUAL(tok, name)) {
		_dl_freeDisplayListItem(gedp, gdlp);
		found = 1;

		break;
	    }

	    tok = strtok((char *)NULL, "/");
	}

	/* Look for name in solids list */
	if (!found) {
	    struct db_full_path subpath;

	    if (db_string_to_path(&subpath, dbip, name) == 0) {
		eraseAllSubpathsFromSolidList(gedp, gdlp, &subpath, skip_first, vlfree);
		db_free_full_path(&subpath);
	    }
	}

	bu_free((void *)dup_path, "dup_path");
	gdlp = next_gdlp;
    }
}


int
_dl_eraseFirstSubpath(struct ged *gedp,
		      struct display_list *gdlp,
		      struct db_full_path *subpath,
		      const int skip_first)
{
    /* Phase 2e: find first shape matching subpath via root->children */
    struct db_full_path gdlp_fp;
    db_full_path_init(&gdlp_fp);
    if (db_string_to_path(&gdlp_fp, gedp->dbip, bu_vls_addr(&gdlp->dl_path)) != 0)
	return 0;

    struct bu_ptbl candidates = BU_PTBL_INIT_ZERO;
    bu_ptbl_init(&candidates, 8, "_dl_eraseFirstSubpath");
    dl_match_shapes(gedp, &gdlp_fp, &candidates);
    db_free_full_path(&gdlp_fp);

    bsg_shape *target = NULL;
    struct db_full_path dup_path;
    db_full_path_init(&dup_path);

    for (size_t si = 0; si < BU_PTBL_LEN(&candidates); si++) {
	bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&candidates, si);
	if (!sp || !sp->s_u_data) continue;
	struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
	if (db_full_path_subset(&bdata->s_fullpath, subpath, skip_first)) {
	    size_t full_len = bdata->s_fullpath.fp_len;
	    bdata->s_fullpath.fp_len = full_len - 1;
	    db_dup_full_path(&dup_path, &bdata->s_fullpath);
	    bdata->s_fullpath.fp_len = full_len;
	    target = sp;
	    break;
	}
    }
    bu_ptbl_free(&candidates);

    if (!target) {
	db_free_full_path(&dup_path);
	return 0;
    }

    dl_free_shape(gedp, target);
    BU_LIST_DEQUEUE(&gdlp->l);
    int ret = headsolid_splitGDL(gedp, gdlp, &dup_path);
    db_free_full_path(&dup_path);
    bu_vls_free(&gdlp->dl_path);
    BU_FREE(gdlp, struct display_list);
    return ret;
}


/*
 * Erase/remove display list item from headDisplay if path is a subset of item's path.
 */
void
_dl_eraseAllPathsFromDisplay(struct ged *gedp, const char *path, const int skip_first)
{
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    struct db_full_path fullpath, subpath;
    struct bu_list *hdlp = gedp->i->ged_gdp->gd_headDisplay;
    struct db_i *dbip = gedp->dbip;

    if (db_string_to_path(&subpath, dbip, path) == 0) {
	gdlp = BU_LIST_NEXT(display_list, hdlp);

	// Zero out the worked flag so we can tell which scene
	// objects have been processed.
	while (BU_LIST_NOT_HEAD(gdlp, hdlp)) {
	    gdlp->dl_wflag = 0;
	    gdlp = BU_LIST_PNEXT(display_list, gdlp);
	}

	gdlp = BU_LIST_NEXT(display_list, hdlp);
	while (BU_LIST_NOT_HEAD(gdlp, hdlp)) {
	    next_gdlp = BU_LIST_PNEXT(display_list, gdlp);

	    /* This display list has already been visited. */
	    if (gdlp->dl_wflag) {
		gdlp = next_gdlp;
		continue;
	    }

	    /* Mark as being visited. */
	    gdlp->dl_wflag = 1;

	    if (db_string_to_path(&fullpath, dbip, bu_vls_addr(&gdlp->dl_path)) == 0) {
		if (db_full_path_subset(&fullpath, &subpath, skip_first)) {
		    _dl_freeDisplayListItem(gedp, gdlp);
		} else if (_dl_eraseFirstSubpath(gedp, gdlp, &subpath, skip_first)) {
		    gdlp = BU_LIST_NEXT(display_list, hdlp);
		    db_free_full_path(&fullpath);
		    continue;
		}

		db_free_full_path(&fullpath);
	    }

	    gdlp = next_gdlp;
	}

	db_free_full_path(&subpath);
    }
}


void
_dl_freeDisplayListItem (struct ged *gedp, struct display_list *gdlp)
{
    /* Phase 2e: collect shapes from root->children and free each one */
    struct bu_ptbl shapes = BU_PTBL_INIT_ZERO;
    bu_ptbl_init(&shapes, 8, "_dl_freeDisplayListItem");
    dl_gdlp_shapes(gedp, gdlp, &shapes);
    for (size_t si = 0; si < BU_PTBL_LEN(&shapes); si++) {
	bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&shapes, si);
	if (sp) dl_free_shape(gedp, sp);
    }
    bu_ptbl_free(&shapes);

    /* Free up the display list */
    BU_LIST_DEQUEUE(&gdlp->l);
    bu_vls_free(&gdlp->dl_path);
    BU_FREE(gdlp, struct display_list);
}


void
color_soltab(bsg_shape *sp)
{
    const struct mater *mp;

    sp->s_old.s_cflag = 0;

    /* the user specified the color, so use it */
    if (sp->s_old.s_uflag) {
	sp->s_color[0] = sp->s_old.s_basecolor[0];
	sp->s_color[1] = sp->s_old.s_basecolor[1];
	sp->s_color[2] = sp->s_old.s_basecolor[2];

	return;
    }

    for (mp = rt_material_head(); mp != MATER_NULL; mp = mp->mt_forw) {
	if (sp->s_old.s_regionid <= mp->mt_high &&
	    sp->s_old.s_regionid >= mp->mt_low) {
	    sp->s_color[0] = mp->mt_r;
	    sp->s_color[1] = mp->mt_g;
	    sp->s_color[2] = mp->mt_b;

	    return;
	}
    }

    /*
     * There is no region-id-based coloring entry in the
     * table, so use the combination-record ("mater"
     * command) based color if one was provided. Otherwise,
     * use the default wireframe color.
     * This is the "new way" of coloring things.
     */

    /* use wireframe_default_color */
    if (sp->s_old.s_dflag)
	sp->s_old.s_cflag = 1;

    /* Be conservative and copy color anyway, to avoid black */
    sp->s_color[0] = sp->s_old.s_basecolor[0];
    sp->s_color[1] = sp->s_old.s_basecolor[1];
    sp->s_color[2] = sp->s_old.s_basecolor[2];
}


/*
 * Pass through the solid table and set pointer to appropriate
 * mater structure.
 */
void
dl_color_soltab(struct bu_list *hdlp)
{
    /* Phase 2e: dl_color_soltab on the legacy hdlp is a no-op once shapes
     * are tracked via root->children.  Use bsg_color_soltab(v) instead. */
    if (!hdlp) return;
}


/*
 * BSG Phase 2e version: recolor all shapes in the scene-root children of v.
 */
void
bsg_color_soltab(bsg_view *v)
{
    if (!v) return;
    bsg_shape *root = bsg_scene_root_get(v);
    if (!root) return;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, i);
	color_soltab(sp);
    }
}

static void
solid_append_vlist(bsg_shape *sp, struct bv_vlist *vlist)
{
    if (BU_LIST_IS_EMPTY(&(sp->s_vlist))) {
	sp->s_vlen = 0;
    }

    sp->s_vlen += bv_vlist_cmd_cnt(vlist);
    BU_LIST_APPEND_LIST(&(sp->s_vlist), &(vlist->l));
}

static void
solid_copy_vlist(struct db_i *UNUSED(dbip), bsg_shape *sp, struct bv_vlist *vlist, struct bu_list *vlfree)
{
    BU_LIST_INIT(&(sp->s_vlist));
    bv_vlist_copy(vlfree, &(sp->s_vlist), (struct bu_list *)vlist);
    sp->s_vlen = bv_vlist_cmd_cnt((struct bv_vlist *)(&(sp->s_vlist)));
}


int invent_solid(struct ged *gedp, char *name, struct bu_list *vhead, long int rgb, int copy,
		 fastf_t transparency, int dmode, int csoltab)
{
    if (!gedp || !gedp->ged_gvp)
	return 0;

    struct bu_list *hdlp = gedp->i->ged_gdp->gd_headDisplay;
    struct db_i *dbip = gedp->dbip;
    struct directory *dp;
    bsg_shape *sp;
    unsigned char type='0';
    struct bu_list *vlfree = &rt_vlfree;

    if (dbip == DBI_NULL)
	return 0;

    if ((dp = db_lookup(dbip, name, LOOKUP_QUIET)) != RT_DIR_NULL) {
	if (dp->d_addr != RT_DIR_PHONY_ADDR) {
	    bu_log("invent_solid(%s) would clobber existing database entry, ignored\n", name);
	    return -1;
	}

	/*
	 * Name exists from some other overlay,
	 * zap any associated solids
	 */
	dl_erasePathFromDisplay(gedp, name, 0);
    }

    /* Obtain a fresh solid structure, and fill it in */
    sp = bsg_shape_get(gedp->ged_gvp, BSG_DB_OBJS);
    struct ged_bv_data *bdata = (sp->s_u_data) ? (struct ged_bv_data *)sp->s_u_data : NULL;
    if (!bdata) {
	BU_GET(bdata, struct ged_bv_data);
	db_full_path_init(&bdata->s_fullpath);
	sp->s_u_data = (void *)bdata;
    } else {
	bdata->s_fullpath.fp_len = 0;
    }
    if (!sp->s_u_data)
	return -1;

    /* Need to enter phony name in directory structure */
    dp = db_diradd(dbip, name, RT_DIR_PHONY_ADDR, 0, RT_DIR_SOLID, (void *)&type);

    if (copy) {
	solid_copy_vlist(dbip, sp, (struct bv_vlist *)vhead, vlfree);
    } else {
	solid_append_vlist(sp, (struct bv_vlist *)vhead);
	BU_LIST_INIT(vhead);
    }
    bsg_shape_bound(sp, gedp->ged_gvp);

    /* set path information -- this is a top level node */
    db_add_node_to_full_path(&bdata->s_fullpath, dp);

    /* Register this name in the display list (for reverse-lookup by path) */
    (void)dl_addToDisplay(hdlp, dbip, name);

    sp->s_iflag = DOWN;
    sp->s_soldash = 0;
    sp->s_old.s_Eflag = 1;            /* Can't be solid edited! */
    sp->s_color[0] = sp->s_old.s_basecolor[0] = (rgb>>16) & 0xFF;
    sp->s_color[1] = sp->s_old.s_basecolor[1] = (rgb>> 8) & 0xFF;
    sp->s_color[2] = sp->s_old.s_basecolor[2] = (rgb) & 0xFF;
    sp->s_old.s_regionid = 0;
    sp->s_dlist = 0;

    sp->s_old.s_uflag = 0;
    sp->s_old.s_dflag = 0;
    sp->s_old.s_cflag = 0;
    sp->s_old.s_wflag = 0;

    sp->s_os->transparency = transparency;
    sp->s_os->s_dmode = dmode;

    if (csoltab)
	color_soltab(sp);

    ged_create_vlist_solid_cb(gedp, sp);

    return 0;           /* OK */

}

void
dl_set_iflag(struct bu_list *hdlp, int iflag)
{
    /* Phase 2e: dl_set_iflag on the legacy hdlp is a no-op once shapes are
     * tracked via root->children.  Use bsg_set_iflag(v, iflag) instead. */
    if (!hdlp) return;
    (void)iflag;
}


/*
 * BSG Phase 2e version: set the iflag on all shapes in the scene-root children.
 */
void
bsg_set_iflag(bsg_view *v, int iflag)
{
    if (!v) return;
    bsg_shape *root = bsg_scene_root_get(v);
    if (!root) return;
    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, i);
	sp->s_iflag = iflag;
    }
}



unsigned long long
dl_name_hash(struct ged *gedp)
{
    bsg_shape *root = (gedp->ged_gvp) ? bsg_scene_root_get(gedp->ged_gvp) : NULL;
    size_t nshapes = root ? BU_PTBL_LEN(&root->children) : 0;

    if (!nshapes && !BU_LIST_NON_EMPTY(gedp->i->ged_gdp->gd_headDisplay))
	return 0;

    struct bu_data_hash_state *state = bu_data_hash_create();
    if (!state)
	return 0;

    if (root) {
	/* Phase 2e: scene-root children is the sole source */
	for (size_t i = 0; i < nshapes; i++) {
	    bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, i);
	    if (!sp->s_u_data) continue;
	    struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
	    for (size_t j = 0; j < bdata->s_fullpath.fp_len; j++) {
		struct directory *dp = bdata->s_fullpath.fp_names[j];
		bu_data_hash_update(state, dp->d_namep, strlen(dp->d_namep));
	    }
	}
    }

    unsigned long long hash_val = bu_data_hash_val(state);
    bu_data_hash_destroy(state);

    return hash_val;
}

/*
 * ged_find_shapes_by_path — BSG Phase 2e helper.
 *
 * Searches the scene-root children of view @p v for bsg_shape nodes whose
 * ged_bv_data::s_fullpath exactly matches @p path, and appends them to
 * @p result.
 *
 * This replaces the legacy nested loop pattern:
 *
 *   for each gdlp in ged_dl(gedp):
 *       for each sp in gdlp->dl_head_scene_obj:
 *           if (db_identical_full_paths(path, &bdata->s_fullpath)) …
 *
 * After Phase 2e (all shapes in scene root) this single pass is the only
 * lookup needed.  During the transition period the scene-root children are
 * populated by the dual-write in dodraw.c / display_list.c.
 */
void
ged_find_shapes_by_path(struct ged *gedp, bsg_view *v,
			const struct db_full_path *path,
			struct bu_ptbl *result)
{
    if (!gedp || !v || !path || !result)
	return;

    bsg_shape *root = bsg_scene_root_get(v);
    if (!root)
	return;

    for (size_t i = 0; i < BU_PTBL_LEN(&root->children); i++) {
	bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, i);
	/* BU_PTBL_GET should never return NULL here, but guard defensively */
	if (!sp)
	    continue;
	if (!sp->s_u_data)
	    continue;
	struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
	if (db_identical_full_paths(path, &bdata->s_fullpath))
	    bu_ptbl_ins(result, (long *)sp);
    }
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
