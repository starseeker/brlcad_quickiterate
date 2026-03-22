/*
 *  Copyright (c) 2000-2022 Inria
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *  * Neither the name of the ALICE Project-Team nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  Contact: Bruno Levy
 *
 *     https://www.inria.fr/fr/bruno-levy
 *
 *     Inria,
 *     Domaine de Voluceau,
 *     78150 Le Chesnay - Rocquencourt
 *     FRANCE
 *
 */

#ifndef GEOGRAM_BASIC_GEOGRAM_OPTIONS
#define GEOGRAM_BASIC_GEOGRAM_OPTIONS

#include <geogram/basic/common.h>
#include <string>

/**
 * \file geogram/basic/geogram_options.h
 * \brief Typed options struct for Geogram algorithm configuration.
 *
 * Replaces the string-keyed CmdLine::get_arg_*("group:key") query pattern
 * with direct typed field access. All options that geogram internally queries
 * at algorithm call sites are collected here with explicit C++ types.
 *
 * The singleton is kept in sync with the CmdLine/Environment system so that
 * existing code paths using CmdLine::set_arg() continue to work correctly.
 */

namespace GEO {

    /**
     * \brief Typed runtime options for Geogram algorithms.
     * \details All options previously accessed via CmdLine::get_arg_*("key")
     * inside the Geogram library are represented here as plain typed fields.
     * Use geo_options() to obtain a reference to the process-wide singleton.
     *
     * Defaults match those declared in command_line_args.cpp.  The singleton
     * is updated whenever CmdLine::declare_arg() or CmdLine::set_arg() is
     * called for a recognised key, so the two systems remain in sync.
     */
    struct GEOGRAM_API GeoOptions {

        // ----------------------------------------------------------------
        // algo group
        // ----------------------------------------------------------------

        /** algo:nn_search — nearest-neighbour search backend ("BNN", ...) */
        std::string algo_nn_search    {"BNN"};

        /** algo:delaunay — Delaunay algorithm name ("NN", "BPOW", ...) */
        std::string algo_delaunay     {"NN"};

        /** algo:hole_filling — hole filling algorithm ("loop_split", "ear_cut", ...) */
        std::string algo_hole_filling {"loop_split"};

        /** algo:predicates — geometric predicates mode ("fast", "exact") */
        std::string algo_predicates   {"fast"};

        /** algo:parallel — enable parallel standard algorithms */
        bool        algo_parallel     {true};

        // ----------------------------------------------------------------
        // sys group
        // ----------------------------------------------------------------

        /** sys:multithread — enable multi-threaded computations */
        bool        sys_multithread   {true};

        /** sys:stats — display statistics on exit */
        bool        sys_stats         {false};

        // ----------------------------------------------------------------
        // remesh group
        // ----------------------------------------------------------------

        /** remesh:multi_nerve — insert vertices to preserve topology */
        bool        remesh_multi_nerve    {true};

        /** remesh:RVC_centroids — use RVC centroids for remeshing */
        bool        remesh_RVC_centroids  {false};

        // ----------------------------------------------------------------
        // dbg group
        // ----------------------------------------------------------------

        /** dbg:save_ANN_histo — save ANN histogram to file */
        bool        dbg_save_ANN_histo    {false};

        // ----------------------------------------------------------------
        // co3ne group (MESH_REPAIR_RECONSTRUCT path)
        // ----------------------------------------------------------------

        /** co3ne:min_comp_area — minimum area of connected components
         *  as a fraction of total mesh area (0.0 = keep all) */
        double      co3ne_min_comp_area    {0.0};

        /** co3ne:min_comp_facets — minimum facet count of connected components
         *  (0 = keep all) */
        unsigned int co3ne_min_comp_facets {0};

        /** co3ne:max_hole_area — maximum area of holes to fill
         *  as a fraction of total mesh area (0.0 = fill none) */
        double      co3ne_max_hole_area    {0.0};

        /** co3ne:max_hole_edges — maximum edge count of holes to fill
         *  (0 = fill none) */
        unsigned int co3ne_max_hole_edges  {0};

        // ----------------------------------------------------------------
        // Singleton access
        // ----------------------------------------------------------------

        /**
         * \brief Returns the process-wide GeoOptions singleton.
         */
        static GeoOptions& instance();
    };

    /**
     * \brief Returns a reference to the thread-local override pointer.
     * \details Used internally by geo_options() and GeoOptionsScope.
     *  The returned reference points to the per-thread GeoOptions override,
     *  or nullptr when no override is active for the current thread.
     */
    GEOGRAM_API const GeoOptions*& geo_options_tl_ref();

    /**
     * \brief Returns the active GeoOptions for the current thread.
     * \details Returns the thread-local override if one has been installed
     *  with GeoOptionsScope, otherwise falls back to the process-wide
     *  singleton.
     */
    inline const GeoOptions& geo_options() {
        const GeoOptions* p = geo_options_tl_ref();
        return p ? *p : GeoOptions::instance();
    }

    /**
     * \brief RAII scope guard that installs a per-thread GeoOptions override.
     * \details Allows parallel callers to use independent configurations
     *  without mutating global state.  All calls to geo_options() inside the
     *  scope (on the same thread) return \p opts instead of the global
     *  singleton.  The previous override (if any) is restored on destruction.
     *
     * Typical usage:
     * \code
     *   GeoOptions myOpts;
     *   myOpts.remesh_multi_nerve = false;
     *   {
     *       GEO::GeoOptionsScope scope(myOpts);
     *       GEO::remesh_smooth(meshIn, meshOut, nbPts);
     *   } // original options restored here
     * \endcode
     */
    class GEOGRAM_API GeoOptionsScope {
    public:
        /**
         * \brief Installs \p opts as the thread-local override.
         * \param[in] opts options struct to use for this scope.
         *  The caller is responsible for keeping \p opts alive while the
         *  scope is active.
         */
        explicit GeoOptionsScope(const GeoOptions& opts)
            : prev_(geo_options_tl_ref())
        {
            geo_options_tl_ref() = &opts;
        }

        /** \brief Restores the previous thread-local override. */
        ~GeoOptionsScope() {
            geo_options_tl_ref() = prev_;
        }

        GeoOptionsScope(const GeoOptionsScope&)            = delete;
        GeoOptionsScope& operator=(const GeoOptionsScope&) = delete;

    private:
        const GeoOptions* prev_;
    };

} // namespace GEO

#endif
