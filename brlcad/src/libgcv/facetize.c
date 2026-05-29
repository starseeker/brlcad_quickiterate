/*                      F A C E T I Z E . C
 * BRL-CAD
 *
 * Copyright (c) 2015-2026 United States Government as represented by
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
/** @file facetize.c
 *
 * Convenience functions for facetization.
 */


#include "common.h"

#include <string.h>

#include "bu/parallel.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "gcv/facetize.h"
#include "gcv/util.h"
#include "rt/conv.h"
#include "rt/db5.h"
#include "rt/db_internal.h"
#include "rt/wdb.h"
#include "rt/global.h"
#include "rt/primitives/bot.h"
#include "rt/functab.h"
#include "rt/nmg_conv.h"


static const struct gcv_facetize_option_desc _gcv_facetize_sample_options[] = {
    {"feature_scale", GCV_FACETIZE_OPT_FASTF, "0.15", "Percentage of average sampled thickness to use for sampling feature size."},
    {"feature_size", GCV_FACETIZE_OPT_FASTF, "0.0", "Explicit sampling feature length; overrides feature_scale."},
    {"d_feature_size", GCV_FACETIZE_OPT_FASTF, "0.0", "Initial decimation feature length; default handling is method specific."},
    {"max_sample_time", GCV_FACETIZE_OPT_INT, "30", "Maximum time to allow point sampling to continue."},
    {"max_pnts", GCV_FACETIZE_OPT_INT, "200000", "Maximum number of points to sample."}
};


static const struct gcv_facetize_option_desc _gcv_facetize_nmg_options[] = {
    {"max_time", GCV_FACETIZE_OPT_INT, "30", "Maximum overall run time for object conversion."},
    {"plate_max_time", GCV_FACETIZE_OPT_INT, "1200", "Maximum run time for plate-mode BoT conversion."},
    {"nmg_debug", GCV_FACETIZE_OPT_INT, "0x00000000", "NMG debugging flag."},
    {"tol_abs", GCV_FACETIZE_OPT_FASTF, "0", "Absolute tessellation distance tolerance."},
    {"tol_norm", GCV_FACETIZE_OPT_FASTF, "0", "Normal tessellation tolerance."},
    {"tol_rel", GCV_FACETIZE_OPT_FASTF, "0.01", "Relative tessellation distance tolerance."}
};


static const struct gcv_facetize_option_desc _gcv_facetize_cm_options[] = {
    {"feature_scale", GCV_FACETIZE_OPT_FASTF, "0.15", "Percentage of average sampled thickness to use for sampling feature size."},
    {"feature_size", GCV_FACETIZE_OPT_FASTF, "0.0", "Explicit sampling feature length; overrides feature_scale."},
    {"d_feature_size", GCV_FACETIZE_OPT_FASTF, "0.0", "Initial decimation feature length; default handling is method specific."},
    {"max_sample_time", GCV_FACETIZE_OPT_INT, "30", "Maximum time to allow point sampling to continue."},
    {"max_pnts", GCV_FACETIZE_OPT_INT, "200000", "Maximum number of points to sample."},
    {"max_cycle_time", GCV_FACETIZE_OPT_INT, "30", "Maximum time to take for one processing cycle."},
    {"max_time", GCV_FACETIZE_OPT_INT, "600", "Maximum overall run time for object conversion."}
};


static const struct gcv_facetize_option_desc _gcv_facetize_spsr_options[] = {
    {"feature_scale", GCV_FACETIZE_OPT_FASTF, "0.15", "Percentage of average sampled thickness to use for sampling feature size."},
    {"feature_size", GCV_FACETIZE_OPT_FASTF, "0.0", "Explicit sampling feature length; overrides feature_scale."},
    {"d_feature_size", GCV_FACETIZE_OPT_FASTF, "0.0", "Initial decimation feature length; default handling is method specific."},
    {"max_sample_time", GCV_FACETIZE_OPT_INT, "30", "Maximum time to allow point sampling to continue."},
    {"max_pnts", GCV_FACETIZE_OPT_INT, "200000", "Maximum number of points to sample."},
    {"depth", GCV_FACETIZE_OPT_INT, "8", "Maximum reconstruction depth."},
    {"interpolate", GCV_FACETIZE_OPT_FASTF, "2.0", "Point interpolation weight for reconstruction accuracy."},
    {"max_time", GCV_FACETIZE_OPT_INT, "600", "Maximum overall run time for object conversion."},
    {"samples_per_node", GCV_FACETIZE_OPT_FASTF, "1.5", "Samples required in a reconstruction cell before refinement."}
};


