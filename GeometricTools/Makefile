# Makefile for building GTE mesh processing test programs

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -I. -IGTE
LDFLAGS = 
PTHREAD = -pthread

# Test source directory
TEST_DIR = tests

# bext_output geogram paths (pre-built; auto-detected relative to repo root).
# Override BRLCAD_EXT_DIR if bext_output is elsewhere.
BRLCAD_EXT_DIR ?= $(realpath $(dir $(lastword $(MAKEFILE_LIST)))/../bext_output)
BRLCAD_EXT_INSTALL_DIR  = $(BRLCAD_EXT_DIR)/install
BRLCAD_EXT_NOINSTALL_DIR = $(BRLCAD_EXT_DIR)/noinstall

# GTE headers bundled in bext_output (preferred over the local GTE/ copy)
BEXT_GTE_INC = -I$(BRLCAD_EXT_NOINSTALL_DIR)/include -I$(BRLCAD_EXT_NOINSTALL_DIR)/include/GTE

# Geogram library bundled in bext_output
BEXT_GEO_INC = -I$(BRLCAD_EXT_INSTALL_DIR)/include/geogram1
BEXT_GEO_LIB_DIR = $(BRLCAD_EXT_INSTALL_DIR)/lib
BEXT_GEO_LIBS = -L$(BEXT_GEO_LIB_DIR) -lgeogram -Wl,-rpath,$(BEXT_GEO_LIB_DIR)

# Geogram submodule paths (fallback when bext_output is not available)
GEOGRAM_SRC = geogram/src/lib
GEOGRAM_INC = -I$(GEOGRAM_SRC) -I$(GEOGRAM_SRC)/geogram/third_party/OpenNL
GEOGRAM_BUILD_DIR = /tmp/geogram_build
GEOGRAM_LIB_DIR = $(GEOGRAM_BUILD_DIR)/lib
GEOGRAM_LIBS = -L$(GEOGRAM_LIB_DIR) -lgeogram -Wl,-rpath,$(GEOGRAM_LIB_DIR)

# VTK Verdict paths (libvtk9-dev)
VTK_VERSION ?= 9.1
VTK_INC  = -I/usr/include/vtk-$(VTK_VERSION)
VTK_LIBS = -lvtkCommonCore-$(VTK_VERSION) -lvtkCommonDataModel-$(VTK_VERSION) \
           -lvtkFiltersVerdict-$(VTK_VERSION) -lvtkverdict-$(VTK_VERSION)

# Primary test targets
TARGETS = test_mesh_repair test_full_algorithms test_rvd \
          demo_rvd_cvt test_remesh_comparison test_newton_optimizer \
          test_rvd_performance stress_test test_threadpool test_parallel_rvd \
          test_anisotropic_remesh test_delaunay6 test_cvt6d \
          test_delaunay_n test_delaunay_nn test_rvd_n test_cvt_n \
          test_anisotropic_end_to_end \
          compare_with_geogram

all: $(TARGETS)

# Basic functionality tests
test_mesh_repair: $(TEST_DIR)/test_mesh_repair.cpp \
                  GTE/Mathematics/MeshHoleFilling.h \
                  GTE/Mathematics/LSCMParameterization.h
	$(CXX) $(CXXFLAGS) -o test_mesh_repair $(TEST_DIR)/test_mesh_repair.cpp $(LDFLAGS)

test_remesh: $(TEST_DIR)/test_remesh.cpp
	$(CXX) $(CXXFLAGS) -o test_remesh $(TEST_DIR)/test_remesh.cpp $(LDFLAGS)

test_full_algorithms: $(TEST_DIR)/test_full_algorithms.cpp
	$(CXX) $(CXXFLAGS) -o test_full_algorithms $(TEST_DIR)/test_full_algorithms.cpp $(LDFLAGS)

test_rvd: $(TEST_DIR)/test_rvd.cpp
	$(CXX) $(CXXFLAGS) -o test_rvd $(TEST_DIR)/test_rvd.cpp $(LDFLAGS)

