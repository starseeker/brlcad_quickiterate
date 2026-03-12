// test_mesh_quality.cpp — GTE-native mesh quality test (no VTK dependency)
//
// Tests MeshQuality<double> on:
//   1. Known triangles with analytically computable metrics
//   2. A simple closed cube mesh (aggregate statistics)
//   3. An OBJ file (if provided on the command line)
//
// Usage:
//   ./test_mesh_quality [input.obj]
//
// Pass criteria:
//   - Equilateral triangle: AR=1, SJ=1, MinAngle=60, Shape=1, Condition=1 (VTK-normalized)
//   - Degenerate triangle correctly detected (AR very large, SJ=0, Shape=0)
//   - Simple solid cube mesh metrics are within expected ranges

#include <GTE/Mathematics/MeshQuality.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <array>
#include <string>

using namespace gte;

// ---- helpers -----------------------------------------------------------

static bool approx_eq(double a, double b, double tol = 1e-3)
{
    return std::abs(a - b) <= tol * std::max(1.0, std::abs(b));
}

static bool load_obj(std::string const& path,
                     std::vector<std::array<double,3>>& verts,
                     std::vector<std::array<int32_t,3>>& tris)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;
    verts.clear(); tris.clear();
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string tok;
        iss >> tok;
        if (tok == "v") {
            double x, y, z; iss >> x >> y >> z;
            verts.push_back({x, y, z});
        } else if (tok == "f") {
            std::string s0, s1, s2;
            iss >> s0 >> s1 >> s2;
            auto parse = [](std::string const& s) -> int32_t {
                return std::stoi(s.substr(0, s.find('/'))) - 1;
            };
            int32_t i0 = parse(s0), i1 = parse(s1), i2 = parse(s2);
            if (i0 >= 0 && i1 >= 0 && i2 >= 0)
                tris.push_back({i0, i1, i2});
        }
    }
    return !verts.empty() && !tris.empty();
}

// ---- unit tests --------------------------------------------------------

static int test_equilateral_triangle()
{
    std::array<double,3> v0{0.0,                   0.0, 0.0};
    std::array<double,3> v1{1.0,                   0.0, 0.0};
    std::array<double,3> v2{0.5, std::sqrt(3.0)/2.0,  0.0};

    auto m = MeshQuality<double>::ComputeTriangle(v0, v1, v2);

    int failures = 0;
    auto chk = [&](const char* name, double got, double exp) {
        if (!approx_eq(got, exp)) {
            std::cerr << "FAIL equilateral " << name
                      << ": got " << got << " expected " << exp << "\n";
            ++failures;
        }
    };
    chk("AspectRatio",    m.aspectRatio,    1.0);
    chk("ScaledJacobian", m.scaledJacobian, 1.0);  // VTK-normalized: equilateral=1.0
    chk("MinAngle",       m.minAngle,       60.0);
    chk("MaxAngle",       m.maxAngle,       60.0);
    chk("Shape",          m.shape,          1.0);   // VTK-normalized: equilateral=1.0
    chk("Condition",      m.condition,      1.0);   // VTK-normalized: equilateral=1.0
    chk("EdgeRatio",      m.edgeRatio,      1.0);

    if (failures == 0)
        std::cout << "PASS: equilateral triangle metrics correct"
                  << " (AR=1, SJ=1, MinAngle=60, Shape=1, Cond=1) [VTK-normalized]\n";
    return failures;
}

