#!/usr/bin/env bash
# benchmark_docs.sh – Compare DocBook vs AsciiDoc/asciiquack documentation build times.
#
# USAGE:
#   ./benchmark_docs.sh [OPTIONS]
#
# OPTIONS:
#   --build-dir DIR   CMake build directory (default: /tmp/brlcad_bench_build)
#   --out-dir DIR     Scratch directory for tool output (default: /tmp/bench_doc_out)
#   --skip-configure  Skip CMake configure; assumes DIR already configured with
#                     BRLCAD_EXTRADOCS=ON and xsl-expand already built
#   --jobs N          Parallel job count for xargs/cmake (default: nproc)
#   --help            Show this help
#
# WHAT THIS MEASURES
#   DocBook and AsciiDoc man-page generation are timed across three scenarios:
#
#   1. CMake configure  (DocBook only)
#      cmake -DBRLCAD_EXTRADOCS=ON ...
#      AsciiDoc needs no configure step; asciiquack is a self-contained binary.
#
#   2. One-time DocBook setup  (extract bundled XSL stylesheets)
#      cmake --build ... --target xsl-expand
#      This unpacks docbook-xsl-ns.tar.bz2 so xsltproc can import it.
#      Must happen before the first xsltproc call; happens automatically inside
#      a full cmake build.  asciiquack has no equivalent setup cost.
#
#   3. Man-page generation – direct tool invocations  (449 files: man1 + mann)
#      DocBook:   xsltproc -nonet -xinclude STYLESHEET XML  (one process/file)
#      asciiquack: asciiquack -b manpage -d manpage -o OUT ADOC (one process/file)
#      Run sequentially (1 job) and in parallel (-j N).
#
#   4. CMake build – mann pages  (266 MGED command pages)
#      Both pipelines have a grouped CMake target for these files:
#        docbook:   docbook-docbook-system-mann   (generates HTML + MANN)
#        asciidoc:  asciidoc-asciidoc-system-mann  (generates MANN only)
#      NOTE: DocBook builds 2 output formats per source file vs AsciiDoc's 1.
#      For man1 (183 pages) a grouped asciidoc target exists
#      (asciidoc-asciidoc-system-man1) but DocBook man1 pages were defined
#      individually so there is no single docbook-docbook-system-man1 target.
#
#   5. In-process throughput  (bench_asciiquack micro-benchmark)
#      bench_asciiquack converts the full BRL-CAD AsciiDoc corpus in-process
#      (no process-startup overhead) over several rounds.  This gives the
#      maximum sustainable throughput of the asciiquack parser/backend.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
BRLCAD_SRC="$REPO_ROOT/brlcad"
BEXT_OUTPUT="$REPO_ROOT/bext_output"

# ── Defaults ──────────────────────────────────────────────────────────────────
BENCH_BUILD_DIR="/tmp/brlcad_bench_build"
BENCH_OUT_DIR="/tmp/bench_doc_out"
SKIP_CONFIGURE=0
NCPU=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)      BENCH_BUILD_DIR="$2"; shift 2 ;;
        --out-dir)        BENCH_OUT_DIR="$2";   shift 2 ;;
        --skip-configure) SKIP_CONFIGURE=1;     shift   ;;
        --jobs)           NCPU="$2";            shift 2 ;;
        --help|-h)
            sed -n '2,50p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Tool paths ────────────────────────────────────────────────────────────────
