With improved mesh boolean logic via Manifold, we are not finding that
limitations in our ability to produce meshes from primitives with the
tess routines are becoming a limitation.

src/libged/tests/bot/tgc.g has some inputs that appear to be problematic
for the tgc tess routine - we're not sure why, but they seem to be
extremely sensitive mathematically and very slight parameter adjustments
can result in success or failure producing outputs - wee seem to be in
some corner case with the math.

In one of the chess sample models (I don't recall which one) there is a torus
or eto that fails to facetize using NMG, perhaps with a timeout or infinite
loop.  It may have a near-zero, zero or negative radius - I don't think the
current NMG tess logic deals with such cases very well. (A zero radius would
probably produce a self-intersecting mesh unless the logic knows to back away
from coalesing the mesh at the vertex in that case, and in a negative radius
scenario it would have to identify the pinch points and stop the mesh there
rather than continuing to close the circles as it does for positive radius
cases.  (Likely both tor and eto have that issue...)

More broadly, most of our primitives respond erratically if at all to the
tessellation tolerances (rel, abs, norm) that are nominally supposed to govern
the amount of detail individual shapes produce.  We need a survey of current
behavior as a function of changes to those parameters, and a plan for
rationalizing our response to them across the board.

Please investigate the specific problem issues above and see if you can find
solutions (you'll need to build BRL-CAD, build the chess sample models, and
iterate through them to find the primitive that doesn't terminate in a reasonable
time frame.)  At the moment the facetize command uses an elaborate system to be
fault tolerant to these sorts of failures, which we need to leave in place, so
you may want to write a specialty test program for this purpose to give you
easy control over specifically running the nmg tess routines for each primitive
and varying the tolerance parameters.
