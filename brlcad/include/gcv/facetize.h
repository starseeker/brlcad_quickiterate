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

#define GCV_FACETIZE_METHOD_ATTR "facetize_method"

#include "common.h"

#include "bu/avs.h"
#include "bu/ptbl.h"
#include "bu/vls.h"
#include "bn/tol.h"
#include "gcv/defines.h"
#include "rt/geom.h"

__BEGIN_DECLS

struct db_i;
struct db_full_path;
struct rt_bot_internal;
struct rt_wdb;
struct directory;
struct bu_list;

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
typedef const char *(*gcv_facetize_variant_name_cb)(void *client_data, const char *path, int is_subtractive);
typedef struct rt_bot_internal *(*gcv_facetize_bot_fixup_cb)(void *client_data, struct db_i *db, struct directory *bot_dp, const char *bot_name);
typedef int (*gcv_facetize_working_file_setup_cb)(void *client_data, struct bu_ptbl *leaf_dps);
typedef const char *(*gcv_facetize_working_file_cb)(void *client_data);
typedef void (*gcv_facetize_variant_plan_reset_cb)(void *client_data);
typedef int (*gcv_facetize_variant_plan_build_cb)(void *client_data, int argc, struct directory **dpa);
typedef int (*gcv_facetize_primitive_tessellate_cb)(void *client_data, struct db_i *source_db, struct bu_ptbl *leaf_dps);
typedef int (*gcv_facetize_variant_tessellate_cb)(void *client_data);
typedef int (*gcv_facetize_validate_objects_cb)(void *client_data, int argc, const char **argv, int new_object_count);
typedef int (*gcv_facetize_nmg_object_eval_cb)(void *client_data, int argc, const char **argv, const char *output_name);
typedef int (*gcv_facetize_manifold_object_eval_cb)(void *client_data, int argc, struct directory **dpa, const char *output_name, int output_to_working, int cleanup);
typedef void (*gcv_facetize_summary_cb)(void *client_data);
typedef void (*gcv_facetize_cleanup_cb)(void *client_data);

struct gcv_facetize_object_callbacks {
    gcv_facetize_validate_objects_cb validate_objects;
    gcv_facetize_nmg_object_eval_cb nmg_eval;
    gcv_facetize_manifold_object_eval_cb manifold_eval;
    gcv_facetize_summary_cb primitive_summary;
    gcv_facetize_cleanup_cb cleanup;
};

typedef int (*gcv_facetize_region_args_cb)(void *client_data, int argc, const char **argv, int new_object_count);
typedef int (*gcv_facetize_region_object_fallback_cb)(void *client_data, int argc, const char **argv);
typedef int (*gcv_facetize_set_working_file_cb)(void *client_data, const char *working_file);
typedef int (*gcv_facetize_region_nmg_eval_cb)(void *client_data, struct db_i *working_db, const char *root_name, const char *result_name);
typedef int (*gcv_facetize_region_manifold_eval_cb)(void *client_data, struct db_i *working_db, struct rt_wdb *wdbp, const char *root_name, const char *result_name, size_t current, size_t total);
typedef int (*gcv_facetize_region_validate_cb)(void *client_data, const char *root_name, const char *result_name, struct db_i **working_db, struct rt_wdb **wdbp, size_t current, size_t total, int *eval_status);
typedef void (*gcv_facetize_use_variant_plan_cb)(void *client_data, int enabled);

typedef void (*gcv_facetize_region_summary_cb)(void *client_data, size_t evaluated_root_count);

struct gcv_facetize_region_callbacks {
    gcv_facetize_region_args_cb validate_args;
    gcv_facetize_region_object_fallback_cb object_fallback;
    gcv_facetize_set_working_file_cb set_working_file;
    gcv_facetize_working_file_setup_cb working_file_setup;
    gcv_facetize_primitive_tessellate_cb primitive_tessellate;
    gcv_facetize_region_nmg_eval_cb nmg_eval;
    gcv_facetize_region_manifold_eval_cb manifold_eval;
    gcv_facetize_region_validate_cb validate_region;
    gcv_facetize_use_variant_plan_cb use_variant_plan;
    gcv_facetize_summary_cb primitive_summary;
    gcv_facetize_region_summary_cb region_summary;
    gcv_facetize_summary_cb variant_summary;
    gcv_facetize_cleanup_cb cleanup;
};

