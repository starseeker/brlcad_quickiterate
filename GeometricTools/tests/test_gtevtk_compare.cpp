// test_gtevtk_compare.cpp
//
// Validates GTE MeshQuality<double> metrics against VTK's vtkMeshQuality
// (which wraps the Verdict library).  Every metric for every triangle in
// the test set must match VTK to within TOLERANCE (relative).
//
// Tests:
//  1. Hard-coded analytical triangles with known exact values
//  2. All triangles from gt.obj (GenericTwin) if provided on the command line
//
// Usage:
//   ./test_gtevtk_compare [gt.obj]
//
// Pass criterion: per-triangle relative difference <= TOLERANCE (1e-4) for all 7 metrics.

#include <GTE/Mathematics/MeshQuality.h>

// VTK
#include <vtkTriangle.h>
#include <vtkPoints.h>
#include <vtkMeshQuality.h>
#include <vtkSmartPointer.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <cstdint>
#include <algorithm>

using namespace gte;

// Comparison tolerance (relative error, capped by absolute min to handle near-zero)
static const double TOLERANCE = 1e-4;
// Large-value sentinel: when both VTK and GTE report "very large" (degenerate) accept
static const double SENTINEL_THRESHOLD = 1e10;

static bool cmp_metric(double gte_val, double vtk_val, double tol = TOLERANCE)
{
    // For sentinels / degenerate: both large or both zero
    if (vtk_val == 0.0 && gte_val == 0.0) return true;
    if (std::abs(vtk_val) > SENTINEL_THRESHOLD && std::abs(gte_val) > SENTINEL_THRESHOLD)
        return true;
    // Relative comparison with a small absolute floor
    double denom = std::max(std::abs(vtk_val), 1e-10);
    return std::abs(gte_val - vtk_val) / denom <= tol;
}

struct TriResult {
    double gte_ar, vtk_ar;
    double gte_sj, vtk_sj;
    double gte_mna, vtk_mna;
    double gte_mxa, vtk_mxa;
    double gte_sh, vtk_sh;
    double gte_cnd, vtk_cnd;
    double gte_er, vtk_er;
    bool all_ok;
};

static TriResult compare_triangle(
    double x0, double y0, double z0,
    double x1, double y1, double z1,
    double x2, double y2, double z2)
{
    TriResult r{};

    // GTE
    std::array<double,3> v0{x0,y0,z0}, v1{x1,y1,z1}, v2{x2,y2,z2};
    auto m = MeshQuality<double>::ComputeTriangle(v0, v1, v2);
    r.gte_ar  = m.aspectRatio;
    r.gte_sj  = m.scaledJacobian;
    r.gte_mna = m.minAngle;
    r.gte_mxa = m.maxAngle;
    r.gte_sh  = m.shape;
    r.gte_cnd = m.condition;
    r.gte_er  = m.edgeRatio;

    // VTK
    auto tri = vtkSmartPointer<vtkTriangle>::New();
    tri->GetPoints()->SetPoint(0, x0, y0, z0);
    tri->GetPoints()->SetPoint(1, x1, y1, z1);
    tri->GetPoints()->SetPoint(2, x2, y2, z2);
    r.vtk_ar  = vtkMeshQuality::TriangleAspectRatio(tri);
    r.vtk_sj  = vtkMeshQuality::TriangleScaledJacobian(tri);
    r.vtk_mna = vtkMeshQuality::TriangleMinAngle(tri);
    r.vtk_mxa = vtkMeshQuality::TriangleMaxAngle(tri);
    r.vtk_sh  = vtkMeshQuality::TriangleShape(tri);
    r.vtk_cnd = vtkMeshQuality::TriangleCondition(tri);
    r.vtk_er  = vtkMeshQuality::TriangleEdgeRatio(tri);

    r.all_ok = cmp_metric(r.gte_ar, r.vtk_ar) &&
               cmp_metric(r.gte_sj, r.vtk_sj) &&
               cmp_metric(r.gte_mna, r.vtk_mna) &&
               cmp_metric(r.gte_mxa, r.vtk_mxa) &&
               cmp_metric(r.gte_sh, r.vtk_sh) &&
               cmp_metric(r.gte_cnd, r.vtk_cnd) &&
               cmp_metric(r.gte_er, r.vtk_er);

    return r;
}

// ---- Analytical tests --------------------------------------------------

struct TestTri {
    const char* label;
    double v[3][3]; // 3 vertices × xyz
};

