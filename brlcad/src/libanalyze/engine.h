/*                       E N G I N E . H
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @file libanalyze/engine.h
 *
 * Internal C++17 engine layer for libanalyze (Phase A).
 *
 * This header is NOT part of the public libanalyze API.  It is included
 * only within libanalyze implementation files.
 *
 * The engine layer provides a clean seam between the public C API facade
 * (analyze_run / perform_raytracing / setter-getter functions) and the
 * actual execution core.  Placing all marshalling and result-harvesting
 * logic here means there is exactly one code path that translates a
 * configuration into a running analysis and collects the output.
 *
 * Phase A goal: structural isolation with zero behaviour change.
 *
 * Future phases:
 *  Phase B — sampler strategy abstraction (TripleGrid, Rotated, Crofton, …)
 *  Phase C — analyzer plugin abstraction (per-metric, per-issue-type)
 *  Phase D — RAII ownership at internal boundaries
 *  Phase E — remove dead code once parity is verified
 */

#ifndef LIBANALYZE_ENGINE_H
#define LIBANALYZE_ENGINE_H

#include "common.h"
#include "raytrace.h"
#include "analyze.h"

__BEGIN_DECLS

/**
 * analyze_engine_run - unified analysis execution entry point.
 *
 * Marshals @p cfg into an AnalyzeRequest, invokes perform_raytracing(),
 * and harvests results into a freshly-allocated struct analyze_results.
 *
 * Both analyze_run() and (in a later phase) the legacy
 * perform_raytracing() path will converge on this function, ensuring
 * that config-to-state mapping logic is never duplicated.
 *
 * @param cfg       Analysis configuration; NULL uses library defaults.
 * @param dbip      Open database instance.
 * @param names     Array of object names to analyse.
 * @param num_names Number of entries in @p names.
 * @param flags     Bitwise OR of ANALYZE_* flags.
 * @return          Heap-allocated results (free with analyze_results_free()),
 *                  or NULL on error.
 */
extern struct analyze_results *
analyze_engine_run(const struct analyze_config *cfg, struct db_i *dbip,
		   char *names[], int num_names, int flags);

__END_DECLS


/* ======================================================================
 * C++17 internal types.
 *
 * The definitions below are compiled only when this header is included
 * from a C++ translation unit.  Plain-C files (api.c, check_options.c, …)
 * see only the extern "C" declaration above.
 * ====================================================================== */
#ifdef __cplusplus

#include <cstddef>
#include <cstring>

/* Forward-declare the opaque C type; full definition in analyze_private.h. */
struct current_state;
struct bu_vls;

namespace analyze {

/**
 * AnalyzeRequest — immutable, validated snapshot of one analysis session.
 *
 * An AnalyzeRequest is built once — from an analyze_config (new API) or,
 * in the legacy-compatibility path, from a current_state — and then
 * passed by const-reference throughout the engine without modification.
 *
 * All string fields are borrowed pointers valid for the lifetime of the
 * originating config object; AnalyzeRequest does not own them.
 *
 * Default member-initializer values mirror those of
 * analyze_current_state_init() so that NULL-config callers get the same
 * results as before Phase A.
 */
struct AnalyzeRequest {
    /* ---- Sampling ---- */
    int    sampler           = ANALYZE_SAMPLER_TRIPLE_GRID;
    int    num_views         = 3;
    double azimuth_deg       = 35.0;
    double elevation_deg     = 25.0;
    double grid_spacing      = 50.0;
    double grid_spacing_min  = 0.5;
    double aspect            = 1.0;
    size_t n_crofton_rays    = 0;

    /* ---- Grid size override ---- */
    int    grid_width  = 0;
    int    grid_height = 0;

    /* ---- Sampling detail ---- */
    int    quiet_missed           = 0;
    double samples_per_model_axis = 2.0;

    /* ---- VIEW_PLANE parameters ---- */
    double  view_size    = 0.0;
    point_t view_eye     = {};
    quat_t  view_quat    = {};

    /* ---- Convergence tolerances ---- */
    double overlap_tol   = 0.0;
    double volume_tol    = -1.0;
    double mass_tol      = -1.0;
    double surf_area_tol = -1.0;

    /* ---- Material densities ---- */
    const char *density_file = nullptr;

    /* ---- Execution ---- */
    int    use_air        = 1;
    int    ncpu           = 0;    /* 0 → all available */
    size_t required_hits  = 1;

    /* ---- Output ---- */
    int         verbose = 0;
    struct bu_vls *log_str = nullptr;

    /* ---- Runtime limits ---- */
    long   timeout_ms      = 0;
    double required_digits = 0.0;

    /* ---- Per-pass volume plot file ---- */
    FILE *volume_plot_file = nullptr;

    /* ---- Presentation-layer render hooks ---- */
    analyze_overlap_render_fn    overlap_render      = nullptr;
    void                        *overlap_render_data = nullptr;
    analyze_gap_render_fn        gap_render          = nullptr;
    void                        *gap_render_data     = nullptr;
    analyze_adj_air_render_fn    adj_air_render      = nullptr;
    void                        *adj_air_render_data = nullptr;
    analyze_exp_air_render_fn    exp_air_render      = nullptr;
    void                        *exp_air_render_data = nullptr;
    analyze_unconf_air_render_fn unconf_air_render      = nullptr;
    void                        *unconf_air_render_data = nullptr;

    /* ---- Analysis flags (ANALYZE_* bitmask) ---- */
    int flags = 0;

    /**
     * Build an AnalyzeRequest from a public analyze_config.
     *
     * When @p cfg is NULL all fields remain at their default values.
     * @p analysis_flags is copied directly into req.flags.
     */
    static AnalyzeRequest from_config(const struct analyze_config *cfg,
				      int analysis_flags);
};

/**
 * Apply an AnalyzeRequest to an already-initialised current_state.
 *
 * Overwrites all current_state fields that the request specifies,
 * using the mapping logic that was previously inlined in analyze_run().
 * Centralising the mapping here means it is exercised identically by
 * both the new analyze_run() path and the eventual legacy-compat path.
 */
void apply_request_to_state(const AnalyzeRequest &req,
			    struct current_state *state);

/**
 * Run the full analysis pipeline from a typed AnalyzeRequest.
 *
 * Internally:
 *   1. Allocates struct analyze_results and initialises all bu_ptbl lists.
 *   2. Creates a current_state via analyze_current_state_init().
 *   3. Calls apply_request_to_state() to populate it.
 *   4. Registers capture callbacks for every requested issue type.
 *   5. Invokes perform_raytracing().
 *   6. Harvests scalar totals and per-object/per-region arrays.
 *   7. Frees the current_state and returns the result.
 *
 * Returns NULL on error.
 */
struct analyze_results *run(const AnalyzeRequest &req,
			    struct db_i *dbip,
			    char *names[], int num_names);

} /* namespace analyze */

#endif /* __cplusplus */

#endif /* LIBANALYZE_ENGINE_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