struct gcv_facetize_db_opts {
    int region_mode;
    int in_place;
    int make_nmg;
    int nmg_booleval;
    int no_perturb;
    int verbosity;
    const char *working_dir;
    const char *base_name;
    const char *prefix;
    const char *suffix;
};

struct gcv_facetize_db_callbacks {
    struct gcv_facetize_object_callbacks objects;
    void *object_data;
    struct gcv_facetize_region_callbacks regions;
    void *region_data;
};

struct gcv_facetize_import_result {
    struct bu_ptbl top_names;
};

struct gcv_facetize_manifold_object_callbacks {
    gcv_facetize_working_file_setup_cb working_file_setup;
    gcv_facetize_working_file_cb working_file;
    gcv_facetize_variant_plan_reset_cb variant_plan_reset;
    gcv_facetize_variant_plan_build_cb variant_plan_build;
    gcv_facetize_primitive_tessellate_cb primitive_tessellate;
    gcv_facetize_variant_tessellate_cb variant_tessellate;
};

struct gcv_facetize_option_desc {
    const char *name;
    enum gcv_facetize_option_type type;
    const char *default_value;
    const char *description;
};

struct gcv_facetize_kv {
    const char *key;
    const char *value;
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

struct gcv_facetize_sample_opts {
    fastf_t feature_scale;
    fastf_t feature_size;
    fastf_t d_feature_size;
    int max_sample_time;
    int max_pnts;

