/*                          P L O T . C
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
/** @file mged/plot.c
 *
 * Provide UNIX-plot output of the current view.
 *
 */

#include "common.h"

#include <math.h>
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#include "bio.h"
#include "bresource.h"

#include "bu/app.h"
#include "bu/units.h"
#include "vmath.h"
#include "raytrace.h"
#include "bsg/appearance.h"
#include "bsg/node.h"
#include "bv/plot3.h"

#include "./mged.h"
#include "./mged_dm.h"

#if defined(HAVE_FDOPEN) && !defined(HAVE_DECL_FDOPEN)
extern FILE *fdopen(int fd, const char *mode);
#endif

/* ------------------------------------------------------------------ */
/* bsg_visit callbacks for solid iteration in plot/area routines      */
/* ------------------------------------------------------------------ */

/* Callback: check whether any solid fails the "area" eligibility test. */
static int
_area_check_solid_cb(bsg_node *n, void *ud)
{
    struct bv_scene_obj *sp = (struct bv_scene_obj *)n;
    int *error = (int *)ud;
    if (!bsg_node_legacy_eflag((const bsg_node *)sp) && bsg_node_line_style((const bsg_node *)sp) != BSG_APPEARANCE_LINE_SOLID) {
	*error = 1;
	return 0; /* early stop */
    }
    return 1;
}

/* Callback: write solid vlists to cad_boundp pipe. */
struct _area_write_data {
    FILE *fp_w;
    const mat_t *rotation;
    struct db_i *dbip;
    vect_t last;
    vect_t fin;
};

static int
_area_write_solid_cb(bsg_node *n, void *ud)
{
    struct bv_scene_obj *sp = (struct bv_scene_obj *)n;
    struct _area_write_data *d = (struct _area_write_data *)ud;
    struct bv_vlist *vp;
    for (BU_LIST_FOR(vp, bv_vlist, bsg_node_vlist_head((bsg_node *)sp))) {
	int i;
	int nused = vp->nused;
	int *cmd = vp->cmd;
	point_t *pt = vp->pt;
	for (i = 0; i < nused; i++, cmd++, pt++) {
	    switch (*cmd) {
		case BV_VLIST_POLY_START:
		case BV_VLIST_POLY_VERTNORM:
		case BV_VLIST_TRI_START:
		case BV_VLIST_TRI_VERTNORM:
		    continue;
		case BV_VLIST_POLY_MOVE:
		case BV_VLIST_LINE_MOVE:
		case BV_VLIST_TRI_MOVE:
		    MAT4X3VEC(d->last, *d->rotation, *pt);
		    continue;
		case BV_VLIST_POLY_DRAW:
		case BV_VLIST_POLY_END:
		case BV_VLIST_LINE_DRAW:
		case BV_VLIST_TRI_DRAW:
		case BV_VLIST_TRI_END:
		    MAT4X3VEC(d->fin, *d->rotation, *pt);
		    break;
	    }
	    fprintf(d->fp_w, "%.9e %.9e %.9e %.9e\n",
		    d->last[X] * d->dbip->dbi_base2local,
		    d->last[Y] * d->dbip->dbi_base2local,
		    d->fin[X]  * d->dbip->dbi_base2local,
		    d->fin[Y]  * d->dbip->dbi_base2local);
	    VMOVE(d->last, d->fin);
	}
    }
    return 1;
}

/* ------------------------------------------------------------------ */

