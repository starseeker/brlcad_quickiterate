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

#include <geogram/basic/process.h>
#include <geogram/basic/process_private.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/string.h>
#include <thread>
#include <chrono>

namespace {
    using namespace GEOBRL;
    static inline double geo_now() {
        auto t = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch());
        return 0.001 * double(ms.count());
    }

    std::atomic<int> running_threads_invocations_{0};

    bool multithreading_initialized_ = false;
    bool multithreading_enabled_ = true;

    index_t max_threads_initialized_ = false;
    index_t max_threads_ = 0;

    bool fpe_initialized_ = false;
    bool fpe_enabled_ = false;

    bool cancel_initialized_ = false;
    bool cancel_enabled_ = false;

    double start_time_ = 0.0;
}

namespace GEOBRL {

    namespace Process {

        void initialize(int flags) {

            Logger::out("Process")
                << "Using C++17 threads"
                << std::endl;

            if(
                (::getenv("GEOBRL_NO_SIGNAL_HANDLER") == nullptr) &&
                ((flags & GEOBRLCAD_INSTALL_HANDLERS) != 0)
            ) {
                os_install_signal_handlers();
            }

            // Initialize Process default values
            enable_multithreading(multithreading_enabled_);
            set_max_threads(number_of_cores());
            if (flags & GEOBRLCAD_INSTALL_FPE) {
                enable_FPE(fpe_enabled_);
            }
            enable_cancel(cancel_enabled_);

            start_time_ = geo_now();
        }

        void show_stats() {

            Logger::out("Process") << "Total elapsed time: "
                                   << (geo_now() - start_time_) << "s" << std::endl;

            const size_t K=size_t(1024);
            const size_t M=K*K;
            const size_t G=K*M;

            size_t max_mem = Process::max_used_memory() ;
            size_t r = max_mem;

            size_t mem_G = r / G;
            r = r % G;
            size_t mem_M = r / M;
            r = r % M;
            size_t mem_K = r / K;
            r = r % K;

            std::string s;
            if(mem_G != 0) {
                s += String::to_string(mem_G)+"G ";
            }
            if(mem_M != 0) {
                s += String::to_string(mem_M)+"M ";
            }
            if(mem_K != 0) {
                s += String::to_string(mem_K)+"K ";
            }
            if(r != 0) {
                s += String::to_string(r);
            }

            Logger::out("Process") << "Maximum used memory: "
                                   << max_mem << " (" << s << ")"
                                   << std::endl;
        }

        void terminate() {
        }

        void brute_force_kill() {
            os_brute_force_kill();
        }

        index_t number_of_cores() {
            static index_t result = 0;
            if(result == 0) {
                result = index_t(std::thread::hardware_concurrency());
                if(result == 0) {
                    result = 1;
                }
            }
            return result;
        }

        size_t used_memory() {
            return os_used_memory();
        }

        size_t max_used_memory() {
            return os_max_used_memory();
        }

        std::string executable_filename() {
            return os_executable_filename();
        }

        void print_stack_trace() {
            os_print_stack_trace();
        }

        bool is_running_threads() {
            return running_threads_invocations_ > 0;
        }

        bool multithreading_enabled() {
            return multithreading_enabled_;
        }

        void enable_multithreading(bool flag) {
            if(
                multithreading_initialized_ &&
                multithreading_enabled_ == flag
            ) {
                return;
            }
            multithreading_initialized_ = true;
            multithreading_enabled_ = flag;
            if(multithreading_enabled_) {
                Logger::out("Process")
                    << "Multithreading enabled" << std::endl
                    << "Available cores = " << number_of_cores()
                    << std::endl;
                // Logger::out("Process")
                //    << "Max. concurrent threads = "
                //    << maximum_concurrent_threads() << std::endl ;
                if(number_of_cores() == 1) {
                    Logger::warn("Process")
                        << "Processor is not a multicore"
                        << "(or multithread is not supported)"
                        << std::endl;
                }
            } else {
                Logger::out("Process")
                    << "Multithreading disabled" << std::endl;
            }
        }

        index_t max_threads() {
            return max_threads_initialized_
                ? max_threads_
                : number_of_cores();
        }

        void set_max_threads(index_t num_threads) {
            if(
                max_threads_initialized_ &&
                max_threads_ == num_threads
            ) {
                return;
            }
            max_threads_initialized_ = true;
            if(num_threads == 0) {
                num_threads = 1;
            } else if(num_threads > number_of_cores()) {
                Logger::warn("Process")
                    << "Cannot allocate " << num_threads
                    << " for multithreading"
                    << std::endl;
                num_threads = number_of_cores();
            }
            max_threads_ = num_threads;
            Logger::out("Process")
                << "Max used threads = " << max_threads_
                << std::endl;
        }

        index_t maximum_concurrent_threads() {
            if(!multithreading_enabled_) {
                return 1;
            }
            return max_threads_;
        }

        bool FPE_enabled() {
            return fpe_enabled_;
        }