    double obj_bbox_vol;
    double pnts_bbox_vol;
    fastf_t target_feature_size;
    fastf_t avg_thickness;
};

struct gcv_facetize_nmg_opts {
    struct bn_tol tol;
    struct bg_tess_tol ttol;
    long nmg_debug;
    int max_time;
    int plate_max_time;
};

struct gcv_facetize_cm_opts {
    struct gcv_facetize_sample_opts sample;
    int max_cycle_time;
    int max_time;
};

struct gcv_facetize_spsr_opts {
    struct gcv_facetize_sample_opts sample;
    int depth;
    fastf_t interpolate;
    fastf_t samples_per_node;
    int max_time;
};

struct gcv_facetize_process_opts {
    struct gcv_facetize_nmg_opts nmg;
    struct gcv_facetize_cm_opts cm;
    struct gcv_facetize_spsr_opts spsr;
    struct gcv_facetize_sample_opts pnts;
};

struct gcv_facetize_method_option_state;

struct gcv_facetize_method_opts_state {
    struct bu_ptbl methods;
    struct bu_ptbl options;
    int max_time_nmg;
    int max_time_cm;
    int max_time_spsr;
    int plate_max_time;
};

GCV_EXPORT extern void
gcv_facetize_opts_default(struct gcv_facetize_opts *opts);

GCV_EXPORT extern int
gcv_facetize_opts_validate(const struct gcv_facetize_opts *opts, struct bu_vls *msg);

GCV_EXPORT extern void
gcv_facetize_sample_opts_default(struct gcv_facetize_sample_opts *opts);

GCV_EXPORT extern void
gcv_facetize_nmg_opts_default(struct gcv_facetize_nmg_opts *opts);

GCV_EXPORT extern void
gcv_facetize_cm_opts_default(struct gcv_facetize_cm_opts *opts);

GCV_EXPORT extern void
gcv_facetize_spsr_opts_default(struct gcv_facetize_spsr_opts *opts);

GCV_EXPORT extern void
gcv_facetize_method_opts_state_init(struct gcv_facetize_method_opts_state *opts);

GCV_EXPORT extern void
gcv_facetize_method_opts_state_free(struct gcv_facetize_method_opts_state *opts);

GCV_EXPORT extern int
gcv_facetize_method_opts_parse_methods(struct bu_vls *msg,
                                       const char *arg,
                                       struct gcv_facetize_method_opts_state *opts);

GCV_EXPORT extern int
gcv_facetize_method_opts_parse_options(struct bu_vls *msg,
                                       const char *arg,
                                       struct gcv_facetize_method_opts_state *opts);

GCV_EXPORT extern int
gcv_facetize_method_opts_opt_methods(struct bu_vls *msg,
                                     size_t argc,
                                     const char **argv,
                                     void *set_var);

GCV_EXPORT extern int
gcv_facetize_method_opts_opt_options(struct bu_vls *msg,
                                     size_t argc,
                                     const char **argv,
                                     void *set_var);

GCV_EXPORT extern size_t
gcv_facetize_method_opts_method_count(const struct gcv_facetize_method_opts_state *opts);

GCV_EXPORT extern const char *
gcv_facetize_method_opts_method_name(const struct gcv_facetize_method_opts_state *opts,
                                     size_t idx);

GCV_EXPORT extern size_t
gcv_facetize_method_opts_method_options(const struct gcv_facetize_method_opts_state *opts,
                                        const char *method,
                                        struct gcv_facetize_kv **options);

GCV_EXPORT extern int
gcv_facetize_method_opts_has_option(const struct gcv_facetize_method_opts_state *opts,
                                    const char *method,
                                    const char *key);

GCV_EXPORT extern int
gcv_facetize_method_opts_set_option(struct gcv_facetize_method_opts_state *opts,
                                    const char *method,
                                    const char *key,
                                    const char *value);

GCV_EXPORT extern int
gcv_facetize_method_opts_time_limit(const struct gcv_facetize_method_opts_state *opts,
                                    const char *method,
                                    int plate_mode);

GCV_EXPORT extern char *
gcv_facetize_method_opts_string(const struct gcv_facetize_method_opts_state *opts,
                                const char *method,
                                struct db_i *db,
                                long nmg_debug_flag);

GCV_EXPORT extern int
gcv_facetize_sample_opts_set(struct gcv_facetize_sample_opts *opts, const char *key, const char *val);

GCV_EXPORT extern int
gcv_facetize_nmg_opts_set(struct gcv_facetize_nmg_opts *opts, const char *key, const char *val);

GCV_EXPORT extern int
gcv_facetize_cm_opts_set(struct gcv_facetize_cm_opts *opts, const char *key, const char *val);

GCV_EXPORT extern int
gcv_facetize_spsr_opts_set(struct gcv_facetize_spsr_opts *opts, const char *key, const char *val);

GCV_EXPORT extern void
gcv_facetize_sample_opts_copy(struct gcv_facetize_sample_opts *dst,
                              const struct gcv_facetize_sample_opts *src);

GCV_EXPORT extern int
gcv_facetize_sample_opts_equal(const struct gcv_facetize_sample_opts *a,
                               const struct gcv_facetize_sample_opts *b);

GCV_EXPORT extern int
gcv_facetize_nmg_opts_apply(struct gcv_facetize_nmg_opts *opts,
                            const struct gcv_facetize_kv *options,
                            size_t option_count);

GCV_EXPORT extern int
gcv_facetize_cm_opts_apply(struct gcv_facetize_cm_opts *opts,
                           const struct gcv_facetize_kv *options,
                           size_t option_count);

GCV_EXPORT extern int
gcv_facetize_spsr_opts_apply(struct gcv_facetize_spsr_opts *opts,
                             const struct gcv_facetize_kv *options,
                             size_t option_count);

GCV_EXPORT extern void
gcv_facetize_process_opts_default(struct gcv_facetize_process_opts *opts);

GCV_EXPORT extern int
gcv_facetize_process_opts_apply_method_options(struct gcv_facetize_process_opts *opts,
                                               const char *method,
                                               const struct gcv_facetize_kv *options,
                                               size_t option_count);

GCV_EXPORT extern int
gcv_facetize_process_opts_select_sample_method(struct gcv_facetize_process_opts *opts,
                                               const char * const *methods,
                                               size_t method_count);

GCV_EXPORT extern size_t
gcv_facetize_methods(const struct gcv_facetize_method_info **methods);

GCV_EXPORT extern const struct gcv_facetize_method_info *
gcv_facetize_method(const char *name);

GCV_EXPORT extern int
gcv_facetize_method_has_capability(const char *method, unsigned capability);

GCV_EXPORT extern size_t
gcv_facetize_method_options(const char *method, const struct gcv_facetize_option_desc **options);

GCV_EXPORT extern int
gcv_facetize_method_option_known(const char *method, const char *option);

GCV_EXPORT extern int
gcv_facetize_method_option_default_int(const char *method,
                                       const char *option,
                                       int *value_out);

GCV_EXPORT extern int
gcv_facetize_method_max_time_default(const char *method,
                                     int *value_out);

GCV_EXPORT extern int
gcv_facetize_plate_max_time_default(int *value_out);

GCV_EXPORT extern size_t
gcv_facetize_resolve_methods(const struct gcv_facetize_opts *opts, const struct gcv_facetize_method_info **methods, size_t max_methods);

GCV_EXPORT extern int
gcv_facetize_resolved_method_names(const struct gcv_facetize_opts *opts,
                                   struct bu_ptbl *method_names);

GCV_EXPORT extern int
gcv_facetize_default_method_names(struct bu_ptbl *method_names);

GCV_EXPORT extern void
gcv_facetize_free_string_ptbl(struct bu_ptbl *strings);

GCV_EXPORT extern void
gcv_facetize_free_method_names(struct bu_ptbl *method_names);

GCV_EXPORT extern size_t
gcv_facetize_unknown_methods(const struct gcv_facetize_opts *opts, const char **unknown, size_t max_unknown);

GCV_EXPORT extern int
gcv_facetize_parse_methods_csv(struct bu_vls *msg,
                               const char *csv,
                               struct bu_ptbl *methods_out);

GCV_EXPORT extern int
gcv_facetize_parse_method_options(struct bu_vls *msg,
                                  const char *arg,
                                  char **method,
                                  struct gcv_facetize_kv **options,
                                  size_t *option_count);

GCV_EXPORT extern void
gcv_facetize_free_method_options(struct gcv_facetize_kv *options,
                                 size_t option_count);

GCV_EXPORT extern int
gcv_facetize_kv_int(const struct gcv_facetize_kv *options,
                    size_t option_count,
                    const char *key,
                    int *value_out);

GCV_EXPORT extern char *
gcv_facetize_process_exec(void);

GCV_EXPORT extern size_t
gcv_facetize_process_base_argv(const char **argv,
                               size_t max_argv,
                               const char *exec_path,
                               const char *db_path,
                               int overwrite,
                               const char *method,
                               const char *method_opts,
                               const char *cache_dir);

GCV_EXPORT extern int
gcv_facetize_process_method_slots(size_t *method_idx,
                                  size_t *method_opts_idx,
                                  int overwrite);

GCV_EXPORT extern int
gcv_facetize_process_set_method_argv(const char **argv,
                                     size_t method_idx,
                                     size_t method_opts_idx,
                                     const char *method,
                                     const char *method_opts);

GCV_EXPORT extern int
gcv_facetize_process_run(const char **argv,
                         size_t argc,
                         const char *working_file,
                         fastf_t max_time,
                         int object_count,
                         gcv_facetize_log_cb log_cb,
                         void *log_ctx);

GCV_EXPORT extern int
gcv_facetize_process_run_objects(const char **base_argv,
                                 size_t base_argc,
                                 const char *working_file,
                                 const char **object_names,
                                 size_t object_count,
                                 fastf_t max_time,
                                 int bisect_failures,
                                 struct bu_ptbl *bad_object_names,
                                 gcv_facetize_log_cb log_cb,
                                 void *log_ctx);

GCV_EXPORT extern int
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
                                        void *log_ctx);

