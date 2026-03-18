/*                         D R A W . C P P
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
#include "bsg/defines.h"
#include "bg/sat.h"
#include "bsg/lod.h"
#include "nmg.h"
#include "rt/view.h"

#include "ged/view.h"
#include "./ged_private.h"
#include "./dbi.h"

static void
draw_free_data(bsg_shape *s)
{
    /* Validate */
    if (!s)
	return;

    if (s->s_path) {
	struct db_full_path *sfp = (struct db_full_path *)s->s_path;
	db_free_full_path(sfp);
	BU_PUT(sfp, struct db_full_path);
    }

    /* free drawing info */
    struct draw_update_data_t *d = (struct draw_update_data_t *)s->s_i_data;
    if (!d)
	return;
    BU_PUT(d, struct draw_update_data_t);
    s->s_i_data = NULL;
}


extern "C" int rt_generic_scene_obj(bsg_shape *s, struct directory *dp,
				    struct db_i *dbip,
				    const struct bg_tess_tol *ttol,
				    const struct bn_tol *tol,
				    const bsg_view *v);

/* This function is the master controller that decides, based on available settings
 * and data, which specific drawing routines need to be triggered.
 *
 * After the setup phase, dispatch to OBJ[dp->d_minor_type].ft_scene_obj() for
 * all primitive types.  The per-primitive ft_scene_obj implementations
 * (rt_bot_scene_obj, rt_brep_scene_obj, rt_comb_scene_obj,
 * rt_generic_scene_obj) contain all the per-type drawing logic — there are
 * no remaining special cases in draw_scene.
 *
 * Mode 3 (evaluated wireframe for combs) is handled by rt_comb_scene_obj
 * → rt_comb_eval_m3() in librt/comb/comb_scene_obj.c.
 *
 * BREP mode 1 + adaptive_plot_mesh (LoD-managed hidden-line) is handled by
 * rt_brep_scene_obj() in librt/primitives/brep/brep.cpp.
 */
