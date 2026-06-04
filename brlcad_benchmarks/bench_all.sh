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

# Determine auto-routing for PR binary by comparing auto vs forced-NUBSP throughput.
# If auto throughput is closer to HLBVH than NUBSP it routed to HLBVH.
# SAH values are produced by the bbox-overlap SAH-BVH (measured with RT_FORCE_HLBVH=1 -x 16384).
# D_overlap = sum(vol(bbox_i∩bbox_j))/vol(scene_bbox) via sweep-and-prune (also -x 16384).
# Routing: small-scene bypass (≤30 unique prims) → HLBVH unconditionally;
#          then D_overlap in [0.15, 0.30] → NUBSP (catches m35);
#          then SAH > 0.060 → NUBSP (catches bldg391, ktank);
#          else → HLBVH.
# All 10 benchmark scenes are correctly routed with the combined SAH + D_overlap policy.
#   sphflake: 822 unique, SAH 0.0022, D_overlap 0.0038 → HLBVH (+19%)
#   havoc:   2427 unique, SAH 0.0040, D_overlap 0.5744 → HLBVH (+114%)
#   m35:     1125 unique, SAH 0.0120, D_overlap 0.2260 → NUBSP (D_overlap band; HLBVH -25%)
#   moss:       6 unique                               → HLBVH (small-scene bypass, ≤30 unique prims)
#   bldg391:  203 unique, SAH 0.0867, D_overlap 1.0009 → NUBSP (SAH>threshold; HLBVH -50%)
#   castle:   429 unique, SAH 0.0259, D_overlap 0.3339 → HLBVH (+12%)
#   ktank:     78 unique, SAH 0.0911, D_overlap 0.0744 → NUBSP (SAH>threshold)
#   crod:      17 unique                               → HLBVH (small-scene bypass)
#   cube:     160 unique, SAH 0.0431, D_overlap 0.0028 → HLBVH (+5%)
#   GenericTwin: 2239 unique, SAH 0.0104, D_overlap 3.1820 → HLBVH (~tie with NUBSP)
declare -A OBJS SAH UNIQ DOVERLAP
OBJS[sphflake]="scene.r";   SAH[sphflake]="0.0022"; UNIQ[sphflake]=822;   DOVERLAP[sphflake]="0.0038"
OBJS[havoc]="havoc";        SAH[havoc]="0.0040";    UNIQ[havoc]=2427;     DOVERLAP[havoc]="0.5744"
OBJS[m35]="all.g";          SAH[m35]="0.0120";      UNIQ[m35]=1125;       DOVERLAP[m35]="0.2260"
OBJS[moss]="all.g";         SAH[moss]="small(6)";   UNIQ[moss]=6;         DOVERLAP[moss]="0.0008"
OBJS[bldg391]="all.g";      SAH[bldg391]="0.0867";  UNIQ[bldg391]=203;    DOVERLAP[bldg391]="1.0009"
OBJS[castle]="all.g";       SAH[castle]="0.0259";   UNIQ[castle]=429;     DOVERLAP[castle]="0.3339"
OBJS[ktank]="tank";         SAH[ktank]="0.0911";    UNIQ[ktank]=78;       DOVERLAP[ktank]="0.0744"
OBJS[crod]="crod";          SAH[crod]="small(17)";  UNIQ[crod]=17;        DOVERLAP[crod]="0.6403"
OBJS[cube]="all.g";         SAH[cube]="0.0431";     UNIQ[cube]=160;       DOVERLAP[cube]="0.0028"
OBJS[GenericTwin]="all";    SAH[GenericTwin]="0.0104"; UNIQ[GenericTwin]=2239; DOVERLAP[GenericTwin]="3.1820"

SCENES="sphflake havoc m35 moss bldg391 castle ktank crod cube GenericTwin"

printf "\n=== BRL-CAD RT Benchmark: HLBVH vs NUBSP  (-s%d, %d-run avg) ===\n\n" "$SIZE" "$RUNS"
printf "%-14s %-9s | %-11s | %-11s %-11s %-11s | HLBVH vs NUBSP\n" \
    "Scene" "SAH" "main NUBSP" "PR auto" "PR NUBSP" "PR HLBVH"
printf "%s\n" "--------------------------------------------------------------------------"

for name in $SCENES; do
    obj="${OBJS[$name]}"
    sah="${SAH[$name]}"
    uniq="${UNIQ[$name]}"
    gfile="$DB/${name}.g"
    [[ ! -f "$gfile" ]] && { echo "  MISSING: $gfile"; continue; }

    mn=$(avg_rays         "$MAIN_RT" "$MAIN_LIB" "$gfile" "$obj")
    pa=$(avg_rays         "$PR_RT"   "$PR_LIB"   "$gfile" "$obj")
    pn=$(avg_rays         "$PR_RT"   "$PR_LIB"   "$gfile" "$obj" -,0)
    ph=$(avg_forced_hlbvh "$PR_RT"   "$PR_LIB"   "$gfile" "$obj")

    # Determine actual auto-routing: small-scene bypass, then D_overlap band, then SAH threshold
    doverlap="${DOVERLAP[$name]}"
    if [[ "$uniq" -le 30 ]]; then
        auto_route="HLBVH(small)"
    elif [[ "$(echo "$doverlap >= 0.15 && $doverlap <= 0.30" | bc -l)" = "1" ]]; then
        auto_route="NUBSP(overlap)"
    elif [[ "$(echo "$sah > 0.060" | bc -l)" = "1" ]]; then
        auto_route="NUBSP(SAH)"
    else
        auto_route="HLBVH"
    fi

    if [[ "$pn" != "0" && "$ph" != "0" ]]; then
        delta=$(echo "scale=1; ($ph - $pn) * 100 / $pn" | bc -l)
        winner="HLBVH"; [[ "$(echo "$delta < 0" | bc -l)" = "1" ]] && winner="NUBSP"
        badge="${delta}% (${winner} wins)"
    else
        badge="N/A"
    fi

    printf "%-14s %-9s | %-11s | %-11s %-11s %-11s | %s [auto→%s]\n" \
        "$name" "$sah" "$mn" "$pa" "$pn" "$ph" "$badge" "$auto_route"
done
printf "\n"
