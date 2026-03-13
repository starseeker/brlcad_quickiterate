# BRL-CAD-specific GTE header overlay.
#
# These headers extend or replace headers from the upstream GeometricTools
# library (bext_output/noinstall/include/GTE/).  They are internal to BRL-CAD
# and are not part of the public API.  Any build target that needs them must
# add ${BRLCAD_SOURCE_DIR}/src/libbg/GTE *before* ${GTE_INCLUDE_DIR} in its
# include-directory list so that these copies shadow the upstream originals.
#
# Files registered here for dist-check / source-tree tracking only.

cmakefiles(
  Mathematics/CVT6D.h
  Mathematics/CVTN.h
  Mathematics/CVTOptimizer.h
  Mathematics/DelaunayN.h
  Mathematics/DelaunayNN.h
  Mathematics/IntegrationSimplex.h
  Mathematics/LSCMParameterization.h
  Mathematics/MeshAnisotropy.h
  Mathematics/MeshHoleFilling.h
  Mathematics/MeshPreprocessing.h
  Mathematics/MeshQuality.h
  Mathematics/MeshRemesh.h
  Mathematics/MeshRepair.h
  Mathematics/MeshValidation.h
  Mathematics/NearestNeighborSearchN.h
  Mathematics/RestrictedVoronoiDiagram.h
  Mathematics/RestrictedVoronoiDiagramN.h
  Mathematics/SurfaceRVDN.h
  CMakeLists.txt
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
