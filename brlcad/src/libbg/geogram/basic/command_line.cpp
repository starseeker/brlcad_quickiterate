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

#include <geogram/basic/command_line.h>
#include <geogram/basic/logger.h>
#include <iostream>
#include <iomanip>

#if defined(GEO_OS_LINUX) || defined(GEO_OS_APPLE)
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#endif

#ifdef GEO_OS_WINDOWS
#include <io.h> // for _isatty()
#endif

// =================== Console logging utilies =====================

namespace {

    using namespace GEO;
    using namespace CmdLine;

    /** Maximum length of a feature name used in ui_feature() formatting */
    const unsigned int feature_max_length = 12;

    // True if displaying help in a way that will be easily processed by help2man
    bool man_mode = false;

    /** Keeps length of a feature name */
    bool ui_separator_opened = false;

    /** Width of the current terminal */
    index_t ui_term_width = 79;

    /** Terminal left margin */
    index_t ui_left_margin = 0;

    /** Terminal right margin */
    index_t ui_right_margin = 0;

    /** Characters of the progress wheel */
    const char working[] = {'|', '/', '-', '\\'};

    /** Current index in the progress wheel */
    index_t working_index = 0;

    /** Characters of the progress wave */
    const char waves[] = {',', '.', 'o', 'O', '\'', 'O', 'o', '.', ','};

    /**
     * \brief Gets the console output stream
     * \return a reference to a std::ostream
     */
    inline std::ostream& ui_out() {
        return std::cout;
    }

    /**
     * \brief Prints a sequence of identical chars
     * \param[in] c the character to print
     * \param[in] nb the number of characters to print
     */
    inline void ui_pad(char c, size_t nb) {
        for(index_t i = 0; i < nb; i++) {
            std::cout << c;
        }
    }

    /**
     * \brief Checks if the standard output is redirected
     * \details It is used by the various console formatting functions to
     * determine if messages are printed in pretty mode or not.
     * \retval true if the console is redirected to a file
     * \retval false otherwise
     */
    bool is_redirected() {
        static bool initialized = false;
        static bool result;
        if(!initialized) {
#ifdef GEO_OS_WINDOWS
            result = !_isatty(1);
#else
            result = !isatty(1);
#endif
            initialized = true;
        }
        return result || !Logger::instance()->is_pretty();
    }

    /**
     * \brief Recomputes the width of the terminal
     */
    void update_ui_term_width() {
#ifndef GEO_OS_WINDOWS
        if(is_redirected()) {
            return;
        }
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        ui_term_width = w.ws_col;
        if(ui_term_width < 20) {
            ui_term_width = 79;
        }
        if(ui_term_width <= 82) {
            ui_left_margin = 0;
            ui_right_margin = 0;
        } else if(ui_term_width < 90) {
            ui_left_margin = 2;
            ui_right_margin = 2;
        } else {
            ui_left_margin = 4;
            ui_right_margin = 4;
        }
#endif
    }

    /**
     * \brief Safe unsigned subtraction
     * \return a - b if a > b, 0 otherwise
     */
    inline size_t sub(size_t a, size_t b) {
        return a > b ? a - b : 0;
    }
}

namespace GEO {

    namespace CmdLine {

        void initialize() {
        }

        void terminate() {
            ui_close_separator();
        }

        index_t ui_terminal_width() {
            index_t ui_term_width_bkp = ui_term_width;
            update_ui_term_width();
            ui_term_width = std::min(ui_term_width, ui_term_width_bkp);
            return ui_term_width;
        }

        void ui_separator() {
            if(Logger::instance()->is_quiet() || is_redirected()) {
                return;
            }

            update_ui_term_width();
            ui_separator_opened = true;

            ui_out() << " ";
            ui_pad(' ', ui_left_margin);
            ui_pad(
                '_',
                sub(ui_terminal_width(), 2 + ui_left_margin + ui_right_margin)
            );
            ui_out() << " " << std::endl;

            // Force a blank line under the separator
            ui_message("\n");
        }

        void ui_separator(
            const std::string& title,
            const std::string& short_title
        ) {
            if(Logger::instance()->is_quiet()) {
                return;
            }

            if(man_mode) {
                if(title == "") {
                    return;
                }
                ui_out() << std::endl;
                std::string shortt = short_title;
                if(shortt.length() > 0 && shortt[0] == '*') {
                    shortt = shortt.substr(1, shortt.length()-1);
                    ui_out() << title << " (\"" << shortt << ":*\" options, advanced)"
                             << std::endl;
                } else {
                    ui_out() << title << " (\"" << shortt << ":*\" options)"
                             << std::endl;
                }
                ui_out() << std::endl << std::endl;
                return;
            }

            if(is_redirected()) {
                ui_out() << std::endl;
                if(short_title != "" && title != "") {
                    ui_out() << "=[" << short_title << "]=["
                             << title << "]=" << std::endl;
                } else {
                    std::string s = title + short_title;
                    ui_out() << "=[" << s << "]=" << std::endl;
                }
                return;
            }

            update_ui_term_width();
            ui_separator_opened = true;

            size_t L = title.length() + short_title.length();

            ui_out() << "   ";
            ui_pad(' ', ui_left_margin);
            ui_pad('_', L + 14);
            ui_out() << std::endl;

            ui_pad(' ', ui_left_margin);
            if(short_title != "" && title != "") {
                ui_out() << " _/ ==[" << short_title << "]====["
                         << title << "]== \\";
            } else {
                std::string s = title + short_title;
                ui_out() << " _/ =====[" << s << "]===== \\";
            }

            ui_pad(
                '_',
                sub(
                    ui_terminal_width(),
                    19 + L + ui_left_margin + ui_right_margin
                )
            );
            ui_out() << std::endl;

            // Force a blank line under the separator
            ui_message("\n");
        }

