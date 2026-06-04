#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

DB_ROOT="${1:-$REPO_DIR/brlcad_build/share/db}"
RT_BIN="${RT_BIN:-$REPO_DIR/brlcad_build/bin/rt}"
MGED_BIN="${MGED_BIN:-$REPO_DIR/brlcad_build/bin/mged}"
SIZE="${SIZE:-1024}"
OUTPUT_DIR="${OUTPUT_DIR:-$SCRIPT_DIR/output/rt_partition_sweep}"
TIE_PCT="${TIE_PCT:-1.0}"

usage() {
    cat <<USAGE
Usage: $0 [db_root]

Environment overrides:
  RT_BIN=/absolute/path/to/rt
  MGED_BIN=/absolute/path/to/mged
  SIZE=1024
  OUTPUT_DIR=/absolute/path/to/output
  TIE_PCT=1.0   # forced-method performance percent difference treated as tie
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ ! -d "$DB_ROOT" ]]; then
    echo "ERROR: database root not found: $DB_ROOT" >&2
    exit 1
fi
if [[ ! -x "$RT_BIN" ]]; then
    echo "ERROR: rt binary not executable: $RT_BIN" >&2
    exit 1
fi
if [[ ! -x "$MGED_BIN" ]]; then
    echo "ERROR: mged binary not executable: $MGED_BIN" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
OBJ_CSV="$OUTPUT_DIR/object_results.csv"
DB_CSV="$OUTPUT_DIR/database_summary.csv"
MIS_CSV="$OUTPUT_DIR/misclassified.csv"

# object_results columns:
# database,object,auto_selected,auto_rps,nubsp_rps,hlbvh_rps,
# relative_* (each method as % of per-object best),
# should_method (winner of forced NUBSP vs HLBVH, with tie threshold), flag.
printf 'database,object,auto_selected,auto_rps,nubsp_rps,hlbvh_rps,relative_auto,relative_nubsp,relative_hlbvh,should_method,flag\n' > "$OBJ_CSV"
# database_summary columns:
# database,tested_objects,auto_* counts, majority auto mode,
# average RPS for auto/nubsp/hlbvh, best_avg_method, relative_* vs best average, misclassified_count.
printf 'database,tested_objects,auto_hlbvh_count,auto_nubsp_count,auto_other_count,auto_majority,avg_auto_rps,avg_nubsp_rps,avg_hlbvh_rps,best_avg_method,relative_auto,relative_nubsp,relative_hlbvh,misclassified_count\n' > "$DB_CSV"
printf 'database,object,auto_selected,should_method,auto_rps,nubsp_rps,hlbvh_rps\n' > "$MIS_CSV"

run_rt() {
    local mode="$1" db="$2" obj="$3"
    local out method rps

    case "$mode" in
        auto)
            out="$("$RT_BIN" -s"$SIZE" -F/dev/null "$db" "$obj" 2>&1)" || return 1
            ;;
        nubsp)
            out="$("$RT_BIN" -,0 -s"$SIZE" -F/dev/null "$db" "$obj" 2>&1)" || return 1
            ;;
        hlbvh)
            out="$(RT_FORCE_HLBVH=1 "$RT_BIN" -,1 -s"$SIZE" -F/dev/null "$db" "$obj" 2>&1)" || return 1
            ;;
        *)
            return 1
            ;;
    esac

    # rt currently reports either HLBVH: or NUBSP: status lines.
    method="$(printf '%s\n' "$out" | sed -n 's/^\(HLBVH\|NUBSP\):.*/\1/p' | head -n1)"
    rps="$(printf '%s\n' "$out" | sed -n 's/.*= *\([0-9.][0-9.]*\) rays\/sec (RTFM).*/\1/p' | tail -n1)"
    if [[ -z "$rps" ]]; then
        return 1
    fi

    printf '%s|%s\n' "$method" "$rps"
}

fmt_rel() {
    local val="$1" best="$2"
    # Guard invalid best values (should be positive RPS).
    awk -v v="$val" -v b="$best" 'BEGIN{if (b<=0) {printf "0.00%%"; exit}; printf "%.2f%%", (v/b)*100.0}'
}

