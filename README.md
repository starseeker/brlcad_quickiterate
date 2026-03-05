This branch is intended to evolve the libbv bview API and related APIs to a form
that makes them a better match for the Obol Inventor-style scene APIs.

Background:  BRL-CAD uses a very old libdm/libfb layer for 3D scene management
with limited capabilities - it has long hindered our graphics progress.  We are
working on a fork of Coin3d's Coin library called Obol designed for our purposes -
it is present in this repository in the obol directory for reference.  With some
variations, it is an Open Inventor style API - it provides much richer 3D capabilities
than BRL-CAD currently has access to.

BRL-CAD's current code uses bview and bv_scene_obj structures that have evolved in
an ad-hoc basis over the years, and have not benefited from proper design.  However,
we do not want to tie our internal code (libged and below) directly to the Inventor
API - we instead want to migrate our existing logic to a form better suited for easy
mapping to and from the Inventor API.

You will see two primary drawing paths in the code - the following src/libged files
are for a new drawing stack that is used by the second-generation attempt at a drawing
stack:

draw/draw2.cpp
edit/edit2.cpp
erase/erase2.cpp
lod/lod2.cpp
rtcheck/rtcheck2.cpp
select/select2.cpp
view/autoview2.cpp
who/who2.cpp
zap/zap2.cpp

The src/gtools/gsh console application with the --new-cmds" option enabled is
where to start when testing - it is able to use the swrast headless display
system to produce images when using dm attach and screengrab, so it is a good
"low impact" way to test visualization capabilities after making code changes.
qged is the other application that targets this second gen system, but in these
early stages you'll want to leave Qt off - there will be enough to deal with,
and once we have the basic work done we'll circle back to Qt.

The brlcad_viewdbi folder has a version of BRL-CAD with some experimental work
on cleaning up the view structures and their associated LoD logic, but it is
not in final form and conflates work on updating the dbi_state management that
we're not ready to tackle here.  Still, it makes view sets the application's
responsibility rather than having libbv try to manage per-view LoD itself, and
that's definitely something we want to do - Obol has LoD capabilities, so we
will want to hook up our backend LoD generation logic to their system rather
than trying to manage it ourselves.

The initial goal is to propose a design for a new bview/bv_scene_obj and related
APIs that will allow our code to be a more straightforward mapping to the Obol
Inventor API concepts.  If possible, try to come up with new names for our
libbv replacements - what we will want to do is migrate all BRL-CAD code to
using your new containers/functions/etc., but keep the old API as an isolated,
unused, deprecated API for a couple releases until we can phase it out.

In the first phase, we'll want to migrate our logic as-is while minimizing
behavioral changes - this may limit us in a few cases like the LoD handling,
but let's get as close to the Inventor-style API setup as we can without major
rework in phase 1.  Phase 2 we can look at being a little more invasive and try
to get as close as we can while preserving an "old" path for backwards compatibility,
but we want Phase 1 in place first to keep our diffs minimal and clean.  Phase 3
will be to look at adapting the brlcad_viewdbi rework and improvements to the LoD
caching system - right now it's all tied up with the dbi_state rework, but we'll want
to migrate it in without also having major disruptions to the dbi_state layer.  It's
smarter about storing multiple data types in the background, so we want to get that
enhancement in place even if we're not set up yet to make proper use of it in the
scene displays.

Please maintain and update this document with your progress, so successive AI sessions
can keep working incrementally to achieve the goal.  The libbv API is used fairly
widely in the code, and we want to change all of it out except for a backwards
compatibility/deprecation leftover in libbv, so this is understood to be a major refactor
and should be approached accordingly.