GCV_EXPORT extern size_t
gcv_facetize_process_method_argv_len(size_t *argc_out,
                                     const char *working_file,
                                     int overwrite,
                                     const char *method,
                                     const struct gcv_facetize_method_opts_state *method_opts,
                                     struct db_i *db,
                                     long nmg_debug_flag,
                                     const char *cache_dir);

GCV_EXPORT extern int
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
                                 void *log_ctx);

GCV_EXPORT extern char *
gcv_facetize_backup_path(const char *path);

GCV_EXPORT extern int
gcv_facetize_file_copy(const char *src, const char *dst);

GCV_EXPORT extern char *
gcv_facetize_method_optstr(const char *method,
                           const struct gcv_facetize_kv *options,
                           size_t option_count,
                           struct db_i *db,
                           long nmg_debug_flag);

GCV_EXPORT extern void
gcv_facetize_db_opts_default(struct gcv_facetize_db_opts *opts);

GCV_EXPORT extern int
gcv_facetize_to_db(struct db_i *db,
                   int argc,
                   const char **argv,
                   const struct gcv_facetize_db_opts *opts,
                   const struct gcv_facetize_db_callbacks *callbacks);

GCV_EXPORT extern int
gcv_facetize_nmg_eval_to_db(struct db_i *db,
                            int argc,
                            const char **object_names,
                            const char *output_name,
                            int make_nmg,
                            int verbosity,
                            gcv_facetize_log_cb log_cb,
                            void *log_ctx);

