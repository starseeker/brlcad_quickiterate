#!/usr/bin/env bash
# mged_edit_test.sh — Systematic MGED primitive editing screenshot generator
#
# Usage: ./mged_edit_test.sh <mged_binary> <output_dir> [prefix]
#
# Generates "during-edit" and "after-edit" screenshots for all MGED-editable
# primitives and all parameter-edit operations.  Captures faceplate labels,
# solid-edit illumination and the full edit state so regressions are visible.
#
# Prerequisites: ImageMagick (convert), Xvfb, DM_SWRAST=1 support in mged.
#
# Screenshots are named:  <prefix>_<primitive>_<op>_during.png
#                         <prefix>_<primitive>_<op>_after.png
#
# The two-image pairs can be compared across builds with mged_compare.sh.

set -euo pipefail

MGED="${1:-mged}"
OUTDIR="${2:-mged_screenshots/sys}"
PREFIX="${3:-upd}"

mkdir -p "$OUTDIR"

# ─── helper: two-shot capture (during-edit + after-edit) ─────────────────────
# two_shot <db> <prefix> <prim> <op_tag> <apply_cmd> [setup_cmd...]
#   setup_cmds : tcl commands that put mged into the desired edit sub-mode
#                (run AFTER 'e <prim>', 'ae 35 25', 'sed <prim>')
#   apply_cmd  : the 'p' command (or similar) that actually modifies geometry
#                (the LAST argument)
#
two_shot() {
    local db="$1"
    local pfx="$2"
    local prim="$3"
    local tag="$4"
    local during="${OUTDIR}/${pfx}_${prim}_${tag}_during.png"
    local after="${OUTDIR}/${pfx}_${prim}_${tag}_after.png"

    shift 4
    # everything up to (but not including) the last arg is setup
    local ncmds=$(( $# - 1 ))
    local apply="${@: -1}"   # last arg
    local setup_cmds=("${@:1:$ncmds}")

    # build setup portion
    local setup_str=""
    for c in "${setup_cmds[@]}"; do
        setup_str+="$c\n"
    done

    printf "%b" \
        "attach swrast\n" \
        "e $prim\n" \
        "ae 35 25\n" \
        "sed $prim\n" \
        "${setup_str}" \
        "screengrab $during\n" \
        "$apply\n" \
        "screengrab $after\n" \
        "press accept\n" \
    | DM_SWRAST=1 "$MGED" -c "$db" 2>/dev/null || true
}

# ─── helper: menu-click (M 1 -1400 <y>) for menu-based primitives ────────────
# menu item i (0=title,1=first real item) maps to pen_y:
#   ms_top = MENUY + 52 = 1832
#   boundary[i] = ms_top + (i+1)*MENU_DY = 1832 - 104*(i+1)
#   use midpoint = boundary[i] + 52
menu_y() {
    local item="$1"   # 0-indexed, 0=title
    echo $(( 1832 - 104 * (item + 1) + 52 ))
}

# ─── Create a .g file with all test primitives ────────────────────────────────
DB="/tmp/mged_edit_test_$$.g"

printf "%b" \
    "make sph1    sph\n" \
    "make tor1    tor\n" \
    "make ell1    ell\n" \
    "make rpc1    rpc\n" \
    "make rhc1    rhc\n" \
    "make epa1    epa\n" \
    "make ehy1    ehy\n" \
    "make eto1    eto\n" \
    "make hyp1    hyp\n" \
    "make superell1 superell\n" \
    "make part1   part\n" \
    "make grip1   grip\n" \
    "make half1   half\n" \
    "make tgc1    tgc\n" \
    "make extrude1 extrude\n" \
    "make revolve1 revolve\n" \
    "make arb8_1  arb8\n" \
    "make arb7_1  arb7\n" \
    "make arb6_1  arb6\n" \
    "make arb5_1  arb5\n" \
    "make arb4_1  arb4\n" \
    "make bot1    bot\n" \
    "make nmg1    nmg\n" \
    "make ars1    ars\n" \
    "make pipe1   pipe\n" \
    "make metaball1 metaball\n" \
    "q\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Database created: $DB"
echo "Output directory: $OUTDIR"
echo "Prefix: $PREFIX"
echo ""

# ─── SPH ─────────────────────────────────────────────────────────────────────
echo "Testing SPH..."
two_shot "$DB" "$PREFIX" "sph1" "sscale" \
    "press sscale" \
    "p 1.5"

# ─── ELL ─────────────────────────────────────────────────────────────────────
echo "Testing ELL..."
two_shot "$DB" "$PREFIX" "ell1" "setA" \
    "press sedit" \
    "p 2.0"

# ─── TOR ─────────────────────────────────────────────────────────────────────
echo "Testing TOR..."
two_shot "$DB" "$PREFIX" "tor1" "setR1" \
    "press sedit" \
    "p 2.5"
two_shot "$DB" "$PREFIX" "tor1" "setR2" \
    "" \
    "p 0.5"

# ─── TGC ─────────────────────────────────────────────────────────────────────
echo "Testing TGC..."
two_shot "$DB" "$PREFIX" "tgc1" "setH" \
    "" \
    "p 0 0 3"
two_shot "$DB" "$PREFIX" "tgc1" "rotH" \
    "" \
    "p 45 0"
two_shot "$DB" "$PREFIX" "tgc1" "setrh" \
    "" \
    "p 1.5"
two_shot "$DB" "$PREFIX" "tgc1" "setRH" \
    "" \
    "p 1.5"

# ─── RPC ─────────────────────────────────────────────────────────────────────
echo "Testing RPC..."
two_shot "$DB" "$PREFIX" "rpc1" "setH" \
    "" \
    "p 0 0 3"
two_shot "$DB" "$PREFIX" "rpc1" "setR" \
    "" \
    "p 1.5"
two_shot "$DB" "$PREFIX" "rpc1" "setB" \
    "" \
    "p 0.5"

# ─── RHC ─────────────────────────────────────────────────────────────────────
echo "Testing RHC..."
two_shot "$DB" "$PREFIX" "rhc1" "setH" \
    "" \
    "p 0 0 3"
two_shot "$DB" "$PREFIX" "rhc1" "setR" \
    "" \
    "p 1.5"
two_shot "$DB" "$PREFIX" "rhc1" "setB" \
    "" \
    "p 0.5"
two_shot "$DB" "$PREFIX" "rhc1" "setC" \
    "" \
    "p 0.75"

# ─── EPA ─────────────────────────────────────────────────────────────────────
echo "Testing EPA..."
two_shot "$DB" "$PREFIX" "epa1" "setH" \
    "" \
    "p 0 0 3"
two_shot "$DB" "$PREFIX" "epa1" "setR1" \
    "" \
    "p 1.5"
two_shot "$DB" "$PREFIX" "epa1" "setR2" \
    "" \
    "p 0.75"

# ─── EHY ─────────────────────────────────────────────────────────────────────
echo "Testing EHY..."
two_shot "$DB" "$PREFIX" "ehy1" "setH" \
    "" \
    "p 0 0 3"
two_shot "$DB" "$PREFIX" "ehy1" "setR1" \
    "" \
    "p 1.5"
two_shot "$DB" "$PREFIX" "ehy1" "setR2" \
    "" \
    "p 0.75"
two_shot "$DB" "$PREFIX" "ehy1" "setC" \
    "" \
    "p 0.4"

# ─── ETO ─────────────────────────────────────────────────────────────────────
echo "Testing ETO..."
two_shot "$DB" "$PREFIX" "eto1" "setR" \
    "" \
    "p 3.0"
two_shot "$DB" "$PREFIX" "eto1" "setrd" \
    "" \
    "p 0.8"
two_shot "$DB" "$PREFIX" "eto1" "setC" \
    "" \
    "p 0 1 0"
two_shot "$DB" "$PREFIX" "eto1" "setD" \
    "" \
    "p 0.5"

# ─── HYP ─────────────────────────────────────────────────────────────────────
echo "Testing HYP..."
two_shot "$DB" "$PREFIX" "hyp1" "setH" \
    "" \
    "p 0 0 3"
two_shot "$DB" "$PREFIX" "hyp1" "rotH" \
    "" \
    "p 30 0"
two_shot "$DB" "$PREFIX" "hyp1" "setRad" \
    "" \
    "p 1.5"

# ─── SUPERELL ────────────────────────────────────────────────────────────────
echo "Testing SUPERELL..."
two_shot "$DB" "$PREFIX" "superell1" "setA" \
    "" \
    "p 2.0"
two_shot "$DB" "$PREFIX" "superell1" "setB" \
    "" \
    "p 1.5"
two_shot "$DB" "$PREFIX" "superell1" "setC" \
    "" \
    "p 1.2"
two_shot "$DB" "$PREFIX" "superell1" "setN" \
    "" \
    "p 0.3"

# ─── PART ────────────────────────────────────────────────────────────────────
echo "Testing PART..."
two_shot "$DB" "$PREFIX" "part1" "setH" \
    "" \
    "p 0 0 3"
two_shot "$DB" "$PREFIX" "part1" "setV" \
    "" \
    "p 1.5"
two_shot "$DB" "$PREFIX" "part1" "setR2" \
    "" \
    "p 0.5"

# ─── GRIP ────────────────────────────────────────────────────────────────────
echo "Testing GRIP..."
two_shot "$DB" "$PREFIX" "grip1" "setMag" \
    "" \
    "p 2.0"

# ─── HALF ────────────────────────────────────────────────────────────────────
echo "Testing HALF..."
two_shot "$DB" "$PREFIX" "half1" "setD" \
    "" \
    "p 1 0 0 2.5"

# ─── EXTRUDE ─────────────────────────────────────────────────────────────────
echo "Testing EXTRUDE..."
two_shot "$DB" "$PREFIX" "extrude1" "setH" \
    "" \
    "p 0 0 4"

# ─── ARB8 — main menu + sub-menus ────────────────────────────────────────────
# ARB8 menu navigation uses M 1 -1400 <y> for menu clicks.
# cntrl_menu items (y values for M command):
ARB8_MOVE_EDGES=$(menu_y 1)   # 1670
ARB8_MOVE_FACES=$(menu_y 2)   # 1570
ARB8_ROT_FACES=$(menu_y 3)    # 1466
# edge8_menu items (same y formula, same positions since menu replaces):
EDGE_12=$(menu_y 1)   # 1670
EDGE_23=$(menu_y 2)
EDGE_34=$(menu_y 3)
EDGE_14=$(menu_y 4)
EDGE_15=$(menu_y 5)
EDGE_26=$(menu_y 6)
EDGE_56=$(menu_y 7)
EDGE_67=$(menu_y 8)
EDGE_78=$(menu_y 9)
EDGE_58=$(menu_y 10)
EDGE_37=$(menu_y 11)
EDGE_48=$(menu_y 12)
# mv8_menu items (Move Faces):
MF_1234=$(menu_y 1)
MF_5678=$(menu_y 2)
MF_1584=$(menu_y 3)
MF_2376=$(menu_y 4)
MF_1265=$(menu_y 5)
MF_4378=$(menu_y 6)
# rot8_menu items (Rotate Faces):
RF_1234=$(menu_y 1)
RF_5678=$(menu_y 2)
RF_1584=$(menu_y 3)
RF_2376=$(menu_y 4)
RF_1265=$(menu_y 5)
RF_4378=$(menu_y 6)

echo "Testing ARB8 — Move Edges..."
# For each edge: open Move Edges submenu, click the edge, do a small translate
for edge_tag in e12 e23 e34 e14 e15 e26 e56 e67 e78 e58 e37 e48; do
    case "$edge_tag" in
        e12) EY=$EDGE_12 ;;
        e23) EY=$EDGE_23 ;;
        e34) EY=$EDGE_34 ;;
        e14) EY=$EDGE_14 ;;
        e15) EY=$EDGE_15 ;;
        e26) EY=$EDGE_26 ;;
        e56) EY=$EDGE_56 ;;
        e67) EY=$EDGE_67 ;;
        e78) EY=$EDGE_78 ;;
        e58) EY=$EDGE_58 ;;
        e37) EY=$EDGE_37 ;;
        e48) EY=$EDGE_48 ;;
    esac

    during="${OUTDIR}/${PREFIX}_arb8_${edge_tag}_during.png"
    after="${OUTDIR}/${PREFIX}_arb8_${edge_tag}_after.png"
    printf "%b" \
        "attach swrast\n" \
        "ae 35 25\n" \
        "e arb8_1\n" \
        "sed arb8_1\n" \
        "M 1 -1400 $ARB8_MOVE_EDGES\n" \
        "M 1 -1400 $EY\n" \
        "screengrab $during\n" \
        "p 0.5 0.5 0\n" \
        "screengrab $after\n" \
        "press accept\n" \
    | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
