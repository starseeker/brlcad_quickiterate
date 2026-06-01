/*                     F A C E T I Z E . C P P
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
/** @file libged/facetize.cpp
 *
 * The facetize command.
 *
 */

#include "common.h"

#include <charconv>
#include <sstream>
#include <string>
#include <vector>

#include "bu/app.h"
#include "bu/path.h"
#include "bu/opt.h"
#include "gcv/facetize.h"
#include "wdb.h"

#include "../ged_private.h"

#include "./ged_facetize.h"

void
_facetize_methods_help(struct ged *gedp)
{
    const struct gcv_facetize_method_info *methods = NULL;
    size_t method_cnt = gcv_facetize_methods(&methods);
    struct bu_vls desc = BU_VLS_INIT_ZERO;

    bu_vls_printf(gedp->ged_result_str, "Available BoT tessellation methods:");
    for (size_t i = 0; i < method_cnt; i++) {
	bu_vls_printf(gedp->ged_result_str, "%s%s", (i == 0) ? " " : ", ", methods[i].name);
    }
    bu_vls_printf(gedp->ged_result_str, "\n");

    gcv_facetize_describe_options(&desc);
    if (bu_vls_strlen(&desc)) {
	bu_vls_printf(gedp->ged_result_str, "\nMethod specific options:\n\n%s\n", bu_vls_cstr(&desc));
    }

    bu_vls_free(&desc);
}

struct _ged_facetize_state *
_ged_facetize_state_create()
{
    struct _ged_facetize_state *s = NULL;
    BU_GET(s, struct _ged_facetize_state);
    s->verbosity = 0;
    s->no_empty = 0;
    s->make_nmg = 0;
    s->nonovlp_brep = 0;
    s->no_fixup = 0;
    s->nmg_booleval = 0;
    s->use_variant_plan = 1;
    s->perturb_sa_tol  = 10.0;
    s->perturb_vol_tol = 10.0;

    s->wdir = NULL;

    BU_GET(s->log_file, struct bu_vls);
    bu_vls_init(s->log_file);

    s->lfile = NULL;

    BU_GET(s->wfile, struct bu_vls);
    bu_vls_init(s->wfile);

    BU_GET(s->bname, struct bu_vls);
    bu_vls_init(s->bname);

    s->regions = 0;
    s->resume = 0;
    s->in_place = 0;

    BU_GET(s->suffix, struct bu_vls);
    bu_vls_init(s->suffix);

    BU_GET(s->prefix, struct bu_vls);
    bu_vls_init(s->prefix);

    BU_GET(s->solid_suffix, struct bu_vls);
    bu_vls_init(s->solid_suffix);
    bu_vls_sprintf(s->solid_suffix, ".bot");

    s->max_time = 0;
    s->max_pnts = 0;

    s->tol = NULL;
    s->nonovlp_threshold = 0;

    s->gedp = NULL;

    s->variant_plan = NULL;

    return s;
}
void _ged_facetize_state_destroy(struct _ged_facetize_state *s)
{
    if (!s)
       	return;

    if (s->wdir)
	bu_free(s->wdir, "wdir");

    if (s->bname) {
	bu_vls_free(s->bname);
	BU_PUT(s->bname, struct bu_vls);
    }

    if (s->lfile) {
	fclose(s->lfile);
	s->lfile = NULL;
    }

    if (s->log_file) {
	bu_vls_free(s->log_file);
	BU_PUT(s->log_file, struct bu_vls);
    }

    if (s->wfile) {
	bu_vls_free(s->wfile);
	BU_PUT(s->wfile, struct bu_vls);
    }

    if (s->prefix) {
	bu_vls_free(s->prefix);
	BU_PUT(s->prefix, struct bu_vls);
    }

    if (s->suffix) {
	bu_vls_free(s->suffix);
	BU_PUT(s->suffix, struct bu_vls);
    }

    if (s->solid_suffix) {
	bu_vls_free(s->solid_suffix);
	BU_PUT(s->solid_suffix, struct bu_vls);
    }

    if (s->variant_plan) {
	delete (FacetizeVariantPlan *)s->variant_plan;
	s->variant_plan = NULL;
    }

    BU_PUT(s, struct _ged_facetize_state);
}

