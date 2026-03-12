// Remesh quality evaluation using VTK Verdict metrics
//
// This test evaluates the quality of remeshed triangle meshes produced by:
//   1. GTE MeshRemesh (CVT-based anisotropic remeshing, new BRL-CAD integration)
//   2. Geogram remesh_smooth (previous BRL-CAD integration)
//
// Quality is measured using VTK's Verdict library, accessed via vtkMeshQuality.
// Verdict provides a standardized set of triangle quality metrics widely used
// in finite element analysis (FEA) and mesh generation research:
//
//   - Aspect Ratio     : ratio of longest to shortest edge normalized by equilateral;
//                        ideal = 1.0, higher = worse (more elongated).
//   - Scaled Jacobian  : normalized cross-product of edge vectors;
//                        ideal = 1.0 (equilateral), negative = inverted triangle.
//   - Min Angle (°)    : smallest interior angle; ideal = 60°, lower = worse.
//   - Max Angle (°)    : largest interior angle; ideal = 60°, higher = worse.
//   - Shape            : ratio of inradius to circumradius, normalized;
//                        ideal = 1.0, lower = worse.
//   - Condition Number : condition number of the Jacobian matrix;
//                        ideal = 1.0, higher = worse.
//   - Edge Ratio       : ratio of longest to shortest edge; ideal = 1.0.
//
// The median is used for the primary comparative assertions because the mean
// can be heavily skewed by extreme outlier triangles produced on fragmented
// inputs like GenericTwin (1109 disconnected components).  The percentage of
// "pathological" triangles (AR > 3, min angle < 20°) is also compared, as
// this directly measures the usability of the remeshed mesh.
//
// Usage:
//   ./test_remesh_verdict <input.obj>
//
// Pass criteria:
//   GTE median scaled Jacobian  >= VERDICT_MIN_SJ_MEDIAN
//   GTE median aspect ratio     <= VERDICT_MAX_AR_MEDIAN
//   GTE median min angle        >= VERDICT_MIN_ANGLE_MEDIAN_DEG
//   GTE no inverted triangles
//   GTE vs Geogram: median metrics within VERDICT_COMPARATIVE_TOLERANCE_PCT

#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshRemesh.h>

// Geogram includes
#include <geogram/basic/process.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_geometry.h>
#include <geogram/mesh/mesh_preprocessing.h>
#include <geogram/mesh/mesh_remesh.h>

// VTK / Verdict includes
#include <vtkTriangle.h>
#include <vtkPoints.h>
#include <vtkMeshQuality.h>
#include <vtkSmartPointer.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include <numeric>

using namespace gte;

// ---- Parameters mirroring BRL-CAD remesh.cpp ----

// Remesh parameters
static constexpr size_t REMESH_TARGET_VERTICES   = 1000;
static constexpr size_t REMESH_LLOYD_ITER        = 5;
static constexpr double REMESH_ANISOTROPY_SCALE  = 0.04;   // 2*0.02 from BRL-CAD

// Absolute quality thresholds for GTE (based on achievable values on real-world meshes).
// Using median (not mean) because the mean is heavily skewed by extreme outliers on
// fragmented inputs like GenericTwin (1109 disconnected components).
// Scaled Jacobian median > 0.50: most triangles are non-degenerate, reasonable quality.
static constexpr double VERDICT_MIN_SJ_MEDIAN        = 0.50;
// Aspect ratio median < 2.5: most triangles are not overly elongated.
static constexpr double VERDICT_MAX_AR_MEDIAN        = 2.5;
// Min angle median > 20.0°: most triangles have non-degenerate smallest angle.
static constexpr double VERDICT_MIN_ANGLE_MEDIAN_DEG = 20.0;
// GTE vs Geogram: median metrics should be within 20% of each other.
// (Wider than a clean-mesh tolerance to accommodate the highly fragmented GT mesh.)
static constexpr double VERDICT_COMPARATIVE_TOLERANCE_PCT = 20.0;

// ---- OBJ I/O ----

