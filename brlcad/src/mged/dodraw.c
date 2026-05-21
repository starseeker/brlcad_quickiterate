/*                        D O D R A W . C
 * BRL-CAD
 *
 * Copyright (c) 1985-2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */

#include "common.h"

#include <string.h>

#include "vmath.h"
#include "bn.h"
#include "nmg.h"
#include "rt/geom.h"		/* for ID_POLY special support */
#include "raytrace.h"
#include "rt/db4.h"
#include "bsg/appearance.h"
#include "bsg/node.h"
#include "bsg/payload.h"

#include "./mged.h"
#include "./mged_dm.h"
#include "./cmd.h"

#define GET_BV_SCENE_OBJ(p, fp) { \
          if (BU_LIST_IS_EMPTY(fp)) { \
              BU_ALLOC((p), struct bv_scene_obj); \
              struct ged_bv_data *bdata; \
              BU_GET(bdata, struct ged_bv_data); \
              db_full_path_init(&bdata->s_fullpath); \
              bsg_node_ged_data_set((bsg_node *)(p), (void *)bdata); \
          } else { \
              p = BU_LIST_NEXT(bv_scene_obj, fp); \
              BU_LIST_DEQUEUE(&((p)->bsg.l)); \
              if (bsg_node_ged_data_get((bsg_node *)(p))) { \
                  struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_ged_data_get((bsg_node *)(p)); \
                  bdata->s_fullpath.fp_len = 0; \
              } \
          } \
          BU_LIST_INIT(bsg_node_vlist_head((bsg_node *)(p))); }


/*
 * Compute the min, max, and center points of the solid.
 * Also finds s_vlen;
 */
static void
mged_bound_solid(struct mged_state *s, struct bv_scene_obj *sp)
{
    point_t bmin, bmax;
    size_t length = 0;
    int cmd;
    int dispmode;
    VSET(bmin, INFINITY, INFINITY, INFINITY);
    VSET(bmax, -INFINITY, -INFINITY, -INFINITY);

    cmd = bv_vlist_bbox(bsg_node_vlist_head((bsg_node *)sp), &bmin, &bmax, &length, &dispmode);
    if (cmd) {
	struct bu_vls tmp_vls = BU_VLS_INIT_ZERO;
	bu_vls_printf(&tmp_vls, "unknown vlist op %d\n", cmd);
	Tcl_AppendResult(s->interp, bu_vls_addr(&tmp_vls), (char *)NULL);
	bu_vls_free(&tmp_vls);
    }

    bsg_node_vlist_count_set((bsg_node *)sp, (size_t)length);
    {
	vect_t center;
	VADD2SCALE(center, bmin, bmax, 0.5);
	bsg_node_center_set((bsg_node *)sp, center);
    }
    {
	fastf_t sz = bmax[X] - bmin[X];
	V_MAX(sz, bmax[Y] - bmin[Y]);
	V_MAX(sz, bmax[Z] - bmin[Z]);
	bsg_node_size_set((bsg_node *)sp, sz);
    }
    bsg_node_set_display_obj((bsg_node *)sp, dispmode);
}


/*
 * Once the vlist has been created, perform the common tasks
 * in handling the drawn solid.
 *
 * This routine must be prepared to run in parallel.
 */
void
drawH_part2(struct mged_state *s, int dashflag, struct bu_list *vhead, const struct db_full_path *pathp, struct db_tree_state *tsp, struct bv_scene_obj *existing_sp)
{
    struct bv_scene_obj *sp;

    if (!existing_sp) {
	/* Handling a new solid */
	struct bv_scene_obj *free_scene_obj = bv_set_fsos(&s->gedp->ged_views);
	GET_BV_SCENE_OBJ(sp, &free_scene_obj->bsg.l);
	BU_LIST_APPEND(&free_scene_obj->bsg.l, &((sp)->bsg.l) );
    } else {
	/* Just updating an existing solid.
	 * 'tsp' and 'pathpos' will not be used
	 */
	sp = existing_sp;
    }

    /*
     * Compute the min, max, and center points.
     */
    BU_LIST_APPEND_LIST(bsg_node_vlist_head((bsg_node *)sp), vhead);
    mged_bound_solid(s, sp);

    /*
     * If this solid is new, fill in its information.
     * Otherwise, don't touch what is already there.
     */
    if (!existing_sp) {
	/* Take note of the base color */
	bsg_node_set_legacy_uflag((bsg_node *)sp, 0);
	if (tsp) {
	    if (tsp->ts_mater.ma_color_valid) {
		bsg_node_set_legacy_dflag((bsg_node *)sp, 0); /* color specified in db */
	    } else {
		bsg_node_set_legacy_dflag((bsg_node *)sp, 1); /* default color */
	    }
	    /* Copy into basecolor anyway, to prevent black */
	    bsg_node_legacy_basecolor_set((bsg_node *)sp,
		(unsigned char)(tsp->ts_mater.ma_color[0] * 255.0),
		(unsigned char)(tsp->ts_mater.ma_color[1] * 255.0),
		(unsigned char)(tsp->ts_mater.ma_color[2] * 255.0));
	}
	bsg_node_set_legacy_cflag((bsg_node *)sp, 0);
	sp->bsg.bsg_iflag = DOWN;
	bsg_node_set_line_style((bsg_node *)sp,
	    dashflag ? BSG_APPEARANCE_LINE_DASHED : BSG_APPEARANCE_LINE_SOLID);
	bsg_node_set_legacy_eflag((bsg_node *)sp, 0); /* This is a solid */
	if (bsg_node_ged_data_get((bsg_node *)sp)) {
	    struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_ged_data_get((bsg_node *)sp);
	    db_dup_full_path(&bdata->s_fullpath, pathp);
	}
	if (tsp)
	    bsg_node_set_legacy_regionid((bsg_node *)sp, tsp->ts_regionid);
    }

    /* Solid is successfully drawn */
    if (!existing_sp) {
	/* Add to linked list of solid structs */
	bu_semaphore_acquire(RT_SEM_MODEL);

	bsg_view_obj_append_to_last_group(s->gedp, sp);

	bu_semaphore_release(RT_SEM_MODEL);
    } else {
	/* replacing existing solid -- struct already linked in */
	sp->bsg.bsg_iflag = UP;
    }
}

