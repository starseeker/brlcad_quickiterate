/*                         P I P E . C P P
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
/** @file pipe.cpp
 *
 * Test editing of PIPE primitive parameters.
 *
 * Reference PIPE: 2 segments
 *   pt1: coord=(0,0,0),  od=2.0, id=1.0, bendradius=1.0
 *   pt2: coord=(0,0,10), od=2.0, id=1.0, bendradius=1.0
 *
 * Keypoint = first segment coord = (0,0,0) when nothing selected.
 *
 * rt_pipe_mat: MAT4X3PNT for coord; od/id/bendradius scale by 1/mat[15].
 * For scale=s about (0,0,0): coord *= s, od/id/bendradius *= s.
 * For translation: coord shifts; od/id/bendradius unchanged (mat[15]=1).
 * For rotation: coord rotates; od/id/bendradius unchanged.
 *
 * ECMD_PIPE_SCALE_OD: scales all pipe point od by es_scale.
 * ECMD_PIPE_PT_MOVE: moves selected point to new_pt (e_inpara=3, mv_context=1).
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
#include "rt/primitives/pipe.h"
#include "rt/rt_ecmds.h"


/* Mirror the private struct from edpipe.c */
struct rt_pipe_edit_local {
    struct wdb_pipe_pnt *es_pipe_pnt;
};

/* ECMD constants from edpipe.c */
#define ECMD_PIPE_PT_MOVE	15033
#define ECMD_PIPE_SCALE_OD	15067
#define ECMD_PIPE_SCALE_ID	15068
#define ECMD_PIPE_SCALE_RADIUS	15074


struct directory *
make_pipe(struct rt_wdb *wdbp)
{
    const char *objname = "pipe";

    struct rt_pipe_internal *pip;
    BU_ALLOC(pip, struct rt_pipe_internal);
    pip->pipe_magic = RT_PIPE_INTERNAL_MAGIC;
    BU_LIST_INIT(&pip->pipe_segs_head);
    pip->pipe_count = 2;

    /* pt1: (0,0,0) */
    struct wdb_pipe_pnt *p1;
    BU_ALLOC(p1, struct wdb_pipe_pnt);
    p1->l.magic = WDB_PIPESEG_MAGIC;
    VSET(p1->pp_coord, 0, 0, 0);
    p1->pp_od = 2.0;
    p1->pp_id = 1.0;
    p1->pp_bendradius = 10.0;   /* after 2x scale: od=4, bendradius=20 >= od/2=2 */
    BU_LIST_INSERT(&pip->pipe_segs_head, &p1->l);

    /* pt2: (0,0,10) */
    struct wdb_pipe_pnt *p2;
    BU_ALLOC(p2, struct wdb_pipe_pnt);
    p2->l.magic = WDB_PIPESEG_MAGIC;
    VSET(p2->pp_coord, 0, 0, 10);
    p2->pp_od = 2.0;
    p2->pp_id = 1.0;
    p2->pp_bendradius = 10.0;   /* after 2x scale: od=4, bendradius=20 >= od/2=2 */
    BU_LIST_INSERT(&pip->pipe_segs_head, &p2->l);

    wdb_export(wdbp, objname, (void *)pip, ID_PIPE, 1.0);

    struct directory *dp = db_lookup(wdbp->dbip, objname, LOOKUP_QUIET);
    if (dp == RT_DIR_NULL)
	bu_exit(1, "ERROR: Unable to create pipe object\n");

    return dp;
}

static struct wdb_pipe_pnt *
pipe_first(struct rt_edit *s)
{
    struct rt_pipe_internal *pip =
	(struct rt_pipe_internal *)s->es_int.idb_ptr;
    return BU_LIST_FIRST(wdb_pipe_pnt, &pip->pipe_segs_head);
}

static struct wdb_pipe_pnt *
pipe_second(struct rt_edit *s)
{
    struct wdb_pipe_pnt *first = pipe_first(s);
    return BU_LIST_NEXT(wdb_pipe_pnt, &first->l);
}

