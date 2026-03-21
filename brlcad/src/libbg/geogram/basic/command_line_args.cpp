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
    void import_arg_group_pre() {
    }

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
    void import_arg_group_post() {
    }

    /**
     * \brief Imports the optimizer option group
     */
    void import_arg_group_opt() {
    }

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
    void import_arg_group_co3ne() {
    }

    /**
     * \brief Import the statistics option group
     */
    void import_arg_group_stat() {
    }

    /**
     * \brief Imports the polyhedral meshing option group
     */
    void import_arg_group_poly() {
    }

    /**
     * \brief Imports the hex-dominant meshing option group
     */
    void import_arg_group_hex() {
    }

    /**
     * \brief Imports the quad-dominant meshing option group
     */
    void import_arg_group_quad() {
    }

    /**
     * \brief Imports the tetrahedral meshing option group
     */
    void import_arg_group_tet() {
    }

    /**
     * \brief Imports the graphics option group
     */
    void import_arg_group_gfx() {
    }

    /**
     * \brief Imports the biblio option group
     */
    void import_arg_group_biblio() {
    }

    /**
     * \brief Imports the gui option group.
     */
    void import_arg_group_gui() {
    }

    /************************************************************************/

    /**
     * \brief Sets the CAD profile
     */
    void set_profile_cad() {
        set_arg("pre:repair", true);
        set_arg_percent("pre:margin", 0.05);
        set_arg("post:repair", true);
        set_arg("remesh:sharp_edges", true);
        set_arg("remesh:RVC_centroids", false);
    }

    /**
     * \brief Sets the scanner profile
     */
    void set_profile_scan() {
        set_arg("pre:Nsmooth_iter", 3);
        set_arg("pre:repair", true);
        set_arg_percent("pre:max_hole_area", 10);
        set_arg("remesh:anisotropy", 1.0);
        set_arg_percent("pre:min_comp_area", 3);
        set_arg_percent("post:min_comp_area", 3);
    }

    /**
     * \brief Sets the conversion profile
     */
    void set_profile_convert() {
        set_arg("pre", false);
        set_arg("post", false);
        set_arg("remesh", false);
    }

    /**
     * \brief Sets the repair profile
     */
    void set_profile_repair() {
        set_arg("pre", true);
        set_arg("pre:repair", true);
        set_arg("pre:intersect", true);
        set_arg("pre:intersect_remove_internal_shells",true);
        set_arg("post", false);
        set_arg("remesh", false);
    }

    /**
     * \brief Sets the heal profile
     */
    void set_profile_heal() {
        set_arg("remesh", true);
        set_arg("remesh:multi_nerve", false);
        set_arg("post", true);
        set_arg_percent("post:max_hole_area", 10);
        set_arg_percent("post:min_comp_area", 3);
    }

    /**
     * \brief Sets the reconstruction profile
     */
    void set_profile_reconstruct() {
        set_arg("pre", false);
        set_arg("post", false);
        set_arg("remesh", false);
        set_arg("co3ne", true);
    }

    /**
     * \brief Sets the hex-dominant meshing profile
     */
    void set_profile_hex() {
        set_arg("hex", true);
    }

    /**
     * \brief Sets the quad-dominant meshing profile
     */
    void set_profile_quad() {
        set_arg("quad", true);
    }

    /**
     * \brief Sets the tetrahedral meshing profile
     */
    void set_profile_tet() {
        set_arg("tet", true);
    }

    /**
     * \brief Sets the polyhedral meshing profile
     */
    void set_profile_poly() {
        set_arg("poly", true);
    }
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
            } else if(name == "pre") {
                import_arg_group_pre();
            } else if(name == "remesh") {
                import_arg_group_remesh();
            } else if(name == "algo") {
                import_arg_group_algo();
            } else if(name == "post") {
                import_arg_group_post();
            } else if(name == "opt") {
                import_arg_group_opt();
            } else if(name == "co3ne") {
                import_arg_group_co3ne();
            } else if(name == "stat") {
                import_arg_group_stat();
            } else if(name == "quad") {
                import_arg_group_quad();
            } else if(name == "hex") {
                import_arg_group_hex();
            } else if(name == "tet") {
                import_arg_group_tet();
            } else if(name == "poly") {
                import_arg_group_poly();
            } else if(name == "gfx") {
                import_arg_group_gfx();
            } else if(name == "gui") {
                import_arg_group_gui();
            } else {
                Logger::instance()->set_quiet(false);
                Logger::err("CmdLine")
                    << "No such option group: " << name
                    << std::endl;
                return false;
            }
            return true;
        }

        bool set_profile(
            const std::string& name
        ) {
            if(name == "cad") {
                set_profile_cad();
            } else if(name == "scan") {
                set_profile_scan();
            } else if(name == "convert") {
                set_profile_convert();
            } else if(name == "repair") {
                set_profile_repair();
            } else if(name == "heal") {
                set_profile_heal();
            } else if(name == "reconstruct") {
                set_profile_reconstruct();
            } else if(name == "tet") {
                set_profile_tet();
            } else if(name == "quad") {
                set_profile_quad();
            } else if(name == "hex") {
                set_profile_hex();
            } else if(name == "poly") {
                set_profile_poly();
            } else {
                Logger::instance()->set_quiet(false);
                Logger::err("CmdLine")
                    << "No such profile: " << name
                    << std::endl;
                return false;
            }
            return true;
        }
    }
}