AQ_BIN="$BEXT_OUTPUT/noinstall/bin/asciiquack"
XSLTPROC_BIN="$BEXT_OUTPUT/noinstall/bin/xsltproc"
export LD_LIBRARY_PATH="$BEXT_OUTPUT/noinstall/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# ── Helpers ───────────────────────────────────────────────────────────────────
hr()  { printf '%s\n' "$(printf '─%.0s' {1..72})"; }
hr2() { printf '%s\n' "$(printf '═%.0s' {1..72})"; }
epoch_ms() { date +%s%3N; }
elapsed() {   # $1=start_ms → seconds string
    local end; end=$(epoch_ms)
    echo "scale=3; ($end - $1) / 1000" | bc
}
fmt_s() {     # $1=seconds → human-readable
    local s="$1"
    local gt60; gt60=$(echo "$s > 60" | bc -l 2>/dev/null || echo 0)
    if [[ "$gt60" == "1" ]]; then
        local m; m=$(echo "scale=0; $s / 60" | bc)
        local r; r=$(echo "scale=1; $s - ($m * 60)" | bc)
        echo "${m}m ${r}s"
    else
        printf "%.3fs" "$s"
    fi
}
speedup() {   # $1=slow $2=fast → "N.NNx" or "N/A"
    if echo "$2 == 0" | bc -l | grep -q 1; then echo "N/A"; return; fi
    echo "scale=1; $1 / $2" | bc
}
per_file() {  # $1=total_s $2=n_files → ms/file
    echo "scale=1; $1 * 1000 / $2" | bc
}

# ── Prerequisite checks ───────────────────────────────────────────────────────
hr2
echo "  BRL-CAD DocBook vs AsciiDoc/asciiquack Documentation Build Benchmark"
hr2
echo ""

for tool in cmake xargs bc; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: Required tool not found: $tool"; exit 1
    fi
done

# Build asciiquack if needed
if [[ ! -x "$AQ_BIN" ]]; then
    echo "asciiquack not found at $AQ_BIN – building from source..."
    mkdir -p /tmp/aq_build
    cmake -S "$REPO_ROOT/asciiquack" -B /tmp/aq_build \
          -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
    cmake --build /tmp/aq_build -j"$NCPU" >/dev/null 2>&1
    AQ_BIN="/tmp/aq_build/asciiquack"
    echo "  Built: $AQ_BIN"
fi

if [[ ! -x "$XSLTPROC_BIN" ]]; then
    echo "ERROR: xsltproc not found at $XSLTPROC_BIN"; exit 1
fi

AQ_VERSION=$("$AQ_BIN" --version 2>&1 | head -1 || echo "unknown")

echo "Configuration:"
echo "  Repo root  : $REPO_ROOT"
echo "  CMake build: $BENCH_BUILD_DIR"
echo "  Scratch out: $BENCH_OUT_DIR"
echo "  Parallel -j: $NCPU"
echo "  asciiquack : $AQ_BIN  ($AQ_VERSION)"
echo "  xsltproc   : $XSLTPROC_BIN"
echo ""

