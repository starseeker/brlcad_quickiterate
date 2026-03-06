#!/usr/bin/env bash
# mged_edit_test.sh — Systematic MGED primitive editing screenshot generator
#
# Usage: ./mged_edit_test.sh <mged_binary> <output_dir> [prefix]
#
# Generates "during-edit" and "after-edit" screenshots for all MGED-editable
# primitives and all parameter-edit operations.  Captures faceplate labels,
# solid-edit illumination and the full edit state so regressions are visible.
#
# Prerequisites: Xvfb running on $DISPLAY, DM_SWRAST=1 support in mged.
#
# Screenshots are named:  <prefix>_<primitive>_<op>_during.png
#                         <prefix>_<primitive>_<op>_after.png
#
# The two-image pairs can be compared across builds with mged_compare.sh.
#
# IMPORTANT: After entering sed mode, a screengrab is always fired first to
# trigger mmenu_display() and initialize ms_top.  Without this initial
# refresh the M command cannot locate menu items ("mouse press outside valid menu").
#
# All parameter values are in local units (mm) matching the default geometry
# from `make <name> <type>` which creates shapes at roughly 500mm scale.

set -euo pipefail

MGED="${1:-mged}"
OUTDIR="${2:-mged_screenshots/sys}"
PREFIX="${3:-upd}"

mkdir -p "$OUTDIR"

# ─── menu_y: compute pen_y for menu item N (0=title, 1=first real item) ──────
# MGED menu layout (menu.h):
#   MENUY=1780, MENU_DY=-104
#   ms_top = MENUY - MENU_DY/2 = 1780 + 52 = 1832
#   mmenu_select iterates items 0,1,2...; for item N:
#     yy = ms_top + (N+1)*MENU_DY = 1832 - 104*(N+1)
#     item N is selected if pen_y > yy  (i.e. pen_y > 1832 - 104*(N+1))
#   reliable midpoint for item N:
#     between yy(N) and yy(N-1)  =  1832 - 104*(N+1)  and  1832 - 104*N
#     midpoint = (1832-104*(N+1) + 1832-104*N) / 2
#              = 1832 - 104*N - 52
#              = 1780 - 104*N
#   item 0 (title): 1780
#   item 1:         1676
#   item 2:         1572
#   item 3:         1468
# All MGED parameter-edit primitives use menu index 0 (MENU_L1).
menu_y() {
    local item="$1"
    echo $(( 1780 - 104 * item ))
}

