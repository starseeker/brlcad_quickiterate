/*                       S K E T C H . C P P
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
/** @file sketch.cpp
 *
 * Test editing of SKETCH (2-D sketch) primitive parameters via
 * the new edsketch.c ECMD suite.
 *
 * Test sketch layout (4 vertices, 1 line segment):
 *
 *   verts[0] = (0, 0)
 *   verts[1] = (10, 0)
 *   verts[2] = (10, 10)
 *   verts[3] = (0, 10)
 *   segments: line 0→1
 *
 * V = (0,0,0), u_vec = (1,0,0), v_vec = (0,1,0)
 */

#include "common.h"

#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "raytrace.h"
#include "rt/geom.h"
#include "rt/primitives/sketch.h"
#include "rt/rt_ecmds.h"

/* ------------------------------------------------------------------ */
/* ECMD constants (must match edsketch.c; they are file-local there)  */
/* ------------------------------------------------------------------ */

#define ECMD_SKETCH_PICK_VERTEX    26001
#define ECMD_SKETCH_MOVE_VERTEX    26002
#define ECMD_SKETCH_PICK_SEGMENT   26003
#define ECMD_SKETCH_MOVE_SEGMENT   26004
#define ECMD_SKETCH_APPEND_LINE    26005
#define ECMD_SKETCH_APPEND_ARC     26006
#define ECMD_SKETCH_APPEND_BEZIER  26007
#define ECMD_SKETCH_DELETE_VERTEX  26008
#define ECMD_SKETCH_DELETE_SEGMENT 26009


/* ------------------------------------------------------------------ */
/* Build the test sketch                                               */
/* ------------------------------------------------------------------ */

static struct directory *
make_test_sketch(struct rt_wdb *wdbp)
{
    struct rt_sketch_internal *skt;
    BU_ALLOC(skt, struct rt_sketch_internal);
    skt->magic = RT_SKETCH_INTERNAL_MAGIC;
    VSET(skt->V,     0, 0, 0);
    VSET(skt->u_vec, 1, 0, 0);
    VSET(skt->v_vec, 0, 1, 0);

    skt->vert_count = 4;
    skt->verts = (point2d_t *)bu_malloc(4 * sizeof(point2d_t), "sketch verts");
    V2SET(skt->verts[0],  0,  0);
    V2SET(skt->verts[1], 10,  0);
    V2SET(skt->verts[2], 10, 10);
    V2SET(skt->verts[3],  0, 10);

    /* One line segment: vert 0 → vert 1 */
    struct line_seg *ls;
    BU_ALLOC(ls, struct line_seg);
    ls->magic = CURVE_LSEG_MAGIC;
    ls->start = 0;
    ls->end   = 1;

    skt->curve.count   = 1;
    skt->curve.segment = (void **)bu_malloc(sizeof(void *), "skt segs");
    skt->curve.reverse = (int  *)bu_malloc(sizeof(int),     "skt reverse");
    skt->curve.segment[0] = (void *)ls;
    skt->curve.reverse[0] = 0;

    wdb_export(wdbp, "test_sketch", (void *)skt, ID_SKETCH, 1.0);

    struct directory *dp = db_lookup(wdbp->dbip, "test_sketch", LOOKUP_QUIET);
    if (!dp)
	bu_exit(1, "ERROR: failed to look up test_sketch\n");
    return dp;
}