static const struct gcv_facetize_method_info _gcv_facetize_methods[] = {
    {"NMG", "N-Manifold Geometry tessellation method.", "BRL-CAD primitives and BoTs", 0, GCV_FACETIZE_CAP_SUBPROCESS_RECOMMENDED | GCV_FACETIZE_CAP_REPAIRS_BOTS | GCV_FACETIZE_CAP_DETERMINISTIC | GCV_FACETIZE_CAP_RESUMABLE, _gcv_facetize_nmg_options, sizeof(_gcv_facetize_nmg_options) / sizeof(_gcv_facetize_nmg_options[0])},
    {"CM", "Continuation Method/Bloomenthal polygonizer.", "Sampled implicit geometry", 1, GCV_FACETIZE_CAP_SUBPROCESS_RECOMMENDED | GCV_FACETIZE_CAP_CONSUMES_POINT_SAMPLES | GCV_FACETIZE_CAP_RESUMABLE, _gcv_facetize_cm_options, sizeof(_gcv_facetize_cm_options) / sizeof(_gcv_facetize_cm_options[0])},
    {"SPSR", "Screened Poisson Surface Reconstruction.", "Sampled point clouds", 2, GCV_FACETIZE_CAP_SUBPROCESS_RECOMMENDED | GCV_FACETIZE_CAP_CONSUMES_POINT_SAMPLES | GCV_FACETIZE_CAP_RESUMABLE, _gcv_facetize_spsr_options, sizeof(_gcv_facetize_spsr_options) / sizeof(_gcv_facetize_spsr_options[0])}
};


static const struct gcv_facetize_step_info _gcv_facetize_boolean_evaluators[] = {
    {"Manifold", "Manifold-backed triangular boolean evaluation.", 0, GCV_FACETIZE_CAP_DETERMINISTIC, NULL, 0},
    {"NMG", "libnmg boolean evaluation.", 1, GCV_FACETIZE_CAP_DETERMINISTIC, NULL, 0}
};


static const struct gcv_facetize_step_info _gcv_facetize_postprocess_steps[] = {
    {"bot-fixup", "BoT post-processing and mesh cleanup.", 0, GCV_FACETIZE_CAP_REPAIRS_BOTS, NULL, 0},
    {"perturb", "Coplanarity-avoidance perturbation retry planning.", 1, GCV_FACETIZE_CAP_DETERMINISTIC, NULL, 0}
};


void
gcv_facetize_opts_default(struct gcv_facetize_opts *opts)
{
    if (!opts)
	return;

    memset(opts, 0, sizeof(*opts));
    opts->output_mode = GCV_FACETIZE_OUTPUT_SINGLE_BOT;
    opts->boolean_engine = GCV_FACETIZE_BOOL_MANIFOLD;
    opts->subprocess = 1;
    opts->max_time = 0;
    opts->per_method_max_time = 0;
    opts->no_empty = 0;
    opts->disable_fixup = 0;
    opts->perturb = 0;
    opts->perturb_volume_threshold = 10.0;
    opts->perturb_surface_area_threshold = 10.0;
    opts->max_sampled_points = 200000;
    bu_avs_init_empty(&opts->method_options);
}


size_t
gcv_facetize_methods(const struct gcv_facetize_method_info **methods)
{
    if (methods)
	*methods = _gcv_facetize_methods;
    return sizeof(_gcv_facetize_methods) / sizeof(_gcv_facetize_methods[0]);
}


const struct gcv_facetize_method_info *
gcv_facetize_method(const char *name)
{
    size_t i = 0;
    size_t method_cnt = gcv_facetize_methods(NULL);

    if (!name)
	return NULL;

    for (i = 0; i < method_cnt; i++)
	if (BU_STR_EQUAL(name, _gcv_facetize_methods[i].name))
	    return &_gcv_facetize_methods[i];

    return NULL;
}


size_t
gcv_facetize_method_options(const char *method, const struct gcv_facetize_option_desc **options)
{
    const struct gcv_facetize_method_info *minfo = gcv_facetize_method(method);

    if (!minfo) {
	if (options)
	    *options = NULL;
	return 0;
    }

    if (options)
	*options = minfo->options;
    return minfo->option_count;
}