done

echo "Testing ARB8 — Move Faces..."
for face_tag in f1234 f5678 f1584 f2376 f1265 f4378; do
    case "$face_tag" in
        f1234) FY=$MF_1234 ;;
        f5678) FY=$MF_5678 ;;
        f1584) FY=$MF_1584 ;;
        f2376) FY=$MF_2376 ;;
        f1265) FY=$MF_1265 ;;
        f4378) FY=$MF_4378 ;;
    esac

    during="${OUTDIR}/${PREFIX}_arb8_mf${face_tag}_during.png"
    after="${OUTDIR}/${PREFIX}_arb8_mf${face_tag}_after.png"
    printf "%b" \
        "attach swrast\n" \
        "ae 35 25\n" \
        "e arb8_1\n" \
        "sed arb8_1\n" \
        "M 1 -1400 $ARB8_MOVE_FACES\n" \
        "M 1 -1400 $FY\n" \
        "screengrab $during\n" \
        "p 1.5 1.5 1.5\n" \
        "screengrab $after\n" \
        "press accept\n" \
    | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
done

echo "Testing ARB8 — Rotate Faces..."
for face_tag in f1234 f5678 f1584 f2376 f1265 f4378; do
    case "$face_tag" in
        f1234) FY=$RF_1234 ;;
        f5678) FY=$RF_5678 ;;
        f1584) FY=$RF_1584 ;;
        f2376) FY=$RF_2376 ;;
        f1265) FY=$RF_1265 ;;
        f4378) FY=$RF_4378 ;;
    esac

    during="${OUTDIR}/${PREFIX}_arb8_rf${face_tag}_during.png"
    after="${OUTDIR}/${PREFIX}_arb8_rf${face_tag}_after.png"
    printf "%b" \
        "attach swrast\n" \
        "ae 35 25\n" \
        "e arb8_1\n" \
        "sed arb8_1\n" \
        "M 1 -1400 $ARB8_ROT_FACES\n" \
        "M 1 -1400 $FY\n" \
        "screengrab $during\n" \
        "p 10\n" \
        "press accept\n" \
        "screengrab $after\n" \
    | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
