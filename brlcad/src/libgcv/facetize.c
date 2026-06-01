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
#include <errno.h>
#include <stdarg.h>

#include "bu/parallel.h"
#include "bu/app.h"
#include "bu/opt.h"
#include "bu/snooze.h"
#include "bu/str.h"
#include "bu/time.h"
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

#include "subprocess.h"


#define GCV_FACETIZE_SAMPLE_OPTION_ENTRIES \
    {"feature_scale", GCV_FACETIZE_OPT_FASTF, "0.15", "Percentage of average sampled thickness to use for sampling feature size."}, \
    {"feature_size", GCV_FACETIZE_OPT_FASTF, "0.0", "Explicit sampling feature length; overrides feature_scale."}, \
    {"d_feature_size", GCV_FACETIZE_OPT_FASTF, "0.0", "Initial decimation feature length; default handling is method specific."}, \
    {"max_sample_time", GCV_FACETIZE_OPT_INT, "30", "Maximum time to allow point sampling to continue."}, \
    {"max_pnts", GCV_FACETIZE_OPT_INT, "200000", "Maximum number of points to sample."}

static const struct gcv_facetize_option_desc _gcv_facetize_nmg_options[] = {
    {"max_time", GCV_FACETIZE_OPT_INT, "30", "Maximum overall run time for object conversion."},
    {"plate_max_time", GCV_FACETIZE_OPT_INT, "1200", "Maximum run time for plate-mode BoT conversion."},
    {"nmg_debug", GCV_FACETIZE_OPT_INT, "0x00000000", "NMG debugging flag."},
    {"tol_abs", GCV_FACETIZE_OPT_FASTF, "0", "Absolute tessellation distance tolerance."},
    {"tol_norm", GCV_FACETIZE_OPT_FASTF, "0", "Normal tessellation tolerance."},
    {"tol_rel", GCV_FACETIZE_OPT_FASTF, "0.01", "Relative tessellation distance tolerance."}
};


static const struct gcv_facetize_option_desc _gcv_facetize_cm_options[] = {
    GCV_FACETIZE_SAMPLE_OPTION_ENTRIES,
    {"max_cycle_time", GCV_FACETIZE_OPT_INT, "30", "Maximum time to take for one processing cycle."},
    {"max_time", GCV_FACETIZE_OPT_INT, "600", "Maximum overall run time for object conversion."}
};


