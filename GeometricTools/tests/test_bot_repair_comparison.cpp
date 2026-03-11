// test_bot_repair_comparison.cpp
//
// Simulates BRL-CAD's `bot repair` command on each BoT mesh in GenericTwin
// (gt.obj) using both the new GTE-backed repair path and the old Geogram path.
//
// This test mirrors the logic inside BRL-CAD's rt_bot_repair() (in
// brlcad/src/librt/primitives/bot/repair.cpp), which uses:
//   GTE MeshRepair + MeshHoleFilling -> Manifold validation
//
// And compares it with the old Geogram pipeline:
//   GEO::mesh_repair + GEO::fill_holes -> Manifold check via boundary edges
//
// Repair success criterion (matching BRL-CAD):
//   1. GTE path:     GTE repair + fill holes -> Manifold::Status == NoError
//   2. Geogram path: mesh_repair + fill_holes -> 0 boundary edges remaining
//
// Usage:
//   ./test_bot_repair_comparison <gt.obj>
//
// Outputs per-mesh results and a summary.

#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshPreprocessing.h>
#include <GTE/Mathematics/MeshValidation.h>

// Manifold (from bext_output)
#include <manifold/manifold.h>

// Geogram
#include <geogram/basic/process.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_geometry.h>
#include <geogram/mesh/mesh_repair.h>
#include <geogram/mesh/mesh_fill_holes.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <algorithm>

using namespace gte;

// ---- Mesh data types ----

struct MeshData {
    std::string name;
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
};

// ---- OBJ loading: splits into per-group meshes ----

std::vector<MeshData> LoadOBJByGroup(std::string const& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << filename << "\n";
        return {};
    }

    std::vector<Vector3<double>> allVerts;
    std::vector<MeshData> meshes;
    MeshData current;
    bool inGroup = false;

    // local vertex-index offset for the current group
    int groupVertStart = 0;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "g") {
            if (inGroup && !current.triangles.empty()) {
                meshes.push_back(current);
            }
            current = MeshData{};
            iss >> current.name;
            inGroup = true;
            groupVertStart = (int)allVerts.size();
        } else if (prefix == "v") {
            double x, y, z;
            iss >> x >> y >> z;
            allVerts.push_back(Vector3<double>{x, y, z});
            if (inGroup) {
                current.vertices.push_back(Vector3<double>{x, y, z});
            }
        } else if (prefix == "f") {
            if (!inGroup) continue;
            int32_t v0, v1, v2;
            iss >> v0 >> v1 >> v2;
            // OBJ indices are global 1-based; map to group-local 0-based
            v0 -= (groupVertStart + 1);
            v1 -= (groupVertStart + 1);
            v2 -= (groupVertStart + 1);
            current.triangles.push_back({v0, v1, v2});
        }
    }

    if (inGroup && !current.triangles.empty()) {
        meshes.push_back(current);
    }

    return meshes;
}

// ---- GTE helpers (matching rt_bot_repair logic) ----

