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

#include <geogram/basic/common.h>
#include <geogram/basic/process.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/progress.h>
#include <geogram/basic/geogram_options.h>
#include <geogram/basic/stopwatch.h>
#include <geogram/numerics/multi_precision.h>
#include <geogram/numerics/predicates.h>
#include <geogram/delaunay/delaunay.h>

#include <sstream>
#include <iomanip>
#include <optional>

namespace GEOBRL {

    namespace {

        /**
         * \brief A global object that manages initialization and
         *   termination of the Geogram library
         */
        struct GeogramLibSingleton {

            static std::optional<GeogramLibSingleton>& instance(
                int flags, const GeoOptions& opts
            ) {
                static std::optional<GeogramLibSingleton> instance(
                    std::in_place, flags, opts
                );
                return instance;
            }

            GeogramLibSingleton(int flags, const GeoOptions& opts)
                : opts_(opts)
            {

                // When locale is set to non-us countries,
                // this may cause some problems when reading
                // floating-point numbers (some locale expect
                // a decimal ',' instead of a '.').
                // This restores the default behavior for
                // reading floating-point numbers.
#ifdef GEOBRL_OS_UNIX
                if (flags & GEOBRLCAD_INSTALL_LOCALE) {
                    setenv("LC_NUMERIC","POSIX",1);
                }
#endif

                Logger::initialize();
                Process::initialize(flags);
                Progress::initialize();
                Stopwatch::initialize(opts_);
                PCK::initialize();
                Delaunay::initialize();

                // Clear lastest system error
                if (flags & GEOBRLCAD_INSTALL_ERRNO) {
                    errno = 0;
                }
            }

            ~GeogramLibSingleton() {

                if(opts_.sys_stats) {
                    Logger::div("System Statistics");
                    PCK::show_stats();
                    Process::show_stats();
                }

                PCK::terminate();

                Progress::terminate();
                Process::terminate();
                CmdLine::terminate();
                Logger::terminate();

            }

            GeoOptions opts_;
        };

    }

    void initialize(int flags, const GeoOptions& opts) {
        GeogramLibSingleton::instance(flags, opts);
    }

    void terminate() {
        GeogramLibSingleton::instance(
            GEOBRLCAD_INSTALL_NONE, GeoOptions()
        ).reset();
    }
}