        void enable_FPE(bool flag) {
            if(fpe_initialized_ && fpe_enabled_ == flag) {
                return;
            }
            fpe_initialized_ = true;
            fpe_enabled_ = flag;
            os_enable_FPE(flag);
        }

        bool cancel_enabled() {
            return cancel_enabled_;
        }

        void enable_cancel(bool flag) {
            if(cancel_initialized_ && cancel_enabled_ == flag) {
                return;
            }
            cancel_initialized_ = true;
            cancel_enabled_ = flag;

            if(os_enable_cancel(flag)) {
                Logger::out("Process")
                    << (flag ? "Cancel mode enabled" : "Cancel mode disabled")
                    << std::endl;
            } else {
                Logger::warn("Process")
                    << "Cancel mode not implemented" << std::endl;
            }
        }
    }
}


namespace GEOBRL {

    void parallel_for(
        index_t from, index_t to, std::function<void(index_t)> func,
        index_t threads_per_core, bool interleaved
    ) {
        index_t nb_threads = std::min(
            to - from,
            Process::maximum_concurrent_threads() * threads_per_core
        );
        nb_threads = std::max(index_t(1), nb_threads);
        index_t batch_size = (to - from) / nb_threads;

        if(running_threads_invocations_ > 0 || nb_threads == 1) {
            for(index_t i = from; i < to; i++) {
                func(i);
            }
            return;
        }

        ++running_threads_invocations_;
        std::vector<std::thread> threads;
        threads.reserve(nb_threads);

        if(interleaved) {
            for(index_t i = 0; i < nb_threads; i++) {
                threads.emplace_back([func, from, to, i, nb_threads]() {
                    for(index_t j = from + i; j < to; j += nb_threads) {
                        func(j);
                    }
                });
            }
        } else {
            index_t cur = from;
            for(index_t i = 0; i < nb_threads; i++) {
                index_t end = (i == nb_threads - 1) ? to : cur + batch_size;
                threads.emplace_back([func, cur, end]() {
                    for(index_t j = cur; j < end; j++) {
                        func(j);
                    }
                });
                cur += batch_size;
            }
        }

        for(auto& t : threads) t.join();
        --running_threads_invocations_;
    }


    void parallel_for_slice(
        index_t from, index_t to, std::function<void(index_t, index_t)> func,
        index_t threads_per_core
    ) {
        index_t nb_threads = std::min(
            to - from,
            Process::maximum_concurrent_threads() * threads_per_core
        );
        nb_threads = std::max(index_t(1), nb_threads);
        index_t batch_size = (to - from) / nb_threads;

        if(running_threads_invocations_ > 0 || nb_threads == 1) {
            func(from, to);
            return;
        }

        ++running_threads_invocations_;
        std::vector<std::thread> threads;
        threads.reserve(nb_threads);

        index_t cur = from;
        for(index_t i = 0; i < nb_threads; i++) {
            index_t end = (i == nb_threads - 1) ? to : cur + batch_size;
            threads.emplace_back([func, cur, end]() {
                func(cur, end);
            });
            cur += batch_size;
        }

        for(auto& t : threads) t.join();
        --running_threads_invocations_;
    }

    void parallel(
        std::function<void()> f1,
        std::function<void()> f2
    ) {
        if(running_threads_invocations_ > 0 || Process::maximum_concurrent_threads() <= 1) {
            f1(); f2();
            return;
        }
        ++running_threads_invocations_;
        std::thread t1(f1), t2(f2);
        t1.join(); t2.join();
        --running_threads_invocations_;
    }


    void parallel(
        std::function<void()> f1,
        std::function<void()> f2,
        std::function<void()> f3,
        std::function<void()> f4
    ) {
        if(running_threads_invocations_ > 0 || Process::maximum_concurrent_threads() <= 1) {
            f1(); f2(); f3(); f4();
            return;
        }
        ++running_threads_invocations_;
        std::thread t1(f1), t2(f2), t3(f3), t4(f4);
        t1.join(); t2.join(); t3.join(); t4.join();
        --running_threads_invocations_;
    }


    void parallel(
        std::function<void()> f1,
        std::function<void()> f2,
        std::function<void()> f3,
        std::function<void()> f4,
        std::function<void()> f5,
        std::function<void()> f6,
        std::function<void()> f7,
        std::function<void()> f8
    ) {
        if(running_threads_invocations_ > 0 || Process::maximum_concurrent_threads() <= 1) {
            f1(); f2(); f3(); f4(); f5(); f6(); f7(); f8();
            return;
        }
        ++running_threads_invocations_;
        std::thread t1(f1), t2(f2), t3(f3), t4(f4);
        std::thread t5(f5), t6(f6), t7(f7), t8(f8);
        t1.join(); t2.join(); t3.join(); t4.join();
        t5.join(); t6.join(); t7.join(); t8.join();
        --running_threads_invocations_;
    }

    namespace Process {
        void sleep(index_t microseconds) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(microseconds)
            );
        }
    }
}