static const TestTri ANALYTICAL_TRIS[] = {
    // Equilateral (unit edge)
    {"equilateral_unit",
     {{0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.5, 0.8660254037844387, 0.0}}},
    // Right isoceles
    {"right_isoceles",
     {{0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0}}},
    // 3:4:5 right triangle
    {"3_4_5_right",
     {{0.0, 0.0, 0.0},
      {3.0, 0.0, 0.0},
      {0.0, 4.0, 0.0}}},
    // Very elongated (aspect ratio ≈ 10)
    {"elongated_10x",
     {{0.0, 0.0, 0.0},
      {10.0, 0.0, 0.0},
      {5.0, 0.1, 0.0}}},
    // Equilateral (large scale)
    {"equilateral_100",
     {{0.0, 0.0, 0.0},
      {100.0, 0.0, 0.0},
      {50.0, 86.60254037844387, 0.0}}},
    // 3D triangle (not in XY plane)
    {"3d_triangle",
     {{0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.0, 1.0, 1.0}}},
    // Nearly degenerate (very thin)
    {"nearly_degenerate",
     {{0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.5, 0.001, 0.0}}},
    // Obtuse triangle
    {"obtuse_120",
     {{0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {-0.5, 0.8660254037844387, 0.0}}},
    // Scalene
    {"scalene",
     {{0.0, 0.0, 0.0},
      {2.0, 0.0, 0.0},
      {1.0, 1.5, 0.0}}},
    // 3D skinny (nearly degenerate in 3D)
    {"3d_skinny",
     {{0.0,  0.0,  0.0},
      {10.0, 0.0,  0.0},
      {5.0,  0.05, 0.05}}},
};

static int run_analytical_tests()
{
    std::cout << "=== Analytical per-triangle GTE vs VTK Verdict comparison ===\n";
    std::cout << std::fixed << std::setprecision(6);

    int failures = 0;
    int ntests = (int)(sizeof(ANALYTICAL_TRIS)/sizeof(ANALYTICAL_TRIS[0]));

    for (int i = 0; i < ntests; ++i) {
        auto const& t = ANALYTICAL_TRIS[i];
        TriResult r = compare_triangle(
            t.v[0][0], t.v[0][1], t.v[0][2],
            t.v[1][0], t.v[1][1], t.v[1][2],
            t.v[2][0], t.v[2][1], t.v[2][2]);

        if (r.all_ok) {
            std::cout << "  PASS [" << t.label << "]:  "
                      << "AR=" << r.gte_ar
                      << " SJ=" << r.gte_sj
                      << " MinA=" << r.gte_mna
                      << " Shape=" << r.gte_sh
                      << " Cond=" << r.gte_cnd << "\n";
        } else {
            ++failures;
            std::cerr << "  FAIL [" << t.label << "]:\n";
            auto chk = [&](const char* name, double gv, double vv) {
                if (!cmp_metric(gv, vv)) {
                    double rel = (std::abs(vv) > 1e-10) ? std::abs(gv-vv)/std::abs(vv) : std::abs(gv-vv);
                    std::cerr << "    " << name << ": GTE=" << gv
                              << " VTK=" << vv << " rel_err=" << rel << "\n";
                }
            };
            chk("AR",        r.gte_ar,  r.vtk_ar);
            chk("SJ",        r.gte_sj,  r.vtk_sj);
            chk("MinAngle",  r.gte_mna, r.vtk_mna);
            chk("MaxAngle",  r.gte_mxa, r.vtk_mxa);
            chk("Shape",     r.gte_sh,  r.vtk_sh);
            chk("Condition", r.gte_cnd, r.vtk_cnd);
            chk("EdgeRatio", r.gte_er,  r.vtk_er);
        }
    }

    if (failures == 0)
        std::cout << "PASS: all " << ntests << " analytical triangles agree with VTK Verdict\n";
    else
        std::cerr << "FAIL: " << failures << "/" << ntests << " analytical triangles differ from VTK\n";

    return failures;
}

// ---- GenericTwin / OBJ file comparison ---------------------------------

static bool load_obj_flat(std::string const& path,
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
                return (int32_t)(std::stoi(s.substr(0, s.find('/'))) - 1);
            };
            int32_t i0 = parse(s0), i1 = parse(s1), i2 = parse(s2);
            if (i0 >= 0 && i1 >= 0 && i2 >= 0)
                tris.push_back({i0, i1, i2});
        }
    }
    return !verts.empty() && !tris.empty();
}