static double gte_bbox_diagonal(std::vector<Vector3<double>> const& verts)
{
    if (verts.empty()) return 0.0;
    double minx = verts[0][0], maxx = verts[0][0];
    double miny = verts[0][1], maxy = verts[0][1];
    double minz = verts[0][2], maxz = verts[0][2];
    for (auto const& v : verts) {
        if (v[0] < minx) minx = v[0]; if (v[0] > maxx) maxx = v[0];
        if (v[1] < miny) miny = v[1]; if (v[1] > maxy) maxy = v[1];
        if (v[2] < minz) minz = v[2]; if (v[2] > maxz) maxz = v[2];
    }
    double dx = maxx-minx, dy = maxy-miny, dz = maxz-minz;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

static double gte_mesh_area(std::vector<Vector3<double>> const& verts,
                             std::vector<std::array<int32_t, 3>> const& tris)
{
    double area = 0.0;
    for (auto const& tri : tris) {
        if (tri[0] < 0 || tri[1] < 0 || tri[2] < 0 ||
            tri[0] >= (int32_t)verts.size() ||
            tri[1] >= (int32_t)verts.size() ||
            tri[2] >= (int32_t)verts.size()) continue;
        auto e1 = verts[tri[1]] - verts[tri[0]];
        auto e2 = verts[tri[2]] - verts[tri[0]];
        area += Length(Cross(e1, e2)) * 0.5;
    }
    return area;
}

// Match rt_bot_repair: repair + small component removal
static void gte_preprocess(std::vector<Vector3<double>>& verts,
                            std::vector<std::array<int32_t, 3>>& tris)
{
    double bbox_diag = gte_bbox_diagonal(verts);
    double epsilon = 1e-6 * (0.01 * bbox_diag);
    MeshRepair<double>::Parameters rp;
    rp.epsilon = epsilon;
    MeshRepair<double>::Repair(verts, tris, rp);

    double area = gte_mesh_area(verts, tris);
    double min_comp = 0.03 * area;
    if (min_comp > 0.0) {
        size_t nf = tris.size();
        MeshPreprocessing<double>::RemoveSmallComponents(verts, tris, min_comp);
        if (tris.size() != nf)
            MeshRepair<double>::Repair(verts, tris, rp);
    }
}

// Returns a failure-mode string instead of bool, for diagnosis
enum GteFailMode { GTE_OK=0, GTE_FAIL_EMPTY, GTE_FAIL_AREA_DECREASED,
                   GTE_FAIL_MANIFOLD, GTE_FAIL_PREPROCESS };

static GteFailMode gte_repair_diag(std::vector<Vector3<double>> verts,
                                    std::vector<std::array<int32_t, 3>> tris)
{
    if (verts.empty() || tris.size() < 4) return GTE_FAIL_EMPTY;

    // Step 1: Try Manifold directly (cheap check first)
    {
        manifold::MeshGL64 m64;
        for (auto const& v : verts) {
            m64.vertProperties.push_back((float)v[0]);
            m64.vertProperties.push_back((float)v[1]);
            m64.vertProperties.push_back((float)v[2]);
        }
        for (auto const& t : tris) {
            m64.triVerts.push_back(t[0]);
            m64.triVerts.push_back(t[1]);
            m64.triVerts.push_back(t[2]);
        }
        if (m64.Merge()) {
            manifold::Manifold m(m64);
            if (m.Status() == manifold::Manifold::Error::NoError)
                return GTE_OK;
        }
    }

    // Step 2: GTE preprocess
    gte_preprocess(verts, tris);
    if (tris.empty()) return GTE_FAIL_PREPROCESS;

    // Step 3: Fill holes using LSCM (always succeeds for any simple boundary
    // loop; CDT fails on non-planar holes which are common in aircraft-skin
    // meshes).  Do NOT run MeshRepair after FillHoles — the repair pass removes
    // newly-added fill triangles, re-opening the holes it just filled.
    double area_before = gte_mesh_area(verts, tris);
    MeshHoleFilling<double>::Parameters fp;
    fp.maxArea = 1e30;
    fp.method = MeshHoleFilling<double>::TriangulationMethod::LSCM;
    fp.autoFallback = true;
    MeshHoleFilling<double>::FillHoles(verts, tris, fp);

    // Sanity check: area should not decrease
    double area_after = gte_mesh_area(verts, tris);
    if (area_after < area_before) return GTE_FAIL_AREA_DECREASED;

    // Step 4: Check Manifold accepts the result
    manifold::MeshGL gmm;
    for (auto const& v : verts) {
        gmm.vertProperties.push_back((float)v[0]);
        gmm.vertProperties.push_back((float)v[1]);
        gmm.vertProperties.push_back((float)v[2]);
    }
    for (auto const& t : tris) {
        gmm.triVerts.push_back((uint32_t)t[0]);
        gmm.triVerts.push_back((uint32_t)t[1]);
        gmm.triVerts.push_back((uint32_t)t[2]);
    }
    manifold::Manifold gman(gmm);
    if (gman.Status() != manifold::Manifold::Error::NoError)
        return GTE_FAIL_MANIFOLD;
    return GTE_OK;
}

// Returns true if the GTE+Manifold repair pipeline succeeds (matching rt_bot_repair)
static bool gte_repair(std::vector<Vector3<double>> verts,
                        std::vector<std::array<int32_t, 3>> tris)
{
    return gte_repair_diag(verts, tris) == GTE_OK;
}

// ---- Geogram repair (matching the old BRL-CAD geogram repair.cpp) ----

// Convert GTE mesh data to a GEO::Mesh
static GEO::Mesh* to_geogram_mesh(std::vector<Vector3<double>> const& verts,
                                   std::vector<std::array<int32_t, 3>> const& tris)
{
    GEO::Mesh* gm = new GEO::Mesh(3, false);
    gm->vertices.create_vertices((int)verts.size());
    for (size_t i = 0; i < verts.size(); i++) {
        double* p = gm->vertices.point_ptr((int)i);
        p[0] = verts[i][0];
        p[1] = verts[i][1];
        p[2] = verts[i][2];
    }
    // Only add triangles with valid, non-degenerate face indices
    int nv = (int)verts.size();
    for (size_t i = 0; i < tris.size(); i++) {
        int v0 = tris[i][0], v1 = tris[i][1], v2 = tris[i][2];
        if (v0 < 0 || v1 < 0 || v2 < 0) continue;
        if (v0 >= nv || v1 >= nv || v2 >= nv) continue;
        if (v0 == v1 || v1 == v2 || v0 == v2) continue;  // degenerate
        gm->facets.create_triangle(v0, v1, v2);
    }
    // Don't call connect() here; let mesh_repair handle connectivity
    return gm;
}

// Count boundary edges in a GEO::Mesh
static int geogram_boundary_edges(GEO::Mesh& gm)
{
    int count = 0;
    for (GEO::index_t f = 0; f < gm.facets.nb(); f++) {
        for (GEO::index_t e = 0; e < gm.facets.nb_vertices(f); e++) {
            if (gm.facets.adjacent(f, e) == GEO::NO_FACET)
                count++;
        }
    }
    return count;
}

// Returns true if geogram repair succeeds (0 boundary edges after fill_holes)
static bool geogram_repair(std::vector<Vector3<double>> const& verts,
                             std::vector<std::array<int32_t, 3>> const& tris)
{
    if (verts.empty() || tris.size() < 4) return false;

    GEO::Mesh* gm = to_geogram_mesh(verts, tris);
    if (!gm) return false;

    // Match old BRL-CAD geogram repair path:
    //   1. mesh_repair (colocate + dedup + triangulate)
    //   2. compute_bbox_diag → set epsilon = 1e-6 * 0.01 * diag
    //   3. fill_holes with max_area = 1e30 (fill all holes)
    double diag = GEO::bbox_diagonal(*gm);
    double epsilon = 1e-6 * 0.01 * diag;
    GEO::mesh_repair(*gm, GEO::MESH_REPAIR_DEFAULT, epsilon);
    GEO::fill_holes(*gm, 1e30);

    int boundary = geogram_boundary_edges(*gm);
    delete gm;
    return (boundary == 0);
}

// ---- Check if mesh is already manifold (before repair) ----
static bool is_already_manifold(std::vector<Vector3<double>> const& verts,
                                  std::vector<std::array<int32_t, 3>> const& tris)
{
    if (verts.empty() || tris.empty()) return false;
    auto val = MeshValidation<double>::Validate(verts, tris, false);
    return val.isManifold && val.boundaryEdges == 0;
}

// ---- Main ----

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <gt.obj>\n";
        std::cerr << "  Runs GTE and Geogram repair on each mesh group in gt.obj\n";
        std::cerr << "  and reports how many shapes each backend can successfully repair.\n";
        return 1;
    }

    // Suppress geogram output
    GEO::initialize();
    GEO::CmdLine::import_arg_group("standard");
    GEO::CmdLine::import_arg_group("algo");
    GEO::CmdLine::set_arg("sys:assert", "abort");
    GEO::Logger::instance()->unregister_all_clients();

    std::string gtFile = argv[1];
    std::cout << "Loading meshes from: " << gtFile << "\n";

    std::vector<MeshData> meshes = LoadOBJByGroup(gtFile);
    if (meshes.empty()) {
        std::cerr << "No mesh groups found in " << gtFile << "\n";
        return 1;
    }
    std::cout << "Total mesh groups loaded: " << meshes.size() << "\n\n";

    int total = 0;
    int already_manifold = 0;
    int gte_success = 0;
    int gte_fail = 0;
    int geo_success = 0;
    int geo_fail = 0;
    int gte_only = 0;    // GTE succeeds but Geogram fails
    int geo_only = 0;    // Geogram succeeds but GTE fails
    int both_fail = 0;   // Both fail
    int both_success = 0;

    // GTE failure mode counts
    int fail_empty = 0, fail_area_dec = 0;
    int fail_manifold = 0, fail_preproc = 0;

    // Track meshes where results differ
    std::vector<std::string> gte_only_names;
    std::vector<std::string> geo_only_names;
    std::vector<std::string> both_fail_names;

    for (auto const& mesh : meshes) {
        total++;

        bool manifold = is_already_manifold(mesh.vertices, mesh.triangles);
        if (manifold) {
            already_manifold++;
            gte_success++;
            geo_success++;
            both_success++;
            continue;
        }

        GteFailMode gfm = gte_repair_diag(mesh.vertices, mesh.triangles);
        bool gte_ok = (gfm == GTE_OK);
        bool geo_ok = geogram_repair(mesh.vertices, mesh.triangles);

        if (gte_ok) gte_success++; else gte_fail++;
        if (geo_ok) geo_success++; else geo_fail++;

        if (!gte_ok) {
            switch (gfm) {
                case GTE_FAIL_EMPTY:        fail_empty++;   break;
                case GTE_FAIL_AREA_DECREASED: fail_area_dec++; break;
                case GTE_FAIL_MANIFOLD:     fail_manifold++; break;
                case GTE_FAIL_PREPROCESS:   fail_preproc++; break;
                default: break;
            }
        }

        if (gte_ok && geo_ok) {
            both_success++;
        } else if (gte_ok && !geo_ok) {
            gte_only++;
            gte_only_names.push_back(mesh.name);
        } else if (!gte_ok && geo_ok) {
            geo_only++;
            geo_only_names.push_back(mesh.name);
        } else {
            both_fail++;
            both_fail_names.push_back(mesh.name);
        }
    }

    std::cout << "=== BRL-CAD bot repair simulation: GTE vs Geogram ===\n\n";
    std::cout << "Input: " << gtFile << "\n";
    std::cout << "Total mesh groups:    " << total << "\n";
    std::cout << "Already manifold:     " << already_manifold << "\n";
    std::cout << "Needing repair:       " << (total - already_manifold) << "\n\n";

    std::cout << "--- Repair results ---\n";
    std::cout << std::left << std::setw(28) << "Backend"
              << std::setw(10) << "Success"
              << std::setw(10) << "Fail"
              << std::setw(12) << "Success%"
              << "\n";
    std::cout << std::string(60, '-') << "\n";

    auto pct = [&](int s, int t) -> double {
        return (t > 0) ? 100.0 * s / t : 0.0;
    };

    std::cout << std::left << std::setw(28) << "GTE (new)"
              << std::setw(10) << gte_success
              << std::setw(10) << gte_fail
              << std::fixed << std::setprecision(1)
              << pct(gte_success, total) << "%\n";

    std::cout << std::left << std::setw(28) << "Geogram (previous)"
              << std::setw(10) << geo_success
              << std::setw(10) << geo_fail
              << std::fixed << std::setprecision(1)
              << pct(geo_success, total) << "%\n";

    std::cout << "\n--- Agreement ---\n";
    std::cout << "Both succeed:         " << both_success << "\n";
    std::cout << "GTE only succeeds:    " << gte_only << "\n";
    std::cout << "Geogram only succeeds:" << geo_only << "\n";
    std::cout << "Both fail:            " << both_fail << "\n";

    if (!gte_only_names.empty()) {
        std::cout << "\nShapes repaired by GTE but NOT Geogram (" << gte_only << "):\n";
        for (auto const& n : gte_only_names)
            std::cout << "  GTE-only: " << n << "\n";
    }

    if (!geo_only_names.empty()) {
        std::cout << "\nShapes repaired by Geogram but NOT GTE (" << geo_only << "):\n";
        for (auto const& n : geo_only_names)
            std::cout << "  Geo-only: " << n << "\n";
    }

    if (!both_fail_names.empty()) {
        std::cout << "\nShapes that BOTH fail to repair (" << both_fail << "):\n";
        for (auto const& n : both_fail_names)
            std::cout << "  Both-fail: " << n << "\n";
    }

    std::cout << "\n--- GTE failure mode breakdown (of " << gte_fail << " failures) ---\n";
    std::cout << "  Manifold rejection:    " << fail_manifold   << " (GTE+LSCM+fill didn't satisfy Manifold)\n";
    std::cout << "  Area decreased:        " << fail_area_dec   << " (fill removed geometry)\n";
    std::cout << "  Post-preprocess empty: " << fail_preproc    << " (small-comp removal left nothing)\n";
    std::cout << "  Too small (<4 tris):   " << fail_empty      << "\n";

    std::cout << "\n=== SUMMARY ===\n";

    bool gte_all_success = (gte_fail == 0);
    bool geo_all_success = (geo_fail == 0);
    bool gte_as_good = (gte_success >= geo_success);

    std::cout << "GTE repairs all shapes:      " << (gte_all_success ? "YES" : "NO") << "\n";
    std::cout << "Geogram repairs all shapes:  " << (geo_all_success ? "YES" : "NO") << "\n";
    std::cout << "GTE as good as Geogram:      " << (gte_as_good ? "YES" : "NO")
              << " (GTE=" << gte_success << ", Geo=" << geo_success << ")\n";

    // Overall pass criteria:
    //   1. GTE must repair at least as many shapes in total as Geogram.
    //   2. The number of shapes only Geogram can repair (geo_only) must not
    //      exceed 5% of the total needing repair.  This allows for small
    //      per-shape algorithmic differences (GTE and Geogram use different
    //      triangulation algorithms and may succeed/fail on different
    //      individual meshes) while catching large regressions.
    int needing_repair = total - already_manifold;
    double geo_only_pct = (needing_repair > 0) ? 100.0 * geo_only / needing_repair : 0.0;
    bool overall_pass = gte_as_good && (geo_only_pct <= 5.0);
    std::cout << "Geogram-only repairs as % of needing-repair: "
              << std::fixed << std::setprecision(1) << geo_only_pct << "% (threshold 5%)\n";

    std::cout << "\n=== OVERALL: " << (overall_pass ? "PASS" : "FAIL") << " ===\n";

    if (!overall_pass && !gte_as_good) {
        std::cout << "REASON: GTE repairs fewer shapes overall than Geogram"
                  << " (GTE=" << gte_success << ", Geo=" << geo_success << ")\n";
    }
    if (!overall_pass && geo_only_pct > 5.0) {
        std::cout << "REASON: " << geo_only << " shape(s) (" << geo_only_pct
                  << "%) repaired by Geogram but NOT GTE exceeds 5% threshold.\n";
    }

    return overall_pass ? 0 : 1;
}
