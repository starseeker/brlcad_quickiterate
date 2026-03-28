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

#ifndef GEOBRLCAD_BASIC_PROCESS
#define GEOBRLCAD_BASIC_PROCESS

#include <geogram/basic/geogram_common.h>
#include <geogram/basic/thread_sync.h>
#include <functional>

/**
 * \file geogram/basic/process.h
 * \brief Function and classes for process manipulation
 */

namespace GEOBRL {

    /**
     * \brief Abstraction layer for process management and multi-threading.
     */
    namespace Process {

        /**
         * \brief Initializes GeogramLib
         * \param[in] flags the flags passed to GEOBRL::initialize()
         * \details This function must be called once before using
         * any functionality of GeogramLib.
         */
        void GEOBRLCAD_API initialize(int flags);

        /**
         * \brief Terminates GeogramLib
         * \details This function is called automatically when the program
         * exits, so it should never be called directly.
         */
        void GEOBRLCAD_API terminate();


        /**
         * \brief Sleeps for a period of time.
         * \param[in] microseconds the time to sleep,
         *  in microseconds.
         */
        void GEOBRLCAD_API sleep(index_t microseconds);

        /**
         * \brief Displays statistics about the current process
         * \details Displays the maximum used amount of memory.
         */
        void GEOBRLCAD_API show_stats();

        /**
         * \brief Terminates the current process.
         */
        void GEOBRLCAD_API brute_force_kill();

        /**
         * \brief Returns the maximum number of threads that can be running
         * simultaneously.
         * \retval The number of cores if multi-threading is supported
         * \retval 1 otherwise.
         */
        index_t GEOBRLCAD_API maximum_concurrent_threads();

        /**
         * \brief Gets the number of available cores
         * \return The number of available cores including the "virtual ones" if
         * hyper-threading is activated.
         */
        index_t GEOBRLCAD_API number_of_cores();

        /**
         * \brief Checks whether threads are running.
         * \retval true if concurrent threads are currently running as an
         * effect to Process::run_threads().
         * \retval false otherwise.
         * \see Process::run_threads()
         */
        bool GEOBRLCAD_API is_running_threads();

        /**
         * \brief Enables/disables floating point exceptions
         * \details If FPEs are enabled, then floating point exceptions
         * raise a SIGFPE signal, otherwise they generate NaNs. FPEs can also
         * be configured by setting the value of the property "sys:FPE" with
         * Environment::set_value().
         * \param[in] flag set to \c true to enable FPEs, \c false to disable.
         * \see FPE_enabled()
         */
        void GEOBRLCAD_API enable_FPE(bool flag);

        /**
         * \brief Gets the status of floating point exceptions
         * \retval true if FPE are enabled
         * \retval false otherwise
         * \see enable_FPE()
         */
        bool GEOBRLCAD_API FPE_enabled();

        /**
         * \brief Enables/disables multi-threaded computations
         * Multi-threading can also be configured by setting the value of the
         * property "sys:multithread" with Environment::set_value().
         * \param[in] flag set to \c true to enable multi-threading, \c false
         * to disable.
         * \see multithreading_enabled()
         */
        void GEOBRLCAD_API enable_multithreading(bool flag);

        /**
         * \brief Gets the status of multi-threading
         * \retval true if multi-threading is enabled
         * \retval false otherwise
         * \see enable_multithreading()
         */
        bool GEOBRLCAD_API multithreading_enabled();

        /**
         * \brief Limits the number of concurrent threads to use
         * \details The number of threads can also be configured by setting
         * the value of the property "sys:max_threads" with
         * Environment::set_value().
         * \param[in] num_threads maximum number of threads to use.
         * \see max_threads()
         */
        void GEOBRLCAD_API set_max_threads(index_t num_threads);

        /**
         * \brief Gets the number of allowed concurrent threads
         * \see set_max_threads()
         */
        index_t GEOBRLCAD_API max_threads();

        /**
         * \brief Enables interruption of cancelable tasks
         * \details This allows to interrupt cancelable tasks by typing
         * CTRL-C in the terminal. This sets a specific handler on the
         * interrupt signal that calls Progress::cancel() is there is a
         * running cancelable task. If no task is running, the program is
         * interrupted. The cancel mode can also be configured by setting the
         * value of the property "sys:cancel" with
         * Environment::set_value().
         * \param[in] flag set to \c true to enable cancel mode, \c false
         * to disable.
         * \see cancel_enabled()
         */
        void GEOBRLCAD_API enable_cancel(bool flag);