        void ui_message(const std::string& message) {
            // By default, wrap to the column that is right after the feature
            // name and its decorations.
            ui_message(message, feature_max_length + 5);
        }

        void ui_message(
            const std::string& message,
            index_t wrap_margin
        ) {
            if(Logger::instance()->is_quiet()) {
                return;
            }

            if(!ui_separator_opened) {
                ui_separator();
            }

            if(is_redirected()) {
                ui_out() << message;
                return;
            }

            std::string cur = message;
            size_t maxL =
                sub(ui_terminal_width(), 4 + ui_left_margin + ui_right_margin);
            index_t wrap = 0;

            for(;;) {
                std::size_t newline = cur.find('\n');
                if(newline != std::string::npos && newline < maxL) {
                    // Got a new line that occurs before the right border
                    // We cut before the newline and pad with spaces
                    ui_pad(' ', ui_left_margin);
                    ui_out() << "| ";
                    ui_pad(' ', wrap);
                    ui_out() << cur.substr(0, newline);
                    ui_pad(' ', sub(maxL,newline));
                    ui_out() << " |" << std::endl;
                    cur = cur.substr(newline + 1);
                } else if(cur.length() > maxL) {
                    // The line length runs past the right border
                    // We cut the string just before the border
                    ui_pad(' ', ui_left_margin);
                    ui_out() << "| ";
                    ui_pad(' ', wrap);
                    ui_out() << cur.substr(0, maxL);
                    ui_out() << " |" << std::endl;
                    cur = cur.substr(maxL);
                } else if(cur.length() != 0) {
                    // Print the remaining portion of the string
                    // and pad with spaces
                    ui_pad(' ', ui_left_margin);
                    ui_out() << "| ";
                    ui_pad(' ', wrap);
                    ui_out() << cur;
                    ui_pad(' ', sub(maxL,cur.length()));
                    ui_out() << " |";
                    break;
                } else {
                    // No more chars to print
                    break;
                }

                if(wrap == 0) {
                    wrap = wrap_margin;
                    maxL = sub(maxL,wrap_margin);
                }
            }
        }

        void ui_clear_line() {
            if(Logger::instance()->is_quiet() || is_redirected()) {
                return;
            }

            ui_pad('\b', ui_terminal_width());
            ui_out() << std::flush;
        }

        void ui_close_separator() {
            if(!ui_separator_opened) {
                return;
            }

            if(Logger::instance()->is_quiet() || is_redirected()) {
                return;
            }

            ui_pad(' ', ui_left_margin);
            ui_out() << '\\';
            ui_pad(
                '_',
                sub(ui_terminal_width(), 2 + ui_left_margin + ui_right_margin)
            );
            ui_out() << '/';
            ui_out() << std::endl;

            ui_separator_opened = false;
        }

        void ui_progress(
            const std::string& task_name, index_t val, index_t percent,
            bool clear
        ) {
            if(Logger::instance()->is_quiet() || is_redirected()) {
                return;
            }

            working_index++;

            std::ostringstream os;

            if(percent != val) {
                os << ui_feature(task_name)
                   << "("
                   << working[(working_index % sizeof(working))]
                   << ")-["
                   << std::setw(3) << percent
                   << "%]-["
                   << std::setw(3) << val
                   << "]--[";
            } else {
                os << ui_feature(task_name)
                   << "("
                   << working[(working_index % sizeof(working))]
                   << ")-["
                   << std::setw(3) << percent
                   << "%]--------[";
            }

            size_t max_L =
                sub(ui_terminal_width(), 43 + ui_left_margin + ui_right_margin);

            max_L -= size_t(std::log10(std::max(double(val),1.0)));
            max_L += 2;

            if(val > max_L) {
                // No space enough to expand the progress bar
                // Do some animation...
                for(index_t i = 0; i < max_L; i++) {
                    os << waves[((val - i + working_index) % sizeof(waves))];
                }
            } else {
                for(index_t i = 0; i < val; i++) {
                    os << "o";
                }
            }
            os << " ]";

            if(clear) {
                ui_clear_line();
            }
            ui_message(os.str());
        }

        void ui_progress_time(
            const std::string& task_name, double elapsed, bool clear
        ) {
            if(Logger::instance()->is_quiet()) {
                return;
            }

            std::ostringstream os;
            os << ui_feature(task_name)
               << "Elapsed time: " << elapsed
               << "s\n";

            if(clear) {
                ui_clear_line();
            }
            ui_message(os.str());
        }

        void ui_progress_canceled(
            const std::string& task_name,
            double elapsed, index_t percent, bool clear
        ) {
            if(Logger::instance()->is_quiet()) {
                return;
            }

            std::ostringstream os;
            os << ui_feature(task_name)
               << "Task canceled after " << elapsed
               << "s (" << percent << "%)\n";

            if(clear) {
                ui_clear_line();
            }
            ui_message(os.str());
        }

        std::string ui_feature(
            const std::string& feat_in, bool show
        ) {
            if(feat_in.empty()) {
                return feat_in;
            }

            if(!show) {
                return std::string(feature_max_length + 5, ' ');
            }

            std::string result = feat_in;
            if(!is_redirected()) {
                result = result.substr(0, feature_max_length);
            }
            if(result.length() < feature_max_length) {
                result.append(feature_max_length - result.length(), ' ');
            }
            return "o-[" + result + "] ";
        }
    }
}

