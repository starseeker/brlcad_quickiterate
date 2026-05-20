/*                         D R A W . C P P
 * BRL-CAD
 *
 * Copyright (c) 2008-2026 United States Government as represented by
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
/** @file draw.cpp
 *
 * Drawing routines that generate scene objects.
 *
 */

#include "common.h"

#include <set>
#include <unordered_map>

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include "bsocket.h"

#include "bu/cmd.h"
#include "bu/hash.h"
#include "bu/opt.h"
#include "bu/sort.h"
#include "bu/str.h"
#include "bv/defines.h"
#include "bg/sat.h"
#include "bv/lod.h"
#include "bsg/appearance.h"
#include "bsg/material.h"
#include "bsg/node.h"
#include "bsg/payload.h"
#include "bsg/settings.h"
#include "nmg.h"
#include "rt/view.h"

#include "ged/view.h"
#include "../librt/librt_private.h"
#include "./ged_private.h"

static struct draw_update_data_t *
scene_draw_update_data(const struct bv_scene_obj *s)
{
    return (struct draw_update_data_t *)bsg_node_user_data_get((const bsg_node *)s);
}

static struct db_full_path *
scene_source_path(const struct bv_scene_obj *s)
{
    return (struct db_full_path *)bsg_node_source_path_get((const bsg_node *)s);
}

static struct directory *
scene_directory(const struct bv_scene_obj *s)
{
    struct db_full_path *fp = scene_source_path(s);
    return (fp) ? DB_FULL_PATH_CUR_DIR(fp) : (struct directory *)bsg_node_app_data_get((const bsg_node *)s);
}

static int
prim_tess(struct bv_scene_obj *s, struct rt_db_internal *ip)
{
    struct draw_update_data_t *d = scene_draw_update_data(s);
    struct directory *dp = scene_directory(s);
    const struct bn_tol *tol = d->tol;
    const struct bg_tess_tol *ttol = d->ttol;
    RT_CK_DB_INTERNAL(ip);
    RT_CK_DIR(dp);
    BN_CK_TOL(tol);
    BG_CK_TESS_TOL(ttol);
    if (!ip->idb_meth || !ip->idb_meth->ft_tessellate) {
	bu_log("ERROR(%s): tessellation support not available\n", dp->d_namep);
	return -1;
    }

    struct model *m = nmg_mm();
    struct nmgregion *r = (struct nmgregion *)NULL;
    if (ip->idb_meth->ft_tessellate(&r, m, ip, ttol, tol) < 0) {
	bu_log("ERROR(%s): tessellation failure\n", dp->d_namep);
	return -1;
    }

    NMG_CK_REGION(r);
    struct bu_list *vhead = bsg_node_vlist_head((bsg_node *)s);
    if (!vhead) {
	nmg_km(m);
	return -1;
    }
    nmg_r_to_vlist(vhead, r, NMG_VLIST_STYLE_POLYGON, s->vlfree);
    nmg_km(m);

    s->current = 1;

    return 0;
}

static void
draw_free_data(struct bv_scene_obj *s)
{
    /* Validate */
    if (!s)
	return;

    struct db_full_path *sfp = scene_source_path(s);
    if (sfp) {
	db_free_full_path(sfp);
	BU_PUT(sfp, struct db_full_path);
	bsg_node_source_path_set((bsg_node *)s, NULL);
    }

    /* free drawing info */
    struct draw_update_data_t *d = scene_draw_update_data(s);
    if (!d)
	return;
    BU_PUT(d, struct draw_update_data_t);
    bsg_node_user_data_set((bsg_node *)s, NULL);
}

static void
mesh_lod_draw_free(struct bv_scene_obj *s)
{
    if (!s)
	return;
    bv_mesh_lod_free(s);
    draw_free_data(s);
}

static int
scene_draw_mode(const struct bv_scene_obj *s)
{
    struct bsg_appearance app;
    bsg_appearance_init(&app);
    if (!s)
	return 0;
    (void)bsg_node_appearance_get((const bsg_node *)s, &app);
    return app.draw_mode;
}

static void
scene_draw_mode_set(struct bv_scene_obj *s, int mode)
{
    struct bsg_appearance app;
    if (!s)
	return;
    bsg_appearance_init(&app);
    (void)bsg_node_appearance_get((const bsg_node *)s, &app);
    app.draw_mode = mode;
    bsg_node_appearance_set((bsg_node *)s, &app);
}

static int
scene_color_override(const struct bv_scene_obj *s)
{
    struct bsg_material mat;
    bsg_material_init(&mat);
    if (!s)
	return 0;
    (void)bsg_node_material_get((const bsg_node *)s, &mat);
    return mat.use_override_color;
}

static int
scene_draw_non_subtract_only(const struct bv_scene_obj *s)
{
    struct bsg_appearance app;
    bsg_appearance_init(&app);
    if (!s)
	return 0;
    (void)bsg_node_appearance_get((const bsg_node *)s, &app);
    return app.draw_non_subtract_only;
}

static int
scene_draw_solid_lines_only(const struct bv_scene_obj *s)
{
    struct bsg_appearance app;
    bsg_appearance_init(&app);
    if (!s)
	return 0;
    (void)bsg_node_appearance_get((const bsg_node *)s, &app);
    return app.draw_solid_lines_only;
}


