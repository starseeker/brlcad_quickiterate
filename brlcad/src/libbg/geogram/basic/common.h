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

#ifndef GEOBRLCAD_BASIC_COMMON
#define GEOBRLCAD_BASIC_COMMON


/**
 * \brief Basic definitions for the Geogram C API
 */

/*
 * Deactivate warnings about documentation
 * We do that, because CLANG's doxygen parser does not know
 * some doxygen commands that we use (retval, copydoc) and
 * generates many warnings for them...
 */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#endif

/**
 * \brief Linkage declaration for geogram symbols.
 */

#if defined(GEOBRL_DYNAMIC_LIBS)
#if defined(_MSC_VER)
#define GEOBRL_IMPORT __declspec(dllimport)
#define GEOBRL_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define GEOBRL_IMPORT
#define GEOBRL_EXPORT __attribute__ ((visibility("default")))
#else
#define GEOBRL_IMPORT
#define GEOBRL_EXPORT
#endif
#else
#define GEOBRL_IMPORT
#define GEOBRL_EXPORT
#endif

#ifdef geogram_EXPORTS
#define GEOBRLCAD_API GEOBRL_EXPORT
#else
#define GEOBRLCAD_API GEOBRL_IMPORT
#endif


/**
 * \brief A place-holder linkage declaration to indicate
 *  that the symbol should not be exported by Windows DLLs.
 * \details For instance, classes that inherit templates from
 *  the STL should not be exported, else it generates multiply
 *  defined symbols.
 */
#define NO_GEOBRLCAD_API

/**
 * \brief Opaque identifier of a mesh.
 * \details Used by the C API.
 */
typedef int GeoMesh;

/**
 * \brief Represents dimension (e.g. 3 for 3d, 4 for 4d ...).
 * \details Used by the C API.
 */
typedef unsigned char geo_coord_index_t;

/*
 * If GARGANTUA is defined, then geogram is compiled
 * with 64 bit indices.
 */
#ifdef GARGANTUA

#include <stdint.h>

/**
 * \brief Represents indices.
 * \details Used by the C API.
 */
typedef uint64_t geo_index_t;

/**
 * \brief Represents possibly negative indices.
 * \details Used by the C API.
 */
typedef int64_t geo_signed_index_t;

#else

/**
 * \brief Represents indices.
 * \details Used by the C API.
 */
typedef unsigned int geo_index_t;

/**
 * \brief Represents possibly negative indices.
 * \details Used by the C API.
 */
typedef int geo_signed_index_t;

#endif

/**
 * \brief Represents floating-point coordinates.
 * \details Used by the C API.
 */
typedef double geo_coord_t;

/**
 * \brief Represents truth values.
 * \details Used by the C API.
 */
typedef int geo_boolean;

/**
 * \brief Thruth values (geo_boolean).
 * \details Used by the C API.
 */
enum {
    GEOBRL_FALSE = 0,
    GEOBRL_TRUE = 1
};

#include "geogram/basic/geogram_options.h"

// iostream should be included before anything else,
// otherwise 'cin', 'cout' and 'cerr' will be uninitialized.
#include <iostream>

/**
 * \file geogram/basic/common.h
 * \brief Common include file, providing basic definitions. Should be
 *  included before anything else by all header files in Vorpaline.
 */


/**
 * \brief Global Vorpaline namespace
 * \details This namespace contains all the Vorpaline classes and functions
 * organized in sub-namespaces.
 */
namespace GEOBRL {

    /**
     * \brief Symbolic constants for GEOBRL::initialize()
     */
    enum {
        /// Do not install error handlers
        GEOBRLCAD_INSTALL_NONE = 0,
        /// Install Geogram's signal handlers
        GEOBRLCAD_INSTALL_HANDLERS = 1,
        /// Sets the locale to POSIX
        GEOBRLCAD_INSTALL_LOCALE = 2,
        /// Reset errno to 0
        GEOBRLCAD_INSTALL_ERRNO = 4,
        /// Enable or disable FPE during initialization
        GEOBRLCAD_INSTALL_FPE = 8,
        /// Enable global citation database
        GEOBRLCAD_INSTALL_BIBLIO = 16,
        /// Install everything
        GEOBRLCAD_INSTALL_ALL = GEOBRLCAD_INSTALL_HANDLERS
        | GEOBRLCAD_INSTALL_LOCALE
        | GEOBRLCAD_INSTALL_ERRNO
        | GEOBRLCAD_INSTALL_FPE
        | GEOBRLCAD_INSTALL_BIBLIO
    };