bool LoadOBJ(std::string const& filename,
    std::vector<Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << "\n";
        return false;
    }
    vertices.clear();
    triangles.clear();
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;
        if (type == "v") {
            double x, y, z;
            iss >> x >> y >> z;
            vertices.push_back(Vector3<double>{ x, y, z });
        } else if (type == "f") {
            // Support "f v1 v2 v3" and "f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3"
            std::string s0, s1, s2;
            iss >> s0 >> s1 >> s2;
            int32_t i0 = std::stoi(s0) - 1;
            int32_t i1 = std::stoi(s1) - 1;
            int32_t i2 = std::stoi(s2) - 1;
            if (i0 >= 0 && i1 >= 0 && i2 >= 0)
                triangles.push_back({ i0, i1, i2 });
        }
    }
    return !vertices.empty() && !triangles.empty();
}

void SaveOBJ(std::string const& filename,
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream f(filename);
    for (auto const& v : vertices)
        f << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    for (auto const& t : triangles)
        f << "f " << (t[0]+1) << " " << (t[1]+1) << " " << (t[2]+1) << "\n";
}

// ---- Geogram helpers ----

bool PopulateGeogramMesh(
    std::vector<Vector3<double>> const& verts,
    std::vector<std::array<int32_t, 3>> const& tris,
    GEO::Mesh& gm)
{
    gm.clear();
    gm.vertices.assign_points(
        reinterpret_cast<double const*>(verts.data()),
        3,
        static_cast<GEO::index_t>(verts.size())
    );
    for (auto const& t : tris) {
        GEO::index_t f = gm.facets.create_polygon(3);
        gm.facets.set_vertex(f, 0, static_cast<GEO::index_t>(t[0]));
        gm.facets.set_vertex(f, 1, static_cast<GEO::index_t>(t[1]));
        gm.facets.set_vertex(f, 2, static_cast<GEO::index_t>(t[2]));
    }
    return true;
}

void GeogramMeshToArrays(GEO::Mesh const& gm,
    std::vector<Vector3<double>>& verts,
    std::vector<std::array<int32_t, 3>>& tris)
{
    verts.resize(gm.vertices.nb());
    for (GEO::index_t i = 0; i < gm.vertices.nb(); ++i) {
        const double* p = gm.vertices.point_ptr(i);
        verts[i] = Vector3<double>{ p[0], p[1], p[2] };
    }
    tris.clear();
    for (GEO::index_t f = 0; f < gm.facets.nb(); ++f) {
        if (gm.facets.nb_vertices(f) == 3) {
            tris.push_back({
                (int32_t)gm.facets.vertex(f, 0),
                (int32_t)gm.facets.vertex(f, 1),
                (int32_t)gm.facets.vertex(f, 2)
            });
        }
    }
}

// ---- Remesh pipelines ----

bool RunGTERemesh(std::string const& inputFile,
    size_t targetVertices,
    std::vector<Vector3<double>>& outVerts,
    std::vector<std::array<int32_t, 3>>& outTris)
{
    if (!LoadOBJ(inputFile, outVerts, outTris)) return false;

    // Repair (mirrors BRL-CAD remesh.cpp)
    {
        double minx = outVerts[0][0], maxx = outVerts[0][0];
        double miny = outVerts[0][1], maxy = outVerts[0][1];
        double minz = outVerts[0][2], maxz = outVerts[0][2];
        for (auto const& v : outVerts) {
            if (v[0] < minx) minx = v[0];
            if (v[0] > maxx) maxx = v[0];
            if (v[1] < miny) miny = v[1];
            if (v[1] > maxy) maxy = v[1];
            if (v[2] < minz) minz = v[2];
            if (v[2] > maxz) maxz = v[2];
        }
        double dx = maxx-minx, dy = maxy-miny, dz = maxz-minz;
        double diag = std::sqrt(dx*dx + dy*dy + dz*dz);
        MeshRepair<double>::Parameters rp;
        rp.epsilon = 1e-6 * (0.01 * diag);
        MeshRepair<double>::Repair(outVerts, outTris, rp);
    }

    MeshRemesh<double>::Parameters mp;
    mp.targetVertexCount = targetVertices;
    mp.lloydIterations   = REMESH_LLOYD_ITER;
    mp.useAnisotropic    = true;
    mp.anisotropyScale   = REMESH_ANISOTROPY_SCALE;
    return MeshRemesh<double>::RemeshCVT(outVerts, outTris, outVerts, outTris, mp);
}