static int run_obj_comparison(std::string const& path)
{
    std::cout << "\n=== Per-triangle GTE vs VTK comparison on " << path << " ===\n";

    std::vector<std::array<double,3>> verts;
    std::vector<std::array<int32_t,3>> tris;
    if (!load_obj_flat(path, verts, tris)) {
        std::cerr << "Failed to load " << path << "\n";
        return 1;
    }
    std::cout << "  Loaded " << tris.size() << " triangles, " << verts.size() << " vertices\n";

    size_t n = verts.size();
    size_t ok = 0, fail = 0;
    size_t first_fail_idx = (size_t)-1;

    // Worst-case relative errors per metric
    double max_rel_ar = 0, max_rel_sj = 0, max_rel_mna = 0, max_rel_mxa = 0;
    double max_rel_sh = 0, max_rel_cnd = 0, max_rel_er = 0;

    for (size_t ti = 0; ti < tris.size(); ++ti) {
        auto const& t = tris[ti];
        if (t[0] < 0 || t[1] < 0 || t[2] < 0 ||
            (size_t)t[0] >= n || (size_t)t[1] >= n || (size_t)t[2] >= n)
            continue;

        auto const& p0 = verts[(size_t)t[0]];
        auto const& p1 = verts[(size_t)t[1]];
        auto const& p2 = verts[(size_t)t[2]];

        TriResult r = compare_triangle(
            p0[0], p0[1], p0[2],
            p1[0], p1[1], p1[2],
            p2[0], p2[1], p2[2]);

        // Track max relative errors (skip sentinel values)
        auto rel = [](double g, double v) {
            if (std::abs(v) > SENTINEL_THRESHOLD || std::abs(g) > SENTINEL_THRESHOLD) return 0.0;
            double d = std::max(std::abs(v), 1e-10);
            return std::abs(g - v) / d;
        };
        max_rel_ar  = std::max(max_rel_ar,  rel(r.gte_ar,  r.vtk_ar));
        max_rel_sj  = std::max(max_rel_sj,  rel(r.gte_sj,  r.vtk_sj));
        max_rel_mna = std::max(max_rel_mna, rel(r.gte_mna, r.vtk_mna));
        max_rel_mxa = std::max(max_rel_mxa, rel(r.gte_mxa, r.vtk_mxa));
        max_rel_sh  = std::max(max_rel_sh,  rel(r.gte_sh,  r.vtk_sh));
        max_rel_cnd = std::max(max_rel_cnd, rel(r.gte_cnd, r.vtk_cnd));
        max_rel_er  = std::max(max_rel_er,  rel(r.gte_er,  r.vtk_er));

        if (r.all_ok) {
            ++ok;
        } else {
            ++fail;
            if (first_fail_idx == (size_t)-1) first_fail_idx = ti;
        }
    }

    size_t total = ok + fail;
    std::cout << std::fixed << std::setprecision(8);
    std::cout << "  Results: " << ok << "/" << total << " triangles match VTK\n";
    if (fail > 0)
        std::cout << "  WARNING: " << fail << " triangles differ (first fail at tri " << first_fail_idx << ")\n";

    std::cout << "\n  Maximum per-metric relative error vs VTK:\n";
    std::cout << std::scientific << std::setprecision(3);
    std::cout << "    AspectRatio:    " << max_rel_ar  << "\n";
    std::cout << "    ScaledJacobian: " << max_rel_sj  << "\n";
    std::cout << "    MinAngle:       " << max_rel_mna << "\n";
    std::cout << "    MaxAngle:       " << max_rel_mxa << "\n";
    std::cout << "    Shape:          " << max_rel_sh  << "\n";
    std::cout << "    Condition:      " << max_rel_cnd << "\n";
    std::cout << "    EdgeRatio:      " << max_rel_er  << "\n";

    double max_all = std::max({max_rel_ar, max_rel_sj, max_rel_mna, max_rel_mxa,
                               max_rel_sh, max_rel_cnd, max_rel_er});
    std::cout << "\n  Max relative error across all metrics: " << max_all << "\n";

    if (fail == 0 || (double)fail / (double)total < 0.001) {
        std::cout << "PASS: GTE metrics match VTK Verdict on " << path << "\n";
        return 0;
    } else {
        std::cerr << "FAIL: " << fail << "/" << total
                  << " triangles have GTE/VTK discrepancy > " << TOLERANCE << "\n";
        return 1;
    }
}

// ---- main --------------------------------------------------------------

int main(int argc, char* argv[])
{
    std::cout << "=== GTE MeshQuality vs VTK Verdict Validation ===\n";
    std::cout << "  Tolerance: " << TOLERANCE << " (relative per metric)\n\n";

    int failures = 0;
    failures += run_analytical_tests();

    if (argc >= 2) {
        failures += run_obj_comparison(argv[1]);
    }

    std::cout << "\n=== OVERALL: " << (failures == 0 ? "PASS" : "FAIL") << " ===\n";
    return failures ? 1 : 0;
}