static const struct gcv_facetize_option_desc _gcv_facetize_spsr_options[] = {
    GCV_FACETIZE_SAMPLE_OPTION_ENTRIES,
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


static int
_gcv_append_method(struct bu_vls *msg, struct bu_ptbl *methods_out, const char *name)
{
    if (!name || !strlen(name))
        return BRLCAD_OK;
    if (!gcv_facetize_method(name)) {
        if (msg)
            bu_vls_printf(msg, "Unknown facetize method: %s", name);
        return BRLCAD_ERROR;
    }
    bu_ptbl_ins(methods_out, (long *)bu_strdup(name));
    return BRLCAD_OK;
}


static const char *
_gcv_facetize_kv_get(const struct gcv_facetize_kv *options, size_t option_count, const char *key)
{
    size_t i = 0;

    if (!options || !key)
        return NULL;

    for (i = 0; i < option_count; i++) {
        if (!options[i].key)
            continue;
        if (BU_STR_EQUAL(options[i].key, key))
            return options[i].value;
    }

    return NULL;
}


void
gcv_facetize_db_opts_default(struct gcv_facetize_db_opts *opts)
{
    if (!opts)
        return;

    memset(opts, 0, sizeof(*opts));
}

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


int
gcv_facetize_opts_validate(const struct gcv_facetize_opts *opts, struct bu_vls *msg)
{
    size_t i = 0;

    if (!opts)
        return BRLCAD_OK;

    if (opts->output_mode < GCV_FACETIZE_OUTPUT_SINGLE_BOT || opts->output_mode > GCV_FACETIZE_OUTPUT_TARGET_DB) {
        if (msg)
            bu_vls_printf(msg, "Invalid gcv_facetize output mode: %d\n", (int)opts->output_mode);
        return BRLCAD_ERROR;
    }

    if (opts->boolean_engine < GCV_FACETIZE_BOOL_MANIFOLD || opts->boolean_engine > GCV_FACETIZE_BOOL_NMG) {
        if (msg)
            bu_vls_printf(msg, "Invalid gcv_facetize boolean engine: %d\n", (int)opts->boolean_engine);
        return BRLCAD_ERROR;
    }

    if (opts->method_count && !opts->methods) {
        if (msg)
            bu_vls_printf(msg, "Facetize method_count is nonzero but methods is NULL\n");
        return BRLCAD_ERROR;
    }

    for (i = 0; i < opts->method_count; i++) {
        if (!gcv_facetize_method(opts->methods[i])) {
            if (msg)
                bu_vls_printf(msg, "Unknown facetize method: %s\n", opts->methods[i] ? opts->methods[i] : "(null)");
            return BRLCAD_ERROR;
        }
    }

    return BRLCAD_OK;
}


void
gcv_facetize_sample_opts_default(struct gcv_facetize_sample_opts *opts)
{
    if (!opts)
        return;

    opts->feature_scale = 0.15;
    opts->feature_size = 0.0;
    opts->d_feature_size = 0.0;
    opts->max_sample_time = 30;
    opts->max_pnts = 200000;
    opts->obj_bbox_vol = 0.0;
    opts->pnts_bbox_vol = 0.0;
    opts->target_feature_size = 0.0;
    opts->avg_thickness = 0.0;
}


void
gcv_facetize_nmg_opts_default(struct gcv_facetize_nmg_opts *opts)
{
    int val = 0;

    if (!opts)
        return;

    opts->tol = (struct bn_tol)BN_TOL_INIT_TOL;
    opts->ttol = (struct bg_tess_tol)BG_TESS_TOL_INIT_TOL;
    opts->nmg_debug = 0;
    opts->max_time = (gcv_facetize_method_max_time_default("NMG", &val) == BRLCAD_OK) ? val : 30;
    opts->plate_max_time = (gcv_facetize_plate_max_time_default(&val) == BRLCAD_OK) ? val : 1200;
}


void
gcv_facetize_cm_opts_default(struct gcv_facetize_cm_opts *opts)
{
    int val = 0;

    if (!opts)
        return;

    gcv_facetize_sample_opts_default(&opts->sample);
    opts->max_cycle_time = 30;
    opts->max_time = (gcv_facetize_method_max_time_default("CM", &val) == BRLCAD_OK) ? val : 600;
}


void
gcv_facetize_spsr_opts_default(struct gcv_facetize_spsr_opts *opts)
{
    int val = 0;

    if (!opts)
        return;

    gcv_facetize_sample_opts_default(&opts->sample);
    opts->depth = 8;
    opts->interpolate = 2.0;
    opts->samples_per_node = 1.5;
    opts->max_time = (gcv_facetize_method_max_time_default("SPSR", &val) == BRLCAD_OK) ? val : 600;
}


struct gcv_facetize_method_option_state {
    char *method;
    char *key;
    char *value;
};


static void
_gcv_facetize_method_opts_defaults(struct gcv_facetize_method_opts_state *opts)
{
    int val = 0;

    if (!opts)
        return;

    opts->max_time_nmg = (gcv_facetize_method_max_time_default("NMG", &val) == BRLCAD_OK) ? val : 30;
    opts->max_time_cm = (gcv_facetize_method_max_time_default("CM", &val) == BRLCAD_OK) ? val : 600;
    opts->max_time_spsr = (gcv_facetize_method_max_time_default("SPSR", &val) == BRLCAD_OK) ? val : 600;
    opts->plate_max_time = (gcv_facetize_plate_max_time_default(&val) == BRLCAD_OK) ? val : 1200;
}


void
gcv_facetize_method_opts_state_init(struct gcv_facetize_method_opts_state *opts)
{
    if (!opts)
        return;

    memset(opts, 0, sizeof(*opts));
    bu_ptbl_init(&opts->methods, 8, "gcv facetize methods");
    bu_ptbl_init(&opts->options, 8, "gcv facetize method options");
    _gcv_facetize_method_opts_defaults(opts);
}


void
gcv_facetize_method_opts_state_free(struct gcv_facetize_method_opts_state *opts)
{
    size_t i = 0;

    if (!opts)
        return;

    for (i = 0; i < BU_PTBL_LEN(&opts->methods); i++) {
        char *m = (char *)BU_PTBL_GET(&opts->methods, i);
        if (m)
            bu_free(m, "facetize method name");
    }
    bu_ptbl_free(&opts->methods);

    for (i = 0; i < BU_PTBL_LEN(&opts->options); i++) {
        struct gcv_facetize_method_option_state *o = (struct gcv_facetize_method_option_state *)BU_PTBL_GET(&opts->options, i);
        if (!o)
            continue;
        if (o->method)
            bu_free(o->method, "facetize option method");
        if (o->key)
            bu_free(o->key, "facetize option key");
        if (o->value)
            bu_free(o->value, "facetize option value");
        BU_PUT(o, struct gcv_facetize_method_option_state);
    }
    bu_ptbl_free(&opts->options);
    _gcv_facetize_method_opts_defaults(opts);
}


static void
_gcv_facetize_method_opts_set_time(struct gcv_facetize_method_opts_state *opts,
                                   const char *method,
                                   const char *key,
                                   const struct gcv_facetize_kv *options,
                                   size_t option_count)
{
    int max_time_val = 0;

    if (!opts || !method || !key)
        return;

    if (BU_STR_EQUAL(key, "max_time") && gcv_facetize_kv_int(options, option_count, "max_time", &max_time_val) == BRLCAD_OK) {
        if (BU_STR_EQUAL(method, "NMG"))
            opts->max_time_nmg = max_time_val;
        if (BU_STR_EQUAL(method, "CM"))
            opts->max_time_cm = max_time_val;
        if (BU_STR_EQUAL(method, "SPSR"))
            opts->max_time_spsr = max_time_val;
    }

    if (BU_STR_EQUAL(key, "plate_max_time") && gcv_facetize_kv_int(options, option_count, "plate_max_time", &max_time_val) == BRLCAD_OK)
        opts->plate_max_time = max_time_val;
}


int
gcv_facetize_method_opts_parse_methods(struct bu_vls *msg,
                                       const char *arg,
                                       struct gcv_facetize_method_opts_state *opts)
{
    struct bu_ptbl methods = BU_PTBL_INIT_ZERO;
    size_t i = 0;

    if (!opts || !arg)
        return BRLCAD_ERROR;

    if (gcv_facetize_parse_methods_csv(msg, arg, &methods) != BRLCAD_OK) {
        gcv_facetize_free_string_ptbl(&methods);
        return BRLCAD_ERROR;
    }

    for (i = 0; i < BU_PTBL_LEN(&methods); i++) {
        char *mn = (char *)BU_PTBL_GET(&methods, i);
        if (mn)
            bu_ptbl_ins(&opts->methods, (long *)bu_strdup(mn));
    }
    gcv_facetize_free_string_ptbl(&methods);
    return BRLCAD_OK;
}


int
gcv_facetize_method_opts_parse_options(struct bu_vls *msg,
                                       const char *arg,
                                       struct gcv_facetize_method_opts_state *opts)
{
    char *method = NULL;
    struct gcv_facetize_kv *options = NULL;
    size_t option_count = 0;
    size_t i = 0;

    if (!opts || !arg)
        return BRLCAD_ERROR;

    if (gcv_facetize_parse_method_options(msg, arg, &method, &options, &option_count) != BRLCAD_OK)
        return BRLCAD_ERROR;

    for (i = 0; i < option_count; i++) {
        struct gcv_facetize_method_option_state *o = NULL;
        if (!options[i].key)
            continue;
        BU_GET(o, struct gcv_facetize_method_option_state);
        o->method = bu_strdup(method ? method : "");
        o->key = bu_strdup(options[i].key ? options[i].key : "");
        o->value = bu_strdup(options[i].value ? options[i].value : "");
        bu_ptbl_ins(&opts->options, (long *)o);
        _gcv_facetize_method_opts_set_time(opts, method, options[i].key, options, option_count);
    }

    if (method)
        bu_free(method, "parsed method");
    gcv_facetize_free_method_options(options, option_count);
    return BRLCAD_OK;
}


int
gcv_facetize_method_opts_opt_methods(struct bu_vls *msg,
                                     size_t argc,
                                     const char **argv,
                                     void *set_var)
{
    BU_OPT_CHECK_ARGV0(msg, argc, argv, "gcv_facetize_method_opts_opt_methods");
    return (gcv_facetize_method_opts_parse_methods(msg, argv[0], (struct gcv_facetize_method_opts_state *)set_var) == BRLCAD_OK) ? 1 : -1;
}


int
gcv_facetize_method_opts_opt_options(struct bu_vls *msg,
                                     size_t argc,
                                     const char **argv,
                                     void *set_var)
{
    BU_OPT_CHECK_ARGV0(msg, argc, argv, "gcv_facetize_method_opts_opt_options");
    return (gcv_facetize_method_opts_parse_options(msg, argv[0], (struct gcv_facetize_method_opts_state *)set_var) == BRLCAD_OK) ? 1 : -1;
}


size_t
gcv_facetize_method_opts_method_count(const struct gcv_facetize_method_opts_state *opts)
{
    if (!opts)
        return 0;
    return BU_PTBL_LEN(&opts->methods);
}


const char *
gcv_facetize_method_opts_method_name(const struct gcv_facetize_method_opts_state *opts,
                                     size_t idx)
{
    if (!opts || idx >= BU_PTBL_LEN(&opts->methods))
        return NULL;
    return (const char *)BU_PTBL_GET(&opts->methods, idx);
}


size_t
gcv_facetize_method_opts_method_options(const struct gcv_facetize_method_opts_state *opts,
                                        const char *method,
                                        struct gcv_facetize_kv **options)
{
    size_t i = 0;
    size_t cnt = 0;

    if (options)
        *options = NULL;
    if (!opts || !method)
        return 0;

    for (i = 0; i < BU_PTBL_LEN(&opts->options); i++) {
        struct gcv_facetize_method_option_state *o = (struct gcv_facetize_method_option_state *)BU_PTBL_GET(&opts->options, i);
        if (o && o->method && BU_STR_EQUAL(o->method, method))
            cnt++;
    }

    if (options && cnt) {
        size_t oi = 0;
        *options = (struct gcv_facetize_kv *)bu_calloc(cnt, sizeof(struct gcv_facetize_kv), "method option kv");
        for (i = 0; i < BU_PTBL_LEN(&opts->options); i++) {
            struct gcv_facetize_method_option_state *o = (struct gcv_facetize_method_option_state *)BU_PTBL_GET(&opts->options, i);
            if (!o || !o->method || !BU_STR_EQUAL(o->method, method))
                continue;
            (*options)[oi].key = o->key;
            (*options)[oi].value = o->value;
            oi++;
        }
    }

    return cnt;
}


int
gcv_facetize_method_opts_has_option(const struct gcv_facetize_method_opts_state *opts,
                                    const char *method,
                                    const char *key)
{
    size_t i = 0;

    if (!opts || !method || !key)
        return 0;

    for (i = 0; i < BU_PTBL_LEN(&opts->options); i++) {
        struct gcv_facetize_method_option_state *o = (struct gcv_facetize_method_option_state *)BU_PTBL_GET(&opts->options, i);
        if (o && o->method && o->key && BU_STR_EQUAL(o->method, method) && BU_STR_EQUAL(o->key, key))
            return 1;
    }

    return 0;
}


int
gcv_facetize_method_opts_set_option(struct gcv_facetize_method_opts_state *opts,
                                    const char *method,
                                    const char *key,
                                    const char *value)
{
    struct gcv_facetize_kv kv;
    size_t i = 0;

    if (!opts || !method || !key)
        return BRLCAD_ERROR;

    for (i = 0; i < BU_PTBL_LEN(&opts->options); i++) {
        struct gcv_facetize_method_option_state *o = (struct gcv_facetize_method_option_state *)BU_PTBL_GET(&opts->options, i);
        if (!o || !o->method || !o->key || !BU_STR_EQUAL(o->method, method) || !BU_STR_EQUAL(o->key, key))
            continue;
        if (o->value)
            bu_free(o->value, "facetize option value");
        o->value = bu_strdup(value ? value : "");
        kv.key = key;
        kv.value = o->value;
        _gcv_facetize_method_opts_set_time(opts, method, key, &kv, 1);
        return BRLCAD_OK;
    }

    struct gcv_facetize_method_option_state *o = NULL;
    BU_GET(o, struct gcv_facetize_method_option_state);
    o->method = bu_strdup(method);
    o->key = bu_strdup(key);
    o->value = bu_strdup(value ? value : "");
    bu_ptbl_ins(&opts->options, (long *)o);

    kv.key = key;
    kv.value = o->value;
    _gcv_facetize_method_opts_set_time(opts, method, key, &kv, 1);
    return BRLCAD_OK;
}


int
gcv_facetize_method_opts_time_limit(const struct gcv_facetize_method_opts_state *opts,
                                    const char *method,
                                    int plate_mode)
{
    int dflt = 30;

    if (plate_mode) {
        if (opts && opts->plate_max_time > 0)
            return opts->plate_max_time;
        if (gcv_facetize_plate_max_time_default(&dflt) == BRLCAD_OK)
            return dflt;
        return 1200;
    }

    if (opts && method) {
        if (BU_STR_EQUAL(method, "NMG") && opts->max_time_nmg > 0)
            return opts->max_time_nmg;
        if (BU_STR_EQUAL(method, "CM") && opts->max_time_cm > 0)
            return opts->max_time_cm;
        if (BU_STR_EQUAL(method, "SPSR") && opts->max_time_spsr > 0)
            return opts->max_time_spsr;
    }
    if (method && gcv_facetize_method_max_time_default(method, &dflt) == BRLCAD_OK)
        return dflt;
    return 30;
}


char *
gcv_facetize_method_opts_string(const struct gcv_facetize_method_opts_state *opts,
                                const char *method,
                                struct db_i *db,
                                long nmg_debug_flag)
{
    struct gcv_facetize_kv *kv = NULL;
    size_t kv_cnt = 0;
    char *optstr = NULL;

    if (!method || !strlen(method))
        return bu_strdup("");

    kv_cnt = gcv_facetize_method_opts_method_options(opts, method, &kv);
    if (!kv_cnt && !BU_STR_EQUAL(method, "NMG"))
        return bu_strdup(method);

    optstr = gcv_facetize_method_optstr(method, kv, kv_cnt, db, nmg_debug_flag);
    if (kv)
        bu_free(kv, "method option kv");
    if (!optstr)
        return bu_strdup("");
    return optstr;
}


static int
_gcv_opt_fastf(const char *val, fastf_t *out)
{
    const char *cstr[2] = {NULL, NULL};

    if (!out)
        return BRLCAD_ERROR;
    if (!val || !strlen(val)) {
        *out = 0.0;
        return BRLCAD_OK;
    }
    cstr[0] = val;
    return (bu_opt_fastf_t(NULL, 1, cstr, (void *)out) < 0) ? BRLCAD_ERROR : BRLCAD_OK;
}


static int
_gcv_opt_int(const char *val, int *out)
{
    const char *cstr[2] = {NULL, NULL};

    if (!out)
        return BRLCAD_ERROR;
    if (!val || !strlen(val)) {
        *out = 0;
        return BRLCAD_OK;
    }
    cstr[0] = val;
    return (bu_opt_int(NULL, 1, cstr, (void *)out) < 0) ? BRLCAD_ERROR : BRLCAD_OK;
}


static int
_gcv_opt_long(const char *val, long *out)
{
    const char *cstr[2] = {NULL, NULL};

    if (!out)
        return BRLCAD_ERROR;
    if (!val || !strlen(val)) {
        *out = 0;
        return BRLCAD_OK;
    }
    cstr[0] = val;
    return (bu_opt_long(NULL, 1, cstr, (void *)out) < 0) ? BRLCAD_ERROR : BRLCAD_OK;
}


int
gcv_facetize_sample_opts_set(struct gcv_facetize_sample_opts *opts, const char *key, const char *val)
{
    if (!opts || !key || !strlen(key))
        return BRLCAD_ERROR;

    if (BU_STR_EQUAL(key, "feature_scale"))
        return _gcv_opt_fastf(val, &opts->feature_scale);
    if (BU_STR_EQUAL(key, "feature_size"))
        return _gcv_opt_fastf(val, &opts->feature_size);
    if (BU_STR_EQUAL(key, "d_feature_size"))
        return _gcv_opt_fastf(val, &opts->d_feature_size);
    if (BU_STR_EQUAL(key, "max_sample_time"))
        return _gcv_opt_int(val, &opts->max_sample_time);
    if (BU_STR_EQUAL(key, "max_pnts"))
        return _gcv_opt_int(val, &opts->max_pnts);

    return BRLCAD_ERROR;
}


int
gcv_facetize_nmg_opts_set(struct gcv_facetize_nmg_opts *opts, const char *key, const char *val)
{
    if (!opts || !key || !strlen(key))
        return BRLCAD_ERROR;

    if (BU_STR_EQUAL(key, "tol_rel"))
        return _gcv_opt_fastf(val, &opts->ttol.rel);
    if (BU_STR_EQUAL(key, "tol_abs"))
        return _gcv_opt_fastf(val, &opts->ttol.abs);
    if (BU_STR_EQUAL(key, "tol_norm"))
        return _gcv_opt_fastf(val, &opts->ttol.norm);
    if (BU_STR_EQUAL(key, "nmg_debug"))
        return _gcv_opt_long(val, &opts->nmg_debug);
    if (BU_STR_EQUAL(key, "max_time"))
        return _gcv_opt_int(val, &opts->max_time);
    if (BU_STR_EQUAL(key, "plate_max_time"))
        return _gcv_opt_int(val, &opts->plate_max_time);

    return BRLCAD_ERROR;
}


int
gcv_facetize_cm_opts_set(struct gcv_facetize_cm_opts *opts, const char *key, const char *val)
{
    if (!opts || !key || !strlen(key))
        return BRLCAD_ERROR;

    if (BU_STR_EQUAL(key, "max_cycle_time"))
        return _gcv_opt_int(val, &opts->max_cycle_time);
    if (BU_STR_EQUAL(key, "max_time"))
        return _gcv_opt_int(val, &opts->max_time);

    return gcv_facetize_sample_opts_set(&opts->sample, key, val);
}


int
gcv_facetize_spsr_opts_set(struct gcv_facetize_spsr_opts *opts, const char *key, const char *val)
{
    if (!opts || !key || !strlen(key))
        return BRLCAD_ERROR;

    if (BU_STR_EQUAL(key, "depth"))
        return _gcv_opt_int(val, &opts->depth);
    if (BU_STR_EQUAL(key, "interpolate"))
        return _gcv_opt_fastf(val, &opts->interpolate);
    if (BU_STR_EQUAL(key, "samples_per_node"))
        return _gcv_opt_fastf(val, &opts->samples_per_node);
    if (BU_STR_EQUAL(key, "max_time"))
        return _gcv_opt_int(val, &opts->max_time);

    return gcv_facetize_sample_opts_set(&opts->sample, key, val);
}


void
gcv_facetize_sample_opts_copy(struct gcv_facetize_sample_opts *dst,
                              const struct gcv_facetize_sample_opts *src)
{
    if (!dst || !src)
        return;

    *dst = *src;
}


int
gcv_facetize_sample_opts_equal(const struct gcv_facetize_sample_opts *a,
                               const struct gcv_facetize_sample_opts *b)
{
    if (!a || !b)
        return 0;

    if (!NEAR_EQUAL(a->feature_scale, b->feature_scale, VUNITIZE_TOL))
        return 0;
    if (!NEAR_EQUAL(a->feature_size, b->feature_size, VUNITIZE_TOL))
        return 0;
    if (!NEAR_EQUAL(a->d_feature_size, b->d_feature_size, VUNITIZE_TOL))
        return 0;
    if (a->max_sample_time != b->max_sample_time)
        return 0;
    if (a->max_pnts != b->max_pnts)
        return 0;

    return 1;
}


int
gcv_facetize_nmg_opts_apply(struct gcv_facetize_nmg_opts *opts,
                            const struct gcv_facetize_kv *options,
                            size_t option_count)
{
    size_t i = 0;

    if (!opts)
        return BRLCAD_ERROR;

    for (i = 0; i < option_count; i++) {
        if (!options || !options[i].key)
            continue;
        if (gcv_facetize_nmg_opts_set(opts, options[i].key, options[i].value) != BRLCAD_OK)
            return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}


int
gcv_facetize_cm_opts_apply(struct gcv_facetize_cm_opts *opts,
                           const struct gcv_facetize_kv *options,
                           size_t option_count)
{
    size_t i = 0;

    if (!opts)
        return BRLCAD_ERROR;

    for (i = 0; i < option_count; i++) {
        if (!options || !options[i].key)
            continue;
        if (gcv_facetize_cm_opts_set(opts, options[i].key, options[i].value) != BRLCAD_OK)
            return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}


int
gcv_facetize_spsr_opts_apply(struct gcv_facetize_spsr_opts *opts,
                             const struct gcv_facetize_kv *options,
                             size_t option_count)
{
    size_t i = 0;

    if (!opts)
        return BRLCAD_ERROR;

    for (i = 0; i < option_count; i++) {
        if (!options || !options[i].key)
            continue;
        if (gcv_facetize_spsr_opts_set(opts, options[i].key, options[i].value) != BRLCAD_OK)
            return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}


void
gcv_facetize_process_opts_default(struct gcv_facetize_process_opts *opts)
{
    if (!opts)
        return;

    gcv_facetize_nmg_opts_default(&opts->nmg);
    gcv_facetize_cm_opts_default(&opts->cm);
    gcv_facetize_spsr_opts_default(&opts->spsr);
    gcv_facetize_sample_opts_default(&opts->pnts);
}


int
gcv_facetize_process_opts_apply_method_options(struct gcv_facetize_process_opts *opts,
                                               const char *method,
                                               const struct gcv_facetize_kv *options,
                                               size_t option_count)
{
    if (!opts || !method)
        return BRLCAD_ERROR;

    if (BU_STR_EQUAL(method, "NMG"))
        return gcv_facetize_nmg_opts_apply(&opts->nmg, options, option_count);
    if (BU_STR_EQUAL(method, "CM"))
        return gcv_facetize_cm_opts_apply(&opts->cm, options, option_count);
    if (BU_STR_EQUAL(method, "SPSR"))
        return gcv_facetize_spsr_opts_apply(&opts->spsr, options, option_count);

    return BRLCAD_ERROR;
}


int
gcv_facetize_process_opts_select_sample_method(struct gcv_facetize_process_opts *opts,
                                               const char * const *methods,
                                               size_t method_count)
{
    size_t i = 0;

    if (!opts)
        return BRLCAD_ERROR;

    for (i = 0; i < method_count; i++) {
        const char *method = methods ? methods[i] : NULL;
        if (!method || !gcv_facetize_method_has_capability(method, GCV_FACETIZE_CAP_CONSUMES_POINT_SAMPLES))
            continue;
        if (BU_STR_EQUAL(method, "CM")) {
            gcv_facetize_sample_opts_copy(&opts->pnts, &opts->cm.sample);
            return BRLCAD_OK;
        }
        if (BU_STR_EQUAL(method, "SPSR")) {
            gcv_facetize_sample_opts_copy(&opts->pnts, &opts->spsr.sample);
            return BRLCAD_OK;
        }
    }

    return BRLCAD_OK;
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


int
gcv_facetize_method_has_capability(const char *method, unsigned capability)
{
    const struct gcv_facetize_method_info *minfo = gcv_facetize_method(method);

    if (!minfo)
        return 0;

    return ((minfo->capabilities & capability) != 0);
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


int
gcv_facetize_method_option_known(const char *method, const char *option)
{
    size_t i = 0;
    const struct gcv_facetize_option_desc *options = NULL;
    size_t option_cnt = 0;

    if (!option || !method)
        return 0;

    option_cnt = gcv_facetize_method_options(method, &options);
    for (i = 0; i < option_cnt; i++) {
        if (BU_STR_EQUAL(option, options[i].name))
            return 1;
    }

    return 0;
}


int
gcv_facetize_method_option_default_int(const char *method,
                                       const char *option,
                                       int *value_out)
{
    size_t i = 0;
    const struct gcv_facetize_option_desc *options = NULL;
    size_t option_cnt = 0;
    int ival = 0;
    const char *cstr[2] = {NULL, NULL};

    if (!method || !option || !value_out)
        return BRLCAD_ERROR;

    option_cnt = gcv_facetize_method_options(method, &options);
    for (i = 0; i < option_cnt; i++) {
        if (!BU_STR_EQUAL(options[i].name, option))
            continue;
        if (!options[i].default_value)
            return BRLCAD_ERROR;
        cstr[0] = options[i].default_value;
        if (bu_opt_int(NULL, 1, cstr, (void *)&ival) < 0)
            return BRLCAD_ERROR;
        *value_out = ival;
        return BRLCAD_OK;
    }

    return BRLCAD_ERROR;
}


int
gcv_facetize_method_max_time_default(const char *method,
                                     int *value_out)
{
    return gcv_facetize_method_option_default_int(method, "max_time", value_out);
}


int
gcv_facetize_plate_max_time_default(int *value_out)
{
    return gcv_facetize_method_option_default_int("NMG", "plate_max_time", value_out);
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


int
gcv_facetize_resolved_method_names(const struct gcv_facetize_opts *opts,
                                   struct bu_ptbl *method_names)
{
    const struct gcv_facetize_method_info *resolved[32] = {0};
    size_t i = 0;
    size_t mcnt = 0;

    if (!method_names)
        return BRLCAD_ERROR;

    mcnt = gcv_facetize_resolve_methods(opts, resolved, 32);
    if (!mcnt)
        return BRLCAD_ERROR;

    for (i = 0; i < mcnt && i < 32; i++) {
        if (!resolved[i] || !resolved[i]->name)
            continue;
        bu_ptbl_ins(method_names, (long *)bu_strdup(resolved[i]->name));
    }

    return (BU_PTBL_LEN(method_names) > 0) ? BRLCAD_OK : BRLCAD_ERROR;
}


int
gcv_facetize_default_method_names(struct bu_ptbl *method_names)
{
    return gcv_facetize_resolved_method_names(NULL, method_names);
}


void
gcv_facetize_free_string_ptbl(struct bu_ptbl *strings)
{
    size_t i = 0;

    if (!strings)
        return;

    for (i = 0; i < BU_PTBL_LEN(strings); i++) {
        char *s = (char *)BU_PTBL_GET(strings, i);
        if (s)
            bu_free(s, "string ptbl entry");
    }
    bu_ptbl_free(strings);
}


void
gcv_facetize_free_method_names(struct bu_ptbl *method_names)
{
    gcv_facetize_free_string_ptbl(method_names);
}


size_t
gcv_facetize_unknown_methods(const struct gcv_facetize_opts *opts, const char **unknown, size_t max_unknown)
{
    size_t i = 0;
    size_t cnt = 0;

    if (!opts || !opts->methods || !opts->method_count)
        return 0;

    for (i = 0; i < opts->method_count; i++) {
        if (gcv_facetize_method(opts->methods[i]))
            continue;
        if (unknown && cnt < max_unknown)
            unknown[cnt] = opts->methods[i];
        cnt++;
    }

    return cnt;
}


int
gcv_facetize_parse_methods_csv(struct bu_vls *msg,
                               const char *csv,
                               struct bu_ptbl *methods_out)
{
    char *wcsv = NULL;
    char *saveptr = NULL;
    char *tok = NULL;

    if (!csv || !methods_out)
        return BRLCAD_ERROR;

    wcsv = bu_strdup(csv);
    tok = strtok_r(wcsv, ",", &saveptr);
    while (tok) {
        int ret = _gcv_append_method(msg, methods_out, tok);
        if (ret != BRLCAD_OK) {
            bu_free(wcsv, "methods csv");
            return BRLCAD_ERROR;
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }

    bu_free(wcsv, "methods csv");
    return BRLCAD_OK;
}


int
gcv_facetize_parse_method_options(struct bu_vls *msg,
                                  const char *arg,
                                  char **method,
                                  struct gcv_facetize_kv **options,
                                  size_t *option_count)
{
    char *warg = NULL;
    char *saveptr = NULL;
    char *tok = NULL;
    struct gcv_facetize_kv *opts = NULL;
    size_t cnt = 0;

    if (!arg || !method || !options || !option_count)
        return BRLCAD_ERROR;

    *method = NULL;
    *options = NULL;
    *option_count = 0;

    warg = bu_strdup(arg);
    tok = strtok_r(warg, " ", &saveptr);
    if (!tok) {
        bu_free(warg, "method opts string");
        return BRLCAD_OK;
    }

    if (!gcv_facetize_method(tok)) {
        if (msg)
            bu_vls_printf(msg, "Unknown method in --method-opts: %s", tok);
        bu_free(warg, "method opts string");
        return BRLCAD_ERROR;
    }
    *method = bu_strdup(tok);

    tok = strtok_r(NULL, " ", &saveptr);
    while (tok) {
        char *eq = strchr(tok, '=');
        if (!eq) {
            tok = strtok_r(NULL, " ", &saveptr);
            continue;
        }
        *eq = '\0';
        if (!gcv_facetize_method_option_known(*method, tok)) {
            if (msg)
                bu_vls_printf(msg, "Unknown option '%s' for method '%s'", tok, *method);
            bu_free(warg, "method opts string");
            gcv_facetize_free_method_options(opts, cnt);
            bu_free(*method, "method name");
            *method = NULL;
            return BRLCAD_ERROR;
        }
        opts = (struct gcv_facetize_kv *)bu_realloc(opts, sizeof(struct gcv_facetize_kv) * (cnt + 1), "method opts");
        opts[cnt].key = bu_strdup(tok);
        opts[cnt].value = bu_strdup(eq + 1);
        cnt++;
        tok = strtok_r(NULL, " ", &saveptr);
    }

    bu_free(warg, "method opts string");
    *options = opts;
    *option_count = cnt;
    return BRLCAD_OK;
}


void
gcv_facetize_free_method_options(struct gcv_facetize_kv *options,
                                 size_t option_count)
{
    size_t i = 0;

    if (!options)
        return;

    for (i = 0; i < option_count; i++) {
        if (options[i].key)
            bu_free((void *)options[i].key, "option key");
        if (options[i].value)
            bu_free((void *)options[i].value, "option value");
    }
    bu_free(options, "method options array");
}


int
gcv_facetize_kv_int(const struct gcv_facetize_kv *options,
                    size_t option_count,
                    const char *key,
                    int *value_out)
{
    const char *v = NULL;
    const char *cstr[2] = {NULL, NULL};
    int ival = 0;

    if (!options || !key || !value_out)
        return BRLCAD_ERROR;

    v = _gcv_facetize_kv_get(options, option_count, key);
    if (!v)
        return BRLCAD_ERROR;

    cstr[0] = v;
    if (bu_opt_int(NULL, 1, cstr, (void *)&ival) < 0)
        return BRLCAD_ERROR;

    *value_out = ival;
    return BRLCAD_OK;
}


char *
gcv_facetize_process_exec(void)
{
    char tess_exec[MAXPATHLEN] = {0};
    bu_dir(tess_exec, MAXPATHLEN, BU_DIR_BIN, "ged_exec", BU_DIR_EXT, NULL);
    return bu_strdup(tess_exec);
}


size_t
gcv_facetize_process_base_argv(const char **argv,
                               size_t max_argv,
                               const char *exec_path,
                               const char *db_path,
                               int overwrite,
                               const char *method,
                               const char *method_opts,
                               const char *cache_dir)
{
    size_t cnt = 0;

    if (!argv || max_argv < 4 || !exec_path || !db_path)
        return 0;

    argv[cnt++] = exec_path;
    argv[cnt++] = "facetize_process";
    if (overwrite) {
        if (cnt >= max_argv)
            return 0;
        argv[cnt++] = "-O";
    }
    if (cnt >= max_argv)
        return 0;
    argv[cnt++] = db_path;

    if (method) {
        if (cnt + 2 >= max_argv)
            return 0;
        argv[cnt++] = "--methods";
        argv[cnt++] = method;
    }
    if (method_opts) {
        if (cnt + 2 >= max_argv)
            return 0;
        argv[cnt++] = "--method-opts";
        argv[cnt++] = method_opts;
    }
    if (cache_dir) {
        if (cnt + 2 >= max_argv)
            return 0;
        argv[cnt++] = "--cache-dir";
        argv[cnt++] = cache_dir;
    }

    return cnt;
}


int
gcv_facetize_process_method_slots(size_t *method_idx,
                                  size_t *method_opts_idx,
                                  int overwrite)
{
    size_t idx = 0;

    if (!method_idx || !method_opts_idx)
        return BRLCAD_ERROR;

    idx = 0; /* exec */
    idx++;   /* facetize_process */
    if (overwrite)
        idx++; /* -O */
    idx++;   /* db path */
    idx++;   /* --methods */
    *method_idx = idx + 1;
    idx++;   /* method value */
    idx++;   /* --method-opts */
    *method_opts_idx = idx + 1;

    return BRLCAD_OK;
}


int
gcv_facetize_process_set_method_argv(const char **argv,
                                     size_t method_idx,
                                     size_t method_opts_idx,
                                     const char *method,
                                     const char *method_opts)
{
    if (!argv || !method || !method_opts)
        return BRLCAD_ERROR;

    argv[method_idx] = method;
    argv[method_opts_idx] = method_opts;
    return BRLCAD_OK;
}


static void
_gcv_facetize_process_log(gcv_facetize_log_cb log_cb, void *log_ctx, int verbosity, const char *fmt, ...)
{
    struct bu_vls msg = BU_VLS_INIT_ZERO;
    va_list ap;

    if (!log_cb || !fmt)
        return;

    va_start(ap, fmt);
    bu_vls_vprintf(&msg, fmt, ap);
    va_end(ap);
    log_cb(log_ctx, verbosity, bu_vls_cstr(&msg));
    bu_vls_free(&msg);
}


static void
_gcv_facetize_process_drain(struct subprocess_s *p, gcv_facetize_log_cb log_cb, void *log_ctx, int verbosity, int add_newline)
{
    char out[MAXPATHLEN*10] = {'\0'};
    char err[MAXPATHLEN*10] = {'\0'};

    if (!p)
        return;

    subprocess_read_stdout(p, out, MAXPATHLEN*10);
    if (strlen(out))
        _gcv_facetize_process_log(log_cb, log_ctx, verbosity, add_newline ? "%s\n" : "%s", out);
    subprocess_read_stderr(p, err, MAXPATHLEN*10);
    if (strlen(err))
        _gcv_facetize_process_log(log_cb, log_ctx, verbosity, add_newline ? "%s\n" : "%s", err);
}


int
gcv_facetize_process_run(const char **argv,
                         size_t argc,
                         const char *working_file,
                         fastf_t max_time,
                         int object_count,
                         gcv_facetize_log_cb log_cb,
                         void *log_ctx)
{
    char *backup_file = NULL;
    struct bu_vls cmd = BU_VLS_INIT_ZERO;
    struct subprocess_s p;
    int64_t start = 0;
    int w_rc = BRLCAD_ERROR;

    if (!argv || argc == 0 || !working_file)
        return BRLCAD_ERROR;

    backup_file = gcv_facetize_backup_path(working_file);
    if (!backup_file)
        return BRLCAD_ERROR;
    if (gcv_facetize_file_copy(working_file, backup_file) != BRLCAD_OK) {
        bu_log("Unable to create backup file %s\n", backup_file);
        bu_free(backup_file, "working file backup path");
        return BRLCAD_ERROR;
    }

    for (size_t i = 0; i < argc; i++)
        bu_vls_printf(&cmd, "%s ", argv[i]);
    _gcv_facetize_process_log(log_cb, log_ctx, 2, "%s\n", bu_vls_cstr(&cmd));
    bu_vls_free(&cmd);

    if (object_count == 1)
        _gcv_facetize_process_log(log_cb, log_ctx, 1, "Attempting to triangulate %s...", argv[argc - 1]);
    if (object_count > 1)
        _gcv_facetize_process_log(log_cb, log_ctx, 1, "Attempting to triangulate %d solids...", object_count);

    argv[argc] = NULL;
    if (subprocess_create(argv, subprocess_option_no_window|subprocess_option_enable_async|subprocess_option_inherit_environment, &p)) {
        _gcv_facetize_process_log(log_cb, log_ctx, 0, " FAILED.\n");
        _gcv_facetize_process_log(log_cb, log_ctx, 0, "Unable to create subprocess\n");
        bu_free(backup_file, "working file backup path");
        return BRLCAD_ERROR;
    }

    start = bu_gettime();
    while (subprocess_alive(&p)) {
        int64_t elapsed = 0;
        fastf_t seconds = 0.0;

        bu_snooze(100000);
        elapsed = bu_gettime() - start;
        seconds = elapsed / 1000000.0;

        _gcv_facetize_process_drain(&p, log_cb, log_ctx, 1, 0);

        if (seconds > max_time) {
            subprocess_terminate(&p);
            _gcv_facetize_process_log(log_cb, log_ctx, 0, " FAILED.\n");
            _gcv_facetize_process_log(log_cb, log_ctx, 0, "facetize subprocess killed %g %g\n", seconds, max_time);
            _gcv_facetize_process_drain(&p, log_cb, log_ctx, 0, 1);
            subprocess_destroy(&p);

            if (gcv_facetize_file_copy(backup_file, working_file) != BRLCAD_OK) {
                bu_free(backup_file, "working file backup path");
                return BRLCAD_ERROR;
            }

            bu_free(backup_file, "working file backup path");
            return BRLCAD_ERROR;
        }
    }

    if (subprocess_join(&p, &w_rc)) {
        _gcv_facetize_process_log(log_cb, log_ctx, 0, " FAILED.\n");
        _gcv_facetize_process_log(log_cb, log_ctx, 0, "facetize subprocess unable to join\n");
        _gcv_facetize_process_drain(&p, log_cb, log_ctx, 0, 1);
        bu_file_delete(backup_file);
        bu_free(backup_file, "working file backup path");
        return BRLCAD_ERROR;
    }

    bu_file_delete(backup_file);
    bu_free(backup_file, "working file backup path");

    _gcv_facetize_process_drain(&p, log_cb, log_ctx, 0, 1);
    subprocess_destroy(&p);

    if (w_rc == BRLCAD_OK)
        _gcv_facetize_process_log(log_cb, log_ctx, 1, " Success.\n");
    else
        _gcv_facetize_process_log(log_cb, log_ctx, 0, " FAILED.\n");

    return w_rc ? BRLCAD_ERROR : BRLCAD_OK;
}


static int
_gcv_facetize_process_run_objects_internal(const char **base_argv,
                                           size_t base_argc,
                                           const char *working_file,
                                           const char **object_names,
                                           size_t object_count,
                                           fastf_t max_time,
                                           int bisect_failures,
                                           struct bu_ptbl *bad_object_names,
                                           gcv_facetize_log_cb log_cb,
                                           void *log_ctx)
{
    const char **argv = NULL;
    int ret = BRLCAD_ERROR;

    if (!base_argv || !base_argc || !working_file || !object_names || !object_count)
        return -1;

    argv = (const char **)bu_calloc(base_argc + object_count + 1, sizeof(const char *), "facetize process object argv");
    for (size_t i = 0; i < base_argc; i++)
        argv[i] = base_argv[i];
    for (size_t i = 0; i < object_count; i++)
        argv[base_argc + i] = object_names[i];

    ret = gcv_facetize_process_run(argv, base_argc + object_count, working_file, max_time, (int)object_count, log_cb, log_ctx);
    bu_free(argv, "facetize process object argv");

    if (ret == BRLCAD_OK)
        return 0;

    if (bisect_failures && object_count > 1) {
        size_t left_cnt = object_count / 2;
        size_t right_cnt = object_count - left_cnt;
        int lret = _gcv_facetize_process_run_objects_internal(base_argv, base_argc, working_file, object_names, left_cnt, max_time, bisect_failures, bad_object_names, log_cb, log_ctx);
        int rret = _gcv_facetize_process_run_objects_internal(base_argv, base_argc, working_file, object_names + left_cnt, right_cnt, max_time, bisect_failures, bad_object_names, log_cb, log_ctx);
        if (lret < 0 || rret < 0)
            return -1;
        return lret + rret;
    }

    if (bad_object_names && object_names[0])
        bu_ptbl_ins(bad_object_names, (long *)bu_strdup(object_names[0]));

    return 1;
}


int
gcv_facetize_process_run_objects(const char **base_argv,
                                 size_t base_argc,
                                 const char *working_file,
                                 const char **object_names,
                                 size_t object_count,
                                 fastf_t max_time,
                                 int bisect_failures,
                                 struct bu_ptbl *bad_object_names,
                                 gcv_facetize_log_cb log_cb,
                                 void *log_ctx)
{
    return _gcv_facetize_process_run_objects_internal(base_argv,
            base_argc,
            working_file,
            object_names,
            object_count,
            max_time,
            bisect_failures,
            bad_object_names,
            log_cb,
            log_ctx);
}


static size_t
_gcv_facetize_process_method_base_argv(const char **base_argv,
                                       size_t max_argv,
                                       const char *working_file,
                                       int overwrite,
                                       const char *method,
                                       const struct gcv_facetize_method_opts_state *method_opts,
                                       struct db_i *db,
                                       long nmg_debug_flag,
                                       const char *cache_dir,
                                       char **exec_path_out,
                                       char **method_opts_arg_out)
{
    char *exec_path = NULL;
    char *method_opts_arg = NULL;
    size_t base_argc = 0;

    if (exec_path_out)
        *exec_path_out = NULL;
    if (method_opts_arg_out)
        *method_opts_arg_out = NULL;
    if (!base_argv || !working_file || !method)
        return 0;

    exec_path = gcv_facetize_process_exec();
    if (!exec_path)
        return 0;

    method_opts_arg = gcv_facetize_method_opts_string(method_opts, method, db, nmg_debug_flag);
    if (!method_opts_arg) {
        bu_free(exec_path, "facetize process exec path");
        return 0;
    }

    base_argc = gcv_facetize_process_base_argv(base_argv,
            max_argv,
            exec_path,
            working_file,
            overwrite,
            method,
            method_opts_arg,
            cache_dir);
    if (!base_argc) {
        bu_free(method_opts_arg, "facetize method opts arg");
        bu_free(exec_path, "facetize process exec path");
        return 0;
    }

    if (exec_path_out) {
        *exec_path_out = exec_path;
    } else {
        bu_free(exec_path, "facetize process exec path");
    }
    if (method_opts_arg_out) {
        *method_opts_arg_out = method_opts_arg;
    } else {
        bu_free(method_opts_arg, "facetize method opts arg");
    }

    return base_argc;
}


int
gcv_facetize_process_run_method_objects(const char *working_file,
                                        int overwrite,
                                        const char *method,
                                        const struct gcv_facetize_method_opts_state *method_opts,
                                        struct db_i *db,
                                        long nmg_debug_flag,
                                        const char *cache_dir,
                                        const char **object_names,
                                        size_t object_count,
                                        fastf_t max_time,
                                        int bisect_failures,
                                        struct bu_ptbl *bad_object_names,
                                        gcv_facetize_log_cb log_cb,
                                        void *log_ctx)
{
    const char *base_argv[MAXPATHLEN] = {NULL};
    char *exec_path = NULL;
    char *method_opts_arg = NULL;
    size_t base_argc = 0;
    int ret = BRLCAD_ERROR;

    if (!working_file || !method || !object_names || !object_count)
        return -1;

    base_argc = _gcv_facetize_process_method_base_argv(base_argv,
            MAXPATHLEN,
            working_file,
            overwrite,
            method,
            method_opts,
            db,
            nmg_debug_flag,
            cache_dir,
            &exec_path,
            &method_opts_arg);
    if (!base_argc)
        return BRLCAD_ERROR;

    ret = gcv_facetize_process_run_objects(base_argv,
            base_argc,
            working_file,
            object_names,
            object_count,
            max_time,
            bisect_failures,
            bad_object_names,
            log_cb,
            log_ctx);

    bu_free(method_opts_arg, "facetize method opts arg");
    bu_free(exec_path, "facetize process exec path");
    return ret;
}


size_t
gcv_facetize_process_method_argv_len(size_t *argc_out,
                                     const char *working_file,
                                     int overwrite,
                                     const char *method,
                                     const struct gcv_facetize_method_opts_state *method_opts,
                                     struct db_i *db,
                                     long nmg_debug_flag,
                                     const char *cache_dir)
{
    const char *base_argv[MAXPATHLEN] = {NULL};
    char *exec_path = NULL;
    char *method_opts_arg = NULL;
    size_t base_argc = 0;
    size_t len = 0;

    if (argc_out)
        *argc_out = 0;

    base_argc = _gcv_facetize_process_method_base_argv(base_argv,
            MAXPATHLEN,
            working_file,
            overwrite,
            method,
            method_opts,
            db,
            nmg_debug_flag,
            cache_dir,
            &exec_path,
            &method_opts_arg);
    if (!base_argc)
        return 0;

    for (size_t i = 0; i < base_argc; i++)
        len += strlen(base_argv[i]) + 1;

    bu_free(method_opts_arg, "facetize method opts arg");
    bu_free(exec_path, "facetize process exec path");

    if (argc_out)
        *argc_out = base_argc;
    return len;
}


static int
_gcv_facetize_process_run_method_batch(const char *working_file,
                                       int overwrite,
                                       const char *method,
                                       const struct gcv_facetize_method_opts_state *method_opts,
                                       struct db_i *db,
                                       long nmg_debug_flag,
                                       const char *cache_dir,
                                       const char * const *object_names,
                                       size_t object_count,
                                       fastf_t max_time,
                                       int scale_time_by_object_count,
                                       struct bu_ptbl *bad_object_names,
                                       gcv_facetize_log_cb log_cb,
                                       void *log_ctx)
{
    if (!method || !object_names || !object_count)
        return -1;

    if (scale_time_by_object_count)
        max_time *= (fastf_t)object_count;

    if (BU_STR_EQUAL(method, "NMG")) {
        return gcv_facetize_process_run_method_objects(working_file,
                overwrite,
                method,
                method_opts,
                db,
                nmg_debug_flag,
                cache_dir,
                (const char **)object_names,
                object_count,
                max_time,
                1,
                bad_object_names,
                log_cb,
                log_ctx);
    }

    int fail_cnt = 0;
    for (size_t i = 0; i < object_count; i++) {
        const char *one_name[1] = {object_names[i]};
        int ret = gcv_facetize_process_run_method_objects(working_file,
                overwrite,
                method,
                method_opts,
                db,
                nmg_debug_flag,
                cache_dir,
                one_name,
                1,
                max_time,
                0,
                NULL,
                log_cb,
                log_ctx);
        if (ret != BRLCAD_OK) {
            if (bad_object_names && object_names[i])
                bu_ptbl_ins(bad_object_names, (long *)bu_strdup(object_names[i]));
            fail_cnt++;
        }
    }

    return fail_cnt;
}


static size_t
_gcv_facetize_process_select_batch(const char *working_file,
                                   int overwrite,
                                   const char * const *methods,
                                   size_t method_count,
                                   const struct gcv_facetize_method_opts_state *method_opts,
                                   struct db_i *db,
                                   long nmg_debug_flag,
                                   const char *cache_dir,
                                   const char * const *object_names,
                                   size_t object_count,
                                   size_t start_idx,
                                   size_t max_argv,
                                   size_t max_cmd_len)
{
    size_t batch_count = 0;

    if (!methods || !method_count || !object_names || start_idx >= object_count)
        return 0;

    while (start_idx + batch_count < object_count) {
        const char *obj_name = object_names[start_idx + batch_count];
        int fits = 1;

        if (!obj_name)
            return batch_count;

        for (size_t i = 0; i < method_count; i++) {
            size_t base_argc = 0;
            size_t cmd_len = gcv_facetize_process_method_argv_len(&base_argc,
                    working_file,
                    overwrite,
                    methods[i],
                    method_opts,
                    db,
                    nmg_debug_flag,
                    cache_dir);
            if (!cmd_len)
                return 0;
            if (base_argc + batch_count + 1 >= max_argv)
                fits = 0;
            if (cmd_len + strlen(obj_name) + 1 > max_cmd_len)
                fits = 0;
            for (size_t j = 0; j < batch_count; j++)
                cmd_len += strlen(object_names[start_idx + j]) + 1;
            if (cmd_len + strlen(obj_name) + 1 > max_cmd_len)
                fits = 0;
        }

        if (!fits)
            break;

        batch_count++;
    }

    return batch_count;
}


int
gcv_facetize_process_run_methods(const char *working_file,
                                 int overwrite,
                                 const char * const *methods,
                                 size_t method_count,
                                 const struct gcv_facetize_method_opts_state *method_opts,
                                 struct db_i *db,
                                 long nmg_debug_flag,
                                 const char *cache_dir,
                                 const char * const *object_names,
                                 size_t object_count,
                                 size_t max_argv,
                                 size_t max_cmd_len,
                                 int plate_mode,
                                 int scale_time_by_object_count,
                                 struct bu_ptbl *bad_object_names,
                                 gcv_facetize_log_cb log_cb,
                                 void *log_ctx)
{
    size_t obj_idx = 0;
    int fail_cnt = 0;

    if (!working_file || !methods || !method_count || !object_names || !object_count || !max_argv || !max_cmd_len)
        return -1;

    while (obj_idx < object_count) {
        size_t batch_count = _gcv_facetize_process_select_batch(working_file,
                overwrite,
                methods,
                method_count,
                method_opts,
                db,
                nmg_debug_flag,
                cache_dir,
                object_names,
                object_count,
                obj_idx,
                max_argv,
                max_cmd_len);
        const char **retry_names = NULL;
        size_t retry_count = 0;
        int batch_ret = 0;

        if (!batch_count)
            return -1;

        retry_count = batch_count;
        retry_names = (const char **)bu_calloc(retry_count, sizeof(const char *), "facetize retry names");
        for (size_t i = 0; i < retry_count; i++)
            retry_names[i] = bu_strdup(object_names[obj_idx + i]);

        for (size_t method_idx = 0; method_idx < method_count && retry_count > 0; method_idx++) {
            struct bu_ptbl method_bad_names = BU_PTBL_INIT_ZERO;
            fastf_t max_time = gcv_facetize_method_opts_time_limit(method_opts, methods[method_idx], plate_mode);

            batch_ret = _gcv_facetize_process_run_method_batch(working_file,
                    overwrite,
                    methods[method_idx],
                    method_opts,
                    db,
                    nmg_debug_flag,
                    cache_dir,
                    retry_names,
                    retry_count,
                    max_time,
                    scale_time_by_object_count,
                    &method_bad_names,
                    log_cb,
                    log_ctx);
            if (batch_ret < 0) {
                gcv_facetize_free_string_ptbl(&method_bad_names);
                for (size_t i = 0; i < retry_count; i++)
                    bu_free((char *)retry_names[i], "facetize retry name");
                bu_free(retry_names, "facetize retry names");
                return -1;
            }

            if (batch_ret == 0) {
                gcv_facetize_free_string_ptbl(&method_bad_names);
                for (size_t i = 0; i < retry_count; i++)
                    bu_free((char *)retry_names[i], "facetize retry name");
                retry_count = 0;
                break;
            }

            for (size_t i = 0; i < retry_count; i++)
                bu_free((char *)retry_names[i], "facetize retry name");
            bu_free(retry_names, "facetize retry names");
            retry_count = BU_PTBL_LEN(&method_bad_names);
            retry_names = (const char **)bu_calloc(retry_count, sizeof(const char *), "facetize retry names");
            for (size_t i = 0; i < retry_count; i++)
                retry_names[i] = bu_strdup((const char *)BU_PTBL_GET(&method_bad_names, i));

            if (method_idx + 1 == method_count) {
                for (size_t i = 0; i < retry_count; i++) {
                    if (bad_object_names && retry_names[i])
                        bu_ptbl_ins(bad_object_names, (long *)bu_strdup(retry_names[i]));
                    fail_cnt++;
                }
            }

            gcv_facetize_free_string_ptbl(&method_bad_names);
        }

        for (size_t i = 0; i < retry_count; i++)
            bu_free((char *)retry_names[i], "facetize retry name");
        bu_free(retry_names, "facetize retry names");
        obj_idx += batch_count;
    }

    return fail_cnt;
}


char *
gcv_facetize_backup_path(const char *path)
{
    struct bu_vls bpath = BU_VLS_INIT_ZERO;

    if (!path)
        return NULL;

    bu_vls_printf(&bpath, "%s.bak", path);
    return bu_vls_strgrab(&bpath);
}


int
gcv_facetize_file_copy(const char *src, const char *dst)
{
    FILE *in = NULL;
    FILE *out = NULL;
    char buf[8192];
    size_t nread = 0;
    int ret = BRLCAD_OK;

    if (!src || !dst)
        return BRLCAD_ERROR;

    in = fopen(src, "rb");
    if (!in)
        return BRLCAD_ERROR;

    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return BRLCAD_ERROR;
    }

    while ((nread = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, nread, out) != nread) {
            ret = BRLCAD_ERROR;
            break;
        }
    }

    if (ferror(in))
        ret = BRLCAD_ERROR;
    if (fclose(out) != 0)
        ret = BRLCAD_ERROR;
    if (fclose(in) != 0)
        ret = BRLCAD_ERROR;

    return ret;
}


char *
gcv_facetize_method_optstr(const char *method,
                           const struct gcv_facetize_kv *options,
                           size_t option_count,
                           struct db_i *db,
                           long nmg_debug_flag)
{
    struct bu_vls moptstr = BU_VLS_INIT_ZERO;
    size_t i = 0;

    if (!method)
        return bu_strdup("");

    bu_vls_printf(&moptstr, "%s", method);
    for (i = 0; i < option_count; i++) {
        if (!options || !options[i].key)
            continue;
        bu_vls_printf(&moptstr, " %s=%s", options[i].key,
                      options[i].value ? options[i].value : "");
    }

    if (BU_STR_EQUAL(method, "NMG")) {
        if (!_gcv_facetize_kv_get(options, option_count, "nmg_debug")) {
            struct bu_vls debug_str = BU_VLS_INIT_ZERO;
            bu_vls_sprintf(&debug_str, "0x%08lx", (unsigned long)nmg_debug_flag);
            bu_vls_printf(&moptstr, " nmg_debug=%s", bu_vls_cstr(&debug_str));
            bu_vls_free(&debug_str);
        }

        if (db) {
            struct rt_wdb *wdbp = wdb_dbopen(db, RT_WDB_TYPE_DB_DEFAULT);
            if (wdbp) {
                if (!_gcv_facetize_kv_get(options, option_count, "tol_abs"))
                    bu_vls_printf(&moptstr, " tol_abs=%0.17f", wdbp->wdb_ttol.abs);
                if (!_gcv_facetize_kv_get(options, option_count, "tol_rel"))
                    bu_vls_printf(&moptstr, " tol_rel=%0.17f", wdbp->wdb_ttol.rel);
                if (!_gcv_facetize_kv_get(options, option_count, "tol_norm"))
                    bu_vls_printf(&moptstr, " tol_norm=%0.17f", wdbp->wdb_ttol.norm);
            }
        }
    }

    return bu_vls_strgrab(&moptstr);
}


char *
gcv_facetize_quote_arg(const char *arg)
{
    struct bu_vls quoted = BU_VLS_INIT_ZERO;

    if (!arg)
        return bu_strdup("\"\"");

    bu_vls_printf(&quoted, "\"%s\"", arg);
    return bu_vls_strgrab(&quoted);
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
	    const char *default_value = opt->default_value ? opt->default_value : "(no default)";
	    bu_vls_printf(description, "  %s (default %s): %s\n", opt->name, default_value, opt->description);
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


static struct rt_bot_internal *
_gcv_facetize_to_bot_nmg(struct db_i *db, const struct db_full_path *path,
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


struct rt_bot_internal *
gcv_facetize_to_bot(struct db_i *db,
                    const struct db_full_path *path,
                    const struct gcv_facetize_opts *opts,
                    const struct bn_tol *tol,
                    const struct bg_tess_tol *tess_tol,
                    struct bu_list *vlfree)
{
    struct gcv_facetize_opts default_opts;

    if (!db || !path || !tol || !tess_tol)
        return NULL;

    if (!opts) {
        gcv_facetize_opts_default(&default_opts);
        opts = &default_opts;
    }

    if (gcv_facetize_opts_validate(opts, NULL) != BRLCAD_OK) {
        if (opts == &default_opts)
            bu_avs_free(&default_opts.method_options);
        return NULL;
    }

    if (opts->output_mode != GCV_FACETIZE_OUTPUT_SINGLE_BOT) {
        if (opts == &default_opts)
            bu_avs_free(&default_opts.method_options);
        return NULL;
    }

    if (opts->boolean_engine != GCV_FACETIZE_BOOL_NMG && opts->boolean_engine != GCV_FACETIZE_BOOL_MANIFOLD) {
        if (opts == &default_opts)
            bu_avs_free(&default_opts.method_options);
        return NULL;
    }

    /* Until the robust Manifold path is fully moved, this public high-level
     * entry point preserves existing libgcv behavior by using the NMG helper. */
    {
        struct rt_bot_internal *bot = _gcv_facetize_to_bot_nmg(db, path, tol, tess_tol, vlfree);
        if (opts == &default_opts)
            bu_avs_free(&default_opts.method_options);
        return bot;
    }
}


struct rt_bot_internal *
gcv_facetize(struct db_i *db, const struct db_full_path *path,
	     const struct bn_tol *tol, const struct bg_tess_tol *tess_tol, struct bu_list *vlfree)
{
    return gcv_facetize_to_bot(db, path, NULL, tol, tess_tol, vlfree);
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