/* Non-static: called directly by BViewState::redraw() for Phase 2-B */
extern "C" int
csg_wireframe_update(struct bv_scene_obj *vo, struct bview *v, int flag)
{
    /* Validate */
    if (!vo || !v)
	return 0;

    if (!v->gv_s->adaptive_plot_csg)
	return 0;

    bv_log(1, "csg_wireframe_update %s[%s]", bu_vls_cstr(&vo->bsg.bsg_name), bu_vls_cstr(&v->gv_name));

    vo->csg_obj = 1;

    // If the object is not visible in the scene, don't change the data.  This
    // check is useful in orthographic camera mode, where we zoom in on a
    // narrow subset of the model and far away objects are still rendered in
    // full detail.  If we have a perspective matrix active don't make this
    // check, since far away objects outside the view obb will be visible.
    //bu_log("min: %f %f %f max: %f %f %f\n", V3ARGS(vo->bmin), V3ARGS(vo->bmax));
    if (!(v->gv_perspective > SMALL_FASTF) && !bg_sat_aabb_obb(vo->bmin, vo->bmax, v->obb_center, v->obb_extent1, v->obb_extent2, v->obb_extent3))
	return 0;

    bool rework = (flag) ? true : false;

    // Check point scale
    if (!rework && !NEAR_EQUAL(vo->curve_scale, vo->s_v->gv_s->curve_scale, SMALL_FASTF))
	rework = true;
    // Check point scale
    if (!rework && !NEAR_EQUAL(vo->point_scale, vo->s_v->gv_s->point_scale, SMALL_FASTF))
	rework = true;
    if (!rework) {
	// Check view scale
	fastf_t delta = vo->view_scale * 0.1/vo->view_scale;
	if (!NEAR_EQUAL(vo->view_scale, v->gv_scale, delta))
	    rework = true;
    }
    if (!rework)
	return 0;

    // We're going to redraw - sync with view
    vo->curve_scale = v->gv_s->curve_scale;
    vo->point_scale = v->gv_s->point_scale;
    vo->view_scale = v->gv_scale;

    // Clear out existing vlists
    struct bu_list *p;
    struct bu_list *vhead = bsg_node_vlist_head((bsg_node *)vo);
    if (!vhead)
	return 0;
    while (BU_LIST_WHILE(p, bu_list, vhead)) {
	BU_LIST_DEQUEUE(p);
	struct bv_vlist *pv = (struct bv_vlist *)p;
	BU_FREE(pv, struct bv_vlist);
    }

    struct draw_update_data_t *d = scene_draw_update_data(vo);
    struct directory *dp = scene_directory(vo);
    struct db_i *dbip = d->dbip;
    struct rt_db_internal dbintern;
    RT_DB_INTERNAL_INIT(&dbintern);
    struct rt_db_internal *ip = &dbintern;
    int ret = rt_db_get_internal(ip, dp, dbip, NULL);
    if (ret < 0)
	return 0;

    if (ip->idb_meth->ft_adaptive_plot) {
	ip->idb_meth->ft_adaptive_plot(vhead, ip, d->tol, v, vo->s_size);
	bv_obj_stale(vo);
    }

    return 1;
}

struct ged_full_detail_clbk_data {
    struct db_i *dbip;
    struct directory *dp;
    struct rt_db_internal *intern;
};

/* Set up the data for drawing */
static int
bot_mesh_info_clbk(struct bv_mesh_lod *lod, void *cb_data)
{
    if (!lod || !cb_data)
	return -1;

    struct ged_full_detail_clbk_data *cd = (struct ged_full_detail_clbk_data *)cb_data;
    struct db_i *dbip = cd->dbip;
    struct directory *dp = cd->dp;

    BU_GET(cd->intern, struct rt_db_internal);
    RT_DB_INTERNAL_INIT(cd->intern);
    struct rt_db_internal *ip = cd->intern;
    int ret = rt_db_get_internal(ip, dp, dbip, NULL);
    if (ret < 0) {
	BU_PUT(cd->intern, struct rt_db_internal);
	return -1;
    }
    struct rt_bot_internal *bot = (struct rt_bot_internal *)ip->idb_ptr;
    RT_BOT_CK_MAGIC(bot);

    lod->faces = bot->faces;
    lod->fcnt = bot->num_faces;
    lod->pcnt = bot->num_vertices;
    lod->points = (const point_t *)bot->vertices;
    lod->points_orig = (const point_t *)bot->vertices;

    return 0;
}

/* Free up the drawing data, but not (yet) done with ged_full_detail_clbk_data */
static int
bot_mesh_info_clear_clbk(struct bv_mesh_lod *lod, void *cb_data)
{
    struct ged_full_detail_clbk_data *cd = (struct ged_full_detail_clbk_data *)cb_data;
    if (cd->intern) {
	rt_db_free_internal(cd->intern);
	BU_PUT(cd->intern, struct rt_db_internal);
    }
    cd->intern = NULL;

    lod->faces = NULL;
    lod->fcnt = 0;
    lod->pcnt = 0;
    lod->points = NULL;
    lod->points_orig = NULL;

    return 0;
}

