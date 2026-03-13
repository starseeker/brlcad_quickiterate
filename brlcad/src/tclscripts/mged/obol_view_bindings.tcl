# obol_view_bindings.tcl — Default Tk event bindings for obol_view widgets
#
# Source this script after creating an obol_view widget to wire up the
# standard BRL-CAD mouse navigation conventions:
#
#   Left-drag     → rotate (orbit)
#   Middle-drag   → pan
#   Right-drag    → zoom
#   Scroll wheel  → zoom
#   <Configure>   → resize
#
# Both HW GL (GLX) and SW (OSMesa) render paths work identically from Tcl;
# the C-level Tk_CreateEventHandler already handles mouse dragging for both.
# This script only adds scroll wheel, resize, and focus bindings.
#
# Usage:
#   obol_view .myview                         ;# auto-detect HW or SW
#   obol_view .myview -type hw                ;# force HW GL
#   obol_view .myview -type sw                ;# force SW (OSMesa)
#   .myview attach [gvp_ptr]
#   obol_view_bind .myview
#
# See RADICAL_MIGRATION.md Stage 6 for context.

proc obol_view_bind {w} {
    # Resize — update renderer dimensions and redraw.
    # The C event handler also responds to ConfigureNotify, but the Tcl
    # binding provides a belt-and-suspenders path for platforms where the
    # event mask may be filtered.
    bind $w <Configure> [list obol_view_resize $w %w %h]

    # Mouse press / release — handled by the C Tk_CreateEventHandler.
    # Clearing the bindings here prevents Tk's default class bindings
    # from interfering.
    bind $w <Button-1>        {}
    bind $w <Button-2>        {}
    bind $w <Button-3>        {}
    bind $w <ButtonRelease-1> {}
    bind $w <ButtonRelease-2> {}
    bind $w <ButtonRelease-3> {}

    # Scroll wheel: Button-4 (X11 wheel-up) and Button-5 (X11 wheel-down)
    # plus <MouseWheel> (macOS Aqua / Windows).
    bind $w <Button-4>    [list obol_view_wheel $w  1]
    bind $w <Button-5>    [list obol_view_wheel $w -1]
    bind $w <MouseWheel>  [list obol_view_wheel $w [expr {%D/120}]]

    # Focus so keyboard events reach the widget
    bind $w <Enter>  { focus %W }
}

# Called on <Configure> — notifies the widget of a new size.
# For the HW GL path this updates SoRenderManager's viewport.
# For the SW path it resizes the photo image.
proc obol_view_resize {w width height} {
    if {[info commands $w] eq ""} return
    $w size $width $height
}

# Scroll-wheel zoom: delta > 0 zooms in, delta < 0 zooms out.
proc obol_view_wheel {w delta} {
    if {[info commands $w] eq ""} return
    if {$delta == 0} return
    set factor [expr {$delta > 0 ? 1.1 : 0.9}]
    $w zoom $factor
}