int
f_area(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    char result[RT_MAXLINE] = {0};
    char tol_str[32] = {0};

#ifndef _WIN32
    FILE *fp_r;
    FILE *fp_w;
    int rpid;
    int pid1;
    int pid2;
    int fd1[2]; /* mged | cad_boundp */
    int fd2[2]; /* cad_boundp | cad_parea */
    int fd3[2]; /* cad_parea | mged */
    int retcode;
    const char *tol_ptr;

    /* XXX needs fixing */

    CHECK_DBI_NULL;

    if (argc < 1 || 2 < argc) {
	struct bu_vls vls = BU_VLS_INIT_ZERO;

	bu_vls_printf(&vls, "help area");
	Tcl_Eval(interp, bu_vls_addr(&vls));
	bu_vls_free(&vls);
	return TCL_ERROR;
    }

    if (not_state(s, ST_VIEW, "Presented Area Calculation") == TCL_ERROR)
	return TCL_ERROR;

    if (!bsg_view_obj_is_nonempty(s->gedp)) {
	Tcl_AppendResult(interp, "No objects displayed!!!\n", (char *)NULL);
	return TCL_ERROR;
    }

    {
	int area_err = 0;
	bsg_visit((bsg_node *)bsg_view_obj_root(s->gedp), BSG_NODE_SHAPE,
		  _area_check_solid_cb, &area_err);
	if (area_err) {
	    struct bu_vls vls = BU_VLS_INIT_ZERO;
	    bu_vls_printf(&vls, "help area");
	    Tcl_Eval(interp, bu_vls_addr(&vls));
	    bu_vls_free(&vls);
	    return TCL_ERROR;
	}
    }

    if (argc == 2) {
	Tcl_AppendResult(interp, "Tolerance is ", argv[1], "\n", (char *)NULL);
	tol_ptr = argv[1];
    } else {
	struct bu_vls tmp_vls = BU_VLS_INIT_ZERO;
	double tol = 0.0005;

	sprintf(tol_str, "%e", tol);
	tol_ptr = tol_str;
	bu_vls_printf(&tmp_vls, "Auto-tolerance is %s\n", tol_str);
	Tcl_AppendResult(interp, bu_vls_addr(&tmp_vls), (char *)NULL);
	bu_vls_free(&tmp_vls);
    }

    if (pipe(fd1) != 0) {
	perror("f_area");
	return TCL_ERROR;
    }

    if (pipe(fd2) != 0) {
	perror("f_area");
	return TCL_ERROR;
    }

    if (pipe(fd3) != 0) {
	perror("f_area");
	return TCL_ERROR;
    }

    if ((pid1 = fork()) == 0) {
	const char *cad_boundp = bu_dir(NULL, 0, BU_DIR_BIN, "cad_boundp", BU_DIR_EXT, NULL);

	dup2(fd1[0], fileno(stdin));
	dup2(fd2[1], fileno(stdout));

	close(fd1[0]);
	close(fd1[1]);
	close(fd2[0]);
	close(fd2[1]);
	close(fd3[0]);
	close(fd3[1]);

	execlp(cad_boundp, cad_boundp, "-t", tol_ptr, (char *)NULL);
    }

    if ((pid2 = fork()) == 0) {
	const char *cad_parea = bu_dir(NULL, 0, BU_DIR_BIN, "cad_parea", BU_DIR_EXT, NULL);

	dup2(fd2[0], fileno(stdin));
	dup2(fd3[1], fileno(stdout));

	close(fd1[0]);
	close(fd1[1]);
	close(fd2[0]);
	close(fd2[1]);
	close(fd3[0]);
	close(fd3[1]);

	execlp(cad_parea, cad_parea, (char *)NULL);
    }

    close(fd1[0]);
    close(fd2[0]);
    close(fd2[1]);
    close(fd3[1]);

    fp_w = fdopen(fd1[1], "w");
    fp_r = fdopen(fd3[0], "r");

    /*
     * Write out rotated but unclipped, untranslated,
     * and unscaled vectors
     */
    {
	struct _area_write_data wd;
	wd.fp_w = fp_w;
	wd.rotation = (const mat_t *)&view_state->vs_gvp->gv_rotation;
	wd.dbip = s->dbip;
	VSETALL(wd.last, 0.0);
	VSETALL(wd.fin, 0.0);
	bsg_visit((bsg_node *)bsg_view_obj_root(s->gedp), BSG_NODE_SHAPE,
		  _area_write_solid_cb, &wd);
    }

    fclose(fp_w);

    Tcl_AppendResult(interp, "Presented area from this viewpoint, square ",
		     bu_units_string(s->dbip->dbi_local2base), ":\n", (char *)NULL);

    /* Read result */
    bu_fgets(result, RT_MAXLINE, fp_r);
    Tcl_AppendResult(interp, result, "\n", (char *)NULL);

    while ((rpid = wait(&retcode)) != pid1 && rpid != -1);
    while ((rpid = wait(&retcode)) != pid2 && rpid != -1);

    fclose(fp_r);
    close(fd1[1]);
    close(fd3[0]);
#endif

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