/* Done - free up everything */
static int
bot_mesh_info_free_clbk(struct bv_mesh_lod *lod, void *cb_data)
{
    bot_mesh_info_clear_clbk(lod, cb_data);
    struct ged_full_detail_clbk_data *cd = (struct ged_full_detail_clbk_data *)cb_data;
    BU_PUT(cd, struct ged_full_detail_clbk_data);
    return 0;
}

static void
bot_adaptive_plot(struct bv_scene_obj *s, struct bview *v)
{
    if (!s || !v)
	return;

    s->csg_obj = 0;
    s->mesh_obj = 1;

    bv_log(1, "bot_adaptive_plot %s[%s]", bu_vls_cstr(&s->bsg.bsg_name), (v) ? bu_vls_cstr(&v->gv_name) : "NULL");

    if (!s->draw_data) {

	s->csg_obj = 0;
	s->mesh_obj = 1;

	struct draw_update_data_t *d = scene_draw_update_data(s);
	if (!d || !d->mesh_c)
	    return;
	struct db_i *dbip = d->dbip;
	struct directory *dp = scene_directory(s);

	if (!dp)
	    return;

	// We need the key to look up the LoD data from the cache, and if we don't
	// already have cache data for this bot we need to generate it.
	unsigned long long key = bv_mesh_lod_key_get(d->mesh_c, dp->d_namep);
	if (!key) {
	    // We don't have a key associated with the name.  Get and check the BoT
	    // data itself, creating the LoD data if we don't already have it
	    struct rt_db_internal dbintern;
	    RT_DB_INTERNAL_INIT(&dbintern);
	    struct rt_db_internal *ip = &dbintern;
	    int ret = rt_db_get_internal(ip, dp, dbip, NULL);
	    if (ret < 0)
		return;
	    struct rt_bot_internal *bot = (struct rt_bot_internal *)ip->idb_ptr;
	    RT_BOT_CK_MAGIC(bot);
	    key = bv_mesh_lod_cache(d->mesh_c, (const point_t *)bot->vertices, bot->num_vertices, NULL, bot->faces, bot->num_faces, 0, 0.66);
	    bv_mesh_lod_key_put(d->mesh_c, dp->d_namep, key);
	    rt_db_free_internal(&dbintern);
	}
	if (!key)
	    return;

	// Once we have a valid key, proceed to create the necessary
	// data structures and objects.
	struct bv_mesh_lod *lod = bv_mesh_lod_create(d->mesh_c, key);
	if (!lod) {
	    // Stale key?  Clear it and try a regeneration
	    unsigned long long old_key = key;
	    bv_mesh_lod_clear_cache(d->mesh_c, key);

	    // Load mesh and process
	    struct rt_db_internal dbintern;
	    RT_DB_INTERNAL_INIT(&dbintern);
	    struct rt_db_internal *ip = &dbintern;
	    int ret = rt_db_get_internal(ip, dp, dbip, NULL);
	    if (ret < 0)
		return;
	    struct rt_bot_internal *bot = (struct rt_bot_internal *)ip->idb_ptr;
	    RT_BOT_CK_MAGIC(bot);
	    key = bv_mesh_lod_cache(d->mesh_c, (const point_t *)bot->vertices, bot->num_vertices, NULL, bot->faces, bot->num_faces, 0, 0.66);
	    bv_mesh_lod_key_put(d->mesh_c, dp->d_namep, key);
	    rt_db_free_internal(&dbintern);

	    // Sanity
	    if (old_key == key) {
		bu_log("%s: LoD lookup by key failed, but regeneration generated the same key (?)\n", dp->d_namep);
		return;
	    }
	    unsigned long long new_key = bv_mesh_lod_key_get(d->mesh_c, dp->d_namep);
	    if (new_key == old_key) {
		bu_log("%s: LoD regenerated with new key, but key lookup still returns old key (?)\n", dp->d_namep);
		return;
	    }

	    // If after all that we STILL don't get an LoD struct, give up
	    lod = bv_mesh_lod_create(d->mesh_c, key);
	    if (!lod)
		return;
	}

	// Assign the LoD information to the object's draw_data, and let
	// the LoD know which object it is associated with.
	s->draw_data = (void *)lod;
	lod->s = s;

	// The object bounds are based on the LoD's calculations.  Because the LoD
	// cache stores only one cached data set per object, but full path
	// instances in the scene can be placed with matrices, we must apply the
	// s_mat transformation to the "baseline" LoD bbox info to get the correct
	// box for the instance.
	mat_t s_mat;
	bsg_node_transform_get((const bsg_node *)s, s_mat);
	MAT4X3PNT(s->bmin, s_mat, lod->bmin);
	MAT4X3PNT(s->bmax, s_mat, lod->bmax);

	// Record the necessary information for full detail information recovery.  We
	// don't duplicate the full mesh detail in the on-disk LoD storage, since we
	// already have that info in the .g itself, but we need to know how to get at
	// it when needed.  The free callback will clean up, but we need to initialize
	// the callback data here.
	struct ged_full_detail_clbk_data *cbd;
	BU_GET(cbd, ged_full_detail_clbk_data);
	cbd->dbip = dbip;
	cbd->dp = dp;
	cbd->intern = NULL;
	bv_mesh_lod_detail_setup_clbk(lod, &bot_mesh_info_clbk, (void *)cbd);
	bv_mesh_lod_detail_clear_clbk(lod, &bot_mesh_info_clear_clbk);
	bv_mesh_lod_detail_free_clbk(lod, &bot_mesh_info_free_clbk);

	bsg_node_set_free_callback((bsg_node *)s, (bsg_node_free_fn)&mesh_lod_draw_free);

	// Initialize the LoD data to the current view
	int level = bv_mesh_lod_view(s, v, 0);
	if (level < 0) {
	    bu_log("Error loading info for initial LoD view\n");
	}
    }

    bv_mesh_lod_view(s, v, 0);
    bv_obj_stale(s);

    return;
}

