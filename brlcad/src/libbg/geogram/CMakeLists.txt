# Geogram subset for BRL-CAD - minimal copy for remesh and repair functionality
# Extracted from geogram/src/lib/geogram/

file(GLOB GEOGRAM_BASIC_SRCS CONFIGURE_DEPENDS basic/*.cpp basic/*.c)
file(GLOB GEOGRAM_NUMERICS_SRCS CONFIGURE_DEPENDS numerics/*.cpp numerics/*.c)
file(GLOB GEOGRAM_MESH_SRCS CONFIGURE_DEPENDS mesh/*.cpp mesh/*.c)
file(GLOB GEOGRAM_DELAUNAY_SRCS CONFIGURE_DEPENDS delaunay/*.cpp delaunay/*.c)
file(GLOB GEOGRAM_VORONOI_SRCS CONFIGURE_DEPENDS voronoi/*.cpp voronoi/*.c)
file(GLOB GEOGRAM_POINTS_SRCS CONFIGURE_DEPENDS points/*.cpp points/*.c)
file(GLOB GEOGRAM_NL_SRCS CONFIGURE_DEPENDS NL/*.cpp NL/*.c)
file(GLOB GEOGRAM_BIBLIOGRAPHY_SRCS CONFIGURE_DEPENDS bibliography/*.cpp bibliography/*.c)

file(GLOB GEOGRAM_TP_LIBMESHB_SRCS CONFIGURE_DEPENDS third_party/libMeshb/sources/*.cpp third_party/libMeshb/sources/*.c)
file(GLOB GEOGRAM_TP_RPLY_SRCS CONFIGURE_DEPENDS third_party/rply/*.cpp third_party/rply/*.c)
file(GLOB GEOGRAM_TP_ZLIB_SRCS CONFIGURE_DEPENDS third_party/zlib/*.cpp third_party/zlib/*.c)
file(GLOB GEOGRAM_TP_POISSON_SRCS CONFIGURE_DEPENDS third_party/PoissonRecon/*.cpp third_party/PoissonRecon/*.c)

set(GEOGRAM_ALL_SRCS
    ${GEOGRAM_BASIC_SRCS}
    ${GEOGRAM_NUMERICS_SRCS}
    ${GEOGRAM_MESH_SRCS}
    ${GEOGRAM_DELAUNAY_SRCS}
    ${GEOGRAM_VORONOI_SRCS}
    ${GEOGRAM_POINTS_SRCS}
    ${GEOGRAM_NL_SRCS}
    ${GEOGRAM_BIBLIOGRAPHY_SRCS}
    ${GEOGRAM_TP_LIBMESHB_SRCS}
    ${GEOGRAM_TP_RPLY_SRCS}
    ${GEOGRAM_TP_ZLIB_SRCS}
    ${GEOGRAM_TP_POISSON_SRCS}
)

list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "delaunay_triangle\\.cpp$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "delaunay_tetgen\\.cpp$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "periodic_delaunay_3d\\.cpp$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "nl_arpack\\.c$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "nl_cholmod\\.c$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "nl_cuda\\.c$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "nl_mkl\\.c$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "nl_superlu\\.c$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "process_win\\.cpp$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "lbfgs_optimizers\\.cpp$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "mesh_CSG_builder\\.cpp$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "mesh_baking\\.cpp$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "mesh_CSG_compiler\\.cpp$")
list(FILTER GEOGRAM_ALL_SRCS EXCLUDE REGEX "mesh_CSG_utils\\.cpp$")

add_library(geogram_brlcad STATIC ${GEOGRAM_ALL_SRCS})

target_include_directories(geogram_brlcad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(geogram_brlcad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libMeshb/sources
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/amgcl
)

target_compile_definitions(geogram_brlcad PUBLIC
    GEOGRAM_WITH_PDEL
    NL_WITH_AMGCL
    BRLCAD_GEOGRAM_EMBED
)

if(MSVC)
    target_compile_options(geogram_brlcad PRIVATE /W0)
else()
    target_compile_options(geogram_brlcad PRIVATE -w -frounding-math -ffp-contract=off)
endif()

target_compile_features(geogram_brlcad PUBLIC cxx_std_17)

if(UNIX)
    target_link_libraries(geogram_brlcad PRIVATE pthread dl)
endif()