    /**
     * \brief Initialize Geogram
     * \param[in] flags an or combination of
     *  - GEOBRLCAD_INSTALL_HANDLERS to install geogram error handlers. This avoid
     *  opening dialog boxes under Windows. This is useful for the automatic
     *  test suite. Else continuous integration tests hang because of the dialog
     *  box. Normal users may want to keep the default Windows behavior, since
     *  geogram error handlers may make debugging more difficult under Windows.
     * - GEOBRLCAD_INSTALL_LOCALE to set the locale to POSIX.
     * - GEOBRLCAD_INSTALL_ERRNO to clear the last system error.
     * - GEOBRLCAD_INSTALL_FPE to enable/disable floating point exceptions.
     * - GEOBRLCAD_INSTALL_BIBLIO to enable global citation database.
     * \details This function must be called once at the very beginning of a
     * program to initialize the Vorpaline library. It also installs a exit()
     * handler that calls function terminate() when the program exists
     * normally. If it is called multiple times, then the supplemental calls
     * have no effect.
     */
    void GEOBRLCAD_API initialize(int flags = GEOBRLCAD_INSTALL_NONE,
                               const GeoOptions& opts = GeoOptions());

    /**
     * \brief Cleans up Geogram
     * \details This function is called automatically when the program exists
     * normally.
     * \warning This function should \b not be called directly.
     * \see initialize()
     */
    void GEOBRLCAD_API terminate();
}

/**
 * \def GEOBRL_DEBUG
 * \brief This macro is set when compiling in debug mode
 *
 * \def GEOBRL_PARANOID
 * \brief This macro is set when compiling in debug mode
 *
 * \def GEOBRL_OS_LINUX
 * \brief This macro is set on Linux systems (Android included).
 *
 * \def GEOBRL_OS_UNIX
 * \brief This macro is set on Unix systems (Android included).
 *
 * \def GEOBRL_OS_WINDOWS
 * \brief This macro is set on Windows systems.
 *
 * \def GEOBRL_OS_APPLE
 * \brief This macro is set on Apple systems.
 *
 * \def GEOBRL_OS_ANDROID
 * \brief This macro is set on Android systems (in addition to GEOBRL_OS_LINUX
 * and GEOBRL_OS_UNIX).
 *
 * \def GEOBRL_OS_X11
 * \brief This macro is set on X11 is supported on the current system.
 *
 * \def GEOBRL_ARCH_32
 * \brief This macro is set if the current system is a 32 bits architecture.
 *
 * \def GEOBRL_ARCH_64
 * \brief This macro is set if the current system is a 64 bits architecture.
 *
 * \def GEOBRL_COMPILER_GCC
 * \brief This macro is set if the source code is compiled with GNU's gcc.
 *
 * \def GEOBRL_COMPILER_INTEL
 * \brief This macro is set if the source code is compiled with Intel's icc.
 *
 * \def GEOBRL_COMPILER_MSVC
 * \brief This macro is set if the source code is compiled with Microsoft's
 * Visual C++.
 *
 * \def GEOBRL_NORETURN_DECL
 * \brief Should be inserted before the prototype of a function that does
 *  not return.
 * \details This helps the compiler determining where the execution flow
 *  goes. This is useful for helping the compiler generate some warnings.
 *   Example of a function prototype for a function that does not return
 *   (note the GEOBRL_NORETURN_DECL keyword before and the GEOBRL_NORETURN
 *    keyword after).
 *   \code
 *      GEOBRL_NORETURN_DECL void GEOBRLCAD_API geo_abort() GEOBRL_NORETURN;
 *   \endcode
 *
 * \def GEOBRL_NORETURN
 * \brief Should be inserted after the prototype of a function that does
 *  not return.
 * \details This helps the compiler determining where the execution flow
 *  goes. This is useful for helping the compiler generate some warnings.
 *   Example of a function prototype for a function that does not return
 *   (note the GEOBRL_NORETURN_DECL keyword before and the GEOBRL_NORETURN
 *    keyword after).
 *   \code
 *      GEOBRL_NORETURN_DECL void GEOBRLCAD_API geo_abort() GEOBRL_NORETURN;
 *   \endcode
 *
 * \def GEOBRL_NOEXCEPT
 * \brief Indicates that a function does not throw any exception.
 * \details Should be specified at the end of the function prototype.
 * \code
 *    void GEOBRLCAD_API foobar() GEOBRL_NOEXCEPT;
 * \encode
 */

#if (defined(NDEBUG) || defined(GEOBRLCAD_PSM)) && !defined(GEOBRLCAD_PSM_DEBUG)
#undef GEOBRL_DEBUG
#undef GEOBRL_PARANOID
#else
#define GEOBRL_DEBUG
#define GEOBRL_PARANOID
#endif

// =============================== LINUX defines ===========================

#if defined(__ANDROID__)
#define GEOBRL_OS_ANDROID
#endif

#if defined(__linux__)

#define GEOBRL_OS_LINUX
#define GEOBRL_OS_UNIX

#define GEOBRL_OS_X11