static void
brep_adaptive_plot(struct bv_scene_obj *s, struct bview *v)
{
    if (!s || !v)
	return;
    struct draw_update_data_t *d = scene_draw_update_data(s);
    if (!d || !d->mesh_c)
	return;
    bv_log(1, "brep_adaptive_plot %s[%s]", bu_vls_cstr(&s->bsg.bsg_name), (v) ? bu_vls_cstr(&v->gv_name) : "NULL");

    s->csg_obj = 0;
    s->mesh_obj = 1;

    if (!s->draw_data) {

	struct db_i *dbip = d->dbip;
	struct directory *dp = scene_directory(s);

	if (!dp)
	    return;

	const struct bn_tol *tol = d->tol;
	const struct bg_tess_tol *ttol = d->ttol;
	struct bv_mesh_lod *lod = NULL;

	// We need the key to look up the LoD data from the cache, and if we don't
	// already have cache data for this brep we need to generate it.
	unsigned long long key = bv_mesh_lod_key_get(d->mesh_c, dp->d_namep);
	if (!key) {
	    // We don't have a key associated with the name.  Get and check the
	    // Brep data itself, creating the mesh data and the corresponding LoD
	    // data if we don't already have it
	    struct bu_external ext = BU_EXTERNAL_INIT_ZERO;
	    if (db_get_external(&ext, dp, dbip))
		return;
	    key = bu_data_hash((void *)ext.ext_buf,  ext.ext_nbytes);
	    bu_free_external(&ext);
	    if (!key)
		return;
	    lod = bv_mesh_lod_create(d->mesh_c, key);
	    if (!lod) {
		// Just in case we have a stale key...
		bv_mesh_lod_clear_cache(d->mesh_c, key);

		struct rt_db_internal dbintern;
		RT_DB_INTERNAL_INIT(&dbintern);
		struct rt_db_internal *ip = &dbintern;
		int ret = rt_db_get_internal(ip, dp, dbip, NULL);
		if (ret < 0)
		    return;
		struct rt_brep_internal *bi = (struct rt_brep_internal *)ip->idb_ptr;
		RT_BREP_CK_MAGIC(bi);

		// Unlike a BoT, which has the mesh data already, we need to generate the
		// mesh from the brep
		int *faces = NULL;
		int face_cnt = 0;
		vect_t *normals = NULL;
		point_t *pnts = NULL;
		int pnt_cnt = 0;

		ret = brep_cdt_fast(&faces, &face_cnt, &normals, &pnts, &pnt_cnt, bi->brep, -1, ttol, tol);
		if (ret != BRLCAD_OK) {
		    bu_free(faces, "faces");
		    bu_free(normals, "normals");
		    bu_free(pnts, "pnts");
		    return;
		}

		// Because we won't have the internal data to use for a full detail scenario, we set the ratio
		// to 1 rather than .66 for breps...
		key = bv_mesh_lod_cache(d->mesh_c, (const point_t *)pnts, pnt_cnt, normals, faces, face_cnt, key, 1);

		if (key)
		    bv_mesh_lod_key_put(d->mesh_c, dp->d_namep, key);

		rt_db_free_internal(&dbintern);

		bu_free(faces, "faces");
		bu_free(normals, "normals");
		bu_free(pnts, "pnts");
	    }
	}
	if (!key)
	    return;

	// Once we have a valid key, proceed to create the necessary
	// data structures and objects.  If the above didn't get us
	// a valid mesh, no point in trying further
	lod = bv_mesh_lod_create(d->mesh_c, key);
	if (!lod)
	    return;

	// Assign the LoD information to the object's draw_data, and let
	// the LoD know which object it is associated with.
	s->draw_data = (void *)lod;
	lod->s = s;

	// The object bounds are based on the LoD's calculations.  Because the LoD
	// cache stores only one cached data set per object, but full path
	// instances in the scene can be placed with matrices, we must apply the
	// s_mat transformation to the "baseline" LoD bbox info to get the correct
	// box for the instance.
	mat_t s_mat;
	bsg_node_transform_get((const bsg_node *)s, s_mat);
	MAT4X3PNT(s->bmin, s_mat, lod->bmin);
	MAT4X3PNT(s->bmax, s_mat, lod->bmax);

	// Record the necessary information for full detail information recovery.  We
	// don't duplicate the full mesh detail in the on-disk LoD storage, since we
	// already have that info in the .g itself, but we need to know how to get at
	// it when needed.  The free callback will clean up, but we need to initialize
	// the callback data here.
	struct ged_full_detail_clbk_data *cbd;
	BU_GET(cbd, ged_full_detail_clbk_data);
	cbd->dbip = dbip;
	cbd->dp = dp;
	cbd->intern = NULL;
	bv_mesh_lod_detail_setup_clbk(lod, &bot_mesh_info_clbk, (void *)cbd);
	bv_mesh_lod_detail_clear_clbk(lod, &bot_mesh_info_clear_clbk);
	bv_mesh_lod_detail_free_clbk(lod, &bot_mesh_info_free_clbk);

	bsg_node_set_free_callback((bsg_node *)s, (bsg_node_free_fn)&mesh_lod_draw_free);

	// Initialize the LoD data to the current view
	int level = bv_mesh_lod_view(s, v, 0);
	if (level < 0) {
	    bu_log("Error loading info for initial LoD view\n");
	}
    }

    bv_mesh_lod_view(s, v, 0);
    bv_obj_stale(s);

    return;
}

