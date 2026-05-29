/*                    F A C E T I Z E . H
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
/** @file gcv/facetize.h
 *
 * Public facetization API and metadata for LIBGCV.
 *
 */

#ifndef GCV_FACETIZE_H
#define GCV_FACETIZE_H

#include "common.h"

#include "bu/avs.h"
#include "bu/vls.h"
#include "bn/tol.h"
#include "gcv/defines.h"
#include "rt/geom.h"

__BEGIN_DECLS

struct db_i;
struct db_full_path;
struct rt_bot_internal;

enum gcv_facetize_output_mode {
    GCV_FACETIZE_OUTPUT_SINGLE_BOT = 0,
    GCV_FACETIZE_OUTPUT_REGION_BOTS,
    GCV_FACETIZE_OUTPUT_NMG,
    GCV_FACETIZE_OUTPUT_REPLACE_DB,
    GCV_FACETIZE_OUTPUT_TARGET_DB
};

enum gcv_facetize_boolean_engine {
    GCV_FACETIZE_BOOL_MANIFOLD = 0,
    GCV_FACETIZE_BOOL_NMG
};

enum gcv_facetize_option_type {
    GCV_FACETIZE_OPT_BOOL = 0,
    GCV_FACETIZE_OPT_INT,
    GCV_FACETIZE_OPT_FASTF,
    GCV_FACETIZE_OPT_STRING
};

enum gcv_facetize_capability {
    GCV_FACETIZE_CAP_SUBPROCESS_RECOMMENDED = 1 << 0,
    GCV_FACETIZE_CAP_CONSUMES_POINT_SAMPLES = 1 << 1,
    GCV_FACETIZE_CAP_REPAIRS_BOTS = 1 << 2,
    GCV_FACETIZE_CAP_DETERMINISTIC = 1 << 3,
    GCV_FACETIZE_CAP_RESUMABLE = 1 << 4
};

typedef void (*gcv_facetize_log_cb)(void *client_data, int level, const char *msg);
typedef void (*gcv_facetize_progress_cb)(void *client_data, const char *stage, size_t current, size_t total);
typedef int (*gcv_facetize_cancel_cb)(void *client_data);
typedef void (*gcv_facetize_method_result_cb)(void *client_data, const char *method, const char *object_name, int status);

struct gcv_facetize_option_desc {
    const char *name;
    enum gcv_facetize_option_type type;
    const char *default_value;
    const char *description;
};

struct gcv_facetize_method_info {
    const char *name;
    const char *description;
    const char *supported_data;
    int fallback_rank;
    unsigned capabilities;
    const struct gcv_facetize_option_desc *options;
    size_t option_count;
};

struct gcv_facetize_step_info {
    const char *name;
    const char *description;
    int fallback_rank;
    unsigned capabilities;
    const struct gcv_facetize_option_desc *options;
    size_t option_count;
};

struct gcv_facetize_opts {
    enum gcv_facetize_output_mode output_mode;
    enum gcv_facetize_boolean_engine boolean_engine;

    const char * const *methods;
    size_t method_count;
    bu_avs_t method_options;

    int subprocess;
    int max_time;
    int per_method_max_time;
    const char *resume_dir;
    const char *log_file;

    int no_empty;
    int disable_fixup;
    int perturb;
    fastf_t perturb_volume_threshold;
    fastf_t perturb_surface_area_threshold;
    int max_sampled_points;

    gcv_facetize_log_cb log_cb;
    gcv_facetize_progress_cb progress_cb;
    gcv_facetize_cancel_cb cancel_cb;
    gcv_facetize_method_result_cb method_result_cb;
    void *callback_data;
};

GCV_EXPORT extern void
gcv_facetize_opts_default(struct gcv_facetize_opts *opts);

GCV_EXPORT extern size_t
gcv_facetize_methods(const struct gcv_facetize_method_info **methods);

GCV_EXPORT extern const struct gcv_facetize_method_info *
gcv_facetize_method(const char *name);

GCV_EXPORT extern size_t
gcv_facetize_method_options(const char *method, const struct gcv_facetize_option_desc **options);

GCV_EXPORT extern size_t
gcv_facetize_resolve_methods(const struct gcv_facetize_opts *opts, const struct gcv_facetize_method_info **methods, size_t max_methods);

GCV_EXPORT extern size_t
gcv_facetize_boolean_evaluators(const struct gcv_facetize_step_info **evaluators);

GCV_EXPORT extern size_t
gcv_facetize_postprocess_steps(const struct gcv_facetize_step_info **steps);

GCV_EXPORT extern void
gcv_facetize_describe_options(struct bu_vls *description);

/*
 * Tessellate the object at the specified path.  This legacy helper remains
 * available while callers migrate to struct gcv_facetize_opts based APIs.
 */
GCV_EXPORT extern struct rt_bot_internal *
gcv_facetize(struct db_i *db, const struct db_full_path *path, const struct bn_tol *tol, const struct bg_tess_tol *tess_tol, struct bu_list *vlfree);

__END_DECLS

#endif /* GCV_FACETIZE_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
