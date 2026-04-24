/*                       E D I T 2 . C P P
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
/** @file libged/edit2.cpp
 *
 * New-forms edit command (Phase 1).
 *
 * Implements a three-pass command-line parser:
 *
 *   Pass 1 — global opts  (-S/-f/-F/-i/-h/-v) harvested before the first
 *             geometry specifier or subcommand token.
 *
 *   Pass 2 — geometry specifiers collected into ged_edit_geom_spec entries.
 *             Each token is tested as: "." batch marker → URI → db_lookup.
 *             If no specifiers are found, the active selection state is used
 *             as a fallback.  Conflicts between an explicit specifier and the
 *             active selection are detected and reported here.
 *
 *   Pass 3 — subcommand dispatch: argv[0] names the operation; the rest are
 *             its arguments, forwarded to the appropriate ged_subcmd handler.
 *
 * All geometry-altering subcommands receive a ged_edit_ctx * (u_data).
 */

#include "common.h"

#include <climits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "./uri.hh"

#include "bu/cmd.h"
#include "bu/opt.h"
#include "bn/rand.h"

#include "../ged_private.h"
#include "../dbi.h"
#include "./ged_edit2.h"


/* ------------------------------------------------------------------ *
 * Helpers: build a ged_edit_geom_spec from a URI-parsed token
 * ------------------------------------------------------------------ */

/**
 * Attempt to resolve argv[i] as a geometry specifier via DbiState.
 * Fills *spec and returns true on success, false if the token does not
 * resolve to any known database object.
 *
 * Handles:
 *   "."         — batch marker (each object acts as its own reference)
 *   "name#frag" — URI with fragment (e.g. vertex key)
 *   "name?q"    — URI with query (e.g. wildcard feature set)
 *   "path"      — bare name or slash-separated path
 */
static bool
_resolve_geom_spec(ged_edit_geom_spec &spec, const char *token, DbiState *dbis)
{
    spec.raw = token;
    spec.is_batch = false;
    spec.dp = RT_DIR_NULL;

    /* Batch marker */
    if (BU_STR_EQUAL(token, ".")) {
	spec.is_batch = true;
	/* Batch marker is valid without a db lookup */
	return true;
    }

    /* Try URI parse: prefix with "g:" so the class sees a scheme */
    std::string path_str;
    try {
	uri obj_uri(std::string("g:") + std::string(token));
	path_str = obj_uri.get_path();

	if (obj_uri.get_fragment().length() > 0)
	    spec.fragment = obj_uri.get_fragment();
	if (obj_uri.get_query().length() > 0)
	    spec.query = obj_uri.get_query();

	if (path_str.empty())
	    path_str = token;

    } catch (std::invalid_argument &) {
	path_str = token;
    }

    spec.path = path_str;
    spec.hashes = dbis->digest_path(path_str.c_str());

    if (spec.hashes.empty())
	return false;

    /* Single-element path — get the head dp */
    if (spec.hashes.size() == 1)
	spec.dp = dbis->get_hdp(spec.hashes[0]);

    /* Multi-element path (comb instance) — dp stays RT_DIR_NULL */

    return (spec.hashes.size() > 1) || (spec.dp != RT_DIR_NULL);
}


/* ================================================================== *
 * Subcommand implementations
 * ================================================================== */