/* Wrapper to handle adaptive vs non-adaptive wireframes */
static void
wireframe_plot(struct bv_scene_obj *s, struct bview *v, struct rt_db_internal *ip)
{
    bv_log(1, "wireframe_plot %s[%s]", bu_vls_cstr(&s->bsg.bsg_name), (v) ? bu_vls_cstr(&v->gv_name) : "NULL");
    struct draw_update_data_t *d = scene_draw_update_data(s);
    struct bu_list *vhead = bsg_node_vlist_head((bsg_node *)s);
    if (!d || !vhead)
	return;
    const struct bn_tol *tol = d->tol;
    const struct bg_tess_tol *ttol = d->ttol;
    s->csg_obj = 1;
    s->mesh_obj = 0;

    // Standard (view independent) wireframe
    if (!v || !v->gv_s->adaptive_plot_csg) {
	if (ip->idb_meth->ft_plot) {
	    ip->idb_meth->ft_plot(vhead, ip, ttol, tol, s->s_v);
	    // Because this data is view independent, it only needs to be
	    // generated once rather than per-view.
	    s->current = 1;
	}
	return;
    }

    // If we're adaptive, call the primitive's adaptive plotting, if any.
    if (ip->idb_meth->ft_adaptive_plot) {
	csg_wireframe_update(s, v, 1);
	return;
    }

    // If we've got this far, we have no adaptive plotting capability for this
    // object.  Do the normal plot rather than show nothing.
    if (ip->idb_meth->ft_plot) {
	ip->idb_meth->ft_plot(vhead, ip, ttol, tol, s->s_v);
	// Because this data is view independent, it only needs to be
	// generated once rather than per-view.
	s->current = 1;
    }
}


extern "C" int draw_m3(struct bv_scene_obj *s);
extern "C" int draw_points(struct bv_scene_obj *s);

/* This function is the master controller that decides, based on available settings
 * and data, which specific drawing routines need to be triggered. */