GCV_EXPORT extern int
gcv_facetize_objects_to_db(struct db_i *db,
                           int argc,
                           const char **argv,
                           int in_place,
                           int make_nmg,
                           int nmg_booleval,
                           const struct gcv_facetize_object_callbacks *callbacks,
                           void *callback_data);

GCV_EXPORT extern int
gcv_facetize_manifold_eval_to_db(struct db_i *eval_db,
                                 struct db_i *source_db,
                                 struct db_i *output_db,
                                 struct rt_wdb *wdbp,
                                 int argc,
                                 const char **object_names,
                                 const char *output_name,
                                 struct bu_list *vlfree,
                                 int no_empty,
                                 int disable_fixup,
                                 int verbosity,
                                 gcv_facetize_log_cb log_cb,
                                 void *log_ctx,
                                 gcv_facetize_variant_name_cb variant_cb,
                                 void *variant_ctx,
                                 gcv_facetize_bot_fixup_cb fixup_cb,
                                 void *fixup_ctx);

GCV_EXPORT extern int
gcv_facetize_regions_to_db(struct db_i *target_db,
                           int argc,
                           const char **argv,
                           const char *working_dir,
                           const char *base_name,
                           const char *prefix,
                           const char *suffix,
                           int in_place,
                           int make_nmg,
                           int nmg_booleval,
                           int no_perturb,
                           int verbosity,
                           const struct gcv_facetize_region_callbacks *callbacks,
                           void *callback_data);

GCV_EXPORT extern int
gcv_facetize_import_working_regions(struct db_i *target_db,
                                    const char *working_file,
                                    int root_count,
                                    const char **root_names,
                                    int overwrite,
                                    int use_prefix,
                                    const char *affix,
                                    struct gcv_facetize_import_result *result);

GCV_EXPORT extern int
gcv_facetize_region_result_name(struct db_i *working_db,
                                const char *root_name,
                                int make_nmg,
                                struct bu_vls *result_name);

GCV_EXPORT extern int
gcv_facetize_region_replace_root(struct db_i *working_db,
                                 const char *root_name,
                                 const char *result_name);

GCV_EXPORT extern void
gcv_facetize_import_result_free(struct gcv_facetize_import_result *result);

GCV_EXPORT extern int
gcv_facetize_manifold_objects_to_db(struct db_i *source_db,
                                    int argc,
                                    struct directory **dpa,
                                    const char *output_name,
                                    const char *working_file,
                                    const char *working_dir,
                                    int output_to_working,
                                    int cleanup,
                                    int make_nmg,
                                    int nmg_booleval,
                                    int no_perturb,
                                    int no_empty,
                                    int disable_fixup,
                                    int verbosity,
                                    struct bu_list *vlfree,
                                    gcv_facetize_log_cb log_cb,
                                    void *log_ctx,
                                    const struct gcv_facetize_manifold_object_callbacks *callbacks,
                                    void *callback_data,
                                    gcv_facetize_variant_name_cb variant_cb,
                                    void *variant_ctx,
                                    gcv_facetize_bot_fixup_cb fixup_cb,
                                    void *fixup_ctx);

GCV_EXPORT extern char *
gcv_facetize_quote_arg(const char *arg);

GCV_EXPORT extern size_t
gcv_facetize_boolean_evaluators(const struct gcv_facetize_step_info **evaluators);

GCV_EXPORT extern size_t
gcv_facetize_postprocess_steps(const struct gcv_facetize_step_info **steps);

GCV_EXPORT extern void
gcv_facetize_describe_options(struct bu_vls *description);

GCV_EXPORT extern struct rt_bot_internal *
gcv_facetize_to_bot(struct db_i *db,
                    const struct db_full_path *path,
                    const struct gcv_facetize_opts *opts,
                    const struct bn_tol *tol,
                    const struct bg_tess_tol *tess_tol,
                    struct bu_list *vlfree);

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