# Comparison and validation
test_remesh_comparison: $(TEST_DIR)/test_remesh_comparison.cpp
	$(CXX) $(CXXFLAGS) -o test_remesh_comparison $(TEST_DIR)/test_remesh_comparison.cpp $(LDFLAGS)

# GTE-only comparison (no geogram required, compares GTE output with a pre-generated geogram .obj)
compare_with_geogram: $(TEST_DIR)/compare_with_geogram.cpp
	$(CXX) $(CXXFLAGS) -o compare_with_geogram $(TEST_DIR)/compare_with_geogram.cpp $(LDFLAGS)

# Build the geogram library from the submodule.
# Initializes the required nested submodules (OpenNL, libMeshb, rply, amgcl) and
# runs a Release cmake build into $(GEOGRAM_BUILD_DIR).
build_geogram:
	@echo "Initializing geogram submodule and its dependencies..."
	git submodule update --init geogram
	cd geogram && git submodule update --init \
	    src/lib/geogram/third_party/OpenNL \
	    src/lib/geogram/third_party/libMeshb \
	    src/lib/geogram/third_party/rply \
	    src/lib/geogram/third_party/amgcl
	@echo "Configuring geogram (Release, no graphics)..."
	mkdir -p $(GEOGRAM_BUILD_DIR)
	cd $(GEOGRAM_BUILD_DIR) && cmake $(CURDIR)/geogram \
	    -DGEOGRAM_WITH_GRAPHICS=OFF \
	    -DGEOGRAM_WITH_TETGEN=OFF \
	    -DGEOGRAM_WITH_TRIANGLE=OFF \
	    -DGEOGRAM_WITH_LEGACY_NUMERICS=OFF \
	    -DCMAKE_BUILD_TYPE=Release
	@echo "Building libgeogram..."
	$(MAKE) -C $(GEOGRAM_BUILD_DIR) -j$$(nproc) geogram
	@echo "geogram built successfully: $(GEOGRAM_LIB_DIR)/libgeogram.so"

# Direct GTE vs Geogram comparison using pre-built bext_output geogram (preferred).
# Falls back to the geogram submodule build if bext_output is absent.
# Uses BEXT_* variables when bext_output/install/lib/libgeogram.so exists;
# otherwise uses GEOGRAM_* submodule variables (run 'make build_geogram' first).
ifeq ($(wildcard $(BEXT_GEO_LIB_DIR)/libgeogram.so),)
# bext_output not present — use submodule
_GEO_INC  = $(GEOGRAM_INC)
_GEO_LIBS = $(GEOGRAM_LIBS)
else
# bext_output present — use pre-built library
_GEO_INC  = $(BEXT_GEO_INC)
_GEO_LIBS = $(BEXT_GEO_LIBS)
endif

test_geogram_comparison: $(TEST_DIR)/test_geogram_comparison.cpp \
                         GTE/Mathematics/MeshRepair.h \
                         GTE/Mathematics/MeshHoleFilling.h \
                         GTE/Mathematics/MeshValidation.h \
                         GTE/Mathematics/MeshRemesh.h \
                         GTE/Mathematics/MeshAnisotropy.h \
                         GTE/Mathematics/SurfaceRVDN.h \
                         GTE/Mathematics/CVTN.h \
                         GTE/Mathematics/DelaunayNN.h
	$(CXX) $(CXXFLAGS) $(_GEO_INC) -o test_geogram_comparison \
	    $(TEST_DIR)/test_geogram_comparison.cpp \
	    $(_GEO_LIBS) $(LDFLAGS)

# Performance and optimization
test_newton_optimizer: $(TEST_DIR)/test_newton_optimizer.cpp
	$(CXX) $(CXXFLAGS) -o test_newton_optimizer $(TEST_DIR)/test_newton_optimizer.cpp $(LDFLAGS)

test_rvd_performance: $(TEST_DIR)/test_rvd_performance.cpp
	$(CXX) $(CXXFLAGS) -o test_rvd_performance $(TEST_DIR)/test_rvd_performance.cpp $(LDFLAGS)