best_method() {
    local nubsp="$1" hlbvh="$2" tie="$3"
    awk -v n="$nubsp" -v h="$hlbvh" -v t="$tie" 'BEGIN {
        max=(n>h?n:h);
        if (max<=0) {print "UNKNOWN"; exit}
        diff=(h-n); if (diff<0) diff=-diff;
        pct=(diff/max)*100.0;
        if (pct<=t) print "TIE";
        else if (h>n) print "HLBVH";
        else print "NUBSP";
    }'
}

majority_method() {
    local h="$1" n="$2"
    if (( h > n )); then
        echo "HLBVH"
    elif (( n > h )); then
        echo "NUBSP"
    elif (( h == 0 && n == 0 )); then
        echo "NONE"
    else
        echo "TIE"
    fi
}

mapfile -t DBS < <(find "$DB_ROOT" -type f -name '*.g' | sort)

if (( ${#DBS[@]} == 0 )); then
    echo "ERROR: no .g databases found under $DB_ROOT" >&2
    exit 1
fi

echo "Running sweep over ${#DBS[@]} databases from: $DB_ROOT"

for db in "${DBS[@]}"; do
    rel_db="${db#"$DB_ROOT"/}"
    echo "[DB] $rel_db"

    tops_raw="$($MGED_BIN -c "$db" tops -n 2>&1 || true)"
    if [[ -z "${tops_raw//[[:space:]]/}" ]]; then
        echo "  no top-level objects, skipping"
        continue
    fi

    declare -A objset=()
    # BRL-CAD object names are tokenized here as whitespace-delimited names.
    for top in $tops_raw; do
        [[ -z "$top" ]] && continue
        objset["$top"]=1

        lt_out="$($MGED_BIN -c "$db" lt -c, "$top" 2>&1 || true)"
        lt_out="${lt_out//$'\n'/}"
        if [[ -n "$lt_out" ]]; then
            IFS=',' read -r -a kids <<< "$lt_out"
            for kid in "${kids[@]}"; do
                kid="$(printf '%s' "$kid" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
                [[ -z "$kid" ]] && continue
                objset["$kid"]=1
            done
        fi
    done

    mapfile -t objects < <(printf '%s\n' "${!objset[@]}" | sort)
    if (( ${#objects[@]} == 0 )); then
        echo "  no objects to test, skipping"
        continue
    fi

    db_auto_h=0
    db_auto_n=0
    db_auto_o=0
    db_mis=0
    db_sum_auto=0
    db_sum_nubsp=0
    db_sum_hlbvh=0

    for obj in "${objects[@]}"; do
        auto_res="$(run_rt auto "$db" "$obj" || true)"
        nubsp_res="$(run_rt nubsp "$db" "$obj" || true)"
        hlbvh_res="$(run_rt hlbvh "$db" "$obj" || true)"

        if [[ -z "$auto_res" || -z "$nubsp_res" || -z "$hlbvh_res" ]]; then
            echo "  WARN: failed run for object '$obj', skipping"
            continue
        fi

        auto_method="${auto_res%%|*}"
        auto_rps="${auto_res##*|}"
        nubsp_rps="${nubsp_res##*|}"
        hlbvh_rps="${hlbvh_res##*|}"

        # If no auto method marker is emitted, infer from forced-method winner.
        if [[ -z "$auto_method" ]]; then
            auto_method="$(best_method "$nubsp_rps" "$hlbvh_rps" 0)"
            [[ "$auto_method" == "TIE" ]] && auto_method="UNKNOWN"
        fi

        should="$(best_method "$nubsp_rps" "$hlbvh_rps" "$TIE_PCT")"
        best_rps="$(awk -v a="$auto_rps" -v n="$nubsp_rps" -v h="$hlbvh_rps" 'BEGIN{m=a; if (n>m)m=n; if (h>m)m=h; print m}')"

        rel_auto="$(fmt_rel "$auto_rps" "$best_rps")"
        rel_nubsp="$(fmt_rel "$nubsp_rps" "$best_rps")"
        rel_hlbvh="$(fmt_rel "$hlbvh_rps" "$best_rps")"

        flag=""
        if [[ "$should" == "HLBVH" && "$auto_method" != "HLBVH" ]]; then
            flag="AUTO_NUBSP_SHOULD_HLBVH"
        elif [[ "$should" == "NUBSP" && "$auto_method" != "NUBSP" ]]; then
            flag="AUTO_HLBVH_SHOULD_NUBSP"
        elif [[ "$should" == "HLBVH" && "$auto_method" == "UNKNOWN" ]]; then
            flag="AUTO_UNKNOWN_SHOULD_HLBVH"
        elif [[ "$should" == "NUBSP" && "$auto_method" == "UNKNOWN" ]]; then
            flag="AUTO_UNKNOWN_SHOULD_NUBSP"
        fi

        case "$auto_method" in
            HLBVH) ((db_auto_h+=1)) ;;
            NUBSP) ((db_auto_n+=1)) ;;
            *) ((db_auto_o+=1)) ;;
        esac

        db_sum_auto="$(awk -v s="$db_sum_auto" -v v="$auto_rps" 'BEGIN{print s+v}')"
        db_sum_nubsp="$(awk -v s="$db_sum_nubsp" -v v="$nubsp_rps" 'BEGIN{print s+v}')"
        db_sum_hlbvh="$(awk -v s="$db_sum_hlbvh" -v v="$hlbvh_rps" 'BEGIN{print s+v}')"

        if [[ -n "$flag" ]]; then
            ((db_mis+=1))
            printf '%s,%s,%s,%s,%s,%s,%s\n' "$rel_db" "$obj" "$auto_method" "$should" "$auto_rps" "$nubsp_rps" "$hlbvh_rps" >> "$MIS_CSV"
        fi

        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
            "$rel_db" "$obj" "$auto_method" "$auto_rps" "$nubsp_rps" "$hlbvh_rps" \
            "$rel_auto" "$rel_nubsp" "$rel_hlbvh" "$should" "$flag" >> "$OBJ_CSV"
    done

    tested=$((db_auto_h + db_auto_n + db_auto_o))
    if (( tested == 0 )); then
        echo "  no successful object runs"
        continue
    fi

    avg_auto="$(awk -v s="$db_sum_auto" -v c="$tested" 'BEGIN{printf "%.2f", (c>0?s/c:0)}')"
    avg_nubsp="$(awk -v s="$db_sum_nubsp" -v c="$tested" 'BEGIN{printf "%.2f", (c>0?s/c:0)}')"
    avg_hlbvh="$(awk -v s="$db_sum_hlbvh" -v c="$tested" 'BEGIN{printf "%.2f", (c>0?s/c:0)}')"

    best_avg="$(best_method "$avg_nubsp" "$avg_hlbvh" "$TIE_PCT")"
    best_avg_val="$(awk -v n="$avg_nubsp" -v h="$avg_hlbvh" 'BEGIN{print (n>h?n:h)}')"
    rel_avg_auto="$(fmt_rel "$avg_auto" "$best_avg_val")"
    rel_avg_nubsp="$(fmt_rel "$avg_nubsp" "$best_avg_val")"
    rel_avg_hlbvh="$(fmt_rel "$avg_hlbvh" "$best_avg_val")"

    auto_majority="$(majority_method "$db_auto_h" "$db_auto_n")"

    printf '%s,%d,%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%s,%d\n' \
        "$rel_db" "$tested" "$db_auto_h" "$db_auto_n" "$db_auto_o" "$auto_majority" \
        "$avg_auto" "$avg_nubsp" "$avg_hlbvh" "$best_avg" \
        "$rel_avg_auto" "$rel_avg_nubsp" "$rel_avg_hlbvh" "$db_mis" >> "$DB_CSV"
done

echo
printf '%-40s %8s %10s %10s %10s %12s %10s\n' "DATABASE" "OBJS" "AUTO(H)" "AUTO(N)" "AUTO(?)" "BEST(AVG)" "MISCLS"
printf '%s\n' "----------------------------------------------------------------------------------------------------------------"
while IFS=',' read -r database tested auto_h auto_n auto_o auto_majority avg_auto avg_nubsp avg_hlbvh best_avg rel_auto rel_nubsp rel_hlbvh mis; do
    [[ "$database" == "database" ]] && continue
    printf '%-40s %8s %10s %10s %10s %12s %10s\n' "$database" "$tested" "$auto_h" "$auto_n" "$auto_o" "$best_avg" "$mis"
done < "$DB_CSV"

echo
echo "Wrote: $OBJ_CSV"
echo "Wrote: $DB_CSV"
echo "Wrote: $MIS_CSV"
