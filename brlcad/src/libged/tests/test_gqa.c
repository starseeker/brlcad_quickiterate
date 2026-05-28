/*                     T E S T _ G Q A . C
 * BRL-CAD
 *
 * Copyright (c) 2018-2026 United States Government as represented by
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
#include "bsg/draw_source.h"
#include <ged.h>
#include <ged/bsg_ged_draw.h>

struct gqa_match {
    const char *target;
    struct bsg_node *result;
};

static int
gqa_find_group(struct bsg_node *group, void *userdata)
{
    struct gqa_match *m = (struct gqa_match *)userdata;
    const char *path = bsg_view_obj_group_path(group);
    if (!path || !BU_STR_EQUAL(path, m->target))
	return 1; /* keep iterating */
    printf("found %s;\n", path);
    m->result = bsg_view_obj_group_first_solid(group);
    return 0; /* stop */
}

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

    /* Programmatically extract the resulting plot data (assuming we're
     * only after ffff00-colored overlap data).  Walks the gedp draw set
     * via the bsg_view_obj_* migration API so this test doesn't depend
     * on the legacy display_list / dl_head_scene_obj internals. */
    struct gqa_match m;
    m.target = "OVERLAPSffff00";
    m.result = NULL;
    bsg_view_obj_foreach_group(gedp, gqa_find_group, &m);
    struct bsg_node *vdata = m.result;

    if (vdata) {
	FILE *fp;
	fp = fopen(gqa_plot_fname, "wb");
	if (!fp)
	    bu_exit(EXIT_FAILURE, "Could not open %s for writing\n", gqa_plot_fname);
	printf("Writing plot data to %s for inspection with overlay command\n", gqa_plot_fname);
	bsg_vlist_to_uplot(fp, bsg_node_vlist_head(vdata));
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