# ─── two_shot: capture during-edit and after-edit screenshots ─────────────────
# Usage: two_shot <db> <prefix> <prim> <tag> [setup_cmds...] <apply_cmd>
#   All arguments after tag are commands to run inside mged.
#   The LAST argument is the "apply" command (the geometry-changing p command).
#   All preceding arguments (if any) are "setup" commands run after sed+refresh,
#   before the during screenshot.
two_shot() {
    local db="$1" pfx="$2" prim="$3" tag="$4"
    local during="${OUTDIR}/${pfx}_${prim}_${tag}_during.png"
    local after="${OUTDIR}/${pfx}_${prim}_${tag}_after.png"
    shift 4

    # Split: last arg = apply, rest = setup
    local ncmds=$(( $# - 1 ))
    local apply="${@: -1}"
    local setup_str=""
    if (( ncmds > 0 )); then
        local setup_cmds=("${@:1:$ncmds}")
        for c in "${setup_cmds[@]}"; do
            setup_str+="${c}\n"
        done
    fi

    {
        printf "attach swrast\n"
        printf "e %s\n" "$prim"
        printf "ae 35 25\n"
        printf "sed %s\n" "$prim"
        # First screengrab triggers mmenu_display → sets ms_top so M commands work
        printf "screengrab %s\n" "${OUTDIR}/${pfx}_${prim}_${tag}_init.png"
        printf "%b" "$setup_str"
        printf "screengrab %s\n" "$during"
        printf "%s\n" "$apply"
        printf "screengrab %s\n" "$after"
        printf "press accept\n"
    } | DM_SWRAST=1 "$MGED" -c "$db" 2>/dev/null || true
    # Remove the init (pre-setup) screenshot - we only want during and after
    rm -f "${OUTDIR}/${pfx}_${prim}_${tag}_init.png"
}

# ─── menu_shot: one-menu-item two-shot helper ─────────────────────────────────
# Usage: menu_shot <db> <prefix> <prim> <tag> <menu_item_n> <apply_cmd>
#   Selects menu item N, grabs during, applies, grabs after.
menu_shot() {
    local db="$1" pfx="$2" prim="$3" tag="$4"
    local item="$5" apply="$6"
    local my; my=$(menu_y "$item")
    two_shot "$db" "$pfx" "$prim" "$tag" \
        "M 1 -1400 $my" \
        "$apply"
}

# ─── Create a .g file with all test primitives ────────────────────────────────
DB="/tmp/mged_edit_test_$$.g"

printf '%s\n' \
    "make sph1      sph" \
    "make tor1      tor" \
    "make ell1      ell" \
    "make rpc1      rpc" \
    "make rhc1      rhc" \
    "make epa1      epa" \
    "make ehy1      ehy" \
    "make eto1      eto" \
    "make hyp1      hyp" \
    "make superell1 superell" \
    "make part1     part" \
    "make grip1     grip" \
    "make half1     half" \
    "make tgc1      tgc" \
    "make extrude1  extrude" \
    "make revolve1  revolve" \
    "make arb8_1    arb8" \
    "make arb7_1    arb7" \
    "make arb6_1    arb6" \
    "make arb5_1    arb5" \
    "make arb4_1    arb4" \
    "make bot1      bot" \
    "make nmg1      nmg" \
    "make ars1      ars" \
    "make pipe1     pipe" \
    "make metaball1 metaball" \
    "q" \
| DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true

echo "Database created: $DB"
echo "Output directory: $OUTDIR"
echo "Prefix:           $PREFIX"
echo ""

# ─── SPH  (A=B=C=500mm; press sscale then p <scale_factor>) ──────────────────
echo "Testing SPH..."
# sscale uses a scale-factor not an absolute value: p 1.5 = scale by 1.5x
two_shot "$DB" "$PREFIX" "sph1" "sscale" \
    "press sscale" \
    "p 1.5"

# ─── ELL  (A=500, B=250, C=125 — all in mm) ──────────────────────────────────
echo "Testing ELL..."
# ell_menu: title(0) / Set A(1) / Set B(2) / Set C(3) / Set A,B,C(4)
# p <mm>: sets new magnitude of the chosen semi-axis
menu_shot "$DB" "$PREFIX" "ell1" "setA"   1 "p 1000"
menu_shot "$DB" "$PREFIX" "ell1" "setB"   2 "p 500"
menu_shot "$DB" "$PREFIX" "ell1" "setC"   3 "p 250"
menu_shot "$DB" "$PREFIX" "ell1" "setABC" 4 "p 750"

# ─── TOR  (r1=400, r2=100 — mm) ──────────────────────────────────────────────
echo "Testing TOR..."
# tor_menu: title(0) / Set Radius 1(1) / Set Radius 2(2)
# p <mm>: sets new radius value
menu_shot "$DB" "$PREFIX" "tor1" "setR1" 1 "p 800"
menu_shot "$DB" "$PREFIX" "tor1" "setR2" 2 "p 200"

# ─── TGC  (H=1000, A=250, B=125, C=125, D=250 — mm) ─────────────────────────
echo "Testing TGC..."
# tgc_menu: title(0) / Set H(1) / Set H move V(2) / Set H adj CD(3) /
#           Set H move V adj AB(4) / Set A(5) / Set B(6) / Set C(7) / Set D(8) /
#           Set A,B(9) / Set C,D(10) / Set A,B,C,D(11) / Rotate H(12) / Move H(13)
# p <mm>: sets new magnitude
menu_shot "$DB" "$PREFIX" "tgc1" "setH"  1 "p 2000"
menu_shot "$DB" "$PREFIX" "tgc1" "setA"  5 "p 500"
menu_shot "$DB" "$PREFIX" "tgc1" "setB"  6 "p 250"
menu_shot "$DB" "$PREFIX" "tgc1" "setC"  7 "p 250"
menu_shot "$DB" "$PREFIX" "tgc1" "setD"  8 "p 500"

# ─── RPC  (B=500, H=1000, r=250 — mm) ────────────────────────────────────────
echo "Testing RPC..."
# rpc_menu: title(0) / Set B(1) / Set H(2) / Set r(3)
menu_shot "$DB" "$PREFIX" "rpc1" "setB" 1 "p 1000"
menu_shot "$DB" "$PREFIX" "rpc1" "setH" 2 "p 2000"
menu_shot "$DB" "$PREFIX" "rpc1" "setr" 3 "p 500"

# ─── RHC  (B=500, H=500, r=250, c=100 — mm) ──────────────────────────────────
echo "Testing RHC..."
# rhc_menu: title(0) / Set B(1) / Set H(2) / Set r(3) / Set c(4)
menu_shot "$DB" "$PREFIX" "rhc1" "setB" 1 "p 1000"
menu_shot "$DB" "$PREFIX" "rhc1" "setH" 2 "p 1000"
menu_shot "$DB" "$PREFIX" "rhc1" "setr" 3 "p 500"
menu_shot "$DB" "$PREFIX" "rhc1" "setc" 4 "p 200"

# ─── EPA  (H=1000, A=500, B=250 — mm) ────────────────────────────────────────
echo "Testing EPA..."
# epa_menu: title(0) / Set H(1) / Set A(2) / Set B(3)
menu_shot "$DB" "$PREFIX" "epa1" "setH" 1 "p 2000"
menu_shot "$DB" "$PREFIX" "epa1" "setA" 2 "p 1000"
menu_shot "$DB" "$PREFIX" "epa1" "setB" 3 "p 500"

# ─── EHY  (H=1000, A=500, B=250, c=250 — mm) ─────────────────────────────────
echo "Testing EHY..."
# ehy_menu: title(0) / Set H(1) / Set A(2) / Set B(3) / Set c(4)
menu_shot "$DB" "$PREFIX" "ehy1" "setH" 1 "p 2000"
menu_shot "$DB" "$PREFIX" "ehy1" "setA" 2 "p 1000"
menu_shot "$DB" "$PREFIX" "ehy1" "setB" 3 "p 500"
menu_shot "$DB" "$PREFIX" "ehy1" "setc" 4 "p 500"

# ─── ETO  (r=400, d=50, C-mag≈141 — mm) ─────────────────────────────────────
echo "Testing ETO..."
# eto_menu: title(0) / Set r(1) / Set D(2) / Set C(3) / Rotate C(4)
menu_shot "$DB" "$PREFIX" "eto1" "setr" 1 "p 800"
menu_shot "$DB" "$PREFIX" "eto1" "setD" 2 "p 100"
menu_shot "$DB" "$PREFIX" "eto1" "setC" 3 "p 280"

# ─── HYP  (H=1000, A=500, B=250, c=0.4 — mm/ratio) ──────────────────────────
echo "Testing HYP..."
# hyp_menu: title(0) / Set H(1) / Set A(2) / Set B(3) / Set c(4) / Rotate H(5)
menu_shot "$DB" "$PREFIX" "hyp1" "setH" 1 "p 2000"
menu_shot "$DB" "$PREFIX" "hyp1" "setA" 2 "p 1000"
menu_shot "$DB" "$PREFIX" "hyp1" "setB" 3 "p 500"
menu_shot "$DB" "$PREFIX" "hyp1" "setc" 4 "p 0.6"

# ─── SUPERELL  (A=500, B=250, C=125 — mm; n,e dimensionless) ─────────────────
echo "Testing SUPERELL..."
# superell_menu: title(0) / Set A(1) / Set B(2) / Set C(3) / Set A,B,C(4)
# NOTE: no Set n / Set e items in the superell_menu — only geometric scaling
menu_shot "$DB" "$PREFIX" "superell1" "setA"   1 "p 1000"
menu_shot "$DB" "$PREFIX" "superell1" "setB"   2 "p 500"
menu_shot "$DB" "$PREFIX" "superell1" "setC"   3 "p 250"
menu_shot "$DB" "$PREFIX" "superell1" "setABC" 4 "p 750"

# ─── PART  (H=500mm, vrad=250, hrad=125 — mm) ────────────────────────────────
echo "Testing PART..."
# part_menu: title(0) / Set H(1) / Set v(2) / Set h(3)
menu_shot "$DB" "$PREFIX" "part1" "setH" 1 "p 1000"
menu_shot "$DB" "$PREFIX" "part1" "setV" 2 "p 500"
menu_shot "$DB" "$PREFIX" "part1" "setR2" 3 "p 250"

# ─── GRIP  (mag set via sscale) ───────────────────────────────────────────────
echo "Testing GRIP..."
two_shot "$DB" "$PREFIX" "grip1" "sscale" \
    "press sscale" \
    "p 1.5"

# ─── HALF  (has no parameter-edit menu; plane wireframe unchanging with sscale) ─
# HALF has no sub-menus (rt_edit_hlf_menu_item returns NULL).
# `make half1 half` creates N=(0,0,1) d=0 — the halfspace plane through origin.
# The halfspace wireframe has infinite extent; its rendering is independent of
# the d value, so sscale or p commands produce no visible wireframe change.
# Skip HALF here; a manual test with a bounded view showing the half cutting
# through other geometry is needed for meaningful visual regression.
echo "Skipping HALF (halfspace wireframe has no bounded extent to compare)"

# ─── EXTRUDE ──────────────────────────────────────────────────────────────────
# `make extrude1 extrude` creates a placeholder with no sketch reference, so
# sed mode enters VIEWING instead of SOL EDIT — no parameter edits are possible.
# Skip EXTRUDE here; it requires `in extrude1 extrude <sketch> …` with a real
# sketch primitive to enable solid editing.
echo "Skipping EXTRUDE (requires valid sketch reference in object)"

# ─── ARB8: Move Edges (12 edges) ──────────────────────────────────────────────
# The ARB8 main menu (ECMD_ARB_MAIN_MENU) shows 3 items:
#   title(0) / Move Edges(1) / Move Faces(2) / Rotate Faces(3)
# After selecting Move Edges, the edge submenu replaces the main menu:
#   title(0) / E12(1) / E23(2) / E34(3) / E14(4) / E15(5) / E26(6)
#              E56(7) / E67(8) / E78(9) / E58(10) / E37(11) / E48(12) / RETURN(13)
# For EARB edges, p x y z moves the edge endpoint in model space.
# Default arb8 vertices are at ±500mm, so move to a clearly different position.
ARB8_MAIN_MOVE_EDGES=$(menu_y 1)
ARB8_MAIN_MOVE_FACES=$(menu_y 2)
ARB8_MAIN_ROT_FACES=$(menu_y 3)

echo "Testing ARB8 — Move Edges..."
for edge_info in \
    "e12:1:750 -500 -500" \
    "e23:2:500  750 -500" \
    "e34:3:500  500  750" \
    "e14:4:750 -500  500" \
    "e15:5:0 0 0" \
    "e26:6:0 0 0" \
    "e56:7:-500 -500 -750" \
    "e67:8:-500  750 -750" \
    "e78:9:-500  500  750" \
    "e58:10:-750 -500  500" \
    "e37:11:500  750  500" \
    "e48:12:750 -500  500"; do
    tag="${edge_info%%:*}"
    rest="${edge_info#*:}"
    item="${rest%%:*}"
    pval="${rest#*:}"
    EY=$(menu_y "$item")

    during="${OUTDIR}/${PREFIX}_arb8_${tag}_during.png"
    after="${OUTDIR}/${PREFIX}_arb8_${tag}_after.png"
    {
        printf "attach swrast\n"
        printf "e arb8_1\n"
        printf "ae 35 25\n"
        printf "sed arb8_1\n"
        # First screengrab initializes ms_top
        printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_arb8_${tag}_init.png"
        # Select "Move Edges" from main ARB menu
        printf "M 1 -1400 %d\n" "$ARB8_MAIN_MOVE_EDGES"
        # Select specific edge
        printf "M 1 -1400 %d\n" "$EY"
        printf "screengrab %s\n" "$during"
        printf "p %s\n" "$pval"
        printf "screengrab %s\n" "$after"
        printf "press accept\n"
    } | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
    rm -f "${OUTDIR}/${PREFIX}_arb8_${tag}_init.png"
done

echo "Testing ARB8 — Move Faces..."
# mv8_menu: title(0) / Face 1234(1) / Face 5678(2) / Face 1584(3) /
#           Face 2376(4) / Face 1265(5) / Face 4378(6) / RETURN(7)
# p x y z: move plane endpoint
for face_info in \
    "mf1234:1:750 0 0" \
    "mf5678:2:-750 0 0" \
    "mf1584:3:0 -750 0" \
    "mf2376:4:0 750 0" \
    "mf1265:5:0 0 -750" \
    "mf4378:6:0 0 750"; do
    tag="${face_info%%:*}"
    rest="${face_info#*:}"
    item="${rest%%:*}"
    pval="${rest#*:}"
    FY=$(menu_y "$item")

    during="${OUTDIR}/${PREFIX}_arb8_${tag}_during.png"
    after="${OUTDIR}/${PREFIX}_arb8_${tag}_after.png"
    {
        printf "attach swrast\n"
        printf "e arb8_1\n"
        printf "ae 35 25\n"
        printf "sed arb8_1\n"
        printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_arb8_${tag}_init.png"
        printf "M 1 -1400 %d\n" "$ARB8_MAIN_MOVE_FACES"
        printf "M 1 -1400 %d\n" "$FY"
        printf "screengrab %s\n" "$during"
        printf "p %s\n" "$pval"
        printf "screengrab %s\n" "$after"
        printf "press accept\n"
    } | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
    rm -f "${OUTDIR}/${PREFIX}_arb8_${tag}_init.png"
done

echo "Testing ARB8 — Rotate Faces..."
# rot8_menu same layout as mv8_menu, p <angle> sets rotation
for face_info in \
    "rf1234:1:10" \
    "rf5678:2:10" \
    "rf1584:3:10" \
    "rf2376:4:10" \
    "rf1265:5:10" \
    "rf4378:6:10"; do
    tag="${face_info%%:*}"
    rest="${face_info#*:}"
    item="${rest%%:*}"
    pval="${rest#*:}"
    FY=$(menu_y "$item")

    during="${OUTDIR}/${PREFIX}_arb8_${tag}_during.png"
    after="${OUTDIR}/${PREFIX}_arb8_${tag}_after.png"
    {
        printf "attach swrast\n"
        printf "e arb8_1\n"
        printf "ae 35 25\n"
        printf "sed arb8_1\n"
        printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_arb8_${tag}_init.png"
        printf "M 1 -1400 %d\n" "$ARB8_MAIN_ROT_FACES"
        printf "M 1 -1400 %d\n" "$FY"
        printf "screengrab %s\n" "$during"
        printf "p %s\n" "$pval"
        printf "press accept\n"
        printf "screengrab %s\n" "$after"
    } | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
    rm -f "${OUTDIR}/${PREFIX}_arb8_${tag}_init.png"
done

# ─── BOT ──────────────────────────────────────────────────────────────────────
# bot_menu: title(0) / Pick Vertex(1) / Pick Edge(2) / Pick Triangle(3) /
#           Move Vertex(4) / Move Edge(5) / Move Triangle(6)
echo "Testing BOT — Pick/Move Vertex..."
BOT_PICKV=$(menu_y 1)
BOT_MOVEV=$(menu_y 4)
during="${OUTDIR}/${PREFIX}_bot_pickV_during.png"
after="${OUTDIR}/${PREFIX}_bot_pickV_after.png"
{
    printf "attach swrast\ne bot1\nae 35 25\nsed bot1\n"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_bot_pickV_init.png"
    printf "M 1 -1400 %d\n" "$BOT_PICKV"
    printf "screengrab %s\n" "$during"
    printf "M 1 -1400 %d\n" "$BOT_MOVEV"
    printf "p 100 100 100\n"
    printf "screengrab %s\n" "$after"
    printf "press accept\n"
} | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
rm -f "${OUTDIR}/${PREFIX}_bot_pickV_init.png"

echo "Testing BOT — Pick/Move Edge..."
BOT_PICKE=$(menu_y 2)
BOT_MOVEE=$(menu_y 5)
during="${OUTDIR}/${PREFIX}_bot_pickE_during.png"
after="${OUTDIR}/${PREFIX}_bot_pickE_after.png"
{
    printf "attach swrast\ne bot1\nae 35 25\nsed bot1\n"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_bot_pickE_init.png"
    printf "M 1 -1400 %d\n" "$BOT_PICKE"
    printf "screengrab %s\n" "$during"
    printf "M 1 -1400 %d\n" "$BOT_MOVEE"
    printf "p 200 0 0\n"
    printf "screengrab %s\n" "$after"
    printf "press accept\n"
} | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
rm -f "${OUTDIR}/${PREFIX}_bot_pickE_init.png"

echo "Testing BOT — Pick/Move Triangle..."
BOT_PICKT=$(menu_y 3)
BOT_MOVET=$(menu_y 6)
during="${OUTDIR}/${PREFIX}_bot_pickT_during.png"
after="${OUTDIR}/${PREFIX}_bot_pickT_after.png"
{
    printf "attach swrast\ne bot1\nae 35 25\nsed bot1\n"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_bot_pickT_init.png"
    printf "M 1 -1400 %d\n" "$BOT_PICKT"
    printf "screengrab %s\n" "$during"
    printf "M 1 -1400 %d\n" "$BOT_MOVET"
    printf "p 0 0 200\n"
    printf "screengrab %s\n" "$after"
    printf "press accept\n"
} | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
rm -f "${OUTDIR}/${PREFIX}_bot_pickT_init.png"

# ─── NMG ──────────────────────────────────────────────────────────────────────
# nmg_menu: title(0) / Pick Edge(1) / Move Edge(2) / ...
# "Move Edge" and "Move Vertex" require a prior screen-space Pick click to set
# es_eu/es_vpt.  The Pick Edge sequence (item 1 → item 2) works because item 1
# sets ECMD_NMG_EPICK and the next M command acts as the pick; item 2 then moves.
# "Pick Vertex" (item 11 → item 12) needs the same, but for the default NMG cube
# the vertex positions in view space make picking unreliable in -c mode.
# Use sscale for the move-vertex case to guarantee a visible change.
NMG_PICKE=$(menu_y 1)
NMG_MOVEE=$(menu_y 2)

echo "Testing NMG — Pick/Move Edge..."
during="${OUTDIR}/${PREFIX}_nmg_pickE_during.png"
after="${OUTDIR}/${PREFIX}_nmg_pickE_after.png"
{
    printf "attach swrast\ne nmg1\nae 35 25\nsed nmg1\n"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_nmg_pickE_init.png"
    printf "M 1 -1400 %d\n" "$NMG_PICKE"
    printf "screengrab %s\n" "$during"
    printf "M 1 -1400 %d\n" "$NMG_MOVEE"
    printf "p 200 0 0\n"
    printf "screengrab %s\n" "$after"
    printf "press accept\n"
} | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
rm -f "${OUTDIR}/${PREFIX}_nmg_pickE_init.png"

# `make nmg1 nmg` creates an NMG with bounding box ~0.0005mm × 0.0005mm × 0.0005mm
# — essentially invisible at the default ae 35 25 sz=2000mm view.  sscale by 1.5x
# makes no visible difference at this scale.  The nmg_pickE test above already
# verifies menu navigation and solid-edit state transitions for NMG.
echo "Skipping NMG sscale (default NMG from 'make' is microscopic; nmg_pickE covers NMG)"

# ─── ARS ──────────────────────────────────────────────────────────────────────
# ars_menu: title(0) / Pick Vertex(1) / Move Point(2) / ...
#           Move Curve(10) / Move Column(11)
# "Move Curve" and "Move Column" require a prior pick (es_ars_crv >= 0).
# "Pick Vertex" (item 1) opens ars_pick_menu; item 1 in that sub-menu (also
# item 1, y=1676) triggers ECMD_ARS_PICK which selects the nearest vertex.
# The two-M sequence works for ars_pickV because the first M enters pick mode.
ARS_PICKV=$(menu_y 1)
ARS_PICK_SUB=$(menu_y 1)
ARS_MOVEPT=$(menu_y 2)

echo "Testing ARS — Pick/Move Point..."
during="${OUTDIR}/${PREFIX}_ars_pickV_during.png"
after="${OUTDIR}/${PREFIX}_ars_pickV_after.png"
{
    printf "attach swrast\ne ars1\nae 35 25\nsed ars1\n"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_ars_pickV_init.png"
    printf "M 1 -1400 %d\n" "$ARS_PICKV"
    printf "M 1 -1400 %d\n" "$ARS_PICK_SUB"
    printf "screengrab %s\n" "$during"
    printf "M 1 -1400 %d\n" "$ARS_MOVEPT"
    printf "p 200 0 0\n"
    printf "screengrab %s\n" "$after"
    printf "press accept\n"
} | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
rm -f "${OUTDIR}/${PREFIX}_ars_pickV_init.png"

echo "Testing ARS — sscale..."
two_shot "$DB" "$PREFIX" "ars1" "sscale" \
    "press sscale" \
    "p 1.5"

# ─── PIPE ─────────────────────────────────────────────────────────────────────
# pipe_menu: title(0) / Select Point(1) / Next Point(2) / Prev Point(3) /
#            Move Point(4) / ... / Set Pipe OD(11) / Set Pipe ID(12) /
#            Set Pipe Bend(13)
# "Select Point" + subsequent "Move Point" / "Set Point OD" require a prior
# screen-space click to select the pipe point (no p-command equivalent).
# "Set Pipe OD" (item 11) adjusts OD for ALL pipe segments globally without
# a prior select — it is the preferred test.
# Default pipe OD ≈ 200mm; p 400 doubles it for a clear visual change.
PIPE_SCALE_OD=$(menu_y 11)

echo "Testing PIPE — Set Pipe OD..."
{
    printf "attach swrast\ne pipe1\nae 35 25\nsed pipe1\n"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_pipe_scaleOD_init.png"
    printf "M 1 -1400 %d\n" "$PIPE_SCALE_OD"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_pipe_scaleOD_during.png"
    printf "p 400\n"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_pipe_scaleOD_after.png"
    printf "press accept\n"
} | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
rm -f "${OUTDIR}/${PREFIX}_pipe_scaleOD_init.png"

# ─── METABALL ─────────────────────────────────────────────────────────────────
# metaball_menu: title(0) / Set Threshold(1) / Set Render Method(2) /
#                Select Point(3) / ... / Scale Point fldstr(7) / ...
# "Select Point" + "Move Point"/"Scale fldstr" require a prior screen-space
# pick to set the active metaball point.  "Set Threshold" (item 1) works
# globally without any prior selection — it is the preferred test.
# Default threshold = 1.0; p 2.0 doubles it for a clear render-mode change.
MB_THRESHOLD=$(menu_y 1)

echo "Testing METABALL — Set Threshold..."
{
    printf "attach swrast\ne metaball1\nae 35 25\nsed metaball1\n"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_mball_threshold_init.png"
    printf "M 1 -1400 %d\n" "$MB_THRESHOLD"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_mball_threshold_during.png"
    printf "p 2.0\n"
    printf "screengrab %s\n" "${OUTDIR}/${PREFIX}_mball_threshold_after.png"
    printf "press accept\n"
} | DM_SWRAST=1 "$MGED" -c "$DB" 2>/dev/null || true
rm -f "${OUTDIR}/${PREFIX}_mball_threshold_init.png"

# ─── EBM (needs external bitmap data file + `in` command to set file path) ────
# `make ebm1 ebm` creates a placeholder EBM without a valid .bw bitmap path, so
# sed enters VIEWING mode instead of SOL EDIT — press sscale then returns no-op.
# Skip EBM in this automated run; it requires a fully configured EBM primitive
# (e.g. created with `in ebm1 ebm 256 256 1 <path>.bw`) to test meaningfully.
echo "Skipping EBM (requires valid .bw bitmap path in object)"

# ─── Clean up ──────────────────────────────────────────────────────────────────
rm -f "$DB"

# ─── Summary ───────────────────────────────────────────────────────────────────
count=$(ls "$OUTDIR"/${PREFIX}_*_during.png 2>/dev/null | wc -l)
echo ""
echo "Done. Generated ${count} during+after pairs in $OUTDIR with prefix '$PREFIX'."
