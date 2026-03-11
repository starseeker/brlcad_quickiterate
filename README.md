The top level GeometricTools directory contains GTE extension files developed
to replicate geogram repair and remeshing functionality used by BRL-CAD code.
Please replace the brlcad/ usage of geogram with the new functionality in
GeometricTools, and test the BRL-CAD features to ensure the results are
as good or better than those obtained with geogram.  Start with the test models
in the repository (GenericTwin in particular is known to have repairable BoT
geometry) and expand to https://github.com/BRL-CAD/models for additional inputs
if needed.  If I recall Cassini and Hubble in that repository are mesh models
and should at least be usable for remeshing testing even if they're already manifold (doubtful.)

If you encounter regressions, please document them and produce test cases we
can use in the upstream GeometricTools repository to replicate and work on
the issues.

The goal is to be able to eliminate geogram as a BRL-CAD dependency.
