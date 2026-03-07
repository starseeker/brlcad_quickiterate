/*                     T E S T _ G Q A . C
 * BRL-CAD
 *
 * Copyright (c) 2018-2025 United States Government as represented by
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
/** @file test_gqa.c
 *
 * Extract plotting data from a gqa run
 *
 */

#include "common.h"

#include <stdio.h>
#include <bu.h>
#include <bsg.h>
#include <ged.h>
#include "bsg/util.h"

int
main(int ac, char *av[]) {
    struct ged *gedp;
    const char *gqa_plot_fname = "gqa_ovlps.plot3";
    const char *gqa[4] = {"gqa", "-Aop", "ovlp", NULL};

    bu_setprogname(av[0]);

    if (ac != 2) {
	printf("Usage: %s file.g\n", av[0]);
	return 1;
    }
    if (!bu_file_exists(av[1], NULL)) {
	printf("ERROR: [%s] does not exist, expecting .g file\n", av[1]);
	return 2;
    }

    gedp = ged_open("db", av[1], 1);
    ged_exec_gqa(gedp, 3, gqa);
    printf("%s\n", bu_vls_cstr(gedp->ged_result_str));

    // Example of programmatically extracting the resulting plot data (assuming
    // we're only after ffff00 colored data)
    //
    // (TODO - this will need to be redone if/when the new drawing setup takes
    // over - there (at least for now) we would do a bsg_view_find_shape on the view
    // with the gqa overlap view object's name (gqa:overlaps) to find the
    // bsg_shape, and then iterate over that object's child objects to get
    // the different colored vlists...)
    bsg_shape *vdata = NULL;

    /* Phase 2e: use scene-root children and find by path name */
    bsg_shape *root = bsg_scene_root_get(gedp->ged_gvp);
    size_t nshapes = root ? BU_PTBL_LEN(&root->children) : 0;
    for (size_t si = 0; si < nshapes; si++) {
	bsg_shape *sp = (bsg_shape *)BU_PTBL_GET(&root->children, si);
	if (!sp->s_u_data) continue;
	struct ged_bv_data *bdata = (struct ged_bv_data *)sp->s_u_data;
	if (bdata->s_fullpath.fp_len == 0) continue;
	struct directory *top = bdata->s_fullpath.fp_names[0];
	if (BU_STR_EQUAL(top->d_namep, "OVERLAPSffff00")) {
	    printf("found %s;\n", top->d_namep);
	    vdata = sp;
	    break;
	}
    }
    /* Phase 2e: dl_head_scene_obj removed; root->children is the only source */

    if (vdata) {
	FILE *fp;
	fp = fopen(gqa_plot_fname, "wb");
	if (!fp)
	    bu_exit(EXIT_FAILURE, "Could not open %s for writing\n", gqa_plot_fname);
	printf("Writing plot data to %s for inspection with overlay command\n", gqa_plot_fname);
	bsg_vlist_to_uplot(fp, &vdata->s_vlist);
	fclose(fp);
    } else {
	bu_exit(EXIT_FAILURE, "No GQA plotting data found.\n");
    }

    ged_close(gedp);

    return 0;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
