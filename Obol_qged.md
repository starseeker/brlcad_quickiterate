This effort involves trying to integrate the Obol scene manager into
BRL-CAD to replace (eventually) our very old school libdm solution to
the problem of 3D display.  There is a new prototype libbv API that
attempts to allow BRL-CAD commands to act with view concepts without
introducing dependencies on Obol's API directly into our core code.
The new API is designed to be easier to map to that API than our old
code.

Obol is present in the tree, but if you need to change it you should
build the new Obol version separately and target it's install for
bext_output/install so BRL-CAD's usual configure process can incorporate
it as a proper 3rd party dependency - the Obol code won't build cleanly
inside BRL-CAD's build.  Obol includes an osmesa, which should be installed
over and used instead of the bext osmesa - Obol's copy is newer.

The Qt based qged interface and its associated libqtcad library are the testing
ground for new drawing concepts, and various libged commands have an
alternative drawing code set up (see, for example, draw2.cpp) to work with a
new scene design instead of our old-school MGED style display.  It is this
drawing path and its dependent codes we are seeking (initially) to switch to
leveraging Obol properly.  The dm command will need an Obol-compatible
alternative to traditional libdm backed dm controls as well.

The libqtcad and qged codes are permitted as necessary or desirable
to talk directly to the Obol APIs, but libged and lower codes should
NOT do so - we want to limit our exposure to the Inventor API to
higher level code paths where toolkit integrations make it a necessity.

One of the most complex interactions between the scene and commands is the ert
command, which does async population of an in-scene overlay showing raytracing
results from another program.  We would like to do this in an Obol idiomatic
way, and the ert2 and ert3 commands represent initial attempts to set up such
an Obol-idiomatic alternative.  The difference between ert2 and ert3 is the
network communication approach - ert3 attempts to use a new libbu API that
let's us leverage pipes locally rather than continuing down the fbserv route.

To support the new ert3 approach, we also are setting up a librtrender and
associated program to enable rt-style async communication with another process
without having to go through libdm's APIs.  Eventually the idea is to have librtrender
offer as both a clean C API and a bu_ipc based async protocol all of rt's rendering
features, but for now we need to use basic abilities to plumb out the full
Obol+qged+ert3 interaction stack.

Where we want to get to is being able to have ert3 successfully render an incremental
raytrace in an Obol driven scene manager in qged.  We also need to verify that Obol
can successfully display wireframe and shaded mode data as well (independent of the
ert3 raytracing question.)

To make this work, you'll need to apt update and apt install the Qt6 development
headers.  Obol should be present in the bext_output/install directory, but you may
need to fine tune its integration into the BRL-CAD build a little - that's been through
a few iterations and upstream Obol doesn't yet do a proper Config.cmake solution (although
that should be coming.)

Once you get qged running with Obol for its scene manager, exercise the draw
and ert2/ert3 commands and capture their rendered outputs with qged screenshots
to demonstrate correct drawing behavior.  Make sure you don't accidentally get
a qged using the old display system - we want to make sure we're using Obol
correctly and having it actually display BRL-CAD geometry data in-scene.

All of this is new, unsettled API and code - you are free to change whatever needs
changing without regard to backwards compatibility.  If Obol needs changes, please
prepare an OBOL_CHANGES.md file we can feed upstream to the Obol project to have them
implement what you need.

This file is intended to make it easier for multiple sessions of copilot to quickly
get down to work on the core tasks rather than having to figure out setup issues -
please update and refine it for that purpose as you work.  Unless instructed otherwise,
copilot sessions working on this project should continue working until they get close
to their session time limit or all work is completed.
