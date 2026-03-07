cmake_minimum_required(VERSION 3.15)

project(asciiquack
    VERSION 0.1.0
    DESCRIPTION "A C++17 implementation of the AsciiDoc text processor"
    LANGUAGES CXX C
)

# ── Language standard ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )
elseif(MSVC)
    add_compile_options(/W4 /WX /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# ── Regex backend ─────────────────────────────────────────────────────────────
# Three choices (controlled by CMake options):
#
#   USE_PCRE2=ON  + USE_SYSTEM_PCRE2=ON  (default)
#       → Use system libpcre2-8 when found   (JIT enabled, fastest ~5×)
#       → Fall back to embedded vendor subset when not found (~3× faster)
#
#   USE_PCRE2=ON  + USE_SYSTEM_PCRE2=OFF
#       → Force embedded PCRE2 subset (no JIT, no external dep)
#
#   USE_PCRE2=OFF
#       → std::regex (no PCRE2 at all, slowest)
#
# RE2 is not provided as a backend; existing patterns use backreferences and
# lookaheads that RE2 intentionally omits.

option(USE_PCRE2        "Use PCRE2 regex backend (faster than std::regex)"   ON)
option(USE_SYSTEM_PCRE2 "Prefer system libpcre2-8 over embedded vendor copy" ON)

set(AQREGEX_DEFS "")
set(AQREGEX_LIBS "")
set(AQREGEX_INCS "")
set(AQREGEX_SRCS "")

if(USE_PCRE2)
    set(_pcre2_system_found FALSE)

    if(USE_SYSTEM_PCRE2)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(PCRE2 QUIET libpcre2-8)
        endif()
        if(NOT PCRE2_FOUND)
            find_library(PCRE2_LIBRARIES NAMES pcre2-8)
            find_path(PCRE2_INCLUDE_DIRS NAMES pcre2.h)
            if(PCRE2_LIBRARIES AND PCRE2_INCLUDE_DIRS)
                set(PCRE2_FOUND TRUE)
            endif()
        endif()
        if(PCRE2_FOUND)
            set(_pcre2_system_found TRUE)
        endif()
    endif()

    if(_pcre2_system_found)
        # ── System PCRE2: JIT-enabled, fastest ───────────────────────────────
        list(APPEND AQREGEX_DEFS AQREGEX_USE_PCRE2 AQREGEX_PCRE2_SYSTEM)
        list(APPEND AQREGEX_LIBS ${PCRE2_LIBRARIES})
        list(APPEND AQREGEX_INCS ${PCRE2_INCLUDE_DIRS})
        message(STATUS "  Regex backend: system PCRE2 ${PCRE2_VERSION} (JIT enabled, ~5× vs std::regex)")
    else()
        # ── Embedded PCRE2 amalgamation: no JIT, no external dependency ───────
        # A single translation unit instantiates all of the PCRE2 implementation.
        # The header vendor/pcre2_embed.h provides the public API to every TU;
        # vendor/pcre2_impl.c defines PCRE2_EMBED_IMPLEMENTATION and includes it.
        list(APPEND AQREGEX_DEFS AQREGEX_USE_PCRE2)
        list(APPEND AQREGEX_SRCS "${CMAKE_SOURCE_DIR}/vendor/pcre2_impl.c")
        list(APPEND AQREGEX_INCS "${CMAKE_SOURCE_DIR}/vendor")
        if(USE_SYSTEM_PCRE2)
            message(STATUS "  Regex backend: embedded PCRE2 10.42 (system library not found; no JIT, ~3× vs std::regex)")
        else()
            message(STATUS "  Regex backend: embedded PCRE2 10.42 (USE_SYSTEM_PCRE2=OFF; no JIT, ~3× vs std::regex)")
        endif()
    endif()
else()
    message(STATUS "  Regex backend: std::regex (USE_PCRE2=OFF)")
endif()

# ── Helper: apply regex settings to a target ─────────────────────────────────
function(aqregex_configure target)
    if(AQREGEX_DEFS)
        target_compile_definitions(${target} PRIVATE ${AQREGEX_DEFS})
    endif()
    if(AQREGEX_LIBS)
        target_link_libraries(${target} PRIVATE ${AQREGEX_LIBS})
    endif()
    if(AQREGEX_INCS)
        target_include_directories(${target} PRIVATE ${AQREGEX_INCS})
    endif()
    if(AQREGEX_SRCS)
        target_sources(${target} PRIVATE ${AQREGEX_SRCS})
        # Suppress warnings from vendored C code; the amalgamation header
        # handles PCRE2_CODE_UNIT_WIDTH and PCRE2_STATIC internally.
        set_source_files_properties(${AQREGEX_SRCS}
            TARGET_DIRECTORY ${target}
            PROPERTIES COMPILE_FLAGS "-w")
    endif()
endfunction()

# ── Syntax highlighting via µlight ────────────────────────────────────────────
# µlight (vendor/ulight/) requires C++23 to build; the project itself stays at
# C++17.  The two can coexist in one binary because the interface between them
# is the plain-C API declared in vendor/ulight/include/ulight/ulight.h.
#
# Detection uses try_compile rather than compiler-version checks so that it
# works correctly on any compiler (GCC, Clang, MSVC, ICC, …) without
# hard-coding version numbers.
#
# Options:
#   USE_ULIGHT=ON  (default)
#       Try to build the embedded vendor/ulight/ library with C++23.
#       If the compiler does not support C++23, ulight is silently disabled.
#   USE_ULIGHT=OFF
#       Skip syntax highlighting unconditionally.

option(USE_ULIGHT "Enable syntax highlighting via embedded µlight (requires C++23)" ON)

set(_ulight_enabled FALSE)

if(USE_ULIGHT)
    # Probe for the specific C++23 features ulight uses: std::expected,
    # char8_t, consteval, if !consteval, and static lambdas.
    # vendor/ulight_cxx23_check.cpp exercises all of them.
    try_compile(_ulight_cxx23_ok
        "${CMAKE_BINARY_DIR}/ulight_cxx23_check"
        SOURCES "${CMAKE_SOURCE_DIR}/vendor/ulight_cxx23_check.cpp"
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    if(_ulight_cxx23_ok)
        set(_ulight_enabled TRUE)
    else()
        message(STATUS "  Syntax highlighting (ulight): DISABLED "
                       "(compiler does not support required C++23 features)")
    endif()
endif()

if(_ulight_enabled)
    # Build all ulight library TUs as a separate static library compiled at
    # C++23.  The main project (C++17) links against it via the C API only.
    add_library(ulight_vendor STATIC
        vendor/ulight/src/chars.cpp
        vendor/ulight/src/io.cpp
        vendor/ulight/src/parse_utils.cpp
        vendor/ulight/src/ulight.cpp
        vendor/ulight/src/lang/bash.cpp
        vendor/ulight/src/lang/cowel.cpp
        vendor/ulight/src/lang/cpp.cpp
        vendor/ulight/src/lang/css.cpp
        vendor/ulight/src/lang/diff.cpp
        vendor/ulight/src/lang/ebnf.cpp
        vendor/ulight/src/lang/html.cpp
        vendor/ulight/src/lang/js.cpp
        vendor/ulight/src/lang/json.cpp
        vendor/ulight/src/lang/kotlin.cpp
        vendor/ulight/src/lang/llvm.cpp
        vendor/ulight/src/lang/lua.cpp
        vendor/ulight/src/lang/nasm.cpp
        vendor/ulight/src/lang/python.cpp
        vendor/ulight/src/lang/rust.cpp
        vendor/ulight/src/lang/tex.cpp
        vendor/ulight/src/lang/xml.cpp
    )

    set_target_properties(ulight_vendor PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    # PUBLIC so that consumers automatically get the include path and
    # therefore can #include "ulight/ulight.h" without extra setup.
    target_include_directories(ulight_vendor PUBLIC
        "${CMAKE_SOURCE_DIR}/vendor/ulight/include"
    )

    # Silence all warnings from the vendored C++23 code; they are not our
    # responsibility to fix and would clutter the build output.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(ulight_vendor PRIVATE -w)
    elseif(MSVC)
        target_compile_options(ulight_vendor PRIVATE /w)
    endif()

    message(STATUS "  Syntax highlighting: embedded µlight (C++23 TUs, C API)")
else()
    message(STATUS "  Syntax highlighting: disabled")
endif()

# Helper: attach µlight to a target when it is available.
function(ulight_configure target)
    if(_ulight_enabled)
        target_link_libraries(${target} PRIVATE ulight_vendor)
        target_compile_definitions(${target} PRIVATE ASCIIQUACK_USE_ULIGHT)
    endif()
endfunction()

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(asciiquack
    asciiquack.cpp
    minipdf.cpp
    parser.cpp
)
target_include_directories(asciiquack PRIVATE ${CMAKE_SOURCE_DIR})
aqregex_configure(asciiquack)
ulight_configure(asciiquack)

# ── Tests ─────────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build the C++ test suite" ON)

if(BUILD_TESTS)
    enable_testing()

    add_executable(asciiquack_tests
        test_asciiquack.cpp
        minipdf.cpp
        parser.cpp
    )
    target_include_directories(asciiquack_tests PRIVATE ${CMAKE_SOURCE_DIR})
    aqregex_configure(asciiquack_tests)
    ulight_configure(asciiquack_tests)

    add_test(NAME asciiquack_tests COMMAND asciiquack_tests)
endif()

# ── Benchmark ─────────────────────────────────────────────────────────────────
option(BUILD_BENCHMARK "Build the C++ benchmark program" ON)

if(BUILD_BENCHMARK)
    add_executable(bench_asciiquack
        benchmark/bench_asciiquack.cpp
        minipdf.cpp
        parser.cpp
    )
    target_include_directories(bench_asciiquack PRIVATE ${CMAKE_SOURCE_DIR})
    aqregex_configure(bench_asciiquack)
    ulight_configure(bench_asciiquack)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS asciiquack RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "asciiquack ${PROJECT_VERSION}")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard : C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler     : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Tests        : ${BUILD_TESTS}")
message(STATUS "  Benchmark    : ${BUILD_BENCHMARK}")
message(STATUS "")
