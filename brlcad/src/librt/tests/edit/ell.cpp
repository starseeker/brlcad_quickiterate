/*                         E L L . C P P
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
/** @file ell.cpp
 *
 * Test editing of ELL primitive parameters.
 */

#include "common.h"

#include "vmath.h"
#include "bu/app.h"
#include "bu/avs.h"
#include "bu/env.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/process.h"
#include "bu/str.h"
#include "raytrace.h"
#include "rt/rt_ecmds.h"


struct directory *
make_ell(struct rt_wdb *wdbp)
{
    const char *objname = "ell";
    struct rt_ell_internal *ell;
    BU_ALLOC(ell, struct rt_ell_internal);
    ell->magic = RT_ELL_INTERNAL_MAGIC;
    VSET(ell->v, 10, 5, 20);
    VSET(ell->a, 4, 0, 0);
    VSET(ell->b, 0, 3, 0);
    VSET(ell->c, 0, 0, 2);

    wdb_export(wdbp, objname, (void *)ell, ID_ELL, 1.0);

    struct directory *dp = db_lookup(wdbp->dbip, objname, LOOKUP_QUIET);
    if (dp == RT_DIR_NULL)
	bu_exit(1, "ERROR: Unable to create ell object: %s\n", objname);

    return dp;
}

int
ell_diff(const char *cmd, struct rt_ell_internal *ctrl, struct rt_ell_internal *e)
{
    int ret = 0;
    if (!ctrl || !e)
	return 1;

    if (!VNEAR_EQUAL(ctrl->v, e->v, VUNITIZE_TOL)) {
	bu_log("ERROR(%s) unexpected change - ell parameter v %0.17f,%0.17f,%0.17f -> %0.17f,%0.17f,%0.17f\n", cmd, V3ARGS(ctrl->v), V3ARGS(e->v));
	ret = 1;
    }
    if (!VNEAR_EQUAL(ctrl->a, e->a, VUNITIZE_TOL)) {
	bu_log("ERROR(%s) unexpected change - ell parameter a %0.17f,%0.17f,%0.17f -> %0.17f,%0.17f,%0.17f\n", cmd, V3ARGS(ctrl->a), V3ARGS(e->a));
	ret = 1;
    }
    if (!VNEAR_EQUAL(ctrl->b, e->b, VUNITIZE_TOL)) {
	bu_log("ERROR(%s) unexpected change - ell parameter b %0.17f,%0.17f,%0.17f -> %0.17f,%0.17f,%0.17f\n", cmd, V3ARGS(ctrl->b), V3ARGS(e->b));
	ret = 1;
    }
    if (!VNEAR_EQUAL(ctrl->c, e->c, VUNITIZE_TOL)) {
	bu_log("ERROR(%s) unexpected change - ell parameter c %0.17f,%0.17f,%0.17f -> %0.17f,%0.17f,%0.17f\n", cmd, V3ARGS(ctrl->c), V3ARGS(e->c));
	ret = 1;
    }

    return ret;
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

    struct directory *dp = make_ell(wdbp);

    // We'll refer to the original for reference
    struct rt_db_internal intern;
    rt_db_get_internal(&intern, dp, dbip, NULL, &rt_uniresource);
    struct rt_ell_internal *orig_ell = (struct rt_ell_internal *)intern.idb_ptr;
    struct rt_db_internal cmpintern;
    rt_db_get_internal(&cmpintern, dp, dbip, NULL, &rt_uniresource);
    struct rt_ell_internal *cmp_ell = (struct rt_ell_internal *)cmpintern.idb_ptr;

    struct bn_tol tol = BN_TOL_INIT_TOL;
    struct db_full_path fp;
    db_full_path_init(&fp);
    db_add_node_to_full_path(&fp, dp);
    struct bview *v;
    BU_GET(v, struct bview);
    bv_init(v, NULL);
    VSET(v->gv_aet, 45, 35, 0);
    bv_mat_aet(v);
    v->gv_size = 73.3197;
    v->gv_isize = 1.0 / v->gv_size;
    v->gv_scale = 0.5 * v->gv_size;
    bv_update(v);
    bu_vls_sprintf(&v->gv_name, "default");
    v->gv_width = 512;
    v->gv_height = 512;

    // Set up rt_edit container
    struct rt_edit *s = rt_edit_create(&fp, dbip, &tol, v);

    // MGED normally has this set, but the user can explicitly disable
    // it.  For most of our testing, have it on.
    s->mv_context = 1;

    // We'll want to directly check and reset the working ellipsoid
    struct rt_ell_internal *edit_ell = (struct rt_ell_internal *)s->es_int.idb_ptr;


    // Now, begin testing modifications.

    /******************
      ECMD_ELL_SCALE_A
     ******************/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_ELL_SCALE_A);

    // Directly set the scale parameter
    s->e_inpara = 0;
    s->es_scale = 2.0;

    // set cmp val to expected: a_new = a * es_scale
    VSCALE(cmp_ell->a, orig_ell->a, s->es_scale);

    rt_edit_process(s);

    if (ell_diff("ECMD_ELL_SCALE_A", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: ECMD_ELL_SCALE_A failed scaling ell parameter a\n");

    bu_log("ECMD_ELL_SCALE_A SUCCESS: original a value %g,%g,%g modified via es_scale to %g,%g,%g\n", V3ARGS(orig_ell->a), V3ARGS(edit_ell->a));

    // Test e_inpara mode - set a to magnitude 4 (restoring original)
    s->e_inpara = 1;
    s->e_para[0] = 4;   /* target magnitude in local units */

    // expected: es_scale = e_para[0] / MAGNITUDE(a_current) = 4 / 8 = 0.5
    // a_new = a_current * 0.5 = orig_ell->a
    VMOVE(cmp_ell->a, orig_ell->a);

    rt_edit_process(s);

    if (ell_diff("ECMD_ELL_SCALE_A", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: ECMD_ELL_SCALE_A failed restoring ell parameter a\n");

    bu_log("ECMD_ELL_SCALE_A SUCCESS: a value restored via e_para to %g,%g,%g\n", V3ARGS(edit_ell->a));

    /******************
      ECMD_ELL_SCALE_B
     ******************/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_ELL_SCALE_B);

    s->e_inpara = 0;
    s->es_scale = 2.0;

    VSCALE(cmp_ell->b, orig_ell->b, s->es_scale);

    rt_edit_process(s);

    if (ell_diff("ECMD_ELL_SCALE_B", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: ECMD_ELL_SCALE_B failed scaling ell parameter b\n");

    bu_log("ECMD_ELL_SCALE_B SUCCESS: original b value %g,%g,%g modified via es_scale to %g,%g,%g\n", V3ARGS(orig_ell->b), V3ARGS(edit_ell->b));

    // Restore via e_inpara
    s->e_inpara = 1;
    s->e_para[0] = 3;   /* target magnitude = original MAGNITUDE(b) = 3 */

    VMOVE(cmp_ell->b, orig_ell->b);

    rt_edit_process(s);

    if (ell_diff("ECMD_ELL_SCALE_B", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: ECMD_ELL_SCALE_B failed restoring ell parameter b\n");

    bu_log("ECMD_ELL_SCALE_B SUCCESS: b value restored via e_para to %g,%g,%g\n", V3ARGS(edit_ell->b));

    /******************
      ECMD_ELL_SCALE_C
     ******************/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_ELL_SCALE_C);

    s->e_inpara = 0;
    s->es_scale = 2.0;

    VSCALE(cmp_ell->c, orig_ell->c, s->es_scale);

    rt_edit_process(s);

    if (ell_diff("ECMD_ELL_SCALE_C", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: ECMD_ELL_SCALE_C failed scaling ell parameter c\n");

    bu_log("ECMD_ELL_SCALE_C SUCCESS: original c value %g,%g,%g modified via es_scale to %g,%g,%g\n", V3ARGS(orig_ell->c), V3ARGS(edit_ell->c));

    // Restore via e_inpara
    s->e_inpara = 1;
    s->e_para[0] = 2;   /* target magnitude = original MAGNITUDE(c) = 2 */

    VMOVE(cmp_ell->c, orig_ell->c);

    rt_edit_process(s);

    if (ell_diff("ECMD_ELL_SCALE_C", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: ECMD_ELL_SCALE_C failed restoring ell parameter c\n");

    bu_log("ECMD_ELL_SCALE_C SUCCESS: c value restored via e_para to %g,%g,%g\n", V3ARGS(edit_ell->c));

    /*********************
      ECMD_ELL_SCALE_ABC
     *********************/
    // Reset all to known state
    VMOVE(edit_ell->v, orig_ell->v);
    VMOVE(edit_ell->a, orig_ell->a);
    VMOVE(edit_ell->b, orig_ell->b);
    VMOVE(edit_ell->c, orig_ell->c);
    VMOVE(cmp_ell->v, orig_ell->v);

    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_ELL_SCALE_ABC);

    s->e_inpara = 0;
    s->es_scale = 2.0;

    // a_new = a * 2 = (8,0,0), ma_new = 8
    // b_new = b * (8/3) = (0,8,0), c_new = c * (8/2) = (0,0,8)
    {
	vect_t a_new, b_new, c_new;
	VSCALE(a_new, orig_ell->a, s->es_scale);
	fastf_t ma = MAGNITUDE(a_new);
	fastf_t mb = MAGNITUDE(orig_ell->b);
	VSCALE(b_new, orig_ell->b, ma/mb);
	mb = MAGNITUDE(orig_ell->c);
	VSCALE(c_new, orig_ell->c, ma/mb);
	VMOVE(cmp_ell->a, a_new);
	VMOVE(cmp_ell->b, b_new);
	VMOVE(cmp_ell->c, c_new);
    }

    rt_edit_process(s);

    if (ell_diff("ECMD_ELL_SCALE_ABC", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: ECMD_ELL_SCALE_ABC failed scaling ell parameters a,b,c\n");

    bu_log("ECMD_ELL_SCALE_ABC SUCCESS: a=%g,%g,%g b=%g,%g,%g c=%g,%g,%g\n",
	   V3ARGS(edit_ell->a), V3ARGS(edit_ell->b), V3ARGS(edit_ell->c));

    // Test e_inpara mode: set a to magnitude 4
    s->e_inpara = 1;
    s->e_para[0] = 4;

    // current a=(8,0,0), ma=8; es_scale = 4/8 = 0.5
    // a_new = (8,0,0)*0.5 = (4,0,0), ma_new = 4
    // current b=(0,8,0), mb=8; b_new = (0,8,0)*(4/8) = (0,4,0)
    // current c=(0,0,8), mb=8; c_new = (0,0,8)*(4/8) = (0,0,4)
    VSET(cmp_ell->a, 4, 0, 0);
    VSET(cmp_ell->b, 0, 4, 0);
    VSET(cmp_ell->c, 0, 0, 4);

    rt_edit_process(s);

    if (ell_diff("ECMD_ELL_SCALE_ABC", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: ECMD_ELL_SCALE_ABC failed resizing ell parameters a,b,c\n");

    bu_log("ECMD_ELL_SCALE_ABC SUCCESS: a=%g,%g,%g b=%g,%g,%g c=%g,%g,%g via e_para\n",
	   V3ARGS(edit_ell->a), V3ARGS(edit_ell->b), V3ARGS(edit_ell->c));

    /**********************
       RT_PARAMS_EDIT_SCALE
     **********************/
    // Reset to original
    VMOVE(edit_ell->v, orig_ell->v);
    VMOVE(edit_ell->a, orig_ell->a);
    VMOVE(edit_ell->b, orig_ell->b);
    VMOVE(edit_ell->c, orig_ell->c);
    VMOVE(cmp_ell->v, orig_ell->v);
    VMOVE(cmp_ell->a, orig_ell->a);
    VMOVE(cmp_ell->b, orig_ell->b);
    VMOVE(cmp_ell->c, orig_ell->c);

    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_SCALE);

    s->e_inpara = 0;
    s->es_scale = 3.0;

    // Uniform scale about keypoint (=v): v stays, a,b,c scale by 3
    VMOVE(cmp_ell->v, orig_ell->v);
    VSCALE(cmp_ell->a, orig_ell->a, s->es_scale);
    VSCALE(cmp_ell->b, orig_ell->b, s->es_scale);
    VSCALE(cmp_ell->c, orig_ell->c, s->es_scale);

    rt_edit_process(s);

    if (ell_diff("RT_PARAMS_EDIT_SCALE", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: RT_PARAMS_EDIT_SCALE failed scaling ell\n");

    bu_log("RT_PARAMS_EDIT_SCALE SUCCESS: a=%g,%g,%g b=%g,%g,%g c=%g,%g,%g\n",
	   V3ARGS(edit_ell->a), V3ARGS(edit_ell->b), V3ARGS(edit_ell->c));

    /**********************
       RT_PARAMS_EDIT_TRANS
     **********************/
    // Reset to original
    VMOVE(edit_ell->v, orig_ell->v);
    VMOVE(edit_ell->a, orig_ell->a);
    VMOVE(edit_ell->b, orig_ell->b);
    VMOVE(edit_ell->c, orig_ell->c);
    VMOVE(cmp_ell->v, orig_ell->v);
    VMOVE(cmp_ell->a, orig_ell->a);
    VMOVE(cmp_ell->b, orig_ell->b);
    VMOVE(cmp_ell->c, orig_ell->c);
    s->es_scale = 1.0;
    s->e_inpara = 0;
    VSETALL(s->e_para, 0);

    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_TRANS);

    // Set translation values (mv_context=1: move v to e_para position)
    s->e_inpara = 1;
    VSET(s->e_para, 20, 55, 40);

    // expected: v moves to e_para, a,b,c unchanged
    VMOVE(cmp_ell->v, s->e_para);
    VMOVE(cmp_ell->a, orig_ell->a);
    VMOVE(cmp_ell->b, orig_ell->b);
    VMOVE(cmp_ell->c, orig_ell->c);

    rt_edit_process(s);

    if (ell_diff("RT_PARAMS_EDIT_TRANS", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: RT_PARAMS_EDIT_TRANS failed translating ell\n");

    bu_log("RT_PARAMS_EDIT_TRANS SUCCESS: original v value %g,%g,%g modified to %g,%g,%g\n", V3ARGS(orig_ell->v), V3ARGS(edit_ell->v));

    /****************************
       ECMD_ELL_SCALE_A - XY
     ****************************/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_ELL_SCALE_A);

    // Reset to original
    VMOVE(edit_ell->v, orig_ell->v);
    VMOVE(edit_ell->a, orig_ell->a);
    VMOVE(edit_ell->b, orig_ell->b);
    VMOVE(edit_ell->c, orig_ell->c);
    MAT_IDN(s->acc_rot_sol);
    s->acc_sc_sol = 1.0;
    s->e_inpara = 0;
    s->es_scale = 0;
    s->mv_context = 1;

    // Use the same mouse position as the tor R1 XY test
    int xpos = 1372;
    int ypos = 1383;
    vect_t mousevec;
    mousevec[X] = xpos * INV_BV;
    mousevec[Y] = ypos * INV_BV;
    mousevec[Z] = 0;

    bu_vls_trunc(s->log_str, 0);
    if ((*EDOBJ[dp->d_minor_type].ft_edit_xy)(s, mousevec) == BRLCAD_ERROR)
	bu_exit(1, "ERROR: ECMD_ELL_SCALE_A (xy) failed ft_edit_xy call: %s\n", bu_vls_cstr(s->log_str));

    // es_scale = 1.0 + 0.25 * mousevec[Y] = 1.0 + 0.25 * (1383 * INV_BV)
    // = 1.0 + 0.25 * 0.67529296875 = 1.168823242187500
    // a_new = a * es_scale = (4,0,0) * 1.168823242187500
    VSET(cmp_ell->v, orig_ell->v[X], orig_ell->v[Y], orig_ell->v[Z]);
    VMOVE(cmp_ell->b, orig_ell->b);
    VMOVE(cmp_ell->c, orig_ell->c);
    VSET(cmp_ell->a, 4.675292968750000, 0, 0);

    rt_edit_process(s);

    if (ell_diff("ECMD_ELL_SCALE_A (xy)", cmp_ell, edit_ell))
	bu_exit(1, "ERROR: ECMD_ELL_SCALE_A(xy) failed scaling ell a param\n");

    bu_log("ECMD_ELL_SCALE_A(xy) SUCCESS: original a value %g,%g,%g modified to %g,%g,%g\n", V3ARGS(orig_ell->a), V3ARGS(edit_ell->a));


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
