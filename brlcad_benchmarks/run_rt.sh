#!/usr/bin/env bash
# run_rt.sh -- wrapper to invoke rt from a committed benchmark build
#
# Usage: run_rt.sh <build>  [rt-flags...] <db.g> <object>
#   build: "pr_release" or "main_release"
#
# Examples:
#   run_rt.sh pr_release -,1 -s 2048 -o /dev/null /path/to/havoc.g havoc
#   run_rt.sh main_release -s 2048 -o /dev/null /path/to/m35.g all.g
#   RT_FORCE_HLBVH=1 run_rt.sh pr_release -,1 -s 2048 -o /dev/null m35.g all.g

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

BUILD="${1:?Usage: $0 <pr_release|main_release> [rt-args...]}"
shift

BIN_DIR="$REPO_DIR/$BUILD/bin"
LIB_DIR="$REPO_DIR/$BUILD/lib"

if [[ ! -x "$BIN_DIR/rt" ]]; then
    echo "ERROR: $BIN_DIR/rt not found or not executable" >&2
    exit 1
fi

exec env LD_LIBRARY_PATH="$LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$BIN_DIR/rt" "$@"