/*
 * Given an existing solid structure that may have been subjected to
 * solid editing, recompute the vector list, etc., to make the solid
 * the same as it originally was.
 *
 * Returns -
 * -1 error
 * 0 OK
 */
int
replot_original_solid(struct mged_state *s, struct bv_scene_obj *sp)
{
    struct rt_db_internal intern;
    struct directory *dp;
    mat_t mat;

    if (s->dbip == DBI_NULL)
	return 0;

    if (!bsg_node_ged_data_get((bsg_node *)sp))
	return 0;
    struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_ged_data_get((bsg_node *)sp);
    dp = LAST_SOLID(bdata);
    if (bsg_node_legacy_eflag((const bsg_node *)sp)) {
	Tcl_AppendResult(s->interp, "replot_original_solid(", dp->d_namep,
			 "): Unable to plot evaluated regions, skipping\n", (char *)NULL);
	return -1;
    }
    (void)db_path_to_mat(s->dbip, &bdata->s_fullpath, mat, bdata->s_fullpath.fp_len-1);

    if (rt_db_get_internal(&intern, dp, s->dbip, mat) < 0) {
	Tcl_AppendResult(s->interp, dp->d_namep, ":  solid import failure\n", (char *)NULL);
	return -1;		/* ERROR */
    }
    RT_CK_DB_INTERNAL(&intern);

    if (replot_modified_solid(s, sp, &intern, bn_mat_identity) < 0) {
	rt_db_free_internal(&intern);
	return -1;
    }
    rt_db_free_internal(&intern);
    return 0;
}


/*
 * Given the solid structure of a solid that has already been drawn,
 * and a new database record and transform matrix,
 * create a new vector list for that solid, and substitute.
 * Used for solid editing mode.
 *
 * Returns -
 * -1 error
 * 0 OK
 */
int
replot_modified_solid(
	struct mged_state *s,
	struct bv_scene_obj *sp,
	struct rt_db_internal *ip,
	const mat_t mat)
{
    struct rt_db_internal intern;
    struct bu_list vhead;

    RT_DB_INTERNAL_INIT(&intern);

    BU_LIST_INIT(&vhead);

    if (sp == NULL) {
	Tcl_AppendResult(s->interp, "replot_modified_solid() sp==NULL?\n", (char *)NULL);
	return -1;
    }

    /* Release existing vlist of this solid */
    {
	struct bu_list *_vlfree = bsg_node_vlfree((bsg_node *)sp);
	struct bu_list *_vhead = bsg_node_vlist_head((bsg_node *)sp);
	BV_FREE_VLIST(_vlfree, _vhead);
    }

    /* Draw (plot) a normal solid */
    RT_CK_DB_INTERNAL(ip);

    s->tol.ttol.magic = BG_TESS_TOL_MAGIC;
    s->tol.ttol.abs = s->tol.abs_tol;
    s->tol.ttol.rel = s->tol.rel_tol;
    s->tol.ttol.norm = s->tol.nrm_tol;

    transform_editing_solid(s, &intern, mat, ip, 0);

    if (OBJ[ip->idb_type].ft_plot(&vhead, &intern, &s->tol.ttol, &s->tol.tol, NULL) < 0) {
	if (!bsg_node_ged_data_get((bsg_node *)sp))
	    return -1;
	struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_ged_data_get((bsg_node *)sp);
	if (bdata->s_fullpath.fp_len > 0)
	    Tcl_AppendResult(s->interp, LAST_SOLID(bdata)->d_namep,
		    ": re-plot failure\n", (char *)NULL);
	return -1;
    }
    rt_db_free_internal(&intern);

    /* Write new displaylist */
    drawH_part2(s, bsg_node_line_style((const bsg_node *)sp) != BSG_APPEARANCE_LINE_SOLID, &vhead,
		(struct db_full_path *)0,
		(struct db_tree_state *)0, sp);

    view_state->vs_flag = 1;
    return 0;
}

void
add_solid_path_to_result(
    Tcl_Interp *interp,
    struct bv_scene_obj *sp)
{
    struct bu_vls str = BU_VLS_INIT_ZERO;
    if (!sp || !bsg_node_ged_data_get((bsg_node *)sp))
	return;
    struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_ged_data_get((bsg_node *)sp);
    db_path_to_vls(&str, &bdata->s_fullpath);
    Tcl_AppendResult(interp, bu_vls_addr(&str), " ", NULL);
    bu_vls_free(&str);
}

int
redraw_visible_objects(struct mged_state *s)
{
    const char *av[1] = {"redraw"};
    int ret = ged_exec_redraw(s->gedp, 1, av);

    if (ret & BRLCAD_ERROR)
	return TCL_ERROR;

    return TCL_OK;
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
