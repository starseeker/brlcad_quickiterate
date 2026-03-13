This branch contains a fork of brlcad making extensive changes to its drawing
layer and related codes in order to modernize its graphics display.  It's starting
point is an attempt to make a BRL-CAD "scene graph" library (libbsg) to act as
the proxy for an Open Inventor style API, but now we want to try something more
radical.

Obol, our fork of the Coin scene graph library, is the eventual target for 3D
graphics display.  Rather than continuing to flesh out libbsg, let's try removing
it in favor of directly using the Obol API in the BRL-CAD code.  You many need to
set some C API for Obol, depending on the situation, but you may move code from C
to C++ in BRL-CAD proper.

This is intended to be an invasive, large scale, radical experiment to effect a
major reshaping of the BRL-CAD display code.  If you need to checkpoint intermediate
non working states that is acceptable, and preferable to creating hybrid states in
an attempt to "keep things working" - the latter is likely to vastly complicate debugging
and problem resolution.

Our goal is to preserve user-facing features - the libged commands, graphical displays
in mged, archer, qged, and rtwizard, and the various command line rendering tests and
image generation options.  Beyond that anything goes - you may reengineer any library
or drawing code necessary to move BRL-CAD's 3D scene visualization to expressing an
idiomatic, properly utilized Obol scene graph.  (The drawing code, for example, may
be altered to directly create hierarchies from geometry in Obol scenes.)

Make a RADICAL_MIGRATION.md plan to guide the work - it will undoubtely take many
stages, and that's fine.  Hopefully the preliminary work done on the qged branch,
which is our staring point here, will be a better foundation than the original
flat list BRL-CAD traditionally used for geometry drawing, but you many rework as
needed to achieve a well designed, well architected, high quality result.