# Stress testing
stress_test: $(TEST_DIR)/stress_test.cpp
	$(CXX) $(CXXFLAGS) -o stress_test $(TEST_DIR)/stress_test.cpp $(LDFLAGS)

# Parallel processing tests
test_threadpool: $(TEST_DIR)/test_threadpool.cpp GTE/Mathematics/ThreadPool.h
	$(CXX) $(CXXFLAGS) $(PTHREAD) -o test_threadpool $(TEST_DIR)/test_threadpool.cpp $(LDFLAGS)

test_parallel_rvd: $(TEST_DIR)/test_parallel_rvd.cpp GTE/Mathematics/RestrictedVoronoiDiagramOptimized.h GTE/Mathematics/ThreadPool.h
	$(CXX) $(CXXFLAGS) $(PTHREAD) -o test_parallel_rvd $(TEST_DIR)/test_parallel_rvd.cpp $(LDFLAGS)

test_enhanced_manifold: $(TEST_DIR)/test_enhanced_manifold.cpp
	$(CXX) $(CXXFLAGS) $(PTHREAD) -o test_enhanced_manifold $(TEST_DIR)/test_enhanced_manifold.cpp $(LDFLAGS)

# Anisotropic remeshing test
test_anisotropic_remesh: $(TEST_DIR)/test_anisotropic_remesh.cpp GTE/Mathematics/MeshAnisotropy.h GTE/Mathematics/MeshRemesh.h GTE/Mathematics/SurfaceRVDN.h
	$(CXX) $(CXXFLAGS) -o test_anisotropic_remesh $(TEST_DIR)/test_anisotropic_remesh.cpp $(LDFLAGS)

# 6D Delaunay test
test_delaunay6: $(TEST_DIR)/test_delaunay6.cpp GTE/Mathematics/Delaunay6.h GTE/Mathematics/MeshAnisotropy.h
	$(CXX) $(CXXFLAGS) -o test_delaunay6 $(TEST_DIR)/test_delaunay6.cpp $(LDFLAGS)

# 6D CVT test
test_cvt6d: $(TEST_DIR)/test_cvt6d.cpp GTE/Mathematics/CVT6D.h GTE/Mathematics/MeshAnisotropy.h
	$(CXX) $(CXXFLAGS) -o test_cvt6d $(TEST_DIR)/test_cvt6d.cpp $(LDFLAGS)

# DelaunayN base class test
test_delaunay_n: $(TEST_DIR)/test_delaunay_n.cpp GTE/Mathematics/DelaunayN.h
	$(CXX) $(CXXFLAGS) -o test_delaunay_n $(TEST_DIR)/test_delaunay_n.cpp $(LDFLAGS)

# DelaunayNN implementation test
test_delaunay_nn: $(TEST_DIR)/test_delaunay_nn.cpp GTE/Mathematics/DelaunayNN.h GTE/Mathematics/NearestNeighborSearchN.h GTE/Mathematics/DelaunayN.h
	$(CXX) $(CXXFLAGS) -o test_delaunay_nn $(TEST_DIR)/test_delaunay_nn.cpp $(LDFLAGS)

# RestrictedVoronoiDiagramN test
test_rvd_n: $(TEST_DIR)/test_rvd_n.cpp GTE/Mathematics/RestrictedVoronoiDiagramN.h GTE/Mathematics/DelaunayNN.h GTE/Mathematics/MeshAnisotropy.h
	$(CXX) $(CXXFLAGS) -o test_rvd_n $(TEST_DIR)/test_rvd_n.cpp $(LDFLAGS)

# CVTN (N-dimensional CVT) test
test_cvt_n: $(TEST_DIR)/test_cvt_n.cpp GTE/Mathematics/CVTN.h GTE/Mathematics/RestrictedVoronoiDiagramN.h GTE/Mathematics/DelaunayNN.h GTE/Mathematics/MeshAnisotropy.h GTE/Mathematics/SurfaceRVDN.h
	$(CXX) $(CXXFLAGS) -o test_cvt_n $(TEST_DIR)/test_cvt_n.cpp $(LDFLAGS)