extern "C" void
draw_scene(bsg_shape *s, bsg_view *v)
{
    // If the scene object indicates we're good, don't repeat.
    if (s->current && !v)
	return;

    bsg_log(1, "draw_scene %s[%s]", bu_vls_cstr(&s->s_name), (v) ? bu_vls_cstr(&v->gv_name) : "NULL");

    // If we're not adaptive, trigger the view insensitive drawing routines
    if (v && !v->gv_s->adaptive_plot_csg && !v->gv_s->adaptive_plot_mesh) {
	return draw_scene(s, NULL);
    }

    // If we have a scene object without drawing data, it is most likely
    // a container holding other objects we do need to draw.  Iterate over
    // any children and trigger their drawing operations.
    struct draw_update_data_t *d = (struct draw_update_data_t *)s->s_i_data;
    if (!d) {
	for (size_t i = 0; i < BU_PTBL_LEN(&s->children); i++) {
	    bsg_shape *c = (bsg_shape *)BU_PTBL_GET(&s->children, i);
	    draw_scene(c, v);
	}
	return;
    }

    /**************************************************************************
     * Path setup: resolve dp and dbip from s->s_i_data.
     **************************************************************************/
    struct db_i *dbip = d->dbip;
    struct db_full_path *fp = (struct db_full_path *)s->s_path;
    if (fp && fp->fp_len <= 0)
	return;
    struct directory *dp = (fp) ? DB_FULL_PATH_CUR_DIR(fp) : (struct directory *)s->dp;
    if (!dp)
	return;

    /**************************************************************************
     * Pipeline setup phase: propagate pipeline results from DbiState onto the
     * shape before ANY drawing dispatch occurs.
     *
     * This is the ONLY place in draw_scene where d->dbis is consulted.  After
     * this block, all downstream code — including rt_bot_scene_obj(),
     * rt_brep_scene_obj(), and other ft_scene_obj() implementations in librt —
     * reads only from bsg_shape fields (s->mesh_c, s->have_bbox, s->s_have_obb,
     * s->s_obb_pts).  This cleanly separates the libged pipeline layer from
     * the per-primitive rendering layer.
     **************************************************************************/
    if (d->mesh_c && !s->mesh_c)
	s->mesh_c = d->mesh_c;

    /* Forward the per-thread resource pointer so ft_scene_obj callbacks can
     * call rt_db_get_internal() (and ft_mat) without accessing draw_update_data_t. */
    if (d->res && !s->s_res)
	s->s_res = d->res;

    if (v && d->dbis) {
	unsigned long long sh =
	    bu_data_hash(dp->d_namep, strlen(dp->d_namep) * sizeof(char));

	/* Late-populate AABB from the async pipeline when not yet available. */
	if (!s->have_bbox) {
	    auto bit = d->dbis->bboxes.find(sh);
	    if (bit != d->dbis->bboxes.end() && bit->second.size() == 6) {
		const auto &bb = bit->second;
		fastf_t x0=bb[0], y0=bb[1], z0=bb[2];
		fastf_t x1=bb[3], y1=bb[4], z1=bb[5];
		point_t corners[8] = {
		    {x0,y0,z0}, {x1,y0,z0}, {x0,y1,z0}, {x1,y1,z0},
		    {x0,y0,z1}, {x1,y0,z1}, {x0,y1,z1}, {x1,y1,z1}
		};
		point_t wbmin, wbmax;
		VSETALL(wbmin,  1e300); VSETALL(wbmax, -1e300);
		for (int _k = 0; _k < 8; _k++) {
		    point_t wc; MAT4X3PNT(wc, s->s_mat, corners[_k]);
		    VMIN(wbmin, wc); VMAX(wbmax, wc);
		}
		VMOVE(s->bmin, wbmin); VMOVE(s->bmax, wbmax);
		s->s_center[X] = (s->bmin[X]+s->bmax[X]) * 0.5;
		s->s_center[Y] = (s->bmin[Y]+s->bmax[Y]) * 0.5;
		s->s_center[Z] = (s->bmin[Z]+s->bmax[Z]) * 0.5;
		s->s_size = s->bmax[X]-s->bmin[X];
		V_MAX(s->s_size, s->bmax[Y]-s->bmin[Y]);
		V_MAX(s->s_size, s->bmax[Z]-s->bmin[Z]);
		s->have_bbox = 1;
	    }
	}

	/* Cache OBB corner data on the shape so drawing callbacks can draw
	 * OBB placeholder wireframes without consulting d->dbis directly.
	 * Only write once (s_have_obb acts as a "written" flag). */
	if (!s->s_have_obb) {
	    auto oit = d->dbis->obbs.find(sh);
	    if (oit != d->dbis->obbs.end()) {
		memcpy(s->s_obb_pts, oit->second.data(), 24 * sizeof(fastf_t));
		s->s_have_obb = 1;
	    }
	}
    }

    /* For non-BoT shapes in the adaptive path, pre-classify as CSG wireframe
     * so BViewState::redraw() can queue this shape for retry after the
     * async AABB pipeline delivers the bbox.  BoT and BREP shapes are
     * reclassified to mesh_obj=1 by their ft_scene_obj implementations
     * (rt_bot_scene_obj and rt_brep_scene_obj respectively). */
    if (v && dp->d_minor_type != DB5_MINORTYPE_BRLCAD_BOT) {
	s->csg_obj  = 1;
	s->mesh_obj = 0;
    }

    /* Lazy AABB placeholder upgrade: if we now have a bbox but still hold
     * a stale placeholder view-object, clear it so ft_scene_obj below
     * generates real geometry. */
    if (v && s->have_bbox) {
	bsg_shape *vo = bsg_shape_for_view(s, v);
	if (vo && !vo->draw_data && vo->s_placeholder > 0)
	    bsg_shape_clear_view_obj(s, v);
    }

    /**************************************************************************
     * ft_scene_obj dispatch — the primary drawing path.
     *
     * OBJ[dp->d_minor_type].ft_scene_obj() is called for every primitive type.
     * Each implementation handles:
     *   - rt_bot_scene_obj:     LoD progressive rendering (absorbed from
     *                           bot_adaptive_plot in this session); Obol SoNode
     *                           when BRLCAD_ENABLE_OBOL is set.
     *   - rt_brep_scene_obj:    BREP adaptive tessellation + Obol SoNode.
     *   - rt_comb_scene_obj:    Container no-op (returns BRLCAD_OK immediately).
     *   - rt_generic_scene_obj: All other primitives; handles all dmodes
     *                           (wireframe, shaded, point-cloud via
     *                           rt_sample_pnts) and the !have_bbox placeholder
     *                           path.
     *
     * This replaces the former fallback rt_db_get_internal + per-mode switch
     * that was the old draw_scene epilogue.
     **************************************************************************/
    const struct bn_tol *tol = d->tol;
    const struct bg_tess_tol *ttol = d->ttol;

    if (OBJ[dp->d_minor_type].ft_scene_obj) {
	OBJ[dp->d_minor_type].ft_scene_obj(s, dp, dbip, ttol, tol, v);
    } else {
	/* No ft_scene_obj for this type — fall back to generic. */
	rt_generic_scene_obj(s, dp, dbip, ttol, tol, v);
    }

    /* Update s_size and s_center */
    bsg_shape_bound(s, v);

    /* Store current view info, in case of adaptive plotting */
    if (s->s_v) {
	s->adaptive_wireframe = s->s_v->gv_s->adaptive_plot_csg;
	s->view_scale    = s->s_v->gv_scale;
	s->bot_threshold = s->s_v->gv_s->bot_threshold;
	s->curve_scale   = s->s_v->gv_s->curve_scale;
	s->point_scale   = s->s_v->gv_s->point_scale;
    }
}

