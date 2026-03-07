asciiquack (sources at asciiquack top level directory, binary present in
bext_output/noinstall/bin) is an in-development port of asciidoctor to
C++.  We are hoping it will make it possible to convert our DocBook
documentation to the asciidoc format and away from xml while still
having a self-contained build.

Our DocBook documentation is found in brlcad/doc/docbook, and there is
an elaborate support structure of CMake build logic and support tools
used to produce html and man page output (and optionally PDF, although
that feature is seldom used.)

Please investigate whether all of our docbook documentation can successfully
be converted to asciidoc while still maintain (or even improve, that would
be fine too) our output quality.  My suggestion would be to set up a
doc/asciidoc parallel directory to doc/docbook and set up the necessary
infrastructure so we can generate outputs from both sources and compare
them to see what we are getting.

asciiquack is something we fully control, so any fixes or enhancements needed
to it may simply be added, but the goal is not to pull in 3rd party dependencies
to drive the conversion process if we don't have to - a major virtue of our
html and man outputs from docbook is the process has been fully self-contained
for over a decade, and very stable - we want to replicate that.

It would be nice if the available PDF output options are reasonable, but that
should wait until after the html and man outputs are sorted - stub in the
build infrastructure for it (hopefully it will be simpler than the docbook
pdf logic!) but defer testing that until later.