// Rotate command
class cmd_rotate : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] rotate X Y Z"); }
	std::string purpose() { return std::string("rotate specified primitive or comb instance"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_rotate edit_rotate_cmd;

int
cmd_rotate::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;

    if (argc < 3 || !argv) {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}


// Tra command
class cmd_tra : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] tra X Y Z"); }
	std::string purpose() { return std::string("translate specified primitive or comb instance relative to its current position"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_tra edit_tra_cmd;

int
cmd_tra::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;

    if (argc < 3 || !argv) {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}


// Translate command
class cmd_translate : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] translate X Y Z"); }
	std::string purpose() { return std::string("translate specified primitive or comb instance to the specified absolute position"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_translate edit_translate_cmd;

int
cmd_translate::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;

    if (argc < 3 || !argv) {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}


// Scale command
class cmd_scale : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] scale factor"); }
	std::string purpose() { return std::string("scale specified primitive or comb instance by the specified factor (must be greater than 0)"); }
	int exec(struct ged *, void *, int, const char **);
};
static cmd_scale edit_scale_cmd;

int
cmd_scale::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    struct ged_edit_ctx *ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;

    if (argc < 3 || !argv) {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}


// Perturb command
class cmd_perturb : public ged_subcmd {
    public:
	std::string usage()   { return std::string("edit [options] [geometry] perturb factor"); }
	std::string purpose() { return std::string("perturb primitive or primitives below comb by the specified factor (must be greater than 0)"); }
	int exec(struct ged *, void *, int, const char **);
	struct ged_edit_ctx *ctx;
    private:
	int dp_perturb(struct directory *dp);
	fastf_t factor = 0;
};
static cmd_perturb edit_perturb_cmd;

int
cmd_perturb::dp_perturb(struct directory *dp)
{
    fastf_t lfactor = factor + factor*0.1*bn_rand_half(ctx->prand);
    bu_log("%s: %g\n", dp->d_namep, lfactor);
    struct rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    struct db_i *dbip = ctx->gedp->dbip;
    if (rt_db_get_internal(&intern, dp, dbip, NULL) < 0) {
	bu_log("rt_db_get_internal failed for %s\n", dp->d_namep);
	return BRLCAD_ERROR;
    }

    if (!intern.idb_meth || !intern.idb_meth->ft_perturb) {
	return BRLCAD_ERROR;
    }

    struct rt_db_internal *pintern;
    if (intern.idb_meth->ft_perturb(&pintern, &intern, 1, lfactor) != BRLCAD_OK) {
	bu_log("librt perturbation failed for %s\n", dp->d_namep);
	return BRLCAD_ERROR;
    }
    if (!pintern) {
	bu_log("librt perturbation failed for %s\n", dp->d_namep);
	return BRLCAD_ERROR;
    }

    std::string oname(dp->d_namep);
    db_delete(dbip, dp);
    db_dirdelete(dbip, dp);
    struct directory *ndp = db_diradd(dbip, oname.c_str(), RT_DIR_PHONY_ADDR, 0, RT_DIR_SOLID, (void *)&pintern->idb_type);
    if (ndp == RT_DIR_NULL) {
	bu_log("Cannot add %s to directory\n", oname.c_str());
	rt_db_free_internal(pintern);
	return BRLCAD_ERROR;
    }

    if (rt_db_put_internal(ndp, dbip, pintern) < 0) {
	bu_log("Failed to write %s to database\n", oname.c_str());
	rt_db_free_internal(pintern);
	return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}

int
cmd_perturb::exec(struct ged *gedp, void *u_data, int argc, const char **argv)
{
    if (!gedp || !u_data || !argc || !argv)
	return BRLCAD_ERROR;

    ctx = (struct ged_edit_ctx *)u_data;
    if (ctx->dp == RT_DIR_NULL)
	return BRLCAD_ERROR;

    argc--; argv++;

    if (argc < 1 || !argv) {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }

    if (bu_opt_fastf_t(NULL, 1, argv, (void *)&factor) != 1) {
	bu_vls_printf(gedp->ged_result_str, "%s\n", usage().c_str());
	return BRLCAD_ERROR;
    }
    if (NEAR_ZERO(factor, SMALL_FASTF))
	return BRLCAD_OK;

    struct bu_ptbl objs = BU_PTBL_INIT_ZERO;
    if (db_search(&objs, DB_SEARCH_RETURN_UNIQ_DP, "-type shape", 1, &ctx->dp, ctx->gedp->dbip, NULL, NULL, NULL) < 0) {
	bu_vls_printf(gedp->ged_result_str, "search error\n");
	return BRLCAD_ERROR;
    }
    if (!BU_PTBL_LEN(&objs)) {
	bu_vls_printf(gedp->ged_result_str, "no solids\n");
	return BRLCAD_OK;
    }

    int ret = BRLCAD_OK;
    for (size_t i = 0; i < BU_PTBL_LEN(&objs); i++) {
	struct directory *odp = (struct directory *)BU_PTBL_GET(&objs, i);
	int oret = dp_perturb(odp);
	if (oret != BRLCAD_OK)
	    ret = BRLCAD_ERROR;
    }
    bu_ptbl_free(&objs);

    DbiState *dbis = (DbiState *)ctx->gedp->dbi_state;
    dbis->update();

    return ret;
}


/* ================================================================== *
 * Main entry point — three-pass parser
 * ================================================================== */

extern "C" int
ged_edit2_core(struct ged *gedp, int argc, const char *argv[])
{
    int help = 0;

    /* ---- Initialise context ---------------------------------------- */
    struct ged_edit_ctx ctx;
    ctx.gedp         = gedp;
    ctx.verbosity    = 0;
    ctx.prand        = NULL;
    ctx.flag_S       = 0;
    ctx.flag_f       = 0;
    ctx.flag_F       = 0;
    ctx.flag_i       = 0;
    ctx.from_selection = false;
    ctx.has_conflict = false;
    ctx.dp           = RT_DIR_NULL;
    bn_rand_init(ctx.prand, 0);

    bu_vls_trunc(gedp->ged_result_str, 0);

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);

    /* Skip past the "edit" command name */
    argc--; argv++;

    /* ---- Build subcommand map --------------------------------------- */
    std::map<std::string, ged_subcmd *> edit_cmds;
    edit_cmds["rot"]       = &edit_rotate_cmd;
    edit_cmds["rotate"]    = &edit_rotate_cmd;
    edit_cmds["tra"]       = &edit_tra_cmd;
    edit_cmds["translate"] = &edit_translate_cmd;
    edit_cmds["sca"]       = &edit_scale_cmd;
    edit_cmds["scale"]     = &edit_scale_cmd;
    edit_cmds["perturb"]   = &edit_perturb_cmd;

    /* ---- Global option descriptors --------------------------------- */
    struct bu_opt_desc d[8];
    BU_OPT(d[0], "h", "help",         "",  NULL, &help,        "Print help");
    BU_OPT(d[1], "v", "verbose",      "",  NULL, &ctx.verbosity,"Verbose output");
    BU_OPT(d[2], "S", "selection",    "",  NULL, &ctx.flag_S,  "Operate on selection (ignore cmd-line specifier)");
    BU_OPT(d[3], "f", "force",        "",  NULL, &ctx.flag_f,  "Force: apply op, write to disk, clear conflict");
    BU_OPT(d[4], "F", "abandon",      "",  NULL, &ctx.flag_F,  "Abandon: discard intermediate state, use on-disk");
    BU_OPT(d[5], "i", "intermediate", "",  NULL, &ctx.flag_i,  "Intermediate: apply to temp buffer only (no disk write)");
    BU_OPT_NULL(d[6]);

    const char *bargs_help = "[options] <geometry_specifier> subcommand [args]";

    if (!argc) {
	_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds, "edit", bargs_help, 0, NULL);
	return BRLCAD_OK;
    }

    /* Note whether the first token looks like an option.  If so we
     * can't reliably distinguish option errors from bad geometry specs. */
    bool maybe_opts = (argv[0][0] == '-');

    /* ---- Pass 1: find positions of first geometry spec and subcommand
     *              so we know how many leading tokens to feed to
     *              bu_opt_parse as global options.                      */
    DbiState *dbis = (DbiState *)gedp->dbi_state;

    int geom_pos = INT_MAX;
    int cmd_pos  = INT_MAX;
    std::vector<unsigned long long> gs;

    for (int i = 0; i < argc; i++) {
	/* Check if this token matches a known subcommand name */
	if (edit_cmds.find(std::string(argv[i])) != edit_cmds.end()) {
	    if (cmd_pos == INT_MAX)
		cmd_pos = i;
	    break;
	}

	/* Try to resolve as a geometry specifier */
	ged_edit_geom_spec spec;
	if (_resolve_geom_spec(spec, argv[i], dbis)) {
	    if (geom_pos == INT_MAX) {
		geom_pos = i;
		gs = spec.hashes;
	    }
	    break;
	}
    }

    /* With no geometry or command found yet — all remaining tokens are
     * candidates for options.  Parse them all: if -h is among them, print
     * help and return OK; otherwise report the first token as invalid. */
    if (geom_pos == INT_MAX && cmd_pos == INT_MAX) {
	if (maybe_opts) {
	    struct bu_vls opterrs = BU_VLS_INIT_ZERO;
	    bu_opt_parse(&opterrs, argc, argv, d);
	    bu_vls_free(&opterrs);
	    if (help) {
		_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
		    "edit", bargs_help, 0, NULL);
		return BRLCAD_OK;
	    }
	    _ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
		"edit", bargs_help, 0, NULL);
	} else {
	    bu_vls_printf(gedp->ged_result_str,
		"Invalid geometry specifier: %s\n", argv[0]);
	}
	return BRLCAD_ERROR;
    }

    /* The option prefix is everything before the first geom or cmd token */
    int opt_prefix_len = (geom_pos < cmd_pos) ? geom_pos : cmd_pos;

    /* Parse global options from the prefix */
    if (opt_prefix_len > 0) {
	struct bu_vls opterrs = BU_VLS_INIT_ZERO;
	int opt_ret = bu_opt_parse(&opterrs, opt_prefix_len, argv, d);
	if (opt_ret < 0) {
	    bu_vls_printf(gedp->ged_result_str, "%s", bu_vls_cstr(&opterrs));
	    _ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
		"edit", bargs_help, 0, NULL);
	    bu_vls_free(&opterrs);
	    return BRLCAD_ERROR;
	}
	bu_vls_free(&opterrs);

	/* Shift remaining tokens to front */
	int remaining = argc - opt_prefix_len;
	for (int i = 0; i < remaining; i++)
	    argv[i] = argv[opt_prefix_len + i];
	argc -= opt_prefix_len;

	/* Adjust positions after shift */
	if (geom_pos != INT_MAX) geom_pos -= opt_prefix_len;
	if (cmd_pos  != INT_MAX) cmd_pos  -= opt_prefix_len;
    }

    /* Handle -h after option processing */
    if (help) {
	const char *cmd_name_for_help = (cmd_pos != INT_MAX) ? argv[cmd_pos] : "edit";
	_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
	    cmd_name_for_help, bargs_help, 0, NULL);
	return BRLCAD_OK;
    }

    /* Sanity: geometry must come before command if both present */
    if (geom_pos != INT_MAX && cmd_pos != INT_MAX &&
	    (geom_pos > cmd_pos || cmd_pos != geom_pos + 1)) {
	_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
	    "edit", bargs_help, 0, NULL);
	return BRLCAD_ERROR;
    }

    /* ---- Pass 2: collect geometry specifiers ----------------------- */

    /* Re-resolve the geometry specifier(s) into ged_edit_geom_spec entries.
     * The current implementation collects a single specifier (the one at
     * geom_pos).  Multi-object editing (dot batch, multiple paths) is
     * supported structurally but dispatched one-at-a-time. */
    const char *geom_str = NULL;
    if (geom_pos != INT_MAX) {
	geom_str = argv[geom_pos];
	ged_edit_geom_spec spec;
	if (_resolve_geom_spec(spec, geom_str, dbis)) {
	    ctx.geom_specs.push_back(spec);
	}
    }

    /* Selection fallback: if no explicit specifier was given, use the
     * active "default" selection state. */
    if (ctx.geom_specs.empty() && !ctx.flag_F) {
	std::vector<BSelectState *> ss = dbis->get_selected_states("default");
	if (!ss.empty()) {
	    std::vector<std::string> sel_paths = ss[0]->list_selected_paths();
	    for (const std::string &spath : sel_paths) {
		ged_edit_geom_spec spec;
		if (_resolve_geom_spec(spec, spath.c_str(), dbis))
		    ctx.geom_specs.push_back(spec);
	    }
	    if (!ctx.geom_specs.empty())
		ctx.from_selection = true;
	}
    }

    /* Selection conflict arbiter: explicit specifier present AND the same
     * object is also in the active selection → require an arbiter flag. */
    if (!ctx.geom_specs.empty() && !ctx.from_selection && !ctx.flag_S &&
	    !ctx.flag_f && !ctx.flag_F && !ctx.flag_i) {
	std::vector<BSelectState *> ss = dbis->get_selected_states("default");
	if (!ss.empty()) {
	    std::vector<std::string> sel_paths = ss[0]->list_selected_paths();
	    for (const auto &gspec : ctx.geom_specs) {
		for (const std::string &spath : sel_paths) {
		    if (gspec.path == spath) {
			ctx.has_conflict = true;
			bu_vls_printf(gedp->ged_result_str,
			    "Conflict: \"%s\" has both an explicit command-line "
			    "specifier and an active selection.\n"
			    "Use -S to operate on the selection, -f to force "
			    "a disk write, -F to abandon the intermediate "
			    "state, or -i to edit the temp buffer.\n",
			    gspec.path.c_str());
			return BRLCAD_ERROR;
		    }
		}
	    }
	}
    }

    /* If -S flag is set, discard explicit specifiers and use selection */
    if (ctx.flag_S) {
	ctx.geom_specs.clear();
	std::vector<BSelectState *> ss = dbis->get_selected_states("default");
	if (!ss.empty()) {
	    std::vector<std::string> sel_paths = ss[0]->list_selected_paths();
	    for (const std::string &spath : sel_paths) {
		ged_edit_geom_spec spec;
		if (_resolve_geom_spec(spec, spath.c_str(), dbis))
		    ctx.geom_specs.push_back(spec);
	    }
	    ctx.from_selection = true;
	}
    }

    /* Set convenience dp from first resolved specifier */
    if (!ctx.geom_specs.empty()) {
	ctx.dp = ctx.geom_specs[0].dp;
    }

    /* Advance past the geometry token */
    if (geom_str) {
	argc--; argv++;
    }

    /* ---- Pass 3: subcommand dispatch ------------------------------- */

    if (!argc || !argv[0]) {
	if (!ctx.geom_specs.empty() || ctx.from_selection) {
	    /* Object specified but no subcommand */
	    bu_vls_printf(gedp->ged_result_str,
		"No subcommand specified for \"%s\"\n",
		geom_str ? geom_str : "(selection)");
	} else {
	    _ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
		"edit", bargs_help, 0, NULL);
	}
	return BRLCAD_ERROR;
    }

    std::string cmd_str(argv[0]);
    auto e_it = edit_cmds.find(cmd_str);
    if (e_it == edit_cmds.end()) {
	bu_vls_printf(gedp->ged_result_str,
	    "Unknown subcommand: %s\n", argv[0]);
	_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
	    "edit", bargs_help, 0, NULL);
	return BRLCAD_ERROR;
    }

    /* Must have a geometry specifier before dispatching */
    if (ctx.geom_specs.empty()) {
	bu_vls_printf(gedp->ged_result_str,
	    "No valid geometry specifier found; nothing to edit.\n");
	_ged_subcmd2_help(gedp, (struct bu_opt_desc *)d, edit_cmds,
	    "edit", bargs_help, 0, NULL);
	return BRLCAD_ERROR;
    }

    return e_it->second->exec(gedp, &ctx, argc, argv);
}



// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8


