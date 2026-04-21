#!/usr/bin/env bash
# bench_all.sh -- Reproduce the s2048 HLBVH vs NUBSP benchmark matrix
#
# Columns: main NUBSP | PR auto | PR forced NUBSP | PR forced HLBVH
# Environment: SIZE=2048 RUNS=3 (override as needed)
#
# Run from repo root or this directory:
#   bash brlcad_benchmarks/bench_all.sh

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$SCRIPT_DIR")"

PR_RT="$REPO/brlcad_pr_release/bin/rt"
PR_LIB="$REPO/brlcad_pr_release/lib"
MAIN_RT="$REPO/brlcad_main_release/bin/rt"
MAIN_LIB="$REPO/brlcad_main_release/lib"
DB="$SCRIPT_DIR/db"

SIZE="${SIZE:-2048}"
RUNS="${RUNS:-3}"

# avg_rays <rt> <lib> <g> <obj> [rt-flag ...]
avg_rays() {
    local rt=$1 lib=$2 g=$3 obj=$4; shift 4
    local sum=0 cnt=0
    for i in $(seq 1 "$RUNS"); do
        r=$(LD_LIBRARY_PATH="$lib" "$rt" "$@" -o /dev/null -s "$SIZE" "$g" "$obj" 2>&1 \
            | grep "RTFM" | grep -oP '[\d.]+(?= rays/sec)' || true)
        if [[ -n "$r" ]]; then sum=$(echo "$sum + $r" | bc -l); cnt=$((cnt+1)); fi
    done
    [[ $cnt -gt 0 ]] && printf "%.0f" "$(echo "$sum/$cnt" | bc -l)" || echo "0"
}

avg_forced_hlbvh() {
    local rt=$1 lib=$2 g=$3 obj=$4
    local sum=0 cnt=0
    for i in $(seq 1 "$RUNS"); do
        r=$(RT_FORCE_HLBVH=1 LD_LIBRARY_PATH="$lib" "$rt" -,1 -o /dev/null -s "$SIZE" "$g" "$obj" 2>&1 \
            | grep "RTFM" | grep -oP '[\d.]+(?= rays/sec)' || true)
        if [[ -n "$r" ]]; then sum=$(echo "$sum + $r" | bc -l); cnt=$((cnt+1)); fi
    done
    [[ $cnt -gt 0 ]] && printf "%.0f" "$(echo "$sum/$cnt" | bc -l)" || echo "0"
}

declare -A OBJS SAH
OBJS[sphflake]="scene.r";   SAH[sphflake]="0.0010"
OBJS[havoc]="havoc";        SAH[havoc]="0.0014"
OBJS[m35]="all.g";          SAH[m35]="0.0051"
OBJS[moss]="all.g";         SAH[moss]="0.1310"
OBJS[bldg391]="all.g";      SAH[bldg391]="0.0111"
OBJS[castle]="all.g";       SAH[castle]="0.0099"
OBJS[ktank]="tank";         SAH[ktank]="0.0269"
OBJS[crod]="crod";          SAH[crod]="0.0859"
OBJS[cube]="all.g";         SAH[cube]="0.0088"
OBJS[GenericTwin]="all";    SAH[GenericTwin]="0.2698"

SCENES="sphflake havoc m35 moss bldg391 castle ktank crod cube GenericTwin"

printf "\n=== BRL-CAD RT Benchmark: HLBVH vs NUBSP  (-s%d, %d-run avg) ===\n\n" "$SIZE" "$RUNS"
printf "%-14s %-6s | %-11s | %-11s %-11s %-11s | HLBVH vs NUBSP\n" \
    "Scene" "SAH" "main NUBSP" "PR auto" "PR NUBSP" "PR HLBVH"
printf "%s\n" "----------------------------------------------------------------------"

for name in $SCENES; do
    obj="${OBJS[$name]}"
    sah="${SAH[$name]}"
    gfile="$DB/${name}.g"
    [[ ! -f "$gfile" ]] && { echo "  MISSING: $gfile"; continue; }

    mn=$(avg_rays         "$MAIN_RT" "$MAIN_LIB" "$gfile" "$obj")
    pa=$(avg_rays         "$PR_RT"   "$PR_LIB"   "$gfile" "$obj")
    pn=$(avg_rays         "$PR_RT"   "$PR_LIB"   "$gfile" "$obj" -,0)
    ph=$(avg_forced_hlbvh "$PR_RT"   "$PR_LIB"   "$gfile" "$obj")

    auto_route="HLBVH"
    [[ "$(echo "$sah > 0.003" | bc -l)" = "1" ]] && auto_route="NUBSP"

    if [[ "$pn" != "0" && "$ph" != "0" ]]; then
        delta=$(echo "scale=1; ($ph - $pn) * 100 / $pn" | bc -l)
        winner="HLBVH"; [[ "$(echo "$delta < 0" | bc -l)" = "1" ]] && winner="NUBSP"
        badge="${delta}% (${winner} wins)"
    else
        badge="N/A"
    fi

    printf "%-14s %-6s | %-11s | %-11s %-11s %-11s | %s [auto→%s]\n" \
        "$name" "$sah" "$mn" "$pa" "$pn" "$ph" "$badge" "$auto_route"
done
printf "\n"