static int
_ged_facetize_validate_objects_cb(void *ctx, int argc, const char **argv, int newobj_cnt)
{
    return _ged_validate_objs_list((struct _ged_facetize_state *)ctx, argc, argv, newobj_cnt);
}

static int
_ged_facetize_nmg_eval_cb(void *ctx, int argc, const char **argv, const char *oname)
{
    return _ged_facetize_nmgeval((struct _ged_facetize_state *)ctx, argc, argv, oname);
}

static int
_ged_facetize_manifold_eval_cb(void *ctx, int argc, struct directory **dpa, const char *oname, int output_to_working, int cleanup)
{
    return _ged_facetize_booleval((struct _ged_facetize_state *)ctx, argc, dpa, oname, output_to_working ? true : false, cleanup ? true : false);
}

static void
_ged_facetize_summary_cb(void *ctx)
{
    facetize_primitives_summary((struct _ged_facetize_state *)ctx);
}

static void
_ged_facetize_cleanup_cb(void *ctx)
{
    struct _ged_facetize_state *s = (struct _ged_facetize_state *)ctx;
    if (s && s->wdir)
        bu_dirclear(s->wdir);
}

static void
_ged_facetize_object_callbacks_init(struct gcv_facetize_object_callbacks *callbacks)
{
    if (!callbacks)
	return;

    *callbacks = {};
    callbacks->validate_objects = _ged_facetize_validate_objects_cb;
    callbacks->nmg_eval = _ged_facetize_nmg_eval_cb;
    callbacks->manifold_eval = _ged_facetize_manifold_eval_cb;
    callbacks->primitive_summary = _ged_facetize_summary_cb;
    callbacks->cleanup = _ged_facetize_cleanup_cb;
}

int
_ged_facetize_objs(struct _ged_facetize_state *s, int argc, const char **argv)
{
    if (!s || !s->dbip)
        return BRLCAD_ERROR;

    RT_CHECK_DBI(s->dbip);

    struct gcv_facetize_object_callbacks callbacks;
    _ged_facetize_object_callbacks_init(&callbacks);

    return gcv_facetize_objects_to_db(s->dbip,
            argc,
            argv,
            s->in_place,
            s->make_nmg,
            s->nmg_booleval,
            &callbacks,
            (void *)s);
}

static int
_ged_facetize_execute(struct _ged_facetize_state *s, int argc, const char **argv)
{
    if (!s || !s->dbip)
	return BRLCAD_ERROR;

    RT_CHECK_DBI(s->dbip);

    struct gcv_facetize_db_opts opts;
    gcv_facetize_db_opts_default(&opts);
    opts.region_mode = s->regions;
    opts.in_place = s->in_place;
    opts.make_nmg = s->make_nmg;
    opts.nmg_booleval = s->nmg_booleval;
    opts.no_perturb = s->no_perturb;
    opts.verbosity = s->verbosity;
    opts.working_dir = s->wdir;
    opts.base_name = bu_vls_cstr(s->bname);
    opts.prefix = bu_vls_cstr(s->prefix);
    opts.suffix = bu_vls_cstr(s->suffix);

    struct gcv_facetize_db_callbacks callbacks = {};
    _ged_facetize_object_callbacks_init(&callbacks.objects);
    callbacks.object_data = (void *)s;

    void *region_ctx = NULL;
    if (s->regions) {
	region_ctx = _ged_facetize_region_context_create(s);
	if (!region_ctx)
	    return BRLCAD_ERROR;
	_ged_facetize_region_callbacks_init(&callbacks.regions);
	callbacks.region_data = region_ctx;
    }

    int ret = gcv_facetize_to_db(s->dbip, argc, argv, &opts, &callbacks);
    if (region_ctx)
	_ged_facetize_region_context_destroy(region_ctx);
    return ret;
}

