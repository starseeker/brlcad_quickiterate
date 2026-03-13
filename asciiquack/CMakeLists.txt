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

# ── Optimisation: promote Release builds to -O3 when the compiler supports it ─
#
# CMake's default CMAKE_CXX_FLAGS_RELEASE is already "-O3 -DNDEBUG" with GCC
# and Clang, so for those compilers this block is a documented no-op that makes
# the intent explicit and visible in the build log.  On compilers where Release
# defaults to a lower level (e.g. MSVC uses /O2) the check silently skips the
# flag so the build stays valid everywhere.
#
# check_cxx_compiler_flag() compiles a small test TU with the candidate flag and
# sets the result variable to TRUE only when the compilation succeeds without
# error.  This is the CMake-recommended way to probe optional compiler flags.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-O3 COMPILER_SUPPORTS_O3)
if(COMPILER_SUPPORTS_O3)
    # Apply only to optimised build types to avoid slowing down Debug builds.
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O3>
        $<$<CONFIG:MinSizeRel>:-O3>
    )
    message(STATUS "  Optimisation : -O3 supported and enabled for Release builds")
else()
    message(STATUS "  Optimisation : -O3 not supported by this compiler; using defaults")
endif()

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
        $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
        -Wcast-align
        $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
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

# ── Hand-written parser stack ─────────────────────────────────────────────────
# block_scanner_hand.c  — single-pass block-line classifier (memchr hot path).
# attr_list.h / block_scanner_hand.c also provides aq_parse_attr_list().
# inline_scanner.hpp    — single-pass inline-quote scanner (included directly).
#
# No external tools (re2c, lemon) or libraries (PCRE2, std::regex) are needed.
# The only runtime dependency is the C standard library (memchr, memcmp, etc.).

set(AQSCANNER_SRCS
    "${CMAKE_SOURCE_DIR}/block_scanner_hand.c"
)
function(aqscanner_configure target)
    target_sources(${target} PRIVATE ${AQSCANNER_SRCS})
    target_compile_definitions(${target} PRIVATE
        ASCIIQUACK_USE_SCANNER
        ASCIIQUACK_SCANNER_PARSER
        ASCIIQUACK_USE_INLINE_SCANNER
        ASCIIQUACK_HAND_BLOCK_SCANNER
    )
endfunction()

message(STATUS "  Parser stack: hand-written (block_scanner_hand.c + inline_scanner.hpp)")
message(STATUS "                no PCRE2, re2c, or lemon required")

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

# ── Image support via bundled lodepng (PNG decoding) ─────────────────────────
message(STATUS "  Image support: using bundled lodepng; PNG and JPEG images enabled in PDF output")

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(asciiquack
    asciiquack.cpp
    parser.cpp
    vendor/lodepng.cpp
    vendor/minipdf.cpp
)
target_include_directories(asciiquack PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(asciiquack PRIVATE ${CMAKE_SOURCE_DIR}/vendor)
target_compile_definitions(asciiquack PRIVATE
    ASCIIQUACK_NOTO_FONTS_DIR="${CMAKE_SOURCE_DIR}/fonts"
)
aqscanner_configure(asciiquack)
ulight_configure(asciiquack)

# ── Tests ─────────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build the C++ test suite" ON)

if(BUILD_TESTS)
    enable_testing()

    add_executable(asciiquack_tests
        test_asciiquack.cpp
        parser.cpp
        vendor/lodepng.cpp
        vendor/minipdf.cpp
    )
    target_include_directories(asciiquack_tests PRIVATE ${CMAKE_SOURCE_DIR})
	 target_include_directories(asciiquack_tests PRIVATE ${CMAKE_SOURCE_DIR}/vendor)
	 target_compile_definitions(asciiquack_tests PRIVATE
        ASCIIQUACK_NOTO_FONTS_DIR="${CMAKE_SOURCE_DIR}/fonts"
        CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    aqscanner_configure(asciiquack_tests)
    ulight_configure(asciiquack_tests)

    add_test(NAME asciiquack_tests COMMAND asciiquack_tests)
endif()

# ── Benchmark ─────────────────────────────────────────────────────────────────
option(BUILD_BENCHMARK "Build the C++ benchmark program" ON)

if(BUILD_BENCHMARK)
    add_executable(bench_asciiquack
        benchmark/bench_asciiquack.cpp
        parser.cpp
        vendor/lodepng.cpp
        vendor/minipdf.cpp
    )
    target_include_directories(bench_asciiquack PRIVATE ${CMAKE_SOURCE_DIR})
	 target_include_directories(bench_asciiquack PRIVATE ${CMAKE_SOURCE_DIR}/vendor)
    aqscanner_configure(bench_asciiquack)
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
message(STATUS "  -O3 enabled  : ${COMPILER_SUPPORTS_O3}")
message(STATUS "  Tests        : ${BUILD_TESTS}")
message(STATUS "  Benchmark    : ${BUILD_BENCHMARK}")
message(STATUS "")
