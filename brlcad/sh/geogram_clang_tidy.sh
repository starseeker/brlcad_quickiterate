#!/bin/sh
#          G E O G R A M _ C L A N G _ T I D Y . S H
# BRL-CAD
#
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
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
###
#
# Run clang-tidy on BRL-CAD's embedded geogram subset to identify
# dead/unnecessary code.
#
# Usage: sh geogram_clang_tidy.sh <build_dir>
#
# Requires:
#   - clang-tidy in PATH
#   - A CMake build directory configured with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
#   - python3 in PATH
#
# Checks enabled:
#   readability-unused-member-function  - member functions never called
#   misc-unused-parameters              - function parameters never used
#   misc-unused-alias-decls             - using/typedef aliases never referenced
#
###

if test $# -lt 1 ; then
    echo "Usage: $0 <cmake_build_dir>"
    exit 1
fi

BUILD_DIR="$1"
COMPILE_DB="$BUILD_DIR/compile_commands.json"

if test ! -f "$COMPILE_DB" ; then
    echo "ERROR: compile_commands.json not found in $BUILD_DIR"
    echo "Reconfigure cmake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    exit 1
fi

if ! command -v clang-tidy > /dev/null 2>&1 ; then
    echo "ERROR: clang-tidy not found in PATH"
    exit 1
fi

if ! command -v python3 > /dev/null 2>&1 ; then
    echo "ERROR: python3 not found in PATH"
    exit 1
fi

# Locate the geogram source directory relative to this script
SCRIPT_DIR=`dirname "$0"`
GEOGRAM_DIR=`cd "$SCRIPT_DIR/../src/libbg/geogram" && pwd`

if test ! -d "$GEOGRAM_DIR" ; then
    echo "ERROR: geogram source directory not found at $GEOGRAM_DIR"
    exit 1
fi

echo "Geogram source: $GEOGRAM_DIR"
echo "Build dir:      $BUILD_DIR"
echo ""

# Create a temporary filtered compile_commands.json removing GCC-only flags
# that clang-tidy does not recognise (e.g. -fipa-pta).
TMPDIR_CT=`mktemp -d`
FILTERED_DB="$TMPDIR_CT/compile_commands.json"

python3 - "$COMPILE_DB" "$FILTERED_DB" << 'PYEOF'
import json, sys, re

gcc_only_flags = [
    '-fipa-pta',
    '-fvariable-expansion-in-unroller',
    '-ftracer',
    '-fgcse-sm',
    '-fgcse-las',
    '-floop-interchange',
    '-floop-unroll-and-jam',
    '-fno-printf-return-value',
    '-fno-sched-dep-count-heuristic',
]

with open(sys.argv[1]) as f:
    cmds = json.load(f)

filtered = []
for entry in cmds:
    if '/geogram/' not in entry['file']:
        continue
    if not entry['file'].endswith(('.cpp', '.c')):
        continue
    new_entry = dict(entry)
    cmd = new_entry['command']
    for flag in gcc_only_flags:
        cmd = cmd.replace(' ' + flag, '')
    new_entry['command'] = cmd
    filtered.append(new_entry)

with open(sys.argv[2], 'w') as f:
    json.dump(filtered, f, indent=2)
print(f"Filtered {len(filtered)} geogram compilation units")
PYEOF

if test $? -ne 0 ; then
    echo "ERROR: failed to generate filtered compile_commands.json"
    rm -rf "$TMPDIR_CT"
    exit 1
fi

# Gather the list of geogram C++ source files (not NL C files, not third_party)
CPP_FILES=`python3 -c "
import json
with open('$FILTERED_DB') as f:
    cmds = json.load(f)
files = [e['file'] for e in cmds if e['file'].endswith('.cpp')]
print(' '.join(files))
"`

echo "Running clang-tidy (readability-unused-member-function, misc-unused-parameters,"
echo "                    misc-unused-alias-decls) ..."
echo ""

clang-tidy \
    -p "$TMPDIR_CT" \
    -checks='-*,readability-unused-member-function,misc-unused-parameters,misc-unused-alias-decls' \
    --header-filter="$GEOGRAM_DIR/.*" \
    $CPP_FILES

STATUS=$?

rm -rf "$TMPDIR_CT"
exit $STATUS

# Local Variables:
# mode: sh
# tab-width: 8
# sh-indentation: 4
# sh-basic-offset: 4
# indent-tabs-mode: t
# End:
# ex: shiftwidth=4 tabstop=8
