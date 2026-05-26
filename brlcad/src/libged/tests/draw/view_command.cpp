/*            V I E W _ C O M M A N D . C P P
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 */

#include "common.h"

#include <string>

#include <bu.h>
#include <ged.h>
#include <bv.h>
#include <bsg/lod.h>

#include "../../dbi.h"

#define ASSERT(cond) do { \
    nchecks++; \
    if (!(cond)) { \
	bu_log("FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond); \
	nfails++; \
    } \
} while (0)

static int nchecks = 0;
static int nfails = 0;

static int
run_view(struct ged *gedp, int argc, const char **argv)
{
    int ret = ged_exec_view(gedp, argc, argv);
    return ret;
}

static std::string
result_str(struct ged *gedp)
{
    const char *r = bu_vls_cstr(gedp->ged_result_str);
    return (r) ? std::string(r) : std::string();
}

int
main(int argc, const char **argv)
{
    bu_setprogname(argv[0]);

    if (argc != 2)
	bu_exit(EXIT_FAILURE, "Usage: ged_test_view_command <directory-containing-moss.g>\n");

    struct bu_vls gpath = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&gpath, "%s/moss.g", argv[1]);
    struct ged *gedp = ged_open("db", bu_vls_cstr(&gpath), 1);
    ASSERT(gedp != NULL);
    if (!gedp)
	return EXIT_FAILURE;

    gedp->dbi_state = new DbiState(gedp);
    gedp->new_cmd_forms = 1;
    gedp->ged_lod = bsg_mesh_lod_context_create(gedp->dbip->dbi_filename);

    bsg_set_rm_view(&gedp->ged_views, NULL);
    struct bsg_view *views[2] = {NULL, NULL};
    for (int i = 0; i < 2; i++) {
	BU_GET(views[i], struct bsg_view);
	bsg_init(views[i], &gedp->ged_views);
	bu_vls_sprintf(&views[i]->gv_name, "V%d", i);
	bsg_set_add_view(&gedp->ged_views, views[i]);
	bu_ptbl_ins(&gedp->ged_free_views, (long *)views[i]);
	if (!i)
	    gedp->ged_gvp = views[i];
    }

    const char *c0[] = {"view", "obj", "create", "u_line", "line", "create", "0", "0", "0", NULL};
    ASSERT(run_view(gedp, 9, c0) == BRLCAD_OK);

    const char *c1[] = {"view", "obj", "info", "u_line", "type", NULL};
    ASSERT(run_view(gedp, 5, c1) == BRLCAD_OK);

    const char *c2[] = {"view", "obj", "set", "u_line", "draw", "0", NULL};
    ASSERT(run_view(gedp, 6, c2) == BRLCAD_OK);
    const char *c3[] = {"view", "obj", "info", "u_line", "draw", NULL};
    ASSERT(run_view(gedp, 5, c3) == BRLCAD_OK);

    const char *c4[] = {"view", "obj", "list", "u_*", NULL};
    ASSERT(run_view(gedp, 4, c4) == BRLCAD_OK);
    ASSERT(result_str(gedp).find("u_line") != std::string::npos);

    const char *c5[] = {"view", "obj", "set", "u_line", "arrow", "1", NULL};
    ASSERT(run_view(gedp, 6, c5) == BRLCAD_OK);

    const char *c6[] = {"view", "-V", "V0", "obj", "-L", "create", "l_line", "line", "create", "0", "0", "0", NULL};
    ASSERT(run_view(gedp, 12, c6) == BRLCAD_OK);
    const char *c7[] = {"view", "-V", "V0", "obj", "list", NULL};
    ASSERT(run_view(gedp, 5, c7) == BRLCAD_OK);
    ASSERT(result_str(gedp).find("l_line") != std::string::npos);
    const char *c8[] = {"view", "-V", "V1", "obj", "list", NULL};
    ASSERT(run_view(gedp, 5, c8) == BRLCAD_OK);
    ASSERT(result_str(gedp).find("l_line") == std::string::npos);

    const char *c11[] = {"view", "obj", "-g", "all.g", "create", "g2", NULL};
    ASSERT(run_view(gedp, 6, c11) == BRLCAD_OK);
    const char *c12[] = {"view", "obj", "remove", "g2", NULL};
    ASSERT(run_view(gedp, 4, c12) == BRLCAD_OK);

    bu_vls_free(&gpath);
    ged_close(gedp);

    bu_log("view_command: %d checks, %d failures\n", nchecks, nfails);
    return nfails ? EXIT_FAILURE : EXIT_SUCCESS;
}
