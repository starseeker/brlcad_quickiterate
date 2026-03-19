BRL-CAD uses geogram to support features in its src/librt/primitives/bot/repair.cpp code
and src/libged/bot/remesh.cpp code.  geogram itself as a dependency is somewhat problematic
on various platforms, as it defines per-platform build flags rather than detecting what
configurations should be used.

What we would like to do is extract into src/libbg/geogram just the subset of the geogram
logic needed to support the features BRL-CAD uses.  I'm not sure how much this will pull
in - a first pass should try to simply get our existing code compiling as-is using src/libbg/geogram
and removing any installed geogram build products in bext_output that might interfere.
Once we know what we need, the next step will be to analyze what in the necessary subset
is sensitive to build platform and try to make it more generic - however, first we need to
constrain the problem to just the geogram logic we need.

The geogram sources are available in the geogram top level directory, so you should pull
from there.  You will probably need to suppress compile warnings with geogram sources in
src/libbg/geogram - that's find for the first cut, we just want to identify what pieces of
geogram we are actually using at this stage.  You may also remove portions of files if we
only need part of the file and the other content in the file is pulling in parts of geogram
we don't need for the functionality we are interested in - we're looking to get a minimal
subset identified.
