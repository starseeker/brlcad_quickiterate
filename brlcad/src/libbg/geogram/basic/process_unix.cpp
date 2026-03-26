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

#include <geogram/basic/geogram_common.h>

#ifdef GEOBRL_OS_UNIX

#include <geogram/basic/process.h>
#include <geogram/basic/process_private.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/progress.h>

// LineInput: inlined from basic/line_stream.h / line_stream.cpp
#include <geogram/basic/assert.h>
#include <geogram/basic/numeric.h>
#include <geogram/basic/string.h>
#include <cstring>
#include <stdio.h>
#include <ctype.h>

namespace GEOBRL {
    class LineInput {
    public:
        LineInput(const std::string& filename) :
            file_name_(filename), line_num_(0) {
            F_ = fopen(filename.c_str(), "r");
            ok_ = (F_ != nullptr);
            line_[0] = '\0';
        }
        ~LineInput() {
            if(F_ != nullptr) { fclose(F_); F_ = nullptr; }
        }
        bool OK() const { return ok_; }
        bool eof() const { return feof(F_) ? true : false; }
        bool get_line() {
            if(F_ == nullptr) return false;
            line_[0] = '\0';
            while(!isprint(line_[0]) && line_[0] != '\t') {
                ++line_num_;
                if(fgets(line_, MAX_LINE_LEN, F_) == nullptr) return false;
            }
            bool check_multiline = true;
            Numeric::int64 total_length = MAX_LINE_LEN;
            char* ptr = line_;
            while(check_multiline) {
                size_t L = strlen(ptr);
                total_length -= Numeric::int64(L);
                ptr = ptr + L - 2;
                if(*ptr == '\\' && total_length > 0) {
                    *ptr = ' '; ptr++;
                    if(fgets(ptr, int(total_length), F_) == nullptr) return false;
                    ++line_num_;
                } else {
                    check_multiline = false;
                }
            }
            if(total_length < 0) {
                Logger::err("LineInput")
                    << "MultiLine longer than " << MAX_LINE_LEN << " bytes" << std::endl;
            }
            return true;
        }
        index_t nb_fields() const { return index_t(field_.size()); }
        size_t line_number() const { return line_num_; }
        char* field(index_t i) { geo_assert(i < nb_fields()); return field_[i]; }
        const char* field(index_t i) const { geo_assert(i < nb_fields()); return field_[i]; }
        signed_index_t field_as_int(index_t i) const {
            signed_index_t result = 0;
            if(!String::from_string(field(i), result)) conversion_error(i, "integer");
            return result;
        }
        index_t field_as_uint(index_t i) const {
            index_t result = 0;
            if(!String::from_string(field(i), result)) conversion_error(i, "unsigned integer");
            return result;
        }
        double field_as_double(index_t i) const {
            double result = 0;
            if(!String::from_string(field(i), result)) conversion_error(i, "floating point");
            return result;
        }
        bool field_matches(index_t i, const char* s) const {
            return strcmp(field(i), s) == 0;
        }
        void get_fields(const char* separators = " \t\r\n") {
            field_.resize(0);
            char* context = nullptr;
#ifdef GEOBRL_OS_WINDOWS
            char* tok = strtok_s(line_, separators, &context);
#else
            char* tok = strtok_r(line_, separators, &context);
#endif
            while(tok != nullptr) {
                field_.push_back(tok);
#ifdef GEOBRL_OS_WINDOWS
                tok = strtok_s(nullptr, separators, &context);
#else
                tok = strtok_r(nullptr, separators, &context);
#endif
            }
        }
        const char* current_line() const { return line_; }
    private:
        GEOBRL_NORETURN_DECL void conversion_error(index_t index, const char* type) const GEOBRL_NORETURN {
            std::ostringstream out;
            out << "Line " << line_num_ << ": field #" << index
                << " is not a valid " << type << " value: " << field(index);
            throw std::logic_error(out.str());
        }
        static constexpr index_t MAX_LINE_LEN = 65535;
        FILE* F_;
        std::string file_name_;
        size_t line_num_;
        char line_[MAX_LINE_LEN];
        std::vector<char*> field_;
        bool ok_;
    };
}

#include <sstream>
#include <unistd.h>
#include <limits.h>
#include <fenv.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <new>

#if !defined(GEOBRL_OS_ANDROID) && !defined(GEOBRL_OS_EMSCRIPTEN)
#include <execinfo.h>
#endif