extern "C" void
draw_scene(struct bv_scene_obj *s, struct bview *v)
{
    // If the scene object indicates we're good, don't repeat.
    if (s->current && !v)
	return;

    bv_log(1, "draw_scene %s[%s]", bu_vls_cstr(&s->bsg.bsg_name), (v) ? bu_vls_cstr(&v->gv_name) : "NULL");

    // If we're not adaptive, trigger the view insensitive drawing routines
    if (v && !v->gv_s->adaptive_plot_csg && !v->gv_s->adaptive_plot_mesh) {
	return draw_scene(s, NULL);
    }

    // If we have a scene object without drawing data, it is most likely
    // a container holding other objects we do need to draw.  Iterate over
    // any children and trigger their drawing operations.
    struct draw_update_data_t *d = scene_draw_update_data(s);
    if (!d) {
	for (size_t i = 0; i < BU_PTBL_LEN(&s->bsg.bsg_children); i++) {
	    struct bv_scene_obj *c = (struct bv_scene_obj *)BU_PTBL_GET(&s->bsg.bsg_children, i);
	    draw_scene(c, v);
	}
	return;
    }

    /**************************************************************************
     * A couple of the drawing modes have their own completely independent and
     * substantial logic defined elsewhere.  Spot those cases first and steer
     * them where they need to go
     **************************************************************************/

    /* Mode 3 generates an evaluated wireframe rather than drawing
     * the individual solid wireframes */
    if (scene_draw_mode(s) == 3) {
	draw_m3(s);
	bv_scene_obj_bound(s, v);
	s->current = 1;
	return;
    }

    /* Mode 5 draws a point cloud in lieu of wireframes */
    if (scene_draw_mode(s) == 5) {
	draw_points(s);
	bv_scene_obj_bound(s, v);
	s->current = 1;
	return;
    }


    /**************************************************************************
     * A couple of the object types will also have unique logic, typically for
     * special handling of difficult drawing cases.  Look for those as well.
     **************************************************************************/
    struct db_i *dbip = d->dbip;
    struct db_full_path *fp = scene_source_path(s);
    if (fp && fp->fp_len <= 0)
	return;
    struct directory *dp = scene_directory(s);
    if (!dp)
	return;

    // Adaptive BoTs have specialized LoD routines to help cope with very large
    // data sets, both for wireframe and shaded mode.
    int draw_mode = scene_draw_mode(s);
    if (dp->d_minor_type == DB5_MINORTYPE_BRLCAD_BOT && v && v->gv_s->adaptive_plot_mesh &&
       (draw_mode == 0 || draw_mode == 1)) {
	bot_adaptive_plot(s, v);
	return;
    }

    // Adaptive BReps have specialized LoD routines to manage shaded displays, which
    // can involve slow and large mesh generations.  BRep wireframes are based on the
    // NURBS data, so this is used only for shaded mode
    if (dp->d_minor_type == DB5_MINORTYPE_BRLCAD_BREP && v && v->gv_s->adaptive_plot_mesh && draw_mode == 1) {
	brep_adaptive_plot(s, v);
	return;
    }

    /**************************************************************************
     * For the remainder of the options we're into more standard wireframe
     * callback modes - crack the internal and stage the tolerances
     **************************************************************************/
    const struct bn_tol *tol = d->tol;
    const struct bg_tess_tol *ttol = d->ttol;
    struct rt_db_internal dbintern;
    RT_DB_INTERNAL_INIT(&dbintern);
    struct rt_db_internal *ip = &dbintern;
    mat_t s_mat;
    bsg_node_transform_get((const bsg_node *)s, s_mat);
    int ret = rt_db_get_internal(ip, dp, dbip, s_mat);
    if (ret < 0)
	return;

    // If we don't have a BRL-CAD type, see if we've got a plot routine
    if (ip->idb_major_type != DB5_MAJORTYPE_BRLCAD) {
	wireframe_plot(s, v, ip);
	goto geom_done;
    }

    // At least for the moment, we don't try anything fancy with pipes - they
    // get a wireframe, regardless of mode settings
    if (ip->idb_minor_type == DB5_MINORTYPE_BRLCAD_PIPE) {
	wireframe_plot(s, v, ip);
	goto geom_done;
    }

    // For anything other than mode 0, we call specific routines for
    // some of the primitives.
    if (draw_mode > 0) {
	switch (ip->idb_minor_type) {
	    case DB5_MINORTYPE_BRLCAD_BOT:
		(void)rt_bot_plot_poly(bsg_node_vlist_head((bsg_node *)s), ip, ttol, tol);
		goto geom_done;
		break;
	    case DB5_MINORTYPE_BRLCAD_POLY:
		(void)rt_pg_plot_poly(bsg_node_vlist_head((bsg_node *)s), ip, ttol, tol);
		goto geom_done;
		break;
	    case DB5_MINORTYPE_BRLCAD_BREP:
		(void)rt_brep_plot_poly(bsg_node_vlist_head((bsg_node *)s), dp, ip, ttol, tol, NULL);
		goto geom_done;
		break;
	    default:
		break;
	}
    }

    // Now the more general cases
    switch (draw_mode) {
	case 0:
	case 1:
	    // Get wireframe (for mode 1, all the non-wireframes are handled
	    // by the above BOT/POLY/BREP cases
	    wireframe_plot(s, v, ip);
	    scene_draw_mode_set(s, 0);
	    break;
	case 2:
	    // Shade everything except pipe, don't evaluate, fall
	    // back to wireframe in case of failure
	    if (prim_tess(s, ip) < 0) {
		wireframe_plot(s, v, ip);
		scene_draw_mode_set(s, 0);
	    } else {
		s->current = 1;
	    }
	    break;
	case 3:
	    // Evaluated wireframes
	    bu_log("Error - got too deep into _scene_obj_draw routine with drawing mode 3 - wireframe drawing with evaluated booleans\n");
	    return;
	    break;
	case 4:
	    // Hidden line - generate polygonal forms, fall back to
	    // un-hidden wireframe in case of failure
	    if (prim_tess(s, ip) < 0) {
		wireframe_plot(s, v, ip);
		scene_draw_mode_set(s, 0);
	    } else {
		s->current = 1;
	    }
	    break;
	case 5:
	    // Triangles at sampled points
	    bu_log("Error - got too deep into _scene_obj_draw routine with drawing mode 5 - triangles at ray-sampled points\n");
	    return;
	    break;
	default:
	    // Default to wireframe
	    wireframe_plot(s, v, ip);
	    break;
    }

geom_done:

    // Update s_size and s_center
    bv_scene_obj_bound(s, v);

    // Store current view info, in case of adaptive plotting
    s->adaptive_wireframe = s->s_v->gv_s->adaptive_plot_csg;
    s->view_scale = s->s_v->gv_scale;
    s->bot_threshold= s->s_v->gv_s->bot_threshold;
    s->curve_scale = s->s_v->gv_s->curve_scale;
    s->point_scale = s->s_v->gv_s->point_scale;

    rt_db_free_internal(&dbintern);
}

