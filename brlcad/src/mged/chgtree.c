/*                       C H G T R E E . C
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
/** @file mged/chgtree.c
 *
 * This module contains functions which change the tree structure
 * of the model, and delete solids or combinations or combination elements.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "tcl.h"

#include "vmath.h"
#include "bn.h"
#include "wdb.h"
#include "rt/geom.h"

#include "./sedit.h"
#include "./mged.h"
#include "./mged_dm.h"
#include "./cmd.h"
#include "bsg/node.h"


/**
 * Copy a cylinder and position at end of original cylinder
 * Used in making "wires"
 *
 * Format: cpi old new
 */
int
f_copy_inv(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    struct directory *proto;
    struct directory *dp;
    struct rt_db_internal internal;
    struct rt_tgc_internal *tgc_ip;
    int id;

    CHECK_DBI_NULL;
    CHECK_READ_ONLY;

    if (argc < 3 || 3 < argc) {
	struct bu_vls vls = BU_VLS_INIT_ZERO;

	bu_vls_printf(&vls, "help cpi");
	Tcl_Eval(interp, bu_vls_addr(&vls));
	bu_vls_free(&vls);
	return TCL_ERROR;
    }

    if ((proto = db_lookup(s->dbip,  argv[1], LOOKUP_NOISY)) == RT_DIR_NULL)
	return TCL_ERROR;

    if (db_lookup(s->dbip,  argv[2], LOOKUP_QUIET) != RT_DIR_NULL) {
	Tcl_AppendResult(s->interp, argv[2], ":  already exists\n", (char *)NULL);
	return TCL_ERROR;
    }

    if ((id = rt_db_get_internal(&internal, proto, s->dbip, (fastf_t *)NULL)) < 0) {
	Tcl_AppendResult(s->interp, "Database read error, aborting\n", (char *)NULL);
	return TCL_ERROR;
    }
    /* make sure it is a TGC */
    if (id != ID_TGC) {
	Tcl_AppendResult(interp, "f_copy_inv: ", argv[1],
			 " is not a cylinder\n", (char *)NULL);
	rt_db_free_internal(&internal);
	return TCL_ERROR;
    }
    tgc_ip = (struct rt_tgc_internal *)internal.idb_ptr;

    /* translate to end of "original" cylinder */
    VADD2(tgc_ip->v, tgc_ip->v, tgc_ip->h);

    /* no interrupts */
    (void)signal(SIGINT, SIG_IGN);

    if ((dp = db_diradd(s->dbip, argv[2], -1L, 0, proto->d_flags, &proto->d_minor_type)) == RT_DIR_NULL) {
	Tcl_AppendResult(s->interp, "An error has occurred while adding a new object to the database.\n", (char *)NULL);
	Tcl_AppendResult(s->interp, ERROR_RECOVERY_SUGGESTION, (char *)NULL);
	return TCL_ERROR;
    }

    if (rt_db_put_internal(dp, s->dbip, &internal) < 0) {
	Tcl_AppendResult(s->interp, "Database write error, aborting.\n", (char *)NULL);
	Tcl_AppendResult(s->interp, ERROR_RECOVERY_SUGGESTION, (char *)NULL);
	return TCL_ERROR;
    }

    {
	const char *av[3];

	av[0] = "e";
	av[1] = argv[2]; /* depends on solid name being in argv[2] */
	av[2] = NULL;

	/* draw the new solid */
	(void)cmd_draw(clientData, interp, 2, av);
    }

    if (s->global_editing_state == ST_VIEW) {
	struct bu_vls sed_cmd = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&sed_cmd, "sed %s", argv[2]);

	/* solid edit this new cylinder */
	Tcl_Eval(interp, bu_vls_cstr(&sed_cmd));

	bu_vls_free(&sed_cmd);
    }

    return TCL_OK;
}


struct _fswp_data {
    struct db_full_path *pathp;
    struct bv_scene_obj *ret;
    int count;
};

static int
_find_solid_with_path_cb(bsg_node *n, void *ud)
{
    struct bv_scene_obj *sp = (struct bv_scene_obj *)n;
    struct _fswp_data *d = (struct _fswp_data *)ud;
    if (!bsg_node_uptr_get((const bsg_node *)sp, 2)) return 1;
    struct ged_bv_data *bdata = (struct ged_bv_data *)bsg_node_uptr_get((const bsg_node *)sp, 2);
    if (!db_identical_full_paths(d->pathp, &bdata->s_fullpath)) return 1;
    /* Walk up to the root child (depth-1 group) */
    {
	struct bv_scene_obj *_g = (struct bv_scene_obj *)sp->bsg.bsg_parent;
	while (_g && _g->bsg.bsg_parent &&
	       ((struct bv_scene_obj *)_g->bsg.bsg_parent)->bsg.bsg_parent != NULL)
	    _g = (struct bv_scene_obj *)_g->bsg.bsg_parent;
	illum_gdlp = _g;
    }
    d->ret = sp;
    d->count++;
    return 1; /* keep scanning for duplicates */
}