/* ------------------------------------------------------------------ */
/* Main test                                                           */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);
    if (argc != 1)
	return BRLCAD_ERROR;

    struct db_i *dbip = db_open_inmem();
    if (dbip == DBI_NULL)
	bu_exit(1, "ERROR: Unable to create in-memory database\n");

    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);
    struct directory *dp = make_test_sketch(wdbp);

    struct bn_tol tol = BN_TOL_INIT_TOL;
    struct db_full_path fp;
    db_full_path_init(&fp);
    db_add_node_to_full_path(&fp, dp);

    struct bview *v;
    BU_GET(v, struct bview);
    bv_init(v, NULL);
    VSET(v->gv_aet, 45, 35, 0);
    bv_mat_aet(v);
    v->gv_size = 100.0;
    v->gv_isize = 1.0 / v->gv_size;
    v->gv_scale = 50.0;
    bv_update(v);
    bu_vls_sprintf(&v->gv_name, "default");
    v->gv_width  = 512;
    v->gv_height = 512;

    struct rt_edit *s = rt_edit_create(&fp, dbip, &tol, v);
    s->mv_context = 1;
    s->local2base = 1.0;

    struct rt_sketch_internal *skt =
	(struct rt_sketch_internal *)s->es_int.idb_ptr;
    struct rt_sketch_edit *se = (struct rt_sketch_edit *)s->ipe_ptr;

    /* ================================================================
     * Initial state: ipe_ptr initialized with curr_vert/curr_seg = -1
     * ================================================================*/
    if (!se)
	bu_exit(1, "ERROR: ipe_ptr not allocated\n");
    if (se->curr_vert != -1 || se->curr_seg != -1)
	bu_exit(1, "ERROR: initial state wrong (expected -1,-1)\n");
    bu_log("SKETCH initial state SUCCESS: curr_vert=%d curr_seg=%d\n",
	   se->curr_vert, se->curr_seg);

    /* ================================================================
     * ECMD_SKETCH_PICK_VERTEX  (select vertex 2)
     * ================================================================*/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_SKETCH_PICK_VERTEX);
    s->e_inpara = 1;
    s->e_para[0] = 2.0;

    rt_edit_process(s);
    if (se->curr_vert != 2)
	bu_exit(1, "ERROR: ECMD_SKETCH_PICK_VERTEX: expected curr_vert=2, got %d\n",
		se->curr_vert);
    bu_log("ECMD_SKETCH_PICK_VERTEX SUCCESS: curr_vert=%d\n", se->curr_vert);

    /* ================================================================
     * ECMD_SKETCH_MOVE_VERTEX  (move vertex 2 to (20,20))
     * ================================================================*/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_SKETCH_MOVE_VERTEX);
    s->e_inpara = 2;
    s->e_para[0] = 20.0;
    s->e_para[1] = 20.0;

    rt_edit_process(s);
    if (!NEAR_EQUAL(skt->verts[2][0], 20.0, VUNITIZE_TOL) ||
	!NEAR_EQUAL(skt->verts[2][1], 20.0, VUNITIZE_TOL))
	bu_exit(1, "ERROR: ECMD_SKETCH_MOVE_VERTEX: expected (20,20), got (%g,%g)\n",
		skt->verts[2][0], skt->verts[2][1]);
    bu_log("ECMD_SKETCH_MOVE_VERTEX SUCCESS: verts[2]=(%g,%g)\n",
	   skt->verts[2][0], skt->verts[2][1]);

    /* Restore verts[2] */
    V2SET(skt->verts[2], 10, 10);

    /* ================================================================
     * ECMD_SKETCH_PICK_SEGMENT  (select segment 0)
     * ================================================================*/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_SKETCH_PICK_SEGMENT);
    s->e_inpara = 1;
    s->e_para[0] = 0.0;

    rt_edit_process(s);
    if (se->curr_seg != 0)
	bu_exit(1, "ERROR: ECMD_SKETCH_PICK_SEGMENT: expected curr_seg=0, got %d\n",
		se->curr_seg);
    bu_log("ECMD_SKETCH_PICK_SEGMENT SUCCESS: curr_seg=%d\n", se->curr_seg);

    /* ================================================================
     * ECMD_SKETCH_MOVE_SEGMENT  (translate line seg 0 by (+5, +5))
     * Segment 0 is line_seg with start=0 (0,0) and end=1 (10,0).
     * After move: verts[0]=(5,5), verts[1]=(15,5).
     * ================================================================*/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_SKETCH_MOVE_SEGMENT);
    s->e_inpara = 2;
    s->e_para[0] = 5.0;
    s->e_para[1] = 5.0;

    rt_edit_process(s);
    if (!NEAR_EQUAL(skt->verts[0][0], 5.0,  VUNITIZE_TOL) ||
	!NEAR_EQUAL(skt->verts[0][1], 5.0,  VUNITIZE_TOL) ||
	!NEAR_EQUAL(skt->verts[1][0], 15.0, VUNITIZE_TOL) ||
	!NEAR_EQUAL(skt->verts[1][1], 5.0,  VUNITIZE_TOL))
	bu_exit(1,
		"ERROR: ECMD_SKETCH_MOVE_SEGMENT: expected verts[0]=(5,5) verts[1]=(15,5), "
		"got (%g,%g) (%g,%g)\n",
		skt->verts[0][0], skt->verts[0][1],
		skt->verts[1][0], skt->verts[1][1]);
    bu_log("ECMD_SKETCH_MOVE_SEGMENT SUCCESS: verts[0]=(%g,%g) verts[1]=(%g,%g)\n",
	   skt->verts[0][0], skt->verts[0][1],
	   skt->verts[1][0], skt->verts[1][1]);

    /* Restore verts */
    V2SET(skt->verts[0],  0,  0);
    V2SET(skt->verts[1], 10,  0);

    /* ================================================================
     * ECMD_SKETCH_APPEND_LINE  (add line 2→3; curve should now have 2 segs)
     * ================================================================*/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_SKETCH_APPEND_LINE);
    s->e_inpara = 2;
    s->e_para[0] = 2.0;
    s->e_para[1] = 3.0;

    rt_edit_process(s);
    if (skt->curve.count != 2)
	bu_exit(1, "ERROR: ECMD_SKETCH_APPEND_LINE: expected 2 segments, got %zu\n",
		skt->curve.count);
    {
	struct line_seg *ls = (struct line_seg *)skt->curve.segment[1];
	if (!ls || ls->magic != CURVE_LSEG_MAGIC || ls->start != 2 || ls->end != 3)
	    bu_exit(1, "ERROR: ECMD_SKETCH_APPEND_LINE: new segment wrong\n");
    }
    bu_log("ECMD_SKETCH_APPEND_LINE SUCCESS: curve now has %zu segments\n",
	   skt->curve.count);

    /* ================================================================
     * ECMD_SKETCH_APPEND_ARC  (add arc from vert 1 to vert 2, r=8)
     * e_inpara=3: [0]=start [1]=end [2]=radius
     * ================================================================*/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_SKETCH_APPEND_ARC);
    s->e_inpara = 3;
    s->e_para[0] = 1.0;  /* start vert */
    s->e_para[1] = 2.0;  /* end vert */
    s->e_para[2] = 8.0;  /* radius */

    rt_edit_process(s);
    if (skt->curve.count != 3)
	bu_exit(1, "ERROR: ECMD_SKETCH_APPEND_ARC: expected 3 segments, got %zu\n",
		skt->curve.count);
    {
	struct carc_seg *cs = (struct carc_seg *)skt->curve.segment[2];
	if (!cs || cs->magic != CURVE_CARC_MAGIC || cs->start != 1 || cs->end != 2 ||
	    !NEAR_EQUAL(cs->radius, 8.0, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: ECMD_SKETCH_APPEND_ARC: new segment wrong\n");
    }
    bu_log("ECMD_SKETCH_APPEND_ARC SUCCESS: curve now has %zu segments\n",
	   skt->curve.count);

    /* ================================================================
     * ECMD_SKETCH_APPEND_BEZIER  (quadratic Bezier: verts 0, 1, 2)
     * e_inpara=3 → degree=2; e_para[0..2] are control point indices
     * ================================================================*/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_SKETCH_APPEND_BEZIER);
    s->e_inpara = 3;
    s->e_para[0] = 0.0;  /* cp 0 */
    s->e_para[1] = 1.0;  /* cp 1 */
    s->e_para[2] = 2.0;  /* cp 2 */

    rt_edit_process(s);
    if (skt->curve.count != 4)
	bu_exit(1, "ERROR: ECMD_SKETCH_APPEND_BEZIER: expected 4 segments, got %zu\n",
		skt->curve.count);
    {
	struct bezier_seg *bs = (struct bezier_seg *)skt->curve.segment[3];
	if (!bs || bs->magic != CURVE_BEZIER_MAGIC || bs->degree != 2 ||
	    bs->ctl_points[0] != 0 || bs->ctl_points[1] != 1 || bs->ctl_points[2] != 2)
	    bu_exit(1, "ERROR: ECMD_SKETCH_APPEND_BEZIER: new segment wrong\n");
    }
    bu_log("ECMD_SKETCH_APPEND_BEZIER SUCCESS: curve now has %zu segments\n",
	   skt->curve.count);

    /* ================================================================
     * ECMD_SKETCH_DELETE_VERTEX (should fail: vertex 0 is in use)
     * Verify by checking that vert_count is unchanged after the attempt.
     * ================================================================*/
    se->curr_vert = 0;
    {
	size_t vc_before = skt->vert_count;
	EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_SKETCH_DELETE_VERTEX);
	rt_edit_process(s);
	if (skt->vert_count != vc_before)
	    bu_exit(1, "ERROR: ECMD_SKETCH_DELETE_VERTEX incorrectly deleted in-use vertex\n");
    }
    bu_log("ECMD_SKETCH_DELETE_VERTEX (in-use vertex) correctly refused\n");
    se->curr_vert = -1;

    /* ================================================================
     * ECMD_SKETCH_DELETE_SEGMENT (delete the last segment, index 3)
     * After: curve.count should be 3
     * ================================================================*/
    se->curr_seg = 3;
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_SKETCH_DELETE_SEGMENT);
    s->e_inpara = 0;

    rt_edit_process(s);
    if (skt->curve.count != 3)
	bu_exit(1, "ERROR: ECMD_SKETCH_DELETE_SEGMENT: expected 3 segments, got %zu\n",
		skt->curve.count);
    if (se->curr_seg != -1)
	bu_exit(1, "ERROR: ECMD_SKETCH_DELETE_SEGMENT: curr_seg not reset to -1\n");
    bu_log("ECMD_SKETCH_DELETE_SEGMENT SUCCESS: curve now has %zu segments\n",
	   skt->curve.count);

    /* ================================================================
     * RT_PARAMS_EDIT_SCALE: uniform scale about keypoint
     * ================================================================*/
    rt_edit_set_edflag(s, RT_PARAMS_EDIT_SCALE);
    s->e_inpara = 0;
    s->es_scale = 2.0;

    rt_edit_process(s);
    /* V should have been scaled - just verify it ran without crash */
    bu_log("RT_PARAMS_EDIT_SCALE SUCCESS\n");

    /* ================================================================
     * RT_MATRIX_EDIT_ROT: matrix rotation
     * ================================================================*/
    rt_edit_set_edflag(s, RT_MATRIX_EDIT_ROT);
    MAT_IDN(s->model_changes);
    MAT_IDN(s->acc_rot_sol);
    s->e_inpara = 1;
    VSET(s->e_para, 30, 0, 0);
    VSET(s->e_keypoint, 0, 0, 0);

    rt_edit_process(s);
    {
	mat_t ident;
	MAT_IDN(ident);
	if (bn_mat_is_equal(s->model_changes, ident, &tol))
	    bu_exit(1, "ERROR: RT_MATRIX_EDIT_ROT did not rotate model_changes\n");
	bu_log("RT_MATRIX_EDIT_ROT SUCCESS\n");
    }

    /* ================================================================
     * RT_MATRIX_EDIT_TRANS_MODEL_XYZ: absolute translation
     * ================================================================*/
    rt_edit_set_edflag(s, RT_MATRIX_EDIT_TRANS_MODEL_XYZ);
    MAT_IDN(s->model_changes);
    s->e_inpara = 1;
    VSET(s->e_para, 10, 20, 30);
    VSET(s->e_keypoint, 0, 0, 0);
    s->local2base = 1.0;

    rt_edit_process(s);
    {
	point_t kp_world;
	MAT4X3PNT(kp_world, s->model_changes, s->e_keypoint);
	vect_t expected = {10, 20, 30};
	if (!VNEAR_EQUAL(kp_world, expected, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_MATRIX_EDIT_TRANS_MODEL_XYZ failed\n");
	bu_log("RT_MATRIX_EDIT_TRANS_MODEL_XYZ SUCCESS: keypoint maps to (%g,%g,%g)\n",
	       V3ARGS(kp_world));
    }

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
