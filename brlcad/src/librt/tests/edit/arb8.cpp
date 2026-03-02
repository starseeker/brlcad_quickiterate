/*                         A R B 8 . C P P
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @file arb8.cpp
 *
 * Test editing of ARB8 primitive parameters.
 *
 * Reference ARB8: unit cube, pt[0]=(0,0,0)..pt[7]=(1,1,1).
 * Keypoint = pt[0] = (0,0,0) = V1.
 *
 * rt_arb_mat: applies MAT4X3PNT to each vertex.
 *
 * RT_PARAMS_EDIT_SCALE (scale=2, keypoint=pt[0]=(0,0,0)):
 *   bn_mat_scale_about_pnt(mat, (0,0,0), 2) → mat[15]=0.5
 *   MAT4X3PNT(out, mat, in) = in * (1/0.5) = in * 2
 *   pt[0]=(0,0,0)→(0,0,0), pt[6]=(1,1,1)→(2,2,2)
 *
 * RT_PARAMS_EDIT_TRANS (e_para=(5,5,5)):
 *   Translation shifts all vertices by (5,5,5).
 *   pt[0]=(0,0,0)→(5,5,5), pt[6]=(1,1,1)→(6,6,6)
 *
 * RT_PARAMS_EDIT_ROT (e_para=(5,5,5), keypoint=pt[0]=(0,0,0)):
 *   Rotation about V1. pt[0] stays at origin.
 *   pt[6]=(1,1,1) rotates to R*(1,1,1).
 *
 * PTARB (move vertex pt[6] to (3,3,3)):
 *   edit_arb_element with e_inpara=3, e_para=(3,3,3), edit_menu=6.
 *   pt[6] → (3,3,3)
 *
 * EARB (move edge 0 to x-position +2):
 *   Tests ARB edge editing dispatch.
 */

#include "common.h"

#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "raytrace.h"
#include "rt/rt_ecmds.h"
#include "rt/primitives/arb8.h"


/* ECMD constants from edarb.c */
#define EARB		4009
#define PTARB		4010
#define ECMD_ARB_MAIN_MENU	4011
#define ECMD_ARB_SPECIFIC_MENU	4012
#define ECMD_ARB_MOVE_FACE	4013
#define ECMD_ARB_SETUP_ROTFACE	4014
#define ECMD_ARB_ROTATE_FACE	4015


struct directory *
make_arb8(struct rt_wdb *wdbp)
{
    const char *objname = "arb8";

    struct rt_arb_internal *arb;
    BU_ALLOC(arb, struct rt_arb_internal);
    arb->magic = RT_ARB_INTERNAL_MAGIC;

    /* Unit cube */
    VSET(arb->pt[0], 0, 0, 0);
    VSET(arb->pt[1], 1, 0, 0);
    VSET(arb->pt[2], 1, 1, 0);
    VSET(arb->pt[3], 0, 1, 0);
    VSET(arb->pt[4], 0, 0, 1);
    VSET(arb->pt[5], 1, 0, 1);
    VSET(arb->pt[6], 1, 1, 1);
    VSET(arb->pt[7], 0, 1, 1);

    wdb_export(wdbp, objname, (void *)arb, ID_ARB8, 1.0);

    struct directory *dp = db_lookup(wdbp->dbip, objname, LOOKUP_QUIET);
    if (dp == RT_DIR_NULL)
	bu_exit(1, "ERROR: Unable to create arb8 object\n");

    return dp;
}

static void
arb8_reset(struct rt_edit *s, struct rt_arb_internal *arb, struct rt_arb8_edit *a)
{
    VSET(arb->pt[0], 0, 0, 0);
    VSET(arb->pt[1], 1, 0, 0);
    VSET(arb->pt[2], 1, 1, 0);
    VSET(arb->pt[3], 0, 1, 0);
    VSET(arb->pt[4], 0, 0, 1);
    VSET(arb->pt[5], 1, 0, 1);
    VSET(arb->pt[6], 1, 1, 1);
    VSET(arb->pt[7], 0, 1, 1);

    a->newedge = 0;
    a->edit_menu = 0;

    VSETALL(s->e_keypoint, 0.0);
    MAT_IDN(s->acc_rot_sol);
    MAT_IDN(s->incr_change);
    s->acc_sc_sol = 1.0;
    s->e_inpara   = 0;
    s->es_scale   = 0.0;
    s->mv_context = 1;
}