done

# ─── BOT ─────────────────────────────────────────────────────────────────────
# BOT menu items (after title at item 0):
BOT_PICKV=$(menu_y 1)    # Pick Vertex
BOT_PICKE=$(menu_y 2)    # Pick Edge
BOT_PICKT=$(menu_y 3)    # Pick Triangle
BOT_MOVEV=$(menu_y 4)    # Move Vertex
BOT_MOVEE=$(menu_y 5)    # Move Edge
BOT_MOVET=$(menu_y 6)    # Move Triangle

echo "Testing BOT — Pick/Move Vertex..."
during="${OUTDIR}/${PREFIX}_bot_pickV_during.png"
after="${OUTDIR}/${PREFIX}_bot_pickV_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e bot1\n" \
    "sed bot1\n" \
    "M 1 -1400 $BOT_PICKV\n" \
    "screengrab $during\n" \
    "M 1 -1400 $BOT_MOVEV\n" \
    "p 0.5 0.5 0.5\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Testing BOT — Pick/Move Edge..."
during="${OUTDIR}/${PREFIX}_bot_pickE_during.png"
after="${OUTDIR}/${PREFIX}_bot_pickE_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e bot1\n" \
    "sed bot1\n" \
    "M 1 -1400 $BOT_PICKE\n" \
    "screengrab $during\n" \
    "M 1 -1400 $BOT_MOVEE\n" \
    "p 0.5 0 0\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Testing BOT — Pick/Move Triangle..."
