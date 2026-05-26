/*                         K I L L R E F S . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2026 United States Government as represented by
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
/** @file libged/killrefs.c
 *
 * The killrefs command.
 *
 */

#include "common.h"

#include <string.h>

#include "bu/cmd.h"
#include "bu/ptbl.h"

#include "../ged_private.h"


/*
 * Return 1 if @p name matches any path component after the first one in
 * @p path.  Path components are separated by '/'.  This is used to identify
 * drawn paths that go *through* a named object (i.e., the object is a
 * non-root member of the path), as opposed to paths where the object is
 * drawn directly at the top level.
 */
static int
_name_in_nonroot_component(const char *path, const char *name)
{
    size_t namelen = strlen(name);
    const char *p = path;

    /* skip the first component */
    while (*p && *p != '/')
	p++;
    if (!*p)
	return 0;  /* single-component path — no interior components */
    p++;  /* step past the '/' separator */

    while (*p) {
	const char *slash = strchr(p, '/');
	size_t clen = slash ? (size_t)(slash - p) : strlen(p);
	if (clen == namelen && bu_strncmp(p, name, clen) == 0)
	    return 1;
	if (!slash)
	    break;
	p = slash + 1;
    }
    return 0;
}


/* Callback data for the killrefs drawn-path collection pass */
struct _killrefs_ctx {
    const char *name;
    struct bu_ptbl *to_erase;  /* collects strdup'd path strings */
};

/*
 * foreach_group callback: collect drawn paths whose non-root components
 * contain the target name.  The paths are copied as strings so the
 * subsequent erase pass does not iterate a mutating tree.
 */
static int
_killrefs_group_cb(struct bsg_node *group, void *userdata)
{
    struct _killrefs_ctx *ctx = (struct _killrefs_ctx *)userdata;
    if (bsg_view_obj_group_is_phony(group))
	return 1;
    const char *path = bsg_view_obj_group_path(group);
    if (path && _name_in_nonroot_component(path, ctx->name))
	bu_ptbl_ins(ctx->to_erase, (long *)bu_strdup(path));
    return 1;
}


int
ged_killrefs_core(struct ged *gedp, int argc, const char *argv[])
{
    int k;
    struct directory *dp;
    struct rt_db_internal intern;
    struct rt_comb_internal *comb;
    int nflag;
    int ret;
    static const char *usage = "[-n] object(s)";

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    if (!gedp->ged_internal_call) {
	/* initialize result */
	bu_vls_trunc(gedp->ged_result_str, 0);
    }

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return GED_HELP;
    }

    /* Process the -n option */
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n' && argv[1][2] == '\0') {
	nflag = 1;
	--argc;
	++argv;
    } else
	nflag = 0;

    if (!nflag && !gedp->ged_internal_call) {
	/*
	 * Erase drawn paths where argv[k] appears as a non-top-level
	 * path component.  Paths where argv[k] is the top-level drawn
	 * object are left intact because killrefs only removes references
	 * from parent combinations — the object itself is not deleted.
	 *
	 * Two passes: first collect matching paths (by value), then erase
	 * them, so the tree is not mutated during the collection walk.
	 */
	for (k = 1; k < argc; k++) {
	    struct bu_ptbl to_erase;
	    bu_ptbl_init(&to_erase, 8, "killrefs erase list");

	    struct _killrefs_ctx ctx;
	    ctx.name = argv[k];
	    ctx.to_erase = &to_erase;
	    bsg_view_obj_foreach_group(gedp, _killrefs_group_cb, &ctx);

	    for (size_t ei = 0; ei < BU_PTBL_LEN(&to_erase); ei++) {
		char *epath = (char *)BU_PTBL_GET(&to_erase, ei);
		struct db_full_path dfp;
		db_full_path_init(&dfp);
		if (db_string_to_path(&dfp, gedp->dbip, epath) == 0)
		    bsg_view_obj_erase_by_dbpath(gedp, &dfp);
		db_free_full_path(&dfp);
		bu_free(epath, "killrefs erase path");
	    }
	    bu_ptbl_free(&to_erase);
	}
    }

    ret = BRLCAD_OK;

    FOR_ALL_DIRECTORY_START(dp, gedp->dbip) {
	if (!(dp->d_flags & RT_DIR_COMB))
	    continue;

	if (rt_db_get_internal(&intern, dp, gedp->dbip, (fastf_t *)NULL) < 0) {
	    bu_vls_printf(gedp->ged_result_str, "rt_db_get_internal(%s) failure", dp->d_namep);
	    ret = BRLCAD_ERROR;
	    continue;
	}
	comb = (struct rt_comb_internal *)intern.idb_ptr;
	RT_CK_COMB(comb);

	for (k = 1; k < argc; k++) {
	    int code;

	    code = db_tree_rm_dbleaf(&(comb->tree), argv[k], nflag);
	    if (code == -1)
		continue;	/* not found */
	    if (code == -2)
		continue;	/* empty tree */
	    if (code < 0) {
		bu_vls_printf(gedp->ged_result_str, "ERROR: Failure deleting %s/%s\n", dp->d_namep, argv[k]);
		ret = BRLCAD_ERROR;
	    } else {
		if (nflag)
		    bu_vls_printf(gedp->ged_result_str, "%s ", dp->d_namep);
		else
		    bu_vls_printf(gedp->ged_result_str, "deleted %s/%s\n", dp->d_namep, argv[k]);
	    }
	}

	if (rt_db_put_internal(dp, gedp->dbip, &intern) < 0) {
	    bu_vls_printf(gedp->ged_result_str, "ERROR: Unable to write new combination into database.\n");
	    ret = BRLCAD_ERROR;
	    continue;
	}
    } FOR_ALL_DIRECTORY_END;

    /* Update references. */
    db_update_nref(gedp->dbip);

    return ret;
}

#include "../include/plugin.h"

#define GED_KILLREFS_COMMANDS(X, XID) \
    X(killrefs, ged_killrefs_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_KILLREFS_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_killrefs", 1, GED_KILLREFS_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
