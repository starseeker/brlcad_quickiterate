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

#include <geogram/basic/command_line_args.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/process.h>
#include <geogram/basic/logger.h>

#include <set>

namespace {

    using namespace GEO;
    using namespace CmdLine;

    /**
     * \brief Imports the global option group
     */
    void import_arg_group_global() {
        declare_arg(
            "profile", "scan",
            "Vorpaline mode "
            "(scan, convert, repair, heal, cad, tet, poly, hex, quad)"
        );
    }

    /**
     * \brief Imports the pre-processing option group
     */

    /**
     * \brief Imports the remeshing option group
     */
    void import_arg_group_remesh() {
        declare_arg_group("remesh", "Remeshing phase");
        declare_arg(
            "remesh:multi_nerve", true,
            "Insert new vertices to preserve topology",
            ARG_ADVANCED
        );
    }

    /**
     * \brief Declares the algorithm option group
     */
    void import_arg_group_algo() {
        declare_arg_group("algo", "Algorithms", ARG_ADVANCED);
        declare_arg(
            "algo:nn_search", "BNN",
            "Nearest neighbors search (BNN, ...)"
        );
        declare_arg(
            "algo:delaunay", "NN",
            "Delaunay algorithm"
        );
        declare_arg(
            "algo:hole_filling", "loop_split",
            "Hole filling mode (loop_split, Nloop_split, ear_cut)"
        );
        declare_arg(
            "algo:predicates", "fast",
            "Geometric predicates (fast, exact)"
        );
#ifdef GEO_OS_ANDROID
        // NDK's default multithreading seems to be not SMP-compliant
        // (missing memory barriers in synchronization primitives)
        declare_arg(
            "algo:parallel", false,
            "Use parallel standard algorithms"
        );
#else
        declare_arg(
            "algo:parallel", true,
            "Use parallel standard algorithms"
        );
#endif
    }

    /**
     * \brief Imports the post-processing option group
     */

    /**
     * \brief Imports the optimizer option group
     */

    /**
     * \brief Imports the system option group
     */
    void import_arg_group_sys() {
        declare_arg_group("sys", "System fine tuning", ARG_ADVANCED);
        declare_arg(
            "sys:multithread", Process::multithreading_enabled(),
            "Enables multi-threaded computations"
        );
        declare_arg(
            "sys:stats", false,
            "Display statistics on exit"
        );
    }

    /**
     * \brief Imports the NL (Numerical Library) option group
     */
    void import_arg_group_nl() {
    }

    /**
     * \brief Imports the Logger option group
     */
    void import_arg_group_log() {
    }

    /**
     * \brief Imports the reconstruction option group
     */

    /**
     * \brief Import the statistics option group
     */

    /**
     * \brief Imports the polyhedral meshing option group
     */

    /**
     * \brief Imports the hex-dominant meshing option group
     */

    /**
     * \brief Imports the quad-dominant meshing option group
     */

    /**
     * \brief Imports the tetrahedral meshing option group
     */

    /**
     * \brief Imports the graphics option group
     */

    /**
     * \brief Imports the biblio option group
     */
    void import_arg_group_biblio() {
    }

    /**
     * \brief Imports the gui option group.
     */

    /************************************************************************/

    /**
     * \brief Sets the CAD profile
     */

    /**
     * \brief Sets the scanner profile
     */

    /**
     * \brief Sets the conversion profile
     */

    /**
     * \brief Sets the repair profile
     */

    /**
     * \brief Sets the heal profile
     */

    /**
     * \brief Sets the reconstruction profile
     */

    /**
     * \brief Sets the hex-dominant meshing profile
     */

    /**
     * \brief Sets the quad-dominant meshing profile
     */

    /**
     * \brief Sets the tetrahedral meshing profile
     */

    /**
     * \brief Sets the polyhedral meshing profile
     */
}

namespace GEO {

    namespace CmdLine {

        bool import_arg_group(
            const std::string& name
        ) {
            static std::set<std::string> imported;
            if(imported.find(name) != imported.end()) {
                return true;
            }
            imported.insert(name);

            if(name == "standard") {
                import_arg_group_global();
                import_arg_group_sys();
                import_arg_group_nl();
                import_arg_group_log();
                import_arg_group_biblio();
            } else if(name == "global") {
                import_arg_group_global();
            } else if(name == "nl") {
                import_arg_group_nl();
            } else if(name == "sys") {
                import_arg_group_sys();
            } else if(name == "log") {
                import_arg_group_log();
            } else if(name == "remesh") {
                import_arg_group_remesh();
            } else if(name == "algo") {
                import_arg_group_algo();
            } else {
                Logger::instance()->set_quiet(false);
                Logger::err("CmdLine")
                    << "No such option group: " << name
                    << std::endl;
                return false;
            }
            return true;
        }
    }
}