during="${OUTDIR}/${PREFIX}_bot_pickT_during.png"
after="${OUTDIR}/${PREFIX}_bot_pickT_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e bot1\n" \
    "sed bot1\n" \
    "M 1 -1400 $BOT_PICKT\n" \
    "screengrab $during\n" \
    "M 1 -1400 $BOT_MOVET\n" \
    "p 0 0 0.5\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

# ─── NMG ─────────────────────────────────────────────────────────────────────
# NMG menu: title + Pick Edge, Move Edge, Split Edge, Delete Edge, Next EU, Prev EU,
#           Radial EU, Extrude Loop, Extrude Loop (dir+dist), Eebug Edge,
#           Pick Vertex, Move Vertex, Pick Face, Move Face
NMG_PICKE=$(menu_y 1)
NMG_MOVEE=$(menu_y 2)
NMG_PICKV=$(menu_y 11)  # item 11
NMG_VMOVE=$(menu_y 12)  # item 12

echo "Testing NMG — Pick/Move Edge..."
during="${OUTDIR}/${PREFIX}_nmg_pickE_during.png"
after="${OUTDIR}/${PREFIX}_nmg_pickE_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e nmg1\n" \
    "sed nmg1\n" \
    "M 1 -1400 $NMG_PICKE\n" \
    "screengrab $during\n" \
    "M 1 -1400 $NMG_MOVEE\n" \
    "p 0.5 0 0\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Testing NMG — Pick/Move Vertex..."