#if defined(__INTEL_COMPILER)
#  define GEOBRL_COMPILER_INTEL
#elif defined(__clang__)
#  define GEOBRL_COMPILER_CLANG
#elif defined(__GNUC__)
#  define GEOBRL_COMPILER_GCC
#else
#  error "Unsupported compiler"
#endif

// The following works on GCC and ICC
#if defined(__x86_64)
#  define GEOBRL_ARCH_64
#  define GEOBRL_PROCESSOR_X86
#else
#  define GEOBRL_ARCH_32
#endif

// =============================== WINDOWS defines =========================

#elif defined(_WIN32) || defined(_WIN64)

#define GEOBRL_OS_WINDOWS
#define GEOBRL_PROCESSOR_X86


#if defined(_MSC_VER)
#  define GEOBRL_COMPILER_MSVC
#elif defined(__MINGW32__) || defined(__MINGW64__)
#  define GEOBRL_COMPILER_MINGW
#endif

#if defined(_WIN64)
#  define GEOBRL_ARCH_64
#else
#  define GEOBRL_ARCH_32
#endif

// =============================== APPLE defines ===========================

#elif defined(__APPLE__)

#define GEOBRL_OS_APPLE
#define GEOBRL_OS_UNIX


#if defined(__clang__)
#  define GEOBRL_COMPILER_CLANG
#elif defined(__GNUC__)
#  define GEOBRL_COMPILER_GCC
#else
#  error "Unsupported compiler"
#endif

#if defined(__x86_64) || defined(__ppc64__) || defined(__arm64__) || defined(__aarch64__) || (defined(__riscv) && __riscv_xlen == 64) || defined(__loongarch_lp64)
#  define GEOBRL_ARCH_64
#else
#  define GEOBRL_ARCH_32
#endif

// =============================== Emscripten defines  ======================

#elif defined(__EMSCRIPTEN__)

#define GEOBRL_OS_UNIX
#define GEOBRL_OS_LINUX
#define GEOBRL_OS_EMSCRIPTEN
#define GEOBRL_ARCH_64
#define GEOBRL_COMPILER_EMSCRIPTEN
#define GEOBRL_COMPILER_CLANG

// =============================== Unsupported =============================
#else
#error "Unsupported operating system"
#endif

#if defined(GEOBRL_COMPILER_GCC)   ||              \
    defined(GEOBRL_COMPILER_CLANG) ||              \
    defined(GEOBRL_COMPILER_MINGW) ||              \
    defined(GEOBRL_COMPILER_EMSCRIPTEN)
#define GEOBRL_COMPILER_GCC_FAMILY
#endif

#ifdef DOXYGEN_ONLY
// Keep doxygen happy
#define GEOBRL_OS_WINDOWS
#define GEOBRL_OS_APPLE
#define GEOBRL_OS_ANDROID
#define GEOBRL_ARCH_32
#define GEOBRL_COMPILER_INTEL
#define GEOBRL_COMPILER_MSVC
#endif

/**
 * \def CPP_CONCAT_(A,B)
 * \brief Helper macro for CPP_CONCAT()
 */
#define CPP_CONCAT_(A, B) A ## B

/**
 * \def CPP_CONCAT(A,B)
 * \brief Creates a new symbol by concatenating its arguments
 */
#define CPP_CONCAT(A, B) CPP_CONCAT_(A, B)

#if defined(GOMGEN)
#define GEOBRL_NORETURN
#elif defined(GEOBRL_COMPILER_GCC_FAMILY) ||       \
    defined(GEOBRL_COMPILER_INTEL)
#define GEOBRL_NORETURN __attribute__((noreturn))
#else
#define GEOBRL_NORETURN
#endif

#if defined(GOMGEN)
#define GEOBRL_NORETURN_DECL
#elif defined(GEOBRL_COMPILER_MSVC)
#define GEOBRL_NORETURN_DECL __declspec(noreturn)
#else
#define GEOBRL_NORETURN_DECL
#endif

#if defined(GEOBRL_COMPILER_CLANG) || defined(GEOBRL_COMPILER_EMSCRIPTEN)
#if __has_feature(cxx_noexcept)
#define GEOBRL_NOEXCEPT noexcept
#endif
#endif

// For Graphite GOM generator (swig is confused by throw() specifier)
#ifdef GOMGEN
#define GEOBRL_NOEXCEPT
#endif

#ifndef GEOBRL_NOEXCEPT
#define GEOBRL_NOEXCEPT throw()
#endif

#define FOR(I,UPPERBND) for(index_t I = 0; I<index_t(UPPERBND); ++I)

// Silence warnings for alloca()
// We use it at different places to allocate objects on the stack
// (for instance, in multi-precision predicates).
#ifdef GEOBRL_COMPILER_CLANG
#pragma GCC diagnostic ignored "-Walloca"
#endif

#endif
