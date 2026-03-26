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

#include <geogram/basic/logger.h>
#include <geogram/basic/assert.h>
#include <geogram/basic/string.h>
#include <geogram/basic/argused.h>
#include <geogram/basic/process.h>
#include <bu/log.h>


#include <stdlib.h>
#include <stdarg.h>

/*
  Disables the warning caused by passing 'this' as an argument while
  construction is not finished (in LoggerStream ctor).
  As LoggerStreamBuf only stores the pointer for later use, so we can
  ignore the fact that 'this' is not completely formed yet.
*/
#ifdef GEOBRL_OS_WINDOWS
#pragma warning(disable:4355)
#endif


namespace {
    using namespace GEOBRL;

    /**
     * \brief The output stream returned by Logger::err_console()
     * \details It has a locking mechanism that avoids messages sent by different
     *  threads to be mixed.
     * \see Logger::err_console(), Logger::out(), Logger::err(), Logger::warn(),
     *  Logger::status()
     */
    class CERRStream : public std::ostream {
    public:
        CERRStream() :
            std::ostream(new CERRStreamBuff(this)),lock_(GEOBRLCAD_SPINLOCK_INIT) {
        }
        ~CERRStream() override{
        }
        void lock() {
            Process::acquire_spinlock(lock_);
        }
        void unlock() {
            Process::release_spinlock(lock_);
        }
    private:
        Process::spinlock lock_;
        class CERRStreamBuff : public std::stringbuf {
        public:
            CERRStreamBuff(CERRStream* stream) : stream_(stream) {
            }
            int sync() override {
                std::cerr << this->str();
                this->str("");
                stream_->unlock();
                return 0;
            }
        private:
            CERRStream* stream_;
        };
    };
}

namespace GEOBRL {

    /************************************************************************/

    int LoggerStreamBuf::sync() {
        std::string str(this->str());
        loggerStream_->notify(str);
        this->str("");
        return 0;
    }

    /************************************************************************/

    LoggerStream::LoggerStream(Logger* logger) :
        std::ostream(new LoggerStreamBuf(this)),
        logger_(logger) {
    }

    LoggerStream::~LoggerStream() {
        std::streambuf* buf = rdbuf();
        delete buf;
    }

    void LoggerStream::notify(const std::string& str) {
        logger_->notify(this, str);
    }

    /************************************************************************/

    LoggerClient::~LoggerClient() {
    }

    /************************************************************************/

    ConsoleLogger::ConsoleLogger() {
    }

    ConsoleLogger::~ConsoleLogger() {
    }

    void ConsoleLogger::div(const std::string& title) {
        bu_log("====== %s ======\n", title.c_str());
    }

    void ConsoleLogger::out(const std::string& str) {
        bu_log("%s", str.c_str());
    }

    void ConsoleLogger::warn(const std::string& str) {
        bu_log("%s", str.c_str());
    }

    void ConsoleLogger::err(const std::string& str) {
        bu_log("%s", str.c_str());
    }

    void ConsoleLogger::status(const std::string& str) {
        geo_argused(str);
    }

    /************************************************************************/

    SmartPointer<Logger> Logger::instance_;

    void Logger::initialize() {
        instance_ = new Logger();
    }

    void Logger::terminate() {
        instance_.reset();
    }

    bool Logger::is_initialized() {
        return (instance_ != nullptr);
    }

    void Logger::register_client(LoggerClient* c) {
        clients_.insert(c);
    }

    void Logger::unregister_client(LoggerClient* c) {
        geo_debug_assert(clients_.find(c) != clients_.end());
        clients_.erase(c);
    }

    void Logger::unregister_all_clients() {
        clients_.clear();
    }

    bool Logger::is_client(LoggerClient* c) const {
        return clients_.find(c) != clients_.end();
    }

    void Logger::set_quiet(bool flag) {
        quiet_ = flag;
    }

    void Logger::set_minimal(bool flag) {
        minimal_ = flag;
    }

    void Logger::set_pretty(bool flag) {
        pretty_ = flag;
    }