# Demonstration programs
demo_rvd_cvt: $(TEST_DIR)/demo_rvd_cvt.cpp
	$(CXX) $(CXXFLAGS) -o demo_rvd_cvt $(TEST_DIR)/demo_rvd_cvt.cpp $(LDFLAGS)

# Test data generation
generate_test_data:
	@echo "Generating stress test meshes..."
	python3 $(TEST_DIR)/scripts/create_stress_meshes.py
	@echo "Test data generated in $(TEST_DIR)/data/"

# Run tests
test: test_mesh_repair
	@echo "Running basic mesh repair test..."
	./test_mesh_repair $(TEST_DIR)/data/gt.obj $(TEST_DIR)/data/gt_repaired.obj

# Run GTE vs Geogram comparison using bext_output geogram (no submodule required).
# Runs both use cases: repair+hole-filling and anisotropic remeshing on GenericTwin (gt.obj).
test_geogram_bext: test_geogram_comparison
	@echo "Running GTE vs Geogram comparison on GenericTwin (gt.obj) using bext_output geogram..."
	LD_LIBRARY_PATH="$(BEXT_GEO_LIB_DIR):$$LD_LIBRARY_PATH" \
	    ./test_geogram_comparison $(TEST_DIR)/data/gt.obj

# Run GTE vs Geogram comparison (requires test_geogram_comparison to be built first)
# Runs both use cases: repair+hole-filling and anisotropic remeshing
# To build geogram automatically: make build_geogram test_geogram_comparison
test_geogram: test_geogram_comparison
	@echo "Running GTE vs Geogram comparison (repair, remesh)..."
	LD_LIBRARY_PATH="$(BEXT_GEO_LIB_DIR):$$LD_LIBRARY_PATH" \
	    ./test_geogram_comparison $(TEST_DIR)/data/gt.obj

# GenericTwin BRL-CAD bot repair simulation: run GTE repair and Geogram repair on every
# BoT mesh in gt.obj and compare success rates (mirrors BRL-CAD's `bot repair` command).
# Requires bext_output geogram.
test_bot_repair_comparison: $(TEST_DIR)/test_bot_repair_comparison.cpp \
                             GTE/Mathematics/MeshRepair.h \
                             GTE/Mathematics/MeshHoleFilling.h \
                             GTE/Mathematics/MeshPreprocessing.h \
                             GTE/Mathematics/MeshValidation.h
	$(CXX) $(CXXFLAGS) $(_GEO_INC) -I$(BRLCAD_EXT_INSTALL_DIR)/include \
	    -o test_bot_repair_comparison \
	    $(TEST_DIR)/test_bot_repair_comparison.cpp \
	    $(_GEO_LIBS) -L$(BRLCAD_EXT_INSTALL_DIR)/lib -lmanifold \
	    -Wl,-rpath,$(BRLCAD_EXT_INSTALL_DIR)/lib

# Run the BRL-CAD bot repair simulation on GenericTwin (gt.obj)
test_bot_repair: test_bot_repair_comparison
	@echo "=== BRL-CAD bot repair: GTE vs Geogram on GenericTwin ==="
	LD_LIBRARY_PATH="$(BEXT_GEO_LIB_DIR):$(BRLCAD_EXT_INSTALL_DIR)/lib:$$LD_LIBRARY_PATH" \
	    ./test_bot_repair_comparison $(TEST_DIR)/data/gt.obj

