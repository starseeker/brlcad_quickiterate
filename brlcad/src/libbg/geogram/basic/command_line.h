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

#ifndef GEOBRLCAD_BASIC_COMMAND_LINE
#define GEOBRLCAD_BASIC_COMMAND_LINE

#include <geogram/basic/common.h>
#include <geogram/basic/numeric.h>

/**
 * \file geogram/basic/command_line.h
 * \brief Console UI utilities (separators, progress bars, feature labels).
 *
 * The option-management API (declare_arg, set_arg, get_arg, import_arg_group,
 * etc.) has been removed.  Algorithm configuration is now done directly
 * through the GeoOptions struct; see geogram/basic/geogram_options.h.
 */

namespace GEOBRL {

    /**
     * \brief Console UI utilities used internally by the Logger and Progress
     *  subsystems.
     */
    namespace CmdLine {

        /**
         * \brief Initializes the command line framework.
         * \details Called by GEOBRL::initialize().
         */
        void GEOBRLCAD_API initialize();

        /**
         * \brief Cleans up the command line framework.
         * \details Called by GEOBRL::terminate().
         */
        void GEOBRLCAD_API terminate();

        /**
         * \brief Gets the width of the console.
         * \return the console width in number of characters.
         */
        index_t GEOBRLCAD_API ui_terminal_width();

        /**
         * \brief Outputs a separator with a title on the console.
         */
        void GEOBRLCAD_API ui_separator(
            const std::string& title,
            const std::string& short_title = ""
        );

        /**
         * \brief Outputs a separator without a title on the console.
         */
        void GEOBRLCAD_API ui_separator();

        /**
         * \brief Closes an opened separator.
         */
        void GEOBRLCAD_API ui_close_separator();

        /**
         * \brief Outputs a message on the console.
         */
        void GEOBRLCAD_API ui_message(
            const std::string& message,
            index_t wrap_margin
        );

        /**
         * \brief Outputs a message on the console.
         */
        void GEOBRLCAD_API ui_message(
            const std::string& message
        );

        /**
         * \brief Clears the last line.
         */
        void GEOBRLCAD_API ui_clear_line();

        /**
         * \brief Displays a progress bar.
         */
        void GEOBRLCAD_API ui_progress(
            const std::string& task_name, index_t val,
            index_t percent, bool clear = true
        );

        /**
         * \brief Displays the time elapsed for a completed task.
         */
        void GEOBRLCAD_API ui_progress_time(
            const std::string& task_name,
            double elapsed, bool clear = true
        );

        /**
         * \brief Displays the time elapsed for a canceled task.
         */
        void GEOBRLCAD_API ui_progress_canceled(
            const std::string& task_name,
            double elapsed, index_t percent, bool clear = true
        );

        /**
         * \brief Formats a Logger feature name.
         */
        std::string GEOBRLCAD_API ui_feature(
            const std::string& feature, bool show = true
        );
    }
}

#endif
