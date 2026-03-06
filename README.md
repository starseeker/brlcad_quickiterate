The libged npush command is intended to be a comprehensive and more powerful
replacment for the push and xpush commands.  However, the nature of what it
does means we must be very careful not to damage the CSG geometry when manipulating
matrices, copying primitives and modifying comb trees.  We need a careful, detailed
analysis of the behavior of npush as compared to the exiting src/libged/push/push.c
and src/libged/xpush/xpush.c implementations, as well as thorough understanding and
testing of its ability to handle complex scenarios like reuses of subtrees in different
parts of a broad complex hierarchy where different combinations of matrices might
ultimately wind up non-obviously producing identical results that don't call for
duplication and tree updating.  (IIRC xpush is currently fairly naive in that regard.)

Please review what testing is available already (start with
regress/ged/push.cpp), and make sure we have a thorough validation suite for
npush, designed to check its behavior with difficult cases that push its resolution
of multiple-different-matrices-on-different-paths-creating-identical situations.

We want to achieve a high-confidence correctness in npush that will let us deploy
it as a production grade tool.

Please also check doc/docbook/system/mann/npush.xml for correctness, readability,
succinctness, thoroughness and proper explanations of the implications of different
options.
