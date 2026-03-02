/*                          B O T . C P P
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
/** @file bot.cpp
 *
 * Test editing of BOT (bag of triangles) primitive parameters.
 *
 * Reference BOT: a simple tetrahedron with 4 vertices and 4 faces.
 * Vertices:
 *   v[0] = (0, 0, 0)
 *   v[1] = (1, 0, 0)
 *   v[2] = (0, 1, 0)
 *   v[3] = (0, 0, 1)
 * Faces: {0,1,2}, {0,1,3}, {0,2,3}, {1,2,3}
 * Keypoint = v[0] = (0,0,0).
 *
 * rt_bot_mat: applies MAT4X3PNT to each vertex.
 *
 * RT_PARAMS_EDIT_SCALE (scale=2, keypoint=v[0]=(0,0,0)):
 *   All vertices scale by 2: v[1]=(1,0,0)→(2,0,0), etc.
 *
 * RT_PARAMS_EDIT_TRANS (e_para=(5,5,5)):
 *   All vertices shift by (5,5,5): v[0]→(5,5,5), v[3]→(5,5,6).
 *
 * RT_PARAMS_EDIT_ROT (e_para=(5,5,5), keypoint=v[0]=(0,0,0)):
 *   v[0] stays at origin (it IS the keypoint). v[1] rotates.
 *
 * ECMD_BOT_MOVEV (move vertex 2 to (3,3,3)):
 *   b->bot_verts[0]=2, e_inpara=3, e_para=(3,3,3).
 *   v[2] → (3,3,3).
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
#include "rt/primitives/bot.h"


/* ECMD constants from edbot.c */
#define ECMD_BOT_PICKV		30061
#define ECMD_BOT_PICKE		30062
#define ECMD_BOT_PICKT		30063
#define ECMD_BOT_MOVEV		30064
#define ECMD_BOT_MOVEE		30065
#define ECMD_BOT_MOVET		30066
#define ECMD_BOT_MODE		30067
#define ECMD_BOT_ORIENT		30068
#define ECMD_BOT_THICK		30069
#define ECMD_BOT_FMODE		30070
#define ECMD_BOT_FDEL		30071
#define ECMD_BOT_FLAGS		30072


struct directory *
make_bot(struct rt_wdb *wdbp)
{
    const char *objname = "bot";

    struct rt_bot_internal *bot;
    BU_ALLOC(bot, struct rt_bot_internal);
    bot->magic       = RT_BOT_INTERNAL_MAGIC;
    bot->mode        = RT_BOT_SOLID;
    bot->orientation = RT_BOT_CCW;
    bot->bot_flags   = 0;

    /* Tetrahedron: 4 vertices, 4 faces */
    bot->num_vertices = 4;
    bot->vertices = (fastf_t *)bu_malloc(4 * 3 * sizeof(fastf_t), "bot vertices");
    VSET(&bot->vertices[0], 0, 0, 0);
    VSET(&bot->vertices[3], 1, 0, 0);
    VSET(&bot->vertices[6], 0, 1, 0);
    VSET(&bot->vertices[9], 0, 0, 1);

    bot->num_faces = 4;
    bot->faces = (int *)bu_malloc(4 * 3 * sizeof(int), "bot faces");
    bot->faces[0] = 0; bot->faces[1] = 1; bot->faces[2] = 2;
    bot->faces[3] = 0; bot->faces[4] = 1; bot->faces[5] = 3;
    bot->faces[6] = 0; bot->faces[7] = 2; bot->faces[8] = 3;
    bot->faces[9] = 1; bot->faces[10] = 2; bot->faces[11] = 3;

    bot->thickness   = NULL;
    bot->face_mode   = NULL;
    bot->num_normals = 0;
    bot->normals     = NULL;
    bot->num_face_normals = 0;
    bot->face_normals = NULL;
    bot->num_uvs     = 0;
    bot->uvs         = NULL;

    wdb_export(wdbp, objname, (void *)bot, ID_BOT, 1.0);

    struct directory *dp = db_lookup(wdbp->dbip, objname, LOOKUP_QUIET);
    if (dp == RT_DIR_NULL)
	bu_exit(1, "ERROR: Unable to create bot object\n");

    return dp;
}