# ── Source file lists ─────────────────────────────────────────────────────────
DB_MAN1=( "$BRLCAD_SRC"/doc/docbook/system/man1/*.xml )
DB_MANN=( "$BRLCAD_SRC"/doc/docbook/system/mann/*.xml )
DB_ALL=( "${DB_MAN1[@]}" "${DB_MANN[@]}" )

AD_MAN1=( "$BRLCAD_SRC"/doc/asciidoc/system/man1/*.adoc )
AD_MANN=( "$BRLCAD_SRC"/doc/asciidoc/system/mann/*.adoc )
AD_ALL=( "${AD_MAN1[@]}" "${AD_MANN[@]}" )

N_MAN1="${#DB_MAN1[@]}"
N_MANN="${#DB_MANN[@]}"
N_ALL="${#DB_ALL[@]}"

echo "Source corpus:"
printf "  %-8s  %4d XML  %4d AsciiDoc\n" "man1"  "$N_MAN1" "${#AD_MAN1[@]}"
printf "  %-8s  %4d XML  %4d AsciiDoc\n" "mann"  "$N_MANN" "${#AD_MANN[@]}"
printf "  %-8s  %4d XML  %4d AsciiDoc\n" "TOTAL" "$N_ALL"  "${#AD_ALL[@]}"
echo ""

# ── STEP 1: CMake configure (DocBook only) ────────────────────────────────────
hr
echo "  STEP 1 — CMake Configure  (DocBook only; AsciiDoc needs none)"
hr
echo ""

if [[ "$SKIP_CONFIGURE" -eq 0 ]]; then
    echo "  Configuring with BRLCAD_EXTRADOCS=ON ..."
    mkdir -p "$BENCH_BUILD_DIR"
    t0=$(epoch_ms)
    cmake -S "$BRLCAD_SRC" -B "$BENCH_BUILD_DIR" \
          -DBRLCAD_EXT_DIR="$BEXT_OUTPUT" \
          -DBRLCAD_EXTRADOCS=ON \
          -DBRLCAD_ENABLE_STEP=OFF \
          -DBRLCAD_ENABLE_GDAL=OFF \
          -DBRLCAD_ENABLE_QT=OFF \
          >"$BENCH_BUILD_DIR/configure.log" 2>&1
    CONFIGURE_S=$(elapsed "$t0")
    echo "  CMake configure time: $(fmt_s "$CONFIGURE_S")"
else
    echo "  --skip-configure: using existing build dir."
    CONFIGURE_S=0
fi

STYLESHEET="$BENCH_BUILD_DIR/doc/docbook/resources/brlcad/brlcad-man-stylesheet.xsl"
if [[ ! -f "$STYLESHEET" ]]; then
    echo "ERROR: man stylesheet not found after configure: $STYLESHEET"
    exit 1
fi
echo ""

# ── STEP 2: DocBook setup – extract XSL stylesheets ──────────────────────────
hr
echo "  STEP 2 — DocBook Setup  (extract bundled XSL stylesheets)"
echo "           AsciiDoc equivalent: none  (asciiquack is self-contained)"
hr
echo ""

XSL_SENTINEL="$BENCH_BUILD_DIR/doc/docbook/resources/other/standard/xsl/xsl.sentinel"
if [[ ! -f "$XSL_SENTINEL" ]]; then
    echo "  Running cmake --build ... --target xsl-expand ..."
    t0=$(epoch_ms)
    cmake --build "$BENCH_BUILD_DIR" --target xsl-expand -j"$NCPU" \
          >/dev/null 2>&1
    XSL_EXPAND_S=$(elapsed "$t0")
    echo "  xsl-expand time: $(fmt_s "$XSL_EXPAND_S")"
else
    echo "  xsl-expand already done (sentinel present); skipping."
    XSL_EXPAND_S=0
fi
echo ""

# ── STEP 3: Sequential man-page generation ───────────────────────────────────
hr
echo "  STEP 3 — Sequential Man-Page Generation  (1 job, $N_ALL files)"
hr
echo ""

mkdir -p "$BENCH_OUT_DIR/db_seq_man1" "$BENCH_OUT_DIR/db_seq_mann" \
         "$BENCH_OUT_DIR/aq_seq_man1" "$BENCH_OUT_DIR/aq_seq_mann"
rm -f "$BENCH_OUT_DIR"/db_seq_man1/* "$BENCH_OUT_DIR"/db_seq_mann/* \
      "$BENCH_OUT_DIR"/aq_seq_man1/* "$BENCH_OUT_DIR"/aq_seq_mann/* 2>/dev/null || true

# DocBook sequential: xsltproc ignores -o and writes to CWD using the
# document's refname, so we cd to the output dir before calling it.
echo "  DocBook xsltproc (sequential) ..."
t0=$(epoch_ms)
for f in "${DB_MAN1[@]}"; do
    cd "$BENCH_OUT_DIR/db_seq_man1"
    "$XSLTPROC_BIN" -nonet -xinclude "$STYLESHEET" "$f" 2>/dev/null || true
done
for f in "${DB_MANN[@]}"; do
    cd "$BENCH_OUT_DIR/db_seq_mann"
    "$XSLTPROC_BIN" -nonet -xinclude "$STYLESHEET" "$f" 2>/dev/null || true
done
DB_SEQ_S=$(elapsed "$t0")
DB_SEQ_MAN1=$(ls "$BENCH_OUT_DIR/db_seq_man1/" | wc -l)
DB_SEQ_MANN=$(ls "$BENCH_OUT_DIR/db_seq_mann/" | wc -l)
DB_SEQ_TOTAL=$((DB_SEQ_MAN1 + DB_SEQ_MANN))
printf "  DocBook seq : %s  (%d output files)\n" "$(fmt_s "$DB_SEQ_S")" "$DB_SEQ_TOTAL"

# asciiquack sequential
echo "  asciiquack (sequential) ..."
t0=$(epoch_ms)
for f in "${AD_MAN1[@]}"; do
    base=$(basename "$f" .adoc)
    "$AQ_BIN" -b manpage -d manpage \
              -o "$BENCH_OUT_DIR/aq_seq_man1/${base}.1" "$f" 2>/dev/null || true
done
for f in "${AD_MANN[@]}"; do
    base=$(basename "$f" .adoc)
    "$AQ_BIN" -b manpage -d manpage \
              -o "$BENCH_OUT_DIR/aq_seq_mann/${base}.nged" "$f" 2>/dev/null || true
done
AQ_SEQ_S=$(elapsed "$t0")
AQ_SEQ_MAN1=$(ls "$BENCH_OUT_DIR/aq_seq_man1/" | wc -l)
AQ_SEQ_MANN=$(ls "$BENCH_OUT_DIR/aq_seq_mann/" | wc -l)
AQ_SEQ_TOTAL=$((AQ_SEQ_MAN1 + AQ_SEQ_MANN))
printf "  asciiquack  : %s  (%d output files)\n" "$(fmt_s "$AQ_SEQ_S")" "$AQ_SEQ_TOTAL"

SEQ_SPEEDUP=$(speedup "$DB_SEQ_S" "$AQ_SEQ_S")
DB_MS_FILE=$(per_file "$DB_SEQ_S" "$N_ALL")
AQ_MS_FILE=$(per_file "$AQ_SEQ_S" "$N_ALL")
echo ""
echo "  Sequential speedup (asciiquack vs DocBook): ${SEQ_SPEEDUP}×"
echo "  Per-file: DocBook ${DB_MS_FILE} ms/file  |  asciiquack ${AQ_MS_FILE} ms/file"
echo ""

# ── STEP 4: Parallel man-page generation ─────────────────────────────────────
hr
echo "  STEP 4 — Parallel Man-Page Generation  (-j${NCPU}, $N_ALL files)"
hr
echo ""

mkdir -p "$BENCH_OUT_DIR/db_par_man1" "$BENCH_OUT_DIR/db_par_mann" \
         "$BENCH_OUT_DIR/aq_par_man1" "$BENCH_OUT_DIR/aq_par_mann"
rm -f "$BENCH_OUT_DIR"/db_par_man1/* "$BENCH_OUT_DIR"/db_par_mann/* \
      "$BENCH_OUT_DIR"/aq_par_man1/* "$BENCH_OUT_DIR"/aq_par_mann/* 2>/dev/null || true

export XSLTPROC_BIN STYLESHEET AQ_BIN BENCH_OUT_DIR LD_LIBRARY_PATH

echo "  DocBook xsltproc (parallel -j${NCPU}) ..."
t0=$(epoch_ms)
printf '%s\n' "${DB_MAN1[@]}" | \
    xargs -P "$NCPU" -I{} bash -c \
        'cd "$BENCH_OUT_DIR/db_par_man1" &&
         "$XSLTPROC_BIN" -nonet -xinclude "$STYLESHEET" "$1" 2>/dev/null || true' \
        _ {}
printf '%s\n' "${DB_MANN[@]}" | \
    xargs -P "$NCPU" -I{} bash -c \
        'cd "$BENCH_OUT_DIR/db_par_mann" &&
         "$XSLTPROC_BIN" -nonet -xinclude "$STYLESHEET" "$1" 2>/dev/null || true' \
        _ {}
DB_PAR_S=$(elapsed "$t0")
DB_PAR_TOTAL=$(($(ls "$BENCH_OUT_DIR/db_par_man1/" | wc -l) + \
                $(ls "$BENCH_OUT_DIR/db_par_mann/" | wc -l)))
printf "  DocBook par : %s  (%d output files)\n" "$(fmt_s "$DB_PAR_S")" "$DB_PAR_TOTAL"

echo "  asciiquack (parallel -j${NCPU}) ..."
t0=$(epoch_ms)
printf '%s\n' "${AD_MAN1[@]}" | \
    xargs -P "$NCPU" -I{} bash -c \
        'base=$(basename "$1" .adoc)
         "$AQ_BIN" -b manpage -d manpage \
             -o "$BENCH_OUT_DIR/aq_par_man1/${base}.1" "$1" 2>/dev/null || true' \
        _ {}
printf '%s\n' "${AD_MANN[@]}" | \
    xargs -P "$NCPU" -I{} bash -c \
        'base=$(basename "$1" .adoc)
         "$AQ_BIN" -b manpage -d manpage \
             -o "$BENCH_OUT_DIR/aq_par_mann/${base}.nged" "$1" 2>/dev/null || true' \
        _ {}
AQ_PAR_S=$(elapsed "$t0")
AQ_PAR_TOTAL=$(($(ls "$BENCH_OUT_DIR/aq_par_man1/" | wc -l) + \
                $(ls "$BENCH_OUT_DIR/aq_par_mann/" | wc -l)))
printf "  asciiquack  : %s  (%d output files)\n" "$(fmt_s "$AQ_PAR_S")" "$AQ_PAR_TOTAL"

PAR_SPEEDUP=$(speedup "$DB_PAR_S" "$AQ_PAR_S")
echo ""
echo "  Parallel speedup (asciiquack vs DocBook): ${PAR_SPEEDUP}×"
echo ""

# ── STEP 5: CMake build – mann pages (both pipelines) ────────────────────────
hr
echo "  STEP 5 — CMake Build Comparison  (266 mann pages)"
echo "           DocBook target : docbook-docbook-system-mann  (generates HTML + MANN)"
echo "           AsciiDoc target: asciidoc-asciidoc-system-mann (generates MANN only)"
hr
echo ""

# Clean outputs so we measure a full build each time
rm -rf "$BENCH_BUILD_DIR/share/man/mann"   2>/dev/null || true
rm -rf "$BENCH_BUILD_DIR/share/doc/html/mann" 2>/dev/null || true
rm -f  "$BENCH_BUILD_DIR/doc/asciidoc/system/mann"/*.nged 2>/dev/null || true

# Touch sources so cmake considers them dirty
find "$BRLCAD_SRC/doc/docbook/system/mann" -name "*.xml" \
     -exec touch {} \; 2>/dev/null || true
find "$BRLCAD_SRC/doc/asciidoc/system/mann" -name "*.adoc" \
     -exec touch {} \; 2>/dev/null || true

echo "  cmake --build ... --target docbook-docbook-system-mann -j${NCPU} ..."
t0=$(epoch_ms)
cmake --build "$BENCH_BUILD_DIR" \
      --target docbook-docbook-system-mann \
      -j "$NCPU" \
      >"$BENCH_BUILD_DIR/cmake_db_mann.log" 2>&1 || {
    echo "  WARNING: cmake docbook mann build returned non-zero (partial results may be valid)"
}
DB_CMAKE_S=$(elapsed "$t0")
DB_CMAKE_MAN_COUNT=$(find "$BENCH_BUILD_DIR/share/man/mann" \
    -name "*.nged" 2>/dev/null | wc -l)
DB_CMAKE_HTML_COUNT=$(find "$BENCH_BUILD_DIR/share/doc/html/mann" \
    -name "*.html" 2>/dev/null | wc -l)
printf "  DocBook cmake: %s  (%d .nged + %d .html)\n" \
    "$(fmt_s "$DB_CMAKE_S")" "$DB_CMAKE_MAN_COUNT" "$DB_CMAKE_HTML_COUNT"

echo "  cmake --build ... --target asciidoc-asciidoc-system-mann -j${NCPU} ..."
t0=$(epoch_ms)
cmake --build "$BENCH_BUILD_DIR" \
      --target asciidoc-asciidoc-system-mann \
      -j "$NCPU" \
      >"$BENCH_BUILD_DIR/cmake_aq_mann.log" 2>&1 || {
    echo "  WARNING: cmake asciidoc mann build returned non-zero"
}
AQ_CMAKE_S=$(elapsed "$t0")
AQ_CMAKE_COUNT=$(find "$BENCH_BUILD_DIR/doc/asciidoc/system/mann" \
    -name "*.nged" 2>/dev/null | wc -l)
printf "  AsciiDoc cmake: %s  (%d .nged)\n" \
    "$(fmt_s "$AQ_CMAKE_S")" "$AQ_CMAKE_COUNT"

CMAKE_SPEEDUP=$(speedup "$DB_CMAKE_S" "$AQ_CMAKE_S")
echo ""
echo "  cmake speedup (asciidoc vs docbook, -j${NCPU}): ${CMAKE_SPEEDUP}×"
echo "  (DocBook also generates HTML per page; for man-only comparison see Steps 3–4)"
echo ""

# ── STEP 6: asciidoc man1 cmake build ────────────────────────────────────────
hr
echo "  STEP 6 — AsciiDoc CMake Build  (183 man1 pages)"
echo "           asciidoc-asciidoc-system-man1 (MANN only)"
echo "           No equivalent grouped DocBook target exists for man1."
hr
echo ""

rm -f "$BENCH_BUILD_DIR/doc/asciidoc/system/man1"/*.1 2>/dev/null || true
find "$BRLCAD_SRC/doc/asciidoc/system/man1" -name "*.adoc" \
     -exec touch {} \; 2>/dev/null || true

echo "  cmake --build ... --target asciidoc-asciidoc-system-man1 -j${NCPU} ..."
t0=$(epoch_ms)
cmake --build "$BENCH_BUILD_DIR" \
      --target asciidoc-asciidoc-system-man1 \
      -j "$NCPU" \
      >"$BENCH_BUILD_DIR/cmake_aq_man1.log" 2>&1 || {
    echo "  WARNING: cmake asciidoc man1 build returned non-zero"
}
AQ_CMAKE_MAN1_S=$(elapsed "$t0")
AQ_CMAKE_MAN1_COUNT=$(find "$BENCH_BUILD_DIR/doc/asciidoc/system/man1" \
    -name "*.1" 2>/dev/null | wc -l)
printf "  AsciiDoc cmake man1: %s  (%d .1 files)\n" \
    "$(fmt_s "$AQ_CMAKE_MAN1_S")" "$AQ_CMAKE_MAN1_COUNT"
echo ""

# ── STEP 7: In-process benchmark (bench_asciiquack) ──────────────────────────
hr
echo "  STEP 7 — In-Process Throughput  (bench_asciiquack)"
hr
echo ""

BENCH_BIN=""
for candidate in /tmp/aq_build/bench_asciiquack \
                 "$REPO_ROOT/bext_output/noinstall/bin/bench_asciiquack"; do
    [[ -x "$candidate" ]] && { BENCH_BIN="$candidate"; break; }
done

if [[ -z "$BENCH_BIN" ]]; then
    echo "  Building bench_asciiquack from source ..."
    mkdir -p /tmp/aq_build
    cmake -S "$REPO_ROOT/asciiquack" -B /tmp/aq_build \
          -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
    cmake --build /tmp/aq_build --target bench_asciiquack \
          -j"$NCPU" >/dev/null 2>&1
    BENCH_BIN="/tmp/aq_build/bench_asciiquack"
fi

if [[ -x "$BENCH_BIN" ]]; then
    echo "  bench_asciiquack: full BRL-CAD asciidoc corpus (3 rounds) ..."
    echo ""
    # Suppress parser warnings (stderr); show only the timing summary (stdout)
    "$BENCH_BIN" "$BRLCAD_SRC/doc/asciidoc" 3 2>/dev/null
else
    echo "  bench_asciiquack not available; skipping in-process benchmark."
fi
echo ""

# ── SUMMARY ───────────────────────────────────────────────────────────────────
hr2
echo "  BENCHMARK SUMMARY"
hr2
echo ""
echo "  System info:"
echo "    CPUs  : $NCPU cores"
echo "    OS    : $(uname -srm)"
echo "    Date  : $(date '+%Y-%m-%d %H:%M:%S %Z')"
echo ""

COL_W=52
printf "  %-${COL_W}s %12s %12s %10s\n" \
    "Measurement" "DocBook" "asciiquack" "Speedup"
# Draw separator line (brace expansion needs a literal number, not a variable)
printf "  %s %s %s %s\n" \
    "$(printf '─%.0s' {1..52})" \
    "$(printf '─%.0s' {1..12})" \
    "$(printf '─%.0s' {1..12})" \
    "$(printf '─%.0s' {1..10})"

if [[ "$CONFIGURE_S" != "0" ]]; then
    printf "  %-${COL_W}s %12s %12s %10s\n" \
        "cmake configure (EXTRADOCS=ON)" \
        "$(fmt_s "$CONFIGURE_S")" "not needed" "N/A"
fi

if [[ "$XSL_EXPAND_S" != "0" ]]; then
    printf "  %-${COL_W}s %12s %12s %10s\n" \
        "xsl-expand (extract DocBook XSL stylesheets)" \
        "$(fmt_s "$XSL_EXPAND_S")" "not needed" "N/A"
fi

printf "  %-${COL_W}s %12s %12s %10s\n" \
    "Sequential man gen, 1 job ($N_ALL files)" \
    "$(fmt_s "$DB_SEQ_S")" "$(fmt_s "$AQ_SEQ_S")" "${SEQ_SPEEDUP}×"

printf "  %-${COL_W}s %12s %12s %10s\n" \
    "Parallel man gen, -j${NCPU} ($N_ALL files)" \
    "$(fmt_s "$DB_PAR_S")" "$(fmt_s "$AQ_PAR_S")" "${PAR_SPEEDUP}×"

printf "  %-${COL_W}s %12s %12s %10s\n" \
    "Per-file latency (sequential)" \
    "${DB_MS_FILE} ms" "${AQ_MS_FILE} ms" "${SEQ_SPEEDUP}×"

printf "  %-${COL_W}s %12s %12s %10s\n" \
    "cmake mann build, -j${NCPU} (266 files) †" \
    "$(fmt_s "$DB_CMAKE_S")" "$(fmt_s "$AQ_CMAKE_S")" "${CMAKE_SPEEDUP}×"

printf "  %-${COL_W}s %12s %12s %10s\n" \
    "cmake man1 build, -j${NCPU} (183 files) ‡" \
    "  (no target)" "$(fmt_s "$AQ_CMAKE_MAN1_S")" "N/A"

echo ""
echo "  † DocBook cmake mann also generates HTML per page (2 formats total);"
echo "    AsciiDoc cmake mann generates man pages only (1 format)."
echo "  ‡ DocBook man1 has no grouped cmake target (pages defined individually);"
echo "    the direct tool numbers above cover man1 fairly."
echo ""
hr2
echo ""