during="${OUTDIR}/${PREFIX}_nmg_pickV_during.png"
after="${OUTDIR}/${PREFIX}_nmg_pickV_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e nmg1\n" \
    "sed nmg1\n" \
    "M 1 -1400 $NMG_PICKV\n" \
    "screengrab $during\n" \
    "M 1 -1400 $NMG_VMOVE\n" \
    "p 0 0.5 0\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

# ─── ARS ─────────────────────────────────────────────────────────────────────
# ARS menu: title + Pick Vertex, Move Point, Delete Curve, Delete Column,
#           Dup Curve, Dup Column, Insert Curve, Scale Curve, Scale Column,
#           Move Curve, Move Column
ARS_PICKV_MENU=$(menu_y 1)   # enters pick sub-menu
# ARS pick sub-menu: title + Pick Vertex, Next Vertex, Prev Vertex, Next Curve, Prev Curve
ARS_PICK_V=$(menu_y 1)
ARS_MOVEPT=$(menu_y 2)       # in main menu
ARS_MOVECURVE=$(menu_y 10)   # in main menu
ARS_MOVECOL=$(menu_y 11)

echo "Testing ARS — Pick/Move Point..."
during="${OUTDIR}/${PREFIX}_ars_pickV_during.png"
after="${OUTDIR}/${PREFIX}_ars_pickV_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e ars1\n" \
    "sed ars1\n" \
    "M 1 -1400 $ARS_PICKV_MENU\n" \
    "M 1 -1400 $ARS_PICK_V\n" \
    "screengrab $during\n" \
    "M 1 -1400 $ARS_MOVEPT\n" \
    "p 0.5 0 0\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Testing ARS — Move Curve..."
during="${OUTDIR}/${PREFIX}_ars_moveCurve_during.png"
after="${OUTDIR}/${PREFIX}_ars_moveCurve_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e ars1\n" \
    "sed ars1\n" \
    "M 1 -1400 $ARS_MOVECURVE\n" \
    "screengrab $during\n" \
    "p 0 0.5 0\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

# ─── PIPE ─────────────────────────────────────────────────────────────────────
# PIPE menu: title + Select Point, Next Point, Previous Point, Move Point,
#            Delete Point, Append Point, Prepend Point, Set Point OD, Set Point ID,
#            Set Point Bend, Set Pipe OD, Set Pipe ID, Set Pipe Bend
PIPE_SELECT=$(menu_y 1)
PIPE_MOVEPT=$(menu_y 4)
PIPE_OD=$(menu_y 8)
PIPE_ID=$(menu_y 9)
PIPE_BEND=$(menu_y 10)
PIPE_SCALE_OD=$(menu_y 11)
PIPE_SCALE_ID=$(menu_y 12)
PIPE_SCALE_BEND=$(menu_y 13)

echo "Testing PIPE — Select/Move Point..."
during="${OUTDIR}/${PREFIX}_pipe_selectPt_during.png"
after="${OUTDIR}/${PREFIX}_pipe_selectPt_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e pipe1\n" \
    "sed pipe1\n" \
    "M 1 -1400 $PIPE_SELECT\n" \
    "screengrab $during\n" \
    "M 1 -1400 $PIPE_MOVEPT\n" \
    "p 0.5 0 0\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Testing PIPE — Set Point OD..."
during="${OUTDIR}/${PREFIX}_pipe_setOD_during.png"
after="${OUTDIR}/${PREFIX}_pipe_setOD_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e pipe1\n" \
    "sed pipe1\n" \
    "M 1 -1400 $PIPE_SELECT\n" \
    "M 1 -1400 $PIPE_OD\n" \
    "screengrab $during\n" \
    "p 0.8\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Testing PIPE — Set Point ID..."
during="${OUTDIR}/${PREFIX}_pipe_setID_during.png"
after="${OUTDIR}/${PREFIX}_pipe_setID_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e pipe1\n" \
    "sed pipe1\n" \
    "M 1 -1400 $PIPE_SELECT\n" \
    "M 1 -1400 $PIPE_ID\n" \
    "screengrab $during\n" \
    "p 0.4\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Testing PIPE — Set Point Bend..."