static void
tree_color(struct directory *dp, struct draw_data_t *dd)
{
    struct bu_attribute_value_set c_avs = BU_AVS_INIT_ZERO;

    // Easy answer - if we're overridden, dd color is already set.
    if (scene_color_override(dd->g))
	return;

    // Not overridden by settings.  Next question - are we under an inherit?
    // If so, dd color is already set.
    if (dd->color_inherit)
	return;

    // Need attributes for the rest of this
    db5_get_attributes(dd->dbip, &c_avs, dp);

    // No inherit.  Do we have a region material table?
    if (db_mater_head(dd->dbip) != MATER_NULL) {
	// If we do, do we have a region id?
	int region_id = -1;
	const char *region_id_val = bu_avs_get(&c_avs, "region_id");
	if (region_id_val) {
	    bu_opt_int(NULL, 1, &region_id_val, (void *)&region_id);
	} else if (dp->d_flags & RT_DIR_REGION) {
	    // If we have a region flag but no region_id, for color table
	    // purposes treat the region_id as 0
	    region_id = 0;
	}
	if (region_id >= 0) {
	    const struct mater *mp;
	    int material_color = 0;
	    for (mp = db_mater_head(dd->dbip); mp != MATER_NULL; mp = mp->mt_forw) {
		if (region_id > mp->mt_high || region_id < mp->mt_low) {
		    continue;
		}
		unsigned char mt[3];
		mt[0] = mp->mt_r;
		mt[1] = mp->mt_g;
		mt[2] = mp->mt_b;
		bu_color_from_rgb_chars(&dd->c, mt);
		material_color = 1;
	    }
	    if (material_color) {
		// Have answer from color table
		bu_avs_free(&c_avs);
		return;
	    }
	}
    }

    // Material table didn't give us the answer - do we have a color or
    // rgb attribute?
    const char *color_val = bu_avs_get(&c_avs, "color");
    if (!color_val) {
	color_val = bu_avs_get(&c_avs, "rgb");
    }
    if (color_val) {
	bu_opt_color(NULL, 1, &color_val, (void *)&dd->c);
    }

    // Check for an inherit flag
    dd->color_inherit = (BU_STR_EQUAL(bu_avs_get(&c_avs, "inherit"), "1")) ? 1 : 0;

    // Done with attributes
    bu_avs_free(&c_avs);
}

/*****************************************************************************

  The primary drawing subtree walk.

  To get an initial idea of scene size and center for adaptive plotting (i.e.
  before we have wireframes drawn) we also have a need to check ft_bbox ahead
  of the vlist generation.  It can be thought of as a "preliminary autoview"
  step.  That mode is also supported by this subtree walk.

******************************************************************************/
static void
draw_walk_tree(struct db_full_path *path, union tree *tp, mat_t *curr_mat,
			 void (*traverse_func) (struct db_full_path *path, mat_t *, void *),
			 void *client_data, void *comb_inst_map)
{
    mat_t om, nm;
    struct directory *dp;
    struct draw_data_t *dd= (struct draw_data_t *)client_data;
    std::unordered_map<std::string, int> *cinst_map = (std::unordered_map<std::string, int> *)comb_inst_map;

    if (!tp)
	return;

    RT_CK_FULL_PATH(path);
    RT_CHECK_DBI(dd->dbip);
    RT_CK_TREE(tp);

    switch (tp->tr_op) {
	case OP_NOT:
	case OP_GUARD:
	case OP_XNOP:
	    draw_walk_tree(path, tp->tr_b.tb_left, curr_mat, traverse_func, client_data, comb_inst_map);
	    break;
	case OP_UNION:
	case OP_INTERSECT:
	case OP_SUBTRACT:
	case OP_XOR:
	    draw_walk_tree(path, tp->tr_b.tb_left, curr_mat, traverse_func, client_data, comb_inst_map);
	    dd->bool_op = tp->tr_op;
	    draw_walk_tree(path, tp->tr_b.tb_right, curr_mat, traverse_func, client_data, comb_inst_map);
	    break;
	case OP_DB_LEAF:
	    if (UNLIKELY(dd->dbip->i->dbi_use_comb_instance_ids && cinst_map))
		(*cinst_map)[std::string(tp->tr_l.tl_name)]++;
	    if ((dp=db_lookup(dd->dbip, tp->tr_l.tl_name, LOOKUP_QUIET)) == RT_DIR_NULL) {
		return;
	    } else {

		/* Update current matrix state to reflect the new branch of
		 * the tree. Either we have a local matrix, or we have an
		 * implicit IDN matrix. */
		MAT_COPY(om, *curr_mat);
		if (tp->tr_l.tl_mat) {
		    MAT_COPY(nm, tp->tr_l.tl_mat);
		} else {
		    MAT_IDN(nm);
		}
		bn_mat_mul(*curr_mat, om, nm);

		// Stash current color settings and see if we're getting new ones
		struct bu_color oc;
		int inherit_old = dd->color_inherit;
		HSET(oc.buc_rgb, dd->c.buc_rgb[0], dd->c.buc_rgb[1], dd->c.buc_rgb[2], dd->c.buc_rgb[3]);
		if (!dd->bound_only) {
		    tree_color(dp, dd);
		}

		// Two things may prevent further processing - a hidden dp, or
		// a cyclic path.  Can check either here or in traverse_func -
		// just do it here since otherwise the logic would have to be
		// duplicated in all traverse functions.
		if (!(dp->d_flags & RT_DIR_HIDDEN)) {
		    db_add_node_to_full_path(path, dp);
		    DB_FULL_PATH_SET_CUR_BOOL(path, tp->tr_op);
		    if (UNLIKELY(dd->dbip->i->dbi_use_comb_instance_ids && cinst_map))
			DB_FULL_PATH_SET_CUR_COMB_INST(path, (*cinst_map)[std::string(tp->tr_l.tl_name)]-1);
		    if (!db_full_path_cyclic(path, NULL, 0)) {
			/* Keep going */
			traverse_func(path, curr_mat, client_data);
		    }
		}

		/* Done with branch - restore path, put back the old matrix state,
		 * and restore previous color settings */
		DB_FULL_PATH_POP(path);
		MAT_COPY(*curr_mat, om);
		if (!dd->bound_only) {
		    dd->color_inherit = inherit_old;
		    HSET(dd->c.buc_rgb, oc.buc_rgb[0], oc.buc_rgb[1], oc.buc_rgb[2], oc.buc_rgb[3]);
		}
		return;
	    }

	default:
	    bu_log("db_functree_subtree: unrecognized operator %d\n", tp->tr_op);
	    bu_bomb("db_functree_subtree: unrecognized operator\n");
    }
}

