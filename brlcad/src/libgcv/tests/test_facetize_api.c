/*                 T E S T _ F A C E T I Z E _ A P I . C
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

#include "common.h"

#include <string.h>

#include "vmath.h"
#include "bu/app.h"
#include "bu/avs.h"
#include "bu/str.h"
#include "bu/vls.h"
#include "gcv/facetize.h"


static int
test_defaults(void)
{
    struct gcv_facetize_opts opts;
    struct gcv_facetize_db_opts db_opts;
    struct bu_vls msg = BU_VLS_INIT_ZERO;
    struct gcv_facetize_sample_opts sample_opts;
    struct gcv_facetize_sample_opts sample_copy;
    struct gcv_facetize_nmg_opts nmg_opts;
    struct gcv_facetize_cm_opts cm_opts;
    struct gcv_facetize_spsr_opts spsr_opts;

    gcv_facetize_opts_default(&opts);
    gcv_facetize_db_opts_default(&db_opts);

    if (db_opts.region_mode || db_opts.in_place || db_opts.make_nmg || db_opts.nmg_booleval ||
	db_opts.no_perturb || db_opts.verbosity || db_opts.working_dir || db_opts.base_name ||
	db_opts.prefix || db_opts.suffix)
	return 1;

    if (opts.output_mode != GCV_FACETIZE_OUTPUT_SINGLE_BOT)
	return 1;
    if (opts.boolean_engine != GCV_FACETIZE_BOOL_MANIFOLD)
	return 1;
    if (!opts.subprocess)
	return 1;
    if (opts.max_sampled_points != 200000)
	return 1;
    if (!NEAR_EQUAL(opts.perturb_volume_threshold, 10.0, VUNITIZE_TOL))
	return 1;
    if (!NEAR_EQUAL(opts.perturb_surface_area_threshold, 10.0, VUNITIZE_TOL))
	return 1;
    if (gcv_facetize_opts_validate(&opts, &msg) != BRLCAD_OK) {
	bu_vls_free(&msg);
	return 1;
    }

    opts.output_mode = (enum gcv_facetize_output_mode)999;
    if (gcv_facetize_opts_validate(&opts, &msg) != BRLCAD_ERROR) {
	bu_vls_free(&msg);
	return 1;
    }

    gcv_facetize_sample_opts_default(&sample_opts);
    if (sample_opts.max_pnts != 200000) {
	bu_vls_free(&msg);
	return 1;
    }
    if (gcv_facetize_sample_opts_set(&sample_opts, "max_pnts", "9") != BRLCAD_OK) {
	bu_vls_free(&msg);
	return 1;
    }
    if (sample_opts.max_pnts != 9) {
	bu_vls_free(&msg);
	return 1;
    }
    sample_opts.avg_thickness = 3.0;
    gcv_facetize_sample_opts_copy(&sample_copy, &sample_opts);
    if (!gcv_facetize_sample_opts_equal(&sample_copy, &sample_opts)) {
	bu_vls_free(&msg);
	return 1;
    }
    if (!NEAR_EQUAL(sample_copy.avg_thickness, 3.0, VUNITIZE_TOL)) {
	bu_vls_free(&msg);
	return 1;
    }
    gcv_facetize_nmg_opts_default(&nmg_opts);
    if (nmg_opts.max_time != 30 || nmg_opts.plate_max_time != 1200) {
	bu_vls_free(&msg);
	return 1;
    }
    if (gcv_facetize_nmg_opts_set(&nmg_opts, "plate_max_time", "77") != BRLCAD_OK) {
	bu_vls_free(&msg);
	return 1;
    }
    if (nmg_opts.plate_max_time != 77) {
	bu_vls_free(&msg);
	return 1;
    }
    {
	struct gcv_facetize_kv apply_opts[1];
	apply_opts[0].key = "max_time";
	apply_opts[0].value = "11";
	if (gcv_facetize_nmg_opts_apply(&nmg_opts, apply_opts, 1) != BRLCAD_OK) {
	    bu_vls_free(&msg);
	    return 1;
	}
	if (nmg_opts.max_time != 11) {
	    bu_vls_free(&msg);
	    return 1;
	}
    }
    gcv_facetize_cm_opts_default(&cm_opts);
    if (cm_opts.max_time != 600 || cm_opts.max_cycle_time != 30) {
	bu_vls_free(&msg);
	return 1;
    }
    if (gcv_facetize_cm_opts_set(&cm_opts, "feature_size", "1.5") != BRLCAD_OK) {
	bu_vls_free(&msg);
	return 1;
    }
    if (!NEAR_EQUAL(cm_opts.sample.feature_size, 1.5, VUNITIZE_TOL)) {
	bu_vls_free(&msg);
	return 1;
    }
    {
	struct gcv_facetize_kv apply_opts[1];
	apply_opts[0].key = "max_cycle_time";
	apply_opts[0].value = "12";
	if (gcv_facetize_cm_opts_apply(&cm_opts, apply_opts, 1) != BRLCAD_OK) {
	    bu_vls_free(&msg);
	    return 1;
	}
	if (cm_opts.max_cycle_time != 12) {
	    bu_vls_free(&msg);
	    return 1;
	}
    }
    gcv_facetize_spsr_opts_default(&spsr_opts);
    if (spsr_opts.max_time != 600 || spsr_opts.depth != 8) {
	bu_vls_free(&msg);
	return 1;
    }
    if (gcv_facetize_spsr_opts_set(&spsr_opts, "depth", "6") != BRLCAD_OK) {
	bu_vls_free(&msg);
	return 1;
    }
    if (spsr_opts.depth != 6) {
	bu_vls_free(&msg);
	return 1;
    }
    {
	struct gcv_facetize_kv apply_opts[1];
	apply_opts[0].key = "samples_per_node";
	apply_opts[0].value = "2.5";
	if (gcv_facetize_spsr_opts_apply(&spsr_opts, apply_opts, 1) != BRLCAD_OK) {
	    bu_vls_free(&msg);
	    return 1;
	}
	if (!NEAR_EQUAL(spsr_opts.samples_per_node, 2.5, VUNITIZE_TOL)) {
	    bu_vls_free(&msg);
	    return 1;
	}
    }

    bu_vls_free(&msg);
    bu_avs_free(&opts.method_options);
    return 0;
}


static int
test_methods(void)
{
    const struct gcv_facetize_method_info *methods = NULL;
    const struct gcv_facetize_method_info *resolved[3] = {NULL, NULL, NULL};
    size_t method_cnt = gcv_facetize_methods(&methods);

    if (method_cnt != 3)
	return 1;
    if (!methods)
	return 1;
    if (!BU_STR_EQUAL(methods[0].name, "NMG"))
	return 1;
    if (!BU_STR_EQUAL(methods[1].name, "CM"))
	return 1;
    if (!BU_STR_EQUAL(methods[2].name, "SPSR"))
	return 1;
    if (!gcv_facetize_method("NMG"))
	return 1;
    if (gcv_facetize_method("missing"))
	return 1;
    if (!gcv_facetize_method_has_capability("CM", GCV_FACETIZE_CAP_CONSUMES_POINT_SAMPLES))
	return 1;
    if (gcv_facetize_method_has_capability("NMG", GCV_FACETIZE_CAP_CONSUMES_POINT_SAMPLES))
	return 1;
    if (gcv_facetize_method_has_capability("missing", GCV_FACETIZE_CAP_CONSUMES_POINT_SAMPLES))
	return 1;
    if (gcv_facetize_resolve_methods(NULL, resolved, 3) != 3)
	return 1;
    if (!resolved[0] || !BU_STR_EQUAL(resolved[0]->name, "NMG"))
	return 1;

    {
	const char *requested[] = {"SPSR", "missing", "NMG"};
	struct gcv_facetize_opts opts;
	const struct gcv_facetize_method_info *filtered[3] = {NULL, NULL, NULL};

	gcv_facetize_opts_default(&opts);
	opts.methods = requested;
	opts.method_count = 3;

	if (gcv_facetize_resolve_methods(&opts, filtered, 3) != 2) {
	    bu_avs_free(&opts.method_options);
	    return 1;
	}
	if (!filtered[0] || !BU_STR_EQUAL(filtered[0]->name, "SPSR")) {
	    bu_avs_free(&opts.method_options);
	    return 1;
	}
	if (!filtered[1] || !BU_STR_EQUAL(filtered[1]->name, "NMG")) {
	    bu_avs_free(&opts.method_options);
	    return 1;
	}

	bu_avs_free(&opts.method_options);

    }

    return 0;
}


static int
test_method_options(void)
{
    const struct gcv_facetize_option_desc *options = NULL;
    size_t option_cnt = gcv_facetize_method_options("SPSR", &options);

    if (!options)
	return 1;
    if (option_cnt == 0)
	return 1;
    if (!gcv_facetize_method_options("NMG", NULL))
	return 1;
    if (gcv_facetize_method_options("missing", &options) != 0)
	return 1;
    if (options)
	return 1;
    if (!gcv_facetize_method_option_known("NMG", "tol_rel"))
	return 1;
    if (gcv_facetize_method_option_known("NMG", "not_real"))
	return 1;
    if (gcv_facetize_method_option_known("missing", "tol_rel"))
	return 1;
    {
	int v = 0;
	if (gcv_facetize_method_option_default_int("NMG", "max_time", &v) != BRLCAD_OK)
	    return 1;
	if (v != 30)
	    return 1;
	if (gcv_facetize_method_max_time_default("CM", &v) != BRLCAD_OK)
	    return 1;
	if (v != 600)
	    return 1;
	if (gcv_facetize_plate_max_time_default(&v) != BRLCAD_OK)
	    return 1;
	if (v != 1200)
	    return 1;
	if (gcv_facetize_method_option_default_int("NMG", "not_real", &v) != BRLCAD_ERROR)
	    return 1;
    }

    return 0;
}


static int
test_unknown_methods(void)
{
    const char *requested[] = {"NMG", "bogus", "SPSR", "bogus"};
    const char *unknown[4] = {NULL, NULL, NULL, NULL};
    struct gcv_facetize_opts opts;
    size_t unknown_cnt = 0;

    gcv_facetize_opts_default(&opts);
    opts.methods = requested;
    opts.method_count = 4;

    unknown_cnt = gcv_facetize_unknown_methods(&opts, unknown, 4);
    bu_avs_free(&opts.method_options);

    if (unknown_cnt != 2)
	return 1;
    if (!unknown[0] || !BU_STR_EQUAL(unknown[0], "bogus"))
	return 1;
    if (!unknown[1] || !BU_STR_EQUAL(unknown[1], "bogus"))
	return 1;

    return 0;
}


static int
test_describe(void)
{
    struct bu_vls desc = BU_VLS_INIT_ZERO;

    gcv_facetize_describe_options(&desc);
    if (!bu_vls_strlen(&desc)) {
	bu_vls_free(&desc);
	return 1;
    }
    if (!strstr(bu_vls_cstr(&desc), "NMG")) {
	bu_vls_free(&desc);
	return 1;
    }

    bu_vls_free(&desc);
    return 0;
}


static int
test_method_optstr(void)
{
    struct gcv_facetize_kv opts[1];
    char *mstr = NULL;

    opts[0].key = "max_time";
    opts[0].value = "42";
    mstr = gcv_facetize_method_optstr("CM", opts, 1, NULL, 0);
    if (!mstr)
	return 1;
    if (!strstr(mstr, "CM")) {
	bu_free(mstr, "method opt str");
	return 1;
    }
    if (!strstr(mstr, "max_time=42")) {
	bu_free(mstr, "method opt str");
	return 1;
    }
    bu_free(mstr, "method opt str");

    mstr = gcv_facetize_method_optstr("NMG", NULL, 0, NULL, 7);
    if (!mstr)
	return 1;
    if (!strstr(mstr, "NMG")) {
	bu_free(mstr, "method opt str");
	return 1;
    }
    if (!strstr(mstr, "nmg_debug=")) {
	bu_free(mstr, "method opt str");
	return 1;
    }
    bu_free(mstr, "method opt str");

    mstr = gcv_facetize_quote_arg("CM max_time=2");
    if (!mstr)
	return 1;
    if (!BU_STR_EQUAL(mstr, "\"CM max_time=2\"")) {
	bu_free(mstr, "quoted arg");
	return 1;
    }
    bu_free(mstr, "quoted arg");

    return 0;
}


static int
test_parse_helpers(void)
{
    struct bu_ptbl methods = BU_PTBL_INIT_ZERO;
    char *method = NULL;
    struct gcv_facetize_kv *options = NULL;
    size_t option_count = 0;

    if (gcv_facetize_parse_methods_csv(NULL, "NMG,CM", &methods) != BRLCAD_OK)
	return 1;
    if (BU_PTBL_LEN(&methods) != 2) {
	bu_ptbl_free(&methods);
	return 1;
    }
    if (!BU_STR_EQUAL((const char *)BU_PTBL_GET(&methods, 0), "NMG")) {
	bu_free((void *)BU_PTBL_GET(&methods, 0), "parsed method");
	bu_free((void *)BU_PTBL_GET(&methods, 1), "parsed method");
	bu_ptbl_free(&methods);
	return 1;
    }
    bu_free((void *)BU_PTBL_GET(&methods, 0), "parsed method");
    bu_free((void *)BU_PTBL_GET(&methods, 1), "parsed method");
    bu_ptbl_free(&methods);

    bu_ptbl_init(&methods, 8, "default methods");
    if (gcv_facetize_default_method_names(&methods) != BRLCAD_OK)
	return 1;
    if (BU_PTBL_LEN(&methods) != 3) {
	gcv_facetize_free_string_ptbl(&methods);
	return 1;
    }
    gcv_facetize_free_string_ptbl(&methods);

    if (gcv_facetize_parse_method_options(NULL, "CM max_time=9", &method, &options, &option_count) != BRLCAD_OK)
	return 1;
    if (!method || !BU_STR_EQUAL(method, "CM")) {
	if (method)
	    bu_free(method, "parsed method");
	gcv_facetize_free_method_options(options, option_count);
	return 1;
    }
    if (option_count != 1) {
	bu_free(method, "parsed method");
	gcv_facetize_free_method_options(options, option_count);
	return 1;
    }
    if (!options[0].key || !BU_STR_EQUAL(options[0].key, "max_time")) {
	bu_free(method, "parsed method");
	gcv_facetize_free_method_options(options, option_count);
	return 1;
    }
    bu_free(method, "parsed method");
    gcv_facetize_free_method_options(options, option_count);

    return 0;
}


static int
test_resolved_method_names(void)
{
    struct gcv_facetize_opts opts;
    struct bu_ptbl names = BU_PTBL_INIT_ZERO;
    const char *requested[] = {"SPSR", "missing", "NMG"};

    gcv_facetize_opts_default(&opts);
    opts.methods = requested;
    opts.method_count = 3;

    if (gcv_facetize_resolved_method_names(&opts, &names) != BRLCAD_OK) {
	bu_avs_free(&opts.method_options);
	return 1;
    }
    bu_avs_free(&opts.method_options);

    if (BU_PTBL_LEN(&names) != 2) {
	gcv_facetize_free_method_names(&names);
	return 1;
    }
    if (!BU_STR_EQUAL((const char *)BU_PTBL_GET(&names, 0), "SPSR")) {
	gcv_facetize_free_method_names(&names);
	return 1;
    }

	gcv_facetize_free_method_names(&names);

	bu_ptbl_init(&names, 8, "resolved method names");
	if (gcv_facetize_resolved_method_names(NULL, &names) != BRLCAD_OK)
	    return 1;
	if (BU_PTBL_LEN(&names) != 3) {
	    gcv_facetize_free_method_names(&names);
	    return 1;
	}
	gcv_facetize_free_method_names(&names);

    return 0;
}


static int
test_method_opts_state(void)
{
    struct gcv_facetize_method_opts_state state;
    struct bu_vls msg = BU_VLS_INIT_ZERO;
    struct gcv_facetize_kv *kv = NULL;
    char *optstr = NULL;
    size_t kv_cnt = 0;

    gcv_facetize_method_opts_state_init(&state);

    if (gcv_facetize_method_opts_parse_methods(&msg, "CM,SPSR", &state) != BRLCAD_OK) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }
    if (gcv_facetize_method_opts_method_count(&state) != 2) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }
    if (!BU_STR_EQUAL(gcv_facetize_method_opts_method_name(&state, 0), "CM")) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }

    if (gcv_facetize_method_opts_parse_options(&msg, "SPSR max_time=11 feature_size=4.5", &state) != BRLCAD_OK) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }
    if (gcv_facetize_method_opts_time_limit(&state, "SPSR", 0) != 11) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }
    kv_cnt = gcv_facetize_method_opts_method_options(&state, "SPSR", &kv);
    if (kv_cnt != 2 || !kv) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }
    bu_free(kv, "method option kv");

    optstr = gcv_facetize_method_opts_string(&state, "SPSR", NULL, 0);
    if (!optstr || !strstr(optstr, "SPSR") || !strstr(optstr, "feature_size=4.5")) {
	if (optstr)
	    bu_free(optstr, "method optstr");
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }
    bu_free(optstr, "method optstr");

    if (!gcv_facetize_method_opts_has_option(&state, "SPSR", "max_time")) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }
    if (gcv_facetize_method_opts_has_option(&state, "CM", "max_time")) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }
    if (gcv_facetize_method_opts_set_option(&state, "CM", "max_time", "22") != BRLCAD_OK) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }
    if (!gcv_facetize_method_opts_has_option(&state, "CM", "max_time")) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }
    if (gcv_facetize_method_opts_time_limit(&state, "CM", 0) != 22) {
	bu_vls_free(&msg);
	gcv_facetize_method_opts_state_free(&state);
	return 1;
    }

    bu_vls_free(&msg);
    gcv_facetize_method_opts_state_free(&state);
    return 0;
}


static int
test_process_cmd_helpers(void)
{
    char *exec_path = NULL;
    char *backup_path = NULL;
    const char *argv[16] = {0};
    size_t argc = 0;
    size_t method_idx = 0;
    size_t method_opts_idx = 0;
    struct gcv_facetize_kv kv[2];
    struct gcv_facetize_process_opts popts;
    const char *methods[2] = {"SPSR", "CM"};
    int ival = 0;

    exec_path = gcv_facetize_process_exec();
    if (!exec_path)
	return 1;

    argc = gcv_facetize_process_base_argv(argv, 16, exec_path, "a.g", 1, "NMG", "NMG max_time=1", "/tmp");
    bu_free(exec_path, "process exec path");

    if (!argc)
	return 1;

    if (!argv[1] || !BU_STR_EQUAL(argv[1], "facetize_process"))
	return 1;

    if (gcv_facetize_process_method_slots(&method_idx, &method_opts_idx, 1) != BRLCAD_OK)
	return 1;
    if (!argv[method_idx] || !BU_STR_EQUAL(argv[method_idx], "NMG"))
	return 1;
    if (!argv[method_opts_idx] || !BU_STR_EQUAL(argv[method_opts_idx], "NMG max_time=1"))
	return 1;
    if (gcv_facetize_process_set_method_argv(argv, method_idx, method_opts_idx, "CM", "CM max_time=2") != BRLCAD_OK)
	return 1;
    if (!BU_STR_EQUAL(argv[method_idx], "CM"))
	return 1;
    if (!BU_STR_EQUAL(argv[method_opts_idx], "CM max_time=2"))
	return 1;

    kv[0].key = "max_time";
    kv[0].value = "7";
    if (gcv_facetize_kv_int(kv, 1, "max_time", &ival) != BRLCAD_OK)
	return 1;
    if (ival != 7)
	return 1;

    if (gcv_facetize_process_run_objects(NULL, 0, NULL, NULL, 0, 1.0, 1, NULL, NULL, NULL) != -1)
	return 1;
    if (gcv_facetize_process_run_method_objects(NULL, 1, "NMG", NULL, NULL, 0, NULL, NULL, 0, 1.0, 1, NULL, NULL, NULL) != -1)
	return 1;
    if (gcv_facetize_process_run_methods(NULL, 1, methods, 2, NULL, NULL, 0, "/tmp", NULL, 0, MAXPATHLEN, 8000, 0, 0, NULL, NULL, NULL) != -1)
	return 1;
    if (gcv_facetize_process_method_argv_len(&argc, "a.g", 1, "NMG", NULL, NULL, 0, "/tmp") == 0)
	return 1;
    if (argc == 0)
	return 1;
    if (gcv_facetize_process_method_argv_len(NULL, NULL, 1, "NMG", NULL, NULL, 0, "/tmp") != 0)
	return 1;

    gcv_facetize_process_opts_default(&popts);
    if (popts.nmg.max_time != 30 || popts.cm.max_time != 600 || popts.spsr.max_time != 600)
	return 1;
    kv[0].key = "feature_size";
    kv[0].value = "3.25";
    kv[1].key = "max_pnts";
    kv[1].value = "1234";
    if (gcv_facetize_process_opts_apply_method_options(&popts, "SPSR", kv, 2) != BRLCAD_OK)
	return 1;
    if (!NEAR_EQUAL(popts.spsr.sample.feature_size, 3.25, VUNITIZE_TOL))
	return 1;
    if (popts.spsr.sample.max_pnts != 1234)
	return 1;
    if (gcv_facetize_process_opts_select_sample_method(&popts, methods, 2) != BRLCAD_OK)
	return 1;
    if (!NEAR_EQUAL(popts.pnts.feature_size, 3.25, VUNITIZE_TOL))
	return 1;
    if (popts.pnts.max_pnts != 1234)
	return 1;
    if (gcv_facetize_process_opts_apply_method_options(&popts, "UNKNOWN", kv, 1) != BRLCAD_ERROR)
	return 1;

    backup_path = gcv_facetize_backup_path("model.g");
    if (!backup_path)
	return 1;
    if (!BU_STR_EQUAL(backup_path, "model.g.bak")) {
	bu_free(backup_path, "backup path");
	return 1;
    }
    bu_free(backup_path, "backup path");

    return 0;
}


static int
test_high_level_entrypoints(void)
{
    struct gcv_facetize_opts opts;

    gcv_facetize_opts_default(&opts);
    if (gcv_facetize_to_bot(NULL, NULL, &opts, NULL, NULL, NULL)) {
	bu_avs_free(&opts.method_options);
	return 1;
    }

    opts.output_mode = GCV_FACETIZE_OUTPUT_REGION_BOTS;
    if (gcv_facetize_to_bot(NULL, NULL, &opts, NULL, NULL, NULL)) {
	bu_avs_free(&opts.method_options);
	return 1;
    }

    if (gcv_facetize_nmg_eval_to_db(NULL, 0, NULL, NULL, 0, 0, NULL, NULL) != BRLCAD_ERROR) {
	bu_avs_free(&opts.method_options);
	return 1;
    }
    if (gcv_facetize_objects_to_db(NULL, 0, NULL, 0, 0, 0, NULL, NULL) != BRLCAD_ERROR) {
	bu_avs_free(&opts.method_options);
	return 1;
    }
    if (gcv_facetize_import_working_regions(NULL, NULL, 0, NULL, 0, 0, NULL, NULL) != BRLCAD_ERROR) {
	bu_avs_free(&opts.method_options);
	return 1;
    }
    if (gcv_facetize_region_result_name(NULL, NULL, 0, NULL) != BRLCAD_ERROR) {
	bu_avs_free(&opts.method_options);
	return 1;
    }
    if (gcv_facetize_region_replace_root(NULL, NULL, NULL) != BRLCAD_ERROR) {
	bu_avs_free(&opts.method_options);
	return 1;
    }
    if (gcv_facetize_regions_to_db(NULL, 0, NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0, NULL, NULL) != BRLCAD_ERROR) {
	bu_avs_free(&opts.method_options);
	return 1;
    }
    if (gcv_facetize_manifold_eval_to_db(NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL) != BRLCAD_ERROR) {
	bu_avs_free(&opts.method_options);
	return 1;
    }
    if (gcv_facetize_manifold_objects_to_db(NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) != BRLCAD_ERROR) {
	bu_avs_free(&opts.method_options);
	return 1;
    }

    bu_avs_free(&opts.method_options);
    return 0;
}


int
main(int argc, char **argv)
{
    int failures = 0;

    bu_setprogname(argv[0]);
    if (argc != 1)
	return 1;

    failures += test_defaults();
    failures += test_methods();
    failures += test_method_options();
    failures += test_unknown_methods();
    failures += test_describe();
    failures += test_method_optstr();
    failures += test_parse_helpers();
    failures += test_resolved_method_names();
    failures += test_method_opts_state();
    failures += test_process_cmd_helpers();
    failures += test_high_level_entrypoints();

    return failures;
}