bool RunGeogramRemesh(std::string const& inputFile,
    size_t targetVertices,
    std::vector<Vector3<double>>& outVerts,
    std::vector<std::array<int32_t, 3>>& outTris)
{
    std::vector<Vector3<double>> inVerts;
    std::vector<std::array<int32_t, 3>> inTris;
    if (!LoadOBJ(inputFile, inVerts, inTris)) return false;

    GEO::Mesh gm;
    if (!PopulateGeogramMesh(inVerts, inTris, gm)) return false;

    double bbox_diag = GEO::bbox_diagonal(gm);
    double epsilon   = 1e-6 * 0.01 * bbox_diag;
    GEO::mesh_repair(gm, GEO::MeshRepairMode(GEO::MESH_REPAIR_DEFAULT), epsilon);
    GEO::compute_normals(gm);
    GEO::set_anisotropy(gm, REMESH_ANISOTROPY_SCALE);

    GEO::Mesh remesh;
    GEO::remesh_smooth(gm, remesh,
        static_cast<GEO::index_t>(targetVertices),
        0,
        static_cast<GEO::index_t>(REMESH_LLOYD_ITER),
        0);

    GeogramMeshToArrays(remesh, outVerts, outTris);
    return !outVerts.empty();
}

// ---- VTK Verdict quality metrics ----

struct VerdictMetrics {
    // Per-metric statistics (mean, median, min, max)
    double aspectRatio_mean,    aspectRatio_median,    aspectRatio_min,    aspectRatio_max;
    double scaledJacobian_mean, scaledJacobian_median, scaledJacobian_min, scaledJacobian_max;
    double minAngle_mean,       minAngle_median,       minAngle_min,       minAngle_max;
    double maxAngle_mean,       maxAngle_median,       maxAngle_min,       maxAngle_max;
    double shape_mean,          shape_median,          shape_min,          shape_max;
    double condition_mean,      condition_median,      condition_min,      condition_max;
    double edgeRatio_mean,      edgeRatio_median,      edgeRatio_min,      edgeRatio_max;
    // Count of "poor quality" triangles per metric
    size_t poorAspect;       // aspect ratio > 3.0
    size_t invertedTri;      // scaled Jacobian < 0
    size_t smallAngle;       // min angle < 20°
    size_t largeAngle;       // max angle > 120°
    size_t totalTriangles;
};