#ifdef GEOBRL_OS_APPLE
#include <mach-o/dyld.h>
#ifdef __x86_64
#include <xmmintrin.h>
#endif
#endif

// Suppresses a warning with CLANG when sigaction is used.
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif

namespace {

    using namespace GEOBRL;

    /**
     * \brief Abnormal termination handler
     * \details If \p message is
     * non null, the following message is printed before exiting.
     * <em>Abnormal program termination: message</em>
     * \param[in] message optional message to print
     */
    GEOBRL_NORETURN_DECL void abnormal_program_termination(
        const char* message = nullptr
    ) GEOBRL_NORETURN;

    void abnormal_program_termination(const char* message) {
        if(message != nullptr) {
            // Do not use Logger here!
            std::cout
                << "Abnormal program termination: "
                << message << std::endl;
        }
        exit(1);
    }

    /**
     * \brief Signal handler
     * \details The handler exits the application
     * \param[in] signal signal number
     */
    GEOBRL_NORETURN_DECL void signal_handler(int signal) GEOBRL_NORETURN;

    void signal_handler(int signal) {
        const char* sigstr = strsignal(signal);
        std::ostringstream os;
        os << "received signal " << signal << " (" << sigstr << ")";
        Process::os_print_stack_trace();
        abnormal_program_termination(os.str().c_str());
    }

    /**
     * \brief Floating point error handler
     * \details The handler exits the application
     * \param[in] signal signal number
     * \param[in] si signal information structure
     * \param[in] data additional data (unused)
     */
    GEOBRL_NORETURN_DECL void fpe_signal_handler(
        int signal, siginfo_t* si, void* data
    ) GEOBRL_NORETURN;

    void fpe_signal_handler(int signal, siginfo_t* si, void* data) {
        geo_argused(signal);
        geo_argused(data);
        const char* error;
        switch(si->si_code) {
        case FPE_INTDIV:
            error = "integer divide by zero";
            break;
        case FPE_INTOVF:
            error = "integer overflow";
            break;
        case FPE_FLTDIV:
            error = "floating point divide by zero";
            break;
        case FPE_FLTOVF:
            error = "floating point overflow";
            break;
        case FPE_FLTUND:
            error = "floating point underflow";
            break;
        case FPE_FLTRES:
            error = "floating point inexact result";
            break;
        case FPE_FLTINV:
            error = "floating point invalid operation";
            break;
        case FPE_FLTSUB:
            error = "subscript out of range";
            break;
        default:
            error = "unknown";
            break;
        }

        std::ostringstream os;
        os << "floating point exception detected: " << error;
        abnormal_program_termination(os.str().c_str());
    }

    /**
     * \brief Interrupt signal handler
     * \details The handler cancels the current task if any or exits the
     * program.
     */
    void sigint_handler(int) {
        if(Progress::current_progress_task() != nullptr) {
            Progress::cancel();
        } else {
            exit(1);
        }
    }

    /**
     * \brief Catches uncaught C++ exceptions
     */
    GEOBRL_NORETURN_DECL void terminate_handler() GEOBRL_NORETURN;

    void terminate_handler() {
        abnormal_program_termination("function terminate() was called");
    }

    /**
     * \brief Catches allocation errors
     */
    GEOBRL_NORETURN_DECL void memory_exhausted_handler() GEOBRL_NORETURN;

    void memory_exhausted_handler() {
        abnormal_program_termination("memory exhausted");
    }
}

/****************************************************************************/

namespace GEOBRL {

    namespace Process {

        bool os_init_threads() {
            // Thread management is handled by CXX17ThreadManager in process.cpp
            return false;
        }

        void os_brute_force_kill() {
            kill(getpid(), SIGKILL);
        }

        index_t os_number_of_cores() {
            return index_t(sysconf(_SC_NPROCESSORS_ONLN));
        }

        size_t os_used_memory() {
#ifdef GEOBRL_OS_APPLE
            size_t result = 0;
            struct rusage usage;
            if(0 == getrusage(RUSAGE_SELF, &usage)) {
                result = (size_t) usage.ru_maxrss;
            }
            return result;
#else
            // The following method seems to be more
            // reliable than  getrusage() under Linux.
            // It works for both Linux and Android.
            size_t result = 0;
            LineInput in("/proc/self/status");
            while(!in.eof() && in.get_line()) {
                in.get_fields();
                if(in.field_matches(0,"VmSize:")) {
                    result = size_t(in.field_as_uint(1)) * size_t(1024);
                    break;
                }
            }
            return result;

            /*
              const char* statm_path = "/proc/self/statm";
              unsigned long size,resident,share,text,lib,data,dt;
              FILE *F = fopen(statm_path,"r");
              if(F == nullptr) {
              perror(statm_path);
              abort();
              }
              if(
              fscanf(F,"%ld %ld %ld %ld %ld %ld %ld",
              &size,&resident,&share,&text,&lib,&data,&dt
              ) != 7
              ) {
              perror(statm_path);
              abort();
              }
              fclose(f);
            */
#endif
        }

