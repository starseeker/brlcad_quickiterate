# Copyright (c) 2025 United States Government as represented by
# the U.S. Army Research Laboratory.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following
# disclaimer in the documentation and/or other materials provided
# with the distribution.
#
# 3. The name of the author may not be used to endorse or promote
# products derived from this software without specific prior written
# permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
# OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# ── Language standard ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Hand-written parser stack ─────────────────────────────────────────────────
set(AQSCANNER_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/block_scanner_hand.c"
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

# ── Syntax highlighting via µlight ────────────────────────────────────────────
# µlight (vendor/ulight/) requires C++23 to build; the project itself stays at
# C++17.  The two can coexist in one binary because the interface between them
# is the plain-C API declared in vendor/ulight/include/ulight/ulight.h.
option(ASCIIQUACK_USE_ULIGHT "Enable syntax highlighting via embedded µlight (requires C++23)" ON)

set(_aq_ulight_enabled FALSE)

if(ASCIIQUACK_USE_ULIGHT)
    try_compile(_aq_ulight_cxx23_ok
        "${CMAKE_BINARY_DIR}/asciiquack_ulight_cxx23_check"
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ulight_cxx23_check.cpp"
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    if(_aq_ulight_cxx23_ok)
        set(_aq_ulight_enabled TRUE)
    endif()
endif()

if(_aq_ulight_enabled)
    add_library(asciiquack_ulight STATIC
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
    set_target_properties(asciiquack_ulight PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_include_directories(asciiquack_ulight PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ulight/include"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(asciiquack_ulight PRIVATE -w)
    elseif(MSVC)
        target_compile_options(asciiquack_ulight PRIVATE /w)
    endif()
    SetTargetFolder(asciiquack_ulight "Compilation Utilities")
endif()

function(aq_ulight_configure target)
    if(_aq_ulight_enabled)
        target_link_libraries(${target} PRIVATE asciiquack_ulight)
        target_compile_definitions(${target} PRIVATE ASCIIQUACK_USE_ULIGHT)
    endif()
endfunction()

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(asciiquack
    asciiquack.cpp
    parser.cpp
    vendor/lodepng.cpp
    vendor/minipdf.cpp
)
target_include_directories(asciiquack PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
)
target_compile_definitions(asciiquack PRIVATE
    ASCIIQUACK_NOTO_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fonts"
)
# Silence warnings from asciiquack sources – this is not a BRL-CAD library
# and its vendor code does not conform to BRL-CAD warning settings.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(asciiquack PRIVATE -w)
elseif(MSVC)
    target_compile_options(asciiquack PRIVATE /w)
endif()
aqscanner_configure(asciiquack)
aq_ulight_configure(asciiquack)

SetTargetFolder(asciiquack "Compilation Utilities")

if(COMMAND CMAKEFILES)
    cmakefiles(
        CMakeLists.txt
        asciiquack.cpp
        parser.cpp
        parser.hpp
        block_scanner_hand.c
        block_scanner.h
        inline_scanner.hpp
        attr_list.h
        aqregex.hpp
        document.hpp
        html5.hpp
        manpage.hpp
        pdf.hpp
        docbook5.hpp
        outbuf.hpp
        reader.hpp
        substitutors.hpp
        test_asciiquack.cpp
    )
endif()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