static void
tree_color(struct directory *dp, struct draw_data_t *dd)
{
    struct bu_attribute_value_set c_avs = BU_AVS_INIT_ZERO;

    // Easy answer - if we're overridden, dd color is already set.
    if (dd->g->s_os->color_override)
	return;

    // Not overridden by settings.  Next question - are we under an inherit?
    // If so, dd color is already set.
    if (dd->color_inherit)
	return;

    // Need attributes for the rest of this
    db5_get_attributes(dd->dbip, &c_avs, dp);

    // No inherit.  Do we have a region material table?
    if (rt_material_head() != MATER_NULL) {
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
	    for (mp = rt_material_head(); mp != MATER_NULL; mp = mp->mt_forw) {
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
	    if (UNLIKELY(dd->dbip->dbi_use_comb_instance_ids && cinst_map))
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
		    if (UNLIKELY(dd->dbip->dbi_use_comb_instance_ids && cinst_map))
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
    if (dd->g->s_os->draw_non_subtract_only && dd->bool_op == 4) {
	return;
    }


    if (dp->d_flags & RT_DIR_COMB) {

	struct rt_db_internal in;
	struct rt_comb_internal *comb;

	if (rt_db_get_internal(&in, dp, dd->dbip, NULL, &rt_uniresource) < 0)
	    return;

	comb = (struct rt_comb_internal *)in.idb_ptr;
	if (UNLIKELY(dd->dbip->dbi_use_comb_instance_ids)) {
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

	bsg_shape *s = bsg_shape_get_child(dd->g);
	db_path_to_vls(&s->s_name, path);
	BU_GET(s->s_path, struct db_full_path);
	db_full_path_init((struct db_full_path *)s->s_path);
	db_dup_full_path((struct db_full_path *)s->s_path, path);

	MAT_COPY(s->s_mat, *curr_mat);
	bsg_material_sync(s->s_os, dd->g->s_os);
	s->s_type_flags = BSG_NODE_DBOBJ_BASED;
	s->current = 0;
	s->s_changed++;
	if (!s->s_os->draw_solid_lines_only) {
	    s->s_soldash = (dd->bool_op == 4) ? 1 : 0;
	}
	bu_color_to_rgb_chars(&dd->c, s->s_color);

	// TODO - check path against the GED default selected set - if we're
	// drawing something the app has already flagged as selected, need to
	// illuminate

	// Stash the information needed for a draw update callback
	struct draw_update_data_t *ud;
	BU_GET(ud, struct draw_update_data_t);
	ud->fp = (struct db_full_path *)s->s_path;
	ud->dbip = dd->dbip;
	ud->tol = dd->tol;
	ud->ttol = dd->ttol;
	ud->mesh_c = dd->mesh_c;
	ud->res = &rt_uniresource; // TODO - at some point this may be from the app or view.  dd->res is temporary, so we don't use it here
	ud->dbis = dd->dbis;
	s->s_i_data = (void *)ud;
	s->s_free_callback = &draw_free_data;

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