extern "C" int
ged_facetize_core(struct ged *gedp, int argc, const char *argv[])
{
    int ret = BRLCAD_OK;
    static const char *usage = "Usage: facetize [options] [old_obj1 ...] [new_obj]\n";
    int print_help = 0;
    int need_help = 0;
    int quiet = 0;
    long verbosity = 0;
    int force_perturb = 0;
    int disable_perturb = 0;
    struct gcv_facetize_method_opts_state method_options;
    gcv_facetize_method_opts_state_init(&method_options);
    struct _ged_facetize_state *s = _ged_facetize_state_create();
    s->gedp = gedp;
    s->dbip = gedp->dbip;
    s->method_opts = &method_options;

    /* General options */
    struct bu_opt_desc d[24];
    BU_OPT(d[ 0], "h", "help",                                      "",                  NULL,           &print_help, "Print help and exit");
    BU_OPT(d[ 1], "v", "verbose",                                   "",  &bu_opt_incr_long,       &verbosity, "Verbose output (multiple flags increase verbosity)");
    BU_OPT(d[ 2], "q", "quiet",                                     "",                  NULL,                &quiet, "Suppress all output (overrides verbose flag)");
    BU_OPT(d[ 3], "n", "nmg-output",                                "",                  NULL,        &(s->make_nmg), "Create an N-Manifold Geometry (NMG) object (default is to create a triangular BoT mesh).  Note that this will disable most other processing options and may reduce the conversion success rate.");
    BU_OPT(d[ 4], "r", "regions",                                   "",                  NULL,         &(s->regions), "For combs, walk the trees and create new copies of the hierarchies with each region's CSG tree replaced by a facetized evaluation of that region.  By default, enables perturb methodology (can be disabled - see --no-perturb)");
    BU_OPT(d[ 5], "s", "suffix",                               "<str>",           &bu_opt_vls,             s->suffix, "When creating new objects for facetize outputs, use this suffix to avoid conflicts");
    BU_OPT(d[ 6], "p", "prefix",                               "<str>",           &bu_opt_vls,             s->prefix, "When creating new objects for facetize, use this prefix to avoid conflicts");
    BU_OPT(d[ 7],  "", "in-place",                                  "",                  NULL,        &(s->in_place), "Replace the specified object(s) with their facetizations. (Warning: this option changes pre-existing geometry!)");
    BU_OPT(d[ 8],  "", "max-time",                                 "#",           &bu_opt_int,        &(s->max_time), "Maximum time to spend per object (in seconds).  Default is method specific.  Note that specifying shorter times may cut off conversions (particularly using sampling methods) that could succeed with longer runtimes.  Per-method time limits can also be adjusted to allow longer runtimes on slower methods.");
    BU_OPT(d[ 9],  "", "max-pnts",                                 "#",           &bu_opt_int,        &(s->max_pnts), "Maximum number of pnts per object to use when applying ray sampling methods.");
    BU_OPT(d[10],  "", "resume",                                    "",                  NULL,          &(s->resume), "Resume an interrupted conversion");
    BU_OPT(d[11],  "", "methods",                          "m1,m2,...", &gcv_facetize_method_opts_opt_methods,        &method_options, "Specify methods to use when tessellating primitives into BoTs.");
    BU_OPT(d[12],  "", "method-opts",    "METHOD opt1=val opt2=val...",    &gcv_facetize_method_opts_opt_options,        &method_options, "For the specified method, set the specified options.");
    BU_OPT(d[13],  "", "no-empty",                                  "",                  NULL,        &(s->no_empty), "Do not output empty BoT objects if the boolean evaluation results in an empty solid.");
    BU_OPT(d[14],  "", "log-file",                        "<filename>",           &bu_opt_vls,           s->log_file, "Specify a location to use for the log file.");
    BU_OPT(d[15],  "", "nmg-booleval",                               "",                  NULL,       &s->nmg_booleval, "Use libnmg Boolean evaluation algorithm, even if we're producing a BoT.  Less robust, but if it succeeds it may produce cleaner output for coplanar inputs.");
    BU_OPT(d[16],  "", "disable-fixup",                             "",                  NULL,          &s->no_fixup, "Disable post-processing steps intended to improve generated meshes.");
    BU_OPT(d[17],  "", "perturb",                                   "",                  NULL,        &force_perturb, "Enable the coplanarity-avoidance perturbation step (overrides non -r option default, conflicts with --no-perturb).");
    BU_OPT(d[18],  "", "no-perturb",                                "",                  NULL,      &disable_perturb, "Disable the coplanarity-avoidance perturbation step (overrides -r option default, conflicts with --perturb).");
    BU_OPT(d[19], "B", "",                                          "",                  NULL,      &s->nonovlp_brep, "EXPERIMENTAL: non-overlapping facetization to BoT objects of union-only brep comb tree.");
    BU_OPT(d[20], "t", "threshold",                                "#",       &bu_opt_fastf_t, &s->nonovlp_threshold, "EXPERIMENTAL: max ovlp threshold length for -B mode.");
    BU_OPT(d[21],  "", "perturb-sa-tol",                           "#",       &bu_opt_fastf_t,   &s->perturb_sa_tol,  "Surface-area percentage threshold (0–100) that triggers the coplanarity-avoidance perturb retry when the CSG Crofton SA differs from the BoT SA by more than this amount. Default is 10.");
    BU_OPT(d[22],  "", "perturb-vol-tol",                          "#",       &bu_opt_fastf_t,   &s->perturb_vol_tol, "Volume percentage threshold (0–100) that triggers the coplanarity-avoidance perturb retry when the CSG Crofton volume differs from the BoT volume by more than this amount. Default is 10.");
    BU_OPT_NULL(d[23]);

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* skip command name argv[0] */
    argc-=(argc>0); argv+=(argc>0);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* parse standard options */
    struct bu_vls omsg = BU_VLS_INIT_ZERO;
    argc = bu_opt_parse(&omsg, argc, argv, d);
    if (argc < 0) {
	bu_vls_printf(gedp->ged_result_str, "option parsing failed: %s\n", bu_vls_cstr(&omsg));
	ret = BRLCAD_ERROR;
	bu_vls_free(&omsg);
	goto ged_facetize_memfree;
    }
    bu_vls_free(&omsg);

    // Sanity
    if (force_perturb && disable_perturb) {
    	bu_vls_printf(gedp->ged_result_str, "Can only specify one of --perturb or --no-perturb\n");
	ret = BRLCAD_ERROR;
	goto ged_facetize_memfree;
    }

    // Set no_perturb according to options.  Enable by default for -r, disable
    // if not doing -r (one large BoT, generally a less likely candidate for
    // worrying about volume/surf_area matching.)
    s->no_perturb = (s->regions) ? 0 : 1;
    if (disable_perturb)
	s->no_perturb = 1;
    if (force_perturb)
	s->no_perturb = 0;

    s->verbosity = (int)verbosity;

    // If we got a max-time top level arg, override any times that aren't specifically set
    // by method options.
    if (s->max_time) {
	std::vector<const char *> methods;
	struct bu_ptbl default_methods = BU_PTBL_INIT_ZERO;
	if (gcv_facetize_method_opts_method_count(&method_options)) {
	    for (size_t i = 0; i < gcv_facetize_method_opts_method_count(&method_options); i++) {
		const char *method = gcv_facetize_method_opts_method_name(&method_options, i);
		if (method)
		    methods.push_back(method);
	    }
	} else if (gcv_facetize_default_method_names(&default_methods) == BRLCAD_OK) {
	    for (size_t i = 0; i < BU_PTBL_LEN(&default_methods); i++) {
		const char *method = (const char *)BU_PTBL_GET(&default_methods, i);
		if (method)
		    methods.push_back(method);
	    }
	}
	for (size_t i = 0; i < methods.size(); i++) {
	    if (!gcv_facetize_method_opts_has_option(&method_options, methods[i], "max_time"))
		gcv_facetize_method_opts_set_option(&method_options, methods[i], "max_time", std::to_string(s->max_time).c_str());
	}
	gcv_facetize_free_method_names(&default_methods);
    }

    /* Sync -q and -v options */
    if (quiet)
	s->verbosity = -1;

    /* Don't allow incorrect type suffixes */
    if (s->make_nmg && BU_STR_EQUAL(bu_vls_cstr(s->solid_suffix), ".bot")) {
	bu_vls_sprintf(s->solid_suffix, ".nmg");
    }
    if (!s->make_nmg && BU_STR_EQUAL(bu_vls_cstr(s->solid_suffix), ".nmg")) {
	bu_vls_sprintf(s->solid_suffix, ".bot");
    }

    /* Check if we want/need help */
    need_help += (argc < 1);
    need_help += (argc < 2 && !s->in_place && !s->regions && !s->resume && !s->nonovlp_brep);
    if (print_help || need_help || argc < 1) {
	_ged_cmd_help(gedp, usage, d);
	_facetize_methods_help(gedp);
	ret = (need_help) ? BRLCAD_ERROR : BRLCAD_OK;
	goto ged_facetize_memfree;
    }

    /* Beyond this point, we're likely to need info on the cache directory. Generate some
     * paths and strings we will need. */
    {
	// Get the root filename
	char rfname[MAXPATHLEN];
	bu_file_realpath(gedp->dbip->dbi_filename, rfname);
	bu_path_component(s->bname, rfname, BU_PATH_BASENAME);

	// Hash the path string and construct a location in the cache directory
	unsigned long long hash_num = bu_data_hash((void *)bu_vls_cstr(s->bname), bu_vls_strlen(s->bname));
	struct bu_vls dname = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&dname, "facetize_%llu", hash_num);
	s->wdir = (char *)bu_calloc(MAXPATHLEN, sizeof(char), "wdir");
	bu_dir(s->wdir, MAXPATHLEN, BU_DIR_CACHE, bu_vls_cstr(&dname), NULL);

	// If we're starting over, clear the old working directory
	if (!s->resume && bu_file_directory(s->wdir)) {
	    bu_dirclear(s->wdir);
	}

	if (!bu_file_directory(s->wdir)) {
	    // Set up the directory
	    bu_mkdir(s->wdir);
	}

	if (!bu_vls_strlen(s->log_file)) {
	    char tmplfile[MAXPATHLEN];
	    bu_vls_sprintf(&dname, "facetize_%s.log", bu_vls_cstr(s->bname));
	    bu_dir(tmplfile, MAXPATHLEN, s->wdir, bu_vls_cstr(&dname), NULL);
	    bu_vls_sprintf(s->log_file, "%s", tmplfile);
	}
	bu_vls_free(&dname);

	s->lfile = fopen(bu_vls_cstr(s->log_file), "a");
	if (!s->lfile) {
	    bu_vls_printf(gedp->ged_result_str, "Unable to open log file %s for writing\n", bu_vls_cstr(s->log_file));
	    ret = BRLCAD_ERROR;
	    goto ged_facetize_memfree;
	}
    }

    /* If we're doing the experimental brep-only logic, it's a separate process */
    if (s->nonovlp_brep) {
	if (NEAR_ZERO(s->nonovlp_threshold, SMALL_FASTF)) {
	    bu_vls_printf(gedp->ged_result_str, "-B option requires a specified length threshold\n");
	    ret = BRLCAD_ERROR;
	    goto ged_facetize_memfree;
	}
	ret = _nonovlp_brep_facetize(s, argc, argv);
	goto ged_facetize_memfree;
    }

    ret = _ged_facetize_execute(s, argc, argv);

ged_facetize_memfree:
    _ged_facetize_state_destroy(s);
    gcv_facetize_method_opts_state_free(&method_options);

    return ret;
}

#include "../include/plugin.h"

#define GED_FACETIZE_COMMANDS(X, XID) \
    X(facetize, ged_facetize_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_FACETIZE_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_facetize", 1, GED_FACETIZE_COMMANDS)

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
