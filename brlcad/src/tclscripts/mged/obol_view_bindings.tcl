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
# Usage:
#   obol_view .myview
#   .myview attach [mged_gvp_ptr]
#   obol_view_bind .myview
#
# See RADICAL_MIGRATION.md Stage 6 for context.

proc obol_view_bind {w} {
    # Resize — update renderer dimensions and redraw
    bind $w <Configure> [list obol_view_resize $w %w %h]

    # Mouse press — record starting position; dragging is handled internally
    # by Tk_CreateEventHandler in C, but we reinforce here for platforms
    # where the C event handler may not have caught button-press ordering.
    bind $w <Button-1>        {}
    bind $w <Button-2>        {}
    bind $w <Button-3>        {}
    bind $w <ButtonRelease-1> {}
    bind $w <ButtonRelease-2> {}
    bind $w <ButtonRelease-3> {}

    # Scroll wheel: Button-4 (X11 up) and Button-5 (X11 down) plus
    # <MouseWheel> (macOS / Windows)
    bind $w <Button-4>    [list obol_view_wheel $w  1]
    bind $w <Button-5>    [list obol_view_wheel $w -1]
    bind $w <MouseWheel>  [list obol_view_wheel $w [expr {%D/120}]]

    # Focus so keyboard events are received
    bind $w <Enter>  { focus %W }
}

# Called on <Configure> to notify the widget of a new size
proc obol_view_resize {w width height} {
    if {[info commands $w] eq ""} return
    $w size $width $height
}

# Scroll-wheel zoom: delta > 0 zooms in, < 0 zooms out
proc obol_view_wheel {w delta} {
    if {[info commands $w] eq ""} return
    if {$delta == 0} return
    set factor [expr {$delta > 0 ? 1.1 : 0.9}]
    $w zoom $factor
}