        size_t os_max_used_memory() {
            // The following method seems to be more
            // reliable than  getrusage() under Linux.
            // It works for both Linux and Android.
            size_t result = 0;
            LineInput in("/proc/self/status");

            // Some versions of Unix may not have the proc
            // filesystem (or a different organization)
            if(!in.OK()) {
                return result;
            }

            while(!in.eof() && in.get_line()) {
                in.get_fields();
                if(in.field_matches(0,"VmPeak:")) {
                    result = size_t(in.field_as_uint(1)) * size_t(1024);
                    break;
                }
            }
            return result;
        }

        bool os_enable_FPE(bool flag) {
#if defined(GEOBRL_OS_APPLE) || defined(GEOBRL_OS_EMSCRIPTEN)
            geo_argused(flag);
#else
            int excepts = 0
                // | FE_INEXACT     // inexact result
                | FE_DIVBYZERO   // division by zero
                | FE_UNDERFLOW   // result not representable due to underflow
                | FE_OVERFLOW    // result not representable due to overflow
                | FE_INVALID     // invalid operation
                ;
            if(flag) {
                feenableexcept(excepts);
            } else {
                fedisableexcept(excepts);
            }
#endif
            return true;
        }

        bool os_enable_cancel(bool flag) {
            if(flag) {
                signal(SIGINT, sigint_handler);
            } else {
                signal(SIGINT, SIG_DFL);
            }
            return true;
        }

        /**
         * \brief Installs signal handlers
         * \details
         * On Unix, this installs handlers for the standard signals.
         * On Windows, this also installs all kind of exception handling
         * routines that prevent the application from being blocked by a bad
         * assertion, a runtime check or runtime error.
         */
        void os_install_signal_handlers() {
            // Install signal handlers
            signal(SIGSEGV, signal_handler);
            signal(SIGILL, signal_handler);
            signal(SIGBUS, signal_handler);

            // Use sigaction for SIGFPE as it provides more details
            // about the error.
            struct sigaction sa, old_sa;
            sa.sa_flags = SA_SIGINFO;
            sa.sa_sigaction = fpe_signal_handler;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGFPE, &sa, &old_sa);

            // Install uncaught c++ exception handlers
            std::set_terminate(terminate_handler);

            // Install memory allocation handler
            std::set_new_handler(memory_exhausted_handler);
        }


        /**
         * \brief Gets the full path to the current executable.
         */
        std::string os_executable_filename() {
            char buff[PATH_MAX];
#ifdef GEOBRL_OS_APPLE
            uint32_t len=PATH_MAX;
            if (_NSGetExecutablePath(buff, &len) == 0) {
                std::string filename(buff);
                size_t pos = std::string::npos;
                while( (pos=filename.find("/./")) != std::string::npos ) {
                    filename.replace(pos, 3, "/");
                }
                return filename;
            }
            return std::string("");
#else
            ssize_t len = ::readlink("/proc/self/exe", buff, sizeof(buff)-1);
            if (len != -1) {
                buff[len] = '\0';
                return std::string(buff);
            }
            return std::string("");
#endif
        }

        void os_print_stack_trace() {
#if !defined(GEOBRL_OS_ANDROID) && !defined(GEOBRL_OS_EMSCRIPTEN)
            constexpr int MAX_STACK_FRAMES=128;
            static void *stack_traces[MAX_STACK_FRAMES];
            int i, trace_size = 0;
            char **messages = nullptr;
            trace_size = backtrace(stack_traces, MAX_STACK_FRAMES);
            messages = backtrace_symbols(stack_traces, trace_size);
            for (i = 0; i < trace_size; ++i)  {
                fprintf(stderr,"Stacktrace: %s\n",messages[i]);
            }
            if (messages != nullptr) {
                free(messages);
            }
#endif
        }
    }

}

#else

// Declare a dummy variable so that
// MSVC does not complain that it
// generated an empty object file.
int dummy_process_unix_compiled = 1;

#endif