static int test_right_triangle()
{
    std::array<double,3> v0{0.0, 0.0, 0.0};
    std::array<double,3> v1{1.0, 0.0, 0.0};
    std::array<double,3> v2{0.0, 1.0, 0.0};

    auto m = MeshQuality<double>::ComputeTriangle(v0, v1, v2);

    int failures = 0;
    if (m.minAngle < 44.0 || m.minAngle > 46.0) {
        std::cerr << "FAIL right-iso MinAngle: got " << m.minAngle
                  << " (expected ~45)\n"; ++failures;
    }
    if (m.maxAngle < 89.0 || m.maxAngle > 91.0) {
        std::cerr << "FAIL right-iso MaxAngle: got " << m.maxAngle
                  << " (expected ~90)\n"; ++failures;
    }
    if (m.aspectRatio <= 1.0) {
        std::cerr << "FAIL right-iso AR should be > 1, got " << m.aspectRatio
                  << "\n"; ++failures;
    }
    if (m.scaledJacobian <= 0.0 || m.scaledJacobian > 1.001) {
        std::cerr << "FAIL right-iso SJ out of range (expected ~0.8165): " << m.scaledJacobian
                  << "\n"; ++failures;
    }
    // VTK gives 0.81650 for right-isoceles — check it's close
    if (!approx_eq(m.scaledJacobian, 0.81649658, 1e-4)) {
        std::cerr << "FAIL right-iso SJ VTK mismatch: got " << m.scaledJacobian
                  << " expected ~0.8165\n"; ++failures;
    }
    // VTK gives Shape=0.86603, Condition=1.15470 for right-isoceles
    if (!approx_eq(m.shape, 0.86602540, 1e-4)) {
        std::cerr << "FAIL right-iso Shape VTK mismatch: got " << m.shape
                  << " expected ~0.8660\n"; ++failures;
    }
    if (!approx_eq(m.condition, 1.15470054, 1e-4)) {
        std::cerr << "FAIL right-iso Condition VTK mismatch: got " << m.condition
                  << " expected ~1.1547\n"; ++failures;
    }
    if (failures == 0)
        std::cout << "PASS: right-isoceles: MinAngle~45, MaxAngle~90, AR>1, SJ~0.8165 (VTK match)\n";
    return failures;
}

static int test_degenerate_triangle()
{
    std::array<double,3> v0{0.0, 0.0, 0.0};
    std::array<double,3> v1{1.0, 0.0, 0.0};
    std::array<double,3> v2{2.0, 0.0, 0.0};

    auto m = MeshQuality<double>::ComputeTriangle(v0, v1, v2);

    int failures = 0;
    if (m.aspectRatio < 1e14) {
        std::cerr << "FAIL degenerate AR should be INF, got "
                  << m.aspectRatio << "\n"; ++failures;
    }
    if (m.scaledJacobian != 0.0) {
        std::cerr << "FAIL degenerate SJ should be 0, got "
                  << m.scaledJacobian << "\n"; ++failures;
    }
    if (m.shape != 0.0) {
        std::cerr << "FAIL degenerate Shape should be 0, got "
                  << m.shape << "\n"; ++failures;
    }
    if (m.condition < 1e14) {
        std::cerr << "FAIL degenerate Condition should be INF, got "
                  << m.condition << "\n"; ++failures;
    }
    if (failures == 0)
        std::cout << "PASS: degenerate (collinear) triangle correctly detected\n";
    return failures;
}

static int test_mesh_metrics_cube()
{
    std::vector<std::array<double,3>> verts = {
        {0,0,0},{1,0,0},{1,1,0},{0,1,0},
        {0,0,1},{1,0,1},{1,1,1},{0,1,1}
    };
    std::vector<std::array<int32_t,3>> tris = {
        {0,1,2},{0,2,3},
        {4,6,5},{4,7,6},
        {0,5,1},{0,4,5},
        {2,6,7},{2,7,3},
        {0,3,7},{0,7,4},
        {1,5,6},{1,6,2}
    };

    auto mm = MeshQuality<double>::ComputeMeshMetrics(verts, tris);

    int failures = 0;
    if (mm.totalTriangles != 12) {
        std::cerr << "FAIL cube totalTriangles=" << mm.totalTriangles
                  << " (expected 12)\n"; ++failures;
    }
    if (mm.invertedTriangles != 0) {
        std::cerr << "FAIL cube has " << mm.invertedTriangles
                  << " inverted triangles\n"; ++failures;
    }
    if (mm.aspectRatio.median < 1.3 || mm.aspectRatio.median > 1.5) {
        std::cerr << "FAIL cube AR median " << mm.aspectRatio.median
                  << " out of range [1.3, 1.5]\n"; ++failures;
    }
    if (mm.minAngle.median < 40.0 || mm.minAngle.median > 50.0) {
        std::cerr << "FAIL cube MinAngle median " << mm.minAngle.median
                  << " out of range [40, 50]\n"; ++failures;
    }
    if (mm.poorAspect != 0) {
        std::cerr << "FAIL cube has " << mm.poorAspect
                  << " poor-aspect triangles (AR>3)\n"; ++failures;
    }
    if (failures == 0) {
        std::cout << "PASS: cube mesh aggregate metrics"
                  << " (AR_med=" << std::fixed << std::setprecision(3)
                  << mm.aspectRatio.median
                  << " MinAngle_med=" << mm.minAngle.median << ")\n";
    }
    return failures;
}

