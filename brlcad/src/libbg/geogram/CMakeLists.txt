# Geogram subset for BRL-CAD
# Minimal copy supporting: GEO::initialize, mesh_repair, remove_small_connected_components,
# fill_holes, compute_normals, remesh_smooth
# Extracted from geogram/src/lib/geogram/

set(GEOGRAM_SRCS
    # basic infrastructure
    basic/algorithm.cpp
    basic/assert.cpp
    basic/attributes.cpp
    basic/command_line.cpp
    basic/common.cpp
    basic/counted.cpp
    basic/environment.cpp
    basic/factory.cpp
    basic/geogram_options.cpp
    basic/geometry.cpp
    basic/line_stream.cpp
    basic/logger.cpp
    basic/numeric.cpp
    basic/packed_arrays.cpp
    basic/process.cpp
    basic/process_unix.cpp
    basic/progress.cpp
    basic/stopwatch.cpp
    basic/string.cpp
    # numerics
    numerics/PCK.cpp
    numerics/multi_precision.cpp
    numerics/optimizer.cpp
    numerics/predicates.cpp
    # delaunay (needed for CVT remeshing and LFS)
    delaunay/delaunay.cpp
    delaunay/delaunay_nn.cpp
    # mesh
    mesh/mesh.cpp
    mesh/mesh_AABB.cpp
    mesh/mesh_fill_holes.cpp
    mesh/mesh_geometry.cpp
    mesh/mesh_halfedges.cpp
    mesh/mesh_partition.cpp
    mesh/mesh_preprocessing.cpp
    mesh/mesh_remesh.cpp
    mesh/mesh_reorder.cpp
    mesh/mesh_repair.cpp
    mesh/mesh_topology.cpp
    # voronoi (needed for remesh_smooth CVT)
    voronoi/CVT.cpp
    voronoi/RVD.cpp
    voronoi/RVD_callback.cpp
    voronoi/RVD_mesh_builder.cpp
    voronoi/generic_RVD_cell.cpp
    voronoi/generic_RVD_polygon.cpp
    voronoi/integration_simplex.cpp
    # points (needed for colocate in mesh_repair, kd_tree for nn_search)
    points/colocate.cpp
    points/kd_tree.cpp
    points/nn_search.cpp
    # OpenNL linear solver (needed for remesh CVT Newton step)
    NL/nl_amgcl.cpp
    NL/nl_api.c
    NL/nl_blas.c
    NL/nl_context.c
    NL/nl_iterative_solvers.c
    NL/nl_matrix.c
    NL/nl_os.c
    NL/nl_preconditioners.c
)

# Windows-only
if(WIN32)
    list(APPEND GEOGRAM_SRCS basic/process_win.cpp)
endif()

add_library(geogram_brlcad STATIC ${GEOGRAM_SRCS})

# Include root: <geogram/...> resolves from parent of this directory
target_include_directories(geogram_brlcad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(geogram_brlcad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/amgcl
)

target_compile_definitions(geogram_brlcad PUBLIC
    NL_WITH_AMGCL
    BRLCAD_GEOGRAM_EMBED
)

# Suppress all warnings - this is third-party code
if(MSVC)
    target_compile_options(geogram_brlcad PRIVATE /W0)
else()
    target_compile_options(geogram_brlcad PRIVATE
        -w
        -frounding-math
        -ffp-contract=off
    )
endif()

target_compile_features(geogram_brlcad PUBLIC cxx_std_17)

if(UNIX)
    target_link_libraries(geogram_brlcad PRIVATE pthread dl)
endif()