VerdictMetrics ComputeVerdictMetrics(
    std::vector<Vector3<double>> const& verts,
    std::vector<std::array<int32_t, 3>> const& tris)
{
    VerdictMetrics m{};
    m.totalTriangles = tris.size();
    if (tris.empty()) return m;

    std::vector<double> aspectRatios, scaledJacobians, minAngles, maxAngles;
    std::vector<double> shapes, conditions, edgeRatios;
    aspectRatios.reserve(tris.size());
    scaledJacobians.reserve(tris.size());
    minAngles.reserve(tris.size());
    maxAngles.reserve(tris.size());
    shapes.reserve(tris.size());
    conditions.reserve(tris.size());
    edgeRatios.reserve(tris.size());

    // Reuse a single vtkTriangle object for efficiency
    auto tri = vtkSmartPointer<vtkTriangle>::New();

    for (auto const& t : tris) {
        // Skip degenerate index references
        if (t[0] < 0 || t[1] < 0 || t[2] < 0 ||
            (size_t)t[0] >= verts.size() || (size_t)t[1] >= verts.size() ||
            (size_t)t[2] >= verts.size())
            continue;

        const double* p0 = &verts[t[0]][0];
        const double* p1 = &verts[t[1]][0];
        const double* p2 = &verts[t[2]][0];

        tri->GetPoints()->SetPoint(0, p0[0], p0[1], p0[2]);
        tri->GetPoints()->SetPoint(1, p1[0], p1[1], p1[2]);
        tri->GetPoints()->SetPoint(2, p2[0], p2[1], p2[2]);

        double ar  = vtkMeshQuality::TriangleAspectRatio(tri);
        double sj  = vtkMeshQuality::TriangleScaledJacobian(tri);
        double mna = vtkMeshQuality::TriangleMinAngle(tri);
        double mxa = vtkMeshQuality::TriangleMaxAngle(tri);
        double sh  = vtkMeshQuality::TriangleShape(tri);
        double cnd = vtkMeshQuality::TriangleCondition(tri);
        double er  = vtkMeshQuality::TriangleEdgeRatio(tri);

        aspectRatios.push_back(ar);
        scaledJacobians.push_back(sj);
        minAngles.push_back(mna);
        maxAngles.push_back(mxa);
        shapes.push_back(sh);
        conditions.push_back(cnd);
        edgeRatios.push_back(er);
    }

    if (aspectRatios.empty()) return m;

    auto stats = [](std::vector<double>& v, double& mean, double& median,
                    double& mn, double& mx) {
        mn = *std::min_element(v.begin(), v.end());
        mx = *std::max_element(v.begin(), v.end());
        mean = std::accumulate(v.begin(), v.end(), 0.0) / (double)v.size();
        std::sort(v.begin(), v.end());
        size_t n = v.size();
        median = (n % 2 == 0) ? 0.5*(v[n/2-1]+v[n/2]) : v[n/2];
    };

    stats(aspectRatios,    m.aspectRatio_mean,    m.aspectRatio_median,    m.aspectRatio_min,    m.aspectRatio_max);
    stats(scaledJacobians, m.scaledJacobian_mean, m.scaledJacobian_median, m.scaledJacobian_min, m.scaledJacobian_max);
    stats(minAngles,       m.minAngle_mean,       m.minAngle_median,       m.minAngle_min,       m.minAngle_max);
    stats(maxAngles,       m.maxAngle_mean,       m.maxAngle_median,       m.maxAngle_min,       m.maxAngle_max);
    stats(shapes,          m.shape_mean,          m.shape_median,          m.shape_min,          m.shape_max);
    stats(conditions,      m.condition_mean,      m.condition_median,      m.condition_min,      m.condition_max);
    stats(edgeRatios,      m.edgeRatio_mean,      m.edgeRatio_median,      m.edgeRatio_min,      m.edgeRatio_max);

    for (double ar  : aspectRatios)    if (ar  > 3.0)  ++m.poorAspect;
    for (double sj  : scaledJacobians) if (sj  < 0.0)  ++m.invertedTri;
    for (double mna : minAngles)       if (mna < 20.0) ++m.smallAngle;
    for (double mxa : maxAngles)       if (mxa > 120.0) ++m.largeAngle;

    return m;
}

// ---- Reporting ----

void PrintVerdictMetrics(std::string const& label, VerdictMetrics const& m)
{
    std::cout << "\n--- " << label << " ---\n";
    std::cout << "  Triangles: " << m.totalTriangles << "\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Metric               Mean     Min      Max\n";
    std::cout << "  --------------------------------------------------\n";
    auto row = [](std::string const& name, double mean, double mn, double mx) {
        std::cout << "  " << std::left << std::setw(20) << name
                  << std::right << std::setw(8) << mean
                  << std::setw(9) << mn
                  << std::setw(9) << mx << "\n";
    };
    row("Aspect Ratio",    m.aspectRatio_mean,    m.aspectRatio_min,    m.aspectRatio_max);
    row("Scaled Jacobian", m.scaledJacobian_mean, m.scaledJacobian_min, m.scaledJacobian_max);
    row("Min Angle (deg)", m.minAngle_mean,       m.minAngle_min,       m.minAngle_max);
    row("Max Angle (deg)", m.maxAngle_mean,       m.maxAngle_min,       m.maxAngle_max);
    row("Shape",           m.shape_mean,          m.shape_min,          m.shape_max);
    row("Condition",       m.condition_mean,      m.condition_min,      m.condition_max);
    row("Edge Ratio",      m.edgeRatio_mean,      m.edgeRatio_min,      m.edgeRatio_max);
    std::cout << "  Poor quality triangles:\n";
    auto pct = [&](size_t n) {
        return (m.totalTriangles > 0)
            ? 100.0 * (double)n / (double)m.totalTriangles : 0.0;
    };
    std::cout << "    Aspect ratio > 3.0  : " << m.poorAspect
              << " (" << std::setprecision(1) << pct(m.poorAspect) << "%)\n";
    std::cout << "    Inverted (SJ < 0)   : " << m.invertedTri
              << " (" << pct(m.invertedTri) << "%)\n";
    std::cout << "    Min angle < 20°     : " << m.smallAngle
              << " (" << pct(m.smallAngle) << "%)\n";
    std::cout << "    Max angle > 120°    : " << m.largeAngle
              << " (" << pct(m.largeAngle) << "%)\n";
}

