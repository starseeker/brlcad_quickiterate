BRL-CAD historically used OpenMesh for a number of subdivison
and smoothing algorithms. We would like to eliminate OpenMesh as a dependency,
so we are translating the algorithms we need in src/libbg/GTE from OpenMesh.
Openmesh sources are in the openmesh/ toplevel directory.

Success is defined as removing OpenMesh as a dependency without
loss of functionality in BRL-CAD's features currently relying on it.  We have
the initial port more or less in place, but there seem to be a couple of things
we need to look into.

Using the top level bunny_stanford.g and bunny_simple.g as working test
models for bot subd and bot smooth:

a.  bot smooth doesn't seem to change either model at all.  is this expected
from these inputs and does it match what openmesh would do?

b.  the midpoint subdivision produces a sparse non-manifold output mesh - it's
as if a lot of triangles are being dropped from the subdivision output.

c.  Performance on the interpolating sqrt3 and butterfly subdivisions seems to
be quite bad - is this also true in openmesh?  More generally, are our GTE
implementations as fast or nearly as fast as OpenMesh's?