    Logger::Logger() :
        out_(this),
        warn_(this),
        err_(this),
        status_(this),
        log_everything_(true),
        current_feature_changed_(false),
        quiet_(true),
        pretty_(true),
        minimal_(false),
        notifying_error_(false)
    {
        // Add a default client printing stuff to std::cout
        register_client(new ConsoleLogger());
#ifdef GEOBRL_DEBUG
        quiet_ = false;
#endif
        err_console_ = new CERRStream;
    }

    Logger::~Logger() {
        delete err_console_;
        err_console_ = nullptr;
    }

    Logger* Logger::instance() {
        // Do not use geo_assert here:
        //  if the instance is nullptr, geo_assert will
        // call the Logger to print the assertion failure, thus ending in a
        // infinite loop.
        if(instance_ == nullptr) {
            std::cerr
                << "CRITICAL: Accessing uninitialized Logger instance"
                << std::endl;
            geo_abort();
        }
        return instance_;
    }

    std::ostream& Logger::err_console() {
        static_cast<CERRStream*>(err_console_)->lock();
        return *err_console_;
    }

    std::ostream& Logger::div(const std::string& title) {
        std::ostream& result =
            (is_initialized() && !Process::is_running_threads()) ?
            instance()->div_stream(title) :
            (instance()->err_console() << "=====" << title << std::endl);
        return result;
    }

    std::ostream& Logger::out(const std::string& feature) {
        std::ostream& result =
            (is_initialized() && !Process::is_running_threads()) ?
            instance()->out_stream(feature) :
            (instance()->err_console() << "    [" << feature << "] ");
        return result;
    }

    std::ostream& Logger::err(const std::string& feature) {
        std::ostream& result =
            (is_initialized() && !Process::is_running_threads()) ?
            instance()->err_stream(feature) :
            (instance()->err_console() << "(E)-[" << feature << "] ");
        return result;
    }

    std::ostream& Logger::warn(const std::string& feature) {
        std::ostream& result =
            (is_initialized() && !Process::is_running_threads()) ?
            instance()->warn_stream(feature) :
            (instance()->err_console() << "(W)-[" << feature << "] ");
        return result;
    }

    std::ostream& Logger::status() {
        std::ostream& result =
            (is_initialized() && !Process::is_running_threads()) ?
            instance()->status_stream() :
            (instance()->err_console() << "[status] ");
        return result;
    }

    std::ostream& Logger::div_stream(const std::string& title) {
        if(!quiet_) {
            current_feature_changed_ = true;
            current_feature_.clear();
            LoggerClients clients = clients_; // clients_ may be modified !
            for(auto it : clients) {
                it->div(title);
            }
        }
        return out_;
    }

    std::ostream& Logger::out_stream(const std::string& feature) {
        if(!quiet_ && !minimal_ && current_feature_ != feature) {
            current_feature_changed_ = true;
            current_feature_ = feature;
        }
        return out_;
    }

    std::ostream& Logger::err_stream(const std::string& feature) {
        if(!quiet_ && current_feature_ != feature) {
            current_feature_changed_ = true;
            current_feature_ = feature;
        }
        return err_;
    }

    std::ostream& Logger::warn_stream(const std::string& feature) {
        if(!quiet_ && current_feature_ != feature) {
            current_feature_changed_ = true;
            current_feature_ = feature;
        }
        return warn_;
    }

    std::ostream& Logger::status_stream() {
        return status_;
    }

    void Logger::notify_out(const std::string& message) {
        if(
            (log_everything_ &&
             log_features_exclude_.find(current_feature_) ==
             log_features_exclude_.end())
            || (log_features_.find(current_feature_) != log_features_.end())
        ) {
            std::string feat_msg =
                CmdLine::ui_feature(current_feature_, current_feature_changed_)
                + message;

            LoggerClients clients = clients_; // clients_ may be modified !
            for(auto it : clients) {
                it->out(feat_msg);
            }

            current_feature_changed_ = false;
        }
    }

    void Logger::notify_warn(const std::string& message) {
        std::string msg = "Warning: " + message;
        std::string feat_msg =
            CmdLine::ui_feature(current_feature_, current_feature_changed_)
            + msg;

        LoggerClients clients = clients_; // clients_ may be modified !
        for(auto it : clients) {
            it->warn(feat_msg);
            it->status(msg);
        }

        current_feature_changed_ = false;
    }