during="${OUTDIR}/${PREFIX}_pipe_setBend_during.png"
after="${OUTDIR}/${PREFIX}_pipe_setBend_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e pipe1\n" \
    "sed pipe1\n" \
    "M 1 -1400 $PIPE_SELECT\n" \
    "M 1 -1400 $PIPE_BEND\n" \
    "screengrab $during\n" \
    "p 1.2\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Testing PIPE — Scale Pipe OD..."
during="${OUTDIR}/${PREFIX}_pipe_scaleOD_during.png"
after="${OUTDIR}/${PREFIX}_pipe_scaleOD_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e pipe1\n" \
    "sed pipe1\n" \
    "M 1 -1400 $PIPE_SCALE_OD\n" \
    "screengrab $during\n" \
    "p 1.5\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

# ─── METABALL ─────────────────────────────────────────────────────────────────
# METABALL menu: title + Set Threshold, Set Render Method, Select Point,
#               Next Point, Previous Point, Move Point, Scale Point fldstr,
#               Scale Point "goo" value, Set Point sweat value,
#               Delete Point, Add Point
MB_THRESHOLD=$(menu_y 1)
MB_METHOD=$(menu_y 2)
MB_SELECT=$(menu_y 3)
MB_MOVEPT=$(menu_y 6)
MB_FLDSTR=$(menu_y 7)
MB_GOO=$(menu_y 8)

echo "Testing METABALL — Select/Move Point..."
during="${OUTDIR}/${PREFIX}_mball_selectPt_during.png"
after="${OUTDIR}/${PREFIX}_mball_selectPt_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e metaball1\n" \
    "sed metaball1\n" \
    "M 1 -1400 $MB_SELECT\n" \
    "screengrab $during\n" \
    "M 1 -1400 $MB_MOVEPT\n" \
    "p 0 0 0.5\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Testing METABALL — Scale fldstr..."
during="${OUTDIR}/${PREFIX}_mball_fldstr_during.png"
after="${OUTDIR}/${PREFIX}_mball_fldstr_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e metaball1\n" \
    "sed metaball1\n" \
    "M 1 -1400 $MB_SELECT\n" \
    "M 1 -1400 $MB_FLDSTR\n" \
    "screengrab $during\n" \
    "p 1.5\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Testing METABALL — Set Threshold..."
during="${OUTDIR}/${PREFIX}_mball_threshold_during.png"
after="${OUTDIR}/${PREFIX}_mball_threshold_after.png"
printf "%b" \
    "attach swrast\n" \
    "ae 35 25\n" \
    "e metaball1\n" \
    "sed metaball1\n" \
    "M 1 -1400 $MB_THRESHOLD\n" \
    "screengrab $during\n" \
    "p 1.2\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

# ─── EBM (requires external data file) ───────────────────────────────────────
echo "Testing EBM..."
EBM_BW="/tmp/ebm_test_$$.bw"
# Create a 256x256 bitmap
dd if=/dev/zero bs=256 count=256 2>/dev/null > "$EBM_BW"
# Set a checkerboard pattern
python3 -c "
import struct
data = bytearray(256*256)
for y in range(256):
    for x in range(256):
        if (x//32 + y//32) % 2 == 0:
            data[y*256+x] = 255
with open('$EBM_BW','wb') as f:
    f.write(data)
" 2>/dev/null || true

EBM_DB="/tmp/ebm_test_$$.g"
printf "%b" \
    "make ebm1 ebm\n" \
    "q\n" \
| DM_SWRAST=1 "$MGED" -c "$EBM_DB" 2>/dev/null || true

during="${OUTDIR}/${PREFIX}_ebm_setDepth_during.png"
after="${OUTDIR}/${PREFIX}_ebm_setDepth_after.png"
printf "%b" \
    "attach swrast\n" \
    "e ebm1\n" \
    "ae 35 25\n" \
    "sed ebm1\n" \
    "screengrab $during\n" \
    "p 2.5\n" \
    "screengrab $after\n" \
    "press accept\n" \
| DM_SWRAST=1 "$MGED" -c "$EBM_DB" 2>/dev/null || true
rm -f "$EBM_DB" "$EBM_BW"

# ─── Clean up ─────────────────────────────────────────────────────────────────
rm -f "$DB"

# ─── Summary ──────────────────────────────────────────────────────────────────
count=$(ls "$OUTDIR"/${PREFIX}_*.png 2>/dev/null | wc -l)
echo ""
echo "Done. Generated $count screenshots in $OUTDIR with prefix '$PREFIX'."