        /**
         * \brief Gets the status of the cancel mode
         * \retval true if the cancel mode is enabled
         * \retval false otherwise
         * \see enable_cancel()
         */
        bool GEOBRLCAD_API cancel_enabled();

        /**
         * \brief Gets the currently used memory.
         * \return the used memory in bytes
         */
        size_t GEOBRLCAD_API used_memory();

        /**
         * \brief Gets the maximum used memory.
         * \return the maximum used memory in bytes
         */
        size_t GEOBRLCAD_API max_used_memory();

        /**
         * \brief Gets the full path to the currently
         *  running program.
         */
        std::string GEOBRLCAD_API executable_filename();

        /**
         * \brief Prints a stack trace to the standard error.
         */
        void print_stack_trace();
    }

    /**
     * \brief Executes a loop with concurrent threads.
     * \details
     *   Executes a parallel for loop from index \p to index \p to, calling
     *   functional object \p func at each iteration.
     *
     * Calling parallel_for(from, to, func) is equivalent
     * to the following loop, computed in parallel:
     * \code
     * for(index_t i = from; i < to; i++) {
     *    func(i)
     * }
     * \endcode
     *
     * When applicable, iterations are executed by concurrent threads:
     * the range of the loop is split in to several contiguous
     * sub-ranges, each of them being executed by a separate thread.
     *
     * If parameter \p interleaved is set to true, the loop range is
     * decomposed in interleaved index sets. Interleaved execution may
     * improve cache coherency.
     *
     * \param[in] func function that takes an index_t.
     * \param[in] from the first iteration index
     * \param[in] to one position past the last iteration index
     * \param[in] threads_per_core number of threads to allocate per physical
     *  core (default is 1).
     * \param[in] interleaved if set to \c true, indices are allocated to
     * threads with an interleaved pattern.
     */
    void GEOBRLCAD_API parallel_for(
        index_t from, index_t to, std::function<void(index_t)> func,
        index_t threads_per_core = 1,
        bool interleaved = false
    );

    /**
     * \brief Executes a loop with concurrent threads.
     *
     * \details
     * When applicable, iterations are executed by concurrent
     * threads: the range of the loop is split in to several contiguous
     * sub-ranges, each of them being executed by a separate thread.
     *
     * Calling parallel_for(func, from, to) is equivalent
     * to the following loop, computed in parallel:
     * \code
     *   func(from, i1);
     *   func(i1, i2);
     *   ...
     *   func(in, to);
     * \endcode
     * where i1,i2,...in are automatically generated. Typically one interval
     * per physical core is generated.
     *
     * \param[in] func functional object that accepts two arguments of
     *  type index_t.
     * \param[in] from first iteration index of the loop
     * \param[in] to one position past the last iteration index
     * \param[in] threads_per_core number of threads to allocate per physical
     *  core (default is 1).
     */
    void GEOBRLCAD_API parallel_for_slice(
        index_t from, index_t to, std::function<void(index_t, index_t)> func,
        index_t threads_per_core = 1
    );

    /**
     * \brief Calls functions in parallel.
     * \details Can be typically used with lambdas that capture this. See
     *  mesh/mesh_reorder.cpp and points/kd_tree.cpp for examples.
     * \param[in] f1 , f2 functions to be called in parallel.
     */
    void GEOBRLCAD_API parallel(
        std::function<void()> f1,
        std::function<void()> f2
    );

    /**
     * \brief Calls functions in parallel.
     * \details Can be typically used with lambdas that capture this. See
     *  mesh/mesh_reorder.cpp and points/kd_tree.cpp for examples.
     * \param[in] f1 , f2 , f3 , f4 functions to be called in parallel.
     */
    void GEOBRLCAD_API parallel(
        std::function<void()> f1,
        std::function<void()> f2,
        std::function<void()> f3,
        std::function<void()> f4
    );

    /**
     * \brief Calls functions in parallel.
     * \details Can be typically used with lambdas that capture this. See
     *  mesh/mesh_reorder.cpp and points/kd_tree.cpp for examples.
     * \param[in] f1 , f2 , f3 , f4 , f5 , f6 , f7 , f8 functions
     *  to be called in parallel.
     */
    void GEOBRLCAD_API parallel(
        std::function<void()> f1,
        std::function<void()> f2,
        std::function<void()> f3,
        std::function<void()> f4,
        std::function<void()> f5,
        std::function<void()> f6,
        std::function<void()> f7,
        std::function<void()> f8
    );

}

#endif