static void
pipe_reset(struct rt_edit *s, struct rt_pipe_edit_local *pe)
{
    struct rt_pipe_internal *pip =
	(struct rt_pipe_internal *)s->es_int.idb_ptr;

    struct wdb_pipe_pnt *p1 = BU_LIST_FIRST(wdb_pipe_pnt, &pip->pipe_segs_head);
    struct wdb_pipe_pnt *p2 = BU_LIST_NEXT(wdb_pipe_pnt, &p1->l);

    VSET(p1->pp_coord, 0, 0,  0); p1->pp_od = 2.0; p1->pp_id = 1.0; p1->pp_bendradius = 10.0;
    VSET(p2->pp_coord, 0, 0, 10); p2->pp_od = 2.0; p2->pp_id = 1.0; p2->pp_bendradius = 10.0;

    pe->es_pipe_pnt = NULL;
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

    struct directory *dp = make_pipe(wdbp);

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

    struct rt_pipe_edit_local *pe =
	(struct rt_pipe_edit_local *)s->ipe_ptr;

    vect_t mousevec;

    /* ================================================================
     * RT_PARAMS_EDIT_SCALE (scale=2 about keypoint (0,0,0))
     *
     * rt_pipe_mat with scale=2 (mat[15]=1/2):
     *   p1.coord (0,0,0)→(0,0,0), od/id/bendradius * 2
     *   p2.coord (0,0,10)→(0,0,20), od/id/bendradius * 2
     * ================================================================*/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_SCALE);
    s->e_inpara = 0;
    s->es_scale = 2.0;

    rt_edit_process(s);
    {
	struct wdb_pipe_pnt *p1 = pipe_first(s);
	struct wdb_pipe_pnt *p2 = pipe_second(s);
	point_t exp_c1 = {0, 0, 0}, exp_c2 = {0, 0, 20};
	if (!VNEAR_EQUAL(p1->pp_coord, exp_c1, VUNITIZE_TOL) ||
	    !NEAR_EQUAL(p1->pp_od, 4.0, VUNITIZE_TOL) ||
	    !NEAR_EQUAL(p1->pp_id, 2.0, VUNITIZE_TOL) ||
	    !VNEAR_EQUAL(p2->pp_coord, exp_c2, VUNITIZE_TOL) ||
	    !NEAR_EQUAL(p2->pp_od, 4.0, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_SCALE failed\n"
		    "  p1: coord=%g,%g,%g od=%g id=%g\n"
		    "  p2: coord=%g,%g,%g od=%g id=%g\n",
		    V3ARGS(p1->pp_coord), p1->pp_od, p1->pp_id,
		    V3ARGS(p2->pp_coord), p2->pp_od, p2->pp_id);
	bu_log("RT_PARAMS_EDIT_SCALE SUCCESS: p1.coord=%g,%g,%g od=%g  p2.coord=%g,%g,%g od=%g\n",
	       V3ARGS(p1->pp_coord), p1->pp_od, V3ARGS(p2->pp_coord), p2->pp_od);
    }

    /* ================================================================
     * RT_PARAMS_EDIT_TRANS (translate; keypoint (0,0,0) → e_para)
     *
     * Translation shifts all coords by e_para - keypoint = (5,5,5).
     * od/id/bendradius unchanged (mat[15]=1).
     * p1: (0,0,0)→(5,5,5)
     * p2: (0,0,10)→(5,5,15)
     * ================================================================*/
    pipe_reset(s, pe);
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_TRANS);
    s->e_inpara = 1;
    VSET(s->e_para, 5, 5, 5);
    VSET(s->e_keypoint, 0, 0, 0);

    rt_edit_process(s);
    {
	struct wdb_pipe_pnt *p1 = pipe_first(s);
	struct wdb_pipe_pnt *p2 = pipe_second(s);
	point_t exp_c1 = {5, 5, 5}, exp_c2 = {5, 5, 15};
	if (!VNEAR_EQUAL(p1->pp_coord, exp_c1, VUNITIZE_TOL) ||
	    !NEAR_EQUAL(p1->pp_od, 2.0, VUNITIZE_TOL) ||
	    !VNEAR_EQUAL(p2->pp_coord, exp_c2, VUNITIZE_TOL) ||
	    !NEAR_EQUAL(p2->pp_od, 2.0, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_TRANS failed\n"
		    "  p1: coord=%g,%g,%g od=%g\n"
		    "  p2: coord=%g,%g,%g od=%g\n",
		    V3ARGS(p1->pp_coord), p1->pp_od,
		    V3ARGS(p2->pp_coord), p2->pp_od);
	bu_log("RT_PARAMS_EDIT_TRANS SUCCESS: p1=%g,%g,%g  p2=%g,%g,%g\n",
	       V3ARGS(p1->pp_coord), V3ARGS(p2->pp_coord));
    }

    /* ================================================================
     * RT_PARAMS_EDIT_ROT (rotate about keypoint (0,0,0), angles=5,5,5)
     *
     * p1 (0,0,0) → R*(0,0,0) = (0,0,0)
     * p2 (0,0,10) → R[:,2]*10 = (0.87156..., -0.86824..., 9.92404...)
     * od/id/bendradius unchanged.
     * ================================================================*/
    pipe_reset(s, pe);
    MAT_IDN(s->acc_rot_sol);
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_ROT);
    s->e_inpara = 1;
    VSET(s->e_para, 5, 5, 5);
    s->vp->gv_rotate_about = 'k';
    VSET(s->e_keypoint, 0, 0, 0);

    rt_edit_process(s);
    {
	struct wdb_pipe_pnt *p1 = pipe_first(s);
	struct wdb_pipe_pnt *p2 = pipe_second(s);
	/* p1 stays at origin */
	point_t exp_c1 = {0, 0, 0};
	/* p2: 10 * R[:,2] where R=bn_mat_angles(5,5,5) */
	point_t exp_c2 = {
	    10 * 0.08715574274765817,
	    10 * (-0.08682408883346517),
	    10 * 0.99240387650610407
	};
	if (!VNEAR_EQUAL(p1->pp_coord, exp_c1, VUNITIZE_TOL) ||
	    !VNEAR_EQUAL(p2->pp_coord, exp_c2, VUNITIZE_TOL) ||
	    !NEAR_EQUAL(p1->pp_od, 2.0, VUNITIZE_TOL) ||
	    !NEAR_EQUAL(p2->pp_od, 2.0, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_ROT(k) failed\n"
		    "  p1: coord=%0.10f,%0.10f,%0.10f\n"
		    "  p2: coord=%0.10f,%0.10f,%0.10f\n",
		    V3ARGS(p1->pp_coord), V3ARGS(p2->pp_coord));
	bu_log("RT_PARAMS_EDIT_ROT(k) SUCCESS: p1=%g,%g,%g  p2=%g,%g,%g\n",
	       V3ARGS(p1->pp_coord), V3ARGS(p2->pp_coord));
    }

    /* ================================================================
     * ECMD_PIPE_SCALE_OD (scale entire pipe OD; e_inpara=1, e_para[0]=4)
     *
     * ecmd_pipe_scale_od: es_scale = e_para[0] * e_mat[15] / ps->pp_od
     *                             = 4 * 1 / 2 = 2
     * pipe_scale_od: each point od *= es_scale → 4.0
     * ================================================================*/
    pipe_reset(s, pe);
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_PIPE_SCALE_OD);
    s->e_inpara = 1;
    s->e_para[0] = 4.0;

    rt_edit_process(s);
    {
	struct wdb_pipe_pnt *p1 = pipe_first(s);
	struct wdb_pipe_pnt *p2 = pipe_second(s);
	if (!NEAR_EQUAL(p1->pp_od, 4.0, VUNITIZE_TOL) ||
	    !NEAR_EQUAL(p2->pp_od, 4.0, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: ECMD_PIPE_SCALE_OD failed: p1.od=%g p2.od=%g\n",
		    p1->pp_od, p2->pp_od);
	bu_log("ECMD_PIPE_SCALE_OD SUCCESS: p1.od=%g p2.od=%g\n",
	       p1->pp_od, p2->pp_od);
    }

    /* ================================================================
     * ECMD_PIPE_PT_MOVE (move selected pipe point to (3,4,5))
     *
     * Pre-select second point; e_inpara=3, mv_context=1, e_invmat=IDN.
     * new_pt = MAT4X3PNT(IDN, (3,4,5)) = (3,4,5)
     * p2.pp_coord → (3,4,5)
     * ================================================================*/
    pipe_reset(s, pe);
    pe->es_pipe_pnt = pipe_second(s);
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_PIPE_PT_MOVE);
    s->e_inpara = 3;
    VSET(s->e_para, 3, 4, 5);

    rt_edit_process(s);
    {
	struct wdb_pipe_pnt *p1 = pipe_first(s);
	struct wdb_pipe_pnt *p2 = pipe_second(s);
	point_t exp_p2 = {3, 4, 5};
	point_t orig_p1 = {0, 0, 0};
	if (!VNEAR_EQUAL(p2->pp_coord, exp_p2, VUNITIZE_TOL) ||
	    !VNEAR_EQUAL(p1->pp_coord, orig_p1, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: ECMD_PIPE_PT_MOVE failed\n"
		    "  p1=%g,%g,%g  p2=%g,%g,%g\n",
		    V3ARGS(p1->pp_coord), V3ARGS(p2->pp_coord));
	bu_log("ECMD_PIPE_PT_MOVE SUCCESS: p2=%g,%g,%g (was 0,0,10)\n",
	       V3ARGS(p2->pp_coord));
    }

    /* ================================================================
     * RT_PARAMS_EDIT_TRANS XY: verify coords moved
     * ================================================================*/
    pipe_reset(s, pe);
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_TRANS);
    VMOVE(s->curr_e_axes_pos, pipe_first(s)->pp_coord);
    {
	int xpos = 1482, ypos = 762;
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
	struct wdb_pipe_pnt *p1 = pipe_first(s);
	point_t orig = {0, 0, 0};
	if (VNEAR_EQUAL(p1->pp_coord, orig, SQRT_SMALL_FASTF))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_TRANS(xy) did not translate p1\n");
    }
    bu_log("RT_PARAMS_EDIT_TRANS(xy) SUCCESS: p1=%g,%g,%g (moved from 0,0,0)\n",
	   V3ARGS(pipe_first(s)->pp_coord));

    /* ================================================================
     * RT_PARAMS_EDIT_ROT XY returns BRLCAD_ERROR (unimplemented)
     * ================================================================*/
    pipe_reset(s, pe);
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_ROT);
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
