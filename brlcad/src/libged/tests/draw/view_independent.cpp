/*         V I E W _ I N D E P E N D E N T . C P P
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @file view_independent.cpp
 *
 * Phase V6 regression test: `view independent` is backed by a private
 * structural BSG scope rather than the legacy mode bit alone.
 */

#include "common.h"

#include <algorithm>
#include <string>
#include <vector>

#include <bu.h>
#include <bv.h>
#include <bsg/lod.h>
#include <ged.h>

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

static std::vector<std::string>
drawn_paths(struct ged *gedp, struct bsg_view *v)
{
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    BViewState *bvs = dbis->get_view_state(v);
    return bvs->list_drawn_paths(-1, true);
}

static int
has_path(const std::vector<std::string> &paths, const char *path)
{
    return (std::find(paths.begin(), paths.end(), std::string(path)) != paths.end()) ? 1 : 0;
}

static int
set_view_independent(struct ged *gedp, const char *view_name, int independent)
{
    const char *av[5] = {"view", "independent", view_name, independent ? "1" : "0", NULL};
    return ged_exec_view(gedp, 4, av);
}

static int
draw_shared(struct ged *gedp, const char *path)
{
    const char *av[4] = {"draw", "-R", path, NULL};
    return ged_exec_draw(gedp, 3, av);
}

static int
draw_view(struct ged *gedp, const char *view_name, const char *path)
{
    const char *av[6] = {"draw", "-R", "-V", view_name, path, NULL};
    return ged_exec_draw(gedp, 5, av);
}

int
main(int argc, const char **argv)
{
    bu_setprogname(argv[0]);

    if (argc != 2)
	bu_exit(EXIT_FAILURE, "Usage: ged_test_view_independent <directory-containing-moss.g>\n");

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

    ASSERT(draw_shared(gedp, "all.g") == BRLCAD_OK);
    ASSERT(!bsg_view_is_independent(views[0]));
    ASSERT(drawn_paths(gedp, views[0]).size() == 1);
    ASSERT(has_path(drawn_paths(gedp, views[0]), "all.g"));
    ASSERT(drawn_paths(gedp, views[1]).size() == 1);

    ASSERT(set_view_independent(gedp, "V0", 1) == BRLCAD_OK);
    ASSERT(bsg_view_is_independent(views[0]));
    ASSERT(bsg_view_independent_scope(views[0], 0) != NULL);
    ASSERT(drawn_paths(gedp, views[0]).size() == 1);
    ASSERT(has_path(drawn_paths(gedp, views[0]), "all.g"));

    ASSERT(draw_shared(gedp, "box.r") == BRLCAD_OK);
    ASSERT(drawn_paths(gedp, views[1]).size() == 2);
    ASSERT(has_path(drawn_paths(gedp, views[1]), "box.r"));
    ASSERT(drawn_paths(gedp, views[0]).size() == 1);
    ASSERT(!has_path(drawn_paths(gedp, views[0]), "box.r"));

    ASSERT(draw_view(gedp, "V0", "tor.r") == BRLCAD_OK);
    ASSERT(drawn_paths(gedp, views[0]).size() == 2);
    ASSERT(has_path(drawn_paths(gedp, views[0]), "tor.r"));
    ASSERT(!has_path(drawn_paths(gedp, views[1]), "tor.r"));

    ASSERT(set_view_independent(gedp, "V0", 0) == BRLCAD_OK);
    ASSERT(!bsg_view_is_independent(views[0]));
    ASSERT(bsg_view_independent_scope(views[0], 0) == NULL);
    ASSERT(drawn_paths(gedp, views[0]).size() == 2);
    ASSERT(has_path(drawn_paths(gedp, views[0]), "all.g"));
    ASSERT(has_path(drawn_paths(gedp, views[0]), "box.r"));
    ASSERT(!has_path(drawn_paths(gedp, views[0]), "tor.r"));

    bu_vls_free(&gpath);
    ged_close(gedp);

    bu_log("view_independent: %d checks, %d failures\n", nchecks, nfails);
    return nfails ? EXIT_FAILURE : EXIT_SUCCESS;
}