struct bv_scene_obj *
find_solid_with_path(struct mged_state *s, struct db_full_path *pathp)
{
    RT_CK_FULL_PATH(pathp);

    struct _fswp_data d;
    d.pathp = pathp;
    d.ret = NULL;
    d.count = 0;
    bsg_visit((bsg_node *)bsg_view_obj_root(s->gedp), BSG_NODE_SHAPE, _find_solid_with_path_cb, &d);

    if (d.count > 1) {
	struct bu_vls tmp_vls = BU_VLS_INIT_ZERO;
	bu_vls_printf(&tmp_vls, "find_solid_with_path() found %d matches\n", d.count);
	Tcl_AppendResult(s->interp, bu_vls_addr(&tmp_vls), (char *)NULL);
	bu_vls_free(&tmp_vls);
    }

    return d.ret;
}


/**
 * Transition from VIEW state to OBJECT EDIT state in a single
 * command, rather than requiring "press oill", "ill leaf", "matpick
 * a/b".
 *
 * Takes two parameters which when combined represent the full path to
 * the reference solid of the object to be edited.  The conceptual
 * slash between the two strings signifies which matrix is to be
 * edited.
 */
int
cmd_oed(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;
    struct db_full_path lhs;
    struct db_full_path rhs;
    struct db_full_path both;

    CHECK_DBI_NULL;

    if (argc < 3 || 3 < argc) {
	struct bu_vls vls = BU_VLS_INIT_ZERO;

	bu_vls_printf(&vls, "help oed");
	Tcl_Eval(interp, bu_vls_addr(&vls));
	bu_vls_free(&vls);
	return TCL_ERROR;
    }

    if (not_state(s, ST_VIEW, "Object Illuminate")) {
	return TCL_ERROR;
    }

    /* Common part of illumination */
    if (!bsg_view_obj_is_nonempty(s->gedp)) {
	Tcl_AppendResult(interp, "no solids in view", (char *)NULL);
	return TCL_ERROR;
    }

    if (db_string_to_path(&lhs, s->dbip, argv[1]) < 0) {
	Tcl_AppendResult(interp, "bad lhs path", (char *)NULL);
	return TCL_ERROR;
    }
    if (db_string_to_path(&rhs, s->dbip, argv[2]) < 0) {
	db_free_full_path(&lhs);
	Tcl_AppendResult(interp, "bad rhs path", (char *)NULL);
	return TCL_ERROR;
    }
    if (rhs.fp_len <= 0) {
	db_free_full_path(&lhs);
	db_free_full_path(&rhs);
	Tcl_AppendResult(interp, "rhs must not be null", (char *)NULL);
	return TCL_ERROR;
    }

    db_full_path_init(&both);
    db_dup_full_path(&both, &lhs);
    db_append_full_path(&both, &rhs);

    /* Patterned after ill_common() ... */
    illump = bsg_view_obj_first_solid(s->gedp);
    illum_gdlp = bsg_view_obj_group_of_solid(s->gedp, illump);
    edobj = 0;		/* sanity */
    movedir = 0;		/* No edit modes set */
    MAT_IDN(MEDIT(s)->model_changes);	/* No changes yet */
    (void)chg_state(s, ST_VIEW, ST_O_PICK, "internal change of state");
    /* reset accumulation local scale factors */
    MEDIT(s)->acc_sc[0] = MEDIT(s)->acc_sc[1] = MEDIT(s)->acc_sc[2] = 1.0;
    new_mats(s);

    /* Find the one solid, set s_iflag UP, point illump at it */
    illump = find_solid_with_path(s, &both);
    if (!illump) {
	db_free_full_path(&lhs);
	db_free_full_path(&rhs);
	db_free_full_path(&both);
	Tcl_AppendResult(interp, "Unable to find solid matching path", (char *)NULL);
	illum_gdlp = NULL;
	illump = 0;
	(void)chg_state(s, ST_O_PICK, ST_VIEW, "error recovery");
	return TCL_ERROR;
    }
    (void)chg_state(s, ST_O_PICK, ST_O_PATH, "internal change of state");

    /* Select the matrix */
    struct bu_vls tcl_cmd = BU_VLS_INIT_ZERO;
    bu_vls_printf(&tcl_cmd, "matpick %lu", (long unsigned)lhs.fp_len);
    if (Tcl_Eval(interp, bu_vls_cstr(&tcl_cmd)) != TCL_OK) {
	db_free_full_path(&lhs);
	db_free_full_path(&rhs);
	db_free_full_path(&both);
	bu_vls_free(&tcl_cmd);
	Tcl_AppendResult(interp, "error detected inside f_matpick", (char *)NULL);
	return TCL_ERROR;
    }
    bu_vls_free(&tcl_cmd);
    if (not_state(s, ST_O_EDIT, "Object EDIT")) {
	db_free_full_path(&lhs);
	db_free_full_path(&rhs);
	db_free_full_path(&both);
	Tcl_AppendResult(interp, "MGED state did not advance to Object EDIT", (char *)NULL);
	return TCL_ERROR;
    }
    db_free_full_path(&lhs);
    db_free_full_path(&rhs);
    db_free_full_path(&both);
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