# GenericTwin-specific comparison test: exercises both repair and remesh on the
# gt.obj mesh (exported from the BRL-CAD GenericTwin.fg4 example model).
# This is the canonical regression test for GTE vs Geogram parity.
test_generic_twin: test_geogram_comparison compare_with_geogram test_bot_repair_comparison
	@echo "=== GenericTwin GTE vs Geogram comparison ==="
	@echo "Running full GTE vs Geogram comparison on GenericTwin (gt.obj)..."
	LD_LIBRARY_PATH="$(BEXT_GEO_LIB_DIR):$(BRLCAD_EXT_INSTALL_DIR)/lib:$$LD_LIBRARY_PATH" \
	    ./test_geogram_comparison $(TEST_DIR)/data/gt.obj
	@echo ""
	@echo "Running standalone GTE repair+fill on GenericTwin (gt.obj)..."
	./compare_with_geogram $(TEST_DIR)/data/gt.obj
	@echo ""
	@echo "Running BRL-CAD bot repair simulation on GenericTwin (gt.obj)..."
	LD_LIBRARY_PATH="$(BEXT_GEO_LIB_DIR):$(BRLCAD_EXT_INSTALL_DIR)/lib:$$LD_LIBRARY_PATH" \
	    ./test_bot_repair_comparison $(TEST_DIR)/data/gt.obj

stress: stress_test
	@echo "Running comprehensive stress tests..."
	./stress_test

# Remesh quality evaluation using VTK Verdict metrics.
# Compares GTE MeshRemesh vs Geogram remesh_smooth using standardized FEA mesh quality
# metrics: aspect ratio, scaled Jacobian, min/max angle, shape, condition number, edge ratio.
# Requires libvtk9-dev (sudo apt install libvtk9-dev) and bext_output geogram.
test_remesh_verdict: $(TEST_DIR)/test_remesh_verdict.cpp \
                     GTE/Mathematics/MeshRepair.h \
                     GTE/Mathematics/MeshRemesh.h
	$(CXX) $(CXXFLAGS) $(_GEO_INC) $(BEXT_GTE_INC) $(VTK_INC) \
	    -o test_remesh_verdict \
	    $(TEST_DIR)/test_remesh_verdict.cpp \
	    $(_GEO_LIBS) $(VTK_LIBS) $(LDFLAGS)

# Run the Verdict quality evaluation on GenericTwin (gt.obj)
test_verdict: test_remesh_verdict
	@echo "=== Remesh Quality Evaluation: GTE vs Geogram (VTK Verdict) ==="
	LD_LIBRARY_PATH="$(BEXT_GEO_LIB_DIR):$$LD_LIBRARY_PATH" \
	    ./test_remesh_verdict $(TEST_DIR)/data/gt.obj

# GTE-native mesh quality unit test — no VTK dependency.
# Tests MeshQuality<double> (Verdict-equivalent algorithms implemented in GTE)
# on known analytical cases and an optional OBJ file.
test_mesh_quality: $(TEST_DIR)/test_mesh_quality.cpp \
                   GTE/Mathematics/MeshQuality.h
	$(CXX) $(CXXFLAGS) -o test_mesh_quality \
	    $(TEST_DIR)/test_mesh_quality.cpp $(LDFLAGS)

# Run GTE-native mesh quality unit tests (no external libs needed)
test_quality: test_mesh_quality
	@echo "=== GTE MeshQuality unit tests (no VTK) ==="
	./test_mesh_quality
	@echo ""
	@echo "=== GTE MeshQuality on GenericTwin (if available) ==="
	@test -f $(TEST_DIR)/data/gt.obj && ./test_mesh_quality $(TEST_DIR)/data/gt.obj || \
	    echo "(gt.obj not found, skipping OBJ quality evaluation)"

# GTE MeshQuality vs VTK Verdict per-triangle validation.
# Requires libvtk9-dev (sudo apt install libvtk9-dev).
# Compares every metric for every triangle in the test set and on gt.obj.
test_gtevtk_compare: $(TEST_DIR)/test_gtevtk_compare.cpp \
                     GTE/Mathematics/MeshQuality.h
	$(CXX) $(CXXFLAGS) $(BEXT_GTE_INC) $(VTK_INC) \
	    -o test_gtevtk_compare \
	    $(TEST_DIR)/test_gtevtk_compare.cpp \
	    $(VTK_LIBS) $(LDFLAGS)