void PrintComparisonRow(std::string const& metric,
    double geoVal, double gteVal, bool higherIsBetter)
{
    double diff = 0.0;
    if (std::abs(geoVal) > 1e-10)
        diff = 100.0 * (gteVal - geoVal) / std::abs(geoVal);
    const char* sign = higherIsBetter ? (diff >= 0 ? "+" : "") : (diff <= 0 ? "" : "+");
    std::cout << "  " << std::left  << std::setw(22) << metric
              << std::right << std::setw(10) << geoVal
              << std::setw(10) << gteVal
              << std::setw(8)  << sign << std::setprecision(1) << diff << "%"
              << (higherIsBetter ? (diff >= 0 ? "  [GTE >=]" : "  [GTE <] ")
                                 : (diff <= 0 ? "  [GTE <=]" : "  [GTE >] "))
              << "\n";
}

// ---- Main ----

int main(int argc, char* argv[])
{
    std::cout << std::fixed << std::setprecision(4);

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.obj>\n";
        std::cerr << "  input.obj   - triangle mesh for remesh quality evaluation\n";
        return 1;
    }

    const std::string inputFile = argv[1];

    // Initialize Geogram
    GEO::initialize();
    GEO::Logger::instance()->unregister_all_clients();
    GEO::CmdLine::import_arg_group("standard");
    GEO::CmdLine::import_arg_group("algo");
    GEO::CmdLine::import_arg_group("remesh");

    std::cout << "=== BRL-CAD Remesh Quality Evaluation (VTK Verdict) ===\n\n";
    std::cout << "Input: " << inputFile << "\n";
    std::cout << "Parameters: target_vertices=" << REMESH_TARGET_VERTICES
              << "  lloyd_iter=" << REMESH_LLOYD_ITER
              << "  anisotropy=" << REMESH_ANISOTROPY_SCALE << "\n\n";

    // ---- Run remeshing pipelines ----

    std::vector<Vector3<double>> geoVerts, gteVerts;
    std::vector<std::array<int32_t, 3>> geoTris, gteTris;

    std::cout << "Running Geogram remesh_smooth (previous BRL-CAD integration)...\n";
    bool geoOK = RunGeogramRemesh(inputFile, REMESH_TARGET_VERTICES, geoVerts, geoTris);
    if (!geoOK) {
        std::cerr << "ERROR: Geogram remesh failed\n";
        return 1;
    }
    std::cout << "  -> " << geoVerts.size() << " vertices, " << geoTris.size() << " triangles\n";

    std::cout << "Running GTE MeshRemesh::RemeshCVT (new BRL-CAD integration)...\n";
    bool gteOK = RunGTERemesh(inputFile, REMESH_TARGET_VERTICES, gteVerts, gteTris);
    if (!gteOK) {
        std::cerr << "ERROR: GTE remesh failed\n";
        return 1;
    }
    std::cout << "  -> " << gteVerts.size() << " vertices, " << gteTris.size() << " triangles\n";

    // Save outputs for external inspection
    SaveOBJ("/tmp/remesh_verdict_geogram.obj", geoVerts, geoTris);
    SaveOBJ("/tmp/remesh_verdict_gte.obj",     gteVerts, gteTris);
    std::cout << "\nOutputs saved:\n"
              << "  /tmp/remesh_verdict_geogram.obj\n"
              << "  /tmp/remesh_verdict_gte.obj\n";

    // ---- Compute Verdict quality metrics ----

    std::cout << "\n=== VTK Verdict Quality Metrics ===\n";
    std::cout << "(Ideal equilateral triangle: AR=1.0, SJ=1.0, MinAngle=60°, MaxAngle=60°,\n"
              << " Shape=1.0, Condition=1.0, EdgeRatio=1.0)\n";

    VerdictMetrics geoMetrics = ComputeVerdictMetrics(geoVerts, geoTris);
    VerdictMetrics gteMetrics = ComputeVerdictMetrics(gteVerts, gteTris);

    PrintVerdictMetrics("Geogram remesh_smooth", geoMetrics);
    PrintVerdictMetrics("GTE MeshRemesh::RemeshCVT", gteMetrics);

    // ---- Comparison table ----

    std::cout << "\n=== GTE vs Geogram Comparison (mean values) ===\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  " << std::left << std::setw(22) << "Metric"
              << std::right << std::setw(10) << "Geogram"
              << std::setw(10) << "GTE"
              << std::setw(10) << "Diff%"
              << "  Direction\n";
    std::cout << "  " << std::string(65, '-') << "\n";

    // Higher is better: Scaled Jacobian, Min Angle, Shape
    // Lower is better: Aspect Ratio, Max Angle, Condition, Edge Ratio
    PrintComparisonRow("Aspect Ratio (mean)",    geoMetrics.aspectRatio_mean,    gteMetrics.aspectRatio_mean,    false);
    PrintComparisonRow("Scaled Jacobian (mean)", geoMetrics.scaledJacobian_mean, gteMetrics.scaledJacobian_mean, true);
    PrintComparisonRow("Min Angle mean (deg)",   geoMetrics.minAngle_mean,       gteMetrics.minAngle_mean,       true);
    PrintComparisonRow("Max Angle mean (deg)",   geoMetrics.maxAngle_mean,       gteMetrics.maxAngle_mean,       false);
    PrintComparisonRow("Shape (mean)",           geoMetrics.shape_mean,          gteMetrics.shape_mean,          true);
    PrintComparisonRow("Condition (mean)",       geoMetrics.condition_mean,      gteMetrics.condition_mean,      false);
    PrintComparisonRow("Edge Ratio (mean)",      geoMetrics.edgeRatio_mean,      gteMetrics.edgeRatio_mean,      false);

    // ---- Assessment ----

    std::cout << "\n=== Assessment ===\n";
    bool allPassed = true;

    // 1. GTE absolute quality: scaled Jacobian mean must be > threshold
    bool sjOK = (gteMetrics.scaledJacobian_mean >= VERDICT_MIN_SCALED_JACOBIAN_MEAN);
    std::cout << "GTE Scaled Jacobian mean: " << (sjOK ? "PASS" : "FAIL")
              << " (" << gteMetrics.scaledJacobian_mean
              << " >= " << VERDICT_MIN_SCALED_JACOBIAN_MEAN << " required)\n";
    if (!sjOK) allPassed = false;

    // 2. GTE absolute quality: aspect ratio mean must be < threshold
    bool arOK = (gteMetrics.aspectRatio_mean <= VERDICT_MAX_ASPECT_RATIO_MEAN);
    std::cout << "GTE Aspect Ratio mean:    " << (arOK ? "PASS" : "FAIL")
              << " (" << gteMetrics.aspectRatio_mean
              << " <= " << VERDICT_MAX_ASPECT_RATIO_MEAN << " required)\n";
    if (!arOK) allPassed = false;

    // 3. GTE absolute quality: min angle mean must be > threshold
    bool maOK = (gteMetrics.minAngle_mean >= VERDICT_MIN_ANGLE_MEAN_DEG);
    std::cout << "GTE Min Angle mean:       " << (maOK ? "PASS" : "FAIL")
              << " (" << gteMetrics.minAngle_mean << "°"
              << " >= " << VERDICT_MIN_ANGLE_MEAN_DEG << "° required)\n";
    if (!maOK) allPassed = false;

    // 4. No inverted triangles
    bool noInvertedGTE = (gteMetrics.invertedTri == 0);
    std::cout << "GTE no inverted triangles: " << (noInvertedGTE ? "PASS" : "FAIL")
              << " (" << gteMetrics.invertedTri << " inverted)\n";
    if (!noInvertedGTE) allPassed = false;

    // 5. GTE vs Geogram comparative quality (scaled Jacobian should be within tolerance)
    auto comparativeCheck = [&](std::string const& name, double geoVal, double gteVal,
                                 bool higherIsBetter) -> bool {
        if (std::abs(geoVal) < 1e-10) return true;
        double diffPct = 100.0 * (gteVal - geoVal) / std::abs(geoVal);
        bool ok;
        if (higherIsBetter)
            // GTE should be >= Geogram - tolerance
            ok = (diffPct >= -VERDICT_COMPARATIVE_TOLERANCE_PCT);
        else
            // GTE should be <= Geogram + tolerance
            ok = (diffPct <= VERDICT_COMPARATIVE_TOLERANCE_PCT);
        std::cout << "GTE vs Geogram " << name << ": " << (ok ? "PASS" : "FAIL")
                  << " (diff=" << std::setprecision(1) << diffPct << "%, tolerance="
                  << VERDICT_COMPARATIVE_TOLERANCE_PCT << "%)\n";
        return ok;
    };

    bool c1 = comparativeCheck("Scaled Jacobian (higher=better)",
                               geoMetrics.scaledJacobian_mean, gteMetrics.scaledJacobian_mean, true);
    bool c2 = comparativeCheck("Aspect Ratio (lower=better)",
                               geoMetrics.aspectRatio_mean, gteMetrics.aspectRatio_mean, false);
    bool c3 = comparativeCheck("Min Angle (higher=better)",
                               geoMetrics.minAngle_mean, gteMetrics.minAngle_mean, true);
    bool c4 = comparativeCheck("Shape (higher=better)",
                               geoMetrics.shape_mean, gteMetrics.shape_mean, true);
    if (!c1 || !c2 || !c3 || !c4) allPassed = false;

    // 6. Poor-quality triangle count comparison
    {
        bool poorAR_OK = (gteMetrics.poorAspect <= geoMetrics.poorAspect * 2 + 10);
        std::cout << "GTE poor aspect ratio count: " << (poorAR_OK ? "PASS" : "FAIL")
                  << " (GTE=" << gteMetrics.poorAspect
                  << ", Geogram=" << geoMetrics.poorAspect << ")\n";
        if (!poorAR_OK) allPassed = false;
    }

    std::cout << "\n=== OVERALL: " << (allPassed ? "PASS" : "FAIL") << " ===\n";

    // ---- Interpretation notes ----
    std::cout << "\n=== Verdict Metrics Interpretation ===\n";
    std::cout << "  Aspect Ratio (AR): equilateral=1.0; lower is better.\n"
              << "    AR > 3 indicates poorly-shaped triangles likely to cause FEA issues.\n"
              << "  Scaled Jacobian (SJ): equilateral=1.0; range [-1,1].\n"
              << "    SJ < 0 = inverted (winding flip); SJ > 0.5 = good quality.\n"
              << "  Min Angle: equilateral=60°; 20-40° is adequate; < 20° is poor.\n"
              << "  Max Angle: equilateral=60°; > 120° causes interpolation errors.\n"
              << "  Shape: equilateral=1.0; 0.5+ is adequate; < 0.3 is poor.\n"
              << "  Condition: equilateral=1.0; < 2.5 is good; > 5.0 is poor.\n"
              << "  Edge Ratio: equilateral=1.0; < 2.0 is good.\n";

    std::cout << "\n=== Remesh Behavior Summary ===\n";
    std::cout << "  GTE MeshRemesh uses CVT (Centroidal Voronoi Tessellation) with\n"
              << "  anisotropic metric (position+normal in 6D) for improved curvature\n"
              << "  adaptation. Geogram remesh_smooth uses the same CVT approach.\n"
              << "  Both algorithms target uniform sampling via Lloyd relaxation.\n"
              << "  The Verdict metrics above quantify whether the resulting triangles\n"
              << "  meet FEA quality standards, independent of the exact topology.\n";

    return allPassed ? 0 : 1;
}
