BRL-CAD currently uses OpenMesh for a number of subdivison
algorithms.  We would like to eliminate OpenMesh as a dependency -
please look at whether we can translate the openmesh logic to
the GTE (GeometricTools) framework and use it from there instead.
The code is license compatible, so you may translate rather than
having to rewrite it.  Once translated, update BRL-CAD's code to
use the new GTE functionality rather than OpenMesh.  New GTE style
headers should be added to BRL-CAD's code, not the upstream
GeometricTools sources - we'll keep them separated - but use
GTE style for them in case we do decide to submit them upstream
(but keep OpenMesh copyright info, since that's the code we will
be translating.)

Success is defined as removing OpenMesh as a dependency without
loss of functionality in BRL-CAD's features currently relying on it.