size_t
gcv_facetize_resolve_methods(const struct gcv_facetize_opts *opts, const struct gcv_facetize_method_info **methods, size_t max_methods)
{
    size_t i = 0;
    size_t cnt = 0;

    if (opts && opts->method_count && opts->methods) {
	for (i = 0; i < opts->method_count; i++) {
	    const struct gcv_facetize_method_info *minfo = gcv_facetize_method(opts->methods[i]);
	    if (!minfo)
		continue;
	    if (methods && cnt < max_methods)
		methods[cnt] = minfo;
	    cnt++;
	}
	return cnt;
    }

    cnt = gcv_facetize_methods(NULL);
    if (methods && max_methods) {
	size_t copy_cnt = (cnt < max_methods) ? cnt : max_methods;
	for (i = 0; i < copy_cnt; i++)
	    methods[i] = &_gcv_facetize_methods[i];
    }
    return cnt;
}


size_t
gcv_facetize_boolean_evaluators(const struct gcv_facetize_step_info **evaluators)
{
    if (evaluators)
	*evaluators = _gcv_facetize_boolean_evaluators;
    return sizeof(_gcv_facetize_boolean_evaluators) / sizeof(_gcv_facetize_boolean_evaluators[0]);
}


size_t
gcv_facetize_postprocess_steps(const struct gcv_facetize_step_info **steps)
{
    if (steps)
	*steps = _gcv_facetize_postprocess_steps;
    return sizeof(_gcv_facetize_postprocess_steps) / sizeof(_gcv_facetize_postprocess_steps[0]);
}


void
gcv_facetize_describe_options(struct bu_vls *description)
{
    size_t i = 0;
    size_t j = 0;
    const struct gcv_facetize_method_info *methods = NULL;
    size_t method_cnt = gcv_facetize_methods(&methods);

    if (!description)
	return;

    bu_vls_printf(description, "Facetize primitive conversion methods:\n");
    for (i = 0; i < method_cnt; i++) {
	bu_vls_printf(description, "\n%s - %s\n", methods[i].name, methods[i].description);
	for (j = 0; j < methods[i].option_count; j++) {
	    const struct gcv_facetize_option_desc *opt = &methods[i].options[j];
	    bu_vls_printf(description, "  %s (default %s): %s\n", opt->name, opt->default_value ? opt->default_value : "", opt->description);
	}
    }
}


static union tree *
_gcv_facetize_region_end(struct db_tree_state *tree_state,
			 const struct db_full_path *path, union tree *current_tree, void *client_data)
{
    union tree **facetize_tree;

    RT_CK_DBTS(tree_state);
    RT_CK_FULL_PATH(path);
    RT_CK_TREE(current_tree);

    facetize_tree = (union tree **)client_data;

    if (current_tree->tr_op == OP_NOP)
	return current_tree;

    if (*facetize_tree) {
	union tree *temp;

	RT_CK_TREE(*facetize_tree);

	BU_ALLOC(temp, union tree);
	RT_TREE_INIT(temp);
	temp->tr_op = OP_UNION;
	temp->tr_b.tb_regionp = REGION_NULL;
	temp->tr_b.tb_left = *facetize_tree;
	temp->tr_b.tb_right = current_tree;
	*facetize_tree = temp;
    } else {
	*facetize_tree = current_tree;
    }

    /* Tree has been saved, and will be freed later */
    return TREE_NULL;
}


static struct rt_bot_internal *
_gcv_facetize_cleanup(struct model *nmg_model, union tree *facetize_tree)
{
    if (nmg_model) {
	NMG_CK_MODEL(nmg_model);
	nmg_km(nmg_model);
    }

    if (facetize_tree) {
	RT_CK_TREE(facetize_tree);
	db_free_tree(facetize_tree);
    }

    return NULL;
}


static void
_gcv_facetize_free_bot(struct rt_bot_internal *bot)
{
    /* fill in an rt_db_internal so we can free it */
    struct rt_db_internal internal;

    RT_BOT_CK_MAGIC(bot);

    RT_DB_INTERNAL_INIT(&internal);
    internal.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    internal.idb_minor_type = ID_BOT;
    internal.idb_meth = &OBJ[ID_BOT];
    internal.idb_ptr = bot;

    rt_db_free_internal(&internal);
}


