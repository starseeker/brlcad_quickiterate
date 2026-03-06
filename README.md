BRL-CAD's clone command in src/libged/clone.c is a powerful tool for creating
patterned geometry in a BRL-CAD database, but the code is very old-style (even
by BRL-CAD standards) and has various issues.

There are notes there about using libbu strings/lists, but what I would like to
try is to convert the code to C++17 for a cleaner, easier to understand and
maintain form.  bu_vls is fine to use if convenient, but let's try to avoid
bu_list if we can in favor of standard C++ constructs.  We should clean up the
naming convention (feel free to make use of bu_vls_incr for name generation if
helpful).  We should bu_opt for option handling, adding support for long
options as appropriate.

If there are any ways the performance of the cloning process can be improved
we would be interested.

Check what verification and validation we have in place for clone in
src/libged/tests/ and/or regress/ - I suspect it isn't much.  If that's the
case, please devise a testing regime we can add to validate a refactored clone
against the original clone behavior (as well as spotting any bugs lurking in
the original clone code - we want to fix any that are there rather than
preserving them.)

There is a separate "pattern" tool in src/tclscripts/lib/pattern.tcl and
src/tclscripts/lib/pattern_gui.tcl - this has a few capabilities/patterns not
present in the command line libged clone.  We want to expand the libged clone
to fully support all capabilities of the pattern Tcl implementation, and
replace the Tcl implementation with calls to the libged clone instead - the
pattern_gui.tcl should be updated to work with the libged clone instead of the
current tcl script version.

There is also an xclone in src/tclscripts/xclone.tcl - that is identified as
being a workaround for a limitation of clone.  We should identify and fix this
limitation so xclone isn't needed.

Please check for and evaluate other calls of the "clone" command in the Tcl
logic and other BRL-CAD code to be sure our new version doesn't break anything.

There is a man page for clone at doc/docbook/system/mann/clone.xml - it could
probably use a rework (not even counting the proposed new features and options)
so once you have the new clone in final form please redo the clone man page to
be clear, complete, concise, helpful and informative.