# Run the GTE vs VTK Verdict per-triangle comparison (analytical + GenericTwin)
test_gte_vtk: test_gtevtk_compare
	@echo "=== GTE MeshQuality vs VTK Verdict per-triangle comparison ==="
	./test_gtevtk_compare
	@echo ""
	@echo "=== GTE vs VTK on GenericTwin (if available) ==="
	@test -f $(TEST_DIR)/data/gt.obj && ./test_gtevtk_compare $(TEST_DIR)/data/gt.obj || \
	    echo "(gt.obj not found, skipping GenericTwin comparison)"

# Repair + auto-remesh quality validation on GenericTwin.
# Requires bext_output Manifold and geogram.
test_repair_remesh_quality: $(TEST_DIR)/test_repair_remesh_quality.cpp \
                             GTE/Mathematics/MeshRepair.h \
                             GTE/Mathematics/MeshHoleFilling.h \
                             GTE/Mathematics/MeshPreprocessing.h \
                             GTE/Mathematics/MeshRemesh.h \
                             GTE/Mathematics/MeshQuality.h
	$(CXX) $(CXXFLAGS) $(_GEO_INC) $(BEXT_GTE_INC) \
	    -I$(BRLCAD_EXT_INSTALL_DIR)/include \
	    -o test_repair_remesh_quality \
	    $(TEST_DIR)/test_repair_remesh_quality.cpp \
	    $(_GEO_LIBS) -L$(BRLCAD_EXT_INSTALL_DIR)/lib -lmanifold \
	    -Wl,-rpath,$(BRLCAD_EXT_INSTALL_DIR)/lib \
	    -Wl,-rpath,$(BRLCAD_EXT_NOINSTALL_DIR)/lib $(LDFLAGS)

# Run repair + auto-remesh quality pass on GenericTwin shapes
test_repair_quality: test_repair_remesh_quality
	@echo "=== Repair + auto-remesh quality pass on GenericTwin ==="
	LD_LIBRARY_PATH="$(BEXT_GEO_LIB_DIR):$(BRLCAD_EXT_INSTALL_DIR)/lib:$(BRLCAD_EXT_NOINSTALL_DIR)/lib:$$LD_LIBRARY_PATH" \
	    ./test_repair_remesh_quality $(TEST_DIR)/data/gt.obj

# Clean up
clean:
	rm -f $(TARGETS) test_geogram_comparison test_remesh_verdict test_mesh_quality \
	    test_gtevtk_compare test_repair_remesh_quality
	rm -f $(TEST_DIR)/data/*_repaired.obj $(TEST_DIR)/data/*_output.obj

.PHONY: all clean test test_geogram test_geogram_bext test_generic_twin stress generate_test_data build_geogram test_verdict test_quality test_gte_vtk test_repair_quality

# Phase 4 integration test
test_phase4_integration: $(TEST_DIR)/test_phase4_integration.cpp GTE/Mathematics/CVTN.h GTE/Mathematics/MeshRemesh.h GTE/Mathematics/SurfaceRVDN.h
	$(CXX) $(CXXFLAGS) -o test_phase4_integration $(TEST_DIR)/test_phase4_integration.cpp $(LDFLAGS)

# Comprehensive anisotropic remeshing end-to-end test
test_anisotropic_end_to_end: $(TEST_DIR)/test_anisotropic_end_to_end.cpp GTE/Mathematics/MeshRemesh.h GTE/Mathematics/CVTN.h GTE/Mathematics/MeshAnisotropy.h GTE/Mathematics/SurfaceRVDN.h
	$(CXX) $(CXXFLAGS) -o test_anisotropic_end_to_end $(TEST_DIR)/test_anisotropic_end_to_end.cpp $(LDFLAGS)

# BoundaryPolygonRTree correctness and performance test
test_boundary_polygon_rtree: $(TEST_DIR)/test_boundary_polygon_rtree.cpp GTE/Mathematics/BoundaryPolygonRTree.h
	$(CXX) $(CXXFLAGS) -o test_boundary_polygon_rtree $(TEST_DIR)/test_boundary_polygon_rtree.cpp $(LDFLAGS)