// ---- per-mesh quality report (used when an OBJ is provided) ------------

static void print_metrics(std::string const& label,
                          MeshQuality<double>::MeshMetrics const& mm)
{
    std::cout << "\n--- " << label << " ---\n";
    std::cout << "  Triangles: " << mm.totalTriangles << "\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Metric               Mean     Median   Min      Max\n";
    std::cout << "  ----------------------------------------------------------\n";
    auto row = [](std::string const& name,
                  MeshQuality<double>::MetricStats const& s) {
        std::cout << "  " << std::left << std::setw(20) << name
                  << std::right << std::setw(8)  << s.mean
                  << std::setw(9)  << s.median
                  << std::setw(9)  << s.min_val
                  << std::setw(9)  << s.max_val << "\n";
    };
    row("Aspect Ratio",    mm.aspectRatio);
    row("Scaled Jacobian", mm.scaledJacobian);
    row("Min Angle (deg)", mm.minAngle);
    row("Max Angle (deg)", mm.maxAngle);
    row("Shape",           mm.shape);
    row("Condition",       mm.condition);
    row("Edge Ratio",      mm.edgeRatio);
    std::cout << "  Poor quality triangles:\n";
    auto pct = [&](size_t n) {
        return mm.totalTriangles ? 100.0*n/(double)mm.totalTriangles : 0.0;
    };
    std::cout << std::setprecision(1);
    std::cout << "    Aspect ratio > 3.0  : " << mm.poorAspect
              << " (" << pct(mm.poorAspect) << "%)\n";
    std::cout << "    Inverted (SJ < 0)   : " << mm.invertedTriangles
              << " (" << pct(mm.invertedTriangles) << "%)\n";
    std::cout << "    Min angle < 20 deg  : " << mm.smallAngle
              << " (" << pct(mm.smallAngle) << "%)\n";
    std::cout << "    Max angle > 120 deg : " << mm.largeAngle
              << " (" << pct(mm.largeAngle) << "%)\n";
    std::cout << "\n  Verdict ideal values (VTK-normalized): AR=1, SJ=1, MinAngle=60, MaxAngle=60\n"
              << "                                          Shape=1, Condition=1, EdgeRatio=1\n";
}

// ---- main --------------------------------------------------------------

int main(int argc, char* argv[])
{
    std::cout << "=== GTE MeshQuality (Verdict-equivalent) Tests ===\n\n";
    std::cout << "  Metrics match Verdict/VTK vtkMeshQuality formulas (equilateral=1.0 for all):\n"
              << "    Aspect Ratio    = max_edge * perimeter / (4*sqrt(3)*area)\n"
              << "    Scaled Jacobian = min(sin(A),sin(B),sin(C)) / sin(60) = 4*area/(sqrt(3)*max_edge_prod)\n"
              << "    Shape           = 4*sqrt(3)*area / (l0^2+l1^2+l2^2)\n"
              << "    Condition       = (l0^2+l1^2+l2^2) / (4*sqrt(3)*area) = 1/Shape\n\n";

    int failures = 0;
    failures += test_equilateral_triangle();
    failures += test_right_triangle();
    failures += test_degenerate_triangle();
    failures += test_mesh_metrics_cube();

    std::cout << "\n=== Unit Tests: " << (failures == 0 ? "ALL PASS" : "FAILURES DETECTED")
              << " (" << failures << " failure(s)) ===\n";

    if (argc >= 2) {
        std::string path = argv[1];
        std::cout << "\n=== Mesh quality evaluation for: " << path << " ===\n";

        std::vector<std::array<double,3>> verts;
        std::vector<std::array<int32_t,3>> tris;
        if (!load_obj(path, verts, tris)) {
            std::cerr << "Failed to load " << path << "\n";
            return failures ? 1 : 0;
        }

        auto mm = MeshQuality<double>::ComputeMeshMetrics(verts, tris);
        print_metrics(path, mm);
    }

    return failures ? 1 : 0;
}