static void
_gcv_optimize_model(struct model *nmg_model)
{
    struct nmgregion *current_region;

    NMG_CK_MODEL(nmg_model);

    for (BU_LIST_FOR(current_region, nmgregion, &nmg_model->r_hd)) {
	struct shell *current_shell;

	NMG_CK_REGION(current_region);
	current_shell = BU_LIST_FIRST(shell, &current_region->s_hd);

	while (BU_LIST_NOT_HEAD(&current_shell->l, &current_region->s_hd)) {
	    struct shell *next_shell;

	    NMG_CK_SHELL(current_shell);
	    next_shell = BU_LIST_PNEXT(shell, &current_shell->l);

	    if (nmg_kill_cracks(current_shell))
		nmg_ks(current_shell);

	    current_shell = next_shell;
	}
    }

    nmg_kill_zero_length_edgeuses(nmg_model);
}


struct rt_bot_internal *
gcv_facetize(struct db_i *db, const struct db_full_path *path,
	     const struct bn_tol *tol, const struct bg_tess_tol *tess_tol, struct bu_list *vlfree)
{
    union tree *facetize_tree;
    struct model *nmg_model;

    /* volatile to silence warnings over longjmp */
    struct nmgregion * volatile current_region = NULL;
    struct rt_bot_internal * volatile result = NULL;
    struct shell * volatile current_shell = NULL;

    RT_CK_DBI(db);
    RT_CK_FULL_PATH(path);
    BN_CK_TOL(tol);
    BG_CK_TESS_TOL(tess_tol);

    {
	char * const str_path = db_path_to_string(path);
	struct db_tree_state initial_tree_state;
	RT_DBTS_INIT(&initial_tree_state);
	initial_tree_state.ts_tol = tol;
	initial_tree_state.ts_ttol = tess_tol;
	initial_tree_state.ts_m = &nmg_model;

	facetize_tree = NULL;
	nmg_model = nmg_mm();

	if (db_walk_tree(db, 1, (const char **)&str_path, 1, &initial_tree_state, NULL,
			 _gcv_facetize_region_end, rt_booltree_leaf_tess, &facetize_tree)) {
	    bu_log("gcv_facetize(): error in db_walk_tree()\n");
	    bu_free(str_path, "str_path");
	    return _gcv_facetize_cleanup(nmg_model, facetize_tree);
	}

	bu_free(str_path, "str_path");
    }

    if (!facetize_tree)
	return _gcv_facetize_cleanup(nmg_model, facetize_tree);

    /* Now, evaluate the boolean tree into ONE region */
    if (!BU_SETJUMP) {
	/* try */
	if (nmg_boolean(facetize_tree, nmg_model, vlfree, tol)) {
	    BU_UNSETJUMP;
	    return _gcv_facetize_cleanup(nmg_model, facetize_tree);
	}
    } else {
	/* catch */
	BU_UNSETJUMP;
	bu_log("gcv_facetize(): boolean evaluation failed\n");
	return _gcv_facetize_cleanup(nmg_model, facetize_tree);
    }

    BU_UNSETJUMP;

    /* New region remains part of this nmg "model" */
    NMG_CK_REGION(facetize_tree->tr_d.td_r);

    _gcv_optimize_model(nmg_model);

    for (BU_LIST_FOR(current_region, nmgregion, &nmg_model->r_hd)) {
	current_shell = NULL;

	NMG_CK_REGION(current_region);

	for (BU_LIST_FOR(current_shell, shell, &current_region->s_hd)) {
	    NMG_CK_SHELL(current_shell);

	    if (!BU_SETJUMP) {
		/* try */
		if (!result)
		    result = nmg_bot(current_shell, vlfree, tol);
		else {
		    struct rt_bot_internal *bots[2];
		    bots[0] = result;
		    bots[1] = nmg_bot(current_shell, vlfree, tol);
		    result = rt_bot_merge(sizeof(bots) / sizeof(bots[0]),
					  (const struct rt_bot_internal * const *)bots);
		    _gcv_facetize_free_bot(bots[0]);
		    _gcv_facetize_free_bot(bots[1]);
		}
	    } else {
		/* catch */
		BU_UNSETJUMP;
		bu_log("gcv_facetize(): conversion to BoT failed\n");

		if (result)
		    _gcv_facetize_free_bot(result);

		return _gcv_facetize_cleanup(nmg_model, facetize_tree);
	    }

	    BU_UNSETJUMP;
	}
    }

    if (result) {
	rt_bot_vertex_fuse(result, tol);
	rt_bot_face_fuse(result);
	rt_bot_condense(result);
    }

    _gcv_facetize_cleanup(nmg_model, facetize_tree);
    return result;
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