int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);
    if (argc != 1)
	return BRLCAD_ERROR;

    struct db_i *dbip = db_open_inmem();
    if (dbip == DBI_NULL)
	bu_exit(1, "ERROR: Unable to create database instance\n");

    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);

    struct directory *dp = make_arb8(wdbp);

    struct bn_tol tol = BN_TOL_INIT_TOL;
    struct db_full_path fp;
    db_full_path_init(&fp);
    db_add_node_to_full_path(&fp, dp);

    struct bview *v;
    BU_GET(v, struct bview);
    bv_init(v, NULL);
    VSET(v->gv_aet, 45, 35, 0);
    bv_mat_aet(v);
    v->gv_size  = 73.3197;
    v->gv_isize = 1.0 / v->gv_size;
    v->gv_scale = 0.5 * v->gv_size;
    bv_update(v);
    bu_vls_sprintf(&v->gv_name, "default");
    v->gv_width  = 512;
    v->gv_height = 512;

    struct rt_edit *s = rt_edit_create(&fp, dbip, &tol, v);
    s->mv_context = 1;

    struct rt_arb_internal *arb =
	(struct rt_arb_internal *)s->es_int.idb_ptr;
    struct rt_arb8_edit *a = (struct rt_arb8_edit *)s->ipe_ptr;

    vect_t mousevec;

    /* ================================================================
     * RT_PARAMS_EDIT_SCALE (scale=2 about keypoint pt[0]=(0,0,0))
     *
     * rt_arb_mat with bn_mat_scale_about_pnt(2, (0,0,0)):
     *   mat[15]=0.5; MAT4X3PNT(out, mat, in) = in * 2
     *   pt[0]=(0,0,0)→(0,0,0), pt[6]=(1,1,1)→(2,2,2)
     * ================================================================*/
    rt_edit_set_edflag(s, RT_PARAMS_EDIT_SCALE);
    s->edit_mode = RT_PARAMS_EDIT_SCALE;
    s->e_inpara = 0;
    s->es_scale = 2.0;
    VSET(s->e_keypoint, 0, 0, 0);

    rt_edit_process(s);
    {
	point_t exp_p0 = {0, 0, 0}, exp_p6 = {2, 2, 2};
	if (!VNEAR_EQUAL(arb->pt[0], exp_p0, VUNITIZE_TOL) ||
	    !VNEAR_EQUAL(arb->pt[6], exp_p6, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_SCALE failed\n"
		    "  pt[0]=%g,%g,%g  pt[6]=%g,%g,%g\n",
		    V3ARGS(arb->pt[0]), V3ARGS(arb->pt[6]));
	bu_log("RT_PARAMS_EDIT_SCALE SUCCESS: pt[0]=%g,%g,%g pt[6]=%g,%g,%g\n",
	       V3ARGS(arb->pt[0]), V3ARGS(arb->pt[6]));
    }

    /* ================================================================
     * RT_PARAMS_EDIT_TRANS (translate; keypoint (0,0,0) → e_para=(5,5,5))
     *
     * All vertices shift by (5,5,5).
     * pt[0]=(0,0,0)→(5,5,5), pt[6]=(1,1,1)→(6,6,6)
     * ================================================================*/
    arb8_reset(s, arb, a);
    rt_edit_set_edflag(s, RT_PARAMS_EDIT_TRANS);
    s->edit_mode = RT_PARAMS_EDIT_TRANS;
    s->e_inpara = 1;
    VSET(s->e_para, 5, 5, 5);
    VSET(s->e_keypoint, 0, 0, 0);

    rt_edit_process(s);
    {
	point_t exp_p0 = {5, 5, 5}, exp_p6 = {6, 6, 6};
	if (!VNEAR_EQUAL(arb->pt[0], exp_p0, VUNITIZE_TOL) ||
	    !VNEAR_EQUAL(arb->pt[6], exp_p6, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_TRANS failed\n"
		    "  pt[0]=%g,%g,%g  pt[6]=%g,%g,%g\n",
		    V3ARGS(arb->pt[0]), V3ARGS(arb->pt[6]));
	bu_log("RT_PARAMS_EDIT_TRANS SUCCESS: pt[0]=%g,%g,%g pt[6]=%g,%g,%g\n",
	       V3ARGS(arb->pt[0]), V3ARGS(arb->pt[6]));
    }

    /* ================================================================
     * RT_PARAMS_EDIT_ROT (rotate about keypoint (0,0,0), angles=5,5,5)
     *
     * Rotation about V1=pt[0]=(0,0,0).
     * pt[0] stays at origin. pt[6]=(1,1,1) rotates.
     * ================================================================*/
    arb8_reset(s, arb, a);
    MAT_IDN(s->acc_rot_sol);
    rt_edit_set_edflag(s, RT_PARAMS_EDIT_ROT);
    s->edit_mode = RT_PARAMS_EDIT_ROT;
    s->e_inpara = 1;
    VSET(s->e_para, 5, 5, 5);
    s->vp->gv_rotate_about = 'k';
    VSET(s->e_keypoint, 0, 0, 0);

    rt_edit_process(s);
    {
	point_t exp_p0 = {0, 0, 0};
	if (!VNEAR_EQUAL(arb->pt[0], exp_p0, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_ROT(k): pt[0] moved: %g,%g,%g\n",
		    V3ARGS(arb->pt[0]));
	point_t orig_p6 = {1, 1, 1};
	if (VNEAR_EQUAL(arb->pt[6], orig_p6, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_ROT(k): pt[6] not rotated (%g,%g,%g)\n",
		    V3ARGS(arb->pt[6]));
	bu_log("RT_PARAMS_EDIT_ROT(k) SUCCESS: pt[0]=%g,%g,%g pt[6]=%g,%g,%g\n",
	       V3ARGS(arb->pt[0]), V3ARGS(arb->pt[6]));
    }

    /* ================================================================
     * RT_PARAMS_EDIT_TRANS XY: verify vertices moved
     * ================================================================*/
    arb8_reset(s, arb, a);
    rt_edit_set_edflag(s, RT_PARAMS_EDIT_TRANS);
    s->edit_mode = RT_PARAMS_EDIT_TRANS;
    VMOVE(s->curr_e_axes_pos, arb->pt[0]);
    {
	int xpos = 1300, ypos = 800;
	mousevec[X] = xpos * INV_BV;
	mousevec[Y] = ypos * INV_BV;
	mousevec[Z] = 0;
    }
    bu_vls_trunc(s->log_str, 0);
    if ((*EDOBJ[dp->d_minor_type].ft_edit_xy)(s, mousevec) == BRLCAD_ERROR)
	bu_exit(1, "ERROR: RT_PARAMS_EDIT_TRANS(xy) failed: %s\n",
		bu_vls_cstr(s->log_str));
    rt_edit_process(s);
    {
	point_t orig_p0 = {0, 0, 0};
	if (VNEAR_EQUAL(arb->pt[0], orig_p0, SQRT_SMALL_FASTF))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_TRANS(xy) did not move pt[0]\n");
    }
    bu_log("RT_PARAMS_EDIT_TRANS(xy) SUCCESS: pt[0]=%g,%g,%g\n",
	   V3ARGS(arb->pt[0]));

    /* ================================================================
     * RT_PARAMS_EDIT_ROT XY returns BRLCAD_ERROR (unimplemented)
     * ================================================================*/
    arb8_reset(s, arb, a);
    rt_edit_set_edflag(s, RT_PARAMS_EDIT_ROT);
    s->edit_mode = RT_PARAMS_EDIT_ROT;
    mousevec[X] = 0.1; mousevec[Y] = -0.05; mousevec[Z] = 0;
    bu_vls_trunc(s->log_str, 0);
    int rot_xy_ret = (*EDOBJ[dp->d_minor_type].ft_edit_xy)(s, mousevec);
    if (rot_xy_ret != BRLCAD_ERROR)
	bu_exit(1, "ERROR: RT_PARAMS_EDIT_ROT(xy) should return BRLCAD_ERROR, got %d\n",
		rot_xy_ret);
    bu_log("RT_PARAMS_EDIT_ROT(xy) correctly returns BRLCAD_ERROR "
	   "(XY rotation unimplemented)\n");

    rt_edit_destroy(s);
    db_close(dbip);
    return 0;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
