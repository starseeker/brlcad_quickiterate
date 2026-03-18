/*                         S A V E V I E W . C
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
/** @file libged/saveview.c
 *
 * The saveview command.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "bu/app.h"
#include "bu/file.h"
#include "bu/getopt.h"
#include "bu/ptbl.h"


#include "../ged_private.h"


/**
 * Return basename of path, removing leading slashes and trailing suffix.
 */
static char *
basename_without_suffix(const char *p1, const char *suff)
{
    char *p2, *p3;
    static char buf[128];

    /* find the basename */
    p2 = (char *)p1;
    while (*p1) {
	if (*p1++ == '/')
	    p2 = (char *)p1;
    }

    /* find the end of suffix */
    for (p3=(char *)suff; *p3; p3++)
	;

    /* early out */
    while (p1>p2 && p3>suff) {
	if (*--p3 != *--p1)
	    return p2;
    }

    /* stash and return filename, sans suffix */
    bu_strlcpy(buf, p2, p1-p2+1);
    return buf;
}


int
ged_saveview_core(struct ged *gedp, int argc, const char *argv[])
{
    int i;
    FILE *fp;
    char *base;
    int c;
    char rtcmd[255] = {'r', 't', 0};
    char outlog[255] = {0};
    char outpix[255] = {0};
    char inputg[255] = {0};
    const char *cmdname = argv[0];
    static const char *usage = "[-e] [-i] [-l] [-o] filename [args]";

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_VIEW(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", cmdname, usage);
	return GED_HELP;
    }

    bu_optind = 1;
    while ((c = bu_getopt(argc, (char * const *)argv, "e:i:l:o:")) != -1) {
	switch (c) {
	    case 'e':
		snprintf(rtcmd, 255, "%s", bu_optarg);
		break;
	    case 'l':
		snprintf(outlog, 255, "%s", bu_optarg);
		break;
	    case 'o':
		snprintf(outpix, 255, "%s", bu_optarg);
		break;
	    case 'i':
		snprintf(inputg, 255, "%s", bu_optarg);
		break;
	    default: {
		bu_vls_printf(gedp->ged_result_str, "Option '%c' unknown\n", c);
		bu_vls_printf(gedp->ged_result_str, "help saveview");
		return BRLCAD_ERROR;
	    }
	}
    }
    argc -= bu_optind-1;
    argv += bu_optind-1;

    if (argc < 2) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", cmdname, usage);
	return BRLCAD_ERROR;
    }

    fp = fopen(argv[1], "a");
    if (fp == NULL) {
	perror(argv[1]);
	return BRLCAD_ERROR;
    }
    (void)bu_fchmod(fileno(fp), 0755);	/* executable */

    if (!gedp->dbip->dbi_filename) {
	bu_log("Error: geometry file is not specified\n");
	fclose(fp);
	return BRLCAD_ERROR;
    }

    if (!bu_file_exists(gedp->dbip->dbi_filename, NULL)) {
	bu_log("Error: %s does not exist\n", gedp->dbip->dbi_filename);
	fclose(fp);
	return BRLCAD_ERROR;
    }

    base = basename_without_suffix(argv[1], ".sh");
    if (outpix[0] == '\0') {
	snprintf(outpix, 255, "%s.pix", base);
    }
    if (outlog[0] == '\0') {
	snprintf(outlog, 255, "%s.log", base);
    }

    /* Do not specify -v option to rt; batch jobs must print everything. -Mike */
    fprintf(fp, "#!/bin/sh\n%s -M ", rtcmd);
    { struct bsg_camera _cm; bsg_view_get_camera(gedp->ged_gvp, &_cm);
      if (_cm.perspective > 0)
	  fprintf(fp, "-p%g ", _cm.perspective);
    }
    for (i = 2; i < argc; i++)
	fprintf(fp, "%s ", argv[i]);

    if (bu_strncmp(rtcmd, "nirt", 4) != 0)
	fprintf(fp, "\\\n -o %s\\\n $*\\\n", outpix);

    if (inputg[0] == '\0') {
	snprintf(inputg, 255, "%s", gedp->dbip->dbi_filename);
    }
    fprintf(fp, " '%s'\\\n ", inputg);

    /* Phase 2e: enumerate drawn top-level objects from scene-root children */
    {
	bsg_view *_sv = gedp->ged_gvp;
	bsg_shape *_root = _sv ? bsg_scene_root_get(_sv) : NULL;
	if (_root && BU_PTBL_IS_INITIALIZED(&_root->children)) {
	    struct bu_ptbl drawn_tops;
	    bu_ptbl_init(&drawn_tops, 8, "saveview_tops");
	    for (size_t _si = 0; _si < BU_PTBL_LEN(&_root->children); _si++) {
		bsg_shape *_sp = (bsg_shape *)BU_PTBL_GET(&_root->children, _si);
		if (!_sp || !_sp->s_u_data) continue;
		struct ged_bv_data *_bd = (struct ged_bv_data *)_sp->s_u_data;
		if (!_bd->s_fullpath.fp_len) continue;
		struct directory *_dp = _bd->s_fullpath.fp_names[0];
		if (_dp->d_addr == RT_DIR_PHONY_ADDR) continue;
		if (bu_ptbl_ins_unique(&drawn_tops, (long *)_dp) < 0) continue;
	    }
	    for (size_t _ti = 0; _ti < BU_PTBL_LEN(&drawn_tops); _ti++) {
		struct directory *_dp = (struct directory *)BU_PTBL_GET(&drawn_tops, _ti);
		fprintf(fp, "'%s' ", _dp->d_namep);
	    }
	    bu_ptbl_free(&drawn_tops);
	}
    }

    fprintf(fp, "\\\n 2>> %s\\\n", outlog);
    fprintf(fp, " <<EOF\n");

    {
	vect_t eye_model;

	_ged_rt_set_eye_model(gedp, eye_model);
	_ged_rt_write(gedp, fp, eye_model, -1, NULL);
    }

    fprintf(fp, "\nEOF\n");
    (void)fclose(fp);

    return BRLCAD_OK;
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