static void
bot_reset(struct rt_edit *s, struct rt_bot_internal *bot, struct rt_bot_edit *b)
{
    VSET(&bot->vertices[0], 0, 0, 0);
    VSET(&bot->vertices[3], 1, 0, 0);
    VSET(&bot->vertices[6], 0, 1, 0);
    VSET(&bot->vertices[9], 0, 0, 1);

    b->bot_verts[0] = -1;
    b->bot_verts[1] = -1;
    b->bot_verts[2] = -1;

    VSETALL(s->e_keypoint, 0.0);
    MAT_IDN(s->acc_rot_sol);
    MAT_IDN(s->incr_change);
    s->acc_sc_sol = 1.0;
    s->e_inpara   = 0;
    s->es_scale   = 0.0;
    s->mv_context = 1;
    s->e_mvalid   = 0;
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

    struct directory *dp = make_bot(wdbp);

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

    struct rt_bot_internal *bot =
	(struct rt_bot_internal *)s->es_int.idb_ptr;
    struct rt_bot_edit *b = (struct rt_bot_edit *)s->ipe_ptr;

    vect_t mousevec;

    /* ================================================================
     * RT_PARAMS_EDIT_SCALE (scale=2 about keypoint v[0]=(0,0,0))
     *
     * rt_bot_mat with MAT4X3PNT: v[i] *= 2 for all vertices.
     *   v[0]=(0,0,0)→(0,0,0), v[3]=(0,0,1)→(0,0,2)
     * ================================================================*/
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_SCALE);
    s->e_inpara = 0;
    s->es_scale = 2.0;
    VSET(s->e_keypoint, 0, 0, 0);
    b->bot_verts[0] = -1;
    b->bot_verts[1] = -1;
    b->bot_verts[2] = -1;

    rt_edit_process(s);
    {
	vect_t v0 = {0, 0, 0}, v3 = {0, 0, 2};
	if (!VNEAR_EQUAL(&bot->vertices[0], v0, VUNITIZE_TOL) ||
	    !VNEAR_EQUAL(&bot->vertices[9], v3, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_SCALE failed\n"
		    "  v[0]=%g,%g,%g  v[3]=%g,%g,%g\n",
		    V3ARGS(&bot->vertices[0]), V3ARGS(&bot->vertices[9]));
	bu_log("RT_PARAMS_EDIT_SCALE SUCCESS: v[0]=%g,%g,%g v[3]=%g,%g,%g\n",
	       V3ARGS(&bot->vertices[0]), V3ARGS(&bot->vertices[9]));
    }

    /* ================================================================
     * RT_PARAMS_EDIT_TRANS (translate; keypoint (0,0,0) → e_para=(5,5,5))
     *
     * All vertices shift by (5,5,5).
     * v[0]=(0,0,0)→(5,5,5), v[3]=(0,0,1)→(5,5,6)
     * ================================================================*/
    bot_reset(s, bot, b);
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_TRANS);
    s->e_inpara = 1;
    VSET(s->e_para, 5, 5, 5);
    VSET(s->e_keypoint, 0, 0, 0);
    b->bot_verts[0] = -1;
    b->bot_verts[1] = -1;
    b->bot_verts[2] = -1;

    rt_edit_process(s);
    {
	vect_t v0 = {5, 5, 5}, v3 = {5, 5, 6};
	if (!VNEAR_EQUAL(&bot->vertices[0], v0, VUNITIZE_TOL) ||
	    !VNEAR_EQUAL(&bot->vertices[9], v3, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_TRANS failed\n"
		    "  v[0]=%g,%g,%g  v[3]=%g,%g,%g\n",
		    V3ARGS(&bot->vertices[0]), V3ARGS(&bot->vertices[9]));
	bu_log("RT_PARAMS_EDIT_TRANS SUCCESS: v[0]=%g,%g,%g v[3]=%g,%g,%g\n",
	       V3ARGS(&bot->vertices[0]), V3ARGS(&bot->vertices[9]));
    }

    /* ================================================================
     * RT_PARAMS_EDIT_ROT (rotate about keypoint (0,0,0), angles=5,5,5)
     *
     * v[0] stays at origin. v[1]=(1,0,0) rotates.
     * ================================================================*/
    bot_reset(s, bot, b);
    MAT_IDN(s->acc_rot_sol);
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_ROT);
    s->e_inpara = 1;
    VSET(s->e_para, 5, 5, 5);
    s->vp->gv_rotate_about = 'k';
    VSET(s->e_keypoint, 0, 0, 0);
    b->bot_verts[0] = -1;
    b->bot_verts[1] = -1;
    b->bot_verts[2] = -1;

    rt_edit_process(s);
    {
	vect_t exp_v0 = {0, 0, 0};
	if (!VNEAR_EQUAL(&bot->vertices[0], exp_v0, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_ROT(k): v[0] moved: %g,%g,%g\n",
		    V3ARGS(&bot->vertices[0]));
	vect_t orig_v1 = {1, 0, 0};
	if (VNEAR_EQUAL(&bot->vertices[3], orig_v1, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: RT_PARAMS_EDIT_ROT(k): v[1] not rotated (%g,%g,%g)\n",
		    V3ARGS(&bot->vertices[3]));
	bu_log("RT_PARAMS_EDIT_ROT(k) SUCCESS: v[0]=%g,%g,%g v[1]=%g,%g,%g\n",
	       V3ARGS(&bot->vertices[0]), V3ARGS(&bot->vertices[3]));
    }

    /* ================================================================
     * ECMD_BOT_MOVEV (move vertex 2 to (3,3,3))
     *
     * b->bot_verts[0]=2 (vertex index), e_inpara=3, e_para=(3,3,3).
     * v[2] → (3,3,3).
     * ================================================================*/
    bot_reset(s, bot, b);
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_BOT_MOVEV);
    s->e_inpara = 3;
    VSET(s->e_para, 3, 3, 3);
    b->bot_verts[0] = 2;  /* vertex index to move */
    b->bot_verts[1] = -1;
    b->bot_verts[2] = -1;
    s->e_mvalid = 0;

    rt_edit_process(s);
    {
	vect_t exp_v2 = {3, 3, 3};
	if (!VNEAR_EQUAL(&bot->vertices[6], exp_v2, VUNITIZE_TOL))
	    bu_exit(1, "ERROR: ECMD_BOT_MOVEV failed: v[2]=%g,%g,%g\n",
		    V3ARGS(&bot->vertices[6]));
	bu_log("ECMD_BOT_MOVEV SUCCESS: v[2]=%g,%g,%g\n",
	       V3ARGS(&bot->vertices[6]));
    }

    /* ================================================================
     * RT_PARAMS_EDIT_ROT XY returns BRLCAD_ERROR (unimplemented)
     * ================================================================*/
    bot_reset(s, bot, b);
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