/**
 * This walker builds a list of db_full_path entries corresponding to
 * the contents of the tree under *path.  It does so while assigning
 * the boolean operation associated with each path entry to the
 * db_full_path structure.  This list is then used for further
 * processing and filtering by the search routines.
 */
extern "C" void
draw_gather_paths(struct db_full_path *path, mat_t *curr_mat, void *client_data)
{
    struct directory *dp;
    struct draw_data_t *dd= (struct draw_data_t *)client_data;
    RT_CK_FULL_PATH(path);
    RT_CK_DBI(dd->dbip);

    dp = DB_FULL_PATH_CUR_DIR(path);
    if (!dp)
	return;

    // If we're skipping subtractions and we have a subtraction op there's no
    // point in going further.
    if (scene_draw_non_subtract_only(dd->g) && dd->bool_op == 4) {
	return;
    }


    if (dp->d_flags & RT_DIR_COMB) {

	struct rt_db_internal in;
	struct rt_comb_internal *comb;

	if (rt_db_get_internal(&in, dp, dd->dbip, NULL) < 0)
	    return;

	comb = (struct rt_comb_internal *)in.idb_ptr;
	if (UNLIKELY(dd->dbip->i->dbi_use_comb_instance_ids)) {
	    std::unordered_map<std::string, int> cinst_map;
	    draw_walk_tree(path, comb->tree, curr_mat, draw_gather_paths, client_data, (void *)&cinst_map);
	} else {
	    draw_walk_tree(path, comb->tree, curr_mat, draw_gather_paths, client_data, NULL);
	}
	rt_db_free_internal(&in);

    } else {

	// If we've got a solid, things get interesting.  There are a lot of
	// potentially relevant options to sort through.  It may be that most
	// will end up getting handled by the object update callbacks, and the
	// job here will just be to set up the key data for later use...

	struct bv_scene_obj *s = bv_obj_get_child(dd->g);
	struct bsg_draw_request parent_request;
	db_path_to_vls(&s->bsg.bsg_name, path);
	struct db_full_path *sfp = NULL;
	BU_GET(sfp, struct db_full_path);
	db_full_path_init(sfp);
	db_dup_full_path(sfp, path);
	bsg_node_source_path_set((bsg_node *)s, (void *)sfp);
	bsg_node_app_data_set((bsg_node *)s, (void *)DB_FULL_PATH_CUR_DIR(path));
	bsg_node_transform_set((bsg_node *)s, *curr_mat);
	bsg_node_draw_request_get((const bsg_node *)dd->g, &parent_request);
	bsg_node_draw_request_set((bsg_node *)s, &parent_request);
	s->bsg.bsg_kind = BV_DBOBJ_BASED;
	s->current = 0;
	s->s_changed++;
	if (!scene_draw_solid_lines_only(s)) {
	    s->s_soldash = (dd->bool_op == 4) ? 1 : 0;
	}
	bu_color_to_rgb_chars(&dd->c, s->s_color);

	// TODO - check path against the GED default selected set - if we're
	// drawing something the app has already flagged as selected, need to
	// illuminate

	// Stash the information needed for a draw update callback
	struct draw_update_data_t *ud;
	BU_GET(ud, struct draw_update_data_t);
	ud->fp = (struct db_full_path *)bsg_node_source_path_get((const bsg_node *)s);
	ud->dbip = dd->dbip;
	ud->tol = dd->tol;
	ud->ttol = dd->ttol;
	ud->mesh_c = dd->mesh_c;
	bsg_node_user_data_set((bsg_node *)s, (void *)ud);
	bsg_node_set_free_callback((bsg_node *)s, (bsg_node_free_fn)&draw_free_data);

	// Let the object know about its size
	if (dd->s_size && dd->s_size->find(DB_FULL_PATH_CUR_DIR(path)) != dd->s_size->end()) {
	    s->s_size = (*dd->s_size)[DB_FULL_PATH_CUR_DIR(path)];
	}

    }
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