    void Logger::notify_err(const std::string& message) {
        std::string msg = "Error: " + message;
        std::string feat_msg =
            CmdLine::ui_feature(current_feature_, current_feature_changed_)
            + msg;

        if(notifying_error_) {
            std::cerr << "Error while displaying error (!):"
                      << feat_msg << std::endl;
        } else {
            notifying_error_ = true;
            LoggerClients clients = clients_; // clients_ may be modified !
            for(auto it : clients) {
                it->err(feat_msg);
                it->status(msg);
            }
            notifying_error_ = false;
        }

        current_feature_changed_ = false;
    }

    void Logger::notify_status(const std::string& message) {
        LoggerClients clients = clients_; // clients_ may be modified !
        for(auto it : clients) {
            it->status(message);
        }

        current_feature_changed_ = false;
    }

    void Logger::notify(LoggerStream* s, const std::string& message) {

        if(quiet_ || (minimal_ && s == &out_) || clients_.empty()) {
            return;
        }

        if(s == &out_) {
            notify_out(message);
        } else if(s == &warn_) {
            notify_warn(message);
        } else if(s == &err_) {
            notify_err(message);
        } else if(s == &status_) {
            notify_status(message);
        } else {
            geo_assert_not_reached;
        }
    }

    /************************************************************************/

}

extern "C" {
}

// =================== Console UI implementation (formerly command_line.cpp) ===

#if defined(GEOBRL_OS_LINUX) || defined(GEOBRL_OS_APPLE)
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#endif

#ifdef GEOBRL_OS_WINDOWS
#include <io.h>
#endif

namespace {

    using namespace GEOBRL;

    /** Maximum length of a feature name used in ui_feature() formatting */
    const unsigned int feature_max_length = 12;

    bool ui_separator_opened = false;

    index_t ui_term_width = 79;

    index_t ui_left_margin = 0;

    index_t ui_right_margin = 0;

    const char working[] = {'|', '/', '-', '\\'};

    index_t working_index = 0;

    const char waves[] = {',', '.', 'o', 'O', '\'', 'O', 'o', '.', ','};

    inline std::ostream& ui_out() {
        return std::cout;
    }

    inline void ui_pad(char c, size_t nb) {
        for(index_t i = 0; i < nb; i++) {
            std::cout << c;
        }
    }

    bool is_redirected() {
        static bool initialized = false;
        static bool result;
        if(!initialized) {
#ifdef GEOBRL_OS_WINDOWS
            result = !_isatty(1);
#else
            result = !isatty(1);
#endif
            initialized = true;
        }
        return result || !Logger::instance()->is_pretty();
    }

    void update_ui_term_width() {
#ifndef GEOBRL_OS_WINDOWS
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

    inline size_t sub(size_t a, size_t b) {
        return a > b ? a - b : 0;
    }
}

namespace GEOBRL {

    namespace CmdLine {

        void terminate() {
        }

        index_t ui_terminal_width() {
            index_t ui_term_width_bkp = ui_term_width;
            update_ui_term_width();
            ui_term_width = std::min(ui_term_width, ui_term_width_bkp);
            return ui_term_width;
        }

        void ui_message(const std::string& message) {
            ui_message(message, feature_max_length + 5);
        }

        void ui_message(
            const std::string& message,
            index_t wrap_margin
        ) {
            if(Logger::instance()->is_quiet()) {
                return;
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
                    ui_pad(' ', ui_left_margin);
                    ui_out() << "| ";
                    ui_pad(' ', wrap);
                    ui_out() << cur.substr(0, newline);
                    ui_pad(' ', sub(maxL,newline));
                    ui_out() << " |" << std::endl;
                    cur = cur.substr(newline + 1);
                } else if(cur.length() > maxL) {
                    ui_pad(' ', ui_left_margin);
                    ui_out() << "| ";
                    ui_pad(' ', wrap);
                    ui_out() << cur.substr(0, maxL);
                    ui_out() << " |" << std::endl;
                    cur = cur.substr(maxL);
                } else if(cur.length() != 0) {
                    ui_pad(' ', ui_left_margin);
                    ui_out() << "| ";
                    ui_pad(' ', wrap);
                    ui_out() << cur;
                    ui_pad(' ', sub(maxL,cur.length()));
                    ui_out() << " |";
                    break;
                } else {
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
