MGED is trying to use a separate Tcl_Interp to allow search -exec to
execute Tcl commands safely without causing problems for the main interp.
However, that means any custom procs the user defines in the parent
interp aren't available to exec.

The current version in src/mged tries to address this by creating a local
interp and syncing it with the parent.  Please investigate a) whether this
works at all (i.e. try defining a proc in MGED to set a "visited" attribute to
1 on all objects and seeing if you can search -exec with it successfully using
the share/db/m35.g geometry as a non-trivial input to work with) b) whether there
are any bugs in our implementation and c) whether what we are doing is performant
if we are execing on a large number of objects (i.e., whether it is worth creating
one Tcl_Interp at the beginning of a search command, passing that through mged_state
for reuse on all individual exec calls, and deleting it at the end vs setting up
and tearing down per-exec as we are now.  The latter is most likely faster, but its
integration into MGED command execution will require some checking of what we're about
to execute, since we don't want to create the interp for commands other than search - you'll
have to figure out the right way to integrate it.  You can add a search_interp slot to the
struct mged_state if need be to pass a reusable interp through a search exec run, but
we don't want to persist the interp beyond the search command lifecycle since the
environment may change before the next search is run.)
