BRL-CAD has some support for NURBS boolean evaluation, but it is incomplete
and error prone.

There is a document in brlcad/doc/docbook/devguides/bool_eval_development.xml
that lays out a lot of useful information about where things stand, and
some thoughts on what remains to be done.

Please examine that document, the current state of BRL-CAD's NURBS boolean
evaluation code, and develop a plan to finish the implementation and improve
it to achieve robust, reliable boolean evaluation of NURBS based CSG trees.

As you improve robustness, you can use NURBS conversions of BRL-CAD's sample
models to provide more real test cases - they are representative of the kinds
of problems we're looking to solve.  You will likely need to fix bugs in the
librt routines that are responsible for producing ON_Brep representations of
CSG solids prior to attempting boolean evaluation - be sure to check the
correctness of the primitive provided BReps and address any issues.

Success will be defined as converting all share/db/*.g CSG examples to evaluated
NURBS boolean outputs successfully.  A bonus stage will be wiring this ability
into our g-step exporter for high quality STEP AP203 export of BRL-CAD CSG
geometry as NURBS surfaces.  If there are complex primitives such as DSP, EBM,
or VOL that present severe problems when generating NURBS representations, you
can defer those for a later stage.

